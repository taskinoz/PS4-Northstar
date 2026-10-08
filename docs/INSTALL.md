# Installing PS4 Northstar

PS4 Northstar brings [Northstar](https://northstar.tf), the Titanfall 2 multiplayer mod platform, to the PS4 version of the game. You get Northstar's server browser, custom servers and mods, with your Northstar progress, on the same servers PC players use.

There are two ways to run it:
- **In the [shadPS4](https://shadps4.net) emulator** on a computer. This is the most tested way, and the token helper installs it for you. Start at [What you need](#what-you-need).
- **On a PS4 with GoldHEN**, as a GoldHEN plugin. This is newer and has been tested on one console. See [On a PS4](#on-a-ps4).

This guide is for players. To build from source, see [BUILDING.md](BUILDING.md); for everything you can do once it's installed, see [USING.md](USING.md).

> **Status.** Tested most in shadPS4. On a real PS4, the menus, signing in, the server browser, joining and hosting (with a PC player joining) have been tested on one console.

## What you need

- **Titanfall 2 for PS4**, dumped from your own copy, at the final patch: `CUSA04013`, build `R2PS4_r2dlc11_598_CL297590_2017_12_05_12_36_PM`. The game's archives (`vpk_ps4/`) must be the original, unmodified ones.
- **shadPS4**, set up to run that game folder. See [Which shadPS4 build](#which-shadps4-build) below.
- **An EA account that owns Titanfall 2**, signed in to the **EA app** on a Windows PC (or a Mac, untested). Northstar signs you in through it.
- The **NorthstarPS4 Token Helper** for your computer, from the [PS4 Northstar releases page](https://github.com/taskinoz/PS4-Northstar/releases): `NorthstarPS4TokenHelper-<version>-windows-setup.exe` (or `-windows.exe` to run without installing), `-macos-universal.dmg`, or `-linux-x86_64.AppImage` (`chmod +x` it first) or `-linux-amd64.deb`. It installs PS4 Northstar into the game folder, keeps it up to date, and signs you in.

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

## On a PS4

On a PS4, PS4 Northstar runs as a [GoldHEN](https://github.com/GoldHEN/GoldHEN) plugin. The game stays installed as normal and nothing in it is changed: GoldHEN loads the runtime when Titanfall 2 starts, and the mods live in the console's storage.

### What you need on a PS4

- **A jailbroken PS4 running GoldHEN 2.3 or newer**, with these turned on in GoldHEN's settings:
  - **Plugins** (the plugin loader);
  - **FTP server**, on port **2121**. The console's address is under **Settings → Network → View Connection Status** (IP Address).
- **Titanfall 2 `CUSA04013`, updated to the final patch** (version 1.13, build `R2PS4_r2dlc11_598_CL297590_2017_12_05_12_36_PM`), installed normally from the disc or PlayStation Store. Other regions' versions have not been tested.
- From the [releases page](https://github.com/taskinoz/PS4-Northstar/releases), in a release from after v1.0.0-rc1 (rc1's runtime can't run as a plugin):
  - `northstar_ps4.prx`;
  - `northstar-mods-1.31.13.zip`, `northstar-ps4-mods-<version>.zip` and `northstar-custom-ps4-rpaks-<version>.zip`;
  - the **NorthstarPS4 Token Helper** for your computer, for signing in (see [What you need](#what-you-need)).
- An **FTP client** on your computer, such as [FileZilla](https://filezilla-project.org).
- **An EA account that owns Titanfall 2**, signed in to the **EA app** on a computer on the same network as the PS4.

### Install on a PS4

1. **Put the mods together.** On your computer, make an empty folder and extract the three zips into it, in this order, letting each replace files from the one before:
   1. `northstar-mods-1.31.13.zip`
   2. `northstar-ps4-mods-<version>.zip`
   3. `northstar-custom-ps4-rpaks-<version>.zip`

   You now have an `R2Northstar\mods` folder with `Northstar.Client`, `Northstar.Custom`, `Northstar.CustomServers`, `Northstar.PS4` and `Northstar.DirectConnect` in it.
2. **Connect to the PS4** with the FTP client: the console's address, port `2121`, no user name or password.
3. **Upload the mods.** Create the folder `/data/northstar_ps4`, and upload the `R2Northstar` folder into it, so that the mods end up in `/data/northstar_ps4/R2Northstar/mods`. It's about 100 MB.
4. **Upload the runtime** to `/data/GoldHEN/plugins/northstar_ps4.prx`.
5. **Turn the plugin on for Titanfall 2.** Download `/data/GoldHEN/plugins.ini`, add these two lines at the end, keeping what's already there, and upload it back:

   ```ini
   [CUSA04013]
   /data/GoldHEN/plugins/northstar_ps4.prx
   ```

6. **Start Titanfall 2.** The first start after installing takes a little longer: the runtime copies Northstar.Custom's archive to a folder of its own once (`/data/northstar_ps4/runtime/vpk`), because the PS4's storage is case-sensitive and the game looks for it in lowercase.

When you're done, the console has:

```text
/data/GoldHEN/
  plugins.ini                         ([CUSA04013] added)
  plugins/northstar_ps4.prx           the runtime
/data/northstar_ps4/
  R2Northstar/mods/                   Northstar's mods, Northstar.PS4, your mods
  runtime/                            made by the runtime
```

### Sign in on a PS4

1. On your computer, open the **EA app** and the **token helper**, and select the token helper's **Sign in** tab.
2. On the PS4, select **Launch Northstar**. The message shows the console's address and a 4-digit code, for example `enter 192.168.1.20 4821`.
3. In the token helper, select **On a PS4, or in shadPS4 on another computer**, type the address and code, and select **Sign in**. Select **Launch Northstar** again: you land in the Northstar lobby.
4. The code is only needed the first time: the console and that computer are now paired, and the token helper hides the Code box for this console.
5. **Keep the token helper open while you play.** Your sign-in lasts about a day; when it runs out, the game asks the token helper for a new one by itself. Signing in to Northstar somewhere else with the same EA account (for example on a PC) also replaces the PS4's sign-in. If the token helper isn't open then, Launch Northstar says so and asks you to start it.

### Update on a PS4

Upload the new release's `northstar_ps4.prx` over the old one, and the new mod zips as in step 1 to 3 of [Install on a PS4](#install-on-a-ps4), replacing files. Your settings, enabled mods, sign-in and mod save data are in `/data/northstar_ps4` and are kept.

### Uninstall on a PS4

Remove the `[CUSA04013]` lines from `/data/GoldHEN/plugins.ini`: the game starts as normal again. To remove everything, also delete `/data/GoldHEN/plugins/northstar_ps4.prx` and the `/data/northstar_ps4` folder (that also deletes your sign-in and settings).

### Troubleshooting on a PS4

| What you see | What to do |
| --- | --- |
| Titanfall 2 starts as normal, with no Launch Northstar | Check that GoldHEN's plugin loader is on, that `plugins.ini` has the `[CUSA04013]` lines, and that the runtime is at `/data/GoldHEN/plugins/northstar_ps4.prx`. GoldHEN has to be running: after a restart of the PS4, run it again before starting the game. |
| The game crashes straight away | Check that the runtime is from a release after v1.0.0-rc1, and that the game has the final patch. |
| The Mods list is empty | The mods aren't in `/data/northstar_ps4/R2Northstar/mods`. Check the folder names: the PS4's storage is case-sensitive. |
| Launch Northstar says **Your Northstar sign-in has run out, or was replaced** | Open the token helper on your computer and select Launch Northstar again. |
| You're disconnected with **Resetting invalid loadout** | You have Northstar's progression turned on, and a loadout uses items your account hasn't unlocked yet. The item is put back to its default, as on PC. |
| The PS4 seems frozen after a crash | It is saving a crash report. Give it a minute before restarting. |

Large mods have less room on a PS4 than in shadPS4: the game leaves little memory spare. If a mod stops the game from starting, remove it from `/data/northstar_ps4/R2Northstar/mods`.

To report a problem, GoldHEN's kernel log (TCP port 3232 on the console) has the runtime's `[NorthstarPS4]` lines. Share only those lines, and check them first.

## What's not supported

- Real PS4 hardware without GoldHEN, and Titanfall 2 versions other than `CUSA04013` at the final patch.
- Installing on a PS4 with the token helper's Install tab: it installs into a shadPS4 game folder only. Install on a PS4 by hand, as above.
- Plugins (Windows DLLs): can't run on a PS4.
- Hosting dedicated servers on the PS4: you can host matches from your game, but not a dedicated server.
- Mods whose RPak files are compressed, patch, model or UI-image paks: not readable yet.
- Crowded, heavily modded servers can run a 6 GB graphics card out of memory in shadPS4.

The [release notes](https://github.com/taskinoz/PS4-Northstar/releases) list the known issues for each version.
