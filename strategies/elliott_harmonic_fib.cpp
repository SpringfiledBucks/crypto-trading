// strategies/elliott_harmonic_fib.cpp
// 实现策略的核心逻辑（骨架实现，含注释）

#include "elliott_harmonic_fib.h"
#include <cmath>
#include <sstream>

ElliottHarmonicFibStrategy::ElliottHarmonicFibStrategy() {}

void ElliottHarmonicFibStrategy::onBar(const OHLCV &bar) {
    history_.push_back(bar);
    // 保持长度不过长
    if(history_.size() > 2000) history_.erase(history_.begin());

    // 简单示例：当有足够数据时尝试检测波形
    if(history_.size() >= (size_t)min_wave_len_) {
        std::string pat;
        if(detectHarmonicPattern(pat)) {
            // 发现谐波形态时生成 signal
            Signal s;
            s.type = Signal::BUY;
            s.price = history_.back().close;
            s.reason = "harmonic:" + pat;
            pending_ = s;
            return;
        }

        if(detectElliottWavePattern()) {
            Signal s;
            s.type = Signal::BUY;
            s.price = history_.back().close;
            s.reason = "elliott_wave";
            pending_ = s;
            return;
        }
    }
    // 默认无信号
    pending_ = std::nullopt;
}

void ElliottHarmonicFibStrategy::onTick(double price) {
    // 可按需实现基于 Tick 的快速触发，这里暂不实现
}

std::optional<Signal> ElliottHarmonicFibStrategy::generateSignal() {
    return pending_;
}

void ElliottHarmonicFibStrategy::setFibonacciTolerance(double tol) { fib_tol_ = tol; }
void ElliottHarmonicFibStrategy::setMinWaveLength(int n) { min_wave_len_ = n; }

double ElliottHarmonicFibStrategy::fibRetracement(double a, double b, double x) const {
    if(fabs(b - a) < 1e-12) return 0.0;
    return (x - a) / (b - a);
}

bool ElliottHarmonicFibStrategy::detectElliottWavePattern() {
    // 非常简化的占位检测：查找局部极值序列，判断是否有 5 波结构的大致方向
    if(history_.size() < 10) return false;
    // 简单实现：寻找最近若干波峰/波谷的交替
    int n = history_.size();
    int peaks = 0, troughs = 0;
    for(int i = n - 8; i < n - 1; ++i) {
        if(i <= 0) continue;
        double prev = history_[i-1].close;
        double cur = history_[i].close;
        double next = history_[i+1].close;
        if(cur > prev && cur > next) peaks++;
        if(cur < prev && cur < next) troughs++;
    }
    // 找到交替的峰谷，则视为简单的波段结构
    return (peaks + troughs) >= 3;
}

bool ElliottHarmonicFibStrategy::detectHarmonicPattern(std::string &outPattern) {
    // 检测常见谐波: 简化版只检测 AB=CD 或 Gartley-like 模式
    outPattern.clear();
    if(history_.size() < 8) return false;
    // 取最近 5 个关键点: X A B C D 为近似序列
    size_t n = history_.size();
    double X = history_[n-8].close;
    double A = history_[n-6].close;
    double B = history_[n-4].close;
    double C = history_[n-2].close;
    double D = history_[n-1].close;

    // 计算 AB 相对 XA 的回撤，和 CD 相对 BC 的比例
    double XA = fibRetracement(X, A, B); // B 相对于 X->A 的位置
    double BC = fibRetracement(B, C, D); // D 相对于 B->C 的位置

    // 检验 AB=CD（简化版）：AB 变化接近 CD
    double ab = fabs((A - B));
    double cd = fabs((C - D));
    if(ab < 1e-12 || cd < 1e-12) return false;
    double ratio = fabs(ab - cd) / std::max(ab, cd);
    if(ratio < 0.05) {
        outPattern = "AB=CD";
        return true;
    }

    // Gartley-like: XA 回撤 0.618 左右，BC 回撤 0.382-0.886
    if(fabs(XA - 0.618) < fib_tol_ && BC > 0.38 - fib_tol_ && BC < 0.886 + fib_tol_) {
        outPattern = "Gartley-like";
        return true;
    }

    return false;
}
