#include "http_client.h"
// main: 启动程序，加载配置，初始化 UI，Trader，OrderManager 和 WebSocket 客户端
#include "trader.h"
#include "console_ui.h"
#include "websocket_client.h"
#include <iostream>
#include <thread>
#include <vector>
#include <ctime>
#include <nlohmann/json.hpp>
#include <fstream>
#include "order_manager.h"
#include "data_publisher.h"
#include "webconsole_server.h"
#include <cstdlib>
#include "logger.h"
#include <csignal>
#include <atomic>
#include <unistd.h>

using json = nlohmann::json;

static std::atomic<bool> g_running{true};

void handle_sig(int) {
    g_running = false;
}

int main(int argc, char **argv) {
    ConsoleUI ui;
    Trader trader;
    std::string proxy_url;
    // local structured status state (used to emit JSON state lines)
    std::string g_status_str = "(none)";
    std::string g_conn_str = "(none)";
    std::string g_subs_str = "";
    std::string g_orders_str = "";

    auto emit_state = [&]() {
        try {
            json st;
            st["ts"] = (double)time(nullptr);
            st["status"] = g_status_str;
            st["connection"] = g_conn_str;
            std::vector<std::string> subs;
            if(!g_subs_str.empty()) {
                size_t pos = 0; size_t next;
                while((next = g_subs_str.find(", ", pos)) != std::string::npos) {
                    subs.push_back(g_subs_str.substr(pos, next-pos));
                    pos = next+2;
                }
                if(pos < g_subs_str.size()) subs.push_back(g_subs_str.substr(pos));
            }
            st["subscriptions"] = subs;
            st["orders"] = g_orders_str;
            std::string line = std::string("STATE_JSON ") + st.dump();
            Logger::info(line);
        } catch(...) {}
    };
    // Check env HTTP(S)_PROXY first
    const char *env_http = std::getenv("HTTP_PROXY");
    const char *env_https = std::getenv("HTTPS_PROXY");
    if(env_http) proxy_url = env_http;
    else if(env_https) proxy_url = env_https;

    HttpClient http(proxy_url);

    // Read config
    std::string cfgpath = "config/config.json";
    std::ifstream f(cfgpath);
    if(!f) {
        Logger::warn(std::string("Config file not found: ") + cfgpath);
    } else {
        try {
            json cfg; f >> cfg;
            ui.set_status("Config loaded");
               g_status_str = "Config loaded";
            // read proxy settings if env not set
            if(proxy_url.empty() && cfg.contains("proxy")) {
                auto &p = cfg["proxy"];
                if(p.contains("http") && p["http"].is_string()) proxy_url = p["http"].get<std::string>();
                else if(p.contains("https") && p["https"].is_string()) proxy_url = p["https"].get<std::string>();
            }
        } catch(...) {
            Logger::error("Failed to parse config");
        }
    }
    // 如果设置了 NO_UI 环境变量，跳过控制台 UI（用于 systemd/headless 运行）
    const char *no_ui_env = std::getenv("NO_UI");
    bool no_ui = false;
    if(no_ui_env && std::string(no_ui_env) != "0") {
        no_ui = true;
    }
    // 只有在非 headless 情况下初始化 ncurses UI
    if(!no_ui) {
        ui.start();
    }
    trader.start();

    // Load symbols from config/symbols.json
    std::ifstream symf("config/symbols.json");
    if(symf) {
        try {
            json s; symf >> s;
            if(s.contains("symbols") && s["symbols"].is_array()) {
                std::string list;
                for(auto &it : s["symbols"]) {
                    if(it.is_string()) {
                        if(!list.empty()) list += ", ";
                        list += it.get<std::string>();
                    }
                }
                ui.set_subscriptions(list);
                   g_subs_str = list;
                   emit_state();
            }
        } catch(...) {}
    }

    OrderManager om;
    // prefer environment variables for secrets
    const char *api_key = std::getenv("BINANCE_API_KEY");
    const char *api_secret = std::getenv("BINANCE_API_SECRET");
    if(!api_key || !api_secret) {
        // try local secrets file
        std::ifstream sfile("config/secrets.json");
        if(sfile) {
            try {
                json sec; sfile >> sec;
                if(!api_key && sec.contains("BINANCE_API_KEY")) api_key = sec["BINANCE_API_KEY"].get<std::string>().c_str();
                if(!api_secret && sec.contains("BINANCE_API_SECRET")) api_secret = sec["BINANCE_API_SECRET"].get<std::string>().c_str();
            } catch(...) {}
        }
    }

    // Allow explicit override via TRADING_MODE env var: "paper" or "live"
    const char *trading_mode_env = std::getenv("TRADING_MODE");
    if(trading_mode_env) {
        std::string tm(trading_mode_env);
        if(tm == "live") {
            // live requested; only enable if keys are present
            if(api_key && api_secret) {
                om.set_mode("live", api_key, api_secret);
                ui.set_connection_status("OrderManager: live (forced)");
                   g_conn_str = "OrderManager: live (forced)";
            } else {
                // can't run live without keys, fall back to paper
                om.set_mode("paper");
                ui.set_connection_status("OrderManager: paper (fallback, missing keys)");
                   g_conn_str = "OrderManager: paper (fallback, missing keys)";
            }
        } else {
            // any other value => paper
            om.set_mode("paper");
            ui.set_connection_status("OrderManager: paper (forced)");
               g_conn_str = "OrderManager: paper (forced)";
        }
    } else {
        // default behavior: detect by presence of API keys
        if(api_key && api_secret) {
            om.set_mode("live", api_key, api_secret);
            ui.set_connection_status("OrderManager: live");
               g_conn_str = "OrderManager: live";
        } else {
            om.set_mode("paper");
            ui.set_connection_status("OrderManager: paper");
               g_conn_str = "OrderManager: paper";
        }
    }

    // Example: use WebSocket client to subscribe to mark price stream (placeholder)
    WebSocketClient ws;
    ws.set_on_message([&](const std::string &m){
        ui.set_status("WS msg: " + m);
    });
    // initialize optional file logging when running interactively
    // initialize optional file logging when running interactively or when LOG_TO_FILE=1
    const char *log_to_file_env = std::getenv("LOG_TO_FILE");
    if(isatty(STDOUT_FILENO) || (log_to_file_env && std::string(log_to_file_env) == "1")) {
        // ensure logs directory exists (best-effort)
        system("mkdir -p logs >/dev/null 2>&1");
        Logger::init_file("logs/runtime.log");
    }

    // Connect with exponential backoff retry; parameters can be overridden in config/config.json
    const std::string ws_url = "wss://fstream.binance.com/ws/btcusdt@markPrice";
    int attempt = 0;
    int backoff_ms = 500; // default initial
    int max_attempts = 10;
    int max_backoff_ms = 60000;
    int period_after_max_seconds = 30;
    // try read from config
    try {
        std::ifstream cfgin("config/config.json");
        if(cfgin) {
            json cfg; cfgin >> cfg;
            if(cfg.contains("ws") && cfg["ws"].contains("reconnect")) {
                auto &r = cfg["ws"]["reconnect"];
                if(r.contains("initial_backoff_ms")) backoff_ms = r["initial_backoff_ms"].get<int>();
                if(r.contains("max_backoff_ms")) max_backoff_ms = r["max_backoff_ms"].get<int>();
                if(r.contains("max_attempts")) max_attempts = r["max_attempts"].get<int>();
                if(r.contains("period_after_max_seconds")) period_after_max_seconds = r["period_after_max_seconds"].get<int>();
            }
        }
    } catch(...) {
        Logger::warn("Failed to read WS reconnect params from config, using defaults");
    }

    while(g_running) {
        if(ws.connect(ws_url)) {
            Logger::info(std::string("WS connected: ") + ws_url);
            g_status_str = "WS connected";
            emit_state();
            break;
        }
        attempt++;
        if(attempt >= max_attempts) {
            Logger::error("Max WS connect attempts reached, will keep trying periodically");
            std::this_thread::sleep_for(std::chrono::seconds(period_after_max_seconds));
            continue;
        }
        Logger::warn(std::string("WS connect failed, retrying in ") + std::to_string(backoff_ms) + "ms");
        std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
        backoff_ms = std::min(backoff_ms * 2, max_backoff_ms);
    }

    // Install signal handlers for graceful shutdown
    std::signal(SIGTERM, handle_sig);
    std::signal(SIGINT, handle_sig);

    // periodic state emitter thread (every 5s)
    std::thread([&](){
        while(g_running) {
            emit_state();
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }).detach();

    // Start data publisher (background fetch + aggregation + write to data/latest)
    DataPublisher dp(proxy_url);
    dp.start();

    // Start minimal webconsole server (serve precomputed files and static frontend)
    WebConsoleServer wcs(8080, "webconsole");
    if(wcs.start()) {
        Logger::info("WebConsoleServer started on port 8080");
    } else {
        Logger::warn("Failed to start WebConsoleServer");
    }

    // Keep running until signalled
    Logger::info("Entering main loop");
    while(g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    Logger::info("Shutdown requested, closing WebSocket and exiting");
    try {
        ws.close();
    } catch(...) {}
    // stop publisher
    dp.stop();
    // stop web server
    wcs.stop();

    return 0;
}
