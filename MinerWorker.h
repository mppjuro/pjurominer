#pragma once

#include "MiningCommon.h" // Zawiera definicje MiningJob i Solution
#include <thread>        // Wymagany dla std::jthread
#include <functional>    // Wymagany dla std::function (callback)
#include <mutex>         // Wymagany dla std::mutex (do ochrony pracy)
#include <optional>      // Wymagany dla std::optional (do przechowywania pracy)
#include <string>
#include <atomic>        // <-- DODANO

/**
 * @class MinerWorker
 * @brief Reprezentuje pojedynczy wątek roboczy (worker) wykonujący obliczenia.
 *
 * Używa std::jthread do automatycznego zarządzania cyklem życia wątku (RAII)
 * oraz std::stop_token do bezpiecznego i czystego zatrzymywania pracy.
 */
class MinerWorker {
public:
    /**
     * @brief Definicja typu dla funkcji zwrotnej (callback),
     * wywoływanej po znalezieniu poprawnego rozwiązania.
     */
    using SolutionCallback = std::function<void(const Solution&)>;

    /**
     * @brief Konstruktor.
     * @param id Unikalny identyfikator tego workera (np. 0, 1, 2...).
     * @param callback Funkcja zwrotna do wysyłania znalezionych rozwiązań.
     */
    MinerWorker(int id, SolutionCallback callback);

    /**
     * @brief Destruktor.
     * Automatycznie poprosi o zatrzymanie i zaczeka (join) na wątek
     * dzięki użyciu std::jthread.
     */
    ~MinerWorker();

    /**
     * @brief Uruchamia wątek roboczy.
     */
    void start();

    /**
     * @brief Wysyła żądanie zatrzymania do wątku roboczego.
     * Nie blokuje – std::jthread zajmie się czekaniem w destruktorze.
     */
    void stop();

    /**
     * @brief Przekazuje nowe zadanie (pracę) do wykonania przez wątek.
     * Metoda jest bezpieczna wątkowo (thread-safe).
     * @param job Nowy obiekt MiningJob otrzymany z puli.
     */
    void setNewJob(const MiningJob& job);

    /**
     * @brief Pobiera całkowitą liczbę wykonanych hashów przez ten wątek.
     * @return Liczba hashów.
     */
    uint64_t getHashCount() const;

private:
    /**
     * @brief Główna funkcja (pętla) wykonywana przez wątek std::jthread.
     * @param stoken Token, który jest sprawdzany, aby wiedzieć, kiedy przerwać pętlę.
     */
    void run(std::stop_token stoken);

    int m_id; // Identyfikator tego workera (głównie do logowania)

    std::jthread m_thread; // Obiekt wątku (automatycznie zarządza join)
    SolutionCallback m_solution_callback; // Callback do wysyłania rozwiązań

    // Mutex chroniący dostęp do m_current_job
    std::mutex m_job_mutex;

    // std::optional jest używany jako "flaga" nowej pracy.
    // Jeśli zawiera wartość, oznacza to nową pracę do pobrania.
    std::optional<MiningJob> m_current_job;

    // --- NOWA SEKCJA ---
    std::atomic<uint64_t> m_hash_count{0}; // Atomowy licznik hashów
    // --- KONIEC NOWEJ SEKCJI ---
};