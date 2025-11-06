#include "RandomXManager.h"
#include "MiningCommon.h" // Dla hex_to_bytes i g_cout_mutex
#include <stdexcept>
#include <iostream>
#include <fmt/core.h>

RandomXManager::RandomXManager() {
    // Flagi: JIT, Hard AES (domyślne), Wielkie Strony
    randomx_flags flags = RANDOMX_FLAG_DEFAULT | RANDOMX_FLAG_JIT | RANDOMX_FLAG_LARGE_PAGES | RANDOMX_FLAG_HARD_AES;

    m_cache = randomx_alloc_cache(flags);
    if (!m_cache) {
        throw std::runtime_error("Nie udało się zaalokować RandomX Cache (256MB)");
    }
}

RandomXManager::~RandomXManager() {
    // Ważna kolejność: najpierw dataset, potem cache
    if (m_dataset) {
        randomx_release_dataset(m_dataset);
    }
    if (m_cache) {
        randomx_release_cache(m_cache);
    }
}

bool RandomXManager::updateSeed(const std::string& seed_hash_hex) {
    // Blokujemy mutex na cały czas trwania aktualizacji
    std::lock_guard<std::mutex> lock(m_mutex);

    if (seed_hash_hex == m_current_seed_hex) {
        return false; // Seed jest ten sam, brak zmian
    }

    {
        std::lock_guard<std::mutex> cout_lock(g_cout_mutex);
        std::cout << "[RandomXManager] Wykryto nowy seed. Rozpoczynam aktualizację...\n";
    }

    auto seed_bytes = hex_to_bytes(seed_hash_hex);
    if (seed_bytes.size() != 32) {
        {
            std::lock_guard<std::mutex> cout_lock(g_cout_mutex);
            std::cerr << "[RandomXManager] Błąd: Seed ma nieprawidłową długość.\n";
        }
        return false;
    }

    // 1. Inicjalizuj cache nowym seedem
    randomx_init_cache(m_cache, seed_bytes.data(), seed_bytes.size());

    // 2. Zniszcz stary dataset, jeśli istnieje
    if (m_dataset) {
        randomx_release_dataset(m_dataset);
        m_dataset = nullptr;
    }

    // 3. Alokuj nowy dataset (z flagą Large Pages)
    m_dataset = randomx_alloc_dataset(RANDOMX_FLAG_LARGE_PAGES);
    if (!m_dataset) {
        {
            std::lock_guard<std::mutex> cout_lock(g_cout_mutex);
            std::cerr << "[RandomXManager] KRYTYCZNY BŁĄD: Nie udało się zaalokować Datasetu (2GB)!\n";
            std::cerr << "[RandomXManager] Upewnij się, że masz wystarczająco RAM i uprawnienia do 'Large Pages'.\n";
        }
        // Wątki robocze będą musiały poczekać na następny seed
        return false;
    }

    // 4. Inicjalizuj dataset (TO JEST WOLNA OPERACJA - kilka sekund)
    unsigned int num_threads = 0; // 0 = auto-detect
    unsigned int dataset_item_count = randomx_dataset_item_count();

    {
        std::lock_guard<std::mutex> cout_lock(g_cout_mutex);
        std::cout << "[RandomXManager] Inicjalizuję 2GB Dataset... (to potrwa kilka sekund)\n";
    }

    // Używamy 0, aby pozwolić bibliotece zrównoleglić tę operację
    randomx_init_dataset(m_dataset, m_cache, 0, dataset_item_count);

    {
        std::lock_guard<std::mutex> cout_lock(g_cout_mutex);
        std::cout << "[RandomXManager] Inicjalizacja Datasetu zakończona.\n";
    }

    m_current_seed_hex = seed_hash_hex;
    return true;
}

std::tuple<randomx_cache*, randomx_dataset*> RandomXManager::get_pointers() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return {m_cache, m_dataset};
}

std::string RandomXManager::get_current_seed() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_current_seed_hex;
}