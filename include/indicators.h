// indicators.h - small collection of aggregation and indicator helpers
#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace indicators {
using json = nlohmann::json;

// Aggregate raw 1m bars (Binance style arrays) into larger intervals ("30m", "4h", etc.)
std::vector<json> aggregate_to_interval_no_fmt(const std::vector<json> &raw_arr, const std::string &interval);

// Simple moving average on close prices, returns array of [ts_seconds, value]
std::vector<json> compute_sma(const std::vector<json> &arr, int window);

// VWAP over a sliding window, returns array of [ts_seconds, value]
std::vector<json> compute_vwap(const std::vector<json> &arr, int window);

} // namespace indicators
