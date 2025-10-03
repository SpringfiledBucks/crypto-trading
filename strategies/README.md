Elliott + Harmonic + Fibonacci Strategy
=====================================

本目录包含一个策略骨架 `ElliottHarmonicFibStrategy`：

- 目标：结合艾略特波（Elliott Wave）的结构、常见谐波形态（如 AB=CD、Gartley-like）与斐波那契比例来生成交易信号。
- 实现：提供 `onBar` 回调以接收 K 线（OHLCV），并通过 `generateSignal()` 获取当前信号（若存在）。

注意：当前实现为算法骨架，包含简化的识别逻辑与阈值，需在真实市场数据上回测与参数调优。

如何使用：

1. 在主程序或 Trader 中包含头文件：
   ```cpp
   #include "strategies/elliott_harmonic_fib.h"
   ```
2. 创建实例并在接收新 K 线时调用 `onBar`：
   ```cpp
   ElliottHarmonicFibStrategy strat;
   strat.onBar(bar);
   auto sig = strat.generateSignal();
   if(sig && sig->type == Signal::BUY) { /* 下单逻辑 */ }
   ```

3. 调整参数：`setFibonacciTolerance()` 和 `setMinWaveLength()`。
