# PS4 Northstar

[Northstar](https://northstar.tf), the Titanfall 2 multiplayer mod platform, for the PS4 version
of Titanfall 2 (`CUSA04013`, patch 1.13), in the [shadPS4](https://shadps4.net) emulator or on a
PS4 with GoldHEN.

It runs Northstar's own, unchanged mods through a native PS4 runtime:
- **Server browser:** join PC and PS4 Northstar servers with your Northstar account and
  progress.
- **Hosting:** host matches that appear in the browser, for PC, PS4 and shadPS4 players.
- **Mods:** install mods as on PC, and join servers that download their mods.

Tested in shadPS4 on Windows, and as a GoldHEN plugin on a standard PS4 on firmware 9.00.

## Guides

- **[Installing](docs/INSTALL.md):** shadPS4, PS4, the token helper on Windows, macOS and Linux,
  and PC players joining.
- **[Using](docs/USING.md):** signing in, mods, controller menus, joining and hosting, chat,
  launch options.
- **[Building](docs/BUILDING.md):** building, testing and releasing.
- **[Internals](docs/INTERNALS.md):** how the runtime works, and what is known about the PS4
  game it hooks.

Downloads, with notes and known issues for each version, are on the
[releases page](https://github.com/taskinoz/PS4-Northstar/releases).

## How it works

- **The runtime**, `northstar_ps4.prx`, is a PS4 module. In shadPS4 it is loaded by a few bytes
  added to the game's `eboot.bin`; on a PS4, GoldHEN loads it as a plugin.
- It serves mod files through the game's own filesystem, runs Northstar's scripts and natives in
  the UI, CLIENT and SERVER script VMs, and talks to Northstar's master server.
- The game's archives and Northstar's mods stay byte for byte unchanged. PS4 differences live in
  the runtime or as overrides in this port's `Northstar.PS4` mod.
- **The token helper**, an app for Windows, macOS and Linux, signs the game in to Northstar
  through the EA app on a computer, keeps it signed in, and installs PS4 Northstar into a shadPS4
  game folder.

## Repository

| Path | |
| --- | --- |
| `launcher/` | The runtime (C++). Portable logic is in `launcher/include/northstar_ps4`. |
| `mods/` | This port's mods: `Northstar.PS4` (required), `Northstar.DirectConnect`, and the test harness `AI.Harness`. |
| `token-helper/` | The token helper (Tauri 2: Rust and a web page). |
| `scripts/` | Build, install, test and generator scripts. Settings come from `.env` ([.env.example](.env.example)). |
| `tests/` | Host tests, the game's engine profile, and fake services for the token helper. |
| `vendor/` | Northstar's mods and navmeshes at v1.31.13, as submodules. |
| `docs/` | The guides and the [native API inventory](docs/NATIVE-API-INVENTORY.md). |
| `tools/` | External toolchains ([checklist](tools/README.md)). |

The repository never contains Titanfall 2 files, Northstar release binaries, or extracted or
repacked game archives. `tools/`, `work/` and `dist/` are ignored.
