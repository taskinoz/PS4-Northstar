"""Which console-only save fields do scripts that actually run still use?

The PS4 save definition is being rebased on PC's 231 layout, which is exactly
what Northstar's scripts are written against and fits the engine's
per-player buffer (231's data is 56,169 bytes; the buffer is 56,781). The
risk is stock PS4 scripts that Northstar does not replace: if one of them
reads or writes a field that only the console's 929 layout has, rebasing
would turn it into an "Invalid var name" error.

So this collects every stock script from the unpacked PS4 archives, drops the
ones a deployed mod overrides by path, and resolves every persistent-var path
the rest use - Get/SetPersistentVar*, the Persistence* helpers, and
sh_stats.gnut's AddPersistentStat registrations - against both layouts.
Mod scripts are checked against 231 as well, as a control.

Usage: python scripts/pdef/find_console_fields.py <stock_root> [<stock_root> ...]
"""
import glob
import os
import re
import sys
REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

MODS = "D:/PS4/ShadPS4/CUSA04013/R2Northstar/mods"
P929 = "D:/PS4/ShadPS4/CUSA04013/r2/cfg/server/persistent_player_data_version_929.pdef"
P231 = os.path.join(REPO, "vendor", "NorthstarMods", "Northstar.CustomServers", "mod", "cfg", "server", "persistent_player_data_version_231.pdef")
Q = chr(34)

# Functions whose first argument is a persistence path...
CALLS = ("GetPersistentVar", "GetPersistentVarAsInt", "SetPersistentVar", "SetPersistentVarAsInt",
         "PersistenceGetArrayCount", "GetPersistentIntArray", "IncrementPersistentVar",
         "SetPersistentIntArray")
# ...and those whose first argument is an enum name.
ENUM_CALLS = ("PersistenceEnumValueIsValid", "PersistenceGetEnumCount",
              "PersistenceGetEnumItemNameForIndex", "PersistenceGetEnumIndexForItemName")
DYNAMIC = "@"  # stands in for a non-literal part of a concatenated path


def model(path):
    top, structs, enums = {}, {}, {}
    cur_s = cur_e = None
    for raw in open(path, encoding="utf-8", errors="replace"):
        s = raw.split("//")[0].strip()
        if not s:
            continue
        if s.startswith("$STRUCT_START"):
            cur_s = s.split()[1]; structs[cur_s] = {}; continue
        if s.startswith("$STRUCT_END"):
            cur_s = None; continue
        if s.startswith("$ENUM_START"):
            cur_e = s.split()[1]; enums[cur_e] = set(); continue
        if s.startswith("$ENUM_END"):
            cur_e = None; continue
        if cur_e:
            enums[cur_e].add(s.split()[0]); continue
        p = s.split()
        if len(p) >= 2:
            (structs[cur_s] if cur_s else top)[p[1].split("[")[0]] = p[0]
    return top, structs, enums


def resolves(path, m):
    """True, False, or None when the path is not decidable statically."""
    top, structs, enums = m
    # Index contents never matter; a bracket or dot left open by
    # concatenation ends the literal structure.
    clean = re.sub(r"\[[^\]]*\]", "", path).split("[")[0].strip(".")
    if not clean or clean.startswith(DYNAMIC) and "." not in clean and clean == DYNAMIC:
        return None
    scope = top
    for seg in clean.split("."):
        if DYNAMIC in seg or seg.startswith("%"):
            return None  # a field name chosen at runtime
        if seg not in scope:
            return False
        scope = structs.get(scope[seg], {})
    return True


def split_args(text, start):
    """Top-level arguments of the call whose '(' is just before text[start]."""
    args, cur, depth, i = [], [], 1, start
    while i < len(text) and depth:
        c = text[i]
        if c == Q:
            j = text.find(Q, i + 1)
            if j < 0:
                break
            cur.append(text[i:j + 1]); i = j + 1; continue
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
            if not depth:
                break
        elif c == "," and depth == 1:
            args.append("".join(cur)); cur = []; i += 1; continue
        cur.append(c); i += 1
    args.append("".join(cur))
    return args


def as_path(arg):
    """Concatenated string -> path, with each non-literal part as DYNAMIC."""
    parts, i, out = arg, 0, []
    for piece in re.split("(" + Q + "[^" + Q + "]*" + Q + ")", parts):
        if piece.startswith(Q) and piece.endswith(Q) and len(piece) >= 2:
            out.append(piece[1:-1])
        elif piece.strip().strip("+").strip():
            out.append(DYNAMIC)
    if not any(p != DYNAMIC for p in out):
        return None
    return "".join(out)


def scan(files):
    fields, enum_names = {}, {}
    for f in files:
        text = open(f, encoding="utf-8", errors="replace").read()
        for fn in CALLS + ENUM_CALLS:
            for m in re.finditer(r"\b" + fn + r"\s*\(", text):
                args = split_args(text, m.end())
                p = as_path(args[0]) if args else None
                if p:
                    (enum_names if fn in ENUM_CALLS else fields).setdefault(p, f)
        if f.endswith("sh_stats.gnut"):
            # (category, alias, subAlias, path, desc): the path is argument 4.
            for m in re.finditer(r"\bAddPersistentStat\w*\s*\(", text):
                args = split_args(text, m.end())
                p = as_path(args[3]) if len(args) >= 4 else None
                if p:
                    fields.setdefault(p, f)
    return fields, enum_names


def rel(path):
    path = path.replace(os.sep, "/")
    i = path.find("scripts/vscripts/")
    return path[i:] if i >= 0 else None


m929, m231 = model(P929), model(P231)
stock = {}
for root in sys.argv[1:]:
    for f in glob.glob(root + "/**/scripts/vscripts/**/*.*nut", recursive=True):
        r = rel(f)
        if r:
            stock.setdefault(r, f)
overridden = set()
mod_files = []
for f in glob.glob(MODS + "/*/mod/scripts/vscripts/**/*.*nut", recursive=True):
    mod_files.append(f)
    r = rel(f)
    if r in stock:
        overridden.add(r)
running = [f for r, f in stock.items() if r not in overridden]
print("stock scripts: %d, overridden by mods: %d, stock still in use: %d"
      % (len(stock), len(overridden), len(running)))

def report(label, files):
    fields, enum_names = scan(files)
    undecidable = sum(1 for p in fields if resolves(p, m231) is None)
    print()
    print("%s: %d field paths (%d built at runtime, not decidable), %d enum names"
          % (label, len(fields), undecidable, len(enum_names)))
    miss = [(p, f) for p, f in sorted(fields.items()) if resolves(p, m231) is False]
    print("  field paths missing from PC 231: %d" % len(miss))
    for p, f in miss:
        in929 = resolves(p, m929)
        print("    %-50s %-9s %s" % (p, "929:yes" if in929 else "929:no", rel(f)))
    emiss = [(e, f) for e, f in sorted(enum_names.items()) if e not in m231[2]]
    print("  enum names missing from PC 231: %d" % len(emiss))
    for e, f in emiss:
        print("    %-50s %-9s %s" % (e, "929:yes" if e in m929[2] else "929:no", rel(f)))


report("stock scripts still in use", running)
report("control - mod scripts", mod_files)
