# System Signal Handling / Graceful Shutdown

**Status:** Completed — `Viper.System.Shutdown` exposes poll-based graceful shutdown over
the existing VM interrupt/shutdown path.
**Area:** `src/vm/`, `src/runtime/system/`
**Effort:** S
**Roadmap fit:** v0.2.x hardening

## Problem

The original claim that Viper has no interrupt/signal support was too broad. The VM
already has an interrupt path:

- `src/vm/VM.cpp` installs SIGINT / Windows console Ctrl-C handling, maintains an
  interrupt epoch, exposes `VM::requestInterrupt()` / `VM::clearInterrupt()`, polls in
  dispatch, and raises `TrapKind::Interrupt`.
- `src/tests/vm/InterruptTests.cpp` verifies request, clear, and interrupt traps.
- `TrapKind::Interrupt` is part of the shared VM/bytecode trap identity.

The real gap is one layer higher: a Viper server, game, or long-running tool cannot
observe "shutdown requested" as ordinary script state, finish a frame, flush saves, and
close sockets. Ctrl-C currently means an interrupt trap unless the host embeds the VM and
manages interruption externally.

## Goal & scope

- **In:** Expose a poll-based graceful-shutdown API that reuses the VM interrupt token
  for Ctrl-C and optionally adds SIGTERM through the same approved platform layer.
- **Out:** Arbitrary signal sets, signal-driven callbacks into managed code (unsafe),
  job-control signals, `kill`-by-signal of other processes (the Process API already has
  `Kill`).

## Design — poll, not callback

Polling is both **idiomatic** (the runtime already exposes `Watcher.Poll`,
`Process.Poll`, `canvas_poll`) and **correct**: signal/console handlers may only publish
atomic state; no GC-managed callback runs in signal context. The loop calls
`Shutdown.Poll()` once per frame.

```c
// VM/platform layer owns OS signal capture.
// Runtime API reads the VM interrupt/shutdown token and clears it when requested.
```

`Poll()` reads and clears pending shutdown reasons, returning a bitmask. `Pending()` is a
non-clearing peek. `Request()` lets tests, embeddings, and cooperative subsystems request
the same graceful path without going through the OS.

## Implementation steps

1. Add a small VM/runtime bridge for the active VM interrupt token:
   - `rt_shutdown_request(reason)` — cooperative/manual request.
   - `rt_shutdown_poll() -> i64` — read-and-clear bitmask.
   - `rt_shutdown_pending() -> i1` — non-clearing peek.
   - `rt_shutdown_clear()` — clear pending shutdown state.
   - constants `NONE`, `INTERRUPT`, `TERMINATE`.
2. Keep OS handler installation in the VM/platform layer. If SIGTERM is added, add it
   beside the existing SIGINT/console handling, not in ad hoc runtime code.
3. Decide interrupt-trap interaction explicitly: graceful polling may consume the
   interrupt before the VM raises `TrapKind::Interrupt`; unpolled interrupts still trap.
4. Register in `runtime.def`; run `./scripts/check_runtime_completeness.sh`.
5. Document under `docs/viperlib/system.md` with a canonical shutdown snippet.

## API surface (`runtime.def`)

```
RT_FUNC(ShutdownRequest, rt_shutdown_request, "Viper.System.Shutdown.Request", "void(i64)")
RT_FUNC(ShutdownPoll,    rt_shutdown_poll,    "Viper.System.Shutdown.Poll",    "i64()")
RT_FUNC(ShutdownPending, rt_shutdown_pending, "Viper.System.Shutdown.Pending", "i1()")
RT_FUNC(ShutdownClear,   rt_shutdown_clear,   "Viper.System.Shutdown.Clear",   "void()")
// Static class: RT_CLASS_BEGIN("Viper.System.Shutdown", Shutdown, "none", none)
//   RT_PROP("NONE","i64",...) RT_PROP("INTERRUPT","i64",...) RT_PROP("TERMINATE","i64",...)
//   RT_METHOD("Request","void(i64)",ShutdownRequest) ... RT_CLASS_END
```

Usage pattern:
```
while running {
    if Shutdown.Poll() != Shutdown.NONE { saveAndQuit(); break }
    tick()
}
```

## Tests (`src/tests/runtime/`, platform-gated)

- Unit injection: `Shutdown.Request(INTERRUPT)`, assert `Pending()` peeks, `Poll()`
  returns interrupt once, and a second `Poll()` returns `NONE`.
- VM integration: a loop that polls before dispatch can exit normally; a loop that never
  polls still traps with `TrapKind::Interrupt`.
- If SIGTERM is added: POSIX `raise(SIGTERM)` and Windows-equivalent injection seam prove
  the same reason bit flows through the shared path.
- Gate device/console-dependent assertions with the existing skip pattern used for
  audio/display-dependent tests.

## Cross-platform

Existing SIGINT / console handling is already platform-specific in approved VM code. Any
SIGTERM addition must follow that pattern and pass `./scripts/lint_platform_policy.sh`.

## Documentation

- Document `Viper.System.Shutdown` in the `docs/viperlib/` system section with the
  canonical graceful-shutdown loop snippet and the poll-not-callback rationale.
- Add a Ctrl-C/SIGTERM example to any server or game-loop guide.
- Update `docs/viperlib/README.md` function tallies if it counts them.
- One concise release-notes line.

## Implementation notes

- `src/runtime/system/rt_shutdown.c/.h` implements request, poll, pending, clear, and
  reason constants.
- `runtime.def` registers the static `Viper.System.Shutdown` class and methods.
- Ctrl-C/SIGTERM bridge through the approved VM/platform interrupt path; polling consumes
  the graceful reason while unpolled interrupts still trap.
- `docs/viperlib/system.md` documents the poll-not-callback contract and Zia/BASIC loop
  snippets.

## Verification

- `ctest --test-dir build -R '^test_rt_shutdown$' --output-on-failure`
- `./scripts/check_runtime_completeness.sh`
- `./scripts/lint_platform_policy.sh`

## Risks / open questions

- **Trap vs graceful exit:** define whether `Poll()` consumes the pending VM interrupt.
  The least surprising rule is "polled shutdown exits normally; unpolled interrupt traps."
- **SIGTERM:** do not duplicate signal handlers in runtime code. Extend the existing VM
  platform hook if termination support is added.
