#!/usr/bin/env python3
"""
Convert archive/klines/<SYM>/YYYY-MM.json.gz into data/1m/<SYM>/YYYY-MM.bin.gz matching
`tools/aggregate_1m` OHLCV binary structure (int64 fields, price/qty scaled by 1e8).

Usage: scripts/klines_to_month_bin.py --symbol BTCUSDT
       scripts/klines_to_month_bin.py --all
"""

import os, sys, json, gzip, struct, argparse
from glob import glob
import datetime

BASE = os.path.dirname(os.path.dirname(__file__))
ARCH = os.path.join(BASE, 'archive', 'klines')
OUT = os.path.join(BASE, 'data', '1m')

ensure_dir_cmd = 'mkdir -p '

# OHLCV struct: 6 * int64_t
FMT = '<qqqqqq'  # little-endian


def convert_file(inp, sym):
    # inp: path to .json.gz
    bn = os.path.basename(inp)
    month = bn.replace('.json.gz','')
    outdir = os.path.join(OUT, sym)
    os.makedirs(outdir, exist_ok=True)
    outp = os.path.join(outdir, month + '.bin.gz')
    try:
        with gzip.open(inp, 'rt', encoding='utf-8') as f:
            arr = json.load(f)
    except Exception as e:
        print('  failed to load', inp, e); return False
    try:
        with gzip.open(outp, 'wb') as gf:
            for k in arr:
                ts = int(k[0])
                open_p = int(round(float(k[1]) * 1e8))
                high_p = int(round(float(k[2]) * 1e8))
                low_p = int(round(float(k[3]) * 1e8))
                close_p = int(round(float(k[4]) * 1e8))
                vol = int(round(float(k[5]) * 1e8))
                gf.write(struct.pack(FMT, ts, open_p, high_p, low_p, close_p, vol))
        print('WROTE BIN', outp)
        return True
    except Exception as e:
        print('  failed to write', outp, e)
        return False


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--symbol')
    p.add_argument('--all', action='store_true')
    args = p.parse_args()

    targets = []
    if args.symbol:
        d = os.path.join(ARCH, args.symbol)
        targets = glob(os.path.join(d, '*.json.gz'))
    elif args.all:
        for d in os.listdir(ARCH):
            dd = os.path.join(ARCH, d)
            if os.path.isdir(dd):
                targets += glob(os.path.join(dd, '*.json.gz'))
    else:
        print('specify --symbol or --all'); sys.exit(1)

    for t in targets:
        sym = os.path.basename(os.path.dirname(t))
        convert_file(t, sym)

if __name__ == '__main__':
    main()
