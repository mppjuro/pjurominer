#include "MiningCommon.h"
#include <stdexcept>
#include <format>
#include <algorithm> // Dla std::reverse

// Funkcja pomocnicza do konwersji pojedynczego znaku hex
uint8_t hex_char_to_byte(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    throw std::runtime_error("Nieprawidłowy znak hex");
}

std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
    if (hex.length() % 2 != 0) {
        throw std::runtime_error("String hex ma nieparzystą długość");
    }
    std::vector<uint8_t> bytes;
    bytes.reserve(hex.length() / 2);
    for (size_t i = 0; i < hex.length(); i += 2) {
        bytes.push_back((hex_char_to_byte(hex[i]) << 4) | hex_char_to_byte(hex[i + 1]));
    }
    return bytes;
}

std::string bytes_to_hex(const uint8_t* bytes, size_t size) {
    std::string hex_str;
    hex_str.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        hex_str += std::format("{:02x}", bytes[i]);
    }
    return hex_str;
}

/**
 * @brief Porównuje dwa 256-bitowe hashe (w formacie hex).
 * Ważne: Hashe Monero (i targety) są w formacie little-endian.
 * Musimy je porównywać od tyłu.
 */
bool check_hash_target_real(const std::string& hash_hex, const std::string& target_hex) {
    // 1. Konwertujemy hex na bajty
    auto hash_bytes = hex_to_bytes(hash_hex);
    auto target_bytes = hex_to_bytes(target_hex);

    if (hash_bytes.size() != 32 || target_bytes.size() != 32) {
        return false; // Nieprawidłowy rozmiar
    }

    // 2. Porównanie Little-Endian (od końca do początku)
    // int memcmp(a, b, n) zwraca:
    // < 0 jeśli a < b
    // = 0 jeśli a == b
    // > 0 jeśli a > b
    // My musimy porównać od tyłu (najbardziej znaczący bajt jest na końcu)
    for (int i = 31; i >= 0; --i) {
        if (hash_bytes[i] < target_bytes[i]) {
            return true; // hash < target
        }
        if (hash_bytes[i] > target_bytes[i]) {
            return false; // hash > target
        }
    }
    // Jeśli pętla się zakończyła, są równe
    return true; // hash == target
}