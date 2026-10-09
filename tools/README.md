# tools/

External toolchains and reference sources. Everything here except this file is ignored by git;
a fresh clone has only this checklist.

## Required

| Path | What it is | Source |
| --- | --- | --- |
| `tools/openorbis-0.5.4/OpenOrbis/PS4Toolchain/` | The OpenOrbis PS4 toolchain (headers, libraries, `crtlib.o`, `create-fself.exe`), used by `scripts/Build-Northstar.ps1` | [OpenOrbis-PS4Toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4Toolchain) release `v0.5.4`, asset `toolchain-llvm-18.tar.gz` (SHA-256 `3c7cd5bb593ca74fa1c13fd59f3938dc0fc07985167f7275063019e63abe4526`). Extract it so this path exists, or set `OO_PS4_TOOLCHAIN` to it elsewhere. |

LLVM 18's `clang++`, `ld.lld`, `llvm-nm` and `llvm-objdump` must also be on `PATH`. The host tests
use the same `clang++`.

## Optional

| Path | What it is | Used for |
| --- | --- | --- |
| `tools/tf2vpk-bin/` | [tf2vpk](https://github.com/pg9182/tf2vpk) | Listing and unpacking the game's VPKs, for example into `PS4_EXTRACTED_ROOT` for the tests against real files |
| `tools/RSPNVPK/` | [RSPNVPK](https://github.com/taskinoz/RSPNVPK) (`feature/vpk-expansion` fixes multi-archive chunk numbers) | Building test mod VPKs |
| `tools/go-portable/` | A Go toolchain | Building tf2vpk from source |
| `tools/python-packages/` | `capstone`, `lief` (`pip install --target tools/python-packages capstone lief`) | Disassembling and reading the game's modules |

## Reference sources

Read-only clones, for comparing with PC Northstar. None is needed to build.

| Path | Repository | |
| --- | --- | --- |
| `tools/NorthstarLauncher-reference/` | [R2Northstar/NorthstarLauncher](https://github.com/R2Northstar/NorthstarLauncher) | PC Northstar's native code; `scripts/Update-NorthstarApiInventory.py` reads it. Its Windows offsets never apply to the PS4. |
| `tools/Atlas-reference/` | [R2Northstar/Atlas](https://github.com/R2Northstar/Atlas) | The master server: its API, error codes and packet formats |

Nothing in `tools/` may be game data, Northstar release binaries or extracted archives.
Emulator builds downloaded for testing go in `work/`.
