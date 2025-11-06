#include "RandomXHasher.h"
#include "MiningCommon.h" // Dla hex_to_bytes, bytes_to_hex i g_cout_mutex
#include <stdexcept>
#include <iostream>
#include <cstring> // Dla std::memcpy
#include <fmt/core.h>
#include <thread>

RandomXHasher::RandomXHasher() : m_vm(nullptr) {
    // Konstruktor jest teraz pusty
}

RandomXHasher::~RandomXHasher() {
    if (m_vm) {
        randomx_destroy_vm(m_vm);
    }
}

void RandomXHasher::create_vm(randomx_cache* cache, randomx_dataset* dataset) {
    // 1. Zniszcz starą VM, jeśli istnieje
    if (m_vm) {
        randomx_destroy_vm(m_vm);
        m_vm = nullptr;
    }

    if (!cache || !dataset) {
        {
            std::lock_guard<std::mutex> lock(g_cout_mutex);
            std::cerr << "[Hasher] Błąd: Próba utworzenia VM z pustym cache lub datasetem.\n";
        }
        return;
    }

    // 2. Ustaw flagi dla VM (Tryb Szybki, JIT, Large Pages)
    // RANDOMX_FLAG_HARD_AES jest domyślnie włączone, jeśli CPU wspiera
    randomx_flags vm_flags = RANDOMX_FLAG_DEFAULT | RANDOMX_FLAG_JIT | RANDOMX_FLAG_FULL_MEM | RANDOMX_FLAG_LARGE_PAGES | RANDOMX_FLAG_HARD_AES;

    // 3. Stwórz nową VM
    m_vm = randomx_create_vm(vm_flags, cache, dataset);
    if (!m_vm) {
        // To może się zdarzyć, jeśli np. Large Pages zawiodą
        {
            std::lock_guard<std::mutex> lock(g_cout_mutex);
            std::cerr << "[Hasher] KRYTYCZNY BŁĄD: Nie udało się utworzyć RandomX VM!\n";
            std::cerr << "[Hasher] Sprawdź uprawnienia 'Large Pages' (secpol.msc -> Lock pages in memory).\n";
        }
        throw std::runtime_error("Nie udało się utworzyć RandomX VM");
    }
}


std::string RandomXHasher::hash(const std::string& blob_hex, uint32_t nonce) {
    const static std::string ZEROHASH = "0000000000000000000000000000000000000000000000000000000000000000";

    if (!m_vm) {
        // VM nie jest gotowa (np. dataset się jeszcze nie zbudował)
        return ZEROHASH;
    }

    auto blob_bytes = hex_to_bytes(blob_hex);

    if (blob_bytes.size() < 43) {
        return ZEROHASH;
    }

    // Wstrzyknięcie Nonce
    std::memcpy(blob_bytes.data() + 39, &nonce, sizeof(uint32_t));

    uint8_t hash_result_bytes[RANDOMX_HASH_SIZE];

    // Obliczanie hasha (teraz bardzo szybkie)
    randomx_calculate_hash(m_vm, blob_bytes.data(), blob_bytes.size(), hash_result_bytes);

    return bytes_to_hex(hash_result_bytes, RANDOMX_HASH_SIZE);
}