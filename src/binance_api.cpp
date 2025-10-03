#include "binance_api.h"
// binance_api: 币安 REST API 简单实现，包含签名与时间获取
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <curl/curl.h>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <chrono>
using json = nlohmann::json;
#include <thread>

static size_t write_cb(void* ptr, size_t size, size_t nmemb, void* userdata) {
    std::string *s = static_cast<std::string*>(userdata);
    s->append(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

void BinanceAPI::set_credentials(const std::string &api_key, const std::string &secret) {
    api_key_ = api_key;
    secret_ = secret;
}

std::optional<long long> BinanceAPI::get_server_time() {
    CURL *curl = curl_easy_init();
    if(!curl) return std::nullopt;
    std::string url = base_url_ + "/fapi/v1/time";
    std::string resp;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if(res != CURLE_OK) return std::nullopt;
    try {
        auto j = json::parse(resp);
        if(j.contains("serverTime")) return j["serverTime"].get<long long>();
    } catch(...) { return std::nullopt; }
    return std::nullopt;
}

std::optional<BinanceResponse> BinanceAPI::send_signed_request(const std::string &method, const std::string &path, const std::string &params) {
    if(api_key_.empty() || secret_.empty()) return std::nullopt;
    // simple rate limit
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    if(last_request_ts_ != 0) {
        long long diff = now - last_request_ts_;
        if(diff < min_interval_ms_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(min_interval_ms_ - diff));
        }
    }
    // timestamp add
    long long ts = (long long)(time(nullptr) * 1000);
    std::string payload = params + "&timestamp=" + std::to_string(ts);
    unsigned char result[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    HMAC(EVP_sha256(), secret_.c_str(), secret_.size(), (unsigned char*)payload.c_str(), payload.size(), result, &len);
    std::ostringstream hex;
    for(unsigned int i=0;i<len;i++) hex << std::hex << std::setw(2) << std::setfill('0') << (int)result[i];
    std::string signature = hex.str();
    std::string url = base_url_ + path + "?" + payload + "&signature=" + signature;

    CURL *curl = curl_easy_init();
    if(!curl) return std::nullopt;
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, std::string("X-MBX-APIKEY: " + api_key_).c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    if(method == "POST") curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    std::string resp;
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    BinanceResponse br{http_code, resp};
    last_request_ts_ = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return br;
}
