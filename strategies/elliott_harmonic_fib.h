// strategies/elliott_harmonic_fib.h
// 基于艾略特波（Elliott Wave）、谐波形态与斐波那契比例的策略骨架
// 提供检测函数、信号生成与参数配置的接口

#pragma once

#include <vector>
#include <optional>
#include <string>

struct OHLCV {
    double open;
    double high;
    double low;
    double close;
    double volume;
    long long ts; // 时间戳
};

struct Signal {
    enum Type { BUY, SELL, NONE } type = NONE;
    double price = 0.0;
    std::string reason;
};

class ElliottHarmonicFibStrategy {
public:
    // 构造函数：接受参数（可扩展）
    ElliottHarmonicFibStrategy();

    // 在每个新的 K 线到达时调用
    void onBar(const OHLCV &bar);

    // 在逐条逐 Tick 调用（可选）
    void onTick(double price);

    // 外部调用以获取当前生成的信号（或空）
    std::optional<Signal> generateSignal();

    // 策略调参（阈值、回撤容忍度等）
    void setFibonacciTolerance(double tol);
    void setMinWaveLength(int n);

private:
    std::vector<OHLCV> history_;
    std::optional<Signal> pending_;

    // 参数
    double fib_tol_ = 0.02; // 斐波那契匹配容忍度（2%）
    int min_wave_len_ = 3;  // 识别波段的最小 K 数

    // 内部算法方法
    // compute retracement ratio between two price points
    double fibRetracement(double a, double b, double x) const;

    // 检测简单的艾略特波结构（骨架）：识别 5 波上升或 3 波回撤
    bool detectElliottWavePattern();

    // 基于斐波那契与谐波模板检测 Gartley/AB=CD 等
    bool detectHarmonicPattern(std::string &outPattern);
};
