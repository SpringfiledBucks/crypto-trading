#!/usr/bin/env python3
"""
Simple SSE server to stream logs/runtime.log and a small status endpoint.
Run with: python3 webconsole/server.py --port 8080
"""
import argparse
import os
import time
from http import server

LOG_PATH = os.path.join(os.path.dirname(__file__), '..', 'logs', 'runtime.log')

class SSEHandler(server.BaseHTTPRequestHandler):
    def _set_cors(self):
        self.send_header('Access-Control-Allow-Origin', '*')

    def do_GET(self):
        if self.path == '/' or self.path.startswith('/index.html'):
            self.send_response(200)
            self.send_header('Content-type', 'text/html')
            self._set_cors()
            self.end_headers()
            with open(os.path.join(os.path.dirname(__file__), 'index.html'), 'rb') as f:
                self.wfile.write(f.read())
            return
        if self.path == '/events':
            self.send_response(200)
            self.send_header('Content-type', 'text/event-stream')
            self.send_header('Cache-Control', 'no-cache')
            self._set_cors()
            self.end_headers()
            # tail the log file and stream new lines
            try:
                with open(LOG_PATH, 'r') as lf:
                    lf.seek(0, os.SEEK_END)
                    while True:
                        line = lf.readline()
                        if not line:
                            time.sleep(0.2)
                            continue
                        msg = 'data: ' + line.strip() + '\n\n'
                        self.wfile.write(msg.encode('utf-8'))
                        self.wfile.flush()
            except Exception as e:
                # send a simple error then close
                self.wfile.write(('data: [error] %s\n\n' % str(e)).encode('utf-8'))
            return
        if self.path == '/status':
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self._set_cors()
            self.end_headers()
            # simple status: file exists and size
            ok = os.path.exists(LOG_PATH)
            size = os.path.getsize(LOG_PATH) if ok else 0
            self.wfile.write(('{"log_exists": %s, "size": %d}' % (str(ok).lower(), size)).encode('utf-8'))
            return
        self.send_response(404)
        self.end_headers()

def run(port):
    server_address = ('', port)
    httpd = server.ThreadingHTTPServer(server_address, SSEHandler)
    print('Starting webconsole on port', port)
    httpd.serve_forever()

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--port', type=int, default=8080)
    args = p.parse_args()
    run(args.port)
