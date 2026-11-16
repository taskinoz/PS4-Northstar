"""Assemble the Northstar release mods from the pinned submodules.

The PS4 profile is built from two submodules pinned to exactly what the
Northstar release packages (vendor/northstar-release.json):

  vendor/Northstar      R2Northstar/Northstar (the monorepo: launcher, mods,
                        plugins, docs) @ the release tag; mods are in mods/
  vendor/NorthstarNavs  R2Northstar/NorthstarNavs @ v4

and this script repeats the release's own packaging (the monorepo's
nix/mods.nix, nix/northstar-navmeshes.nix and nix/northstar-release.nix),
which is only two steps:

  1. copy the mods and stamp the release version into the three mod.json
     files (`jq ".Version = \"<version>\""`: re-indented to two spaces);
  2. copy NorthstarNavs' AI graphs and navmeshes into
     Northstar.CustomServers/mod/maps, which hosting needs.

The submodules are checked out without line-ending conversion
(core.autocrlf=false, core.eol=lf). Upstream commits some files with CRLF and
declares its localisation files UTF-16LE through .gitattributes; with those
settings the checkout matches the release byte for byte, and a Windows
default checkout does not.

--compare <mods dir> checks the result byte for byte against an existing
install, e.g. R2Northstar/mods from the release zip.

The monorepo's own submodules (the launcher's curl, minhook, minizip) are not
needed and are never initialised.

Usage:
  python scripts/Build-NorthstarMods.py [--output DIR] [--compare MODS_DIR]
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
PIN = os.path.join(REPO, "vendor", "northstar-release.json")
MODS = ("Northstar.Client", "Northstar.Custom", "Northstar.CustomServers")
NAV_DIRS = ("graphs", "navmesh")


def git(path, *args):
    return subprocess.run(["git", "-C", path] + list(args), check=True,
                          capture_output=True, text=True).stdout.strip()


def prepare_submodule(relpath, rev):
    """Initialise a submodule at its pin with byte-exact line endings."""
    path = os.path.join(REPO, relpath)
    if not os.path.isdir(os.path.join(path, ".git")) and not os.path.isfile(os.path.join(path, ".git")):
        subprocess.run(["git", "-C", REPO, "submodule", "update", "--init", "--depth", "1", relpath],
                       check=True)
    head = git(path, "rev-parse", "HEAD")
    if head != rev:
        sys.exit("%s is at %s, but the pin is %s. Run: git submodule update %s"
                 % (relpath, head[:12], rev[:12], relpath))
    # Files checked out before these settings keep their converted line endings
    # (git status doesn't show it), so the checkout is redone once per revision
    # and the revision recorded.
    def setting(key):
        result = subprocess.run(["git", "-C", path, "config", "--get", key], capture_output=True, text=True)
        return result.stdout.strip()
    if setting("core.autocrlf") != "false" or setting("core.eol") != "lf" or \
            setting("northstarps4.exactcheckout") != rev:
        print("%s: switching to byte-exact checkout (autocrlf=false, eol=lf)" % relpath)
        git(path, "config", "core.autocrlf", "false")
        git(path, "config", "core.eol", "lf")
        git(path, "rm", "-q", "--cached", "-r", ".")
        git(path, "reset", "-q", "--hard", "HEAD")
        git(path, "config", "northstarps4.exactcheckout", rev)
    return path


def stamp_version(mod_json, version):
    """Equivalent of the release's `jq ".Version = \"<version>\""`."""
    data = json.load(open(mod_json, encoding="utf-8"))
    data["Version"] = version
    text = json.dumps(data, indent=2, ensure_ascii=False) + "\n"
    open(mod_json, "w", encoding="utf-8", newline="\n").write(text)


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def compare(built, other):
    same, differ, missing, extra = 0, [], [], []
    for mod in MODS:
        for d, _, files in os.walk(os.path.join(built, mod)):
            for f in files:
                rel = os.path.relpath(os.path.join(d, f), built)
                o = os.path.join(other, rel)
                if not os.path.exists(o):
                    missing.append(rel)
                elif sha(os.path.join(d, f)) == sha(o):
                    same += 1
                else:
                    differ.append(rel)
        for d, _, files in os.walk(os.path.join(other, mod)):
            for f in files:
                rel = os.path.relpath(os.path.join(d, f), other)
                if not os.path.exists(os.path.join(built, rel)):
                    extra.append(rel)
    print("compare with %s: %d identical, %d differ, %d only in build, %d only there"
          % (other, same, len(differ), len(missing), len(extra)))
    for label, items in (("differ", differ), ("only in build", missing), ("only there", extra)):
        for rel in items[:10]:
            print("  %-13s %s" % (label, rel))
    return not (differ or missing or extra)


def main():
    pin = json.load(open(PIN, encoding="utf-8"))
    version = pin["version"]
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default=os.path.join(REPO, "work", "northstar-release", version, "mods"))
    parser.add_argument("--compare")
    args = parser.parse_args()

    northstar = prepare_submodule(pin["northstar"]["path"], pin["northstar"]["rev"])
    mods_src = os.path.join(northstar, pin["northstar"]["mods"])
    navs_src = prepare_submodule(pin["navs"]["path"], pin["navs"]["rev"])

    out = os.path.abspath(args.output)
    if os.path.exists(out):
        shutil.rmtree(out)
    os.makedirs(out)
    for mod in MODS:
        shutil.copytree(os.path.join(mods_src, mod), os.path.join(out, mod))
        stamp_version(os.path.join(out, mod, "mod.json"), version)
    maps = os.path.join(out, "Northstar.CustomServers", "mod", "maps")
    for nav in NAV_DIRS:
        shutil.copytree(os.path.join(navs_src, nav), os.path.join(maps, nav), dirs_exist_ok=True)

    count = sum(len(files) for _, _, files in os.walk(out))
    print("Northstar %s mods assembled: %d files -> %s" % (version, count, out))
    if args.compare and not compare(out, args.compare):
        sys.exit(1)


if __name__ == "__main__":
    main()
