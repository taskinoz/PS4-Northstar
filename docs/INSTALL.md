# Installing PS4 Northstar in shadPS4

## Release status

The target is PC Northstar behaviour with unchanged mod packages. This is an alpha, tested only under shadPS4, not on PS4 hardware.

What works: the Northstar lobby and menus; signing in with an Atlas identity exported from a PC; the server browser; joining public servers; hosting private matches; leaving a match back to the lobby; custom map mods (tested with `mp_box`); mod localisation; controller navigation in the game mode menu; live mod reload; and downloading a server's required mods from the verified list (tested in the lobby, not yet as part of a real server join).

What does not yet: mod rpaks (the shipped ones are PC builds, so custom maps have no loading screen), and mods whose files are PC-only in other ways. Crowded, heavily modded servers can exhaust a 6 GB GPU under shadPS4. See the [release notes](https://github.com/taskinoz/PS4-Northstar/releases) for the current list.

A PRX being present, or mod folders being discovered, does not prove scripts executed. The bootstrap-only build in the rollback section deliberately loads no mods.

## Requirements

- The supported PS4 Titanfall 2 build: `R2PS4_r2dlc11_598_CL297590_2017_12_05_12_36_PM` (this workspace uses CUSA04013).
- Original retail VPKs. Do not run old Stage 1 repacking/merge scripts or restore archives from a modded backup.
- Original Northstar mod folders containing `mod.json`, `mod/`, and any other package content. Keep these files unchanged.
- shadPS4 configured to launch this exact PS4 game folder, using **nightly `2b5666b3` (2026-09-25)**, the latest build it is known to run on. Get it from the shadPS4 CI run (needs a GitHub login; CI artifacts expire after about 90 days):
  `gh run download 36164450310 -R shadps4-emu/shadPS4 -n shadps4-win64-sdl-2026-09-25-2b5666b`
  (https://github.com/shadps4-emu/shadPS4/actions/runs/36164450310 has the Linux and macOS builds too). In the Qt launcher, put `shadPS4.exe` in its own folder under `%APPDATA%\shadPS4QtLauncher\versions` and add it to `versions.json` (or run it directly).
  - Builds before `ca89b01` (2026-09-23), including v0.18.0, crash whenever you leave a loaded map (after a match, map to map, or joining a server from the lobby).
  - Builds from `c6fa48c7` (#5110, 2026-09-25) on, including the 2026-09-26 pre-releases, draw matches black apart from the HUD. Use `2b5666b3` until that is fixed upstream.
- Recommended per-game settings for CUSA04013: `pipeline_cache_enabled: true` (avoids shader-compile stalls that time out remote matches; a cache made by an older shadPS4 build is ignored by `ca89b01`, so delete or rename `%APPDATA%\shadPS4\cache\CUSA04013` after upgrading), `copy_gpu_buffers: false`, and log filter `*:Info Lib.Http:Warning` (shadPS4 otherwise writes the Atlas player token into its log with every request URL).
- PowerShell and the LLVM/OpenOrbis tools listed in [tools/README.md](../tools/README.md) to build from source.

PC DLLs cannot be used as PS4 binaries. The runtime and eboot bootstrap below are the PS4 replacements. Windows plugins and platform-specific assets require their own ports.

## 1. Configure the project

Run commands from the repository root. The Northstar mods come from two pinned submodules, `vendor/NorthstarMods` and `vendor/NorthstarNavs`, at exactly the revisions Northstar v1.31.13 packages (`vendor/northstar-release.json`). Fetch them and assemble the release the same way Northstar's own packaging does:

```bash
git submodule update --init --depth 1
python scripts/Build-NorthstarMods.py
```

That writes the mods to `work/northstar-release/1.31.13/mods`. It is byte-identical to an installed 1.31.13 (`--compare <PC mods dir>` checks this). If `config/local.json` already exists, edit it rather than overwriting it. Otherwise copy `config/project.example.json` to it. Set at least:

```json
{
  "ps4GameRoot": "D:\\PS4\\ShadPS4\\CUSA04013",
  "northstarModsRoot": "C:\\path\\to\\repo\\work\\northstar-release\\1.31.13\\mods"
}
```

Retain the other example fields if using the existing environment tools. `northstarModsRoot` must point directly to the directory containing the individual mod folders, not to an extra nested `R2Northstar` directory. It can still point at a PC install's `R2Northstar\mods` instead, but then the profile follows whatever version that install updates to.

## 2. Package the unchanged mods

Use a new output directory each time:

```powershell
.\scripts\New-NorthstarProfile.ps1 -Output .\dist\install-profile
```

This copies mod folders byte-for-byte and verifies their hashes. It also copies the source profile's `enabledmods.json` when present. It does not modify game archives. Add `-IncludePs4CompatibilityMods` to include this port's own mods, `Northstar.PS4` and `Northstar.DirectConnect`. The runtime needs `Northstar.PS4`: it carries the PS4 fixes (save layout, controller menus, client command bridge) as overrides, so the Northstar mods themselves stay unchanged. `scripts\Sync-NorthstarProfile.ps1` keeps an existing install in sync the same way. Release builds ship both mods in `northstar-ps4-mods-<version>.zip`.

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

**This is an alpha.** Lobby, server browser, joining and private matches work under shadPS4; see "Release status" above for what does not yet.

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

Reloading mods works live, as on PC: the server browser switches client-required mods on and off before a join, and the menus and the next map use the new set. One difference: text from a mod that a reload switches off stays loaded until the game restarts.

Mods that servers require are downloaded, as on PC, when they are on Northstar's [verified list](https://github.com/R2Northstar/VerifiedMods) and `allow_mod_auto_download` is on. They go to guest `/data/northstar_ps4/runtime/remote/mods` (PC: `R2Northstar/runtime/remote/mods`), are checked against the list's SHA-256, and are only switched on for servers that need them.

**What PC mods can use on PS4.** Scripts, `RunOn` conditions, console variables, localisation (English), KeyValues (weapons, playlists, AI settings), VPKs and custom maps, `Dependencies` constants for optional mods, particle manifests, and sound replacements (`audio/` folders, as on PC) work. `ns_print_played_sounds 1` in the console logs the sound events that play, to find their names. Particle-manifest loading is new and has passed build/host tests, but still needs a real particle mod checked in game. Not yet: rpak assets (skins, models, loading-screen images), datatable CSVs, mod console commands (`ConCommands`) and plugins.

### Hosting a private match for other players

In the private lobby, **L1 opens Host Options**:

- **Server browser** opens its own page:
  - **Listed** (`ns_report_server_to_masterserver`, on by default as on PC): whether the lobby is on Northstar's server list. Switching it off takes the lobby off the list straight away.
  - **Name** and **Description** (`ns_server_name`, `ns_server_desc`): typed on the system keyboard. The list shows a change within a few seconds.
  - **Password** (`ns_server_password`): players who join from the server browser are asked for it. The keyboard starts empty; **Done** with nothing typed removes the password, and cancelling keeps the current one. Northstar only takes a password when a server is added to the list, so a new password takes the lobby off the list and adds it again.
- **Other players** (`ns_auth_allow_insecure`, off by default as on PC servers). Players who join through the server browser are signed in by Northstar and always let in. Anyone else who connects (by address, for example) is removed while this is off. Turn it on to play with them.
- **Same account on several machines** (`ns_allow_duplicate_accounts`, PC's `-allowdupeaccounts`). Two machines signed in with the same Atlas identity have the same account, and the second is otherwise refused with "Player's account is already on the server", as on PC.

What you change here is saved in `host_options.txt` in the shadPS4 user data folder (guest `/data/northstar_ps4/`) and kept after a restart. Launch options in `ns_startup_args.txt` in the game folder (next to `eboot.bin`, where PC Northstar keeps it) or in guest `/data/northstar_ps4/ns_startup_args.txt` are applied afterwards, so they win. For example, `-allowdupeaccounts` starts with duplicate accounts on.

**On the server browser.** A PS4-hosted match puts itself on Northstar's server list, as a PC-hosted one does (a private match shows up while its lobby is open). This only works if other players can reach your machine:
- UDP port 37015 must be forwarded on your router to the PC running shadPS4.
- If your internet provider puts you behind carrier-grade NAT (your router's WAN address starts with `100.64`–`100.127`), port forwarding can't work. Ask the provider for a public address.

Northstar checks the port when the match starts, and doesn't list the match if it can't reach it.

Set the name and password in **Host Options → Server browser**. PC-style launch options (`+ns_server_name "My PS4 server"`, `+ns_server_password ...`, `+ns_report_server_to_masterserver 0`) in `ns_startup_args.txt` still work and override the menu.

shadPS4 writes web requests to `shad_log.txt` when its `Lib.Http` log level is Info or lower, and the listing request carries the server password (Northstar's API takes it in the address). Keep `Lib.Http:Warning` in the log filter, or don't share that log, if the password matters.

Players who join through the server browser play with their own Northstar progress, and what they earn is saved back to their account when a match ends or they leave, as on a PC server. To host without saving players' progress, add `+ns_ps4_write_remote_persistence 0` to `ns_startup_args.txt`. As the host you play with your own Northstar progress too, and it is saved to your account the same way. If Northstar can't be reached or your exported token has expired, the lobby still opens, but with a fresh local profile, and nothing from that session is saved to your account.

PC players can connect to a PS4 host: it no longer sends the PS4 client module's checksum, which a PC always rejected with "Your .dll [..\bin\x64_retail\client.dll] differs from the server\'s."

### Text chat

In a match, open the in-game menu and press **L2** to chat with everyone or **R2** to chat with your team; the private lobby has the same buttons. The system keyboard opens, and its Send button posts the message. Messages show at the bottom left of the screen, as on PC. The `say` and `say_team` console commands work too.

### Mods that make web requests

Mods can make web requests through Northstar's HTTP functions, as on PC. The same launch options apply, in `ns_startup_args.txt`:
- `-disablehttprequests` turns requests off;
- `-allowlocalhttp` lets mods reach addresses on your own network, which are refused by default;
- `-disablehttpssl` skips the certificate host name check.

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

Joining uses the imported identity to request a per-connection token from Atlas (`auth_with_server`), as PC does, so verified servers accept the connection. If the token has expired, the join dialog says to re-export it.

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
