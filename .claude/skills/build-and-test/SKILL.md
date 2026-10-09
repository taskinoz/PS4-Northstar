---
name: build-and-test
description: Build, deploy, test and release PS4 Northstar (the runtime PRX, the Northstar.PS4 mod, the token helper). Use when changing launcher/ code, mods/, scripts/ or token-helper/, when booting the game in shadPS4 or on a PS4 to check a change, or when preparing a release.
---

# Building and testing PS4 Northstar

Read `AGENTS.md` first for the rules (verified-byte patching, no secrets in logs, PC
compatibility). `docs/INTERNALS.md` has the addresses and layouts each subsystem relies on;
`docs/BUILDING.md` is the long form of this skill.

All `.ps1` scripts run in PowerShell from the repository root and read their settings through
`scripts/Env.ps1` (environment variables, else `.env`). If a script says a setting is missing,
ask the user for the value and have them put it in `.env`; don't guess paths.

## 1. Edit

| Change | Where | Also |
| --- | --- | --- |
| Platform-free logic (parsing, rules, conversion) | `launcher/include/northstar_ps4/<name>.h` | Test in `tests/<name>.cpp`, suite name added to the list in `scripts/Test-NorthstarProfile.ps1` |
| A hook or native | `launcher/src/runtime_<subsystem>.inl` | Gate on the module hash and the exact preimage bytes; add the address to `docs/INTERNALS.md` |
| A new byte anchor the profile should check | `tests/engine-profile/CUSA04013.json` | `Test-EngineProfile.ps1` |
| A script, menu or KeyValues fix for the PS4 | `mods/Northstar.PS4/mod/...` | Never edit `vendor/` or Northstar's mods |
| A generated menu or pdef | the generator in `scripts/menus/` or `scripts/pdef/` | Rerun it and commit the output |
| Token helper | `token-helper/src-tauri/src/*.rs`, `token-helper/ui/` | `cargo test` |

Write comments as facts about the game or the code. No dates or history.

## 2. Build

```powershell
.\scripts\Build-Northstar.ps1                    # dist\northstar-ps4\northstar_ps4.prx (+ .elf, build.json)
.\scripts\Build-Northstar.ps1 -ForceBranchStubs  # exercise the PS4 jump-stub path in shadPS4
```

The `ld.lld: cannot find entry symbol _start` warning is expected.

## 3. Host tests (always)

```powershell
.\scripts\Test-NorthstarProfile.ps1   # every tests\*.cpp suite + KeyValues merges + packaging
.\scripts\Test-EngineProfile.ps1      # game module hashes and anchors (needs NORTHSTAR_PS4_GAME_ROOT)
```

Token helper:

```bash
cd token-helper/src-tauri && cargo test
```

```powershell
.\scripts\Test-AtlasTokenHelper.ps1   # terminal mode against fake EA app / Atlas / console
```

## 4. Boot in shadPS4

Close the game before deploying (a running game holds the PRX open and the deploy silently
tests the old build).

```powershell
.\scripts\Deploy-Runtime.ps1                               # copies the PRX, backs up, checks the hash
.\scripts\Sync-NorthstarProfile.ps1 -ConvertRpaksForPs4    # only when mods/ changed
.\scripts\Invoke-TestBoot.ps1 -SkipBuild -SkipDeploy -KeepRunning -SuccessPattern 'UI lifecycle completed'
.\scripts\Test-BootLoop.ps1 -Count 8                       # boot stability, signed in against fakes
.\scripts\Test-BootLoop.ps1 -Count 2 -HostMatch            # plus a hosted match
```

- shadPS4 needs about 9 GB of free commit; "Insufficient system resources" in
  `address_space.cpp` means the user must close programs. Ask; don't retry in a loop.
- `Test-BootLoop.ps1` moves the real `atlas_identity.json` aside unread and restores it; check
  its report that size and date are unchanged.
- AI.Harness must be enabled for the boot loop (`New-NorthstarProfile.ps1 -IncludeAIHarness`, or
  copy `mods/AI.Harness`). Drive a running game with `Send-AIHarnessCommand.ps1`,
  `Send-PadInput.ps1`, `Capture-GameWindow.ps1` (see `docs/AI-HARNESS.md`).
- Read results from `%SHADPS4_USER_DIR%\log\shad_log.txt`, current boot only. Show the user only
  `[NorthstarPS4]` lines, and never a line containing `playerToken=` or `password=`.
- Remove any test mod from the user's mods folder when done.

## 5. On a PS4 (only with the user's go-ahead)

Uploading to the console is the user's call each time; it may also be blocked for agents, in
which case give them the command.

```bash
python scripts/ps4/upload.py --prx dist/northstar-ps4/northstar_ps4.prx   # PS4_ADDRESS from .env
python scripts/ps4/klog.py            # run in the background; records work/ps4-klog.txt
python scripts/ps4/lastboot.py        # last boot's runtime lines + crash, symbolised, secrets removed
```

GoldHEN's klog serves one reader: if it answers busy, another recorder is still connected.
Check uploads by `LIST` size (the script does); `SIZE` reports a decrypted size for a `.prx`.

## 6. Release (with the user)

1. Build in `dist\northstar-ps4`; run steps 3–5.
2. `python scripts/New-ReleaseAssets.py <version> --previous <previous version>` → `dist/release/<version>/`.
3. Token helper builds come from the `token-helper` workflow (`token-helper-v<version>` tag).
4. The user approves publishing; then create the GitHub release with those files.
