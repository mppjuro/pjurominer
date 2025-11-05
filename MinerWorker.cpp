#include "MinerWorker.h"
#include <iostream>
#include <fmt/core.h>
#include "RandomXHasher.h"
#include "MiningCommon.h"

/**
 * @brief Konstruktor. Inicjalizuje ID i callback.
 */
MinerWorker::MinerWorker(int id, SolutionCallback callback)
        : m_id(id),
          m_solution_callback(std::move(callback)) {
}

/**
 * @brief Destruktor.
 */
MinerWorker::~MinerWorker() {
    stop();
}

/**
 * @brief Tworzy i uruchamia std::jthread.
 */
void MinerWorker::start() {
    m_thread = std::jthread([this](std::stop_token st){ this->run(st); });
    {
        std::lock_guard<std::mutex> lock(g_cout_mutex);
        std::cout << fmt::format("[Worker {}] Uruchomiony.\n", m_id);
    }
}

/**
 * @brief Prosi wątek o zatrzymanie.
 */
void MinerWorker::stop() {
    m_thread.request_stop();
}

/**
 * @brief Bezpiecznie wątkowo ustawia nową pracę.
 */
void MinerWorker::setNewJob(const MiningJob& job) {
    std::lock_guard<std::mutex> lock(m_job_mutex);
    m_current_job = job;
}

/**
 * @brief Pobiera całkowitą liczbę wykonanych hashów przez ten wątek.
 */
uint64_t MinerWorker::getHashCount() const {
    return m_hash_count.load();
}

/**
 * @brief Główna pętla robocza wątku.
 */
void MinerWorker::run(std::stop_token stoken) {
    uint32_t nonce = (rand() % 10000) * m_id;
    std::optional<MiningJob> local_job;

    RandomXHasher hasher;
    std::string current_seed_hex;


    while (!stoken.stop_requested()) {

        {
            std::lock_guard<std::mutex> lock(m_job_mutex);
            if (m_current_job) {
                local_job = m_current_job;
                m_current_job.reset();
                nonce = 0;

                //{
                //    std::lock_guard<std::mutex> cout_lock(g_cout_mutex);
                //    std::cout << fmt::format("[Worker {}] Rozpoczynam pracę nad {}\n", m_id, local_job->job_id);
                //}
            }
        }

        if (!local_job) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (local_job->seed_hash != current_seed_hex) {
            try {
                hasher.updateSeed(local_job->seed_hash);
                current_seed_hex = local_job->seed_hash;
            } catch (const std::exception& e) {
                {
                    std::lock_guard<std::mutex> lock(g_cout_mutex);
                    std::cerr << fmt::format("[Worker {}] Krytyczny błąd Hashera: {}\n", m_id, e.what());
                }
                break;
            }
        }

        std::string hash_result_hex = hasher.hash(local_job->blob, nonce);

        m_hash_count++;

        if (check_hash_target_real(hash_result_hex, local_job->target)) {

            // --- ZMIANA: Zbuduj string PRZED blokadą ---
            std::string solution_report = fmt::format("\n!!! [Worker {}] ZNALAZŁEM ROZWIĄZANIE !!!\n", m_id);
            solution_report += fmt::format("    Job:  {}\n", local_job->job_id);
            solution_report += fmt::format("    Nonce: {}\n", nonce);
            solution_report += fmt::format("    Hash: {}\n\n", hash_result_hex);
            // --- Koniec budowania stringa ---

            {
                std::lock_guard<std::mutex> lock(g_cout_mutex);
                std::cout << solution_report; // Wypisz gotowy string
                std::cout.flush();
            }

            Solution sol = {
                    local_job->job_id,
                    nonce,
                    hash_result_hex
            };

            m_solution_callback(sol);
            local_job.reset();
        }

        nonce++;

        if (nonce % 256 == 0) {
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