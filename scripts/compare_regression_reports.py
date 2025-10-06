#!/usr/bin/env python3
import sys, json
A = sys.argv[1] if len(sys.argv)>1 else 'logs/klines_regression_report_full.json'
B = sys.argv[2] if len(sys.argv)>2 else 'logs/klines_regression_report_cpp.json'
aj = json.load(open(A))
bj = json.load(open(B))
map_a = {(r['symbol'], r['interval'], r['limit']): r for r in aj['results']}
map_b = {(r['symbol'], r['interval'], r['limit']): r for r in bj['results']}
keys = sorted(set(list(map_a.keys())+list(map_b.keys())))
errs = 0
for k in keys:
    a = map_a.get(k)
    b = map_b.get(k)
    if a is None:
        print('MISSING IN A', k); errs+=1; continue
    if b is None:
        print('MISSING IN B', k); errs+=1; continue
    # compare source and klines_count
    sa = a.get('source'); sb = b.get('source')
    ca = a.get('klines_count'); cb = b.get('klines_count')
    ava = a.get('available_field'); avb = b.get('available_field')
    if sa != sb or ca != cb or ava != avb:
        print('DIFF', k, 'source', sa, 'vs', sb, 'klines', ca, 'vs', cb, 'avail', ava, 'vs', avb)
        errs+=1
print('done diffs total=', errs)
