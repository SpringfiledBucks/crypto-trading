#include "webconsole_server.h"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/signal_set.hpp>
#include "logger.h"
#include "indicators.h"
#include <nlohmann/json.hpp>
#include <thread>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <sstream>
#include <vector>
#include <algorithm>
#include <zlib.h>
#include <ctime>

using tcp = boost::asio::ip::tcp;
namespace http = boost::beast::http;
namespace fs = std::filesystem;
using json = nlohmann::json;

// Simple in-memory aggregation cache to speed repeated requests
struct AggCacheEntry { json payload; std::chrono::steady_clock::time_point ts; };
static std::mutex agg_cache_mutex;
static std::unordered_map<std::string, AggCacheEntry> agg_cache;

WebConsoleServer::WebConsoleServer(unsigned short port, const std::string &wwwroot): port_(port), wwwroot_(wwwroot) {}
WebConsoleServer::~WebConsoleServer() { stop(); }

bool WebConsoleServer::start() {
    if(running_.exchange(true)) return false;
    worker_ = std::thread([this]{ run(); });
    return true;
}

void WebConsoleServer::stop() {
    running_ = false;
    if(worker_.joinable()) worker_.join();
}

static std::string load_file(const fs::path &p) {
    try {
        std::ifstream ifs(p, std::ios::binary);
        if(!ifs) return {};
        std::ostringstream ss; ss << ifs.rdbuf();
        return ss.str();
    } catch(...) { return {}; }
}

static std::string load_gz_file(const fs::path &p){
    try{
        gzFile gz = gzopen(p.string().c_str(), "rb");
        if(!gz) return {};
        std::string out;
        char buf[8192];
        int r;
        while((r = gzread(gz, buf, sizeof(buf))) > 0){
            out.append(buf, buf + r);
        }
        gzclose(gz);
        return out;
    }catch(...){
        return {};
    }
}

// parse simple ISO like YYYY-MM-DD or full YYYY-MM-DDTHH:MM:SSZ -> ms since epoch
static bool parse_iso_to_ms(const std::string &s, int64_t &out_ms){
    std::tm tm{};
    std::istringstream iss(s);
    if(s.find('T') != std::string::npos){
        // expect UTC with trailing Z
        iss.str(s);
        iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
        if(iss.fail()) return false;
    } else {
        iss.str(s);
        iss >> std::get_time(&tm, "%Y-%m-%d");
        if(iss.fail()) return false;
        tm.tm_hour = 0; tm.tm_min = 0; tm.tm_sec = 0;
    }
    tm.tm_isdst = 0;
    // timegm converts tm (UTC) to time_t
    time_t sec = timegm(&tm);
    if(sec == -1) return false;
    out_ms = (int64_t)sec * 1000;
    return true;
}

static std::vector<std::string> months_between(int y1,int m1,int y2,int m2){
    std::vector<std::string> out;
    int y = y1, m = m1;
    while(y < y2 || (y==y2 && m<=m2)){
        char buf[16];
        snprintf(buf, sizeof(buf), "%04d-%02d", y, m);
        out.emplace_back(buf);
        if(m==12){ m=1; y++; } else m++;
    }
    return out;
}

void WebConsoleServer::run() {
    try {
        boost::asio::io_context ioc{1};
        tcp::acceptor acceptor{ioc, {tcp::v4(), port_}};
        while(running_) {
            tcp::socket socket{ioc};
            acceptor.accept(socket);

            std::thread([this, s = std::move(socket)]() mutable {
                try {
                    // disable Nagle to reduce latency for small SSE writes
                    try {
                        s.set_option(tcp::no_delay(true));
                        Logger::info("connection: TCP_NODELAY set");
                    } catch(const std::exception &e){
                        Logger::warn(std::string("failed to set TCP_NODELAY: ")+e.what());
                    }
                    boost::beast::flat_buffer buffer;
                    http::request<http::string_body> req;
                    http::read(s, buffer, req);
                    http::response<http::string_body> res{http::status::ok, req.version()};
                    res.set(http::field::server, "crypto_trading_webconsole_cpp");
                    std::string target = std::string(req.target());

                    auto respond_text = [&](const std::string &body, const std::string &ctype="text/plain", http::status st = http::status::ok){
                        res.result(st);
                        res.set(http::field::content_type, ctype);
                        res.body() = body;
                        res.prepare_payload();
                        http::write(s, res);
                    };

                    // simple query parser
                    auto parse_qs = [](const std::string &t)->std::map<std::string,std::string>{
                        std::map<std::string,std::string> m;
                        auto pos = t.find('?');
                        if(pos==std::string::npos) return m;
                        std::string q = t.substr(pos+1);
                        size_t i=0;
                        while(i<q.size()){
                            auto j = q.find('&', i);
                            if(j==std::string::npos) j = q.size();
                            auto eq = q.find('=', i);
                            if(eq!=std::string::npos && eq<j) {
                                m[q.substr(i, eq-i)] = q.substr(eq+1, j-eq-1);
                            }
                            i = j+1;
                        }
                        return m;
                    };

                    // route handlers (each returns after writing response)
                    if(target == "/" || target.find("/index.html") == 0) {
                        auto body = load_file(fs::path(wwwroot_) / "index.html");
                        if(body.empty()) respond_text("index not found", "text/plain", http::status::not_found);
                        else respond_text(body, "text/html", http::status::ok);
                        s.shutdown(tcp::socket::shutdown_send);
                        return;
                    }

                    if(target.find("/static/") == 0) {
                        fs::path p = fs::path(wwwroot_) / target.substr(1);
                        auto body = load_file(p);
                        if(body.empty()) respond_text("not found", "text/plain", http::status::not_found);
                        else respond_text(body, "application/javascript", http::status::ok);
                        s.shutdown(tcp::socket::shutdown_send);
                        return;
                    }

                    if(target.rfind("/symbols",0) == 0) {
                        auto body = load_file("config/symbols.json");
                        if(body.empty()) respond_text("{}", "application/json", http::status::not_found);
                        else respond_text(body, "application/json", http::status::ok);
                        s.shutdown(tcp::socket::shutdown_send);
                        return;
                    }

                    if(target.rfind("/klines",0) == 0) {
                        Logger::info(std::string("HTTP /klines request: ") + target);
                        auto qp = parse_qs(target);
                        std::string symbol = qp.count("symbol")?qp["symbol"]:"";
                        std::string interval = qp.count("interval")?qp["interval"]:"1m";
                        int limit = qp.count("limit")?std::stoi(qp["limit"]):500;
                        limit = std::max(1, std::min(1000, limit));

                        if(symbol.empty()) { respond_text("{}", "application/json", http::status::bad_request); s.shutdown(tcp::socket::shutdown_send); return; }
                        // if start & end provided, serve merged archive range from archive/klines/<SYM>/YYYY-MM.json.gz
                        if(qp.count("start") && qp.count("end")){
                            int64_t start_ms=0, end_ms=0;
                            if(!parse_iso_to_ms(qp["start"], start_ms) || !parse_iso_to_ms(qp["end"], end_ms)){
                                respond_text("{}", "application/json", http::status::bad_request);
                                s.shutdown(tcp::socket::shutdown_send);
                                return;
                            }
                            // compute month list
                            std::tm tm1{}, tm2{};
                            time_t tsec1 = (time_t)(start_ms/1000);
                            time_t tsec2 = (time_t)(end_ms/1000);
                            gmtime_r(&tsec1, &tm1);
                            gmtime_r(&tsec2, &tm2);
                            auto months = months_between(tm1.tm_year + 1900, tm1.tm_mon + 1, tm2.tm_year + 1900, tm2.tm_mon + 1);
                            std::vector<json> all;
                            fs::path arch = fs::path("archive") / "klines" / symbol;
                            for(auto &m: months){
                                fs::path path = arch / (m + ".json.gz");
                                if(!fs::exists(path)) continue;
                                auto raw = load_gz_file(path);
                                if(raw.empty()) continue;
                                try{
                                    json arr = json::parse(raw);
                                    if(arr.is_array()){
                                        for(auto &it: arr) all.push_back(it);
                                    }
                                } catch(...) { continue; }
                            }
                            // sort by timestamp (element 0)
                            std::sort(all.begin(), all.end(), [](const json &a, const json &b){ return (int64_t)a[0] < (int64_t)b[0]; });
                            // filter by start/end
                            json outarr = json::array();
                            for(auto &k: all){ int64_t ts = (int64_t)k[0]; if(ts >= start_ms && ts <= end_ms) outarr.push_back(k); }
                            if((int)outarr.size() > limit){ json tmp = json::array(); int start_idx = (int)outarr.size() - limit; for(int i=start_idx;i<(int)outarr.size();++i) tmp.push_back(outarr[i]); outarr = std::move(tmp); }

                            // attempt to use precomputed per-month indicators (archive/*.ind.json.gz)
                            int n = (int)outarr.size();
                            std::vector<int64_t> ts_list(n);
                            std::unordered_map<int64_t,int> ts_to_idx;
                            for(int i=0;i<n;++i){ ts_list[i] = (int64_t)outarr[i][0]; ts_to_idx[ts_list[i]] = i; }

                            std::vector<json> sma20_parts(n, nullptr), sma50_parts(n, nullptr), vwap20_parts(n, nullptr);
                            bool all_present = true;
                            // iterate months and try to map precomputed indicators
                            for(auto &m: months){
                                fs::path indp = fs::path("archive") / "klines" / symbol / (m + ".ind.json.gz");
                                fs::path rawp = fs::path("archive") / "klines" / symbol / (m + ".json.gz");
                                if(!fs::exists(indp) || !fs::exists(rawp)) { all_present = false; break; }
                                try{
                                    std::string indraw = load_gz_file(indp);
                                    json indj = json::parse(indraw);
                                    // load raw to align timestamps
                                    std::string raw = load_gz_file(rawp);
                                    json arrm = json::parse(raw);
                                    // for each entry in month, if timestamp in our outarr range, pick indicator value
                                    for(size_t i=0;i<arrm.size();++i){ int64_t ts = (int64_t)arrm[i][0]; auto it = ts_to_idx.find(ts); if(it!=ts_to_idx.end()){
                                                int dst = it->second;
                                                if(indj.contains("sma20") && indj["sma20"].is_array() && (int)indj["sma20"].size() > (int)i) sma20_parts[dst] = indj["sma20"][i];
                                                if(indj.contains("sma50") && indj["sma50"].is_array() && (int)indj["sma50"].size() > (int)i) sma50_parts[dst] = indj["sma50"][i];
                                                if(indj.contains("vwap20") && indj["vwap20"].is_array() && (int)indj["vwap20"].size() > (int)i) vwap20_parts[dst] = indj["vwap20"][i];
                                        }
                                    }
                                } catch(...) { all_present = false; break; }
                            }

                            json indicators;
                            if(all_present){
                                // verify none are null
                                bool missing = false;
                                for(int i=0;i<n;++i){ if(sma20_parts[i].is_null() && sma50_parts[i].is_null() && vwap20_parts[i].is_null()){ missing = true; break; } }
                                if(!missing){
                                    json s20 = json::array(); json s50 = json::array(); json v20 = json::array();
                                    for(int i=0;i<n;++i){ s20.push_back(sma20_parts[i]); s50.push_back(sma50_parts[i]); v20.push_back(vwap20_parts[i]); }
                                    indicators = json::object({{"sma20", s20}, {"sma50", s50}, {"vwap20", v20}});
                                    // cache hit
                                    this->cache_hits_.fetch_add(1);
                                    Logger::info(std::string("/klines cache-hit for ")+symbol+" months="+std::to_string(months.size())+" out_count="+std::to_string(n));
                                } else all_present = false;
                            }

                            if(!all_present){
                                // fallback: compute indicators from outarr closes and vols
                                this->cache_misses_.fetch_add(1);
                                Logger::info(std::string("/klines cache-miss for ")+symbol+" months="+std::to_string(months.size())+" out_count="+std::to_string(n));
                                std::vector<double> closes(n,0.0), vols(n,0.0);
                                for(int i=0;i<n;++i){ try{ closes[i] = std::stod(outarr[i][4].get<std::string>()); vols[i] = std::stod(outarr[i][5].get<std::string>()); } catch(...) { closes[i]=0.0; vols[i]=0.0; } }
                                auto rolling_sma = [&](const std::vector<double> &vals, int window){ json arr = json::array(); if((int)vals.size()==0) return arr; std::vector<double> pref(vals.size()+1, 0.0); for(size_t i=0;i<vals.size();++i) pref[i+1] = pref[i] + vals[i]; for(size_t i=0;i<vals.size();++i){ if((int)i >= window-1){ double s = pref[i+1] - pref[i+1-window]; arr.push_back(s / window); } else arr.push_back(nullptr); } return arr; };
                                auto rolling_vwap = [&](const std::vector<double> &cl, const std::vector<double> &vl, int window){ json arr = json::array(); if((int)cl.size()==0) return arr; std::vector<double> tp(cl.size(), 0.0); for(size_t i=0;i<cl.size();++i) tp[i] = cl[i] * vl[i]; std::vector<double> pref_num(tp.size()+1, 0.0), pref_den(tp.size()+1, 0.0); for(size_t i=0;i<tp.size();++i){ pref_num[i+1] = pref_num[i] + tp[i]; pref_den[i+1] = pref_den[i] + vl[i]; } for(size_t i=0;i<tp.size();++i){ if((int)i >= window-1){ double num = pref_num[i+1] - pref_num[i+1-window]; double den = pref_den[i+1] - pref_den[i+1-window]; if(den!=0) arr.push_back(num/den); else arr.push_back(nullptr); } else arr.push_back(nullptr); } return arr; };
                                json sma20 = rolling_sma(closes, 20);
                                json sma50 = rolling_sma(closes, 50);
                                json vwap20 = rolling_vwap(closes, vols, 20);
                                indicators = json::object({{"sma20", sma20}, {"sma50", sma50}, {"vwap20", vwap20}});
                            }

                            json out = json::object({{"symbol", symbol}, {"klines", outarr}, {"indicators", indicators}, {"source", "archive.range"}, {"available", (int)outarr.size()}});
                            respond_text(out.dump(), "application/json", http::status::ok);
                            s.shutdown(tcp::socket::shutdown_send);
                            return;
                        }

                        // attempt: if client omitted start/end, try to serve recent data by merging archive months
                        // First, check if a precomputed aggregate exists for this symbol/interval
                        try {
                            fs::path precomputed = fs::path("data") / "precomputed" / (symbol + std::string(".json"));
                            if(fs::exists(precomputed)){
                                try{
                                    auto pre_raw = load_file(precomputed);
                                    json prej = json::parse(pre_raw);
                                    if(prej.contains(interval) && prej[interval].is_array()){
                                        auto parr = prej[interval];
                                        if((int)parr.size() >= limit){
                                            json indicators = prej.value("indicators", json::object());
                                            json out = json::object({{"symbol", symbol}, {"klines", parr}, {"indicators", indicators}, {"source", "precomputed"}, {"available", (int)parr.size()}});
                                            respond_text(out.dump(), "application/json", http::status::ok);
                                            s.shutdown(tcp::socket::shutdown_send);
                                            return;
                                        }
                                    }
                                } catch(...){}
                            }
                        } catch(...){}
                        // collect months present in archive/klines/<symbol> sorted descending and accumulate entries until limit reached
                        try {
                            fs::path arch = fs::path("archive") / "klines" / symbol;
                            std::vector<json> collected;
                            if(fs::exists(arch) && fs::is_directory(arch)){
                                // list month files YYYY-MM.json.gz
                                std::vector<std::string> months;
                                for(auto &p: fs::directory_iterator(arch)){
                                    auto name = p.path().filename().string();
                                    if(name.size() >= 12 && name.substr(name.size()-8) == ".json.gz"){
                                        months.push_back(name.substr(0, name.size()-8));
                                    }
                                }
                                // sort descending (newest first)
                                std::sort(months.begin(), months.end(), std::greater<std::string>());
                                // determine how many raw 1m bars we should aim to collect before aggregating
                                int raw_needed = limit * 2;
                                if(interval != "1m"){
                                    std::map<std::string,int> mapping{{"1m",60},{"30m",60*30},{"4h",60*60*4}};
                                    auto itmap = mapping.find(interval);
                                    if(itmap != mapping.end()){
                                        int bucket_s = itmap->second;
                                        // number of 1m bars per bucket = bucket_s / 60
                                        int bars_per_bucket = bucket_s / 60;
                                        // we need roughly limit * bars_per_bucket 1m bars to produce `limit` aggregated bars
                                        raw_needed = limit * bars_per_bucket;
                                        // add much more slack so we read extra 1m bars from archive months
                                        // this helps produce enough aggregated buckets for coarse intervals
                                        raw_needed += 1024;
                                    }
                                }
                                for(auto &m: months){
                                    fs::path path = arch / (m + ".json.gz");
                                    if(!fs::exists(path)) continue;
                                    auto raw = load_gz_file(path);
                                    if(raw.empty()) continue;
                                    try{
                                        json arr = json::parse(raw);
                                        if(arr.is_array()){
                                            // append month's entries to collected (we will later trim to the most recent limit)
                                            for(size_t i=0;i<arr.size();++i) collected.push_back(arr[i]);
                                        }
                                    } catch(...) { continue; }
                                    // continue reading all available months (we will trim later to the recent window)
                                }
                                // sort collected by timestamp ascending (oldest first)
                                std::sort(collected.begin(), collected.end(), [](const json &a, const json &b){ return (int64_t)a[0] < (int64_t)b[0]; });
                                // Trim collected to the appropriate recent window:
                                // - if client requested 1m, keep up to `limit` most-recent 1m bars
                                // - if client requested a coarser interval, keep up to `raw_needed` most-recent 1m bars
                                if((int)collected.size() > 0){
                                    if(interval == "1m"){
                                        if((int)collected.size() > limit){
                                            std::vector<json> tmp;
                                            int start_idx = (int)collected.size() - limit;
                                            for(int i=start_idx;i<(int)collected.size();++i) tmp.push_back(collected[i]);
                                            collected = std::move(tmp);
                                        }
                                    } else {
                                        if((int)collected.size() > raw_needed){
                                            std::vector<json> tmp;
                                            int start_idx = (int)collected.size() - raw_needed;
                                            for(int i=start_idx;i<(int)collected.size();++i) tmp.push_back(collected[i]);
                                            collected = std::move(tmp);
                                        }
                                    }
                                }
                            }

                            if(!collected.empty()){
                                // prepare indicators: try to reuse existing per-month indicator cache mapping similar to archive branch above
                                int n = (int)collected.size();
                                std::vector<int64_t> ts_list(n);
                                std::unordered_map<int64_t,int> ts_to_idx;
                                for(int i=0;i<n;++i){ ts_list[i] = (int64_t)collected[i][0]; ts_to_idx[ts_list[i]] = i; }

                                std::vector<json> sma20_parts(n, nullptr), sma50_parts(n, nullptr), vwap20_parts(n, nullptr);
                                bool all_present = true;
                                // iterate months (ascending) to map indicators
                                for(auto &m: std::vector<std::string>()){} // placeholder to keep structure
                                // reuse months list from above (compute months_between from earliest to latest of collected)
                                if((int)collected.size()>0){
                                    int64_t first_ts = (int64_t)collected.front()[0];
                                    int64_t last_ts = (int64_t)collected.back()[0];
                                    std::tm tm1{}, tm2{};
                                    time_t tsec1 = (time_t)(first_ts/1000);
                                    time_t tsec2 = (time_t)(last_ts/1000);
                                    gmtime_r(&tsec1, &tm1);
                                    gmtime_r(&tsec2, &tm2);
                                    auto months = months_between(tm1.tm_year + 1900, tm1.tm_mon + 1, tm2.tm_year + 1900, tm2.tm_mon + 1);
                                    for(auto &m: months){
                                        fs::path indp = fs::path("archive") / "klines" / symbol / (m + ".ind.json.gz");
                                        fs::path rawp = fs::path("archive") / "klines" / symbol / (m + ".json.gz");
                                        if(!fs::exists(indp) || !fs::exists(rawp)) { all_present = false; break; }
                                        try{
                                            std::string indraw = load_gz_file(indp);
                                            json indj = json::parse(indraw);
                                            std::string raw = load_gz_file(rawp);
                                            json arrm = json::parse(raw);
                                            for(size_t i=0;i<arrm.size();++i){ int64_t ts = (int64_t)arrm[i][0]; auto it = ts_to_idx.find(ts); if(it!=ts_to_idx.end()){
                                                        int dst = it->second;
                                                        if(indj.contains("sma20") && indj["sma20"].is_array() && (int)indj["sma20"].size() > (int)i) sma20_parts[dst] = indj["sma20"][i];
                                                        if(indj.contains("sma50") && indj["sma50"].is_array() && (int)indj["sma50"].size() > (int)i) sma50_parts[dst] = indj["sma50"][i];
                                                        if(indj.contains("vwap20") && indj["vwap20"].is_array() && (int)indj["vwap20"].size() > (int)i) vwap20_parts[dst] = indj["vwap20"][i];
                                                }
                                            }
                                        } catch(...) { all_present = false; break; }
                                    }
                                }

                                json indicators;
                                if(all_present){
                                    bool missing = false;
                                    for(int i=0;i<n;++i){ if(sma20_parts[i].is_null() && sma50_parts[i].is_null() && vwap20_parts[i].is_null()){ missing = true; break; } }
                                    if(!missing){ json s20 = json::array(); json s50 = json::array(); json v20 = json::array(); for(int i=0;i<n;++i){ s20.push_back(sma20_parts[i]); s50.push_back(sma50_parts[i]); v20.push_back(vwap20_parts[i]); } indicators = json::object({{"sma20", s20}, {"sma50", s50}, {"vwap20", v20}}); this->cache_hits_.fetch_add(1); Logger::info(std::string("/klines archive-cache-hit for ")+symbol+" out_count="+std::to_string(n)); }
                                }
                                if(indicators.empty()){
                                    // fallback compute
                                    this->cache_misses_.fetch_add(1);
                                    int n2 = n;
                                    std::vector<double> closes(n2,0.0), vols(n2,0.0);
                                    for(int i=0;i<n2;++i){ try{ closes[i] = std::stod(collected[i][4].get<std::string>()); vols[i] = std::stod(collected[i][5].get<std::string>()); } catch(...) { closes[i]=0.0; vols[i]=0.0; } }
                                    auto rolling_sma = [&](const std::vector<double> &vals, int window){ json arr = json::array(); if((int)vals.size()==0) return arr; std::vector<double> pref(vals.size()+1, 0.0); for(size_t i=0;i<vals.size();++i) pref[i+1] = pref[i] + vals[i]; for(size_t i=0;i<vals.size();++i){ if((int)i >= window-1){ double s = pref[i+1] - pref[i+1-window]; arr.push_back(s / window); } else arr.push_back(nullptr); } return arr; };
                                    auto rolling_vwap = [&](const std::vector<double> &cl, const std::vector<double> &vl, int window){ json arr = json::array(); if((int)cl.size()==0) return arr; std::vector<double> tp(cl.size(), 0.0); for(size_t i=0;i<cl.size();++i) tp[i] = cl[i] * vl[i]; std::vector<double> pref_num(tp.size()+1, 0.0), pref_den(tp.size()+1, 0.0); for(size_t i=0;i<tp.size();++i){ pref_num[i+1] = pref_num[i] + tp[i]; pref_den[i+1] = pref_den[i] + vl[i]; } for(size_t i=0;i<tp.size();++i){ if((int)i >= window-1){ double num = pref_num[i+1] - pref_num[i+1-window]; double den = pref_den[i+1] - pref_den[i+1-window]; if(den!=0) arr.push_back(num/den); else arr.push_back(nullptr); } else arr.push_back(nullptr); } return arr; };
                                    json sma20 = rolling_sma(closes, 20);
                                    json sma50 = rolling_sma(closes, 50);
                                    json vwap20 = rolling_vwap(closes, vols, 20);
                                    indicators = json::object({{"sma20", sma20}, {"sma50", sma50}, {"vwap20", vwap20}});
                                }

                                // prepare final klines array for client; if client requested a non-1m interval
                                // and collected appears to be raw 1m bars, aggregate to the requested interval
                                json arr_for_client = json::array();
                                try {
                                    if(interval != "1m"){
                                        // convert to vector<json> and aggregate
                                        std::vector<json> raw_vec;
                                        for(auto &it: collected) raw_vec.push_back(it);
                                        auto agg = indicators::aggregate_to_interval_no_fmt(raw_vec, interval);
                                        for(auto &it: agg) arr_for_client.push_back(it);
                                        // recompute indicators from aggregated data
                                        json ns20 = json::array(); json ns50 = json::array(); json nv20 = json::array();
                                        auto s20 = indicators::compute_sma(agg, 20);
                                        auto s50 = indicators::compute_sma(agg, 50);
                                        auto v20 = indicators::compute_vwap(agg, 20);
                                        for(auto &x: s20) ns20.push_back(x);
                                        for(auto &x: s50) ns50.push_back(x);
                                        for(auto &x: v20) nv20.push_back(x);
                                        indicators = json::object({{"sma20", ns20}, {"sma50", ns50}, {"vwap20", nv20}});
                                    } else {
                                        for(auto &it: collected) arr_for_client.push_back(it);
                                    }
                                } catch(...) {
                                    for(auto &it: collected) arr_for_client.push_back(it);
                                }

                                // If archive aggregation produced fewer than `limit` buckets,
                                // prefer it unless data/latest can provide more aggregated buckets.
                                if((int)arr_for_client.size() < limit){
                                    int archive_count = (int)arr_for_client.size();
                                    int latest_count = 0;
                                    try{
                                        fs::path latestp = fs::path("data") / "latest" / (symbol + ".json");
                                        if(fs::exists(latestp)){
                                            auto latest_raw = load_file(latestp);
                                            json lj = json::parse(latest_raw);
                                            json src = lj.value("1m", lj.value("raw_1m", json::array()));
                                            if(src.is_array() && !src.empty()){
                                                std::vector<json> raw_vec;
                                                for(auto &it: src) raw_vec.push_back(it);
                                                auto la = indicators::aggregate_to_interval_no_fmt(raw_vec, interval);
                                                latest_count = (int)la.size();
                                            }
                                        }
                                    } catch(...) { latest_count = 0; }

                                    if(latest_count > archive_count){
                                        Logger::info(std::string("HTTP /klines (archive-merge) insufficient for ")+symbol+" have="+std::to_string(archive_count)+" need="+std::to_string(limit)+" - falling through to data/latest (latest_count="+std::to_string(latest_count)+")");
                                        // fallthrough to data/latest
                                    } else {
                                        // return archive aggregated result even if smaller than limit
                                        json out = json::object({{"symbol", symbol}, {"klines", arr_for_client}, {"indicators", indicators}, {"source", "archive.merge"}, {"available", (int)arr_for_client.size()}});
                                        respond_text(out.dump(), "application/json", http::status::ok);
                                        Logger::info(std::string("HTTP /klines (archive-merge) response for ")+symbol+" size="+std::to_string(out.dump().size())+" (archive_count="+std::to_string(archive_count)+")");
                                        s.shutdown(tcp::socket::shutdown_send);
                                        return;
                                    }
                                } else {
                                    json out = json::object({{"symbol", symbol}, {"klines", arr_for_client}, {"indicators", indicators}, {"source", "archive.merge"}, {"available", (int)arr_for_client.size()}});
                                    respond_text(out.dump(), "application/json", http::status::ok);
                                    Logger::info(std::string("HTTP /klines (archive-merge) response for ")+symbol+" size="+std::to_string(out.dump().size()));
                                    s.shutdown(tcp::socket::shutdown_send);
                                    return;
                                }
                            }
                        } catch(...) {
                            // fallthrough to data/latest fallback below
                        }

                        // fallback: serve data/latest as before
                        fs::path fp = fs::path("data") / "latest" / (symbol + ".json");
                        if(!fs::exists(fp)) { respond_text("{}", "application/json", http::status::bad_gateway); s.shutdown(tcp::socket::shutdown_send); return; }

                        try {
                            auto raw = load_file(fp);
                            json j = json::parse(raw);
                            // choose interval array or fallback: prefer the requested interval, else fall back to available 1m/raw_1m
                            std::string key = interval;
                            if(!j.contains(key)){
                                if(j.contains("1m")) key = "1m";
                                else if(j.contains("raw_1m")) key = "raw_1m";
                            }
                            json arr = j.value(key, json::array());
                            if(!arr.is_array()) arr = json::array();
                            // if the requested interval isn't present or precomputed array is too short,
                            // try to aggregate from 1m/raw_1m source when available
                            if(interval != "1m"){
                                bool need_aggregate = false;
                                if(!j.contains(interval)) need_aggregate = true;
                                else {
                                    try { if((int)arr.size() < limit) need_aggregate = true; } catch(...) { need_aggregate = true; }
                                }
                                if(need_aggregate){
                                    try {
                                        // Build a cache key
                                        std::string cache_key = symbol + "|" + interval + "|" + std::to_string(limit);
                                        {
                                            std::lock_guard<std::mutex> g(agg_cache_mutex);
                                            auto it = agg_cache.find(cache_key);
                                            if(it != agg_cache.end()){
                                                // 30s TTL
                                                auto age = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - it->second.ts).count();
                                                if(age < 30){
                                                    try { respond_text(it->second.payload.dump(), "application/json", http::status::ok); s.shutdown(tcp::socket::shutdown_send); return; } catch(...){}
                                                } else {
                                                    agg_cache.erase(it);
                                                }
                                            }
                                        }

                                        json src = j.value("1m", j.value("raw_1m", json::array()));
                                        if(src.is_array() && !src.empty()){
                                            // Find the longest continuous tail segment (no gaps) from src's end going backwards
                                            std::vector<json> tail;
                                            int64_t prev_ts = -1;
                                            for(int i = (int)src.size() - 1; i >= 0; --i){
                                                try{
                                                    int64_t ts = (int64_t)src[i][0];
                                                    if(prev_ts == -1){ tail.push_back(src[i]); prev_ts = ts; }
                                                    else {
                                                        if(prev_ts - ts == 60000){ // exactly 60s gap
                                                            tail.push_back(src[i]); prev_ts = ts;
                                                        } else break; // gap detected
                                                    }
                                                } catch(...) { break; }
                                            }
                                            if(tail.empty()){
                                                // fallback to whole src
                                                for(auto &it: src) tail.push_back(it);
                                            }
                                            // tail currently in reverse chronological order; reverse to ascending
                                            std::reverse(tail.begin(), tail.end());

                                            // If we need more raw bars to attempt to satisfy limit for coarse intervals, expand tail
                                            int raw_needed = limit * 2;
                                            if(interval != "1m"){
                                                std::map<std::string,int> mapping{{"1m",60},{"30m",60*30},{"4h",60*60*4}};
                                                auto itmap = mapping.find(interval);
                                                if(itmap != mapping.end()){
                                                    int bucket_s = itmap->second;
                                                    int bars_per_bucket = bucket_s / 60;
                                                    raw_needed = limit * bars_per_bucket + 128;
                                                }
                                            }
                                            // if tail smaller than raw_needed, try to include older contiguous blocks if possible (we already scanned contiguous only), so otherwise we use what we have

                                            std::vector<json> raw_vec;
                                            for(auto &it: tail) raw_vec.push_back(it);

                                            auto agg = indicators::aggregate_to_interval_no_fmt(raw_vec, interval);
                                            arr = json::array();
                                            for(auto &it: agg) arr.push_back(it);

                                            // recompute indicators
                                            json ns20 = json::array(); json ns50 = json::array(); json nv20 = json::array();
                                            auto s20 = indicators::compute_sma(agg, 20);
                                            auto s50 = indicators::compute_sma(agg, 50);
                                            auto v20 = indicators::compute_vwap(agg, 20);
                                            for(auto &x: s20) ns20.push_back(x);
                                            for(auto &x: s50) ns50.push_back(x);
                                            for(auto &x: v20) nv20.push_back(x);
                                            j["indicators"] = json::object({{"sma20", ns20}, {"sma50", ns50}, {"vwap20", nv20}});

                                            // Cache the response
                                            json out = json::object({{"symbol", symbol}, {"klines", arr}, {"indicators", j["indicators"]}, {"source", "data.latest.contiguous_tail"}, {"available", (int)arr.size()}});
                                            AggCacheEntry ent; ent.payload = out; ent.ts = std::chrono::steady_clock::now();
                                            {
                                                std::lock_guard<std::mutex> g(agg_cache_mutex);
                                                agg_cache[cache_key] = ent;
                                            }
                                            respond_text(out.dump(), "application/json", http::status::ok);
                                            s.shutdown(tcp::socket::shutdown_send);
                                            return;
                                        }
                                    } catch(...) {
                                        // fall through, keep arr as-is
                                    }
                                }
                            }
                            if((int)arr.size() > limit) {
                                json tmp = json::array();
                                int start = (int)arr.size() - limit;
                                for(int k = start; k < (int)arr.size(); ++k) tmp.push_back(arr[k]);
                                arr = std::move(tmp);
                            }
                            json indicators = j.value("indicators", json::object());
                            json out = json::object({{"symbol", symbol}, {"klines", arr}, {"indicators", indicators}, {"source", std::string("data.latest")}, {"available", (int)arr.size()}});
                            respond_text(out.dump(), "application/json", http::status::ok);
                            Logger::info(std::string("HTTP /klines response for ")+symbol+" size="+std::to_string(out.dump().size())+" source=data.latest available="+std::to_string((int)arr.size()));
                        } catch(...) {
                            respond_text("{}", "application/json", http::status::internal_server_error);
                        }
                        s.shutdown(tcp::socket::shutdown_send);
                        return;
                    }

                    if(target.rfind("/indicators",0) == 0) {
                        auto qp = parse_qs(target);
                        std::string symbol = qp.count("symbol")?qp["symbol"]:"";
                        if(symbol.empty()) { respond_text("{}", "application/json", http::status::bad_request); s.shutdown(tcp::socket::shutdown_send); return; }
                        fs::path ip = fs::path("data") / "indicators" / (symbol + ".json");
                        if(fs::exists(ip)) {
                            respond_text(load_file(ip), "application/json", http::status::ok);
                        } else {
                            // fallback into latest
                            fs::path fp = fs::path("data") / "latest" / (symbol + ".json");
                            if(!fs::exists(fp)) respond_text("{}", "application/json", http::status::not_found);
                            else {
                                try { json j = json::parse(load_file(fp)); json indicators = j.value("indicators", json::object()); respond_text(indicators.dump(), "application/json", http::status::ok); }
                                catch(...) { respond_text("{}", "application/json", http::status::internal_server_error); }
                            }
                        }
                        s.shutdown(tcp::socket::shutdown_send);
                        return;
                    }

                    if(target.rfind("/status",0) == 0) {
                        fs::path lp = "logs/runtime.log";
                        json st = json::object();
                        if(fs::exists(lp)) {
                            std::ifstream ifs(lp);
                            std::string line, last;
                            while(std::getline(ifs,line)) { auto pos = line.find("STATE_JSON "); if(pos!=std::string::npos) last = line.substr(pos+11); }
                            if(!last.empty()) try { st = json::parse(last); } catch(...) {}
                        }
                        // include cache counters
                        try {
                            st["archive_indicator_cache"] = json::object({{"hits", this->cache_hits_.load()}, {"misses", this->cache_misses_.load()}});
                        } catch(...) {}
                        respond_text(st.dump(), "application/json", http::status::ok);
                        s.shutdown(tcp::socket::shutdown_send);
                        return;
                    }

                    if(target.rfind("/events",0) == 0) {
                        // SSE: stream new lines from logs/runtime.log and periodic state events
                        fs::path lp = "logs/runtime.log";
                        // capture Last-Event-ID if provided by client
                        std::string client_last_event_id;
                        {
                            auto it = req.find("Last-Event-ID");
                            if(it != req.end()) client_last_event_id = std::string(it->value());
                        }
                        if(!client_last_event_id.empty()) Logger::info(std::string("SSE: client Last-Event-ID=") + client_last_event_id);

                        // helper to create simple monotonically unique event id (ms since epoch)
                        auto make_event_id = []()->std::string{
                            auto now = std::chrono::system_clock::now();
                            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
                            return std::to_string(ms);
                        };

                        // write SSE headers (raw HTTP write) and include CORS + buffering hint
                        try {
                            std::string hdr =
                                "HTTP/1.1 200 OK\r\n"
                                "Content-Type: text/event-stream\r\n"
                                "Cache-Control: no-cache\r\n"
                                "Connection: keep-alive\r\n"
                                "Access-Control-Allow-Origin: *\r\n"
                                "Access-Control-Allow-Headers: Last-Event-ID\r\n"
                                "X-Accel-Buffering: no\r\n"
                                "\r\n";
                            try{
                                auto n = boost::asio::write(s, boost::asio::buffer(hdr));
                                Logger::info(std::string("SSE: wrote header bytes=") + std::to_string(n));
                            } catch(const std::exception &e){
                                Logger::error(std::string("SSE header write error: ")+e.what());
                                throw;
                            }

                            // initial state send (include an id: line so clients can resume if they wish)
                            auto send_state = [&](bool include_id=true){
                                std::string last_state = "";
                                if(fs::exists(lp)){
                                    std::ifstream ifs(lp);
                                    std::string line; std::string last;
                                    while(std::getline(ifs, line)){
                                        auto pos = line.find("STATE_JSON ");
                                        if(pos!=std::string::npos) last = line.substr(pos+11);
                                    }
                                    last_state = last;
                                }
                                if(!last_state.empty()){
                                    std::string idline = include_id ? (std::string("id: ") + make_event_id() + "\n") : std::string();
                                    std::string ev = idline + std::string("event: state\n") + "data: " + last_state + "\n\n";
                                    auto n = boost::asio::write(s, boost::asio::buffer(ev));
                                    Logger::info(std::string("SSE: send_state bytes=") + std::to_string(n));
                                }
                            };

                            Logger::info("SSE: initial state send");
                            send_state(true);

                            // open file and seek to end for tailing
                            std::ifstream ifs;
                            if(fs::exists(lp)){
                                ifs.open(lp);
                                ifs.clear();
                                ifs.seekg(0, std::ios::end);
                            }

                            auto last_write_time = std::chrono::steady_clock::now();
                            auto last_state_send = std::chrono::steady_clock::now();
                            while(running_) {
                                bool did_write = false;
                                // check for new log line
                                if(ifs && ifs.good()){
                                    std::string line;
                                    if(std::getline(ifs, line)){
                                        // send raw log line as data event, include an id for client offset
                                        std::string idline = std::string("id: ") + make_event_id() + "\n";
                                        std::string msg = idline + std::string("data: ") + line + "\n\n";
                                        try{
                                                auto n = boost::asio::write(s, boost::asio::buffer(msg));
                                                Logger::info(std::string("SSE: sent log line bytes=")+std::to_string(n)+" content_len="+std::to_string(line.size()));
                                                did_write = true;
                                        } catch(const std::exception &e){
                                            Logger::error(std::string("SSE write error: ") + e.what());
                                            break;
                                        }
                                        last_state_send = std::chrono::steady_clock::now();
                                    }
                                }

                                // periodic state update every 1s
                                auto now = std::chrono::steady_clock::now();
                                if(std::chrono::duration_cast<std::chrono::milliseconds>(now - last_state_send).count() > 1000){
                                    try{
                                        send_state(true);
                                        Logger::info("SSE: periodic state sent");
                                        did_write = true;
                                    } catch(const std::exception &e){
                                        Logger::error(std::string("SSE state send error: ") + e.what());
                                        break;
                                    }
                                    last_state_send = now;
                                }

                                if(did_write) last_write_time = std::chrono::steady_clock::now();

                                // heartbeat every 15s if nothing written to keep connection alive through proxies
                                auto since = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_write_time).count();
                                if(since > 15000){
                                    try{
                                        std::string hb = std::string(": heartbeat\n\n");
                                        auto n = boost::asio::write(s, boost::asio::buffer(hb));
                                        Logger::info(std::string("SSE: heartbeat bytes=") + std::to_string(n));
                                        last_write_time = std::chrono::steady_clock::now();
                                    } catch(const std::exception &e){
                                        Logger::info(std::string("SSE heartbeat write failed: ") + e.what());
                                        break;
                                    }
                                }

                                // small sleep to avoid busy loop
                                std::this_thread::sleep_for(std::chrono::milliseconds(200));

                                // handle if log file appeared later
                                if(!ifs.is_open() && fs::exists(lp)){
                                    ifs.open(lp);
                                    ifs.clear();
                                    ifs.seekg(0, std::ios::end);
                                }
                            }
                        } catch(const std::exception &e) {
                            Logger::error(std::string("SSE handler exception: ") + e.what());
                        }
                        try { s.shutdown(tcp::socket::shutdown_send); } catch(...) {}
                        return;
                    }

                    // default: 404
                    respond_text("not found", "text/plain", http::status::not_found);
                    s.shutdown(tcp::socket::shutdown_send);
                    return;

                } catch(const std::exception &ex) {
                    std::cerr << "connection handler exception: " << ex.what() << std::endl;
                }
            }).detach();
        }
    } catch(const std::exception &ex) {
        std::cerr << "WebConsoleServer exception: " << ex.what() << std::endl;
    }
}
