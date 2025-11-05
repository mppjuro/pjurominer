#include "StratumClient.h"
#include <iostream>
#include <fmt/core.h>

/**
 * @brief Konstruktor klienta Stratum.
 * @param io_context Kontekst Asio (pętla zdarzeń)
 * @param host Adres puli
 * @param port Port puli
 * @param user Adres portfela (użytkownik)
 * @param job_cb Funkcja zwrotna (callback) do przekazywania nowej pracy (MiningJob)
 */
StratumClient::StratumClient(asio::io_context& io_context,
                             const std::string& host,
                             const std::string& port,
                             const std::string& user,
                             JobCallback job_cb)
        : m_io_context(io_context),
          m_socket(io_context),
          m_resolver(io_context),
          m_host(host),
          m_port(port),
          m_user(user),
          m_pass("x"), // Standardowe hasło "x" dla pul Monero
          m_job_callback(std::move(job_cb)),
          m_request_id(1) {} // Zaczynamy ID zapytań od 1

/**
 * @brief Publiczna metoda inicjująca połączenie.
 */
void StratumClient::connect() {
    do_resolve();
}

/**
 * @brief Krok 1: Rozwiąż nazwę hosta (np. "pool.supportxmr.com" na adres IP).
 */
void StratumClient::do_resolve() {
    auto self = shared_from_this(); // Utrzymujemy obiekt przy życiu na czas operacji asynchronicznej
    m_resolver.async_resolve(m_host, m_port,
                             [this, self](const asio::error_code& ec, tcp::resolver::results_type endpoints) {
                                 on_resolve(ec, endpoints);
                             });
}

/**
 * @brief Krok 2: Callback po rozwiązaniu nazwy. Próba połączenia.
 */
void StratumClient::on_resolve(const asio::error_code& ec, tcp::resolver::results_type endpoints) {
    if (ec) {
        std::cerr << fmt::format("Błąd rozwiązywania adresu: {}\n", ec.message());
        return;
    }
    auto self = shared_from_this();
    // Próbujemy połączyć się z listą znalezionych adresów
    asio::async_connect(m_socket, endpoints,
                        [this, self](const asio::error_code& ec, const tcp::endpoint& /*endpoint*/) {
                            on_connect(ec);
                        });
}

/**
 * @brief Krok 3: Callback po pomyślnym (lub nie) połączeniu.
 */
void StratumClient::on_connect(const asio::error_code& ec) {
    if (ec) {
        std::cerr << fmt::format("Błąd połączenia: {}\n", ec.message());
        // W produkcji: dodać logikę ponawiania połączenia
        return;
    }

    std::cout << fmt::format("[Stratum] Połączono z {}:{}\n", m_host, m_port);

    // Po połączeniu, logujemy się do puli
    do_login();

    // I natychmiast zaczynamy nasłuchiwać na odpowiedzi
    do_read();
}

/**
 * @brief Wysyła żądanie logowania (Stratum JSON-RPC) do puli.
 */
void StratumClient::do_login() {
    json login_req = {
            {"id", m_request_id++},
            {"method", "login"},
            {"params", {
                           {"login", m_user}, // Nasz portfel
                           {"pass", m_pass},  // Zazwyczaj "x"
                           {"agent", "pjurominer/0.1"} // Nazwa naszego minera
                   }}
    };
    do_write(login_req);
}

/**
 * @brief Publiczna metoda do wysyłania znalezionego rozwiązania do puli.
 */
void StratumClient::submit(const Solution& solution) {
    // UWAGA: Format nonce i result musi być zgodny ze specyfikacją puli
    // (zazwyczaj hex string). Nasza zaślepka tego nie gwarantuje.
    json submit_req = {
            {"id", m_request_id++},
            {"method", "submit"},
            {"params", {
                           {"id", m_login_id}, // ID subskrypcji otrzymane przy logowaniu
                           {"job_id", solution.job_id},
                           {"nonce", fmt::format("{:08x}", solution.nonce)}, // Przykładowe formatowanie nonce do hex
                           {"result", solution.result_hash}
                   }}
    };

    std::cout << fmt::format("[Stratum] Wysyłam rozwiązanie dla {}\n", solution.job_id);
    do_write(submit_req);
}

/**
 * @brief Wewnętrzna funkcja do wysyłania dowolnego obiektu JSON do puli.
 */
void StratumClient::do_write(const json& j) {
    auto self = shared_from_this();
    // Protokół Stratum wymaga znaku nowej linii ('\n') na końcu każdej wiadomości JSON
    std::string request = j.dump() + "\n";

    asio::async_write(m_socket, asio::buffer(request.data(), request.length()),
                      [this, self](const asio::error_code& ec, std::size_t /*length*/) {
                          if (ec) {
                              std::cerr << fmt::format("Błąd zapisu: {}\n", ec.message());
                          }
                      });
}

/**
 * @brief Rozpoczyna asynchroniczne nasłuchiwanie na dane od puli.
 */
void StratumClient::do_read() {
    auto self = shared_from_this();
    // Czytamy dane z gniazda, aż napotkamy znak nowej linii
    asio::async_read_until(m_socket, m_buffer, '\n',
                           [this, self](const asio::error_code& ec, std::size_t length) {
                               on_read(ec, length);
                           });
}

/**
 * @brief Callback wywoływany po otrzymaniu danych (zakończonych '\n').
 */
void StratumClient::on_read(const asio::error_code& ec, std::size_t length) {
    if (ec) {
        if (ec != asio::error::eof) {
            // Prawdziwy błąd
            std::cerr << fmt::format("Błąd odczytu: {}\n", ec.message());
        } else {
            // Pula zamknęła połączenie
            std::cout << "[Stratum] Pula zamknęła połączenie.\n";
        }
        m_socket.close(); // Zamykamy gniazdo w przypadku błędu lub EOF
        return;
    }

    // Konwertujemy bufor asio (m_buffer) na std::string
    std::istream is(&m_buffer);
    std::string message(std::istreambuf_iterator<char>(is), {});

    // Bierzemy tylko tę część bufora, która została odczytana (do '\n')
    // Reszta (jeśli jest) zostanie w m_buffer do następnego wywołania on_read
    std::string line = message.substr(0, length);

    // Przetwarzamy otrzymaną linię
    handle_message(line);

    // Kontynuujemy nasłuchiwanie
    do_read();
}

/**
 * @brief Przetwarza pojedynczą wiadomość JSON-RPC (jedną linię) od puli.
 */
void StratumClient::handle_message(const std::string& message_str) {
    try {
        json j = json::parse(message_str);

        // Zmniejszono poziom logowania, aby nie mieszać z logami pracy
        if (!j["result"].is_null()) {
            // std::cout << "[Stratum] Otrzymano odpowiedź (result).\n";
        } else if (!j["error"].is_null()) {
            std::cout << "[Stratum] Otrzymano błąd.\n";
        }

        // PRZYPADEK 1: Pula wysyła nam nową pracę (nowy blok do kopania)
        if (!j["method"].is_null() && j["method"] == "job") {
            auto params = j["params"];
            MiningJob job = {
                    params["job_id"],
                    params["blob"],    // Dane bloku do haszowania
                    params["target"], // Cel trudności
                    params["seed_hash"] // Seed dla RandomX
            };

            // --- DODANO LOGOWANIE ---
            std::cout << fmt::format("[Stratum] Otrzymano nową pracę: {} (Seed: ...{})\n",
                                     job.job_id,
                                     job.seed_hash);
            // ---

            // Przekazujemy pracę do workerów (przez callback do main.cpp)
            m_job_callback(job);

            // PRZYPADEK 2: Pula odpowiada na nasze żądanie (np. na login)
        } else if (!j["result"].is_null() && !j["result"]["id"].is_null()) {

            // Zakładamy, że to odpowiedź na login
            m_login_id = j["result"]["id"]; // Zapisujemy nasz ID sesji
            std::cout << fmt::format("[Stratum] Zalogowano. ID subskrypcji: {}\n", m_login_id);

            // Czasem pula wysyła pierwszą pracę od razu w odpowiedzi na login
            if (!j["result"]["job"].is_null()) {
                auto job_params = j["result"]["job"];
                MiningJob job = {
                        job_params["job_id"],
                        job_params["blob"],
                        job_params["target"],
                        job_params["seed_hash"]
                };

                // --- DODANO LOGOWANIE ---
                std::cout << fmt::format("[Stratum] Otrzymano pierwszą pracę: {} (Seed: ...{})\n",
                                         job.job_id,
                                         job.seed_hash.substr(job.seed_hash.length() - 6));
                // ---

                m_job_callback(job);
            }

            // PRZYPADEK 3: Pula zgłasza błąd (np. zły portfel, złe rozwiązanie)
        } else if (!j["error"].is_null()) {
            std::cerr << fmt::format("[Stratum] Błąd puli: {}\n", j["error"].dump());
        }

    } catch (json::parse_error& e) {
        std::cerr << fmt::format("Błąd parsowania JSON: {}\n", e.what());
        std::cerr << fmt::format("Otrzymana (uszkodzona?) wiadomość: {}\n", message_str);
    }
}