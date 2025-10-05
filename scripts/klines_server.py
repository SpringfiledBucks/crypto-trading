#!/usr/bin/env python3
"""
Simple on-demand klines HTTP server.

Endpoints:
  GET /klines?symbol=BTCUSDT&start=2025-08-01T00:00:00Z&end=2025-10-06T00:00:00Z&limit=2000

Reads monthly archives from archive/klines/<SYM>/YYYY-MM.json.gz, merges months, filters by start/end (ISO Z) and returns JSON:
  { "symbol": "BTCUSDT", "klines": [ ... ] }

No external deps. Adds CORS header so frontend can call it directly.
"""

import http.server
import socketserver
import urllib.parse
import os
import json
import gzip
import datetime
import sys

BASE = os.path.dirname(os.path.dirname(__file__))
OUTDIR_ARCHIVE = os.path.join(BASE, 'archive', 'klines')

PORT = int(os.environ.get('KLINES_PORT', '8081'))


def parse_iso_to_ms(s):
    # accept YYYY-MM-DD or full ISO with Z
    try:
        if s.endswith('Z'):
            dt = datetime.datetime.fromisoformat(s.replace('Z', '+00:00'))
        else:
            dt = datetime.datetime.fromisoformat(s)
            if dt.tzinfo is None:
                dt = dt.replace(tzinfo=datetime.timezone.utc)
    except Exception:
        # fallback to date-only
        dt = datetime.datetime.strptime(s, '%Y-%m-%d').replace(tzinfo=datetime.timezone.utc)
    return int(dt.timestamp() * 1000)


def daterange_months(start_dt, end_dt):
    current = datetime.date(start_dt.year, start_dt.month, 1)
    while current <= end_dt:
        yield current.year, current.month
        if current.month == 12:
            current = datetime.date(current.year+1,1,1)
        else:
            current = datetime.date(current.year, current.month+1,1)


class Handler(http.server.BaseHTTPRequestHandler):
    def _set_json_headers(self, code=200):
        self.send_response(code)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET,OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.end_headers()

    def do_OPTIONS(self):
        self.send_response(200)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET,OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.end_headers()

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path != '/klines':
            self.send_response(404)
            self.end_headers()
            self.wfile.write(b'Not found')
            return
        qs = urllib.parse.parse_qs(parsed.query)
        symbol = (qs.get('symbol', [''])[0] or '').strip()
        start = qs.get('start', [None])[0]
        end = qs.get('end', [None])[0]
        limit = int(qs.get('limit', [2000])[0])
        if not symbol or not start or not end:
            self._set_json_headers(400)
            self.wfile.write(json.dumps({'error': 'symbol, start and end required'}).encode('utf-8'))
            return
        try:
            start_ms = parse_iso_to_ms(start)
            end_ms = parse_iso_to_ms(end)
        except Exception as e:
            self._set_json_headers(400)
            self.wfile.write(json.dumps({'error': 'invalid date format', 'detail': str(e)}).encode('utf-8'))
            return

        # compute months to load
        sdt = datetime.date(datetime.datetime.fromtimestamp(start_ms/1000, datetime.timezone.utc).year,
                            datetime.datetime.fromtimestamp(start_ms/1000, datetime.timezone.utc).month, 1)
        edt = datetime.date(datetime.datetime.fromtimestamp(end_ms/1000, datetime.timezone.utc).year,
                            datetime.datetime.fromtimestamp(end_ms/1000, datetime.timezone.utc).month, 1)
        months = []
        for y, m in daterange_months(sdt, edt):
            months.append(f'{y:04d}-{m:02d}')

        klines = []
        symdir = os.path.join(OUTDIR_ARCHIVE, symbol)
        for month in months:
            path = os.path.join(symdir, f'{month}.json.gz')
            if not os.path.exists(path):
                # skip missing months
                continue
            try:
                with gzip.open(path, 'rt', encoding='utf-8') as rf:
                    arr = json.load(rf)
                    if isinstance(arr, list):
                        klines.extend(arr)
            except Exception as e:
                # skip corrupt month
                print('failed to read', path, e, file=sys.stderr)
                continue

        # sort and filter by timestamp
        klines = sorted(klines, key=lambda k: int(k[0]))
        out = [k for k in klines if int(k[0]) >= start_ms and int(k[0]) <= end_ms]
        # respect limit but return in chronological order
        if len(out) > limit:
            out = out[-limit:]

        resp = {'symbol': symbol, 'klines': out}
        self._set_json_headers(200)
        self.wfile.write(json.dumps(resp).encode('utf-8'))


if __name__ == '__main__':
    print('Starting klines server on port', PORT)
    with socketserver.ThreadingTCPServer(('0.0.0.0', PORT), Handler) as httpd:
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            httpd.shutdown()
            print('shutdown')
