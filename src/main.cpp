#include "http_client.h"
// main: 启动程序，加载配置，初始化 UI，Trader，OrderManager 和 WebSocket 客户端
#include "trader.h"
#include "console_ui.h"
#include "websocket_client.h"
#include <iostream>
#include <thread>
#include <nlohmann/json.hpp>
#include <fstream>
#include "order_manager.h"
#include <cstdlib>

using json = nlohmann::json;

int main(int argc, char **argv) {
    ConsoleUI ui;
    Trader trader;
    std::string proxy_url;
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
        std::cerr << "Config file not found: " << cfgpath << "\n";
    } else {
        try {
            json cfg; f >> cfg;
            ui.set_status("Config loaded");
            // read proxy settings if env not set
            if(proxy_url.empty() && cfg.contains("proxy")) {
                auto &p = cfg["proxy"];
                if(p.contains("http") && p["http"].is_string()) proxy_url = p["http"].get<std::string>();
                else if(p.contains("https") && p["https"].is_string()) proxy_url = p["https"].get<std::string>();
            }
        } catch(...) {
            std::cerr << "Failed to parse config\n";
        }
    }
    ui.start();
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

    if(api_key && api_secret) {
        om.set_mode("live", api_key, api_secret);
        ui.set_connection_status("OrderManager: live");
    } else {
        om.set_mode("paper");
        ui.set_connection_status("OrderManager: paper");
    }

    // Example: use WebSocket client to subscribe to mark price stream (placeholder)
    WebSocketClient ws;
    ws.set_on_message([&](const std::string &m){
        ui.set_status("WS msg: " + m);
    });
    ws.connect("wss://fstream.binance.com/ws/btcusdt@markPrice");

    // Keep running until UI quits
    while(true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}
