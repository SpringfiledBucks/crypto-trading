#include "http_client.h"
// http_client: libcurl 封装，支持可选代理
#include <curl/curl.h>
#include <string>
#include <vector>
#include <iostream>

static size_t write_cb(void* ptr, size_t size, size_t nmemb, void* userdata) {
    std::string *s = static_cast<std::string*>(userdata);
    s->append(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

HttpClient::HttpClient(const std::string &proxy): proxy_(proxy) {}
HttpClient::~HttpClient() {}

std::optional<std::string> HttpClient::get(const std::string &url) {
    CURL *curl = curl_easy_init();
    if(!curl) return std::nullopt;
    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    if(!proxy_.empty()) {
        curl_easy_setopt(curl, CURLOPT_PROXY, proxy_.c_str());
    }
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if(res != CURLE_OK) return std::nullopt;
    return response;
}
