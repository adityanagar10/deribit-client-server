#include "httplib.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#define CLIENT_ID ""
#define CLIENT_SECRET "";

using json = nlohmann::json;
typedef websocketpp::server<websocketpp::config::asio> server;
typedef std::set<websocketpp::connection_hdl,
                 std::owner_less<websocketpp::connection_hdl>>
    con_list;

class websocket_server {
public:
  websocket_server() {
    m_server.init_asio();

    m_server.set_open_handler(websocketpp::lib::bind(
        &websocket_server::on_open, this, websocketpp::lib::placeholders::_1));
    m_server.set_close_handler(websocketpp::lib::bind(
        &websocket_server::on_close, this, websocketpp::lib::placeholders::_1));
    m_server.set_message_handler(websocketpp::lib::bind(
        &websocket_server::on_message, this, websocketpp::lib::placeholders::_1,
        websocketpp::lib::placeholders::_2));

    // m_server.clear_access_channels(websocketpp::log::alevel::all);
    // m_server.set_access_channels(websocketpp::log::alevel::fail);
    // m_server.clear_error_channels(websocketpp::log::elevel::all);
    // m_server.set_error_channels(websocketpp::log::elevel::fatal);

    fetch_instruments();
  }

  void run(uint16_t port) {
    m_server.listen(port);
    m_server.start_accept();

    std::thread orderbook_thread(&websocket_server::orderbook_update_loop,
                                 this);
    std::thread positions_thread(&websocket_server::positions_update_loop,
                                 this);
    std::thread open_orders_thread(&websocket_server::open_orders_update_loop,
                                   this);

    m_server.run();

    m_done = true;
    orderbook_thread.join();
    positions_thread.join();
    open_orders_thread.join();
  }

private:
  httplib::SSLClient m_http_client{"test.deribit.com"};
  const std::vector<int> valid_depths = {1, 5, 10, 20, 50, 100, 1000, 10000};

  // Pre-allocated buffers
  std::vector<char> response_buffer;
  std::string path_buffer;
  json update_template;
  std::string get_current_time() {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S");
    return ss.str();
  }
  void on_open(websocketpp::connection_hdl hdl) {
    std::lock_guard<std::mutex> lock(m_connections_mutex);
    m_connections.insert(hdl);
  }

  void on_close(websocketpp::connection_hdl hdl) {
    std::lock_guard<std::mutex> lock(m_connections_mutex);
    m_connections.erase(hdl);
  }

  void on_message(websocketpp::connection_hdl hdl, server::message_ptr msg) {
    process_message(hdl, msg);
  }

  void process_message(websocketpp::connection_hdl hdl,
                       server::message_ptr msg) {
    try {
      std::string payload = msg->get_payload();
      json j = json::parse(payload);
      std::string message_type = j["type"];

      if (message_type == "echo") {
        // Echo the message back to the client
        // needed for benchmarking
        m_server.send(hdl, payload, msg->get_opcode());
      } else if (message_type == "get_instruments") {
        std::string currency = j["currency"];
        std::string kind = j["kind"];
        std::string instruments_response = fetch_instruments(currency, kind);
        m_server.send(hdl, instruments_response, msg->get_opcode());
      } else if (message_type == "modify_order") {
        std::string modify_response = process_modify_order(payload);
        m_server.send(hdl, modify_response, msg->get_opcode());
        broadcast_open_orders_update();
      } else if (message_type == "cancel_order") {
        std::string cancel_response = process_cancel_order(payload);
        m_server.send(hdl, cancel_response, msg->get_opcode());
        broadcast_open_orders_update();
      }
      if (message_type == "place_order") {
        std::string order_response = process_order(payload);
        m_server.send(hdl, order_response, msg->get_opcode());
        broadcast_open_orders_update();
      }
    } catch (const std::exception &e) {
      m_server.send(hdl, "Internal server error", msg->get_opcode());
    }
  }

  void fetch_instruments() {
    httplib::SSLClient cli("test.deribit.com");
    cli.set_connection_timeout(5);
    cli.set_read_timeout(5);

    auto res =
        cli.Get("/api/v2/public/get_instruments?currency=BTC&kind=future");

    if (res && res->status == 200) {
      json response = json::parse(res->body);
      m_supported_instruments.clear();
      for (const auto &instrument : response["result"]) {
        m_supported_instruments.push_back(
            instrument["instrument_name"].get<std::string>());
      }
    }
  }

  std::string fetch_instruments(const std::string &currency,
                                const std::string &kind) {
    httplib::SSLClient cli("test.deribit.com");
    cli.set_connection_timeout(5);
    cli.set_read_timeout(5);

    std::string path =
        "/api/v2/public/get_instruments?currency=" + currency + "&kind=" + kind;
    auto res = cli.Get(path.c_str());

    if (res && res->status == 200) {
      json response = json::parse(res->body);
      response["type"] = "instruments";
      return response.dump();
    } else {
      return json{{"type", "instruments"},
                  {"error", "Failed to fetch instruments"}}
          .dump();
    }
  }

  std::string get_access_token() {
    static std::string access_token;
    static std::chrono::steady_clock::time_point token_expiry;

    if (access_token.empty() ||
        std::chrono::steady_clock::now() >= token_expiry) {
      httplib::SSLClient cli("test.deribit.com");
      cli.set_connection_timeout(5);
      cli.set_read_timeout(5);

      const char *client_id = CLIENT_ID;
      const char *client_secret = CLIENT_SECRET;

      json auth_request = {{"jsonrpc", "2.0"},
                           {"id", 9929},
                           {"method", "public/auth"},
                           {"params",
                            {{"grant_type", "client_credentials"},
                             {"client_id", client_id},
                             {"client_secret", client_secret}}}};

      auto res = cli.Post("/api/v2/public/auth", auth_request.dump(),
                          "application/json");

      if (res && res->status == 200) {
        json response = json::parse(res->body);
        access_token = response["result"]["access_token"].get<std::string>();
        token_expiry =
            std::chrono::steady_clock::now() + std::chrono::minutes(15);
      } else {
        throw std::runtime_error("Failed to obtain access token");
      }
    }

    return access_token;
  }

  std::string process_order(const std::string &payload) {
    static const std::unordered_set<std::string> requiredFields = {
        "instrument_name", "amount", "type", "direction"};
    static httplib::SSLClient cli("test.deribit.com");
    static std::once_flag cli_init_flag;

    try {
      const json order = json::parse(payload);

      if (!order.contains("data")) {
        return R"({"type": "order_response", "error": "Invalid order format: 'data' field missing"})";
      }

      const auto &orderData = order["data"];

      // Check for required fields
      for (const auto &field : requiredFields) {
        if (!orderData.contains(field)) {
          return R"({"type": "order_response", "error": "Missing required field: )" +
                 field + R"("})";
        }
      }

      // Initialize client once
      std::call_once(cli_init_flag, [&]() {
        cli.set_connection_timeout(20);
        cli.set_read_timeout(20);
      });

      json request_body = {{"jsonrpc", "2.0"},
                           {"id", 5275},
                           {"method", orderData["direction"] == "buy"
                                          ? "private/buy"
                                          : "private/sell"},
                           {"params",
                            {{"instrument_name", orderData["instrument_name"]},
                             {"amount", orderData["amount"]},
                             {"type", orderData["type"]},
                             {"label", "ui_order"}}}};

      // Add price only for limit orders
      if (orderData["type"] == "limit") {
        if (!orderData.contains("price")) {
          return R"({"type": "order_response", "error": "Price is required for limit orders"})";
        }
        request_body["params"]["price"] = orderData["price"];
      }

      const std::string access_token = get_access_token();
      httplib::Headers headers = {{"Authorization", "Bearer " + access_token}};

      auto res = cli.Post("/api/v2/private/buy", headers, request_body.dump(),
                          "application/json");

      if (res) {
        if (res->status == 200) {
          json response = json::parse(res->body);
          response["type"] = "order_response";
          return response.dump();
        } else {
          return R"({"type": "order_response", "error": "Failed to process order: )" +
                 res->body + R"("})";
        }
      } else {
        return R"({"type": "order_response", "error": "No response from Deribit API"})";
      }
    } catch (const std::exception &e) {
      return R"({"type": "order_response", "error": "Error processing order: )" +
             std::string(e.what()) + R"("})";
    }
  }

  std::string process_modify_order(const std::string &payload) {
    try {
      json request = json::parse(payload);

      if (!request.contains("data")) {
        return json{{"type", "modify_response"},
                    {"error", "Invalid request format: 'data' field missing"}}
            .dump();
      }

      json orderData = request["data"];

      // Check for required fields
      std::vector<std::string> requiredFields = {"order_id", "amount"};
      for (const auto &field : requiredFields) {
        if (!orderData.contains(field)) {
          return json{{"type", "modify_response"},
                      {"error", "Missing required field: " + field}}
              .dump();
        }
      }

      std::string access_token = get_access_token();

      static httplib::SSLClient cli("test.deribit.com");
      cli.set_connection_timeout(5);
      cli.set_read_timeout(5);

      json api_request = {{"jsonrpc", "2.0"},
                          {"id", 123},
                          {"method", "private/edit"},
                          {"params",
                           {{"order_id", orderData["order_id"]},
                            {"amount", orderData["amount"]}}}};

      // Add optional parameters if present
      if (orderData.contains("price")) {
        api_request["params"]["price"] = orderData["price"];
      }
      if (orderData.contains("post_only")) {
        api_request["params"]["post_only"] = orderData["post_only"];
      }
      if (orderData.contains("reduce_only")) {
        api_request["params"]["reduce_only"] = orderData["reduce_only"];
      }

      httplib::Headers headers = {{"Authorization", "Bearer " + access_token}};

      auto res = cli.Post("/api/v2/private/edit", headers, api_request.dump(),
                          "application/json");

      if (res && res->status == 200) {
        json response = json::parse(res->body);
        response["type"] = "modify_response";
        return response.dump();
      } else {
        std::string error_msg =
            res ? "HTTP Error: " + std::to_string(res->status)
                : "Failed to send request";
        return json{{"type", "modify_response"}, {"error", error_msg}}.dump();
      }
    } catch (const std::exception &e) {
      return json{
          {"type", "modify_response"},
          {"error", std::string("Error processing modify order: ") + e.what()}}
          .dump();
    }
  }

  std::string process_cancel_order(const std::string &payload) {
    try {
      json request = json::parse(payload);

      if (!request.contains("data") || !request["data"].contains("order_id")) {
        return json{
            {"type", "cancel_response"},
            {"error", "Invalid request format: 'order_id' field missing"}}
            .dump();
      }

      std::string order_id = request["data"]["order_id"];

      std::string access_token = get_access_token();

      static httplib::SSLClient cli("test.deribit.com");
      cli.set_connection_timeout(5);
      cli.set_read_timeout(5);

      json api_request = {{"jsonrpc", "2.0"},
                          {"id", 123},
                          {"method", "private/cancel"},
                          {"params", {{"order_id", order_id}}}};

      httplib::Headers headers = {{"Authorization", "Bearer " + access_token}};

      auto res = cli.Post("/api/v2/private/cancel", headers, api_request.dump(),
                          "application/json");

      if (res && res->status == 200) {
        json response = json::parse(res->body);
        response["type"] = "cancel_response";
        return response.dump();
      } else {
        std::string error_msg =
            res ? "HTTP Error: " + std::to_string(res->status)
                : "Failed to send request";
        return json{{"type", "cancel_response"}, {"error", error_msg}}.dump();
      }
    } catch (const std::exception &e) {
      return json{
          {"type", "cancel_response"},
          {"error", std::string("Error processing cancel order: ") + e.what()}}
          .dump();
    }
  }

  void orderbook_update_loop() {
    const int depth = 20;
    auto last_update = std::chrono::steady_clock::now();
    const auto update_interval = std::chrono::microseconds(25000); // 25ms
    const auto sleep_interval = std::chrono::microseconds(100);    // 100μs

    // Configure HTTP client
    m_http_client.set_connection_timeout(2);
    m_http_client.set_read_timeout(1);
    m_http_client.set_keep_alive(true);

    // Pre-allocate buffers
    response_buffer.reserve(64 * 1024); // 64KB buffer
    path_buffer.reserve(256);           // 256B path buffer

    const std::string path_prefix =
        "/api/v2/public/get_order_book?instrument_name=";
    const std::string depth_str = "&depth=" + std::to_string(depth);

    // Initialize update template
    update_template = {{"type", "orderbook_update"},
                       {"instrument", ""},
                       {"timestamp", 0},
                       {"data", nullptr}};

    while (!m_done) {
      try {
        auto now = std::chrono::steady_clock::now();
        if (now - last_update < update_interval) {
          std::this_thread::sleep_for(sleep_interval);
          continue;
        }

        if (m_connections.empty()) {
          last_update = now;
          std::this_thread::sleep_for(update_interval);
          continue;
        }

        // Take timestamp at start of cycle
        auto cycle_start = std::chrono::system_clock::now();
        auto timestamp = cycle_start.time_since_epoch().count();

        for (const auto &instrument : m_supported_instruments) {
          try {
            // Construct path using pre-allocated buffer
            path_buffer.clear();
            path_buffer += path_prefix;
            path_buffer += instrument;
            path_buffer += depth_str;

            auto res = m_http_client.Get(path_buffer.c_str());

            if (res && res->status == 200) {
              // Quick validation
              if (res->body[0] != '{')
                continue;

              json orderbook = json::parse(res->body);

              if (!orderbook.contains("error") &&
                  orderbook.contains("result")) {
                update_template["instrument"] = instrument;
                update_template["timestamp"] = timestamp;
                update_template["data"] = std::move(orderbook["result"]);

                // Use pre-allocated buffer for serialization
                response_buffer.clear();
                auto json_str = update_template.dump();
                broadcast(json_str);
              }
            }
          } catch (const std::exception &e) {
            std::cerr << "Error processing " << instrument << ": " << e.what()
                      << '\n';
          }
        }

        last_update = now;
      } catch (const std::exception &e) {
        std::cerr << "Error in update loop: " << e.what() << '\n';
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }
  }

  void positions_update_loop() {
    while (!m_done) {
      std::string access_token = get_access_token();

      std::vector<std::string> currencies = {"BTC", "ETH"};
      std::vector<std::string> kinds = {"future", "option"};

      for (const auto &currency : currencies) {
        for (const auto &kind : kinds) {
          static httplib::SSLClient cli("test.deribit.com");
          cli.set_connection_timeout(5);
          cli.set_read_timeout(1);

          json request_body = {
              {"jsonrpc", "2.0"},
              {"id", 2236},
              {"method", "private/get_positions"},
              {"params", {{"currency", currency}, {"kind", kind}}}};

          httplib::Headers headers = {
              {"Authorization", "Bearer " + access_token}};

          auto res = cli.Post("/api/v2/private/get_positions", headers,
                              request_body.dump(), "application/json");

          if (res && res->status == 200) {
            json response = json::parse(res->body);
            if (response.contains("result")) {
              json update = {{"type", "positions_update"},
                             {"currency", currency},
                             {"kind", kind},
                             {"data", response["result"]}};
              broadcast(update.dump());
            }
          }

          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
      }

      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }

  void open_orders_update_loop() {
    while (!m_done) {
      std::string open_orders = get_open_orders();

      json update = {{"type", "open_orders_update"},
                     {"data", json::parse(open_orders)}};

      broadcast(update.dump());

      std::this_thread::sleep_for(std::chrono::seconds(10));
    }
  }

  std::string get_open_orders() {
    std::string access_token = get_access_token();

    static httplib::SSLClient cli("test.deribit.com");
    cli.set_connection_timeout(5);
    cli.set_read_timeout(5);

    json api_request = {{"jsonrpc", "2.0"},
                        {"id", 124},
                        {"method", "private/get_open_orders_by_currency"},
                        {"params", {{"currency", "BTC"}}}};

    httplib::Headers headers = {{"Authorization", "Bearer " + access_token}};

    auto res = cli.Post("/api/v2/private/get_open_orders_by_currency", headers,
                        api_request.dump(), "application/json");

    if (res && res->status == 200) {
      return res->body;
    } else {
      std::string error_msg = res ? "HTTP Error: " + std::to_string(res->status)
                                  : "Failed to send request";
      return json({{"error", error_msg}}).dump();
    }
  }

  void broadcast_open_orders_update() {
    std::string open_orders = get_open_orders();
    json update = {{"type", "open_orders_update"},
                   {"data", json::parse(open_orders)}};
    broadcast(update.dump());
  }

  void broadcast(const std::string &message) {
    std::lock_guard<std::mutex> lock(m_connections_mutex);
    for (auto it : m_connections) {
      m_server.send(it, message, websocketpp::frame::opcode::text);
    }
  }

  std::string fetch_orderbook(const std::string &instrument, int depth) {
    static httplib::SSLClient cli("test.deribit.com");
    cli.set_connection_timeout(5);
    cli.set_read_timeout(1);

    std::vector<int> valid_depths = {1, 5, 10, 20, 50, 100, 1000, 10000};
    auto it = std::lower_bound(valid_depths.begin(), valid_depths.end(), depth);
    if (it == valid_depths.end() || *it != depth) {
      depth = (it == valid_depths.end()) ? valid_depths.back() : *it;
    }

    std::string path =
        "/api/v2/public/get_order_book?instrument_name=" + instrument +
        "&depth=" + std::to_string(depth);
    auto res = cli.Get(path.c_str());

    if (res && res->status == 200) {
      return res->body;
    } else {
      return json{{"error", "Failed to fetch orderbook data"}}.dump();
    }
  }

  server m_server;
  con_list m_connections;
  std::mutex m_connections_mutex;
  std::atomic<bool> m_done{false};
  std::vector<std::string> m_supported_instruments;
};

int main() {
  try {
    websocket_server server;
    std::cout << "Server started at PORT 9002";
    server.run(9002);
  } catch (const std::exception &e) {
    std::cerr << "Fatal error: " << e.what() << std::endl;
    return 1;
  } catch (...) {
    std::cerr << "Unknown fatal error occurred." << std::endl;
    return 1;
  }
  return 0;
}
