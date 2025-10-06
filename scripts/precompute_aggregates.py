#!/usr/bin/env python3
"""
Precompute 30m/4h aggregates for all symbols using data/latest/<SYM>.json or archive if needed.
Writes results to data/precomputed/<SYM>.json with keys '30m' and '4h' and indicators.
Usage: scripts/precompute_aggregates.py --symbols BTCUSDT ETHUSDT
"""
import os, json, argparse
from glob import glob
BASE = os.path.dirname(os.path.dirname(__file__))
LATEST_DIR = os.path.join(BASE, 'data', 'latest')
PRE_DIR = os.path.join(BASE, 'data', 'precomputed')
os.makedirs(PRE_DIR, exist_ok=True)

# we re-use compute_indicators.py logic patterns here
from math import isnan

def aggregate_to_interval(raw, interval):
    # simple implementation: group by interval boundaries
    # raw: list of [ts, o, h, l, c, v, closeTs]
    if interval == '30m': bucket_s = 30*60
    elif interval == '4h': bucket_s = 4*60*60
    else: return []
    out = []
    cur = None
    for r in raw:
        ts = int(r[0])//1000
        bucket = (ts // bucket_s) * bucket_s
        if cur is None or cur[0] != bucket*1000:
            cur = [bucket*1000, r[1], r[2], r[3], r[4], r[5], (bucket+bucket_s-1)*1000]
            out.append(cur)
        else:
            # update high/low/close/vol
            cur[2] = str(max(float(cur[2]), float(r[2])))
            cur[3] = str(min(float(cur[3]), float(r[3])))
            cur[4] = str(float(r[4]))
            cur[5] = str(float(cur[5]) + float(r[5]))
    return out


def compute_indicators_for_series(series):
    # series: list of [ts,o,h,l,c,v,closeTs]
    closes = []
    vols = []
    for k in series:
        try:
            closes.append(float(k[4]))
        except:
            closes.append(None)
        try:
            vols.append(float(k[5]))
        except:
            vols.append(None)
    def rolling_sma(vals, window):
        out=[]; pref=[0]
        for v in vals: pref.append(pref[-1] + (v if v is not None else 0.0))
        for i in range(len(vals)):
            if i>=window-1: out.append(pref[i+1]-pref[i+1-window])
            else: out.append(None)
        return out
    def rolling_vwap(cl, vl, window):
        out=[]; tp=[( (c if c is not None else 0.0) * (v if v is not None else 0.0) ) for c,v in zip(cl,vl)]
        pref_n=[0]; pref_d=[0]
        for i in range(len(tp)):
            pref_n.append(pref_n[-1]+tp[i]); pref_d.append(pref_d[-1]+(vl[i] if vl[i] is not None else 0))
        for i in range(len(tp)):
            if i>=window-1:
                num = pref_n[i+1]-pref_n[i+1-window]; den = pref_d[i+1]-pref_d[i+1-window]
                out.append(num/den if den!=0 else None)
            else:
                out.append(None)
        return out
    return {
        'sma20': rolling_sma(closes, 20),
        'sma50': rolling_sma(closes, 50),
        'vwap20': rolling_vwap(closes, vols, 20)
    }


def process_symbol(sym):
    path = os.path.join(LATEST_DIR, sym + '.json')
    raw = None
    if os.path.exists(path):
        with open(path,'r') as f: j = json.load(f)
        raw = j.get('1m') or j.get('raw_1m')
    else:
        # fallback to archive merge via build_latest_from_archive.py could be invoked, but skip for now
        print('no latest for', sym); return
    if not raw or len(raw)==0:
        print('no raw data for', sym); return
    # aggregate
    agg30 = aggregate_to_interval(raw, '30m')
    agg4 = aggregate_to_interval(raw, '4h')
    out = {'30m': agg30, '4h': agg4}
    out['indicators'] = {}
    out['indicators']['30m'] = compute_indicators_for_series(agg30)
    out['indicators']['4h'] = compute_indicators_for_series(agg4)
    with open(os.path.join(PRE_DIR, sym + '.json.tmp'),'w') as wf: json.dump(out, wf)
    os.replace(os.path.join(PRE_DIR, sym + '.json.tmp'), os.path.join(PRE_DIR, sym + '.json'))
    print('wrote precomputed', sym, '30m=', len(agg30), '4h=', len(agg4))


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--symbols', nargs='*')
    args = p.parse_args()
    syms = args.symbols or [ os.path.basename(x)[:-5] for x in glob(os.path.join(LATEST_DIR, '*.json')) ]
    for s in syms:
        process_symbol(s)

if __name__ == '__main__':
    main()
