#!/usr/bin/env python3
"""
Download 1m klines for configured symbols starting from a given date (UTC) until now.
Writes each symbol to data/latest/<SYMBOL>.json as {"1m": [...], "indicators": {}}

Usage examples:
  scripts/download_from_date.py --from 2025-08-01T00:00:00Z
  scripts/download_from_date.py --from 2025-08-01 --symbols BTCUSDT

This script pages using Binance /fapi/v1/klines with limit=1000 and startTime/ endTime params.
It respects HTTP_PROXY/HTTPS_PROXY environment variables if set (run via scripts/run.sh to inherit proxies).
"""

import os, sys, json, time, argparse, datetime, math, shutil, random, subprocess
from urllib import request, parse, error

BASE = os.path.dirname(os.path.dirname(__file__))
OUTDIR = os.path.join(BASE, 'data', 'latest')
os.makedirs(OUTDIR, exist_ok=True)

BASE_URL = 'https://fapi.binance.com/fapi/v1/klines'
HEADERS = {'User-Agent': 'crypto-trading/1.0'}
PAGE_LIMIT = 1000

def iso_to_ms(s):
    # accept YYYY-MM-DD or full ISO
    if 'T' not in s:
        s = s + 'T00:00:00Z'
    dt = datetime.datetime.fromisoformat(s.replace('Z','+00:00'))
    return int(dt.timestamp() * 1000)

def fetch_page(sym, start_ms, end_ms=None):
    params = {'symbol': sym, 'interval': '1m', 'limit': str(PAGE_LIMIT), 'startTime': str(start_ms)}
    if end_ms:
        params['endTime'] = str(end_ms)
    url = BASE_URL + '?' + parse.urlencode(params)
    # try curl if proxy socks
    curl_path = shutil.which('curl')
    proxy = os.environ.get('HTTP_PROXY') or os.environ.get('HTTPS_PROXY')
    if curl_path and proxy and proxy.startswith('socks'):
        cmd = [curl_path, '--silent', '--show-error', '--max-time', '30', '--proxy', proxy, url]
        out = None
        try:
            out = subprocess.check_output(cmd)
            return 200, out
        except Exception as e:
            raise
    else:
        req = request.Request(url, headers=HEADERS)
        with request.urlopen(req, timeout=30) as resp:
            code = resp.getcode()
            body = resp.read()
        return code, body


def download_symbol(sym, from_ms, dry_run=False):
    print('Downloading', sym, 'from', from_ms)
    cur = from_ms
    all_bars = []
    now_ms = int(time.time() * 1000)
    retries = 0
    while cur < now_ms:
        try:
            code, body = fetch_page(sym, cur)
            if code != 200:
                print('  HTTP code', code)
                time.sleep(1)
                retries += 1
                if retries > 6:
                    print('  too many retries, abort')
                    break
                continue
            arr = json.loads(body.decode('utf-8'))
            if not isinstance(arr, list) or not arr:
                # empty page -> advance by 1000 minutes
                cur += PAGE_LIMIT * 60 * 1000
                continue
            all_bars.extend(arr)
            # advance to last bar + 1 ms
            last_start = int(arr[-1][0])
            cur = last_start + 60 * 1000
            # brief pacing
            time.sleep(0.2 + random.random() * 0.1)
        except Exception as e:
            print('  error', e)
            time.sleep(2)
            retries += 1
            if retries > 6:
                print('  giving up after retries')
                break

    # Trim to most recent entries if huge
    if len(all_bars) > 1000000:
        all_bars = all_bars[-1000000:]

    data = {'1m': all_bars, 'indicators': {}}
    outp = os.path.join(OUTDIR, sym + '.json')
    tmp = outp + '.tmp'
    if dry_run:
        print('  dry-run: would write', outp, 'entries=', len(all_bars))
    else:
        with open(tmp, 'w') as wf:
            json.dump(data, wf)
        os.replace(tmp, outp)
        print('  wrote', outp, 'entries=', len(all_bars))


def load_symbols():
    cfg = os.path.join(BASE, 'config', 'symbols.json')
    try:
        with open(cfg) as f:
            j = json.load(f)
        syms = j.get('symbols', [])
        return [s for s in syms if isinstance(s, str)]
    except Exception:
        return []


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--from', dest='from_iso', required=True, help='ISO date, e.g. 2025-08-01 or 2025-08-01T00:00:00Z')
    p.add_argument('--symbols', nargs='*', help='override symbols from config')
    p.add_argument('--dry-run', action='store_true')
    args = p.parse_args()
    syms = args.symbols if args.symbols else load_symbols()
    start_ms = iso_to_ms(args.from_iso)
    for s in syms:
        download_symbol(s, start_ms, dry_run=args.dry_run)
