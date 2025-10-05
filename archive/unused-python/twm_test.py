#!/usr/bin/env python3
"""启动 ThreadedWebsocketManager 并打印原始消息以便诊断。

用法：
    PROXY_URL=socks5h://127.0.0.1:15651 python scripts/twm_test.py
"""
import os
import time
import logging

PROXY_URL = os.environ.get('PROXY_URL')
PAIRS = os.environ.get('PAIRS', 'btcusdt').split(',')

from binance import ThreadedWebsocketManager

def raw_cb(msg):
    print('raw msg:', msg)

def main():
    logging.basicConfig(level=logging.DEBUG)
    https_proxy = PROXY_URL
    # 规范化 socks5h -> socks5
    if https_proxy and https_proxy.startswith('socks') and https_proxy.endswith('h'):
        https_proxy = https_proxy[:-1]
    print('Using https_proxy=', https_proxy)
    twm = ThreadedWebsocketManager(https_proxy=https_proxy)
    twm.start()
    streams = [f"{p.lower()}@trade" for p in PAIRS]
    print('Starting multiplex for', streams)
    twm.start_multiplex_socket(callback=raw_cb, streams=streams)
    # run for a short while
    try:
        time.sleep(15)
    except KeyboardInterrupt:
        pass
    print('stopping')
    twm.stop()

if __name__ == '__main__':
    main()
