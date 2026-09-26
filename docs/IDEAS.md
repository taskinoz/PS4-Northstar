# Ideas

Things worth building, with enough detail to pick any of them up cold.

## Status (2026-09-27)

Most of this has since been built; the sections below are kept for their
design notes.

- **Idea 1 (test-connect mod):** superseded. The opt-in `AI.Harness` mod
  ([AI-HARNESS.md](AI-HARNESS.md)) launches the local lobby by itself and runs
  console commands, including `connect`. The server browser joins public servers
  through Atlas.
- **Idea 2 (agent harness):** largely built. There's `AI.Harness` (launch, status,
  console, menu, back, json, cvar, localize), `scripts/Send-PadInput.ps1` for pad
  input and `scripts/Capture-GameWindow.ps1` for screenshots. Still missing:
  structured events, assertion manifests and a machine-readable verdict.
- **Idea 3 (console pipe):** built as the AI.Harness mailbox under
  `/data/northstar_ps4/ai_harness`, executed on the UI thread through
  `ClientCommand`. The runtime also registers native console commands now
  (`runtime_concommands.inl`), through the engine's ConCommand constructor.
  Local `Cbuf` execution is still not identified.

## Correction that unblocks two of these

The console investigation concluded that running a command needed an engine
primitive - `Cbuf_AddText` or `Cmd_ExecuteString` - and that finding it was the
blocker. That was wrong, and in a way that matters for ideas 1 and 3.

`ClientCommand()` is a **Squirrel native that already works**.
`Northstar.DirectConnect` uses it today:

```squirrel
ClientCommand( "net_usesocketsforloopback 1" )
ClientCommand( "connect " + server )
```

Connecting to a server through that menu is already proven on this port, so the
native is present, reachable and does what it says. This module can already call
Squirrel functions with arguments - `FindFunction` (client+0x685cf0),
`PushObject` (client+0x6875f0) and `sq_call` (client+0x6876c0) are all resolved
and in use for lifecycle dispatch.

So command execution should route **through Squirrel**, not through the engine's
command buffer. `runtime_console.inl` keeps the `VEngineClient013` vtable dump
and the `CCommand::Tokenize` address, which stay useful if a native-level path
is ever wanted, but neither is needed for the ideas below.

The engine-level route is still unsolved, and the notes there record why:
`engine+0xf0870` is ServerCmd's network send, not local execution.

## 1. A test-connect mod

**What.** A tiny mod that runs `connect <ip>:<port>` by itself, instead of
driving the DirectConnect menu by hand on every test.

**Why.** Every connect test today is manual: boot, wait for the menu, open
DirectConnect, type an address on a controller. That is the slowest part of the
loop and it cannot be automated, which also blocks idea 2.

**How.** A mod with a UI callback that calls `ClientCommand( "connect " + addr )`.
The address belongs in a file rather than the script, so changing it does not
mean re-deploying a mod: read it through the Safe I/O natives this port already
implements, or from a convar.

Three triggers, and they suit different tests:

- **On startup**, from the UI VM's init - fastest loop, but it fires before the
  main menu exists, so it needs a delay or a menu-opened callback to be safe.
- **When the menu loads**, from `UICodeCallback_ActivateMenus` - the natural
  place, and the one to build first.
- **On a menu selection** - keeps a manual escape hatch for when the automatic
  connect is not wanted.

Worth having a guard so a boot loop cannot wedge the game: skip the auto-connect
if a marker file says the last attempt did not reach the lobby, the same shape
as the KeyValues kill switch.

## 2. An agent harness for shadPS4

**What.** A harness that can drive a boot, a connect and a match end to end and
assert on the result, rather than a person watching a log.

**Why.** Most of the hard bugs on this port were found by reading a 2.4M-line
log for one line that was or was not there. That is mechanical, and it is
exactly the work that should not be manual. Several bugs were also *missed* this
way - the missing `.mdl` files sat in plain sight in the log for hours, as
repeated failed opens, because nothing was asserting on them.

**What already exists.** `Invoke-Stage2Iteration.ps1` launches, watches the log
for a success or failure pattern and kills the process; `Start-NorthstarSession.ps1`
captures a hands-on session with `--log-append`; `Test-NorthstarProfile.ps1` runs
the host suites. That is most of a harness already.

**What is missing.**

- **Structured events.** The port logs prose. A stable machine-readable line -
  one JSON object per event - would let a harness assert on state instead of
  grepping. The log markers already act as this informally, and they have been
  wrong before: `UI lifecycle completed` fires *before* the playlist loads, which
  is how a boot test once passed while the game was about to die.
- **Assertions, not patterns.** Express a test as "reaches the lobby, serves the
  merged playlist, opens no error model" rather than one regex. Every failure in
  this project was a *set* of conditions.
- **Input injection.** Nothing can press a button today. Without it the harness
  can only test what happens automatically, which is why idea 1 comes first.
- **Screenshots.** The two most recent bugs - the invisible titan and the error
  model - were *visual*. No log assertion would have caught either. shadPS4 can
  capture frames; comparing a region against a reference would catch a whole
  class of problems that is currently invisible to automation.
- **A machine-readable verdict.** `result.json` is close; it needs the assertion
  results too.

**Shape.** A test is a manifest: build flags, profile state, the actions to take,
the assertions to check. The runner deploys, launches, drives, collects, and
returns a verdict. An agent then only has to read a verdict and a diff, not a
log.

## 3. Console access over `console.txt`, as an IPC pipe

**What.** A command pipe into the running game, modelled on
[Source-Command-Pipe](https://github.com/taskinoz/Source-Command-Pipe).

**Why.** There is no keyboard on this platform - `sceKeyboard` and `sceIme`
appear in none of the modules - so the real console can never be typed into.
A pipe sidesteps the input problem completely, and it is more useful than the
console UI for development anyway.

**The design to copy.** That project's important decision is not the pipe, it is
the threading: commands are *queued* by the IPC thread and *executed on the
engine thread* during `GameFrame`. Its own changelog records moving
`ServerCommand` out of the pipe worker and into `GameFrame` to stop failures
when entity state was unstable. The same discipline applies here, and more
sharply - Squirrel VMs are not thread-safe, and this port has already destroyed
a VM from the wrong thread once.

**How it maps.**

- **Transport.** A named pipe is not available to the guest. A file under
  `/data/northstar_ps4/` is, and the port already reads and writes there. Poll
  it, read the lines, truncate it.
- **Queue.** Parse on whichever thread notices the file; push onto a queue.
- **Drain on the engine thread.** The current sketch in `runtime_console.inl`
  drains at VM lifecycle completion, which *is* the right thread but only ticks
  on menu and map changes. A per-frame hook would make it a real pipe. The
  deferred script-call queue already needs a per-frame drain, so both want the
  same hook.
- **Execute.** `ClientCommand( line )` through Squirrel, per the correction
  above.

**Worth having.** An output path as well as an input one - commands are much
more useful when the result comes back. Script `print` output is already teed
into the log, so a reply file or a marker per command would close the loop and
give idea 2 something to assert on.

**Care.** The file is a remote-code-execution channel into the game by design.
It only reads from the app's own writable storage, which on hardware is not
reachable by anything but the app and the user, but it should stay a
development-build feature rather than something a shipped profile leaves armed.
