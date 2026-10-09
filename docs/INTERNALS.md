# Internals

How the runtime works and what is known about the PS4 game it hooks: addresses, layouts,
and the platform behaviour each feature depends on. Organised by subsystem. For building
and testing see [BUILDING.md](BUILDING.md); for installing see [INSTALL.md](INSTALL.md).

All addresses are offsets into the module named (`engine+0x…` is an offset from the start
of `engine.prx`), for one game build only:

| | |
| --- | --- |
| Game | Titanfall 2, CUSA04013, patch 1.13 |
| Build | `R2PS4_r2dlc11_598_CL297590_2017_12_05_12_36_PM` (`gameversion.txt` v2.0.11.0) |
| PC counterpart | `Titanfall2_v2_0_11_0` |
| Profile | [tests/engine-profile/CUSA04013.json](../tests/engine-profile/CUSA04013.json): module hashes and byte anchors, checked by `scripts/Test-EngineProfile.ps1` |

PC Northstar offsets are never valid on the PS4. Every PS4 counterpart was found by its
strings, its callers, or by matching code from the module that shares it (client.prx and
server.prx embed the same Squirrel; PS4 Miles is the PC Miles).

## Contents

- [Loading the runtime](#loading-the-runtime)
- [Patching rules](#patching-rules)
- [Memory and threads](#memory-and-threads)
- [Filesystem](#filesystem)
- [Mods](#mods)
- [Squirrel VMs](#squirrel-vms)
- [Scripts manifest](#scripts-manifest)
- [KeyValues](#keyvalues)
- [Localisation](#localisation)
- [RPaks](#rpaks)
- [Materials, textures and models](#materials-textures-and-models)
- [Audio, particles, datatables](#audio-particles-datatables)
- [Persistence](#persistence)
- [Atlas](#atlas)
- [Hosting](#hosting)
- [Network and exploit fixes](#network-and-exploit-fixes)
- [Console commands](#console-commands)
- [Chat](#chat)
- [Script HTTP requests and save files](#script-http-requests-and-save-files)
- [Mod downloads](#mod-downloads)
- [Menus and controllers](#menus-and-controllers)
- [Files in /data/northstar_ps4](#files-in-datanorthstar_ps4)
- [Real PS4 compared with shadPS4](#real-ps4-compared-with-shadps4)
- [shadPS4](#shadps4)
- [Token helper](#token-helper)
- [Open questions](#open-questions)

## Loading the runtime

The runtime is one PRX, `northstar_ps4.prx`, built with OpenOrbis. It changes no retail
file except, under shadPS4, one call in `eboot.bin`.

**shadPS4: eboot bootstrap.** `scripts/Enable-Bootstrap.ps1` (and the token helper's
installer, which is its Rust port) patches the decrypted retail `eboot.bin`. It reuses the
eboot's existing `sceKernelLoadStartModule` import; nothing is added to the import table.

| Item | Value |
| --- | --- |
| Retail eboot SHA-256 | `590956ab2c9251f588348a1c066ed4045ce94ffc87c25faa5872a1f92ec35824` |
| Patched eboot SHA-256 | `5a4b52ff7224cca7bb9f5f25888847d5d2bd1669af1a4f21328927932fbfd502` |
| `sceKernelLoadStartModule` NID | `wzvqT4UqKX8` (symbol 63, jump relocation 28) |
| Loader GOT / PLT | `0x42c0` / `0x14e0` |
| Startup call replaced | VA `0x128d` (file `0x528d`), calls the eboot initialiser at `0x20` |
| Bootstrap stub | VA `0x29e0` (file `0x69e0`), 38 bytes; PRX path string at `0x2a06` |

The stub calls the original initialiser, then loads `/app0/bin/ps4_retail/northstar_ps4.prx`
through the PLT entry, then returns. It sits in zero padding after the RX segment, whose
file and memory sizes are extended just enough to cover it. The script refuses any eboot
whose hash, call bytes, segment sizes or padding differ, and keeps the original as
`eboot.bin.northstar-stage2.bak`. `-Disable` restores it.

**PS4: GoldHEN plugin.** On a jailbroken PS4 the same PRX is a GoldHEN plugin, listed under
`[CUSA04013]` in `/data/GoldHEN/plugins.ini`. GoldHEN calls `plugin_load`; nothing in the
game is changed.

**Entry point.** shadPS4 reports an OpenOrbis PRX as started but never runs the
`module_start` path in `crtlib.o`; it does run the ELF's `DT_INIT`. `Build-Northstar.ps1`
rewrites `DT_INIT` to `NorthstarPs4Init` before `create-fself`. `NorthstarPs4Init` is
idempotent and also stays in `.init_array`, so a loader that does call `module_start` does
not initialise twice. Without the rewrite, shadPS4 logs the module as started and none of its
code runs.

**Linker script.** OpenOrbis 0.5.4's `link.x` places `.init_array` but does not define
`__init_array_start`/`__init_array_end`, which `crtlib.o` expects. `launcher/link.x` defines
them. Even so, under shadPS4 C++ static constructors do not run, so globals that need
construction (`std::unordered_map`, objects with non-zero defaults) are allocated on first use.
A zero-filled `std::string` or `std::vector` happens to be a valid empty one.

**Logging.** `sceKernelDebugOutText` does not interpret format strings, so `LogFormat`
formats into a 512-byte stack buffer first. Every line starts `[NorthstarPS4]`. On shadPS4
the lines go to `shad_log.txt`; on a PS4 to GoldHEN's kernel log (TCP 3232). The first line
is `[NorthstarPS4] runtime loaded`.

**Module discovery.** A tracker thread polls `sceKernelGetModuleList` /
`sceKernelGetModuleInfo` and installs each module's hooks once it appears and passes its
hash and anchor checks. Module names end in `.prx` on a PS4 and `.sprx` under shadPS4;
`ModuleNamed()` accepts both. Base addresses change every run. `server.prx` is not loaded at
boot, so its hooks are installed lazily: retried from the filesystem hook, at most one module
enumeration per 64 opens. A module that is found but fails its checks is not retried.

## Patching rules

- Every patch is gated on the exact bytes it replaces (its preimage), and on the module's
  hash where the profile records one. A mismatch logs and skips that feature.
- Hooks patch **call sites** (rel32 `call`/`jmp` displacements) or vtable slots, not function
  prologues. shadPS4 aborts on `sceKernelMprotect(…, 7)` (`address_space.cpp`, "Protect:
  Unreachable code!"), so no RWX trampoline can be made. Where a function has to be wrapped,
  its callers are redirected and the untouched original is called directly.
- Protection bits are 4 = read, 2 = write, 1 = execute. Restoring a data page to 3 makes it
  write+execute without read.
- Vtables in read-only data are made writable (protection 3), patched, then set back to 1.
  Where a vtable is shared or refilled, a copy is made and the object's pointer swapped.
- `ScriptFunctionBinding`s are refilled on every VM creation, so natives are replaced by
  repointing the `lea reg, [rip+native]` that fills them, not by writing the binding.
- On a PS4 the PRX is mapped near `0x800000000`, out of rel32 reach of the game's modules
  (`0x8xxxxxxx`). `Reachable()` (`runtime_hardware.inl`) places a 14-byte `jmp [rip]` stub in
  a page mapped within reach (searched from well below the modules, so it does not take the
  address `client.prx` loads at), or in int3 padding.
- The PS4 enforces protection per 16 KiB page. `PageSpan()` unprotects every page a write
  touches; a 5-byte patch can straddle two.

## Memory and threads

- **Heap.** The PRX links OpenOrbis's musl. musl's `malloc`, `free` and `__lock` only lock
  when `__libc.threads_minus_1` (`__libc+0xc`) is non-zero, which only musl's own
  `pthread_create` raises. This module's threads come from `scePthreadCreate` and its hooks
  run on game threads, so `Initialize` sets the field to 1. OpenOrbis builds `__wait` as a
  bare `ret`, so the lock spins. stdio is unaffected.
- **Arena.** The game's allocator unmaps a range and maps it again at the same address. Under
  shadPS4 an `mmap(0, …)` from another thread can take that range in between. The runtime
  defines musl's internal `__mmap`/`__munmap` and serves anonymous read/write requests from an
  8 MiB arena mapped once at start, in 16 KiB pages, zeroed, under a spinlock; other requests
  or a full arena go to the real `mmap`. `__expand_heap` and malloc's large path both call
  `__mmap`. Peak use in the menus is about 4.3 MiB.
- **PS4 flexible memory.** About 332 MiB is free when the runtime starts and 24–32 MiB once
  the game is hooked. A larger arena (64 MiB) made the game unload `client.prx`. By the menus a
  new thread may get no stack, so 12 stacks of 256 KiB are reserved at start and reused one
  second after their thread exits.
- **SSL.** The SSL pool is 384 KiB and the HTTP pool 256 KiB, created right after hooking.
  A 96 KiB pool cannot parse Cloudflare's ECDSA chain for `northstar.tf`; NanoSSL then fails
  with `0x809517d5` (-6101, `ERR_MEM_ALLOC_FAIL`).
- **Shared buffers.** `CollectModNames` is called from the tracker, the main thread and UI
  natives, and takes a spinlock.

## Filesystem

`filesystem_stdio.prx` exports `CreateInterface`, which returns `VFileSystem017`. The
primary vtable is PC's shifted by +2 slots. The secondary interface is at `fs+8`.

| Function | Slot | Address | Notes |
| --- | --- | --- | --- |
| `AddSearchPath` | primary 10 | | `xor eax, eax; ret` on PS4: search paths cannot be added |
| `OpenEx` | primary 76 | `+0xd4a0` | Hooked: serves mod files |
| `ReadFromCache` | primary 97 | | Hooked: answers false for a file a mod overrides, so the engine opens it |
| `MountVPK` | primary 113 | `+0xa900` | Formats `"%s.pak000"` into 0x104 bytes and lowercases it |
| (archive query) | primary 112 | `+0xa300` | Searches the mounted list; not a mount |
| `Size` | primary 135 | `+0xde20` | Hooked |
| `Size` | secondary 7 | `+0xe1e0` | `this -= 8; jmp +0xde20`, which bypasses the primary slot; hooked separately |
| `ReadFile` | secondary 14 | `+0xc3d0` | Opens internally without `OpenEx`; hooked |

**Overlay.** Mod files are served from the enabled mods' `mod/` folders, in load priority
order, in place of the game's files of the same path. A one-time index of every enabled
mod's files replaces per-request `open()` probes (failed opens per map load fall from
thousands to a few dozen, and loads are 20–40% faster). The index keeps each file's on-disk
spelling, because `/data` on a PS4 is case-sensitive and the game asks for, for example,
`resource/UI/menus/panels/mod_setting.res`.

Generated files are served through the same hooks: `scripts.rson`, KeyValues merges, the
persistence definition and the particle manifest. `Size` returns the generated length from a
cache (the platform `stat` misreports sizes), so a generated file may be larger than the
original.

**VPKs.** Enabled mods' `vpk/english*.bsp.pak000_dir.vpk` are mounted through the original
`MountVPK` after the game's own mounts, by their language-neutral stem. `vpk.json` follows
PC: absent or invalid means preload; otherwise `Preload` decides, and a non-preload archive
mounts when its stem matches the engine's mount request (how a map mod's VPK mounts with its
map). `MountVPK` lowercases its path, so on a case-sensitive PS4 the mod's `vpk/` is copied
once to `/data/northstar_ps4/runtime/vpk/<mod>`; it is skipped where the lowercase path already
resolves.

**Mod roots.** Installed mods are read from `/data/northstar_ps4/R2Northstar/mods` when it
exists, else `/app0/R2Northstar/mods` (`/app0` is read-only on a PS4). Downloaded mods are in
`/data/northstar_ps4/runtime/remote/mods`. Every mod path is built from the mod's own
directory.

## Mods

**Discovery** (`mod_catalog.h`) reads every `mod.json` with its own JSON parser.
`enabledmods.json` decides which are enabled. Two enabled copies of one mod name are both
left out, with PC's "has several versions enabled" warning. Remote (downloaded) mods start
disabled at every boot and follow `enabledmods.json` only after a reload, as PC's "Do not
load remote mods on first load".

**ConVars.** Each mod ConVar gets its own permanent allocation, constructed with the engine's
ConVar constructor. Flags come from `mod.json`: a number as is, or names parsed with PC's
table. `ARCHIVE_PLAYERPROFILE` ConVars are saved to the profile; on PS4 the engine writes it
only when `savePlayerConfig` sets the flag at `engine+0x3eefe00` (callback `engine+0x120d00`,
checked by the host frame at `engine+0x111fd0`), so the Mod Settings menu runs
`savePlayerConfig` when it closes. The profile is read during host init (`engine+0x10d720`),
after mod ConVars are registered, so saved values apply.

**ConCommands** (`mod_concommands.h`) are registered with the engine's ConCommand
constructor. Running one finds its Squirrel function in the owning VM and calls it with the
root table alone (no arguments) or with an `array<string>` of the arguments, as PC does.

**Dependency constants** (`mod_dependencies.h`). `Dependencies` and `PluginDependencies` are
read from every mod, enabled or not; the first mod to name a constant keeps it. Each VM gets
the constant as whether an enabled mod has that `Name`. Plugin constants are always false.
`ScriptConstants()` adds `VANILLA`, `NS_VERSION_*` and `MAX_FOLDER_SIZE`, and is rebuilt for
every VM so a reload's enabled set applies.

**Reload.** `NSReloadMods` saves `enabledmods.json` and calls `ReloadModState`
(`runtime_mod_reload.inl`), which rebuilds the overlay and its index as a new snapshot
behind an atomic pointer (old snapshots are kept, since engine threads may still read
them), the scripts manifest (on next open), the mod VPK list, the KeyValues patch list, new
mods' ConVars, the UI and CLIENT callback lists, and the catalog. Northstar's `ReloadMods()`
then queues `reload_localization`, `loadPlaylists`, `weapon_reparse` and `uiscript_reset`.

## Squirrel VMs

**VM creation.** client.prx creates the UI and CLIENT VMs, server.prx the SERVER VM. The
script-owner global is `client+0x1afbfb8`; its `+0x8` is the UI VM wrapper, whose context is
at `+0x8` and whose SQVM is at `+0x50`.

| | client | server |
| --- | --- | --- |
| VM initialiser | `0x6746c0` | `0x625da0` |
| Its call site (hooked) | `0x6717af` | `0x622e8b` |
| `SQString::Create` | `0x6a96a0` | `0x666600` |
| `SQTable::NewSlot` | `0x6ab3e0` | `0x668470` |
| Const-table gate | `0x6759d2` | `0x6270b2` |
| Compile-file wrapper | `0x6783f0` | `0x629ad0` |
| VM release | `0x6787a0` | `0x629e80` (calls `0x1b60dd`, `0x1b644b`, `0x70d022` hooked) |
| Compile-error handler | `0x67e7b0` | |
| Script-error routine | `0x67e5b0` (`Error()` at `+0x67e76f`) | `0x62ff60` |

**Native registration.** `client+0x67a3c0` registers a native from a 0x68-byte record whose
callback pointer is at `+0x60`. The runtime's natives (`runtime_ui_api.inl`,
`runtime_server_vm.inl`) are registered in that format.

**Helpers are per module.** Squirrel helpers use module-local globals and allocators, so
SERVER natives call server.prx's copies:

| Helper | client | server |
| --- | --- | --- |
| `sq_throwerror` | `0x682a60` | `0x634320` |
| `sq_pushstring` | `0x682e00` | `0x6346c0` |
| `sq_pushasset` | `0x682f40` | `0x634800` |
| `sq_newtable` | `0x683230` | `0x634af0` |
| `sq_newarray` | `0x683330` | `0x634bf0` |
| `sq_arrayappend` | `0x6835e0` | `0x634ea0` |
| `sq_newstruct` | `0x684970` | `0x636230` |
| function lookup | `0x685cf0` | `0x6374d0` |
| object push | `0x6875f0` | `0x638ce0` |
| `sq_call` | `0x6876c0` | `0x638db0` |

server.prx has no separate `sq_sealstructslot`; its natives inline it, and so does the
runtime's SERVER path (retain the new object, release the old field, copy, pop and release the
stack reference). `server+0x6363c0` is a different function.

**Values.** `OT_STRING` is `0x08000010` and `OT_ASSET` `0x08000400`; both keep their
characters at `+0x30` but are not interchangeable. A class instance (tag bit `0x408000`) has
its entity at `+0x40`; an entity's word at `+0x68` is its index, and client = index − 1.
A player argument is accepted only if it is the entity the server's player table holds for
that index.

**`SQTable::NewSlot`** returns "a new node was created", which is false whenever the key ends
up in an existing node, including every insert that grows the table (the retry after
`Rehash` takes the replace path). Its result is ignored. It takes its own reference to the
value. `SQString::Create` writes neither the shared-state back-pointer at `+0x18` nor a
reference, and the table's release path dereferences both at teardown, so key strings
created by the runtime are given both.

**Script output.** `print` (`client+0x6d0b30`) calls the shared state's print function at
`SQSharedState+0x4350` (`sqvm+0x50`, then `+0x4350`). The runtime installs a function there
that logs the text with its context and forwards to the previous one, so the in-game console
still gets it.

**Lifecycle.** For each new VM, in order:
1. constants;
2. untyped natives;
3. SERVER only: enabled mods' `InitScript`s in priority order, compiled through the
   module's compile-file wrapper, then the typed natives whose signatures name mod types;
4. mods' `Before` callbacks, the engine's own init, mods' `After` callbacks.

SERVER `MapSpawn` callbacks are dispatched at `server+0x70cd64` (the call to `0x62b1b0`).
When lifecycle hooks are installed, the scripts manifest declares no `InitScript`s, so none
is compiled twice. At SERVER release, mods' `ServerCallback.Destroy` functions run before
the VM is freed.

**Host frame.** PS4 inlines `CHostState::FrameUpdate`'s state handlers into
`engine+0x1334d0` (one caller, at `0x176151`). That call is hooked; after the original
returns, each bound UI, CLIENT and SERVER VM drains its queue of async results (HTTP,
save-file loads, mod-download callbacks). Each VM starts a new async generation; teardown
bumps it and clears the queue under the queue lock, and a worker enqueues only if the
generation it started under is still current.

**Compile errors.** A fatal compile error used to end in `Error()` with the main thread
stopped. The immediate `mov esi, 1` at `client+0x67e8b7` (and `server+0x630267`) is changed
to 0, which takes the run-time error path instead. `runtime_script_errors.inl` reads the
captured output, finds the file and the mod that owns it, and for a VM that has not started
yet queues `disconnect "Encountered CLIENT script compilation error in <file> (<mod>)…"`
through `Cbuf_AddText` (`engine+0x203f30`, buffer `engine+0x3e74280`, mutex
`engine+0x3e78fb0`).

**Blocked functions.** Unless `-allowunsafesqfuncs` is given, `DevTextBufferWrite`,
`DevTextBufferClear`, `DevTextBufferDumpToFile`, `Dev_CommandLineAddParm`, `DevP4Checkout`
and `DevP4Add` are replaced with a stub that logs and returns null, as on PC (12 `lea` sites,
six per module).

**`GetEntByIndex`.** The SERVER native (`server+0x71b150`) checks for a negative index but not
the upper bound of the 0x4000-entry list at `server+0xfc27a0`. Its `lea` at `server+0x1d574e`
points at a guard that returns null past the end, as on PC.

**Custom DX buffer natives.** PC's four `NS*CustomDXBuffer*`/`NSBindTextureToMaterial`
CLIENT natives write Direct3D 11 buffers. They are registered as no-ops so mods that call them
still compile.

## Scripts manifest

The runtime serves a generated `scripts.rson`: the stock manifest plus each enabled mod's
`Scripts` entries, as PC writes them. `RunOn` is written verbatim, so compound expressions
are evaluated by the engine. An entry without `Path` or `RunOn`, or whose text would break the
rson (a quote, newline or bracket), is skipped with a log line. A mod's repeated entries for
one file (for example `RunOn` UI, CLIENT and SERVER separately) are all kept; a higher-priority
mod's entry for the same path replaces them.

**Remote-function checksum.** The client drops with `#DISCONNECT_OUT_OF_SYNC` when its
remote-function table's checksum (`[r14+0x2fc]`) differs from the server's (global
`client+0x1e44b34`; compared at `0x43dea0` and `0x43e64c`; handlers registered by
`0x43e680`). The table follows from which scripts compiled and in what order, so the manifest
must match a PC Northstar server's for the same mods.

## KeyValues

Mods' `keyvalues/` patches are merged by the runtime (`keyvalues.h`), which serves one
complete file per patched path. `#base` does work in this engine for most loaders (weapons,
aisettings, menus), but not in the playlist loader, so merging is done for every file.

- Escape sequences pass through untouched. The playlist holds 2,525 `\n` and 138 `\"` in
  localised strings.
- Platform conditionals (`[$PC]`, `[!$JAPANESE && !$TCHINESE]`) attach to the entry before
  them. A patch prefers the base entry with the same conditional; an entry under a different
  conditional is added beside the original.
- Duplicate keys in a block are kept; a merge targets the first, as Valve's `FindKey` does.
- Patches are matched case-insensitively (the engine asks for `resource/UI/HudScripted_mp.res`).
- A mod's own copy of a file is the base its patch applies to.
- A patch root merges into the file's only root whatever its name, as `#base` does
  (Northstar's server_browser.menu calls its root `mods_browse.menu`).
- `KeyValuesKeepsKeys` checks a merge keeps every key of the original, occurrence by
  occurrence under the same conditional.
- `ReadFromCache` is bypassed for patched paths; otherwise the cache serves the vanilla file.

## Localisation

`CLocalize::AddFile` is `localize+0x5c60`. The PS4 version does not expand `%language%`; it
probes the stock `resource` folders and returns true without opening anything. The runtime
substitutes the system language (`sceSystemServiceParamGetInt(LANG)`, mapped to `japanese`,
`english`, `french`, `spanish`, `mspanish`, `german`, `italian`, `portuguese`, `russian`,
`polish`, `tchinese`), and uses the English file when a mod lacks that translation.

Mod files are added on the main thread, as PC adds them after `CEngineVGui::Init`. The engine
loads its stock files at `engine+0x1cd4af..0x1cd518` through the CLocalize pointer at
`engine+0x51e9bf0` (vtable slot 9). The last load (`r1_%language%_lv.txt`, behind the flag at
`engine+0x3eef284`) is a 33-byte block replaced with a call that makes the same load and then
adds the mods' files. The log shows `mod localisation added (vgui init)`. Adding them from the
tracker thread raced the engine's own loads and hung boots.

`reload_localization` (ConCommand object `engine+0x1a1d2c0`, callback `0x812e0`) is repointed:
the stock rebuild ran a hash-chain walk on another thread during the rebuild
(`localize+0x7aed`). The replacement only adds newly enabled mods' files; tokens from mods a
reload disables stay until the next boot.

## RPaks

The pak system is in `rtech_game.prx`.

| | |
| --- | --- |
| `LoadPakAsync(name, allocator, flags)` | `+0x76f0`; its two call sites `+0x78c0`, `+0x7ed1` are hooked |
| Pak table | `+0x2a66d08`: 512 entries of 0xa8 bytes (handle `+0x00`, state `+0x04`, name `+0x10`, allocator `+0x20`); state 7 = loaded |
| Worker | `+0x5340`; checks `_hotswap.starpak` at `+0x601a`, builds `/app0/r2/<path>` at `+0x6717` |
| Stream file open | the call at `+0x6773` to `+0x0a90` is hooked for mod STARPaks |

**Paths are absolute.** A relative name reaches the filesystem verbatim, so mod paks are
requested by full path.

**When mod paks load.** Every mod pak loads in the hooked `LoadPakAsync`, after the engine's
own request returns, as PC loads Postload paks. The pak table says which are due. `Preload`
paks wait for `common.rpak` (PC loads them before it), and a batch loads its Preload paks
first. Loading paks from the tracker thread crashed the game's pak thread.

**`rpak.json`.** `Preload`/`Postload` rules, `Aliases` (bare `.rpak` names; the
highest-priority mod wins) and on-request paks (a mod pak loaded when the game asks for a pak
name it does not have, checked against `/app0/r2/paks/PS4/`) follow PC's `LoadPakAsync` hook
(`ResolveRpakRequest`). The legacy `"<pak>": "<map regex>"` form is not supported.

**STARPaks.** A v7 header lists its streamed files (NUL-separated, offset `0x58`, length
`0x38`). Discovery validates them (no absolute or `..` paths) and redirects a stream open to
`<mod>/paks/<path>` only when the mod ships that file; a mod pak may also stream from the game's
own STARPaks.

**What is accepted.** Mod paks must be v7, uncompressed (`0x100` is RTech compression, not a
platform marker), not patch paks, with header flags 0, and every texture in the PS4 layout.
Paks holding only PS4-layout textures, materials (`matl` v12), UI images (`uimg` v10) or
datatables (`dtbl` v0) are accepted. Header bit 0 (HAS_MODULE in RePak's naming) makes the
engine call `sceKernelLoadStartModule` for `<pak>.prx`; the game's `ui.rpak` uses it. A mod pak
needs no module, so the converter clears it and the runtime refuses any pak with it set.

**Texture layout.** PS4 texture headers carry platform byte 8 and set usage bit 0. Each mip is
Morton-ordered in 8×8 compression-block tiles and padded to whole tiles (so the five smallest
BC1 mips take 512 bytes each), and mips are stored largest first (PC stores them smallest
first). `rpak_texture_converter.h` converts uncompressed, non-patch v7 paks: it reorders
blocks and mips, pads, updates `dataSize`, platform and usage bytes, page and slab sizes,
rebuilds permanent-mip pages, relocates pointers and descriptors, and converts streamed mips
inside their existing 4 KiB STARPak allocations, so STARPak offsets stay fixed. It refuses
anything it cannot convert exactly. `Convert-NorthstarModRpaks.ps1` writes converted copies;
a pak it cannot convert is skipped with a warning. LegionPlus exports PC originals and
converted copies to byte-identical DDS files.

Some of the game's own paks hold linear (platform 0) textures, for example
`sp_s2s_loadscreen.rpak`. Mod paks with them are still refused.

Titanfall 2 paks contain no models; model mods ship `.mdl` files loose or in VPKs. No current
tool makes compressed or patch paks, so neither is supported.

## Materials, textures and models

**Loose `.mdl`** files load through the overlay.

**Loose `.vmt`.** The material KeyValues loader (`materialsystem_ps4+0x9fb50`) asks the VPK
cache first; when the cache answers false it opened a file from disk only if the name ended in
three numeric groups (the engine's generated `___%s_%d.vmt`, checked at `+0x9fcdc`). Two
patches (`runtime_materials.inl`): the `jl` at `+0x9fce0` is removed, and the 10-byte NOP at
`+0x4f9a6` becomes `test al, al; je +0x4f9fd`, because after loading the engine looks the
material up in the cache again and takes a spinlock in the result without checking it.

**`.vtf`.** Mod textures stay in the PC layout, loose or in a mod VPK; a PS4-layout copy draws
scrambled. The VTF loader (`+0x6b140`) asks the cache, then either opens the file through
`OpenEx` or, when the texture system has already read the game's copy into a buffer,
unserializes that buffer. The cache-miss branch at `+0x6b24c` now always opens the file
(`and dword [rbx+0xb0], 0; jmp +0x6b2ca`), so a loose texture that replaces one of the game's
is used.

The PS4 VPK texture layout (for reference; the port does not repack game VPKs): image data
first, mips largest first, frame-major, Morton-tiled in 8×8-block tiles padded as in paks
(uncompressed formats in 256-byte tiles); then the PC header and resources with offsets
shifted, image offset 0 and version 7.5; then a u32 image size. `NOMIP` keeps one mip;
BGR888/RGB888 become RGBA8888 with flag 0x2000. Game VPK texture entries carry flags 0x80000.

**Models in a mod VPK** are read from it. The viewmodel's own materials were not looked up
from a mod VPK while the loose copies were; ship materials loose.

**Mod VPKs.** In Respawn VPKs the u16 after each chunk of a multi-chunk file is the archive
index, or 0xFFFF after the last. RSPNVPK wrote 0, which is only right for archive 0; build mod
VPKs with `-n 0` or a fixed RSPNVPK.

## Audio, particles, datatables

**Audio** (`audio_override.h`). Miles 10.0.10 is linked into `client.prx`.

| PC (mileswin64) | PS4 (client) | |
| --- | --- | --- |
| `0xF110` | `0x99b0` | `LoadSampleMetadata(sample, buffer, length, type)` |
| `0x294C0` | `0x22450` | plays an event; the event name is at `arg2+0x30` |

Samples keep data at `+0xe8` and length at `+0xf0`. The event function's three call sites
(`0x162c4`, `0x2bd0f`, `0x2cc35`) record the event name; `LoadSampleMetadata`'s four
(`0x9fbf`, `0xa40a`, `0x133a1`, `0x13d1c`) swap in the replacement and pass type 64, which
makes Miles detect the format. Matching rules, selection and `ns_print_played_sounds` follow
PC. Samples are read on first play and kept for the session. `std::regex` cannot be used (no
exceptions; libc++ aborts on a bad pattern), so `regex_lite.h` matches the ECMAScript subset
mods use, capped in steps.

**Particles.** `particle_manifest.h` appends enabled mods' `particles/particles_manifest.txt`
bodies to the retail manifest in priority order, written to
`/data/northstar_ps4/particles/particles_manifest.txt`. A malformed mod manifest is skipped.

**Datatables.** The engine's datatable builtins are replaced at the exact call sites that pass
the native registrar (client and server). `datatable/<name>.rpak` resolves to the
highest-priority enabled mod's `scripts/datatable/<name>.csv` (`datatable_csv.h`), and falls
back to the engine when no mod has one. Vector cells use `<x,y,z>` and are packed as the
stock `GetDataTableVector` (`client+0x77e6a0`) packs them.

**Mod pdiffs** (`pdef_diff.h`). `$ENUM_ADD` blocks and `$PROP_START` declarations from enabled
mods' `mod.pdiff` are merged into the persistence definition, as PC's `modpdef.cpp`. The merge
is refused, naming the mod, when a directive is malformed, an enum is missing, the result does
not parse, the data exceeds 56,781 bytes or the file exceeds 53,248 bytes.

## Persistence

**Layouts.** The PS4 game loads persistence version 929; PC and Atlas use 231 (56,169 bytes
of data, the first int being 231). Northstar.PS4's `persistent_player_data_version_929.pdef`
is PC 231 verbatim plus the console's black market (`bm`, 181 bytes): 56,350 bytes.
`scripts/pdef/build_ps4_pdef.py` generates it; `find_console_fields.py` shows the black market
is the only console-only data the stock scripts no mod replaces still use. The 231 range lines
up byte for byte with a PC server's.

**Buffer.** The PS4 client slot is PC's `CBaseClient` at `+0x250`, so every PC offset is
shifted by 0x250: the save buffer is at `+0x74a` (PC `0x4fa`), 56,781 bytes
(`PERSISTENCE_MAX_SIZE`); `m_UID` at `+0xf750`; `m_Name` at `+0x266`. The engine sizes data
from the definition and never checks it against the buffer; the definition file itself is
limited to 0xD000 bytes.

**Atlas pdata** is 56,306 bytes for a fresh account: the 231 structure plus trailing bytes
where PC mods' pdiff fields live. `pdata_convert.h` copies the 231 structure into the buffer
and zeroes the black market, keeping the trailing bytes aside; writing back sends the 231
structure plus those bytes.

**Install** (PC `AuthenticatePlayer`). Right after the engine handles the connect request that
carried a player's Atlas token (signon 2), the pdata is copied into the buffer and the slot set
to `READY_REMOTE` (4). Installing later, at the first persistence check, is too late: the client
has already been sent its data. Records keep their scope (`persistence_owner.h`): the host's own
`auth_with_self` save installs only into slot 0, Atlas `connect` records only into slots above
0, so two connections with one account do not swap saves. Anyone else stays `READY_INSECURE`.

**Write back** (`POST /accounts/write_persistence`, multipart `file.pdata`) on disconnect, on
`NSEarlyWritePlayerPersistenceForLeave` and at each map change. `CBaseClient::Disconnect`
(`engine+0xd75f0`) is detoured: its first 13 bytes move to a stub that keeps every argument
register including xmm0–7 (the function is variadic). A save is written only if it changed;
only a 2xx response advances the acknowledged copy (`persistence_write_state.h`).
`ns_ps4_write_remote_persistence 0` turns writes off.

**`everything_unlocked`.** PC runs `autoexec_ns_server.cfg` on every new game, which sets
`everything_unlocked 1`; the runtime sets it at startup. Without it a host checks real unlock
progress and resets loadouts.

## Atlas

The master server is `https://northstar.tf` (`+ns_masterserver_hostname <url>` in
`ns_startup_args.txt` replaces it).

**User agent.** Atlas refuses any request whose User-Agent does not start
`R2Northstar/<semver>`. The runtime sends `R2Northstar/<Northstar.Client version>+ps4
NorthstarPS4`.

**Identity.** The player's uid and token are in `/data/northstar_ps4/atlas_identity.json`
(`uid`, `playerToken`, `refreshUrl`, `refreshKey`), written by the token helper or by a
sign-in push. The token is never logged. Tokens are 32 lowercase hex, minted at
`/client/origin_auth?id=<uid>&token=<EA code>`. A token is refused once the account mints a
newer one (signing in on PC replaces it) or after the deployment's expiry (24 h by default).

**uid in the connect packet.** The engine builds the connect request at `engine+0x155516`,
parsing `platform_user_id` into a u64, then writing the name and `serverFilter`. It rewrites
`platform_user_id` from the PSN account id, or to the literal `"1"` when that is zero
(`engine+0xb87da`, `engine+0x1191b7`). A naked stub at `engine+0x155516..0x155542` substitutes
the imported uid where the packet builder picks the string; `EnsureConnectUid` also re-applies
it before each connect.

**ConVar writes.** `ConVar::SetValue(const char*)` is vtable slot 15 (slots 15–18 forward to the
parent at `this+0x38`). ConVar layout: name `+0x18`, help `+0x20`, default `+0x40`, value
`+0x48`, int value `+0x5c`. `serverFilter` is registered camelCase; `FindVar` is
case-insensitive.

**Client requests** (`runtime_server_list.inl`, `runtime_server_join.inl`):
- `GET /client/servers` → the browser, parsed by `server_list.h`;
- `POST /client/auth_with_server` → `{ip, port, authToken}`; the token goes into
  `serverfilter` and `connect ip:port` runs through the UI helper `NSPS4_ClientCommand`;
- `POST /client/auth_with_self` → the account's save for a local lobby; the token goes into
  `serverfilter`. If it fails because Atlas refused the token and no helper could renew it,
  Launch Northstar fails with the reason, as on PC;
- `GET /client/mainmenupromos` → the main menu announcements.

**Token refresh** (`atlas_refresh.h`). When Atlas answers `INVALID_MASTERSERVER_TOKEN`, the
runtime GETs `refreshUrl` with the `X-NorthstarPS4-Key: <refreshKey>` header, accepts a reply
with the same uid and a new 32-hex token, saves it and retries once. Refreshes are serialised.

**Sign-in push** (`runtime_signin.inl`). The runtime listens on TCP 37012 (`-nopcsignin` turns
it off), with non-blocking sockets (shadPS4's blocking `accept` holds a lock every socket call
needs). `GET /northstar/hello` answers `{"app":"NorthstarPS4","signedIn":…,"paired":…}`;
`paired` is true when the request's `X-NorthstarPS4-Key` matches the stored key (compared in
constant time). `POST /northstar/signin` takes `{uid, playerToken, refreshUrl, refreshKey,
code}` and is accepted from 127.x, with the 4-digit code shown in the Launch Northstar error,
or with the paired key. After 5 wrong codes only the paired key or loopback is accepted until
restart. An accepted push takes effect without a restart.

## Hosting

A PS4 or shadPS4 hosts as a listen server; the host owns client slot 0. There is no dedicated
server mode.

**Server list** (`runtime_atlas_server.inl`, PC `serverpresence.cpp`/`masterserver.cpp`).
Northstar.PS4's `ps4_server_presence.nut` reports map, playlist, max players and player count
once a second; a reporter thread sends `add_server` (with `modinfo.json` built from the loaded
mods), `update_values` every `ns_server_presence_update_rate` ms, and `remove_server` when the
SERVER VM stops reporting for 60 s or listing is turned off. Atlas reads `password` only at
creation, so a password change re-lists the server. Atlas verifies a new server with a UDP
probe to the game port; a host behind NAT without a forwarded port is refused with
`NO_GAMESERVER_RESPONSE`.

**Atlas packets.** Atlas sends signed `T` connectionless packets (HMAC-SHA256 with the
server's auth token) to the game port. The hook is on
`CBaseServer::ProcessConnectionlessPacket` (`engine+0xe4b40`, slot 2 of the vtable at
`engine+0x3acfb0`; `ConnectClient` is inlined there). Packet: raw data with the `ff ff ff ff`
header at `+0x18`, bit reader at `+0x20` (base `+0x58`), size at `+0x60`. For `sigreq1` the
signature is checked in constant time and the `connect` request is handled as on PC: the pdata
is fetched (`GET /server/connect`, 200, at most 0xDDCD bytes) and accepted.

**Admission.** A connect request (`A`) is `ff ff ff ff 'A'`, four 32-bit fields, the uid (u64
LE), the name, then strings ending with `serverfilter`. When uid and token match an accepted
Atlas connect, the player is authenticated. The engine's own serverfilter comparison (the `je`
at `engine+0xe749b`) is made a `jmp`, as PC patches it out.

**Client CRC.** `GetServerClientCRC` (`engine+0x10d180`) returns -1 on a dedicated server,
otherwise a CRC of `client.prx`, which no PC can match. The cache at `engine+0x3ef01a4` is
seeded with -1, so PC clients skip the check.

**Host options.** `ns_auth_allow_insecure 0` (the default) makes `ps4_host_options.nut` remove
every player that is not slot 0, a bot, or Atlas-authenticated, once a second, through
`NSPS4_DisconnectClient` → `CBaseClient::Disconnect`. `ns_allow_duplicate_accounts`: the
duplicate-account check (the `je` at `engine+0xe734c`, comparing the uid at
`client+0x2d3d5`) jumps to a stub that allows a match when the ConVar is 1;
`-allowdupeaccounts` sets it. `NSIsPlayerLocalPlayer` is true only for slot 0.

**Player natives.** `GetPlayerUID` (VEngineServer slot 0x5c0, `engine+0x2dba60`) returns
`m_UID`, which no PS4 code fills; the hook fills it from the connect uid (`element+0x2d8a8`).
`NSSendClientPrint` uses VEngineServer slot 0xd8. `GetUserInfoKV*` read the userinfo KeyValues
at `element+0x4a8` with `FindKey` (`0x20a2f0`) and `GetString` (`0x20a5c0`). PS4 userinfo has
the console key set and no `name`.

**Bans** (`banlist.h`, `banlist.txt`). A banned uid's `A` packet is refused through
`RejectConnection` (`engine+0xe4850`), and its Atlas connect answered
`reject=Banned from this server.` before pdata is fetched.

**Packets** are AES-128-GCM with a fixed key (Atlas `pkg/nspkt/r2crypto.go`): nonce (12), tag
(16), ciphertext, AAD `01..10`. The engine ignores connectionless packets from loopback.

## Network and exploit fixes

Ports of PC's `exploitfixes.cpp` and `ns_limits.cpp`.

**Netmessages** (`runtime_netmessage_fixes.inl`). Each message's `GetName` is referenced by
exactly one vtable, found through the module's RELATIVE relocations. Slots: 4 Process, 5
ReadFromBuffer, 6 WriteToBuffer, 11 GetName.

| Message | Vtable | Hooked | Behaviour |
| --- | --- | --- | --- |
| `clc_Screenshot` | `0x3b0938` | read `0x1b86b0`, write `0x1b8620` | refused |
| `clc_CmdKeyValues` | `0x3af508` | read `0x1ae8d0` | refused |
| `svc_CmdKeyValues` | `0x3af590` | read `0x1ae960` | refused |
| `net_SetConVar` | `0x3afb68` | process `0x2eef30` | count, terminators, replicated flag |
| `clc_Move` | `0x3afe10` | process `0x2ef130` | counts and length |

The PS4 listen server and client share threads, so a `net_SetConVar` whose handler is in the
engine's client array is the server's.

**String commands** (`runtime_string_commands.inl`). `CGameClient::ExecuteStringCommand` is
`engine+0xd8920`; its callers `0xd8908` and `0xd8d6c` are hooked. Commands are counted against
`sv_quota_stringcmdspersecond`; a remote client's ConCommand without
`FCVAR_GAMEDLL_FOR_REMOTE_CLIENTS` is refused; with `sv_cheats 0` PC's blocked list applies. The
flag is set on the engine's 17 client commands (table `engine+0x3ace80`) and PC's cheat list,
and cleared from `migrateme`, `recheck`, `rpt_client_enable` and `rpt_password`.

**Connectionless limits.** `sv_querylimit_per_sec` (15) per address, then a minute of silence.
Atlas `T` packets are exempt. The PS4 `netadr_t` is `{int type; uint8 ip[4]; uint16 port}` with
NA_IP = 3 (PC: 16-byte address, NA_IP = 2).

**Usercmds.** ReadUsercmd is inlined into `CPlayer::ProcessUsercmds` (`server+0xb5950`). Its
call to `CBasePlayer::ProcessUsercmds` (`server+0x43e0f0`) at `server+0xbbb15` goes through a
guard that zeroes non-finite vectors and clears commands with bogus timing (NaN counts as
bogus). `CUserCmd` matches PC's `SV_CUserCmd` (stride 0x138).

**LZSS.** `CLZSS::SafeUncompress` (`engine+0x20f190`) did not check that a back-reference lies
within the output. Its entry jumps to `lzss.h`.

**Receive loop.** `NET_ProcessSocket` (`engine+0x1a6350`) stops for the frame when
`NET_GetPacket` (`engine+0x1a5500`) returns null, which a bad packet also does. The `recvfrom`
call (`engine+0x1a596e`) records per thread whether a datagram arrived, and both
`NET_GetPacket` calls (`+0x1a6442`, `+0x1a853c`) retry while one did, up to
`ns_recvfrom_per_frame_limit` (1000).

**Baselines.** The string-table baseline overflow's `Host_Error` call (`engine+0xe997d`) goes to
a thunk that disconnects that client instead of stopping the server.

**Server-sent commands.** The client's `ProcessStringCmd` (`engine+0x155e80`) wraps a remote
server's commands in execution markers (`engine+0xef330`, count at `engine+0x3e78fe0`); a remote
server is always restricted (`[this+0xfac4]` is 1 from the constructor). Past 0x800 markers the
oldest is dropped, so a command that would need more is ignored, as on PC.

## Console commands

Registered with `ConCommand::ConCommand(this, name, callback, help, flags, completion)` at
`engine+0x204e60` (object 0x58 bytes; name `+0x18`, help `+0x20`, flags `+0x28`, callback
`+0x40`). Callbacks get a PC-layout `CCommand` (argc `+0`, ArgS `+0x10`, argv `+0x410`).

- `setplaylist` → `SetCurrentPlaylist` (`engine+0x14a3a0`). When the playlist becomes `tdm`,
  `mp_gamemode` is set to `tdm` too, since PC reverts replicated ConVars on disconnect and the
  PS4 engine does not.
- `setplaylistvaroverrides` → engine-server slot 72 (`engine+0x1491d0`); the pre-map check is
  NOPed, and the 64-entry table's 128/64-byte buffers are respected.
- `ns_start_reauth_and_leave_to_lobby` (server-executable, as on PC) → `setplaylist tdm`, `map
  mp_lobby`.
- `map`'s callback (`engine+0x1224a0`) tears down the current game before checking the map.
  Its ConCommand (`engine+0x3ef2ce8`) is repointed to a guard that refuses a map that is not in
  a retail VPK, an enabled mod's VPK, or `maps/<map>.bsp`.
- `ban`, `unban`, `clearbanlist`, `say`, `say_team`, mod ConCommands.

The Source console exists in `client.prx` but cannot be typed into. Commands reach the game
through scripts (`ClientCommand`), the AI harness, or `Cbuf_AddText`.

## Chat

`ClientSayText` is `engine+0x473e0` (PC `engine.dll+0x54780`); its in-game branch reads the
client state through `engine+0x9f9618` without a check, so callers check it. Typing uses the
system keyboard (`sceImeDialog`, UTF-16; the toolchain's `wchar_t` is 4 bytes).
`OnReceivedSayTextMessage` is `server+0xaeb50`; its hook runs mods' callbacks as PC does.

On the client, the SayText handler (`client+0x1db690`) has `CHudChat::AddGameLine` inlined.
- The text-restriction check (`engine+0x2bf450`) returns the console's flag when
  `debug_force_textRestriction` is -1; the runtime sets it to 0.
- Only chat panels of type 1 receive lines. The in-match `IngameTextChat` and the lobby's
  `LobbyChatBox` are `[$WINDOWS]`-only in the stock `.res`/`.menu`; Northstar.PS4 adds them as
  `[$GAMECONSOLE]` KeyValues patches.
- The panel loop at `client+0x1db91b` jumps to a stub that hands the message to Northstar's
  `CHudChat_ProcessMessageStartThread`. Panel fields: colours `+0x2bc..+0x2c8`, context
  `+0x2d8`, rich text `+0x2e8`, next `+0x2f0`; rich-text `InsertString` takes UTF-16.

`NSBroadcastMessage` builds SayText as the server does (`CRecipientFilter` vtable
`server+0xa30dc0`, `UserMessageBegin` slot 0xc8, `MessageEnd` slot 0xd0). Players are
`[[gpGlobals+0x80] + i*8 + 0xe040]`, gpGlobals at `server+0xabd3f8`.

## Script HTTP requests and save files

**HTTP** (`runtime_http_script.inl`, `http_request.h`). `NS_InternalMakeHttpRequest` uses
sceHttp with its own template. PC's query, scheme and private-address rules apply
(`-allowlocalhttp`, `-disablehttprequests`, `-disablehttpssl` from `ns_startup_args.txt`).
sceHttp errors map to curl codes (resolve 6, connect 7, reply 8, timeout 28, TLS 60, else 56).
Results reach the VM on a later frame through the host-frame drain.

**Save files** (`mod_savefiles.h`). Sizes come from `open` + `lseek(SEEK_END)`, because
shadPS4's `stat` misreports `st_size`. Loads complete on a later frame. `-maxfoldersize` sets the
folder limit. Paths follow PC's canonical containment; backslashes and colons are refused.

## Mod downloads

`runtime_mod_download.inl` implements PC's `ModDownloader` natives: `verified-mods.json`
from R2Northstar/VerifiedMods, a streamed download hashed as it arrives, and extraction.
`mod_archive.h` has a streaming DEFLATE decoder, CRC-32 and a zip reader (no zip64, encryption
or methods other than 0 and 8; `..`, absolute and drive paths are dropped). Extraction goes to
`.staging`, the installed copy moves to `.replaced`, staging is renamed into place, and the old
copy is deleted, so a failed download keeps the working copy. `fstat` is a stub under shadPS4,
so the archive is sized with `lseek`. Downloaded paks are not converted, so they load only if
already in the PS4 layout.

## Menus and controllers

- PS4 menus move focus only along explicit `navUp`/`navDown`/`navLeft`/`navRight` links. Rows
  inside nested panels cannot be linked, so lists do their pad movement in script (d-pad and
  stick callbacks, scrolling at the edges, L1/R1 paging).
- Northstar.PS4 overrides the mode select, Mods and Mod Settings menus
  (`scripts/menus/build_mod_list_override.py`, `build_mod_settings_override.py`) and patches
  the join password prompt with nav links.
- Cross on a focused text box opens the system keyboard natively; a second keyboard is
  refused with `0x80bc0001`. Number boxes in Custom Match Settings open it on focus.
- `sceImeDialog`'s password option is refused (`0x80bc0030`) unless the type is Basic Latin or
  Number.
- PC `.vtf` UI images (VTF 7.5 header) draw as a checkerboard; retail PS4 `.vtf` in VPKs have no
  header.
- `colorsliders.menu` uses an HTML comment, which KeyValues reads as structure;
  `build_colorsliders_override.py` rewrites it with `//`.
- Footer text: `%[L_SHOULDER|]%` renders; `%[L_SHOULDER]%` renders blank.

## Files in /data/northstar_ps4

| File | |
| --- | --- |
| `R2Northstar/mods/` | Installed mods (PS4; shadPS4 uses the game folder's `R2Northstar`) |
| `enabledmods.json` | Enabled mods |
| `atlas_identity.json` | Atlas uid, token and helper pairing; secret |
| `ns_startup_args.txt` | Launch options and `+convar value` pairs, as PC's command line |
| `host_options.txt` | Host Options values saved from the menu (`name=value`); startup args win |
| `banlist.txt` | Bans |
| `save_data/` | Mod save files |
| `runtime/remote/mods/` | Downloaded mods |
| `runtime/vpk/` | Mod VPK copies (case-sensitive filesystems) |
| `scripts.rson`, `kv/`, `particles/`, `persistent_player_data_version_929.pdef` | Generated files |
| `ai_harness/`, `http_probe.txt` | Test-only (AI harness mailbox, HTTP probe URL) |

On shadPS4, `/data` is the emulator's per-game data folder
(`%APPDATA%\shadPS4\data\northstar_ps4` on Windows).

## Real PS4 compared with shadPS4

| On a PS4 | Cause | Handling |
| --- | --- | --- |
| SIGBUS in `vfprintf` | `DT_INIT` is called with the stack 8 bytes off alignment | `force_align_arg_pointer` on the entry points |
| Stack overflow in discovery | Small default thread stacks | Runtime threads get reserved stacks |
| Hooks out of rel32 range | PRX mapped near `0x800000000` | `Reachable()` stubs |
| Write fault in the next page | Per-page protection | `PageSpan()` |
| `client.prx` unloaded | Flexible memory exhausted | 8 MiB arena |
| HTTPS fails `0x809517d5` | SSL pool too small | 384 KiB SSL pool |
| Files not found | `/data` is case-sensitive | Index keeps on-disk spelling |
| Mod VPK does not mount | `MountVPK` lowercases | VPK copy under `runtime/vpk` |

GoldHEN's FTP server returns a `.prx`'s decrypted size from `SIZE` and decrypted bytes from
`RETR`; check uploads by the `LIST` size. Its kernel log serves one reader at a time and
answers "failed to open klog -16" while an earlier connection is held. Tested on a standard
PS4 (not Slim or Pro) on firmware 9.00 with GoldHEN 2.3+.

## shadPS4

- **Build.** shadPS4 `2b5666b3` and later work. Builds from #5110 (`c6fa48c7`) up to #5133
  (`f6cd16e8`) draw matches black (shadPS4#5124); `ca89b01` fixed a crash when leaving a map
  (`VCRUNTIME140+0x1cca7` on `GpuSchedPriorityPendingOpsRunner`).
- **Pipeline cache.** Enable it for CUSA04013; a map's first visit otherwise compiles hundreds of
  pipelines and can time out a join. A cache from another shadPS4 build fails at start
  ("Invalid or corrupted deserialization container", or `serdes.h` assertion); move
  `cache/CUSA04013` aside when changing builds. A forced stop can leave a 64-byte `profile.bin`
  that fails the same way.
- **Logs.** The Qt launcher starts the emulator without `--log-append`, so `shad_log.txt` is
  truncated each launch; turn on per-game `Log.append`. The log stops at about 1 GB. Use the log
  filter `*:Info Lib.Http:Warning`: at Info shadPS4 logs every HTTP URL, and Atlas takes the
  token and server password in the query string. A hung boot's log is cut mid-line because the
  emulator's buffer is not flushed; the cut is not the hang point.
- **Memory.** shadPS4 needs about 9 GB of free commit at start; with less it fails in
  `address_space.cpp` with "Insufficient system resources".
- **VRAM.** Busy maps peak at 5.3–5.7 GB on a 6 GB card; shadPS4's GPU caches, not the game's
  streaming budget, fill it.
- **APIs.** `fstat` is a stub; `stat` misreports sizes; `_fcntl` returns 0; `unlink` may leave a
  file whose handle is still in the table; `sceSslInit` logs DUMMY but HTTPS works (shadPS4
  does TLS itself); the HTTP library is HLE, so `libSceHttp.sprx` never appears as a module.
- **Debugging.** shadPS4 runs guest code natively, so `cdb` attaches directly; use `sxd av`,
  because the emulator handles access violations itself.
- **Users.** The player name comes from shadPS4's `users.json` `user_name`; Atlas replaces it
  with the account name.

## Token helper

The token helper (`token-helper/`, Tauri 2 with Rust) gets Atlas tokens on a PC signed in to
the EA app, signs in a running game, serves token refreshes, and installs the runtime.

**EA app (LSX).** The EA app serves Origin's SDK protocol on TCP 127.0.0.1:3216: XML messages
ended by a NUL byte.
1. The app sends `<LSX><Event><Challenge key=…/>`.
2. The helper answers `ChallengeResponse`: the key AES-128-ECB encrypted under key `00..0f`,
   as hex, with ContentId 1039093 and Title Titanfall2.
3. The session key is derived from the response's first two hex characters:
   `seed = c0 << 8 | c1`, then 16 bytes from MSVC `rand()` (state 7, `state = rand() + seed`,
   `rand() & 0xff`). Seed 1337 gives 251, 135, 22, 197, ….
4. Later messages are encrypted under that key and sent as hex to `EbisuSDK`: `GetProfile`
   returns the account's `UserId`; `GetAuthCode` with ClientId `TITANFALL2-PC-SERVER` returns
   the code.

The helper exchanges the code at `/client/origin_auth`, caches the token for 60 s, writes
`atlas_identity.json` through a `.tmp` and rename, and serves `GET /atlas/token` (checked
against `X-NorthstarPS4-Key`; 403, 404 or 502 with `{"error": …}` otherwise). It never prints
the code, token or key. Pairings are kept in `token-helper.json` under the OS config folder.

**Installer** (`install.rs`). Finds the newest release with a runtime through GitHub's API,
checks each asset against GitHub's SHA-256 digest, replaces Northstar's three mod folders and
this port's two, extracts the converted paks, writes the runtime, and patches `eboot.bin` only
if it hashes to the retail value (keeping the backup). Nothing changes until every download
has passed. Uninstall restores the original eboot or removes the bootstrap exactly; an
unrecognised eboot is never touched.

## Open questions

- A PS4 host once kicked its own player with "Resetting invalid loadout"
  (`FailsItemLockedValidationCheck` for `redline_sight`, then `pas_fast_ads`, in pilot loadout
  3) although `everything_unlocked` was 1. `DevEverythingUnlocked` returns false for a player
  with `ns_progression 1`, so the items are most likely locked on the account, which PC would
  also reset. Not yet confirmed against the account on PC.
- A boot occasionally stalls early. The cause is not established.
- The macOS EA app has not been tested with the token helper's LSX client.
- Not exercised in play: the restricted server-command path, the baseline overflow, client-side
  `net_SetConVar`, the usercmd blocking path, a wrong-password join.
- Not ported: `script`/`script_client`/`script_ui` console commands (the PS4 compile path is
  `0x82b160`, run by `0x82bb10` or `0x82b270`), `CNetChan::ProcessMessages` time limits,
  `CL_CopyExistingEntity` (inlined; `CL_CopyNewEntity` already checks `MAX_EDICTS`).
