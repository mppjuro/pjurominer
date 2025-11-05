#include <iostream>
#include <vector>
#include <memory>
#include <csignal>
#include <fmt/core.h>
#include <thread>     // <-- DODANO
#include <atomic>     // <-- DODANO
#include <string>     // <-- DODANO
#include "StratumClient.h"
#include "MinerWorker.h"

// --- DODANY NAGŁÓWEK DLA KONSOLI WINDOWS ---
#ifdef _WIN32
#include <windows.h>
#endif
// ---

// --- KONFIGURACJA (bez zmian) ---
const std::string POOL_HOST = "pool.supportxmr.com";
const std::string POOL_PORT = "3333";
const std::string YOUR_WALLET_ADDRESS = "44xLKKizoqAioFsVQtm9AbUVYW7TrJGFBcYVQErc18qcVRrW5koAK2Yh3kVvGibh8w15E5gym3n5V8RSV7Q2bSuPT7kHQ72";

// --- ZMIANA: Przeniesiono globalne wskaźniki i dodano flagę atomową ---
std::shared_ptr<StratumClient> client;
std::vector<std::shared_ptr<MinerWorker>> workers;
std::shared_ptr<asio::io_context> io_context;
std::atomic_bool is_shutting_down{false}; // Flaga zapobiegająca wielokrotnemu zamykaniu
// ---

/**
 * @brief Wspólna funkcja do bezpiecznego zamykania minera.
 * Może być wywołana z wątku sygnału (Ctrl+C) lub wątku wejścia ('q').
 */
void shutdown_miner() {
    // Używamy exchange, aby atomowo ustawić flagę na true i pobrać jej poprzednią wartość
    bool already_shutting_down = is_shutting_down.exchange(true);
    if (already_shutting_down) {
        return; // Już jesteśmy w trakcie zamykania
    }

    std::cout << "\nZatrzymywanie minera...\n";

    // Prosimy workery o zatrzymanie
    for (auto& worker : workers) {
        worker->stop();
    }

    // Zatrzymujemy pętlę sieciową
    if (io_context) {
        io_context->stop();
    }
}

/**
 * @brief Handler sygnałów (np. Ctrl+C).
 */
void signal_handler(int signum) {
    std::cout << fmt::format("\nOtrzymano sygnał {}...", signum);
    shutdown_miner();
}

/**
 * @brief Nowa funkcja uruchamiana w osobnym wątku do nasłuchiwania na 'q'.
 */
void watch_stdin_for_quit() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "q") {
            std::cout << "Wykryto 'q', zamykanie...";
            shutdown_miner();
            break;
        }
    }
}

int main() {
    // --- DODANE LINIE DLA KONSOLI WINDOWS ---
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    // ---

    if (YOUR_WALLET_ADDRESS == "TUTAJ_WKLEJ_SWOJ_ADRES_MONERO") {
        std::cerr << "BŁĄD: Musisz edytować main.cpp i podać swój adres portfela Monero.\n";
        return 1;
    }

    // Rejestrujemy handlery sygnałów
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    int num_threads = std::max(1u, std::thread::hardware_concurrency() - 1);
    if (num_threads == 0) num_threads = 1;

    std::cout << "--- Mój CPU Miner (Szkielet C++23) ---\n";
    std::cout << fmt::format(" Adres puli: {}:{}\n", POOL_HOST, POOL_PORT);
    std::cout << fmt::format(" Portfel: {}\n", YOUR_WALLET_ADDRESS);
    std::cout << fmt::format(" Uruchamiam {} wątków roboczych.\n", num_threads);
    std::cout << "\nWpisz 'q' i naciśnij Enter, aby zakończyć.\n\n"; // <-- DODANO INSTRUKCJĘ

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

    // --- DODANO WĄTEK NASŁUCHUJĄCY NA WEJŚCIE ---
    // Uruchamiamy wątek i go "odłączamy" (detach),
    // aby nie blokował głównego programu na zamknięcie.
    // Zostanie on siłowo zakończony przez system, gdy main() się zakończy.
    std::thread input_thread(watch_stdin_for_quit);
    input_thread.detach();
    // ---

    client->connect();
    io_context->run(); // Blokuje, dopóki shutdown_miner() nie wywoła stop()

    std::cout << "Pętla sieciowa zatrzymana. Czekanie na wątki robocze...\n";

    // Destruktory jthread (w wektorze 'workers') automatycznie poczekają (join)
    // na zakończenie każdego wątku roboczego tutaj, gdy 'workers' wychodzi z zasięgu.

    std::cout << "Wszystkie wątki zatrzymane. Zamykanie.\n";
    return 0;
}