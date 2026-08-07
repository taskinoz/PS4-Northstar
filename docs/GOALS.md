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

Statically profiled the engine's internal ConVar constructor path for the exact retail hash (`config/stage2-offsets-CUSA04013.json`) and registered two ConVars through it at runtime: a diagnostic (`ns_stage2_loaded`) and `ns_allow_team_changes`. Both verified via `FindVar` returning the exact object the PRX allocated.

## Goal 4 — Native Squirrel function registration ✅ complete (proof only)

Registered a callable native (`NSStage2Ping`) into the UI Squirrel VM's real global table and had a shipped UI script call it successfully (`NSStage2Ping invoked` in the log). The sparse-script edit used for the proof was reverted afterward; the mechanism itself — insert into `internal+0x40d0`, never the type-registry table at `+0x4120` — is documented in [TECHNICAL-NOTES.md](TECHNICAL-NOTES.md) and ready to reuse.

## Goal 5 — Filesystem overlay + mod metadata discovery ✅ complete

- PC Northstar mod loose files (`Northstar.Client`, `Northstar.Custom`) are served from the PS4 `r2` search directory with zero VPK repacking and no engine memory mutation.
- The native PRX discovers mods from `/app0/mods` at runtime, parses each `mod.json` with a self-contained parser, and registers every mod ConVar through the Goal 3 constructor (18/18 verified in the last run).

## Goal 6 — Runtime mod script loading ⏳ in progress

- [x] Discover each mod's `Scripts` array from `mod.json` (parsed and counted; not yet compiled).
- [x] Compile an arbitrary script file into the engine at runtime via `CompileList` (client VA `0x3153f0`). A crash in this path was root-caused and fixed on 2026-08-07 (see TECHNICAL-NOTES.md) and is now verified 3/3 with no crash.
- [x] Determined the `scripts.rson` manifest-overlay approach (`Merge-Stage2ScriptsRson.ps1`) does **not** work: the engine reads the VPK-packed vanilla `scripts.rson`, not the `r2`-staged merged copy, whenever the same path exists in both. `CompileList` is the correct mechanism going forward, not the manifest merge.
- [ ] Prove a runtime-compiled script's code actually *executes*, not just compiles. Blocked: bare top-level statements don't compile in this UI script compiler, and `FindUiFunction` does not reliably resolve names (see Next steps below for the concrete unblock).
- [ ] Drive `CompileList` from each mod's already-parsed `Scripts` array instead of the one hardcoded probe path used to prove the mechanism.

## Goal 7 — Mod enable/disable + lifecycle ⬜ not started

Depends on Goal 6. Every discovered mod currently loads unconditionally. Needs a design decision (a convar per mod? a settings file under `mods/`? an in-game menu, mirroring PC Northstar's mod list UI?) before any code is written, then: skip disabled mods' script/ConVar registration, and make the toggle persist across boots without rebuilding the PRX.

## Goal 8 — Atlas authentication ⬜ not started

Port the native calls PC NorthstarLauncher uses for Atlas authentication (`tools/NorthstarLauncher-reference` has the reference implementation) to their PS4 `engine.prx`/`client.prx` equivalents. No PS4-side investigation has started; treat this as research-first (see Next steps).

## Goal 9 — Server browser & matchmaking ⬜ not started

Depends on Goal 8. No PS4-side investigation has started.

## Goal 10 — Real PS4 hardware validation ⬜ not started, deferred

Every goal above is verified only under shadPS4. TECHNICAL-NOTES.md repeatedly flags shadPS4 and real PS4 behavior as related but distinct targets — module list contents, base addresses, and kernel call behavior can all differ. This also requires a jailbroken PS4 with an existing kernel exploit/homebrew loader chain, which is out of scope until the native runtime work is stable under the emulator.

## Immediate next steps (priority order)

1. **Unblock Goal 6's execution proof.** A bare `NSM6ProbeMarker()` call at file scope fails to compile (`Global variable definition is followed by "("`). The next attempt should call the mod's `InitScript` (already parsed) through whatever real engine call site invokes it — trace that call site read-only first — or reuse the Goal 4 native-closure-callback trick (call a registered native *from inside* the injected script) instead of a bare statement.
2. **Wire mod `Scripts[]` into `CompileList`.** Goal 6's metadata discovery and its `CompileList` call currently don't talk to each other — one hardcoded probe path proved the mechanism. Loop over each mod's parsed `Scripts` array and call `CompileList` per file, gated the same way the probe is.
3. **Re-verify `ns_allow_team_changes` a second time**, and — more importantly — confirm the registered ConVar actually changes gameplay behavior in a live match, not just that `FindVar` returns the right object identity. Only one clean run exists; two earlier attempts hit an unrelated host memory/address-space issue, not a code fault.
4. **Design mod enable/disable (Goal 7)** before writing code: pick the storage mechanism and write it down, then implement.
5. **Start Atlas research (Goal 8) as a standalone, read-only task**: read the PC client's Atlas auth flow in `tools/NorthstarLauncher-reference`, write down the exact calls/protocol needed, and only then look for PS4 equivalents. Do not touch `engine.prx`/`client.prx` before the research is written down.
6. **Decide the fate of the Goal 0 VPK-patch pipeline** once Goal 7 is done: either keep it permanently as the foundation layer, or retire `scripts/Apply-Stage1Compatibility.ps1`'s transforms once native equivalents cover everything they currently paper over. Not urgent.

## Longer-term / stretch goals

- Full mod enable/disable parity with PC Northstar's in-game mod list UI.
- Investigate whether the PS4's own multiplayer networking stack imposes constraints Northstar's client/server protocol doesn't expect (untested; no work has started here).
- Revisit whether the Goal 0 static VPK pipeline can be fully retired in favor of the Goal 5 filesystem overlay for *all* content, not just loose mod files.

## Known risks & open questions

- Real PS4 kernel/module-list behavior may differ from shadPS4 in ways that break Goal 2's discovery logic; it has never been tested outside the emulator.
- `CompileList`'s internal object/vtable dependency (Goal 6) was observed populated in 3/3 recent runs but null in one earlier run; the exact readiness condition is still not fully understood, only safely gated against.
- The `ns_allow_team_changes` ConVar's semantics (`default=0 flags=0`) were chosen conservatively because current upstream Northstar no longer defines this legacy identifier — verify this is still the right default before shipping anything that depends on it.
- No investigation has started on whether Atlas auth or the server browser are even reachable from a PS4 client's network stack under shadPS4.

## Safety rules (always apply)

- Never reuse NorthstarLauncher's Windows `engine.dll`/`client.dll` offsets on PS4 — every address here is PS4-binary-specific.
- Key every code patch to a retail module hash and an exact byte preimage; validate both at runtime before dereferencing anything.
- Keep generated game data, extracted VPK content, retail binaries, and toolchain archives out of Git (see `.gitignore` and `tools/README.md`).
- Back up before replacing any file in the game directory; keep the `eboot.bin` rollback path working.
- Treat shadPS4 behavior and real PS4 behavior as related but distinct targets.
- Make the next native milestone read-only until module identity and base addresses are verified, and restore the default inert PRX with a clean-boot check after any experiment.

## Superseded / removed tooling

- `scripts/New-Stage2Workspace.ps1` (removed 2026-08-07): an early standalone tool that hashed `launcher.prx`/`engine.prx`/`client.prx` into a JSON inventory file. No script or doc ever called it — it was superseded by the live module tracker in `native/stage2/src/runtime.cpp` (which resolves the same modules at runtime) and by `scripts/New-Stage2R2Overlay.ps1` for staging. Kept only in git history.
