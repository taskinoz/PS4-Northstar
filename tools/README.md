# tools/

Nothing under `tools/` is original project content — it's downloaded toolchains, built binaries, and reference source clones, all `.gitignore`d (`tools/*` with an exception for this file). A fresh clone starts with an empty `tools/` directory; this file is the checklist for what needs to go where before the scripts in `scripts/` will run.

## Required — the automated pipeline reads these directly

| Path | What it is | Where it comes from |
| --- | --- | --- |
| `tools/RSPNVPK-bin/RSPNVPK.exe` (+ `lzham_x64.dll`) | VPK repacker used by `scripts/Build-AndDeployStage1Vpks.ps1` | Build from `tools/RSPNVPK` (below), or grab a release from [taskinoz/RSPNVPK](https://github.com/taskinoz/RSPNVPK) |
| `tools/openorbis-0.5.4/OpenOrbis/PS4Toolchain/` | PS4 native toolchain (headers, `crtlib.o`, `create-fself.exe`) used by `scripts/Build-Stage2Poc.ps1` | [OpenOrbis/OpenOrbis-PS4Toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4Toolchain) release `v0.5.4`, asset `toolchain-llvm-18.tar.gz`. Archive SHA-256 `3c7cd5bb593ca74fa1c13fd59f3938dc0fc07985167f7275063019e63abe4526` (see `docs/TECHNICAL-NOTES.md`). Extract so this exact path exists. Also put `clang++`, `ld.lld`, and `llvm-nm` (LLVM 18) on `PATH`. |
| `tools/vanilla-scripts/scripts/vscripts/scripts.rson` | The *unmodified* PS4 build's `scripts.rson`, used as the merge base by `scripts/Merge-Stage2ScriptsRson.ps1` | Extract `scripts/vscripts/scripts.rson` from the PS4 game's `frontend` VPK before any patching (e.g. with `tf2vpk`/`tf2vpk-bin`, below), from a clean/backup copy of the install. |

`scripts/Test-Environment.ps1` does **not** check for these — it only validates the PC/PS4 game roots in `config/local.json`. If a Stage-specific script fails with "not found", check this table first.

## Setup-time / manual tools — not called by any script, but needed to prepare inputs or continue native RE work

| Path | What it is | Notes |
| --- | --- | --- |
| `tools/tf2vpk/` (source), `tools/tf2vpk-bin/` (built exes) | VPK inspection/unpacking CLI | [pg9182/tf2vpk](https://github.com/pg9182/tf2vpk). Used interactively to extract vanilla content (e.g. `tools/vanilla-scripts` above) and inspect PS4 VPK chunk layout. Build from source with the Go toolchain below, or use a prebuilt exe. |
| `tools/go-portable/` | A portable Go toolchain | Only needed to build `tf2vpk` from source if you don't already have Go installed. Not required if you have your own Go, or if you already have built `tf2vpk-bin`. |
| `tools/python-packages/` (`capstone`, `lief`) | Vendored Python packages for disassembling/parsing the retail PRX/ELF files during reverse-engineering | Not called by any script; used ad hoc for analysis (see `docs/TECHNICAL-NOTES.md` for examples). Reproduce with `python -m pip install --target tools/python-packages capstone lief`, or just `pip install capstone lief` into your own environment and skip vendoring. |
| `tools/RSPNVPK/` | RSPNVPK source (C#) | [taskinoz/RSPNVPK](https://github.com/taskinoz/RSPNVPK). Only needed if rebuilding `RSPNVPK-bin` yourself. |

## Reference-only clones — not needed to build or run anything

| Path | What it is |
| --- | --- |
| `tools/NorthstarLauncher-reference/` | Read-only clone of [R2Northstar/NorthstarLauncher](https://github.com/R2Northstar/NorthstarLauncher) (PC Windows client/launcher). Source of truth for what native services Northstar needs — **never reuse its Windows `engine.dll`/`client.dll` offsets on PS4**; PS4 addresses must always come from analyzing the PS4 binaries directly. Also the reference implementation to read before starting Atlas auth work (Goal 8 in `docs/GOALS.md`). |
| `tools/Enhanced-Menu-Mod-reference/` | Read-only clone of a PC Northstar UI mod, useful as an example of how Northstar UI script mods are structured. |

## Safety note

None of this directory is, or should ever contain, Titanfall 2 game data, Northstar release binaries, or extracted/rebuilt archives — see the repository policy in the top-level README. Toolchains and reference clones only.
