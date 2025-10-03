#!/usr/bin/env python3
"""Small test: attempt a single websocket handshake to Binance combined trade stream
using websockets_proxy (if installed) or plain websockets. Prints detailed errors.

Usage:
  PROXY_URL=socks5h://127.0.0.1:15651 python scripts/ws_proxy_test.py
"""
import os
import asyncio
import json
import traceback

PROXY_URL = os.environ.get('PROXY_URL')
PAIRS = os.environ.get('PAIRS', 'btcusdt').split(',')
streams = [f"{p.lower()}@trade" for p in PAIRS]
stream_path = "/".join(streams)
WS_URL = f"wss://stream.binance.com:9443/stream?streams={stream_path}"

try:
    from websockets_proxy import Proxy as WsProxy, proxy_connect as ws_proxy_connect
except Exception:
    WsProxy = None
    ws_proxy_connect = None

import websockets as _websockets

async def do_connect():
    connect_cm = None
    if PROXY_URL and ws_proxy_connect and WsProxy:
        try:
            from urllib.parse import urlparse, urlunparse
            p = urlparse(PROXY_URL)
            scheme = p.scheme
            if scheme.endswith('h'):
                scheme = scheme[:-1]
                p = p._replace(scheme=scheme)  # type: ignore
            proxy_url_norm = urlunparse(p)
            proxy = WsProxy.from_url(proxy_url_norm)
            connect_cm = ws_proxy_connect(WS_URL, proxy=proxy)
            print('Attempting websocket connect via websockets_proxy using', proxy_url_norm)
        except Exception:
            print('Failed to create websockets_proxy connect context:')
            traceback.print_exc()
            connect_cm = None

    if connect_cm is None:
        print('Falling back to plain websockets.connect (no proxy context)')
        connect_cm = _websockets.connect(WS_URL)

    try:
        async with connect_cm as ws:
            print('Websocket connected to', WS_URL)
            # try receive for a short time to observe messages
            try:
                msg = await asyncio.wait_for(ws.recv(), timeout=5)
                print('Received one message, len=', len(msg))
                try:
                    j = json.loads(msg)
                    print('Message sample keys:', list(j.keys())[:5])
                except Exception:
                    print('Message not JSON parseable')
            except asyncio.TimeoutError:
                print('Connected but no message received within 5s')
    except Exception as e:
        print('WebSocket connect failed:')
        traceback.print_exc()

def main():
    asyncio.run(do_connect())

if __name__ == '__main__':
    main()
