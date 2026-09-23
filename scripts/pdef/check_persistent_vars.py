"""Resolve every persistent var path the mod scripts reference against a pdef.

Reports each unresolved path, the first segment that fails, and where PC's 231
declares that name (top level or which struct), so the gaps can be filled from
PC's own declarations."""
import glob
import re
import sys
import os
REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

MODS = 'D:/PS4/ShadPS4/CUSA04013/R2Northstar/mods'
PDEF = sys.argv[1]
P231 = os.path.join(REPO, "vendor", "NorthstarMods", "Northstar.CustomServers", "mod", "cfg", "server", "persistent_player_data_version_231.pdef")
QUOTE = chr(34)

CALL = re.compile(r'(?:Set|Get)PersistentVar\w*\s*\(\s*' + QUOTE + '([^' + QUOTE + ']+)' + QUOTE)


def model(path):
    top, structs, enums = {}, {}, {}
    cur_s = cur_e = None
    for raw in open(path, encoding='utf-8', errors='replace'):
        s = raw.split('//')[0].strip()
        if not s:
            continue
        if s.startswith('$STRUCT_START'):
            cur_s = s.split()[1]; structs[cur_s] = {}; continue
        if s.startswith('$STRUCT_END'):
            cur_s = None; continue
        if s.startswith('$ENUM_START'):
            cur_e = s.split()[1]; enums[cur_e] = []; continue
        if s.startswith('$ENUM_END'):
            cur_e = None; continue
        if cur_e:
            enums[cur_e].append(s.split()[0]); continue
        p = s.split()
        if len(p) < 2:
            continue
        name = p[1].split('[')[0]
        (structs[cur_s] if cur_s else top)[name] = (p[0], p[1])
    return top, structs, enums


def resolve(path, top, structs):
    # Drop array indices and anything after an incomplete concatenation.
    clean = re.sub(r'\[[^\]]*\]', '', path)
    clean = clean.split('[')[0]
    segs = [x for x in clean.split('.') if x]
    scope = top
    for i, seg in enumerate(segs):
        if seg not in scope:
            return seg, i
        typ = scope[seg][0]
        scope = structs.get(typ, {})
    return None, None


t929, s929, _ = model(PDEF)
t231, s231, _ = model(P231)

refs = {}
for f in glob.glob(MODS + '/**/*.*nut', recursive=True):
    for m in CALL.finditer(open(f, encoding='utf-8', errors='replace').read()):
        refs.setdefault(m.group(1), f.split('mods')[-1])

missing = {}
for path, where in sorted(refs.items()):
    seg, depth = resolve(path, t929, s929)
    if seg is None:
        continue
    if depth == 0:
        src = ('top', t231[seg][1], t231[seg][0]) if seg in t231 else None
    else:
        src = None
        for sn, fields in s231.items():
            if seg in fields:
                src = (sn, fields[seg][1], fields[seg][0])
                break
    missing.setdefault(seg, (path, where, src))

print('persistent var paths referenced: %d' % len(refs))
print('unresolved names: %d' % len(missing))
for seg, (path, where, src) in sorted(missing.items()):
    print('  %-34s via %-40s 231: %s' % (seg, path[:40], src))
