"""Summarises the last boot in a PS4 kernel log recorded by klog.py.

usage: python scripts/ps4/lastboot.py [--log work/ps4-klog.txt] [--elf dist/northstar-ps4/northstar_ps4.elf] [--all]

Prints the runtime's [NorthstarPS4] lines from the last boot (module listings and
other bulk left out unless --all), then any crash: the signal, the faulting
thread and address, and rip and the last branch mapped to a module, with a
symbol when it is in the runtime (needs llvm-nm and the ELF of the same build).
Lines that could carry a sign-in token or password are replaced.
"""
import argparse
import os
import re
import subprocess
import sys

# The runtime's first line; releases up to v1.0.0-rc2 log the second form.
BOOT = re.compile(r'\[NorthstarPS4\] (runtime loaded|Stage 2 PoC initializer executed)')
SECRET = re.compile(r'playerToken=|password=|token|authcode|"code"', re.I)
BULK = re.compile(r'initial\[|discovered module=|module scan attempt|^\[NorthstarPS4\]\s+segment\[|'
                  r'fs overlay vtable|keyvalues patch declared|console vtable\[')


def symbol_for(elf, offset):
    try:
        rows = subprocess.run(['llvm-nm', '-n', '-C', '--defined-only', elf], capture_output=True, text=True).stdout
    except OSError:
        return ''
    best = None
    for row in rows.splitlines():
        parts = row.split(' ', 2)
        if len(parts) == 3 and parts[0] and parts[1] in 'tTwW' and int(parts[0], 16) <= offset:
            best = (int(parts[0], 16), parts[2])
    return ' %s+%#x' % (best[1], offset - best[0]) if best else ''


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--log', default=os.path.join('work', 'ps4-klog.txt'))
    parser.add_argument('--elf', default=os.path.join('dist', 'northstar-ps4', 'northstar_ps4.elf'))
    parser.add_argument('--all', action='store_true')
    args = parser.parse_args()
    lines = open(args.log, 'rb').read().decode('utf-8', 'replace').splitlines()
    starts = [i for i, line in enumerate(lines) if BOOT.search(line)]
    if not starts:
        raise SystemExit('no boot of the runtime in ' + args.log)
    boot = lines[starts[-1]:]
    for line in boot:
        if '[NorthstarPS4]' in line and (args.all or not BULK.search(line)):
            print('[line left out]' if SECRET.search(line) else line[:240])

    crash = next((i for i, line in enumerate(boot) if 'A user thread receives a fatal signal' in line), None)
    if crash is None:
        print('\n(no crash in this boot)')
        return
    print()
    registers = {}
    for line in boot[crash:crash + 40]:
        if line.startswith('# ') and any(k in line for k in ('signal', 'reason', 'fault address', 'thread name')):
            print(line)
        for name, value in re.findall(r'\b(r[a-z0-9]+|BrF|BrT)\s*:\s*([0-9a-f]{16})', line):
            registers[name] = int(value, 16)
    modules, current = [], None
    for line in boot[crash:crash + 600]:
        match = re.match(r'# (/\S+)$', line)
        if match:
            current = match.group(1)
        match = re.match(r'#\s+(text|data): ([0-9a-f]+):([0-9a-f]+)', line)
        if match and current:
            modules.append((current, match.group(1), int(match.group(2), 16), int(match.group(3), 16)))
    for name in ('rip', 'BrF', 'BrT'):
        if name not in registers:
            continue
        address = registers[name]
        found = next(((m, kind, address - a) for m, kind, a, b in modules if a <= address < b), None)
        if not found:
            print('%s %x: no module' % (name, address))
            continue
        module, kind, offset = found
        symbol = symbol_for(args.elf, offset) if 'northstar_ps4' in module and kind == 'text' else ''
        print('%s %x: %s %s+%#x%s' % (name, address, module.rsplit('/', 1)[-1], kind, offset, symbol))


if __name__ == '__main__':
    main()
