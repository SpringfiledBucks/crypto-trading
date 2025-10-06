#!/usr/bin/env python3
import sys, json, csv, os

A = sys.argv[1] if len(sys.argv)>1 else 'logs/klines_regression_report_full.json'
B = sys.argv[2] if len(sys.argv)>2 else 'logs/klines_regression_report_cpp.json'
aj = json.load(open(A))
bj = json.load(open(B))
map_a = {(r['symbol'], r['interval'], r['limit']): r for r in aj['results']}
map_b = {(r['symbol'], r['interval'], r['limit']): r for r in bj['results']}

# If config/symbols.json exists, only compare those symbols to avoid false positives
cfg_syms = None
cfg_path = os.path.join('config','symbols.json')
if os.path.exists(cfg_path):
    try:
        cfg = json.load(open(cfg_path))
        cfg_syms = set(cfg.get('symbols', []))
    except:
        cfg_syms = None

all_keys = set(list(map_a.keys())+list(map_b.keys()))
if cfg_syms is not None:
    keys = sorted(k for k in all_keys if k[0] in cfg_syms)
else:
    keys = sorted(all_keys)

# Prepare CSV and summary outputs
os.makedirs('logs', exist_ok=True)
csv_path = 'logs/klines_diff_summary.csv'
txt_path = 'logs/klines_diff_summary.txt'
csv_rows = []
summary_lines = []
errs = 0
for k in keys:
    a = map_a.get(k)
    b = map_b.get(k)
    if a is None:
        summary_lines.append(f'MISSING_IN_A {k}')
        csv_rows.append((k[0],k[1],k[2],'MISSING_IN_A','','',''))
        errs+=1; continue
    if b is None:
        summary_lines.append(f'MISSING_IN_B {k}')
        csv_rows.append((k[0],k[1],k[2],'MISSING_IN_B',a.get('klines_count',''), '', a.get('source','')))
        errs+=1; continue
    # compare source and klines_count
    sa = a.get('source'); sb = b.get('source')
    ca = a.get('klines_count'); cb = b.get('klines_count')
    ava = a.get('available_field'); avb = b.get('available_field')
    if sa != sb or ca != cb or ava != avb:
        summary_lines.append(f'DIFF {k} source {sa} vs {sb} klines {ca} vs {cb} avail {ava} vs {avb}')
        csv_rows.append((k[0],k[1],k[2],'DIFF', ca, cb, f'{sa} => {sb}'))
        errs+=1
    else:
        csv_rows.append((k[0],k[1],k[2],'OK', ca, cb, ''))
with open(csv_path,'w',newline='') as cf:
    w = csv.writer(cf)
    w.writerow(['symbol','interval','limit','status','python_klines','cpp_klines','note'])
    for r in csv_rows: w.writerow(r)

with open(txt_path,'w') as tf:
    if summary_lines:
        tf.write('\n'.join(summary_lines) + '\n')
    tf.write(f'done diffs total= {errs}\n')

print('wrote', csv_path, txt_path)
print('done diffs total=', errs)
sys.exit(1 if errs>0 else 0)
