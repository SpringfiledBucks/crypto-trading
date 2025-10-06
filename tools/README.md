klines_regression tool
======================

This directory contains `tools/klines_regression.cpp`, a small C++ CLI tool that queries the local webconsole `/klines` endpoint for a list of symbols, intervals, and limits and writes a JSON report.

Usage examples:

Run over all symbols found in `data/latest`:

```
./build/klines_regression --output logs/klines_regression_report_cpp.json
```

Run for specific symbols and intervals:

```
./build/klines_regression --symbols BTCUSDT,ETHUSDT --intervals 1m,30m --limits 100,500 --output myreport.json
```

The script `scripts/compare_regression_reports.py` compares the Python-generated report with the C++ output.
