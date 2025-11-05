#pragma once

#include "randomx.h"
#include <string>
#include <mutex>
#include <tuple>
#include <memory>

/**
 * @class RandomXManager
 * @brief Zarządza globalnym, współdzielonym stanem RandomX (Cache i Dataset).
 * Ta klasa jest thread-safe.
 */
class RandomXManager {
public:
    /**
     * @brief Konstruktor. Alokuje wstępny cache.
     */
    RandomXManager();

    /**
     * @brief Destruktor. Zwalnia cache i dataset.
     */
    ~RandomXManager();

    // Usuwamy możliwość kopiowania
    RandomXManager(const RandomXManager&) = delete;
    RandomXManager& operator=(const RandomXManager&) = delete;

    /**
     * @brief Aktualizuje seed, jeśli jest nowy.
     * To jest wolna, blokująca operacja, która przebudowuje 2GB datasetu.
     * @param seed_hash_hex Nowy seed z puli.
     * @return true, jeśli seed był nowy i dataset został przebudowany.
     */
    bool updateSeed(const std::string& seed_hash_hex);

    /**
     * @brief Zwraca wskaźniki do aktualnego cache'a i datasetu.
     * @return Para wskaźników {cache, dataset}.
     */
    std::tuple<randomx_cache*, randomx_dataset*> get_pointers();

    /**
     * @brief Zwraca aktualnie używany seed.
     */
    std::string get_current_seed();

private:
    randomx_cache* m_cache = nullptr;
    randomx_dataset* m_dataset = nullptr;
    std::string m_current_seed_hex;

    // Mutex chroniący dostęp do wszystkich zasobów (cache, dataset, seed)
    std::mutex m_mutex;
};