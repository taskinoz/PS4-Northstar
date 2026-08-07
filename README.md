# PS4 Northstar

An experimental port of the Northstar modding framework to the PS4 version of Titanfall 2, targeting shadPS4 and Titanfall 2 build `R2PS4_r2dlc11_598_CL297590_2017_12_05_12_36_PM`.

**Start here: [docs/GOALS.md](docs/GOALS.md)** — the current goal-by-goal status and prioritized next steps. Static VPK integration (formerly "Stage 1") is complete; everything active right now is native runtime work (formerly "Stage 2").

## Repository policy

This repository does not contain Titanfall 2, Northstar release binaries, extracted archives, or rebuilt game files. You must provide matching PC and PS4 installations. Generated data is written beneath `work/` and `dist/`, and external toolchains live under `tools/` — all three are ignored by Git. See [tools/README.md](tools/README.md) for what needs to go under `tools/` and where to get it.

## Repository layout

| Path | Contents |
| --- | --- |
| [`docs/GOALS.md`](docs/GOALS.md) | Current status and next steps — read this first. |
| [`docs/FOUNDATION.md`](docs/FOUNDATION.md) | The completed static VPK integration phase (Goal 0). |
| [`docs/TECHNICAL-NOTES.md`](docs/TECHNICAL-NOTES.md) | The detailed native-port lab notebook: hashes, virtual addresses, byte preimages, run history. Read before changing any hash-locked code. |
| `native/stage2/` | The native OpenOrbis PRX (`northstar_ps4.prx`) source. |
| `scripts/` | PowerShell automation: environment checks, VPK build/deploy, native PRX build/deploy/iteration, overlay staging. |
| `config/` | Project configuration and hash-locked offset/manifest profiles. `config/local.json` (your machine's paths) is gitignored — copy it from `config/project.example.json`. |
| `tools/` | External toolchains and reference clones, entirely gitignored except [`tools/README.md`](tools/README.md), which lists what each subfolder needs. |
| `work/`, `dist/` | Generated staging and build output, entirely gitignored. |
| `.codex/AGENTS.md` | Role/context brief for coding agents working on this repo. |

## Bootstrap

1. Copy `config/project.example.json` to `config/local.json` and adjust the paths.
2. Fetch the required tools listed in [tools/README.md](tools/README.md).
3. Run `.\scripts\Test-Environment.ps1` to validate the PC and PS4 installations match the expected build.

## Foundation: static VPK integration (Goal 0, complete)

The currently-installed game boots on top of VPKs patched by this pipeline — it is still required, not legacy. See [FOUNDATION.md](docs/FOUNDATION.md) for the full plan and safety gates.

```powershell
.\scripts\New-Stage1Workspace.ps1 -Clean   # stage loose mod content into work/stage1/loose (does not modify either install)
.\scripts\Build-AndDeployStage1Vpks.ps1    # rebuild chunk 228 with RSPNVPK, back up, install, verify hashes
```

## Native runtime port (Goals 1+, in progress)

A hash-locked eboot bootstrap loads `northstar_ps4.prx` before Titanfall's own modules under shadPS4; its initializer runs and Titanfall continues to the main menu. See [docs/GOALS.md](docs/GOALS.md) for what's done and what's next, and [docs/TECHNICAL-NOTES.md](docs/TECHNICAL-NOTES.md) before touching the bootstrap, linker script, or runtime discovery code.

Build and redeploy the PRX:

```powershell
.\scripts\Build-Stage2Poc.ps1
.\scripts\Deploy-Stage2Poc.ps1
```

The build uses OpenOrbis through `OO_PS4_TOOLCHAIN` and includes project-local fixes for constructor boundaries and shadPS4 `DT_INIT` behavior. Do not apply the bootstrap to an unrecognized retail executable.

Iterate against a live shadPS4 run (build, deploy, launch, watch the log for a success/failure marker, stop):

```powershell
.\scripts\Invoke-Stage2Iteration.ps1
```

See `docs/TECHNICAL-NOTES.md` for the full set of build-time feature flags (`-EnableM6ScriptProbe`, `-EnableM6ScriptInject`, ...) and how to read iteration results under `work/stage2/iterations/`.
