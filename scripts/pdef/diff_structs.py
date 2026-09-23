"""List fields PC's 231 persistence definition has that a console one lacks.

check_persistent_vars.py only sees var paths written out literally in script.
Paths assembled at runtime (e.g. "titanChassis[" + i + "].newPrimeTitanDecals",
walked by PersistenceGetArrayCount) are invisible to it, so this compares
the definitions directly: for every struct both files declare, and for the
top level, print what 231 has and the target does not, with PC's declaration.

Usage: python scripts/pdef/diff_structs.py <target.pdef> [pc_231.pdef]
"""
import os
import sys

TARGET = sys.argv[1]
PC = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "vendor", "NorthstarMods",
    "Northstar.CustomServers", "mod", "cfg", "server", "persistent_player_data_version_231.pdef")


def parse(path):
    blocks = {"<top>": {}}
    enums = {}
    cur_s = cur_e = None
    for raw in open(path, encoding="utf-8", errors="replace"):
        s = raw.split("//")[0].strip()
        if not s:
            continue
        if s.startswith("$STRUCT_START"):
            cur_s = s.split()[1]
            blocks[cur_s] = {}
            continue
        if s.startswith("$STRUCT_END"):
            cur_s = None
            continue
        if s.startswith("$ENUM_START"):
            cur_e = s.split()[1]
            enums[cur_e] = []
            continue
        if s.startswith("$ENUM_END"):
            cur_e = None
            continue
        if cur_e:
            enums[cur_e].append(s.split()[0])
            continue
        parts = s.split()
        if len(parts) < 2:
            continue
        blocks[cur_s or "<top>"][parts[1].split("[")[0]] = parts[0] + " " + parts[1]
    return blocks, enums


target, target_enums = parse(TARGET)
pc, pc_enums = parse(PC)

known_types = {"int", "bool", "float"} | set(target) | set(target_enums)
total = 0
for name, fields in pc.items():
    if name not in target:
        continue
    missing = [(f, d) for f, d in fields.items() if f not in target[name]]
    if not missing:
        continue
    print("%s  (%d missing)" % (name, len(missing)))
    for f, decl in missing:
        typ = decl.split()[0]
        note = "" if typ in known_types or typ.startswith("string{") else "   <- type %s not in target" % typ
        print("    " + decl + note)
    total += len(missing)
print()
print("missing fields in shared structs: %d" % total)
