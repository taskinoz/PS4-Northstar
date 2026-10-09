"""Records a GoldHEN PS4's kernel log (TCP port 3232) to a file, reconnecting when it drops.

usage: python scripts/ps4/klog.py [ps4 address] [--out work/ps4-klog.txt]

The kernel log carries the runtime's [NorthstarPS4] lines and the kernel's crash
reports. Read it with lastboot.py, which leaves out sign-in secrets; the raw file
can contain them.

GoldHEN serves one reader at a time and answers "failed to open klog" while an
earlier connection is still held, so that backs off. A quiet log is normal, so
the connection is never dropped for silence; TCP keepalive notices a console
that restarted.
"""
import argparse
import os
import socket
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from northstar_env import setting  # noqa: E402

BUSY = b'failed to open klog'


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('host', nargs='?', default=setting('PS4_ADDRESS'),
                        help='address of the PS4; defaults to PS4_ADDRESS')
    parser.add_argument('--port', type=int, default=3232)
    parser.add_argument('--out', default=os.path.join('work', 'ps4-klog.txt'))
    args = parser.parse_args()
    if not args.host:
        parser.error('no PS4 address: pass one or set PS4_ADDRESS')
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    while True:
        connection = None
        try:
            connection = socket.create_connection((args.host, args.port), timeout=10)
            connection.settimeout(None)
            connection.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
            if hasattr(socket, 'SIO_KEEPALIVE_VALS'):
                connection.ioctl(socket.SIO_KEEPALIVE_VALS, (1, 15000, 5000))
            first = True
            with open(args.out, 'ab') as log:
                log.write(b'\n--- connected %d ---\n' % int(time.time()))
                while True:
                    data = connection.recv(65536)
                    if not data:
                        break
                    log.write(data)
                    log.flush()
                    if first and BUSY in data:
                        time.sleep(30)
                        break
                    first = False
        except OSError:
            pass
        finally:
            if connection:
                connection.close()
        time.sleep(5)


if __name__ == '__main__':
    main()
