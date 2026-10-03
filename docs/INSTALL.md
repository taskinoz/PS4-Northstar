# Installing PS4 Northstar

PS4 Northstar brings [Northstar](https://northstar.tf), the Titanfall 2 multiplayer mod platform, to the PS4 version of the game running in the [shadPS4](https://shadps4.net) emulator. You get Northstar's server browser, custom servers and mods, with your Northstar progress, on the same servers PC players use.

This guide is for players. To build from source, see [BUILDING.md](BUILDING.md); for everything you can do once it's installed, see [USING.md](USING.md).

> **Status.** Tested in shadPS4 only. Nothing has been tested on a real PS4 yet.

## What you need

- **Titanfall 2 for PS4**, dumped from your own copy, at the final patch: `CUSA04013`, build `R2PS4_r2dlc11_598_CL297590_2017_12_05_12_36_PM`. The game's archives (`vpk_ps4/`) must be the original, unmodified ones.
- **shadPS4**, set up to run that game folder. See [Which shadPS4 build](#which-shadps4-build) below.
- **An EA account that owns Titanfall 2**, signed in to the **EA app** on a Windows PC (or a Mac, untested). Northstar signs you in through it.
- From the [PS4 Northstar releases page](https://github.com/taskinoz/PS4-Northstar/releases):
  - `northstar_ps4.prx`, the runtime;
  - `northstar-ps4-mods-<version>.zip`, this port's own mods;
  - `northstar-custom-ps4-rpaks-<version>.zip`, Northstar.Custom's textures converted for the PS4;
  - the **NorthstarPS4 Token Helper** for your computer (Windows installer or `.exe`, macOS `.dmg`, Linux AppImage or `.deb`).
- **Northstar 1.31.13** from [Northstar's releases](https://github.com/R2Northstar/Northstar/releases/tag/v1.31.13): `Northstar.release.v1.31.13.zip`. Only its mods are used, not the PC launcher.
- `Enable-Stage2Bootstrap.ps1` from this repository's [`scripts/`](../scripts/Enable-Stage2Bootstrap.ps1) folder, run once to let the game load the runtime. It runs in Windows PowerShell, which every Windows PC has.

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

In these steps, the **game folder** is the folder with the game's `eboot.bin` (for example `D:\PS4\CUSA04013`). The **data folder** is shadPS4's save data for PS4 Northstar: `%APPDATA%\shadPS4\data\northstar_ps4` on Windows.

1. **Back up the game folder** if you haven't already, or at least `eboot.bin`.
2. **Add the Northstar mods.** Open `Northstar.release.v1.31.13.zip` and copy its `R2Northstar\mods` folder into the game folder, so you have `<game folder>\R2Northstar\mods\Northstar.Client`, `Northstar.Custom` and so on. You don't need anything else from that zip.
3. **Add PS4 Northstar's mods.** Extract `northstar-ps4-mods-<version>.zip` into the game folder. It adds `Northstar.PS4` (required: the PS4 fixes live in it) and `Northstar.DirectConnect` beside the Northstar mods.
4. **Add the converted textures.** Extract `northstar-custom-ps4-rpaks-<version>.zip` into the game folder, and replace the files when asked. The PC versions of these files can't be read on the PS4.
5. **Add the runtime.** Copy `northstar_ps4.prx` to `<game folder>\bin\ps4_retail\northstar_ps4.prx`.
6. **Let the game load it (once).** In PowerShell, in the folder where you saved `Enable-Stage2Bootstrap.ps1`:

   ```powershell
   powershell -ExecutionPolicy Bypass -File .\Enable-Stage2Bootstrap.ps1 -GameRoot 'D:\PS4\CUSA04013'
   ```

   This adds a few bytes to `eboot.bin` so that it loads the runtime. It only works on the exact original `eboot.bin`, and it keeps that original as `eboot.bin.northstar-stage2.bak`. Updates don't need it again.

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

There must be no extra level such as `R2Northstar\mods\mods`.

## Sign in

Northstar needs to know who you are. A PS4 has no EA app, so the **NorthstarPS4 Token Helper** on your computer signs the game in for it, and keeps it signed in.

1. Install or open the token helper. It isn't signed, so the first time:
   - **Windows:** select **More info**, then **Run anyway**.
   - **macOS:** open **System Settings → Privacy & Security**, select **Open Anyway** next to its name, and confirm.
2. Open the **EA app** and sign in with the account that owns Titanfall 2. The token helper shows the account it found under **EA account**.
3. Start Titanfall 2 in shadPS4. Under **Where is Northstar running?**, select **In shadPS4 on this computer** (or **On a PS4, or in shadPS4 on another computer**, then type the address and code the game shows when you select Launch Northstar). Select **Sign in**.
4. **Keep the token helper open while you play.** Your sign-in lasts about a day. When it runs out, the game gets a new one from the helper by itself.

## Play

From the main menu, select **Launch Northstar**. The first start takes longer while shadPS4 builds its shader cache. You land in the Northstar lobby, and **Server Browser** lists Northstar's servers, as on PC.

To use more mods, add their folders to `<game folder>\R2Northstar\mods` and restart. Mods that servers require are downloaded when you join, as on PC. [USING.md](USING.md) covers which mods work, hosting your own matches, chat, controller menus and more.

## Update

1. Close the game.
2. Replace `<game folder>\bin\ps4_retail\northstar_ps4.prx` with the new release's.
3. Extract the new release's `northstar-ps4-mods-<version>.zip` (and `northstar-custom-ps4-rpaks-<version>.zip`, if the release has one) into the game folder again, replacing files.
4. Update the token helper if the release has a new one.

Don't run `Enable-Stage2Bootstrap.ps1` again. Your settings, enabled mods, sign-in and mod save data are in the data folder, and an update keeps them.

## Uninstall

To go back to the ordinary game:

```powershell
powershell -ExecutionPolicy Bypass -File .\Enable-Stage2Bootstrap.ps1 -GameRoot 'D:\PS4\CUSA04013' -Disable
```

This restores the original `eboot.bin` from `eboot.bin.northstar-stage2.bak`. Then delete `bin\ps4_retail\northstar_ps4.prx` and the `R2Northstar` folder. To also remove your Northstar settings and sign-in, delete the data folder. The game's own archives were never changed.

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
| `Enable-Stage2Bootstrap.ps1` says **Refusing to patch unknown eboot** | Your `eboot.bin` isn't the expected original (wrong game version, or already patched by something else). Restore the original from your backup. |

When reporting a problem, include shadPS4's log (`%APPDATA%\shadPS4\log\shad_log.txt`). With the log filter above it contains no sign-in token. Still, check it before posting, and never share `atlas_identity.json` from the data folder.

## What's not supported

- Real PS4 hardware: untested.
- Plugins (Windows DLLs): can't run on a PS4.
- Hosting dedicated servers on the PS4: you can host matches from your game, but not a dedicated server.
- Mods whose RPak files are compressed, patch, model or UI-image paks: not readable yet.
- Crowded, heavily modded servers can run a 6 GB graphics card out of memory in shadPS4.

The [release notes](https://github.com/taskinoz/PS4-Northstar/releases) list the known issues for each version.
