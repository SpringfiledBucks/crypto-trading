#!/usr/bin/env python3
"""Minimal Futures WS dashboard (stable, CSP-friendly).

Serves a small SPA and provides /inline_config.js for runtime config so we avoid inline JS eval.
"""

import os
import json
import csv
import subprocess
from pathlib import Path
from aiohttp import web

ROOT = Path(__file__).resolve().parent.parent
VIZ_DIR = ROOT / 'data' / 'viz'

routes = web.RouteTableDef()


def _check_token(request: web.Request) -> bool:
    token = os.environ.get('DASHBOARD_TOKEN')
    if not token:
        return True
    hdr = request.headers.get('X-Auth-Token')
    if hdr == token:
        return True
    q = request.rel_url.query.get('token')
    return q == token


def load_symbols():
    p = ROOT / 'config' / 'futures_pairs.json'
    try:
        if p.exists():
            with p.open('r', encoding='utf-8') as f:
                return json.load(f)
    except Exception:
        pass
    out = []
    p2 = VIZ_DIR / '1m'
    if p2.exists():
        for f in p2.iterdir():
            if f.suffix == '.csv':
                out.append(f.stem)
    return sorted(out)


def get_services_snapshot():
    services = [
        'futures_ws.service', 
        'futures_ws_dashboard.service',
        'archive_ws.service',
    ]
    out = {}
    for s in services:
        info = {'name': s, 'active': 'unknown', 'sub': '', 'mainpid': None, 'enabled': 'unknown'}
        try:
            p = subprocess.run(['systemctl', 'show', s, '--no-page', '--property=ActiveState,SubState,MainPID'], capture_output=True, text=True, timeout=2)
            for line in p.stdout.splitlines():
                if '=' in line:
                    k, v = line.split('=', 1)
                    k = k.strip(); v = v.strip()
                    if k == 'ActiveState':
                        info['active'] = v
                    elif k == 'SubState':
                        info['sub'] = v
                    elif k == 'MainPID':
                        info['mainpid'] = v or None
        except Exception:
            pass
        try:
            p2 = subprocess.run(['systemctl', 'is-enabled', s], capture_output=True, text=True, timeout=2)
            info['enabled'] = p2.stdout.strip()
        except Exception:
            info['enabled'] = 'unknown'
        out[s] = info
    return out


@routes.get('/')
async def index(request):
    syms = load_symbols()
    options_html = ''.join([f'<option value="{s}">{s}</option>' for s in syms])
    services_snapshot = get_services_snapshot()
    initial_services_html = '<table><tr><th style="text-align:left">service</th><th>status</th><th>sub</th><th>mainpid</th><th>enabled</th></tr>'
    for k, it in services_snapshot.items():
        state = 'service-up' if it.get('active') == 'active' else ('service-down' if it.get('active') == 'failed' else 'service-unknown')
        dot = f'<span class="dot {state}"></span>'
        initial_services_html += f"<tr><td>{it.get('name')}</td><td>{dot}<span class=\"{state}\">{it.get('active')}</span></td><td>{it.get('sub')}</td><td>{it.get('mainpid') or ''}</td><td>{it.get('enabled')}</td></tr>"
    initial_services_html += '</table>'

    html = """<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <script src="https://cdn.plot.ly/plotly-2.24.1.min.js"></script>
  <title>Futures WS 控制台</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 12px }
    .dot { display:inline-block; width:10px; height:10px; border-radius:50%; margin-right:6px; vertical-align:middle; }
    .service-up { color:green } .service-down { color:red } .service-unknown { color:#999 }
  </style>
</head>
<body>
    <h1>Futures WS 控制台</h1>
    <div id="debug-banner" style="background:#fffae6;padding:8px;border:1px solid #eee;margin-bottom:8px">DASHBOARD: 服务端已响应 — 若页面空白请检查浏览器控制台 (F12)</div>
        <div>
        <label for="sym">选择合约：</label>
        <select id="sym">{options_html}</select>
        <label for="interval">周期：</label>
        <select id="interval"><option value="1m">1m</option><option value="15m">15m</option><option value="4h">4h</option></select>
        <button id="load">载入</button>
    </div>
    <div id="chart" style="width:100%;height:520px;margin-top:12px"></div>
  <h3>服务状态</h3>
  <div id="services_table">{initial_services_html}</div>
    <script src="/inline_config.js"></script>
    <script src="/app.js"></script>
</body>
</html>"""
    html = html.replace('{options_html}', options_html)
    html = html.replace('{initial_services_html}', initial_services_html)
    return web.Response(text=html, content_type='text/html')


@routes.get('/inline_config.js')
async def inline_config(request):
    snap = get_services_snapshot()
    services_js = f"window.__INITIAL_SERVICES = {json.dumps(snap, separators=(',',':'))};\n"
    token = os.environ.get('DASHBOARD_TOKEN')
    token_js = f"window.__DASHBOARD_TOKEN = '{token}';\n" if token else ''
    body = token_js + services_js
    return web.Response(text=body, content_type='application/javascript')



@routes.get('/app.js')
async def app_js(request):
        js = r"""document.addEventListener('DOMContentLoaded', function(){
    const symSel = document.getElementById('sym');
    const intervalSel = document.getElementById('interval');
    const loadBtn = document.getElementById('load');
    const chartDiv = document.getElementById('chart');

    function showMessage(msg){ chartDiv.innerHTML = '<div style="padding:12px;color:#333">'+msg+'</div>'; }

    function render(sym, interval){
        showMessage('加载中...');
        fetch(`/viz/${interval}/${sym}`).then(r=>r.json()).then(rows=>{
            if(!rows || rows.length===0){ showMessage('未找到数据'); return; }
            const dates = rows.map(r=>r[0]);
            const opens = rows.map(r=>r[1]);
            const highs = rows.map(r=>r[2]);
            const lows = rows.map(r=>r[3]);
            const closes = rows.map(r=>r[4]);
            const volumes = rows.map(r=>r[5]||0);
            const trace = { x: dates, open: opens, high: highs, low: lows, close: closes, type: 'candlestick' };
            const vol = { x: dates, y: volumes, type: 'bar', yaxis: 'y2', marker:{color:'rgba(100,100,100,0.3)'} };
            const layout = { margin:{t:20}, xaxis:{rangeslider:{visible:false}}, yaxis:{title:'Price'}, yaxis2:{domain:[0,0.2],overlaying:'y',side:'right',title:'Volume'} };
            try{ Plotly.newPlot(chartDiv, [trace, vol], layout, {responsive:true}); }catch(e){ showMessage('绘图失败: '+e); }
        }).catch(e=>{ showMessage('加载数据失败: '+e); });
    }

    loadBtn.addEventListener('click', function(){ const s = symSel.value; const i = intervalSel.value; if(s) render(s,i); });
    if(symSel.options.length>0){ render(symSel.value, intervalSel.value); }
});"""
        return web.Response(text=js, content_type='application/javascript')


@routes.get('/services')
async def services(request):
    return web.json_response(get_services_snapshot())


@routes.get('/status')
async def status(request):
    p = ROOT / 'data' / 'ws' / 'metrics.json'
    if p.exists():
        try:
            with p.open('r', encoding='utf-8') as f:
                return web.json_response(json.load(f))
        except Exception:
            pass
    return web.json_response({'total_messages': 0})


@routes.get('/viz/{interval}/{sym}')
async def viz_interval(request):
    interval = request.match_info.get('interval')
    sym = request.match_info.get('sym')
    p = VIZ_DIR / interval / f'{sym}.csv'
    rows = []
    if p.exists():
        try:
            with p.open('r', encoding='utf-8', errors='ignore') as f:
                r = csv.reader(f)
                next(r, None)
                for row in r:
                    try:
                        rows.append([row[0], float(row[1]) if row[1] else None, float(row[2]) if row[2] else None, float(row[3]) if row[3] else None, float(row[4]) if row[4] else None, float(row[5]) if row[5] else None])
                    except Exception:
                        continue
        except Exception:
            pass
    return web.json_response(rows)


@routes.get('/logs/{sym}')
async def logs(request):
    if not _check_token(request):
        return web.Response(status=401, text='unauthorized')
    sym = request.match_info.get('sym')
    p = ROOT / 'data' / 'ws' / 'history' / f'{sym}.csv'
    if not p.exists():
        return web.json_response({'lines': [], 'path': str(p), 'mtime': None})
    try:
        with p.open('r', encoding='utf-8', errors='ignore') as f:
            lines = f.read().splitlines()[-200:]
    except Exception:
        lines = []
    try:
        mtime = p.stat().st_mtime
    except Exception:
        mtime = None
    return web.json_response({'lines': lines, 'path': str(p), 'mtime': mtime})


@routes.get('/__plain')
async def plain_text(request):
    """Simple health/visibility endpoint to verify the server and browser can show plain text."""
    return web.Response(text='DASHBOARD OK', content_type='text/plain')


@routes.get('/__debug_min')
async def debug_min(request):
    """Minimal HTML without external scripts to rule out CDN/script blocking in the browser."""
    html = """<!doctype html>
<html lang="zh-CN">
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>debug</title></head>
<body><h1>DEBUG MINIMAL</h1><div>如果你在浏览器看到本行，说明服务器与浏览器渲染基本正常。</div></body>
</html>"""
    return web.Response(text=html, content_type='text/html')


app = web.Application()
app.add_routes(routes)


if __name__ == '__main__':
    bind = os.environ.get('DASHBOARD_BIND', '127.0.0.1')
    port = int(os.environ.get('DASHBOARD_PORT', '8080'))
    web.run_app(app, host=bind, port=port)
