# Native port — technical notes

This is the detailed, chronological technical log behind the native runtime port: exact hashes, virtual addresses, byte preimages, run IDs, and the reasoning behind each fix. For current status and what to do next, start at [GOALS.md](GOALS.md) instead — this file is the evidence trail, not the status tracker. Section headings below still say "Milestone N" / "Stage 1" in places; read those as historical labels (they map onto the Goals in GOALS.md) rather than an active framing.

Entries run from the first boot (2026-08-14) to the present, oldest first, and a later entry can correct an earlier one (look for **Correction**). All results are Titanfall 2 PS4 CUSA04013, build `R2PS4_r2dlc11_598_CL297590_2017_12_05_12_36_PM` (PC counterpart `Titanfall2_v2_0_11_0`), under shadPS4; the emulator revision matters (see GOALS.md for the build to use).

## First result (2026-08-14)

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
| Current northstar_ps4.prx (default, inert) | f640b1dce71a36c90670434239ab74de5c7ec5de907fa3cf17a4f1e6922445b2 (2026-08-07; older hashes below reflect the default at the time each section was written). Localise-enabled build 2026-08-14: `9f03837a338c92741a27a400e71b539c39b5d33164f604ab192751a467d8fc11` (105,440 B, `-EnableM6Localise -EnableM6FsOverlay -EnableM6ModMetadata`) |

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

The project-local launcher/link.x defines:

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

Verification harness (`ProbeFilesystemInterface` in `launcher\src\runtime.cpp`, retained flag-guarded behind `-EnableM6FsOverlay`):

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

**Fix and result.** `ProbeUiScriptSystem` in `launcher/src/runtime.cpp`
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

`ModInfo` (`launcher/src/runtime.cpp`) now stores each mod's actual
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

### Debugger setup (2026-08-07): shadPS4 is native x86-64, cdb works directly

shadPS4 runs PS4 game code (also x86-64) directly on the host CPU — it does
not JIT-translate CPU instructions, only virtualizes OS/kernel calls. That
means guest crash addresses (like the `client.prx` VA above) are real host
virtual addresses in the shadPS4 process, and a standard Windows debugger
attached to `shadPS4.exe` can set breakpoints and inspect registers there
directly. `shadPS4.exe --help` also confirms a `--wait-for-debugger` flag
exists, though it wasn't needed here.

**Tooling installed this session:**
- Modern WinDbg (`winget install --id Microsoft.WinDbg`) is GUI-only
  (`DbgX.Shell.exe`) with no console debugger binary — not usable for
  scripted, non-interactive sessions.
- The classic console debugger (`cdb.exe`) comes from the "Debugging Tools
  for Windows" standalone feature of the Windows SDK, installed via the
  official web installer with only that feature selected (no full SDK, no
  admin GUI):

      winsdksetup.exe /features OptionId.WindowsDesktopDebuggers /quiet /norestart

  (`winsdksetup.exe` is downloaded from `download.microsoft.com`; winget's
  `Microsoft.WindowsSDK.10.0.26100` package can also fetch it, but silently
  no-ops with exit code 1000 if it thinks a same-version SDK component is
  already registered — running the downloaded `winsdksetup.exe` directly
  with the command above is more reliable.) Installs to
  `C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe`.

**shadPS4 uses guard-page-based memory virtualization** — its own
`Kernel.Vmm` reserve/allocate/map logging is the tell. This means first-chance
access violations are frequent and *expected* (the guest's memory manager
handles them internally). `sxe av` (break on every first-chance AV) is far
too broad — it stops at the very first routine, harmless one within seconds
of launch. **Use `sxd av` instead** (disable first-chance stop for AV, let
shadPS4's own handler process it) and let cdb's normal, non-optional
second-chance stop catch only the genuinely unhandled fault — which is
exactly the `[NorthstarPS4] ... SignalHandler: Unhandled Exception` case
already used as this project's `FailurePattern`.

Minimal working recipe (adjust the PRX build/deploy step and command line to
match whatever's being investigated):

    cdb.exe -o -g -G -cf <commandfile> -logo <outputfile> ^
        "<shadPS4.exe path>" --game "<eboot.bin path>" --log-append

commandfile contents:

    sxd av
    g
    r
    k
    q

(`-o` also debugs child processes if any are spawned; `-g`/`-G` skip the
initial process-start and final process-exit breakpoints so the script runs
to the real stop unattended.) `-logo <file>` mirrors all output to a file
readable after the (potentially long, GUI-module-heavy) session completes.

To break at a specific `client.prx`/`engine.prx` offset instead of waiting
for the crash: read the run's actual base from the shad_log.txt line
`target module=client.sprx ... segment[0] address=...` (varies per run, must
never be hard-coded — same rule as in the safety rules below) and compute
`bp <base + offset>` before the `sxd av`/`g` pair. A `k` (stack) at a raw
address breakpoint inside manually-mapped guest code shows only raw return
addresses, not symbol names — expected, still useful for register/memory
state at that exact point.

### Live crash analysis: r15 is `vmPool`, not the UI VM

With cdb attached (recipe above, `-EnableM6ModMetadata -EnableM6ScriptProbe
-EnableM6ScriptInject -EnableM6ScriptInjectFromMods`), the second-chance stop
landed at exactly the predicted instruction and confirmed the crash
mechanically for the first time (previously only inferred from shadPS4's own
log line and static disassembly):

    (55f0.xxxx): Access violation - code c0000005 (!!! second chance !!!)
    00000008`1fc1d269 448b6b38   mov r13d,dword ptr [rbx+38h] ds:00000000`00000038=????????
    rax=0000000000000100 rbx=0000000000000000 rcx=00000000000000af
    rdx=000000022ac58200 rsi=0000000202608d40 rdi=000000022ac619a0
    r15=0000000227182700

`ds:00000000'00000038` proves `rbx` is exactly **null** (not merely
"invalid" — the access is address `0x38`, i.e. `0 + 0x38`). Critically,
**`r15` (`0x227182700`) is not our probed UI VM at all — it is the exact
`vmPool` value this project's own code already logs** (`vmObj+0x4218`, e.g.
`M6 script inject pool ... vmPool=0x227182700` in the same run's
`shad_log.txt`). The earlier hypothesis (`r15+0x2d8 -> +0x50` reaches the
*same* `internal_vm` this project profiled at `internal_vm+0x40a0`, and that
table is simply unpopulated) was corrected, then further refined by a second
pass:

By that point in the instruction sequence `rax` has already been overwritten
by a later `mov eax,[rsi+0x40c4]`, so `dq rax` at the crash itself no longer
shows the `+0x50`-dereferenced pointer — a breakpoint set *before* that
overwrite (`client_base + 0x861263`, right after `rbx` is assigned but before
`eax` is clobbered) is needed to see it live. Across 6 sampled hits of that
breakpoint in one run (before the real crash, which needs more hits to
reach), `rax == rsi` every time, and both matched a stable, healthy,
**non-null** value — `0x226ea2480` in that run, which is the *exact* value
this project's own read-only `internal_vm+0x40a0` profile already found
during a plain, non-injecting boot (see the profiling section above). So:

- `rsi` genuinely is `internal_vm` (the `+0x50` chain from `vmPool+0x2d8`
  really does resolve to the same object our UI-native work has profiled all
  along — the original coincidental-offset concern was unfounded here).
- `internal_vm+0x40a0` (`rbx` in the crash) is not broadly broken — it was
  healthy across at least 6 consecutive calls to this code path in the same
  compile. `rcx` (read from `internal_vm+0x40c0` earlier in the sequence)
  incremented 3→4→5→6→7→8 across those hits, consistent with a per-call
  counter, not a table identity check.
- The null result therefore happens on a **specific, later call** in the
  same compile pass — most plausibly the one resolving `ModInfo` specifically
  (the one new, mod-defined type `ui/menu_ns_modmenu.nut` references), not a
  structurally-unpopulated table. A miss/not-found result for that one
  lookup is returned as null and used without a null check, rather than the
  whole table being absent.

**Status:** root cause narrowed to "a specific type lookup misses and the
miss isn't checked," not "the table doesn't exist yet." Not yet conclusively
identified *why* that one lookup misses. Next step for whoever picks this
up: repeat the `client_base + 0x861263` breakpoint recipe above with either
a much higher iteration count or (better) a conditional breakpoint that only
stops when `rbx == 0`, e.g.

    bp <addr> ".if (poi(rax+40a0) = 0) {} .else {g}"

(untested exact syntax — verify interactively) to land exactly on the
failing call and inspect what `internal_vm+0x40c0`/`+0x40c4` (the
counter/capacity pair read around it) look like right before the miss, and
what makes that specific call different from the healthy ones. Still gated
behind `-EnableM6ScriptInjectFromMods`; do not remove that gate until this is
resolved. Default inert PRX restored and boot re-verified clean after this
session (`work/stage2/iterations/20260807-171205`).

### Goal 8 connectivity test: no loose `.cfg` files are read at boot (2026-08-07)

Attempted the "test a real insecure server connection end-to-end" next step.
A local dedicated server was started (`D:\Games\Titanfall2\NorthstarLauncher.exe
-dedicated -multiple`, using the machine's existing `ns_startup_args_dedi.txt`
which already had `+ns_auth_allow_insecure 1` and `+net_usesocketsforloopback 1`
set from prior manual testing) and confirmed listening on UDP `37015`/`37005`.

To trigger the client's `connect <ip>:<port>` without any GUI automation
available (no desktop input tool for the shadPS4 window; only the in-app
Browser tools exist, which don't apply to a native emulator window), two
loose-`.cfg`-file injection attempts were tried, both negative:

1. Appended `connect 127.0.0.1:37015` to the `r2`-staged
   `cfg/autoexec_ns_client.cfg` (Northstar's own client-init cfg, already
   proven readable via the M6 filesystem overlay). No effect — this file is
   normally `exec`'d by Northstar's own C++ injection on PC, which this
   project has not reimplemented; the retail engine has no reason to know
   about a Northstar-namespaced filename on its own.
2. Appended the same line to the **vanilla** `cfg/autoexec_client.cfg`
   (rebuilt and deployed through the proven Stage 1 VPK pipeline, i.e. really
   installed, not just r2-overlaid) — the standard Source-engine
   auto-executed startup script on PC. Also no effect.

For (2), the kernel filesystem log for the entire boot (module load through
reaching the main menu) was checked line by line: the **only** `.cfg` file
ever opened is `/savedata0/profile.cfg` (a save-data file, not a boot
script). No `autoexec_client.cfg`, `startup.cfg`, or any other loose `.cfg`
is opened at all. This strongly suggests the retail PS4 build does not
support the PC/dev-console convention of executing loose cfg files as
startup commands at all — plausibly because the feature exists for a
user-facing developer console that isn't present in a retail, non-dev SKU.
**Loose-cfg-file injection is not a viable way to trigger `connect` (or any
other console command) on this platform; do not retry this approach.**

Both temporary `.cfg` edits were reverted and the clean Stage 1 VPKs
(`work/stage1/sparse`) and default inert native PRX were rebuilt, redeployed,
and boot re-verified clean afterward.

**Implication for the actual next step:** triggering `connect` (or any other
console command) reliably requires either (a) real GUI/input automation
against the shadPS4 window — outside this project's current tooling — or
(b) calling the engine's console-command execution path directly from the
native PRX (e.g. locating and calling `Cbuf_AddText`/`Cbuf_Execute` or
equivalent in `engine.prx`, the same way `CompileList` was already located
and safely called). (b) is more in keeping with this project's established
methodology and doesn't depend on external tooling, but is a new,
non-trivial reverse-engineering task (locate candidate functions, verify
with hash-locked preimages, prove the calling convention with a harmless
command before attempting `connect`) that hasn't been started.

### The user drove it manually: two missing Northstar ConVars found and fixed (2026-08-07)

With (a) above — the user has direct interactive access to the shadPS4
window and can drive the in-game direct-connect menu by hand, which this
project's own tooling cannot do. This unblocked real testing immediately.

**First attempt** hit a blocking UI dialog: `ui/menu_ingame.nut #699 [UI]
ConVar ns_allow_team_change is not valid`. Reaching `menu_ingame.nut` (the
in-game/pause menu) at all is itself a good sign — it means the connection
attempt got past the main menu and direct-connect flow into an actual game
session. Checked the exact source directly: `grep`ping
`work/stage1/sparse/frontend/scripts/vscripts/ui/menu_ingame.nut:699` shows
`Hud_SetLocked( button, !GetConVarBool( "ns_allow_team_change" ) )` — **the
real shipped script uses the singular `ns_allow_team_change`**, not the
plural `ns_allow_team_changes` this project had been registering (a
carry-over guess from a legacy identifier no longer present in current
NorthstarLauncher source — see the original registration note above). Fixed
the name in `launcher/src/runtime.cpp` (`replace_all` across the whole
file; verified via `grep` beforehand that the misspelled string appeared
nowhere else).

**Second attempt** (after rebuilding/redeploying with the corrected name)
hit a second, different dialog: `ui/menu_main.nut #102 [UI] ConVar
ns_has_agreed_to_send_token is not valid`. To avoid another round-trip, did
a systematic sweep instead of fixing one-by-one: extracted every
`GetConVar(Bool|Int|Float|String)("...")` call across all of
`work/stage1/sparse` (the actual shipped script content, not assumptions),
cross-referenced the `ns_`-prefixed subset (Northstar's own naming
convention — the only ones actually missing so far) against (a) the 18
ConVars already registered from `Northstar.Client`/`Northstar.Custom`
`mod.json`, and (b) NorthstarLauncher's natively-registered ConVars
(`grep -rn 'new ConVar('` across `tools/NorthstarLauncher-reference`, since
this project's own runtime.cpp only ports a couple of these). Every
`ns_`-prefixed name shipped in this build's UI content was already covered
**except** `ns_has_agreed_to_send_token` — confirmed against
`primedev/client/clientauthhooks.cpp`: an int ConVar, default `"0"`
(`NOT_DECIDED_TO_SEND_TOKEN`; `1`=agreed, `2`=disagreed),
`FCVAR_ARCHIVE_PLAYERPROFILE` on PC. One more `ns_`-prefixed reference,
`ns_auth_allow_insecure` in `ui/atlas_auth.nut`, was checked and found
**not** currently reachable: `AtlasAuthDialog()` (the only caller) has no
call site anywhere in `work/stage1/sparse` — Stage 1's compatibility patch
already keeps this Northstar-native-auth-dependent code path unreached, so
it doesn't need a registration yet.

**Fix:** both `ns_allow_team_change` and `ns_has_agreed_to_send_token` are
now registered **unconditionally** in `runtime.cpp` — no longer gated behind
`-EnableTeamChangesConVar`/experimental flags, because live testing proved
them required for basic UI functionality (a blocking error dialog without
them), not experimental. The `constructor`/preimage-gate variables they both
depend on were promoted out of their old `#if defined(...)` guard to match
(previously only compiled when a diagnostic/experimental flag was set).
`-EnableTeamChangesConVar` is still accepted as a build flag for backward
compatibility but is now a no-op. Verified: default build (no flags) now
registers both with `success=1`
(`[NorthstarPS4] team changes convar registration ... success=1`,
`[NorthstarPS4] ns_has_agreed_to_send_token convar registration ...
success=1`) and boots clean to the main menu. New default inert PRX
SHA-256 `89eedb52fb85a9cee9cf71392d70c3cd46c0326191895cfb3629d4184fc2f981`
(87,584 B).

**Also fixed in passing:** while investigating, found the currently
installed VPKs no longer matched this project's expected Stage 1 build (the
user had separately tested VPK modding and restored some files, which
turned out to be a source of the very Squirrel errors being chased —
running with genuinely vanilla, unpatched frontend scripts reproduces
exactly this class of "missing Northstar ConVar/API" error, which is
precisely what the Stage 1 compatibility patch exists to avoid). Rebuilt and
redeployed the correct Stage 1 VPKs via the existing
`Build-AndDeployStage1Vpks.ps1` pipeline; all 6 hashes now match
`dist/stage1-sparse/manifest.csv` again.


## Dialog UI accepts zero input; worked around by pre-agreeing the token dialog (2026-08-07)

After the `ns_allow_team_change`/`ns_has_agreed_to_send_token` ConVar fixes
above got the user past both blocking-error dialogs, the user drove the
in-game menu (real hardware input to the shadPS4 window, no automation
tooling — same manual-driving arrangement as before) and reached
`NorthstarMasterServerAuthDialog()` — the "Yes"/"No" consent dialog
`ui/menu_main.nut` opens on first boot when `ns_has_agreed_to_send_token`
is unset. No input of any kind advanced it:

- Arrow keys, WASD, `B`/`N` (shadPS4's default keyboard-to-pad mapping:
  `b`→circle, `n`→cross, `w/a/s/d`→left-stick axes, arrows→dpad, per
  `%APPDATA%/shadPS4/input_config/default.ini`) — no response.
- Real controller circle/cross — no response.
- Mouse hover over the buttons (no highlight) and mouse click — no response.
- The `` ` `` (backtick) key, bound to `toggleconsole` via
  `autoexec_ns_client.cfg`'s `exec` — no response either.

That last check was the important one: `toggleconsole` is not a
Northstar-PC-only feature that would need native reimplementation on PS4
(there is no `RegisterConCommand`/`toggleconsole` anywhere in
`launcher/src/runtime.cpp`, confirmed by grep) — it's a genuine
engine-native binding. `work/stage1/extracted/frontend/cfg/config_default_console.cfg`
(the platform-console/`CONSOLE_PROG` default bind file, unmodified, not a
Stage 1 patch) contains the exact same `bind "`" "toggleconsole"` plus
`bind "START" "ingamemenu_activate"` — i.e. this is the stock retail
binding for console builds, and it not firing means **no bound input
command was executing at all**, not merely a UI-focus problem local to the
dialog.

Traced the whole dialog/focus call chain to rule out a script-level fix
before concluding this needs native/live investigation:

- `ui/menu_main.nut`'s `NorthstarMasterServerAuthDialog()` uses the same
  generic `DialogData`/`AddDialogButton`/`OpenDialog` API as every other
  dialog in the game (leave-match confirm, connecting dialog, generic
  server-callback dialogs, etc.) — defined in
  `work/stage1/extracted/frontend/scripts/vscripts/ui/menu_dialog.nut`.
- Diffed `menu_dialog.nut`, `_menus.nut`, and
  `resource/ui/menus/dialog.menu` against `work/stage1/extracted` (the
  unmodified retail baseline): **byte-identical**. None of these are Stage 1
  patch targets — this is 100% vanilla code, unmodified since the PC-derived
  original.
- `dialog.menu` already has correct `tabPosition`/nav wiring: `Button0` has
  `tabPosition 1`, and `Button0`–`Button3` all have `navUp`/`navDown` wired
  to each other in a ring — this is the same lead the user used to fix an
  analogous issue in their separate Direct-Connect-Menu mod (a from-scratch
  custom `.menu` file that had *no* `tabPosition` anywhere, so the engine's
  focus-picker had nothing to grab). Here the target already exists and is
  already tagged, so adding `tabPosition` isn't an available fix.
- `OpenDialog()` → `AdvanceMenu(menu)` → `OpenMenuWrapper(menu, true)` →
  `FocusDefault(menu)` → `FocusDefaultMenuItem(menu)`. `FocusDefaultMenuItem`
  has **no Squirrel definition anywhere** in the extracted script tree
  (confirmed by grep) — it's a native (C++) engine call baked into
  `client.prx`, presumably the thing that actually reads `tabPosition` and
  calls `Hud_SetFocused` to establish initial input focus.
- Ruled out a theory that `ActivatePanel()` (in `menu_main.nut`'s
  `OnMainMenu_Open`, called on the underlying `MainMenuPanel` right after
  the dialog opens, since PS4 isn't `PC_PROG` and falls into the
  sign-in-state polling loop instead of returning immediately like PC does)
  steals focus back from the dialog: `ShowPanel`/`HidePanel`
  (`_tabs.nut`) only call `Hud_Show`/`Hud_Hide` plus an optional
  `showFunc`/`hideFunc` thread — no focus calls at all. Not the cause.

Conclusion: this is either (a) a native engine input-focus bug specific to
this PS4 port (`FocusDefaultMenuItem`/`Hud_SetFocused` not working, or not
being reached), or (b) input events genuinely aren't reaching the engine's
command/bind-processing layer at all yet (shadPS4 pad/keyboard HLE, or a
missing native init step). Distinguishing these needs a live cdb session
breaking on the engine's input-polling and `FocusDefaultMenuItem`-equivalent
functions — not yet done. This is the first interactive menu screen this
project has ever reached (the dialog opens automatically at boot, before
any player-driven navigation), so there's no prior evidence either way about
whether UI input works at all on this port.

**Workaround shipped, not a fix:** since this blocks all forward progress
and the real fix needs a separate live-debugging investigation,
`ns_has_agreed_to_send_token`'s registered default in `runtime.cpp` was
changed from `"0"` (`NOT_DECIDED_TO_SEND_TOKEN`) to `"1"`
(`NS_AGREED_TO_SEND_TOKEN`), so `menu_main.nut`'s
`if ( !GetConVarBool( "ns_has_agreed_to_send_token" ) )
NorthstarMasterServerAuthDialog()` gate is never true and the dialog never
opens. New default inert PRX SHA-256
`75582481bce8946f6a3d4fdbe61537953bfb7295729b1e83e91abfc34eb67c71`
(87,584 B — same size as the prior build, single default-string byte
changed). This is a real product trade-off, not just a testing shortcut:
it silently opts every player in to sending their origin token to the
Northstar masterserver, with no chance to decline. Acceptable to unblock
testing now; must be revisited once the dialog-input bug has an actual fix,
at which point this default should go back to `"0"`.

**Still open and now higher priority:** the underlying dialog-input bug is
unfixed and will block the *next* dialog the player reaches too (leave-match
confirm, connecting-dialog cancel, any error dialog) — the workaround here
only covers this one specific ConVar-gated dialog by preventing it from ever
opening, it does not fix dialogs in general.

## DirectConnectMenu registration crash, new main-menu button, and mod scaffold (2026-08-07)

With the auth dialog worked around (see above), the user clicked "Launch
Northstar" on the main menu and hit a new fatal error:

```
ui/_menus.nut #874
[UI] The index "DirectConnectMenu" does not exist
```

**Root cause:** an earlier session had already added a working direct-connect
UI flow to the Stage 1 sparse patch -- `menu_direct_connect.nut`
(`InitDirectConnectMenu`, a `ConnectButton` handler that runs
`connect <ip:port>`), `resource/ui/menus/direct_connect.menu` (the resource
file, already has `tabPosition 1` on its text entry), and the "Launch
Northstar" button's `OnPlayNSButton_Activate` in `panel_mainmenu.nut`
already called `AdvanceMenu( GetMenu( "DirectConnectMenu" ) )` -- but nobody
ever added the corresponding `AddMenu( "DirectConnectMenu", ... )` call to
`_menus.nut`'s `InitMenus()`. Every other menu in the game is registered
there (`AddMenu( "MainMenu", ... )`, `AddMenu( "LobbyMenu", ... )`, etc.);
`GetMenu()` (`_menus.nut:872-875`) just indexes `uiGlobal.menus[ menuName ]`,
so an unregistered name is a hard Squirrel index-not-found error, not a
soft null. This was presumably always broken and only surfaced now because
this is the first time input has worked well enough to actually click the
button (the dialog-input investigation above happened first).

**Fix:** added `AddMenu( "DirectConnectMenu", $"resource/ui/menus/direct_connect.menu", InitDirectConnectMenu )`
to `work/stage1/sparse/frontend/scripts/vscripts/ui/_menus.nut`'s
`InitMenus()`, right after the `MainMenu`/`EstablishUserPanel`/`MainMenuPanel`
block.

**Also added, per user request:** a second, standalone "Direct Connect"
button next to "Launch Northstar" in `panel_mainmenu.nut` (`file.directConnectButton`,
`OnDirectConnectButton_Activate`), rather than only reachable through the
Northstar button, plus a new `MENU_DIRECT_CONNECT` = "Direct Connect" key
added to `resource/northstar_client_localisation_english.txt` for its label
(English only -- the other 11 language files were left untouched). That
localisation file is UTF-16 LE with a BOM (confirmed via `xxd`); edited with
a small Python script (`io.open(..., encoding='utf-16-le')`) rather than the
normal text-edit tools, to avoid corrupting the encoding -- a plain-text
edit through a tool that assumes UTF-8 would have silently mangled every
multi-byte character in the file.

Rebuilt and redeployed via `Build-AndDeployStage1Vpks.ps1`; all 6 VPK
hashes re-verified against `dist/stage1-sparse/manifest.csv`.

### Mod scaffold: `Northstar.DirectConnect`

Per user request ("re-write it as a northstar mod"), also built a proper
Northstar-mod-shaped version of the menu logic at
`work/stage1/loose/Northstar.DirectConnect/` (mirroring the
`Northstar.Client`/`Northstar.Custom` loose-mod layout already deployed to
`D:\PS4\ShadPS4\CUSA04013\mods\`):

- `mod.json`: one script entry, `ui/menu_direct_connect.nut`, `RunOn: "UI"`,
  `UICallback.Before: "AddDirectConnectMenu"` -- the same convention
  `Northstar.Client` already uses for `ConnectWithPasswordMenu`
  (`menu_ns_connect_password.nut`) and `ModListMenu` (`menu_ns_modmenu.nut`):
  the mod's own `AddDirectConnectMenu()` function calls `AddMenu(...)`
  itself, since that's a global function any script can call, rather than
  needing to patch core `_menus.nut`.
- `scripts/vscripts/ui/menu_direct_connect.nut`: same `InitDirectConnectMenu`/
  `ConnectButton_Activate` logic as the Stage 1 patch version, plus the new
  `AddDirectConnectMenu()` entry point.
- `resource/ui/menus/direct_connect.menu`: identical copy of the working
  resource file.

Deployed to `D:\PS4\ShadPS4\CUSA04013\mods\Northstar.DirectConnect\` so
Goal 5's mod-metadata discovery can see it (should bring the discovered-mod
count from 2 to 3).

**Important: this mod is inert by default and does NOT replace the Stage 1
patch fix above.** Goal 6 (runtime mod script loading) is still gated behind
`-EnableM6ScriptInjectFromMods`, off by default, specifically because it's
unproven/risky for scripts with typed struct parameters (see the
`menu_ns_modmenu.nut`/`ModInfo` crash investigation). `menu_direct_connect.nut`
has no typed struct parameters, so it's a plausible low-risk candidate to
actually test that flag against -- but that hasn't been done. **If mod
script injection is ever enabled with both this mod and the Stage 1 patch's
`_menus.nut` registration active at the same time, `AddMenu("DirectConnectMenu", ...)`
would be called twice and likely crash/assert** -- whichever path is
adopted long-term, the other one's registration needs to be removed first.
This is the same static-patch-vs-mod tradeoff already tracked as open in
GOALS.md's "Immediate next steps" item 6.

The "Direct Connect" main-menu button itself (`panel_mainmenu.nut`) was
**not** moved into the mod -- it reaches into `panel_mainmenu.nut`'s private
`file` struct and its dynamically-built combo-button list, which isn't
something an independent mod script can hook into without a `Before`/`After`
callback wrapping `InitMainMenuPanel` itself (Northstar.Client doesn't do
this for its own menu entries either; it uses `AddMenuFooterOption` instead,
e.g. the Mods list's Y-button footer option). The button stays in the Stage 1
patch; only the menu-opening logic behind it was duplicated into the mod
scaffold.

## Two more compile-time crashes, then a live connection that stalls on handshake (2026-08-08)

This directly continues the DirectConnectMenu section above and predates the
"Layout mirror" milestone below by 6 days -- it was done in a session whose
notes never made it into this file before now (the code changes did land,
via a later bulk commit; only the writeup was missing). Recorded now,
2026-08-14, from that session's transcript, for continuity.

### `menu_direct_connect.nut` was never actually compiled -- moved into `_menus.nut`

After the fix above, clicking "Launch Northstar" no longer hit the
`GetMenu("DirectConnectMenu")` runtime error, but the game now sat at the
Respawn loading screen forever with no error dialog at all. shadPS4's own
stdout (not the sparse `shadps4.log`, which only captures a config summary --
capture full output with `> file 2>&1` on the launch command instead) had
the real answer:

    FatalError: ui/_menus.nut: UI SCRIPT COMPILE ERROR: Undefined variable "InitDirectConnectMenu"

This fires before any menu/dialog system exists to display it, so the whole
game hangs with zero visible feedback -- the same failure class as the
runtime "index does not exist" error, just one step earlier and worse
(unrecoverable instead of "just" a blocking error box).

**Root cause:** `menu_direct_connect.nut` sat in `work/stage1/sparse/frontend/scripts/vscripts/ui/`
next to every other working UI script, with a correct `global function
InitDirectConnectMenu` forward declaration -- but it turns out **which
`ui/*.nut` files actually get compiled into the UI script VM is a native,
hardcoded list inside `client.sprx`, not a content-editable manifest.**
Physically shipping a file in that directory does not make the engine
compile it. This had been invisible because nothing previously referenced
the bare identifier `InitDirectConnectMenu` at compile time -- only a
runtime string lookup (`GetMenu("DirectConnectMenu")`), which fails
"softly" (a catchable-ish index error) rather than aborting the whole
compile. The `AddMenu( ..., InitDirectConnectMenu )` call added in the fix
above was the first thing to reference the bare identifier, which is what
exposed this.

**Fix:** moved `InitDirectConnectMenu` and its `ConnectButton_Activate`
handler (renamed `DirectConnect_ConnectButton_Activate` to avoid any
ambiguity) directly into `_menus.nut` itself -- proven to be on the real
compile list, since `InitMenus()` demonstrably runs. Deleted the now-dead
`menu_direct_connect.nut` from the sparse tree. Forward-declared
`InitDirectConnectMenu` alongside the other `global function` declarations
at the top of `_menus.nut`; defined both functions at the bottom of the
file with a small `directConnectFile` struct for state (renamed from `file`
to avoid colliding with any future top-level `file` struct in the same
file). Rebuilt, redeployed, verified via full-log capture that `FatalError`
no longer appears and the game reaches the main menu.

**Lesson for any future "new menu from a mod" work:** a brand-new `.nut`
file is *not* automatically part of the compiled UI bundle just because
it's in the right directory. Either fold new logic into a file already
proven on the compile list (what this fix does), or route it through the
real mod-script-injection path (`CompileList`, Goal 6) once that's stable
-- do not assume directory presence is sufficient.

(This also means the `Northstar.DirectConnect` mod scaffold's own
`menu_direct_connect.nut`, loaded via `mod.json`'s `UICallback.Before`
mechanism, is a *different* code path -- Goal 6's `CompileList` injection,
not the native hardcoded UI list -- so it isn't subject to this same gap.
It remains untested/inert per Goal 6's status.)

### Second compile-time crash, deeper in: `AddServerToClientStringCommandCallback`

With that fixed, the user connected to a real Northstar dedicated server
(server-side saw the connection) and got past the main menu into an actual
level load (`mp_forwardbase_kodai`) -- then hung again, no error on screen.
Same diagnostic approach (full stdout capture) found:

    FatalError: sh_damage_types.nut: CLIENT SCRIPT COMPILE ERROR: Undefined variable "AddServerToClientStringCommandCallback"

`AddServerToClientStringCommandCallback` is called from three `mp_common`
scripts (`sh_damage_types.nut`, `sh_message_utils.gnut`,
`sh_custom_scoreboard_columns.gnut`) to register a handler for custom
string commands a Northstar server can push to the client. It is **not** a
vanilla engine native -- it's a Northstar-ecosystem Squirrel helper whose
native companion is NorthstarLauncher's own C++ hook
(`primedev/scripts/client/scriptservertoclientstringcommand.cpp`,
`ns_script_servertoclientstringcommand` ConCommand ->
`NSClientCodeCallback_RecievedServerToClientStringCommand`). Neither the
Squirrel-side registration helper nor that native ConCommand/dispatch path
exist anywhere in this port (confirmed by grep across `work/stage1`,
`tools/NorthstarLauncher-reference`, and the mod folders -- no definition
found anywhere, only call sites).

**Fix:** added a genuine no-op stub, `AddServerToClientStringCommandCallback( string commandName, void functionref( array<string> ) callbackFunc )`,
to `work/stage1/sparse/mp_common/scripts/vscripts/_custom_codecallbacks_client.gnut`
(forward-declared alongside the file's existing `AddCallback_On*`
declarations; confirmed no prior definition or name collision anywhere in
the sparse tree). Registered callbacks are accepted and silently dropped --
any gameplay feature a server drives purely through this channel (certain
custom scoreboard columns, custom HUD messages) will not function on this
PS4 client. If that turns out to matter, the real fix is implementing the
native `ns_script_servertoclientstringcommand` ConCommand and
`NSClientCodeCallback_RecievedServerToClientStringCommand` dispatch in
`launcher/src/runtime.cpp`, not expanding this stub.

**This class of bug (a Northstar-ecosystem Squirrel helper that's simply
undefined on this port) should be assumed to recur.** Both compile errors
found so far were only discovered by actually pushing further into the
connect flow, live, with full stdout capture -- there is no static way
found yet to enumerate every such gap up front. Reusing the grep-the-real-
script-content method (per Immediate next steps item 1's original two
ConVar fixes) proactively, before the next live test, would likely be
cheaper than finding them one hang at a time. `AddCallback_OnClientConnected`,
used right next to the missing function in `sh_damage_types.nut`, is a
genuine vanilla engine native and does NOT need a stub -- don't stub
functions that already work.

### Live connection now reaches level load, then stalls with no error at all

With both compile errors fixed, the user connected again: server confirmed
the connection, client progressed through the main-menu boot and into
loading `mp_forwardbase_kodai` (opened VPK chunks `000`-`003` of that map's
content), then went completely idle -- no more asset loads, no script
messages, no `FatalError`, nothing -- for multiple minutes of real time
(confirmed via periodic `UpdatePlayTime` log deltas a full minute apart
with zero intervening progress).

Attached `cdb.exe` **non-invasively** (`-p <pid> -pv`, dumps state without
suspending/killing the target; detached cleanly with `qd` afterward,
leaving the stuck session running and undisturbed) and pulled `~*kb` (every
thread's stack) from the already-hung process. Every thread checked --
`MainThrd`, `RenderThread`, `IOJob0`, `VPKMasterCacheThread`, `GlobPool0`,
`SDLTimer` -- was parked in a legitimate blocking wait
(`WaitForSingleObjectEx` / `SleepConditionVariableSRW`), not spinning and
not deadlocked on a lock. This is a materially different signature from
both compile-error hangs above (those never got far enough for any thread
to reach a normal wait state at all).

**Interpretation:** with every local worker thread idle and nothing being
actively streamed or compiled, this does not look like a missing local
asset. It looks like the client is blocked waiting for the **next step of
the server's connect/signon sequence** (Source engine's connect flow is
multi-message after the initial UDP handshake: challenge/response, then
the server pushing stringtables/precache tables/entity baselines) and that
next message either never arrives or arrives but isn't being processed.

**Not yet done / next step:** check the dedicated server's own console/log
on the far side of the connection for the same time window -- does it show
the client stuck at a particular signon stage, repeated retransmits, or an
error the PS4 client never sees? That's the other half of this handshake
and wasn't available from the PS4 client's own logs. This is the actual
open item blocking Goal 8's success criteria (a real match, not just a
server-acknowledged connection) and is not yet resolved as of this note.

## Layout mirror + native localisation milestone (2026-08-14)

Two changes landed and were verified this session.

### Mod layout rework (mirror whole mod dir; r2 overlay from the `mod/` subdir)

- `mods/Northstar.DirectConnect/` is now the canonical, PC-mirrored source for
  the PS4-specific DirectConnect mod (repo-root `mods/` source root):
  `mod/mod.json`, `mod/resource/ui/menus/direct_connect.menu`,
  `mod/scripts/vscripts/ui/menu_direct_connect.nut`.
- New shared helper `scripts/Resolve-ModSource.ps1` (
  `Resolve-ModSourceRoot` / `Get-ModSourceDir` / `Get-ModJsonPath`) drives both
  `New-Stage1Workspace.ps1` and `New-Stage2R2Overlay.ps1`.
- Stage 2 overlay manifest (`config/stage2-overlay-manifest.json`) is now
  version 2: `sourceSubdirectory` is the whole PC mod directory *without* its
  trailing `\mod`, and `sourceRoot` is relative to the repo root (e.g.
  `mods`). Two-step deploy: (1) mirror the entire PC mod dir into
  `/app0/mods/<Name>/` (so `mod.json` sits at `mods/<Name>/mod.json` and the
  r2 overlay source is `mods/<Name>/mod`), then (2) generate the r2 overlay
  from the source `mod/` subdir. `exclude` is now `*.dll/*.exe/*.so/*.pdb`
  (`*.json` is no longer excluded, so `mod.json` publishes into the mods root).
- Stage 1 manifest (`config/stage1-manifest.json`) is version 2 with
  `Northstar.DirectConnect` added; it uses `Resolve-ModSource.ps1` too.
- Verified deploy: 194 staged files, 215 mods-mirror files, 4 metadata files.
  DirectConnect now lands at `mods/Northstar.DirectConnect/mod.json` plus
  `mod/...`, with r2 overlay entries
  `r2/resource/ui/menus/direct_connect.menu` and
  `r2/scripts/vscripts/ui/menu_direct_connect.nut`. `scripts.rson` regenerated
  (83 blocks appended; 2 expected missing-file warnings for Northstar.Custom
  gamemodes `sh_gamemode_fw.nut` / `cl_gamemode_fw.nut`).

### Native localisation loading via localize.prx `AddFile` (verified end-to-end)

Probe implemented in `launcher/src/runtime.cpp` (`ProbeLocaliseInterface`,
gated on `-EnableM6Localise` + `-EnableM6ModMetadata`).

localize.prx ABI findings (all file VAs; runtime = base + file VA):

- `AddFile` at VA `0x5c60`, signature
  `bool(void* this, const char* path, const char* pathId, bool includeFallbackSearchPaths)`.
  Prologue preimage `55 48 89 e5 41 57 41 56 41 55 41 54 53 48 83 e4`.
- Instance accessor at VA `0x4f90` (`48 8d 05 e9 82 01 00 c3` = `lea rax,[rip+0x182e9]; ret`)
  returns `base + 0x1d280`.
- The singleton **object** lives in `.bss` at `base + 0x1d280`; its first qword
  is the **vptr**, installed by the module's own init code. The initial
  read-only copy lives in the rodata segment (seg 1, file offset `0x1c000`):
  the qword at file offset `0x1c010` is `0x5180`, relocated at runtime to
  `base + 0x18010`.
- The vtable is the relocated rodata table at `base + 0x18010`
  (Itanium layout: `[-2]` offset-to-top `0`, `[-1]` typeinfo `0x18150`,
  `[0]` first virtual `0x5180`). Slot 9 (`+0x48`) is `0x5c60` = **AddFile
  itself**; `vtable[9]` is the call `AddFile` makes on its english-immediate
  `%language%` fallback path (disassembly at `0x5e53`/`0x5e69`).
- `this + 0x48` is the "fallback search enabled" field. When `0`, `AddFile`
  takes the fallback path that dispatches through `vtable[9]` (i.e. calls
  itself -- infinite recursion hazard). The probe therefore saves the byte,
  forces it to `1` for the duration of its calls (taking the direct path via
  `0x5f60`), and restores it afterwards.
- Module-size trap: `sceKernelGetModuleInfo` `segmentInfo[0].size` is only the
  first segment (localize seg 0 memsz `0x18000`), but the singleton/vtable lie
  in later segments (`0x1d280` / `0x18010`); the span must be computed as
  max end over all segments (`0x24000`). The probe now computes this the same
  way the client span is computed. Without it, the first attempt refused the
  instance as "out of range" and, after widening, read `*(vptr)` as the vptr
  (double-deref bug) and rejected `vtable[9]` as out-of-module.

Verified run `work/stage2/iterations/20260814-163721/` (deployed PRX SHA-256
`9f03837a338c92741a27a400e71b539c39b5d33164f604ab192751a467d8fc11`,
status success):

    localise probe start handle=0x15 base=0x81d78c000 size=0x24000
    localise gate addFile=1 accessor=1
    localise singleton this=0x81d7a9280 vptr=0x81d7a4010 vtable[9]=0x81d791c60 this+0x48=0
    localise Northstar.Client  file=resource/northstar_client_localisation_%language%.txt result=1
    localise Northstar.Custom  file=resource/northstar_custom_%language%.txt result=1
    localise self-test vanilla english result=1
    localise probe complete files=2 loaded=2

The stock deployed mods already declare `Localisation[]` in their `mod.json`
(Northstar.Client: `resource/northstar_client_localisation_%language%.txt`;
Northstar.Custom: `resource/northstar_custom_%language%.txt`), and both loaded
natively through the game's real `AddFile`. `Northstar.DirectConnect` declares
none. Localisation tokens were verified earlier to need no additions:
`MENU_DIRECT_CONNECT` exists in neither the PC nor the deployed localisation,
and the DirectConnect mod's UI script only references the vanilla `#BACK` /
`#B_BUTTON_BACK` tokens (both present), so it hardcodes its "Direct Connect"
label as-is.

## Real Northstar mod-loading architecture: scripts.rson + hook dispatch (2026-08-15)

Full narrative, the 8 issues found/fixed en route, and the local test-Atlas
setup are written up in `docs/GOALS.md` under **Goal 12** and **Goal 13** --
not duplicated here to avoid drift between the two documents. Short pointer
for anyone starting from this file: the new tool is
`scripts/Build-Stage1ModIntegration.ps1`; it replaces the old hand-patched
Northstar.Client copy with the genuine PC mod, using `scripts.rson` (baked
into the VPK, not r2-overlaid) as the real compile list and a generated
`UICallback` hook-dispatch block in `_menus.nut` in place of
NorthstarLauncher's native hook dispatch. `ServerCallback`/`ClientCallback`
dispatch is not yet built (needed for `Northstar.Custom`).

## Auth token systems context (from Northstar Discord, relayed 2026-08-17) and a live PS4-specific lead

Not from this project's own investigation -- relayed by the user from the
Northstar Discord's reverse-engineering channel, but directly relevant and
worth recording verbatim in spirit:

- Titanfall 2's client has **at least four separate token/auth concepts**:
  Origin token (PC/EA), Durango token (Xbox), a "3P" token (**third-party,
  i.e. console platform auth -- confirmed "unused on PC"**), and a Nucleus
  token (EA's internal matchmaking API token, sent to *every* server the
  client connects to). These are easy to conflate; even NorthstarLauncher's
  own code reportedly mislabels the Nucleus token as "P3PToken" ("3P" backwards)
  at the point it's referenced, which caused real confusion in that Discord
  thread too.
- Per that thread, Northstar's client-side code overwrites the Nucleus
  token before it's sent to a server (presumably because sending a player's
  real EA-issued token to an arbitrary, potentially untrusted Northstar
  server would leak more than intended). Grepped for this in
  `tools/NorthstarLauncher-reference` (Nucleus/P3PToken/3pToken/ThirdParty) --
  **no matches at all**, so either this override lives somewhere this
  checkout doesn't have, or it happens without any C++-side involvement
  (e.g. purely via a native engine ConVar or callback vanilla code already
  drives, that Northstar's PC build simply doesn't need to touch).
- Separately (not from Discord, general Source-engine knowledge and this
  thread): connecting to a loopback address (127.0.0.1/localhost) on PC
  bypasses real networking entirely -- client and server share a process
  and use a direct buffer, no socket at all, unless
  `net_usesocketsforloopback` forces real socket logic even for loopback.
  Not directly relevant to this project's testing so far (the tested
  dedicated server is a real LAN IP, not loopback), but worth knowing if a
  same-machine client+server test is ever attempted.

**Live PS4-specific lead worth testing:** "3P is for console" means this is
*exactly* the token path a retail PS4 build would actually exercise (PSN
auth), as opposed to Northstar's PC-centric Origin/Nucleus-override
machinery. shadPS4's own boot log confirms the game genuinely loads
`libSceNpAuth` (the real PS4 SDK library for this) at startup -- but every
`sceNpGetOnlineId`/`sceNpGetNpId` call observed so far returns
`SIGNED_OUT` (`user_id=1000 shadnet_enabled=false signed_in=false`), since
shadPS4 has no real PSN account signed in. If the client's native connect
flow tries to fetch a 3P/PSN auth token as part of the signon handshake and
that fails silently because of the SIGNED_OUT state, a server could
plausibly wait forever on a token that never arrives -- which would fit the
2026-08-08 network-handshake-stall finding (every client thread legitimately
parked, not spinning, not crashed) far better than a missing-asset theory
does. Not yet confirmed: no `sceNpAuth*` (as opposed to `sceNpGetOnlineId`/
`sceNpGetNpId`) calls have been observed in any captured log yet, but none
of those logs are from an actual connection attempt -- next real connection
test should specifically watch for `sceNpAuth` activity during the stall,
not just `sceNpGetOnlineId`.

## Real dedicated-server log: root cause of the connect-then-freeze report (2026-08-17)

The user reported "I've connected with and without ns insecure and its
frozen on connection at the moment, the server is stuck on connecting" and
shared a real PC NorthstarLauncher dedicated-server log
(`nslog2026-08-17 18-42-11.txt`, 1766 lines). The relevant window:

```
[18:44:04] [SCRIPT SV] Player connect started: entity (1: player The_taskinoz [1])---UID:1000108120826
[18:44:05] [SCRIPT UI] UICodeCallback_LevelInit: mp_forwardbase_kodai
[18:44:05] [SCRIPT SV] Player client script initialization complete: entity (1: player The_taskinoz [1])
[18:44:06] [SCRIPT SV] started intro!
[18:44:06] [SCRIPT SV] starting dropship intro!
...
[18:44:17] [NORTHSTAR] shadPS4's (uid 1) connection was rejected: "Authentication Failed."
[18:44:17] [NORTHSTAR] Player  disconnected: "NetChannel removed."
[18:44:17] [NORTHSTAR] shadPS4's (uid 1) connection was rejected: "Authentication Failed."
[18:44:17] [NORTHSTAR] Player  disconnected: "NetChannel removed."
[18:44:21] [SCRIPT SV] intro finished!
[18:48:05] [NORTHSTAR] Player shadPS4 disconnected: "#DISCONNECT_TIMEDOUT"
```

**This is not a client-side freeze.** The player fully connects, finishes
client script init, and starts playing the dropship intro -- then ~11s
later gets rejected with "Authentication Failed." (appearing twice, in a
pattern consistent with a retried/duplicate connect attempt racing the
original), and the "frozen" appearance the user sees is almost certainly
the client falling back to a connecting/stuck state after the server tears
its connection down mid-session, followed eventually by a 4-minute
`#DISCONNECT_TIMEDOUT` cleanup.

Traced the exact rejection in `tools/NorthstarLauncher-reference`:
`serverauthentication.cpp`'s `h_CBaseClient__Connect` (line ~284) calls
`CheckAuthentication`, whose very first check is:

```cpp
bool ServerAuthenticationManager::CheckAuthentication(CBaseClient* pPlayer, uint64_t iUid, char* pAuthToken)
{
    ...
    // if we don't need auth this is valid
    if (Cvar_ns_auth_allow_insecure->GetBool())
        return true;
    ...
    // don't allow duplicate accounts
    if (IsDuplicateAccount(pPlayer, sUid.c_str()))
        return false;
    ...
}
```

`ns_auth_allow_insecure` is registered `FCVAR_GAMEDLL` and is read **only
by the dedicated server process** -- it gates whether the server accepts a
connection without a masterserver-issued token at all. Getting a genuine
"Authentication Failed." rejection is only possible when this cvar is
**false** on the server at that moment; if it were true, `CheckAuthentication`
returns `true` unconditionally on the very first line, before the
duplicate-account check (`IsDuplicateAccount`) is ever reached.

**Conclusion / action for the user:** since the rejection happened
regardless of the client-side `ns_auth_allow_insecure` toggle, that toggle
was never reaching the place that matters. `ns_auth_allow_insecure` must be
set directly on the **PC dedicated server's own config or console**
(e.g. `autoexec_ns_server.cfg`, or typed into the server's own console, or
passed as `+ns_auth_allow_insecure 1` on its launch command line), not on
the PS4 client -- setting it PS4-side has no effect on this check at all.
This project's native Atlas masterserver client (Goal 13) doesn't exist yet
to supply a real per-connection auth token the server could otherwise
validate via `m_RemoteAuthenticationData`, so insecure mode is the only way
to get a clean connection until Goal 13 lands.

Secondary, lower-priority observation: the double "Authentication Failed." /
"NetChannel removed." pair is consistent with the PS4 client re-sending a
full connect handshake ~11s after an already-successful connect (the
original connection was live long enough to run client script init and
start the dropship intro). If insecure mode alone doesn't fully resolve the
user's next test, this retry behavior -- and whether it's a shadPS4/engine
netchannel quirk rather than anything this project's own launcher code
controls -- is the next thing to investigate.

### Re-tested 2026-08-17 19:30: identical rejection despite ns_auth_allow_insecure reportedly set server-side

A second live test (`nslog2026-08-17 19-30-38.txt`), after the user set
`ns_auth_allow_insecure 1` directly in the dedicated server's own console
(confirmed not a restart-reset scenario -- typed after the server was
already running, no restart since), produced the **identical** failure
signature: player connects, spawns, dropship intro starts, then ~4s later
(`19:31:59`, vs ~11s in the first test) gets kicked with the same
"Authentication Failed." / "NetChannel removed." pair, twice.

This is mechanically significant: `CheckAuthentication`'s literal first
statement is `if (Cvar_ns_auth_allow_insecure->GetBool()) return true;` --
if that cvar were genuinely `1` in the running server process at connect
time, this rejection *cannot* happen, full stop, regardless of any other
state (duplicate account, remote auth data, etc.). Since it happened again
under conditions that should have ruled out the "console-set-then-restart"
explanation, either (a) the value didn't actually take for a reason not yet
identified (wrong console/process, typo, a second unaccounted-for
server instance), or (b) there's a second code path producing the exact
same "Authentication Failed." string that hasn't been found in the
reference source yet (only one match with a trailing period was found,
in `serverauthentication.cpp`; two other matches without a trailing period
exist in `masterserver.cpp` but format the string differently in the
console print). **Next step, not yet done:** get the live value of
`ns_auth_allow_insecure` printed directly from the dedicated server's
console (typing the bare convar name with no value prints its current
setting) at the moment of a test, to conclusively confirm whether it's
reading `1` or `0` server-side.

Also newly visible in this log: the dedicated server's own attempt to
register itself with the local test Atlas instance is failing --
```
[19:32:04] [error] Couldn't find request id in response
[19:32:09] [info] Attempting to register the local server to the master server.
[19:32:19] [error] Couldn't find request id in response
[19:32:19] [warning] Reached max ms server registration attempts.
```
Traced to Atlas's `/server/add_server` handler (`tools/Atlas-reference/pkg/api/api0/server.go`):
on success it responds with `{"success":true,"id":...,"serverAuthToken":...}`,
but several rejection paths (`respFail`) omit `"id"` entirely, which is
exactly what NorthstarLauncher's "Couldn't find request id in response"
log line detects. The specific rejection likely at play here:
`handleServerUpsert` runs `probeUDP()` -- Atlas actively sends a
connectionless "connect" probe packet to the dedicated server's own game
UDP port and only finalizes registration (and includes `"id"`) if that
probe succeeds. If there's a firewall or NAT gap between the Atlas host and
the dedicated server's UDP game port, this fails silently in exactly this
shape. Not blocking direct-connect testing (this only affects server-list
registration, i.e. the browser), but relevant once Goal 13's real server
browser work resumes -- worth checking connectivity from the Atlas
machine to the dedicated server's UDP port directly (e.g. a raw UDP probe
tool) before assuming it's a code bug.

## Found the real cause of "frozen on connect": stale /app0/mods overlay shadowing every VPK fix all session (2026-08-17)

The user's own theory turned out to be exactly right ("I think this might
be a vscript error where its missing some scripts... I can try update the
mods to test that theory"). Capturing the **client's** own log during a
live connect attempt (not just the server's, which is all that had been
checked up to this point) showed a genuine boot-time FatalError that
shadPS4/this port simply never surfaces on screen:

```
FatalError: _custom_codecallbacks_client.gnut: CLIENT SCRIPT COMPILE ERROR: Undefined variable "NSChatWriteRaw"
```

Traced `NSChatWriteRaw`, `NSChatWrite`, `NSChatWriteLine`, and `NSSendMessage`
(CLIENT variant) to genuine NorthstarLauncher natives
(`primedev/scripts/client/clientchathooks.cpp`,
`primedev/client/chatcommand.cpp`) this port has never implemented --
same missing-native class of bug as `AddServerToClientStringCommandCallback`
earlier. Stubbed all four as no-ops in `mods/Northstar.PS4/mod/scripts/vscripts/_custom_codecallbacks_client.gnut`,
following the same forward-declare-then-define pattern, with one added
subtlety: `NSChatWrite`/`NSChatWriteLine`/`NSSendMessage` are also used by
Northstar.Client's own `client/cl_chat.gnut`, and Northstar.Client's
mod.json lists `_custom_codecallbacks_client.gnut` (Scripts[] index 62)
before `client/cl_chat.gnut` (index 66) -- since `Build-Stage1ModIntegration.ps1`
generates each mod's rson entries in its own Scripts[] order and rson-entry
*position* for an overridden path comes from whichever mod is processed
first (dedup keeps the first-seen entry; only file *content* comes from
the highest-LoadPriority mod), this ordering already guaranteed the stub
declarations compile before `cl_chat.gnut` uses them.

**Rebuilt, redeployed the VPK, and the exact same FatalError happened
again on retest.** Extracting `_custom_codecallbacks_client.gnut` directly
from the deployed VPK proved the fix genuinely was baked in (`grep -c
NSChatWriteRaw` = 12, correct). This contradiction -- fix confirmed present
in the deployed VPK, but the exact same undefined-variable error still
firing at runtime -- led to finding the real cause: **a second, entirely
separate mod-deployment path**, `scripts/New-Stage2R2Overlay.ps1`
(Goal 5's native `/app0/mods/<Name>/mod` search-path overlay,
`launcher/src/runtime.cpp`'s `ModSearchPathOpen` hook), which had last been
run on 2026-08-11/14 -- **before almost all of this session's Goal 12 work**
-- and was never refreshed since. `D:\PS4\ShadPS4\CUSA04013\mods\Northstar.Client\mod\...`
still held the original, unmodified PC file (confirmed via direct
inspection: 7 matches for the old undefined natives, 0 stub content), and
`D:\PS4\ShadPS4\CUSA04013\mods\Northstar.PS4\mod.json` on disk didn't even
have a `Scripts[]` array yet (an old version, predating every fix from this
session). This live overlay silently wins over VPK-baked content --
confirmed by reading `launcher/src/runtime.cpp`'s `ModSearchPathOpen` hook
(~line 1764): it replaces the engine's filesystem vtable's open slot to
serve mod files from their own `/app0/mods/<Name>/mod` directory *first*,
falling through to the engine's original search paths (loose `/app0/r2` +
VPK mounts) only if no mod root has the file. Every fix made earlier this
session that touched a file also present in this stale overlay (which,
given the overlay mirrors entire mods, is basically everything Northstar.Client/PS4
touch) was being silently shadowed by 6-day-old content the whole time --
the scripts.rson ordering fix, the auth investigation, all of it was
working against VPK content the live game was never actually reading for
these paths.

Confirmed the resolution order is priority-correct before relying on it:
`ModSearchPathOpen` iterates `g_modRoots[]` **in reverse discovery order**
(`for (i = g_modRootCount - 1; i >= 0; --i)`, `runtime.cpp` ~line 1808),
i.e. the *last-discovered* mod is checked *first* -- mirroring PC's
`AddSearchPath` semantics where the last-registered path wins.
`CollectModNames` discovers mods either via `opendir("/app0/mods")` (whose
enumeration order happened to already be alphabetical in testing, putting
Northstar.PS4 last) or by falling back to reading `/app0/mods/.ns_mod_manifest`
(explicitly written in `config/stage2-overlay-manifest.json`'s `mods[]`
array order: Client, Custom, DirectConnect, PS4 -- PS4 last either way).
So Northstar.PS4 (LoadPriority 99) reliably ends up checked first at
runtime, which is correct -- the bug was purely that its overlay copy of
this file didn't exist/wasn't current, not a resolution-order problem.

**Fix:** re-ran `New-Stage2R2Overlay.ps1 -Clean -SkipR2ModStage` (matching
how it was originally deployed -- `-SkipR2ModStage` is required; without it
the script instead dumps mod content flatly into `/app0/r2`, a different,
older mechanism this port isn't using). Verified boot-clean afterward
(`[NorthstarPS4] fs overlay mod root[3]=/app0/mods/Northstar.PS4/mod`,
`fs overlay vtable2 repointed... roots=4`, zero `FatalError` matches,
reached `MAINMENU`).

**Process takeaway for future sessions:** this port has *two* independent
content-delivery paths that can each silently shadow the other --
VPK-baked content (`work/stage1/sparse` -> `Build-AndDeployStage1Vpks.ps1`
-> `D:\...\vpk_ps4`) and the live `/app0/mods` search-path overlay
(`mods/` in this repo -> `New-Stage2R2Overlay.ps1 -SkipR2ModStage` ->
`D:\...\mods`). **Any change to a mod's script content needs both
redeployed together**, or the overlay (which wins at runtime for any path
it covers) will silently serve stale content indefinitely with no error of
any kind. Consider folding `New-Stage2R2Overlay.ps1 -SkipR2ModStage` into
the same rebuild step as `Build-Stage1ModIntegration.ps1` +
`Build-AndDeployStage1Vpks.ps1` so this can't drift apart again.

## scripts.rson merge insertion-order bug (found and fixed 2026-08-17)

While investigating the above, a live client log (`launch18.log`) separately
showed `FatalError: sh_damage_types.nut: CLIENT SCRIPT COMPILE ERROR:
Undefined variable "AddServerToClientStringCommandCallback"` -- a regression
of a bug already fixed earlier (see "Two more compile-time crashes..."
above). The stub was confirmed present in both the staged source tree
(`grep -c` returned 3 matches in
`work/stage1/sparse/mp_common/scripts/vscripts/_custom_codecallbacks_client.gnut`)
and the deployed VPK (hash matched `dist/stage1-sparse/manifest.csv`
exactly), ruling out a stale-deploy explanation.

Root cause: `Build-Stage1ModIntegration.ps1`'s final `scripts.rson` merge
step inserted all mod-provided blocks immediately before the vanilla
`// DEVSCRIPTS CONTENT` marker, which sits near the very end of the vanilla
file. Vanilla content much earlier in the file --
`sh_damage_types.nut` at ~line 191, in a `"SERVER || CLIENT"` block --
references the mod-provided `AddServerToClientStringCommandCallback` global.
Squirrel has no forward declaration for a not-yet-compiled global; a file
compiled before the global it references exists fails immediately with
"Undefined variable", it does not resolve lazily. This is the exact same
class of bug as the earlier `InitScript`-ordering fix within a single mod's
own blocks, but at the whole-merge level across mods and vanilla content.

Fix: mod blocks are now inserted right after the header comment, before the
**first** vanilla `When:` block, rather than before `// DEVSCRIPTS CONTENT`.
Each generated block is already a self-contained `When: "..." Scripts:
[...]` unit, so relocating the whole group to the top of the file is
syntactically safe -- RSON has no enclosing root structure, just a flat
sequence of independently-compiled `When:`/`Scripts:` blocks. Verified in
the redeployed VPK: `_custom_codecallbacks_client.gnut` now appears at line
82 of the merged `scripts.rson`, `sh_damage_types.nut` at line 269 (was
previously the reverse, with mod content past line 1000).


## 2026-09-06: clean archives, native filesystem and first global native

Client SHA256 `abc6efd2125a2a58d32ad9d23d245a03fd3213a763e407712a575f0bc04e879b`; filesystem SHA256 `4d6b7b653c1d9cde01b2f0634d11a06d8b986dded4374b87ca330100e4969266`.

All addresses below are this PS4 build's module-relative VAs, discovered from local disassembly. Executable sections have file offset VA+0x4000. Runtime mutation gates validate addresses and preimages.

- Filesystem primary vtable VA 0x70eb0, secondary 0x713f0. Primary slot 76 OpenEx is 0xd4a0; secondary Open 0xd480 adjusts this by -8 and delegates. OpenEx ABI `(self, path, mode, uint32 flags, pathID, resolvedPath**)`. Copy retains the preceding two RTTI words and 166 function slots.
- Primary slot 97 ReadFromCache is 0x67f0. Client compile function 0x6783f0 calls primary offset 0x308; false falls back through ReadFile/OpenEx. Bypass only when an enabled loose override exists. This resolves duplicate UICodeCallback_MouseMovementCapture caused by mixed cached vanilla and mod scripts.
- Constant table is sharedState+0x40d8 (tag 0x0a000020, pointer +0x40e0), sharedState=HSQVM+0x50. Intern 0x6a96a0 uses stringtable sharedState+0x4048. Table insertion 0x6ab3e0 returns bool in AL. VM owner global 0x1afbfb8, HSQVM=owner+8. First manifest access precedes owner creation; InitScript access has a live UI VM.
- Global native registration 0x67a3c0 actually takes `(CSquirrelVM*, record*, classTree*, textualTypes, instance)`. Retail call 0x6718f6 passes null classTree, textualTypes=1, instance=0. Do not use the Windows three-argument signature. Record size 0x68 with textual return/arguments at +0x18/+0x20 and callback +0x60.
- Verified bool return stack layout against 0x682137..0x6821dc: HSQVM top +0x68, stack pointer +0x70, 16-byte objects, bool tag 0x1000008. Replacing a reference releases the old object (refcount +8, destructor vtable+0x10). Runtime callback invocation remains untested; compile-time native symbol/type registration succeeded.
- Native lifecycle callback dispatch candidate 0x679d40: UI caller 0x31df24, client map caller 0x768386. **Read-only profiling only; not hooked.**

Latest probe `work/stage2/iterations/20260906-030006/shad-new-lines.log`: registered NSIsMasterServerAuthenticated; next compile failure NSTryAuthWithLocalServer. PRX SHA256 `aec370a131d91c362e5326ca189342189b88808d7758fd57d75321a1ae932c8a`. The installed PRX was subsequently restored to the non-injecting bootstrap. No native-only full menu or map acceptance has passed.


## 2026-09-17: deferred native signatures and UI callback hook

The typed-return natives must be registered after `cl_northstar_client_init.nut` has declared ModInfo/ServerInfo/MasterServerAuthResult. Before declaration, the PS4 signature resolver gave NSGetModInformation return type `var`, causing a typed-assignment compile error. Deferred registration at the observed panel_mainmenu read resolves that error for the current core mods; this is still a core-mod-specific timing trigger, not a general VM lifecycle integration.

Verified PS4 Squirrel helpers: string push 0x682e00, new array 0x683330, append 0x6835e0, new struct 0x684970, seal field 0x684b00, raise error 0x682a60. The runtime validates helper preimages before exposing callbacks. Invocation of the new struct-returning APIs still awaits completion of UI compilation.

UI call at 0x31df24 has preimage `e8 17 be 35 00` targeting 0x679d40. The experimental hook changes only its rel32 displacement, refuses out-of-range targets, and leaves the original callback function untouched. Callback metadata is parsed fully before mutation. The main-thread registration point patches the containing 16 KiB page then restores RX. The controlled boot logged 15 callbacks installed and successful protection restoration, but never reached execution: compilation stopped at missing NSFetchVerifiedModsManifesto. No Before/After execution, InitScriptCallback, Destroy, client or server callback proof is claimed.

Enabled-state persistence is under writable guest `/data/northstar_ps4/enabledmods.json`, with temporary-file flush/fsync/rename and original app0 profile fallback. NSReloadMods explicitly raises a restart-required error after saving; there is no live script VM replacement. Host tests cover preservation of unrelated settings and callback parsing. Network functions remain explicitly offline/error adapters; authentication, downloads and server-list transport are not ported.


## 2026-09-17: Safe I/O, JSON and the completed UI lifecycle

Client SHA256 `abc6efd2125a2a58d32ad9d23d245a03fd3213a763e407712a575f0bc04e879b` throughout. All VAs below are module-relative in that client.prx, found by local disassembly and each gated on an exact byte preimage before use.

**Call stack, for per-mod save isolation.** The client's own callstack printer at 0x67f062-0x67f0bf reads `sqvm+0x38` `_callstack`, `sqvm+0x40` `_callstacksize`, indexes `_callstack[size - level - 1]` with a 0x48-byte CallInfo, checks `ci+0x20` for `OT_FUNCPROTO` (0x8002000), takes the funcproto from `ci+0x28`, and reads its source `SQString*` from `proto+0x40` guarded by `OT_STRING` (0x8000010) at `proto+0x38`; characters live at `SQString+0x30`. The funcname pair is `proto+0x48`/`proto+0x50`. `CallingSource` performs the same reads and nothing else - no engine call and no writes - which is why PC's `sq_stackinfos` never had to be located.

**Script call with arguments.** The engine's own dispatcher at 0x679280 shows the sequence: push the function object, push `this`, push each argument, then call. Derived entry points:

- 0x685cf0 `FindFunction(sqvm, name, SQObject* out, const char* signature)`; returns negative when absent, and a null `signature` skips signature validation (`test r13,r13; je` at 0x685d97).
- 0x6875f0 `PushObject(sqvm, uint64 packedTag, void* value)`; increments the refcount itself.
- 0x6876c0 `sq_call(sqvm, params, retval, raiseerror)`; on success it pops `params` entries (loop at 0x687740) and leaves the closure, which the caller pops, as 0x679570 does.
- The VM root table object is at `sqvm+0xb8` (pushed from `rax+0xb8` at 0x67944a).

**Squirrel containers, for JSON.** `sq_newtable(sqvm)` is 0x683230 (pushes `OT_TABLE` 0x0a000020). `SQTable` layout confirmed at the insert routine 0x6ab480: `+0x38` nodes, `+0x40` node count (a power-of-two mask), node stride 0x28 with `val` at +0x00, `key` at +0x10 and `next` at +0x20. `SQArray` is `+0x30` values and `+0x38` used slots, confirmed by the one-million bound check at 0x683650. The insert at 0x6ab3e0 does **not** take a reference: the client increments the refcount itself before calling (0x684555), so the runtime does the same and then pops the stack entry, transferring ownership.

**Callback dispatch semantics.** PC's `CallScriptInitCallbackHook` ignores the result of every Before and After mod callback and always runs the rest; only the engine's own callback supplies the return value. The PS4 hook now matches. This is what unblocked the last three After callbacks: `NSUpdateGameStateUIStart` is declared in Northstar.Client's `mod.json` but defined in no installed script, and the previous abort-on-failure behaviour stopped `AddColorPickerMenu`, `AtlasAuthDialog` and `InitialiseArenaLoadouts` from ever running.

Boot `work/stage2/iterations/20260917-134347/shad-new-lines.log`, PRX SHA256 `830102e32f74b0a3fb8d74415ab595137486ec95b8dbd5eb9518ddee897a78f2`: UI compilation completes, all 16 UI callbacks dispatch in load order and `UI lifecycle completed` is reached. A 152-second follow-up run stayed up with no crash and no script errors. The only parse errors belong to the mod's own `colorsliders.menu`, which ends with an XML-style `<!-- ... -->` comment that Source KeyValues cannot parse; PC hits the same thing and the file is not edited. All 1,069 recorded VPK and mod files rehashed unchanged after these runs.

Not claimed: no CLIENT or SERVER VM lifecycle, no per-frame drain for the deferred script-call queue (PC uses `CHostState::FrameUpdate`; results currently land at the next UI code callback), no `NSLoadFile` exercised by real mod content, and no map load.


## 2026-09-17: CLIENT VM lifecycle, and why no C++ static initializer runs

**Context values.** The VM initializer hooked at 0x6717af receives PC's
`ScriptContext` values: 0 SERVER, 1 CLIENT, 2 UI. Only UI (2) appears during
menu boot; a CLIENT VM is created on map load and `server.prx` is not even in
the loaded module list until a server is hosted.

**CLIENT call sites.** All five callers of the code-callback dispatcher
0x679d40 were resolved by their name argument:

| VA | callback | context |
|----|----------|---------|
| 0x31df24 | `UICodeCallback_UIInit` | UI (hooked) |
| 0x768386 | `ClientCodeCallback_MapSpawn` | CLIENT (hooked) |
| 0x34ed2d, 0x768487 | `ClientCodeCallback_SetupRumble` | CLIENT |
| 0x768465 | `ClientCodeCallback_SaveResumed` | CLIENT |

`ClientCodeCallback_MapInit` and `ClientCodeCallback_MyScriptInit` dispatch
through a second entry point at 0x679e80 instead. Only the two hooked names
are the ones PC's `bShouldCallCustomCallbacks` lets custom callbacks run for,
so the hook set matches PC exactly. The client VM owner is the global at
0x19d4fe8; the UI owner global is 0x1afbfb8.

**VM teardown.** Four call sites release `owner->sqvm` through 0x6787a0: the
three already known UI paths (0x1441ce, 0x31e02a, 0x33559a, all clearing
0x1afbfb8) and the CLIENT path at 0x2e7813, whose surrounding block
(0x2e77f2-0x2e7834) loads and then clears 0x19d4fe8. All four now route
through one destroy hook that matches the owner against each context's state.

**`sq_pushasset` is 0x682f40** (the function after `sq_pushstring`, storing
tag 0x8000400), which makes `StringToAsset` a real implementation rather than
a stub.

**This module runs no C++ dynamic initializers.** `Build-Stage2Poc.ps1`
deliberately patches `DT_INIT` to point straight at `NorthstarPs4Init`,
because shadPS4 starts `DT_INIT` but never calls OpenOrbis's hidden
`module_start`. That bypasses the crt code that would walk `.init_array`, so
**only that one constructor ever runs**. Every other global in this module
must be constant-initialized.

This surfaced as a hard-to-read crash. A new
`VmLifecycle{"UI", "UICallback", ...}` global had a `std::vector` member,
which makes the whole aggregate initializer *dynamic* rather than constant, so
the struct was left zero-filled and its `metadataKey` was null by the time
`ParseScriptCallbacks` used it: an access violation inside this module
immediately after the first `mod.json` was read, with nothing in the log to
point at the cause. Existing globals like `std::string authFailure` and
`std::vector<CatalogEntry> catalog` survive only because a zero-filled libc++
`string`/`vector` happens to be a valid empty one. The fix was to move the
vectors into their own zero-initialized globals and keep the struct's
initializer constant. Two earlier suspicions (engine-thread stack exhaustion
from the ~25 KiB metadata buffers) were wrong, though the buffers were moved
out of those frames anyway and that change was kept.

Boot after the fix: `work/stage2/iterations/20260917-142801/shad-new-lines.log`,
PRX SHA256 `829a4f59b082760fd8d4e331a920f89c4ebbbed4e3d95e0e426e39308496cbaa`.
Both lifecycle hooks installed with protection restored (`va=31df24` and
`va=768386`, both `protection=0`), 15 UI and 27 CLIENT callback entries
loaded, UI startup unchanged. **The CLIENT path is installed but unproven:**
nothing in this run created a CLIENT VM, so no CLIENT InitScript compile,
native registration or callback dispatch has executed yet. That needs a map
load.


## Northstar.DirectConnect: the menu had no entry point (2026-09-17)

The mod registered `DirectConnectMenu` through its `UICallback.Before` but
nothing ever opened it. The button that used to open it lived in the Stage 1
sparse VPK patch of `panel_mainmenu.nut`, and the 2026-08-07 notes above
already record that it was deliberately *not* moved into the mod, because it
reached into `panel_mainmenu.nut`'s private `file` struct. That patch is
retired, and under Northstar `panel_mainmenu.nut` now comes from
Northstar.Client anyway, so the button no longer exists in any form.

Fixed with the pattern Northstar.Client uses for its own Mods entry: a second
`UICallback.After` (`AddDirectConnectMenu_MainMenuFooter`) that calls
`AddMenuFooterOption( GetMenu( "MainMenu" ), ... )`. `Before` is too early for
this - `GetMenu( "MainMenu" )` is only valid once UIInit has finished - which
is exactly why Northstar splits its own registration the same way.

**BUTTON_X is free on the main menu under Northstar.** `menu_main.nut` guards
its only BUTTON_X footer option (inbox accept) with `#if VANILLA`, and this
runtime registers `VANILLA = 0`, so it is compiled out. BUTTON_Y is taken by
Northstar's Mods list and BUTTON_A is the console select prompt. Note also
that `_footer.nut`'s `InitFooterOptions` wires footer *click* handlers only
under `#if PC_PROG`, so on PS4 a footer option is reachable by its physical
button, not by clicking it.

The per-option `conditionCheckFunc` argument of `AddMenuFooterOption` is left
null deliberately: `menu_main.nut` gates its console footer options on
`IsConsoleSignedIn`, and shadPS4 reports `SIGNED_OUT` for every
`sceNpGetOnlineId` call, so copying that gate would have hidden the option
permanently.

The mod now ships its own `MENU_DIRECT_CONNECT` token in a UTF-16 LE
localisation file rather than depending on `Northstar.PS4`, which is not part
of the installed profile. Boot confirms
`localise Northstar.DirectConnect file=resource/northstar_directconnect_localisation_%language%.txt result=1`,
4 mods discovered (was 3), 106 declared scripts (was 105), and both
`AddDirectConnectMenu` and `AddDirectConnectMenu_MainMenuFooter` dispatching
without a "callback not found". Whether the option renders and navigates is
not verified here; that needs someone at the screen.


## First CLIENT VM: init works, InitScript was compiled twice (2026-09-17)

The user reached a real connect through the Direct Connect menu, twice (MP
lobby and an in-progress game), against a PC dedicated server with
`ns_auth_allow_insecure 1`. Both froze. The log shows why, and it is not the
auth path this time.

**What worked.** A CLIENT VM was created for the first time
(`VM initialized context=1 owner=0x22e5c4900 result=1`) and the whole CLIENT
lifecycle plumbing ran: 27 non-deferred natives registered, the InitScript
compiled, 3 deferred natives registered, `CLIENT VM native initialization
complete`. All 30 CLIENT natives registered, including
`NSChatWrite`/`NSChatWriteLine`/`NSChatWriteRaw`/`NSSendMessage`, whose
absence was the documented cause of the earlier "frozen on connect" report.

**What broke.**

```
FatalError: cl_northstar_client_init.nut: CLIENT SCRIPT COMPILE ERROR: Redefinition of enumeration "eDiscordGameState"
```

`BuildRuntimeManifest` emits an `initBlocks` entry declaring every mod's
`InitScript`, and that entry was `When: "SERVER || CLIENT"` whenever the VM
init hook was installed. That was correct while `RuntimeVmInit` only handled
UI: UI compiled the InitScript from the hook, and the manifest covered SERVER
and CLIENT. Extending `RuntimeVmInit` to CLIENT made CLIENT compile it twice -
once from the hook at VM creation, once from the manifest during the engine's
own boot compile pass - and the second pass is a hard Squirrel fatal.

`cl_northstar_client_init.nut` is Northstar.Client's `InitScript` only; it is
not in its `Scripts[]`, and the generated manifest contained exactly one
declaration of it. So this was purely the hook and the manifest overlapping.

Fixed by narrowing the block to `When: "SERVER"` once the hook is installed.
The invariant is now written next to the code: the manifest may only declare
the contexts `RuntimeVmInit` does not hook, and when SERVER is eventually
hooked this block goes away entirely rather than gaining another context.

`ClientCodeCallback_MapSpawn` was never reached, so CLIENT Before/After
dispatch is still unproven - the fatal happened first. PRX after the fix:
`2f3f57022e1816d70ae4e3ee1e02f7eb4c96571a855f99be1368f46921fea513`.


## Log capture: the Qt launcher truncates shad_log.txt (2026-09-17)

Two rounds of hands-on testing were reported and neither could be diagnosed,
because the evidence no longer existed by the time the log was read.
`shadps4.log` records how the Qt launcher starts the emulator:

```
main: Run: ["...shadPS4.exe", "--game", "D:/PS4/ShadPS4/CUSA04013\eboot.bin"]
```

No `--log-append`, so `shad_log.txt` is truncated on every launch. Any crash
or hang that is investigated after the game has been relaunched has already
lost its log. `Invoke-Stage2Iteration.ps1` passes `--log-append` and archives
the new lines, which is why automated probes are always diagnosable and manual
sessions were not.

`scripts/Start-NorthstarSession.ps1` covers the manual case: it passes
`--log-append`, never terminates the game (an automated probe kills it on a
pattern match, which is wrong for a play session), waits for the window to be
closed, and archives that session's lines plus a `result.json` under
`work/stage2/sessions/<timestamp>-<label>/`. The result records the installed
PRX SHA256 and the build record alongside markers for
`UI lifecycle completed`, `VM initialized context=1`,
`CLIENT lifecycle completed` and the first fatal line.

Recording the PRX hash per session is deliberate: an earlier retest in this
same sequence reproduced a fixed bug exactly, because the rebuilt PRX had
never been deployed - the deploy had failed on a file lock while the game held
the PRX open, and relaunching the game does not replace it. A session whose
hash does not match the build under test is the most misleading artefact in
this workflow.

### Two hypotheses ruled out while waiting for a usable log

- **Double compilation.** Every declaration in the generated manifest was
  evaluated against every context/mode combination plus every assignment of
  the free tokens each expression uses (`GAMEMODE_*`, `MAP_*`, `DEV`, ...):
  856 distinct paths, **0** that can compile twice in one VM. The 12 paths
  declared more than once are all vanilla's own mutually exclusive pairs
  (`CLIENT && SP` vs `CLIENT && MP`, `SERVER` vs `CLIENT`, ...). The
  InitScript collision fixed earlier was the only instance of that bug class.
- **Missing CLIENT natives.** All 54 scripts the installed mods declare for
  the CLIENT context reference only natives this runtime registers into that
  VM. (An earlier pass reported 28 scripts; that count came from a regex over
  `mod.json` that missed roughly half the entries. The conclusion held, the
  number did not.)

Note that `Northstar.Custom` declares `gamemodes/sh_gamemode_fw.nut` and
`gamemodes/cl_gamemode_fw.nut`, which do not exist in the mod folder. That is
not a packaging fault: both are stock `mp_common` scripts and the mod
re-declares them so they compile in MP. The filesystem hook finds no mod
override and falls through to the game archives, which is correct.


## Live stall captured during a connect: same signature as 2026-08-08 (2026-09-17)

First session captured with `Start-NorthstarSession.ps1`, read while the game
was still hung. Evidence preserved in
`work/stage2/sessions/20260917-174522-session/`.

**Sequence.** UI startup completed normally, then a healthy level load:
`menu_act01.bik`, the `client_mp_common.bsp.pak000_*` chunks,
`mp_forwardbase_kodai_loadscreen.rpak`, `ps4_all.starpak`, and
`client_mp_forwardbase_kodai.bsp.pak000_000`-`003`. It then streamed
materials and stopped partway through, last request
`materials/models/mendoko/interior/mendoko_ui_decals/mendoko_ui_decals_col.vtf`.
No fatal, no script error, no crash. Over 200,000 further log lines contain
no filesystem activity at all, only idle polling.

**Thread state - and a correction.** Every worker thread except `MainThrd`
and `chatserver:Net_FrameLoop` stops producing log lines within ~1,300 lines
of each other. That was first written up here as those threads "parking",
which the log does not actually support: `RenderThread`,
`shadPS4:GpuCommandProcessor` and the rest only log discrete events
(memory maps, file opens), so silence means no *loggable* event, not a
blocked thread. The 2026-08-08 note established real parking with `cdb`
thread stacks; nothing equivalent was captured here.

**The emulator stub spam is a frame clock, not a symptom.** `UpdatePlayTime`
lines are one per wall-clock minute, so the rate of `sceVoiceGetPortAttr`
from `MainThrd` can be measured against them:

| window | voice polls | file opens | rate |
|--------|-------------|------------|------|
| menu idle (35s)   | 2108 | 19   | ~60/s |
| loading (60s)     | 1316 | 7221 | ~22/s |
| stalled (60s)     | 3514 | 19   | ~59/s |
| stalled (60s)     | 3493 | 0    | ~58/s |

`sceVoiceGetPortAttr` is a once-per-frame poll: 60/s at an idle menu,
dropping to 22/s while the loader is actually working, and back to a steady
~58-59/s during the stall. `sceHttpWaitRequest` from the chat server tracks
the same curve. **The client is therefore running a full 60 fps frame loop
throughout the stall** - it is not frozen, deadlocked or starved. Neither
stub is implicated in the hang; both are useful as a frame clock precisely
because they are unconditional per-frame calls.

**This is the 2026-08-08 stall, not a regression.** That note records the
same map, the same VPK chunk range, the same "completely idle, no more asset
loads, no script messages, no FatalError" outcome, and the same
`UpdatePlayTime` minute-deltas with zero intervening progress - observed
under the retired Stage 1 VPK architecture, long before any of the runtime
mod loading, CLIENT lifecycle or manifest work. The one recorded difference
is that `cdb` showed MainThrd parked in a blocking wait then, whereas here it
still polls; `sceVoiceGetPortAttr` is a stub that logs on every call, so a
main thread pumping voice inside a wait loop is consistent with both.

**The 3P/PSN auth-token theory is disproven.** The relayed Discord lead
(recorded above) predicted the client might block fetching a console auth
token, and explicitly asked for a real connection attempt to be checked for
`sceNpAuth*` activity during the stall, since no earlier log came from an
actual connect. This log does, and contains **zero** `sceNpAuth*` calls
anywhere. The only `Np` traffic during the stall is `sceNpGetOnlineId`
returning `SIGNED_OUT`, which begins at the main menu (29 lines after
`UI lifecycle completed`), not at the connect, and so is ordinary background
polling rather than part of the signon path. `sceVoiceGetPortAttr` from
`MainThrd` likewise starts at the menu, so it is not a connect-time wait
either.

**Squirrel `print()` does not reach shadPS4 stdout.** `Northstar.DirectConnect`
prints `[DIRECT-CONNECT]: connecting to '<server>'` before issuing the
`connect` command and that string appears nowhere in the log, while this
module's own `LogFormat` output does, because it calls
`sceKernelDebugOutText` directly. Script-side prints cannot be used as
diagnostic markers on this port.

**The client log cannot see the netchannel.** The only `sceNet*` entries in
the whole log are `sceNetInit`, `sceNetPoolCreate` and `sceNetCtlInit`;
shadPS4 does not log per-packet socket traffic for this title. Combined with
the frame-rate finding above - a healthy client, rendering normally, with no
local work left to do - the remaining explanation is the one the 2026-08-08
note reached: the client is waiting on the next step of the server's
connect/signon sequence.

**Still the open item, unchanged since 2026-08-08:** the other half of this
handshake is only visible from the dedicated server's log for the same time
window. Nothing further can be concluded from the client side alone.


## First full CLIENT lifecycle, then a crash in VM teardown (2026-09-17)

A connect to the lobby produced the port's first complete CLIENT script
lifecycle, and then an access violation on the way out. Both halves are
useful.

**The CLIENT lifecycle ran.** `ClientCodeCallback_MapSpawn` dispatched for
the first time: 19 Before callbacks (`Progression_Init`,
`Sh_GamemodeChamber_Init`, `Sh_GamemodeHidden_Init`, `SNSMode_Init`,
`SHCreateGamemodeFW_Init`, `Sh_GamemodeGG_Init`, `Sh_GamemodeTT_Init`,
`Sh_GamemodeInfection_Init`, `Sh_GamemodeArena_Init`, `Sh_GamemodeKR_Init`,
`Sh_GamemodeFastball_Init`, `Sh_GamemodeHideAndSeek_Init`,
`ShGamemodeCTFComp_Init`, `Sh_GamemodeTFFA_Init`,
`FirstPersonSequenceForce1P_Init`, `BleedoutDamage_PreInit`,
`MessageUtils_ClientInit`, `Testing_Init`,
`Client_CustomScoreboardColumns_Init`) followed by the After set
(`InitialiseArenaLoadouts`, `RiffInstagib_Init`, `CustomAirAccelVars_Init`,
`Promode_Init`, `BleedoutDamage_Init`, `CustomOOBTimer_Init`,
`ClassicRodeo_InitPlaylistVars`, `CustomPilotCollision_InitPlaylistVars`, ...)
and `CLIENT lifecycle completed`. `NSUpdateGameStateClientStart` is declared
by Northstar.Client but defined nowhere, the CLIENT-side twin of
`NSUpdateGameStateUIStart`; it is logged and skipped, as PC does.

**The crash.** The final two lines were this module's own
`CLIENT VM lifecycle state cleared` followed by
`Unhandled Exception code 0xc0000005 at 0x960636e4`. client.sprx segment[0]
was at `0x959c4000`, so the faulting VA is `0x69f6e4`:

```
0069f6c0  push rbp; mov rbp,rsp; ...
0069f6ca  mov  r15, rdi                      ; SQString*
0069f6dc  mov  rcx, qword ptr [r15 + 0x18]   ; its shared-state back-pointer
0069f6e0  mov  rax, qword ptr [r15 + 0x28]
0069f6e4  mov  r9,  qword ptr [rcx + 0x4048] ; <-- fault, the string table
0069f6eb  movsxd rcx, dword ptr [r9 + 0xc]   ; then hash % bucket count,
0069f6ef  div  rcx                           ; and a bucket-chain walk
```

That is SQString release unlinking a string from the intern table, and it
faulted because `string+0x18` did not hold a valid `SQSharedState*`.

**Cause: `RegisterRuntimeConstants` never took ownership of its keys.** It
interned `VANILLA` and the four `NS_VERSION_*` names and inserted them into
the constants table without writing the shared-state back-pointer or taking
a reference. `SQString::Create` (0x6a96a0) does neither itself - disassembly
to its first `ret` contains no access to `+0x18` at all - and every engine
caller does both immediately after interning (`sq_pushstring` 0x682e00, the
script-function lookup 0x685cf0, the native registrar 0x684630). The table
therefore held unowned keys with a garbage `+0x18`, and releasing them at
teardown dereferenced it.

**Why it surfaced only now.** The bug predates this session's work, but was
unreachable: the constants table is only released when its VM is destroyed,
the UI VM lives for the whole session, and until this run no CLIENT VM had
ever been created *and* destroyed on this port. Extending
`RegisterRuntimeConstants` to the CLIENT context made a destroyed VM
possible for the first time, which exposed it.

Fixed by setting `keyString[0x18] = sharedState` and incrementing the
refcount before the insert, matching the engine's own pattern. PRX
`e9fdbbc7368043254307e9d3ecc9284deddf2e4e3ba0c41b31a2816661ca3124`; UI
startup still reaches `UI lifecycle completed` and the five constants still
register. **The fix is not yet confirmed against the case that broke:** that
needs another connect followed by leaving the map, so a CLIENT VM is created
and destroyed again.

This crash is unrelated to the connect stall documented above. The stall is a
healthy client at 60 fps waiting on the server; this was a teardown fault on
the way out of a map.


## shadPS4 updated: runtime compatibility re-verified, stub surface unchanged (2026-09-17)

The emulator was updated in place under the launcher's `Pre-release` folder,
which is the path both `Invoke-Stage2Iteration.ps1` and
`Start-NorthstarSession.ps1` already use, so no script changes were needed.
It is a different build: the launcher's own log moved its `main:` call site
from `main.cpp:125` to `main.cpp:145`/`150`.

**Nothing in this runtime depends on the emulator build**, because every gate
validates bytes in the game's own PRX files rather than anything shadPS4
provides. Re-verified on the new build anyway, since the hooks do depend on
`sceKernelMprotect` and `sceKernelGetModuleInfo` behaving: boot reached
`UI lifecycle completed` in 26.8 s with `OpenEx profile gate match=1`,
`OpenEx hook installed roots=4`, `localise gate addFile=1 accessor=1`,
`VM init hook installed protection=0`, `VM destroy hooks installed count=4
protection=0`, and both lifecycle call sites patched
(`va=31df24`, `va=768386`, both `protection=0`). No preimage mismatch. The
`IsSelfFile: Not a SELF file. Magic mismatch` lines are shadPS4's loader
distinguishing SELF from plain ELF and are unrelated to this module's gates.

**The stub surface is essentially unchanged.** Stubs called during a 27 s
boot: `sceVoiceGetPortAttr` 1160 times, then single init-time calls to
`sceVoiceInit`, `sceVoiceStart`, `sceVoiceCreatePort`,
`sceVoiceSetThreadsParams`, `sceVoiceConnectIPortToOPort`,
`sceGameLiveStreamingInitialize`, `sceNpRegisterStateCallbackForToolkit`,
`sceNetCtlInit`, `sceKernelGetProcessType`, `__sys_regmgr_call`,
`sceVideoOutSetWindowModeMargins`, `sceSystemServiceParamGetString`,
`scePadInit` and `sceCoredumpRegisterCoredumpHandler`. Voice is still not
implemented, which is why `sceVoiceGetPortAttr` continues to dominate the
log at roughly one call per frame. As established above that is a frame
clock rather than a fault, so this does not change any open item.

Whether the update changes the connect stall is untested and is worth a
retest, since a networking change there would be invisible from this
module's own gates.


## Teardown fix confirmed, and the out-of-sync disconnect identified (2026-09-17)

A five-session log captured a connect to `mp_lobby`. Two results.

### The VM teardown crash is fixed

The last session ran the exact path that faulted before and completed it:

```
314791  VM initialized context=1            (CLIENT VM created)
326708  CLIENT lifecycle completed          (MapSpawn dispatched)
327645  CLIENT VM lifecycle state cleared   (CLIENT VM destroyed - no crash)
328072  UI VM lifecycle state cleared
328103  VM initialized context=2            (new UI VM)
336804  UI lifecycle completed              (back at the menu)
```

The only `Unhandled Exception` in the whole log is at line 147998, in an
earlier session that predates the deploy. Between `CLIENT lifecycle completed`
and teardown the client loaded `resource/UI/HudVoice.res` and the GPU thread
compiled in-game graphics and compute pipelines, so it was rendering the
lobby, not merely loading it. It then left cleanly, released both VMs,
created a fresh UI VM and returned to the menu. This is the furthest this
port has reached.

### `#DISCONNECT_OUT_OF_SYNC` is a remote-function-table checksum mismatch

The on-screen "out of sync" error is not printed to stdout, so it does not
appear in the log at all. It comes from `client.prx`. The string
`#DISCONNECT_OUT_OF_SYNC` is at VA `0x90b286`, immediately adjacent to
`RemoteFunctionCall` (`0x90b29e`) and `RemoteFunctionCallsChecksum`
(`0x90b2b1`).

A registrar at `0x43e680` installs two network message handlers on the object
at `0x15f2950`:

| message | handler |
|---------|---------|
| `RemoteFunctionCall` | `0x43dae0` |
| `RemoteFunctionCallsChecksum` | `0x43e520` |

The server's checksum lands in the global at `0x1e44b34`, and two sites
compare against it and disconnect on mismatch:

```
0043dea0  mov eax, dword ptr [r14 + 0x2fc]   ; the client's own table checksum
0043dea7  cmp eax, dword ptr [rip + ...]     ; vs the server's, at 0x1e44b34
0043dead  je  0x43decc                       ; equal: continue
0043deaf  mov rdi, qword ptr [rip + ...]     ; engine interface at 0xb2f0a0
0043deb6  lea rsi, [rip + ...]               ; #DISCONNECT_OUT_OF_SYNC
0043dec3  call qword ptr [rax + 0xc8]        ; Disconnect(reason)
```

with the same comparison repeated at `0x43e64c` on the receive path.

**Interpretation.** The client and server each checksum their registered
remote-function-call table, and Titanfall drops the client when they differ.
That table is a product of which scripts were compiled and in what order, and
this port compiles a *generated* `scripts.rson`
(`initBlocks + original + modBlocks`) whose contents and ordering are this
runtime's own construction, not PC Northstar's. A mismatch is therefore the
expected outcome rather than a surprise, and it only became reachable now
that the CLIENT VM compiles and registers remote functions at all.

Northstar's own `Northstar.CustomServers/mod/scripts/vscripts/lobby/_private_lobby.gnut:91`
carries a matching comment - `// GameRules_SetGameMode( args[0] ) // can't do
this here due to out of sync errors with new clients` - confirming this error
class is familiar on PC too.

**Next step:** make the CLIENT VM's remote-function table match what a PC
Northstar server builds. That means reconciling the generated manifest's
script set and ordering with PC's, and identifying exactly which declarations
feed `[r14+0x2fc]`. Not attempted yet.


## Client profile was two Northstar versions behind the server (2026-09-17)

After the `#DISCONNECT_OUT_OF_SYNC` finding the server was moved to the
stable release, and the client then hung in endless loading. The cause of the
mismatch turned out to be straightforward and worth recording, because
nothing in the client log makes it visible.

The installed PS4 profile was carrying **`0.0.0.1+dev`** builds of all three
core mods while the PC source (and therefore the server) was on **`1.31.13`**.
Comparing the two trees file by file:

| mod | identical | differing | only in the release |
|-----|-----------|-----------|---------------------|
| Northstar.Client | 84 | 3 | 0 |
| Northstar.Custom | 124 | 1 | 0 |
| Northstar.CustomServers | 493 | 3 | 110 |

The 110 missing files are 88 `.nm` navmeshes and 22 `.ain` AI graphs -
server-side AI data that a client never compiles, so not checksum-relevant.
The seven differing files are: all four `mod.json`,
`ui/menu_ns_serverbrowser.nut`, `ui/menu_private_match.nut`,
`cfg/autoexec_ns_server.cfg` and `gamemodes/_hardpoints.gnut`. The release
also declares one script the dev copy does not,
`sh_custom_scoreboard_columns.gnut` in Northstar.Custom. (The apparent
`sh_northstar_http_requests.gnut` difference is only JSON whitespace.)

A differing declared script set is exactly what changes the compiled script
set, and therefore the remote-function-call table the previous note traced
`#DISCONNECT_OUT_OF_SYNC` to. **Client and server must run identical mod
versions**, which is easy to lose track of here because the PS4 profile is a
*copy* of the PC mods rather than the same directory.

Repackaged with `New-NorthstarProfile.ps1` from the updated source and
verified: 818 files byte-identical to the PC mods, all three core mods now
`1.31.13`. `Northstar.DirectConnect` was re-added by hand afterwards rather
than through `-IncludePs4CompatibilityMods`, because that switch would also
install `Northstar.PS4`, whose script stubs have been replaced by native
adapters and whose extra scripts would change the compiled set again.
DirectConnect declares a single `RunOn: "UI"` script, which cannot contribute
to a remote-function table. The previous profile is preserved at
`work/stage2/deploy-backups/20260917-184731-R2Northstar-pre-1.31.13`.

Boot verified afterwards: 4 mods discovered at the expected versions,
manifest regenerated (106 scripts), `UI lifecycle completed`, and the CLIENT
native scan still reports no unregistered natives across the 54 CLIENT-context
scripts the newer mods declare.

**Unresolved and worth separating:** the endless-loading stall reproduces on
`mp_forwardbase_kodai` and stops at the same asset every time
(`materials/models/mendoko/interior/mendoko_ui_decals/mendoko_ui_decals_col.vtf`),
whereas `mp_lobby` loaded, ran the full CLIENT lifecycle and rendered. Whether
the version mismatch also caused that stall is untested - every stall so far
predates the profile update.


## Playable attrition, and three now-separate failures (2026-09-17)

With client and server both on `1.31.13` the out-of-sync disconnect is gone
and **attrition connects and plays**. That is the project's first real match.
What remains splits into three distinct problems rather than one.

### 1. Attrition: playable, with occasional crashes

No captured log yet. Needs a `Start-NorthstarSession.ps1` run that reproduces
one, since the crash is the only one of the three with no diagnosis at all.

### 2. Fastball: hangs on the loading screen

Every stall captured so far was loading `mp_forwardbase_kodai` and stopped at
the same asset,
`materials/models/mendoko/interior/mendoko_ui_decals/mendoko_ui_decals_col.vtf`,
with the client still running a full 60 fps frame loop. Attrition now loads a
map successfully, so the loader is not broken in general.

**The decisive control has not been run:** every failure so far is
gamemode *and* map confounded. Attrition on `mp_forwardbase_kodai`, or
fastball on whatever map attrition succeeded on, would separate "fastball's
custom content" from "this particular map". Worth doing before any code is
written for it.

### 3. Private lobby: persistence is not available to the client

The dialog is a script error at `ui/menu_private_match.nut#378`, which is:

```squirrel
while ( player.GetPersistentVarAsInt( "initializedVersion" ) < PERSISTENCE_INIT_VERSION )
    WaitFrame()
```

`GetPersistentVarAsInt` raises `Persistent data not available`. That string is
at client.prx VA `0x8e7e2f`, beside the rest of the persistence accessor's
error set (`Blank var name not allowed`, `Invalid var name '%s'`,
`Specified var is a struct`). The guard that reaches it is at `0x2f89e2`:

```
002f89e2  mov  rdi, qword ptr [rip + ...]  ; engine interface singleton 0xb2f0a0
002f89e9  xor  esi, esi                    ; client index 0 - the "#0" in the message
002f89f1  call qword ptr [rax + 0xb60]     ; IsPersistentDataAvailable(0)
002f89f7  test al, al
002f89f9  je   0x2f8ad0                    ; false -> "Persistent data not available"
```

That is the same singleton the out-of-sync disconnect path uses
(`0xb2f0a0`, called through `[rax+0xc8]`), so it is a central engine
interface rather than anything this port installs.

**Where persistence is meant to come from.** Northstar.CustomServers'
`autoexec_ns_server.cfg` documents the tradeoff on the cvar itself:
`ns_auth_allow_insecure 0 // keep this to 0 unless you want to allow people to
join without masterserver auth/persistence`. Insecure mode therefore has no
masterserver pdata by design. PC compensates server-side:
`serverauthentication.cpp` sets `m_iPersistenceReady = READY_INSECURE` with
the comment *"actual placeholder persistent data is populated in script with
InitPersistentData()"*, and Northstar.CustomServers calls
`InitPersistentData( player )` from `CodeCallback_OnClientConnectionCompleted`
(`mp/_base_gametype_mp.gnut:146`), before its `IsLobby()` branch, so a lobby
should receive placeholder pdata like any other mode.

So either that server-side initialisation is not running for this connection,
or its result is not reaching the PS4 client in time for `OnLobbyMenu_Open`.
Attrition playing successfully suggests pdata does arrive in a normal match,
which points at the lobby reading it earlier than it becomes available.
Distinguishing the two needs the server's log for a lobby connect; the client
cannot see which.

Note this is **not** the SERVER-context persistence native gap tracked in
"Remaining native work" item 3 (`NSIsWritingPlayerPersistence`,
`NSEarlyWritePlayerPersistenceForLeave`). Those run in the server's own VM on
the PC host and are unrelated to a PS4 client reading its own pdata.


## Why the fastball icon is missing: custom modes reach the UI by a CLIENT->UI call (2026-09-17)

The lobby persistence error resolved itself once client and server matched.
The remaining observation - fastball has no icon when switching gamemodes -
turns out to depend on a cross-VM call, which is worth writing down because
nothing about it is visible in the log.

The registration chain for any custom gamemode is:

1. `gamemodes/sh_gamemode_fastball.gnut` (`( CLIENT || SERVER ) && MP`) runs
   `Sh_GamemodeFastball_Init`, which only *registers* a callback via
   `AddCallback_OnCustomGamemodesInit`. This is confirmed to run: it appears
   in the CLIENT Before callback list.
2. Northstar's override of `gamemodes/sh_gamemodes.gnut` calls
   `PrivateMatchModesInit()` and `InitCustomGamemodes()` inside
   `#if SERVER || CLIENT`, which fires those callbacks, so
   `CreateGamemodeFastball` runs and calls `AddPrivateMatchMode( "fastball" )`.
3. `AddPrivateMatchMode` in `lobby/sh_lobby.gnut` appends to its own list and
   then mirrors the result into the other VM:

```squirrel
void function AddPrivateMatchMode( string mode )
{
    if ( !file.modes.contains( mode ) )
        file.modes.append( mode )
    #if CLIENT
        // call this on ui too so the client and ui states are the same
        RunUIScript( "AddPrivateMatchMode", mode )
    #endif
}
```

4. The UI menus (`ui/menu_mode_select.nut`, `ui/menu_ns_serverbrowser.nut`,
   `ui/menu_stats_maps.nut`, all Northstar.Client) list modes from
   `GetPrivateMatchModes()`, which just returns that mirrored list.

So the UI never registers custom gamemodes itself - neither
`sh_gamemode_fastball.gnut` nor `sh_gamemodes_custom.gnut` is compiled in UI,
both being `(CLIENT || SERVER) && MP`. The mode list exists in the UI VM only
because the CLIENT VM pushed it there through `RunUIScript`. A missing mode in
the menu therefore means one of steps 2-3 did not complete.

`RunUIScript` is the engine's own native at client VA `0x767b10`. This module
validates its preimage in `ProbeUiVm` but never hooks or replaces it, so it is
running vanilla engine code.

**This cannot be narrowed further from the current logs**, because every
diagnostic in that chain is a Squirrel `print()` - including
`InitCustomGamemodes`'s own - and script prints do not reach shadPS4 stdout on
this port (established earlier: `Northstar.DirectConnect`'s
`[DIRECT-CONNECT]` print never appears while this module's `LogFormat` does,
because the latter calls `sceKernelDebugOutText` directly).

**Next step, and it unblocks more than this issue:** route the engine's script
print path into `sceKernelDebugOutText` so Squirrel `print`/`printt` output
lands in the log. That would immediately show whether `InitCustomGamemodes`
ran, and would give script-level visibility for the fastball load hang and the
attrition crashes as well. The engine's own error printing (`FatalError:`,
`Error: [N] KeyValues Error:`) already reaches stdout, so a usable sink
exists; what is missing is the connection from the script print native to it.


## Script output now reaches the log (2026-09-17)

Squirrel `print`, `printl`, `printt` and `Msg` produced nothing in the module
log, which blocked diagnosis of the fastball load hang, the attrition crashes
and the missing gamemode icon alike, since every useful marker in those paths
is a script print.

**Where script output goes.** The Squirrel base library registers `print` at
VA `0x6d0b30`; its registration entry is at `0xa83ea8` and names the string at
`0x92ddc5`. The native ends:

```
006d0b84  mov  rax, qword ptr [rbx + 0x50]    ; sqvm -> SQSharedState
006d0b88  mov  rcx, qword ptr [rax + 0x4350]  ; SQSharedState::_printfunc
006d0b8f  test rcx, rcx
006d0b92  je   0x6d0ba2                       ; null: returns silently
006d0b94  lea  rsi, [rip + ...]               ; "%s"
006d0ba0  call rcx                            ; printfunc(vm, "%s", text)
```

Everything funnels through it: `printl( text )` is `print( text + "\n" )`,
`printt( ... )` joins its arguments and calls `printl`, and `Msg` calls
`print` (`ui/init.nut`, `ui/_threads.nut`). Mods use `printt` 999 times,
`print` 154 and `printl` 71, so one sink covers all of it.

**A prediction that was wrong, and the correction.** The expectation was that
`_printfunc` would be null, making prints silently discarded. It is not: the
first run reported `previous=0x9603fae0`, which is engine code. Script output
was never being dropped - it was going to the engine's own console sink, which
does not reach shadPS4 stdout the way this module's `sceKernelDebugOutText`
output does. The implementation therefore **tees**: it logs the formatted text
and then forwards to the previous function, so the in-game console keeps
working. Had the original been replaced outright, console output would have
been silently removed to gain log output.

**Implementation** (`launcher/src/runtime_script_print.inl`). Installing a
`_printfunc` is a data write into the VM's own shared state, so no call site is
patched and the engine's `print` implementation runs unchanged. It is gated on
two preimages: `0x6d0b30` for the native itself and `0x6d0b84` for the sink
read, the latter chosen because it literally encodes both offsets the hook
depends on (`mov rax,[rbx+0x50]`, `mov rcx,[rax+0x4350]`). A mismatch refuses
and logs rather than writing a function pointer into an unknown field. One
entry per `SQSharedState` is recorded with the context label captured at
install time, so lines are tagged without reaching into lifecycle globals.
Installation happens in `RegisterRuntimeConstants` immediately after the shared
state is resolved and before constant or native registration, so script output
is available even when the rest of setup fails.

**Verified.** Boot log now contains lines such as:

```
[NorthstarPS4] UI script print installed shared=0x202589ac0 previous=0x9603fae0
[NorthstarPS4] [UI script] SAVEGAME try is valid savegame
[NorthstarPS4] [UI script] mvp - 13
```

PRX `7784da5b55dc851ff7469dad5a54176a14ef5d218ee1ef735d91a824a9289474`; UI
startup unchanged, host tests pass. Only 16 script lines appear at the menu,
which is expected - the value is during a connect, where `InitCustomGamemodes`
and the gamemode registration chain print, and where the hang and crash live.


## Root cause of the custom-gamemode out-of-sync: client GAMETYPE is wrong (2026-09-17)

The script print sink paid for itself on its first real connect. A lobby
connect now yields, from the client's own scripts:

```
[UI script]     [DIRECT-CONNECT]: connecting to '192.168.0.145:37015'
[UI script]     UICodeCallback_LevelLoadingStarted:
[UI script]     UICodeCallback_UpdateLoadingLevelName: mp_lobby
[UI script]     UICodeCallback_LevelInit: mp_lobby
[CLIENT script] InitCustomGamemodes
[CLIENT script] GAMETYPE: tdm
[CLIENT script] MAX_TEAMS: 2
[CLIENT script] InitCustomNetworkVars
[UI script]     UICodeCallback_ErrorDialog: Out of sync with server.
[UI script]     UICodeCallback_LevelShutdown: mp_lobby
```

Three findings, in order of importance.

**1. `InitCustomGamemodes` does run on the client.** The custom-gamemode
registration chain is intact, so the missing fastball icon is *not* caused by
that hub failing. It must be the `RunUIScript( "AddPrivateMatchMode", mode )`
step or something past it. That is now a separate, narrower question.

**2. The client's `GAMETYPE` is `tdm`.** `_settings.nut`'s `Settings_Init`
resolves it from the engine:

```squirrel
#if SERVER
    if ( GameRules_GetGameMode() == "" )
        GameRules_SetGameMode( "tdm" )   // note: SERVER only
#endif
GAMETYPE = GameRules_GetGameMode()
printl( "GAMETYPE: " + GAMETYPE )
```

The `tdm` fallback is compiled `#if SERVER`, so a client reporting `tdm` is
reporting what the engine actually holds for it, not a script-side default.

**3. That is exactly what breaks the checksum.** Custom gamemodes gate their
remote-function registration on `GAMETYPE`:

```squirrel
void function FastballRegisterNetworkVars()
{
    if ( GAMETYPE != GAMEMODE_FASTBALL )
        return
    Remote_RegisterFunction( "ServerCallback_FastballUpdatePanelRui" )
    Remote_RegisterFunction( "ServerCallback_FastballPanelHacked" )
    Remote_RegisterFunction( "ServerCallback_FastballRespawnPlayer" )
}
```

With the server on fastball and the client believing `tdm`, the server
registers three remote functions the client does not. The remote-function-call
table checksum traced earlier therefore differs, and `0x43dea0` disconnects
with `#DISCONNECT_OUT_OF_SYNC` - the dialog now visible in the log as
`UICodeCallback_ErrorDialog: Out of sync with server.`

**This also explains why attrition works and fastball does not.** Attrition
and TDM are vanilla modes: they register no custom network vars, so the table
matches whatever the client believes the gamemode is. Only modes that call
`AddCallback_OnRegisteringCustomNetworkVars` are sensitive to a wrong
`GAMETYPE`, and every one of those is a Northstar custom mode. It is
consistent with Northstar's own comment at
`lobby/_private_lobby.gnut:91` - `// GameRules_SetGameMode( args[0] ) //
can't do this here due to out of sync errors with new clients`.

**Next step:** find how a client is supposed to learn the server's gamemode
before `Settings_Init` runs, and why this port ends up with `tdm`.
`GameRules_GetGameMode()` is an engine call, and PC Northstar does not patch
it, so the value should arrive over the connection. A cheap discriminator
first: connect to the working attrition server and read the `GAMETYPE:` line.
If it also says `tdm`, the client never receives the gamemode at all and the
vanilla modes were only ever passing by luck; if it says `at`, the value does
arrive and something about the lobby or custom modes is specific.

### Correction and refinement (same day, after two more lobby connects)

The section above concluded that a wrong client `GAMETYPE` was the root cause.
That was stated with more confidence than the evidence supports, and two more
captured connects refine it.

Both reached `mp_lobby` and both reported `GAMETYPE: tdm`, and a third connect
to a different port timed out, so **no reading has yet been taken from the
attrition server that works**. The discriminator that section asked for was
not obtained.

More importantly, `tdm` in a lobby may well be *correct on both sides*.
Northstar deliberately does not set the game mode there - that is the whole
point of the `_private_lobby.gnut:91` comment, `// GameRules_SetGameMode(
args[0] ) // can't do this here due to out of sync errors with new clients`.
If the server is also `tdm` in the lobby, then neither side registers custom
network vars and `GAMETYPE` cannot be what differs. The reasoning in the
previous section is sound *only* if the server's gamemode differs from the
client's, which is unverified.

The timeline also does not fit a connect-time rejection:

```
330753  VM initialized context=1
341022  [CLIENT script] GAMETYPE: tdm
342566  CLIENT lifecycle completed
342780  [UI script] UICodeCallback_LevelInit: mp_lobby
        [UI script] menu_PrivateLobbyMenu menu opened     <- the lobby worked
343498  [UI script] UICodeCallback_ErrorDialog: Out of sync with server.
        [UI script] UICodeCallback_LevelShutdown: mp_lobby
```

The client loaded the lobby and successfully opened `PrivateLobbyMenu` before
being disconnected. That points at the second of the two checksum comparison
sites found earlier - `0x43e64c`, on the handler for an incoming
`RemoteFunctionCallsChecksum` message - rather than the connect-time check at
`0x43dea0`. In other words the server sent a checksum *after* the client was
already in the lobby, and that is what did not match.

**What is actually established:** the disconnect is a remote-function-table
mismatch that arrives after a successful lobby load, and vanilla-mode matches
are unaffected. **What is not:** whether the two sides disagree about
`GAMETYPE`, and whether the mismatching table comes from a gamemode change in
the lobby or from this port compiling a different script set than the server.
Both remain open, and the client log alone cannot separate them.

### The print hook table was too small, and it cost a test round

The session that followed produced a clean behavioural pattern from the user:
vanilla modes work, custom modes do not. Connecting to the lobby with
**attrition** selected loaded the map; with **fastball** selected it
disconnected out of sync; connecting while the server was mid-match on
**fastball** or **gun game** connected but never entered the game, and ending
the server's match redirected to the lobby and then disconnected out of sync.

The log appeared to show something dramatic for those custom-mode connects:
three CLIENT VMs were created and reached `CLIENT lifecycle completed` while
printing *nothing at all* - no `InitCustomGamemodes`, no `GAMETYPE:`. That
looked like client script init dying early for custom modes.

It was not. It was this module's own instrumentation:

```
26682  | UI script print installed     shared=0x20258c5a0
81618  | CLIENT script print installed shared=0x20a039d80
94510  | UI script print installed     shared=0x2033e0dc0
259151 | UI script print installed     shared=0x20258b320
330754 | CLIENT script print installed shared=0x20a0351e0
343753 | UI script print installed     shared=0x2033de980
446472 | CLIENT script print installed shared=0x20a69e420
503865 | script print hook table full
559286 | script print hook table full
641871 | script print hook table full
```

`kMaxScriptPrintHooks` was 4 and `InstallScriptPrint` refused once full. Every
VM creation allocates a fresh `SQSharedState`, and entries were never released
when a VM died, so after seven VMs the table was exhausted and the three
connects that mattered captured no script output whatsoever. **No conclusion
can be drawn from their silence.**

Fixed three ways: the table is now 16 entries; `RemoveScriptPrint` releases the
entry from the destroy hook while the shared state is still valid, which also
prevents mislabelling a later VM the allocator hands the same address; and a
full table now evicts its oldest entry instead of refusing, since a stale
entry can only refer to an already-freed shared state. PRX
`adbff109ec58f7e38ec9f35b2248fd6fe98608581b208c34a4ce14bc40daaee9`.

The behavioural pattern the user established stands on its own and is the more
useful result: **every failing case is a Northstar custom gamemode and every
working case is a vanilla one**, which is consistent with the
remote-function-table mechanism and folds the "fastball hangs on loading"
symptom into it - that connect reached the server and never entered the game,
rather than failing to load content.


## Root cause of every custom-gamemode failure: KeyValues patches are never applied (2026-09-17)

The client receives the server's gamemode correctly. A lobby connect now
reports `[CLIENT script] GAMETYPE: aitdm`, not the `tdm` default, which kills
the "the client never learns the gamemode" hypothesis outright.

The real gap is that **this port does not implement Northstar's KeyValues
patch mechanism at all**, so the client runs the stock playlist file while the
PC server runs a merged one.

`Northstar.Custom/keyvalues/playlists_v2.txt` is an 879-line patch that
defines every custom gamemode: `arena`, `chamber`, `ctf_comp`, `fastball`,
`gg`, `hidden`, `hs`, `inf`, `kr`, `sns`, `tffa`, `tt`.
`Northstar.CustomServers` ships another. Neither is ever read. The engine asks
for `playlists_v2.txt`, the filesystem overlay checks each mod root and every
one misses:

```
3 path = /app0/R2Northstar/mods/Northstar.Client/mod/playlists_v2.txt        -> failed
3 path = /app0/R2Northstar/mods/Northstar.Custom/mod/playlists_v2.txt        -> failed
3 path = /app0/R2Northstar/mods/Northstar.CustomServers/mod/playlists_v2.txt -> failed
3 path = /app0/R2Northstar/mods/Northstar.DirectConnect/mod/playlists_v2.txt -> failed
```

then falls through to the vanilla file. The overlay roots are
`<mod>/mod`, and KeyValues patches live in `<mod>/keyvalues`, a *sibling* of
`mod/` and therefore outside every served root. It is not a path bug: these
files are not meant to be served directly at all.

**What PC does.** `ModManager::TryBuildKeyValues`
(`mods/compiled/modkeyvalues.cpp`) writes a generated KeyValues file that
`#base`-includes each enabled mod's patch - later mods first - alongside a
copy of the original, and serves that in place of the requested file. The
engine's own KeyValues loader performs the merge through `#base`.

**This explains the entire custom-gamemode pattern** that was previously split
across three symptoms:

- custom modes missing from the Modes menu, and no fastball icon: the client's
  playlist has no such gamemodes
- "the gamemode menu loads weirdly": the menu is built from a playlist missing
  those entries
- connecting to a custom-mode match connects but never enters the game: the
  client cannot resolve a gamemode it has no playlist entry for
- out of sync on custom modes: playlist vars drive gamemode setup, so the two
  sides do not agree
- attrition and TDM work: they are in the stock playlist

It also retires the map-versus-gamemode control that kept being requested -
the variable was never the map.

**The fix has a precedent in this codebase.** `BuildRuntimeManifest` already
generates a merged `scripts.rson` into `/data/northstar_ps4/` and serves it
from the `OpenEx` hook. A KeyValues equivalent is the same shape: detect a
request for a file some enabled mod patches, generate a `#base` wrapper plus
copies of the patches into the writable guest directory, and serve that. The
one new requirement is that the `#base` targets resolve relative to the
generated file, so the patch copies must sit beside it.

Listed in "Remaining native work" item 3 as "generated KeyValues/assets";
this is the first evidence of what it actually costs.


## KeyValues patches implemented (2026-09-17)

`launcher/src/runtime_keyvalues.inl`, wired into the `OpenEx` hook. Same shape
as `BuildRuntimeManifest`: generate into the writable guest directory and serve
the generated file, never touching mod sources or game archives.

**Behaviour, matching `ModManager::TryBuildKeyValues`.** At filesystem-hook
install time each enabled mod's `keyvalues/` tree is scanned recursively and
the relative paths recorded. When the engine later requests one of those paths,
the runtime writes into `/data/northstar_ps4/kv/`:

- `mod_patch_<n>_<leaf>` - a copy of each mod's patch, **highest priority
  first**, because `#base` does not override keys that already exist, so the
  earliest include wins
- `mod_original_<leaf>` - a copy of the engine's own file, read through
  `g_originalFsOpenEx` so this module's hook is bypassed and the vanilla
  content is what gets copied
- `<leaf>` - the wrapper, which `#base`-includes the patches, then the
  original last, then declares an empty root object

The root object name is parsed out of the original the way PC does it: skip
whitespace, `//` comments and `#` directives to the first identifier.

**Verified end to end.** Boot discovers nine patches across the installed mods -
`playlists_v2.txt`, `resource/fontfiletable.txt`,
`scripts/aisettings/npc_pilot_elite.txt` and five `scripts/weapons/*.txt` -
none of which were ever applied before. The playlist builds and is served:

```
keyvalues patch mod_patch_0_playlists_v2.txt <- .../Northstar.Custom/keyvalues/playlists_v2.txt (15295 bytes)
keyvalues patch mod_patch_1_playlists_v2.txt <- .../Northstar.CustomServers/keyvalues/playlists_v2.txt (118 bytes)
keyvalues built playlists_v2.txt root=playlists patches=2 original=360403 bytes
keyvalues served: playlists_v2.txt
```

and the generated wrapper is exactly the PC shape:

```
// AUTOGENERATED: MOD PATCH KV
#base "mod_patch_0_playlists_v2.txt"
#base "mod_patch_1_playlists_v2.txt"
#base "mod_original_playlists_v2.txt"
playlists
{
}
```

Critically, **the engine resolved every `#base` target**, each opened once from
`/data/northstar_ps4/kv/` with no failures. It resolved them relative to the
*served* file's directory rather than the requested path, so the generated
files are found without the requested path mattering; the prefix match on
`mod_patch_`/`mod_original_` in the hook is retained as a fallback in case a
different caller resolves them the other way.

The merged patch defines `arena`, `chamber`, `fastball`, `gg` and the rest, so
the client's playlist now contains the custom gamemodes the server has. PRX
`49723ea33fb4eaf6b3f7ae3b7edadcfb61e871ce62a8a6c8bd94c7b09699c21c`. UI startup
unchanged, host tests pass, **zero VPKs modified**.

**Baseline note.** The mod integrity check now reports 7 changed files against
`mods-before.json`: the four `mod.json`, `ui/menu_ns_serverbrowser.nut`,
`ui/menu_private_match.nut`, `cfg/autoexec_ns_server.cfg` and
`gamemodes/_hardpoints.gnut`. Those are exactly the deliberate `0.0.0.1+dev` ->
`1.31.13` profile update, not drift. A fresh baseline for the current profile
is recorded at `work/native-loading/mods-1.31.13.json` (823 files);
`mods-before.json` is kept as the dev-mod record. The VPK baseline is untouched
and still reports zero changes, which is the invariant that matters.

**Not yet proven:** that this fixes the custom-gamemode failures. The playlist
now contains them, but whether the Modes menu lists them, whether a fastball
match loads, and whether the out-of-sync disconnect is gone all need a live
connect.

### The #base delegation does not work on this engine, and how the test missed it

Serving the generated wrapper broke boot: `FatalError: Failed to load playlist
data`, no main menu. The wrapper declares an empty root object and expects all
content to arrive through `#base`; this engine build read the wrapper and
**never opened a single `#base` target**, so the playlist came back empty.

**The verification in the previous section was wrong.** It claimed the engine
resolved every `#base` target, citing three opens of the generated files. The
flags tell the real story:

```
path = /data/northstar_ps4/kv/mod_original_playlists_v2.txt flags = 0x601   <- this module writing
path = /data/northstar_ps4/kv/mod_patch_0_playlists_v2.txt  flags = 0x601   <- this module writing
path = /data/northstar_ps4/kv/mod_patch_1_playlists_v2.txt  flags = 0x601   <- this module writing
path = /data/northstar_ps4/kv/playlists_v2.txt              flags = 0x601   <- this module writing
path = /data/northstar_ps4/kv/playlists_v2.txt              flags = 0x0     <- the engine reading
```

Only the wrapper was ever read. `flags = 0x601` is a create/write open by this
module; the engine's read is `flags = 0x0`. **An open in this log is not
evidence of the engine reading a file until its flags are checked.**

**The boot test also could not have caught it.** The success pattern was
`UI lifecycle completed`, and `Invoke-Stage2Iteration.ps1` terminates the
process the moment the pattern matches. The playlist load happens *after* UI
lifecycle completion, so the run was declared a success and killed before the
fatal error could appear. Any check that stops at the first good marker cannot
detect a failure that comes later.

Fixed for future runs by testing against a marker that is genuinely late in
boot and by making the fatal itself a failure pattern:

```
-SuccessPattern 'UICodeCallback_ActivateMenus: menu_MainMenu'
-FailurePattern 'Failed to load playlist data|SCRIPT COMPILE ERROR|SIGSEGV|...'
```

That marker is a script print, so it only became usable once the script print
sink existed.

**Current state.** `kKeyValuesServeGenerated` is `false`: discovery and
generation still run and still log, but nothing is served, so behaviour is
identical to before the feature existed. Boot verified to the real main menu
with zero playlist errors, PRX
`ad979da26ae16fbcb1fc5fdad419e5fc60a4a86d5fb86bde55e5bd6d5fde66fa`.

**What the real fix has to be.** The merge cannot be delegated to the engine
through `#base`; this module has to perform it and emit one complete file.
That means a KeyValues parser and a recursive merge with Northstar's
precedence (a patch overrides the original, and higher-priority mods override
lower). The existing discovery, patch enumeration, original retrieval and
root-name parsing are all still correct and reusable - only the "write a
wrapper and hope" step is wrong. It is also worth checking whether the engine
honours `#base` anywhere at all before assuming the mechanism is simply
absent, since PC relies on it for every patched KeyValues file.

### Does #base work on this engine? Yes - but not in the playlist loader

Checked against the game's own shipped data, which is stronger evidence than
any probe: 231 vanilla files rely on `#base`, so the directive is genuinely
supported. Where they live is the useful part:

| directory | vanilla files using #base |
|-----------|---------------------------|
| `scripts/aisettings` | 128 |
| `scripts/players/mp` | 69 (`.set`) |
| `resource/ui/menus` | 17 |
| `scripts/weapons` | 14 |
| `scripts/screens` | 2 |
| `resource/ui` | 1 |

for example `scripts/weapons/melee_titan_punch_fighter.txt` opens with
`#base "melee_titan_punch.txt"`. Those loaders must therefore process it.

**No vanilla playlist uses `#base`, and no `resource/*.txt` does either.**
Combined with the observed failure - the engine read the generated wrapper and
opened none of its includes - the conclusion is that `playlists_v2.txt` is
parsed by a loader that does not implement the directive, rather than the
directive being absent from the build.

Mapping that onto the nine patches the installed mods ship:

| patch | `#base` viable |
|-------|----------------|
| `scripts/weapons/*.txt` (6 files) | yes, proven by vanilla |
| `scripts/aisettings/npc_pilot_elite.txt` | yes, proven by vanilla |
| `resource/fontfiletable.txt` | unproven - no `resource/*.txt` uses it |
| `playlists_v2.txt` | **no** |

So the cheap path covers seven of nine, and the one that actually gates the
custom gamemodes is the one it cannot cover. The playlist still needs this
module to parse and merge KeyValues itself and emit a single complete file.

## KeyValues merged in-module; custom gamemodes load (2026-09-17)

`launcher/include/northstar_ps4/keyvalues.h` parses, merges and serialises
Valve KeyValues, and `runtime_keyvalues.inl` now emits one complete merged file
per patched path instead of a `#base` wrapper. Verified boot with PRX SHA256
`71867f6a5a981b9bfc63039a26e7f3260ab2f8ddbc0095f03e7de037c9b0d7dc`:
`playlists_v2.txt` merges to 348,560 bytes from a 360,403-byte original plus
two patches, `Gamemodes` goes from 21 entries to 35, and the client reaches
`GAMETYPE: fastball` on `mp_forwardbase_kodai` - the first custom gamemode to
load on this port.

Three things had to be right, and two of them were only found by checking the
real files rather than reasoning about the format.

**The engine reads a served file short unless both Size interfaces are hooked.**
The first working merge produced a 372,535-byte playlist and died with
`FatalError: KeyValues Error: Error reading token in file playlists`. Byte
360,403 of the merged file - exactly the original's length - lands mid-value
inside `LocalizedStrings/lang/Tokens/PL_amped_tacticals_desc`, which is the
breadcrumb the error printed. The first fix hooked `Size(fileName, pathID)` on
primary filesystem vtable slot 135 (`filesystem_stdio.prx` + `0xde20`), but the
playlist loader uses secondary interface slot 7. That entry is a direct
`this -= 8; jmp 0xde20` thunk at `+0xe1e0`, so it bypasses the primary vtable.
The runtime now copies and hooks that secondary slot too, adjusts its `this`
pointer back to the primary object and returns the generated file's cached
serialised length. Caching matters: the platform libc `stat` layout reported a
348,560-byte file as 681 bytes in this module. Generated merges may therefore
be larger than retail without being truncated. They remain unindented to save
about 24 KB of startup I/O, and `Test-NorthstarProfile.ps1` parses and validates
every real merged file rather than imposing the obsolete size limit.

**Escape sequences must pass through untouched.** The playlist holds 2,525
`\n` and 138 `\"` inside localised strings. Decoding them on parse and not
re-encoding them on write silently dropped every backslash, which no structural
check catches - it just runs 2,525 menu descriptions together. The parser now
keeps the source bytes and the serialiser writes them back, so the merged file
says byte for byte what the original said. Rescanning the shipped playlist with
escapes disabled turns 639 keys into fragments of German prose, which is how we
know the engine honours them.

**Platform conditionals are syntax, not tokens.** `[$PC]`,
`[!$JAPANESE && !$TCHINESE]` and friends attach to the entry before them.
Reading one as an ordinary token shifts every following key/value pair by one;
`resource/fontfiletable.txt` failed outright with `key
'resource/Lato-Regular.ttf' has no value`, and the shipped original would have
merged into nonsense. Conditionals are now parsed, attached and written back,
and a patch prefers the base entry carrying the same conditional - the shipped
font table has `lucida console` twice, `[$PC]` and `[$GAMECONSOLE]`, and a
patch for one must not land on the other.

Duplicate keys within a block are preserved throughout: the playlist has 18,
including eleven `lang` blocks under `playlists/LocalizedStrings`. A merge
targets the first match, which is what Valve's `FindKey` returns.

### Open after this change

- **The server discards the PS4 client's usercmds.** A live fastball match had
  the server echoing `Bogus cmd timing` from `exploitfixes.cpp`, which fires on
  `frameTime <= 0 || tick_count == 0 || command_time <= 0` and then zeroes the
  command's view angles, movement, buttons and melee target. The client-side log
  for the same match sits in `spectator` with repeated
  `ServerCallback_YouDied() healthFrac: 1`, and the player saw a black 3D view
  with working HUD and audio - consistent with being stuck spectating with no
  view entity. Not yet proven to be one cause; the next step is to compare what
  the PS4 client writes into `CUserCmd` against what the PC server reads.
- **shadPS4 crashes on the level transition out of a match.** Host-side
  `Unhandled Exception code 0xc0000005 at 0x7ff89014cca7` in the emulator's own
  `GpuSchedPriorityPendingOpsRunner` thread, at the same address in all three
  occurrences, while loading `mp_lobby`. Emulator GPU bug, not game code.

## Mod VPKs are never mounted: the invisible fastball titan (2026-09-17)

Fastball precaches and spawns `models/titans/buddy/titan_buddy.mdl` (BT) in
`_gamemode_fastball_intro.gnut`. The titan is invisible on this port while
everything else in the match works.

`models/titans/buddy/` ships **only in the SP VPKs**, on PS4 and on PC alike -
it is in all thirteen `englishclient_sp_*.bsp.pak000_dir.vpk` and in none of the
MP ones. PC gets the model into an MP session because `Northstar.Custom` ships
its own `vpk/client_mp_northstar_common` (86 MB), and NorthstarLauncher's
`h_MountVPK` mounts every enabled mod's `vpk/` entries after each engine mount.

**This port implements no part of that.** The overlay serves loose files from
`<mod>/mod/` only. The 86 MB VPK is deployed in the PS4 profile and
`northstar_common` appears zero times in a 2.4M-line log; the engine mounts only
stock `client_frontend`, `client_mp_common` and the map. So the prop spawns and
the model resolves to nothing.

**Filesystem vtable, `filesystem_stdio.prx`.** PC's `IFileSystem::VTable` puts
`AddSearchPath` at 10, `ReadFromCache` at 95 and `MountVPK` at 111. This port
already resolves `ReadFromCache` at **97**, so the PS4 table is shifted by +2,
which predicts `MountVPK` at **113** - and slot 113 is `+0xa900`, which takes
`(this, const char* vpkPath)`, formats it with `"%s.pak000"` into a 0x104 buffer,
lowercases it and scans the mounted-VPK list at `[this+0x280]` with count
`[this+0x298]`, comparing each entry at `+5`. That is `MountVPK`. Slot 112
(`+0xa300`) is the PS4 map-VPK loader and is the one holding
`vpk_ps4/%sclient_%s.bsp.pak000%s` and `vpk_ps4/server_%s.bsp.pak000%s`.

Because the format is a bare `"%s.pak000"`, a mod VPK can be mounted by passing
a path the engine can resolve, the same way PC passes
`<mod>/vpk/client_mp_northstar_common.bsp`.

**Still to decide.** The shipped `client_mp_northstar_common` is a PC VPK
holding PC model data, so mounting it on PS4 is unlikely to produce a usable
model even once the mechanism exists. The PS4-native model is in
`client_sp_training` (`titan_buddy.mdl` 16 MB, plus the eleven sibling animation
models it references internally, 60 MB total), and `tools/tf2vpk-bin` has
`tf2-vpkunpack-ps4` and `tf2vpk-ps4` to unpack and repack. Keeping the PC shape
therefore means: implement the mount hook, then ship a PS4-built VPK in the
mod's own `vpk/` directory rather than copying loose models, which was tried and
reverted because it diverges from how PC does it.

Textures are a separate step: they live in the rpaks, BT's in the `common_sp`
family, and an MP session loads `common_mp`. This port implements no rpak
loading, so a mounted model may still render untextured.


## Native mod VPK mounting implemented - 2026-09-17

`runtime_vpks.inl` discovers enabled mods' `vpk/english*.bsp.pak000_dir.vpk` files and mounts their language-neutral stem through the original PS4 MountVPK. The primary filesystem vtable slot is **113**, function VA **0xa900**, gated against the expected slot address and exact prologue `55 48 89 e5 41 57 41 56 41 55 41 54 53 48 81 ec 28 02 00 00` in the existing hash-locked filesystem profile. The hook calls the retail mount first and mounts matching/preloaded mod archives afterward. A scripts.rson open also catches archives whose initial retail mount preceded interface installation. A recursion guard prevents re-entry; the engine deduplicates mounts by normalized path. The original return value is preserved, with the PC-style mod handle fallback when the original mount fails.

Correction to the earlier profiling note: **slot 112 / VA 0xa300 is a mounted-archive query, not the map mount function**. Its disassembly searches the existing list and returns a query result. Map-like string references in the adjacent function must not be attributed to this slot. The observed stock mount events are intercepted at slot 113.

Config behavior: absent or invalid vpk.json defaults to preload; a valid object requires boolean Preload:true for preload. Otherwise the archive stem must equal the basename of the engine's mount request. Comments/trailing commas are normalized before strict JSON validation. Folder discovery is filtered by enabled settings and sorted by mod load priority, with deterministic per-folder archive order. The native mount formats `%s.pak000` into 260 bytes; longer mod paths are rejected before calling it.

**The existing Northstar.Custom Buddy models do not need conversion.** Contrary to the earlier asset-format hypothesis, the original mod's `models/titans/buddy/titan_buddy.mdl` is byte-for-byte identical to the retail PS4 sp_training copy: 16,291,124 bytes, SHA256 `f5632b4857eeba6f6ef6296f8eaffb32105de1a6c786b51fcf987895b8caf44f`, CRC `1592F2E9`, IDST version 53. All twelve matching Buddy model entries have identical CRCs. Archive compression differs; the decompressed BT payload does not. A temporary native-subset experiment stayed under dist and was not installed; the working test uses the unchanged original mod VPK.

Runtime evidence: `work/stage2/iterations/20260917-220847/shad-new-lines.log`, PRX SHA256 `4a8d17e493e1cc78b66c9b7bcb9c7cb32e240d20ad8489dcbaeed6ac2b47d157`. The engine mounted `vpk_ps4/client_mp_common.bsp`, the hook mounted `/app0/R2Northstar/mods/Northstar.Custom/vpk/client_mp_northstar_common.bsp` with a non-null handle, and original OpenEx/Read returned a 12-byte IDST header for BT. The boot completed the UI lifecycle in 38.841 seconds. This proves native lookup of mod VPK content, not BT rendering or material/RPAK support. Retail VPKs and installed mod sources were not modified by this work.

Final configuration-validation build also passed: `work/stage2/iterations/20260917-221314/shad-new-lines.log`, PRX SHA256 `608689815ad53970eb968f24e24efc701ba26812e7ce83eab9720adb93648cfc`. UI startup completed in 36.217 seconds; the same non-null mod mount and `read=12 IDST=1` asset lookup were observed. This build remains installed. Host suites and `git diff --check` passed.

## Mod rpaks are not loaded; the twin-B error model is probably not a port gap (2026-09-17)

BT renders correctly in a real fastball match, so mod VPK mounting is confirmed
end to end and BT's materials resolve from retail content. The twin-B shotgun
shows the Source error model instead.

**The model does not exist in either game.** `mp_weapon_shotgun_doublebarrel.txt`
(shipped loose by `Northstar.Custom`) points at
`models/weapons/shotgun_doublebarrel/ptpov_shotgun_doublebarrel.mdl` and
`w_shotgun_doublebarrel.mdl`. Neither path is in any PS4 VPK, any PC VPK, or the
`client_mp_northstar_common` mod VPK. PS4 and PC `mp_common` both carry only
`scripts/weapons/mp_weapon_shotgun_doublebarrel.txt` and the matching `.nut`;
the mod VPK carries only `materials/models/weapons/twinbshotgun/shotgun_bullet*`.
`models/weapons/shotgun*` returns nothing from `client_mp_common`,
`client_sp_crashsite` or `client_sp_training`.

**Northstar's rpaks contain no models.** Both mod rpaks are uncompressed
(`RPak` version 7, flags 0x0000, against retail's flags 0x0100) and readable:
`mp_weapon_shotgun_doublebarrel.rpak` holds 18 `txtr` and 9 `matl` and zero
`mdl_`/`rmdl`, naming `models/weapons/twinbshotgun/*` skins;
`northstarEventModels.rpak` holds 29 `txtr`, 5 `matl`, zero models, naming
`models/northstartree/*`. They are reskins for models that must already exist.

So **implementing mod rpak loading would not fix the twin-B** - no mod rpak
anywhere carries that model. The remaining uncertainty is that retail rpaks are
compressed (`common.rpak` returns zero hits for `"models/"` and `"materials/"`,
so string searching them proves nothing), so the model could exist as an `mdl_`
asset inside an SP-only retail rpak that an MP session never loads. Asking
whether the twin-B renders on a PC Northstar install settles it in one minute:
the PC and PS4 asset sets for this weapon are identical, so if PC shows the
error model too, this is not a port gap.

**Mod rpak loading is still genuinely unimplemented** and does matter for
texture and material mods. Groundwork: the system lives in `rtech_game.prx`
(128 KB). Its strings include `/app0/r2/paks/PS4/%s`, `(%02u).rpak`,
`patch_master.rpak`, `_hotswap.starpak` and `PakDispatchLoad`. The path format
is referenced from two sites, `+0x669d` and `+0x8947`; `+0x83d0` is the dispatch
worker that takes a context in `rdi` and walks counters at `+0x7c`/`+0x80`, and
the site at `+0x669d` formats the pak name from `r14` into a 0x3fd buffer via
the snprintf at `+0xa030`. Unlike `MountVPK`, which accepts a free path, the pak
loader hardcodes its directory, so mod paks either need `../` traversal in the
name or to be staged under `r2/paks/PS4`. The entry point equivalent to PC's
`LoadPakAsync(path, allocator, flags)` is not yet identified.

## Mod rpak loader built; disabled because the shipped mod paks are PC builds (2026-09-18)

`launcher/include/northstar_ps4/mod_rpaks.h` and `launcher/src/runtime_rpaks.inl`
implement Northstar's `paks/rpak.json` mechanism. `tests/rpaks.cpp` covers the
rules and runs against the shipped config. The feature is gated off by
`kModRpakLoadingEnabled`; everything below is why.

**The pak system, in `rtech_game.prx` (128 KB).** `+0x76f0` is the loader:
`int LoadPakAsync(const char* name, void* allocator, int flags)`. It takes the
lock at `+0x2a7460c`, allocates a handle (-1 on failure), indexes a 512-slot
table of 0xa8-byte entries by `handle & 0x1ff`, stores flags at `+0x380638` and
state 1 at `+0x380634`, strlens the name, calls
`allocator->alloc(allocator, len + 1, 1)` through the allocator's first vtable
slot, copies the name in and returns the handle. `+0x78b0` wraps it for the rest
of the game, forwarding rdi/rsi/edx and handing rcx and r8 to the completion
registrar at `+0x74b0`. The worker at `+0x5340` drains the queue.

**Hooking it took three attempts, and the first two were wrong.**

1. *Rewriting import slots that hold the function's address.* This found nothing
   for `+0x76f0`, which is unsurprising in hindsight - it has two callers inside
   the module and its address appears nowhere as data, because it is not the
   exported entry. The one match it did find was a coincidental 64-bit value in
   `tier0.sprx`. Rewriting memory because its contents happen to equal an
   address is how unrelated data gets corrupted. The scan also crashed the boot
   twice: once reading uncommitted pages inside a reported segment, and once
   because it "restored" data pages to protection 3. The bits are 4=read,
   2=write, 1=execute, so 3 is write-execute with no read and the next ordinary
   read of the page faults.
2. *Detouring the entry's prologue.* Needs an executable trampoline, and shadPS4
   refuses to make this module's data page RWX: `sceKernelMprotect(..., 0x7)`
   aborts the emulator with `Protect: Unreachable code!` from
   `address_space.cpp:562`.
3. *Patching the loader's two call sites, `+0x78c0` and `+0x7ed1`* - the same
   rel32 patcher the lifecycle hooks use. No new executable memory is needed
   because the loader is left intact and called directly as the original. This
   works: both sites patch, and both of Northstar.Custom's paks are dispatched
   right after `common.rpak` with valid handles.

**Paths are absolute.** The worker holds a `/app0/r2/paks/PS4/%s` format string,
but it is not applied on the route a load request takes: a relative name reached
the filesystem verbatim and the open failed on the literal
`../../../R2Northstar/...`. Passing the full path works, and the engine then
opens and reads the pak from the mod directory with no pak error.

**Why it is off.** The archives Northstar ships are PC builds - `RPak` version 7
but flags 0x0000 against retail's 0x0100 - holding `txtr` and `matl` assets in PC
formats. Loading them wedges the boot: the engine looks for
`mp_weapon_shotgun_doublebarrel.starpak` at `/app0/r2/`, where a mod's streamed
data does not live, and the pak system then walks the address space in
`sceKernelAvailableDirectMemorySize` in 0x4000 steps and never finishes. No
playlist, no UI lifecycle, `UI VM probe timed out`. With the gate off the boot is
healthy again: UI lifecycle completes, the merged playlist is served, the mod VPK
mounts, no FatalError.

This is the opposite of how the VPK work turned out, where the shipped archive
held bytes identical to the PS4 originals. Turning this on needs PS4-format mod
paks, and starpak resolution alongside - PC registers those separately rather
than leaving them beside the rpak.

Note also that none of this would have fixed the twin-B shotgun: both mod rpaks
contain zero `mdl_`/`rmdl` assets, and the model it wants is absent from PC and
PS4 alike.

## Source console: present in the build, but not typeable (2026-09-18)

The console is **not stripped**. `client.prx` carries `CGameConsole`,
`IGameConsole`, `CGameConsoleDialog`, `CConsoleDialog`, `CConsolePanel` and
`IConsoleDisplayFunc`, exposes them as `GameConsole004`, and holds both the
`toggleconsole` command and the `con_enable` convar. `engine.prx` has
`Con_Init()` and `Con_Shutdown()`. Northstar's `autoexec_ns_client.cfg` already
binds a key to `toggleconsole` and this port already serves it.

**There is no way to type.** `sceKeyboard` and `sceIme` return zero hits across
every module, so neither a USB keyboard nor the system on-screen keyboard can
reach the guest. (`TextboxConsoleKeyboard` in engine.prx is an ordinary vgui
TextEntry resource key, not keyboard support.) Any console-like feature on this
platform has to get its input from somewhere other than typing.

**Engine command execution is still unsolved.** `IVEngineClient` is registered
as `VEngineClient013`; its 48-entry vtable was dumped at runtime. Slot 26
(`engine+0x470a0`) is `ServerCmd`: it formats with `"cmd %s"` into a 255-byte
buffer, tokenizes with `CCommand::Tokenize` at `engine+0x204b70`, then calls
`engine+0xf0870`. It is the only slot touching either.

`engine+0x204b70` is confirmed good: it zeroes the CCommand itself (`[0]`,
`[8]`, `[0x10]`), takes `(CCommand*, const char*)`, and resolves its break set
(`{}()':`) through its own rip-relative reference, so a caller supplies only
storage.

`engine+0xf0870` is **not** local execution, which the `cmd ` prefix should have
given away: ServerCmd ships a command to the server. Two probes agree - `echo`
produced no output, and `exec` of a missing file produced no filesystem request
at all, which is what a network send does with nothing connected. Local
execution would need `Cbuf_AddText`/`Cmd_ExecuteString`; engine.prx has
`Cbuf_Init()`/`Cbuf_Shutdown()` only as profiler strings and no
`Unknown command` string to anchor the dispatcher. `runtime_console.inl` keeps
the addresses behind `kConsoleCommandsEnabled = false`.

**None of that is on the critical path.** `ClientCommand()` is a Squirrel native
that already works - `Northstar.DirectConnect` calls it to connect to a server,
which is proven on this port - and this module can already call Squirrel
functions with arguments through `FindFunction` (client+0x685cf0), `PushObject`
(client+0x6875f0) and `sq_call` (client+0x6876c0). Command execution should go
through Squirrel. See `docs/IDEAS.md`.

## Deployed profiles drift, and nothing detected it (2026-09-18)

`New-NorthstarProfile.ps1` throws when its output directory exists, so a
deployed profile can never be refreshed, and the hash manifest it writes
(`profile-files.json`) is left in the staging directory rather than deployed.
The result was silent drift: the deployed `Northstar.Custom` held 117 of its 125
files, missing all eight `.mdl` files, so the twin-B shotgun rendered as the
Source error model while the engine's failed opens for
`w_shotgun_doublebarrel.mdl` sat in the log unread.

`Sync-NorthstarProfile.ps1` updates a deployment in place: it compares every
source file by hash, copies what is missing or changed, verifies each copy,
reports stale files (pruning only on request) and deploys the manifest so the
next run can report drift. Repository mods override PC ones of the same folder
name. A dry run against the current deployment reports 818 PC-sourced files in
sync and flags that `Northstar.PS4` has never been deployed at all.

**This is emulator-shaped.** On hardware `/app0` is the read-only application
image, so mods cannot be copied in after install: they are baked into the PKG,
and anything updatable has to live in writable storage - `/data`, which this
port already uses for `scripts.rson`, merged KeyValues and save data. The
durable fix is to make the mods root a search path (`/data` first, then
`/app0`) rather than the single hardcoded `kModsRoot` literal it is today. A
directory junction would work on the emulator and has no hardware equivalent.

## Atlas authentication: the client token half now works (2026-09-21)

**The flow, from the reference implementation.** The client exchanges its Origin
token with the master server (`/client/origin_auth?id=<uid>&token=<originToken>`)
for a Northstar player token. To join a server it calls
`/client/auth_with_server?id=<uid>&playerToken=<token>&server=<id>&password=<pw>`
and receives `{ip, port, authToken}` - a token minted for that one connection.
It then puts that token in the **`serverfilter` convar** and runs
`connect <ip>:<port>`; `serverfilter` is an ordinary userinfo convar Northstar
repurposes, so the token rides along in the handshake. The server receives
`(uid, serverFilter)` as parameters of `CBaseServer::ConnectClient` and calls
`CheckAuthentication(uid, token)`, which compares the uid Atlas recorded against
the uid the client sent.

So a genuine connection needs **both halves**: a token Atlas minted, and the uid
it was minted for. The captured server log shows the PS4 client arriving as
`uid 1` with a token that matched nothing, which is exactly
`"Authentication Failed."`.

**`ns_auth_allow_insecure` cannot produce that rejection.** `CheckAuthentication`
returns `true` immediately when it is set, so the captured log predates it being
active. Connecting to an insecure server already works; what does not work is
being recognised by Atlas, which is what would allow joining any server.

**`serverFilter` exists here and is writable.** Searching the binaries for
`serverfilter` finds nothing, which looked like the console build had dropped
it - the registered name is camelCase and `FindVar` is case-insensitive, so the
lookup succeeds and returns a real convar with the expected help string. Writing
it round-trips: set to a marker, read back identical, restored. That proves the
token half of the handshake can be driven from this module with no server in the
loop. The self-test is left behind `kAuthConVarRoundTripTest = false`.

**ConVar writing, generally.** `ConVar::SetValue(const char*)` is **vtable slot
15**. Slots 15 to 18 are IConVar forwarders: each loads the parent from
`this + 0x38` and tail calls the parent's vtable at +0x98, +0xa0, +0xa8, +0xb0,
which are slots 19 to 22. Slot 19 (`engine+0x2057f0`) keeps `rsi` as a pointer
and slot 21 keeps `esi` as an int, giving the usual `const char*`, `float`,
`int`, `Color` order. Slot 15 is the one to call, because it preserves the
parent redirect. Gated on the prologues of both `engine+0x206040` and
`engine+0x2057f0`. This build's ConVar layout: +0x18 name, +0x20 help, +0x40
default value, +0x48 current value.

**The uid is the open half.** `nucleus_pid` exists and holds `"0"`, but the
server saw `uid 1`, so the connect uid may not come from that convar at all. On
PC the client reads it from `g_pLocalPlayerUserID` (engine.dll+0x13F8E688), a
plain `char*` the Origin login fills in; here PSN reports signed out. Testing
whether writing `nucleus_pid` changes what the server receives needs a server.

**Correction to the 2026-08-17 note.** That entry says grepping the reference for
the Nucleus token override found no matches. It is there - `clientauthhooks.cpp`
hooks `Auth3PToken` and overwrites `p3PToken` with the literal
`"Protocol 3: Protect the Pilot"` whenever a Northstar client token exists. The
earlier grep missed it because the symbol is spelled `p3PToken`.

**Still unsolved: getting a real Atlas token.** On PC the Origin token lives in
the running game's memory (`engine.dll+0x13979C80`) and Northstar reads it from
there, so a PC-side helper would have to attach to a running Titanfall2.exe
rather than read a file. That is the main obstacle to the import plan, and it is
worth confirming before building the helper.

## Atlas auth: both halves of the handshake are now reachable (2026-09-21)

Following on from the flow mapped above, the two values the server checks are
both ordinary convars on this build, and both can be written.

**The uid is `platform_user_id`** - "Platform user id (origin user id on PC,
xuid on xboxone)". It exists, reads `"0"`, and is writable. This supersedes the
`nucleus_pid` guess: that convar also holds `"0"` but is the Nucleus persona id,
not the platform identity the connect path uses.

**The token is `serverFilter`**, already established as writable with a proven
round trip.

**The player name is not a blocker.** `VerifyPlayerName` replaces whatever the
client sent with the username Atlas has for the token, and only checks the
result is non-empty printable ASCII. The `"shadPS4"` name in the captured server
log comes from the emulator's `users.json` `user_name`, and would be overridden
anyway.

**Identity extraction works.** `scripts/Export-AtlasCredentials.ps1` reads a
signed-in PC client:

- uid from `engine.dll + 0x13F8E688`, the same `g_pLocalPlayerUserID` the PC
  launcher reads. The offset was **verified against this Northstar build**: it
  read `1000108120826`, matching the uid in the client's own log. The script
  cross-checks the two and refuses to export on a mismatch.
- player token by locating `MasterServerManager::m_sOwnClientAuthToken`, a
  `char[33]` of 32 lowercase hex preceded by `m_sOwnServerId[33]` and
  `m_sOwnServerAuthToken[33]`, both empty on a non-hosting client. Scanning
  committed private writable memory for that signature found **exactly one**
  match across 3.3 GB. The script refuses to guess if it finds more than one.

The Origin token is never read. Re-running `origin_auth` was deliberately
avoided: Atlas overwrites `acct.AuthToken` on every call, so it would invalidate
the running PC client's own session.

**Atlas details worth knowing.** Tokens are `cryptoRandHex(32)`, exactly 32
lowercase hex characters, expiring after 24 hours by default or whenever the PC
client authenticates again. `auth_with_server` validates the token and expiry
but **not** the source IP - `AuthIP` is only enforced on the persistence-write
path - so a token minted on PC is usable from the console. Validation order is
server-before-token, so a bogus server id cannot be used to test a token without
authorising yourself onto someone else's server.

**Applying it.** `ApplyAtlasIdentity` reads
`/data/northstar_ps4/atlas_identity.json` at startup and writes the uid into
`platform_user_id`; the log shows `"0" -> "1000108120826"`. Without that file
nothing happens, so an un-exported profile behaves exactly as before. The player
token is deliberately **not** applied to `serverFilter`: that convar needs the
per-connection token Atlas mints for one specific server, not the long-lived
player token.

**Still unverified, and it needs a server.** Whether the engine actually reads
`platform_user_id` when building the connect handshake. The captured log shows
the PS4 arriving as `uid 1` while the convar read `"0"`, so either it is not the
source or it is sampled elsewhere. Until that is settled, applying the identity
only changes a convar.

**The remaining piece** is the per-connection token: something has to call
`auth_with_server` for the target server and get `{ip, port, authToken}` to the
console before it connects. On the user's own server this is all moot -
`ns_auth_allow_insecure` returns true immediately - so this only matters for
joining servers that verify.

## Atlas transport works: the PS4 client reached the master server (2026-09-21)

The server browser, authentication and mod downloads were all "deliberately
unimplemented" for one reason - there was no HTTP transport. There is now, and
it reaches the real Atlas over TLS.

`runtime_http.inl` implements a bounded GET on `sceHttp`. Init order is the
SDK's: `sceNetInit`, `sceNetPoolCreate`, `sceSslInit`, `sceHttpInit`,
`sceHttpCreateTemplate`. The toolchain ships `libSceHttp.so`, `libSceNet.so` and
`libSceSsl.so` stubs, added to the link; shadPS4 provides the functions by HLE
rather than loading a real module, which is why `libSceHttp.sprx` never appears
in the module list even though the calls succeed.

**Two results, both from the module inside the game.**

Against a local server, to separate transport from TLS:

```
http probe url=http://127.0.0.1:8099/ps4probe ok=1 status=200 bytes=20
http probe body: NORTHSTARPS4_HTTP_OK
```

Against the real master server:

```
http probe url=https://northstar.tf/client/servers ok=1 status=200 bytes=8191
body: [{"lastHeartbeat":...,"id":"c5c76dab...","name":"...","region":"US West",...
```

shadPS4's own log agrees: `(SUCCESS) reqId=4 status=200 body=89988 bytes`, and
`created connection connId=3 host=northstar.tf port=443 scheme=https`. The 8 KB
probe buffer truncated an 89,988-byte response, so a real client needs a much
larger one.

**TLS works despite appearances.** `sceSslInit` logs as `(DUMMY)` and
`sceNetInit`/`sceNetPoolCreate` as `(DUMMY)` too, but the request still
completes over port 443 - shadPS4 performs the TLS itself rather than emulating
the SDK's SSL layer, so the DUMMY tag is not a warning that HTTPS is unavailable.

**What this unblocks.** Everything the master server does is HTTP:
`/client/origin_auth`, `/client/auth_with_server`, `/client/servers`. With
`platform_user_id` and `serverFilter` both writable and a working transport, the
remaining work on the browser is marshalling - fetching `/client/servers`,
parsing it, and backing `NSGetServerCount`/`NSGetGameServers` with the result
instead of the current empty stubs.

The probe reads its URL from `/data/northstar_ps4/http_probe.txt` and does
nothing when that file is absent, so it stays inert unless deliberately pointed
at something.

## Authentication re-enabled behind an imported identity (2026-09-21)

Master-server authentication is no longer unconditionally off. It reports a real
session when an Atlas identity has been imported, and when one has not it says
how to get one.

`ApplyAtlasIdentity` now records state rather than only applying the uid:

| State | Condition |
| --- | --- |
| `PS4_AUTH_IMPORTED` | uid plus a 32-hex `playerToken` |
| `PS4_AUTH_NO_IDENTITY` | no `atlas_identity.json` |
| `PS4_AUTH_NO_TOKEN` | uid present, token absent |
| `PS4_AUTH_BAD_IDENTITY` | unparseable, non-numeric uid, or a token that is not 32 hex |

Each state carries a message naming the fix, and those messages are what
`NSGetMasterServerAuthResult` and `NSGetAuthFailReason` return, so the menu
shows an instruction rather than a bare failure. The state is logged at startup
too, because that is where most people will look first.

**Only two natives changed.** `Authenticated` was a single stub shared by eight
registrations, so flipping it would have claimed HTTP requests, mod downloads
and server-list requests all worked. `NSIsMasterServerAuthenticated` and
`NSMasterServerConnectionSuccessful` now use a separate identity-backed
function; everything else still reports unimplemented. `NSIsHttpEnabled` stays
false deliberately: the transport exists now, but
`NS_InternalMakeHttpRequest` does not, and a mod told HTTP is available would
fail at the call instead of at the check.

The player token is held in memory and **never logged** - verified by grepping a
boot log for its first characters and finding none.

**One regression this exposed and fixed.** Once authentication reported true,
`ui/atlas_auth.nut` and `ui/panel_mainmenu.nut` began reaching
`GetConVarBool("ns_auth_allow_insecure")`, which had never been registered on
this client, raising `SCRIPT ERROR: [UI] ConVar ns_auth_allow_insecure is not
valid`. It had been latent all along, hidden by the surrounding branches
short-circuiting first. Registered with upstream's default `"0"`, and the boot
is now free of script errors.

**Next blocker for the multiplayer path.** `panel_mainmenu.nut` gates on
`( NSIsMasterServerAuthenticated() && IsStryderAuthenticated() ) ||
GetConVarBool("ns_auth_allow_insecure")`. The first half is satisfied now, but
`IsStryderAuthenticated()` is the engine's own Stryder session, and PSN reports
signed out on this platform. That gate, not the master-server half, is likely
what stands between here and the menu enabling multiplayer.

## Why "Launch Northstar" did nothing after authenticating (2026-09-21)

Authenticating was not enough on its own. Three separate things stood between a
successful sign-in and the local lobby starting.

**The launch path.** `OnPlayNSButton_Activate` in `ui/panel_mainmenu.nut` runs:

```squirrel
NSTryAuthWithLocalServer()
while ( NSIsAuthenticatingWithServer() ) WaitFrame()
if ( NSWasAuthSuccessful() ) {
    NSCompleteAuthWithLocalServer()
    ClientCommand( "setplaylist tdm" )
    ClientCommand( "map mp_lobby" )
} else { ...dialog with NSGetAuthFailReason() }
```

`map mp_lobby` is what starts the local lobby and brings up the Northstar menus.

**Two stubs blocked it.** `NSWasAuthSuccessful` was the shared always-false
`Authenticated` stub, so the success branch never ran. Worse,
`NSCompleteAuthWithLocalServer` was the shared `CompleteAuth` stub, which
*raises a Squirrel error* - had the first been fixed alone, the script would
have aborted between the success branch and the `map mp_lobby` that follows it.
Both now have their own implementations, and the result of the last attempt is
tracked separately from whether an identity exists, because having an identity
is not the same as having tried to use it.

There is nothing to negotiate for a local server here: PC authenticates with its
own server through Atlas because its server half enforces auth, and this port
does not hook `server.prx` at all.

**The button was also locked.** `panel_mainmenu.nut` threads
`UpdatePlayButton( file.fdButton )` onto the Launch Northstar button, and on
this platform that compiles the `#elseif PS4_PROG` console permission chain,
ending in `isLocked = file.mpButtonActivateFunc == null`. One of its branches is
`!hasPermission || !isMPAllowed`, and `IsStryderAllowingMP()` is just
`GetConVarInt( "mp_allowed" ) == 1`. This build boots with `mp_allowed` at
`-1` - Stryder has not answered, and never will, because there is no Stryder
session. Now set to `1` at startup.

**Correction to the previous entry.** It predicted `IsStryderAuthenticated()`
was the blocker. It is not: in the non-vanilla build that function
*returns `true` unconditionally*, because Northstar does not care about Stryder
when not using official servers. The PS4 script consults the console permission
chain only because `PS4_PROG` is the branch that compiles.

**Still unknown.** The same chain also gates on `Console_IsOnline`,
`HasLatestPatch`, `Ps4_PSN_Is_Loggedin`,
`Console_HasPermissionToPlayMultiplayer` and an age check. Those are natives,
not convars, so they cannot be read or written the same way. The chain sets a
localised `message` for whichever branch fires and the menu displays it under
the button, so the on-screen text names the next blocker directly - worth
reading before guessing. Note two branches (`PS4_NETWORK_STATUS_UNKNOWN` and
`PS4_NETWORK_STATUS_IN_ERROR`) assign `LaunchMP` and would leave the button
usable, so a failing network query is not automatically fatal here.

## SERVER VM hooked; the lobby's compile error was a missing constant (2026-09-21)

With the launch path fixed, `map mp_lobby` ran and died immediately:

```
FatalError: _items.nut: SERVER SCRIPT COMPILE ERROR: Undefined variable "VANILLA"
```

`VANILLA` is a compile-time define, not a script global. NorthstarLauncher sets
it with `defconst(vm, "VANILLA", ...)` from `VMCreated`, which runs for every
context. This module did the equivalent for UI and CLIENT by hooking client.prx's
VM initializer, so the SERVER VM was the only one compiling Northstar scripts
without the constants they are written against. `server.prx` had never been
profiled or hooked at all.

**Locating the server equivalents.** client.prx and server.prx embed the same
Squirrel implementation, so each address was found by matching the client
function's own bytes - choosing a stretch of body with no rip-relative operands,
since those differ per module and defeat a naive match. A 48-byte window from
each function found only `table insert`; the prologue alone matched 38 places.
Picking distinctive non-relocated body instructions gave exactly one hit each:

| | client | server |
| --- | --- | --- |
| VM initializer | 0x6746c0 | **0x625da0** |
| its call site | 0x6717af | **0x622e8b** |
| SQString::Create | 0x6a96a0 | **0x666600** |
| table insert | 0x6ab3e0 | **0x668470** |
| const-table gate | 0x6759d2 | **0x6270b2** |

The two call sites corroborate each other: client's is `e8 0c 2f 00 00` and
server's `e8 10 2f 00 00` - the same instruction reaching the same function from
nearly the same distance, which is what one piece of code compiled into two
modules looks like. The server initializer's 20-byte prologue is byte-identical
to the client's.

**The hook has to be lazy.** `server.prx` is not loaded at boot - it never
appears in the module list the tracker logs - so it cannot be hooked at startup
like client.prx. Installation is retried from the filesystem hook, throttled to
one module enumeration per 64 opens, and a module that is found but fails its
gate is not retried. In practice the module appears during an ordinary boot,
well before any server starts, and the log shows
`SERVER VM init hook installed base=... protection=0`.

The key-string ownership fix the client path needs applies here with more force:
`SQString::Create` writes neither the shared-state back-pointer at `+0x18` nor a
reference, and the table's release path dereferences both at VM teardown. A
SERVER VM is destroyed on every map change, so omitting it would fault far more
often than it did on the client, where only leaving a map tore a VM down.

**Not done here.** The script print sink gates on client.prx addresses, so
SERVER script output is still invisible - worth adding, since it is the natural
diagnostic channel for whatever the lobby does next.


## Feature-parity audit and SERVER MapSpawn dispatch - 2026-09-23

GOALS.md is now the current 26-goal tracker; the old contradictory chronology is archived in GOALS-HISTORY-2026-09-17.md. NATIVE-API-INVENTORY.md is generated by scripts/Update-NorthstarApiInventory.py from PC revision 4df8857814dd683147f1cc5fdae0b3b419a7f1cf and the current PS4 registration table. It lists 62 explicit PC registration declarations, their signatures/contexts and PS4 handlers. Counts do not imply completion; engine builtin hooks and non-script services have separate goals. Imported PC Atlas credentials are in scope, but token presence does not prove live authentication or persistence retrieval.

Captured evidence preserved at work/parity-audit/pre-callback-fix.log. The log contains multiple boots: missing newPrimeTitans/prime-array paths are schema errors, whereas the unregistered EndUpdateCachedLoadouts signal is a missing initialization callback. The original Northstar.CustomServers `_loadouts_mp.gnut` registers it in SvLoadoutsMP_Init (also calls InitDefaultLoadouts). Its mod.json declares that function as ServerCallback.After. Prior SERVER support registered constants/natives but never dispatched those callbacks.

Profile: server.prx SHA256 cb5164458a58dbba9568f98684255410460f77c60685253d899bef495125eee8. CodeCallback_MapSpawn string VA 0x88fe6a is referenced by LEA at 0x70cd5d; the call at 0x70cd64 (`e8 47 e4 f1 ff`) targets 0x62b1b0. The preceding owner load at 0x70cd56 and callback string LEA are gated as 14 exact bytes. The target has the same CSquirrelVM/name callback ABI as the client equivalent and a separately validated 17-byte prologue. No Windows offsets are used.

runtime_server_vm.inl now loads enabled/ordered ServerCallback metadata on each successful SERVER VM initialization and dispatches Before -> native MapSpawn -> After at that call site. It resets owner/started even when the allocator reuses an owner address. Missing mod callbacks do not stop the chain; the engine result is preserved. Both VM-init and MapSpawn code pages are acquired before either call is patched and restored to RX afterward. SERVER Destroy and early InitScript/type lifecycle remain incomplete.

Portable DispatchScriptInitCallbacks tests cover context filtering, source order, missing callbacks, engine failure and ignoring mod return values. All existing profile suites passed, including live KeyValues and RPAK configs. The PS4 build succeeded with the existing linker `_start` warning.

Deployed PRX: 6c48660219ef7b358a7f4f73cb55905bed46b799aa12cb7144e9be005ce1a9ac, built in dist/northstar-parity-server. Startup probe work/stage2/iterations/20260923-140725/shad-new-lines.log passed in 48.31s, logging `SERVER VM init and MapSpawn hooks installed ... protection=0` and `UI lifecycle completed`. This verifies gates/install/startup, **not SERVER callback execution or resolution of the lobby errors**. Next: enter the local lobby, capture SERVER After: SvLoadoutsMP_Init and lifecycle completed, then repeat return/re-enter. Persistence schema compatibility, real account pdata and write-back remain unimplemented/incomplete. This change edits neither retail archives nor installed mod sources.


## 2026-09-23: AI.Harness command transport and live parity probe

`runtime_ai_harness.inl` adds UI-only, calling-mod-gated read/field/reply natives. `AI.Harness` starts a 250 ms UI polling thread via UICallback.After; consumes a fixed local mailbox request before executing; uses script ClientCommand for local execution. It mirrors the core main-menu local authentication sequence before mp_lobby. No unprofiled Cbuf address, HTTP callback or retail archive edit is involved. `console.txt` is imported explicitly by the host helper; the disabled legacy reader no longer truncates it.

First live attempt caught native DecodeJSON table insertion failure (`string could not be stored`), so harness fields use the existing native metadata string parser. General JSON VM marshalling remains unresolved, not hidden by portable parser tests. EncodeJSON replies worked live.

Build/deployed PRX SHA256: `370d806ec891702bd07161eaec89d78f497436b7c70c1853a40534631770114f`. Automatic launch and subsequent correlated status reply verified in iteration `20260923-165316`; SERVER lifecycle completed at approximately 64 seconds. Console disconnect and subsequent status both acknowledged. A 15-second console probe timed out during loading then was consumed later: callers must treat timeout as unknown, never as cancellation. UI VM recovery restarts the polling thread and does not replay consumed requests.

The generated PDEF served 34217 bytes (34140-byte original prefix), resolving the observed newPrimeTitans/netWorth root lookups. Subsequent errors are newTitanExecutions (UI array count) and factionGiftsFixed (SERVER persistence read). Neither a fully working lobby nor complete persistence is claimed. See docs/AI-HARNESS.md for installation and command usage.


## 2026-09-23: harness-driven multiplayer initialization repairs

Pinned PC mod 231 declarations are validated before generating the PS4 929 cache. Added newTitanExecutions/unlockedTitanExecutions, factionGiftsFixed, newCommsIcons/unlockedCommsIcons, custom_emoji_initialized/custom_emoji, and random player/Titan/weapon/faction/Coliseum reward counters. Existing newPrimeTitans/netWorth handling remains. These address UI cache/inbox reads and SERVER InitPersistentData/AwardRandomItem failures without editing the callers.

Next SERVER error was titanLoadouts[6].special = mp_titanweapon_stun_laser during Monarch initialization. Merge missing values from PC loadoutWeaponsAndAbilities and titanPassive by appending them inside the existing enums. Keep all existing PS4 entries and indices, including PS4-only entries; do not substitute PC numeric encodings. Enum-count-sized arrays may grow and downstream binary offsets change.

Next UI error was unlockedPilotSkins[4] from OnLobbyMenu_Open. A scoped integer declaration pass expands literal int capacities to the PC capacity when larger, including scalar-to-array promotion. Match both enclosing struct and member name; never shrink, replace symbolic arrays with literal sizes, or copy incompatible types. This covers PC skin/feature/calling-card/icon capacities together instead of repeatedly patching menu callers. Tests verify scoped matching and repeated generation, including actual installed PDEF files. Full generation: 34140 -> 35206 bytes; no claim that old binary saves or PC pdata match this layout.

Final deployed build `4cf10c9a765d65fae291e5487a0dfef1c84c9c781ae7bae56e41b46d526de25a`. Iteration 20260923-171208 reached mp_lobby; harness status and a second status after 20 seconds both confirm connected/lobby. Zero script errors across the session. A later disconnect terminated the emulator on GpuSchedPriorityPendingOpsRunner with access violation 0xc0000005 at 0x7ff93f72cca7. No guest script error or guest stack accompanied it. This is a separate unresolved teardown failure; see work/parity-audit/lobby-fix-session5.log. Do not count repeated in-process map transitions as verified.

- **Repeat confirmed:** second fresh launch `20260923-171632` also reached and remained in mp_lobby; immediate and delayed harness replies both report connected/lobby true. `work/parity-audit/lobby-fix-repeat-session.log` has zero script errors or critical entries at capture; state evidence is in `lobby-fix-repeat-status.json` and `lobby-fix-repeat-stable.json`. Emulator left running in the lobby for user inspection. This verifies repeated fresh launches, not in-process leave/re-entry.

## 2026-09-23: server browser, joining, hosting on Kodai, and persistence rebased on PC 231

**Server browser** (`runtime_server_list.inl`, parsing in `northstar_ps4/server_list.h`,
host-tested in `tests/server_list.cpp`). `GET https://northstar.tf/client/servers` on a
worker thread; "requesting" goes true inside `NSRequestServerList` because the browser
spins on `NSIsRequestingServerList`. Lock-free handover via an atomic state. Live on the
console: `server list ok: 93 servers, 0 malformed skipped`.

**Joining** (`runtime_server_join.inl`). `POST /client/auth_with_server`, then
`NSConnectToAuthedServer` sets `serverfilter` and runs `connect ip:port` through the UI
script helper `NSPS4_ClientCommand` (Northstar.PS4), since native command dispatch is not
usable. Atlas refuses any User-Agent not starting `R2Northstar/<semver>`; this port sends
`R2Northstar/<Northstar.Client version>+ps4 NorthstarPS4` (not `+dev`, which skips the
minimum-version gate). Verified up to Atlas: UA accepted, refused only with
`401 Invalid or expired masterserver token`. Address/port/token from Atlas are validated
before reaching the command line. Unverified past that: whether the game server accepts
the PS4's uid on connect.

**Hosting: `ReadFile` bypassed the mod overlay.** Hosting a match died on
`Couldn't read scripts/aibehavior/behaviors.txt!`. The stock game (PS4 and PC) has
`scripts/aibehavior/` only in SP archives; Northstar.CustomServers ships it. The server's
AI init reads it with `IBaseFileSystem::ReadFile` (secondary vtable slot 14,
`filesystem_stdio+0xc3d0`, a direct implementation, not a thunk; called at server.prx
`0x2fc2c4` with pathID `"game"`), which opens internally and never reaches the hooked
`OpenEx`. Now hooked in a copied secondary table, gated on its first 32 bytes. Files
`OpenEx` rewrites (runtime `scripts.rson`, KeyValues merges) keep stock `ReadFile`
behaviour. Verified: all AI behaviour files load from the mod and `map
mp_forwardbase_kodai` hosts with SERVER and CLIENT lifecycles complete.

**Persistence: the served schema overflowed the player buffer.** The PS4 client record
is PC's layout shifted by `0x250` at both ends of the save buffer (buffer `0x74a` vs
`0x4fa`, UID `0xf750` vs `0xf500`), so it holds PC's 56,781 bytes. The engine sizes data
from the pdef and never checks it against the buffer (no size limit string or constant
exists; only a 0xD000 file-text limit). The 929-extension approach (earlier generator plus
the runtime `persistence_schema` pipeline) had reached 59,818 bytes of data. PC 231 is
56,169 bytes (confirmed by Atlas: `UnmarshalBinary` rejects shorter input).

Rebased: `scripts/pdef/build_ps4_pdef.py` emits PC 231 verbatim plus only the console's
black market (`bm`, 181 bytes, and its types) = 56,350 bytes. Evidence it is enough:
no PS4 native binary looks up a console-only field by name (only candidate,
`bc.discard.%d:1|c`, is a statsd counter); `scripts/pdef/find_console_fields.py`
resolves every persistent-var path in the 462 stock scripts no mod overrides and finds
only `bm.*` + `BlackMarketUnlocks` missing from 231, and no literal enum index invalid
in 231. The runtime pipeline (`runtime_persistence_schema.inl`, `persistence_schema.h`,
its test) was removed; the mod overlay serves Northstar.PS4's file. Verified: hosted
Kodai, player connected, `READY_INSECURE` set, zero script errors (the previous
`Invalid var name 'xp_count[0]'` disconnect is gone). A side benefit: 231 lines up
byte-for-byte with a PC Northstar server's pdata.

**Private-match crash (emulator).** `0xc0000005` on `GpuSchedPriorityPendingOpsRunner`
maps (module snapshot, bases stable per boot) to `VCRUNTIME140.dll+0x1cca7`: the AVX
large-copy path of memcpy/memmove (>= 1.5 MB), faulting on the *source* load. shadPS4
copies from guest memory already unmapped by level teardown. `copy_gpu_buffers` is off
for this title and is the setting aimed at that race; untested.

**Tooling moved.** `tools/` is gitignored, so generators live in `scripts/pdef/` and
`scripts/menus/`.

**Northstar mods are vendored, pinned to release 1.31.13.** `vendor/NorthstarMods`
(`509b14c7`) and `vendor/NorthstarNavs` (`v4`, `0d3c1332`) are shallow submodules at
the revisions `R2Northstar/Northstar` v1.31.13's `flake.lock` packages
(`vendor/northstar-release.json`). NorthstarMods itself always commits
`"Version": "0.0.0.1+dev"`; the release stamps the version with `jq` and copies
NorthstarNavs' `graphs/` and `navmesh/` into `Northstar.CustomServers/mod/maps`.
`scripts/Build-NorthstarMods.py` repeats exactly that into
`work/northstar-release/1.31.13/mods`, and `northstarModsRoot` points there. Verified
byte-identical to the PC install: 818/818 files, and `Sync-NorthstarProfile.ps1` against
the deployed profile reports `InSync=True` with no changes. The submodules must be checked
out with `core.autocrlf=false`, `core.eol=lf` (the script sets this): upstream commits some
files with CRLF and marks localisation `working-tree-encoding=UTF-16LE-BOM` with `text`,
so a Windows-default checkout differs from the release in line endings.

**Joining: the connect uid, not the token (2026-09-23).** With a fresh export, Atlas
accepted the join (`server auth succeeded: 172.250.149.199:37015`) and the game server then
refused it: `Connection rejected: Authentication Failed.` The PS4's connect packet is built
at engine `0x155516`: it parses `platform_user_id`'s string (`call 0x658`, base 0) into a
64-bit uid, writes it, then the name, then `serverFilter`. So the convar is right, but at
engine `0xb87d3` the engine rewrites it from the PSN account id it fetches into
`engine+0x2e7a310` (`%llu`), or to the literal `"1"` when that id is zero (`0x34bc8c`), as it
is under shadPS4 with PSN signed out. That is the `uid 1` a PC server logged earlier.
Seeding the global does not hold (re-fetched at `0xb7bd4` before each rewrite), so
`EnsureConnectUid` re-applies the imported uid immediately before `connect` and logs the
value it found. Unverified until the next join.

**`copy_gpu_buffers` does not fix the teardown crash.** Harness transition test
(lobby -> `map mp_forwardbase_kodai` -> `map mp_lobby`): with it off, Kodai loads and the
return to the lobby crashes in 8 s at `VCRUNTIME140+0x1cca7` (reproducible). With it on,
the *first* lobby load hangs: no `LevelLoadingFinished` after 7 minutes, main thread
silent. Left off for this title.

**Joined a public server (2026-09-23).** With `EnsureConnectUid` the log shows
`connect uid was "1" (engine rewrite); reset to 1000108120826`, the server accepted the
connection and the client loaded `mp_complex3` (Attrition), CLIENT lifecycle complete,
in spectator waiting to spawn. It then failed with `Connection to server timed out.`
and shadPS4 crashed on the way back to the menu (the known teardown race). The whole
session was 2-3 minutes by the log's 60 s play-time ticks, so the load itself was not
slow. What happened after the load, before the timeout: **229 Vulkan pipeline compiles**
(11 during the load) and 19,304 overlay probe misses. Pipeline caching is off for this
title, so every compile repeats on every visit. The compiles, not the probes, are the
likely stall; enabling the pipeline cache and pre-visiting maps is the next thing to try.

**Mod file index (built, disabled).** Replacing per-root `open()` probes with a
one-time index of mod files cut failed opens per session from 55,590 to 103 and boot to
the Northstar lobby from 70-74 s to 52-57 s. But harness A/B on lobby ->
`map mp_forwardbase_kodai`: with the index 3/3 crash 12 s in at `VCRUNTIME140+0x1cca7`;
without it 2/2 load. The crash follows the main thread's large `sceKernelMunmap` calls
(3.3 MB, 5.4 MB) during the transition while the GPU thread still has copies pending
from that memory; the probe overhead was delaying the main thread enough for them to
drain. `kModFileIndexEnabled = false` until the race is fixed in shadPS4 or guarded here.
This also makes the race reproducible on demand, which is useful for a shadPS4 report.


## Level-transition crash, pipeline cache and DecodeJSON (2026-09-24)

**Pipeline cache on for CUSA04013.** `custom_configs/CUSA04013.json` now has
`pipeline_cache_enabled: true` (backup in `work/stage2/harness-archive/`). The cache is
written incrementally under `%APPDATA%\shadPS4\cache\CUSA04013`, so a killed session keeps
what it compiled. Second visit to Kodai: 144 graphics pipeline compiles -> 16, boot 93 s ->
76 s. This is the fix aimed at the remote-match timeout (229 compiles after load on
`mp_complex3`); each MP map was then visited once to fill the cache (one boot per map,
because leaving a map crashes).

**The transition crash is any exit from a loaded map, not just returning to the lobby.**
Harness hops lobby -> Kodai -> `mp_complex3` crash on the second hop with the same
`VCRUNTIME140+0x1cca7` on `GpuSchedPriorityPendingOpsRunner`. Emulator: shadPS4 v0.18.1 WIP
`5b92da8` (Pre-release, 2026-09-16).

**A guest-side unmap delay does not fix it (tested, removed).** The game modules'
`sceKernelMunmap` imports can be redirected by value: the GOT slot holds the same resolved
address as this module's own import (`0x7000008c37d0`), and exactly one slot matched in each
of `titanfall2_ps4`, `libSceLibcInternal`, `libc`, `libSceNpToolkit` and `tier0`. With every
main-thread (`MainThrd`, matched by `scePthreadGetname`; the UI VM is created on `Thread4`)
unmap of >= 1 MB of mapped memory delayed 5 ms, and then 50 ms, `mp_complex3` still crashed
3/3. In the 50 ms run the fault landed during model loading with no unmap nearby. So the
correlation with large unmaps noted under "Mod file index" was coincidental, and the
explanation there (probe overhead letting copies drain) is withdrawn. The experiment is in
the session scratchpad, not the tree.

What the logs do show: the game's texture-streaming threads reserve a range and map only its
tail (`sceKernelReserveVirtualRange` 0x158000 at `0x694c54000`, then
`sceKernelMapNamedDirectMemory` 0x8000 at `0x694da4000`), and the GPU command processor
later tracks the whole range (`UpdatePageWatchers: Tracking memory region 0x694c54000 -
0x694dab000 which is not fully GPU mapped`). A buffer-cache copy over such a range would
read uncommitted host pages, which fits a source-side fault in a large memcpy, but no single
warning precedes every crash, so this is a lead, not a finding. Upstream rewrote the buffer
manager after this build (`965c97c8`, #5047, "sparse arenas"); the Sept 23 pre-release
`ca89b01` includes it and is the next thing to test.

**DecodeJSON "string could not be stored" was a misread return value.** 0x6ab3e0 is
`SQTable::NewSlot`. Its bool is "a new node was created", and it is false whenever the key
ends up in an existing node. That includes every insert that grows the table, because the key
is written into its main position (0x6ab59f) before the free-node search, and the retry after
`Rehash` (0x6ab400) finds it and takes the replace path (`xor eax, eax` at 0x6ab7e0). So any
object with more members than the initial node count failed. It is also why the constants log
shows `NS_VERSION_PATCH ... result=0` while the constant works. `TableStoreTop` now ignores
the result. It also no longer adds a reference to the value: NewSlot takes its own
(0x6ab7af / 0x6ab7d3), and the client's increment at 0x684555 belongs to its local
`SQObjectPtr`, released at 0x6845ab, so the old extra increment leaked every decoded
string, array and table. The key's extra reference stays (the verified teardown fix; at worst
an interned key string is never freed). The harness has a `json` action that round-trips text
through DecodeJSON/EncodeJSON in the UI VM. Verified live (PRX `3bc339ee`): a 20-member
object with nested arrays and objects, floats, bools, escapes, `\u00e9` and a duplicate key
came back intact (last duplicate wins; the null member dropped, as on PC), and a truncated
document raised `Failed parsing json file: encountered parse error "expected ',' or ']'" at
offset 9`. The lobby stayed up with no script errors.

**Expired identity at join.** Atlas answers a stale imported token with
`INVALID_MASTERSERVER_TOKEN` (or `PLAYER_NOT_FOUND`). PC recovers by re-authenticating with
Origin; the PS4 cannot, so the join dialog now appends how to re-export the identity. The
parser records `error.enum` separately and keeps PC's reason text.
## shadPS4 ca89b01 fixes the transition crash; mod file index on (2026-09-24)

**Busy-server joins crashed the same way.** Two joins to the most-populated public server
(`mp_eden`, then `mp_wargames`) passed Atlas auth, the uid fix and connect, then crashed at
`VCRUNTIME140+0x1cca7` on `GpuSchedPriorityPendingOpsRunner` about 5,000 log lines after the
map VPK mounted. An empty server (`mp_angel_city`) loaded and stayed connected with no
timeout. The two joins run the same sequence; the difference in the crash window is that the
texture-streaming threads (Thread4-8) were already reserving and mapping memory in the
crashing joins. Joining from the lobby tears down the listen server and briefly shows the
main menu, which a local `map` change does not.

**shadPS4 pre-release `ca89b01` (2026-09-23) fixes it.** The transition harness that crashed
3/3 on `5b92da8` passed 20/20 hops on `ca89b01`: Kodai -> complex3 -> lobby -> wargames, then
eden -> lobby -> angel_city -> glitch -> lobby -> Kodai, and two more runs with the index
below. All loads had zero script errors. The likely fix is the buffer manager rewrite (#5047).
`docs/INSTALL.md` now names `ca89b01` as the minimum.

**Mod file index enabled.** It was only off because it made the old crash certain on
`5b92da8`. On `ca89b01`, with it on, 10/10 hops loaded and loads got much faster:

| Hop | Probing | Index |
|---|---|---|
| lobby -> Kodai | 55 s | 34 s |
| Kodai -> complex3 | 85 s | 66 s |
| -> lobby | 35-38 s | 23-24 s |
| lobby -> wargames | 89 s | 66 s |
| lobby -> eden | 60 s | 39 s |

Failed opens per hop: 3-33, down from thousands. PRX `5b4acd32`.

**Atlas token in shadPS4's log.** Atlas reads `auth_with_server` parameters only from the
query string (`r.URL.Query()` in `pkg/api/api0/client.go`), and shadPS4 logs full URLs at
Info in four places (`sceHttpCreateConnectionWithURL`, `sceHttpCreateRequestWithURL`,
`sceHttpSendRequest`, `LogSendRequestSettings`). The per-game log filter
`*:Info Lib.Http:Warning` removes them (verified: 0 token lines, 0 `Lib.Http` Info lines in
the next session) without affecting this module's `[Tty]` output. Per-game `Log.append` is
now on so a crash log survives the next launcher start.
## Backlog from v0.2.1 user testing (2026-09-25)

**Native console commands (`runtime_concommands.inl`).** NorthstarLauncher adds console
commands the stock game lacks, and on PS4 they silently did nothing. Two user reports
traced to that:

- *Quitting a private match to the main menu, then entering multiplayer again, rebuilt the
  private match.* "Launch Northstar" runs `setplaylist tdm` then `map mp_lobby`, and
  `_lobby.gnut` picks the lobby type from `GetCurrentPlaylistName()`. `setplaylist` is a
  Northstar command (shared/playlist.cpp). Harness evidence before the fix: after a normal
  launch the playlist read "Load a map on the command line"; after private match ->
  `disconnect` -> launch it read `private_match` with `IsPrivateMatch()` true. After:
  `tdm`/false.
- *Leaving a match stalled.* A server ends every leave with
  `ClientCommand( player, "ns_start_reauth_and_leave_to_lobby" )`
  (`_menu_callbacks.gnut`), also a Northstar command (shared/misccommands.cpp), so remote
  servers and non-host leaves never returned. Now it checks the imported identity (PS4
  local auth), requires a CLIENT VM as PC's second half does, sets the playlist to `tdm` and
  runs `map mp_lobby` through the UI VM's `NSPS4_ClientCommand`. Verified three ways, each
  back to a `tdm` lobby in 10-11 s: the command from the console; `LeaveMatch` with
  `ns_should_return_to_lobby 0`, which is the server-sent path a remote server uses; and
  `LeaveMatch` as host with the default 1 (`GameRules_EndMatch`, lands in the private lobby
  as on PC).

Engine profile: `ConCommand::ConCommand(this, name, callback, help, flags, completion)` is
engine 0x204e60 (the engine's static constructors call it that way, e.g. `disconnect` at
0x122bd1; objects 0x58 apart; name +0x18, help +0x20, flags +0x28, callback +0x40). It
registers immediately when the cvar system is up. Callbacks get a PC-layout `CCommand`
(argc +0, ArgS +0x10, argv +0x410; `connect` 0x62bd0, Tokenize 0x204b70).
`SetCurrentPlaylist(const char*)` is engine 0x14a3a0, found through server.prx's
`SetCurrentPlaylist` native (0x6ce0a0), which calls slot 81 of the engine server interface;
its vtable (0x3acfd8) was located from the engine's relocations by slot 185 = 0x2dba90. Flag
values match PC (`connect`/`map` 0x20000 DONTRECORD; SERVER_CAN_EXECUTE 1<<28).
`setplaylistvaroverrides` is not added yet: Northstar's own menus use the
`PrivateMatchSetPlaylistVarOverride` client command instead. One build (`344c37b8`) logged
no registration at all, for reasons not established; the next build with only an extra log
line (`91cf0df3`) registered on every one of 6+ boots.

**`mp_box` has no map on either platform.** Northstar.Custom ships only its level script,
rson and loading screen. `maps/mp_box.bsp` is in no PS4 VPK and nowhere in the PC install
(PC VPKs listed with tf2-vpklist; no loose file). `map mp_box` on PS4 leaves the game with
no level and an unresponsive UI: while connected, the engine's `map` callback (0x1224a0)
tears down the current game (0x120d80) *before* checking the map (an interface at
engine+0x51e9b60, slot 0x328, then `VEngineServer::IsMapValid`, vtable slot 2 = 0x2d7470),
and a failed check just returns. A guard that ran both checks first stopped the main thread
instead (no "map load failed" line; only the chat thread kept logging), so it was removed
(kept in the session scratchpad). Loading mp_box needs a map mod that ships the BSP.

**Custom modes menu on a controller.** `menu_mode_select.nut` shows 15 rows and scrolls
only with the mouse wheel or the slider, so modes past the first page were unreachable on a
pad. Northstar.PS4 overrides the script: d-pad/stick past the bottom row (or above the top
row while there is more above) scrolls by one and keeps focus on that row (the server
browser's dummy-button behaviour; the mode rows are nested panels, so explicit nav links are
not available), L1/R1 page by five with footer hints, and opening the menu always focuses a
mode. A press that moved focus within the list in the same frame is left alone, and the
re-focus waits a frame, so it holds whichever order the engine handles navigation and the
callback in. Verified: the override is served and compiles, and the menu opens from a
private lobby with no script errors. Controller behaviour itself needs a pad.

**VRAM (busy-server crash).** The 2026-09-24 session on a 29-mod server ended in
`vk::Result=ErrorOutOfDeviceMemory` on a 6 GB GTX 1060. Harness hops with `nvidia-smi`:
Eden peaks at 5.3-5.7 GB, others 2-4.7 GB, and memory is not fully returned between maps
(lobby 1.1 GB at boot, 3.0 GB after three maps, 3.6 GB after seven, but it also falls
again, e.g. Eden 5.3 -> lobby 2.7 -> Eden 4.2). Halving the game's texture streaming
budget (`stream_memory` 800000 -> 400000) with `stream_drop_unused 1` did not lower the
peak (Eden 5.5 GB), so the budget is not what fills VRAM; shadPS4's own GPU caches are.
There is no shadPS4 setting that caps it. A crowded match on a heavy map will need more
than 6 GB until shadPS4 bounds its caches.

**Old pipeline cache is ignored by `ca89b01`.** Its log says `Pipeline cache profile has
unexpected size (60 != 64). Ignoring the cache`: the cache built on `5b92da8` is a different
format, so the pre-warming from 2026-09-24 did nothing on the new build, and the new build
did not write to that folder either. After an unexpected power-off, boots hung at the same
point twice (log cut mid-line after the Northstar.Client localisation file). With the old
folder moved aside (`cache/CUSA04013.after-power-loss`) they succeeded every time. The cause
is not established.
## Modes menu follow-up, mod localisation, shadPS4 fc5d2cc2 (2026-09-26)

**Modes menu on reopen.** User report: after paging and selecting, reopening the menu
allowed up but not down. The list kept its old scroll position (e.g. the last page) and the
override focused a row in MENU_OPEN, after which the engine restores the menu's previous
focus. So the highlighted row and the focused one disagreed, and at the end of the list down
had nowhere to go. Now, one frame after opening, the list scrolls the selected mode
(`PrivateMatch_GetSelectedMode()`) to the middle and focuses it; the stock
"first mode when none is selected" focus is kept. Screenshots of reopening with
FFA, Frontier Defense: Hard and Skirmish selected show that mode centred and focused.

**Screenshots.** `CaptureGame.ps1` (session scratchpad) grabs only the shadPS4 window with
`PrintWindow(PW_CLIENTONLY | PW_RENDERFULLCONTENT)`, which works while the window is covered.

**Mod localisation does not load.** `CLocalise::AddFile` (localize.prx 0x5c60) returns true
for every mod's `Localisation` entry but never opens the file: the log shows only directory
probes of `/app0/{platform,,r2,r2_doNotShip,r1_doNotShip}/resource`. The new harness
`localize` action confirms it: `#MENU_DIRECT_CONNECT` (Northstar.PS4) comes back unresolved,
while retail tokens and Northstar.Client's tokens resolve (the latter are in the stock
`frontend` VPK). So the Direct Connect label and any other mod-only token show raw. The
modes menu footer therefore uses literal `%[L_SHOULDER|]% Prev Page` text. The retail
`#LB_BUTTON_BROWSE_PREVPAGE` resolves to `%[L_SHOULDER]% Prev Page` (no pipe), which renders
blank in this footer.

**shadPS4 `fc5d2cc2`: black 3D view in matches.** The user's newest pre-release (41 commits
after `ca89b01`) renders the lobby but shows only the HUD in a match. Reproduced on Kodai
with the current runtime and with the v0.2.1 release runtime (`5b4acd32`), which rendered
normally for the user on `ca89b01`, so it is an emulator regression. Candidates in the range
include the buffer-cache/memory-tracker rework (#5100), `invalidate cached images after raw
buffer writes` (#5092) and several shader recompiler changes. The next pre-release
(`c6b24ec`) adds only renderer optimisations (#5112). A CI build of `ca89b01`
(`shadps4-win64-sdl-2026-09-23-ca89b01`, run 35853065697) is still available for rollback or
bisecting. The user's last logged session also ran v0.18.0 (`e3ce810f`, 108 commits before
`ca89b01`), which has the transition crash.
## Pad navigation in the modes menu; shadPS4 regression bisected (2026-09-27)

**Why a pad could not move in the modes menu.** On PS4 the engine moves focus with the d-pad
only along explicit `navUp`/`navDown`/`navLeft`/`navRight` links in a `.menu` file. The
user's Direct Connect menu (github.com/taskinoz/Direct-Connect-Menu) became pad-usable only
after gaining those links on every control, plus `tabPosition 1` on the first field. The
stock `mode_select.menu` has none, and its rows are nested panels (`PanelN` containing
`BtnMode`), which such links cannot join. The override now does all pad movement in script,
from d-pad/left-stick callbacks:
- up/down through the list, skipping category headers and scrolling at either edge;
- up/down through the search/filter/clear column;
- right from a row to the search box, and left back to the row it came from (the filter
  switch keeps left/right).

A press is ignored when focus already moved in the same frame, in case the engine does
navigate somewhere.

Verified with pad input (`scripts/Send-PadInput.ps1`) and window captures on shadPS4
`2b5666b3`:
- down 3 from Skirmish, skipping the PvPvE header;
- down to the end of the list (Turbo LTS);
- right to search, down to Clear, up, then left back to Turbo LTS;
- up 16 scrolling to the top;
- Cross on Capture the Flag, reopen (opens centred on it), down 2 to Marked For Death.

Locked rows (Frontier Defense on this map) take focus but ignore Cross, as on PC.

**shadPS4 regression bisected to #5110.** CI builds of the 41 commits `ca89b01..fc5d2cc2`
(29 have a Windows artifact) were fetched with `gh run download` into
`work/shadps4-builds/`. Each was tested by harness boot -> `map mp_forwardbase_kodai` ->
window capture -> mean brightness of the screen centre (black < 25; `ca89b01` 136):
`2b5666b3` renders (104), `b77d194e` (10), `2f40f87b` (16) and `776b5fdb` (16) are black.
Between the last good and first bad build are only `c6fa48c7` (no CI build) and `776b5fdb`
(an `#include` and USB-toy code). So the break is **c6fa48c7, "texture_cache: Fix some image
validation issues" (#5110)**. It (a) drops the storage usage flag from multisampled images
when `shaderStorageImageMultisample` is unsupported, and (b) when a view type is
incompatible with its image, now creates the view with the image's type instead of only
logging. The black runs log `image view type Color1D is incompatible with image type
Color2D` twice, so (b) is the likelier cause: a shader expecting a 1D texture gets a 2D
view. That is not proven. Newest good build: `work/shadps4-builds/2b5666b3/shadPS4.exe`
(25 commits past `ca89b01`). The pipeline cache was disabled for the bisect and has been
turned back on.
## Mod localisation fixed; shadPS4 issue filed (2026-09-27)

**Correction to "Mod localisation does not load" (2026-09-26).** Northstar.Client's tokens do
*not* come from the stock `frontend` VPK. The live VPKs are retail; the only copies with
Northstar content are under `.codex-validation-sparse/`, a test folder the game does not
mount. They resolved because the localise probe also calls `AddFile` with the explicit name
`resource/northstar_client_localisation_english.txt`, and that loads. The failing calls were
the ones passing `%language%`: the PS4 `AddFile` does not expand it, only probes the stock
`resource` folders, and returns true without opening anything. The probe now substitutes
`english` before calling `AddFile` (PC's fallback language; choosing the system language is
not done). Verified: every mod's file is added under its `_english` name, and the harness
`localize` action resolves `#MENU_DIRECT_CONNECT` to "Direct Connect". PRX `a22ae48c`.

**shadPS4 issue.** Reproduced on an unmodified game (retail eboot restored with
`Enable-Stage2Bootstrap.ps1 -Disable`, since restored): at the start of the Pilot's Gauntlet,
`2b5666b3` renders the training pod and `fc5d2cc2` draws only the HUD. Brightened 8x, the bad
frame shows film grain and HUD only. The clean session logs the same two `Color1D`/`Color2D`
view errors and nothing Critical. Filed as shadps4-emu/shadPS4#5124, with screenshots and the
clean log on the `media/shadps4-5110` branch of this repo. The user saw occasional lighter
flashes on bad builds; a 16-frame burst did not catch any.
## Missing-map guard on `map`; repository review (2026-09-27)

**`map <missing>` no longer strands the game.** The engine's `map` callback (0x1224a0)
tears the current game down before checking the map, so `map mp_box` (no BSP on PS4 or PC)
left no level and a dead UI. The first guard called `VEngineServer::IsMapValid` itself and
stopped the main thread. The new guard (`runtime_concommands.inl`) swaps the `map`
ConCommand's callback (object engine+0x3ef2ce8, +0x40) and checks presence without that call.
A map is present if any of these holds: its retail archive
`/app0/vpk_ps4/englishclient_<map>.bsp.pak000_dir.vpk` exists; an enabled mod ships
`client_<map>.bsp`; or `maps/<map>.bsp` opens through the game filesystem (`mp_lobby` lives
in `mp_common`). Missing maps are refused with `map load failed: <map> not found`, and names
outside `[A-Za-z0-9_]` pass through. On `2b5666b3` with PRX `7eb56728`: `map mp_box` refused
with the lobby and harness still responsive; `map mp_forwardbase_kodai` loaded; the
leave-to-lobby command (which runs `map mp_lobby`) returned to the lobby.

**Review.** GOALS.md was rewritten against current evidence; its superseded handoff,
backlog and checkpoints moved to GOALS-HISTORY-2026-09-27.md. Also updated: README (status,
supported build, the port's mods), FOUNDATION (retired), IDEAS (what has been built),
tools/README, stale code comments (the SERVER VM is hooked; the connect uid is settled), the
build manifest's blocker text, and the regenerated NATIVE-API-INVENTORY (three
server-browser rows still named placeholder handlers).
## Custom map: `mp_box` (2026-09-27)

`mp_box` is a community map converted from Titanfall Online, shipped as the
`bobthebob.mp_box` mod (version 0.0.999). It contains:
- a loose `mod/maps/mp_box.bsp` (rBSP v37, same as the PS4 game), plus its `.ent`, `.stbsp`,
  AI graph and navmesh files;
- a PC VPK `vpk/client_mp_box.bsp`, with `vpk.json` `{"Preload": {"mp_box.bsp": false}}`;
- a loading-screen rpak;
- a script that calls `AddPrivateMatchMap( "mp_box" )`.

On `2b5666b3` with PRX `7eb56728`:
- **Discovery:** the mod, its VPK (preload 0) and its script were found at boot, with no
  script errors.
- **Load:** `map mp_box` passed the missing-map guard (the mod archive's stem is
  `client_mp_box.bsp`). The engine read the loose BSP through the overlay. Its mount of
  `vpk_ps4/client_mp_box.bsp` failed, and the MountVPK hook then mounted the mod's archive,
  as PC does for map-supplied mod archives. The level loaded in about 11 s.
- **In the level:** SERVER and CLIENT lifecycles completed with zero script errors. The
  player spawned with weapon and HUD, and the geometry and sky render. The minimap shows the
  missing-texture checkerboard: the mod has no overview asset, and its own comment says the
  same happens on PC.
- **Return:** the leave-to-lobby command came back to a `tdm` lobby.

In the private match map menu, which moves fine on a pad as shipped, Box is listed last,
with no thumbnail (its loading-screen rpak isn't loaded, since mod rpaks are disabled), and
it's locked. `IsLocked` requires the map to be in the selected mode's playlist, and no
playlist lists `mp_box`. That's the same on PC, so Box is reached with `map mp_box` or a
server's rotation.

## Mod auto-download, live mod reload, weapon KeyValues (2026-09-28)

PRX `ccc0768544ca6dd0936d33c675ce490a56241157f8fc1a5c5c9b7b5df0a08de6`, shadPS4 `2b5666b3` (the download and reload runs used the builds just before it, which lacked only the KeyValues changes at the end of this entry).

**Mod auto-download.** The five `ModDownloader` natives are implemented in
`runtime_mod_download.inl`, following primedev/mods/autodownload/moddownloader.cpp:
- `NSFetchVerifiedModsManifesto` fetches `verified-mods.json` from R2Northstar/VerifiedMods
  on a worker thread and reports DONE whether or not it worked, as PC does.
- `NSDownloadMod` streams the archive to `runtime/remote/mods/.download.zip`, hashing it
  as it arrives, checks the SHA-256 against the list, and extracts it.
- Install folders follow PC: `<archive name minus .zip>` for Thunderstore, with its
  `mods/<Name>` inside; `<stem>-<version>` for ModWorkshop, rooted at the first `mod.json`'s
  folder.
- The state struct, progress, total and ratio match PC's.
- A failed or cancelled install removes its folder.

The toolchain has no zlib, so `northstar_ps4/mod_archive.h` has a small streaming DEFLATE
decoder (puff-style, 32 KiB window), CRC-32 and a zip central-directory reader. It
refuses zip64, encryption and methods other than 0 and 8, and it drops `..`, absolute and
drive-letter paths. `mod_download.h` has SHA-256, the list parser and PC's naming rules.
`tests/mod_download.cpp` covers both, with Python-made vectors plus real packages: the 22 MB
`mp_brick` package inflates with every CRC matching, and both packages' SHA-256 match the
live list.

Three things specific to the platform:
- `/app0` is read-only on hardware, so remote mods live under
  `/data/northstar_ps4/runtime/remote/mods`.
- `fstat` is an unimplemented stub under shadPS4: it returns 0 and leaves the struct
  alone, so the first extraction failed with FAILED_READING_ARCHIVE. The archive is now
  sized with `lseek(SEEK_END)`.
- shadPS4's `unlink` only reaches the host when the path has no stale handle-table entry,
  so `.download.zip` can survive a successful install under the emulator. It is
  overwritten next time and ignored by discovery (leading dot).

**Discovery with two roots.** `ModDiscovery` now carries each mod's absolute directory and a
remote flag, and every path is built from that directory instead of
`/app0/R2Northstar/mods/<folder>`. That covers the overlay, manifest, VPKs, rpaks,
KeyValues, save files, VM callbacks and localisation. Remote mods start disabled at every
boot and follow `enabledmods.json` only after a reload: PC's "Do not load remote mods on
first load". NSGetModsInformation reports `isRemote`.

**Live reload.** `NSReloadMods` saves `enabledmods.json` and then calls `ReloadModState`
(`runtime_mod_reload.inl`), which rebuilds:
- the file overlay and its index, as a new snapshot behind an atomic pointer (readers on
  engine threads keep the one they loaded; old snapshots are leaked on purpose);
- the runtime `scripts.rson`, on its next open;
- the mod VPK list, under the mount flag;
- the KeyValues patch list, remerged on next open;
- the ConVars of newly enabled mods;
- the catalog.

Northstar's `ReloadMods()` then queues `reload_localization`, `loadPlaylists`,
`weapon_reparse` and `uiscript_reset`. Each was run in the lobby:
- `uiscript_reset` rebuilds the UI VM through the existing VM hooks and a map load brings
  the menus back.
- `loadPlaylists` and `weapon_reparse` are fine.
- `reload_localization` faulted at the first try, at localize+0x7aed (a hash-chain walk
  on another thread while the main thread rebuilt the table). Its ConCommand (engine
  object `0x1a1d2c0`, callback `0x812e0`, name `0x33357d`) is now repointed like the `map`
  guard. The replacement adds the Localisation files of newly enabled mods, which only
  inserts into the table. Tokens of mods a reload disables stay until the next boot.

**In game**, through the harness `download` action (Northstar's own
`FetchVerifiedModsManifesto` and `DownloadMod` dialogs, then `NSSetModEnabled` and
`ReloadMods`) with `lexi.lexire125` 1.0.7:
- The list loaded 21 mods. The download went through with the checksum matching, and the
  state reached DONE, installing `LexiGlasss_Modding_Cooperation-lexire125-1.0.7/mods/lexi.re125`.
- The reload came up with 7 enabled mods, and the manifest regenerated at 109 scripts.
- The UI VM rebuilt with zero script errors.
- `weapon_reparse` merged the mod's RE-45 patch
  (`mp_weapon_autopistol.txt` 18134 -> 15473 bytes).
- `map mp_glitch` then loaded and spawned with a HUD.
- After a restart the mod was listed as disabled ("downloaded; enabled per server").

The first reload attempt ended in `UI SCRIPT COMPILE ERROR: Undefined variable
"ProgressionEnabledForPlayer"`. `BuildRuntimeManifest` still built mod.json paths from
`/app0/R2Northstar/mods`, failed on the remote mod, and the stock manifest was served. It was
the last unported path site.

Not yet run: the same flow from a real server join (the harness `join` action does the
browser's sequence against a live server, but the exported Atlas token had expired),
cancelling, and a corrupt download.

**Weapon KeyValues were never applied.** With the reload in place, the RE-45 patch still did
not merge on a map load. Nor had Northstar.Custom's six weapon patches or the font table in
any session: only `playlists_v2.txt` (at boot) and `npc_pilot_elite.txt` (at map load) ever
logged "keyvalues built".

The engine reads these files through `ReadFromCache` first. `ModReadFromCache` only
bypassed the cache for files in a mod's search path, and a KeyValues patch lives in the
mod's `keyvalues/` folder, so the cache served the vanilla file. It now also bypasses the
cache for patched paths.

That exposed a second fault. `melee_pilot_arena.txt` was refused as "suspiciously small":
the served merge had to be at least half the original's size, and its comments make the
compact merge 1951 bytes against 4056. The check is now structural. `KeyValuesKeepsKeys`
requires every key of the original to survive, matching repeated keys occurrence by
occurrence under the same conditional, which is how the merge treats them. It is
host-tested, including against the real playlist and all eight shipped files.

Now eight patches build at boot (font table, playlists and six weapons), and
`npc_pilot_elite` builds at the first map load. A hosted Kodai match plays with them.

## Parkour server follow-up: client callbacks after a reload, lobby game mode, duplicate accounts (2026-09-28)

PRX `a4537912fe29c211ef54f21d6de89f60f41b89748afe4418359e01ce6aa2039a`, shadPS4 `2b5666b3`.

The user downloaded `Parkour` 0.3.6 through the server browser and joined Parkour servers
three times (sessions at log lines 2605609, 2725709 and 2957945). Each time the browser
enabled the mod, `NSReloadMods` ran and the server's `pk` gamemode loaded. In game the HUD
was missing, the screen blurred and jumping didn't work. After leaving, the game stuck on a
half-drawn blurred frame. Two causes:

- **Stale UI and CLIENT callbacks.** The lifecycle callback lists for UI and CLIENT were
  read once, when their hooks were installed; SERVER rereads its list at every VM.
  After a reload the CLIENT VM compiled Parkour's scripts but never ran its `PKMode_Init`
  "Before" callback. The gamemode was never created, `GAMEDESC_CURRENT = GAMETYPE_DESC[GAMETYPE]`
  failed ("The index pk does not exist"), and the client then raised "The index roundBased
  does not exist" and "HideMeleePrompt" thousands of times per match. `ReloadModState` now
  rereads both lists. Checked by hosting `pk` on `mp_thaw` after a reload:
  `CLIENT Before: PKMode_Init` runs and the client errors are gone. The local server then
  stops in the mod's own `utils/perks.nut` line 157: `GiveWeapon( PK_perks.weapon )` runs
  with an empty name when no perks were configured (a real Parkour server gets them from
  its API). That is a mod bug outside its `if`, not a port difference.
- **The lobby kept the remote server's game mode.** After each Parkour match the local
  lobby started with `SERVER GAMETYPE: pk`, and after an attrition server with `aitdm`.
  `ns_start_reauth_and_leave_to_lobby` had set the `tdm` playlist, as PC does. But the
  local server takes its mode from `mp_gamemode`, which still held the remote server's
  replicated value; PC's engine reverts replicated convars on disconnect. The native
  `SetPlaylist` now also sets `mp_gamemode tdm` whenever the playlist becomes `tdm`,
  which both routes to the local lobby do. Checked: after an `aitdm` match on Glitch, the
  leave command logged `mp_gamemode aitdm -> tdm`, both VMs reported `GAMETYPE: tdm`, and
  the lobby menu appeared.

**Duplicate accounts.** A second shadPS4 machine was refused with "Player's account is
already on the server". Both machines import the same Atlas identity, so they send the
same uid; shadPS4 user names don't change it. PC refuses this too, unless the host is
started with `-allowdupeaccounts`, which patches engine 0x114510. The PS4 check is the
`je` at engine+0xe734c, taken when a connected client's uid (client+0x2d3d5) equals the
connecting one's. With `-allowdupeaccounts` in `/data/northstar_ps4/ns_startup_args.txt`
(PC reads `ns_startup_args.txt` the same way), the runtime replaces that branch with six
NOPs, gated on the 13 bytes from 0xe7345. Checked that the patch applies at boot; a second
machine joining has not been tested.

**Resolved later the same day** (next entry): the message was "Your .dll [..\bin\x64_retail\client.dll] differs from the server\'s."
A PC connecting to a PS4-hosted private match was refused with it; it was first reported
as "binary is different". The PS4 host logs nothing for it. The PS4 engine has the
Source protocol checks ("Client has network protocol %i, server has network protocol %i")
and the PC has "Client persistent data definition cache version mismatch with server"; the
PS4 persistence layout is 929, not PC's 231. The exact message is needed to tell which.

Separately, the newer shadPS4 build `4cbd23ef` stopped at boot on "Invalid or corrupted
deserialization container/shader cache": a pipeline cache written by `2b5666b3`. Moving
`cache/CUSA04013` aside gets past it, as with every build change.

## PC clients on a PS4 host; host options in the private lobby (2026-09-28)

PRX `fb5015365887486a8da7a57149f29e537b970cb30ff77a012efa48b64bf68e40`, shadPS4 `2b5666b3`.

**"Your .dll [..\bin\x64_retail\client.dll] differs from the server\'s."** The connecting PC's nslog had the exact message. It comes from the
PC engine's client CRC check (engine.dll 0x728c0). The client compares the CRC the server
sent (client state +0x101a0) with its own client.dll's, and skips the comparison when the
server sent -1.

The PS4 engine's GetServerClientCRC (engine+0x10d180) returns -1 on a dedicated server.
Otherwise it returns a cached value (engine+0x3ef01a4), or computes the CRC of
`bin/ps4_retail/client.prx` (or `ps4_profile` with `-profilePS4ClientCRC`). No PC can match
that. Its two callers are the server-info fill (0xeb64d) and the crash-report upload
(0x3ebc7, field `cdll`). `InstallHostOptions` seeds the cache with -1, gated on the
function's bytes, so a PS4 host sends what a dedicated server sends. PS4 clients joining PC
servers were never affected. A PC joining a PS4 host has not been tested past this point:
the persistence layouts differ (929 vs 231), and that is the next thing it may hit.

**Duplicate accounts, now switchable.** The one-time NOP patch is replaced with a jump from
engine+0xe7345 to `DuplicateAccountCheck`, a naked stub. It repeats the uid comparison and,
on a match, rejects only if the convar `ns_allow_duplicate_accounts` (registered natively;
PC has only the command-line flag) reads 0. The stub saves rax, which holds the loop bound
there. It reads the convar's int at +0x5c, the offset the engine's own convar tests use.
Checked by booting with `-allowdupeaccounts`: the native read and the script's
GetConVarBool both give 1.

**ns_auth_allow_insecure on a PS4 host.** Every client on a PS4 host is marked READY_INSECURE
at signon, so the host has always behaved as insecure. It cannot check Atlas
authentication, not being registered with Atlas. The convar (registered natively, default
0, PC's autoexec_ns_server.cfg value) now means what it can:
- at 1, anyone may join;
- at 0, Northstar.PS4's SERVER script `ps4_host_options.nut` removes every player except
  entity 1 (the listen server's own client) and bots, once a second.

It is a timer because in the lobby, where private matches start,
CodeCallback_OnClientConnectionCompleted returns before the client-connected callbacks. It
disconnects through the PS4-only SERVER native `NSPS4_DisconnectClient(int client, string
reason)`, which calls CBaseClient::Disconnect (engine+0xd75f0). That address was found
through `kickid` (engine+0x124380), which passes client-array element +0x250 and skips slots
with +0x4d3 set. PC's `NSDisconnectPlayer` still needs an entity-to-client mapping.

Disconnecting from inside the persistence-availability hook was tried first and dropped
before testing: that hook answers a running SERVER script, and returning false there makes
the script error, which ends the host's match.

**UI.** L1 in the private lobby opens a Host Options dialog (`ui/ps4_host_options_menu.nut`)
that toggles both convars and reopens with the new state. Driven with scripted pad input:
the dialog opened, both toggles switched to On, the convars read 1, and the host stayed in
the lobby. Removing a remote player needs a second machine and is untested.

## First PC client on a PS4 host; shadPS4 #5133 fixes the black matches (2026-09-28)

**PC on a PS4 host.** With v0.2.4-alpha the PC got past the client CRC check:
- The PC's nslog (18:12 session) shows it entering the PS4 host's private lobby.
- The host's log (session at shad_log line 4176527) has `client #1 persistence marked
  READY_INSECURE` for the PC player, `The_taskinoz`.
- On `mp_forwardbase_kodai` the host's SERVER finished client script initialization for
  both players.
- The intro ended, and the PC player killed the host's player ("Doing a replay because
  the attacker is a player").

About 80 seconds after loading, the PC reported "Connection to server timed out". The host
kept running frames, the host's player respawned, and no network or script errors were
logged. The shadPS4 log has no timestamps, so the two logs cannot be lined up to the
second.

**Explained:** the user ended the test by closing the game on the host, which is what the
PC's timeout reports. So a PC client played a match on a PS4 host with no failure found.
The persistence layouts differ (PC 231, PS4 929), but that did not stop the lobby or the
match. The host does not save remote players' progress anyway (READY_INSECURE).

**#5124.** squidbus pointed to shadPS4#5133 ("shader_compiler: Improve image type
compatibility": 1D images handled as 2D), merged as `f6cd16e8`. Nightly `4cbd23ef` includes
it. The user's first runs of it stopped on the `2b5666b3` pipeline cache ("Invalid or
corrupted deserialization container/shader cache"). With that cache moved aside (kept as
`cache/CUSA04013.2b5666b3`), 4cbd23ef:
- boots to the Northstar lobby;
- renders Kodai fully (not black);
- returns to a `tdm` lobby and loads Glitch, with no crash and no script errors.

## Text chat (2026-09-28)

Console Titanfall 2 had no text chat, but the PS4 build kept PC's chat code. Chat now
works the way it does on PC.

**Sending.** Engine `ClientSayText` is at engine+0x473e0, with the same arguments as PC's
engine.dll 0x54780 (found through its `resetidletimer` branch). It builds
`clc_ClientSayText`, whose GetName is engine+0x2effd0 and vtable slot 0x3b0dd0.
- The native `say` and `say_team` commands and CLIENT `NSSendMessage` call it.
- Its in-game branch reads the client state through the pointer at engine+0x9f9618
  without checking it. A `say` from the main menu faulted at engine+0x47495, so callers
  now check that pointer.

**Typing.** The system keyboard (`sceImeDialog`, linked with `SceImeDialog` and
`SceUserService`; shadPS4 implements it with its own UI) opens from L2 (Chat) and R2 (Team
Chat) in the in-game menu and the private lobby (`ui/ps4_chat_menu.nut`). The text goes to
CLIENT `NS_PreSendMessage`, as PC's chat box hook does, so mods' OnPreSendMessage callbacks
run. It never passes through a console command, where a `;` would run commands. Parameter
layouts follow shadPS4's ime_common.h: text is UTF-16, and this toolchain's `wchar_t` is 4
bytes.

**Server.** `CServerGameDLL::OnReceivedSayTextMessage` is server+0xaeb50 (PC server.dll
0x1595C0; the slot at server+0x9a7d08 was found with the new `scripts/sce_relocs.py`). The
hook calls Northstar's `CServerGameDLL_ProcessMessageStartThread`, which runs mods'
callbacks and then SERVER `NSSendMessage`. That is now PC's ChatSendMessage: the vanilla
function called past the hook.

**Client display.** The SayText user-message handler (client+0x1db690) has PC's
CHudChat::AddGameLine inlined, and dropped every message for two reasons:
1. The engine's text-restriction check (engine+0x2bf450) returns the convar
   `debug_force_textRestriction` when it is 0 or more, and the console's restriction flag
   (engine+0x3e79e5, which is 1) when it is negative, its default of -1. The runtime sets
   the convar to 0.
2. It writes only to chat panels of type 1 (panel +0x2d8, in the list at client+0x10b1be0,
   next at +0x2f0). The in-match panel `IngameTextChat` and the lobby's `LobbyChatBox` are
   `[$WINDOWS]` only in `hudscripted_mp.res` and `private_lobby.menu`, so only the menus'
   type-0 chat-room panels existed. Northstar.PS4 adds both as `[$GAMECONSOLE]` KeyValues
   patches. A CLIENT script shows the panel as PC's `#if PC_PROG` InitChatHUD and
   UpdateChatHUDVisibility do. It waits for the panel, because the HUD layout is applied
   after the CLIENT init callbacks.

KeyValues changes this needed:
- Patches are matched case-insensitively; the engine asks for
  `resource/UI/HudScripted_mp.res`.
- An entry under a different platform condition is added beside the original, not merged
  into it.
- A mod's copy of a file is the base a patch applies to.
- A patch root merges into the only root whatever its name, as `#base` does. Northstar's
  server_browser.menu calls its root `mods_browse.menu`.

**Tested**, hosting on mp_glitch:
- `say`, `say_team`, and pad typing (L2, "hi", R2 Send) reach the server.
- The server script logs `Received message from ...` and the messages show in the chat
  panel, team messages with `[TEAM]`.

**Receiving.** PC hooks CHudChat::AddGameLine and hands each message to CLIENT
`CHudChat_ProcessMessageStartThread`, which runs mods' OnReceivedSayTextMessage callbacks
and draws the line with `NSChatWrite`/`NSChatWriteRaw`. On PS4, AddGameLine is the loop
over chat panels inside the SayText handler. By then the handler has read:
- the sender index (r14);
- the text (rbp-0x5d0);
- isTeam (byte at rbp-0x5d9);
- isDead (dword at rbp-0x5e0).

The loop starts at client+0x1db91b with the load of the panel list. Those 16 bytes are
now a jump to a naked stub (`runtime_chat_client.inl`). It calls the script (after
`RemoveAsciiControlSequences`, as PC does) and returns from the handler. When the script
function is missing, the vanilla loop runs instead. This matters because custom messages
set the sender's high bit (or use sender 0), and the vanilla loop drops those.

`NSChatWrite`, `NSChatWriteRaw` and `NSChatWriteLine` are PC's LocalChatWriter. The PS4
panel layout is PC's shifted, read from the inlined code:

| Field | PS4 offset |
|---|---|
| Colours: same team, enemy, main text, network name | +0x2bc, +0x2c0, +0x2c4, +0x2c8 |
| Context | +0x2d8 |
| Rich-text panel | +0x2e8 |
| Next panel | +0x2f0 |

The rich-text methods are at PC's vtable slots: InsertChar 0x758, InsertString 0x768,
InsertColorChange 0x7e8, InsertFade 0x810. On PS4, InsertString takes UTF-16. Fade times
come from the settings at client+0x10b1cc0, 0x10b1d50 and 0x10b1de0, read as the handler
reads them. The ANSI colour parser, the control-character filter and UTF-8 to UTF-16 are
in `northstar_ps4/chat_text.h`, host-tested by `tests/chat_text.cpp`. PC's quirks are
kept: text before an escape is cut at 255 bytes, and a malformed escape leaves the byte
after `\x1b[` blanked.

**Broadcasts.** SERVER `NSBroadcastMessage` is PC's ChatBroadcastMessage:
- The sender is 0 (anonymous) or `(index+1) | 0x80`.
- The message type is the text's first byte.

The vanilla sender only sends from a connected player, so the runtime builds SayText
the way it does (server+0xaec8e onward):
1. A CRecipientFilter on the stack (vtable server+0xa30dc0), with AddRecipient at
   0x14a430 and MakeReliable at vtable slot 3.
2. The SayText index from the user-message table (lookup 0x7ef2f0, table 0x149a7f8).
3. g_pEngineServer UserMessageBegin (slot 0xc8), with its buffer stored in the message
   buffer global (0xac2830).
4. WriteByte 0x591bf0, WriteString 0x591cf0, then the inlined one-bit writes.
5. MessageEnd (slot 0xd0), then the filter destructor at 0x6cada0.

Players come from the table the vanilla loop walks:
- gpGlobals is the pointer at server+0xabd3f8, and maxClients is at +0x34.
- Player i is at `[[gpGlobals+0x80] + i*8 + 0xe040]`.

Preimages gate every address.

**Tested** with a throwaway mod that registered a client command calling Northstar's
`Chat_ServerBroadcast` (with and without the tag), `Chat_ServerPrivateMessage`,
`Chat_PrivateMessage` and `Chat_Impersonate`. Each drew the same prefixes and colours as
PC: `[SERVER]` in magenta, `[WHISPER]`, team-coloured names, and untagged text plain.
(`script` is not a console command on PS4; an unknown command goes to the server as a
client command.)

**Rate limit.** PC's `CheckChatLimits` is ported. `sv_max_chat_messages_per_sec`
(default 5) caps each client slot's messages per one-second window, and the rest are
dropped. Tested with eight `say` commands in one console line: five arrived, then the next
second's message went through.

**Private lobby** tested: the lobby's chat box shows history from the match, and L2, typing,
then Send posted "hi" through the server script.

**Also:** the server browser's player and server totals moved right, clear of the PS4
footer (patch on Northstar.Client's server_browser.menu).

## Player identity and the SERVER player natives (2026-09-29)

**UID.** `player.GetUID()` calls VEngineServer slot 0x5c0, `GetPlayerUID` (engine+0x2dba60).
It returns the string at client element+0xf750 for a fully connected client: PC's
`CBaseClient::m_UID`, 0xf500 from the CBaseClient at element+0x250. Neither engine fills it:
- PC Northstar writes it in `AuthenticatePlayer` (`std::to_string(uid)`, "0" for bots).
- The PS4 engine keeps only the number from the connect packet. ConnectClient stores it at
  element+0x2d8a8 (engine+0xe767d); the duplicate-account check compares the same field.

The runtime hooks slot 0x5c0 in the VEngineServer vtable copy the persistence hook already
uses. On first use it fills the string from that number, and the engine clears it when the
slot is reused (engine+0xd5709).

**The number itself.** The listen server's own player connected as uid 1. The engine keeps
rewriting `platform_user_id` from the PSN account id, which is zero under shadPS4, and zero
becomes the literal "1". It does this in at least two places (engine+0xb87da and
engine+0x1191b7). `EnsureConnectUid` only covered joins. Now the connect packet builder
takes the imported Atlas uid instead, at the point it picks the convar's string before
strtoull (engine+0x155516..0x155542, a naked stub). That covers hosting and joins alike,
and without an imported identity it behaves as before.

Tested:
- The host's player reports its Atlas uid.
- A join to a public vanilla server (mp_rise) still authenticated and connected.

**Entity to client.** Natives now resolve a player argument the way server.prx's own
methods read `this` (GetUID, server+0x7a66b0, via 0x62e9a0):
1. A class instance (tag bit 0x408000) has its entity at instance+0x40.
2. The entity's word at +0x68 is the entity index, and client = index - 1.
3. The entity must be the one the player table holds for that index; anything else is
   refused as a null player, as on PC.

With that, these natives are now implemented:

| Native | PS4 implementation |
|---|---|
| `NSIsPlayerLocalPlayer` | PC's `strcmp(localUid, m_UID)`, against the imported uid; client #0 when no identity is imported. Was always true. |
| `NSDisconnectPlayer` | CBaseClient::Disconnect with PC's default reason. Was a stub. Tested by disconnecting the host's own player. |
| `NSSendClientPrint` | VEngineServer slot 0xd8 (engine+0x2d8240), which calls CBaseClient::ClientPrintf (0xd5b60) with "%s", as PC does. Runs without error; client console output isn't in the log, so its arrival wasn't seen. |
| `GetUserInfoKV*_Internal` | The client's userinfo KeyValues at element+0x4a8 (PC's m_ConVars, 0x258), through KeyValues::FindKey (0x20a2f0) and GetString (0x20a5c0), both pinned by `GetClientConVarValue` (slot 0x158, engine+0x2d8d20). Missing keys give the default; int, float and bool parse the string as KeyValues does. |
| `NSGetLoadedMapNames` | PC's RefreshMapList order: mods' loose `maps/*.bsp`, then `vpk_ps4/englishclient_*.bsp.pak000_dir.vpk` (not frontend; mp_common is mp_lobby), then `r2/maps/*.bsp`. 37 maps here. |
| `NSEarlyWritePlayerPersistenceForLeave` | A documented no-op. PC writes only for players Atlas supplied data for (READY_REMOTE), and a PS4 host has none. |

The PS4 userinfo keys are the console set: `match_*`, `sp_*`, `cl_*`, `platform_description`
and so on. There is no `name` key.

**Testing note:** shadPS4 appends every session to one `shad_log.txt` and stops writing it at
about 1 GB. After that, a boot looks hung to the harness, which waits for a log line. Move the
file aside before launching when it gets large.

## Script HTTP requests and the async call queue (2026-09-29)

PC's `scripthttprequesthandler.cpp` is ported in `runtime_http_script.inl`.
`NS_InternalMakeHttpRequest` sits behind Northstar.CustomServers' `NSHttpRequest`,
`NSHttpGet`, `NSHttpPostQuery` and `NSHttpPostBody`. It reads the method, URL, header and
query tables, content type, body, timeout and user agent (tables through the SQTable and
SQArray layouts the JSON natives use). It returns a handle and runs the request on its own
thread.

**Transport.** The request goes through sceHttp, which the port already uses for Atlas:
- a separate template with the Northstar user agent;
- `sceHttpCreateRequestWithURL2` with the method name;
- `sceHttpAddRequestHeader` for Content-Type (POST-like with a body), the script's headers
  and a user-agent override;
- resolve, connect, send and receive timeouts set to the clamped 1-60 s;
- the response headers from `sceHttpGetAllResponseHeaders`.

shadPS4 2b5666b3 implements all of these (its `http.cpp` was read at that commit).
`SetRecvTimeOut` is declared without parameters in the OpenOrbis header, so it is called
through a typed pointer.

**Where PC's rules land:**
- **Query string:** PC's `(query-capable && !POST-like) || body empty` rule, curl's escaping,
  and `?`/`&` depending on an existing query.
- **URL:** a URL without a scheme is https; only http and https are accepted.
- **Launch options:** `-disablehttprequests`, `-allowlocalhttp` and `-disablehttpssl` come
  from `ns_startup_args.txt`, read once.
- **Private destinations:** without `-allowlocalhttp`, the host is resolved
  (`sceNetResolverStartNtoa`). IPv6, a failed resolve and private or reserved IPv4 are
  refused with PC's message and code 0.
- **Pure parts:** these are in `northstar_ps4/http_request.h`, tested by
  `tests/http_request.cpp`.

Differences are in failure details only:
- sceHttp errors are reported as the matching curl code and text: resolve 6, connect 7,
  reply 8, timeout 28, TLS 60, otherwise 56.
- The checked address is not pinned; sceHttp resolves again.
- PC's two range typos (192.18/15 and 192.51.100/24 for 198.x) use the real ranges.
- The timeout applies per phase.

**Queue (G06).** PC delivers results with `SquirrelManager::AsyncCall`, which the host frame
drains per context. Here:
1. Results go to a per-context queue.
2. `NSPS4_RunAsyncCalls()` runs that context's queue on the calling VM:
   `NSHandleSuccessfulHttpRequest( handle, status, body, headers )` or
   `NSHandleFailedHttpRequest( handle, code, message )`.
3. The native host-state frame hook calls it for every currently bound UI, CLIENT and SERVER
   VM after the original frame update. The callbacks run on `MainThrd`, matching PC's drain
   placement; Northstar.PS4's former `ps4_async_calls.nut` polling loop has been removed.

The natives are registered once per context, so each knows its queue without having to
identify the VM when called directly. The native frame path maps each bound VM explicitly.

**Tested** with a throwaway mod making four requests in each VM, at UI init and again for
CLIENT and SERVER after a map load:

| Request | Result |
|---|---|
| GET httpbin.org/get with query | 200, query echoed, 7 headers parsed |
| POST httpbin.org/post with a JSON body and `X-Test` header | 200, both echoed |
| httpbin.org/status/404 | success callback with 404, as PC |
| http://127.0.0.1:8080/ | refused, code 0, PC's message |

## Hosted servers on the Atlas server list (2026-09-29)

PC's server presence and Atlas server protocol are ported in `runtime_atlas_server.inl`:
- `server/serverpresence.cpp`;
- `masterserver/masterserver.cpp` (MasterServerPresenceReporter and
  ProcessConnectionlessPacketSigreq1);
- `server/servernethooks.cpp`.

**Presence.** PC reports from CHostState::FrameUpdate. Here:
1. Northstar.PS4's `ps4_server_presence.nut` (SERVER && MP) calls
   `NSPS4_UpdateServerPresence(GetMapName(), GetCurrentPlaylistName(),
   GetCurrentPlaylistVarInt("max_players", 6), GetPlayerArray().len())` once a second.
2. A reporter thread takes it from there, as MasterServerPresenceReporter does:
   - **add_server**: POST with PC's query (`port`=hostport, `authPort=udp`, name,
     description, map, playlist, maxPlayers, password). `modinfo.json` goes as a multipart
     part: PC's BuildModInfo over the loaded mods, including each mod's `mod.pdiff` text.
   - **Retries**: five attempts, a 20 s pause after DUPLICATE_SERVER, and none after an
     unreadable reply or no connection.
   - **update_values** every `ns_server_presence_update_rate` ms. Atlas re-creates the
     entry if it had dropped it, and a returned id and token replace ours.
   - **remove_server** (DELETE) when the SERVER VM stops reporting for 60 s. That stands
     in for PC's GameShutdown and covers map changes.
3. PC's convars are registered: `ns_server_name`, `ns_server_desc`, `ns_server_password`,
   `ns_report_server_to_masterserver` (1), `ns_report_sp_server_to_masterserver` (0),
   `ns_server_presence_update_rate` (5000), `net_debug_atlas_packet` and
   `net_debug_atlas_packet_insecure`.
4. The name and description get PC's `\uXXXX` unescape.
5. `+name value` pairs in `ns_startup_args.txt` now set existing convars at startup, as
   PC's command line does. Password values are not logged.

**Atlas's packets.** Atlas signs requests with HMAC-SHA256 and the server auth token and
sends them to the game port as 'T' connectionless packets. The hook sits on
CBaseServer::ProcessConnectionlessPacket:
- The IConnectionlessPacketHandler vtable is engine+0x3acfb0; slot 2, at engine+0x3acfc0,
  points to engine+0xe4b40 (PC engine.dll 0x117800). ConnectClient is inlined there, which
  is why the "Bad challenge." string and the duplicate-account check sit inside it.
- The packet layout was read from live packets: raw data (with the `ff ff ff ff` header)
  at +0x18, the bit reader at +0x20 (data base at +0x58), and the size in bytes at +0x60.
- A 'T' packet never reaches the engine. For `sigreq1` the hook checks the signature in
  constant time, then a thread handles the `connect` request as PC does:
  1. It deduplicates the token and reads uid and username.
  2. It fetches the pdata (GET `/server/connect?serverId&token`), which must be status 200,
     non-empty and at most 0xDDCD bytes.
  3. It stores the record and accepts (POST `...&reject=`).
- HMAC-SHA256 (built on the mod-download SHA-256, now with a raw `Finish`) passes PC's own
  self-test vector and RFC 4231 cases 2 and 6 (`tests/atlas_server.cpp`).

**Admission.** A client's connect request ('A', written byte-aligned by the engine at
engine+0x1554c3) holds, in order:
1. `ff ff ff ff 'A'`;
2. four 32-bit fields;
3. the uid (u64 LE);
4. the name;
5. `serverfilter`, which is the Atlas token.

When uid and token match an accepted `connect`, the uid is Atlas-authenticated, as PC's
CheckAuthentication decides. `NSPS4_IsClientAtlasAuthenticated(client)` lets Northstar.PS4's
host options keep such players with `ns_auth_allow_insecure 0`. Their pdata is not
installed (PC 231 vs PS4 929 layouts, G02), so they stay READY_INSECURE with script
placeholder data, and nothing is written back.

**Tested:**
- **Registration.** Atlas accepted the requests: version gate, parameters and modinfo
  passed. It then refused them with `NO_GAMESERVER_RESPONSE` ("failed to connect to game
  port"). This machine is behind carrier-grade NAT (100.64/10); the Windows firewall
  already allows shadPS4. After five attempts reporting stopped, as on PC.
- **Reachability.** A local copy of Atlas's `Hconnect` probe (AES-GCM, `r2crypto.go`) sent
  from the LAN address got the reply Atlas waits for: 'I', the same uid, `connect\0`.
  With or without Atlas's trailing platform byte. The engine ignores the probe from
  loopback. So a host with a reachable UDP hostport would pass verification.
- **Signed requests.** A local `Tsigreq1` packet reached the hook and was refused
  correctly ("no masterserver token yet").
- **Connect parsing.** The local client's own connect request parsed with its name. Its
  token was empty, as a local connect has none.
- **Startup arguments.** `+ns_server_name "..."`, a description with `\u00e9`, a hidden
  password and an unknown name were each applied or reported (`tests/startup_args.cpp`).
- **End to end (later the same day):** the user's host had a reachable port. The server
  registered and was listed with `hasPassword: true` once the password came from
  `ns_startup_args.txt`. Atlas takes the password only at creation, and the harness's
  `launch` starts the lobby before console commands run.
  - Two PC players joined through the browser. Their first attempts failed on the engine's
    own serverfilter comparison ("Incoming server filter of ... doesn't match our server
    filter of"). PC patches that out (engine.dll 0x114655 -> EB); here the `je` at
    engine+0xe749b becomes `jmp`.
  - A PC client's connect request has an empty string before serverfilter, so admission
    now matches the token against every string after the name.
  - After both fixes, each player's Atlas `connect` was verified, 56306 bytes of pdata
    fetched, and the connection accepted and admitted with `ns_auth_allow_insecure 0`.
  - They played a fastball match on mp_lf_uma and returned to the lobby. Chat worked both
    ways, and the server script logged their real UIDs.
  - Their pdata is not installed yet (G02).
  - `net_debug_atlas_packet 1` logs each request in full, including the player's IP address
    and token, as PC does. Leave it off outside debugging.

**Debugging:** `net_debug_atlas_packet 1` logs each connectionless packet's type, each
Atlas packet and each connect request's parse.

**Boot hang (intermittent).** Twice on 2026-09-29, the first boot of a freshly deployed
PRX stopped at the Northstar.Custom localisation load, with the log cut mid-line and a
spinner on screen. The same PRX then booted normally.

## Remote players' pdata on a PS4 host (2026-09-29)

**Layouts.** Atlas and PC use persistence layout 231. Its structure is 56,169 bytes (Atlas
`pkg/pdata` UnmarshalBinary), with the version, 231, as the first int. A save carries
trailing bytes after that structure, which Atlas keeps as they are; its placeholder pdata
is 56,306 bytes, and PC mods' pdiff fields live in that space.

Northstar.PS4's `persistent_player_data_version_929.pdef` is 231 followed by the console's
black market (`bm`, 181 bytes), 56,350 bytes in all. Checked by parsing both definitions:
- the first 165 root members are 231's, in order;
- `bm` is the only addition, at the end;
- all 48 of 231's enums and structs are identical member by member;
- the only new types are the four black-market ones.

So the 231 range lines up byte for byte. The earlier note's "56,169 = 231's size, 612 bytes
spare" is right about the structure. The pdata Atlas sends is larger because of the
trailing bytes.

**Conversion** (`northstar_ps4/pdata_convert.h`, `tests/pdata_convert.cpp`, run against
Atlas's own placeholder pdata):
- **Atlas to PS4:** copy the 231 structure and zero the rest of the 56,781-byte buffer, so
  the black market starts empty. The trailing bytes, which would land on the black market,
  are kept aside.
- **PS4 to Atlas:** the 231 structure plus the kept trailing bytes, so the save keeps its
  size and whatever PC mods stored there. It is refused if the buffer no longer starts with
  version 231.

**Install** (PC AuthenticatePlayer):
1. Atlas's `connect` handling keeps the fetched pdata with the record; the 16 most recent
   records keep theirs.
2. When a client's connect request matches the record's uid and token, the pdata waits for
   that uid.
3. On the slot's first persistence check once fully connected, runtime_persistence.inl
   copies it into the buffer (element+0x74a) and sets `READY_REMOTE` (4; the engine reads
   `> 2`).
4. Anyone else stays `READY_INSECURE`.
5. Northstar's InitPersistentData leaves an installed save alone, because
   `initializedVersion` (231) is non-zero.

The engine keeps a slot's state and buffer through map changes (the host's slot is marked
once per session), and a reconnect is a new connection with a new Atlas record.

**Write back** (PC WritePersistentData, POST `/accounts/write_persistence?id&serverId`, the
pdata as a multipart `file.pdata`):
- **On disconnect.** CBaseClient::Disconnect (engine+0xd75f0) starts with a detour: its
  first 13 bytes (the register pushes) move to a stub that keeps every argument register,
  including xmm0-7, since the function is variadic.
- **On `NSEarlyWritePlayerPersistenceForLeave`**, and on each map change (the next SERVER
  VM's init).
- **`NSIsWritingPlayerPersistence`** reports writes in flight.

Writes are gated by `ns_ps4_write_remote_persistence`. It was 0 until the test below, and
is 1 (on, as PC) from 2026-09-29. `+ns_ps4_write_remote_persistence 0` in
`ns_startup_args.txt` turns writes off, and then the host logs what it would have written.

**Tested:**
- **The detour.** The host's own disconnect goes through it and hosting resumes.
- **Late install fails.** The first install came at the slot's first persistence check,
  after the client had been sent its data. The engine only sends script changes after
  that, so the PC kept zeros and stopped at `sh_loadouts.nut` IsTitanClassPrime:
  "Execution reached script marked as unreachable".
- **Install at connect works.** The install now happens right after the engine has
  handled the connect request that carried the player's token (the slot is at signon 2),
  as PC does in CBaseClient::Connect. It is installed again if the engine resets the slot.
  With that, a PC player joined and confirmed their progress loaded.
- **Connect retries.** A client that retries its connect request is disconnected and
  connected again with nothing played. So a write goes out only when the save differs
  from what was last received or written, and it logs how many bytes changed.
- **Writes, first test.** With `+ns_ps4_write_remote_persistence 1` (startup args) and
  the player's consent:
  - Connect retries logged "pdata unchanged (disconnect), nothing to write", as did the
    map change into the match.
  - Two matches produced writes of 162 and 104 changed bytes. Both went out as full
    56,306-byte saves and got status 200 from Atlas.
  - On PC the player's end-of-match summary showed the PS4-hosted match with its merits
    and credits applied (Skirmish on Forwardbase Kodai, 1 kill, victory). That summary is
    read from the saved pdata.
- **The default is now 1**, as PC writes always. `+ns_ps4_write_remote_persistence 0`
  turns writes off.

## The host's own save, and PC servers' unlock rule (2026-09-29)

**Own save** (PC MasterServerManager::AuthenticateWithOwnServer). "Launch Northstar" calls
`NSTryAuthWithLocalServer`, which now POSTs `/client/auth_with_self?id&playerToken` on a
worker thread; `NSIsAuthenticatingWithServer` is true meanwhile. Atlas answers with the
account id, an auth token and the save as a JSON array of bytes (`ParseSelfAuthResponse`
in server_list.h, host-tested).
- The save becomes an Atlas record like an accepted `connect`.
- `NSCompleteAuthWithLocalServer` puts the token in `serverfilter`, as PC does.
- The host's own connect request then matches, and the save is installed at connect as
  `READY_REMOTE`.
- Writes go through the same path. For a player on their own server, Atlas only checks
  that the write comes from the IP address that authenticated
  (`acct.IsOnOwnServer()`), so no server id is needed.

When the request fails (expired token, no connection), PC does not start the lobby. Here
the lobby starts with the local placeholder save and the reason is logged, so an expired
imported token does not lock a PS4 player out. `serverfilter` is cleared in that case, so
a token left over from a join is not sent.

Verified: the lobby showed the account's real progress (Level 50, "Regen Available", 650
credits), where earlier sessions showed Level 1 and 20 credits. A match's changes were
written to the account (status 200).

**`everything_unlocked`.** The first such match kicked the host with "Resetting invalid
loadout":
- `FailsItemLockedValidationCheck` failed for pilot loadout 1's `redline_sight`, and
  `sh_loadouts.nut` reset it to the default.
- The reset (2 bytes) was then written to the account.

PC never hits this. Its hoststate hook runs `exec autoexec_ns_server` on every NewGame,
listen servers included, and Northstar.CustomServers' file sets `everything_unlocked 1`.
The port does not run that file, so a PS4 host checked real unlock progress. That also
applied to remote players from v0.2.7-alpha on, since writes default on there.

The runtime now sets `everything_unlocked 1` at startup, before startup arguments are
applied. Checked against the file's other values on PS4:

| Setting | PS4 | PC file |
|---|---|---|
| `base_tickinterval_mp` | 0.016666667 | same |
| `sv_updaterate_mp` | 20 | same |
| `sv_max_snapshots_multiplayer` | 300 | same |
| `net_data_block_enabled` | 1 | 0 |
| `host_skip_client_dll_crc` | 0 | 1 (the port skips that check its own way) |
| `ns_erase_auth_info`, `net_chan_limit_*`, `sv_querylimit_per_sec` | not registered | set |

Verified: the same match with the fix gave no reset and no kick.

## Host Options: listing, name and password (2026-09-29)

PC servers take these from a config file or the command line. On PS4 the private lobby's
L1 Host Options sets them (Northstar.PS4 `ui/ps4_host_options_menu.nut`).
- A dialog has four buttons (`dialog.menu` has four `DialogButtonClass` elements), so the
  listing, name, description and password are on a second dialog.
- The dialog closes when a button is chosen; the script opens it again with the new state
  and the same button focused.

**Text entry.** `NSPS4_OpenTextInput(title, text, maxLength, secret)`,
`NSPS4_UpdateTextInput()` and `NSPS4_GetTextInput()` (UI, `runtime_chat_ui.inl`) use the
same sceImeDialog as chat, one keyboard at a time. The buffer starts with `text`, and
shadPS4 shows it. The password option (4) is refused with `0x80bc0030` (invalid parameter)
unless the type is Basic Latin (1) or Number, as on the console, so a secret field uses
Basic Latin.

**Saving.** `NSPS4_SetHostOption(name, value)` sets one of the six console variables in
`host_options.h` and rewrites that entry in `/data/northstar_ps4/host_options.txt`
(`name=value` lines). Only entries changed in the menu are written, so values from startup
arguments are not saved. At startup the file is read before `ns_startup_args.txt`, and the
startup arguments win, as PC's `+` options do over its configs. The password is logged as
"(value hidden)".

**Listing** (the presence reporter in `runtime_atlas_server.inl`):
- PC stops heartbeats when `ns_report_server_to_masterserver` goes to 0, and Atlas drops the
  entry later. Here the reporter sends `remove_server` straight away, and starts registration
  again (with fresh attempts) when it goes back to 1.
- `update_values` carries the name and description, and Atlas updates both.
- Atlas reads `password` only when a server is created. The reporter keeps the password the
  listing was created with; when `ns_server_password` differs, it removes the listing and
  adds it again.
- `NSPS4_IsServerListed()` reports whether an add succeeded, for the dialog's status line.

Verified with scripted pad input: listing on and off (Atlas list checked), a name change
shown by Atlas within one heartbeat, a password change re-listed with `hasPassword`, an
empty password cleared, and the file read back at the next boot.

shadPS4 logs each HTTP request's URL at Info, and the Atlas API puts the password in the
query, so it reaches `shad_log.txt` unless `Lib.Http` is filtered to Warning. The token was
already a reason for that filter.

## Mod features used on Thunderstore; dependency constants (2026-09-29)

To pick what to port next, the GitHub sources of about 100 of the most downloaded Northstar
mods on Thunderstore were checked for the mod features they use (scratchpad survey, repository
trees and `mod.json` through the GitHub API):
- `Dependencies` constants: HUD Revamp, Titan Framework, VanillaPlus, BetterServerBrowser,
  Server Utilities and others. Missing on PS4, so `#if CONSTANT` failed to compile.
- `audio/` replacements: several sound mods. Missing.
- rpaks: most skin, model, reticle and titan mods. Loader present, off (PC formats).
- `RunOn` expressions, `ConVars`, `Localisation`, `keyvalues`, `InitScript`, `.menu`/`.res`
  files: common, already supported.
- Datatable CSVs and particle manifests: one mod each. `ConCommands`, `mod.pdiff`: none.

**Dependency constants** (PC `mods/mod.cpp` ParseDependencies, `squirrel.cpp` VMCreated).
`mod_dependencies.h` (host-tested) reads `Dependencies` (`{ "CONSTANT": "Mod Name" }`) and
`PluginDependencies` (`[ "CONSTANT" ]`) from every mod, enabled or not; the first mod to
register a name keeps it. Each VM gets the constant as whether an enabled mod has that
`Name`. Plugin constants are always false, since no plugin can load here. `ScriptConstants()`
builds the list with `VANILLA`, `NS_VERSION_*` and `MAX_FOLDER_SIZE` (51200, PC's 50 MiB / 1024,
which the port lacked) for the UI, CLIENT and SERVER constant tables. It is rebuilt for each
VM, so a reload's enabled set applies.

**Manifest fix found by the test.** A mod listing one file three times (`RunOn` UI, CLIENT and
SERVER) only compiled it in SERVER: the `scripts.rson` generator kept one entry per path so a
higher-priority mod could override a script, and that also collapsed a mod's own entries. PC
writes a block per entry. Repeats within one mod are now kept; another mod's entry still
replaces them.

Verified with a throwaway mod: all three VMs printed `custom yes`, `missing no`, `plugin no`
and `MAX_FOLDER_SIZE 51200` from `#if` blocks.

## Custom audio (2026-09-29)

PC (`client/audio.cpp`) replaces the sample Miles is about to load when the sound event has
an override. Mods ship `audio/<name>.json` (`EventId`, optional `EventIdRegex` and
`AudioSelectionStrategy`) and `.wav` files under `audio/<name>/`.

**Miles on PS4.** Miles 10.0.10 is linked into `client.prx` (PC: `mileswin64.dll`). The two
functions PC hooks were matched by their strings and structure:

| PC (mileswin64) | PS4 (client.prx) | Role |
|---|---|---|
| `0xF110` | `0x99b0` | LoadSampleMetadata(sample, buffer, length, type); "Unknown File Type", "Bink Audio decoder has not been registered." |
| `0x294C0` | `0x22450` | plays an event; the event's name is at arg2+0x30 on both |

- The sample keeps its data pointer at `+0xe8` and its length at `+0xf0` on both platforms
  (the PS4 callers load both from there before calling).
- On PC the sample setup is inlined into the event function. On PS4 it is a separate function,
  `0x9f50`, called from `0x22450`.
- Type 64 makes Miles detect the format (`0xcc80`: `1FCB` Bink, `3PMM` MP3, `OggS`, `RIFF`…`WAVE`).

**Hooks.** shadPS4 cannot give this module an executable trampoline, so the call sites are
patched instead of the functions:
- The event function's three call sites (`0x162c4`, `0x2bd0f`, `0x2cc35`) go through
  `AudioEventStub`. It stores `[rsi+0x30]` and jumps on.
- LoadSampleMetadata's four call sites (`0x9fbf`, `0xa40a`, `0x133a1`, `0x13d1c`) go to
  `AudioLoadSampleMetadata`. It swaps in the replacement and calls Miles with type 64, as PC
  does.

The rules are PC's, in `audio_override.h` (host-tested):
- lookup is exact, then `*`, then the regexes, with regex matches cached;
- the first mod in load order keeps an event;
- a sample whose folder names an already claimed event is skipped;
- `!event` leaves an event alone, and `*` leaves ambient and emitter events alone;
- selection is sequential or random, and no samples means silence (PC's empty WAVE);
- `ns_print_played_sounds 1` logs events, and on PS4 also each replacement.

Samples are read on first play and kept for the session. Miles plays from the buffer it was
given, so nothing is freed, not even on reload (`ReloadModState` builds a new registry).

**Two PS4-only problems found in testing:**
- `std::regex` is unusable: the build has no exceptions, and libc++ aborts on a malformed
  pattern. `regex_lite.h` is a small backtracking matcher for the ECMAScript subset these
  patterns use. It is checked against `std::regex` on the host, rejects unsupported syntax, and
  caps its steps.
- A global `std::unordered_map` aborted on its first insert. This module runs no static
  constructors, so the map was all zeroes, including a max load factor of 0. It is now
  allocated at install time. (A zero-filled `std::string` or `std::vector` happens to be a
  valid empty one, which is why other globals worked.)

Verified: `menu_focus` replaced by two generated 16-bit tones in rotation (Miles returned
success each time, no crash). Whether they are audible still needs someone listening.

## Particle manifests (2026-09-29)

PC combines `mod/particles/particles_manifest.txt` from enabled mods after the retail
manifest. The PS4 runtime now does the same in load-priority order. `particle_manifest.h`
parses a BOM, comments, quoted or unquoted root name and balanced braces without treating
braces inside comments/strings as structure. It rejects an incomplete manifest instead of
serving a truncated file.

`runtime_particles.inl` reads the retail manifest through the original filesystem, appends
each valid mod body, and writes `/data/northstar_ps4/particles/particles_manifest.txt`.
OpenEx, ReadFile, Size and ReadFromCache all use the generated copy, and a mod reload clears
the generation flag. One bad mod manifest is logged and skipped rather than hiding retail
particles or the other mods.

The parser/builder suite, the complete host profile suite and a full runtime-manifest PS4
build pass. Live validation on 2026-09-30 used a development-only isolated fixture made
from Moblin.Archon 1.7.1's unmodified PCFs. `P_wpn_mflash_arcball_st` precached as `-484`
and spawned as handle `65538` in the CLIENT VM. A second persistent fixture using
`wpn_vortex_chargingCP_mod_arc` produced the same successful readback and visibly rendered
the blue vortex arc around the lobby pilot. The successful transcript is
`work/stage2/iterations/20260930-005639/shad-new-lines.log` and the capture is
`work/particle-fixture/moblin-archon-persistent-particle.png`. This proves manifest
combination, engine ingestion and rendering of real third-party PCF data; it does not claim
that the complete Moblin.Archon package or its TitanFramework/RPAK dependencies work.

## Custom CSV datatables (2026-09-30)

PC Northstar replaces the engine's datatable builtins while each Squirrel function is
registered, then resolves `datatable/*.rpak` names to enabled mods' corresponding
`scripts/datatable/*.csv` files. The PS4 port now intercepts only client executable call
sites whose decoded rel32 target is the verified native registrar. The registrar preimage
and every call target are checked before mutation; calls for unrelated engine functions
remain unchanged. The same exact-target interception is installed in `server.prx` before
its VM initializes. Client and server originals are retained separately as the fallback
when no enabled mod supplies a CSV.

`datatable_csv.h` parses UTF-8 BOMs, CRLF, quoted commas, doubled quotes and empty cells,
and rejects NULs, unterminated rows/quotes and unexpected text after a closing quote. The
runtime caches the highest-priority resolved mod file, clears that cache on mod reload and
supports row/column counts, string/asset/int/float/bool scalar accessors, exact row searches
and PC-compatible integer/float comparison searches. Vector cells use PC Northstar's
`<x,y,z>` text form. The return value packs the three IEEE-754 components in the Squirrel
object exactly like PS4's stock `GetDataTableVector` at client+0x77e6a0; exact vector row
matching is supported too.

Live validation used shadPS4 `2b5666b3` and experimental PRX SHA256
`48398ffa878b2c778c1747b6fe221047a883a77975d0e219d400e1ce88b68c17`. Normal startup loaded
Northstar.CustomServers tables including `titan_properties`, `xp_per_player_level`,
`startpoints`, `camo_skins`, `calling_cards`, `burn_meter_store` and
`burn_meter_rewards`. The harness independently queried `burn_meter_rewards` and returned
`20|burnmeter_maphack|125|0.700|1`, exercising row count, column lookup, row matching and
all scalar accessors. The boot transcript is
`work/stage2/iterations/20260930-013544/shad-new-lines.log`; the later command and CSV-load
lines are in that session's appended shadPS4 log. A later isolated fixture live-verified
vector access and matching in UI (`1.500|-2.000|3.250|1`) and SERVER. The SERVER module had
582 exact calls to its registrar patched; its After callback printed the same vector and row
from a custom CSV. The SERVER transcript is
`work/stage2/iterations/20260930-085038/shad-new-lines.log`, using experimental PRX SHA256
`7b69ace327002969af68f70dae4dc0dabcdfb6e91961e3423cf04a6b8b2a7430`. The fixture was removed
from the installed profile after the test. The portable parser suite and complete host
profile suite cover malformed inputs and the runtime build.

## PS4 system-language localisation (2026-09-30)

The PS4 `CLocalise::AddFile` does not expand a mod path's `%language%` token. The runtime
now reads `ORBIS_SYSTEM_SERVICE_PARAM_ID_LANG` through `sceSystemServiceParamGetInt`, maps
the languages for which Titanfall/Northstar commonly ship suffixes (`japanese`, `english`,
`french`, `spanish`, `mspanish`, `german`, `italian`, `portuguese`, `russian`, `polish` and
`tchinese`), and uses English for unsupported system languages. It checks the enabled mod
overlay before adding a non-English path; a mod that lacks that translation gets its English
file rather than silently losing all of its tokens.

The portable mapping suite covers both English variants, Canadian French, Brazilian
Portuguese, Latin-American Spanish, Traditional Chinese and unsupported-language fallback.
Live validation on shadPS4 `2b5666b3` returned system language ID 1 (`english`) with result
0, loaded Northstar.Client, Northstar.Custom, Northstar.DirectConnect and Northstar.PS4's
English files, then completed the UI lifecycle. The experimental PRX was SHA256
`6e7eca7d29d205b71de784eb20ff32e95d1345a0f33bc4841ae13094989165c9`; transcript
`work/stage2/iterations/20260930-085714/shad-new-lines.log`. A non-English emulator setting
still needs visual acceptance; only its mapping and missing-file fallback are host-tested.

## STARPak ownership and the remaining RPAK boundary (2026-09-29)

PC does not register a STARPak with the RPAK load call. It reads the NUL-separated stream
paths at v7 header offset `0x58` (length at `0x38`) during mod discovery, then redirects a
later low-level stream open to `<mod>/paks/<embedded path>`.

The PS4 `rtech_game.prx` worker has the same split:

- `+0x601a` checks an embedded name for `_hotswap.starpak`;
- `+0x6717` builds `/app0/r2/<embedded path>`;
- `+0x6773` is the only call to the module's file opener (`+0x0a90`) on that loop.

The port now parses and validates those paths, rejects absolute/traversing references,
records their owning mod and has an exact-preimage rel32 patch for `+0x6773`. If that patch
cannot be installed, the RPAK hook refuses to install rather than loading a streamed archive
without its data. Host tests use both malicious fixtures and the real
`mp_weapon_shotgun_doublebarrel.rpak`; the real archive reports its expected one STARPak.

The outer flag correction matters: `0x0100` is not a PS4 marker. Matching retail PC and PS4
`camo_skin00_col.rpak` files both have v7 flags `0x0100`; it denotes the RTech-compressed
payload. Northstar's generated mod archives are uncompressed and use `0x0000`. Platform
information lives deeper in texture assets: LegionPlus identifies texture-header byte `8`
as PS4 and unswizzles its 8-by-8 Morton blocks, while RePak emits PC-oriented linear texture
data. The official LegionPlus 1.9.2 CLI successfully decoded and listed the same texture from
both local retail PC and PS4 archives, confirming the comparison is of equivalent assets.

The initial live test exposed a startup race: the module watcher installed the hook after
`common.rpak` had entered the queue but before its worker open. The loader therefore never saw
the dependency request. The pak table at `rtech_game+0x2a66d08` has 512 entries of 0xa8 bytes;
each retains its handle (`+0x00`), state (`+0x04`), copied request name (`+0x10`) and allocator
(`+0x20`). Catch-up now waits for the named dependency's successful terminal state 7, then
queues its dependants. Queueing immediately while the dependency was still active reproduced
an access violation on RTech worker threads; waiting for state 7 removed it.

Normal filesystem-overlay builds now include the loader. Discovery refuses an archive unless
the bounded v7 inspector succeeds and every texture carries the verified PS4 platform marker;
PC-linear, mixed, compressed/patch, texture-free and unknown layouts never reach the engine.

The v7 archive inspector now walks the slab/page/asset tables with bounds checks and reports
each texture header's platform marker before an experimental load. It refuses compressed and
patch archives, whose page pointers cannot be interpreted without decoding or the base pak.
Against Northstar.Custom's real double-barrel shotgun archive it reports 18 textures, all 18
PC-linear and none PS4-swizzled. This establishes that existing weapon-skin archives need a
texture-data conversion/rebuild; renaming or only changing the header marker is not sufficient.

**Playlist variable overrides.** The stock PS4 SERVER native at server+0x6cd2e0 identifies
the engine-server interface operation: vtable slot 72 is engine+0x2d9120, a thunk to
engine+0x1491d0 `SetPlaylistVarOverride(name, value)`. `setplaylistvaroverrides` now mirrors
Northstar's multi-pair command and guards the retail table's 64-entry capacity plus its fixed
128-byte name and 64-byte value buffers. The setter's six-byte pre-map conditional is NOPed,
matching PC Northstar's ability to stage overrides before map initialization. It is
live-verified with release PRX SHA256
`a42f4bbea26418233639e8607d6a5d25412e7300e4cba74cd2bbf2293a90b9ba` on shadPS4
`2b5666b3`: from the local `tdm` lobby, one command changed `scorelimit` from 75 to 91 and
`timelimit` to 17, and both values read back through `GetPlaylistVarOrUseValue`. A 128-byte
name logged `playlist override refused: invalid name/value length`; the valid score limit
remained 91. AI.Harness now has a `playlistvar` readback action for this test. The successful
boot transcript is `work/stage2/iterations/20260930-002830/shad-new-lines.log`; the post-boot
command evidence is in the same appended shadPS4 session log. The real controller menu and a
match visibly ending at the overridden limit remain manual acceptance checks.

## Offline PS4 texture conversion for mod RPaks (2026-09-29)

Matching uncompressed retail patch archives provided the missing size rule. PC
`camo_skin101_col(01).rpak` is 44,111 bytes and its 256x256 BC1 permanent mip page is
43,728 bytes. The PS4 archive has the same texture GUID and header version, but is 46,463
bytes: the page is 46,080 bytes, platform byte is 8 and usage bit 0 is set. Every PS4 mip
occupies complete 8x8 compression-block tiles, so the five smallest BC1 mips each consume
512 bytes. RSX also establishes the second platform difference: PC v8 groups are stored
bottom-to-top, while a swizzled texture is stored top-to-bottom.

`rpak_texture_converter.h` implements the offline conversion for uncompressed, non-patch
Titanfall 2 v7 archives. It:

- converts PC linear blocks into the PS4 8x8 Morton order and reverses physical mip order;
- applies complete-tile padding, updates `dataSize`, platform/usage bytes, page and slab sizes;
- rebuilds permanent-mip pages and relocates asset pointers, pointer descriptors, their
  pointed-to `PagePtr` values and asset-use locations;
- converts streamed mip blocks within their existing 4 KiB STARPak allocations, preserving
  offsets, entry tables, unrelated blocks and cross-asset references;
- refuses compression, patch archives, arrays, unknown formats, malformed bounds, conflicting
  shared stream conversions and any converted stream that does not fit its allocation.

The host CLI and `Convert-NorthstarModRpaks.ps1` write a fresh overlay. Profile creation and
sync expose it only through `-ConvertRpaksForPs4`, so source mods and retail archives remain
unchanged. Normal runtime builds contain the loader and reject unconverted archives safely.

Validation:

- the 256x256 retail PC camo converts to exactly 46,463 bytes with a 46,080-byte mip page;
- LegionPlus exports the original PC camo and converted PS4 camo to byte-identical DDS files;
- Northstar.Custom converts 47 textures and 41 distinct streamed blocks. The shotgun RPak
  grows 197,556 -> 244,692 bytes while its 9,605,416-byte STARPak and every stream offset stay
  fixed; LegionPlus exports all 18 PC/converted shotgun DDS files byte-identically;
- a real converted profile reports 18 PS4 and zero PC texture headers for the shotgun pack;
- two consecutive converted syncs report 840 added files, then 840 unchanged files;
- all host suites pass.

Live validation used shadPS4 `2b5666b3` and the converted deployed profile. The final test build
was PRX SHA256 `ed9ce0505fb3d972aca41e69aefc9e83bdab73762e26dde83965c291d8f1eaa9`;
`work/stage2/iterations/20260929-235400/shad-new-lines.log` reached UI lifecycle completion in
38.523 seconds. The late catch-up loaded the shotgun and event packs with handles 3 and 4,
both mod RPaks were opened by the worker, and both embedded STARPaks redirected and opened from
Northstar.Custom. No fatal or pak error followed.

For a visible end-to-end check, only the installed test copy of `_event_models.gnut` had its
December date condition temporarily forced true (the source mod was not changed). Launching the
local lobby loaded the holiday tree/floor models and their material files, and the tree rendered
with its coloured light textures in `work/rpak-event-model.png`. The installed script was then
restored by a converted profile sync. The double-barrel archive also loads cleanly, but its
weapon definition points at a model absent from both the PC and PS4 game data, so it is not a
valid visual test target.

## Runtime mod persistence diffs (2026-09-30)

PC Northstar's `mods/compiled/modpdef.cpp` starts with the stock persistence definition, walks
enabled mods in load order, inserts each `$ENUM_ADD` block before its existing enum's
`$ENUM_END`, then appends declarations after `$PROP_START`. The PS4 runtime now performs that
merge against Northstar.PS4's PC-231-compatible 929 base and atomically serves the generated
file from `/data/northstar_ps4`. Reload invalidates the generated copy so the next request uses
the new enabled set.

Unlike the permissive PC implementation, generation is refused with the offending mod name
when a directive is malformed, an enum target is missing, the result cannot be parsed, the
serialized data exceeds the game's 56,781-byte player buffer, or the text exceeds the 53,248-
byte file limit. The game never falls through to a partly merged or stale definition after one
of those failures. Host tests cover enum insertion, property appending, atomic failure and the
real shipped base: 56,350 bytes of player data, increasing to 56,354 for a fixture integer.

Live validation used shadPS4 `2b5666b3`, PRX SHA256
`5acdf6c1e3d4d39be2855203165819c4d97aaa54c33e2295c0a5a99968fbfefe`, and an isolated
temporary enabled mod containing `$PROP_START` plus `int ps4PdiffFixture`. The runtime logged
`pdef generated mods=1 data=56354 file=33931`, served that file through `ReadFile`, and the
SERVER VM reached `SERVER lifecycle completed result=1` without a script, PDEF, fatal or guest
error. The fixture was removed after the test. Transcript:
`work/stage2/iterations/20260930-103304/shad-new-lines.log`.

## Per-module Squirrel helpers (2026-09-30)

Squirrel helper implementations are not interchangeable across the game modules: they use
module-local globals and allocators. UI and CLIENT continue to use the verified client.prx
copies, while SERVER now dispatches through the corresponding server.prx functions selected
from the owning VM. The matched client -> server offsets are `sq_throwerror` 0x682a60 ->
0x634320, `sq_pushstring` 0x682e00 -> 0x6346c0, `sq_pushasset` 0x682f40 -> 0x634800,
`sq_newtable` 0x683230 -> 0x634af0, `sq_newarray` 0x683330 -> 0x634bf0,
`sq_arrayappend` 0x6835e0 -> 0x634ea0, `sq_newstruct` 0x684970 -> 0x636230,
function lookup 0x685cf0 -> 0x6374d0,
object push 0x6875f0 -> 0x638ce0 and `sq_call` 0x6876c0 -> 0x638db0. The runtime gates
both modules' helper preimages before installing natives and binds each VM to its owning
module when that VM is initialized.

Later typed-struct testing corrected one entry in that initial profile: server+0x6363c0 is
the function following the operation represented by client+0x684b00, not a SERVER
`sq_sealstructslot`. Retail server natives inline the retain/copy/release sequence instead;
the runtime now does the same. See the SERVER typed-native entry below.

Live validation used shadPS4 `2b5666b3` and experimental PRX SHA256
`92962323efdb38cbc14bf5269375f40f42bb5e5a7ac0386964aa1e78ee537c36`. An isolated fixture
round-tripped nested JSON through `DecodeJSON` and `EncodeJSON` and printed
`{"nested":{"value":2.5},"name":"ps4","items":[1,true,"ok"]}` from UI, SERVER and CLIENT.
After a `map mp_forwardbase_kodai` transition the SERVER and CLIENT callbacks completed again,
which also exercised rebinding on new VM generations. The successful boot transcript is
`work/stage2/iterations/20260930-105108/shad-new-lines.log`; the temporary fixture was removed.
An earlier launch in `work/stage2/iterations/20260930-105025` faulted in shadPS4 JIT code before
any VM initialized, so it is recorded separately rather than attributed to this change.

## VM destruction and async-generation cancellation (2026-09-30)

NorthstarLauncher revision `f698c8a90b9d3d6d1740eda57101db7011d051d2` destroys a context's
message buffer with its Squirrel VM and drains the active buffers from
`CHostState::FrameUpdate` (`engine/hoststate.cpp`). The PS4 HTTP queue previously belonged only
to a context number, so a worker that finished after teardown could enqueue into that context's
replacement VM.

Each UI, CLIENT and SERVER VM now starts a new async generation. Teardown increments the same
generation and clears its queue while holding the queue lock; worker completion takes that lock
and enqueues only if the generation captured when the request started is still current. This
closes both sides of the race: a result queued before teardown is cleared, and one finishing
after teardown is rejected. Safe-I/O results remain keyed to the requesting VM pointer and are
dropped at the same teardown sites.

SERVER teardown now has PC callback parity too. Normalized disassembly matched client.prx's
68-instruction release function at 0x6787a0 exactly to server.prx 0x629e80. All three direct
SERVER release calls are gated and redirected before the engine frees the VM: 0x1b60dd,
0x1b644b and 0x70d022. The hook dispatches enabled mods' ordered `ServerCallback.Destroy`
functions, removes the print sink, retires async work, drops safe-I/O results, unbinds the
module-local helpers and clears lifecycle ownership before calling the untouched release.

Live validation used shadPS4 `4cbd23ef` and experimental PRX SHA256
`79c1caa365f12411c36dd2f9e995ccdb9dd76d70ee098855fa9cebf65c555bc4`. A temporary SERVER
fixture initialized in `mp_lobby`, printed its Destroy callback before lifecycle state was
cleared, then initialized again in `mp_forwardbase_kodai`; repeated lobby/map transitions did
the same without a fresh fatal or guest exception. The successful boot transcript is
`work/stage2/iterations/20260930-142754/shad-new-lines.log`; the later lifecycle evidence is in
that boot's appended shadPS4 log (fixture Init at line 3532036, Destroy at 3542432, state clear
at 3542434 and replacement Init at 3556748). The fixture was removed afterward.

Two attempted delayed-request acceptance checks were inconclusive because the public endpoints
returned immediately (200 and 404) before the old VM was destroyed. The locked generation
implementation is present, but its late-worker rejection log still needs a deterministic local
delayed endpoint test.

## Native host-state frame drain (2026-09-30)

PC Northstar hooks `CHostState::FrameUpdate` at engine.dll RVA 0x16db00 and drains UI, CLIENT
and SERVER message buffers after the original update. PS4 Clang inlines the same state handlers
into engine.prx 0x1334d0: it has the same eight-state dispatcher, the same 0.5/1.5 transition
values, the same frame-time arguments and one direct frame-loop caller at 0x176151. Both the
20-byte function prologue and five-byte caller are now in the supported engine profile. The
runtime rewrites only that call, calls the untouched original first, then drains every bound VM.
A build mismatch is refused before mutation.

Northstar.PS4's `ps4_async_calls.nut` registration and file were removed. The opt-in AI harness
now has an `http` acceptance action which starts `NSHttpGet` and cannot reply until its deferred
callback runs. Live validation on shadPS4 `4cbd23ef` used PRX SHA256
`c737197167529eb00a5daf04fbd8bb0fcc042d9477f4fdf7fcc88596633c202e`:

- the hook installed at global log line 3676660;
- native UI, SERVER and CLIENT drains ran on `MainThrd` at lines 3709978, 3745193 and 3746638;
- `https://example.com/` returned `success|200|713`, and the harness callback completed at
  line 3772255 without the polling script;
- a subsequent transition reached `mp_forwardbase_kodai`; replacement SERVER and CLIENT
  lifecycles completed at lines 3788553/3791679 and again at 3858805/3861925, with no fresh
  fatal, guest exception or script error. The early boot transcript is
  `work/stage2/iterations/20260930-145056/shad-new-lines.log`.

The first launch attempt failed before game code because shadPS4 rejected another corrupt
64-byte shader-cache `profile.bin`. It was moved recoverably to
`work/cache-backups/20260930-1452/profile.bin.corrupt`; the retry rebuilt it and succeeded.

## SERVER early InitScripts and typed-native second pass (2026-09-30)

PC declares mod-owned Squirrel types from each enabled mod's `InitScript` before it registers
natives whose signatures name those types. The PS4 SERVER path previously left InitScripts in
the generated manifest and skipped those natives permanently. server.prx+0x629ad0 is the exact
module-local counterpart of client.prx+0x6783f0's compile-file wrapper; its 17-byte prologue is
now gated with the rest of the SERVER profile. Each new SERVER VM now performs this order:
constants, untyped natives, enabled mods' InitScripts in priority order, optional
`InitScriptCallback`, the typed-native pass, then the existing ordered lifecycle callbacks.
When VM lifecycle hooks are installed, the runtime manifest no longer declares InitScripts in
any context, preventing a second compile and fatal type redefinitions.

The typed test exposed an earlier ABI profiling error. server.prx+0x6363c0 is not the SERVER
equivalent of client.prx's `sq_sealstructslot`; server retail natives inline that operation.
Calling 0x6363c0 crashed when the first struct result was built. SERVER `Seal` now mirrors the
retail sequence: locate the 16-byte field from the struct base slot, retain the new referenced
object, release the old field, copy it, pop and release the stack-owned reference, and reset the
popped slot. The incorrect helper gate and mapping were removed.

Live validation on shadPS4 `4cbd23ef` used experimental PRX SHA256
`e607ffef4b6bd4a4aa642c267cec82e582869bf81856cacec2cffe40d7cc5121` and a temporary
AI.Harness SERVER callback. In the lobby, the VM compiled Northstar.Client's InitScript,
registered three deferred typed natives, and successfully called `NSGetModInstallState`,
`NSGetModInformation` and `NSGetModsInformation`; the returned `ModInstallState` and
`array<ModInfo>` values were read by typed script code. After `map mp_forwardbase_kodai`, the
replacement SERVER VM repeated the compile and three-native pass and printed the same values at
global log lines 4096419-4101022 before completing its lifecycle. No new exception followed.
The temporary callback was removed, the deployed Custom RPAK manifest restored, and the test
profile resynchronized after validation.

## Deterministic late async-result rejection (2026-09-30)

The earlier generation-cancellation checks used public endpoints that completed before their
requesting VM was destroyed. AI.Harness now has an `httpretire` action that starts a request and
immediately runs the normal `ReloadMods()`/`uiscript_reset` path. The reusable
`Start-DelayedHttpFixture.ps1` endpoint holds its response for a chosen interval, making the
worker/teardown order deterministic instead of dependent on internet timing.

Live validation used release v0.2.11-alpha's PRX SHA256
`e607ffef4b6bd4a4aa642c267cec82e582869bf81856cacec2cffe40d7cc5121`, shadPS4
`4cbd23ef`, a loopback endpoint delayed by 12 seconds, and the temporary startup option
`-allowlocalhttp`. The request was queued at global line 4261562, the old UI lifecycle was
cleared at 4261654, and the worker logged `dropped async result for retired context=2
generation=1` at 4262553. The replacement UI lifecycle then completed at 4262621. No callback
was delivered to the replacement VM. `ns_startup_args.txt` was restored to
`-allowdupeaccounts` after the test.

The first launch attempt failed in shadPS4's cache deserializer before game code because the
previous forced shutdown had left a 64-byte `profile.bin`. It was moved recoverably to
`work/cache-backups/20260930-1616-async-retire/profile.bin.corrupt`; the retry rebuilt the cache
and reached the test. The retry's early boot transcript is
`work/stage2/iterations/20260930-161716/shad-new-lines.log`; the retirement evidence is in the
same appended shadPS4 log.

## Real-controller acceptance and main-menu announcements (2026-09-30)

The user tested shadPS4 prerelease revision
`94e21778155b2178b0fd5b74963b680a51899055` with a real controller. Host Options opened and
accepted both password creation and password removal. The final Dismiss dialog button was
redundant with Circle/back and has been removed. A PC join after changing the password is still
the remaining password acceptance test; creating and clearing it on the host does not by itself
prove the remote challenge.

In the same user session, the PS4 joined a Parkour server and then a different public server.
This verifies the real browser's modded join followed by another join on the newer emulator.
The latter connection eventually logged `UICodeCallback_ErrorDialog: Connection to server
timed out.` at global shadPS4 log line 4555527 and returned to the main menu. There is no crash
or evidence in that line that identifies a port defect, so it remains an unclassified network
timeout unless it reproduces with server/timing details.

The missing main-menu announcements were a port defect. `NSRequestCustomMainMenuPromos` was a
transport-unavailable adapter, `NSGetCustomMainMenuPromoData` always errored, and
`NSHasCustomMainMenuPromoData` incorrectly returned Atlas authentication state. The runtime now
performs the PC-compatible asynchronous GET of `/client/mainmenupromos`, strictly parses the
four response objects, and publishes the thirteen string/integer fields only after the worker
finishes. The two `MainMenuPromoData` instances are allocated on first use because this PRX's
DT_INIT deliberately skips the C++ global constructor table. Malformed, oversized and non-200
responses leave promo data unavailable instead of exposing a partial result.

Host parser tests were added to `Test-NorthstarProfile.ps1`; the complete Northstar profile and
Stage 2 engine profile suites passed. The live build's PRX SHA256 was
`4186f2d5ea479a9daa41425ee86004cbc79665260870066fc983ac572c4f2330`. On the same shadPS4
revision the runtime logged `main-menu promos parsed=1 bytes=599` at global line 4664517, and
the menu visibly rendered the current three-line Northstar announcement plus the Custom
shaders, Discord and Wiki spotlight cards. Evidence is
`work/stage2/iterations/20260930-170207/shad-new-lines.log` and
`work/main-menu-promos.png`.

The first announcement-test launch separately faulted in the known Custom RPAK startup path at
PRX address `0x812fa345`, before UI initialization. Its 64-byte cache was quarantined at
`work/cache-backups/20260930-promos/profile.bin.rpak-startup-crash`; retrying the same PRX with
the Custom RPAK manifest temporarily disabled reached the menu and verified announcements.
The manifest was restored after the test. This isolated startup failure is not counted as an
announcement failure.

## Duplicate-account client leaving a PS4 listen server (2026-09-30)

The password challenge is now accepted end to end: the user changed the PS4 host password in
Host Options and joined it from PC with that password. The PC and PS4 used the same exported
Atlas identity, as permitted by `ns_allow_duplicate_accounts`.

When the PC selected Leave, the PS4 followed it back to the multiplayer lobby. Entering Private
Match again on PS4 then pulled the PC into that lobby. These are two symptoms of one ownership
error. `NSIsPlayerLocalPlayer` compared each connected client's UID to the imported Atlas UID;
with a duplicate account, both the listen host and remote PC matched. Northstar.CustomServers'
`ClientCommandCallback_LeaveMatch` therefore took its local-player branch for the PC and called
`GameRules_EndMatch`, returning the entire server to `mp_lobby` without disconnecting the PC.
That still-connected PC naturally followed the host's subsequent private-lobby state.

PS4 has no dedicated-server mode in this port, and its listen host owns client slot zero.
`NSIsPlayerLocalPlayer` now resolves the entity to its validated engine client index and returns
true only for slot zero. UID remains the player's account identity but no longer determines
connection ownership. The remaining acceptance test is to repeat the same-account PC Leave
action: the server should run the remote-player persistence/leave path, disconnect only the PC,
and leave the PS4 host in its private lobby.

The follow-up command-path audit found no generic remote-to-local console bridge:

- client-to-server command strings enter `CodeCallback_ClientCommand`, which invokes only names
  registered through `AddClientCommandCallback`; unknown names return false rather than reaching
  the engine command buffer;
- server-to-client string commands dispatch only callbacks registered through
  `AddServerToClientStringCommandCallback`;
- the PS4-only `NSPS4_ClientCommand` UI helper is called by native code only with either a
  validated `connect <IPv4>:<port>` string or the fixed `map mp_lobby` string;
- `ExecuteEngineCommand` and the file-backed console drain are compile-time disabled; and
- `ns_start_reauth_and_leave_to_lobby` is deliberately `FCVAR_SERVER_CAN_EXECUTE`, matching the
  Northstar flow where a server tells that specific departing client to build its local lobby.

Thus a server can issue the intended leave command to its client, as on PC Northstar, but the
port adds no path for another client to execute arbitrary ConCommands on the PS4 host. The live
log likewise contains no `[NorthstarPS4] console command:` event; the repeated unknown client
commands shown there are rejected by the callback dispatcher. The observed cross-machine lobby
control remains attributable to duplicate-UID local-player misclassification pending the retest.

The user repeated the same-account PC departure with experimental PRX SHA256
`c825f89aa8f54c6fef60c5a8e41132202f089c562b7251ed6100e5cf9e10a575` and confirmed the
problem is fixed. The PC no longer makes the PS4 host follow it back to multiplayer, and the
subsequent Private Match transition no longer pulls that departed PC back into the lobby. This
closes the duplicate-account local-player ownership regression; wrong-password refusal remains
a separate browser authentication test.

## Duplicate-account persistence ownership (2026-09-30)

The same accepted session exposed a second UID-versus-connection ownership error. For both PC
joins, Atlas supplied a 56,306-byte save for the remote connection, but the connect-time search
selected client #0 because it returned the first slot whose connect UID matched. The PS4 host and
PC intentionally used the same exported Atlas identity, so client #0 matched before the newly
created client #1. The log then showed client #0 installed as `READY_REMOTE` at signon 8 and
client #1 falling back to `READY_INSECURE` (lines 4872106/4872431 and
5046495-5046501/5046866). This could overwrite the host buffer with the PC connection's fetched
save and denied the remote connection its authenticated persistence.

Authentication records now retain their connection scope. `auth_with_self` records may install
only into the listen host's slot zero; records created by Atlas `/server/connect` may install only
into slots above zero. Among several remote slots with the same UID, an unowned matching slot is
chosen before an already-owned slot, while the second pass still supports a connect retry. The
late-install queue carries the same scope and receives the actual client index, so it cannot move
a remote record into slot zero merely because the UID matches.

`northstar_ps4/persistence_owner.h` keeps that policy independent of engine memory and
`tests/persistence_owner.cpp` covers a slot-zero host plus two same-UID remote clients, retry,
fake-player and missing-account cases. The complete Northstar profile suite (including this new
test) and Stage 2 engine-profile suite pass. Supported experimental PRX SHA256
`0b4d1a7d9cd6966ad835d967eaf6c37ecad75352cf3934eb95a081c663f3a928` was deployed with a
recoverable backup. It reached `UI lifecycle completed` at log line 5087122 with no later fatal
marker before the smoke test was stopped. Live acceptance still requires another same-account PC
join: the expected marker is client #1 installed from Atlas at connect as `READY_REMOTE`
(`remote`), with no new remote installation into client #0.

## Four-VM SERVER lifecycle acceptance (2026-09-30)

The remaining G03 transition run used experimental PRX
`0b4d1a7d9cd6966ad835d967eaf6c37ecad75352cf3934eb95a081c663f3a928` and the harness to
load `mp_lobby`, `mp_forwardbase_kodai`, `mp_colony02`, then `mp_lobby` again. The exported
Atlas identity had expired, so normal `launch` reported `Local authentication timed out`; the
test deliberately used the harness's explicit local `map` command instead. This validates VM
lifecycle and map replacement only, not authentication.

The fresh-session range begins with the first SERVER VM at log line 5188280. Its compact
sequence is:

- initialize 5188280, lifecycle complete 5192971, teardown clear 5201819;
- initialize 5211548, lifecycle complete 5216401, teardown clear 5231109;
- initialize 5257018, lifecycle complete 5261410, teardown clear 5272601; and
- initialize 5272751, lifecycle complete 5275256.

The final VM remained live until the emulator was stopped, so three clears for four VMs is the
expected count. In that range there are four `SERVER Before: Progression_Init` and four
`SERVER After: PS4HostOptions_Init` markers—one representative callback from each phase per
VM—and four distinct owner addresses. There are zero `SCRIPT ERROR`, `FatalError` or
`Unhandled Exception` markers. Harness status confirmed each destination level before the
next transition. This completes the G03 multi-transition acceptance; later native argument and
return-shape coverage remains G06 work.

## Same-account remote persistence accepted live (2026-09-30)

After refreshing the exported Atlas identity, the user connected the same-account PC to the PS4
host with experimental PRX
`0b4d1a7d9cd6966ad835d967eaf6c37ecad75352cf3934eb95a081c663f3a928`.
The remote connection's 56,306-byte save installed into client #1 at signon 2 and was explicitly
logged as `READY_REMOTE, remote` at lines 5391759 and 5391762. There was no remote installation
into client #0. Client #0 had already followed its separate insecure host path at line 5357476,
so account identity no longer collapsed the two connection owners.

The repeated connect first disconnected an unchanged client #1 save, then reinstalled it into
the same remote slot. Leaving invoked the correct client #1 early-write path at line 5393347 and
the final disconnect path at 5393961; both reported the save unchanged and correctly skipped an
Atlas POST. No script, fatal or unhandled-exception marker appears in the fresh-session range.
This accepts the same-account ownership fix and independently proves early leave targets the
remote save. A changed-save early leave and an Atlas-unreachable write remain G05 tests.

## Remaining P0 contract fixes and all-context ABI acceptance (2026-09-30)

Two source audits found concrete contract bugs. `GetUserInfoKVAsset_Internal` read its third
argument with the string-tag helper even though PC calls `getasset`; Titanfall's `OT_STRING`
(`0x08000010`) and `OT_ASSET` (`0x08000400`) objects share the `+0x30` character layout but are
not interchangeable. The runtime now uses a distinct asset getter, and portable tag tests reject
cross-typed and null values.

Persistence writes previously treated any completed HTTP transport as "sent", including a
non-2xx Atlas response, and advanced the clean snapshot before the worker returned. The runtime
now keeps acknowledged and queued snapshots separately. Only a 2xx response advances the
acknowledged baseline; a transport or HTTP failure clears the in-flight marker while leaving the
save dirty; duplicate early-leave/disconnect submissions are coalesced; and an older completion
cannot overwrite a newer queued snapshot. `tests/persistence_write_state.cpp` covers successful,
failed, duplicate and out-of-order completion. A live network-failure run is still required.

Supported test PRX SHA256
`acb514b0a7ddfd9720dad276fdfc470067c956c35d0b2b09d1f1972ea96ffc82` was built, passed the
complete profile suite and engine preimage suite, and was installed with a recoverable backup.
It reached the UI in 36.9 seconds, authenticated into `mp_lobby`, and loaded
`mp_forwardbase_kodai`. An isolated one-script fixture ran after lifecycle creation in all three
VMs. Its accepted markers are log lines 5681394 (UI), 5702566 and 5721537 (SERVER generations),
and 5704166 and 5724737 (CLIENT generations). Every marker returned
`$"ui/menu/common/button"`, a nine-element `array<string>` mod list and `error=true` after
catching a deliberately malformed fatal DecodeJSON call. The earlier fixture draft's line
5644023 compile failure belongs to the discarded `#elif` syntax run before this clean boot. No
script, fatal or unhandled error occurs in the accepted range. The fixture was removed after the
test, completing G06.

`Start-NorthstarSession.ps1` now records the installed runtime and game-module hashes, emulator
revision, mod metadata and enabled state, enabled-settings hash, runtime persistence-schema hash,
and first error per VM. It archives the preceding and current logs with Atlas token/password JSON
and query parameters redacted. One complete repeated transition should still be captured through
this upgraded recorder to close G01 acceptance.

## Oversized KeyValues merge verified; documentation reconciled (2026-10-01)

The secondary `Size` hook (`filesystem_stdio.prx` +0xe1e0, see the KeyValues section) was
checked with a merge larger than retail. A throwaway mod added 701 variables to the playlist
`defaults`: `keyvalues built playlists_v2.txt patches=3 original=360403 merged=390589`, and
`keyvalues size playlists_v2.txt = 390589`. The harness `playlistvar` action read the last
added variable (`4242`), one in the middle, vanilla `tdm` `scorelimit` (75) and Northstar's
`fw` `max_players` (16). Earlier merges were all smaller than their originals, so this is the
first live proof that the size cap is gone. Run on shadPS4 `94e21778`, whose pipeline cache the
user now uses. Launching `2b5666b3` against that cache fails at start with
`serdes.h:111 Read: Assertion Failed!`, so test sessions should launch the build that made the
current cache.

Documentation reconciled against the code:
- GOALS' "Current state" and "Known limits" said mod RPaks were off and untested in game. That
  was out of date: the loader runs in normal builds (`kModRpakLoadingEnabled = true`), and only
  profile copies converted with `-ConvertRpaksForPs4` are loaded. A PC-layout pak is refused at
  runtime. The deployed test profile still had PC-layout Northstar.Custom paks, so they were being
  refused until it was converted.
- INSTALL, GOALS and the build manifest text said to stay on `2b5666b3` "until #5124 is fixed
  upstream". It was fixed by #5133 (`f6cd16e8`), and `4cbd23ef`/`94e21778` have run real
  sessions. `2b5666b3` stays documented until longer play (G25), and the text now says why.
- Older dated sections here (for example "rpaks are disabled" in the 2026-09-27 custom map
  section) are left as history. This file is chronological, and later entries correct earlier
  ones.

## Thunderstore packages, ConCommands, bans and controller menus (2026-10-01)

**Thunderstore test set.** Four small packages the user approved were downloaded to
`work/thunderstore-test` (outside Git):
- smooshie CAR UwU and Volt UwU: texture-only v7 paks, 7 textures each, 2048x2048,
  formats 1, 6, 8 and 13, every mip in the rpak. `Convert-NorthstarModRpaks.ps1` converted
  both (7/7 textures). Both skins rendered in first person (`give mp_weapon_car`,
  `give mp_weapon_hemlok_smg` under `sv_cheats` in a hosted match) and on the lobby's pilot
  model.
- Rwyn's Kraber reload pack: three `audio/*.json`, 16-bit PCM WAVs. All three events were
  replaced (`ns_print_played_sounds 1`); `slideback` is "random" and drew 2, 2, 1.
- S2.SpeedometerV2: `CLIENT || UI` script with integer ConVar `Flags` and Mod Settings entries.
  It draws its readout (with `s2_speedometer_fade 0` while standing still).

**Mod ConVar pool.** Mod ConVars were constructed into a static pool of 32 objects shared by
all mods. Northstar.Client, Northstar.Custom and Northstar.CustomServers already declare 30, so
S2.SpeedometerV2 lost six of its eight and its scripts could not find them. Each ConVar now
gets its own permanent allocation. Flags are still registered as 0, as before; see G21.

**Rpak discovery crash.** `ReadRpakTexturePlatforms` read each mod pak whole (limit 64 MB) to
count PC and PS4 textures. With the two skin packs installed, one boot aborted right after
`mod rpak discovered: .../CAR UwU.rpak`: the 22 MB `std::vector` allocation failed, and with
no exceptions that is an abort. `InspectRpakTexturePlatformsWith` now takes a reader. It reads
the tables once and one byte (+0x1c) per texture header; the buffer version for host tests
wraps it.

**Mod ConCommands** (PC `modmanager.cpp` ModConCommandCallback, `mod_concommands.h`).
- Registration uses the engine's ConCommand constructor, as the native commands do.
- Running one finds its Function in the UI, CLIENT or SERVER VM (owners' VM at +8). The
  function is called with only the root table when the command has no arguments, and with an
  `array<string>` of the arguments otherwise, as PC does.
- String flags are parsed with PC's `g_PrintCommandFlags` names, including its quirks.
- A throwaway mod showed the UI command with no and with quoted arguments, and the CLIENT
  command in the lobby and in a match. The SERVER command, flagged `CHEAT`, was refused until
  `sv_cheats 1`.

**Bans** (PC `bansystem.cpp`, `banlist.h`). The uid is checked where PC checks it:
- The connectionless hook refuses an `'A'` connect packet from a banned uid. It calls the
  engine's `RejectConnection`, engine+0xe4850. That function ignores `rdi` and takes the socket
  (server+0xc), the packet (whose first field is the sender's address) and a format string;
  all seven callers inside ProcessConnectionlessPacket pass exactly that.
- An Atlas connect request for a banned uid is answered with
  `reject=Banned from this server.` before any pdata is fetched.

The host's `platform_user_id` is always allowed. The client name for `ban <name>` is at slot
+0x266: the PS4 slot holds PC's CBaseClient at +0x250, and the known offsets match PC's (UID
0xf500, signon 0x2a0), so PC's m_Name at 0x16 lands there. `ban` confirmed the host's name
there. m_UID is empty until scripts ask for it, so `ban` falls back to the connect uid.

Verified with an encrypted connect probe. Titanfall 2 packets are AES-128-GCM with a fixed key
(Atlas `pkg/nspkt/r2crypto.go`): nonce(12) | tag(16) | ciphertext, AAD 01..10. A plain packet
never reaches the hook.
- The banned uid got `Banned From Server.` back.
- An unbanned uid got the engine's normal protocol refusal, so it passed the ban check.
- Packets from loopback are ignored by the engine.

The unban date comes from `localtime`, which reads as UTC here.

**Controller audit** (G22), with scripted pad input on shadPS4 `94e21778`.

Opening menus through the harness `menu` action gave false results. Custom Match Settings
opened empty, and the server browser raised `sh_lobby.gnut #213: The index "" does not exist`
from private-match state, so every menu was re-checked through its normal path.
- Custom Match Settings: categories and enum toggles worked. Number settings are text boxes:
  Cross on the row moved focus into the box and nothing could be typed.
  `ps4_text_entry_keyboard.nut` opens the system keyboard when such a box gains focus. Done
  returns focus to the row, so Northstar's lose-focus handler sends the override. The SERVER
  script logged `override the setting scorelimit to 755`.
- Server browser: list, scrolling, filters (reached through the column tabs, as on PC) and
  Search (native keyboard) worked as shipped.
- Mod Settings and the Mods list: nothing could be reached with the pad. Focus sat in Search
  (Mod Settings) or nowhere (Mods). PS4 follows only explicit nav links, the rows' links do not
  reach them, and both lists scroll only with a mouse wheel. S2.SpeedometerV2's entries sat
  below the first page and could not be reached at all.

  Generated overrides (`scripts/menus/build_mod_settings_override.py`,
  `build_mod_list_override.py`) add row-to-row d-pad movement that skips headers, scrolling at
  the edges, and L1/R1 paging. The Mods list also focuses its first mod on open.

  Text boxes needed nothing extra: on PS4, Cross on a focused text box opens the system
  keyboard natively. A script keyboard opened on top is refused with `0x80bc0001` (busy). The
  value applies when focus leaves the box, as on PC.
- Mod Settings' reset marker `vgui/reset` is a PC `.vtf` with a VTF 7.5 header. Retail PS4
  `.vtf` files in the VPKs are raw data with no header, so it drew as a magenta checkerboard.
  The `mod_setting.res` override uses `vgui/hud/white` as a small dot.

**Test harness notes.**
- The `quit` console command does not close the game. Test scripts now `disconnect` first,
  then stop the process, so a match is not killed mid pipeline-cache write.
- A request left pending in the harness mailbox makes the next `Send-AIHarnessCommand` throw.
  Clear `request.json` and `claimed.json` before a new boot.
- `Send-PadInput.ps1` now has `square` (`v`) and `triangle` (`c`).

## Client string commands, recorded sessions and boot faults (2026-10-01)

**Client string commands** (PC `exploitfixes.cpp` CGameClient__ExecuteStringCommand,
`ns_limits.cpp`, `misccommands.cpp`; `runtime_string_commands.inl`). PS4's
`CGameClient::ExecuteStringCommand(this, cmd)` is engine+0xd8920. Its two callers are patched
(rel32 call sites, preimage-gated): ProcessStringCmd at 0xd8908 (vtable slot at 0x3acd08) and
the `this - 8` thunk at 0xd8d6c. The guard does, in PC's order:
- logs each command when `ns_should_log_all_clientcommands` is 1;
- counts commands per client slot per second against `sv_quota_stringcmdspersecond` (60, `-1`
  off) and disconnects with "Sent too many stringcmd commands";
- refuses a remote client's ConCommand that lacks FCVAR_GAMEDLL_FOR_REMOTE_CLIENTS (1 << 10);
- with `sv_cheats 0`, blocks `emit`, `pre_go_to_hub`, `pre_go_to_calibration`, `end_movie` and
  `load_recent_checkpoint`.

The remote-client rule needs the flag set where PC sets it. The engine's client commands are a
17-entry string table at engine+0x3ace80 (status, pause, recheck, migrateme,
server_single_frame, setpause, unpause, ping, rpt_* and ss_*), which ExecuteStringCommand walks
at 0xd8bae. On the first command, the flag is added to those and to PC's cheat list (`give`,
`ent_*`, `particle_*` and so on), and removed from migrateme (also SERVER_CAN_EXECUTE), recheck,
rpt_client_enable and rpt_password. 29 commands were marked and 4 cleared.

The check uses ICvar::FindCommand (slot 18), as PC does, so a ConVar line such as
`save_enable 0`, which clients send on map load, is not refused. FindCommandBase is slot 14 and
ConCommandBase has its name at +0x18 and flags at +0x28, as on PC. The rule turns on only if both
lookups return `ban` (flags 0x4, FCVAR_GAMEDLL) and FindCommand skips the ConVar `hostport`.
The host is client slot 0, or the slot whose connect uid is the local `platform_user_id`; it is
exempt.

Tested in a hosted Kodai match:
- the host's own commands passed;
- a throwaway build that treated the host as remote refused nothing in a lobby-to-match run;
- that build refused `cmd unban 1` (forwarded to the server), while `cmd status` and
  `cmd save_enable 0` passed.
A real remote client has not run it yet.

**Recorded session.** `Start-NorthstarSession.ps1` recorded three lobby -> Kodai -> Colony ->
lobby cycles with the Thunderstore test mods, 59 minutes, PRX 029b99d4:
`work/stage2/sessions/20261001-014902-repeat-transitions-mods2`. No UI or SERVER errors. The first CLIENT error was
`Attempted to call Show on invalid instance` from `ps4_chat_hud.nut`: the HUD is rebuilt during
a match (here on entering spectator), which invalidates the panel handle, and a script error ends
the match for that client. The panel is now looked up again whenever a call on it fails. Chat
still showed afterwards. The manifest's `EmulatorRevision` was empty: the recorder expected
`Revision:` or `Revision=`, while shadPS4 logs `Run: Revision <hash>`. The pattern now accepts
both.

**Intermittent boot faults** (shadPS4 prerelease `94e21778`). Neither has been tied to a
runtime change.
- Hang: the log stops mid-line while the game sits on the Respawn logo with the spinner turning
  and the process using CPU. It stopped during mod localisation, or after `UI VM probe timed
  out`. It happened on 2026-09-29 (`shad_log.txt.hang2`, before the string command guard
  existed) and twice on 2026-10-01 (`.hang3`).
- Crash: `Unhandled Exception code 0xc0000005 at 0x700000782c41` on a game thread (Thread5, then
  Thread4), each time straight after Northstar.Custom's
  `paks/northstarEventModels.rpak` loads (`mod rpak load: ... handle=4`). This was in the first recorded session
  and in one boot on 2026-10-01.

A retry booted normally each time. Next: boot repeatedly with Northstar.Custom's paks disabled,
then enabled, to see whether the crash follows mod-rpak loading.

## Password prompt on a pad; user checks (2026-10-01)

The user confirmed on a real pad that Mod Settings and Custom Match Settings work, and heard
the Kraber sound pack's replaced reload sounds. They could not reach Connect on the join
password prompt (`connect_password.menu`, Northstar.Client): the password box and the button
have no navigation links, and PS4 focus moves only along explicit links. The Northstar.PS4
KeyValues patch `keyvalues/resource/ui/menus/connect_password.menu` adds `navDown`/`navUp`
between them (root key `mods_browse.menu`, copied from the source file).

Checked with scripted input:
- the box has focus on open, down reaches Connect and up returns;
- Cross opens the system keyboard; three letters and Done showed `***`, then down reached
  Connect.
Connect itself was not pressed, as no server was selected. Closing the keyboard with Circle
from the scripted input also backed out of the prompt; that has not been checked with a real
pad.

**Slow game drive.** The game folder is on D:, a 1 TB laptop hard disk (HGST
HTS721010A9E630). From 13:01 that day the R5Reloaded launcher was reading about 22 MB/s from it,
with the disk at 354% busy and queue length 4. Cold small-file reads took about 115 ms each,
and `Sync-NorthstarProfile.ps1` hashes every source and destination file, so a sync did not
finish in 20 minutes; the single changed file was copied by hand instead. One boot hang in this
period fits that, but the earlier hangs on the same day were not measured.

## Boot hangs and crashes traced to runtime races (2026-10-01)

This corrects the entry above that called the boot hang and the `0x700000782c41` crash
emulator faults not tied to the runtime, and its note that one hang fitted the slow drive.
With the drive idle, 2 of 6 boots still hung. With the two skin packs disabled, 8 of 8 boots
reached the lobby; with them enabled, 4 of 9. All four causes were in the runtime.
Each test booted repeatedly with `Invoke-Stage2Iteration.ps1 -SkipBuild -SkipDeploy` (success
pattern `menu_LobbyMenu menu opened`, 240 s timeout), recording lobby, hang or crash.

1. **Mod localisation from the tracker thread.** `ProbeLocaliseInterface` called
   `CLocalize::AddFile` from the module tracker while the engine's main thread loaded its own
   files. PC adds mod files right after `CEngineVGui::Init`. The PS4 engine loads its stock
   files at engine+0x1cd4af..0x1cd518 through the CLocalize pointer at engine+0x51e9bf0 (vtable
   slot 9). The last load (`r1_%language%_lv.txt`, behind the flag at engine+0x3eef284) is a
   33-byte block with no jumps into it. `runtime_localise_boot.inl` replaces it with a call that
   makes the same load and then adds the mods' files. The patch goes in as soon as the tracker
   sees the engine, and the log shows `mod localisation added (vgui init)` in every boot. If it
   lands too late, the first host frame adds them. The tracker now only checks that the engine's
   CLocalize is the profiled singleton.
2. **musl malloc never locked.** The PRX links OpenOrbis's musl. Its malloc, free and `__lock`
   lock only when `__libc.threads_minus_1` (`__libc+0xc`) is non-zero, and only musl's own
   `pthread_create` raises it. This module's threads come from `scePthreadCreate` and its hooks
   run on game threads, so the heap was unprotected; one boot crashed inside `malloc` (+0x615, a
   free-list unlink). `Initialize` sets the field to 1. OpenOrbis builds `__wait` as a bare
   `ret`, so the lock is a spinlock. stdio is unaffected: `FILE::lock` stays -1 without musl's
   `pthread_create`.
3. **Mod paks loaded from the tracker thread.** The rpak hook usually installs after the engine
   has requested common.rpak, so a catch-up loop on the tracker loaded overdue mod paks itself,
   and `Preload` paks loaded inside the engine's next request. Crashes at `0x700000782c41` (a
   shadPS4 host address) hit the tracker or the game's pak thread right after such a load. Now
   every mod pak loads in the hooked `LoadPakAsync`, after the engine's own request returns, as
   PC loads Postload paks. The pak table (engine state 7 = loaded) says which are overdue.
   `Preload` paks wait for common.rpak (a PS4 difference). All four mod paks now load on
   Thread4, the game's pak thread.
4. **`CollectModNames` shared static buffers** (enabled settings and `mod.json`). It is called
   from the tracker, the main thread (localisation, VM creation) and UI natives. After change 1
   one boot crashed in `memcmp` during the tracker's scan. It now takes a spinlock.

Results, boots to the lobby with both skin packs enabled: 4 of 9 before; 7 of 8 after 1-3 with
preload deferred but tracker loading kept; 7 of 8 again; 11 of 12 after change 3 (one `memcmp`
crash); 12 of 12 after change 4. The CAR skin rendered in a Kodai match and the lobby showed the
mods' localised footer text.

The mid-line cut in hung logs is the emulator's unflushed log buffer, not the hang point.
`LogFormat` uses `sceKernelDebugOutText`, not stdio.

## Netmessage fixes, connectionless limits and GetEntByIndex (2026-10-01)

Ports from PC `shared/exploit_fixes/exploitfixes.cpp` and `ns_limits.cpp`
(`runtime_netmessage_fixes.inl`, `runtime_atlas_server.inl`).

**Finding the messages.** The console engine has its netmessage names but not PC's debug
strings. Each message's GetName (`lea rax, [rip+name]; ret`) is referenced by exactly one vtable,
found through the module's RELATIVE relocations. The layout is Source's: slot 4 Process, 5
ReadFromBuffer, 6 WriteToBuffer, 11 GetName. Slot 5 of clc_Move parses its fields and slot 4
forwards to the handler at +0x18. PC's `Base_CmdKeyValues` is shared by `clc_CmdKeyValues` and
`svc_CmdKeyValues`. Slots are replaced in the engine's read-only data (mprotect 3, then back to 1)
only when every GetName and every target slot holds the expected function.

| message | vtable | hooked | PC behaviour |
|---|---|---|---|
| clc_Screenshot | 0x3b0938 | read 0x1b86b0, write 0x1b8620 | both refused |
| clc_CmdKeyValues | 0x3af508 | read 0x1ae8d0 | refused |
| svc_CmdKeyValues | 0x3af590 | read 0x1ae960 | refused |
| net_SetConVar | 0x3afb68 | process 0x2eef30 | count, terminators, REPLICATED |
| clc_Move | 0x3afe10 | process 0x2ef130 | counts and length |

The message layouts match PC's:
- net_SetConVar (reader engine+0x1b4690): entries at +0x20 (two 0x104-byte strings, 0x208 per
  entry), count at +0x38, an 8-bit count on the wire.
- clc_Move (engine+0x1ac460): new commands at +0x24, backup commands at +0x20, length at +0x28.

PC tells the server's copy of net_SetConVar from the client's by thread. The PS4 listen server
and client share threads, so a message whose handler lies in the engine's client array is the
server's. ConVar name and flags are read at +0x18/+0x28 only after FindVar("hostport") is checked
to return the same object as FindCommandBase.

In hosted Kodai matches the clc_Move and server-side net_SetConVar hooks ran (logged once each)
and blocked nothing. The client side of net_SetConVar runs only when joining another server and
has not been exercised yet.

**Connectionless limits** (PC `CheckConnectionlessPacketLimits`). At most `sv_querylimit_per_sec`
(15) connectionless packets a second are accepted from one address, then it gets a minute of
silence. Atlas's `T` packets are exempt, and `N` packets too while `net_data_block_enabled` is on.

The packet starts with its sender's netadr_t:
- On PS4 it is Source's `{int type; uint8 ip[4]; uint16 port}` with NA_IP = 3 (NA_BROADCAST is
  still 2), not PC's `{type, ip[16], port}` with NA_IP = 2.
- The bytes after the port change between packets, so the key is the 4-byte address.
- A steady stream of type 1 (loopback) packets comes from the host's own client. As on PC,
  loopback is not limited.

Test with 20 encrypted connect requests from this machine:
- 14 were answered and the 15th tripped the limit.
- The address was still silent 5 s later and was answered again after a minute.
- A hosted match still loaded.

**GetEntByIndex** (PC hooks server.dll's GetEntByIndex at 0x4000). The PS4 SERVER native
(server+0x71b150) raises a script error for a negative index but indexes the 0x4000-entry entity
list at server+0xfc27a0 with no upper bound. From a SERVER client-command callback, index 20000
happened to read a null slot. Index 16384.5 read a garbage entity and crashed the game with a call
to 0x0.

The native's ScriptFunctionBinding (server+0xf280c8) is refilled on every SERVER VM creation by
server+0x1d5600, so writing the binding did not stick. Instead the `lea rdi, [rip+native]` at
server+0x1d574e is repointed at the guard once server.prx is mapped. For an index past the list,
the guard pushes null (`uiapi::Null`) and returns 1 without calling the native, matching PC.
Afterwards index 1 returned the player, 20000 and 16384.5 returned null, and the match continued.

**Not ported, and why:**
- CL_CopyExistingEntity: the PS4 engine inlined it into its caller, and CL_CopyNewEntity already
  checks MAX_EDICTS.
- CNetChan::ProcessMessages time limits: no strings left to find it by; PC only warns by
  default.
- Also still open: the WriteBaselines overflow, NET_ReceiveDatagram, LZSS and UTF-8 parsing, Cbuf
  execution markers and IsRespawnMod.

**Analysis tools** (scratchpad, not committed): `msgvt.py` prints a message's vtable from its
name using the RELATIVE relocations; `xref.py` finds strings, rip-relative references and
disassembly.

**Stability.** The build with all three fixes booted to the lobby 12 times out of 12. While these
fixes were in development, one boot crashed in `malloc` (+0x615, a free-list unlink) during the
module tracker's scan of the mod folders. That is the same signature as before the malloc lock
was turned on (one in roughly 20 boots since). With malloc locked, this points to a heap
corruption detected at that point rather than two threads allocating at once. The cause is not
yet found; all such crashes so far happened during `opendir`/`readdir` scans on the tracker.

## Usercmd checks (2026-10-01)

PC hooks server.dll's ReadUsercmd and checks each command after it is read:
- non-finite world-view, attack, local-view, camera position, camera angle and move vectors are
  zeroed;
- a command with bogus timing (frameTime <= 0, tick_count 0 or command_time <= 0) has its
  angles, movement, camera, buttons and melee target cleared.

**Where the hook goes on PS4.** ReadUsercmd is inlined into CPlayer::ProcessUsercmds
(server+0xb5950). That function holds both "CPlayer::ProcessUsercmds: too many cmds" and
net_sv_showusercmd's "ReadUsercmd: from=%d ..." strings, and has no direct callers. It reads up
to 64 commands (stride 0x138) into a stack array: each read starts with CUserCmd::operator=
(server+0x198a60), and the array is read last to first. Afterwards it passes the whole array to
CBasePlayer::ProcessUsercmds (server+0x43e0f0, six register arguments: player, cmds, numcmds,
totalcmds, dropped, paused). That call, at server+0xbbb15, is redirected to a guard that checks
every one of the totalcmds commands (at most 64) and then calls the original. The call and the
target's prologue are checked before the patch.

**Layout.** The fields match PC's SV_CUserCmd:
- net_sv_showusercmd prints command_number, tick_count and command_time from +0/+4/+8;
- the copy function moves PC's field widths, including the single bytes at +0x18 and +0x44 and
  frameTime at +0x9c;
- offsets: world-view angles +0x0c, local-view +0x1c, attack +0x28, move +0x34, buttons +0x40,
  melee target +0x48, camera position +0x70, camera angles +0x7c.

Differences from PC:
- NaN timing counts as bogus; PC's `<= 0` comparisons let it through.
- The bogus-timing line is logged at most every 5 s, under `ns_exploitfixes_log`.

**Test.** In a hosted Kodai match, scripted left-stick input moved the player normally and
nothing was flagged. No malformed commands were sent, so the blocking path itself has not been
exercised. `Send-PadInput.ps1` gained `move_forward`, `move_back`, `move_left` and `move_right`
(the left stick's w/s/a/d) for this.

## Unsafe script functions, DX buffer natives, LZSS and the host tests (2026-10-02)

**Unsafe script functions** (PC `squirrel.cpp` StubUnsafeSQFuncs). Unless `-allowunsafesqfuncs`
is given, PC replaces six functions with a stub that logs "Blocking call to stubbed function"
and returns null: DevTextBufferWrite, DevTextBufferClear, DevTextBufferDumpToFile,
Dev_CommandLineAddParm, DevP4Checkout and DevP4Add. Otherwise a mod script could write files or
add command-line parameters.

All six are still registered by the retail client.prx (UI and CLIENT) and server.prx. As with
GetEntByIndex, each module refills a ScriptFunctionBinding on every VM creation. The native's
address is loaded with `lea rdi|rax, [rip+native]` and stored at binding+0x60. The 12 `lea`s
(6 per module, found by the scratchpad `binding_scan.py`) are checked against their bytes and
targets and pointed at the stubs:
- the client's before the UI VM exists;
- the server's when server.prx is mapped.

A throwaway mod called them from UI and SERVER scripts in a hosted match. Every call was blocked
and logged, and the scripts carried on.

**Custom DX buffer natives.** PC's `materialsystem/nscustomdxbuffer.cpp` registers four CLIENT
natives through AddFuncRegistration, which the ADD_SQFUNC inventory does not see:
NSRegisterCustomDXBufferForGUID, NSDeregisterCustomDXBufferForGUID,
NSUpdateCustomDXBufferForGUID and NSBindTextureToMaterial. They write a Direct3D 11 constant
buffer and texture slots for an RPak material; GNM has no equivalent. Unregistered, a mod calling
them fails to compile and does not load. They are now registered, do nothing, return null and
log once. A throwaway CLIENT script calling two of them compiled and ran.

**LZSS** (PC `exploitfixes_lzss.cpp`). The engine's CLZSS::SafeUncompress is at engine+0x20f190.
It is static, taking input, output and buffer size. It checks the header, the declared size and
the output bound, but not that a back-reference lies within the output written so far. Its
entry is now a jump to PC's rewrite (`northstar_ps4/lzss.h`), after its first 21 bytes are
checked. `tests/lzss.cpp` covers:
- literals, back-references and overlapping runs;
- a reference to before the output (refused);
- exact and one-past bounds;
- header checks, output overflow and a length mismatch.

**Script console commands.** PC's `script`, `script_client` and `script_ui` compile a buffer with
sq_compilebuffer and call it. The PS4 client's compile-file wrapper (0x6783f0) compiles through
0x82b160, called on an object taken from the shared state (SQVM+0x50, then +0x4218) with the
SQVM, a buffer state and a source name; 0x82bb10 or 0x82b270 then runs it. Not wired up yet.

**Host tests.** Eleven suites existed but no script ran them. `Test-NorthstarProfile.ps1` now runs
them, and all pass: atlas_server, audio_override, banlist, chat_text, host_options, http_request,
mod_concommands, mod_dependencies, pdata_convert, regex_lite, startup_args (plus the new lzss).

**Not applicable:** PC's UTF-8 parser fix guards data from Origin open invites and communities,
which the PS4 port does not use.

## Save files, mod ConVar flags and download staging (2026-10-02)

**Save files** (PC `mods/modsavefiles.cpp`).
- **Sizes.** shadPS4's `stat` reported st_size 1 for a 25-byte save. NSLoadFile read one byte
  and called it a success, NSGetFileSize was wrong and the quota undercounted. Sizes now come from
  `open` + `lseek(SEEK_END)`; `stat` is only used for the file type.
- **Load timing.** Results reach NSHandleLoadResult on a later frame, as on PC, drained per bound
  VM in the host frame. They used to wait for the next script code callback. The queue is locked,
  because SERVER scripts also run on the engine's worker threads (Thread4/5).
- **Limits.** Loads go up to the folder limit (the 1 MB cap is gone). `-maxfoldersize` (bytes) in
  ns_startup_args.txt sets the limit and MAX_FOLDER_SIZE; the user-data copy wins.
- **Paths.** SavePathSafe follows PC's weakly_canonical containment rather than rejecting `.`,
  `..` and empty segments outright. Backslashes and colons stay refused.
- **Test.** A UI probe saved through `./probe/notes.txt`, checked NSDoesFileExist and
  NSGetTotalSpaceRemaining (51,199 KB), loaded the file through `probe/../probe/notes.txt` (all 25
  bytes) and saw a missing file reported as failed.

**Mod ConVar flags.** Mod ConVars were registered with flags 0, so settings mods' values were
never saved. They now get mod.json's Flags:
- a number is taken as is (S2.SpeedometerV2: 16777232 = ARCHIVE_PLAYERPROFILE | 1 << 4);
- a string is parsed as names with PC's table;
- the catalog used to drop a numeric Flags member and now keeps its digits.

On PS4 the profile is `/savedata0/profile.cfg`; under shadPS4 it is user 1004's savedata here,
`home/1004/savedata/CUSA04013/CONFIG/profile.cfg`. The engine reads it during host init
(engine+0x10d720, from 0x114100). That is after the runtime registers mod ConVars, so saved
values apply: `s2_speedometer_fade 0` was read back as 0 after a reboot.

The engine writes the profile when `savePlayerConfig` ("Store player settings.", callback
engine+0x120d00) sets the flag at engine+0x3eefe00, which the host frame (engine+0x111fd0)
checks. PC instead writes archived ConVars at shutdown, which a PS4 game rarely gets. The Mod
Settings override therefore runs `savePlayerConfig` when the menu closes; closing it after a
change wrote the new value.

**Mod download staging.** The extractor used to delete the mod's final folder and extract into
it, so a failed or cancelled download of an installed version removed the working copy. Now:
1. The archive is extracted into `.staging`.
2. When it is complete, any existing copy moves to `.replaced`.
3. Staging is renamed into place, then the old copy is deleted.
4. If the rename fails, the old copy is restored.

A fresh install of lexi.lexire125 1.0.7 through this path matched the previous copy file for
file. shadPS4 logs the temporary `.download.zip` as unlinked but the host file stays; the next
download truncates it.

## RPak survey, materials-only paks and the heap arena (2026-10-02)

**What Thunderstore paks contain.** About 30 paks from 13 packages were read: weapon skins, the
Archon titan, an Octane knife, a reticle framework, TF1 ammo counters, a restore mod and the
Resonance Rifle. None is compressed (header flags 0). One sets bit 0 (HAS_MODULE in RePak's
naming) on a datatable-only pak. Asset types:
- txtr v8 (textures);
- matl v12 (materials);
- uimg v10 (UI image atlas; Archon's);
- dtbl v0 (datatable).
None of these samples has model assets in a pak; model mods ship .mdl files loose or in VPKs.
The converter converted every pak except the flagged one. "Compressed RPaks" are therefore not
what blocks current mods; the content types are.

**Materials-only paks.** Discovery accepted only paks with textures, so a skin whose materials
live in their own pak (the Octane knife's `ocanfh.rpak`, two matl, beside `ocanfh_preload.rpak`
with 13 textures) was refused at boot. Paks with only PS4-layout textures and/or materials are
now accepted (tests/rpaks.cpp).

**Load order.** Preload and Postload paks both become due at common.rpak on PS4, and they loaded
in name order, so `ocanfh.rpak` went in before its textures and that boot stalled. A batch now
loads its Preload paks first.

**Conversion failures.** `Convert-NorthstarModRpaks.ps1` used to abort the whole profile sync on
one pak it could not convert. It now skips that pak with a warning; the runtime refuses the
unconverted copy.

**Heap arena.** One knife-pak boot crashed right after this module's mmap returned 0x23af7c000
while a game thread logged "Unable to map 0xc000 bytes at address 0x23af7c000". The game's
allocator frees a range (sceKernelMunmap) and maps it again at the same address
(sceKernelMapNamedDirectMemory with in_addr). Under shadPS4, an mmap(0, ...) from another thread
in between can be given that range.

musl maps memory that way whenever its heap grows and for every allocation above its mmap
threshold. Anonymous mmaps tallied over 19 boots came to about 5 MB per boot, almost all from
this module's threads and hooks. Changes:
- `Initialize` maps a 64 MiB arena once;
- this module defines musl's internal `__mmap`/`__munmap`, so libc.a's are not linked;
- anonymous read/write requests are served from the arena in 16 KiB pages, zeroed as mmap memory
  is, under a spinlock;
- other requests, or a full arena, go to the real mmap.

`__expand_heap` and malloc's large-allocation path both call `__mmap` (checked in the ELF).

Results with this build: of 13 boots, the first two after deploying it timed out and the next 11
reached the lobby. The arena mapped every time and never filled, and a hosted match loads.
Whether the arena also removes the rare `malloc` crash during the mod scan needs more boots.

## Datatable and UI image paks, and the pak module flag (2026-10-02)

**UI image atlases.** Archon's `archon.rpak` holds one 2048x2048 atlas texture and one uimg.
Its uimg is version 10 with a 0x40-byte header in the same segments (head 0x40, CPU data 0x43)
as the ten uimg in the PS4 game's `ui.rpak`. Only the atlas texture is platform-specific, and
the converter already handles it. The pak loads at boot; nothing has displayed it yet.

**Datatables.** The Restore mod's `startpoints.rpak` holds one dtbl v0 asset: 7 columns, 313
rows, and a 0x20-byte header laid out like the dtbl assets in PS4 `common.rpak`. Discovery used to
need a texture or material in a pak; it now counts dtbl assets too. Its GUID is
`datatable/startpoints.rpak`, which Northstar.CustomServers also provides as a CSV. The CSV is
used, as on PC, where the datatable hook checks mod CSVs before the pak asset.

**Header bit 0 loads a code module.** That pak also sets header flag 0x1 (HAS_MODULE in RePak's
naming). With the flag allowed, the engine called `sceKernelLoadStartModule` for
`.../paks/startpoints.prx` and logged that it does not exist. The game's own `ui.rpak` carries
the same flag and loads `ui(11).prx` this way. A mod could therefore get a `.prx` of its own run
through a flagged pak. Changes:
- the converter accepts bit 0 and writes 0, since a mod pak needs no module (the datatable loads
  without one);
- the runtime still refuses any pak with header flags set, so an unconverted flagged pak is never
  loaded;
- compression (0x100) and patch paks are refused as before.

The in-game mod downloader does not run the converter. A downloaded mod's paks load only when
they are already in the PS4 layout with no header flags.

Boots: 3/3 to the lobby with both paks before the flag change, 3/3 after it, and 5/5 with the
Octane knife paks on the heap-arena build. The knife paks load preload-first.

## NET_ReceiveDatagram limit and the WriteBaselines overflow (2026-10-02)

**Receive loop.** NET_ProcessSocket (engine+0x1a6350) reads packets with NET_GetPacket
(engine+0x1a5500, NET_ReceiveDatagram inlined) until one returns null. A packet that fails to decode
returns null, just as "no data" does, so the loop stops for that frame. Examples are a compressed
packet (header -3, "LZSS") that fails to decompress, or a bad size. One such packet per frame starves
every other client. PC (from R1Delta) retries while its `net_error` is clear.

The PS4 build has no `net_error`. Its recvfrom call (engine+0x1a596e, PLT engine+0x1dd8) handles
0, EAGAIN (35) and EMSGSIZE (40) as no data, and retries other errors itself. So:
- that call goes through a wrapper that records, per thread, whether a datagram arrived. Threads
  are keyed by %fs:0, because NET_ProcessSocket takes scratch buffers from a lock-free list;
- both NET_GetPacket calls in the loop (engine+0x1a6442 and +0x1a853c) go through a guard. It
  retries only when a datagram arrived and still no packet came out, up to
  `ns_recvfrom_per_frame_limit` (default 1000, as on PC).

Test (scratchpad `lzss_flood.py`), against a private lobby:
- encrypted packets with a back-reference before the start of the output, about 7,000 a second;
- one connect request ('A') every 0.25 s;
- results: 32/32 requests answered with the limit at 1000, 1/32 at 1 (the old behaviour), and
  32/32 back at 1000.

**WriteBaselines.** PC disconnects a client whose server info overflows the string-table baselines,
rather than letting the error stop the server. On PS4, CBaseClient::SendServerInfo is inlined into
the server's per-client frame code (engine+0xe81a0, a 0x50000-byte local buffer). The index and
overflow errors share one `call Host_Error` (engine+0x10d560) at engine+0xe997d. That call now goes
to `ns_baseline_overflow_thunk` (runtime.cpp, global asm):
- it passes on `[rbp-0x50130]` of the engine's frame, the client's byte offset into the client
  array (engine+0x3818680, stride 0x2d738);
- the engine carries on as after a successful write, since both paths join at engine+0xe9982;
- the host frame then disconnects that client with PC's message.

The fix installs, but no overflow has been triggered.

## Server-sent console commands on the PS4 client (2026-10-02)

PC Northstar hooks IsRespawnMod so that `m_bRestrictServerCommands` is set. A remote server's
net_StringCmd then runs only commands flagged FCVAR_SERVER_CAN_EXECUTE. PC also ignores a command
when two more execution markers would not fit.

On PS4:
- The client's ProcessStringCmd is engine+0x155e80, slot 2 of the vtable at engine+0x3a25a0. It
  was found through the execution-marker command `[$&*,`]` (format `;%s %c %d;`); the function that
  adds markers is engine+0xef330 and their count is at engine+0x3e78fe0. Unless the command buffer
  is unrestricted, it wraps the command text in marker 'a' and marker 'b'. The command buffer is
  unrestricted when `[this+0xfac4]` is 0 or a local server is active (`[engine+0x3818688] >= 2`).
- `[this+0xfac4]` is set to 1 in the client state's constructor (engine+0x77247) and nothing
  writes 0. So a remote server is always restricted, and IsRespawnMod has no PS4 counterpart to
  fix.
- Past 0x800 markers, the add function drops the oldest marker. A dropped 'a' marker would leave the
  commands after it unrestricted. The slot now goes through `ClientStringCmd`: when
  count + 2 >= 2048 the command is ignored and the guard returns true, as on PC (false would drop
  the connection).

The hook installs (netmessage fixes 7/7) and the lobby loads. Only a remote server reaches the
restricted path, so it is untested in play.

## Script entries, pak aliases and on-request mod paks (2026-10-02)

**Script entries.** PC's Mod::ParseScripts skips a `Scripts` entry that lacks `Path` or `RunOn`,
with a warning. The runtime manifest builder used to return failure instead, and the game then
read the stock scripts.rson: every mod's scripts, Northstar's own included, were dropped. Such an
entry is now skipped with a log line. So is one whose text would break the generated rson
(a quote, newline or bracket). `RunOn` is written verbatim as PC does, so compound expressions are
evaluated by the engine. Verified with a test mod holding three bad entries: the manifest built
(11 mods, 116 scripts) and the lobby loaded.

**rpak.json Aliases and FixupPakPath.** PC's LoadPakAsync hook does two things to the game's own
requests:
- it redirects a request through the highest-priority mod's `Aliases`. Custom maps built from
  campaign levels use this, e.g. `"mp_s2s.rpak": "sp_s2s.rpak"`;
- when the game has no pak of the requested name, it loads an enabled mod's pak of that name. PC
  registers every mod pak for this, with or without a load rule, which is how a custom map ships
  its own paks (e.g. a loading-screen pak listed as `false`).

The PS4 hook (`ModLoadPakAsync`) now does both (`ResolveRpakRequest` in mod_rpaks.h,
host-tested):
- alias names must be bare `.rpak` file names;
- the game's paks directory (`/app0/r2/paks/PS4/`) is checked only when a mod has a pak of that
  name;
- discovery keeps paks with no rule, still checked for the PS4 layout, as on-request paks.

Verified live with the lobby's loading screen:
- an alias to `sp_s2s_loadscreen.rpak` made the game open that pak instead;
- an alias to a converted mod pak with no rule went through the missing-pak check to the mod's
  path, and the game's pak thread opened it.

The legacy `"<pak>": "<map regex>"` form is not supported; GitHub code search found it only in
old templates.

**Game STARPaks named by a mod pak.** A pak may stream from the game's own STARPaks; a decompressed
campaign loading screen refers to `paks\PS4\ps4_all.starpak`. Discovery used to register a
redirect for every streamed name, which would have sent every game stream of that file to a
mod folder without it. A redirect is now registered only when the mod ships the file.

**Linear textures in game paks.** The game's own loading-screen pak (`sp_s2s_loadscreen.rpak`,
decompressed) has five textures whose platform byte is 0, which discovery treats as the PC layout.
So the PS4 game does use linear textures in some places. Discovery still refuses them; a
converted mod pak is unaffected.

**Several versions of one mod.** PC's ModManager::DisableMultipleModVersions disables every copy
of a mod whose name is enabled more than once, with "has several versions enabled". Discovery now
collects the enabled mods first and leaves such copies out with the same warning. It applies only
to the active set, not to the listing that includes disabled mods. Verified with two versions of a
test mod: both were left out and the lobby loaded. Unlike PC, the Mods list reads
enabledmods.json and still shows them as enabled.
