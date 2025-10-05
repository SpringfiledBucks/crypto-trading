#!/usr/bin/env bash
set -euo pipefail

OUTFILE="binance_proxy_latency.csv"
ENDPOINTS=(
  "https://api.binance.com/api/v3/ping"
  "https://api.binance.com/api/v3/time"
  "https://api.binance.com/api/v3/depth?symbol=BTCUSDT&limit=5"
)
HTTP_PROXY_URL="http://127.0.0.1:15615"
SOCKS_HOSTPORT="127.0.0.1:15651"
TRIES=5

echo "mode,endpoint,trial,success,time_s,exit_code" > "$OUTFILE"

for mode in direct http socks; do
  for ep in "${ENDPOINTS[@]}"; do
    for i in $(seq 1 $TRIES); do
      if [ "$mode" = "direct" ]; then
        proxy_args=()
      elif [ "$mode" = "http" ]; then
        proxy_args=(--proxy "$HTTP_PROXY_URL")
      else
        proxy_args=(--socks5-hostname "$SOCKS_HOSTPORT")
      fi
      set +e
      time_total=$(curl -s -o /dev/null -w "%{time_total}" "${proxy_args[@]}" "$ep")
      exit_code=$?
      set -e
      if [ "$exit_code" -ne 0 ] || [ -z "$time_total" ]; then
        printf "%s,%s,%d,false,,%d\n" "$mode" "$ep" "$i" "$exit_code" >> "$OUTFILE"
      else
        printf "%s,%s,%d,true,%.6f,%d\n" "$mode" "$ep" "$i" "$time_total" "$exit_code" >> "$OUTFILE"
      fi
      sleep 0.4
    done
  done
done

echo "测速完成，结果保存在 $OUTFILE" >&2

python3 - <<'PY'
import csv,statistics
f='binance_proxy_latency.csv'
data={}
with open(f) as fh:
    r=csv.DictReader(fh)
    for row in r:
        if row['success']!='true':
            continue
        key=(row['mode'],row['endpoint'])
        data.setdefault(key,[]).append(float(row['time_s']))

for k,vals in sorted(data.items()):
    print(f"{k[0]} {k[1]} -> n={len(vals)} avg={statistics.mean(vals):.3f}s min={min(vals):.3f}s max={max(vals):.3f}s")

print('\nCSV 文件：', f)
PY

exit 0
