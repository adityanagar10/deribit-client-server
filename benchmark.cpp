#include <atomic>
#include <benchmark/benchmark.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <nlohmann/json.hpp>
#include <random>
#include <thread>
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_no_tls.hpp>

using json = nlohmann::json;
typedef websocketpp::client<websocketpp::config::asio> client;

class performancebenchmark {
public:
  static void measuremarketdatalatency(benchmark::state &state) {
    client ws_client;
    ws_client.clear_access_channels(websocketpp::log::alevel::all);
    ws_client.set_access_channels(websocketpp::log::alevel::connect);
    ws_client.set_access_channels(websocketpp::log::alevel::disconnect);
    ws_client.set_access_channels(websocketpp::log::alevel::app);

    ws_client.init_asio();

    std::atomic<bool> connected{false};
    std::atomic<bool> messagereceived{false};
    std::mutex mtx;
    std::condition_variable cv;
    websocketpp::connection_hdl connection_hdl;
    client::connection_ptr con;

    // create connection
    websocketpp::lib::error_code ec;
    con = ws_client.get_connection("ws://localhost:9002", ec);
    if (ec) {
      state.skipwitherror(std::string("connection creation failed: ")
                              .append(ec.message())
                              .c_str());
      return;
    }

    // set up handlers before connecting
    con->set_open_handler([&](websocketpp::connection_hdl hdl) {
      std::cout << "connection established for market data" << std::endl;
      connection_hdl = hdl;
      connected = true;
      cv.notify_one();
    });

    con->set_message_handler(
        [&](websocketpp::connection_hdl, client::message_ptr msg) {
          try {
            auto payload = msg->get_payload();
            json response = json::parse(payload);
            if (response["type"] == "orderbook_update") {
              messagereceived = true;
              cv.notify_one();
            }
          } catch (const json::exception &e) {
          } catch (const std::exception &e) {
          }
        });

    // connect and start websocket thread
    ws_client.connect(con);
    std::thread ws_thread([&ws_client]() {
      try {
        ws_client.run();
      } catch (const std::exception &e) {
      }
    });

    // wait for initial connection with longer timeout
    {
      std::unique_lock<std::mutex> lock(mtx);
      if (!cv.wait_for(lock, std::chrono::seconds(10),
                       [&] { return connected.load(); })) {
        ws_client.stop();
        if (ws_thread.joinable())
          ws_thread.join();
        return;
      }
    }

    // subscribe to orderbook updates
    try {
      json subscribe = {{"type", "subscribe"},
                        {"channel", "orderbook"},
                        {"instrument_name", "btc-perpetual"}};
      ws_client.send(connection_hdl, subscribe.dump(),
                     websocketpp::frame::opcode::text);
      std::cout << "market data subscription message sent" << std::endl;
    } catch (const std::exception &e) {
      if (ws_thread.joinable())
        ws_thread.join();
      return;
    }

    // wait longer for subscription to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // benchmark loop
    for (auto _ : state) {
      messagereceived = false;
      auto start = std::chrono::high_resolution_clock::now();

      {
        std::unique_lock<std::mutex> lock(mtx);
        if (!cv.wait_for(lock, std::chrono::seconds(5),
                         [&] { return messagereceived.load(); })) {
          continue; // skip this iteration but continue the benchmark
        }
      }

      auto end = std::chrono::high_resolution_clock::now();
      auto elapsed_seconds =
          std::chrono::duration_cast<std::chrono::duration<double>>(end - start)
              .count();
      state.setiterationtime(elapsed_seconds);
    }

    // cleanup
    try {
      if (connected) {
        ws_client.close(connection_hdl, websocketpp::close::status::normal,
                        "market data benchmark complete");
      }
      ws_client.stop();
      if (ws_thread.joinable()) {
        ws_thread.join();
      }
    } catch (const std::exception &e) {
      std::cerr << "market data cleanup error: " << e.what() << std::endl;
    }
  }

  static void measureorderplacementlatency(benchmark::state &state) {
    client ws_client;
    ws_client.clear_access_channels(websocketpp::log::alevel::all);
    ws_client.set_access_channels(websocketpp::log::alevel::connect);
    ws_client.set_access_channels(websocketpp::log::alevel::disconnect);
    ws_client.set_access_channels(websocketpp::log::alevel::app);

    ws_client.init_asio();

    std::atomic<bool> connected{false};
    std::atomic<bool> orderresponsereceived{false};
    std::mutex mtx;
    std::condition_variable cv;
    websocketpp::connection_hdl connection_hdl;
    client::connection_ptr con;

    std::function<bool()> connect = [&]() -> bool {
      websocketpp::lib::error_code ec;
      con = ws_client.get_connection("ws://localhost:9002", ec);
      if (ec) {
        std::cerr << "connection creation failed: " << ec.message()
                  << std::endl;
        return false;
      }

      con->set_open_handler([&](websocketpp::connection_hdl hdl) {
        std::cout << "connection established for order placement" << std::endl;
        connection_hdl = hdl;
        connected = true;
        cv.notify_one();
      });

      con->set_message_handler([&](websocketpp::connection_hdl,
                                   client::message_ptr msg) {
        try {
          auto payload = msg->get_payload();
          json response = json::parse(payload);

          if (response["type"] == "order_response") {
            orderresponsereceived = true;
            cv.notify_one();
          }
        } catch (const json::exception &e) {
        } catch (const std::exception &e) {
          std::cerr << "error in message handler: " << e.what() << std::endl;
        }
      });

      ws_client.connect(con);
      return true;
    };

    if (!connect()) {
      state.skipwitherror("order placement initial connection failed");
      return;
    }

    std::thread ws_thread([&ws_client]() {
      try {
        ws_client.run();
      } catch (const std::exception &e) {
        std::cerr << "websocket thread error: " << e.what() << std::endl;
      }
    });

    // wait for initial connection
    {
      std::unique_lock<std::mutex> lock(mtx);
      if (!cv.wait_for(lock, std::chrono::seconds(10),
                       [&] { return connected.load(); })) {
        state.skipwitherror("order placement initial connection timeout");
        ws_client.stop();
        if (ws_thread.joinable())
          ws_thread.join();
        return;
      }
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> contracts_dist(1, 10); // 1-10 contracts
    std::uniform_real_distribution<> price_dist(20000.0, 70000.0);
    std::bernoulli_distribution bool_dist(0.5);

    for (auto _ : state) {
      orderresponsereceived = false;

      int contracts = contracts_dist(gen);
      double price =
          std::round(price_dist(gen) * 0.5) / 0.5; // 0.5 tick size for price

      json order = {
          {"type", "place_order"},
          {"data",
           {{"instrument_name", "btc-perpetual"},
            {"amount", contracts * 10}, // convert contracts to usd amount
            {"type", "limit"}, // always use limit orders for consistency
            {"direction", bool_dist(gen) ? "buy" : "sell"},
            {"price", price}}}};

      auto start = std::chrono::high_resolution_clock::now();

      try {
        std::string order_str = order.dump();
        ws_client.send(connection_hdl, order_str,
                       websocketpp::frame::opcode::text);
      } catch (const std::exception &e) {
        state.skipwitherror(
            std::string("order send failed: ").append(e.what()).c_str());
        continue;
      }

      {
        std::unique_lock<std::mutex> lock(mtx);
        if (!cv.wait_for(lock, std::chrono::seconds(10),
                         [&] { return orderresponsereceived.load(); })) {
          continue;
        }
      }

      auto end = std::chrono::high_resolution_clock::now();
      auto elapsed_seconds =
          std::chrono::duration_cast<std::chrono::duration<double>>(end - start)
              .count();
      state.setiterationtime(elapsed_seconds);
    }

    try {
      if (connected) {
        ws_client.close(connection_hdl, websocketpp::close::status::normal,
                        "order placement benchmark complete");
      }
      ws_client.stop();
      if (ws_thread.joinable()) {
        ws_thread.join();
      }
    } catch (const std::exception &e) {
      std::cerr << "order placement cleanup error: " << e.what() << std::endl;
    }
  }

  static void measurewebsocketpropagationdelay(benchmark::state &state) {
    client ws_client;
    ws_client.clear_access_channels(websocketpp::log::alevel::all);
    ws_client.set_access_channels(websocketpp::log::alevel::connect);
    ws_client.set_access_channels(websocketpp::log::alevel::disconnect);
    ws_client.set_access_channels(websocketpp::log::alevel::app);

    ws_client.init_asio();

    std::atomic<bool> connected{false};
    std::atomic<bool> messagereceived{false};
    std::mutex mtx;
    std::condition_variable cv;
    websocketpp::connection_hdl connection_hdl;
    client::connection_ptr con;

    // create connection
    websocketpp::lib::error_code ec;
    con = ws_client.get_connection("ws://localhost:9002", ec);
    if (ec) {
      state.skipwitherror(std::string("connection creation failed: ")
                              .append(ec.message())
                              .c_str());
      return;
    }

    // set up handlers before connecting
    con->set_open_handler([&](websocketpp::connection_hdl hdl) {
      std::cout << "connection established for propagation delay test"
                << std::endl;
      connection_hdl = hdl;
      connected = true;
      cv.notify_one();
    });

    con->set_message_handler(
        [&](websocketpp::connection_hdl, client::message_ptr msg) {
          try {
            auto payload = msg->get_payload();
            json response = json::parse(payload);
            if (response["type"] == "echo") {
              messagereceived = true;
              cv.notify_one();
            }
          } catch (const json::exception &e) {
          } catch (const std::exception &e) {
            std::cerr << "error in message handler: " << e.what() << std::endl;
          }
        });

    // connect and start websocket thread
    ws_client.connect(con);
    std::thread ws_thread([&ws_client]() {
      try {
        ws_client.run();
      } catch (const std::exception &e) {
        std::cerr << "websocket thread error: " << e.what() << std::endl;
      }
    });

    // wait for initial connection
    {
      std::unique_lock<std::mutex> lock(mtx);
      if (!cv.wait_for(lock, std::chrono::seconds(10),
                       [&] { return connected.load(); })) {
        state.skipwitherror("initial connection timeout");
        ws_client.stop();
        if (ws_thread.joinable())
          ws_thread.join();
        return;
      }
    }

    // benchmark loop
    for (auto _ : state) {
      messagereceived = false;
      json echo_message = {{"type", "echo"}, {"data", "hello, server!"}};

      auto start = std::chrono::high_resolution_clock::now();

      try {
        ws_client.send(connection_hdl, echo_message.dump(),
                       websocketpp::frame::opcode::text);
      } catch (const std::exception &e) {
        state.skipwitherror(
            std::string("message send failed: ").append(e.what()).c_str());
        continue;
      }

      {
        std::unique_lock<std::mutex> lock(mtx);
        if (!cv.wait_for(lock, std::chrono::seconds(5),
                         [&] { return messagereceived.load(); })) {
          std::cout << "timeout waiting for echo response" << std::endl;
          continue; // skip this iteration but continue the benchmark
        }
      }

      auto end = std::chrono::high_resolution_clock::now();
      auto elapsed_seconds =
          std::chrono::duration_cast<std::chrono::duration<double>>(end - start)
              .count();
      state.setiterationtime(elapsed_seconds);
    }

    // cleanup
    try {
      if (connected) {
        ws_client.close(connection_hdl, websocketpp::close::status::normal,
                        "propagation delay benchmark complete");
      }
      ws_client.stop();
      if (ws_thread.joinable()) {
        ws_thread.join();
      }
    } catch (const std::exception &e) {
      std::cerr << "propagation delay cleanup error: " << e.what() << std::endl;
    }
  }

  static void measureendtoendtradinglatency(benchmark::state &state) {
    client ws_client;
    ws_client.clear_access_channels(websocketpp::log::alevel::all);
    ws_client.set_access_channels(websocketpp::log::alevel::connect);
    ws_client.set_access_channels(websocketpp::log::alevel::disconnect);
    ws_client.set_access_channels(websocketpp::log::alevel::app);

    ws_client.init_asio();

    std::atomic<bool> connected{false};
    std::atomic<bool> orderresponsereceived{false};
    std::atomic<bool> orderexecuted{false};
    std::mutex mtx;
    std::condition_variable cv;
    websocketpp::connection_hdl connection_hdl;
    client::connection_ptr con;

    std::function<bool()> connect = [&]() -> bool {
      websocketpp::lib::error_code ec;
      con = ws_client.get_connection("ws://localhost:9002", ec);
      if (ec) {
        std::cerr << "connection creation failed: " << ec.message()
                  << std::endl;
        return false;
      }

      con->set_open_handler([&](websocketpp::connection_hdl hdl) {
        std::cout << "connection established for end-to-end trading"
                  << std::endl;
        connection_hdl = hdl;
        connected = true;
        cv.notify_one();
      });

      con->set_message_handler([&](websocketpp::connection_hdl,
                                   client::message_ptr msg) {
        try {
          auto payload = msg->get_payload();
          json response = json::parse(payload);

          if (response["type"] == "order_response") {
            orderresponsereceived = true;
            cv.notify_one();
          } else if (response["type"] == "order_execution") {
            std::cout << "order executed" << std::endl;
            orderexecuted = true;
            cv.notify_one();
          }
        } catch (const json::exception &e) {
        } catch (const std::exception &e) {
          std::cerr << "error in message handler: " << e.what() << std::endl;
        }
      });

      ws_client.connect(con);
      return true;
    };

    if (!connect()) {
      state.skipwitherror("end-to-end trading initial connection failed");
      return;
    }

    std::thread ws_thread([&ws_client]() {
      try {
        ws_client.run();
      } catch (const std::exception &e) {
        std::cerr << "websocket thread error: " << e.what() << std::endl;
      }
    });

    // wait for initial connection
    {
      std::unique_lock<std::mutex> lock(mtx);
      if (!cv.wait_for(lock, std::chrono::seconds(10),
                       [&] { return connected.load(); })) {
        state.skipwitherror("end-to-end trading initial connection timeout");
        ws_client.stop();
        if (ws_thread.joinable())
          ws_thread.join();
        return;
      }
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> contracts_dist(1, 10); // 1-10 contracts
    std::bernoulli_distribution bool_dist(0.5);

    for (auto _ : state) {
      orderresponsereceived = false;
      orderexecuted = false;

      int contracts = contracts_dist(gen);

      json order = {
          {"type", "place_order"},
          {"data",
           {{"instrument_name", "btc-perpetual"},
            {"amount", contracts * 10}, // convert contracts to usd amount
            {"type", "market"},
            {"direction", bool_dist(gen) ? "buy" : "sell"}}}};

      auto start = std::chrono::high_resolution_clock::now();

      try {
        std::string order_str = order.dump();
        ws_client.send(connection_hdl, order_str,
                       websocketpp::frame::opcode::text);
      } catch (const std::exception &e) {
        state.skipwitherror(
            std::string("order send failed: ").append(e.what()).c_str());
        continue;
      }

      {
        std::unique_lock<std::mutex> lock(mtx);
        if (!cv.wait_for(lock, std::chrono::seconds(10),
                         [&] { return orderresponsereceived.load(); })) {
          std::cout << "timeout waiting for order response" << std::endl;
          continue;
        }
      }

      {
        std::unique_lock<std::mutex> lock(mtx);
        if (!cv.wait_for(lock, std::chrono::seconds(5),
                         [&] { return orderexecuted.load(); })) {
          std::cout << "timeout waiting for order execution" << std::endl;
          continue;
        }
      }

      auto end = std::chrono::high_resolution_clock::now();
      auto elapsed_seconds =
          std::chrono::duration_cast<std::chrono::duration<double>>(end - start)
              .count();
      state.setiterationtime(elapsed_seconds);
    }

    try {
      if (connected) {
        ws_client.close(connection_hdl, websocketpp::close::status::normal,
                        "end-to-end trading benchmark complete");
      }
      ws_client.stop();
      if (ws_thread.joinable()) {
        ws_thread.join();
      }
    } catch (const std::exception &e) {
      std::cerr << "end-to-end trading cleanup error: " << e.what()
                << std::endl;
    }
  }
};

benchmark(performancebenchmark::measuremarketdatalatency)
    ->iterations(1)
    ->unit(benchmark::kmillisecond);

benchmark(performancebenchmark::measureorderplacementlatency)
    ->iterations(1)
    ->unit(benchmark::kmillisecond);

benchmark(performancebenchmark::measurewebsocketpropagationdelay)
    ->iterations(100)
    ->unit(benchmark::kmicrosecond);

benchmark(performancebenchmark::measureendtoendtradinglatency)
    ->iterations(1)
    ->unit(benchmark::kmillisecond);

benchmark_main();
