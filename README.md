# PS4 Northstar

Experimental PS4 native port of Northstar for Titanfall 2 under shadPS4, targeting `R2PS4_r2dlc11_598_CL297590_2017_12_05_12_36_PM`.

The project now targets the PC Northstar install layout. Mods retain their original `mod.json`, `mod/`, `keyvalues/`, and other content; PS4 differences belong in the native launcher/runtime. **No VPK repacking, baked script manifests, or edits to mod scripts are part of setup.**

**Status: alpha, tested under shadPS4 only.** The Northstar lobby, the server browser, joining public servers (with an Atlas identity exported from PC Northstar), hosting private matches, and leaving back to the lobby all work. Mod downloads, mod rpaks and live mod reload do not yet. Releases, with the shadPS4 build to use, are on the [releases page](https://github.com/taskinoz/PS4-Northstar/releases); the current tracker is [docs/GOALS.md](docs/GOALS.md).

For the current feature-completeness roadmap and agent handoff, use [docs/GOALS.md](docs/GOALS.md). The [native API inventory](docs/NATIVE-API-INVENTORY.md) maps the pinned PC registrations to PS4 handlers and can be regenerated from source. Historical technical notes do not override the current tracker.

## Layout

```text
Titanfall2 PS4/
  eboot.bin                         # hash-locked PS4 bootstrap
  bin/ps4_retail/northstar_ps4.prx    # PS4 native runtime
  R2Northstar/
    enabledmods.json                # PC Name -> Version -> bool format
    mods/
      Author.Mod/
        mod.json
        mod/
        keyvalues/                  # native merged overrides
        vpk/                        # original mod VPKs, mounted by runtime
  vpk_ps4/                          # original retail archives
```

Add/remove mod folders in `R2Northstar/mods` and restart the game. No mod list is compiled into the PRX. Discovery uses each mod's metadata `Name` and `Version` for enabled state. New mods default enabled; lower `LoadPriority` loads first and higher priority wins file overrides. Equal priorities use folder-name ordering for deterministic PS4 behavior. Current bounds: 128 mod folders, folder/metadata names below 64 bytes and metadata below 16 KiB. Other prototype limits are described in the catalog header.

See [the shadPS4 installation guide](docs/INSTALL.md) for exact build/deploy commands, runtime identification and rollback.

## Setup and build

1. Run `git submodule update --init --depth 1` and `python scripts/Build-NorthstarMods.py` to assemble the pinned Northstar 1.31.13 mods, then copy `config/project.example.json` to `config/local.json` and set `northstarModsRoot` to `work/northstar-release/1.31.13/mods` (see [docs/INSTALL.md](docs/INSTALL.md)). Packaging only needs this source, not a PC executable or extracted game content.
2. Run `.\scripts\New-NorthstarProfile.ps1 -IncludePs4CompatibilityMods`. It discovers every immediate mod folder, copies files byte-for-byte, verifies their hashes, and preserves the source profile's `enabledmods.json`. The switch adds this repository's `Northstar.PS4` (required by the runtime) and `Northstar.DirectConnect`. Output is `dist/northstar-profile/R2Northstar`; supply a fresh `-Output` for later packages. `.\scripts\Sync-NorthstarProfile.ps1` keeps an installed profile in sync instead.
3. Install the toolchain in [tools/README.md](tools/README.md) and run `.\scripts\Build-Northstar.ps1 -EnableRuntimeManifest -Output .\dist\northstar-runtime-manifest`. That is the supported build; the default and `-EnableExperimentalScriptLoading` builds are older experiments.
4. On a clean matching retail install, copy the generated `R2Northstar` folder beside `eboot.bin`, install the runtime with `Deploy-Stage2Poc.ps1 -Source .\dist\northstar-runtime-manifest\northstar_ps4.prx` (it keeps a backup), and patch the eboot once with `Enable-Stage2Bootstrap.ps1`. [docs/INSTALL.md](docs/INSTALL.md) has the details, the shadPS4 build to use, and signing in.

The PRX reads `/app0/R2Northstar`; it does not import an old `/app0/mods` or flattened `r2` overlay. A generated `.ns_mod_manifest` is only an emulator fallback when directory enumeration fails; refresh the package if using that fallback after adding/removing folders.

Existing installations with Stage 1 patched VPKs or staged `r2` scripts must be returned to a verified clean retail baseline before testing this architecture. The setup does not guess which old game files are safe to delete or restore. Historical VPK entry points now stop with an error. UI startup with vanilla archives now passes; full script/API parity remains outstanding.

`mods/` holds this port's own mods: `Northstar.PS4` (PS4 fixes as overrides of Northstar files, such as the save layout and controller-friendly menus; required), `Northstar.DirectConnect`, and the opt-in development `AI.Harness`. None of them edit the PC mods. Windows plugin DLLs and platform-specific assets are not made PS4-compatible by copying them. Packaging preserves files; the PS4 runtime supplies the supported services.

## Development

The supported build is `Build-Northstar.ps1 -EnableRuntimeManifest`. It builds the guest script manifest from the vanilla one and enabled mod metadata, serves mod files through the PS4 filesystem, and hooks the UI, CLIENT and SERVER script VMs (constants, Northstar natives, lifecycle callbacks). It also registers the console commands PC Northstar adds (`setplaylist`, `ns_start_reauth_and_leave_to_lobby`). The older `-EnableExperimentalScriptLoading` is a separate late-injection experiment.

UI mod-setting changes are saved under guest `/data/northstar_ps4/enabledmods.json`, which takes precedence over the original profile on subsequent boots; the reload function reports that a restart is required. Mod downloads remain unimplemented.


`launcher/include/northstar_ps4/mod_catalog.h` contains portable metadata, enabled-state and ordering policy. `launcher/src/runtime.cpp` contains PS4 module/ABI discovery and engine adapters. `Build-Stage2Poc.ps1` remains available for isolated diagnostic builds. `Invoke-Stage2Iteration.ps1` builds/deploys the PRX and observes emulator logs; it no longer generates script manifests.

Run `.\scripts\Test-NorthstarProfile.ps1` for host catalog and package tests. Game data, extracted archives, downloaded reference sources, toolchains and generated output stay outside Git (`tools/`, `work/`, `dist/`).

Development automation: [AI.Harness installation and commands](docs/AI-HARNESS.md), plus `scripts/Send-PadInput.ps1` (pad buttons) and `scripts/Capture-GameWindow.ps1` (screenshots) for testing menus and rendering without a person at the controller.
