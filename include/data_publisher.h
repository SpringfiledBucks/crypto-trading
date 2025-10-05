// DataPublisher: periodically fetch klines (1m) and compute aggregations and indicators
#pragma once

#include <string>
#include <vector>
#include <thread>
#include <atomic>

class DataPublisher {
public:
    DataPublisher(const std::string &proxy = "");
    ~DataPublisher();

    void start();
    void stop();

private:
    std::string proxy_;
    std::atomic<bool> running_{false};
    std::thread worker_;

    void run_loop();
};
