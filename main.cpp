#include <iostream>
#include <vector>
#include <memory>
#include <csignal>
#include <fmt/core.h>
#include <thread>
#include <atomic>
#include <string>
#include <cstdio>
#include <deque>
#include <sstream>
#include "StratumClient.h"
#include "MinerWorker.h"
#include "MiningCommon.h"
#include "RandomXManager.h" // <-- DODANO

// --- NAGŁÓWKI KONSOLI (bez zmian) ---
#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#endif
// ---

// --- KONFIGURACJA I GLOBALS (zmiany) ---
const std::string POOL_HOST = "pool.supportxmr.com";
const std::string POOL_PORT = "3333";
const std::string YOUR_WALLET_ADDRESS = "44xLKKizoqAioFsVQtm9AbUVYW7TrJGFBcYVQErc18qcVRrW5koAK2Yh3kVvGibh8w15E5gym3n5V8RSV7Q2bSuPT7kHQ72";

std::shared_ptr<StratumClient> client;
std::vector<std::shared_ptr<MinerWorker>> workers;
std::shared_ptr<asio::io_context> io_context;
std::atomic_bool is_shutting_down{false};

// --- NOWY GLOBALNY MANAGER ---
std::shared_ptr<RandomXManager> g_rx_manager;
// ---

std::mutex g_stats_mutex;
std::deque<double> g_hashrate_samples;
const size_t MAX_SAMPLES_1H = 360;
// ---

// --- FUNKCJE POMOCNICZE (bez zmian) ---
void print_green_line(const std::string& s) {
    std::lock_guard<std::mutex> lock(g_cout_mutex);
#ifdef _WIN32
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
    WORD saved_attributes;
    GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
    saved_attributes = consoleInfo.wAttributes;
    SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    std::cout << s;
    SetConsoleTextAttribute(hConsole, saved_attributes);
#else
    if (isatty(STDOUT_FILENO)) {
        std::cout << "\033[1;32m" << s << "\033[0m";
    } else {
        std::cout << s;
    }
#endif
    std::cout.flush();
}

double calculate_average(size_t count) {
    if (g_hashrate_samples.empty() || count == 0) {
        return 0.0;
    }
    double sum = 0.0;
    size_t num_samples = std::min(g_hashrate_samples.size(), count);
    auto it = g_hashrate_samples.rbegin();
    for (size_t i = 0; i < num_samples; ++i) {
        sum += *it;
        ++it;
    }
    return sum / num_samples;
}

void shutdown_miner() {
    bool already_shutting_down = is_shutting_down.exchange(true);
    if (already_shutting_down) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_cout_mutex);
        std::cout << "\nZatrzymywanie minera...\n";
        std::cout.flush();
    }
    for (auto& worker : workers) {
        worker->stop();
    }
    if (io_context) {
        io_context->stop();
    }
#ifdef _WIN32
    _fcloseall();
#else
    fclose(stdin);
#endif
}

void signal_handler(int signum) {
    {
        std::lock_guard<std::mutex> lock(g_cout_mutex);
        std::cout << fmt::format("\nOtrzymano sygnał {}...", signum);
    }
    shutdown_miner();
}
// --- KONIEC FUNKCJI POMOCNICZYCH ---


/**
 * @brief Pętla raportowania Hashrate (bez zmian)
 */
void report_hashrate_loop(int num_threads) {
    std::vector<uint64_t> last_hash_counts(num_threads, 0);
    auto last_stats_time = std::chrono::steady_clock::now();

    while (!is_shutting_down) {
        auto now = std::chrono::steady_clock::now();
        double elapsed_stats = std::chrono::duration<double>(now - last_stats_time).count();

        if (elapsed_stats >= 60.0) {
            std::vector<double> thread_hashrates;
            double total_hashrate = 0;

            if (workers.size() == num_threads) {
                for (int i = 0; i < num_threads; ++i) {
                    uint64_t current_count = workers[i]->getHashCount();
                    uint64_t count_delta = current_count - last_hash_counts[i];
                    double hashrate = count_delta / elapsed_stats;
                    thread_hashrates.push_back(hashrate);
                    total_hashrate += hashrate;
                    last_hash_counts[i] = current_count;
                }
            }
            {
                std::lock_guard<std::mutex> lock(g_stats_mutex);
                g_hashrate_samples.push_back(total_hashrate);
                if (g_hashrate_samples.size() > MAX_SAMPLES_1H) {
                    g_hashrate_samples.pop_front();
                }
            }

            std::stringstream ss;
            ss << fmt::format("[HASHRATE] Total: {:.2f} H/s | Wątki: [", total_hashrate);
            for (size_t i = 0; i < thread_hashrates.size(); ++i) {
                ss << fmt::format("{:.1f}{}", thread_hashrates[i], (i == thread_hashrates.size() - 1) ? "" : ", ");
            }
            ss << "]\n";

            {
                std::lock_guard<std::mutex> lock(g_cout_mutex);
                std::cout << ss.str();
                std::cout.flush();
            }

            last_stats_time = now;
        }

        if (is_shutting_down.load()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}


/**
 * @brief Pętla sprawdzania klawiatury (bez zmian)
 */
void watch_stdin() {
#ifdef _WIN32
    int ch;
    while (!is_shutting_down) {
        if (_kbhit()) {
            ch = _getch();
            if (ch == 'q' || ch == 'Q') {
                shutdown_miner();
                break;
            }
            if (ch == 's' || ch == 'S') {
                double avg_1m, avg_15m, avg_1h;
                {
                    std::lock_guard<std::mutex> lock(g_stats_mutex);
                    avg_1m = calculate_average(6);
                    avg_15m = calculate_average(90);
                    avg_1h = calculate_average(360);
                }
                std::string stats_report = "\n--- STATYSTYKI ---\n";
                stats_report += fmt::format(" Średnia (1m):   {:.2f} H/s\n", avg_1m);
                stats_report += fmt::format(" Średnia (15m):  {:.2f} H/s\n", avg_15m);
                stats_report += fmt::format(" Średnia (1h):   {:.2f} H/s\n", avg_1h);
                stats_report += "------------------\n";
                {
                    std::lock_guard<std::mutex> cout_lock(g_cout_mutex);
                    std::cout << stats_report;
                    std::cout.flush();
                }
            }
        }
        if (is_shutting_down.load()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
#else
    // Wersja POSIX (Linux/macOS)
    struct termios old_tio, new_tio;
    if (tcgetattr(STDIN_FILENO, &old_tio) != 0) return;
    new_tio = old_tio;
    new_tio.c_lflag &= ~(ICANON | ECHO);
    if (tcsetattr(STDIN_FILENO, TCSANOW, &new_tio) != 0) return;
    int old_fcntl = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, old_fcntl | O_NONBLOCK);

    char c;
    while (!is_shutting_down) {
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n > 0) {
            if (c == 'q' || c == 'Q') {
                shutdown_miner();
                break;
            }
            if (c == 's' || c == 'S') {
                double avg_1m, avg_15m, avg_1h;
                {
                    std::lock_guard<std::mutex> lock(g_stats_mutex);
                    avg_1m = calculate_average(6);
                    avg_15m = calculate_average(90);
                    avg_1h = calculate_average(360);
                }
                std::string stats_report = "\n--- STATYSTYKI ---\n";
                stats_report += fmt::format(" Średnia (1m):   {:.2f} H/s\n", avg_1m);
                stats_report += fmt::format(" Średnia (15m):  {:.2f} H/s\n", avg_15m);
                stats_report += fmt::format(" Średnia (1h):   {:.2f} H/s\n", avg_1h);
                stats_report += "------------------\n";
                {
                    std::lock_guard<std::mutex> cout_lock(g_cout_mutex);
                    std::cout << stats_report;
                    std::cout.flush();
                }
            }
        } else if (n == 0) {
             break;
        }
        if (is_shutting_down.load()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
    fcntl(STDIN_FILENO, F_SETFL, old_fcntl);
#endif
}


/**
 * @brief Główna funkcja programu
 */
int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    if (YOUR_WALLET_ADDRESS == "TUTAJ_WKLEJ_SWOJ_ADRES_MONERO") {
        std::cerr << "BŁĄD: Musisz edytować main.cpp i podać swój adres portfela Monero.\n";
        return 1;
    }
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    int num_threads = std::max(1u, std::thread::hardware_concurrency());

    std::cout << "--- Mój CPU Miner (Szkielet C++23) ---\n";
    std::cout << fmt::format(" Adres puli: {}:{}\n", POOL_HOST, POOL_PORT);
    std::cout << fmt::format(" Portfel: {}\n", YOUR_WALLET_ADDRESS);
    std::cout << fmt::format(" Uruchamiam {} wątków roboczych (1 na fizyczny rdzeń).\n", num_threads);
    std::cout << "\nWAŻNE: Upewnij się, że masz ustawione 'Large Pages' (Blokuj strony w pamięci)!\n";
    std::cout << "Windows: 'secpol.msc' -> Zasady Lokalne -> Przypisywanie praw -> 'Blokuj strony w pamięci' (i restart).\n";
    std::cout << "Linux: 'sudo sysctl -w vm.nr_hugepages=...' (wymagane > 1100 stron 2MB).\n";
    std::cout << "\nNaciśnij 'q', aby zakończyć, 's' aby zobaczyć statystyki.\n\n";

    try {
        g_rx_manager = std::make_shared<RandomXManager>();
    } catch (const std::exception& e) {
        std::cerr << fmt::format("Krytyczny błąd inicjalizacji RandomX: {}\n", e.what());
        return 1;
    }

    io_context = std::make_shared<asio::io_context>();
    workers.reserve(num_threads);

    auto job_callback = [&](const MiningJob& job) {
        bool seed_changed = g_rx_manager->updateSeed(job.seed_hash);

        if (seed_changed) {
            {
                std::lock_guard<std::mutex> lock(g_cout_mutex);
                std::cout << fmt::format("\n[MANAGER] Globalny Dataset zaktualizowany do seeda: ...{}\n",
                                         job.seed_hash.substr(job.seed_hash.length() - 6));
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_cout_mutex);
            std::cout << fmt::format("\n[MANAGER] Rozdzielam nową pracę: {} (Seed: ...{})\n",
                                     job.job_id,
                                     job.seed_hash.substr(job.seed_hash.length() - 6));
        }
        for (auto& worker : workers) {
            worker->setNewJob(job);
        }
    };

    auto solution_callback = [&](const Solution& solution) {
        if (client) {
            client->submit(solution);
        }
    };

    auto accepted_share_callback = []() {
        print_green_line(fmt::format("[Stratum] Share zaakceptowany! :-)\n"));
    };

    client = std::make_shared<StratumClient>(
            *io_context, POOL_HOST, POOL_PORT, YOUR_WALLET_ADDRESS, job_callback,
            accepted_share_callback
    );

    for (int i = 0; i < num_threads; ++i) {
        auto worker = std::make_shared<MinerWorker>(i, solution_callback, g_rx_manager);
        workers.push_back(worker);
        worker->start();
    }

    // --- POCZĄTEK POPRAWKI 2 ---
    // Uruchamiamy wątki, ale ich NIE odłączamy (bez .detach())
    std::thread input_thread(watch_stdin);
    std::thread hashrate_thread(report_hashrate_loop, num_threads);

    client->connect();
    io_context->run(); // Ta linia blokuje, dopóki shutdown_miner() nie wywoła io_context->stop()

    // --- Kod wykonywany po zatrzymaniu io_context ---

    {
        std::lock_guard<std::mutex> lock(g_cout_mutex);
        std::cout << "Pętla sieciowa zatrzymana. Czekanie na wątki pomocnicze...\n";
    }

    // shutdown_miner() ustawił is_shutting_down na true.
    // Czekamy (join) na wątki pomocnicze, aż same się zakończą.
    if (hashrate_thread.joinable()) {
        hashrate_thread.join();
    }
    if (input_thread.joinable()) {
        input_thread.join();
    }

    {
        std::lock_guard<std::mutex> lock(g_cout_mutex);
        std::cout << "Wątki pomocnicze zatrzymane. Czekanie na wątki robocze...\n";
    }

    // Teraz, gdy wątki pomocnicze są zatrzymane, możemy bezpiecznie
    // zniszczyć wątki robocze.
    // Wyczyszczenie wektora wywoła destruktory shared_ptr,
    // co wywoła destruktory MinerWorker, co wywoła destruktory jthread,
    // które z kolei wykonają 'join' na każdym wątku roboczym.
    // Komunikaty "[Worker X] Zatrzymany." pojawią się tutaj.
    workers.clear();

    // --- KONIEC POPRAWKI 2 ---

    {
        std::lock_guard<std::mutex> lock(g_cout_mutex);
        std::cout << "Wszystkie wątki zatrzymane. Zamykanie.\n";
    }
    return 0;
    // Teraz funkcja main() może bezpiecznie zwrócić. Wszystkie wątki
    // są zakończone, więc nie będzie wyścigu przy niszczeniu
    // globalnych zmiennych (jak g_cout_mutex).
}