# ViperIDE Real Debugger — Design (Phase 3)

## Context

ViperIDE's current "debugger" (`rt_debug_protocol.cpp`) is a non-executing **source-line
simulator**: it splits the file into lines and walks a counter, never running the program, with
no real values or output. Phase 0 honestly relabeled it "Debug Preview." This document designs the
**real** debugger: run the user's Zia/BASIC program under VM control with breakpoints, step
over/into/out, a live call stack, variable inspection by name, and program output — crash-isolated
from the IDE, zero external dependencies, cross-platform.

## Key finding: the VM is already a (batch) debugger

The hard part — a debug-aware execution engine — **already exists**. Evidence:

| Capability | Where | Status |
|---|---|---|
| Per-instruction debug hook (pre/post) | `src/vm/debug/VMDebug.cpp:186` `processDebugControl` | ✅ |
| Source-line breakpoint check in the loop | `VMDebug.cpp:151` `debug.shouldBreakOn(*in)` | ✅ |
| Breakpoint API | `src/vm/debug/Debug.cpp:148` `addBreakSrcLine`, `:190` `shouldBreakOn`, `:158` `hasSrcLineBPs` | ✅ |
| Step over/into/out | `VMDebug.cpp` `debugStepMode_` + `debugStepTargetDepth_` vs `execStack.size()` (`applyDebugAction`) | ✅ |
| Call stack walk | `src/vm/debug/VM_DebugUtils.cpp` `buildBacktrace()` over `execStack` | ✅ |
| Source locations end-to-end | `il::core::Instr::loc` (file/line/col), set by Zia `ZiaLocationScope` and BASIC `LocationScope`, serialized as `.loc` | ✅ |
| **Named locals** | `src/il/core/Function.hpp:79` `valueNames[ssaId] -> source name` | ✅ |
| Debug-enabled launch | `viper run --debug-vm` → `cmd_run.cpp:447` `verifyAndExecute` → `vm::Runner(module, RunConfig{.debug})` | ✅ |
| Stop-and-resume seam | `VMDebug.cpp:67` `pauseOrAdvanceDebugScript`: `script ? script->nextAction() : halt`, prints `[BREAK]` to stderr | ✅ (file-driven) |

**The one missing piece is interactivity.** Today the sequence of debug actions is read from a
**`DebugScript` file** (`src/vm/debug/DebugScript.cpp:36` — constructed from a path) and stops are
reported as `[BREAK] …` **stderr text**. The real debugger reuses all of the above and only changes
*where actions come from* (a live IDE command stream) and *how stops are reported* (structured
events).

The IDE side is also already wired: `viperide/src/build/debug_session.zia`,
`commands/debug_commands.zia` (start/continue/step/pause/stop handlers, gutter breakpoints), and
`build/breakpoints.zia` (persisted path+line store, gutter icons) all exist and currently drive the
placeholder. They consume events shaped as `{type, reason, path, line}`, frames `{id,name,path,line}`,
locals `{name,value,type}`.

## Architecture

**Out-of-process, VM-backed, DAP-shaped adapter.** The IDE spawns a child
`viper run --debug-adapter <file>` that hosts the VM under debug control and speaks a small JSON
protocol. Rationale:

- **Crash isolation** — debugging runs buggy code; a debuggee segfault/hang/OOM must not take down
  the IDE. An in-process VM (linking the VM into `viperide`) is rejected for this reason, plus VM
  global state / thread-safety concerns.
- **Reuses the existing build-job pattern** — `build_commands.UpdateJob` already spawns a child,
  streams output each frame, and reports completion. The debug client mirrors it.
- **Native vs VM**: "Run" uses the native codegen binary (fast); "Debug" runs the program
  **interpreted under the VM** (the only path with the debug hooks). This is the standard
  debug/release split. (Native DWARF debugging via lldb is out of scope — codegen already emits
  `.debug_line`, so it's a possible *future* path, not this one.)

### Transport: protocol on the child's stdout (newline-delimited JSON)

Three streams exist (debuggee-stdout, debuggee-stderr, control) but `rt_process` gives two pipes
(stdout/stderr). Decision, matching the DAP standard:

- **stdout = control channel**, exclusively newline-delimited JSON events written by the adapter.
- **debuggee stdout/stderr** are captured by the adapter (one-time `dup2` redirect at startup) and
  re-emitted as `{"type":"output","category":"stdout|stderr","text":…}` events. The IDE renders
  these in the Output/Debug console (wiring already exists).
- **stdin = command channel**, newline-delimited JSON from the IDE. Requires one new runtime
  primitive: `rt_process_write_stdin` (+ `Process.WriteStdin` binding) — a symmetric addition to the
  existing stdout/stderr pipe setup in `src/runtime/system/rt_process.c`.

This avoids sockets entirely (no ports, no macOS firewall prompt, trivial Windows portability).
*Alternative if stdout-capture proves awkward on Windows:* a loopback socket on an OS-assigned
ephemeral port announced on stdout (`rt_async_socket` exists). Recorded as fallback, not the plan.

### The VM seam: extract a `DebugFrontend`

Refactor the file-driven stop logic in `VMDebug.cpp` (`pauseOrAdvanceDebugScript`, `handleDebugBreak`)
behind a small interface, replacing the hardcoded `std::cerr` prints and `script->nextAction()`:

```
class DebugFrontend {                       // src/vm/debug/DebugFrontend.hpp (new)
  // Called at every pause. Emits a stop and returns the next action to apply.
  virtual DebugAction onStop(VM &vm, ExecState &st, std::string_view reason) = 0;
  virtual void onOutput(std::string_view text, bool isErr) {}   // optional
  virtual void onTerminated(int64_t exitCode, std::string_view reason) {}
};
```

- **`ScriptedFrontend`** wraps today's behavior (read `DebugScript` file, stderr `[BREAK]` text) —
  keeps existing batch tests/golden behavior working.
- **`AdapterFrontend`** (new) implements `onStop` by: building a `stopped` event (reason; current
  `Instr::loc` → path+line via the `SourceManager`; `buildBacktrace()` → frames; current frame
  `regs` + `Function::valueNames` → locals), writing it as JSON to the control fd, then **blocking
  on a read of the next command from stdin** and translating it to a `DebugAction`
  (continue/stepOver/stepIn/stepOut/terminate). `applyDebugAction` already maps these to the existing
  step machinery.

`RunConfig` gains `DebugFrontend *frontend` next to the existing `debug`/`debugScript`
(`include/viper/vm/VM.hpp:74-75`). `pauseOrAdvanceDebugScript` becomes:
`return frontend ? applyDebugAction(frontend->onStop(...)) : <existing script-or-halt path>`.

### Variable inspection

At a stop, the current frame is `execStack.back()->fr`. For each SSA id `i` with non-empty
`func->valueNames[i]`, emit `{name: valueNames[i], value: format(regs[i], type), type}`. Type-aware
formatting (i64/f64/str/ptr from the `Slot` union + the instruction/param type). First cut: all
named values defined at/before the current ip ("best-effort liveness"); refine later. This is the
*only* genuinely new data-shaping code; everything feeding it already exists.

### Async pause (single-threaded adapter)

Keep the adapter single-threaded: at a stop it blocks for the next command (natural). For **async
pause** while running, reuse the existing `RunConfig::pollCallback` / `interruptEveryN`
(`VM.hpp` `PollConfig`) — the callback does a *non-blocking* stdin peek every N instructions; if a
`pause` command is waiting, request a stop at the next instruction. No threads, no shared-state
races.

## Protocol (minimal, DAP-inspired; event shapes match the existing IDE contract)

Commands (IDE→adapter, one JSON object per line on stdin):
`setBreakpoints {path, lines[]}` · `launch {}` · `continue` · `stepOver` · `stepIn` · `stepOut` ·
`pause` · `terminate` · (later) `evaluate {expr, frameId}`.

Events (adapter→IDE, one JSON object per line on stdout) — **keys identical to today's
`rt_debug_protocol` shapes** so `debug_session.Capture`/`FormatConsole` need minimal change:
- `{type:"initialized", reason:"launch", path, line}`
- `{type:"stopped", reason:"breakpoint|step|pause|exception", path, line, frames:[{id,name,path,line}], locals:[{name,value,type}]}`
- `{type:"output", category:"stdout|stderr", text}`
- `{type:"terminated", reason:"exit|crash|terminated", exitCode}`

## IDE-side changes

- **New transport client** in `debug_session.zia`: `Launch` spawns `viper run --debug-adapter <file>`
  via `Process.StartWithEnv`, sends `setBreakpoints` then `launch` on stdin; `Continue/StepOver/…`
  write the matching command. Replaces the in-process `Viper.Debug.Protocol.*` calls.
- **Async poll**: add `DebugUpdate(shell, …, session)` to the main loop next to
  `build_commands.UpdateJob` (`main.zia`), reading newline-framed events from the child stdout each
  frame and driving the existing `Publish`/`OpenStopLocation`/`FormatConsole` (`debug_commands.zia`).
  The protocol becomes async (command now, `stopped` later) — exactly the build-job model.
- **`Process.WriteStdin`** binding (new) over `rt_process_write_stdin`.
- **Breakpoints**: `breakpoints.zia` already stores/persists path+line and draws gutter icons;
  toggling during a session sends `setBreakpoints`.
- **Un-gate**: revert Phase 0.3 "Debug Preview" labels (menu/toolbar/status) once real.
- The `rt_debug_protocol.*` placeholder and its `Viper.Debug.Protocol` bindings can be retired after
  parity.

## Phasing (each independently shippable + headlessly testable)

- **3a — Transport + launch skeleton.** Add `rt_process_write_stdin` (+binding+test). Add
  `viper run --debug-adapter`: compiles file→IL (reuse), runs under VM, captures debuggee
  stdout/stderr → `output` events, emits `initialized`/`terminated`. IDE: async poll + program
  output in the debug console. *Milestone: run under the debugger, see real output, see it exit.*
- **3b — Breakpoints + stop/continue.** `AdapterFrontend.onStop` emits `stopped` with path/line;
  gutter breakpoints → `setBreakpoints` → `addBreakSrcLine`; `continue` resumes. *Milestone: hit a
  breakpoint, editor jumps to the line, continue.*
- **3c — Stepping.** Wire `stepOver/stepIn/stepOut` to `applyDebugAction` (machinery already exists).
- **3d — Call stack + variables.** `buildBacktrace()` → frames panel; `regs`+`valueNames` → locals
  panel with type-aware formatting.
- **3e — Polish.** Restart; stop-on-unhandled-trap (hook `TrapDispatchSignal` → `exception` stop with
  the failing line); conditional breakpoints; hover-evaluate; multi-file stepping.

## Testing

The adapter is a process speaking JSON over stdio → **fully testable headlessly** (no GUI). A
`tools`-labeled test drives `viper run --debug-adapter` with a fixture program + breakpoints, writes
commands, and asserts the `stopped` lines, frames, and locals. Add VM-level unit tests for
`AdapterFrontend` action translation and the `regs`+`valueNames` → locals formatter. Existing
`DebugScript`/golden behavior stays green via the unchanged `ScriptedFrontend`.

## Risks / decisions

- **Debug = interpreted (VM), not native.** Accepted; standard debug/release split.
- **Liveness of named locals** is best-effort (SSA temps don't perfectly map to user variables for
  all sub-expressions); `valueNames` covers the user-named ones, which is what matters.
- **stdout capture** is the one fiddly cross-platform bit; socket fallback documented.
- **Determinism**: the debug path must not alter program semantics — the hooks only observe and gate
  progress, matching the existing `processDebugControl` contract.
- **Scope is a refactor + adapter + async client + one runtime primitive**, not a new engine —
  materially smaller than a from-scratch debugger because the VM is already debug-aware.

## Critical files

VM: `src/vm/debug/VMDebug.cpp` (seam), `Debug.cpp`/`Debug.hpp` (breakpoints), `DebugScript.*`
(model to generalize), `VM_DebugUtils.cpp` (`buildBacktrace`), `include/viper/vm/VM.hpp` (`RunConfig`).
Launch: `src/tools/viper/cmd_run.cpp` (`--debug-vm`/new `--debug-adapter`). IL: `il/core/Instr.hpp`
(`loc`), `il/core/Function.hpp:79` (`valueNames`). Runtime: `src/runtime/system/rt_process.c`
(+`write_stdin`), `src/il/runtime/runtime.def` (binding). IDE: `viperide/src/build/debug_session.zia`,
`commands/debug_commands.zia`, `build/breakpoints.zia`, `main.zia` (poll loop).
