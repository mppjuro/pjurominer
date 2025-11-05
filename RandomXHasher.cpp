#include "RandomXHasher.h"
#include "MiningCommon.h" // Dla hex_to_bytes, bytes_to_hex i g_cout_mutex
#include <stdexcept>
#include <iostream>
#include <cstring> // Dla std::memcpy
#include <fmt/core.h>
#include <thread>

RandomXHasher::RandomXHasher() {
    m_cache = randomx_alloc_cache(RANDOMX_FLAG_DEFAULT | RANDOMX_FLAG_JIT);
    if (!m_cache) {
        throw std::runtime_error("Nie udało się zaalokować RandomX Cache");
    }
}

RandomXHasher::~RandomXHasher() {
    if (m_vm) {
        randomx_destroy_vm(m_vm);
    }
    if (m_cache) {
        randomx_release_cache(m_cache);
    }
}

void RandomXHasher::updateSeed(const std::string& seed_hash_hex) {
    if (seed_hash_hex == m_current_seed_hex) {
        return;
    }

    if (seed_hash_hex.empty()) {
        // --- ZMIANA: Dodano blokadę ---
        {
            std::lock_guard<std::mutex> lock(g_cout_mutex);
            std::cerr << "[Hasher] Błąd: Otrzymano pusty seed_hash.\n";
        }
        return;
    }

    // --- ZMIANA: Dodano blokadę ---
    {
        std::lock_guard<std::mutex> lock(g_cout_mutex);
        std::cout << "[Hasher wątek: " << std::this_thread::get_id() << "] Aktualizuję seed RandomX...\n";
    }

    auto seed_bytes = hex_to_bytes(seed_hash_hex);

    if (seed_bytes.size() != 32) {
        // --- ZMIANA: Dodano blokadę ---
        {
            std::lock_guard<std::mutex> lock(g_cout_mutex);
            std::cerr << "[Hasher wątek: " << std::this_thread::get_id() << "] Błąd: seed_hash ma nieprawidłową długość.\n";
        }
        return;
    }

    randomx_init_cache(m_cache, seed_bytes.data(), seed_bytes.size());

    if (m_vm) {
        randomx_destroy_vm(m_vm);
    }

    m_vm = randomx_create_vm(RANDOMX_FLAG_DEFAULT | RANDOMX_FLAG_JIT, m_cache, nullptr);
    if (!m_vm) {
        throw std::runtime_error("Nie udało się odtworzyć VM po zmianie seeda");
    }

    m_current_seed_hex = seed_hash_hex;
    // --- ZMIANA: Dodano blokadę ---
    {
        std::lock_guard<std::mutex> lock(g_cout_mutex);
        std::cout << "[Hasher wątek: " << std::this_thread::get_id() << "] Seed zaktualizowany na : "<< m_current_seed_hex <<"\n";
    }
}

std::string RandomXHasher::hash(const std::string& blob_hex, uint32_t nonce) {
    if (!m_vm) {
        return "0000000000000000000000000000000000000000000000000000000000000000";
    }

    auto blob_bytes = hex_to_bytes(blob_hex);

    if (blob_bytes.size() < 43) {
        return "0000000000000000000000000000000000000000000000000000000000000000";
    }
    std::memcpy(blob_bytes.data() + 39, &nonce, sizeof(uint32_t));

    uint8_t hash_result_bytes[RANDOMX_HASH_SIZE];

    randomx_calculate_hash(m_vm, blob_bytes.data(), blob_bytes.size(), hash_result_bytes);

    return bytes_to_hex(hash_result_bytes, RANDOMX_HASH_SIZE);
}