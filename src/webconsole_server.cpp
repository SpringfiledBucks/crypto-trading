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
                        respond_text(st.dump(), "application/json", http::status::ok);
                        s.shutdown(tcp::socket::shutdown_send);
                        return;
                    }

                    if(target.rfind("/events",0) == 0) {
                        // SSE: stream new lines from logs/runtime.log and periodic state events
                        fs::path lp = "logs/runtime.log";
                        // write SSE headers (raw HTTP write)
                        try {
                            std::string hdr = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-cache\r\nConnection: keep-alive\r\n\r\n";
                            try{
                                auto n = boost::asio::write(s, boost::asio::buffer(hdr));
                                Logger::info(std::string("SSE: wrote header bytes=") + std::to_string(n));
                            } catch(const std::exception &e){
                                Logger::error(std::string("SSE header write error: ")+e.what());
                                throw;
                            }

                            // initial state send
                            auto send_state = [&](){
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
                                    std::string ev = std::string("event: state\n") + "data: " + last_state + "\n\n";
                                    auto n = boost::asio::write(s, boost::asio::buffer(ev));
                                    Logger::info(std::string("SSE: send_state bytes=") + std::to_string(n));
                                }
                            };

                            Logger::info("SSE: initial state send");
                            send_state();

                            // open file and seek to end for tailing
                            std::ifstream ifs;
                            if(fs::exists(lp)){
                                ifs.open(lp);
                                ifs.clear();
                                ifs.seekg(0, std::ios::end);
                            }

                            auto last_state_send = std::chrono::steady_clock::now();
                            while(running_) {
                                // check for new log line
                                if(ifs && ifs.good()){
                                    std::string line;
                                    if(std::getline(ifs, line)){
                                        // send raw log line as data event
                                        std::string msg = std::string("data: ") + line + "\n\n";
                                        try{
                                                auto n = boost::asio::write(s, boost::asio::buffer(msg));
                                                Logger::info(std::string("SSE: sent log line bytes=")+std::to_string(n)+" content_len="+std::to_string(line.size()));
                                        } catch(const std::exception &e){
                                            Logger::error(std::string("SSE write error: ") + e.what());
                                            break;
                                        }
                                        last_state_send = std::chrono::steady_clock::now();
                                        continue;
                                    }
                                }
                                // periodic state update every 1s
                                auto now = std::chrono::steady_clock::now();
                                if(std::chrono::duration_cast<std::chrono::milliseconds>(now - last_state_send).count() > 1000){
                                    try{
                                        send_state();
                                        Logger::info("SSE: periodic state sent");
                                    } catch(const std::exception &e){
                                        Logger::error(std::string("SSE state send error: ") + e.what());
                                        break;
                                    }
                                    last_state_send = now;
                                }
                                // small sleep to avoid busy loop
                                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                                // handle if log file appeared later
                                if(!ifs.is_open() && fs::exists(lp)){
                                    ifs.open(lp);
                                    ifs.clear();
                                    ifs.seekg(0, std::ios::end);
                                }
                                // detect socket closed: try a zero-byte write
                                boost::system::error_code ec;
                                boost::asio::write(s, boost::asio::buffer(std::string("")), ec);
                                if(ec){ Logger::info(std::string("SSE: socket write error (probe): ") + ec.message()); break; }
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
