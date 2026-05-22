# Phase 2 - Run, Console, and Debug

## Implementation Status

Partially implemented for the current ViperIDE/runtime boundary. Safe process
plumbing landed, but the developer console and debug experience are not
product-complete. Console UX is now tracked by
[editor-first-class-plan.md](editor-first-class-plan.md); real debugging remains
future work.

- Build/run now use `RunConfig` records and direct argument vectors through
  `Viper.System.Process`, including project-level command overrides from
  `viper.project`.
- Build/run jobs are owned by the IDE frame loop, stream stdout/stderr into the
  output panel, prompt before replacing an active job, can be stopped with
  Shift+F5, and parse problem lines into structured `LocationStore` records for
  click-through navigation.
- Build/run preflight goes through the Phase 0 save path (`handleSaveAll`) so
  dirty files and external-change guards are respected before a process starts.
- Breakpoints are stored in `~/.viperide/settings.ini`, painted in the editor
  gutter, toggled by gutter click or F9, and replayed into debug launches.
- The IDE drives the existing headless `Viper.Debug.Protocol` placeholder for
  start, continue, step-over, pause, terminate, simulated stack/locals, and
  crash/exit event shape. This placeholder interprets source text; it does not
  execute compiled code, evaluate expressions, follow real control flow, or
  expose VM stack frames.
- A real debugger remains future work: either a crash-isolated external adapter
  subprocess or a hosted VM debugger with proven lifetime, reentrancy, and crash
  isolation.
- The focused CTest gate is `zia_viperide_phase2_phase3`; it covers paths with
  spaces, project run overrides, real project-entry execution, streaming and
  cancellable process jobs, diagnostic parsing, persisted breakpoints, the
  placeholder debug protocol event shape, and the Phase 3 scene data contract.

## 1. Summary and Objective

Close the edit-build-run-debug loop without freezing the IDE or relying on shell-string commands. The current implementation starts with safe build/run jobs and structured output rows; a real console surface remains part of the editor-first correction plan.

The debugger is not just "expose VM hooks." A live IDE debugger needs launch configs, process lifetime management, source mapping, breakpoints, pause/kill/restart, stdout/stderr streaming, target crash handling, and a stable protocol boundary.

## 2. Scope

In:

- Project-aware run/build configurations.
- Safe argument-vector execution for immediate build/run improvements.
- Output rows with structured clickable locations. A fuller console surface with
  copy/search/filter/wrap/severity UI remains deferred.
- Cancellable process/job abstraction if current `Viper.System.Exec` cannot stream output.
- Debug adapter/protocol decision.
- Breakpoint persistence and debug UI against the current placeholder protocol.

Out:

- Reverse debugging.
- Multi-process attach.
- Watch-expression language beyond locals/stack inspection.

## 3. Existing Primitives

- Current ViperIDE build system hard-codes `compilerPath = "zia"` and uses `Exec.ShellFull`.
- Runtime has `Viper.System.Exec.CaptureArgs` and `RunArgs`, which are safer than shell strings but still blocking.
- Runtime also has `Viper.System.Process` and `Viper.System.Process.Handle` with direct argument-vector process start, cwd/env support, non-blocking stdout/stderr reads, polling, exit-code retrieval, termination, wait, and destroy.
- VM debug internals exist under `src/vm/debug/`, but the IDE does not have a live process/debug protocol.
- A headless `Viper.Debug.Protocol` shell exists, but ViperIDE still needs launch/session integration with either a hosted VM or subprocess adapter.
- Editor gutter breakpoint glyphs exist in the code editor widget model.

## 4. Stages

### 4.1 Stage A - Safe Build/Run and Run Configs

Requirements:

- Replace shell-string build/run commands with argument-vector execution through `Viper.System.Process` for any command that may run long or produce streamed output.
- Add a `RunConfig` model:
  - name
  - kind (`current-file`, `project-entry`, `custom`)
  - program/compiler
  - args
  - workingDirectory
  - env entries
  - problemMatcher
- Read defaults from `viper.project` where possible, with project-root fallback.
- Quote nothing manually when using args APIs.
- Save dirty files before build/run through Phase 0 close/save contracts.
- Prefer `Viper.Zia.Toolchain` structured diagnostics for Zia builds where possible; parse external tool output only through explicit problem matchers into structured `Location` records.

Acceptance:

- Paths with spaces build and run.
- Zia and BASIC projects can define different commands.
- Build diagnostics open the correct file and line.

### 4.2 Stage B - Output Console

Requirements:

- Replace or supplement the output `ListBox` with a real console widget or console surface.
- Support monospace rendering, scrollback, clear, copy, search/filter, line wrapping, and severity markers.
- Console rows that contain problem locations link to Phase 0 `LocationStore`.
- The console must not be the data model; diagnostics/problems remain structured records.

Acceptance:

- Build output preserves ordering and can be copied.
- Clicking an error opens the file.
- Long output does not make the IDE unusable.

### 4.3 Stage C - Cancellable Jobs and Streaming Output

Use the current `Viper.System.Process` API as the runtime process primitive before shipping long-running run/debug features.

Current runtime shape:

- `Viper.System.Process.Start(program, args) -> Process.Handle`
- `Viper.System.Process.StartIn(program, args, cwd) -> Process.Handle`
- `Viper.System.Process.StartWithEnv(program, args, cwd, env) -> Process.Handle`
- `Process.Handle.Poll() -> bool`
- `Process.Handle.IsRunning() -> bool`
- `Process.Handle.ReadStdout() -> str`
- `Process.Handle.ReadStderr() -> str`
- `Process.Handle.Kill() -> bool`
- `Process.Handle.ExitCode() -> i64`
- `Process.Handle.Wait() -> i64`
- `Process.Handle.Destroy()`

Requirements:

- Jobs are owned by the IDE and ticked in the frame loop.
- Build, run, search, indexing, and future scene validation can share an IDE-side job/progress model where practical. This wrapper is product/UI state above the runtime process handle, not a replacement for child-process control.
- Stopping a run must terminate the child process and update UI state.

Acceptance:

- Running an infinite program does not freeze the IDE.
- Stop kills the process on macOS, Linux, and Windows.
- Output streams while the process is running.

### 4.4 Stage D - Debug Protocol Decision

Decide before UI work:

- External adapter/subprocess protocol is preferred because it isolates target crashes from the IDE and matches the current run model.
- In-process hosted VM is acceptable only if lifetime, reentrancy, graphics/input, and crash isolation are proven.

Protocol requirements:

- launch/terminate/restart
- set/clear/list breakpoints
- continue/pause/step over/step into/step out
- stopped events with reason
- stack frames
- scopes/locals
- source path normalization
- stdout/stderr events
- target exit/crash events

Acceptance:

- A headless protocol test can launch a sample, stop at a breakpoint, step, inspect locals, and terminate.

### 4.5 Stage E - Debug UI

Requirements:

- Breakpoints in the gutter with persisted path/line state.
- Debug toolbar: continue, pause, step over, step into, step out, restart, stop.
- Call stack panel.
- Locals panel.
- Debug console output.
- Clear disabled states when no debug session is active.

Acceptance:

- Set breakpoint, run, stop, step, inspect local, continue, terminate.
- Breakpoints survive IDE restart.
- Target crash does not crash the IDE.

## 5. Error Handling

- Missing run config: offer to run current file or project entry.
- Command not found: structured error in console, no crash.
- Build/run already active: prompt to stop or reuse.
- Process start failure: show command, cwd, and reason.
- Debug protocol disconnect: stop session and keep editor state.

## 6. Tests

- Args execution: path with spaces.
- Problem matcher: compiler output produces structured locations.
- Console: append, scrollback cap, clickable location.
- Process API if added: start, poll, read stdout, kill.
- Debug protocol: breakpoint hit, step, locals, terminate.
- UI manual: build, run, stop, debug from ViperIDE.

## 7. Manual Verification

- Run current file and project entry.
- Run a long process and stop it.
- Build a file with errors and click diagnostics.
- Debug a small project from launch through breakpoint, step, locals, and stop.
