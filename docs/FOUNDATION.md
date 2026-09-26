> Historical only: VPK modification is retired as of 2026-09-06. Use the native-only direction in GOALS.md; the commands below are no longer an installation workflow.

# Foundation: static PS4 content integration

**Status: retired.** This records the first, static approach: repacking Northstar scripts into the PS4 VPKs. The native runtime replaced it. It now runs on unmodified retail VPKs and loads mods from `R2Northstar/mods`, so none of this tooling is part of installation. An install that still has Stage 1 patched VPKs must be returned to clean retail archives before using the current runtime (see [INSTALL.md](INSTALL.md)).

## Goal

Join a Northstar server by IP from shadPS4 while the server runs `Northstar.CustomServers` and `Northstar.Custom`, without requiring Northstar's native Windows loader.

## Constraints

- PC VPK archives must not replace PS4 VPK archives. Their chunk layouts differ. The two platforms also use unrelated build-tagging schemes — PC's `build.txt` reports `Titanfall2_v2_0_11_0`, PS4's reports `R2PS4_r2dlc11_598_CL297590_2017_12_05_12_36_PM` — validated separately (`expectedPcBuild`/`expectedPs4Build` in `config/local.json`), not compared to each other.
- The repository contains only original project code, manifests, documentation, and patches. Extracted or rebuilt game data stays in ignored directories.
- This phase uses static integration only. Runtime mod loading, Atlas, and the server browser are native work — see Goals 1+ in [GOALS.md](GOALS.md).
- `Northstar.CustomServers` is server-side and is not staged into the PS4 client by default.

## Pipeline

1. Validate the PC and PS4 installations against the expected build.
2. Stage `Northstar.Client/mod` and `Northstar.Custom/mod` under `work/stage1/loose`.
3. Inventory staged paths against the PS4 VPK directories.
4. Define an explicit source-path-to-PS4-archive mapping.
5. Resolve Northstar's loader-provided `VANILLA` preprocessor symbol as false with `scripts/Resolve-Stage1Defines.ps1`.
6. Pack only changed files into patch chunk 228 with RSPNVPK, using pristine PS4 directory indexes.
7. Install into a disposable shadPS4 game copy and test direct connection.

This phase does not modify the script compiler. Native registration of Northstar compile symbols, including `VANILLA`, is native work (Goal 3+).

## Success criteria

- Titanfall 2 reaches the main menu in shadPS4 with rebuilt archives.
- The direct-connect UI accepts an IPv4 address and port.
- The client loads into a controlled Northstar server without missing script, VPK, RPAK, or network-table errors.
- The original game can be restored using clean archive backups.

All four criteria were met by this approach before it was retired. See [GOALS.md](GOALS.md) for the current work.
