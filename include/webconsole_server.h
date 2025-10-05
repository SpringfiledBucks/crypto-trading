#pragma once

#include <string>
#include <thread>
#include <atomic>

class WebConsoleServer {
public:
    WebConsoleServer(unsigned short port = 8080, const std::string &wwwroot = "webconsole");
    ~WebConsoleServer();

    bool start();
    void stop();

private:
    unsigned short port_;
    std::string wwwroot_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    // counters for archive indicator cache usage
    std::atomic<uint64_t> cache_hits_{0};
    std::atomic<uint64_t> cache_misses_{0};

    void run();
};
