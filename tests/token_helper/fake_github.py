"""A stand-in for GitHub's releases API and downloads, for testing the token helper's installer.

usage: fake_github.py <port> <version> <asset folder>
Serves GET /releases (a listing with one release, its assets taken from every
file in <asset folder>, with SHA-256 digests as GitHub publishes them) and
GET /download/<name>. Serves until killed.
"""
import hashlib, json, os, sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import socketserver

port, version, folder = int(sys.argv[1]), sys.argv[2], sys.argv[3]
base = f'http://127.0.0.1:{port}'
assets = []
for name in sorted(os.listdir(folder)):
    path = os.path.join(folder, name)
    if os.path.isfile(path):
        data = open(path, 'rb').read()
        assets.append({'name': name, 'size': len(data), 'browser_download_url': f'{base}/download/{name}',
                       'digest': 'sha256:' + hashlib.sha256(data).hexdigest()})
listing = json.dumps([{'tag_name': version, 'draft': False, 'prerelease': True, 'assets': assets}]).encode()


class QuickServer(ThreadingHTTPServer):
    def server_bind(self):  # skip HTTPServer's slow host-name lookup
        socketserver.TCPServer.server_bind(self)
        self.server_name, self.server_port = self.server_address[:2]


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def send(self, status, body, kind='application/json'):
        self.send_response(status)
        self.send_header('Content-Type', kind)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path.startswith('/releases'):
            return self.send(200, listing)
        if self.path.startswith('/download/'):
            path = os.path.join(folder, os.path.basename(self.path))
            if os.path.isfile(path):
                return self.send(200, open(path, 'rb').read(), 'application/octet-stream')
        self.send(404, b'{}')


QuickServer(('127.0.0.1', port), Handler).serve_forever()
