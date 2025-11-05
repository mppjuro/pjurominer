#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <mutex> // <-- DODANO

/**
 * @struct MiningJob
 * @brief Przechowuje informacje o pracy z puli.
 * --- ZMIANA: Dodano seed_hash ---
 */
struct MiningJob {
    std::string job_id;
    std::string blob;
    std::string target;
    std::string seed_hash; // Niezbędny do inicjalizacji RandomX Cache
};

/**
 * @struct Solution
 * @brief Przechowuje znalezione rozwiązanie (bez zmian).
 */
struct Solution {
    std::string job_id;
    uint32_t nonce;
    std::string result_hash; // Hash w formacie hex
};

// --- NOWA SEKCJA ---
// Globalny mutex chroniący std::cout i std::cerr
// Zadeklarowany tutaj, zdefiniowany w MiningCommon.cpp
extern std::mutex g_cout_mutex;
// --- KONIEC NOWEJ SEKCJI ---


// --- NOWE FUNKCJE POMOCNICZE ---

/**
 * @brief Konwertuje string heksadecymalny na wektor bajtów.
 * @param hex String hex (np. "414243").
 * @return Wektor bajtów (np. {0x41, 0x42, 0x43}).
 */
std::vector<uint8_t> hex_to_bytes(const std::string& hex);

/**
 * @brief Konwertuje wektor (lub tablicę) bajtów na string heksadecymalny.
 * @param bytes Wskaźnik na dane.
 * @param size Liczba bajtów.
 * @return String hex.
 */
std::string bytes_to_hex(const uint8_t* bytes, size_t size);

/**
 * @brief Prawdziwa weryfikacja hasha (256-bit) względem celu (target).
 * @param hash_hex Obliczony hash (32 bajty hex).
 * @param target_hex Cel trudności z puli (32 bajty hex).
 * @return true, jeśli hash <= target, false w przeciwnym razie.
 */
bool check_hash_target_real(const std::string& hash_hex, const std::string& target_hex);