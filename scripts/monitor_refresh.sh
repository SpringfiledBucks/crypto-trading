#!/usr/bin/env bash
# Simple monitor: run a sample refresh via the configured proxy and append output to logs/refresh_monitor.log
set -euo pipefail
mkdir -p logs
export HTTP_PROXY='socks5h://127.0.0.1:15651'
export HTTPS_PROXY='socks5h://127.0.0.1:15651'
export WS_PROXY='http://127.0.0.1:15615'
# run sample refresh (limit=5) and timestamp the output
echo "--- $(date -u +%Y-%m-%dT%H:%M:%SZ) refresh sample ---" >> logs/refresh_monitor.log
python3 scripts/refresh_all.py --sample >> logs/refresh_monitor.log 2>&1 || echo "refresh script failed at $(date -u)" >> logs/refresh_monitor.log
# Simple monitor: run a sample refresh via the configured proxy and append output to logs/refresh_monitor.log
set -euo pipefail
mkdir -p logs
export HTTP_PROXY='socks5h://127.0.0.1:15651'
export HTTPS_PROXY='socks5h://127.0.0.1:15651'
export WS_PROXY='http://127.0.0.1:15615'
# run sample refresh (limit=5) and timestamp the output
echo ---
