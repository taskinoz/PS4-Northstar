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

**The engine reads a served file short if it is larger than the original.**
The first working merge produced a 372,535-byte playlist and died with
`FatalError: KeyValues Error: Error reading token in file playlists`. Byte
360,403 of the merged file - exactly the original's length - lands mid-value
inside `LocalizedStrings/lang/Tokens/PL_amped_tacticals_desc`, which is the
breadcrumb the error printed. Hooking `Size(fileName, pathID)` (primary
filesystem vtable slot 135, `filesystem_stdio.prx` + `0xde20`) did **not** fix
it: the hook installs and is never called for this file, so the size comes from
somewhere still unidentified. The merged file is therefore written without
indentation, which costs nothing and saves about 24 KB, and
`BuildKeyValuesPatch` refuses to serve any merge larger than its original,
falling back to vanilla rather than producing a fatal error. All nine patched
files fit with room to spare; `Test-NorthstarProfile.ps1` merges each one for
real and fails if that stops being true.

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