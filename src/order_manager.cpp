#include "order_manager.h"
// order_manager: 负责管理下单请求（paper/live），调用 BinanceAPI 执行签名请求并处理重试
#include "binance_api.h"
#include <iostream>
#include <thread>
#include <chrono>

OrderManager::OrderManager() {}
OrderManager::~OrderManager() {}

void OrderManager::set_mode(const std::string &mode, const std::string &api_key, const std::string &secret) {
    mode_ = mode;
    api_key_ = api_key;
    secret_ = secret;
    if(mode_ == "live") {
        binance_.set_credentials(api_key_, secret_);
        // Attempt to sync time
        auto st = binance_.get_server_time();
        if(st) {
            std::cout << "[OrderManager] Binance server time: " << *st << "\n";
        }
    }
}

std::optional<OrderManager::Order> OrderManager::place_order(const std::string &symbol, const std::string &side, double qty, double price) {
    Order o; o.symbol = symbol; o.price = price; o.quantity = qty; o.side = side; o.status = "NEW";
    if(mode_ == "paper") {
        std::cout << "[OrderManager] paper order: " << symbol << " " << side << " " << qty << "@" << price << "\n";
        return o;
    }

    // live mode: try to place order (LIMIT for now), with simple retry
    std::string params = "symbol=" + symbol + "&side=" + side + "&type=LIMIT&quantity=" + std::to_string(qty) + "&price=" + std::to_string(price) + "&timeInForce=GTC";
    int attempts = 0;
    while(attempts < 3) {
        auto resp = binance_.send_signed_request("POST", "/fapi/v1/order", params);
        if(!resp) {
            std::cerr << "[OrderManager] order attempt failed (no response), retrying...\n";
            attempts++;
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }
        if(resp->http_code >= 200 && resp->http_code < 300) {
            o.status = "PLACED";
            std::cout << "[OrderManager] order placed: " << resp->body << "\n";
            return o;
        } else {
            std::cerr << "[OrderManager] order failed: " << resp->http_code << " " << resp->body << "\n";
            attempts++;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    return std::nullopt;
}
