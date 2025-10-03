// WebSocketClient: WebSocket 客户端抽象，当前实现基于 Boost.Beast 支持 wss://
#pragma once

#include <string>
#include <functional>
#include <memory>

// WebSocket 客户端接口
class WebSocketClient {
public:
    using on_msg_t = std::function<void(const std::string&)>;

    WebSocketClient();
    ~WebSocketClient();

    // Non-blocking connect (returns immediately). on_open/on_error can be handled via callbacks.
    bool connect(const std::string &url);
    void close();

    // Send text frame
    bool send(const std::string &msg);

    // Set message handler callback
    void set_on_message(on_msg_t cb);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
