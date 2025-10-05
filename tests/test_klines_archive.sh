#!/usr/bin/env bash
# Simple smoke test: call local C++ server /klines for BTCUSDT small range and ensure JSON contains klines
set -euo pipefail
curl -s "http://127.0.0.1:8080/klines?symbol=BTCUSDT&start=2025-08-01T00:00:00Z&end=2025-08-02T00:00:00Z&limit=10" > /tmp/klines_resp.json || { echo "curl failed"; exit 2; }
jq -e '.klines and (.klines|length>0)' /tmp/klines_resp.json >/dev/null || { echo "no klines returned"; cat /tmp/klines_resp.json; exit 3; }
echo "smoke ok"
