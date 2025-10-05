#!/usr/bin/env python3
"""
Compute SMA20/SMA50/VWAP20 for each monthly archive and write alongside as .ind.json.gz
Writes: archive/klines/<SYM>/YYYY-MM.ind.json.gz containing {"sma20":[], "sma50":[], "vwap20":[]}

Usage: scripts/precompute_archive_indicators.py --symbol BTCUSDT
       scripts/precompute_archive_indicators.py --all
"""

import os, sys, json, gzip, argparse
from glob import glob

BASE = os.path.dirname(os.path.dirname(__file__))
ARCH = os.path.join(BASE, 'archive', 'klines')


def rolling_sma(vals, w):
    n = len(vals)
    out = [None]*n
    pref = [0.0]
    for v in vals: pref.append(pref[-1] + (v if v is not None else 0.0))
    for i in range(n):
        if i >= w-1:
            out[i] = (pref[i+1] - pref[i+1-w]) / w
        else:
            out[i] = None
    return out


def rolling_vwap(closes, vols, w):
    n = len(closes)
    out = [None]*n
    tp = [(closes[i] if closes[i] is not None else 0.0) * (vols[i] if vols[i] is not None else 0.0) for i in range(n)]
    pref_num = [0.0]
    pref_den = [0.0]
    for i in range(n):
        pref_num.append(pref_num[-1] + tp[i])
        pref_den.append(pref_den[-1] + (vols[i] if vols[i] is not None else 0.0))
    for i in range(n):
        if i >= w-1:
            num = pref_num[i+1] - pref_num[i+1-w]
            den = pref_den[i+1] - pref_den[i+1-w]
            out[i] = num/den if den != 0 else None
        else:
            out[i] = None
    return out


def process_month(path):
    try:
        with gzip.open(path, 'rt', encoding='utf-8') as f:
            arr = json.load(f)
            closes = []
            vols = []
            for k in arr:
                try:
                    closes.append(float(k[4]))
                    vols.append(float(k[5]))
                except Exception:
                    closes.append(None)
                    vols.append(None)
            sma20 = rolling_sma(closes, 20)
            sma50 = rolling_sma(closes, 50)
            vwap20 = rolling_vwap(closes, vols, 20)
            outp = path.replace('.json.gz', '.ind.json.gz')
            with gzip.open(outp, 'wt', encoding='utf-8') as wf:
                json.dump({'sma20': sma20, 'sma50': sma50, 'vwap20': vwap20}, wf)
            print('WROTE IND', outp)
    except Exception as e:
        print('FAILED', path, e)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--symbol')
    p.add_argument('--all', action='store_true')
    args = p.parse_args()

    if args.symbol:
        dirp = os.path.join(ARCH, args.symbol)
        if not os.path.isdir(dirp):
            print('no symbol dir', dirp); return
        for f in sorted(os.listdir(dirp)):
            if f.endswith('.json.gz') and not f.endswith('.ind.json.gz'):
                process_month(os.path.join(dirp, f))
    elif args.all:
        for symdir in sorted(os.listdir(ARCH)):
            d = os.path.join(ARCH, symdir)
            if not os.path.isdir(d): continue
            for f in sorted(os.listdir(d)):
                if f.endswith('.json.gz') and not f.endswith('.ind.json.gz'):
                    process_month(os.path.join(d, f))
    else:
        print('specify --symbol or --all')


if __name__ == '__main__':
    main()
