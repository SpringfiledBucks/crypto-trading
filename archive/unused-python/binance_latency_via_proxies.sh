#!/usr/bin/env bash
set -euo pipefail

HTTP_PROXY_URL="http://127.0.0.1:15615"
SOCKS_PROXY_HOSTPORT="127.0.0.1:15651"
SOCKS_PROXY_URL="socks5://127.0.0.1:15651"
ENDPOINTS=("https://fapi.binance.com/fapi/v1/ping")
TRIALS=5
CSV_FILE="$(pwd)/binance_proxy_latency.csv"

if [ ! -f "$CSV_FILE" ]; then
  echo "mode,endpoint,trial,success,time_s,http_code,exit_code" > "$CSV_FILE"
fi

for endpoint in "${ENDPOINTS[@]}"; do
  for i in $(seq 1 $TRIALS); do
    out=$(curl -s -o /dev/null -w "%{http_code} %{time_total}" --proxy "$HTTP_PROXY_URL" --max-time 10 "$endpoint")
    rc=$?
    http_code=$(echo "$out" | awk '{print $1}')
    time_total=$(echo "$out" | awk '{print $2}')
    if [ $rc -ne 0 ]; then
      http_code="000"
      time_total=""
    fi
    success=false
    if [ "$http_code" = "200" ]; then
      success=true
    fi
    echo "http_proxy,$endpoint,$i,$success,$time_total,$http_code,$rc" >> "$CSV_FILE"
    echo "[http_proxy] trial=$i endpoint=$endpoint success=$success time=$time_total code=$http_code exit=$rc"
  done

  for i in $(seq 1 $TRIALS); do
    out=$(curl -s -o /dev/null -w "%{http_code} %{time_total}" --socks5-hostname "$SOCKS_PROXY_HOSTPORT" --max-time 10 "$endpoint")
    rc=$?
    http_code=$(echo "$out" | awk '{print $1}')
    time_total=$(echo "$out" | awk '{print $2}')
    if [ $rc -ne 0 ]; then
      http_code="000"
      time_total=""
    fi
    success=false
    if [ "$http_code" = "200" ]; then
      success=true
    fi
  echo "socks_proxy,$endpoint,$i,$success,$time_total,$http_code,$rc" >> "$CSV_FILE"
  echo "[socks_proxy] trial=$i endpoint=$endpoint success=$success time=$time_total code=$http_code exit=$rc"
  done
done

echo "测速完成，结果追加到： $CSV_FILE"
