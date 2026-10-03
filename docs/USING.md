# Using PS4 Northstar

What works, and how to use it, once PS4 Northstar is [installed](INSTALL.md). PS4 Northstar aims to behave as PC Northstar does. This page covers what you use, and where the PS4 differs.

Paths below use two folders:
- the **game folder**, with the game's `eboot.bin`;
- the **data folder**, shadPS4's save data for PS4 Northstar: `%APPDATA%\shadPS4\data\northstar_ps4` on Windows. The game sees it as `/data/northstar_ps4`.

## Contents

- [Signing in](#signing-in)
- [Mods](#mods)
- [Controller menus](#controller-menus)
- [Joining servers](#joining-servers)
- [Hosting a match for other players](#hosting-a-match-for-other-players)
- [Banning players](#banning-players)
- [Text chat](#text-chat)
- [Launch options](#launch-options)
- [Where things are kept](#where-things-are-kept)

## Signing in

PC Northstar signs you in through the EA app. A PS4 has no EA app, so the **NorthstarPS4 Token Helper** on a computer does it for the game. It's on the [releases page](https://github.com/taskinoz/PS4-Northstar/releases):

| System | Download |
| --- | --- |
| Windows | `NorthstarPS4 Token Helper_<version>_x64-setup.exe` (installer), or `NorthstarPS4TokenHelper.exe` to run without installing |
| macOS (Apple Silicon and Intel) | `NorthstarPS4 Token Helper_<version>_universal.dmg` (untested with the Mac EA app) |
| Linux | `.AppImage` or `.deb` |

1. Open the EA app and sign in with the account that owns Titanfall 2. PC Northstar doesn't need to be running.
2. Open the token helper. It shows the EA account it found, or what to fix (with **Try again**). It isn't signed:
   - Windows may warn about it the first time: select **More info**, then **Run anyway**.
   - macOS refuses to open it the first time: open **System Settings → Privacy & Security**, select **Open Anyway** next to its name, and confirm.
3. Under **Where is Northstar running?**:
   - **In shadPS4 on this computer:** if the game is running, the helper finds it and signs it in by itself. Otherwise select **Sign in**, and the game picks up the sign-in when it starts.
   - **On a PS4, or in shadPS4 on another computer:** start the game and select **Launch Northstar**. The message shows the console's address and a 4-digit code, for example `enter 192.168.1.20 4821`. Type them into **Address** and **Code** and select **Sign in**. The game signs in at once, so select Launch Northstar again.
4. Keep the helper open while you play; minimising it is fine. A sign-in lasts about a day. When Northstar refuses the old one, the game asks the helper for a new one and carries on. Each time, a line appears under **Activity**, but never the token. Closing the helper stops it.

Next time, the helper remembers the console and signs it in without the code; just have the game running when you open the helper.

**Things to know:**
- **One sign-in at a time.** Each new sign-in ends the previous one for your account. Starting PC Northstar, or Northstar on any other machine, signs the game out, and it then asks the helper again. Using the helper ends a PC Northstar session the same way.
- **Pairing.** Signing in pairs the game with the helper: the game stores a key, and the helper only gives sign-ins to a game that sends it. The key is in `token-helper.json` on the computer (`%APPDATA%\NorthstarPS4` on Windows, `~/Library/Application Support/NorthstarPS4` on macOS) and in `atlas_identity.json` in the data folder.
- **Network.** The game and the helper talk over plain HTTP: the game listens on port 37012 and the helper on 37011. Use the helper on a network you trust. Your system may ask whether to let the token helper or shadPS4 use the network: allow it on private networks.
- **Keep `atlas_identity.json` private.** Anyone with it can sign in as you, and with its key they can ask your helper for new sign-ins. Never share or upload it.

### The token helper from a terminal

The same app takes options. With any of these it works in the terminal instead of opening its window (`--help` lists them all):

| Option | What it does |
| --- | --- |
| `--console "192.168.1.20 4821"` | Signs that console in. Use just the address for a console paired before. |
| `--local` | Signs in Northstar in shadPS4 on this computer, or, if it isn't running, saves the sign-in for when it starts. |
| `--cli` | Signs in a running game or the console paired last time; otherwise saves the sign-in for shadPS4 on this computer. |
| `--once` | Signs the game in and exits, without serving new sign-ins. |
| `--output <file>` | Where `atlas_identity.json` goes when the game isn't running here. Defaults to the shadPS4 data folder. |
| `--port <n>` / `--console-port <n>` | The helper's port (37011) and the game's (37012). |
| `--advertise-host <address>` | The address the game uses to reach this computer, if the one picked automatically is wrong (for example, with a VPN). |

In the terminal it keeps serving new sign-ins until Ctrl+C, and exits with 0 when signed in, 1 when signing in failed, and 2 for a bad option. On Windows the app is a window app, so the shell doesn't wait for it: use `start /wait NorthstarPS4TokenHelper.exe --once ...` in cmd, or `Start-Process -Wait`, when a script needs the exit code. On macOS the program is inside the app at `NorthstarPS4 Token Helper.app/Contents/MacOS/NorthstarPS4TokenHelper`.

### Signing in without the token helper

A running PC Northstar can hand over its sign-in instead. It isn't renewed, so you repeat this when it runs out (about a day, or as soon as PC Northstar signs in again). From a copy of this repository:

1. On the PC, start Northstar and wait for its log to say `Northstar origin authentication completed successfully`.
2. Run `scripts\Export-AtlasCredentials.ps1`. It reads the account id and sign-in out of the running client, checks the id against the client's own log, and writes `atlas_identity.json` into the data folder (`-Output` writes it elsewhere, `-WhatIf` shows what it would do).

### What the game reports

Launch Northstar says what's wrong, with the console's address and sign-in code, when it isn't signed in:

| State | Meaning |
| --- | --- |
| `PS4_AUTH_IMPORTED` | Signed in. |
| `PS4_AUTH_NO_IDENTITY` | Not signed in yet. Use the token helper. |
| `PS4_AUTH_NO_TOKEN` | `atlas_identity.json` has an account but no sign-in. Sign in again with the token helper. |
| `PS4_AUTH_BAD_IDENTITY` | `atlas_identity.json` can't be read. Sign in again with the token helper. |

If Northstar refuses the sign-in and the helper can't replace it, joining a server says why: for example, the helper isn't running, or the EA app is signed out. The lobby still opens without a working sign-in, but with a fresh local profile, and nothing from that session is saved to your account.

## Mods

Mods go in `<game folder>\R2Northstar\mods`, one folder each, exactly as on PC; restart the game after adding or removing one. **Mods** in the main menu turns them on and off, live, as on PC. Your choices are saved in `enabledmods.json` in the data folder, which takes precedence over the one in the game folder.

**Works, as on PC:**
- scripts and their `RunOn` conditions, mod console variables (Mod Settings) and console commands;
- localisation, following the PS4 system language with an English fallback;
- KeyValues (weapons, playlists, AI settings) and custom datatables;
- mod VPKs (models and textures inside), custom maps, particle manifests;
- sound replacements (`audio/` folders); `ns_print_played_sounds 1` in the console logs the sound events that play, to find their names;
- `Dependencies` constants for optional mods;
- loose materials and textures (`.vmt`, `.vtf`) in their normal PC format;
- web requests (see [Launch options](#launch-options));
- per-mod save data (Northstar's Safe I/O), with PC's rules: `.txt` and `.json` files only, up to 50 MB per mod.

**Needs converting:**
- RPak texture and material packs (`paks/*.rpak`, `*.starpak`). PC-format paks can't be read on the PS4, and the game refuses them instead of crashing. Northstar.Custom's come converted in the release. For other mods, a copy of this repository converts them: `scripts\Sync-NorthstarProfile.ps1 -ConvertRpaksForPs4`, or `New-NorthstarProfile.ps1` with the same switch (see [BUILDING.md](BUILDING.md)). Converted packs tested so far: the smooshie CAR UwU and Volt UwU weapon skins, and S2Mods' Resonance Rifle.

**Doesn't work yet:**
- compressed or patch RPaks, and paks with models or UI images;
- plugins (Windows DLLs).

Other tested mods include Rwyn's Kraber reload sound pack, S2.SpeedometerV2 (a HUD with Mod Settings), the `bobthebob.mp_box` custom map and Moblin.Archon's particles.

**Downloaded mods.** Joining a server downloads the mods it needs when they're on Northstar's [verified list](https://github.com/R2Northstar/VerifiedMods) and `allow_mod_auto_download` is on, as on PC. They're checked against the list, saved under `runtime/remote/mods` in the data folder, and switched on only for servers that need them.

**Broken mods.** If a mod's script fails to compile, the game returns to the menus with a message naming the mod, instead of freezing. Disable it in **Mods**, or install what it depends on. A mod switched off with a live reload keeps its text loaded until the game restarts.

## Controller menus

Northstar's menus work with a pad:
- **Mods and Mod Settings:** the d-pad moves between entries, and L1/R1 page through long lists.
- **Text boxes:** Cross on a text box (a setting's value, or Search) opens the system keyboard. A Mod Settings value applies when you move off the box, as on PC.
- **Private Match → Settings:** choosing a number setting (score limit, time limit…) opens the keyboard straight away.
- **Server passwords:** when a server asks for one, Cross on the box opens the keyboard; after **Done**, press down to reach **Connect**.

## Joining servers

**Server Browser** in the Northstar lobby lists Northstar's servers, PC and PS4 alike, with the same filters as on PC. Joining uses your Northstar sign-in, so servers see your account and you play with your own progress. Password-protected servers ask for the password. Leaving a match returns you to the lobby.

## Hosting a match for other players

A private match you host is listed in Northstar's server browser, as a PC-hosted one is (while its lobby is open). PC and PS4 players can join it.

**Host Options.** In the private lobby, **L1** opens Host Options:
- **Server browser:**
  - **Listed** (`ns_report_server_to_masterserver`, on by default): whether the lobby appears in the list. Switching it off removes it straight away.
  - **Name** and **Description** (`ns_server_name`, `ns_server_desc`), typed on the system keyboard. The list shows a change within a few seconds.
  - **Password** (`ns_server_password`): players joining from the browser are asked for it. The keyboard starts empty: **Done** with nothing typed removes the password, and cancelling keeps the current one. Northstar only takes a password when a server is added to the list, so a new password takes the lobby off the list and adds it again.
- **Other players** (`ns_auth_allow_insecure`, off by default as on PC servers). Players who join through the server browser are signed in by Northstar and always let in. Anyone else (connecting by address, for example) is removed while this is off.
- **Same account on several machines** (`ns_allow_duplicate_accounts`, PC's `-allowdupeaccounts`). Two machines with the same Northstar account are otherwise refused with "Player's account is already on the server", as on PC.

Host Options are saved in `host_options.txt` in the data folder. [Launch options](#launch-options) are applied afterwards, so they win.

**Your network.** Other players must be able to reach your computer:
- UDP port **37015** must be forwarded on your router to the computer running shadPS4.
- If your internet provider uses carrier-grade NAT (your router's internet address starts with `100.64`–`100.127`), port forwarding can't work; ask the provider for a public address.

Northstar checks the port when the match starts, and doesn't list a match it can't reach.

**Progress.** Players who join through the server browser play with their own Northstar progress, and what they earn is saved to their account when a match ends or they leave, as on a PC server. You play with yours too. To host without saving players' progress, add `+ns_ps4_write_remote_persistence 0` to the launch options.

## Banning players

From the console, as on a PC server:
- `ban <name or uid>` bans a connected player and disconnects them;
- `unban <uid>` lifts a ban;
- `clearbanlist` empties the list.

The list is `banlist.txt` in the data folder (PC keeps it in `R2Northstar`): one account id per line, `#` for comments. Edits apply from the next connection. A banned player is refused with "Banned From Server.", whether they connect directly or through the server browser.

## Text chat

In a match, open the in-game menu and press **L2** to chat with everyone or **R2** to chat with your team; the private lobby has the same buttons. The system keyboard opens, and its **Send** button posts the message. Messages show at the bottom left, as on PC. The `say` and `say_team` console commands work too. Some characters show as boxes because the game's font doesn't contain them.

## Launch options

PC Northstar reads extra launch options from `ns_startup_args.txt`. PS4 Northstar reads that file from the game folder (beside `eboot.bin`) or from the data folder; the data folder's wins. Options are separated by spaces, with quotes around values that contain spaces. `+name value` sets a console variable at startup, for example `+ns_server_name "My PS4 server"`.

| Option | What it does |
| --- | --- |
| `+ns_server_name "…"`, `+ns_server_desc "…"`, `+ns_server_password …` | Your hosted match's name, description and password. These override Host Options. |
| `+ns_report_server_to_masterserver 0` | Don't list your hosted match. |
| `+ns_ps4_write_remote_persistence 0` | Host without saving other players' progress. |
| `-allowdupeaccounts` | Let the same account join from several machines. |
| `-disablehttprequests` | Turn off mods' web requests. |
| `-allowlocalhttp` | Let mods reach addresses on your own network, which are refused by default. |
| `-disablehttpssl` | Skip the certificate host-name check for mods' web requests. |
| `-maxfoldersize <bytes>` | Per-mod save data limit (default 50 MB). |
| `-allowunsafesqfuncs` | Let servers and mods use script functions PC Northstar restricts. |
| `-nopcsignin` | Turn off the game's listener for the token helper (port 37012). |
| `+ns_masterserver_hostname <url>` | Use another Northstar master server (default `https://northstar.tf`). Read once per session. |

## Where things are kept

In the data folder (`/data/northstar_ps4`):

| File | What it is |
| --- | --- |
| `atlas_identity.json` | Your Northstar sign-in. **Private**: never share it. |
| `enabledmods.json` | Which mods are on. |
| `host_options.txt` | Host Options. |
| `banlist.txt` | Banned account ids. |
| `ns_startup_args.txt` | Launch options (optional). |
| `save_data/<mod>/` | Mods' own save data. |
| `runtime/remote/mods/` | Mods downloaded from servers. |
