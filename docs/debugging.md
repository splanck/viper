# Viper Debugging Guide

This guide covers all debugging features available in the Viper platform, including both the VM interpreter and the Zia/BASIC frontends.

---

## Quick Reference

| Goal | Command / Flag |
|------|---------------|
| Trace every IL instruction | `--trace=il` |
| Trace source locations | `--trace=src` |
| Break at source line | `--break-src main.zia:42` |
| Break at block label | `--break entry` |
| Watch a variable | `--watch x` |
| Limit execution steps | `--max-steps 10000` |
| Single-step on entry | `--step` |
| Dump IL on trap | `--dump-trap` |
| Run debug script | `--debug-cmds script.dbg` |
| Dump token stream | `--dump-tokens` |
| Dump AST after parsing | `--dump-ast` |
| Dump AST after sema | `--dump-sema-ast` |
| Dump IL after lowering | `--dump-il` |
| Dump IL after optimization | `--dump-il-opt` |
| Dump IL per-pass | `--dump-il-passes` |

---

## 1. Tracing

Tracing prints detailed execution information to stderr as the VM runs each instruction.

### IL Trace Mode

```sh
viper -run program.il --trace=il
```

Output format:
```
[IL] fn=@main blk=L3 ip=#5 op=add 10, 20 -> %t5
```

Each line shows: function name, block label, instruction index, opcode, operands, and result register.

### Source Trace Mode

```sh
viper -run program.il --trace=src
```

Output format:
```
[SRC] main.zia:42:10 (fn=@main blk=L3 ip=#5)
```

Shows the original source file, line, and column for each executed instruction. Requires that the IL was compiled with source location information (Zia and BASIC both propagate source locations to IL).

### Notes

- Both trace modes write to stderr, so program output on stdout remains clean.
- Tracing is **all-or-nothing** per mode; there is no per-function or per-file filter.
- In `viper front basic` and `viper front zia`, trace and debug modes disable IL optimization to preserve the instruction-to-source mapping.

---

## 2. Breakpoints

Breakpoints pause execution at a specific point and return control to the debug controller.

### Source-Line Breakpoints

```sh
viper -run program.il --break-src main.zia:42
```

Pauses before executing any instruction originating from line 42 of `main.zia`. File paths are normalized automatically (forward slashes, case-insensitive on Windows).

### Block-Label Breakpoints

```sh
viper -run program.il --break entry
```

Pauses when the VM enters a block with the given label. Also supports `file:line` format as a shorthand for `--break-src`:

```sh
viper -run program.il --break main.zia:42
```

### Multiple Breakpoints

Specify multiple `--break` or `--break-src` flags:

```sh
viper -run program.il --break-src main.zia:10 --break-src util.zia:25
```

### Breakpoint Coalescing

Repeated hits on the same source line are coalesced — the debugger does not spam notifications for every IL instruction that maps to the same line.

---

## 3. Variable Watches

Watches report every time a named variable is stored to during execution.

```sh
viper -run program.il --watch x --watch total
```

Output format:
```
[WATCH] x=I64:42 (fn=@main blk=L3 ip=#5)
```

### How It Works

- Watches use an O(1) fast-path via a symbol-to-watch-ID map.
- Only reports when the value **changes** from the previous watched value (deduplication).
- Multiple watches can be active simultaneously.

### Limitations

- Watch names must match the IL register/variable names used by the lowerer (e.g., `x` for a local named `x`).
- There is currently no source-name-to-IL-register mapping, so you may need to inspect the IL output (`--dump-il` or `-emit-il`) to find the correct name.

---

## 4. Memory Watches

Memory watches monitor reads/writes to specific memory address ranges. This is primarily useful for debugging the runtime or tracking heap corruption.

### Programmatic API

```cpp
Runner runner(module);
runner.addMemWatch(addr, size, "my-tag");
// ... run ...
auto hits = runner.drainMemWatchHits();
for (auto &hit : hits) {
    // hit.tag, hit.addr, hit.size
}
runner.removeMemWatch(addr, size, "my-tag");
```

Memory watches use binary search for efficient lookup when 8+ watches are active.

---

## 5. Stepping

### Single-Step on Entry

```sh
viper -run program.il --step
```

Executes one instruction at the entry point, then continues to completion. Useful for verifying the program starts correctly.

### Step Budget

```sh
viper -run program.il --max-steps 10000
```

Limits the total number of IL instructions executed. When exceeded, execution stops with `RunStatus::StepBudgetExceeded`. Set to 0 (default) for unlimited.

### Debug Script Automation

```sh
viper -run program.il --debug-cmds script.dbg
```

The debug script file contains one command per line:

| Command | Description |
|---------|-------------|
| `continue` | Resume execution until next breakpoint or halt |
| `step` | Execute exactly 1 instruction |
| `step N` | Execute exactly N instructions |

Example `script.dbg`:
```
step 5
continue
step 10
continue
```

Empty lines and unrecognized commands are ignored with a `[DEBUG]` message to stderr.

### Programmatic Stepping (C++ API)

```cpp
Runner runner(module);
// Execute one instruction
auto result = runner.step();
// result.status: Advanced, Halted, BreakpointHit, Trapped, Paused

// Continue until terminal state
auto status = runner.continueRun();
// status: Halted, BreakpointHit, Trapped, Paused, StepBudgetExceeded
```

### Current Limitations

- **Step = 1 IL instruction**, not 1 source line. A single source line may compile to many IL instructions.
- **No step-over**: stepping into function calls cannot be skipped.
- **No step-out**: there is no mechanism to run until the current function returns.

---

## 6. Error Reporting

### Diagnostic Format

All compiler diagnostics follow this format:

```
<path>:<line>:<column>: <severity>[<code>]: <message>
```

For example:
```
main.zia:42:15: error[V3000]: Type mismatch: expected Integer, got String
 42 | var x: Integer = "hello";
    |                  ^
```

Severity levels: `note`, `warning`, `error`.

Diagnostic codes are prefixed by subsystem:
- `V3xxx` — Zia frontend (semantic analysis)
- `B1xxx` — BASIC frontend
- `IL0xx` — IL verification

### Source Snippets

When a source manager is available and the source file can be loaded, diagnostics include the offending source line with a caret (`^`) pointing to the error column. Tab characters in the source are preserved for correct alignment.

### Trap Format

Runtime traps (VM errors) use this format:

```
Trap @function:block#ip line N: Kind (code=C)
```

For example:
```
Trap @processRow:L3#2 line 145: Bounds (code=0)
```

### Trap Kinds

| Kind | Code | Description |
|------|------|-------------|
| DivideByZero | 0 | Integer division or remainder by zero |
| Overflow | 1 | Arithmetic or conversion overflow |
| InvalidCast | 2 | Invalid cast or type conversion |
| DomainError | 3 | Semantic domain violation |
| Bounds | 4 | Array index out of bounds |
| FileNotFound | 5 | File not found |
| EOF | 6 | Unexpected end of file |
| IOError | 7 | I/O failure |
| InvalidOperation | 8 | Invalid operation for current state |
| RuntimeError | 9 | General runtime error |
| Interrupt | 10 | External interrupt |

Use `--dump-trap` to ensure trap messages are printed to stderr even when the program handles them internally.

---

## 7. Runtime Logging

### Viper.Log API (Zia / BASIC)

The runtime provides a leveled logging system accessible from both Zia and BASIC:

```
Viper.Log.Debug("detailed info")
Viper.Log.Info("normal info")
Viper.Log.Warn("potential issue")
Viper.Log.Error("something failed")
```

Output format:
```
[INFO] 14:30:05 normal info
```

Log levels (lowest to highest): DEBUG (0), INFO (1), WARN (2), ERROR (3), OFF (4).

Default level: INFO. Messages below the current level are suppressed.

### Viper.Core.Diagnostics API

The runtime assertion library provides 12 assertion variants for test and debug use:

| Function | Description |
|----------|-------------|
| `Assert(condition)` | Assert condition is true |
| `AssertEq(a, b)` | Assert values are equal |
| `AssertNeq(a, b)` | Assert values are not equal |
| `AssertEqNum(a, b)` | Assert numeric equality |
| `AssertEqStr(a, b)` | Assert string equality |
| `AssertNull(val)` | Assert value is null |
| `AssertNotNull(val)` | Assert value is non-null |
| `AssertFail(msg)` | Unconditionally fail with message |
| `AssertGt(a, b)` | Assert a > b |
| `AssertLt(a, b)` | Assert a < b |
| `AssertGte(a, b)` | Assert a >= b |
| `AssertLte(a, b)` | Assert a <= b |
| `Trap(msg)` | Raise a runtime trap with message |

### Debug Print

For quick debugging, use:
```
Viper.Debug.PrintI32(value)    // Print integer to stderr
Viper.Debug.PrintStr(text)     // Print string to stderr
```

These flush immediately for crash safety.

---

## 8. Pipeline Dump Flags

The compiler supports dump flags for inspecting intermediate results at every stage of the pipeline. All dumps go to **stderr** so they don't interfere with program output or `-emit-il`. These work with `viper run`, `viper front zia`, and `viper front basic`.

### Token Stream

```sh
viper run --dump-tokens program.zia
viper front basic -run program.bas --dump-tokens
```

Prints every token produced by the lexer with location, kind, text, and literal values:

```
=== Zia Token Stream ===
1:1     module  "module"
1:8     identifier      "Test"
2:1     func    "func"
2:6     identifier      "start"
3:27    integer "42"    value=42
5:1     eof
=== End Token Stream ===
```

### AST Dump

```sh
viper run --dump-ast program.zia
```

Prints the parsed AST (abstract syntax tree) as an indented tree. For Zia, this includes source locations, node kinds, operators, and literal values:

```
=== AST after parsing ===
ModuleDecl "Test" (1:1)
  FunctionDecl "start" (2:1)
    Body:
      BlockStmt (2:14)
        ExprStmt (3:5)
          CallExpr (3:26)
            Callee:
              FieldExpr "SayInt" (3:19)
            Args:
              Arg:
                IntLiteral 42 (3:27)
=== End AST ===
```

For BASIC, the AST uses the existing `AstPrinter` format.

### AST Dump After Semantic Analysis (Zia Only)

```sh
viper run --dump-sema-ast program.zia
```

Prints the AST after semantic analysis has run. This is useful for seeing what sema has annotated or transformed — comparing `--dump-ast` with `--dump-sema-ast` shows what the semantic pass changed.

### IL After Lowering

```sh
viper run --dump-il program.zia
```

Prints the IL module immediately after lowering from the AST, before any optimization:

```
=== IL after lowering ===
il 0.2.0
extern @Viper.Terminal.SayInt(i64) -> void
func @main() -> void {
entry_0:
  .loc 1 3 26
  call @Viper.Terminal.SayInt(42)
  .loc 1 2 1
  ret
}
=== End IL ===
```

### IL Per-Pass Dump

```sh
viper run -O1 --dump-il-passes program.zia
```

Prints the full IL module before and after each optimization pass. Requires `-O1` or `-O2` (at `-O0`, the only passes are SimplifyCFG and DCE). Uses the PassManager's built-in instrumentation hooks:

```
*** IR before pass 'simplify-cfg' ***
...
*** IR after pass 'simplify-cfg' ***
...
*** IR before pass 'mem2reg' ***
...
```

### IL After Optimization

```sh
viper run -O1 --dump-il-opt program.zia
```

Prints the IL module after the entire optimization pipeline has completed:

```
=== IL after optimization (O1) ===
...
=== End IL ===
```

### Combining Flags

All dump flags can be combined freely. They print in pipeline order:

```sh
viper run --dump-tokens --dump-ast --dump-il --dump-il-opt program.zia
```

This prints the token stream, then the AST, then the pre-optimization IL, then the post-optimization IL.

### Programmatic API

The same flags are available in `CompilerOptions` (Zia) and `BasicCompilerOptions` (BASIC):

| Option | CLI Flag | Description |
|--------|----------|-------------|
| `dumpTokens` | `--dump-tokens` | Dump lexer token stream |
| `dumpAst` | `--dump-ast` | Dump AST after parsing |
| `dumpSemaAst` | `--dump-sema-ast` | Dump AST after sema (Zia only) |
| `dumpIL` | `--dump-il` | Dump IL after lowering |
| `dumpILOpt` | `--dump-il-opt` | Dump IL after optimization |
| `dumpILPasses` | `--dump-il-passes` | Dump IL before/after each pass |

### Safety Checks

All enabled by default. Can be toggled via `CompilerOptions`:

| Option | Default | Description |
|--------|---------|-------------|
| `boundsChecks` | true | Emit array bounds checking code |
| `overflowChecks` | true | Emit arithmetic overflow checks |
| `nullChecks` | true | Emit null pointer checks |

When enabled, these generate IL instructions (`IdxChk`, `SDivChk0`, etc.) that trap on violations rather than producing undefined behavior.

### Optimization Levels

| Level | Description |
|-------|-------------|
| O0 | Minimal optimization (SimplifyCFG + DCE only) |
| O1 | Standard optimizations |
| O2 | Aggressive optimizations |

Debug and trace modes automatically disable optimization to preserve the instruction-to-source mapping.

### Compiler Phase Timing

The `debugTime()` helper in the Zia compiler prints elapsed time for each compilation phase:
- Lexing
- Parsing
- Import resolution
- Semantic analysis
- IL lowering

---

## 9. IL Inspection

### Serialization

IL modules can be serialized in two modes:

- **Pretty mode** — Human-readable with indentation and comments
- **Canonical mode** — Deterministic output suitable for golden tests

Use `-emit-il` to output the final IL module to stdout, or `--dump-il` / `--dump-il-opt` to print to stderr at specific pipeline stages:

```sh
viper front zia -emit-il program.zia          # Final IL to stdout
viper front zia -run program.zia --dump-il    # IL after lowering to stderr
```

### Verification

The IL verifier (`Verifier::verify()`) checks:
- Type consistency across instructions
- Control flow graph validity (block connectivity, terminator presence)
- Exception handling structure (EhEntry/EhExit pairing)
- External function declaration correctness
- Global variable definitions

Verification runs automatically during compilation. Invalid IL is reported as diagnostics.

---

## 10. VM Debug Hooks

### DebugCtrl

Every VM instance has a `DebugCtrl` member that provides:
- Breakpoint management (block-label and source-line)
- Variable watch tracking
- Memory watch monitoring

Fast-path flags (`fastDebugBreak_`, `fastDebugMemWatch_`) ensure **zero overhead** when no debug features are active.

### TraceSink Callbacks

The trace system fires callbacks during execution:
- `onFramePrepared()` — New function frame created
- `onStep()` — Instruction about to execute
- `onTailCall()` — Tail call optimization activated

### Host Polling

For interactive applications, configure periodic callbacks:

```cpp
RunConfig config;
config.interruptEveryN = 1000;  // Check every 1000 instructions
config.pollCallback = [&]() -> bool {
    return shouldContinue;  // Return false to pause
};
```

---

## 11. Exception Handling

### IL Exception Model

Viper IL uses structured exception handling with `EhEntry`/`EhExit` opcodes:

```
EhEntry handler_block
  ; protected code
EhExit
```

Handler blocks receive `(%err: Error, %tok: ResumeTok)` parameters.

### Resume Variants

- `ResumeSame` — Re-raise the same exception
- `ResumeNext` — Continue after the protected region
- `ResumeLabel` — Jump to a specific block

### VM Exception Handling

When a trap occurs:
1. The VM searches the current frame's exception handler stack
2. If a handler is found, control transfers to it
3. If no handler exists, the trap propagates up the call stack
4. Unhandled traps call `rt_abort()` with diagnostic output

---

## 12. Benchmarking

### `viper bench` Command

```sh
viper bench program.il -n 5 --all
```

Runs the program multiple times across different VM dispatch strategies and reports timing:

```
BENCH program.il table instr=50000 time_ms=12 insns_per_sec=4166666
```

### Dispatch Strategies

| Flag | Strategy |
|------|----------|
| `--table` | Function table dispatch (standard VM) |
| `--switch` | Switch dispatch (standard VM) |
| `--threaded` | Threaded dispatch (standard VM) |
| `--bc-switch` | Bytecode VM, switch dispatch |
| `--bc-threaded` | Bytecode VM, threaded dispatch |
| `--all` | All strategies (default) |

### JSON Output

```sh
viper bench program.il --json
```

### Opcode Counting

When compiled with `VIPER_VM_OPCOUNTS=1` (default in Debug builds):

```cpp
Runner runner(module);
runner.run();
auto counts = runner.opcodeCounts();      // Per-opcode array
auto top = runner.topOpcodes(10);         // Top 10 by frequency
runner.resetOpcodeCounts();               // Reset counters
```

---

## 13. Execution Statistics

### Instruction Count

```sh
viper -run program.il --count
```

Output:
```
[SUMMARY] instr=142857
```

### Execution Time

```sh
viper -run program.il --time
```

Output:
```
[SUMMARY] time_ms=42
```

Both flags can be combined:

```sh
viper -run program.il --count --time
```

---

## 14. Programmatic Debugging API

### Complete Example

```cpp
#include "viper/vm/VM.hpp"

// Load module
auto module = loadModule("program.il");

// Configure runner
Runner runner(module);

// Set breakpoint
runner.setBreakpoint({file_id, 42, 0});

// Run to breakpoint
auto status = runner.continueRun();
if (status == RunStatus::BreakpointHit) {
    // Inspect state
    auto trap = runner.lastTrap();

    // Single-step
    auto step = runner.step();

    // Continue
    status = runner.continueRun();
}

// Check for errors
if (auto msg = runner.lastTrapMessage()) {
    std::cerr << *msg << "\n";
}

// Statistics
std::cout << "Instructions: " << runner.instructionCount() << "\n";
```

### Key Types

| Type | Description |
|------|-------------|
| `Runner` | Main VM execution controller |
| `Runner::StepResult` | Result of `step()` — status enum |
| `Runner::RunStatus` | Result of `continueRun()` — terminal state |
| `Runner::TrapInfo` | Trap details: kind, code, ip, line, function, block, message |
| `DebugCtrl` | Low-level breakpoint/watch controller |
| `TraceSink` | Trace output handler with file caching |
| `TraceConfig` | Trace mode and source manager configuration |
| `DebugScript` | File-based debug automation |

---

## 15. Known Limitations

| Area | Status | Notes |
|------|--------|-------|
| Source-line stepping | Not implemented | `step()` advances 1 IL instruction, not 1 source line |
| Step-over / step-out | Not implemented | No frame-depth-aware stepping |
| Full backtrace API | Not implemented | `execStack` is private; only single-frame `TrapInfo` exposed |
| Conditional breakpoints | Not implemented | No expression evaluation on break condition |
| Source-to-IL name mapping | Not implemented | Watches require IL register names |
| DWARF debug info | Not implemented | Native codegen has no debug info for GDB/LLDB |
| Debug Adapter Protocol | Not implemented | No IDE integration (VS Code, etc.) |
| Signal/crash handler | Not implemented | Native crashes produce no diagnostic output |
| In-VM profiling | Not implemented | No function-level timing or allocation tracking |
| Subsystem log filtering | Not implemented | VM trace and Viper.Log are all-or-nothing |
