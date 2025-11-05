#include <iostream>
#include <vector>
#include <memory>
#include <csignal>
#include <format> // <-- ZMIANA
#include "StratumClient.h"
#include "MinerWorker.h"

// --- KONFIGURACJA (bez zmian) ---
const std::string POOL_HOST = "pool.supportxmr.com";
const std::string POOL_PORT = "3333";
const std::string YOUR_WALLET_ADDRESS = "TUTAJ_WKLEJ_SWOJ_ADRES_MONERO";

// Globalne wskaźniki (bez zmian)
std::shared_ptr<StratumClient> client;
std::vector<std::shared_ptr<MinerWorker>> workers;
std::shared_ptr<asio::io_context> io_context;

void signal_handler(int signum) {
    // --- ZMIANA ---
    std::cout << std::format("\nZatrzymywanie minera (sygnał {})...\n", signum);

    // Prosimy workery o zatrzymanie
    for (auto& worker : workers) {
        worker->stop(); // To teraz tylko wysyła request_stop()
    }

    // Zatrzymujemy pętlę sieciową
    if (io_context) {
        io_context->stop();
    }
}

int main() {
    if (YOUR_WALLET_ADDRESS == "TUTAJ_WKLEJ_SWOJ_ADRES_MONERO") {
        std::cerr << "BŁĄD: Musisz edytować main.cpp i podać swój adres portfela Monero.\n";
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    int num_threads = std::max(1u, std::thread::hardware_concurrency() - 1);
    if (num_threads == 0) num_threads = 1;

    // --- ZMIANY (std::format) ---
    std::cout << "--- Mój CPU Miner (Szkielet C++23) ---\n";
    std::cout << std::format(" Adres puli: {}:{}\n", POOL_HOST, POOL_PORT);
    std::cout << std::format(" Portfel: {}\n", YOUR_WALLET_ADDRESS);
    std::cout << std::format(" Uruchamiam {} wątków roboczych.\n", num_threads);

    io_context = std::make_shared<asio::io_context>();
    workers.reserve(num_threads);

    auto job_callback = [&](const MiningJob& job) {
        for (auto& worker : workers) {
            worker->setNewJob(job);
        }
    };

    auto solution_callback = [&](const Solution& solution) {
        if (client) {
            client->submit(solution);
        }
    };

    client = std::make_shared<StratumClient>(
        *io_context, POOL_HOST, POOL_PORT, YOUR_WALLET_ADDRESS, job_callback
    );

    for (int i = 0; i < num_threads; ++i) {
        auto worker = std::make_shared<MinerWorker>(i, solution_callback);
        workers.push_back(worker);
        worker->start();
    }

    client->connect();
    io_context->run(); // Blokuje, dopóki signal_handler nie wywoła stop()

    std::cout << "Wszystkie wątki zatrzymane. Zamykanie.\n";
    return 0;
}