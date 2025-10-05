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

    void run();
};
