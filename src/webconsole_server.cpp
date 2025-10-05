#include "webconsole_server.h"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/signal_set.hpp>
#include "logger.h"
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

                            json out = json::object({{"symbol", symbol}, {"klines", outarr}, {"indicators", indicators}});
                            respond_text(out.dump(), "application/json", http::status::ok);
                            s.shutdown(tcp::socket::shutdown_send);
                            return;
                        }

                        // fallback: serve data/latest as before
                        fs::path fp = fs::path("data") / "latest" / (symbol + ".json");
                        if(!fs::exists(fp)) { respond_text("{}", "application/json", http::status::bad_gateway); s.shutdown(tcp::socket::shutdown_send); return; }

                        try {
                            auto raw = load_file(fp);
                            json j = json::parse(raw);
                            // choose interval array or fallback
                            std::string key = interval;
                            if(!j.contains(key) && interval=="1m" && j.contains("raw_1m")) key = "raw_1m";
                            json arr = j.value(key, json::array());
                            if(!arr.is_array()) arr = json::array();
                            if((int)arr.size() > limit) {
                                json tmp = json::array();
                                int start = (int)arr.size() - limit;
                                for(int k = start; k < (int)arr.size(); ++k) tmp.push_back(arr[k]);
                                arr = std::move(tmp);
                            }
                            json indicators = j.value("indicators", json::object());
                            json out = json::object({{"klines", arr}, {"indicators", indicators}});
                            respond_text(out.dump(), "application/json", http::status::ok);
                            Logger::info(std::string("HTTP /klines response for ")+symbol+" size="+std::to_string(out.dump().size()));
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
