#!/usr/bin/env bash
# Simple monitor: run a sample refresh via the configured proxy and append output to logs/refresh_monitor.log
# To avoid clobbering data/latest with tiny sample results, run in --dry-run mode so files are not written.
set -euo pipefail
mkdir -p logs
mkdir -p data/sample
export HTTP_PROXY='socks5h://127.0.0.1:15651'
export HTTPS_PROXY='socks5h://127.0.0.1:15651'
export WS_PROXY='http://127.0.0.1:15615'
# run sample refresh into data/sample (do not clobber data/latest). keep --dry-run as extra safety.
echo "--- $(date -u +%Y-%m-%dT%H:%M:%SZ) refresh sample (outdir=data/sample) ---" >> logs/refresh_monitor.log
python3 scripts/refresh_all.py --sample --outdir data/sample --dry-run >> logs/refresh_monitor.log 2>&1 || echo "refresh script failed at $(date -u)" >> logs/refresh_monitor.log

