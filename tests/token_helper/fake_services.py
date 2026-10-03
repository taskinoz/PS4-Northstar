"""Fake EA app (LSX) and fake Atlas for testing scripts/Start-AtlasTokenHelper.ps1.

usage: fake_services.py <lsx port> <atlas port> <state file>
Serves until killed. Writes one JSON line per event to <state file> (requests
seen, tokens issued) so the test can check what happened. No real account or
network service is involved.
"""
import json, os, secrets, socket, sys, threading
import xml.etree.ElementTree as ET
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs
from cryptography.hazmat.primitives import padding
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

USER_ID = '1012345678901'
AUTH_CODE = 'QUOxFAKEcodeForTests'
CONTENT_ID = '1039093'
CLIENT_ID = 'TITANFALL2-PC-SERVER'


def lsx_key(seed):
    if seed == 0:
        return bytes(range(16))
    state = 7

    def nxt():
        nonlocal state
        state = (state * 214013 + 2531011) & 0xffffffff
        return (state >> 16) & 0x7fff
    state = (nxt() + seed) & 0xffffffff
    return bytes(nxt() & 0xff for _ in range(16))


def encrypt_hex(key, text):
    padder = padding.PKCS7(128).padder()
    data = padder.update(text.encode()) + padder.finalize()
    enc = Cipher(algorithms.AES(key), modes.ECB()).encryptor()
    return (enc.update(data) + enc.finalize()).hex()


def decrypt_hex(key, text):
    dec = Cipher(algorithms.AES(key), modes.ECB()).decryptor()
    data = dec.update(bytes.fromhex(text)) + dec.finalize()
    unpad = padding.PKCS7(128).unpadder()
    return (unpad.update(data) + unpad.finalize()).decode()


def note(state_file, **event):
    with open(state_file, 'a') as f:
        f.write(json.dumps(event) + '\n')


def read_message(conn, buf):
    while b'\0' not in buf[0]:
        chunk = conn.recv(4096)
        if not chunk:
            raise EOFError
        buf[0] += chunk
    msg, buf[0] = buf[0].split(b'\0', 1)
    return msg.decode()


def serve_lsx(conn, state_file):
    buf = [b'']
    challenge = secrets.token_hex(16)
    conn.sendall(f'<LSX><Event sender="EALS"><Challenge key="{challenge}" version="3" build="fake"/></Event></LSX>\0'.encode())
    root = ET.fromstring(read_message(conn, buf))
    reply = root.find('Request/ChallengeResponse')
    expected = encrypt_hex(lsx_key(0), challenge)
    ok = reply is not None and reply.get('response') == expected and reply.get('key') == challenge and \
        reply.findtext('ContentId') == CONTENT_ID
    note(state_file, event='handshake', ok=ok)
    if not ok:
        conn.sendall(b'<LSX><Response id="0" sender="EALS"><ErrorSuccess Code="-1" Description="bad challenge"/></Response></LSX>\0')
        return
    key = lsx_key((ord(expected[0]) << 8) | ord(expected[1]))
    conn.sendall(f'<LSX><Response id="0" sender="EALS"><ChallengeAccepted response="{expected}"/></Response></LSX>\0'.encode())
    while True:
        root = ET.fromstring(decrypt_hex(key, read_message(conn, buf)))
        request = root.find('Request')
        rid = request.get('id')
        body = list(request)[0]
        if body.tag == 'GetProfile':
            out = f'<GetProfileResponse UserIndex="0" UserId="{USER_ID}" PersonaId="1" Persona="fake"/>'
        elif body.tag == 'GetAuthCode':
            good = body.get('UserId') == USER_ID and body.get('ClientId') == CLIENT_ID
            note(state_file, event='authcode', ok=good)
            out = f'<AuthCode value="{AUTH_CODE}"/>' if good else '<ErrorSuccess Code="-2" Description="bad client"/>'
        else:
            out = '<ErrorSuccess Code="-3" Description="unsupported"/>'
        # An unrelated event first, as the EA app sends them at any time.
        conn.sendall((encrypt_hex(key, '<LSX><Event sender="EbisuSDK"><Login IsLoggedIn="true"/></Event></LSX>') + '\0').encode())
        conn.sendall((encrypt_hex(key, f'<LSX><Response id="{rid}" sender="EbisuSDK">{out}</Response></LSX>') + '\0').encode())


def lsx_server(port, state_file):
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(('127.0.0.1', port))
    srv.listen()
    while True:
        conn, _ = srv.accept()
        def run(c=conn):
            try:
                serve_lsx(c, state_file)
            except Exception:
                pass
            finally:
                c.close()
        threading.Thread(target=run, daemon=True).start()


def atlas_server(port, state_file):
    # Like Atlas, only the newest token of the account is valid.
    current = {'token': None}

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def reply(self, status, payload):
            body = json.dumps(payload).encode()
            self.send_response(status)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            url = urlparse(self.path)
            query = parse_qs(url.query)
            agent = self.headers.get('User-Agent', '')
            if url.path == '/client/servers':
                return self.reply(200, [])
            if url.path == '/client/mainmenupromos':
                return self.reply(200, {})
            if url.path != '/client/origin_auth' or not agent.startswith('R2Northstar/'):
                return self.reply(400, {'success': False})
            if query.get('id') == [USER_ID] and query.get('token') == [AUTH_CODE]:
                token = secrets.token_hex(16)
                current['token'] = token
                note(state_file, event='token', token=token)
                self.reply(200, {'success': True, 'token': token})
            else:
                self.reply(403, {'success': False, 'error': {'enum': 'UNAUTHORIZED_GAME', 'msg': 'bad code'}})

        # The game's session requests (auth_with_self, auth_with_server): a
        # refused token gets Atlas's INVALID_MASTERSERVER_TOKEN; an accepted
        # one is recorded and then turned down with an error of its own, as
        # this fake keeps no save or servers.
        def do_POST(self):
            url = urlparse(self.path)
            query = parse_qs(url.query)
            if url.path not in ('/client/auth_with_self', '/client/auth_with_server'):
                return self.reply(404, {'success': False})
            valid = query.get('id') == [USER_ID] and current['token'] is not None and                 query.get('playerToken') == [current['token']]
            note(state_file, event=url.path.rsplit('/', 1)[1], ok=valid)
            if not valid:
                return self.reply(401, {'success': False, 'error': {'enum': 'INVALID_MASTERSERVER_TOKEN',
                                                                    'msg': 'Invalid or expired masterserver token'}})
            self.reply(200, {'success': False, 'error': {'enum': 'FAKE_ATLAS', 'msg': 'fake Atlas keeps no save'}})

    ThreadingHTTPServer(('127.0.0.1', port), Handler).serve_forever()


if __name__ == '__main__':
    lsx_port, atlas_port, state = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
    assert list(lsx_key(1337)) == [251, 135, 22, 197, 214, 181, 148, 115, 149, 93, 40, 78, 123, 141, 60, 108]
    threading.Thread(target=lsx_server, args=(lsx_port, state), daemon=True).start()
    note(state, event='ready')
    atlas_server(atlas_port, state)
