# AGENTS.md

Context for coding agents working in this repository. Human-facing docs are in `docs/`.

## What this is

PS4 Northstar ports [Northstar](https://northstar.tf), the Titanfall 2 mod platform, to the PS4
version of the game (`CUSA04013`, patch 1.13, build
`R2PS4_r2dlc11_598_CL297590_2017_12_05_12_36_PM`). It runs Northstar's unchanged mods through a
native runtime, `northstar_ps4.prx`, which hooks the retail game's modules at known addresses.

It runs in two places, and both must keep working:
- **shadPS4** (the emulator, on Windows): a patched `eboot.bin` loads the runtime from
  `<game>/bin/ps4_retail/`; mods are in `<game>/R2Northstar/mods`.
- **A real PS4 with GoldHEN**: the runtime is a GoldHEN plugin; mods are in
  `/data/northstar_ps4/R2Northstar/mods`.

The port stays compatible with PC Northstar: PC players join PS4 hosts and PS4 players join PC
servers, through the same master server (Atlas, `northstar.tf`).

## Layout

| Path | |
| --- | --- |
| `launcher/src/runtime.cpp` | The runtime. Includes every `runtime_*.inl` (one per subsystem: filesystem overlay, VMs, Atlas, chat, rpaks, …). |
| `launcher/src/module.cpp` | Entry points: `NorthstarPs4Init` (DT_INIT), GoldHEN `plugin_load`/`plugin_unload`. |
| `launcher/include/northstar_ps4/*.h` | Portable, platform-free logic (parsers, rules, converters), each with a host test in `tests/`. |
| `launcher/link.x` | Linker script (defines `__init_array_start/end`). |
| `mods/Northstar.PS4` | Required companion mod: PS4 overrides of Northstar files, KeyValues patches, menus, persistence definition. |
| `mods/Northstar.DirectConnect` | Direct Connect menu. |
| `mods/AI.Harness` | Test harness mod (never shipped). |
| `token-helper/` | Tauri 2 app (Rust in `src-tauri/src`, page in `ui/`): EA app sign-in (LSX), token serving, installer. |
| `scripts/` | PowerShell and Python tooling. `Env.ps1` / `northstar_env.py` load settings. |
| `scripts/ps4/` | Real-PS4 tooling: FTP upload, kernel-log recorder, last-boot summary. |
| `tests/` | Host test suites (`*.cpp`), `engine-profile/CUSA04013.json`, `token_helper/` fakes. |
| `vendor/` | NorthstarMods and NorthstarNavs submodules pinned to Northstar v1.31.13. |
| `docs/INTERNALS.md` | Addresses, layouts and platform behaviour, by subsystem. Read the relevant section before changing a hook. |

## Tools and settings

- Windows, PowerShell 7 for the `.ps1` scripts, Python 3, LLVM 18 (`clang++`, `ld.lld`,
  `llvm-nm`, `llvm-objdump`), the OpenOrbis 0.5.4 toolchain (`tools/README.md`).
- Token helper: Bun and Rust.
- Machine-specific paths come from environment variables or `.env` at the root (see
  `.env.example`): `NORTHSTAR_PS4_GAME_ROOT`, `SHADPS4_EXE`, `SHADPS4_USER_DIR`,
  `NORTHSTAR_MODS_ROOT`, `PS4_EXTRACTED_ROOT`, `OO_PS4_TOOLCHAIN`, `PS4_ADDRESS`. Never hard-code
  a path from one machine.
- The `build-and-test` skill in `.claude/skills/` has the build, deploy and test commands.

## How the runtime is written

- **Patch only at verified bytes.** Every hook is gated on the module's hash and the exact bytes
  it replaces; a mismatch logs and skips. PC Northstar offsets never apply to the PS4: find the
  PS4 address from strings, callers, or code shared between modules, and record it in
  `docs/INTERNALS.md`.
- **No RWX memory under shadPS4.** Hook call sites (rel32 displacement) or vtable slots, not
  prologues. On a PS4 the PRX is out of rel32 range of the game, so patches go through
  `Reachable()` jump stubs, and writes through `PageSpan()`. `-ForceBranchStubs` builds exercise
  the stub path in shadPS4.
- **No static constructors run** under shadPS4. Allocate non-trivial globals on first use.
- **No exceptions, no `std::regex`.** The build uses `-fno-exceptions`; allocation failure
  aborts. Use the existing `regex_lite.h`, bounded buffers, and readers instead of whole-file
  reads.
- **Memory is tight on a PS4.** A large allocation, thread or arena can make the game unload
  its own modules. Reuse the reserved thread stacks; keep buffers small.
- **Threads.** Hooks run on engine threads. Shared state needs a lock or an atomic snapshot;
  script calls happen on the VM's own thread, and async results go through the host-frame drain.
- **Logging.** `LogFormat` with an `[NorthstarPS4]` prefix. Log facts that diagnose a failure;
  never a token, auth code, refresh key or password.
- **Portable logic goes in a header** under `launcher/include/northstar_ps4/` with a test in
  `tests/`, registered in `Test-NorthstarProfile.ps1`.
- **Behave as PC Northstar does.** Port PC's rules (the NorthstarLauncher source) and note any
  deliberate difference.
- **PS4-specific script or menu fixes** go in `mods/Northstar.PS4` as overrides or KeyValues
  patches, never as edits to Northstar's mods.

Comments and docs state facts and findings, in plain words. No dates, session history, or
narration; history lives in git.

## Testing

Run the host tests and engine profile after any runtime change, and a boot when hooks change.
Boot stability is measured over 8 or more boots. The token helper has `cargo test`. The commands
are in `docs/BUILDING.md` and the skill.

## Safety

- Never commit or print `atlas_identity.json`, Atlas tokens, EA auth codes, refresh keys or
  server passwords. Filter shared logs to `[NorthstarPS4]` lines without `playerToken=` or
  `password=`. Keep `net_debug_atlas_packet` off outside debugging.
- Test sign-in against the fakes (`tests/token_helper/fake_services.py`, `Test-BootLoop.ps1`).
  Anything touching a real EA account or Atlas needs the user's permission each time. The real
  `atlas_identity.json` may only be moved aside unread for a test and restored unchanged.
- Never modify the game's retail archives or commit game files, Northstar binaries, or
  extracted archives. `eboot.bin` is only patched by `Enable-Bootstrap.ps1` or the installer,
  which check its hash and keep the original.
- Test mods placed in a player's mods folder show up in their sessions; remove them after use.
- Ask before uploading to a PS4, publishing a release, or posting anywhere.
