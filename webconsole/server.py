
# webconsole/server.py
# This file has been removed in favor of the C++ implementation (src/webconsole_server.cpp).
# The Python-based webconsole is deprecated and should not be used. Build and run the C++ binary:
#   mkdir -p build && cd build && cmake .. && make -j && ./crypto_trading

raise SystemExit('webconsole/server.py removed: use C++ webconsole')

def parse_line_into_state(line: str):
    line = line.rstrip('\n')
    if not line:
        return

    def strip_ansi(s):
        return re.sub(r'\x1b\[[0-9;?]*[0-9A-Za-z]', '', s)

    clean = strip_ansi(line).strip()
    if not clean:
        return

    m = re.search(r'STATE_JSON\s+(\{.*\})', clean)
    if m:
        try:
            j = json.loads(m.group(1))
            with state_lock:
                if 'status' in j:
                    state['status'] = j.get('status', '')
                if 'connection' in j:
                    state['connection'] = j.get('connection', '')
                if 'subscriptions' in j:
                    subs = j['subscriptions']
                    if isinstance(subs, list):
                        state['subscriptions'] = subs
                    elif isinstance(subs, str):
                        state['subscriptions'] = [s.strip() for s in subs.split(',') if s.strip()]
                if 'orders' in j:
                    val = j.get('orders', [])
                    if isinstance(val, list):
                        state['orders'] = val
                    elif isinstance(val, str):
                        state['orders'] = [val] if val else []
        except Exception:
            pass
        return

    if clean.startswith('Status:'):
        with state_lock:
            state['status'] = clean[len('Status:'):].strip()
        return

    if clean.startswith('Conn:'):
        with state_lock:
            state['connection'] = clean[len('Conn:'):].strip()
        return

    if clean.startswith('Subs:'):
        subs = clean[len('Subs:'):].strip()
        with state_lock:
            state['subscriptions'] = [s.strip() for s in subs.split(',') if s.strip()]
        return

    if clean.startswith('Orders:'):
        with state_lock:
            state['orders'] = []
        return

    low = clean.lower()
    if re.search(r'\border\b', low) or re.search(r'\bplaced\b', low):
        with state_lock:
            state['orders'].append(clean)


def tail_log_and_update_state():
    while True:
        try:
            if not os.path.exists(LOG_PATH):
                time.sleep(0.5)
                continue
            with open(LOG_PATH, 'r') as lf:
                try:
                    lf.seek(0)
                    for line in lf:
                        parse_line_into_state(line)
                except Exception:
                    pass
                lf.seek(0, os.SEEK_END)
                while True:
                    line = lf.readline()
                    if not line:
                        time.sleep(0.2)
                        continue
                    parse_line_into_state(line)
        except Exception:
            time.sleep(1)


def load_config_symbols():
    try:
        if os.path.exists(CONFIG_PATH):
            with open(CONFIG_PATH, 'r') as f:
                j = json.load(f)
            syms = j.get('symbols') if isinstance(j, dict) else None
            if isinstance(syms, list):
                return [s for s in syms if isinstance(s, str)]
    except Exception:
        pass
    return []


def poll_local_raw_data():
    """Background poller that copies data/raw/<SYM>.json -> data/latest/<SYM>.json when available.
    Assumes the C++ service or other producer writes raw 1m Binance-style kline arrays into data/raw/.
    When a file is updated, invalidate _klines_cache entries for that symbol so new requests use latest data.
    """
    outdir = pathlib.Path(HERE.parent) / 'data' / 'latest'
    rawdir = pathlib.Path(HERE.parent) / 'data' / 'raw'
    outdir.mkdir(parents=True, exist_ok=True)
    rawdir.mkdir(parents=True, exist_ok=True)
    last_mtimes = {}
    while True:
        try:
            syms = load_config_symbols()
            with state_lock:
                # keep state subscriptions in sync with config file
                if state.get('subscriptions') != syms:
                    state['subscriptions'] = syms
            for sym in syms:
                src = rawdir / (sym + '.json')
                dst = outdir / (sym + '.json')
                try:
                    if src.exists():
                        m = src.stat().st_mtime
                        if last_mtimes.get(sym) != m or not dst.exists():
                            # copy or update latest from raw
                            data = src.read_bytes()
                            # quick validate JSON
                            try:
                                _json.loads(data.decode('utf-8'))
                            except Exception:
                                # skip invalid data
                                last_mtimes[sym] = m
                                continue
                            dst.write_bytes(data)
                            last_mtimes[sym] = m
                            # invalidate cache entries for this symbol
                            keys = [k for k in list(_klines_cache.keys()) if k.startswith(sym + ':')]
                            for k in keys:
                                try:
                                    del _klines_cache[k]
                                except Exception:
                                    pass
                except Exception:
                    pass
            time.sleep(3)
        except Exception:
            time.sleep(3)


class SSEHandler(server.BaseHTTPRequestHandler):
    def _set_cors(self):
        self.send_header('Access-Control-Allow-Origin', '*')

    def _require_api_key(self):
        """Return True if request is authorized (or no key configured)."""
        if not API_KEY:
            return True
        # header first
        key = self.headers.get('X-API-Key') or self.headers.get('X-Api-Key')
        if key and key == API_KEY:
            return True
        # allow query param api_key
        qp = urllib.parse.urlparse(self.path).query
        params = urllib.parse.parse_qs(qp)
        if params.get('api_key', [''])[0] == API_KEY:
            return True
        return False

    def do_HEAD(self):
        # Mirror common GET headers without body
        if self.path == '/favicon.ico':
            self.send_response(204)
            self.send_header('Content-type', 'image/x-icon')
            self._set_cors()
            self.end_headers()
            return

        if self.path.startswith('/static/'):
            rel = self.path[len('/static/'):]
            target = os.path.join(HERE, 'static', rel)
            if os.path.exists(target):
                ctype = 'application/javascript' if target.endswith('.js') else 'application/octet-stream'
                self.send_response(200)
                self.send_header('Content-type', ctype)
                self._set_cors()
                self.end_headers()
                return
            self.send_response(404)
            self._set_cors()
            self.end_headers()
            return

        if self.path.startswith('/klines'):
            if not self._require_api_key():
                self.send_response(401)
                self._set_cors()
                self.end_headers()
                return
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self._set_cors()
            self.end_headers()
            return

        if self.path == '/' or self.path.startswith('/index.html'):
            self.send_response(200)
            self.send_header('Content-type', 'text/html')
            self._set_cors()
            self.end_headers()
            return

        if self.path == '/events':
            if not self._require_api_key():
                self.send_response(401)
                self._set_cors()
                self.end_headers()
                return
            self.send_response(200)
            self.send_header('Content-type', 'text/event-stream')
            self._set_cors()
            self.end_headers()
            return

        if self.path == '/status':
            if not self._require_api_key():
                self.send_response(401)
                self._set_cors()
                self.end_headers()
                return
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self._set_cors()
            self.end_headers()
            return

        self.send_response(404)
        self._set_cors()
        self.end_headers()

    def do_GET(self):
        try:
            # favicon
            if self.path == '/favicon.ico':
                self.send_response(204)
                self.send_header('Content-type', 'image/x-icon')
                self._set_cors()
                self.end_headers()
                return

            # /klines handler
            if self.path.startswith('/klines'):
                if not self._require_api_key():
                    self.send_response(401)
                    self._set_cors()
                    self.end_headers()
                    return
                return self._handle_klines()

            # static files
            if self.path.startswith('/static/'):
                rel = self.path[len('/static/'):]
                target = os.path.join(HERE, 'static', rel)
                if os.path.exists(target):
                    ctype = 'application/javascript' if target.endswith('.js') else 'application/octet-stream'
                    self.send_response(200)
                    self.send_header('Content-type', ctype)
                    self._set_cors()
                    self.end_headers()
                    with open(target, 'rb') as f:
                        self.wfile.write(f.read())
                    return
                # attempt to fetch known remote library (lightweight-charts)
                if rel == 'lightweight-charts.standalone.production.js':
                    cdn_urls = [
                        'https://unpkg.com/lightweight-charts@3.9.0/dist/lightweight-charts.standalone.production.js',
                        'https://cdn.jsdelivr.net/npm/lightweight-charts@3.9.0/dist/lightweight-charts.standalone.production.js',
                        'https://cdn.skypack.dev/lightweight-charts@3.9.0/dist/lightweight-charts.standalone.production.js'
                    ]
                    os.makedirs(os.path.join(HERE, 'static'), exist_ok=True)
                    for url in cdn_urls:
                        try:
                            req = urllib.request.Request(url, headers={'User-Agent': 'crypto_webconsole/1.0'})
                            with urllib.request.urlopen(req, timeout=12) as resp:
                                data = resp.read()
                            with open(target, 'wb') as wf:
                                wf.write(data)
                            self.send_response(200)
                            self.send_header('Content-type', 'application/javascript')
                            self._set_cors()
                            self.end_headers()
                            self.wfile.write(data)
                            return
                        except Exception:
                            continue
                self.send_response(404)
                self._set_cors()
                self.end_headers()
                return

            # index
            if self.path == '/' or self.path.startswith('/index.html'):
                self.send_response(200)
                self.send_header('Content-type', 'text/html')
                self._set_cors()
                self.end_headers()
                with open(os.path.join(HERE, 'index.html'), 'rb') as f:
                    self.wfile.write(f.read())
                return

            # events - SSE
            if self.path == '/events':
                if not self._require_api_key():
                    self.send_response(401)
                    self._set_cors()
                    self.end_headers()
                    return
                self.send_response(200)
                self.send_header('Content-type', 'text/event-stream')
                self.send_header('Cache-Control', 'no-cache')
                self._set_cors()
                self.end_headers()
                try:
                    with open(LOG_PATH, 'r') as lf:
                        lf.seek(0, os.SEEK_END)
                        with state_lock:
                            st = json.dumps(state)
                        self.wfile.write(('event: state\ndata: %s\n\n' % st).encode('utf-8'))
                        self.wfile.flush()
                        last_state_send = time.time()
                        while True:
                            line = lf.readline()
                            if not line:
                                if time.time() - last_state_send > 1.0:
                                    with state_lock:
                                        st = json.dumps(state)
                                    self.wfile.write(('event: state\ndata: %s\n\n' % st).encode('utf-8'))
                                    self.wfile.flush()
                                    last_state_send = time.time()
                                time.sleep(0.2)
                                continue
                            msg = 'data: ' + line.strip() + '\n\n'
                            self.wfile.write(msg.encode('utf-8'))
                            self.wfile.flush()
                            with state_lock:
                                st = json.dumps(state)
                            self.wfile.write(('event: state\ndata: %s\n\n' % st).encode('utf-8'))
                            self.wfile.flush()
                except Exception:
                    try:
                        self.wfile.write(('data: [error] %s\n\n' % traceback.format_exc()).encode('utf-8'))
                    except Exception:
                        pass
                return

            # status
            if self.path == '/status':
                if not self._require_api_key():
                    self.send_response(401)
                    self._set_cors()
                    self.end_headers()
                    return
                self.send_response(200)
                self.send_header('Content-type', 'application/json')
                self._set_cors()
                self.end_headers()
                with state_lock:
                    self.wfile.write(json.dumps(state).encode('utf-8'))
                return

            # list configured symbols
            if self.path.startswith('/symbols'):
                syms = load_config_symbols()
                self.send_response(200)
                self.send_header('Content-type', 'application/json')
                self._set_cors()
                self.end_headers()
                self.wfile.write(json.dumps({'symbols': syms}).encode('utf-8'))
                return

            # trigger a local fetch/copy from data/raw -> data/latest (optional symbol param)
            if self.path.startswith('/fetch-local'):
                qp = urllib.parse.urlparse(self.path).query
                params = urllib.parse.parse_qs(qp)
                sym = params.get('symbol', [None])[0]
                rawdir = pathlib.Path(HERE.parent) / 'data' / 'raw'
                outdir = pathlib.Path(HERE.parent) / 'data' / 'latest'
                outdir.mkdir(parents=True, exist_ok=True)
                results = {}
                def try_copy(s):
                    src = rawdir / (s + '.json')
                    dst = outdir / (s + '.json')
                    if not src.exists():
                        return False
                    try:
                        data = src.read_bytes()
                        _json.loads(data.decode('utf-8'))
                        dst.write_bytes(data)
                        # invalidate cache keys
                        keys = [k for k in list(_klines_cache.keys()) if k.startswith(s + ':')]
                        for k in keys:
                            try:
                                del _klines_cache[k]
                            except Exception:
                                pass
                        return True
                    except Exception:
                        return False

                if sym:
                    ok = try_copy(sym)
                    results[sym] = ok
                else:
                    syms = load_config_symbols()
                    for s in syms:
                        results[s] = try_copy(s)

                self.send_response(200)
                self.send_header('Content-type', 'application/json')
                self._set_cors()
                self.end_headers()
                self.wfile.write(json.dumps({'results': results}).encode('utf-8'))
                return

            # not found
            self.send_response(404)
            self.end_headers()
            return

        except Exception:
            tb = traceback.format_exc()
            try:
                print('Unhandled exception in do_GET:', tb)
            except Exception:
                pass
            try:
                self.send_response(500)
                self.send_header('Content-type', 'text/plain')
                self._set_cors()
                self.end_headers()
                self.wfile.write(b'Internal server error')
            except Exception:
                pass

    def _handle_klines(self):
        # Extract params
        qp = urllib.parse.urlparse(self.path).query
        params = urllib.parse.parse_qs(qp)
        symbol = params.get('symbol', [''])[0]
        interval = params.get('interval', ['1m'])[0]
        # normalize limit to an int with sensible bounds
        try:
            limit_i = int(params.get('limit', ['500'])[0])
        except Exception:
            limit_i = 500
        limit_i = max(1, min(1000, limit_i))
        if not symbol:
            self.send_response(400)
            self.end_headers()
            self.wfile.write(b'{}')
            return

        compact = params.get('compact', ['0'])[0]
        compact_flag = str(compact).lower() in ('1', 'true', 'yes')
        key = f"{symbol}:{interval}:{limit_i}:compact={int(bool(compact_flag))}"
        now = _time()
        cached = _klines_cache.get(key)
        body = None

        def compute_sma(arr, window):
            res = []
            s = 0.0
            q = []
            for i, c in enumerate(arr):
                try:
                    close = float(c[4])
                except Exception:
                    close = 0.0
                q.append(close)
                s += close
                if len(q) > window:
                    s -= q.pop(0)
                if len(q) == window:
                    res.append([int(c[0])//1000, s / window])
            return res

        def compute_vwap(arr, window):
            res = []
            pv = []
            vol = []
            for i, c in enumerate(arr):
                try:
                    typical = (float(c[2]) + float(c[3]) + float(c[4])) / 3.0
                    v = float(c[5])
                except Exception:
                    typical = 0.0; v = 0.0
                pv.append(typical * v)
                vol.append(v)
                if len(pv) > window:
                    pv.pop(0); vol.pop(0)
                if len(pv) == window:
                    s_pv = sum(pv); s_vol = sum(vol)
                    val = (s_pv / s_vol) if s_vol > 0 else 0.0
                    res.append([int(c[0])//1000, val])
            return res

        if cached and now - cached[0] < _KLINES_TTL:
            body = cached[1]
        else:
            # Use local persisted data only (no remote Binance calls)
            try:
                outdir = HERE.parent / 'data' / 'latest'
                fp = outdir / (symbol + '.json')
                if not fp.exists():
                    # no local data available
                    body = None
                else:
                    with open(fp, 'rb') as rf:
                        raw = rf.read()
                    try:
                        arr = _json.loads(raw.decode('utf-8'))
                    except Exception:
                        arr = []

                    # helper: aggregate 1m base data into requested interval
                    def aggregate_to_interval(raw_arr, interval_str):
                        # raw_arr: list of binance kline arrays using ms timestamps
                        if not isinstance(raw_arr, list):
                            return []
                        # mapping to seconds
                        mapping = {'1m': 60, '30m': 30 * 60, '4h': 4 * 60 * 60}
                        if interval_str not in mapping:
                            # unsupported interval, fallback to raw
                            return raw_arr
                        bucket_s = mapping[interval_str]
                        # convert raw bars to (ts_s, open, high, low, close, volume)
                        rows = []
                        for c in raw_arr:
                            try:
                                ts = int(c[0]) // 1000
                                o = float(c[1])
                                h = float(c[2])
                                l = float(c[3])
                                cl = float(c[4])
                                v = float(c[5])
                                rows.append((ts, o, h, l, cl, v))
                            except Exception:
                                continue
                        if not rows:
                            return []
                        # group by bucket
                        groups = {}
                        order = []
                        for ts, o, h, l, cl, v in rows:
                            bucket = (ts // bucket_s) * bucket_s
                            if bucket not in groups:
                                groups[bucket] = {'open': o, 'high': h, 'low': l, 'close': cl, 'volume': v, 'first_ts': ts, 'last_ts': ts}
                                order.append(bucket)
                            else:
                                g = groups[bucket]
                                g['high'] = max(g['high'], h)
                                g['low'] = min(g['low'], l)
                                g['close'] = cl
                                g['volume'] += v
                                g['last_ts'] = ts
                        # build aggregated list in ascending order
                        ag = []
                        order.sort()
                        for b in order:
                            g = groups[b]
                            # produce Binance-style array minimal fields: open_time_ms, open, high, low, close, volume, close_time_ms
                            open_ms = b * 1000
                            close_ms = (b + bucket_s) * 1000 - 1
                            ag.append([open_ms, format(g['open'], '.8f'), format(g['high'], '.8f'), format(g['low'], '.8f'), format(g['close'], '.8f'), format(g['volume'], '.8f'), close_ms])
                        return ag

                    # aggregate as needed
                    try:
                        arr = aggregate_to_interval(arr, interval)
                    except Exception:
                        pass

                    # enforce requested limit (slice to most recent bars)
                    try:
                        if isinstance(arr, list):
                            arr = arr[-limit_i:]
                    except Exception:
                        pass

                    # prepare indicators (timestamps in seconds) from aggregated arr
                    indicators = {'sma20': compute_sma(arr, 20), 'sma50': compute_sma(arr, 50), 'vwap20': compute_vwap(arr, 20)}
                    if compact_flag:
                        kl = []
                        for c in arr:
                            try:
                                kl.append({'time': int(c[0])//1000, 'open': float(c[1]), 'high': float(c[2]), 'low': float(c[3]), 'close': float(c[4]), 'volume': float(c[5])})
                            except Exception:
                                continue
                        out = {'klines': kl, 'indicators': indicators}
                    else:
                        out = {'klines': arr, 'indicators': indicators}
                    body = _json.dumps(out).encode('utf-8')
                    _klines_cache[key] = (now, body)
            except Exception:
                body = None

        if body is not None:
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            self.wfile.write(body)
            return

        self.send_response(502)
        self.send_header('Content-type', 'application/json')
        self.send_header('Access-Control-Allow-Origin', '*')
        self.end_headers()
        self.wfile.write(str({'error': 'klines fetch failed and no fallback available'}).encode('utf-8'))


def run(port: int):
    t = Thread(target=tail_log_and_update_state, daemon=True)
    t.start()
    p = Thread(target=poll_local_raw_data, daemon=True)
    p.start()
    server_address = ('', port)
    httpd = server.ThreadingHTTPServer(server_address, SSEHandler)
    print('Starting webconsole on port', port)
    httpd.serve_forever()


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--port', type=int, default=8080)
    args = p.parse_args()
    run(args.port)
