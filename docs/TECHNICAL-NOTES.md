# Native port — technical notes

This is the detailed, chronological technical log behind the native runtime port: exact hashes, virtual addresses, byte preimages, run IDs, and the reasoning behind each fix. For current status and what to do next, start at [GOALS.md](GOALS.md) instead — this file is the evidence trail, not the status tracker. Section headings below still say "Milestone N" / "Stage 1" in places; read those as historical labels (they map onto the Goals in GOALS.md) rather than an active framing.

Last verified: 2026-08-14, Titanfall 2 PS4 CUSA04013, PS4 build `R2PS4_r2dlc11_598_CL297590_2017_12_05_12_36_PM` (PC counterpart `Titanfall2_v2_0_11_0`), running under shadPS4.

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
