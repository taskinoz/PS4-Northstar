# Building PS4 Northstar

For developers. Players should use the release downloads and [INSTALL.md](INSTALL.md).
Commands run from the repository root in PowerShell unless noted. How the runtime works is in
[INTERNALS.md](INTERNALS.md).

## Requirements

- **The game and shadPS4**, set up as in [INSTALL.md](INSTALL.md#what-you-need), for testing.
- **The runtime:** the OpenOrbis toolchain and LLVM 18 (`clang++`, `ld.lld`, `llvm-nm` on
  `PATH`); see [tools/README.md](../tools/README.md). Python 3.
- **The token helper:** [Bun](https://bun.sh), Rust (`cargo`) and, on Windows, the Visual
  Studio C++ build tools.

## Settings

The scripts read machine-specific paths from environment variables, or from a `.env` file at
the repository root (ignored by git). Copy [.env.example](../.env.example) to `.env` and fill
it in:

| Setting | Used for |
| --- | --- |
| `NORTHSTAR_PS4_GAME_ROOT` | The shadPS4 game folder (with `eboot.bin` and `vpk_ps4`). Deploying, testing, profile sync. |
| `SHADPS4_EXE` | The `shadPS4.exe` the test scripts launch. |
| `SHADPS4_USER_DIR` | shadPS4's user folder (`log\`, `data\`). Default `%APPDATA%\shadPS4`. |
| `NORTHSTAR_MODS_ROOT` | Northstar's mods. Default `work\northstar-release\1.31.13\mods`. |
| `PS4_EXTRACTED_ROOT` | The game's `frontend` and `mp_common` VPKs unpacked, for tests against real files. Optional. |
| `OO_PS4_TOOLCHAIN` | The OpenOrbis toolchain. Default `tools\openorbis-0.5.4\OpenOrbis\PS4Toolchain`. |
| `PS4_ADDRESS` | A PS4 running GoldHEN, for `scripts/ps4/`. |

A variable set in the environment takes precedence over `.env`.

## Northstar's mods

Northstar's mods come from two pinned submodules, `vendor/NorthstarMods` and
`vendor/NorthstarNavs`, at the revisions Northstar v1.31.13 packages
(`vendor/northstar-release.json`). Assemble them as Northstar's own release does:

```powershell
git submodule update --init --depth 1
python scripts/Build-NorthstarMods.py
```

This writes `work/northstar-release/1.31.13/mods`, byte-identical to an installed 1.31.13
(`--compare <PC mods folder>` checks it). The submodules are checked out with
`core.autocrlf=false` and `core.eol=lf`, which the script sets: upstream commits some files with
CRLF and stores localisation as UTF-16.

## The runtime

```powershell
.\scripts\Build-Northstar.ps1
```

Builds `dist\northstar-ps4\northstar_ps4.prx` and its build record,
`northstar_ps4.build.json`. A PRX embeds its output path, so build a release in the folder
whose bytes are tested and shipped. `-ForceBranchStubs` makes every hook go through a jump
stub, as on a PS4, for testing that path in shadPS4.

### Install it in shadPS4

Close the game first.

```powershell
.\scripts\Deploy-Runtime.ps1
.\scripts\Enable-Bootstrap.ps1
.\scripts\Sync-NorthstarProfile.ps1 -ConvertRpaksForPs4
```

- `Deploy-Runtime.ps1` copies the PRX to `<game>\bin\ps4_retail\`, backs up the previous one
  in `work\deploy-backups`, and checks the installed hash.
- `Enable-Bootstrap.ps1` patches `eboot.bin`, once. It refuses any eboot but the retail one,
  keeps the original as `eboot.bin.northstar-stage2.bak`, and `-Disable` restores it.
- `Sync-NorthstarProfile.ps1` brings `<game>\R2Northstar\mods` in line with Northstar's mods
  plus this repository's `mods/Northstar.PS4` and `mods/Northstar.DirectConnect`.
  `-ConvertRpaksForPs4` converts mod RPaks to the PS4 layout in the copy; `-Prune` removes mods
  the sources don't have. Mod folders are copied byte for byte; the sources are never changed.

`New-NorthstarProfile.ps1 -Output <folder> -IncludePs4CompatibilityMods` writes a fresh
`R2Northstar` folder instead, and `-IncludeAIHarness` adds the [test harness](AI-HARNESS.md).

### Install it on a PS4

With GoldHEN's FTP server on (port 2121):

```bash
python scripts/ps4/upload.py <ps4 address> --prx dist/northstar-ps4/northstar_ps4.prx
python scripts/ps4/upload.py <ps4 address> --mods <folder holding R2Northstar>
python scripts/ps4/upload.py <ps4 address> --enable-plugin
```

`--release <folder with the release zips>` extracts and uploads a release's three mod zips in
order. Files whose size already matches are skipped, so an interrupted upload can be rerun.

GoldHEN's kernel log (TCP 3232) carries the runtime's log:

```bash
python scripts/ps4/klog.py <ps4 address>
python scripts/ps4/lastboot.py
```

`klog.py` records to `work/ps4-klog.txt`. `lastboot.py` prints the last boot's `[NorthstarPS4]`
lines and crash report, with secrets filtered out, and maps crash addresses to runtime symbols
using `dist/northstar-ps4/northstar_ps4.elf`.

## Checking a boot

In `shad_log.txt` (shadPS4) or the kernel log (PS4), for the current boot only:

| Line | Meaning |
| --- | --- |
| `[NorthstarPS4] runtime loaded` | The runtime started |
| `runtime manifest generated` | The scripts manifest was built from the enabled mods |
| `UI lifecycle completed` | The UI VM ran every mod callback |
| `mod VPK mount` with a non-null handle | A mod VPK mounted |
| `mod rpak load` with a non-negative handle | A mod pak loaded |
| `FatalError`, `SCRIPT COMPILE ERROR`, `SCRIPT ERROR` | Failures |

A callback a mod declares but never defines is logged as `callback not found` and skipped, as
on PC.

## Tests

| Command | What it covers |
| --- | --- |
| `.\scripts\Test-NorthstarProfile.ps1` | Host tests for the runtime's portable code (`launcher/include`, `tests/*.cpp`, built with `clang++`), KeyValues merges of every shipped patch, and profile packaging. With `NORTHSTAR_MODS_ROOT` and `PS4_EXTRACTED_ROOT` set, it also runs against the real playlist and paks. |
| `.\scripts\Test-EngineProfile.ps1` | The game's module hashes and byte anchors against [tests/engine-profile/CUSA04013.json](../tests/engine-profile/CUSA04013.json). |
| `cargo test` in `token-helper/src-tauri` | The token helper. With `NS_TEST_EBOOT` set to an original `eboot.bin`, also the bootstrap patch on the real eboot. |
| `.\scripts\Test-AtlasTokenHelper.ps1` | The token helper's terminal mode against a fake EA app, Atlas and console (`tests/token_helper/fake_services.py`, needs Python's `cryptography`). |
| `.\scripts\Test-BootLoop.ps1 [-Count 8] [-HostMatch]` | Boots shadPS4 repeatedly, signed in against the fakes, and counts boots that reach the lobby. `-HostMatch` also hosts a match. Needs AI.Harness enabled and about 9 GB of free memory. |

Boot stability is measured over several boots in a row (8 or more), never one.

`.\scripts\Invoke-TestBoot.ps1` builds, deploys and boots once, following the log until a
success or failure pattern (`-SkipBuild`, `-SkipDeploy`, `-KeepRunning`, `-SuccessPattern`).
`.\scripts\Start-NorthstarSession.ps1 -Label <name>` launches a play session with
`--log-append` and archives its log lines and a record of the installed build and mods under
`work\sessions\`.

## Test automation

- **AI.Harness** ([AI-HARNESS.md](AI-HARNESS.md)) runs launch, console, menu and other actions
  in the UI VM and replies through a mailbox.
- **`Send-PadInput.ps1`** presses pad buttons through shadPS4's keyboard mapping, without
  taking focus.
- **`Capture-GameWindow.ps1`** saves a PNG of the game window, even while it is covered.

## Generated files

| Generator | Output |
| --- | --- |
| `scripts/pdef/build_ps4_pdef.py` | `mods/Northstar.PS4/mod/cfg/server/persistent_player_data_version_929.pdef` (PC 231 plus the black market) |
| `scripts/pdef/find_console_fields.py <stock roots>` | Which console-only save fields stock scripts use |
| `scripts/menus/build_mod_list_override.py`, `build_mod_settings_override.py`, `build_colorsliders_override.py` | Northstar.PS4's controller-ready menu overrides, from `vendor/NorthstarMods` |
| `scripts/Update-NorthstarApiInventory.py` | [NATIVE-API-INVENTORY.md](NATIVE-API-INVENTORY.md) |
| `scripts/Convert-NorthstarModRpaks.ps1` | PS4-layout copies of a mod's RPaks and STARPaks |

Rerun a generator when its source changes, and commit its output.

## The token helper

`token-helper/` is a Tauri 2 app: Rust in `src-tauri/`, the page in `ui/`.

```powershell
.\scripts\Build-TokenHelper.ps1            # exe in dist\token-helper
.\scripts\Build-TokenHelper.ps1 -Bundle    # plus the Windows installer
```

Or in `token-helper/`: `bun install`, then `bun tauri build`, or `bun tauri dev` while working
on it. Tauri builds only for the system it runs on, so
[.github/workflows/token-helper.yml](../.github/workflows/token-helper.yml) builds Windows, a
universal macOS `.dmg`, and Linux AppImage and `.deb`, from the Actions tab or a
`token-helper-v<version>` tag. `make-icon.ps1` redraws `app-icon.png`; `bun tauri icon
app-icon.png` regenerates the icon set.

The installer (`src-tauri/src/install.rs`) writes the same eboot patch as
`Enable-Bootstrap.ps1`. `--releases-api`, `--northstar-zip-url` and the `--*-sha256` options
point it at other sources; `tests/token_helper/fake_github.py` serves a folder as a release.

## Releases

1. Build the runtime in `dist\northstar-ps4` and test it (host tests, engine profile, a boot
   loop, and play on shadPS4 and a PS4).
2. Assemble the assets:

   ```bash
   python scripts/New-ReleaseAssets.py <version> --previous <previous version>
   ```

   This writes `dist/release/<version>/`: the PRX and build record,
   `northstar-ps4-mods-<version>.zip` (this repository's mods, as tracked in git),
   `northstar-custom-ps4-rpaks-<version>.zip` (copied from the previous release) and
   `northstar-mods-1.31.13.zip`. Every zip keeps the `R2Northstar/mods/` prefix.
3. Add the token helper builds from the token-helper workflow.
4. Publish a GitHub release with those files.

The token helper installs the newest release (pre-releases included) that has both
`northstar_ps4.prx` and a `northstar-ps4-mods-*.zip`, and checks every download against the
SHA-256 `digest` GitHub publishes. Without a `northstar-mods-*.zip` it downloads Northstar's own
release zip and checks it against the hash in `install.rs`.

## Installing a release by hand

1. Extract `northstar-mods-1.31.13.zip`, `northstar-ps4-mods-<version>.zip` and
   `northstar-custom-ps4-rpaks-<version>.zip` into the game folder, in that order, replacing
   files.
2. Copy `northstar_ps4.prx` to `<game folder>\bin\ps4_retail\northstar_ps4.prx`.
3. Run `.\scripts\Enable-Bootstrap.ps1` once.

To go back to the ordinary game, run `.\scripts\Enable-Bootstrap.ps1 -Disable`. It checks that
the backup is the retail eboot (SHA-256 `590956ab…`) first.

## Rules

- Never change the retail archives (`vpk_ps4/`) or Northstar's mods. PS4 differences go in the
  runtime or as overrides in `Northstar.PS4`, and the port stays compatible with PC servers and
  clients.
- Every code patch is gated on the module's hash and the exact bytes it replaces. PC Northstar
  offsets are never valid on the PS4.
- Game files, extracted archives, Northstar binaries, toolchains and build output stay out of
  git (`tools/`, `work/`, `dist/` are ignored).
- Never log or commit an Atlas token, `atlas_identity.json`, or a server password. When sharing a
  log, keep only `[NorthstarPS4]` lines and drop any with `playerToken=` or `password=`. Keep
  `net_debug_atlas_packet` off outside debugging: it logs players' addresses and tokens.
