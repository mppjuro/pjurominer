#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "randomx.h" // Nagłówek z libRandomX

/**
 * @class RandomXHasher
 * @brief Klasa opakowująca (wrapper) dla libRandomX.
 * Zarządza stanem Cache i VM dla jednego wątku w trybie "light".
 */
class RandomXHasher {
public:
    /**
     * @brief Konstruktor. Alokuje Cache i VM.
     */
    RandomXHasher();

    /**
     * @brief Destruktor. Zwalnia zasoby RandomX.
     */
    ~RandomXHasher();

    // Usuwamy konstruktory kopiujące, aby uniknąć problemów z zasobami
    RandomXHasher(const RandomXHasher&) = delete;
    RandomXHasher& operator=(const RandomXHasher&) = delete;

    /**
     * @brief Aktualizuje "seed" dla Cache, jeśli jest nowy.
     * To jest wolna operacja i powinna być wywoływana tylko przy zmianie seeda.
     * @param seed_hash_hex Nowy seed z puli (32 bajty hex).
     */
    void updateSeed(const std::string& seed_hash_hex);

    /**
     * @brief Haszuje blob przy użyciu danego nonce.
     * @param blob_hex Dane bloku (76 bajtów lub więcej) w formacie hex.
     * @param nonce Nonce do wstrzyknięcia.
     * @return 32-bajtowy hash w formacie hex.
     */
    std::string hash(const std::string& blob_hex, uint32_t nonce);

private:
    std::string m_current_seed_hex; // Przechowuje ostatnio użyty seed
    randomx_cache* m_cache = nullptr; // Wskaźnik na RandomX Cache
    randomx_vm* m_vm = nullptr;     // Wskaźnik na maszynę wirtualną RandomX
};