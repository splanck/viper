# Viper Web Server Demo - Bug Tracker

This file tracks all bugs discovered while building the web server demo.

## Bug Format
- **ID**: BUG-XXX
- **Severity**: Critical / High / Medium / Low
- **Status**: Open / Fixed / Won't Fix
- **Component**: Zia Frontend / Viper Runtime / Backend
- **Description**: What went wrong
- **Steps to Reproduce**: How to trigger it
- **Expected**: What should happen
- **Actual**: What actually happened
- **Root Cause**: Why it happened (if known)
- **Fix**: How it was resolved

---

## Bugs

### BUG-002: Zia doesn't support function pointers for Thread.Start

- **ID**: BUG-002
- **Severity**: High
- **Status**: Fixed
- **Component**: Zia Frontend
- **Description**: When trying to pass a function reference to `Viper.Threads.Thread.Start()`, the function reference is not being correctly converted to a function pointer. Instead, it appears to be interpreted as 0 (null), causing "Thread.Start: invalid entry" trap.
- **Steps to Reproduce**:
  1. Create a Zia program with a worker function and try to start a thread:
     ```zia
     func workerThread(arg: Ptr) {
         // do work
     }

     func main() {
         var t = Viper.Threads.Thread.Start(workerThread, null);
     }
     ```
  2. Run the program
- **Expected**: Thread should start with workerThread as the entry point
- **Actual**: Trap with "Thread.Start: invalid entry"
- **Root Cause**: `lowerIdent()` in Lowerer_Expr.cpp returned `Value::constInt(0)` for unknown identifiers instead of checking if the identifier is a function. Function names like `workerThread` were not being converted to `Value::global(name)` to get their address.
- **Fix**: Modified `lowerIdent()` in `src/frontends/zia/Lowerer_Expr.cpp` to check if an identifier refers to a defined function (via `definedFunctions_`) or an extern function (via `sema_.findExternFunction()`), and if so return `Value::global(name)` with Ptr type. The IL now correctly generates `@workerThread` for function references.
- **Note**: While this fix enables correct IL generation for function pointers, threading in the VM has a separate issue (see BUG-003).

---

### BUG-003: VM threading crashes or hangs

- **ID**: BUG-003
- **Severity**: High
- **Status**: Fixed
- **Component**: VM / BytecodeVM
- **Description**: When running programs with threading through the VM (both Zia and BASIC), the program either crashes (SIGSEGV at address 0x0) or hangs indefinitely. This occurs even when function pointers are correctly resolved (BUG-002 is fixed).
- **Steps to Reproduce**:
  1. Create a simple threading program in Zia or BASIC:
     ```zia
     func worker(arg: Ptr) { }
     func main() {
         var t = Viper.Threads.Thread.Start(worker, null);
         Viper.Threads.Thread.Join(t);
     }
     ```
  2. Run through VM: `zia program.zia`
  3. Program crashes or hangs
- **Expected**: Thread should start, run, and join successfully
- **Actual**: Crash with SIGSEGV (exit code 139) or hang
- **Root Cause**: The BytecodeVM (default execution mode) was not setting a thread-local context like the standard VM does. When `Thread.Start` was called, the handler checked `activeVMInstance()` which returned nullptr because BytecodeVM is a separate class. This caused the handler to fall back to calling `rt_thread_start` directly with the function pointer - but the function pointer was an IL function object or tagged bytecode function index, not a native function pointer, causing a crash.
- **Fix**:
  1. Added thread-local tracking for active BytecodeVM (`tlsActiveBytecodeVM`, `tlsActiveBytecodeModule`) in BytecodeVM.cpp
  2. Created `ActiveBytecodeVMGuard` RAII class to set the thread-local during bytecode execution
  3. Implemented a unified `Thread.Start` handler in BytecodeVM.cpp that:
     - First checks for standard VM via `activeVMInstance()` and handles it
     - Then checks for BytecodeVM via `activeBytecodeVMInstance()` and handles it
     - Falls back to direct `rt_thread_start` only for native code paths
  4. The bytecode handler resolves tagged function pointers (high bit set, lower bits are function index) to `BytecodeFunction*` and spawns a new BytecodeVM on the child thread

---

### BUG-004: VM string store/load corrupts dynamically created strings

- **ID**: BUG-004
- **Severity**: High
- **Status**: Fixed
- **Component**: VM / Runtime
- **Description**: When a string returned from a runtime function (like `Path.Join` or string concatenation) is stored to a variable and then loaded, the string value becomes corrupted or invalid.
- **Steps to Reproduce**:
  1. Concatenate two strings and store the result
  2. Use the stored string
  3. The string is corrupted
- **Expected**: String operations should work correctly
- **Actual**: String values are corrupted after concatenation
- **Root Cause**: The `rt_concat` runtime function releases both of its string arguments after use, but the RuntimeBridge was using `DirectHandler` which passes arguments directly without retaining them. This caused use-after-free when the VM still had references to the input strings.
- **Fix**: Modified `src/tools/rtgen/rtgen.cpp` to detect functions like `rt_concat` that consume their string arguments and use `ConsumingStringHandler` instead of `DirectHandler`. The `ConsumingStringHandler` template retains string arguments (via `rt_string_ref`) before the call, ensuring the strings remain valid during the function call.
- **Files Modified**:
  - `src/tools/rtgen/rtgen.cpp`: Added `needsConsumingStringHandler()` and `buildConsumingStringHandlerExpr()` functions
  - `src/il/runtime/generated/RuntimeSignatures.inc`: Regenerated with ConsumingStringHandler for rt_concat

---

### BUG-001: Stdout not flushed without --trace flag

- **ID**: BUG-001
- **Severity**: Low
- **Status**: Fixed
- **Component**: Viper Runtime
- **Description**: When running a Zia program without the `--trace` flag, stdout output from `Viper.Terminal.Say()` is not immediately visible. Output appears to be buffered and may not be flushed until the program terminates or until a significant amount of output accumulates.
- **Steps to Reproduce**:
  1. Create a Zia program with multiple `Viper.Terminal.Say()` calls followed by a blocking operation (like `TcpServer.Accept()`)
  2. Run without `--trace`: `zia program.zia`
  3. Observe that output before the blocking operation is not visible
  4. Run with `--trace`: `zia program.zia --trace`
  5. Observe that all output is visible
- **Expected**: Output from `Say()` should appear immediately regardless of trace mode
- **Actual**: Output is buffered and only appears with `--trace` flag
- **Root Cause**: The runtime configures stdout for full buffering (`_IOFBF`) for performance, but `rt_term_say()` and related functions weren't calling `rt_output_flush()` after writing. This left output sitting in the buffer.
- **Fix**: Modified `src/runtime/rt_io.c` to add `rt_output_flush()` calls to `rt_term_say()`, `rt_term_say_i64()`, `rt_term_say_f64()`, and `rt_term_say_bool()`. This ensures output is visible immediately after each Say() call.

