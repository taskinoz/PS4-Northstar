# AI.Harness local game control

AI.Harness is an opt-in development mod. It runs commands on the UI script thread through the game's `ClientCommand`, without editing retail VPKs or upstream mods. There is no web listener or emulated controller dependency. It requires the PS4 runtime containing the `NSAIHarness*` natives.

## Installation

1. Build the runtime with `scripts/Build-Northstar.ps1 -EnableRuntimeManifest` and deploy it using the project's normal deployment script.
2. Copy `mods/AI.Harness` into the game's `R2Northstar/mods/AI.Harness` and ensure `AI.Harness` is enabled in `enabledmods.json` if explicitly listed there.
3. Run the host helper below. It creates the mailbox under `%APPDATA%\shadPS4\data\northstar_ps4\ai_harness`. Use `-Mailbox` for another emulator user-data location.

Profile packaging excludes the harness unless `New-NorthstarProfile.ps1 -IncludeAIHarness` is specified. Disable or remove the mod when automation is not needed.

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

# Explicitly submit the legacy console.txt contents (file remains intact).
./scripts/Send-AIHarnessCommand.ps1 -FromConsoleFile
```

`launch` follows Northstar's local authentication sequence, completes local authentication, then queues `setplaylist tdm` and `map mp_lobby`. Authentication failure is returned as an error. A successful reply says **queued**, not that the map loaded successfully. Check the fresh session log for script errors and actual lobby readiness. `status` confirms the UI polling thread is responding and now reports `connected`, `lobby`, and `level` from game state. These do not prove that every initialization callback succeeded; also inspect the current session for script errors.

`json` passes `-Command` through `DecodeJSON(text, true)` and `EncodeJSON` and returns the result in the reply's `json` field, to test the JSON natives in the UI VM. Member order follows the Squirrel table, not the input.

`menu` opens a registered menu and `back` closes the active menu. These are script operations, not physical button clicks. Arbitrary button event injection and controller emulation are not implemented.

## Transport and recovery

The helper publishes a UTF-8 JSON request atomically, with a unique ID and a 16 KiB limit. Only the enabled AI.Harness mod can call its UI-only bridge. The game claims/removes a request before executing it and atomically publishes `response.json`. Replies contain the matching ID, action and status or an error. The helper serializes waiting writers; queue-only requests should be used one at a time, waiting for their response before sending another.

A timeout is an unknown outcome, not cancellation. Inspect `request.json`, `claimed.json`, `response.json` and the current game log before retrying. A crash after consumption does not replay the request on restart. If a stale request needs removing, first stop the game and check its ID. Do not send credentials through console commands or log them.

The legacy native console dispatcher remains disabled because its profiled address sends commands to a server instead of executing locally. Its reader now leaves console.txt untouched while disabled. Use the explicit helper import above. The harness's own request parsing still uses its native field reader rather than DecodeJSON, and it avoids the incomplete HTTP callback API.
