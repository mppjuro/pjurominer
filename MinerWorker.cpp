#include "MinerWorker.h"
#include <iostream>
#include <format>
#include "RandomXHasher.h"
#include "MiningCommon.h"
/**
 * @brief Konstruktor. Inicjalizuje ID i callback.
 */
MinerWorker::MinerWorker(int id, SolutionCallback callback)
    : m_id(id),
      m_solution_callback(std::move(callback)) {
    // m_thread jest domyślnie konstruowany (bez uruchamiania)
}

/**
 * @brief Destruktor.
 * Jeśli wątek wciąż działa, wywołanie stop() jest dobrą praktyką.
 * Nawet bez tego, destruktor std::jthread (m_thread) sam
 * wywołałby request_stop() i join().
 */
MinerWorker::~MinerWorker() {
    stop();
}

/**
 * @brief Tworzy i uruchamia std::jthread, przekazując mu funkcję run
 * oraz (niejawnie) stop_token.
 */
void MinerWorker::start() {
    // Uruchamiamy jthread, przekazując mu funkcję 'run' jako lambdę
    // lub (jak tutaj) wskaźnik do metody klasy.
    // std::jthread automatycznie przekaże stop_token do funkcji 'run'.
    m_thread = std::jthread([this](std::stop_token st){ this->run(st); });
    std::cout << std::format("[Worker {}] Uruchomiony.\n", m_id);
}

/**
 * @brief Prosi wątek o zatrzymanie.
 */
void MinerWorker::stop() {
    // Wysyła żądanie zatrzymania. Wątek zakończy się, gdy
    // sprawdzi token w pętli 'run'.
    m_thread.request_stop();
}

/**
 * @brief Bezpiecznie wątkowo ustawia nową pracę.
 */
void MinerWorker::setNewJob(const MiningJob& job) {
    // Blokujemy mutex, aby bezpiecznie zaktualizować m_current_job
    std::lock_guard<std::mutex> lock(m_job_mutex);
    m_current_job = job; // Zastępujemy starą pracę nową
    
    // Logowanie (opcjonalne, ale przydatne)
    // std::cout << std::format("[Worker {}] Otrzymałem nową pracę: {}\n", m_id, job.job_id);
}

/**
 * @brief Główna pętla robocza wątku.
 */
void MinerWorker::run(std::stop_token stoken) {
    uint32_t nonce = (rand() % 10000) * m_id;
    std::optional<MiningJob> local_job;

    // --- NOWA SEKCJA ---
    // Każdy wątek tworzy własną instancję Hashera (własne VM i Cache).
    // To jest wymagane dla bezpieczeństwa wątkowego w libRandomX.
    RandomXHasher hasher;
    std::string current_seed_hex; // Śledzimy bieżący seed
    // --- KONIEC NOWEJ SEKCJI ---


    while (!stoken.stop_requested()) {

        {
            std::lock_guard<std::mutex> lock(m_job_mutex);
            if (m_current_job) {
                local_job = m_current_job;
                m_current_job.reset();
                nonce = 0;
            }
        }

        if (!local_job) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // --- AKTUALIZACJA SERCA MINERA ---

        // Krok 1: Sprawdź, czy seed się zmienił. Jeśli tak, zaktualizuj hasher.
        // To jest wolna operacja, ale zdarza się rzadko (co kilka minut).
        if (local_job->seed_hash != current_seed_hex) {
            try {
                hasher.updateSeed(local_job->seed_hash);
                current_seed_hex = local_job->seed_hash;
            } catch (const std::exception& e) {
                std::cerr << std::format("[Worker {}] Krytyczny błąd Hashera: {}\n", m_id, e.what());
                // W prawdziwym minerze, powinniśmy zatrzymać ten wątek
                break; // Zatrzymaj pętlę
            }
        }

        // Krok 2: Wykonaj prawdziwe haszowanie
        std::string hash_result_hex = hasher.hash(local_job->blob, nonce);

        // Krok 3: Wykonaj prawdziwą weryfikację hasha
        if (check_hash_target_real(hash_result_hex, local_job->target)) {
            std::cout << std::format("\n!!! [Worker {}] ZNALAZŁEM ROZWIĄZANIE !!!\n", m_id);
            std::cout << std::format("    Hash: {}\n\n", hash_result_hex);

            Solution sol = {
                local_job->job_id,
                nonce,
                hash_result_hex // Wysyłamy prawdziwy hash
            };

            m_solution_callback(sol);
            // Uwaga: W prawdziwym minerze powinniśmy tu poczekać na nową pracę,
            // aby nie wysyłać wielu rozwiązań dla tego samego bloku.
            // Dla prostoty, po prostu kontynuujemy.
        }

        // --- KONIEC SERCA MINERA ---

        nonce++;

        if (nonce % 256 == 0) { // Możemy sprawdzać rzadziej niż co 1024
            if (stoken.stop_requested()) {
                break;
            }
        }
    }

    std::cout << std::format("[Worker {}] Zatrzymany.\n", m_id);
}