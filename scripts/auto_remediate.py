#!/usr/bin/env python3
"""
Auto remediation script:
- reads a regression report (JSON)
- finds symbols with 1m klines_count < threshold (default 500) or source contains contiguous_tail
- for each symbol: calls build_latest_from_archive.py --symbol <sym> --max 500, then compute_indicators.py and precompute_aggregates.py
- writes a remediation report
"""
import argparse, json, subprocess, os

def find_affected(report_path, threshold=500):
    r = json.load(open(report_path))
    need = set()
    for rec in r.get('results',[]):
        sym = rec.get('symbol')
        if not sym: continue
        if rec.get('interval')=='1m' and rec.get('klines_count',0) < threshold:
            need.add(sym)
        if 'contiguous_tail' in str(rec.get('source','')):
            need.add(sym)
    return sorted(list(need))


def remediate(symbols):
    out = {}
    for s in symbols:
        out[s] = {'actions':[]}
        try:
            subprocess.run(['python3','scripts/build_latest_from_archive.py','--symbol',s,'--max','500'], check=True)
            out[s]['actions'].append('build_latest_from_archive')
        except Exception as e:
            out[s]['error_build'] = str(e)
            continue
        try:
            subprocess.run(['python3','scripts/compute_indicators.py','--symbol',s], check=True)
            out[s]['actions'].append('compute_indicators')
        except Exception as e:
            out[s]['error_ind'] = str(e)
        try:
            # use compiled C++ precompute tool
            subprocess.run([os.path.join('build','precompute'), '--symbols', s], check=True)
            out[s]['actions'].append('precompute_cpp')
        except Exception as e:
            out[s]['error_pre'] = str(e)
    return out


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--report', default='logs/klines_regression_report_cpp.json')
    p.add_argument('--threshold', type=int, default=500)
    p.add_argument('--out', default='logs/auto_remediation.json')
    p.add_argument('--symbols', nargs='*', help='Optional list of symbols to remediate; if omitted, read from config/symbols.json')
    args = p.parse_args()
    if not os.path.exists(args.report):
        print('report not found:', args.report); return
    syms = find_affected(args.report, args.threshold)
    # if user provided symbols, intersect with affected
    if args.symbols:
        syms = [s for s in syms if s in args.symbols]
    else:
        # filter by config/symbols.json if present
        cfg = os.path.join('config','symbols.json')
        if os.path.exists(cfg):
            cj = json.load(open(cfg))
            cfg_syms = set(cj.get('symbols', []))
            syms = [s for s in syms if s in cfg_syms]
    print('affected:', syms)
    out = remediate(syms)
    with open(args.out,'w') as f: json.dump(out,f,indent=2)
    print('wrote', args.out)

if __name__=='__main__': main()
