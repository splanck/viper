# Phase 2 - Run, Console, and Debug

## 1. Summary and Objective

Close the edit-build-run-debug loop without freezing the IDE or relying on shell-string commands. This phase starts with safe build/run jobs and a real console, then defines the debugger protocol and UI.

The debugger is not just "expose VM hooks." A live IDE debugger needs launch configs, process lifetime management, source mapping, breakpoints, pause/kill/restart, stdout/stderr streaming, target crash handling, and a stable protocol boundary.

## 2. Scope

In:

- Project-aware run/build configurations.
- Safe argument-vector execution for immediate build/run improvements.
- Output console with structured clickable locations.
- Cancellable process/job abstraction if current `Viper.System.Exec` cannot stream output.
- Debug adapter/protocol decision.
- Breakpoint persistence and debug UI after the protocol exists.

Out:

- Reverse debugging.
- Multi-process attach.
- Watch-expression language beyond locals/stack inspection.

## 3. Existing Primitives

- Current ViperIDE build system hard-codes `compilerPath = "zia"` and uses `Exec.ShellFull`.
- Runtime has `Viper.System.Exec.CaptureArgs` and `RunArgs`, which are safer than shell strings but still blocking.
- VM debug internals exist under `src/vm/debug/`, but the IDE does not have a live process/debug protocol.
- Editor gutter breakpoint glyphs exist in the code editor widget model.

## 4. Stages

### 4.1 Stage A - Safe Build/Run and Run Configs

Requirements:

- Replace shell-string build/run commands with argument-vector execution where possible.
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
- Parse diagnostics into structured `Location` records.

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

If current `Viper.System.Exec` remains blocking, add a runtime process/job API before shipping long-running run/debug features.

Preferred shape:

- `Viper.System.Process.Start(program, args, cwd, env) -> Process`
- `Process.Poll() -> status`
- `Process.ReadStdout() -> str`
- `Process.ReadStderr() -> str`
- `Process.Kill()`
- `Process.ExitCode`

Requirements:

- Jobs are owned by the IDE and ticked in the frame loop.
- Build, run, search, indexing, and future scene validation can share the job/progress model where practical.
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
