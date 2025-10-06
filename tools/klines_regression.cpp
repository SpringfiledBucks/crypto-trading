// small regression tool to query local /klines endpoint for many symbols
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <nlohmann/json.hpp>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <chrono>

namespace beast = boost::beast; namespace http = beast::http; namespace net = boost::asio; using tcp = net::ip::tcp;
using json = nlohmann::json; namespace fs = std::filesystem;

std::string http_get(const std::string &host, const std::string &port, const std::string &target) {
    try {
        net::io_context ioc;
        tcp::resolver resolver{ioc};
        auto eps = resolver.resolve(host, port);
        beast::tcp_stream stream{ioc};
        stream.connect(eps);
        http::request<http::string_body> req{http::verb::get, target, 11};
        req.set(http::field::host, host);
        req.set(http::field::user_agent, "klines_regression_tool");
        http::write(stream, req);
        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);
        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);
        return res.body();
    } catch(std::exception &e) {
        return std::string("__ERR__:") + e.what();
    }
}

int main(int argc, char **argv){
    std::string host = "127.0.0.1";
    std::string port = "8080";
    std::vector<std::string> intervals = {"1m","30m","4h"};
    std::vector<int> limits = {100,250,500};
    std::vector<std::string> syms;
    int timeout_s = 20;
    std::string output = "logs/klines_regression_report_cpp.json";
    bool do_compare = false;
    // simple CLI parse
    for(int i=1;i<argc;++i){ std::string a = argv[i]; if(a=="--symbols" && i+1<argc){ std::string s=argv[++i]; std::stringstream ss(s); while(ss.good()){ std::string tok; getline(ss, tok, ','); if(!tok.empty()) syms.push_back(tok); } }
        else if(a=="--limits" && i+1<argc){ std::string s=argv[++i]; std::stringstream ss(s); while(ss.good()){ std::string tok; getline(ss, tok, ','); if(!tok.empty()) limits.push_back(std::stoi(tok)); } }
        else if(a=="--intervals" && i+1<argc){ std::string s=argv[++i]; intervals.clear(); std::stringstream ss(s); while(ss.good()){ std::string tok; getline(ss, tok, ','); if(!tok.empty()) intervals.push_back(tok); } }
        else if(a=="--timeout" && i+1<argc){ timeout_s = std::stoi(argv[++i]); }
        else if(a=="--output" && i+1<argc){ output = argv[++i]; }
        else if(a=="--compare") { do_compare = true; }
    }
    if(syms.empty()){
        for(auto &p: fs::directory_iterator("data/latest")){
            if(!p.is_regular_file()) continue;
            auto name = p.path().filename().string();
            if(name.size()>5 && name.substr(name.size()-5) == ".json"){
                if(name.size()>12 && name.substr(name.size()-12) == ".latest.json") continue;
                syms.push_back(name.substr(0, name.size()-5));
            }
        }
    }
    std::sort(syms.begin(), syms.end());
    json report;
    report["generated_at"] = "";
    report["results"] = json::array();
    for(auto &sym: syms){
        for(auto &it: intervals){
            for(auto lim: limits){
                std::string target = "/klines?symbol=" + sym + "&interval=" + it + "&limit=" + std::to_string(lim);
                std::cout << "querying " << sym << " " << it << " " << lim << std::endl;
                auto body = http_get(host, port, target);
                if(body.rfind("__ERR__:",0)==0){
                    report["results"].push_back({{"symbol", sym}, {"interval", it}, {"limit", lim}, {"error", body}});
                    continue;
                }
                try{
                    json j = json::parse(body);
                    json kl = j.value("klines", json::array());
                    json ind = j.value("indicators", json::object());
                    json ic = json::object();
                    for(auto it2 = ind.begin(); it2 != ind.end(); ++it2){ ic[it2.key()] = it2.value().is_array() ? (int)it2.value().size() : 0; }
                    int kc = kl.is_array() ? (int)kl.size() : 0;
                    json rec = json::object();
                    rec["symbol"] = sym;
                    rec["interval"] = it;
                    rec["limit"] = lim;
                    if(j.contains("source")) rec["source"] = j["source"]; else rec["source"] = nullptr;
                    if(j.contains("available")) rec["available_field"] = j["available"]; else rec["available_field"] = nullptr;
                    rec["klines_count"] = kc;
                    rec["indicators_counts"] = ic;
                    rec["first_ts"] = kc ? kl[0][0] : nullptr;
                    rec["last_ts"] = kc ? kl[kc-1][0] : nullptr;
                    report["results"].push_back(rec);
                } catch(std::exception &e){
                    report["results"].push_back({{"symbol", sym}, {"interval", it}, {"limit", lim}, {"error", e.what()}});
                }
            }
        }
    }
    // set generated timestamp as ISO8601
    auto now = std::chrono::system_clock::now();
    std::time_t tnow = std::chrono::system_clock::to_time_t(now);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&tnow));
    report["generated_at"] = std::string(buf);
    std::ofstream ofs(output);
    ofs << report.dump(2);
    ofs.close();
    std::cout << "WROTE " << output << "\n";

    if(do_compare){
        // run comparator script to diff python and cpp reports
        std::string cmd = std::string("python3 scripts/compare_regression_reports.py logs/klines_regression_report_full.json ") + output;
        std::cout << "Running compare: " << cmd << std::endl;
        int rc = system(cmd.c_str()); (void)rc;
    }
    return 0;
}
