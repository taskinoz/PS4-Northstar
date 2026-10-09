# Installing PS4 Northstar

PS4 Northstar brings [Northstar](https://northstar.tf), the Titanfall 2 multiplayer mod
platform, to the PS4 version of the game: Northstar's server browser, custom servers and mods,
with your Northstar progress, on the same servers PC players use.

It runs in two places:
- **[In shadPS4](#in-shadps4)**, the PS4 emulator, on a Windows computer. The token helper
  installs it for you.
- **[On a PS4](#on-a-ps4)** with GoldHEN, as a GoldHEN plugin. Tested on a standard PS4 on
  firmware 9.00.

Either way, you sign in with the **[token helper](#the-token-helper)** on a computer that has
the EA app. [PC players](#pc-players) join PS4 and shadPS4 games with ordinary PC Northstar.

To build from source, see [BUILDING.md](BUILDING.md). For everything you can do once it's
installed, see [USING.md](USING.md).

## Contents

- [The token helper](#the-token-helper)
- [In shadPS4](#in-shadps4)
- [On a PS4](#on-a-ps4)
- [PC players](#pc-players)
- [What's not supported](#whats-not-supported)

## The game

Both ways need **Titanfall 2 for PS4**, `CUSA04013`, at the final patch (1.13, build
`R2PS4_r2dlc11_598_CL297590_2017_12_05_12_36_PM`), from your own copy. Other regions' versions
are not supported. You also need **an EA account that owns Titanfall 2**, which is how Northstar
knows who you are.

## The token helper

A PS4 has no EA app, so the **NorthstarPS4 Token Helper** signs the game in to Northstar from a
computer, and keeps it signed in. In shadPS4 it also installs and updates PS4 Northstar.

**Download** it from the [releases page](https://github.com/taskinoz/PS4-Northstar/releases):

| System | File |
| --- | --- |
| Windows 10 or 11 | `NorthstarPS4TokenHelper-<version>-windows-setup.exe` (installer), or `-windows.exe` to run without installing |
| macOS (Apple Silicon and Intel) | `NorthstarPS4TokenHelper-<version>-macos-universal.dmg`. Untested with the Mac EA app. |
| Linux (x86-64) | `NorthstarPS4TokenHelper-<version>-linux-x86_64.AppImage` (run `chmod +x` on it first), or `-linux-amd64.deb` |

**First start.** The app isn't signed:
- **Windows:** select **More info**, then **Run anyway**.
- **macOS:** open **System Settings → Privacy & Security**, select **Open Anyway** next to its
  name, and confirm.
- **Linux:** make the AppImage executable, or install the `.deb`.

**It needs** the **EA app**, signed in with the account that owns Titanfall 2, on the same
computer. PC Northstar doesn't need to be running. The computer must be on the same network as
the PS4, if you play on one. Allow the token helper on private networks if your system asks.

**Keep it open while you play.** A sign-in lasts about a day, and signing in to Northstar
anywhere else with the same EA account (for example PC Northstar) replaces it. Either way the
game asks the token helper for a new one by itself, so it has to be running.

## In shadPS4

### What you need

- The [game](#the-game), dumped from your own copy. Its archives (`vpk_ps4/`) must be the
  original, unmodified ones.
- **shadPS4** on Windows, set up to run that game folder. See [Which shadPS4
  build](#which-shadps4-build).
- The [token helper](#the-token-helper) on the same computer.

### Which shadPS4 build

| shadPS4 build | Result |
| --- | --- |
| Nightly **`2b5666b3`** | **Recommended.** The build PS4 Northstar is documented against. |
| Prerelease `94e21778`, nightly `4cbd23ef` | Work well in testing. |
| `c6fa48c7` up to `f6cd16e8` | Levels draw black apart from the HUD ([shadPS4#5124](https://github.com/shadps4-emu/shadPS4/issues/5124)). Avoid. |
| Before `ca89b01` (including v0.18.0) | Crash whenever you leave a loaded map. Avoid. |

`2b5666b3` is in shadPS4's CI run
[36164450310](https://github.com/shadps4-emu/shadPS4/actions/runs/36164450310) (downloading it
needs a GitHub login; CI downloads expire after about 90 days). In the shadPS4 Qt launcher, put
`shadPS4.exe` in its own folder under `%APPDATA%\shadPS4QtLauncher\versions` and add it to
`versions.json`, or run it directly.

Per-game settings for `CUSA04013`:

| Setting | Value | Why |
| --- | --- | --- |
| `pipeline_cache_enabled` | `true` | Avoids shader-compile stalls that time out online matches. |
| `copy_gpu_buffers` | `false` | The tested setting. |
| Log filter | `*:Info Lib.Http:Warning` | Otherwise shadPS4 writes your sign-in token, and your server password when hosting, into its log. |
| `Log.append` | `true` | Keeps the log of a crash when the game is started again. |

Shader caches aren't shared between shadPS4 builds: after switching builds, move
`%APPDATA%\shadPS4\cache\CUSA04013` aside.

shadPS4 needs plenty of free memory to start (about 9 GB of free commit). If it fails with
"Insufficient system resources", close other programs.

### Install

The **game folder** is the folder with the game's `eboot.bin` and `vpk_ps4`.

1. Open the token helper and select the **Install** tab.
2. Select **Choose…** and pick the game folder. The helper checks it: "Titanfall 2 found. PS4
   Northstar isn't installed yet."
3. Select **Install**. The helper downloads the newest PS4 Northstar release and Northstar's
   mods (about 130 MB), checks every download, and installs them. Nothing in the game folder
   changes until every download has arrived and passed its check.

What it changes in the game folder:
- **`R2Northstar\mods`:** Northstar's mods, PS4 Northstar's mods (`Northstar.PS4`,
  `Northstar.DirectConnect`), and Northstar.Custom's textures converted for the PS4. Other mods
  already there are left alone.
- **`bin\ps4_retail\northstar_ps4.prx`:** the runtime.
- **`eboot.bin`:** a few bytes are added so the game loads the runtime. This only works on the
  exact original `eboot.bin`, and the original is kept as `eboot.bin.northstar-stage2.bak`.

The game's archives are never changed.

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
  vpk_ps4/                            (unchanged)
```

### Sign in

1. Start Titanfall 2 in shadPS4.
2. In the token helper's **Sign in** tab, check **EA account** shows your account. Under
   **Where is Northstar running?**, select **In shadPS4 on this computer**, then **Sign in**. If
   the game is running, the helper finds it and signs it in by itself.
3. In the game, select **Launch Northstar**. You land in the Northstar lobby, and **Server
   Browser** lists Northstar's servers. The first start takes longer while shadPS4 builds its
   shader cache.

To play in shadPS4 on **another computer** than the token helper's, select **On a PS4, or in
shadPS4 on another computer** instead, and enter the address and code the game shows, as for a
[PS4](#sign-in-on-a-ps4).

### Update

When a new release is out, the token helper's **Install** tab offers **Update to
<version>**. Your settings, enabled mods, sign-in and mod save data are kept. **Check for
updates** looks again. New versions of the token helper itself are on the releases page.

### Uninstall

On the **Install** tab, open **Uninstall** and select **Uninstall PS4 Northstar**. It puts back
the original `eboot.bin` and removes the runtime. Tick **Also delete the R2Northstar folder and
every mod in it** to remove the mods too. Your Northstar settings and sign-in are in shadPS4's
data folder (`%APPDATA%\shadPS4\data\northstar_ps4`); delete it to remove them as well.

### Troubleshooting

| What you see | What to do |
| --- | --- |
| Launch Northstar says **Not signed in to Northstar** | Sign in with the token helper ([Sign in](#sign-in)). |
| Joining fails with **the token helper could not be reached** | Start the token helper, then join again. |
| Levels are black apart from the HUD | Your shadPS4 build has the black-level bug. See [Which shadPS4 build](#which-shadps4-build). |
| The game crashes when a match ends or you change maps | Your shadPS4 build is older than `ca89b01`. Update shadPS4. |
| A message names a mod with a **script compilation error** | That mod is broken or missing a mod it depends on. Disable it in **Mods**, or install what it needs. |
| Textures show as a magenta-and-black checkerboard | A mod's paks are in the PC format. See [Mods](USING.md#mods). |
| Boot stops on the Respawn logo | Close the game and start it again. If it keeps happening, remove recently added mods to find the cause. |
| shadPS4 stops at start with a cache error | Move `%APPDATA%\shadPS4\cache\CUSA04013` aside: it was made by another shadPS4 build. |
| Others can't join a match you host | Forward UDP port 37015 to your computer. See [Hosting](USING.md#hosting-a-match-for-other-players). |
| The token helper says the folder's **eboot.bin isn't the supported Titanfall 2 version** | It isn't the original `eboot.bin` of the final patch. Restore the original from your own copy. |

To report a problem, include shadPS4's log (`%APPDATA%\shadPS4\log\shad_log.txt`). With the log
filter above it contains no sign-in token; check it before posting anyway, and never share
`atlas_identity.json`.

## On a PS4

On a PS4, PS4 Northstar runs as a [GoldHEN](https://github.com/GoldHEN/GoldHEN) plugin. The game
stays installed as normal and nothing in it is changed: GoldHEN loads the runtime when Titanfall 2
starts, and the mods live in the console's storage.

### What you need

- **A jailbroken PS4 running GoldHEN 2.3 or newer.** Tested on a standard PS4 (not Slim or Pro)
  on firmware 9.00; other models and firmware versions that GoldHEN supports are untested. In
  GoldHEN's settings, turn on:
  - **Plugins** (the plugin loader);
  - **FTP server**, port **2121**. The console's address is under **Settings → Network → View
    Connection Status**.
- The [game](#the-game), installed normally from the disc or PlayStation Store and updated to
  1.13.
- From the [releases page](https://github.com/taskinoz/PS4-Northstar/releases), a release after
  v1.0.0-rc1:
  - `northstar_ps4.prx`;
  - `northstar-mods-1.31.13.zip`, `northstar-ps4-mods-<version>.zip` and
    `northstar-custom-ps4-rpaks-<version>.zip`.
- An **FTP client** on your computer, such as [FileZilla](https://filezilla-project.org).
- The [token helper](#the-token-helper), on a computer on the same network as the PS4.

### Install

1. **Put the mods together.** Make an empty folder and extract the three zips into it, in this
   order, letting each replace files from the one before:
   1. `northstar-mods-1.31.13.zip`
   2. `northstar-ps4-mods-<version>.zip`
   3. `northstar-custom-ps4-rpaks-<version>.zip`

   You now have an `R2Northstar\mods` folder with `Northstar.Client`, `Northstar.Custom`,
   `Northstar.CustomServers`, `Northstar.PS4` and `Northstar.DirectConnect` in it.
2. **Connect to the PS4** with the FTP client: the console's address, port `2121`, no user name
   or password.
3. **Upload the mods.** Create the folder `/data/northstar_ps4` and upload `R2Northstar` into
   it, so the mods end up in `/data/northstar_ps4/R2Northstar/mods`. It's about 100 MB.
4. **Upload the runtime** to `/data/GoldHEN/plugins/northstar_ps4.prx`.
5. **Turn the plugin on for Titanfall 2.** Download `/data/GoldHEN/plugins.ini`, add these two
   lines at the end, keeping what's there, and upload it back:

   ```ini
   [CUSA04013]
   /data/GoldHEN/plugins/northstar_ps4.prx
   ```

6. **Start Titanfall 2.** The first start takes a little longer: the runtime copies
   Northstar.Custom's archive once to `/data/northstar_ps4/runtime/vpk`, because the PS4's
   storage is case-sensitive.

```text
/data/GoldHEN/
  plugins.ini                         ([CUSA04013] added)
  plugins/northstar_ps4.prx           the runtime
/data/northstar_ps4/
  R2Northstar/mods/                   Northstar's mods, Northstar.PS4, your mods
  runtime/                            made by the runtime
```

### Sign in on a PS4

1. On your computer, open the EA app and the token helper, and select the **Sign in** tab.
2. On the PS4, select **Launch Northstar**. The message shows the console's address and a
   4-digit code, for example `enter 192.168.1.20 4821`.
3. In the token helper, select **On a PS4, or in shadPS4 on another computer**, type the address
   and code, and select **Sign in**. Select **Launch Northstar** again: you land in the
   Northstar lobby.

The code is only needed the first time. The console and that computer are then paired, and the
token helper hides the Code box for this console.

### Update

Upload the new release's `northstar_ps4.prx` over the old one, and its mod zips as in steps 1 to
3 of [Install](#install-1), replacing files. Your settings, enabled mods, sign-in and mod save
data are in `/data/northstar_ps4` and are kept.

### Uninstall

Remove the `[CUSA04013]` lines from `/data/GoldHEN/plugins.ini`, and the game starts as normal.
To remove everything, also delete `/data/GoldHEN/plugins/northstar_ps4.prx` and
`/data/northstar_ps4` (that deletes your sign-in and settings too).

### Troubleshooting

| What you see | What to do |
| --- | --- |
| Titanfall 2 starts as normal, with no Launch Northstar | Check that GoldHEN's plugin loader is on, `plugins.ini` has the `[CUSA04013]` lines, and the runtime is at `/data/GoldHEN/plugins/northstar_ps4.prx`. After restarting the PS4, run GoldHEN again before starting the game. |
| The game crashes straight away | Check that the runtime is from a release after v1.0.0-rc1, and that the game is at patch 1.13. |
| The Mods list is empty | The mods aren't in `/data/northstar_ps4/R2Northstar/mods`. Folder names are case-sensitive. |
| Launch Northstar says **Your Northstar sign-in has run out, or was replaced** | Open the token helper on your computer, then select Launch Northstar again. |
| You're disconnected with **Resetting invalid loadout** | You have Northstar's progression turned on and a loadout uses items your account hasn't unlocked. The item goes back to its default, as on PC. |
| The PS4 seems frozen after a crash | It is saving a crash report. Give it a minute before restarting. |

A PS4 has less memory to spare than shadPS4, so very large mods may not fit. If a mod stops the
game from starting, remove it from `/data/northstar_ps4/R2Northstar/mods`.

To report a problem, GoldHEN's kernel log (TCP port 3232 on the console) has the runtime's
`[NorthstarPS4]` lines. Share only those lines, and check them first.

## PC players

PC players need nothing extra. With [PC Northstar](https://northstar.tf) installed as usual:
- **Join a PS4 or shadPS4 player's match** from Northstar's **Server Browser**. Matches hosted
  on PS4 Northstar are listed like any other, while their lobby is open, and progress is saved to
  the PC player's account as on any Northstar server.
- **Play together on any Northstar server**: PS4, shadPS4 and PC players share the same
  servers.

The host must be reachable for others to join: UDP port 37015 forwarded to the computer or PS4
that hosts. See [Hosting](USING.md#hosting-a-match-for-other-players).

One Northstar sign-in exists per EA account at a time. A player who uses PC Northstar and PS4
Northstar with the same account signs the other one out; with the token helper running, the
game signs in again by itself.

## What's not supported

- A PS4 without GoldHEN, and Titanfall 2 versions other than `CUSA04013` at patch 1.13.
- Installing on a PS4 with the token helper's Install tab: it installs into a shadPS4 game
  folder only.
- Plugins (Windows DLLs).
- Dedicated servers on a PS4 or shadPS4: you can host matches from your game, but not a
  dedicated server.
- Compressed or patch RPaks.
- Crowded, heavily modded servers can run a 6 GB graphics card out of memory in shadPS4.

The [release notes](https://github.com/taskinoz/PS4-Northstar/releases) list the known issues
for each version.
