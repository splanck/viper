# ADR 0016: PTY Runtime Surface (Viper.System.Pty) and Integrated Terminal

## Status

Accepted (runtime implemented; the ViperIDE terminal UI that consumes it follows
in the same Phase 5D work).

## Context

ViperIDE needs an integrated terminal. The existing `Viper.System.Process` is a
pipe-based, dual-stream (`ReadStdout`/`ReadStderr`) child-process API with no
controlling terminal and no window-size concept — so programs run under it see a
non-TTY, disable interactive behavior, and cannot be resized. Exposing a real
pseudo-terminal is a new runtime C-ABI surface, which requires an ADR.

The project forbids external dependencies and requires 100% cross-platform
support, so a vendored terminal library (libvterm/ConPTY wrappers) is out; the
PTY must be built directly on OS primitives.

## Decision

Add a new runtime class pair, distinct from `Process`:

- `Viper.System.Pty` (factory): `Open(program, args, cwd, env, cols, rows) ->
  PtySession`, `IsSupported() -> i1`, `LastError() -> str`.
- `Viper.System.Pty.PtySession` (handle): `IsValid`, `Poll`, `IsRunning`, `Read`
  (one **merged** ANSI-bearing stream), `ReadResult` (`text`/`truncated`),
  `Write`, `Resize(cols, rows)`, `ExitCode`, `Kill`, `Wait`, `Destroy`.

Rationale for a separate class (not extending `Process`): a PTY merges
stdout+stderr into one TTY stream and adds resize, which would make the
`Process` contract incoherent. Leaf names `Pty`/`PtySession` are globally unique
(enforced by `test_runtime_class_qualified_surface`); new enum members
`RTCLS_Pty`/`RTCLS_PtySession`; runtime class id `-0x440202` (next free after
`Process`'s `-0x440201`).

The handle mirrors `Process`'s proven model: GC-finalized, main-thread-only,
incremental non-blocking drain on each `Read`/`Poll` (no background reader
thread), 16 MiB ring buffer with truncation trap, SIGTERM→SIGKILL escalation on
close.

`LastError()` is a non-throwing diagnostic companion for `Open`/`IsSupported`.
It preserves the existing nullable `Open` contract while allowing IDE surfaces
to explain unsupported ConPTY, invalid startup paths, and platform open/fork
failures without changing the `PtySession` handle shape.

**Backends** (platform `#ifdef`s confined to `rt_pty.c`):
- **POSIX (macOS/Linux):** `posix_openpt`+`grantpt`+`unlockpt`+`ptsname`, then
  `fork` + `setsid` + `ioctl(TIOCSCTTY)` + `dup2(slave→0/1/2)` + `execvp`/`execve`
  (a controlling terminal cannot be established via `posix_spawn`). Non-blocking
  master via `fcntl`; resize via `ioctl(TIOCSWINSZ)` (delivers SIGWINCH).
- **Windows:** ConPTY (`CreatePseudoConsole`/`ResizePseudoConsole`/
  `ClosePseudoConsole`) wired through `STARTUPINFOEXW` +
  `PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE`, with non-blocking `PeekNamedPipe`
  drains. The three ConPTY symbols are resolved **dynamically** so the binary
  still loads on pre-1809 Windows; `IsSupported()` reflects availability.
- **ViperDOS:** unsupported (`Open` returns NULL, `IsSupported` returns 0).

**Rendering** reuses the existing `Viper.GUI.OutputPane`, which is already
ANSI-SGR-aware and color-stateful across appends; only carriage-return column
reset and `ESC[K`/`ESC[2K` erase-line handling are added to the widget. No new
GUI widget or class.

## Consequences / Limitations

- v1 renders with `OutputPane`'s line-append model, so full-screen/alt-screen TUI
  apps (vim, htop, ncurses) do not render correctly; 256-color/truecolor and
  cursor-addressing escapes are swallowed harmlessly. This is an accepted v1
  scope ("build/run/REPL console", not a full xterm); a cursor-addressable grid
  widget would be a separate future ADR.
- `i1` returns map to Zia `Boolean` (compare with `== false`), `i64` to `Integer`.

## Cross-platform verification (no CI)

The POSIX path is implemented and verified on macOS (live `Pty.Open` smoke). The
**Windows ConPTY path cannot be built or run on a non-Windows host**, so per
project policy (no CI) it must be validated by hand on Windows. `IsSupported()`
keeps the headless terminal probe green on every platform meanwhile.

## Compliance

- `source_health_baseline.tsv` `runtime_api_contract_files` 790→792 (the two new
  `rt_pty.{c,h}` files).
- `check_runtime_completeness.sh` passes (every RT_METHOD has an RT_FUNC).
- `test_runtime_class_qualified_surface` passes (unique leaf names).
- Full Viper file headers on `rt_pty.{c,h}`; raw platform macros confined to the
  `rt_pty.c` adapter.

## Alternatives Considered

- **Extend `Process`** — rejected: incoherent surface (TTY merge + resize).
- **A new `vg_terminal` grid widget** — rejected for v1: duplicates `OutputPane`,
  large surface, needs its own `RTCLS_`/ADR. Revisit if full TUI support is
  needed.
- **Bundle a vterm library** — rejected: violates zero-dependencies.
