// Trader: 交易策略骨架，接收行情数据并做决策（开平单动作由 OrderManager 执行）。
#pragma once

#include <string>
#include <functional>
#include <atomic>

// 行情数据结构
struct MarketData {
    std::string symbol;
    double price;
};

class Trader {
public:
    Trader();
    ~Trader();

    void start();
    void stop();

    void on_market_data(const MarketData &md);

    std::atomic<bool> running{false};
};
