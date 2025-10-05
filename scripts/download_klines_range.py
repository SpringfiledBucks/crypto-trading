#!/usr/bin/env python3
"""
Download 1m klines from Binance (fapi) for a given symbol from a start date to now.
Writes per-month gzipped JSON files under archive/klines/<SYMBOL>/YYYY-MM.json.gz
Also writes/updates data/latest/<SYMBOL>.json with the most recent 500 bars to be served to the webconsole.
Supports proxies via HTTP_PROXY/HTTPS_PROXY env; if proxy starts with 'socks' and curl exists, uses curl.

Usage:
  scripts/download_klines_range.py --symbol BTCUSDT --start 2025-08-01
  scripts/download_klines_range.py --symbols BTCUSDT ETHUSDT --start 2025-08-01

This script pages using Binance API's startTime/endTime/limit (max 1000 per request).
"""

import os, sys, json, time, argparse, shutil, subprocess, gzip, datetime, threading, queue
from urllib import request, parse, error

BASE = os.path.dirname(os.path.dirname(__file__))
OUTDIR_LATEST = os.path.join(BASE, 'data', 'latest')
OUTDIR_ARCHIVE = os.path.join(BASE, 'archive', 'klines')
CONFIG_SYMBOLS = os.path.join(BASE, 'config', 'symbols.json')

BASE_URL = 'https://fapi.binance.com/fapi/v1/klines'
INTERVAL = '1m'
LIMIT = 1000
HEADERS = {'User-Agent': 'crypto-trading/1.0'}

os.makedirs(OUTDIR_LATEST, exist_ok=True)


class TokenBucket:
    def __init__(self, rate):
        self.rate = float(rate)
        self.capacity = max(1.0, float(rate))
        self._tokens = self.capacity
        self._last = time.time()
        self.lock = threading.Lock()

    def consume(self, amount=1.0):
        with self.lock:
            now = time.time()
            delta = now - self._last
            self._tokens = min(self.capacity, self._tokens + delta * self.rate)
            self._last = now
            if self._tokens >= amount:
                self._tokens -= amount
                return True
            return False


def iso_to_ms(s):
    dt = datetime.datetime.strptime(s, '%Y-%m-%d')
    return int(dt.replace(tzinfo=datetime.timezone.utc).timestamp() * 1000)


def ms_to_iso(ms):
    # use timezone-aware API to avoid deprecation
    return datetime.datetime.fromtimestamp(ms/1000, datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')


def ensure_dir(p):
    os.makedirs(p, exist_ok=True)


# prefer urllib but if socks proxy and curl present use curl
curl_path = shutil.which('curl')
proxy = os.environ.get('HTTP_PROXY') or os.environ.get('HTTPS_PROXY')
use_curl_socks = False
if curl_path and proxy and proxy.startswith('socks'):
    use_curl_socks = True

proxy_handler = request.ProxyHandler({'http': os.environ.get('HTTP_PROXY',''), 'https': os.environ.get('HTTPS_PROXY','')})
opener = request.build_opener(proxy_handler)
request.install_opener(opener)


def fetch_klines(sym, start_ms, end_ms):
    params = {'symbol': sym, 'interval': INTERVAL, 'startTime': str(start_ms), 'endTime': str(end_ms), 'limit': str(LIMIT)}
    url = BASE_URL + '?' + parse.urlencode(params)
    if use_curl_socks:
        cmd = [curl_path, '--silent', '--show-error', '--max-time', '30', '--proxy', proxy, url]
        try:
            out = subprocess.check_output(cmd)
            return 200, out, {}
        except subprocess.CalledProcessError as e:
            raise error.URLError(str(e))
    else:
        req = request.Request(url, headers=HEADERS)
        with request.urlopen(req, timeout=30) as resp:
            code = resp.getcode()
            body = resp.read()
            headers = dict(resp.getheaders())
        return code, body, headers


def fetch_exchange_info_rate():
    url = 'https://fapi.binance.com/fapi/v1/exchangeInfo'
    try:
        code, body, headers = fetch_generic(url)
        if code == 200:
            ei = json.loads(body.decode('utf-8'))
            # find REQUEST_WEIGHT per minute
            req_weight = None
            for r in ei.get('rateLimits', []):
                if r.get('rateLimitType') == 'REQUEST_WEIGHT' and r.get('interval') == 'MINUTE':
                    req_weight = int(r.get('limit', 6000))
                    break
            if req_weight is None:
                req_weight = 6000
            # set refill per sec as limit/60 * safety_factor
            refill_per_sec = (req_weight / 60.0) * 0.6
            print('exchangeInfo REQUEST_WEIGHT=', req_weight, ' -> rate/sec=', refill_per_sec)
            return refill_per_sec
    except Exception as e:
        print('failed to fetch exchangeInfo', e)
    return None


def fetch_generic(url):
    curl_path = shutil.which('curl')
    proxy = os.environ.get('HTTP_PROXY') or os.environ.get('HTTPS_PROXY')
    if curl_path and proxy and proxy.startswith('socks'):
        cmd = [curl_path, '--silent', '--show-error', '--max-time', '20', '--proxy', proxy, url]
        out = subprocess.check_output(cmd)
        return 200, out, {}
    else:
        req = request.Request(url, headers=HEADERS)
        with request.urlopen(req, timeout=20) as resp:
            body = resp.read()
            return resp.getcode(), body, dict(resp.getheaders())


def write_month_archive(sym, month_key, klines):
    # month_key like YYYY-MM
    d = os.path.join(OUTDIR_ARCHIVE, sym)
    ensure_dir(d)
    fn = os.path.join(d, f'{month_key}.json.gz')
    tmp = fn + '.tmp'
    with gzip.open(tmp, 'wt', encoding='utf-8') as wf:
        json.dump(klines, wf)
    os.replace(tmp, fn)
    print('WROTE', fn)


def run_full_pipeline(sym):
    # convert month archives to binary and compute indicators for latest
    try:
        subprocess.check_call(['python3', os.path.join(BASE, 'scripts', 'klines_to_month_bin.py'), '--symbol', sym])
    except Exception as e:
        print('  full-pipeline: conversion failed', e)
    try:
        subprocess.check_call(['python3', os.path.join(BASE, 'scripts', 'compute_indicators.py'), '--symbol', sym])
    except Exception as e:
        print('  full-pipeline: indicators failed', e)


def load_existing_latest(sym):
    p = os.path.join(OUTDIR_LATEST, f'{sym}.json')
    if not os.path.exists(p):
        return {'1m': [], 'indicators': {}}
    try:
        with open(p, 'r') as f:
            return json.load(f)
    except Exception:
        return {'1m': [], 'indicators': {}}


def write_latest(sym, klines, max_bars=500):
    data = {'1m': klines[-max_bars:], 'indicators': {}}
    tmp = os.path.join(OUTDIR_LATEST, f'{sym}.json.tmp')
    outp = os.path.join(OUTDIR_LATEST, f'{sym}.json')
    with open(tmp, 'w') as wf:
        json.dump(data, wf)
    os.replace(tmp, outp)
    print('UPDATED LATEST', outp, 'bars=', len(data['1m']))


def daterange_months(start_dt, end_dt):
    current = datetime.date(start_dt.year, start_dt.month, 1)
    while current <= end_dt:
        yield current.year, current.month
        if current.month == 12:
            current = datetime.date(current.year+1,1,1)
        else:
            current = datetime.date(current.year, current.month+1,1)


def download_symbol(sym, start_iso, end_iso=None):
    start_ms = iso_to_ms(start_iso)
    end_ms = int(time.time() * 1000) if end_iso is None else iso_to_ms(end_iso)
    print(f'Downloading {sym} from {ms_to_iso(start_ms)} to {ms_to_iso(end_ms)} using proxy={proxy}')
    # if month archive exists, skip that month (simple resume)
    all_klines = []
    cur_start = start_ms
    checkpoint_dir = os.path.join(OUTDIR_ARCHIVE, sym)
    ensure_dir(checkpoint_dir)
    checkpoint_file = os.path.join(checkpoint_dir, '.progress')
    if os.path.exists(checkpoint_file):
        try:
            with open(checkpoint_file) as f:
                cur_start = int(f.read().strip())
                print('  resuming from checkpoint', ms_to_iso(cur_start))
        except Exception:
            pass
    max_retries = 6
    while cur_start < end_ms:
        attempt = 0
        while attempt <= max_retries:
            try:
                # Binance limit=1000. We'll set end as end_ms but API returns up to 1000 starting from startTime
                code, body, headers = fetch_klines(sym, cur_start, end_ms)
                if code != 200:
                    raise Exception(f'HTTP {code}')
                arr = json.loads(body.decode('utf-8'))
                if not isinstance(arr, list):
                    raise Exception('unexpected response')
                if len(arr) == 0:
                    # no more data
                    cur_start = end_ms
                    break
                # append
                all_klines.extend(arr)
                # progress: next start should be last returned bar's open time + 1 ms
                last_open = int(arr[-1][0])
                cur_start = last_open + 1
                print(f'  fetched {len(arr)} bars, next start={ms_to_iso(cur_start)} total={len(all_klines)}')
                break
            except Exception as e:
                attempt += 1
                backoff = 2 ** attempt
                print('  fetch error', e, 'backoff', backoff)
                time.sleep(backoff)
        else:
            print('  giving up after retries')
            break
        # small sleep to respect rate limits
        time.sleep(0.2)

    if not all_klines:
        print('No klines fetched for', sym)
        return

    # write per-month archives: group by year-month using the open time (ms)
    by_month = {}
    for k in all_klines:
        ts = int(k[0])
        dt = datetime.datetime.fromtimestamp(ts/1000, datetime.timezone.utc)
        key = f'{dt.year:04d}-{dt.month:02d}'
        by_month.setdefault(key, []).append(k)
    for key, arr in by_month.items():
        path = os.path.join(OUTDIR_ARCHIVE, sym, f'{key}.json.gz')
        if os.path.exists(path):
            print('  month archive exists, skipping', path)
        else:
            write_month_archive(sym, key, arr)

    # save checkpoint
    try:
        with open(checkpoint_file, 'w') as f:
            f.write(str(cur_start))
    except Exception:
        pass

    # run full pipeline post-processing if desired
    if os.environ.get('FULL_PIPELINE') == '1' or globals().get('FULL_PIPELINE_FLAG', False):
        run_full_pipeline(sym)

    # update latest with last 500 bars
    write_latest(sym, all_klines, max_bars=500)
    # after writing latest, invoke incremental indicator computation for this symbol (fast)
    try:
        # compute_indicators accepts --symbol and optional --since-ts (ms). We pass since-ts as the earliest ts we have
        earliest = int(all_klines[0][0]) if len(all_klines) else None
        cmd = ['python3', os.path.join(BASE, 'scripts', 'compute_indicators.py'), '--symbol', sym]
        if earliest:
            cmd += ['--since-ts', str(earliest)]
        subprocess.check_call(cmd)
    except Exception as e:
        print('  incremental indicators failed', e)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--symbols', nargs='*', help='symbols to download (default: config/symbols.json list)')
    parser.add_argument('--symbol', help='single symbol (alias for --symbols)')
    parser.add_argument('--start', required=True, help='start date YYYY-MM-DD')
    parser.add_argument('--end', help='end date YYYY-MM-DD (default now)')
    parser.add_argument('--workers', type=int, default=1, help='number of parallel workers (default 1)')
    parser.add_argument('--rate', type=float, default=2.0, help='approx requests per second token-bucket refill rate (default 2)')
    parser.add_argument('--full-pipeline', action='store_true', help='after download convert months to bin and compute indicators')
    parser.add_argument('--use-exchange-info', action='store_true', help='fetch exchangeInfo and adapt token-bucket rate')
    args = parser.parse_args()

    syms = []
    if args.symbol: syms = [args.symbol]
    if args.symbols: syms = args.symbols
    if not syms:
        try:
            with open(CONFIG_SYMBOLS) as f:
                cfg = json.load(f)
                raw = cfg.get('symbols', cfg)
                # support either ["BTCUSDT", ...] or [{"symbol":"BTCUSDT"}, ...]
                if isinstance(raw, list) and raw and isinstance(raw[0], str):
                    syms = raw
                else:
                    syms = [s.get('symbol') for s in raw if isinstance(s, dict) and s.get('symbol')]
        except Exception:
            print('No symbols specified and failed to read config/symbols.json')
            sys.exit(1)

    # worker queue
    global FULL_PIPELINE_FLAG
    FULL_PIPELINE_FLAG = args.full_pipeline
    # optionally fetch exchangeInfo to tune rate
    rate = args.rate
    if args.use_exchange_info:
        r = fetch_exchange_info_rate()
        if r:
            rate = r
    tb = TokenBucket(rate=rate)
    q = queue.Queue()
    for s in syms:
        q.put(s)

    def worker():
        while not q.empty():
            s = q.get()
            # pace: ensure token available before each download_symbol call
            while not tb.consume(1.0):
                time.sleep(0.05)
            try:
                download_symbol(s, args.start, args.end)
            except Exception as e:
                print('error downloading', s, e)
            q.task_done()

    threads = []
    for i in range(args.workers):
        t = threading.Thread(target=worker)
        t.start()
        threads.append(t)
    q.join()
    for t in threads:
        t.join()

if __name__ == '__main__':
    main()
