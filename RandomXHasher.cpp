#include "RandomXHasher.h"
#include "MiningCommon.h" // Dla hex_to_bytes i bytes_to_hex
#include <stdexcept>
#include <iostream>
#include <cstring> // Dla std::memcpy

RandomXHasher::RandomXHasher() {
    // Inicjalizujemy Cache
    // Używamy RANDOMX_FLAG_JIT dla kompilacji JIT (szybsze)
    m_cache = randomx_alloc_cache(RANDOMX_FLAG_DEFAULT | RANDOMX_FLAG_JIT);
    if (!m_cache) {
        throw std::runtime_error("Nie udało się zaalokować RandomX Cache");
    }

    // Inicjalizujemy VM w trybie "light" (używa tylko Cache)
    // RANDOMX_FLAG_LIGHT | RANDOMX_FLAG_JIT | RANDOMX_FLAG_HARD_AES
    m_vm = randomx_create_vm(RANDOMX_FLAG_DEFAULT | RANDOMX_FLAG_JIT, m_cache, nullptr);
    if (!m_vm) {
        throw std::runtime_error("Nie udało się stworzyć RandomX VM");
    }
}

RandomXHasher::~RandomXHasher() {
    // Zwalniamy zasoby w odwrotnej kolejności
    if (m_vm) {
        randomx_destroy_vm(m_vm);
    }
    if (m_cache) {
        randomx_release_cache(m_cache);
    }
}

void RandomXHasher::updateSeed(const std::string& seed_hash_hex) {
    // Nie robimy nic, jeśli seed jest ten sam
    if (seed_hash_hex == m_current_seed_hex) {
        return;
    }

    std::cout << "[Hasher] Aktualizuję seed RandomX..." << std::endl;
    auto seed_bytes = hex_to_bytes(seed_hash_hex);

    // Inicjalizujemy Cache nowym seedem
    // To jest wolna operacja (ok. 1-2 sekund)
    randomx_init_cache(m_cache, seed_bytes.data(), seed_bytes.size());

    // Musimy "przeładować" VM nowym Cache
    // (W trybie light, VM musi być po prostu poinformowany o zmianie cache)
    // Najprościej jest go odtworzyć
    randomx_destroy_vm(m_vm);
    m_vm = randomx_create_vm(RANDOMX_FLAG_DEFAULT | RANDOMX_FLAG_JIT, m_cache, nullptr);
    if (!m_vm) {
        throw std::runtime_error("Nie udało się odtworzyć VM po zmianie seeda");
    }
    
    m_current_seed_hex = seed_hash_hex;
    std::cout << "[Hasher] Seed zaktualizowany.\n";
}

std::string RandomXHasher::hash(const std::string& blob_hex, uint32_t nonce) {
    auto blob_bytes = hex_to_bytes(blob_hex);

    // --- KRYTYCZNY KROK: WSTRZYKNIĘCIE NONCE ---
    // W protokole Monero, pula rezerwuje 4 bajty na nonce.
    // Zazwyczaj jest to na pozycji 39.
    if (blob_bytes.size() < 43) { // 39 + 4
        throw std::runtime_error("Blob jest za krótki, by wstrzyknąć nonce");
    }
    // Kopiujemy 4 bajty 'nonce' do bloba
    std::memcpy(blob_bytes.data() + 39, &nonce, sizeof(uint32_t));
    // --- KONIEC WSTRZYKIWANIA ---

    // Tablica na wynikowy hash
    uint8_t hash_result_bytes[RANDOMX_HASH_SIZE];

    // Haszujemy!
    randomx_calculate_hash(m_vm, blob_bytes.data(), blob_bytes.size(), hash_result_bytes);

    // Konwertujemy wynik z powrotem na hex
    return bytes_to_hex(hash_result_bytes, RANDOMX_HASH_SIZE);
}