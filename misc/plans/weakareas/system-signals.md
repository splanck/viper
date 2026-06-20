# System Signal Handling / Graceful Shutdown

**Status:** Verified real
**Area:** `src/runtime/system/`
**Effort:** S
**Roadmap fit:** v0.2.x hardening

## Problem

There is no OS signal handling exposed to the language. A broad grep of `runtime.def`
for `SIGINT`/`SIGTERM`/`Signal`/`Interrupt`/`atexit`/`CtrlC` finds nothing relevant
(the only `Signal` hits are `Threads.Debouncer.Signal` and a Game3D collision phase).
Consequently a Viper server, game, or long-running tool **cannot catch Ctrl-C / SIGTERM
to flush saves, close sockets, or shut down cleanly** — it just dies.

The `system/` module is otherwise solid (Environment, Clipboard, Exec, full Process
API), so this is a small, well-scoped addition that closes a real gap.

## Goal & scope

- **In:** Catch SIGINT (Ctrl-C) and SIGTERM, expose them through a **poll-based** API so
  a frame/event loop can notice and shut down gracefully.
- **Out:** Arbitrary signal sets, signal-driven callbacks into managed code (unsafe),
  job-control signals, `kill`-by-signal of other processes (the Process API already has
  `Kill`).

## Design — poll, not callback

Polling is both **idiomatic** (the runtime already exposes `Watcher.Poll`,
`Process.Poll`, `canvas_poll`) and **correct**: the C signal handler must be
async-signal-safe, so it only sets a `volatile sig_atomic_t` flag; no GC-managed
callback runs in signal context. The loop calls `Signals.Poll()` once per frame.

```c
static volatile sig_atomic_t g_pending = 0;   // bitmask of RT_SIG_*
// POSIX handler:  void on_sig(int s){ g_pending |= bit(s); }
// Windows:        BOOL WINAPI on_ctrl(DWORD t){ g_pending |= bit(t); return TRUE; }
```

`Poll()` atomically reads and clears `g_pending`, returning which signal(s) fired since
the last poll (0 = none). `Pending()` is a non-clearing peek.

## Implementation steps

1. `src/runtime/system/rt_signals.h/.c`:
   - `rt_signals_install(void)` — register handlers (idempotent).
   - `rt_signals_restore(void)` — restore defaults.
   - `rt_signals_poll(void) -> int64_t` — read-and-clear bitmask.
   - `rt_signals_pending(void) -> int` — non-clearing.
   - Constants `RT_SIG_INTERRUPT`, `RT_SIG_TERMINATE`.
2. Platform split **behind `rt_platform.h`** (no raw `_WIN32`/`__linux__` in module
   code — per the platform policy this lives in the approved adapter layer):
   - POSIX: `sigaction` with `SA_RESTART` cleared so blocking syscalls return `EINTR`.
   - Windows: `SetConsoleCtrlHandler` (handler runs on a separate OS thread → the flag
     must be written atomically; `sig_atomic_t`/interlocked is sufficient for a set).
3. Register in `runtime.def` (below); `check_runtime_completeness.sh`.
4. Document under `docs/viperlib/` (system section) with a canonical shutdown snippet.

## API surface (`runtime.def`)

```
RT_FUNC(SignalsInstall, rt_signals_install, "Viper.System.Signals.Install",  "void()")
RT_FUNC(SignalsRestore, rt_signals_restore, "Viper.System.Signals.Restore",  "void()")
RT_FUNC(SignalsPoll,    rt_signals_poll,    "Viper.System.Signals.Poll",     "i64()")
RT_FUNC(SignalsPending, rt_signals_pending, "Viper.System.Signals.Pending",  "i1()")
// Static class:  RT_CLASS_BEGIN("Viper.System.Signals", Signals, "none", none)
//   RT_PROP("SIGNAL_NONE","i64",...) RT_PROP("SIGNAL_INTERRUPT","i64",...) RT_PROP("SIGNAL_TERMINATE","i64",...)
//   RT_METHOD("Install","void()",SignalsInstall) ... RT_CLASS_END
```

Usage pattern:
```
Signals.Install()
while running {
    if Signals.Poll() != Signals.SIGNAL_NONE { saveAndQuit(); break }
    tick()
}
```

## Tests (`src/tests/runtime/`, platform-gated)

- POSIX: `Install()`, `raise(SIGINT)`, assert `Poll()` reports interrupt then 0 on the
  next poll. Repeat for SIGTERM.
- Windows: drive via `GenerateConsoleCtrlEvent` (or a unit-level injection seam) and
  assert the same.
- `Pending()` peeks without clearing; `Restore()` returns to default disposition.
- Gate device/console-dependent assertions with the existing skip pattern used for
  audio/display-dependent tests.

## Cross-platform

POSIX `sigaction` vs Windows `SetConsoleCtrlHandler`, isolated in the `rt_platform.h`
adapter. Run `./scripts/lint_platform_policy.sh` to confirm no stray raw macros.

## Documentation

- Document `Viper.System.Signals` in the `docs/viperlib/` system section with the
  canonical graceful-shutdown loop snippet and the poll-not-callback rationale.
- Add a Ctrl-C/SIGTERM example to any server or game-loop guide.
- Update `docs/viperlib/README.md` function tallies if it counts them.
- One concise release-notes line.

## Risks / open questions

- **Async-signal-safety:** the handler may only set the flag — no allocation, no
  locks, no managed calls. Document this hard invariant in the file header.
- **EINTR:** clearing `SA_RESTART` makes blocking I/O return `EINTR`; verify the
  network/io layers tolerate that, or keep `SA_RESTART` and rely purely on the poll.
- **Windows handler thread:** the flag write is fine, but if later extended to richer
  state, that state must be atomically published.
