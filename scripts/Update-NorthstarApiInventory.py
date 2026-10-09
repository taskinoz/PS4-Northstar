"""Regenerates docs/NATIVE-API-INVENTORY.md from a NorthstarLauncher clone in
tools/NorthstarLauncher-reference and the PS4 registration table."""
import argparse
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PC = ROOT / "tools/NorthstarLauncher-reference"
DECL = re.compile(r'ADD_SQFUNC\s*\(\s*"([^"\n]*)"\s*,\s*(\w+)\s*,\s*"((?:\\.|[^"\\])*)"\s*,\s*"(?:\\.|[^"\\])*"\s*,\s*([^)]*)\)', re.S)
REG = re.compile(r'\{"(\w+)",\s*"((?:\\.|[^"\\])*)",\s*"((?:\\.|[^"\\])*)",\s*(\w+),\s*(\w+)\}')

def cell(text):
    return text.replace("|", "\\|").replace("\n", " ")

def generate():
    revision = subprocess.check_output(["git", "-C", str(PC), "rev-parse", "HEAD"], text=True).strip()
    ps4 = {}
    for match in REG.finditer((ROOT / "launcher/src/runtime_ui_api.inl").read_text()):
        ps4.setdefault(match[1], []).append((match[4], match[5]))
    rows = []
    for path in sorted((PC / "primedev").rglob("*.cpp")):
        source = path.read_text(errors="replace")
        matches = list(DECL.finditer(source))
        expected = len(re.findall(r'ADD_SQFUNC\s*\(\s*"[^"\n]*"\s*,\s*\w+', source))
        if len(matches) != expected:
            raise RuntimeError(f"Unparsed declarations in {path}; extend the scanner before publishing")
        for m in matches:
            relative = path.relative_to(PC / "primedev").as_posix()
            line = source[:m.start()].count("\n") + 1
            handlers = "; ".join(f"`{f}` / `{c}`" for f, c in ps4.get(m[2], [])) or "**Missing registration**"
            rows.append(f"| `{m[2]}` | `{cell(m[1])}` / `{cell(m[3])}` | `{cell(re.sub(r'\s+', ' ', m[4]).strip())}` | {handlers} | `{relative}:{line}` |")
    return "\n".join([
        "# Native API inventory", "",
        f"PC baseline: NorthstarLauncher `{revision}`. Regenerate with `python scripts/Update-NorthstarApiInventory.py`; verify with `--check`.", "",
        "This lists each Squirrel native PC Northstar declares with `ADD_SQFUNC`, beside the PS4 runtime's registration of the same name (`launcher/src/runtime_ui_api.inl`). It lists registrations, not behaviour. Engine builtin overrides, ConVars and the natives PC registers by other means are not listed; see [INTERNALS.md](INTERNALS.md).", "",
        "| PC native | PC return / arguments | PC context | PS4 handler/context | PC source |",
        "|---|---|---|---|---|", *rows, "",
        "## Interpretation", "",
        "`kCtxAll` means UI, CLIENT and SERVER. A name can have a different signature per context (`NSSendMessage`). A few handlers are deliberate no-ops where the PS4 has no counterpart; INTERNALS.md says which.", "",
        f"Coverage: {len(rows)} explicit PC ADD_SQFUNC declarations; {len(ps4)} distinct names in the PS4 registration table. These counts measure different things and are not a completion percentage.", "",
    ])

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    output = ROOT / "docs/NATIVE-API-INVENTORY.md"
    generated = generate()
    if args.check:
        if not output.exists() or output.read_text() != generated:
            raise SystemExit("Native API inventory is stale; regenerate it")
        print("Native API inventory matches source.")
    else:
        output.write_text(generated)
        print(f"Updated {output}")

if __name__ == "__main__":
    main()
