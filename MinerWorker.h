#pragma once

#include "MiningCommon.h"
#include "RandomXHasher.h" // Zmodyfikowany hasher
#include "RandomXManager.h" // Nowy manager
#include <thread>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <atomic>
#include <memory> // Dla std::shared_ptr

/**
 * @class MinerWorker
 * @brief Reprezentuje pojedynczy wątek roboczy (worker) wykonujący obliczenia.
 */
class MinerWorker {
public:
    using SolutionCallback = std::function<void(const Solution&)>;

    /**
     * @brief Konstruktor.
     * @param id Unikalny identyfikator tego workera.
     * @param callback Funkcja zwrotna do wysyłania znalezionych rozwiązań.
     * @param manager Wskaźnik do współdzielonego managera RandomX.
     */
    MinerWorker(int id, SolutionCallback callback, std::shared_ptr<RandomXManager> manager);

    /**
     * @brief Destruktor.
     */
    ~MinerWorker();

    void start();
    void stop();
    void setNewJob(const MiningJob& job);
    uint64_t getHashCount() const;

private:
    void run(std::stop_token stoken);

    int m_id;
    std::jthread m_thread;
    SolutionCallback m_solution_callback;

    // Mutex chroniący dostęp do m_current_job
    std::mutex m_job_mutex;
    std::optional<MiningJob> m_current_job;
    std::atomic<uint64_t> m_hash_count{0};

    // --- NOWA ARCHITEKTURA ---
    std::shared_ptr<RandomXManager> m_rx_manager; // Wskaźnik do managera
    RandomXHasher m_hasher;                       // Lokalny wrapper VM
    std::string m_current_seed_hex;             // Seed, na którym pracuje ten worker
    // --- KONIEC NOWEJ SEKCJI ---
};