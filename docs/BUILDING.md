# Building PS4 Northstar from source

For developers. Players should use the release downloads and the [installation guide](INSTALL.md). Run commands from the repository root in PowerShell.

## Requirements

- The game, shadPS4 and settings from [INSTALL.md](INSTALL.md#what-you-need).
- For the runtime: the LLVM/OpenOrbis toolchain listed in [tools/README.md](../tools/README.md), and Python 3.
- For the token helper: [Bun](https://bun.sh), Rust (`cargo`) and, on Windows, the Visual Studio C++ build tools.

Never repack or edit the retail VPKs, and don't run the old Stage 1 repacking scripts. An install that has Stage 1 patched VPKs or staged `r2` scripts must go back to clean retail archives first.

## 1. Configure

The Northstar mods come from two pinned submodules, `vendor/NorthstarMods` and `vendor/NorthstarNavs`, at the revisions Northstar v1.31.13 packages (`vendor/northstar-release.json`). Assemble the release the same way Northstar's own packaging does:

```powershell
git submodule update --init --depth 1
python scripts/Build-NorthstarMods.py
```

That writes the mods to `work/northstar-release/1.31.13/mods`, byte-identical to an installed 1.31.13 (`--compare <PC mods dir>` checks this). Copy `config/project.example.json` to `config/local.json` (or edit the existing one) and set at least:

```json
{
  "ps4GameRoot": "D:\\PS4\\ShadPS4\\CUSA04013",
  "northstarModsRoot": "C:\\path\\to\\repo\\work\\northstar-release\\1.31.13\\mods"
}
```

`northstarModsRoot` points directly at the folder holding the mod folders, not at an `R2Northstar` folder above it. It can point at a PC install's `R2Northstar\mods` instead, but then the profile follows whatever version that install updates to.

## 2. Package the mods

```powershell
.\scripts\New-NorthstarProfile.ps1 -Output .\dist\install-profile -IncludePs4CompatibilityMods
```

- **Copies, never edits:** mod folders are copied byte-for-byte and their hashes verified, with the source's `enabledmods.json` when present. Game archives are not touched.
- **`-IncludePs4CompatibilityMods`** adds this port's mods: `Northstar.PS4` (required; the PS4 fixes are overrides in it, so Northstar's own mods stay unchanged) and `Northstar.DirectConnect`.
- **`-ConvertRpaksForPs4`** converts the copied profile's uncompressed Titanfall 2 v7 texture/material RPaks and STARPaks to the PS4 layout. The PC source stays unchanged. Compressed, patch, array and other layouts stop with an error instead of being installed, and the runtime refuses a PC-layout pak copied in by hand.
- **`-IncludeAIHarness`** adds the development harness ([AI-HARNESS.md](AI-HARNESS.md)).
- **Keeping an install in sync:** `.\scripts\Sync-NorthstarProfile.ps1` updates an installed profile the same way.

For a fresh install, copy `dist/install-profile/R2Northstar` beside the game's `eboot.bin`. Preserve an existing profile outside the game folder first; don't merge an old modded profile into a new one.

Profile layout rules:
- Discovery uses each mod's `Name` and `Version` for enabled state. New mods default to enabled.
- Lower `LoadPriority` loads first, and higher priority wins file overrides. Equal priorities are ordered by folder name.
- Limits: 128 mod folders, names below 64 bytes, `mod.json` up to 64 KB, and 256 ConVars per mod.
- The PRX reads `/app0/R2Northstar`. A generated `.ns_mod_manifest` is used only when directory enumeration fails.

## 3. Build and install the runtime

Close the game first.

```powershell
.\scripts\Test-NorthstarProfile.ps1
.\scripts\Build-Northstar.ps1 -EnableRuntimeManifest -Output "$PWD\dist\northstar-runtime-manifest"
.\scripts\Deploy-Stage2Poc.ps1 -Source "$PWD\dist\northstar-runtime-manifest\northstar_ps4.prx"
```

- **Supported build:** `-EnableRuntimeManifest` builds the script manifest from the vanilla one and the enabled mods, serves mod files through the game filesystem, and hooks the UI, CLIENT and SERVER script VMs. The default build and `-EnableExperimentalScriptLoading` are older experiments.
- **Output path:** release PRXs embed the output path, so build in the folder whose bytes you ship.
- **Build record:** the build writes `northstar_ps4.build.json` beside the PRX; keep it to identify the build.
- **Deploying:** the deploy script backs up the previous PRX in `work/stage2/deploy-backups` and verifies the installed hash.

Enable the eboot bootstrap once, on the matching retail eboot only:

```powershell
.\scripts\Enable-Stage2Bootstrap.ps1 -GameRoot 'D:\PS4\ShadPS4\CUSA04013'
```

It refuses an unknown eboot hash; don't force past that. It keeps the original as `eboot.bin.northstar-stage2.bak`, and `-Disable` restores it.

## 4. Check the installation

```powershell
.\scripts\Get-NorthstarInstallStatus.ps1
```

This is read-only. It matches the installed PRX hash to local build records and reports one of:
- **Bootstrap only:** the safe build, which loads no mods.
- **Filesystem overrides only:** not a complete script loader.
- **Experimental native mod loader:** the runtime-manifest build.
- **Unknown build:** no matching record. `-BuildInfo <path to northstar_ps4.build.json>` supplies one.

In the log (`%APPDATA%\shadPS4\log\shad_log.txt`), look only at the current boot:
- **Progress, not success:** `runtime manifest generated` and `mod file served`.
- **Failures:** `FatalError` and `SCRIPT COMPILE ERROR`.
- **Script startup:** a successful boot logs each `UI Before:` / `UI After:` callback and ends with `UI lifecycle completed`. A callback a mod declares but never defines is logged as `callback not found` and skipped, as on PC.
- **Mod VPKs:** `mod VPK discovered`, then a non-null `mod VPK mount`.
- **Converted Northstar.Custom paks:** `mod rpak load` with non-negative handles, and `mod starpak redirect` for both streams.

## Tests

| Command | What it covers |
| --- | --- |
| `.\scripts\Test-NorthstarProfile.ps1` | Profile packaging, plus host tests for the runtime's portable code in `launcher/include` and `tests/`. |
| `.\scripts\Test-Stage2EngineProfile.ps1` | Engine-profile checks. |
| `.\scripts\Test-AtlasTokenHelper.ps1` | The token helper against a fake EA app, Atlas and console (`tests/token_helper/fake_services.py`; needs Python's `cryptography`). |
| `cargo test` in `token-helper/src-tauri` | The token helper's Rust tests. |

Boot stability is measured by booting several times in a row (8–12), not once.

## Mod VPKs

Enabled mods' VPKs are mounted through the game filesystem. Keep the PC layout, with every archive chunk together:

```text
R2Northstar/mods/Northstar.Custom/
  mod.json
  vpk/
    englishclient_mp_northstar_common.bsp.pak000_dir.vpk
    client_mp_northstar_common.bsp.pak000_000.vpk
```

Mounting rules:
- **When they mount:** without `vpk/vpk.json`, a mod's archives preload. `"Preload": true` does the same. `false`, or an entry without that member, mounts only when the engine mounts an archive of the same name. Comments and trailing commas are allowed.
- **Names:** only the `english*.bsp.pak000_dir.vpk` naming is discovered.
- **Textures:** keep them in PC layout inside mod VPKs. The PS4's own layout is only for the game's archives.
- **Building a test VPK:** `tools/RSPNVPK` builds one with `-n 0`. Its `feature/vpk-expansion` branch fixes multi-archive chunk numbers.

## The token helper

`token-helper/` is a Tauri 2 app: Rust in `src-tauri/`, the page in `ui/`. Bun installs and runs the Tauri CLI.

```powershell
.\scripts\Build-TokenHelper.ps1            # exe in dist\token-helper
.\scripts\Build-TokenHelper.ps1 -Bundle    # plus the Windows installer
```

Or, in `token-helper\`: `bun install`, then `bun tauri build`, or `bun tauri dev` while working on it. Tauri builds only for the system it runs on, so `.github/workflows/token-helper.yml` builds Windows, a universal macOS `.dmg`, and Linux AppImage and `.deb`. Run it from the Actions tab once it's on the default branch, or push a `token-helper-v<version>` tag. `make-icon.ps1` redraws `app-icon.png`, and `bun tauri icon app-icon.png` regenerates the icon set.

## Going back to the ordinary game

The bootstrap-only PRX keeps the eboot bootstrap but loads no mods:

```powershell
.\scripts\Build-Stage2Poc.ps1 -Output .\dist\northstar-safe-bootstrap
.\scripts\Deploy-Stage2Poc.ps1 -Source "$PWD\dist\northstar-safe-bootstrap\northstar_ps4.prx"
```

To remove the bootstrap, check that `eboot.bin.northstar-stage2.bak` is the original retail eboot (SHA-256 `590956ab2c9251f588348a1c066ed4045ce94ffc87c25faa5872a1f92ec35824`), then run `.\scripts\Enable-Stage2Bootstrap.ps1 -GameRoot <game folder> -Disable`. Neither changes the VPKs or the mod sources.

## Development automation

- **AI.Harness** ([AI-HARNESS.md](AI-HARNESS.md)) queues a Northstar launch before boot and sends console and menu commands with replies.
- **`scripts/Send-PadInput.ps1`** presses pad buttons.
- **`scripts/Capture-GameWindow.ps1`** takes screenshots.

These test menus and rendering without a person at the controller. Keep `net_debug_atlas_packet` off outside debugging. Share logs only after filtering them to `[NorthstarPS4]` lines without `playerToken=` or `password=`.
