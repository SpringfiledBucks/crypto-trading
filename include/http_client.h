// HttpClient: 简单的 HTTP 客户端封装，使用 libcurl 实现。支持可选代理配置。
#pragma once

#include <string>
#include <optional>

// 简单 HTTP 客户端封装
class HttpClient {
public:
    HttpClient(const std::string &proxy = "");
    ~HttpClient();

    // GET request, returns response body or nullopt on error
    std::optional<std::string> get(const std::string &url);

private:
    std::string proxy_;
};
