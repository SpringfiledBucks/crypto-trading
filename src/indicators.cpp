#include "indicators.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <map>
#include <algorithm>

using json = nlohmann::json;

namespace indicators {

static std::string fmt_double(double v) {
    std::ostringstream ss; ss.setf(std::ios::fixed); ss.precision(8); ss << v; return ss.str();
}

std::vector<json> aggregate_to_interval_no_fmt(const std::vector<json> &raw_arr, const std::string &interval) {
    std::map<std::string,int> mapping{{"1m",60},{"30m",60*30},{"4h",60*60*4}};
    if(mapping.find(interval)==mapping.end()) return raw_arr;
    int bucket_s = mapping[interval];
    struct Agg { long bucket; double open, high, low, close, vol; };
    std::map<long, Agg> groups;
    std::vector<long> order;
    for(const auto &c : raw_arr) {
        try {
            long ts = c[0].get<long long>()/1000;
            double o = std::stod(c[1].get<std::string>());
            double h = std::stod(c[2].get<std::string>());
            double l = std::stod(c[3].get<std::string>());
            double cl = std::stod(c[4].get<std::string>());
            double v = std::stod(c[5].get<std::string>());
            long bucket = (ts / bucket_s) * bucket_s;
            auto it = groups.find(bucket);
            if(it == groups.end()) {
                groups[bucket] = Agg{bucket, o, h, l, cl, v};
                order.push_back(bucket);
            } else {
                auto &g = it->second;
                if(h > g.high) g.high = h;
                if(l < g.low) g.low = l;
                g.close = cl;
                g.vol += v;
            }
        } catch(...) { continue; }
    }
    std::sort(order.begin(), order.end());
    std::vector<json> out;
    for(auto b : order) {
        auto &g = groups[b];
        long open_ms = b * 1000;
        long close_ms = (b + bucket_s) * 1000 - 1;
        json item = json::array({open_ms, fmt_double(g.open), fmt_double(g.high), fmt_double(g.low), fmt_double(g.close), fmt_double(g.vol), close_ms});
        out.push_back(item);
    }
    return out;
}

std::vector<json> compute_sma(const std::vector<json> &arr, int window) {
    std::vector<double> q; std::vector<json> res;
    double s = 0.0;
    for(size_t i=0;i<arr.size();++i) {
        try {
            double close = std::stod(arr[i][4].get<std::string>());
            q.push_back(close);
            s += close;
            if(q.size() > (size_t)window) { s -= q.front(); q.erase(q.begin()); }
            if(q.size() == (size_t)window) {
                long ts = arr[i][0].get<long long>()/1000;
                res.push_back(json::array({ts, s / window}));
            }
        } catch(...) { continue; }
    }
    return res;
}

std::vector<json> compute_vwap(const std::vector<json> &arr, int window) {
    std::vector<double> pv; std::vector<double> vol; std::vector<json> res;
    for(size_t i=0;i<arr.size();++i) {
        try {
            double typical = (std::stod(arr[i][2].get<std::string>()) + std::stod(arr[i][3].get<std::string>()) + std::stod(arr[i][4].get<std::string>()))/3.0;
            double v = std::stod(arr[i][5].get<std::string>());
            pv.push_back(typical * v); vol.push_back(v);
            if(pv.size() > (size_t)window) { pv.erase(pv.begin()); vol.erase(vol.begin()); }
            if(pv.size() == (size_t)window) {
                double s_pv = 0.0; double s_vol = 0.0; for(auto x:pv) s_pv+=x; for(auto x:vol) s_vol+=x;
                double val = s_vol>0 ? s_pv / s_vol : 0.0;
                long ts = arr[i][0].get<long long>()/1000;
                res.push_back(json::array({ts, val}));
            }
        } catch(...) { continue; }
    }
    return res;
}

} // namespace indicators
