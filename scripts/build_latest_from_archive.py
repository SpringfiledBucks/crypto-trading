#!/usr/bin/env python3
"""Build data/latest/<SYMBOL>.json by merging archive/klines/<SYMBOL>/*.json.gz
Usage: python3 scripts/build_latest_from_archive.py --symbol BTCUSDT --start 2025-08-01
"""
import os, sys, json, gzip, argparse, datetime

BASE = os.path.dirname(os.path.dirname(__file__))
OUTDIR_LATEST = os.path.join(BASE, 'data', 'latest')
OUTDIR_ARCHIVE = os.path.join(BASE, 'archive', 'klines')

def parse_iso(s):
    return int(datetime.datetime.strptime(s, '%Y-%m-%d').replace(tzinfo=datetime.timezone.utc).timestamp() * 1000)

def load_month(path):
    try:
        with gzip.open(path, 'rt', encoding='utf-8') as f:
            return json.load(f)
    except Exception as e:
        print('failed load', path, e)
        return []

def write_latest(sym, klines, max_bars=500):
    os.makedirs(OUTDIR_LATEST, exist_ok=True)
    data = {'1m': klines[-max_bars:], 'indicators': {}}
    tmp = os.path.join(OUTDIR_LATEST, f'{sym}.json.tmp')
    outp = os.path.join(OUTDIR_LATEST, f'{sym}.json')
    with open(tmp, 'w') as wf:
        json.dump(data, wf)
    os.replace(tmp, outp)
    print('WROTE latest', outp, 'bars=', len(data['1m']))

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--symbol', required=True)
    p.add_argument('--start', required=False)
    p.add_argument('--max', type=int, default=500)
    args = p.parse_args()
    sym = args.symbol
    start_ms = parse_iso(args.start) if args.start else 0
    d = os.path.join(OUTDIR_ARCHIVE, sym)
    if not os.path.isdir(d):
        print('no archive dir for', sym); sys.exit(2)
    arr = []
    for fn in sorted(os.listdir(d)):
        if not fn.endswith('.json.gz'): continue
        path = os.path.join(d, fn)
        m = load_month(path)
        if not isinstance(m, list): continue
        for k in m:
            try:
                ts = int(k[0])
            except Exception:
                continue
            if ts >= start_ms:
                arr.append(k)
    arr.sort(key=lambda k: int(k[0]))
    if not arr:
        print('no klines found after', args.start)
        sys.exit(0)
    write_latest(sym, arr, max_bars=args.max)
    # compute indicators
    try:
        import subprocess
        subprocess.check_call(['python3', os.path.join(BASE, 'scripts', 'compute_indicators.py'), '--symbol', sym])
    except Exception as e:
        print('compute_indicators failed', e)

if __name__ == '__main__':
    main()
