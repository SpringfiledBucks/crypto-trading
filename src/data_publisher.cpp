#include "data_publisher.h"
#include "http_client.h"
#include "logger.h"
#include "websocket_client.h"
#include <nlohmann/json.hpp>
#include "indicators.h"
#include <fstream>
#include <chrono>
#include <thread>
#include <filesystem>
#include <iostream>
#include <vector>
#include <map>
#include <algorithm>
#include <sstream>

using json = nlohmann::json;
namespace fs = std::filesystem;

static std::string config_symbols_path = "config/symbols.json";

DataPublisher::DataPublisher(const std::string &proxy): proxy_(proxy) {}

DataPublisher::~DataPublisher() { stop(); }

void DataPublisher::start() {
    if(running_.exchange(true)) return;
    worker_ = std::thread([this]{ run_loop(); });
}

void DataPublisher::stop() {
    running_ = false;
    if(worker_.joinable()) worker_.join();
}

// helper: safe atomic write
static bool atomic_write_file(const fs::path &dst, const std::string &data) {
    try {
        fs::path tmp = dst;
        tmp += ".tmp";
        std::ofstream ofs(tmp, std::ios::binary);
        if(!ofs) return false;
        ofs << data;
        ofs.close();
        fs::rename(tmp, dst);
        return true;
    } catch(...) {
        return false;
    }
}

// Aggregation and indicator helpers (simple implementations mirroring Python server)
// removed unused aggregate_to_interval that referenced external fmt library

// note: fmt used for formatting; but to avoid extra dependency, fallback to ostringstream
static std::string fmt_double(double v) {
    std::ostringstream ss; ss.setf(std::ios::fixed); ss.precision(8); ss << v; return ss.str();
}

// aggregation and indicators implemented in src/indicators.cpp

void DataPublisher::run_loop() {
    // check global config for temporary disable flag
    try {
        std::ifstream cfgf("config/config.json");
        if(cfgf){
            json cj; cfgf >> cj;
            if(cj.contains("disable_history_download") && cj["disable_history_download"].is_boolean() && cj["disable_history_download"].get<bool>()){
                Logger::info("DataPublisher: history download disabled via config, exiting publisher loop");
                return;
            }
        }
    } catch(...) {}

    // Try WebSocket-based subscription to 1m klines (combined streams) for lower latency and efficiency.
    std::vector<std::string> symbols;
    try {
        std::ifstream sf(config_symbols_path);
        if(sf) {
            json sj; sf >> sj;
            if(sj.contains("symbols") && sj["symbols"].is_array()) {
                for(auto &it: sj["symbols"]) symbols.push_back(it.get<std::string>());
            }
        }
    } catch(...) {}

    fs::path outdir = fs::path("data") / "latest";
    fs::create_directories(outdir);
    fs::path indicators_dir = fs::path("data") / "indicators";
    fs::create_directories(indicators_dir);
    // write short-lived price snapshots to a separate dir to avoid colliding with
    // full `data/latest/<SYMBOL>.json` files which are authoritative for precompute
    fs::path price_snapshots_dir = fs::path("data") / "latest" / "price_snapshots";
    fs::create_directories(price_snapshots_dir);

    if(symbols.empty()) {
        Logger::warn("DataPublisher: no symbols configured, exiting publisher loop");
        return;
    }

    // build combined stream URL: for each symbol subscribe to kline_1m and markPrice
    std::string stream_q;
    for(size_t i=0;i<symbols.size();++i) {
        std::string s = symbols[i];
        // lowercase
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
        if(i) stream_q += "/";
        stream_q += s + "@kline_1m/" + s + "@markPrice";
    }
    std::string ws_url = "wss://fstream.binance.com/stream?streams=" + stream_q;
    Logger::info(std::string("DataPublisher: attempting WS connect to: ") + ws_url);

    // in-memory buffers per symbol (store last N 1m bars) and latest price cache
    std::map<std::string, std::vector<json>> buffer_map;
    std::map<std::string, json> latest_map;
    for(auto &s: symbols) { buffer_map[s] = {}; latest_map[s] = json::object(); }

    // performance counters
    std::atomic<uint64_t> msg_count{0};
    std::atomic<uint64_t> closed_k_count{0};
    auto start_ts = std::chrono::steady_clock::now();

    // try to use WebSocketClient (existing implementation)
    try {
        WebSocketClient ws;
        ws.set_on_message([&](const std::string &msg){
            msg_count++;
            try {
                auto j = json::parse(msg);
                // combined stream has {"stream":..., "data":{...}}
                json data = j.value("data", json::object());
                if(data.is_null() || data.empty()) return;
                if(data.contains("k")) {
                    auto k = data["k"];
                    bool closed = k.value("x", false);
                    if(!closed) return; // only act on closed bars
                    // symbol from stream or s field
                    std::string sym;
                    if(data.contains("s")) sym = data["s"].get<std::string>();
                    else if(j.contains("stream")) {
                        auto st = j["stream"].get<std::string>();
                        auto at = st.find('@'); if(at!=std::string::npos) sym = st.substr(0, at);
                        // upper-case
                        std::transform(sym.begin(), sym.end(), sym.begin(), [](unsigned char c){ return std::toupper(c); });
                    }
                    if(sym.empty()) return;
                    // build binance-style array: [openTime, open, high, low, close, volume, closeTime]
                    long long openTime = k.value("t", 0LL);
                    long long closeTime = k.value("T", 0LL);
                    std::string open = k.value("o", std::string("0"));
                    std::string high = k.value("h", std::string("0"));
                    std::string low = k.value("l", std::string("0"));
                    std::string close = k.value("c", std::string("0"));
                    std::string vol = k.value("v", std::string("0"));
                    json arr = json::array({openTime, open, high, low, close, vol, closeTime});

                    // append to buffer (symbol key uppercase)
                    auto &vec = buffer_map[sym];
                    vec.push_back(arr);
                    if(vec.size() > 2000) vec.erase(vec.begin(), vec.begin() + (vec.size()-2000));

                    // compute aggregated arrays and indicators and write file
                    json out;
                    out["raw_1m"] = vec;
                    out["1m"] = vec;
                    out["30m"] = indicators::aggregate_to_interval_no_fmt(vec, "30m");
                    out["4h"] = indicators::aggregate_to_interval_no_fmt(vec, "4h");
                    out["indicators"] = json::object();
                    out["indicators"]["1m"] = json::object();
                    out["indicators"]["30m"] = json::object();
                    out["indicators"]["4h"] = json::object();
                    out["indicators"]["1m"]["sma20"] = indicators::compute_sma(out["1m"].get<std::vector<json>>(), 20);
                    out["indicators"]["1m"]["sma50"] = indicators::compute_sma(out["1m"].get<std::vector<json>>(), 50);
                    out["indicators"]["1m"]["vwap20"] = indicators::compute_vwap(out["1m"].get<std::vector<json>>(), 20);
                    out["indicators"]["30m"]["sma20"] = indicators::compute_sma(out["30m"].get<std::vector<json>>(), 20);
                    out["indicators"]["30m"]["sma50"] = indicators::compute_sma(out["30m"].get<std::vector<json>>(), 50);
                    out["indicators"]["30m"]["vwap20"] = indicators::compute_vwap(out["30m"].get<std::vector<json>>(), 20);
                    out["indicators"]["4h"]["sma20"] = indicators::compute_sma(out["4h"].get<std::vector<json>>(), 20);
                    out["indicators"]["4h"]["sma50"] = indicators::compute_sma(out["4h"].get<std::vector<json>>(), 50);
                    out["indicators"]["4h"]["vwap20"] = indicators::compute_vwap(out["4h"].get<std::vector<json>>(), 20);

                    // write to disk atomically, but avoid overwriting a larger/more recent file with a smaller/older one
                    fs::path dst = outdir / (sym + std::string(".json"));
                    try {
                        bool do_write = true;
                        if(fs::exists(dst)){
                            try{
                                std::ifstream ifs(dst);
                                if(ifs){
                                    json existing; ifs >> existing;
                                    int existing_n = 0; int64_t existing_last = 0;
                                    if(existing.contains("raw_1m") && existing["raw_1m"].is_array()){
                                        existing_n = (int)existing["raw_1m"].size();
                                        if(existing_n>0) existing_last = (int64_t)existing["raw_1m"].back()[0];
                                    } else if(existing.contains("1m") && existing["1m"].is_array()){
                                        existing_n = (int)existing["1m"].size();
                                        if(existing_n>0) existing_last = (int64_t)existing["1m"].back()[0];
                                    }
                                    int new_n = 0; int64_t new_last = 0;
                                    if(out.contains("raw_1m") && out["raw_1m"].is_array()){
                                        new_n = (int)out["raw_1m"].size();
                                        if(new_n>0) new_last = (int64_t)out["raw_1m"].back()[0];
                                    } else if(out.contains("1m") && out["1m"].is_array()){
                                        new_n = (int)out["1m"].size();
                                        if(new_n>0) new_last = (int64_t)out["1m"].back()[0];
                                    }
                                    // If the existing file has more bars and is at least as recent, skip write
                                    if(existing_n > 0 && existing_n >= new_n && existing_last >= new_last){
                                        do_write = false;
                                        std::ostringstream ss; ss << "DataPublisher: skip writing " << dst.string() << " (existing bars=" << existing_n << " last=" << existing_last << ") vs new bars=" << new_n << " last=" << new_last;
                                        Logger::info(ss.str());
                                    }
                                }
                            } catch(...) { /* ignore parse errors and allow write */ }
                        }
                        if(do_write){ atomic_write_file(dst, out.dump()); }
                    } catch(...) { /* ignore write errors */ }

                    // write indicators separately
                    try {
                        json inds = out.value("indicators", json::object());
                        fs::path idst = indicators_dir / (sym + std::string(".json"));
                        atomic_write_file(idst, inds.dump());
                    } catch(...) {}

                    closed_k_count++;
                }
                // additionally handle markPriceUpdate events to store latest price separately
                if(data.contains("e") && data["e"].is_string() && data["e"].get<std::string>() == "markPriceUpdate") {
                    try {
                        std::string sym = data.value("s", std::string());
                        if(sym.empty() && j.contains("stream")) {
                            auto st = j["stream"].get<std::string>();
                            auto at = st.find('@'); if(at!=std::string::npos) sym = st.substr(0, at);
                            std::transform(sym.begin(), sym.end(), sym.begin(), [](unsigned char c){ return std::toupper(c); });
                        }
                        if(sym.empty()) {
                            // unknown symbol
                        } else {
                            json latest;
                            latest["event"] = data.value("e", json());
                            latest["event_time"] = data.value("E", json());
                            latest["price"] = data.value("p", json());
                            latest_map[sym] = latest;
                            // write latest small price snapshot into a separate directory so it
                            // doesn't accidentally get treated as a full-symbol file by tools
                            // (we intentionally do NOT write to data/latest/<SYM>.json here)
                            fs::path ldst = price_snapshots_dir / (sym + std::string(".json"));
                            atomic_write_file(ldst, latest.dump());
                        }
                    } catch(...) {}
                }
            } catch(...) { }
        });

        if(!ws.connect(ws_url)) {
            Logger::warn("DataPublisher: WS connect failed, falling back to REST loop");
            // fallback: leave existing REST behavior (simple loop)
        } else {
            // keep running until stop requested
            Logger::info("DataPublisher: WS connected, entering message loop");
            while(running_) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_ts).count();
                if(elapsed > 0 && (msg_count.load() > 0)) {
                    double rate = double(msg_count.load()) / double(std::max<int>(1, (int)elapsed));
                    std::ostringstream ss; ss << "DataPublisher WS stats msgs=" << msg_count.load() << " closed_k=" << closed_k_count.load() << " rate/s=" << rate;
                    Logger::info(ss.str());
                }
            }
            ws.close();
            return;
        }
    } catch(const std::exception &ex) {
        Logger::error(std::string("DataPublisher WS error: ") + ex.what());
    }

    // REST fallback if WS not available
    HttpClient http(proxy_);
    while(running_) {
        for(const auto &sym : symbols) {
            try {
                std::string url = "https://fapi.binance.com/fapi/v1/klines?symbol=" + sym + "&interval=1m&limit=1000";
                auto resp = http.get(url);
                if(!resp) continue;
                json raw = json::parse(*resp);
                json out;
                std::vector<json> raw_vec = raw.get<std::vector<json>>();
                out["raw_1m"] = raw_vec;
                out["1m"] = raw_vec;
                out["30m"] = indicators::aggregate_to_interval_no_fmt(raw_vec, "30m");
                out["4h"] = indicators::aggregate_to_interval_no_fmt(raw_vec, "4h");
                out["indicators"] = json::object();
                out["indicators"]["1m"]["sma20"] = indicators::compute_sma(out["1m"].get<std::vector<json>>(), 20);
                out["indicators"]["1m"]["sma50"] = indicators::compute_sma(out["1m"].get<std::vector<json>>(), 50);
                out["indicators"]["1m"]["vwap20"] = indicators::compute_vwap(out["1m"].get<std::vector<json>>(), 20);
                std::string s = out.dump();
                fs::path dst = outdir / (sym + std::string(".json"));
                atomic_write_file(dst, s);
            } catch(...) { continue; }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        for(int i=0;i<30 && running_;++i) std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
