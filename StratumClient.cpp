#include "StratumClient.h"
#include <iostream>
#include <fmt/core.h>
#include "MiningCommon.h"

/**
 * @brief Konstruktor klienta Stratum.
 */
StratumClient::StratumClient(asio::io_context& io_context,
                             const std::string& host,
                             const std::string& port,
                             const std::string& user,
                             JobCallback job_cb,
                             AcceptedShareCallback share_cb)
        : m_io_context(io_context),
          m_socket(io_context),
          m_resolver(io_context),
          m_host(host),
          m_port(port),
          m_user(user),
          m_pass("x"),
          m_job_callback(std::move(job_cb)),
          m_accepted_share_callback(std::move(share_cb)),
          m_request_id(1) {}

void StratumClient::connect() {
    do_resolve();
}

void StratumClient::do_resolve() {
    auto self = shared_from_this();
    m_resolver.async_resolve(m_host, m_port,
                             [this, self](const asio::error_code& ec, tcp::resolver::results_type endpoints) {
                                 on_resolve(ec, endpoints);
                             });
}

void StratumClient::on_resolve(const asio::error_code& ec, tcp::resolver::results_type endpoints) {
    if (ec) {
        {
            std::lock_guard<std::mutex> lock(g_cout_mutex);
            std::cerr << fmt::format("Błąd rozwiązywania adresu: {}\n", ec.message());
        }
        return;
    }
    auto self = shared_from_this();
    asio::async_connect(m_socket, endpoints,
                        [this, self](const asio::error_code& ec, const tcp::endpoint& /*endpoint*/) {
                            on_connect(ec);
                        });
}

void StratumClient::on_connect(const asio::error_code& ec) {
    if (ec) {
        {
            std::lock_guard<std::mutex> lock(g_cout_mutex);
            std::cerr << fmt::format("Błąd połączenia: {}\n", ec.message());
        }
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_cout_mutex);
        std::cout << fmt::format("[Stratum] Połączono z {}:{}\n", m_host, m_port);
    }

    do_login();
    do_read();
}

void StratumClient::do_login() {
    json login_req = {
            {"id", m_request_id++},
            {"method", "login"},
            {"params", {
                           {"login", m_user},
                           {"pass", m_pass},
                           {"agent", "pjurominer/0.1"}
                   }}
    };
    do_write(login_req);
}

void StratumClient::submit(const Solution& solution) {
    int req_id = m_request_id++;

    json submit_req = {
            {"id", req_id},
            {"method", "submit"},
            {"params", {
                           {"id", m_login_id},
                           {"job_id", solution.job_id},
                           {"nonce", fmt::format("{:08x}", solution.nonce)},
                           {"result", solution.result_hash}
                   }}
    };

    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        m_submitted_share_ids.insert(req_id);
    }

    {
        std::lock_guard<std::mutex> lock(g_cout_mutex);
        std::cout << fmt::format("[Stratum] Wysyłam rozwiązanie dla {}\n", solution.job_id);
    }
    do_write(submit_req);
}

void StratumClient::do_write(const json& j) {
    auto self = shared_from_this();
    std::string request = j.dump() + "\n";

    asio::async_write(m_socket, asio::buffer(request.data(), request.length()),
                      [this, self](const asio::error_code& ec, std::size_t /*length*/) {
                          if (ec) {
                              {
                                  std::lock_guard<std::mutex> lock(g_cout_mutex);
                                  std::cerr << fmt::format("Błąd zapisu: {}\n", ec.message());
                              }
                          }
                      });
}

void StratumClient::do_read() {
    auto self = shared_from_this();
    asio::async_read_until(m_socket, m_buffer, '\n',
                           [this, self](const asio::error_code& ec, std::size_t length) {
                               on_read(ec, length);
                           });
}

void StratumClient::on_read(const asio::error_code& ec, std::size_t length) {
    if (ec) {
        {
            std::lock_guard<std::mutex> lock(g_cout_mutex);
            if (ec != asio::error::eof) {
                std::cerr << fmt::format("Błąd odczytu: {}\n", ec.message());
            } else {
                std::cout << "[Stratum] Pula zamknęła połączenie.\n";
            }
        }
        m_socket.close();
        return;
    }

    // --- POCZĄTEK POPRAWKI ---
    // Przetwarzamy bufor linia po linii, ponieważ mogło przyjść wiele wiadomości na raz.
    std::istream is(&m_buffer);
    std::string line;

    // std::getline() odczytuje linię z 'is' (co konsumuje ją z 'm_buffer')
    // i zwraca true, jeśli się udało.
    while (std::getline(is, line)) {

        // Niektóre pule mogą wysyłać \r\n, std::getline usuwa \n, ale zostawia \r
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // Ignoruj puste linie (np. jeśli pula wysłała \n\n)
        if (line.empty()) {
            continue;
        }

        // Przetwórz pełną, czystą linię JSON
        handle_message(line);

        // Jeśli handle_message spowodowało błąd i zamknęło gniazdo, przerwij
        if (!m_socket.is_open()) {
            return;
        }
    }
    // --- KONIEC POPRAWKI ---

    // 'm_buffer' zawiera teraz tylko resztę (prawdopodobnie niekompletną)
    // wiadomość. Kontynuujemy nasłuchiwanie.
    do_read();
}

void StratumClient::handle_message(const std::string& message_str) {
    try {
        json j = json::parse(message_str);

        if (j.contains("id") && j["id"].is_number()) {
            int response_id = j["id"];
            bool is_share_response = false;

            {
                std::lock_guard<std::mutex> lock(m_state_mutex);
                if (m_submitted_share_ids.count(response_id)) {
                    is_share_response = true;
                    m_submitted_share_ids.erase(response_id);
                }
            }

            if (is_share_response) {
                if (j.contains("result") && !j["result"].is_null()) {
                    if (m_accepted_share_callback) {
                        m_accepted_share_callback();
                    }
                } else if (j.contains("error") && !j["error"].is_null()) {
                    {
                        std::lock_guard<std::mutex> lock(g_cout_mutex);
                        std::cerr << fmt::format("[Stratum] Share odrzucony: {}\n", j["error"].dump());
                    }
                }
                return;
            }
        }


        if (!j["error"].is_null()) {
            {
                std::lock_guard<std::mutex> lock(g_cout_mutex);
                std::cerr << fmt::format("[Stratum] Błąd puli: {}\n", j["error"].dump());
            }
        }


        if (!j["method"].is_null() && j["method"] == "job") {
            auto params = j["params"];
            MiningJob job = {
                    params["job_id"],
                    params["blob"],
                    params["target"],
                    params["seed_hash"]
            };

            {
                std::lock_guard<std::mutex> lock(g_cout_mutex);
                std::cout << fmt::format("[Stratum] Otrzymano nową pracę: {} (Seed: ...{})\n",
                                         job.job_id,
                                         job.seed_hash);
            }

            m_job_callback(job);

        } else if (!j["result"].is_null() && !j["result"]["id"].is_null()) {

            m_login_id = j["result"]["id"];
            {
                std::lock_guard<std::mutex> lock(g_cout_mutex);
                std::cout << fmt::format("[Stratum] Zalogowano. ID subskrypcji: {}\n", m_login_id);
            }

            if (!j["result"]["job"].is_null()) {
                auto job_params = j["result"]["job"];
                MiningJob job = {
                        job_params["job_id"],
                        job_params["blob"],
                        job_params["target"],
                        job_params["seed_hash"]
                };

                {
                    std::lock_guard<std::mutex> lock(g_cout_mutex);
                    std::cout << fmt::format("[Stratum] Otrzymano pierwszą pracę: {} (Seed: ...{})\n",
                                             job.job_id,
                                             job.seed_hash.substr(job.seed_hash.length() - 6));
                }

                m_job_callback(job);
            }

        } else if (!j["error"].is_null()) {
            // Już obsłużone wyżej
        }

    } catch (json::parse_error& e) {

        // --- ZMIANA: Zbuduj string PRZED blokadą ---
        std::string error_msg = fmt::format("Błąd parsowania JSON: {}\n", e.what());
        error_msg += fmt::format("Otrzymana (uszkodzona?) wiadomość: {}\n", message_str);
        // --- Koniec budowania stringa ---

        {
            std::lock_guard<std::mutex> lock(g_cout_mutex);
            std::cerr << error_msg; // Wypisz gotowy string
        }
    }
}