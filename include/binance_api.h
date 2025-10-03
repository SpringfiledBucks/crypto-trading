// BinanceAPI: 币安 REST API 简单封装，支持获取服务器时间与发送签名请求（HMAC-SHA256）。
#pragma once

#include <string>
#include <optional>
#include <map>

// 简单响应结构
struct BinanceResponse {
    long http_code;
    std::string body;
};

class BinanceAPI {
public:
    BinanceAPI() = default;
    // optional rate limit (ms) between signed requests
    void set_min_interval_ms(long long ms) { min_interval_ms_ = ms; }
    // Set credentials
    void set_credentials(const std::string &api_key, const std::string &secret);

    // GET /fapi/v1/time -> serverTime in ms
    std::optional<long long> get_server_time();

    // Send signed request (method: GET/POST), path e.g. /fapi/v1/order, params: query string without timestamp/signature
    std::optional<BinanceResponse> send_signed_request(const std::string &method, const std::string &path, const std::string &params);

private:
    std::string api_key_;
    std::string secret_;
    std::string base_url_ = "https://fapi.binance.com";
    long long last_request_ts_{0};
    long long min_interval_ms_{100};
};
