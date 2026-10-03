# Installing PS4 Northstar

PS4 Northstar brings [Northstar](https://northstar.tf), the Titanfall 2 multiplayer mod platform, to the PS4 version of the game running in the [shadPS4](https://shadps4.net) emulator. You get Northstar's server browser, custom servers and mods, with your Northstar progress, on the same servers PC players use.

This guide is for players. To build from source, see [BUILDING.md](BUILDING.md); for everything you can do once it's installed, see [USING.md](USING.md).

> **Status.** Tested in shadPS4 only. Nothing has been tested on a real PS4 yet.

## What you need

- **Titanfall 2 for PS4**, dumped from your own copy, at the final patch: `CUSA04013`, build `R2PS4_r2dlc11_598_CL297590_2017_12_05_12_36_PM`. The game's archives (`vpk_ps4/`) must be the original, unmodified ones.
- **shadPS4**, set up to run that game folder. See [Which shadPS4 build](#which-shadps4-build) below.
- **An EA account that owns Titanfall 2**, signed in to the **EA app** on a Windows PC (or a Mac, untested). Northstar signs you in through it.
- The **NorthstarPS4 Token Helper** for your computer, from the [PS4 Northstar releases page](https://github.com/taskinoz/PS4-Northstar/releases): the Windows installer or `.exe`, the macOS `.dmg`, or the Linux AppImage or `.deb`. It installs PS4 Northstar into the game folder, keeps it up to date, and signs you in.

## Which shadPS4 build

| shadPS4 build | Result |
| --- | --- |
| Nightly **`2b5666b3`** (2026-09-25) | **Recommended.** The build PS4 Northstar is documented against. |
| Prerelease `94e21778` and nightly `4cbd23ef` | Work well in testing (lobby, hosted matches, remote servers). Recommended once they've had longer play. |
| `c6fa48c7` up to `f6cd16e8` (including the 2026-09-26 pre-releases) | Levels draw black apart from the HUD ([shadPS4#5124](https://github.com/shadps4-emu/shadPS4/issues/5124)). Avoid. |
| Before `ca89b01` (including v0.18.0) | Crash whenever you leave a loaded map. Avoid. |

`2b5666b3` is in shadPS4's CI run [36164450310](https://github.com/shadps4-emu/shadPS4/actions/runs/36164450310) (downloading it needs a GitHub login; CI downloads expire after about 90 days). In the shadPS4 Qt launcher, put `shadPS4.exe` in its own folder under `%APPDATA%\shadPS4QtLauncher\versions` and add it to `versions.json`, or run it directly.

Per-game settings for `CUSA04013`:

| Setting | Value | Why |
| --- | --- | --- |
| `pipeline_cache_enabled` | `true` | Avoids shader-compile stalls that time out online matches. |
| `copy_gpu_buffers` | `false` | Tested setting. |
| Log filter | `*:Info Lib.Http:Warning` | Otherwise shadPS4 writes your Northstar sign-in token, and your server password when hosting, into its log. |

Shader caches aren't shared between shadPS4 builds: after switching builds, move `%APPDATA%\shadPS4\cache\CUSA04013` aside.

## Install

The **game folder** is the folder with the game's `eboot.bin` and `vpk_ps4` (for example `D:\PS4\CUSA04013`).

1. **Open the token helper.** It isn't signed, so the first time:
   - **Windows:** select **More info**, then **Run anyway**.
   - **macOS:** open **System Settings → Privacy & Security**, select **Open Anyway** next to its name, and confirm.
2. On the **Install** tab, select **Choose…** and pick the game folder. The helper checks it: "Titanfall 2 found. PS4 Northstar isn't installed yet."
3. Select **Install**. The helper downloads the newest PS4 Northstar release and Northstar's mods (about 130 MB), checks every download, and installs them. **Progress** shows each step.

What it changes in the game folder:
- **`R2Northstar\mods`:** Northstar's own mods, PS4 Northstar's mods (`Northstar.PS4`, `Northstar.DirectConnect`), and Northstar.Custom's textures converted for the PS4. Other mods already there are left alone.
- **`bin\ps4_retail\northstar_ps4.prx`:** the runtime.
- **`eboot.bin`:** a few bytes are added so the game loads the runtime. This only works on the exact original `eboot.bin`, and the original is kept as `eboot.bin.northstar-stage2.bak`.

The game's archives are never changed.

When you're done, the game folder looks like this:

```text
<game folder>/
  eboot.bin                           (with the bootstrap added)
  eboot.bin.northstar-stage2.bak      (the original)
  bin/ps4_retail/northstar_ps4.prx
  R2Northstar/
    mods/
      Northstar.Client/
      Northstar.Custom/
      Northstar.CustomServers/
      Northstar.PS4/
      Northstar.DirectConnect/
      ...
  vpk_ps4/                            (unchanged)
```

## Sign in

Northstar needs to know who you are. A PS4 has no EA app, so the token helper signs the game in for it, and keeps it signed in.

1. Open the **EA app** and sign in with the account that owns Titanfall 2. On the token helper's **Sign in** tab, **EA account** shows the account it found.
2. Start Titanfall 2 in shadPS4. Under **Where is Northstar running?**, select **In shadPS4 on this computer** (or **On a PS4, or in shadPS4 on another computer**, then type the address and code the game shows when you select Launch Northstar). Select **Sign in**.
3. **Keep the token helper open while you play.** Your sign-in lasts about a day. When it runs out, the game gets a new one from the helper by itself.

## Play

From the main menu, select **Launch Northstar**. The first start takes longer while shadPS4 builds its shader cache. You land in the Northstar lobby, and **Server Browser** lists Northstar's servers, as on PC.

To use more mods, add their folders to `<game folder>\R2Northstar\mods` and restart. Mods that servers require are downloaded when you join, as on PC. [USING.md](USING.md) covers which mods work, hosting your own matches, chat, controller menus and more.

## Update

Open the token helper. When a new release is out, the **Install** tab offers **Update to <version>**: select it. Your settings, enabled mods, sign-in and mod save data are kept. **Check for updates** looks again. Download a new token helper from the releases page when one is out.

## Uninstall

On the **Install** tab, open **Uninstall** and select **Uninstall PS4 Northstar**:
- It puts back the original `eboot.bin` and removes the runtime, so the game starts as normal.
- Tick **Also delete the R2Northstar folder and every mod in it** to remove the mods too.
- Your Northstar settings and sign-in stay in shadPS4's data folder (`%APPDATA%\shadPS4\data\northstar_ps4` on Windows). Delete that folder to remove them as well.

## Troubleshooting

| What you see | What to do |
| --- | --- |
| Launch Northstar says **Not signed in to Northstar**, with an address and code | Sign in with the token helper ([Sign in](#sign-in)). The address and code are for a PS4 or another computer. |
| Joining a server fails with **Token refresh failed: the token helper could not be reached** | Start the token helper, then join again. |
| The lobby opens with a fresh profile instead of your progress | Northstar couldn't confirm your sign-in. Check the token helper is running and signed in to the EA app, then go back to the main menu and select Launch Northstar again. |
| Levels are black apart from the HUD | Your shadPS4 build has the black-level bug. Use a build from [Which shadPS4 build](#which-shadps4-build). |
| The game crashes when a match ends or you change maps | Your shadPS4 build is older than `ca89b01`. Update shadPS4. |
| A message names a mod with a **script compilation error** | That mod (or one it needs) is broken or missing a dependency. Disable it in **Mods**, or install what it needs. |
| Textures show as a magenta-and-black checkerboard | A mod's textures or paks are in a format the PS4 can't read. Northstar.Custom's need `northstar-custom-ps4-rpaks-<version>.zip`. Other mods' RPak texture packs need converting (see [USING.md](USING.md#mods)). |
| Boot stops on the Respawn logo | Close the game and start it again. If it keeps happening, remove recently added mods to find the one causing it. |
| Others can't join a match you host | See [Hosting](USING.md#hosting-a-match-for-other-players): UDP port 37015 must be forwarded to your computer. |
| The token helper says the folder's **eboot.bin isn't the supported Titanfall 2 version** | Your `eboot.bin` isn't the expected original: a different game version, or changed by something else. Restore the original from your own backup. |
| Installing fails while downloading | Check your internet connection and select the button again; nothing is changed until every download has arrived and been checked. |

When reporting a problem, include shadPS4's log (`%APPDATA%\shadPS4\log\shad_log.txt`). With the log filter above it contains no sign-in token. Still, check it before posting, and never share `atlas_identity.json` from the data folder.

## Installing by hand

Developers, or anyone without the token helper, can install from the release files and the repository's scripts instead; see [BUILDING.md](BUILDING.md#installing-a-release-by-hand).

## What's not supported

- Real PS4 hardware: untested.
- Plugins (Windows DLLs): can't run on a PS4.
- Hosting dedicated servers on the PS4: you can host matches from your game, but not a dedicated server.
- Mods whose RPak files are compressed, patch, model or UI-image paks: not readable yet.
- Crowded, heavily modded servers can run a 6 GB graphics card out of memory in shadPS4.

The [release notes](https://github.com/taskinoz/PS4-Northstar/releases) list the known issues for each version.
