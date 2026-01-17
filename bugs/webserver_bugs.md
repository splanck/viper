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
- **Status**: Open
- **Component**: VM
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
- **Observations**:
  - Running with `--trace` flag makes the program work correctly
  - The crash occurs in thread #2 at address 0x0
  - Native C runtime threading tests pass
  - Issue affects both Zia and BASIC frontends
- **Root Cause**: Unknown. Likely a race condition or thread synchronization issue in the VM's thread handling. The `--trace` flag slows execution enough to avoid the race.
- **Workaround**: Use `--trace` flag when running threaded programs
- **Impact**: Multithreading in VM-executed programs is unreliable

---

### BUG-004: VM string store/load corrupts dynamically created strings

- **ID**: BUG-004
- **Severity**: High
- **Status**: Open
- **Component**: VM
- **Description**: When a string returned from a runtime function (like `Path.Join`) is stored to a variable and then loaded, the string value becomes corrupted or invalid. Passing the string directly to another function (without storing) works correctly.
- **Steps to Reproduce**:
  1. Call `Path.Join` and store the result in a variable
  2. Pass that variable to `File.Exists`
  3. The file is not found even though it exists
- **Expected**: File.Exists should find the file
- **Actual**: File.Exists returns false for valid paths
- **Working Example**:
  ```zia
  // This works - direct call without storing
  var exists = Viper.IO.File.Exists(Viper.IO.Path.Join("/etc", "passwd"));
  ```
- **Failing Example**:
  ```zia
  // This fails - storing to variable first
  var path = Viper.IO.Path.Join("/etc", "passwd");
  var exists = Viper.IO.File.Exists(path);
  ```
- **Root Cause**: Unknown. Likely an issue with how the VM stores string values to stack slots (`alloca`/`store`) and loads them back (`load`). The string's internal data pointer may not survive the store/load cycle correctly.
- **Workaround**: Avoid storing runtime-returned strings to variables before passing them to other functions. Use direct function chaining instead.
- **Impact**: Significant limitation on code structure when working with file paths and other runtime-generated strings.

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

