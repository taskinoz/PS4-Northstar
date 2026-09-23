"""Generate the PS4 persistence definition shipped in Northstar.PS4.

This console build loads persistence version 929; PC Titanfall 2, and so
stock Northstar, uses 231. The engine asks each mod for
cfg/server/persistent_player_data_version_929.pdef, so Northstar.PS4 answers
with a generated file.

**The generated file is PC's 231 layout, not an extended 929.** Northstar's
scripts are written against 231. Earlier versions of this generator (and a
later runtime pipeline built on top of them) grew the console's 929 layout
towards 231 instead, and the result reached ~59.8 KB of data. That is past the
engine's per-player buffer: the PS4 client record is PC's layout shifted by
0x250 at both ends of the buffer (buffer 0x74a vs 0x4fa, UID 0xf750 vs
0xf500), so it holds exactly what PC's does, 56,781 bytes
(NorthstarLauncher's PERSISTENCE_MAX_SIZE). The engine sizes data from the
definition and never checks it against that buffer, so the excess was being
written into the rest of the player record.

231's own data is 56,169 bytes - Atlas rejects anything shorter
(Atlas-reference pkg/pdata, UnmarshalBinary) - which leaves 612 bytes.

**What is added to 231, and why only that.** Nothing in the PS4 engine,
server or client binaries looks up a console-only field by name (the one
candidate string, `bc.discard.%d:1|c`, is a statsd counter). The remaining
risk is stock PS4 scripts that Northstar does not replace.
find_console_fields.py resolves every persistent-var path those scripts use
against 231: the only console-only data they touch is the black market -
`bm.*` in the end-of-match and challenge menus, and the BlackMarketUnlocks
enum - which is 181 bytes. KEEP_FROM_CONSOLE below is that list; the types it
needs are pulled from 929 automatically.

Appended after 231, never inserted: nothing in 231 moves, so a PS4 client's
data lines up byte for byte with a PC Northstar server's for the whole 231
range, and a PC server's pdata is interpreted correctly on the console.

Usage: python scripts/pdef/build_ps4_pdef.py [pc_231.pdef] [stock_929.pdef] [output.pdef]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pdef_size import size_of  # noqa: E402

NL = chr(10)
TAB = chr(9)

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
# The vendored NorthstarMods pin (vendor/northstar-release.json).
PC_231 = os.path.join(REPO, "vendor", "NorthstarMods", "Northstar.CustomServers", "mod", "cfg", "server", "persistent_player_data_version_231.pdef")
STOCK_929 = "D:/PS4/ShadPS4/CUSA04013/r2/cfg/server/persistent_player_data_version_929.pdef"
OUTPUT = "mods/Northstar.PS4/mod/cfg/server/persistent_player_data_version_929.pdef"

# Top-level console members that stock PS4 scripts still use. Found by
# find_console_fields.py; see the docstring.
KEEP_FROM_CONSOLE = ["bm"]

# PC's per-player buffer (NorthstarLauncher PERSISTENCE_MAX_SIZE) and the
# engine's limit on the definition file itself ("... is larger than the
# internal limit of %dk", engine.prx, compared against 0xd000).
BUFFER_BYTES = 56781
FILE_LIMIT = 0xD000
PRIMITIVES = {"int", "bool", "float"}


def parse(path):
    """Enums and structs as {name: [body lines]}, top-level members as {name: line}."""
    enums, structs, members = {}, {}, {}
    cur, kind = None, None
    for raw in open(path, encoding="utf-8", errors="replace"):
        s = raw.split("//")[0].strip()
        if not s:
            continue
        if s.startswith("$ENUM_START"):
            cur, kind = s.split()[1], "enum"
            enums[cur] = []
        elif s.startswith("$STRUCT_START"):
            cur, kind = s.split()[1], "struct"
            structs[cur] = []
        elif s.startswith("$ENUM_END") or s.startswith("$STRUCT_END"):
            cur = kind = None
        elif kind == "enum":
            enums[cur].append(s.split()[0])
        elif kind == "struct":
            structs[cur].append(" ".join(s.split()))
        else:
            parts = s.split()
            if len(parts) >= 2:
                members[parts[1].split("[")[0]] = " ".join(parts)
    return enums, structs, members


def types_used(decl):
    """Type and array-dimension names a declaration depends on."""
    typ, name = decl.split(None, 1)
    out = [typ]
    if "[" in name:
        out.append(name.split("[", 1)[1].split("]")[0].strip())
    return [t for t in out if t not in PRIMITIVES and not t.startswith("string{") and not t.isdigit()]


def build(pc_path, console_path, out_path):
    pc_enums, pc_structs, pc_members = parse(pc_path)
    c_enums, c_structs, c_members = parse(console_path)
    emit_enums, emit_structs, emit_members = [], [], []

    def need(type_name):
        if type_name in pc_enums or type_name in pc_structs:
            return
        if type_name in c_enums:
            if type_name not in [e for e, _ in emit_enums]:
                emit_enums.append((type_name, c_enums[type_name]))
        elif type_name in c_structs:
            if type_name in [s for s, _ in emit_structs]:
                return
            for decl in c_structs[type_name]:
                for dep in types_used(decl):
                    need(dep)
            # after its own dependencies, so every type is declared before use
            emit_structs.append((type_name, c_structs[type_name]))
        else:
            sys.exit("REFUSED: type %s is in neither definition" % type_name)

    for member in KEEP_FROM_CONSOLE:
        if member in pc_members:
            sys.exit("REFUSED: %s already exists in 231; nothing to add" % member)
        if member not in c_members:
            sys.exit("REFUSED: %s is not a console member" % member)
        for dep in types_used(c_members[member]):
            need(dep)
        emit_members.append(c_members[member])

    header = ["// " + l if l else "//" for l in __doc__.strip().split(NL)]
    pc_text = open(pc_path, encoding="utf-8", errors="replace").read().rstrip(NL)
    block = ["", "", "// ---------------------------------------------------------------------------",
             "// Console-only additions (Northstar.PS4). Everything above is PC 231, verbatim.", ""]
    for name, members in emit_enums:
        block += ["$ENUM_START " + name] + [TAB + m for m in members] + ["$ENUM_END", ""]
    for name, decls in emit_structs:
        block += ["$STRUCT_START " + name] + [TAB + d for d in decls] + ["$STRUCT_END", ""]
    block += emit_members

    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    text = NL.join(header + [""]) + NL + pc_text + NL.join(block) + NL
    tmp = out_path + ".tmp"
    open(tmp, "w", encoding="utf-8", newline=NL).write(text)

    data, pc_data = size_of(tmp), size_of(pc_path)
    file_bytes = os.path.getsize(tmp)
    print("added enums:   %s" % ", ".join(n for n, _ in emit_enums))
    print("added structs: %s" % ", ".join(n for n, _ in emit_structs))
    print("added members: %s" % ", ".join(emit_members))
    print("data %d bytes = PC 231 %d + %d console (buffer %d)   file %d bytes (limit %d)"
          % (data, pc_data, data - pc_data, BUFFER_BYTES, file_bytes, FILE_LIMIT))
    if data > BUFFER_BYTES or file_bytes > FILE_LIMIT:
        os.remove(tmp)
        sys.exit("REFUSED: over the player buffer or the engine's file limit")
    os.replace(tmp, out_path)
    print("wrote " + out_path)


if __name__ == "__main__":
    args = sys.argv[1:] + [None] * 3
    build(args[0] or PC_231, args[1] or STOCK_929, args[2] or OUTPUT)
