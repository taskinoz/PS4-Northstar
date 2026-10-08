# PS4 Northstar

[Northstar](https://northstar.tf), the Titanfall 2 multiplayer mod platform, for the PS4 version of Titanfall 2 running in the [shadPS4](https://shadps4.net) emulator (`CUSA04013`, final patch `R2PS4_r2dlc11_598_CL297590_2017_12_05_12_36_PM`).

It runs Northstar's own, unchanged mods through a native PS4 runtime. You get:
- **Northstar's server browser:** join PC and PS4 servers, with your Northstar account and progress.
- **Hosting:** host matches that appear in the browser.
- **Mods:** install mods as on PC, and join servers that download their mods.

**Status:** tested most in shadPS4. It also runs on a real PS4 with GoldHEN, as a plugin: tested on one console ([installing on a PS4](docs/INSTALL.md#on-a-ps4)).

## Guides

- **[Installing](docs/INSTALL.md):** what you need, installing, signing in, updating and uninstalling.
- **[Using](docs/USING.md):** signing in with the token helper, mods, controller menus, joining and hosting, chat, launch options.
- **[Building from source](docs/BUILDING.md):** for developers.

Downloads, with notes and known issues for each version, are on the [releases page](https://github.com/taskinoz/PS4-Northstar/releases).

## How it works

- **The runtime:** a PS4 module, `northstar_ps4.prx`. In shadPS4 it sits at `bin/ps4_retail/northstar_ps4.prx`, and a few bytes added to the game's `eboot.bin` make it load the runtime. On a PS4, GoldHEN loads it as a plugin, and the mods are in `/data/northstar_ps4/R2Northstar`.
- **What the runtime does:**
  - serves mod files from `R2Northstar/mods` through the game's own filesystem;
  - hooks the UI, CLIENT and SERVER script VMs to run Northstar's scripts and natives;
  - talks to Northstar's master server.
- **Unchanged files:** the retail archives (`vpk_ps4/`) and Northstar's mods stay byte-for-byte unchanged. Platform differences live in the runtime, or as overrides in this port's own `Northstar.PS4` mod.
- **The token helper:** a small app for Windows, macOS and Linux that installs PS4 Northstar into the game folder and keeps it up to date. A PS4 has no EA app, so it also signs the game in to Northstar through the EA app on a computer, and keeps it signed in.

```text
<game folder>/
  eboot.bin                             with the bootstrap
  bin/ps4_retail/northstar_ps4.prx      the runtime
  R2Northstar/
    mods/                               Northstar's mods, Northstar.PS4, your mods
  vpk_ps4/                              retail archives, unchanged
```

## Repository

| Path | What it is |
| --- | --- |
| `launcher/` | The PS4 runtime (C++). Portable logic is in `launcher/include/northstar_ps4`, with host tests in `tests/`. |
| `mods/` | This port's mods: `Northstar.PS4` (required), `Northstar.DirectConnect`, and the development `AI.Harness`. |
| `token-helper/` | The token helper app (Tauri: Rust and a web page). |
| `scripts/` | Build, packaging, deploy, test and diagnostic scripts. |
| `vendor/` | Northstar's mods and navmeshes at v1.31.13, as submodules. |
| `docs/` | The guides, plus [GOALS.md](docs/GOALS.md) (the feature tracker), [TECHNICAL-NOTES.md](docs/TECHNICAL-NOTES.md) (findings, in date order) and the [native API inventory](docs/NATIVE-API-INVENTORY.md). |

Game data, extracted archives, toolchains and build output stay out of Git (`tools/`, `work/`, `dist/`). The repository never contains Titanfall 2 files, Northstar release binaries, or extracted or repacked game archives.
