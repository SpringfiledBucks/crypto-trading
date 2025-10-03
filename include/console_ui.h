// ConsoleUI: 基于 ncurses 的控制台监控页面，用于显示服务状态、订阅与订单信息。
#pragma once

#include <string>

// 控制台 UI 接口
class ConsoleUI {
public:
    ConsoleUI();
    ~ConsoleUI();

    void start();
    void stop();

    void set_status(const std::string &s);
    void set_connection_status(const std::string &s);
    void set_subscriptions(const std::string &s);
    void set_orders(const std::string &s);
};
