# AI.Harness

AI.Harness is a development mod for driving the game from a script. It runs commands on the UI script thread through the game's `ClientCommand` and replies through a mailbox in the data folder. It needs the runtime's `NSAIHarness*` natives, which only AI.Harness can call. It works in shadPS4; the mailbox is a host folder.

## Installation

1. Build and deploy the runtime ([BUILDING.md](BUILDING.md#the-runtime)).
2. Copy `mods/AI.Harness` into the game's `R2Northstar/mods/AI.Harness` and ensure `AI.Harness` is enabled in `enabledmods.json` if explicitly listed there.
3. Run the host helper below. It creates the mailbox in `data\northstar_ps4\ai_harness` under shadPS4's user folder (`SHADPS4_USER_DIR`, default `%APPDATA%\shadPS4`); `-Mailbox` names another.

`New-NorthstarProfile.ps1` leaves the harness out unless given `-IncludeAIHarness`. Disable or remove the mod when it is not needed.

## Commands

From the repository in PowerShell:

```powershell
# Queue once before starting shadPS4; consumed automatically after UI startup.
./scripts/Send-AIHarnessCommand.ps1 -Action launch -QueueOnly

# While the game is running: wait for a correlated reply.
./scripts/Send-AIHarnessCommand.ps1 -Action status
./scripts/Send-AIHarnessCommand.ps1 -Action console -Command 'disconnect'
./scripts/Send-AIHarnessCommand.ps1 -Action menu -Menu 'MainMenu'
./scripts/Send-AIHarnessCommand.ps1 -Action back
./scripts/Send-AIHarnessCommand.ps1 -Action json -Command '{"a":1,"b":[true,"x"]}'
./scripts/Send-AIHarnessCommand.ps1 -Action cvar -Command 'stream_memory'
./scripts/Send-AIHarnessCommand.ps1 -Action playlistvar -Command 'tdm|scorelimit|fallback'
./scripts/Send-AIHarnessCommand.ps1 -Action datatable
./scripts/Send-AIHarnessCommand.ps1 -Action datatablevector # requires the development vector fixture
./scripts/Send-AIHarnessCommand.ps1 -Action localize -Command '#A_BUTTON_SELECT'
./scripts/Send-AIHarnessCommand.ps1 -Action http -Command 'https://example.com/' -TimeoutSeconds 90
./scripts/Send-AIHarnessCommand.ps1 -Action httpretire -Command 'http://127.0.0.1:18765/' -QueueOnly
# Mod auto-download without a server: fetch the verified list, download and install
# (Northstar's own dialogs), enable, ReloadMods. Replies with the mod's state.
./scripts/Send-AIHarnessCommand.ps1 -Action download -Command 'lexi.lexire125|1.0.7' -TimeoutSeconds 180
# The server browser's join for the first listed server whose name contains the text:
# auth, download missing verified mods, switch client-required mods, ReloadMods, connect.
./scripts/Send-AIHarnessCommand.ps1 -Action join -Command 'VAMP D' -TimeoutSeconds 180

# Send the contents of console.txt in the data folder as console commands (the file is kept).
./scripts/Send-AIHarnessCommand.ps1 -FromConsoleFile
```

`launch` follows Northstar's local authentication sequence, completes local authentication, then queues `setplaylist tdm` and `map mp_lobby`. Authentication failure is returned as an error. A successful reply says **queued**, not that the map loaded successfully. Check the fresh session log for script errors and actual lobby readiness. `status` confirms the UI polling thread is responding and now reports `connected`, `lobby`, `level`, `playlist` and `privateMatch` from game state (the last two only while connected). `cvar` returns a convar's value, `playlistvar` reads an effective playlist variable from `<playlist>|<name>|<fallback>`, `datatable` checks disk-CSV row count, column lookup, row search and scalar accessors, `datatablevector` checks vector access and matching when its development fixture is installed, and `localize` resolves a token; each result is in the reply's `json` field. A reply does not show that every callback succeeded; check the session's log for script errors.

`json` passes `-Command` through `DecodeJSON(text, true)` and `EncodeJSON` and returns the result in the reply's `json` field, to test the JSON natives in the UI VM. Member order follows the Squirrel table, not the input. `http` starts `NSHttpGet`, waits for its deferred success or failure callback, and returns `success|status|bodyLength` or `failure|code|message`.

`httpretire` tests that a request outliving its VM is dropped. Point it at a deliberately
delayed endpoint and use `-QueueOnly`: it starts the request, runs the normal `ReloadMods()`
path and resets the UI VM. When the endpoint eventually responds, the runtime must log
`dropped async result for retired context=2` and must not run the callback in the replacement
VM. Local endpoints also require `-allowlocalhttp` in `ns_startup_args.txt`.

`download` and `join` run Northstar's own download helpers (`FetchVerifiedModsManifesto`, `DownloadMod` and the error dialog) and its `ReloadMods()`, so the dialogs appear as they would from the server browser. Both reply before the queued `uiscript_reset` runs, and the harness is ready again once the UI VM rebuilds. `join` needs a current Atlas identity and refuses password-protected servers. The reply says **queued**: check the log for the connect's outcome.

Two helpers work alongside the harness while the game runs:

```powershell
# Presses pad buttons through shadPS4's keyboard mapping (input_config/default.ini),
# by posting key messages to the game window, so it works without taking focus.
./scripts/Send-PadInput.ps1 -Key down -Times 3   # up/down/left/right/l1/r1/l2/r2/options/cross/circle
# Saves a PNG of just the game window (PrintWindow, works while covered).
./scripts/Capture-GameWindow.ps1 -Out shot.png
```

`menu` opens a registered menu and `back` closes the active menu. These are script operations, not physical button clicks. Pad input goes through `Send-PadInput.ps1` above, which depends on the default keyboard mapping.

## Transport and recovery

The helper publishes a UTF-8 JSON request atomically, with a unique ID and a 16 KiB limit. Only the enabled AI.Harness mod can call its UI-only bridge. The game claims/removes a request before executing it and atomically publishes `response.json`. Replies contain the matching ID, action and status or an error. The helper serializes waiting writers; queue-only requests should be used one at a time, waiting for their response before sending another.

A timeout is an unknown outcome, not cancellation. Inspect `request.json`, `claimed.json`, `response.json` and the current game log before retrying. A crash after consumption does not replay the request on restart. If a stale request needs removing, first stop the game and check its ID. Do not send credentials through console commands or log them.

Requests are parsed with the runtime's own field reader, not `DecodeJSON`.
