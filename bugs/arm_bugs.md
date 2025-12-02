# Viper ARM64 Backend Bugs

This file tracks bugs discovered in the ARM64 (AArch64) native code generation backend.

## Bug Template

### BUG-ARM-XXX: [Title]
- **Status**: Open / Fixed / Won't Fix
- **Discovered**: [Date]
- **Fixed**: [Date] (if applicable)
- **Severity**: Critical / High / Medium / Low
- **Component**: [e.g., LowerILToMIR, AsmEmitter, RegAllocLinear]
- **Description**: [What happened]
- **Steps to Reproduce**: [Code snippet or steps]
- **Expected**: [What should happen]
- **Actual**: [What actually happened]
- **Root Cause**: [Technical analysis]
- **Fix**: [Description of fix applied]

---

## Open Bugs

(None)

---

## Fixed Bugs

### BUG-ARM-001: Control Flow Instructions Emitted Before Value Computations
- **Status**: Fixed
- **Discovered**: 2025-12-02
- **Fixed**: 2025-12-02
- **Severity**: Critical
- **Component**: LowerILToMIR.cpp
- **Description**: In multi-block functions, control flow terminators (cbr, br) were lowered and appended to MIR blocks BEFORE the generic instruction lowering loop ran. This caused branches to appear before the values they depend on were computed.
- **Steps to Reproduce**:
  ```
  $ cat /tmp/test.il
  il 0.1
  func @main() -> i64 {
  entry:
    %x = add 5, 3
    %cond = scmp_gt %x, 7
    cbr %cond, then, else_bb
  then:
    ret 1
  else_bb:
    ret 0
  }
  ```
- **Expected**: Assembly should compute `%x` and `%cond` before branching
- **Actual**: Branch instructions appeared BEFORE computation instructions
- **Root Cause**:
  In `LowerILToMIR.cpp`, the lowering was done in two separate loops:
  1. Control flow terminator loop (ran first)
  2. Generic instruction lowering loop (ran second, appended after terminators)

  The control flow loop ran BEFORE the generic loop, causing terminators to be emitted first.

- **Fix**: Removed the premature terminator loop and added terminator lowering at the END of the generic instruction lowering loop. This ensures all non-terminator instructions are lowered first, then terminators are appended last.
  - Modified: `src/codegen/aarch64/LowerILToMIR.cpp`
  - Key change: Moved terminator handling (br, cbr, trap) to after the generic instruction loop

---

### BUG-ARM-002: Missing Underscore Prefix for macOS Symbols
- **Status**: Fixed
- **Discovered**: 2025-12-02
- **Fixed**: 2025-12-02
- **Severity**: Medium
- **Component**: AsmEmitter.cpp, GenAArch64Dispatch.cmake
- **Description**: On macOS (Darwin), C symbols require an underscore prefix (e.g., `_main` not `main`). The ARM64 backend emitted symbols without this prefix, causing linker errors.
- **Steps to Reproduce**:
  ```
  $ cat /tmp/simple.il
  il 0.1
  func @main() -> i64 {
  entry:
    ret 42
  }

  $ ./build/src/tools/ilc/ilc codegen arm64 /tmp/simple.il -S /tmp/simple.s
  $ as /tmp/simple.s -o /tmp/simple.o
  $ clang++ /tmp/simple.o -o /tmp/simple
  ```
- **Expected**: Linking succeeds, executable runs and returns 42
- **Actual**: Linker error: `Undefined symbols for architecture arm64: "_main"`
- **Root Cause**: The emitter intentionally did not add the underscore prefix, expecting downstream tools to handle it. However, no such tool existed in the pipeline.
- **Fix**: Added `mangleSymbol()` helper function in `AsmEmitter.cpp` that conditionally adds underscore prefix on Darwin (`__APPLE__`). Updated the CMake generator (`GenAArch64Dispatch.cmake`) to use `mangleCallTarget()` for `bl` instructions and `mangleSymbol()` for `adrp`/`add` page offset instructions.
  - Modified: `src/codegen/aarch64/AsmEmitter.cpp`
  - Modified: `cmake/GenAArch64Dispatch.cmake`

---

### BUG-ARM-003: Runtime Symbol Name Mismatch
- **Status**: Fixed
- **Discovered**: 2025-12-02
- **Fixed**: 2025-12-02
- **Severity**: High
- **Component**: AsmEmitter.cpp
- **Description**: Generated code emitted calls to IL-style symbol names (e.g., `Viper.Console.PrintI64`) but the runtime library exports C-style names (e.g., `_rt_print_i64`). This caused undefined symbol errors at link time.
- **Steps to Reproduce**:
  ```
  $ cat /tmp/print_test.il
  il 0.1
  extern @Viper.Console.PrintI64(i64) -> void

  func @main() -> i64 {
  entry:
    call @Viper.Console.PrintI64(42)
    ret 0
  }
  ```
- **Expected**: Generated code should call `_rt_print_i64` (the actual runtime symbol)
- **Actual**: Generated code called `Viper.Console.PrintI64` which doesn't exist in the runtime
- **Root Cause**: The IL uses namespaced extern names for clarity and type checking. The VM resolves these at runtime via `RuntimeSignatures`, but the native codegen backend passed IL names directly to `bl` instructions without mapping.
- **Fix**: Added `mapRuntimeSymbol()` function in `AsmEmitter.cpp` that translates IL extern names (like `Viper.Console.PrintI64`) to C runtime names (like `rt_print_i64`). The `mangleCallTarget()` function combines this mapping with platform mangling.
  - Modified: `src/codegen/aarch64/AsmEmitter.cpp`
  - Key mappings include Console, Strings, Math, Random, Collections, IO, and other runtime functions

---

## Investigation Notes

### Working Capabilities (2025-12-02)
- Simple arithmetic programs: **WORKING**
- Control flow (br, cbr, switch): **WORKING** (fixed)
- Register allocation and spilling: **WORKING**
- Wide immediate handling (values > 16 bits): **WORKING**
- Prologue/epilogue generation: **WORKING**
- Runtime function calls: **WORKING** (fixed)
- macOS binary generation: **WORKING** (fixed)
- All 39 unit tests pass (test_codegen_arm64_*)

### Test Commands
```bash
# Build
cmake -S . -B build && cmake --build build -j

# Run ARM64 tests
ctest --test-dir build -R arm64 --output-on-failure

# Generate assembly for an IL file
./build/src/tools/ilc/ilc codegen arm64 <input.il> -S <output.s>

# Assemble and link on macOS (now works without manual fixes!)
as output.s -o output.o
clang++ output.o build/src/runtime/libviper_runtime.a -o output
./output
```

### Example: Working Control Flow Program
```bash
$ cat /tmp/test_cf.il
il 0.1
func @main() -> i64 {
entry:
  %x = add 5, 3
  %cond = scmp_gt %x, 7
  cbr %cond, then, else_bb
then():
  ret 1
else_bb():
  ret 0
}

$ ./build/src/tools/ilc/ilc codegen arm64 /tmp/test_cf.il -S /tmp/test_cf.s
$ as /tmp/test_cf.s -o /tmp/test_cf.o
$ clang++ /tmp/test_cf.o -o /tmp/test_cf
$ ./test_cf; echo $?
1  # Correct! 5+3=8 > 7
```

### Example: Working Runtime Call
```bash
$ cat /tmp/test_print.il
il 0.1
extern @Viper.Console.PrintI64(i64) -> void
func @main() -> i64 {
entry:
  call @Viper.Console.PrintI64(42)
  ret 0
}

$ ./build/src/tools/ilc/ilc codegen arm64 /tmp/test_print.il -S /tmp/test_print.s
$ as /tmp/test_print.s -o /tmp/test_print.o
$ clang++ /tmp/test_print.o build/src/runtime/libviper_runtime.a -o /tmp/test_print
$ ./test_print
42
```
