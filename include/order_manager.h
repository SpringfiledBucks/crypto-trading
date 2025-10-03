// OrderManager: 下单管理器，封装 paper/live 下单逻辑、签名与重试策略。
#pragma once

#include <string>
#include <optional>
#include "binance_api.h"

// 下单管理器接口
class OrderManager {
public:
    OrderManager();
    ~OrderManager();

    // mode: "paper" or "live"
    void set_mode(const std::string &mode, const std::string &api_key = "", const std::string &secret = "");

    struct Order {
        std::string symbol;
        double price;
        double quantity;
        std::string side; // BUY/SELL
        std::string status;
    };

    // Place an order; in paper mode this only simulates and returns an Order object
    std::optional<Order> place_order(const std::string &symbol, const std::string &side, double qty, double price);

private:
    std::string mode_ = "paper";
    std::string api_key_;
    std::string secret_;
    std::string sign_payload(const std::string &payload) const;
    BinanceAPI binance_;
};
