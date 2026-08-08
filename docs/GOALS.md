# Goals & next steps

This is the entry point for planning work on the PS4 Northstar port. It replaces the old "Stage 1 / Stage 2" narrative framing: Goal 0 (formerly "Stage 1") is complete, and everything since has been native runtime work (formerly "Stage 2"). The goal list below is the current source of truth for status; [TECHNICAL-NOTES.md](TECHNICAL-NOTES.md) is the detailed, chronological lab notebook (exact hashes, virtual addresses, byte preimages, run IDs) behind it.

## How to use this document

Each goal is scoped so it can be handed to a single agent/session on its own: it states the objective, current status, what is already verified, and what specifically remains. Before touching any hash-locked binary (`eboot.bin`, `engine.prx`, `client.prx`, ...), read the **Safety rules** section at the bottom — they apply to every goal below without exception.

Status legend: ✅ complete · ⏳ in progress · ⬜ not started

## Goal 0 — Foundation: static content integration ✅ complete

Get Titanfall 2 on PS4 (via shadPS4) to connect to a Northstar server by IP, using only static PS4 VPK patches — no native runtime code. Full detail in [FOUNDATION.md](FOUNDATION.md).

**Still required, not obsolete:** the currently-installed game boots on top of these patched VPKs and the UI-compatibility transforms in `scripts/Apply-Stage1Compatibility.ps1`. Keep this tooling until the native equivalents (Goals 3, 6, 7) cover everything it currently papers over — see "Retiring the Goal 0 VPK patches" under Next steps.

## Goal 1 — Native PRX bootstrap ✅ complete

Load `northstar_ps4.prx` before Titanfall's own game modules via a hash-locked `eboot.bin` patch, execute its initializer, and return control cleanly to normal boot. Verified under shadPS4; the game reaches the main menu with the PRX loaded.

## Goal 2 — Runtime module discovery ✅ complete

Resolve `engine.sprx` / `client.sprx` handles and segment mappings at runtime using the OpenOrbis module-info API, with no fixed/hard-coded addresses (shadPS4 base addresses are per-run and must never be hard-coded).

## Goal 3 — Native ConVar registration ✅ complete

Statically profiled the engine's internal ConVar constructor path for the exact retail hash (`config/stage2-offsets-CUSA04013.json`) and registered two ConVars through it at runtime: a diagnostic (`ns_stage2_loaded`) and `ns_allow_team_change`. Both verified via `FindVar` returning the exact object the PRX allocated. (Originally registered as `ns_allow_team_changes`, plural — corrected 2026-08-07 to the singular spelling the shipped UI scripts actually use; see Goal 8 below.)

## Goal 4 — Native Squirrel function registration ✅ complete (proof only)

Registered a callable native (`NSStage2Ping`) into the UI Squirrel VM's real global table and had a shipped UI script call it successfully (`NSStage2Ping invoked` in the log). The sparse-script edit used for the proof was reverted afterward; the mechanism itself — insert into `internal+0x40d0`, never the type-registry table at `+0x4120` — is documented in [TECHNICAL-NOTES.md](TECHNICAL-NOTES.md) and ready to reuse.

## Goal 5 — Filesystem overlay + mod metadata discovery ✅ complete

- PC Northstar mod loose files (`Northstar.Client`, `Northstar.Custom`) are served from the PS4 `r2` search directory with zero VPK repacking and no engine memory mutation.
- The native PRX discovers mods from `/app0/mods` at runtime, parses each `mod.json` with a self-contained parser, and registers every mod ConVar through the Goal 3 constructor (18/18 verified in the last run).

## Goal 6 — Runtime mod script loading ⏳ in progress

- [x] Discover each mod's `Scripts` array from `mod.json` (parsed and counted; not yet compiled).
- [x] Compile an arbitrary script file into the engine at runtime via `CompileList` (client VA `0x3153f0`). A crash in this path was root-caused and fixed on 2026-08-07 (see TECHNICAL-NOTES.md) and is now verified 3/3 with no crash.
- [x] Determined the `scripts.rson` manifest-overlay approach (`Merge-Stage2ScriptsRson.ps1`) does **not** work: the engine reads the VPK-packed vanilla `scripts.rson`, not the `r2`-staged merged copy, whenever the same path exists in both. `CompileList` is the correct mechanism going forward, not the manifest merge.
- [x] Drive `CompileList` from each mod's already-parsed `Scripts` array instead of the one hardcoded probe path used to prove the mechanism. `ModInfo` now stores each mod's `RunOn: "UI"` script paths; collection into a shared buffer always runs (harmless) when mod metadata + script inject are both enabled. **Actually compiling that real list crashes** on at least one real script (`ui/menu_ns_modmenu.nut`, a real-address fault reading `internal_vm+0x40a0` — see TECHNICAL-NOTES.md), so it's gated behind a further opt-in flag (`-EnableM6ScriptInjectFromMods`) that defaults off; the safe combined build falls back to the harmless probe file and was re-verified clean. The table itself was profiled and found healthy on a normal boot — see next item, the crash is not simply "table unpopulated."
- [ ] Prove a runtime-compiled script's code actually *executes*, not just compiles. Blocked: bare top-level statements don't compile in this UI script compiler, and `FindUiFunction` does not reliably resolve names (see Next steps below for the concrete unblock).
- [x] Profiled `internal_vm+0x40a0` — it's healthy, not the blocker (see below).
- [ ] **Refined (2026-08-07, live-debugged with cdb — see "Debugger setup" and "Live crash analysis" in TECHNICAL-NOTES.md):** `internal_vm+0x40a0` is *not* broadly unpopulated — a debugger attached to the actual fault confirmed `rbx` (the table read) is exactly null (`ds:...038`, i.e. `0+0x38`), but a breakpoint just before the crash showed the *same* table healthy and stable across at least 6 preceding calls in the same compile, with a counter (`internal_vm+0x40c0`) incrementing each call. The miss happens on one *specific, later* call — most likely resolving `ModInfo` itself — not because the table doesn't exist. Next: a conditional breakpoint (`rbx == 0`) to land exactly on the failing call; recipe drafted but untested in TECHNICAL-NOTES.md. Still gated behind `-EnableM6ScriptInjectFromMods` (default off).

## Goal 7 — Mod enable/disable + lifecycle ⬜ not started

Depends on Goal 6. Every discovered mod currently loads unconditionally. Needs a design decision (a convar per mod? a settings file under `mods/`? an in-game menu, mirroring PC Northstar's mod list UI?) before any code is written, then: skip disabled mods' script/ConVar registration, and make the toggle persist across boots without rebuilding the PRX.

## Goal 8 — Real server connectivity & Atlas authentication ⬜ not started

**Re-scoped 2026-08-07.** Full Atlas authentication needs the game's own signed license/session token (tied to the real PSN/game account) — sensitive, and not a near-term priority. De-prioritized; revisit only if a path is found that doesn't require handling that token directly (`tools/NorthstarLauncher-reference` has the PC reference implementation if that's ever attempted — treat as research-first, do not touch `engine.prx`/`client.prx` before the research is written down).

The practical near-term path is **unauthenticated connectivity**: `ns_auth_allow_insecure` is a *server-side* ConVar (`ServerAuthenticationManager::CheckAuthentication`/`AuthenticatePlayer`,`tools/NorthstarLauncher-reference/primedev/server/auth/serverauthentication.cpp`) that makes a server accept connecting players without validating any Atlas token. Because it's entirely server-side, the PS4 client likely needs **no** Atlas-specific work at all to connect to such a server — Goal 0's existing direct-connect flow may already be sufficient (its success criteria already claim a controlled Northstar server connects cleanly). This hasn't been tested end-to-end since the native runtime work began, and doing so would also be the first real opportunity to verify several other currently-open items live (Goal 6 script execution, `ns_allow_team_change` gameplay effect, general stability under real play) — see Immediate next steps.

## Goal 9 — Server browser & matchmaking ⬜ not started

Depends on real Atlas (Goal 8), not just insecure direct-connect. No PS4-side investigation has started.

## Goal 10 — Real PS4 hardware validation ⬜ not started, deferred

Every goal above is verified only under shadPS4. TECHNICAL-NOTES.md repeatedly flags shadPS4 and real PS4 behavior as related but distinct targets — module list contents, base addresses, and kernel call behavior can all differ. This also requires a jailbroken PS4 with an existing kernel exploit/homebrew loader chain, which is out of scope until the native runtime work is stable under the emulator.

## Immediate next steps (priority order)

1. **Test a real insecure server connection end-to-end — in progress, real progress made.** Loose `.cfg` files can't be used to auto-trigger `connect` on retail PS4 (see TECHNICAL-NOTES.md), but the user drove the in-game direct-connect menu by hand and reached an actual in-game session against a real server. That surfaced two blocking UI bugs, both found and fixed the same way (grep the actual shipped script content for every `GetConVar*` call, cross-reference against what's registered) rather than fixing one-by-one via trial and error:
   - `ns_allow_team_change` (singular) was misregistered as `ns_allow_team_changes` (plural) — a stale guess from a legacy identifier. Fixed.
   - `ns_has_agreed_to_send_token` (from NorthstarLauncher's `clientauthhooks.cpp`) was never registered at all. Fixed.
   Both are now registered unconditionally in the default build (no longer gated behind `-EnableTeamChangesConVar`), verified `success=1`, clean boot confirmed. New default inert PRX SHA-256 `89eedb52fb85a9cee9cf71392d70c3cd46c0326191895cfb3629d4184fc2f981`. Also found and fixed along the way: the installed VPKs had drifted from this project's expected Stage 1 build (a side effect of separate manual testing) and were restored via the existing pipeline.
   Getting past `ns_has_agreed_to_send_token`'s dialog then surfaced a third, worse blocker: `NorthstarMasterServerAuthDialog()`'s Yes/No dialog does not respond to **any** input at all — not keyboard, not mouse hover/click, not controller, not even the native `toggleconsole` bind, which is independent of UI focus entirely. This is a generic-dialog-system bug (the `OpenDialog`/`AddDialogButton` machinery in vanilla `menu_dialog.nut`/`_menus.nut`/`dialog.menu`, all confirmed byte-identical to unmodified retail content — not something Stage 1 patched), and it's the first time this project has exercised any interactive menu input at all, so it was previously unknown/untested. Root cause not found yet (see Known risks below). **Workaround shipped (2026-08-07):** `ns_has_agreed_to_send_token` now defaults to `"1"` (agreed) instead of `"0"`, so the dialog is skipped entirely rather than opening and hanging. New default inert PRX SHA-256 `75582481bce8946f6a3d4fdbe61537953bfb7295729b1e83e91abfc34eb67c71` (87,584 B). **Trade-off:** this silently opts every player in to sending their origin token to the Northstar masterserver without asking — acceptable to unblock testing now, but should be revisited once the dialog-input bug is actually fixed (see Known risks). **Still open:** confirm the connection now proceeds past this point with the workaround in place; there may be more blocking dialogs or missing ConVars/APIs once further into a real match (the same grep-and-cross-reference method should be reused proactively rather than waiting for each one to surface as a popup) — and any of those, if they're also dialogs, will hit the same unresponsive-input bug.
   Clicking "Launch Northstar" then hit a fourth blocker: `[UI] The index "DirectConnectMenu" does not exist` (`ui/_menus.nut #874`). An earlier session had already built a working direct-connect menu (`menu_direct_connect.nut`, `direct_connect.menu`) and wired the Northstar button to open it, but never registered it via `AddMenu(...)` in `_menus.nut`. **Fixed (2026-08-07):** added the missing registration, plus (per user request) a second standalone "Direct Connect" button next to "Launch Northstar" and a new `MENU_DIRECT_CONNECT` localisation key. Rebuilt/redeployed via `Build-AndDeployStage1Vpks.ps1`, all 6 VPK hashes re-verified. Also scaffolded (per user request) a proper Northstar-mod-shaped version of the menu logic, `work/stage1/loose/Northstar.DirectConnect/`, deployed to `mods/` for discovery — **inert by default** (mod script injection is still off by default, Goal 6), see TECHNICAL-NOTES.md for the double-registration hazard if it's ever enabled alongside the Stage 1 patch's own registration. **Still open:** actually get a live connection through to a real match now that all four known blockers are cleared.
2. **Land the failing call in the `menu_ns_modmenu.nut` crash with a conditional breakpoint.** A debugger is now set up (cdb.exe, see "Debugger setup" in TECHNICAL-NOTES.md) and confirmed the crash mechanically, narrowing it to one specific, later call in the compile rather than a broadly-unpopulated table. Use the same `client_base + 0x861263` breakpoint with a condition on `rbx == 0` (draft recipe in TECHNICAL-NOTES.md, untested) to see exactly what's being looked up when it misses.
3. **Unblock Goal 6's execution proof.** A bare `NSM6ProbeMarker()` call at file scope fails to compile (`Global variable definition is followed by "("`). The next attempt should call the mod's `InitScript` (already parsed) through whatever real engine call site invokes it — trace that call site read-only first — or reuse the Goal 4 native-closure-callback trick (call a registered native *from inside* the injected script) instead of a bare statement.
4. ~~Re-verify `ns_allow_team_changes` a second time~~ **Done (2026-08-07):** second clean run, no address-space issue this time (confirms that was transient host contention, not a code fault). **Still open**: confirm the registered ConVar actually changes gameplay behavior in a live match — see item 1.
5. **Design mod enable/disable (Goal 7)** before writing code: pick the storage mechanism and write it down, then implement.
6. **Decide the fate of the Goal 0 VPK-patch pipeline** once Goal 7 is done: either keep it permanently as the foundation layer, or retire `scripts/Apply-Stage1Compatibility.ps1`'s transforms once native equivalents cover everything they currently paper over. Not urgent.

## Longer-term / stretch goals

- Full mod enable/disable parity with PC Northstar's in-game mod list UI.
- Investigate whether the PS4's own multiplayer networking stack imposes constraints Northstar's client/server protocol doesn't expect (untested; no work has started here).
- Revisit whether the Goal 0 static VPK pipeline can be fully retired in favor of the Goal 5 filesystem overlay for *all* content, not just loose mod files.

## Known risks & open questions

- Real PS4 kernel/module-list behavior may differ from shadPS4 in ways that break Goal 2's discovery logic; it has never been tested outside the emulator.
- `CompileList`'s internal object/vtable dependency (Goal 6) was observed populated in 3/3 recent runs but null in one earlier run; the exact readiness condition is still not fully understood, only safely gated against.
- The `ns_allow_team_change` ConVar's semantics (`default=0 flags=0`) were chosen conservatively because current upstream Northstar no longer defines this legacy identifier under either spelling — verify this is still the right default before shipping anything that depends on it.
- No investigation has started on whether the server browser is reachable from a PS4 client's network stack under shadPS4; direct-connect (Goal 0) is the only tested connectivity path so far.
- Compiling a real mod UI script with a typed struct parameter (`ui/menu_ns_modmenu.nut`, using `ModInfo`) via `CompileList` crashes reading `internal_vm+0x40a0`. Live-debugged (2026-08-07): the table itself is healthy and stable across multiple calls in the same compile; the null happens on one specific, later call, most likely resolving `ModInfo` itself. Root cause (why that one lookup misses) still open. Not all mod scripts are necessarily affected — only one has been tested — but treat any `-EnableM6ScriptInjectFromMods` script as unverified. See TECHNICAL-NOTES.md.
- `ns_auth_allow_insecure` is a server-side setting (confirmed by reading `tools/NorthstarLauncher-reference`'s source) — connecting to an insecure server should need no PS4-side Atlas work, but this has not actually been tested against this project's native runtime work yet.
- **Generic dialog UI (`OpenDialog`/`AddDialogButton`, `menu_dialog.nut`) does not respond to any input** (keyboard, mouse, controller, or the native `toggleconsole` bind) — confirmed live 2026-08-07 on `NorthstarMasterServerAuthDialog`. All the underlying script/resource files (`menu_dialog.nut`, `_menus.nut`, `dialog.menu`) are byte-identical to unmodified retail content and already have correct `tabPosition`/`navUp`/`navDown` wiring, so this isn't a simple script-data fix. Root cause not yet found — needs live cdb investigation into whether the engine's native `FocusDefaultMenuItem`/input-polling functions are even being reached, or whether shadPS4's pad/keyboard HLE layer is delivering events at all. Currently worked around for the auth dialog only by pre-agreeing `ns_has_agreed_to_send_token` (see item 1) — but this will block **any other dialog** the player reaches (leave-match confirm, error dialogs, data-center picker, etc.), so it needs a real fix before those become reachable in testing.

## Safety rules (always apply)

- Never reuse NorthstarLauncher's Windows `engine.dll`/`client.dll` offsets on PS4 — every address here is PS4-binary-specific.
- Key every code patch to a retail module hash and an exact byte preimage; validate both at runtime before dereferencing anything.
- Keep generated game data, extracted VPK content, retail binaries, and toolchain archives out of Git (see `.gitignore` and `tools/README.md`).
- Back up before replacing any file in the game directory; keep the `eboot.bin` rollback path working.
- Treat shadPS4 behavior and real PS4 behavior as related but distinct targets.
- Make the next native milestone read-only until module identity and base addresses are verified, and restore the default inert PRX with a clean-boot check after any experiment.

## Superseded / removed tooling

- `scripts/New-Stage2Workspace.ps1` (removed 2026-08-07): an early standalone tool that hashed `launcher.prx`/`engine.prx`/`client.prx` into a JSON inventory file. No script or doc ever called it — it was superseded by the live module tracker in `native/stage2/src/runtime.cpp` (which resolves the same modules at runtime) and by `scripts/New-Stage2R2Overlay.ps1` for staging. Kept only in git history.

## Machine-level tooling (not part of this repo)

- **`cdb.exe`** (console debugger, added 2026-08-07): installed via the Windows SDK's standalone "Debugging Tools for Windows" feature, not through this repo or `tools/`. Needed to live-debug shadPS4 crashes (shadPS4 runs PS4 x86-64 code directly on the host CPU, so a normal Windows debugger works). Install/usage recipe in `docs/TECHNICAL-NOTES.md` ("Debugger setup"). Machine-level because it's a general Windows dev tool, not project source — reinstall it on a fresh machine the same way if native crash investigation continues.
