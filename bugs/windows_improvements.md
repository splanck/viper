# Windows Improvements Report

Comprehensive review of the Viper codebase for Windows-specific issues, improvements, missing features, and optimization opportunities.

**Review Date:** 2026-01-18
**Files Reviewed:** 964 source files (excluding tests and viperdos)
**Method:** Individual file review by 10 parallel analysis agents

---

## Executive Summary

| Subsystem | Files Reviewed | Issues Found | Severity |
|-----------|---------------|--------------|----------|
| Bytecode | 7 | 2 | Low (performance) |
| aarch64 Codegen | 35 | 5 | Medium (not targeting Windows ARM64) |
| x86_64 Codegen | 45 | 0 | N/A - Well designed |
| Common/BASIC Frontend | ~60 | 0 | N/A - Well implemented |
| Zia Frontend | ~45 | 1 | Low |
| IL Subsystem | ~100 | 0 | N/A - Platform-agnostic |
| Runtime Library | ~40 | 8 | **CRITICAL** |
| VM | ~30 | 0 | N/A - Well designed |
| Support/Tools | ~25 | 2 | Medium |
| Lib/TUI | ~20 | 3 | Medium |

**Overall Assessment:** The core compiler infrastructure (IL, x86_64 codegen, VM, frontends) is well-designed for Windows. The **critical gaps** are in the runtime library (threading/synchronization completely missing on Windows) and the TUI library (missing console input handling).

---

## Critical Issues (Priority 1)

### RUNTIME-001: Thread Functions Unimplemented on Windows

**Severity:** CRITICAL
**File:** `src/runtime/rt_threads.c`
**Impact:** Any BASIC or Zia program using threads will crash on Windows

**Description:**
All threading functions trap with "unsupported on this platform" on Windows:
- `rt_thread_start`
- `rt_thread_join`
- `rt_thread_try_join`
- `rt_thread_join_for`
- `rt_thread_get_id`
- `rt_thread_get_is_alive`
- `rt_thread_sleep`
- `rt_thread_yield`

**Recommended Fix:**
Implement using Windows Thread API (`CreateThread`, `WaitForSingleObject`, etc.) or C11 `<threads.h>` (if MSVC version supports it).

---

### RUNTIME-002: Monitor Functions Unimplemented on Windows

**Severity:** CRITICAL
**File:** `src/runtime/rt_monitor.c`
**Impact:** Any BASIC or Zia program using synchronization will crash on Windows

**Description:**
All monitor/synchronization functions trap with "unsupported on this platform":
- `rt_monitor_enter`
- `rt_monitor_try_enter`
- `rt_monitor_try_enter_for`
- `rt_monitor_exit`
- `rt_monitor_wait`
- `rt_monitor_wait_for`
- `rt_monitor_pause`
- `rt_monitor_pause_all`

**Recommended Fix:**
Implement using Windows `CRITICAL_SECTION` and `CONDITION_VARIABLE`.

---

## High Priority Issues (Priority 2)

### TUI-001: Missing Windows Console Input Mode Configuration

**Severity:** High
**File:** `src/tui/src/term/session.cpp`
**Impact:** TUI applications may not receive keyboard input correctly on Windows

**Description:**
The code enables `ENABLE_VIRTUAL_TERMINAL_PROCESSING` for output but does not:
1. Enable `ENABLE_VIRTUAL_TERMINAL_INPUT` for input
2. Configure raw input mode via `GetConsoleMode`/`SetConsoleMode` on the input handle
3. Check `_isatty(_fileno(stdin))` before configuring console modes

The POSIX code properly configures raw mode with `tcsetattr`, but Windows has no equivalent setup.

**Recommended Fix:**
Add Windows console input configuration:
```c
HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
GetConsoleMode(hIn, &orig_in_mode_);
SetConsoleMode(hIn, ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_EXTENDED_FLAGS);
```

---

### TUI-002: Missing Input Mode State Storage

**Severity:** High
**File:** `src/tui/include/tui/term/session.hpp`
**Impact:** Cannot restore original console input mode on session exit

**Description:**
The header stores `DWORD orig_out_mode_` for Windows output mode but is missing `DWORD orig_in_mode_` to restore input console mode settings in the destructor.

---

### TUI-003: Windows Input Loop Limitations

**Severity:** Medium
**File:** `src/tui/apps/tui_demo.cpp`
**Impact:** Multi-byte escape sequences (arrow keys, etc.) may not work reliably

**Description:**
The Windows input loop uses `_getch()` from `<conio.h>`, which returns one byte at a time without timeout. This makes it difficult to properly decode multi-byte CSI sequences (like arrow keys `\x1b[A`) compared to the POSIX `read()` approach.

---

## Medium Priority Issues (Priority 3)

### RUNTIME-003: Thread-unsafe Time Functions

**Severity:** Medium
**File:** `src/runtime/rt_datetime.c`
**Impact:** Potential race conditions in multi-threaded programs

**Description:**
Uses `localtime()` and `gmtime()` which are not thread-safe. Should use `localtime_s()` and `gmtime_s()` on Windows for thread safety.

---

### AARCH64-001: Missing Windows PE/COFF Section Handling

**Severity:** Medium (if targeting Windows ARM64)
**File:** `src/codegen/aarch64/RodataPool.cpp` (lines 185-189)

**Description:**
Section directive handling uses `.section __TEXT,__const` for macOS and `.section .rodata` for Linux ELF, but Windows requires `.section .rdata` or similar PE/COFF section syntax.

---

### AARCH64-002: Symbol Mangling Missing Windows Case

**Severity:** Medium
**File:** `src/codegen/aarch64/AsmEmitter.cpp` (lines 131-140)

**Description:**
Symbol mangling adds underscore prefix on Darwin but has no `#ifdef _WIN32` case. Windows also typically requires underscore prefixes for C symbols.

---

### AARCH64-003: Global Symbol Directive Syntax

**Severity:** Medium
**File:** `src/codegen/aarch64/AsmEmitter.cpp` (lines 157-165)

**Description:**
Uses `.globl` directive which is GAS syntax. Windows assemblers (ML64/MASM) use `PUBLIC` instead.

---

### AARCH64-004: No Windows ARM64 Target Implementation

**Severity:** Medium
**File:** `src/codegen/aarch64/TargetAArch64.cpp`

**Description:**
The target is specifically `darwinTarget()` implementing macOS AAPCS64. Windows ARM64 uses ARM64EC or standard Windows ARM64 ABI with differences:
- x18 is reserved as TEB pointer on Windows ARM64
- Different exception handling
- Different register usage conventions

---

### TOOLS-001: ARM64 Codegen Uses Unix-Specific Toolchain

**Severity:** Medium
**File:** `src/tools/ilc/cmd_codegen_arm64.cpp`

**Description:**
- Line 243-256: Uses `cc` for assembling (should use MSVC toolchain on Windows)
- Line 271-604: Uses Unix linker commands (`cc -arch arm64`, `-Wl,-dead_strip`)
- Line 537: Uses macOS-specific `cc -arch arm64`
- Line 655-660: `--run-native` only allows macOS arm64

---

### ZIA-001: Path Separator Only Checks Forward Slash

**Severity:** Low
**File:** `src/frontends/zia/Sema_Decl.cpp` (lines 44-50)

**Description:**
```cpp
auto lastSlash = path.rfind('/');   // Only checks '/'
```
On Windows, paths can use backslash `\` as separator. Should also check for `\\`.

---

## Low Priority Issues (Priority 4)

### BYTECODE-001: No Threaded Dispatch on MSVC

**Severity:** Low (performance only)
**Files:** `src/bytecode/BytecodeVM.hpp` (lines 224-226), `BytecodeVM.cpp` (lines 1551-2740)

**Description:**
The faster computed-goto threaded interpreter is only available on GCC/Clang. MSVC lacks labels-as-values support, so Windows builds fall back to the slower switch-based dispatch (roughly 10-30% slower).

**Note:** This is a known limitation with no easy fix. The fallback is correctly implemented.

---

### BYTECODE-002: Overflow Intrinsics Fallback

**Severity:** Low (performance only)
**File:** `src/bytecode/BytecodeVM.cpp` (lines 1340-1390)

**Description:**
The overflow detection functions use GCC/Clang `__builtin_*_overflow` intrinsics with a portable fallback for MSVC. Could potentially use MSVC's `_addcarry_u64`, `_subborrow_u64` from `<intrin.h>` for better performance.

---

## No Issues Found

The following subsystems were reviewed and found to have **excellent Windows compatibility**:

### x86_64 Codegen (45 files)
- Complete Win64 ABI support via `makeWin64Target()` and `hostTarget()`
- Proper `#ifdef _WIN32` guards for compiler command, executable extension, CRT libraries
- Correct shadow space handling (32 bytes)
- Correct callee-saved XMM registers (XMM6-XMM15)
- Correct argument register order (RCX, RDX, R8, R9)

### IL Subsystem (~100 files)
- Entirely platform-agnostic
- Uses only standard C++ library
- No file system operations (works with streams)
- No hardcoded paths

### VM (~30 files)
- Proper MSVC-compatible implementations in `OpHandlerUtils.hpp` and `IntOpSupport.hpp`
- Correctly guarded `VIPER_THREADING_SUPPORTED` macro
- Standard C++17 features work on all platforms

### Common/BASIC Frontend (~60 files)
- `RunProcess.cpp` has comprehensive Windows API support
- Uses portable `std::filesystem` for path handling
- Uses portable `std::ifstream` for file I/O

---

## Recommendations Summary

### Immediate Action Required
1. **Implement Windows threading** in `rt_threads.c` using Windows Thread API
2. **Implement Windows synchronization** in `rt_monitor.c` using CRITICAL_SECTION/CONDITION_VARIABLE
3. **Fix TUI console input handling** for proper keyboard input on Windows

### Should Fix
4. Fix path separator check in `Sema_Decl.cpp`
5. Use thread-safe time functions (`localtime_s`, `gmtime_s`)

### Nice to Have (if targeting Windows ARM64)
6. Add Windows PE/COFF section handling in aarch64 codegen
7. Add Windows symbol mangling in aarch64 codegen
8. Create Windows ARM64 target implementation

---

*Report generated: 2026-01-18*
