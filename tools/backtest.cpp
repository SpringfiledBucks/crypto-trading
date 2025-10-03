// tools/backtest.cpp
// 简易回测器：将历史 OHLCV（如果存在）或合成数据传入策略，按策略信号进行简化的资金曲线计算

#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <cmath>
#include <chrono>

#include "../strategies/elliott_harmonic_fib.h"

using namespace std;

static vector<OHLCV> load_csv(const string &path) {
    vector<OHLCV> out;
    ifstream f(path);
    if(!f) return out;
    string line;
    // try skip header
    getline(f, line);
    while(getline(f, line)) {
        if(line.empty()) continue;
        stringstream ss(line);
        string tok;
        vector<string> cols;
        while(getline(ss, tok, ',')) cols.push_back(tok);
        if(cols.size() < 6) continue;
        OHLCV b;
        try {
            b.ts = stoll(cols[0]);
            b.open = stod(cols[1]);
            b.high = stod(cols[2]);
            b.low = stod(cols[3]);
            b.close = stod(cols[4]);
            b.volume = stod(cols[5]);
            out.push_back(b);
        } catch(...) { continue; }
    }
    return out;
}

static vector<OHLCV> synth_prices(int n=500, double start=30000.0) {
    vector<OHLCV> out;
    out.reserve(n);
    double price = start;
    for(int i=0;i<n;++i) {
        double t = i/10.0;
        double drift = 0.1 * sin(t*0.2) + 0.05 * cos(t*0.5);
        double vol = 50.0 * sin(t*0.7);
        double open = price;
        double close = price + drift + vol * 0.01;
        double high = max(open, close) + fabs(vol)*0.02;
        double low = min(open, close) - fabs(vol)*0.02;
        OHLCV b{open, high, low, close, 0.0, (long long)(i)};
        out.push_back(b);
        price = close;
    }
    return out;
}

int main(int argc, char **argv) {
    string path = (argc>1? argv[1] : "");
    vector<OHLCV> bars;
    if(!path.empty()) bars = load_csv(path);
    if(bars.empty()) {
        cout << "No CSV found or failed to load; generating synthetic data" << endl;
        bars = synth_prices(800, 30000.0);
    } else {
        cout << "Loaded " << bars.size() << " bars from " << path << endl;
    }

    ElliottHarmonicFibStrategy strat;
    strat.setFibonacciTolerance(0.03);
    strat.setMinWaveLength(4);

    double capital = 10000.0;
    double exposure = 0.1; // 10% per trade
    double position = 0.0; // units
    double entry_price = 0.0;
    int trades = 0;

    for(size_t i=0;i<bars.size();++i) {
        strat.onBar(bars[i]);
        auto sig = strat.generateSignal();
        if(sig && sig->type == Signal::BUY) {
            if(position == 0.0) {
                double qty = (capital * exposure) / sig->price;
                position = qty;
                entry_price = sig->price;
                trades++;
                cout << "[trade open] i="<<i<<" price="<<sig->price<<" qty="<<qty<<" reason="<<sig->reason<<"\n";
            }
        } else if(sig && sig->type == Signal::SELL) {
            if(position > 0.0) {
                double pnl = position * sig->price - position * entry_price;
                capital += pnl;
                cout << "[trade close] i="<<i<<" price="<<sig->price<<" pnl="<<pnl<<"\n";
                position = 0.0;
            }
        }
        // simple mark-to-market (optional)
    }

    // close any open position at last price
    if(position > 0.0) {
        double last = bars.back().close;
        double pnl = position * last - position * entry_price;
        capital += pnl;
        cout << "[final close] price="<<last<<" pnl="<<pnl<<"\n";
        position = 0.0;
    }

    cout << "Trades: "<<trades<<" Final capital: "<<capital<<" Return: "<<((capital-10000.0)/10000.0*100.0)<<"%\n";
    return 0;
}
