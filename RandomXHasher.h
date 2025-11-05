#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "randomx.h" // Nagłówek z libRandomX

/**
 * @class RandomXHasher
 * @brief Klasa opakowująca (wrapper) dla JEDNEJ instancji RandomX VM.
 * Ta klasa jest lekka i przeznaczona do użytku w jednym wątku.
 * NIE posiada już cache'a ani datasetu - korzysta ze współdzielonych.
 */
class RandomXHasher {
public:
    /**
     * @brief Konstruktor.
     */
    RandomXHasher();

    /**
     * @brief Destruktor. Zwalnia VM.
     */
    ~RandomXHasher();

    // Usuwamy konstruktory kopiujące
    RandomXHasher(const RandomXHasher&) = delete;
    RandomXHasher& operator=(const RandomXHasher&) = delete;

    /**
     * @brief Tworzy (lub odtwarza) maszynę wirtualną (VM).
     * @param cache Wskaźnik do współdzielonego cache'a.
     * @param dataset Wskaźnik do współdzielonego datasetu (Tryb Szybki).
     */
    void create_vm(randomx_cache* cache, randomx_dataset* dataset);

    /**
     * @brief Haszuje blob przy użyciu danego nonce.
     * @param blob_hex Dane bloku (76 bajtów lub więcej) w formacie hex.
     * @param nonce Nonce do wstrzyknięcia.
     * @return 32-bajtowy hash w formacie hex.
     */
    std::string hash(const std::string& blob_hex, uint32_t nonce);

private:
    randomx_vm* m_vm = nullptr;     // Wskaźnik na maszynę wirtualną RandomX
};