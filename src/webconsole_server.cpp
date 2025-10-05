#include "webconsole_server.h"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/signal_set.hpp>
#include <nlohmann/json.hpp>
#include <thread>
#include <fstream>
#include <iostream>
#include <filesystem>

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

void WebConsoleServer::run() {
    try {
        boost::asio::io_context ioc{1};
        tcp::acceptor acceptor{ioc, {tcp::v4(), port_}};
        while(running_) {
            tcp::socket socket{ioc};
            acceptor.accept(socket);
            // handle each connection in a detached thread to support SSE
            std::thread([this, s = std::move(socket)]() mutable {
                try {
                    boost::beast::flat_buffer buffer;
                    http::request<http::string_body> req;
                    http::read(s, buffer, req);
                    http::response<http::string_body> res{http::status::ok, req.version()};
                    res.set(http::field::server, "crypto_trading_webconsole_cpp");
                    std::string target = std::string(req.target());

                    auto send_404 = [&](const std::string &msg){ res.result(http::status::not_found); res.body() = msg; res.prepare_payload(); http::write(s, res); };

                    // helper: parse query params
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
                                std::string k = q.substr(i, eq-i);
                                std::string v = q.substr(eq+1, j-eq-1);
                                m[k]=v;
                            }
                            i = j+1;
                        }
                        return m;
                    };

                    if(target == "/" || target.find("/index.html") == 0) {
                        auto body = load_file(fs::path(wwwroot_) / "index.html");
                        if(body.empty()) { send_404("index not found"); }
                        else { res.set(http::field::content_type, "text/html"); res.body() = body; res.prepare_payload(); http::write(s, res); }
                    }
                    else if(target.find("/static/") == 0) {
                        fs::path p = fs::path(wwwroot_) / target.substr(1);
                        auto body = load_file(p);
                        if(body.empty()) send_404("not found");
                        else { res.set(http::field::content_type, "application/javascript"); res.body() = body; res.prepare_payload(); http::write(s, res); }
                    }
                    else if(target.rfind("/symbols",0) == 0) {
                        fs::path cfg = "config/symbols.json";
                        std::string body = load_file(cfg);
                        if(body.empty()) send_404("{}");
                        else { res.set(http::field::content_type, "application/json"); res.body() = body; res.prepare_payload(); http::write(s, res); }
                    }
                    else if(target.rfind("/klines",0) == 0) {
                        Logger::info(std::string("HTTP /klines request: ") + target);
                        auto qp = parse_qs(target);
                        std::string symbol = qp.count("symbol")?qp["symbol"]:"";
                        std::string interval = qp.count("interval")?qp["interval"]:"1m";
                        int limit = qp.count("limit")?std::stoi(qp["limit"]):500;
                        limit = std::max(1, std::min(1000, limit));
                        bool compact = qp.count("compact") && (qp["compact"]=="1" || qp["compact"]=="true");
                        if(symbol.empty()) { res.result(http::status::bad_request); res.body() = "{}"; res.prepare_payload(); http::write(s,res); }
                        else {
                            fs::path fp = fs::path("data") / "latest" / (symbol + ".json");
                            if(!fs::exists(fp)) { res.result(http::status::bad_gateway); res.body() = "{}"; res.prepare_payload(); http::write(s,res); }
                            else {
                                try {
                                    std::string raw = load_file(fp);
                                    json j = json::parse(raw);
                                    // pick interval array
                                    std::string key = interval;
                                    if(!j.contains(key)) {
                                        // fallback: if 1m requested and j has raw_1m
                                        if(interval=="1m" && j.contains("raw_1m")) key = "raw_1m";
                                    }
                                    json arr = j.value(key, json::array());
                                    if(!arr.is_array()) arr = json::array();
                                    // slice to limit
                                    json sliced = json::array();
                                    int start = std::max(0, (int)arr.size() - limit);
                                    for(int i=start;i<(int)arr.size();++i) sliced.push_back(arr[i]);
                                    json indicators = j.value("indicators", json::object());
                                    json ind_for = indicators.value(interval, json::object());
                                    json out;
                                    if(compact) {
                                        json kl = json::array();
                                        for(auto &c : sliced) {
                                            try {
                                                long t = c[0].get<long long>()/1000;
                                                double o = std::stod(c[1].get<std::string>());
                                                double h = std::stod(c[2].get<std::string>());
                                                double l = std::stod(c[3].get<std::string>());
                                                double cl = std::stod(c[4].get<std::string>());
                                                double v = std::stod(c[5].get<std::string>());
                                                kl.push_back(json::object({{"time", t},{"open", o},{"high", h},{"low", l},{"close", cl},{"volume", v}}));
                                            } catch(...) { continue; }
                                        }
                                        out["klines"] = kl;
                                    } else {
                                        out["klines"] = sliced;
                                    }
                                    out["indicators"] = ind_for;
                                    res.set(http::field::content_type, "application/json");
                                    res.body() = out.dump();
                                    res.prepare_payload(); http::write(s, res);
                                    Logger::info(std::string("HTTP /klines response for ")+symbol+" size="+std::to_string(res.body().size()));
                                } catch(...) {
                                    res.result(http::status::internal_server_error); res.body() = "{}"; res.prepare_payload(); http::write(s,res);
                                Logger::error(std::string("HTTP /klines error for ")+symbol);
                                }
                            }
                        }
                        else if(target.rfind("/indicators",0) == 0) {
                            Logger::info(std::string("HTTP /indicators request: ") + target);
                            auto qp = parse_qs(target);
                            std::string symbol = qp.count("symbol")?qp["symbol"]:"";
                            if(symbol.empty()) { res.result(http::status::bad_request); res.body() = "{}"; res.prepare_payload(); http::write(s,res); }
                            else {
                                fs::path ip = fs::path("data") / "indicators" / (symbol + std::string(".json"));
                                if(fs::exists(ip)) {
                                    auto body = load_file(ip);
                                    if(body.empty()) send_404("indicators not found");
                                    else { res.set(http::field::content_type, "application/json"); res.body() = body; res.prepare_payload(); http::write(s, res); }
                                    Logger::info(std::string("HTTP /indicators response size=")+std::to_string(body.size()));
                                } else {
                                    // fallback into latest file
                                    fs::path fp = fs::path("data") / "latest" / (symbol + std::string(".json"));
                                    if(!fs::exists(fp)) { res.result(http::status::not_found); res.body() = "{}"; res.prepare_payload(); http::write(s,res); }
                                    else {
                                        try {
                                            std::string raw = load_file(fp);
                                            json j = json::parse(raw);
                                            json indicators = j.value("indicators", json::object());
                                            res.set(http::field::content_type, "application/json"); res.body() = indicators.dump(); res.prepare_payload(); http::write(s,res);
                                            Logger::info(std::string("HTTP /indicators fallback response for ")+symbol+" size="+std::to_string(res.body().size()));
                                        } catch(...) { res.result(http::status::internal_server_error); res.body() = "{}"; res.prepare_payload(); http::write(s,res); }
                                    }
                                }
                            }
                        }
                    }
                    else if(target.rfind("/status",0) == 0) {
                        // read last STATE_JSON line from logs/runtime.log
                        fs::path lp = "logs/runtime.log";
                        json st = json::object();
                        if(fs::exists(lp)) {
                            std::ifstream ifs(lp);
                            std::string line; std::string last;
                            while(std::getline(ifs, line)) {
                                auto pos = line.find("STATE_JSON ");
                                if(pos!=std::string::npos) last = line.substr(pos+11);
                            }
                            if(!last.empty()) {
                                try { st = json::parse(last); } catch(...) { st = json::object(); }
                            }
                        }
                        res.set(http::field::content_type, "application/json"); res.body() = st.dump(); res.prepare_payload(); http::write(s,res);
                    }
                    else if(target.rfind("/fetch-local",0) == 0) {
                        auto qp = parse_qs(target);
                        std::string sym = qp.count("symbol")?qp["symbol"]:"";
                        fs::path rawdir = fs::path("data") / "raw";
                        fs::path outdir = fs::path("data") / "latest";
                        fs::create_directories(outdir);
                        json results = json::object();
                        auto try_copy = [&](const std::string &s)->bool{
                            fs::path src = rawdir / (s + ".json"); fs::path dst = outdir / (s + ".json");
                            if(!fs::exists(src)) return false;
                            try { std::string d = load_file(src); json::parse(d); std::ofstream ofs(dst); ofs<<d; ofs.close(); return true; } catch(...) { return false; }
                        };
                        if(!sym.empty()) { results[sym] = try_copy(sym); }
                        else {
                            fs::path cfg = "config/symbols.json"; std::string body = load_file(cfg);
                            try { json sj = json::parse(body); if(sj.contains("symbols") && sj["symbols"].is_array()) { for(auto &it: sj["symbols"]) { std::string s = it.get<std::string>(); results[s] = try_copy(s); } } } catch(...) {}
                        }
                        res.set(http::field::content_type, "application/json"); res.body() = json::object({{"results", results}}).dump(); res.prepare_payload(); http::write(s,res);
                    }
                    else if(target.rfind("/events",0) == 0) {
                        // SSE: stream new lines from logs/runtime.log and periodic state
                        fs::path lp = "logs/runtime.log";
                        try {
                            // write raw HTTP headers for SSE
                            std::string hdr = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-cache\r\nConnection: keep-alive\r\n\r\n";
                            boost::asio::write(s, boost::asio::buffer(hdr));
                            std::ifstream ifs(lp);
                            if(!ifs) {
                                std::string err = "data: [no log file]\n\n";
                                boost::asio::write(s, boost::asio::buffer(err));
                            } else {
                                ifs.seekg(0, std::ios::end);
                                while(true) {
                                    std::string line;
                                    if(std::getline(ifs, line)) {
                                        std::string msg = std::string("data: ") + line + "\n\n";
                                        boost::asio::write(s, boost::asio::buffer(msg));
                                    } else {
                                        std::this_thread::sleep_for(std::chrono::milliseconds(200));
                                        if(!running_) break;
                                        continue;
                                    }
                                }
                            }
                        } catch(...) {}
                    }
                    else {
                        send_404("not found");
                    }
                    boost::system::error_code ec; s.shutdown(tcp::socket::shutdown_send, ec);
                } catch(const std::exception &ex) {
                    std::cerr << "connection handler exception: " << ex.what() << std::endl;
                }
            }).detach();
        }
    } catch(const std::exception &ex) {
        std::cerr << "WebConsoleServer exception: " << ex.what() << std::endl;
    }
}
