#!/usr/bin/env python3
"""
Compute simple indicators (SMA20, SMA50, VWAP20) for data/latest/<SYM>.json
and write them into the 'indicators' field. Works in-place and preserves '1m' klines.

Usage:
  scripts/compute_indicators.py --symbol BTCUSDT
  scripts/compute_indicators.py --all
"""

import os, sys, json, argparse, math
from glob import glob

BASE = os.path.dirname(os.path.dirname(__file__))
OUTDIR_LATEST = os.path.join(BASE, 'data', 'latest')


def rolling_sma(values, window):
    # efficient O(N) via prefix sums; treat None as 0.0 (same semantics as before)
    n = len(values)
    out = [None] * n
    pref = [0.0]
    for v in values:
        pref.append(pref[-1] + (v if v is not None else 0.0))
    for i in range(n):
        if i >= window - 1:
            s = pref[i+1] - pref[i+1-window]
            out[i] = s / window
        else:
            out[i] = None
    return out


def rolling_vwap(close_vals, vol_vals, window):
    # efficient O(N) via prefix sums; treat None as 0.0
    n = len(close_vals)
    out = [None] * n
    tp = [ ( (c if c is not None else 0.0) * (v if v is not None else 0.0) ) for c, v in zip(close_vals, vol_vals) ]
    pref_num = [0.0]
    pref_den = [0.0]
    for i in range(n):
        pref_num.append(pref_num[-1] + tp[i])
        pref_den.append(pref_den[-1] + (vol_vals[i] if vol_vals[i] is not None else 0.0))
    for i in range(n):
        if i >= window - 1:
            num = pref_num[i+1] - pref_num[i+1-window]
            den = pref_den[i+1] - pref_den[i+1-window]
            out[i] = (num / den) if den != 0 else None
        else:
            out[i] = None
    return out


def compute_for_file(path):
    try:
        with open(path,'r') as f:
            data = json.load(f)
    except Exception as e:
        print('  failed to load', path, e)
        return False

    klines = data.get('1m') or data.get('raw_1m') or []
    if not isinstance(klines, list) or len(klines) == 0:
        print('  no klines in', path)
        return False

    # parse close and volume
    closes = []
    vols = []
    for k in klines:
        try:
            close = float(k[4])
            vol = float(k[5])
        except Exception:
            close = None
            vol = None
        closes.append(close)
        vols.append(vol)

    # compute efficiently
    sma20 = rolling_sma(closes, 20)
    sma50 = rolling_sma(closes, 50)
    vwap20 = rolling_vwap(closes, vols, 20)

    indicators = {
        'sma20': sma20,
        'sma50': sma50,
        'vwap20': vwap20
    }

    data['indicators'] = indicators
    tmp = path + '.tmp'
    try:
        with open(tmp,'w') as f:
            json.dump(data, f)
        os.replace(tmp, path)
        print('  updated indicators in', path)
        return True
    except Exception as e:
        print('  failed to write', path, e)
        try:
            if os.path.exists(tmp): os.remove(tmp)
        except: pass
        return False


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--symbol')
    p.add_argument('--since-ts', type=int, help='optional start timestamp in ms to indicate incremental update')
    p.add_argument('--all', action='store_true')
    args = p.parse_args()

    files = []
    if args.symbol:
        files = [ os.path.join(OUTDIR_LATEST, f'{args.symbol}.json') ]
    elif args.all:
        files = glob(os.path.join(OUTDIR_LATEST, '*.json'))
    else:
        print('Specify --symbol or --all')
        sys.exit(1)

    for f in files:
        if not os.path.exists(f):
            print('file not found', f); continue
        print('Processing', f)
        # simple incremental: if since-ts provided, we still recompute indicators efficiently (O(N))
        # For small 'latest' files (<=500 bars) this is fast; keeping API for future partial writes.
        compute_for_file(f)


if __name__ == '__main__':
    main()
