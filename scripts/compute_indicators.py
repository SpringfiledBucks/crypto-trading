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
    out = [None] * len(values)
    s = 0.0
    q = []
    for i, v in enumerate(values):
        if v is None:
            q.append(0.0)
            s += 0.0
        else:
            q.append(v)
            s += v
        if i >= window:
            s -= q[i-window]
        if i >= window-1:
            out[i] = s / window
        else:
            out[i] = None
    return out


def rolling_vwap(close_vals, vol_vals, window):
    out = [None] * len(close_vals)
    tp_vol = [ ( (c if c is not None else 0.0) * (v if v is not None else 0.0) ) for c,v in zip(close_vals, vol_vals) ]
    s_num = 0.0
    s_den = 0.0
    numq = []
    denq = []
    for i in range(len(close_vals)):
        numq.append(tp_vol[i])
        denq.append(vol_vals[i] if vol_vals[i] is not None else 0.0)
        s_num += numq[i]
        s_den += denq[i]
        if i >= window:
            s_num -= numq[i-window]
            s_den -= denq[i-window]
        if i >= window-1:
            out[i] = (s_num / s_den) if s_den != 0 else None
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
        compute_for_file(f)


if __name__ == '__main__':
    main()
