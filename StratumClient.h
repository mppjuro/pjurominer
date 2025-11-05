#pragma once

#include <string>
#include <memory>       // Dla std::shared_ptr i std::enable_shared_from_this
#include <functional>   // Dla std::function (callback)
#include <atomic>       // Dla std::atomic (licznik ID zapytań)
#include <set>          // <-- DODANO
#include <mutex>        // <-- DODANO

#include <asio.hpp>               // Główny plik nagłówkowy Asio
#include <nlohmann/json.hpp>      // Biblioteka do obsługi JSON

#include "MiningCommon.h" // Potrzebujemy definicji struktur MiningJob i Solution

// Używamy aliasów dla czytelności
using asio::ip::tcp;
using json = nlohmann::json;

/**
 * @class StratumClient
 * @brief Zarządza asynchroniczną komunikacją sieciową z pulą wydobywczą
 * przez protokół Stratum (JSON-RPC przez TCP).
 *
 * Używa std::enable_shared_from_this, aby bezpiecznie zarządzać
 * swoim cyklem życia podczas operacji asynchronicznych Asio.
 */
class StratumClient : public std::enable_shared_from_this<StratumClient> {
public:
    /**
     * @brief Definicja typu dla funkcji zwrotnej (callback),
     * wywoływanej po otrzymaniu nowej pracy (job) z puli.
     */
    using JobCallback = std::function<void(const MiningJob&)>;

    // --- NOWA SEKCJA ---
    /**
     * @brief Definicja typu dla funkcji zwrotnej (callback),
     * wywoływanej po pomyślnym zaakceptowaniu rozwiązania (share).
     */
    using AcceptedShareCallback = std::function<void()>;
    // --- KONIEC NOWEJ SEKCJI ---


    /**
     * @brief Konstruktor.
     * @param io_context Referencja do głównej pętli zdarzeń Asio.
     * @param host Adres serwera puli.
     * @param port Port serwera puli.
     * @param user Adres portfela (login).
     * @param job_cb Funkcja callback do przekazywania nowych zadań.
     * @param share_cb Funkcja callback dla zaakceptowanych udziałów.
     */
    StratumClient(asio::io_context& io_context,
                  const std::string& host,
                  const std::string& port,
                  const std::string& user,
                  JobCallback job_cb,
                  AcceptedShareCallback share_cb); // <-- ZMODYFIKOWANO

    /**
     * @brief Inicjuje proces łączenia z serwerem.
     */
    void connect();

    /**
     * @brief Wysyła znalezione rozwiązanie (Solution) do puli.
     * Ta metoda jest bezpieczna do wywołania z dowolnego wątku.
     * @param solution Obiekt zawierający znalezione rozwiązanie.
     */
    void submit(const Solution& solution);

private:
    // --- Metody obsługi łańcucha połączenia Asio ---

    /**
     * @brief Rozpoczyna rozwiązywanie nazwy hosta (DNS).
     */
    void do_resolve();

    /**
     * @brief Callback po rozwiązaniu nazwy hosta. Inicjuje połączenie TCP.
     */
    void on_resolve(const asio::error_code& ec, tcp::resolver::results_type endpoints);

    /**
     * @brief Callback po nawiązaniu połączenia TCP. Loguje się i rozpoczyna czytanie.
     */
    void on_connect(const asio::error_code& ec);

    // --- Metody obsługi protokołu Stratum ---

    /**
     * @brief Wysyła żądanie logowania JSON-RPC.
     */
    void do_login();

    // --- Metody obsługi odczytu i zapisu Asio ---

    /**
     * @brief Rozpoczyna asynchroniczne nasłuchiwanie na dane przychodzące (do znaku '\n').
     */
    void do_read();

    /**
     * @brief Callback wywoływany po otrzymaniu danych. Przetwarza dane i kontynuuje nasłuch.
     */
    void on_read(const asio::error_code& ec, std::size_t length);

    /**
     * @brief Przetwarza pojedynczą linię (wiadomość JSON) otrzymaną od puli.
     * @param message_str Wiadomość w formacie string.
     */
    void handle_message(const std::string& message_str);

    /**
     * @brief Wysyła asynchronicznie obiekt JSON do serwera (z dodanym '\n').
     * @param j Obiekt nlohmann::json do wysłania.
     */
    void do_write(const json& j);

    // --- Zmienne członkowskie ---

    // Obiekty Asio
    asio::io_context& m_io_context; // Referencja do pętli zdarzeń
    tcp::socket m_socket;           // Gniazdo TCP
    tcp::resolver m_resolver;       // Resolver DNS
    asio::streambuf m_buffer;       // Bufor do odczytu danych

    // Dane konfiguracyjne
    std::string m_host;
    std::string m_port;
    std::string m_user; // Portfel
    std::string m_pass; // Hasło (zazwyczaj "x")

    // Stan i logika
    JobCallback m_job_callback;     // Callback dla nowych zadań
    AcceptedShareCallback m_accepted_share_callback; // <-- DODANO
    std::atomic<int> m_request_id;  // Licznik dla ID zapytań JSON-RPC
    std::string m_login_id;         // ID sesji/subskrypcji otrzymane z puli

    // --- NOWA SEKCJA ---
    std::mutex m_state_mutex; // Chroni m_submitted_share_ids
    std::set<int> m_submitted_share_ids; // Przechowuje ID wysłanych zapytań 'submit'
    // --- KONIEC NOWEJ SEKCJI ---
};