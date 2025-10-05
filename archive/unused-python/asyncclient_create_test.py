#!/usr/bin/env python3
"""Test AsyncClient.create with aiohttp_socks ProxyConnector created inside the same event loop.
Usage:
  PROXY_URL=socks5h://127.0.0.1:15651 .venv/bin/python scripts/asyncclient_create_test.py
"""
import os
import asyncio
import traceback

PROXY_URL = os.environ.get('PROXY_URL')

async def main():
    from binance.async_client import AsyncClient
    session_params = {}
    https_proxy = PROXY_URL
    if PROXY_URL:
        from urllib.parse import urlparse
        p = urlparse(PROXY_URL)
        scheme = p.scheme or ''
        if scheme.startswith('socks'):
            # normalize socks5h -> socks5
            if scheme.endswith('h'):
                proxy_norm = PROXY_URL.replace(scheme, scheme[:-1], 1)
            else:
                proxy_norm = PROXY_URL
            try:
                from aiohttp_socks import ProxyConnector
                connector = ProxyConnector.from_url(proxy_norm)
                session_params['connector'] = connector
                https_proxy = proxy_norm
                print('Created ProxyConnector for', proxy_norm)
            except Exception:
                print('Failed to create ProxyConnector:')
                traceback.print_exc()

    try:
        print('Calling AsyncClient.create ...')
        client = await AsyncClient.create(loop=asyncio.get_running_loop(), session_params=session_params, https_proxy=https_proxy)
        print('AsyncClient.create succeeded, pinging...')
        res = await client.ping()
        print('ping res:', res)
        await client.close_connection()
    except Exception:
        print('AsyncClient.create failed:')
        traceback.print_exc()

if __name__ == '__main__':
    asyncio.run(main())
