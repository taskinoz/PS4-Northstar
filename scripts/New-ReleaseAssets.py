"""Assembles a release's runtime and mod zips in dist/release/<version>.

usage: python scripts/New-ReleaseAssets.py <version> --previous <previous version> [--build dist/northstar-ps4]

Writes:
- northstar_ps4.prx and northstar_ps4.build.json, copied from --build;
- northstar-ps4-mods-<version>.zip: mods/Northstar.PS4 and mods/Northstar.DirectConnect
  exactly as tracked in git, under R2Northstar/mods/;
- northstar-custom-ps4-rpaks-<version>.zip: copied from the previous release's
  folder (Northstar.Custom's paks converted for the PS4, which change only with
  Northstar.Custom);
- northstar-mods-1.31.13.zip: Northstar's mods from work/northstar-release/1.31.13/mods
  (scripts/Build-NorthstarMods.py), under R2Northstar/mods/.

The token helper builds come from the token-helper CI workflow and are added by hand.
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NORTHSTAR_VERSION = '1.31.13'


def sha(path):
    with open(path, 'rb') as f:
        return hashlib.sha256(f.read()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('version')
    parser.add_argument('--previous', required=True)
    parser.add_argument('--build', default=os.path.join('dist', 'northstar-ps4'))
    args = parser.parse_args()
    os.chdir(ROOT)
    out = os.path.join('dist', 'release', args.version)
    os.makedirs(out, exist_ok=True)

    for name in ('northstar_ps4.prx', 'northstar_ps4.build.json'):
        shutil.copy2(os.path.join(args.build, name), os.path.join(out, name))

    files = subprocess.check_output(['git', 'ls-files', 'mods/Northstar.PS4', 'mods/Northstar.DirectConnect'],
                                    text=True).split()
    with zipfile.ZipFile(os.path.join(out, 'northstar-ps4-mods-%s.zip' % args.version), 'w', zipfile.ZIP_DEFLATED) as z:
        for name in sorted(files):
            z.write(name, 'R2Northstar/' + name)

    previous = os.path.join('dist', 'release', args.previous, 'northstar-custom-ps4-rpaks-%s.zip' % args.previous)
    shutil.copy2(previous, os.path.join(out, 'northstar-custom-ps4-rpaks-%s.zip' % args.version))

    mods = os.path.join('work', 'northstar-release', NORTHSTAR_VERSION, 'mods')
    with zipfile.ZipFile(os.path.join(out, 'northstar-mods-%s.zip' % NORTHSTAR_VERSION), 'w', zipfile.ZIP_DEFLATED,
                         compresslevel=9) as z:
        for directory, dirs, names in os.walk(mods):
            dirs.sort()
            for name in sorted(names):
                full = os.path.join(directory, name)
                z.write(full, 'R2Northstar/mods/' + os.path.relpath(full, mods).replace(os.sep, '/'))

    for name in sorted(os.listdir(out)):
        path = os.path.join(out, name)
        print('%-58s %12d %s' % (name, os.path.getsize(path), sha(path)[:16]))


if __name__ == '__main__':
    main()
