#include "MinerWorker.h"
#include <iostream>
#include <fmt/core.h>
#include "MiningCommon.h"
// RandomXHasher jest już w nagłówku

/**
 * @brief Konstruktor.
 */
MinerWorker::MinerWorker(int id, SolutionCallback callback, std::shared_ptr<RandomXManager> manager)
        : m_id(id),
          m_solution_callback(std::move(callback)),
          m_rx_manager(std::move(manager)) {
    // m_hasher jest tworzony domyślnie (pusty)
}

/**
 * @brief Destruktor.
 */
MinerWorker::~MinerWorker() {
    stop();
}

void MinerWorker::start() {
    m_thread = std::jthread([this](std::stop_token st){ this->run(st); });
    {
        std::lock_guard<std::mutex> lock(g_cout_mutex);
        std::cout << fmt::format("[Worker {}] Uruchomiony.\n", m_id);
    }
}

void MinerWorker::stop() {
    m_thread.request_stop();
}

void MinerWorker::setNewJob(const MiningJob& job) {
    std::lock_guard<std::mutex> lock(m_job_mutex);
    m_current_job = job;
}

uint64_t MinerWorker::getHashCount() const {
    return m_hash_count.load();
}

/**
 * @brief Główna pętla robocza wątku.
 */
void MinerWorker::run(std::stop_token stoken) {
    uint32_t nonce = (rand() % 10000) * m_id;
    std::optional<MiningJob> local_job;

    // m_hasher (RandomXHasher) jest teraz członkiem klasy
    // m_current_seed_hex jest teraz członkiem klasy

    while (!stoken.stop_requested()) {

        {
            std::lock_guard<std::mutex> lock(m_job_mutex);
            if (m_current_job) {
                local_job = m_current_job;
                m_current_job.reset();
                nonce = 0; // Resetuj nonce dla nowej pracy
            }
        }

        if (!local_job) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // --- KLUCZOWA ZMIANA: Sprawdzanie i aktualizacja VM ---
        if (local_job->seed_hash != m_current_seed_hex) {
            try {
                // Seed się zmienił, musimy odtworzyć naszą lokalną VM
                // Pobieramy wskaźniki do globalnego, gotowego cache'a i datasetu
                auto [cache_ptr, dataset_ptr] = m_rx_manager->get_pointers();

                if (dataset_ptr) { // Sprawdzamy, czy dataset jest gotowy
                    m_hasher.create_vm(cache_ptr, dataset_ptr);
                    m_current_seed_hex = local_job->seed_hash;
                    {
                        std::lock_guard<std::mutex> lock(g_cout_mutex);
                        std::cout << fmt::format("[Worker {}] Zaktualizowano VM do seeda ...{}\n", m_id, m_current_seed_hex.substr(m_current_seed_hex.length() - 6));
                    }
                } else {
                    // Manager jeszcze nie skończył budować datasetu. Czekamy.
                    local_job.reset(); // Porzucamy pracę i czekamy
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    continue;
                }
            } catch (const std::exception& e) {
                {
                    std::lock_guard<std::mutex> lock(g_cout_mutex);
                    std::cerr << fmt::format("[Worker {}] Krytyczny błąd Hashera (VM): {}\n", m_id, e.what());
                }
                local_job.reset(); // Nie możemy pracować
                std::this_thread::sleep_for(std::chrono::seconds(5));
                continue;
            }
        }
        // --- KONIEC ZMIANY ---

        std::string hash_result_hex = m_hasher.hash(local_job->blob, nonce);

        m_hash_count++;

        if (check_hash_target_real(hash_result_hex, local_job->target)) {
            std::string solution_report = fmt::format("\n!!! [Worker {}] ZNALAZŁEM ROZWIĄZANIE !!!\n", m_id);
            solution_report += fmt::format("    Job:  {}\n", local_job->job_id);
            solution_report += fmt::format("    Nonce: {}\n", nonce);
            solution_report += fmt::format("    Hash: {}\n\n", hash_result_hex);

            {
                std::lock_guard<std::mutex> lock(g_cout_mutex);
                std::cout << solution_report;
                std::cout.flush();
            }

            Solution sol = {
                    local_job->job_id,
                    nonce,
                    hash_result_hex
            };

            m_solution_callback(sol);
            local_job.reset(); // Znaleziono, czekamy na nową pracę
        }

        nonce++;

        // Szybkie sprawdzanie zatrzymania, aby nie blokować pętli
        if (nonce % 1024 == 0) { // Zwiększono z 256
            if (stoken.stop_requested()) {
                break;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_cout_mutex);
        std::cout << fmt::format("[Worker {}] Zatrzymany.\n", m_id);
    }
}