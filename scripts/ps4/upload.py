"""Installs PS4 Northstar on a PS4 running GoldHEN, over GoldHEN's FTP server.

usage:
  python scripts/ps4/upload.py [ps4 address] --prx dist/northstar-ps4/northstar_ps4.prx
  python scripts/ps4/upload.py [ps4 address] --mods <folder with R2Northstar/mods> [--prx ...]
  python scripts/ps4/upload.py [ps4 address] --release <folder with the release zips> [--prx ...]
  python scripts/ps4/upload.py [ps4 address] --enable-plugin

--prx      uploads the runtime to /data/GoldHEN/plugins/northstar_ps4.prx.
--mods     uploads <folder>/R2Northstar to /data/northstar_ps4/R2Northstar.
--release  extracts northstar-mods-*.zip, northstar-ps4-mods-*.zip and
           northstar-custom-ps4-rpaks-*.zip (in that order) and uploads the result.
--enable-plugin  adds the [CUSA04013] section to /data/GoldHEN/plugins.ini,
           keeping what is there.

Mod files whose size already matches are skipped, so an interrupted upload can
be run again. GoldHEN's FTP server reports a .prx's decrypted size from SIZE and
sends it decrypted on RETR, so sizes are read from LIST.
"""
import argparse
import ftplib
import glob
import io
import os
import sys
import tempfile
import zipfile

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from northstar_env import setting  # noqa: E402

PLUGIN = '/data/GoldHEN/plugins/northstar_ps4.prx'
PLUGINS_INI = '/data/GoldHEN/plugins.ini'
PROFILE = '/data/northstar_ps4/R2Northstar'
SECTION = '[CUSA04013]'


def connect(host, port):
    ftp = ftplib.FTP()
    ftp.connect(host, port, timeout=30)
    ftp.login()
    return ftp


def ensure_dir(ftp, path):
    current = ''
    for part in path.strip('/').split('/'):
        current += '/' + part
        try:
            ftp.mkd(current)
        except ftplib.error_perm:
            pass


def remote_size(ftp, path):
    folder, name = path.rsplit('/', 1)
    lines = []
    try:
        ftp.retrlines('LIST ' + folder, lines.append)
    except ftplib.all_errors:
        return None
    for line in lines:
        parts = line.split(None, 8)
        if len(parts) == 9 and parts[8] == name:
            return int(parts[4])
    return None


def put(ftp, local, remote, always=False):
    size = os.path.getsize(local)
    if not always and remote_size(ftp, remote) == size:
        return False
    with open(local, 'rb') as source:
        ftp.storbinary('STOR ' + remote, source, blocksize=256 * 1024)
    if remote_size(ftp, remote) != size:
        raise SystemExit('size mismatch after upload: ' + remote)
    return True


def stage_release(release, into):
    order = ['northstar-mods-*.zip', 'northstar-ps4-mods-*.zip', 'northstar-custom-ps4-rpaks-*.zip']
    for pattern in order:
        matches = sorted(glob.glob(os.path.join(release, pattern)))
        if not matches:
            raise SystemExit('missing %s in %s' % (pattern, release))
        with zipfile.ZipFile(matches[-1]) as archive:
            for entry in archive.infolist():
                name = entry.filename
                above_mods = entry.is_dir() and 'R2Northstar/mods/'.startswith(name)
                if '..' in name or not (name.startswith('R2Northstar/mods/') or above_mods):
                    raise SystemExit('unexpected entry %s in %s' % (name, matches[-1]))
            archive.extractall(into)


def upload_profile(ftp, folder):
    root = os.path.join(folder, 'R2Northstar')
    if not os.path.isdir(os.path.join(root, 'mods')):
        raise SystemExit('no R2Northstar/mods in ' + folder)
    files = []
    for directory, dirs, names in os.walk(root):
        dirs.sort()
        files += [os.path.join(directory, name) for name in sorted(names)]
    made, sent = set(), 0
    for index, local in enumerate(files):
        remote = PROFILE + '/' + os.path.relpath(local, root).replace(os.sep, '/')
        directory = remote.rsplit('/', 1)[0]
        if directory not in made:
            ensure_dir(ftp, directory)
            made.add(directory)
        sent += put(ftp, local, remote)
        if index % 100 == 0:
            print('%d/%d files checked, %d sent' % (index, len(files), sent), flush=True)
    print('mods: %d files, %d sent' % (len(files), sent))


def enable_plugin(ftp):
    buffer = io.BytesIO()
    try:
        ftp.retrbinary('RETR ' + PLUGINS_INI, buffer.write)
        text = buffer.getvalue().decode('utf-8', 'replace')
    except ftplib.error_perm:
        text = ''
    if PLUGIN in text and SECTION in text:
        print('plugins.ini already loads the runtime for CUSA04013')
        return
    if text and not text.endswith('\n'):
        text += '\n'
    text += '\n%s\n%s\n' % (SECTION, PLUGIN)
    ftp.storbinary('STOR ' + PLUGINS_INI, io.BytesIO(text.encode('utf-8')))
    print('added [CUSA04013] to plugins.ini')


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('host', nargs='?', default=setting('PS4_ADDRESS'),
                        help='address of the PS4; defaults to PS4_ADDRESS')
    parser.add_argument('--port', type=int, default=2121)
    parser.add_argument('--prx')
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--mods')
    group.add_argument('--release')
    parser.add_argument('--enable-plugin', action='store_true')
    args = parser.parse_args()
    if not args.host:
        parser.error('no PS4 address: pass one or set PS4_ADDRESS')
    if not (args.prx or args.mods or args.release or args.enable_plugin):
        parser.error('nothing to do')
    ftp = connect(args.host, args.port)
    try:
        if args.release:
            with tempfile.TemporaryDirectory() as staging:
                stage_release(args.release, staging)
                upload_profile(ftp, staging)
        elif args.mods:
            upload_profile(ftp, args.mods)
        if args.prx:
            ensure_dir(ftp, PLUGIN.rsplit('/', 1)[0])
            put(ftp, args.prx, PLUGIN, always=True)
            print('runtime uploaded to ' + PLUGIN)
        if args.enable_plugin:
            enable_plugin(ftp)
    finally:
        ftp.quit()


if __name__ == '__main__':
    main()
