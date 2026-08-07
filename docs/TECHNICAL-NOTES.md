# Native port — technical notes

This is the detailed, chronological technical log behind the native runtime port: exact hashes, virtual addresses, byte preimages, run IDs, and the reasoning behind each fix. For current status and what to do next, start at [GOALS.md](GOALS.md) instead — this file is the evidence trail, not the status tracker. Section headings below still say "Milestone N" / "Stage 1" in places; read those as historical labels (they map onto the Goals in GOALS.md) rather than an active framing.

Last verified: 2026-08-07, Titanfall 2 PS4 CUSA04013, PS4 build `R2PS4_r2dlc11_598_CL297590_2017_12_05_12_36_PM` (PC counterpart `Titanfall2_v2_0_11_0`), running under shadPS4.

## Current result

The game boots to the main menu with a native OpenOrbis PRX loaded before Titanfall begins loading its normal game modules. The PRX initializer executes and starts a detached diagnostic thread. No retail PRX is modified.

The verified startup sequence in shad_log.txt is:

    sceKernelLoadStartModule: /app0/bin/ps4_retail/northstar_ps4.prx
    Module started : northstar_ps4.prx
    [NorthstarPS4] Stage 2 PoC initializer executed
    [NorthstarPS4] module tracker started
    [NorthstarPS4] module scan result=0x0 available=6

Titanfall then continues loading its normal modules and reaches the menu.

## Exact tested hashes

These hashes identify the only retail build supported by the current bootstrap.

| File | SHA-256 |
| --- | --- |
| Retail eboot.bin backup | 590956ab2c9251f588348a1c066ed4045ce94ffc87c25faa5872a1f92ec35824 |
| Patched eboot.bin | 5a4b52ff7224cca7bb9f5f25888847d5d2bd1669af1a4f21328927932fbfd502 |
| launcher.prx | 0a866190c6dfa7e61555baed6b671ac15d271394731434589c91cfd0f28302e6 |
| engine.prx | fa59636b1b5fa66fa936631457b47e49039b0cfbef29e783c2fd082ece3dae37 |
| client.prx | abc6efd2125a2a58d32ad9d23d245a03fd3213a763e407712a575f0bc04e879b |
| Current northstar_ps4.prx (default, inert) | f640b1dce71a36c90670434239ab74de5c7ec5de907fa3cf17a4f1e6922445b2 (2026-08-07; older hashes below reflect the default at the time each section was written) |

Do not apply the eboot patch to any other hash. The script checks the retail hash and byte preimages before writing.

## Toolchain

The working toolchain is OpenOrbis v0.5.4, release asset toolchain-llvm-18.tar.gz.

Archive SHA-256:

    3c7cd5bb593ca74fa1c13fd59f3938dc0fc07985167f7275063019e63abe4526

The extracted local default is:

    tools/openorbis-0.5.4/OpenOrbis/PS4Toolchain

The build script sets the documented OO_PS4_TOOLCHAIN variable itself. LLVM clang++, ld.lld, and llvm-nm must be available on PATH. Generated outputs are under dist/stage2-poc and are ignored by Git.

## Build and deploy

From the repository root:

    .\scripts\Build-Stage2Poc.ps1
    .\scripts\Deploy-Stage2Poc.ps1

The deploy command backs up an existing northstar_ps4.prx beneath work/stage2/deploy-backups and verifies the installed hash.

To install the eboot bootstrap for the first time, the retail eboot must be active:

    .\scripts\Enable-Stage2Bootstrap.ps1

To restore the retail eboot:

    .\scripts\Enable-Stage2Bootstrap.ps1 -Disable

The recoverable retail backup is:

    D:\PS4\ShadPS4\CUSA04013\eboot.bin.northstar-stage2.bak

After restoring, running Enable-Stage2Bootstrap.ps1 again regenerates and installs the patch.

## Automated shadPS4 iteration

scripts/Invoke-Stage2Iteration.ps1 automates the native test loop using the active shadPS4 CLI.

Default behavior:

1. Builds northstar_ps4.prx.
2. Deploys it with hash verification and backup.
3. Launches the configured shadPS4 executable with --game and --log-append.
4. Reads only log bytes appended after launch.
5. Prints NorthstarPS4 lines live.
6. Stops only the process started by the script after the success or failure regex matches.
7. Writes the appended transcript and result.json beneath work/stage2/iterations/<timestamp>.

Fast native diagnostic run:

    .\scripts\Invoke-Stage2Iteration.ps1

Leave the game running after the native success marker so the menu can be tested:

    .\scripts\Invoke-Stage2Iteration.ps1 -KeepRunning

Reuse the installed PRX without building or deploying:

    .\scripts\Invoke-Stage2Iteration.ps1 -SkipBuild -SkipDeploy

Validate paths, build, and deployment without launching shadPS4:

    .\scripts\Invoke-Stage2Iteration.ps1 -NoLaunch

Override ShadPs4Exe when the Qt launcher installs a new version. The current default points to:

    C:\Users\tristan\AppData\Roaming\shadPS4QtLauncher\versions\Pre-release\shadPS4.exe

The default success expression is the completed engine/client tracker. Change SuccessPattern for a narrower experiment such as the registration-export probe.

Verified automated run on 2026-08-06: the final registration-export marker was reached in 4.901 seconds, PID 21608 was stopped by the runner, and artifacts were saved beneath work/stage2/iterations/20260806-113008.
## Bootstrap design

eboot.bin is already a decrypted Orbis ELF in the shadPS4 game directory. The patch reuses eboot's existing sceKernelLoadStartModule import; it does not add an import.

Resolved details for this retail eboot:

| Item | Value |
| --- | --- |
| sceKernelLoadStartModule NID | wzvqT4UqKX8 |
| Loader symbol index | 63 |
| Loader jump relocation | 28 |
| Loader GOT virtual address | 0x42c0 |
| Loader PLT virtual address | 0x14e0 |
| Original startup call site VA | 0x128d |
| Original startup call file offset | 0x528d |
| Bootstrap stub VA | 0x29e0 |
| Bootstrap stub file offset | 0x69e0 |
| PRX path VA | 0x2a06 |

The original call at 0x128d invokes eboot initialization at 0x20. It is replaced with a call to a 38-byte wrapper. The wrapper:

1. Calls the original initializer at 0x20.
2. Calls the existing loader PLT entry at 0x14e0 with /app0/bin/ps4_retail/northstar_ps4.prx.
3. Restores the stack and returns to normal eboot startup.

The wrapper and path occupy zero-filled alignment space after the original RX segment. The patch extends that segment's file and memory sizes only far enough to cover the payload. Enable-Stage2Bootstrap.ps1 verifies the complete retail SHA-256, original call bytes, segment sizes, and empty injection range.

## OpenOrbis linker correction

OpenOrbis v0.5.4's supplied link.x places .init_array correctly but does not define __init_array_start and __init_array_end. Its crtlib.o expects those names. Without a correction, module_start looks in empty BSS instead of calling C++ constructors.

The project-local native/stage2/link.x defines:

    __init_array_start = .;
    KEEP(*(.init_array .init_array.*));
    __init_array_end = .;

The verified linked addresses are 0x4000 and 0x4008.

Do not silently switch the build back to the toolchain's unmodified link.x.

## shadPS4 PRX lifecycle correction

shadPS4 reports the OpenOrbis PRX as started but does not invoke the hidden module_start path used by crtlib.o. It does execute the ELF DT_INIT address.

Build-Stage2Poc.ps1 therefore finds the linked NorthstarPs4Init symbol and rewrites DT_INIT to that symbol before create-fself conversion. The current verified value is:

    DT_INIT = 0x20

NorthstarPs4Init is idempotent. It also remains in .init_array so a real PS4 loader that invokes OpenOrbis module_start can use the normal constructor path without initializing twice.

Removing the DT_INIT rewrite makes shadPS4 say the PRX started while none of the Northstar code actually runs.

## Logging behavior

sceKernelDebugOutText does not interpret printf formatting under the tested shadPS4 build. Passing a format plus variadic arguments prints percent placeholders literally.

runtime.cpp uses a fixed 512-byte stack buffer with vsnprintf and passes the completed string to sceKernelDebugOutText. Use that helper for formatted diagnostic output.

The reliable success marker is:

    [NorthstarPS4] Stage 2 PoC initializer executed

## Module discovery status

The diagnostic thread and the OpenOrbis module-info ABI are working correctly. sceKernelGetModuleList returned success and the visible count progressed 6, 13, 14, 22, 23, 24, and 25 during the verified menu boot. All six initial sceKernelGetModuleInfo calls succeeded.

shadPS4 normalizes module names returned by sceKernelGetModuleInfo to .sprx. Examples from the verified run:

- eboot.bin is reported as titanfall2_ps4.sprx, handle 0.
- northstar_ps4.prx is reported as northstar_ps4.sprx, handle 5.
- The initial Northstar executable segment was mapped at 0x8090f0000 in that run.

The original tracker incorrectly required engine.prx and client.prx. The current build accepts both .prx and normalized .sprx names and logs each newly discovered handle once.

Runtime discovery is now complete under shadPS4. The verified target mappings from the 2026-08-06 menu boot were:

| Target | Handle | Segment | Address | Size | Protection |
| --- | --- | --- | --- | --- | --- |
| engine.sprx | 0xD | 0 | 0x80FEA8000 | 0x398000 | RX |
| engine.sprx | 0xD | 1 | 0x810240000 | 0x4C000 | R |
| engine.sprx | 0xD | 2 | 0x81028C000 | 0x4E1C000 | RW |
| engine.sprx | 0xD | 3 | 0x80FEA8000 | 0xC50 | RW metadata |
| client.sprx | 0x16 | 0 | 0x81DFA0000 | 0x9E0000 | RX |
| client.sprx | 0x16 | 1 | 0x81E980000 | 0xC4000 | R |
| client.sprx | 0x16 | 2 | 0x81EA44000 | 0x1F08000 | RW |
| client.sprx | 0x16 | 3 | 0x81DFA0000 | 0x590 | RW metadata |

The tracker completed with engine=1 and client=1. These addresses are per-run runtime mappings; use the handles and module-info API rather than hard-coding them. Real PS4 behavior must still be verified separately from shadPS4.

Do not hard-code 0x80fea4000. It is a runtime emulator address and may change.

Current diagnostic build behavior:

- Logs the module-list result whenever available changes.
- Dumps all six initial handles, sceKernelGetModuleInfo results, names, and segment mappings once.
- Continues polling read-only for engine.prx and client.prx.

Recommended next checks:

1. Log the six initial handles and the result, name, segment count, and segment addresses returned by sceKernelGetModuleInfo.
2. Log changes to available over time.
3. Test sceKernelGetModuleList2 and sceKernelGetModuleInfo2 against the current shadPS4 implementation if their ABI can be sourced authoritatively.
4. If LLE game PRXs are intentionally absent from the kernel list, intercept or observe sceKernelLoadStartModule rather than using a fixed base.
5. Only after engine/client discovery works, create an offset profile keyed to the exact retail SHA-256 values.
6. Register a harmless diagnostic convar first, then ns_allow_team_changes.

## Current read-only convar interface probe

The tracker captures vstdlib.sprx, which was handle 0x9 in the verified run. After both engine and client are discovered, the current build:

1. Calls sceKernelDlsym on the vstdlib handle for CreateInterface.
2. Requests VEngineCvar007.
3. If the interface is returned, calls virtual slot 16 (Northstar's known read-only FindVar slot) for sv_cheats.
4. Logs all results and addresses without allocating a ConVar or changing game memory.

The verified read-only probe succeeded under shadPS4: sceKernelDlsym returned CreateInterface at 0x80B3633C0, VEngineCvar007 was 0x80B3752A0, virtual slot 16 was 0x80B351DB0, and FindVar returned sv_cheats at 0x8136BE420. These are per-run addresses and must not be hard-coded.

Failure at any gate returns without dereferencing the next object. This probe validates the PS4 export/interface ABI before any registration work.

The resolution-only registration probe completed: every tested C and Itanium C++ constructor symbol returned 0x80020003 with a null address from both engine.sprx and vstdlib.sprx. Registration therefore requires a hash-specific internal profile or another supported ICvar method; there is no usable exported constructor among the tested symbols.

## Static engine convar profile

Static analysis of the exact engine hash above identified the native sv_cheats construction path. The call site at engine VA 0xDCE52 prepares a static object, name, default/help strings, flags, and callback before calling the internal constructor candidate at VA 0x205450. That routine initializes a 0x90-byte object and delegates to the constructor core at VA 0x205DB0.

Engine VA 0x2DF1C0 calls CreateInterface("VEngineCvar007") and then virtual offset 0xF8 (slot 31) with a command-base pointer. This is the current registration-method candidate. It must remain read-only until the object layout and calling convention are verified with a harmless diagnostic ConVar.

The hash-locked candidate profile is config/stage2-offsets-CUSA04013.json. Verify its SHA-256 and every recorded byte preimage with:

    .\scripts\Test-Stage2EngineProfile.ps1

The profile status is read-only-candidate; its offsets are not yet authorized for mutation or hard-coded runtime calls.

### Verified diagnostic registration

The opt-in diagnostic build successfully registered ns_stage2_loaded under shadPS4 on 2026-08-06. Both runtime preimage gates matched before the constructor call. FindVar("ns_stage2_loaded") returned the exact 0x90-byte object allocated by northstar_ps4.prx:

    [NorthstarPS4] diagnostic convar gate base=0x80fea8000 size=0x398000 constructor=1 callsite=1
    [NorthstarPS4] diagnostic convar registration result=0x8091040b0 expected=0x8091040b0 success=1

The automated run completed in 6.678 seconds and saved its transcript and result beneath work/stage2/iterations/20260806-131039. The tested diagnostic PRX SHA-256 was b3cf875ee8846746ec8aa21dcf25304d9a8c8818fb1bfdb6f9b36af9148aed81.

Diagnostic registration is build-time gated with -EnableDiagnosticConVar. After the test, the default read-only PRX was rebuilt and deployed; its installed SHA-256 is cf2ec3b5c965ea0609afb0cbab462923e12bb1e938a2fc07f8a2017937a5196d.


### Team changes ConVar implementation

ns_allow_team_changes is implemented using the verified constructor path with string default 0, flags 0, no callback, and a 0x90-byte aligned object. It is independently build-time gated:

    .\scripts\Build-Stage2Poc.ps1 -EnableTeamChangesConVar
    .\scripts\Invoke-Stage2Iteration.ps1 -EnableTeamChangesConVar

The current upstream NorthstarLauncher, NorthstarMods, and Northstar release repositories no longer contain this legacy identifier, and the installed Northstar.dll does not contain it. The conservative Stage 1 compatibility semantics are therefore recorded explicitly in the registration log as default=0 flags=0.

Runtime verification was attempted twice on 2026-08-06. Both shadPS4 processes failed in address_space.cpp:200 with \"Insufficient system resources exist to complete the requested service\" before eboot or any Northstar marker executed. Artifacts are beneath:

    work/stage2/iterations/20260806-131844
    work/stage2/iterations/20260806-131915

A third attempt after host cleanup failed at the same pre-eboot address-space assertion and is saved beneath work/stage2/iterations/20260806-132207. At diagnosis time Windows reported approximately 6.0 GB free physical memory, 8.0 GB free commit, and a 7.25 GB pagefile. This confirms the current blocker is host address-space/commit availability or shadPS4 address-space initialization, not an observed failure in the gated ConVar code.

Before retrying, increase the Windows pagefile or close enough applications to provide substantially more commit headroom. Do not change the Stage 2 code based on these failures because no tested run reached eboot.



### Verified team changes registration

The address-space assertion is transient under this shadPS4 build and launch retries are required. In the final verification sequence, attempt 1 failed before eboot and attempt 2 completed normally.

The gated ns_allow_team_changes registration passed all runtime preimage checks. FindVar returned the exact object allocated by northstar_ps4.prx:

    [NorthstarPS4] convar mutation gate base=0x80fea8000 size=0x398000 constructor=1 callsite=1
    [NorthstarPS4] team changes convar registration result=0x8091040b0 expected=0x8091040b0 success=1 default=0 flags=0

The successful run completed in 5.866 seconds and is saved beneath work/stage2/iterations/20260806-132832. Milestone 4 is complete under shadPS4. The default read-only PRX was restored afterward with SHA-256 c6ea058def829d6ed7c7a1ccc6feaabfea2e41f223dc91d9ca41f07ba09fcfc1.

The tested gated PRX SHA-256 was 66cbb28fc531b1220ef0e5b8295395ff83bdbb63dd6a97e76fc5f70f0f971a79. After both attempts, the default read-only PRX was rebuilt and deployed with SHA-256 c6ea058def829d6ed7c7a1ccc6feaabfea2e41f223dc91d9ca41f07ba09fcfc1.

**Second confirmation (2026-08-07, run `20260807-155931`):** re-ran `-EnableTeamChangesConVar` per the Goal 6 next-steps list. Completed cleanly on the first attempt this time (5.345 s, no address-space assertion), same result:

    [NorthstarPS4] convar mutation gate base=0x80fea8000 size=0x398000 constructor=1 callsite=1
    [NorthstarPS4] team changes convar registration result=0x8091040b0 expected=0x8091040b0 success=1 default=0 flags=0

This confirms the registration mechanism itself is reliable and the two 2026-08-06 address-space failures were host resource contention, not a code fault, consistent with what was already suspected. **Still not verified**: whether the registered ConVar actually changes gameplay behavior in a live match (e.g. team-switch UI/logic reading it) — that requires a real Northstar server connection and a team-based gamemode, not just `FindVar` identity, and hasn't been attempted. The default inert PRX was restored and boot re-verified clean afterward (run `20260807-160202`).


## Goal 0 / "Stage 1" context

See [FOUNDATION.md](FOUNDATION.md) for the full static-integration pipeline. In short: Goal 0 builds sparse patch VPKs with RSPNVPK and can boot to the menu. Compatibility transforms temporarily remove Northstar loader-dependent UI calls and convars. The missing native convar ns_allow_team_changes was one reason for moving to native runtime work.

Vanilla in Northstar means the separate unmodified-game environment used for coexistence on PC. It was intentionally removed from the Goal 0 PS4 path and should only be restored after the native filesystem/mod runtime exists.

Do not delete the Goal 0 compatibility transforms yet. Restore original Northstar UI behavior incrementally as matching native services become available.

## Safety rules for continuing

- Never reuse NorthstarLauncher Windows engine.dll or client.dll offsets on PS4.
- Key every code patch to a retail module hash and exact byte preimage.
- Keep generated game data, extracted VPK content, retail binaries, and toolchain archives out of Git.
- Back up before replacing any file in the game directory.
- Keep the eboot rollback path working.
- Treat shadPS4 behavior and real PS4 behavior as related but distinct targets.
- Make the next native milestone read-only until module identity and base addresses are verified.
## UI VM and native-registration discovery

Milestone 5 read-only discovery is verified under shadPS4 for client.prx SHA-256 `abc6efd2125a2a58d32ad9d23d245a03fd3213a763e407712a575f0bc04e879b`.

The client script-owner global is at VA `0x1AFBFB8`. Its `+0x8` field becomes the UI VM wrapper. The wrapper context is `1` at `+0x8`; its underlying engine/Squirrel VM pointer is at `+0x50`, not `+0x8`. The corrected offsets are in `config/stage2-offsets-CUSA04013.json`.

Two client anchors are now exact: native registration at VA `0x67A3C0` with preimage `554889e54157415641554154534883ec`, and the `RunUIScript` registration exemplar at VA `0x2F1554` with preimage `488d3505236e0131d2b9010000004531`.

The default PRX validates both anchors without calling them. The successful marker was:

    [NorthstarPS4] UI registration probe sqvm=0x2028ae620 register=1 exemplar=1

The run completed on the first attempt in 15.458 seconds under `work/stage2/iterations/20260806-135217`. The installed read-only PRX SHA-256 is `cac6174e4e62ad29e3be45c163f5e47beb82b8cbb8cbb47bd1c9f2b538eb434d`.

The exemplar confirms a 0x68-byte record and callback pointer at offset `0x60`, but the complete PS4 record ABI and registration timing are not yet proven. Do not call VA `0x67A3C0` or hand-build a live record until those fields are validated. A polling-thread call after wrapper discovery is not sufficient proof that registration occurs before frontend compilation.

Next safe action: trace the function that invokes the built-in native-registration block immediately after UI VM creation, then insert the diagnostic into the existing sequence or install a narrowly gated pre-frontend hook. Require the client hash and both preimages before mutation.

## Filesystem overlay (Milestone 6, verified)

The overlay approach requires no engine surgery: PC Northstar mod loose files are staged into the engine's GAME search directory `/app0/r2` (on disk `D:\PS4\ShadPS4\CUSA04013\r2`). The engine searches that directory before its other GAME roots (`r2_doNotShip`, `r1_doNotShip`, `platform`), so a loose file with a matching logical path shadows the equivalent VPK content. There is no VPK repacking and no filesystem_stdio memory mutation.

Staging tooling:

- `config/stage2-overlay-manifest.json` selects Northstar.Client + Northstar.Custom (source `D:\Games\Titanfall2\R2Northstar\mods\<mod>\mod`), targets `r2`, and excludes `*.dll`/`*.exe`/`*.so`/`*.pdb`/`*.json`.
- `scripts/New-Stage2R2Overlay.ps1` copies the selected loose files into `CUSA04013\r2\<relative-path>` and records what it staged in `r2\.ns_overlay_staged.json` so `-Clean` removes exactly those files (never the game's own r2 content).
- 192 files staged (84 Northstar.Client + 108 Northstar.Custom), with a collision audit of 0 against the existing r2 content.

Verification harness (`ProbeFilesystemInterface` in `native\stage2\src\runtime.cpp`, retained flag-guarded behind `-EnableM6FsOverlay`):

- `sceKernelDlsym(filesystem_stdio.sprx, "CreateInterface")` → `"VFileSystem017"` returns the real IFileSystem; vtable at `fs+0`, vtable2 at `fs+8`; vtable2[0]=Read, [2]=Open, [3]=Close; calls use the address of the `fs+8` field as `this`.
- In the verified run all four representative staged logical paths opened and read correctly through the GAME pathID, with shadPS4's kernel FS log confirming the resolved `/app0/r2/...` paths: `scripts/vscripts/cl_northstar_client_init.nut` (Client), `resource/northstar_client_localisation_english.txt` (Client), `cfg/autoexec_ns_client.cfg` (Client), `scripts/vscripts/_disallowed_tacticals.gnut` (Custom). Boot reached `module tracker complete engine=1 client=1`.
- The PS4 `filesystem_stdio.prx` vtable[10] AddSearchPath is a no-op stub (`xor eax,eax; ret`), so adding extra search roots through the interface is not supported on the console build; the staged r2 directory is the supported overlay mechanism.

Builds:

- Overlay/probe build: `.\scripts\Invoke-Stage2Iteration.ps1 -EnableM6FsOverlay` → SHA-256 `25178f280500d799e1a191c67d7d7b0b17a45b0` (87,776 B). Iteration `work/stage2/iterations/20260806-*` (probe run showing all four staged-file reads).
- Default inert build (shipping): SHA-256 `b550e64ff8b782fe837f0dfd1aa613146c77ff3` (87,584 B), boot re-verified clean after the overlay work.

### Mod metadata discovery (Milestone 6, verified)

The native PRX now discovers mods at runtime from `/app0/mods` (on disk `D:\PS4\ShadPS4\CUSA04013\mods`), parses each `mod.json`, logs the metadata, and registers the mod's ConVars through the verified engine constructor. It uses the standard PS4 libc (`libSceLibcInternal`) for `opendir`/`open`/`read`, and a self-contained JSON parser (no unknown external ABI).

Staging: `New-Stage2R2Overlay.ps1` now publishes each mod's `mod.json` to `CUSA04013\mods\<name>\mod.json` and writes `CUSA04013\mods\.ns_mod_manifest` (fallback list if `opendir` is unavailable). Both are tracked in `.ns_overlay_staged.json` so `-Clean` removes them.

Verified run (gated build SHA-256 `1073a855c347f586404a3a98f2cf106db93235`, 105,440 B, `-EnableM6ModMetadata`): `opendir /app0/mods` succeeded and discovered 2 mods; both engine profile preimage gates passed; then:

    [NorthstarPS4] mod Northstar.Client version=1.31.10 priority=0 init=cl_northstar_client_init.nut description=Various ui and client changes...
    [NorthstarPS4] mod Northstar.Client convars=12 scripts=18
    [NorthstarPS4] mod Northstar.Custom version=1.31.10 priority=1 init= description=Custom content for Northstar...
    [NorthstarPS4] mod Northstar.Custom convars=6 scripts=65
    [NorthstarPS4] mod metadata probe complete convarsRegistered=18

All 18 ConVars registered with `success=1` (FindVar returned exactly the object allocated by the PRX), covering `allow_mod_auto_download`, `filter_*`, `modlist_*`, `serverlist_remove_colors`, `ns_disallowed_tacticals`, `ns_disallowed_weapons`, `ns_force_melee`, `ns_show_event_models`, and more. Boot reached `module tracker complete engine=1 client=1` with no crash; the FS overlay probe still passes.

Notes and safety gates:

- `ARCHIVE_PLAYERPROFILE` flags are parsed and logged but registered with engine flags `0` until the PS4 flag bit semantics are verified.
- shadPS4 stubs the `_fcntl` libc call (returns zero) but `opendir`/`read`/`close` are fully functional; the probe does not use `fcntl`.
- The metadata code is build-time gated behind `-EnableM6ModMetadata`. The default inert build is unchanged and byte-identical to the prior release (SHA-256 `b550e64ff8b782fe837f0dfd1aa613146c77ff3`, 87,584 B).
- Mod `Scripts` arrays are parsed and counted (the script manifest) but are not yet compiled into the engine; script injection and enable/disable controls remain.

### Runtime script injection (Milestone 6, verified 2026-08-07)

`-EnableM6ScriptInject` (nested inside `-EnableM6ScriptProbe`) calls the
client's own boot-time script loader, `CompileList(owner, ctx, paths, count)`
at client VA `0x3153f0`, from the `NorthstarPS4` diagnostic thread to compile
an extra `.gnut` file after the menu has already loaded. This is the runtime
counterpart to the scripts.rson manifest overlay (previous section): the
manifest approach requires a rebuild of the staged overlay before boot;
`CompileList` would let the native PRX add mod scripts dynamically, e.g. after
discovering them under `/app0/mods` at runtime.

**Prior crash (run 20260806-214046).** The first attempt called `CompileList`
unconditionally once the UI script count had been stable for 5 seconds and
crashed: `Unhandled Exception code 0xc0000005 at 0x0` immediately after the
engine's own file-system layer opened and closed
`/app0/r2/scripts/vscripts/ns_m6_probe.gnut`. Static disassembly of client VA
`0x3153f0` (`tools/python-packages` has `capstone`/`lief` for this) showed
`CompileList` makes a virtual call once per path, before touching the caller's
buffers:

    mov rdi, [rip+0x819c59]   ; -> client VA 0xb2f0d0, a global pointer-to-object
    mov esi, 0xffffffff       ; args consistent with a lock/wait-style call
    xor edx, edx
    mov rax, [rdi]            ; rax = object's vtable
    call [rax+0x58]           ; virtual call, slot 11

RIP=0x0 at the fault means this call read a literal null QWORD from
`vtable+0x58` and jumped to it. The object itself is populated well before
this point (152 real UI scripts had already compiled through the same
`CompileList` by the time our probe's settle loop finished), so the null was
in the vtable slot specifically, not the object pointer.

**Fix and result.** `ProbeUiScriptSystem` in `native/stage2/src/runtime.cpp`
now validates `CompileList`'s 16-byte prologue preimage
(`kClientCompileListVa` / `kClientCompileListPreimage`) and walks
`object -> vtable -> vtable[+0x58]` (`kClientCompileListGatePtrVa` = client VA
`0xb2f0d0`, `kClientCompileListGateVtableSlot` = `0x58`) before calling
`CompileList`, logging the chain and refusing (no crash) if any link is null.
It also validates `FindUiFunction`'s preimage before use, which the original
code omitted. A safe, read-only `FindUiFunction` check for `NSM6ProbeMarker`
now always runs first, independent of whether the `CompileList` gate passes,
so the manifest-overlay question below is answered even when the gate
refuses.

Three consecutive runs on 2026-08-07 (PRX SHA-256
`9a396bd210a318d02686dadf3ec3199b86bcd4051df05ab6d57750980b135354`, 104,160 B)
all found the gate ready (`object`/`vtable`/`slot58` all non-null) and called
`CompileList` successfully with no crash:

    [NorthstarPS4] M6 script inject compileList gate prologue=1 object=0x816099190 vtable=0x811665690 slot58=0x811481f40 ready=1
    [NorthstarPS4] M6 script inject pool staged static=0x8091080b0
    [NorthstarPS4] M6 script inject ctx=2 count=1 result=1 owner=0x227182400 before=152 after=153

`before=152 after=153` confirms the UI script pool genuinely grew by one
entry; the kernel FS log for the same window shows the engine's own file
layer opening and closing `/app0/r2/scripts/vscripts/ns_m6_probe.gnut`
through the normal search order (`platform` miss, `scripts` miss, `r2` hit),
i.e. the file was read via the already-verified overlay mechanism, not a
side channel. One run was kept alive afterward (`-KeepRunning`) and polled
for 25+ seconds post-injection with no `FatalError`/`Critical` lines and
normal `RenderThread` activity, so the extra compiled script does not
destabilize the menu.

The original "wrong thread" hypothesis (calling from `NorthstarPS4` instead
of whatever thread the engine used for its own boot-time compiles) is not
supported by this result: all three successful calls still ran on the
`NorthstarPS4` thread. The more consistent explanation is that the gated
object/vtable simply were not fully constructed yet in the one crashing run
that predates the gate; gating on the exact dependency chain rather than
assuming readiness fixes it either way.

**Caveat — `FindUiFunction` is not a working verification path.** In every
run, including the successful `CompileList` calls,
`FindUiFunction(uiVm, "ui_main_menu", 0, 0)` — a baseline that unquestionably
exists — also returned null. This matches the M5 finding recorded in
`work/stage2/ui-native-investigation.md` (`0x6799f0` resolves the runUiScript
name registry, not the general script/global namespace) and confirms it is
not a reliable way to confirm a compiled script's contents ran or its
functions are callable. `before`/`after` script counts and the kernel FS
open/close log are the only confirmed signals right now that a manifest- or
`CompileList`-loaded script was genuinely compiled.

**Status:** `CompileList` injection is no longer a crash risk (gated,
verified 3/3), and it is a proven way to grow the UI script pool at runtime
by one file per call. It has not yet been proven to make the new script's
functions *callable* from other scripts or natives — that requires either a
working name-resolution probe (not `FindUiFunction`) or a script that calls
itself at load time (as `NSM6ProbeMarker` deliberately does not: it defines a
function but nothing invokes it). The default inert PRX
(SHA-256 `b550e64ff8b782fe837f0dfd1aa613146c77ff3342591999f2fe2418c8b6a2ad`,
87,584 B) was rebuilt, deployed, and boot re-verified clean afterward (run
`work/stage2/iterations/20260807-141335`).

### The scripts.rson manifest overlay does not actually take effect (correction, 2026-08-07)

The "Next safe action" originally written here proposed adding a top-level
`printt()` call to a manifest-loaded script and re-running to prove
manifest-driven loading executes. That test was run (`ns_m6_probe.gnut`
edited to call `NSM6ProbeMarker()` at file scope instead of only defining it;
regenerated via `Merge-Stage2ScriptsRson.ps1 -Probe`) and it disproved the
premise instead: **the r2-overlaid `scripts.rson` is never read.** The
`scripts.rson` opened through the engine's normal `GAME`-pathID file
interface (both by our own `ProbeUiScriptSystem` FS probe and, inferably, by
the engine's own boot-time loader) is **25,511 bytes** — an exact byte match
for `tools/vanilla-scripts/scripts/vscripts/scripts.rson`, the *unmerged*
template — not the 31,630-byte merged file staged at
`r2\scripts\vscripts\scripts.rson`. The probe's marker/`ns_m6_probe.gnut`
substring scan over the full read buffer found zero hits
(`markerM6=0 probeGnut=0`), and unlike every loose-file open in this project
(e.g. `ns_m6_probe.gnut` itself, opened moments later in the same run), there
is **no `[Kernel.Fs] open: path = ...` log line for `scripts.rson` at all** —
it is being served by a different code path than the direct loose-file opens,
consistent with VPK-internal content rather than the `r2` overlay.

This contradicts the Milestone 6 filesystem-overlay claim above ("The engine
searches [r2] before its other GAME roots ... platform"). That claim was
verified against four files that only exist as new mod content in `r2`, with
no vanilla counterpart at the same logical path in any VPK/`platform` root —
so a same-path collision was never actually tested. `scripts.rson` is the
first same-path collision tested (it legitimately exists in the base game
build too), and here `platform`/VPK content won. The overlay's search-order
priority likely only applies to genuinely new paths; take the "r2 shadows
VPK content" claim as unverified for any path that also exists in the base
game until a same-path collision is retested directly (e.g. stage a modified
`autoexec_ns_client.cfg` variant with a unique marker string over a file that
also ships in the VPK, not a mod-only path).

**Practical consequence for Milestone 6:** the scripts.rson-manifest route
(`Merge-Stage2ScriptsRson.ps1`) does not currently cause the engine to load
mod scripts, because `scripts.rson` itself does not get overridden. The
verified, working way to get a mod script compiled at runtime remains the
direct `CompileList(owner, ctx, paths, count)` call proven above (which reads
the target script by an explicit path the engine resolves through
`platform`/`scripts`/`r2` in order, correctly falling through to `r2` when
the path is new — this fallthrough *is* proven to work, since
`ns_m6_probe.gnut` has no `platform` counterpart). Runtime `CompileList`
calls per discovered mod script (driven by the already-working
`/app0/mods/<name>/mod.json` `Scripts` array parsing) is the next concrete
step, not the scripts.rson merge.

**Update:** this was tried (run 20260807-143020) and the compiler rejected
it: `FatalError: ns_m6_probe.gnut: UI SCRIPT COMPILE ERROR: Global variable
definition is followed by "(". Did you forget to declare it as a
"function"?`. Bare statement calls are not legal at file scope in this UI
script compiler — only function/const/global-variable declarations are,
matching the older `Only functions, consts, or global variables are allowed
at file scope` error from run 20260806-205502. This did not crash the
process (no `Unhandled Exception`/`Critical` line, matching the harness's
`FailurePattern`); it hung at the `FatalError` until the harness's own
timeout, and was cleaned up normally. `ns_m6_probe.gnut`'s template in
`Merge-Stage2ScriptsRson.ps1` has been reverted to the function-definition
form (no top-level call) and re-staged; the default inert PRX
(SHA-256 `b550e64ff8b782fe837f0dfd1aa613146c77ff3342591999f2fe2418c8b6a2ad`)
was rebuilt, deployed, and boot re-verified clean afterward (run
`work/stage2/iterations/20260807-143727`).

Next safe action: prove load-time execution through a mechanism this
compiler actually allows at file scope — most likely calling the mod's
`InitScript` (or an equivalent conventionally-named function) via a real
engine call site the way `RunUIScript` already does for the built-in menu
scripts, rather than a bare statement. The mod metadata code already parses
`InitScript` per mod; the open question is which engine call actually invokes
it and whether that call site can be reached safely (read-only trace first,
per the safety rules below), or whether `CompileList` plus a native-closure
callback (the mechanism verified working in Milestone 5) is the more
practical path for now.

### Wiring mod `Scripts[]` into `CompileList` (2026-08-07)

`ModInfo` (`native/stage2/src/runtime.cpp`) now stores each mod's actual
`Scripts[]` entries whose `RunOn` is exactly `"UI"` (`ModInfo::uiScripts`),
not just a count. `ProbeModMetadata` collects them, mod by mod, into a shared
static buffer (`gCollectedUiScripts`/`gCollectedUiScriptCount`, declared
where both `NORTHSTAR_PS4_ENABLE_M6_MOD_METADATA` and
`NORTHSTAR_PS4_ENABLE_M6_SCRIPT_INJECT` are defined) that `ProbeUiScriptSystem`
reads later in the same boot. `InitScript` is collected first per mod, ahead
of that mod's other UI scripts, because it can declare types the other
scripts reference (see below).

**This collection is always safe and always runs** when both flags above are
set — it only copies strings and logs (`M6 script inject: N mod UI script(s)
collected`). **Actually compiling the collected list through `CompileList` is
a separate, additionally-gated experiment** behind
`-EnableM6ScriptInjectFromMods` (`NORTHSTAR_PS4_ENABLE_M6_SCRIPT_INJECT_FROM_MODS`),
because it does not yet work safely:

- `cl_northstar_client_init.nut` (Northstar.Client's `InitScript`) alone
  compiles cleanly via `CompileList` (`before=152 after=153`, run
  `20260807-154149`).
- Adding `ui/menu_ns_modmenu.nut` (RunOn `"UI"`, the mod-list UI panel) to the
  **same** call — just those two files — reproducibly crashes with a *real*
  (non-null) faulting address, not the null-vtable case the `CompileList`
  gate already handles. Reproduced twice (runs `20260807-153420` with 16
  files and `20260807-154245` isolated to these exact 2 files); both crashed
  at the identical VA offset from `client.prx`'s base (`0x861269` in the
  second run — the process differed by exactly the client base shift between
  runs).
- Static disassembly of client VA `0x861269` (in `client.prx`, hash
  `abc6efd2125a2a58d32ad9d23d245a03fd3213a763e407712a575f0bc04e879b`):

      0x86124b: mov rax, [r15+0x2d8]
      0x861252: mov ecx, [rsi+0x40c0]
      0x861258: mov rax, [rax+0x50]        ; matches the known uiVm->internal-VM +0x50 pattern
      0x86125c: mov rbx, [rax+0x40a0]      ; a VM-internal table this project has not seen before
      0x861263: mov eax, [rsi+0x40c4]
      0x861269: mov r13d, [rbx+0x38]       ; <== faults: rbx is invalid

  `rax+0x50` matches the already-verified `uiVmToSqVmOffset` pattern
  (`config/stage2-offsets-CUSA04013.json`), so this is operating on the same
  internal Squirrel VM object the M5 UI-native work uses — but reading a
  *different* internal table, at offset `0x40a0`, that has not been profiled.
  `menu_ns_modmenu.nut` uses `ModInfo` (declared in the `InitScript`) as a
  **typed function parameter**; the leading hypothesis is that this crash
  site is a type/struct-registry lookup for that typed parameter, and
  whatever populates `internal_vm+0x40a0` for the engine's own boot-time
  compile pass has not been (and perhaps cannot be, this late) reproduced by
  our late injection.
- The default combined build (`-EnableM6ModMetadata -EnableM6ScriptProbe
  -EnableM6ScriptInject`, **without** `-EnableM6ScriptInjectFromMods`) was
  re-verified safe after adding this gate: it collects and logs 16 mod UI
  scripts, explicitly declines to inject them, and falls back to the original
  harmless `ns_m6_probe.gnut` probe (`before=152 after=153`, no crash, run
  `20260807-154746`). The default inert PRX
  (SHA-256 `b550e64ff8b782fe837f0dfd1aa613146c77ff3342591999f2fe2418c8b6a2ad`)
  was restored and boot re-verified clean afterward (run
  `20260807-154833`).

**Do not enable `-EnableM6ScriptInjectFromMods`** outside a deliberate,
isolated experiment until `internal_vm+0x40a0` is understood — it is known to
crash on at least one real mod script. Next step: profile that table the same
way `t40d0`/`t4120`/`t41b0` were profiled for Milestone 5 (dump its layout
when the engine's own boot-time compile populates it, before attempting to
read it from an injected compile), or find a mod UI script with no typed
struct/class parameters to test whether the crash is specific to typed
parameters or to something else `menu_ns_modmenu.nut` does.

### Profiling internal_vm+0x40a0 (2026-08-07): the table itself is healthy

`ProbeUiVm` now unconditionally (no build flag needed — pure reads, always
safe) dumps `internal_vm+0x4090..0x40b0` and, if `+0x40a0` looks like a
plausible pointer, the first 8 qwords it points to. Verified on a clean boot
(no injection attempted, run `20260807-155651`):

    UI VM internal+0x4090=0
    UI VM internal+0x4098=0x8000040
    UI VM internal+0x40a0=0x226ea2480
    UI VM internal+0x40a8=0x1000001
    UI VM internal+0x40b0=0
    UI VM internal+0x40a0[+0x0]=0x81ea238a0
    UI VM internal+0x40a0[+0x8]=0x1
    UI VM internal+0x40a0[+0x10]=0
    UI VM internal+0x40a0[+0x18]=0x2270b7480
    UI VM internal+0x40a0[+0x20]=0x2270b72f0
    UI VM internal+0x40a0[+0x28]=0x202606520
    UI VM internal+0x40a0[+0x30]=0x227190640
    UI VM internal+0x40a0[+0x38]=0x400000003

`+0x40a0` is a valid, non-null pointer, and the qword at that table's own
`+0x38` — the exact offset the crash site reads — is `0x400000003`, a
plausible packed value, not null and not garbage. **This means "the table is
unpopulated" is not the explanation for the crash.**

Re-reading the crash disassembly with that in mind:

    0x86124b: mov rax, [r15+0x2d8]
    0x861252: mov ecx, [rsi+0x40c0]
    0x861258: mov rax, [rax+0x50]
    0x86125c: mov rbx, [rax+0x40a0]
    0x861263: mov eax, [rsi+0x40c4]
    0x861269: mov r13d, [rbx+0x38]   ; faults

The assumption that `[r15+0x2d8] -> +0x50` reaches the *same* internal VM
object as `uiVm+0x50` was based only on the `+0x50` offset matching the
already-verified `uiVmToSqVmOffset` pattern — a single coincidental-offset
match, not a proven identity. `r15` here is whatever object this compiler
subroutine received as context, most likely something specific to compiling
a *typed parameter declaration* (`ModInfo modInfo`), not necessarily our
probed UI VM at all. Since our own UI VM's `+0x40a0` table is confirmed
healthy, the more likely explanation is that `r15` (or the chain from it) is
a different, less-initialized object — e.g. a compile-time type/class
resolution context that the engine's own boot-time compile pass sets up
before compiling any file that uses typed struct parameters, and that our
late injection never triggers the setup of.

This does not yet identify what `r15` actually is. Confirming it requires
either a debugger attached to shadPS4 at the fault (not available through
the log-based iteration harness) or tracing backward from the crash site
through the calling function(s) statically. Until then,
`-EnableM6ScriptInjectFromMods` remains unsafe for scripts with typed struct
parameters; scripts without them (like `cl_northstar_client_init.nut`) are
still verified safe.

The `internal+0x4090..0x40b0` dump above is unconditional (no build flag —
pure reads, safe on every build), so it changed the default inert PRX's
bytes and hash. The new default baseline is
SHA-256 `f640b1dce71a36c90670434239ab74de5c7ec5de907fa3cf17a4f1e6922445b2`
(87,584 B), boot-verified clean (run `20260807-155651` and again after the
`-EnableTeamChangesConVar` re-verification below, run `20260807-160202`).

