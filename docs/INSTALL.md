# Installing PS4 Northstar in shadPS4

## Release status

The target is PC Northstar behavior with unchanged mod packages, except authentication is disabled. **The current build has not reached that target**, though it is considerably closer than before: the UI script VM now compiles the core mods and runs Northstar's full UI startup, including every mod Before/After callback, in load order.

What still does not work: the CLIENT and SERVER script VMs have no lifecycle support, so anything those contexts provide is absent; authentication is deliberately disabled; and mod downloads and the server browser have no transport. No map load has been validated.

A PRX being present, or mod folders being discovered, does not prove scripts executed. The bootstrap-only build in the rollback section deliberately loads no mods.

## Requirements

- The supported PS4 Titanfall 2 build: `R2PS4_r2dlc11_598_CL297590_2017_12_05_12_36_PM` (this workspace uses CUSA04013).
- Original retail VPKs. Do not run old Stage 1 repacking/merge scripts or restore archives from a modded backup.
- Original Northstar mod folders containing `mod.json`, `mod/`, and any other package content. Keep these files unchanged.
- shadPS4 configured to launch this exact PS4 game folder.
- PowerShell and the LLVM/OpenOrbis tools listed in [tools/README.md](../tools/README.md) to build from source.

PC DLLs cannot be used as PS4 binaries. The runtime and eboot bootstrap below are the PS4 replacements. Windows plugins and platform-specific assets require their own ports.

## 1. Configure the project

Run commands from the repository root. If `config/local.json` already exists, edit it rather than overwriting it. Otherwise copy `config/project.example.json` to it. Set at least:

```json
{
  "ps4GameRoot": "D:\\PS4\\ShadPS4\\CUSA04013",
  "northstarModsRoot": "D:\\Games\\Titanfall2\\R2Northstar\\mods"
}
```

Retain the other example fields if using the existing environment tools. `northstarModsRoot` must point directly to the directory containing the individual mod folders, not to an extra nested `R2Northstar` directory.

## 2. Package the unchanged mods

Use a new output directory each time:

```powershell
.\scripts\New-NorthstarProfile.ps1 -Output .\dist\install-profile
```

This copies mod folders byte-for-byte and verifies their hashes. It also copies the source profile's `enabledmods.json` when present. It does not modify game archives. Do not add `-IncludePs4CompatibilityMods` for the unchanged-PC-mod configuration.

For a fresh installation, copy `dist/install-profile/R2Northstar` beside the game's `eboot.bin`. If a profile already exists there, preserve it outside the active game folder before replacing it; do not merge an old modded profile into the fresh one.

Expected layout:

```text
CUSA04013/
  eboot.bin
  bin/ps4_retail/northstar_ps4.prx
  R2Northstar/
    enabledmods.json
    mods/
      Northstar.Client/mod.json
      Northstar.Client/mod/...
      Northstar.Custom/mod.json
      Northstar.Custom/mod/...
      Northstar.CustomServers/mod.json
      Northstar.CustomServers/mod/...
  vpk_ps4/                          original retail files
```

Additional mod folders follow the same structure. There must not be an extra `mods/mods` nesting.

## 3. Build and install the experimental runtime

Close the game before replacing its PRX.

```powershell
.\scripts\Test-NorthstarProfile.ps1
.\scripts\Build-Northstar.ps1 -EnableRuntimeManifest -Output .\dist\northstar-runtime-manifest
.\scripts\Deploy-Stage2Poc.ps1 -Source .\dist\northstar-runtime-manifest\northstar_ps4.prx
```

The deployment script backs up the previous PRX in `work/stage2/deploy-backups` and verifies the installed hash. The build produces `northstar_ps4.build.json` beside the PRX; retain it to identify the build. The default `Build-Northstar.ps1` lacks runtime-manifest loading. The older `-EnableExperimentalScriptLoading` flag selects a different late-injection experiment; do not use it for this installation.

**This experimental build completes UI startup but is not a finished port.** CLIENT lifecycle support is implemented; SERVER context support, downloads and the server browser remain incomplete, and further map/asset validation is required. Use it for development and log collection, not as a working replacement for the PC launcher.

## 4. Enable the eboot bootstrap once

Only for a fresh, matching retail eboot with no Northstar bootstrap:

```powershell
.\scripts\Enable-Stage2Bootstrap.ps1 -GameRoot 'D:\PS4\ShadPS4\CUSA04013'
```

This changes the PS4 executable to load the PRX and records a backup. It does not touch VPKs. The script rejects an unknown eboot hash. Do not force past that check. An already bootstrapped installation does not need patching again when updating the PRX.

## 5. Check the installation and launch

```powershell
.\scripts\Get-NorthstarInstallStatus.ps1
```

The diagnostic is read-only. It matches the installed PRX hash to local build information and reports:

- **Bootstrap only (mods not loaded):** the safe build is installed; no Northstar UI is expected.
- **Filesystem overrides only:** not a complete script loader.
- **Experimental native mod loader:** the runtime-manifest build is installed, but compatibility is still unverified.
- **Unknown build:** there is no matching build record; the diagnostic will not guess its features.

If needed, supply the matching record explicitly with `-BuildInfo .\dist\northstar-runtime-manifest\northstar_ps4.build.json`. A mismatched PRX hash is rejected.

Launch the game's `eboot.bin` through shadPS4. In this workspace the log is `C:\Users\tristan\AppData\Roaming\shadPS4\log\shad_log.txt`. Inspect only the current boot's lines. `runtime manifest generated` and `mod file served` prove progress, not successful startup. Look for `FatalError` and `UI SCRIPT COMPILE ERROR`. Even `UI lifecycle hook installed` does not prove the callbacks executed; successful dispatch logs each `UI Before:` and `UI After:` name and ends with `UI lifecycle completed`. A callback a mod declares but never defines is logged as `callback not found` and skipped, which is what PC does. Map validation must still be performed separately.

## Mod settings and authentication

Settings use the PC `Name -> Version -> bool` format. On PS4, saved changes live in guest `/data/northstar_ps4/enabledmods.json` because `/app0` is read-only. That saved file overrides `R2Northstar/enabledmods.json` on subsequent boots. Its host location depends on the emulator's data mount; do not confuse it with the installation profile.

The current reload API saves changes then reports that a restart is required. True live reload remains unfinished. Downloads remain a compatibility gap rather than an intended exclusion.

### Signing in to Atlas

The PS4 build has no Origin session of its own, so it cannot perform the Origin exchange Northstar uses on PC. Instead it reads an identity exported from a PC that is already signed in. No EA credential ever reaches the console: only the account uid and the Northstar player token are copied.

1. On the PC, start Northstar and wait for its log to say `Northstar origin authentication completed successfully`.
2. Run `scripts\Export-AtlasCredentials.ps1`. It reads the uid and player token out of the running client, cross-checks the uid against the client's own log, and refuses to export on a mismatch. Add `-WhatIf` to see what it would write without writing it.
3. It writes `atlas_identity.json` into the emulator's `/data/northstar_ps4/`. Point `-Output` elsewhere if your data mount differs.

The console picks the file up at startup, applies the uid to `platform_user_id`, and reports the session to the menu. If anything is missing the menu says what to do rather than only that it failed:

| State | What it means |
| --- | --- |
| `PS4_AUTH_IMPORTED` | Signed in with the imported identity. |
| `PS4_AUTH_NO_IDENTITY` | No `atlas_identity.json`. Export one and copy it to `/data/northstar_ps4/`. |
| `PS4_AUTH_NO_TOKEN` | The file has a uid but no `playerToken`. Re-export it from a signed-in PC client. |
| `PS4_AUTH_BAD_IDENTITY` | The file could not be read, or the token is not 32 hex characters. Re-export it. |

**The exported file is a live credential.** Anyone holding it can authenticate to Atlas as that account. It expires after 24 hours, and sooner if the PC client authenticates again, which mints a replacement and invalidates the copy. Do not commit or share it; re-export when it stops working.

Joining a server that verifies also needs a per-connection token from Atlas, which is not wired up yet, so this currently signs in rather than completing a verified join. Servers running `ns_auth_allow_insecure 1` do not check either value.

Mods that use Northstar's Safe I/O API keep their data under guest `/data/northstar_ps4/save_data/<mod folder>`, one folder per mod, with the same rules PC applies: ASCII-only paths that cannot leave the mod's own folder, `.txt` and `.json` writes only, and a 50 MiB per-mod cap. As with enabled settings, the host location of that guest path depends on the emulator's data mount.

## Return to ordinary game startup

Install the bootstrap-only PRX:

```powershell
.\scripts\Build-Stage2Poc.ps1 -Output .\dist\northstar-safe-bootstrap
.\scripts\Deploy-Stage2Poc.ps1 -Source .\dist\northstar-safe-bootstrap\northstar_ps4.prx
.\scripts\Get-NorthstarInstallStatus.ps1
```

This keeps the eboot bootstrap but disables mod injection. To remove the bootstrap entirely, first verify that `eboot.bin.northstar-stage2.bak` is the correct original retail eboot (expected SHA256 `590956ab2c9251f588348a1c066ed4045ce94ffc87c25faa5872a1f92ec35824`), then run:

```powershell
.\scripts\Enable-Stage2Bootstrap.ps1 -GameRoot 'D:\PS4\ShadPS4\CUSA04013' -Disable
```

Neither rollback method requires changing the VPKs or mod source files.


## Mod VPK assets (verified 2026-09-17)

The runtime-manifest build mounts enabled mods' existing VPKs through the PS4 game filesystem. Keep the PC folder layout and all archive chunks together:

```text
R2Northstar/mods/Northstar.Custom/
  mod.json
  vpk/
    englishclient_mp_northstar_common.bsp.pak000_dir.vpk
    client_mp_northstar_common.bsp.pak000_000.vpk
```

Use the build/deploy commands in section 3 and restart the game. No additional asset pack is required for the shipped Buddy/BT models, and no retail archive needs editing or repacking. Do not move mod archives into `vpk_ps4`.

An absent `vpk/vpk.json` preloads the mod's archives. An object with `"Preload": true` does the same; `false` or an object without that member mounts only when an engine mount has the same archive stem. Comments and trailing commas are supported. Mods follow enabled state and ascending load priority; changes require a restart. Only the supported `english*.bsp.pak000_dir.vpk` naming convention is discovered, and overlong paths are rejected with a log message.

A successful boot logs `mod VPK discovered`, a non-null `mod VPK mount` result and `UI lifecycle completed`. With the shipped Northstar.Custom archive, the diagnostic also logs `VPK asset lookup: models/titans/buddy/titan_buddy.mdl read=12 IDST=1`. This verifies an engine asset read. In-game model rendering, texture/material compatibility and RPAK loading remain separate checks; arbitrary PC assets are not guaranteed compatible.

## Optional local automation

Install [AI.Harness](AI-HARNESS.md) to queue Northstar multiplayer launch before boot and send console/menu commands with correlated replies. It is excluded from normal profile packaging unless `-IncludeAIHarness` is supplied. Retail VPKs remain unchanged.
