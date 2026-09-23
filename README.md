# PS4 Northstar

Experimental PS4 native port of Northstar for Titanfall 2 under shadPS4, targeting `R2PS4_r2dlc11_598_CL297590_2017_12_05_12_36_PM`.

The project now targets the PC Northstar install layout. Mods retain their original `mod.json`, `mod/`, `keyvalues/`, and other content; PS4 differences belong in the native launcher/runtime. **No VPK repacking, baked script manifests, or edits to mod scripts are part of setup.**

**This is not yet a drop-in replacement for the PC launcher.** The runtime-manifest build completes UI startup, supports CLIENT lifecycle hooks and KeyValues overrides, and mounts enabled mods' VPKs through the PS4 filesystem. Reading the shipped BT model from Northstar.Custom is verified; rendering, RPAKs and several Northstar services remain incomplete. See [docs/GOALS.md](docs/GOALS.md).

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
2. Run `.\scripts\New-NorthstarProfile.ps1`. It discovers every immediate mod folder, copies files byte-for-byte, verifies their hashes, and preserves the source profile's `enabledmods.json`. Output is `dist/northstar-profile/R2Northstar`. For subsequent packages, supply a fresh `-Output` directory; existing profiles are not overwritten.
3. Install the toolchain in [tools/README.md](tools/README.md) and run `.\scripts\Build-Northstar.ps1`. The output is `dist/northstar-ps4/northstar_ps4.prx`. The script-injection experiment requires explicit `-EnableExperimentalScriptLoading`; it is known to fail with some real mods.
4. On a clean matching retail install, copy the generated `R2Northstar` folder beside `eboot.bin`. Use `Deploy-Stage2Poc.ps1 -Source .\dist\northstar-ps4\northstar_ps4.prx` to install the runtime with its existing backup behavior. The hash-locked bootstrap is managed by `Enable-Stage2Bootstrap.ps1`; read [docs/TECHNICAL-NOTES.md](docs/TECHNICAL-NOTES.md) before applying it.

The PRX reads `/app0/R2Northstar`; it does not import an old `/app0/mods` or flattened `r2` overlay. A generated `.ns_mod_manifest` is only an emulator fallback when directory enumeration fails; refresh the package if using that fallback after adding/removing folders.

Existing installations with Stage 1 patched VPKs or staged `r2` scripts must be returned to a verified clean retail baseline before testing this architecture. The setup does not guess which old game files are safe to delete or restore. Historical VPK entry points now stop with an error. UI startup with vanilla archives now passes; full script/API parity remains outstanding.

`mods/` contains earlier PS4 compatibility mods; these are optional (`New-NorthstarProfile.ps1 -IncludePs4CompatibilityMods`), not edits applied to PC mods. Windows plugin DLLs and platform-specific assets are not made PS4-compatible by copying them. Packaging preserves files; the PS4 runtime supplies the supported services.

## Development

The current native script experiment is `Build-Northstar.ps1 -EnableRuntimeManifest`. It builds a writable guest script cache from the vanilla manifest and enabled mod metadata, intercepts cached/loose script reads, and installs the profiled UI API/callback adapter. UI startup and mod VPK asset lookup pass in shadPS4; this remains a development build with incomplete services. The older `-EnableExperimentalScriptLoading` uses the separate late-injection experiment.

UI mod-setting changes are saved under guest `/data/northstar_ps4/enabledmods.json`, which takes precedence over the original profile on subsequent boots. The current reload function reports that a restart is required. Before/After UI dispatch executes in completed boots; authentication/download transport and SERVER lifecycle support remain incomplete.


`launcher/include/northstar_ps4/mod_catalog.h` contains portable metadata, enabled-state and ordering policy. `launcher/src/runtime.cpp` contains PS4 module/ABI discovery and engine adapters. `Build-Stage2Poc.ps1` remains available for isolated diagnostic builds. `Invoke-Stage2Iteration.ps1` builds/deploys the PRX and observes emulator logs; it no longer generates script manifests.

Run `.\scripts\Test-NorthstarProfile.ps1` for host catalog and package tests. Game data, extracted archives, downloaded reference sources, toolchains and generated output stay outside Git (`tools/`, `work/`, `dist/`).

Development automation: [AI.Harness installation and commands](docs/AI-HARNESS.md).
