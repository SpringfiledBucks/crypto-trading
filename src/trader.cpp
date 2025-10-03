#include "trader.h"
// trader: 简单的交易策略循环骨架，接收 MarketData 并触发策略
#include <thread>
#include <chrono>
#include <iostream>

Trader::Trader() {}
Trader::~Trader() { stop(); }

void Trader::start() {
    if(running.exchange(true)) return;
    std::thread([this]{
        while(running) {
            // Placeholder: trading loop
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }).detach();
}

void Trader::stop() { running = false; }

void Trader::on_market_data(const MarketData &md) {
    std::cout << "MarketData: " << md.symbol << " " << md.price << "\n";
    // Placeholder: simple strategy logic
}
