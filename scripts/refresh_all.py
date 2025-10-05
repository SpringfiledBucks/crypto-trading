#!/usr/bin/env python3
"""
Safe refresh of all symbols' 1m klines into data/latest using proxy settings from scripts/run.sh.
Strategy:
 - read config/symbols.json
 - for each symbol fetch /fapi/v1/klines?interval=1m&limit=1440
 - respect Binance weight limits (conservative): default 1200 weight/min -> 20 req/sec global; we use 3 req/sec (0.33s sleep) safe.
 - on HTTP 429 or network errors use exponential backoff, up to max_retries
 - atomic write to data/latest/<sym>.json
"""

import os, json, time, sys, random, argparse
from urllib import request, parse, error
import subprocess
import shutil

# base paths
BASE = os.path.dirname(os.path.dirname(__file__))
RUN_SH = os.path.join(BASE, 'scripts', 'run.sh')
# Proxy defaults are what run.sh sets; we will pick them up from environment when the user runs this script via run.sh

CONFIG = os.path.join(BASE, 'config', 'symbols.json')
OUTDIR = os.path.join(BASE, 'data', 'latest')
os.makedirs(OUTDIR, exist_ok=True)

# conservative defaults (will be overridden by exchangeInfo when available)
REQ_INTERVAL = 1.0  # baseline seconds between requests
MAX_RETRIES = 6
BACKOFF_BASE = 2.0


class TokenBucket:
    def __init__(self, capacity, refill_per_sec):
        self.capacity = float(capacity)
        self.refill = float(refill_per_sec)
        self._tokens = float(capacity)
        self._last = time.time()

    def _add(self):
        now = time.time()
        delta = now - self._last
        self._tokens = min(self.capacity, self._tokens + delta * self.refill)
        self._last = now

    def consume(self, amount=1.0):
        self._add()
        if self._tokens >= amount:
            self._tokens -= amount
            return True
        return False


BASE_URL = 'https://fapi.binance.com/fapi/v1/klines'
LIMIT = 1440
INTERVAL = '1m'
HEADERS = {'User-Agent': 'crypto-trading/1.0'}

with open(CONFIG) as f:
    syms = json.load(f).get('symbols', [])

print('Refreshing {} symbols into {}'.format(len(syms), OUTDIR))
print('Using proxy from environment if set: HTTP_PROXY={}, HTTPS_PROXY={}'.format(os.environ.get('HTTP_PROXY'), os.environ.get('HTTPS_PROXY')))

# setup opener to respect environment proxies
proxy_handler = request.ProxyHandler({'http': os.environ.get('HTTP_PROXY',''), 'https': os.environ.get('HTTPS_PROXY','')})
opener = request.build_opener(proxy_handler)
request.install_opener(opener)

def fetch_klines(sym):
    params = {'symbol': sym, 'interval': INTERVAL, 'limit': str(LIMIT)}
    url = BASE_URL + '?' + parse.urlencode(params)
    # If curl is available and HTTP_PROXY indicates socks5, prefer curl which supports socks5h
    curl_path = shutil.which('curl')
    proxy = os.environ.get('HTTP_PROXY') or os.environ.get('HTTPS_PROXY')
    if curl_path and proxy and proxy.startswith('socks'):
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


def fetch_exchange_info():
    url = 'https://fapi.binance.com/fapi/v1/exchangeInfo'
    curl_path = shutil.which('curl')
    proxy = os.environ.get('HTTP_PROXY') or os.environ.get('HTTPS_PROXY')
    if curl_path and proxy and proxy.startswith('socks'):
        cmd = [curl_path, '--silent', '--show-error', '--max-time', '20', '--proxy', proxy, url]
        try:
            out = subprocess.check_output(cmd)
            return json.loads(out.decode('utf-8'))
        except Exception as e:
            print('  warning: failed to fetch exchangeInfo via curl ->', e)
            return None
    else:
        req = request.Request(url, headers=HEADERS)
        try:
            with request.urlopen(req, timeout=20) as resp:
                body = resp.read()
                return json.loads(body.decode('utf-8'))
        except Exception as e:
            print('  warning: failed to fetch exchangeInfo ->', e)
            return None

errors = {}

parser = argparse.ArgumentParser()
parser.add_argument('--sample', action='store_true', help='do a sample run (limit=5)')
parser.add_argument('--dry-run', action='store_true', help='don\'t write files, just simulate')
parser.add_argument('--symbols', nargs='*', help='optional list of symbols to refresh')
args = parser.parse_args()

if args.symbols:
    syms = args.symbols

if args.sample:
    LIMIT = 5
    print('Running in sample mode: LIMIT=5')

# Try to get exchangeInfo and init token bucket
ei = fetch_exchange_info()
if ei and 'rateLimits' in ei:
    # find REQUEST_WEIGHT limit (per minute)
    req_weight = None
    for r in ei['rateLimits']:
        if r.get('rateLimitType') == 'REQUEST_WEIGHT' and r.get('interval') == 'MINUTE':
            req_weight = int(r.get('limit', 6000))
            break
    if req_weight is None:
        req_weight = 6000
    print('Exchange reported REQUEST_WEIGHT per minute =', req_weight)
    # prepare token bucket: refill_per_sec = (limit/60) * safety_factor
    refill_per_sec = (req_weight / 60.0) * 0.6
    capacity = max(10, req_weight * 0.1)
    bucket = TokenBucket(capacity=capacity, refill_per_sec=refill_per_sec)
    print(f'Initialized TokenBucket capacity={capacity} refill_per_sec={refill_per_sec:.2f} (60% safety factor)')
else:
    print('No exchangeInfo available, using conservative defaults (1 req/sec)')
    bucket = TokenBucket(capacity=10, refill_per_sec=1.0)
for i, sym in enumerate(syms):
    print('\n[{}/{}] Refreshing {}'.format(i+1, len(syms), sym))
    attempt = 0
    while attempt <= MAX_RETRIES:
        # consume token (endpoint weight = 1)
        waited = 0.0
        while not bucket.consume(1.0):
            time.sleep(0.2)
            waited += 0.2
            if waited > 30.0:
                print('  waited >30s for token, continuing')
                break
        # jitter to avoid rigid patterns
        time.sleep(random.uniform(0, 0.2))
        try:
            code, body, resp_headers = fetch_klines(sym)
            if code == 200:
                arr = json.loads(body.decode('utf-8'))
                if not isinstance(arr, list):
                    # if API returned JSON error like {"code":-1121,...}, treat as invalid symbol and stop retrying
                    try:
                        err = json.loads(body.decode('utf-8'))
                        if isinstance(err, dict) and err.get('code') == -1121:
                            print('  API error: invalid symbol, writing empty fallback and skipping')
                            tmp = os.path.join(OUTDIR, sym + '.json.tmp')
                            outp = os.path.join(OUTDIR, sym + '.json')
                            with open(tmp, 'w') as wf:
                                json.dump({'1m': [], 'indicators': {}}, wf)
                            os.replace(tmp, outp)
                            errors[sym] = 'Invalid symbol'
                            break
                    except Exception:
                        pass
                    raise ValueError('unexpected response type')
                data = {'1m': arr, 'indicators': {}}
                tmp = os.path.join(OUTDIR, sym + '.json.tmp')
                outp = os.path.join(OUTDIR, sym + '.json')
                if args.dry_run:
                    print('  dry-run: would write', outp, 'entries=', len(arr))
                else:
                    with open(tmp, 'w') as wf:
                        json.dump(data, wf)
                    os.replace(tmp, outp)
                    print('  wrote', outp, 'size=', os.path.getsize(outp))
                break
            else:
                print('  HTTP code', code)
                # treat like error for retry; build a fake HTTPError but attach headers if present
                he = error.HTTPError(BASE_URL, code, 'HTTP', hdrs=None, fp=None)
                # if server provided Retry-After, push into response headers for handling below
                raise he
        except error.HTTPError as he:
            code = getattr(he, 'code', None)
            print('  HTTPError', code if code else he)
            if code in (429, 418):
                # try to read Retry-After from last response headers if available
                retry_after = None
                try:
                    # if we have resp_headers from last successful read in scope, use it
                    retry_after = int(resp_headers.get('Retry-After')) if 'resp_headers' in locals() and resp_headers.get('Retry-After') else None
                except Exception:
                    retry_after = None
                if retry_after:
                    sleep = retry_after
                else:
                    sleep = BACKOFF_BASE ** attempt
                print('  rate limited, backoff {:.1f}s (attempt {})'.format(sleep, attempt+1))
                time.sleep(sleep)
                attempt += 1
                continue
            else:
                print('  non-rate HTTP error for', sym, '->', he)
                errors[sym] = str(he)
                break
        except Exception as e:
            print('  network/error:', e)
            sleep = BACKOFF_BASE ** attempt
            print('  backoff {:.1f}s (attempt {})'.format(sleep, attempt+1))
            time.sleep(sleep)
            attempt += 1
            if attempt > MAX_RETRIES:
                print('  giving up on', sym)
                errors[sym] = str(e)
                # write safe fallback so UI won't break
                try:
                    tmp = os.path.join(OUTDIR, sym + '.json.tmp')
                    outp = os.path.join(OUTDIR, sym + '.json')
                    with open(tmp, 'w') as wf:
                        json.dump({'1m': [], 'indicators': {}}, wf)
                    os.replace(tmp, outp)
                    print('  wrote empty fallback for', sym)
                except Exception as e2:
                    print('  failed to write fallback for', sym, '->', e2)
                break
    # global pacing
    time.sleep(REQ_INTERVAL)

print('\nRefresh complete. {} errors.'.format(len(errors)))
if errors:
    print(json.dumps(errors, indent=2))

