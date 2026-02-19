# Viper Cross-Platform Compatibility Report

**Date:** February 19, 2026
**Scope:** Windows (x86-64, MSVC/Clang-CL), macOS (Apple Silicon + x86-64), Linux (x86-64, AArch64)
**Claim under review:** *"100% cross-platform support for Windows, macOS, and Linux without requiring developer code changes"*

---

## Executive Summary

Viper has a strong architectural foundation for cross-platform development: `rt_platform.h` is
comprehensive, the runtime I/O and threading layers are well-abstracted, and the graphics/audio
subsystems have genuine per-platform native backends. The project is clearly closer to its
cross-platform goal than most compiler toolchains at this stage.

However, several significant gaps prevent the claim of 100% cross-platform support from being
accurate today. The most impactful: **the x86-64 native backend has an incoming stack argument
offset bug for the Windows x64 calling convention**, meaning compiled programs with more than
4 arguments will read stack arguments at wrong offsets on Windows. Additionally, the AArch64
backend lacks a Windows ARM64 assembly target (though register conventions are identical to
Linux — only assembly syntax differs). CI has no Windows job, so these issues accumulate
undetected.

**Total issues found: 22 across 3 severity tiers.**

---

## Platform Status Matrix

| Component | macOS | Linux | Windows | Notes |
|---|---|---|---|---|
| Zia / BASIC frontends | ✅ | ✅ | ✅ | Pure C++17, std::filesystem |
| Viper IL (core/IO) | ✅ | ✅ | ✅ | No platform-specific types |
| IL VM (interpreter) | ✅ | ✅ | ⚠️ | No SEH; no SIGINT handler |
| Bytecode VM | ✅ | ✅ | ✅ | Switch dispatch works everywhere |
| x86-64 native codegen | ✅ | ✅ | ⚠️ | Win64 ABI 90% done; stack arg offset bug |
| AArch64 native codegen | ✅ | ✅ | ❌ | **Windows ARM64 assembly target missing** |
| Runtime: core/arrays/oop | ✅ | ✅ | ✅ | rt_platform.h well-covered |
| Runtime: threading | ✅ | ✅ | ✅ | C++17 std::thread throughout |
| Runtime: file I/O | ✅ | ✅ | ✅ | Comprehensive POSIX shims |
| Runtime: networking | ✅ | ✅ | ⚠️ | WSACleanup missing; gethostbyname in TLS |
| Runtime: graphics (ViperGFX) | ✅ | ✅ | ✅ | Cocoa / X11 / Win32 backends |
| Runtime: audio (ViperAUD) | ✅ | ✅ | ✅ | Core Audio / ALSA / WASAPI backends |
| Runtime: collections/text | ✅ | ✅ | ✅ | Pure C, well-guarded |
| TUI subsystem | ✅ | ✅ | ✅ | Windows console API fully implemented |
| Build system (CMake) | ✅ | ✅ | ⚠️ | Missing flags, silent failures |
| Build scripts | ✅ | ✅ | ⚠️ | Unix-only assumptions |
| CI testing | ✅ | ✅ | ❌ | **No Windows CI job** |

---

## Critical Issues

These issues either block compilation on Windows or cause silent incorrect behavior at runtime.

---

### CRIT-1 — x86-64 Codegen: Windows x64 Stack Argument Offset Bug

**Severity:** CRITICAL
**File:** `src/codegen/x86_64/LowerILToMIR.cpp` (~lines 519–544)

The x86-64 backend has `win64TargetInstance` with correct register assignments (RCX/RDX/R8/R9 for
integer args, XMM0–3 for FP args, 32-byte shadow space). However, incoming stack argument offset
calculation in `LowerILToMIR.cpp` uses the hardcoded SysV formula:

```cpp
// SysV: offset = 16 + stackArgIdx*8  (8 saved RBP + 8 return address)
// Windows x64: offset = shadowSpace + 16 + stackArgIdx*8  (= 48 + stackArgIdx*8)
```

The Windows x64 ABI places 32 bytes of shadow space before the return address on the caller's
stack frame. Functions with more than 4 arguments (which overflow into stack) will read arguments
at wrong offsets, silently producing incorrect results.

**Fix:** Replace hardcoded `16 +` with `target->shadowSpace + 16 +` in the stack arg loading path.

---

### CRIT-2 — No Windows CI Job

**Severity:** CRITICAL
**File:** `.github/workflows/ci.yml`

The CI matrix tests Ubuntu (GCC, Clang) and macOS (Clang) but has no Windows job. This means:
- MSVC / Clang-CL compilation failures go undetected
- Windows-specific regressions accumulate silently
- There is no automated validation of the Windows compatibility claims

CI is currently fully disabled (`branches: ['__disabled__']`).

**Fix:** Add a `windows-latest` job to the CI matrix using Clang-CL or MSVC.

---

### CRIT-3 — AArch64 Codegen: Windows ARM64 Assembly Target Missing

**Severity:** CRITICAL (for Windows ARM targets)
**File:** `src/codegen/aarch64/TargetAArch64.hpp`, `src/codegen/aarch64/AsmEmitter.cpp`

The AArch64 backend defines only `Darwin` and `Linux` platform variants in `ABIFormat`. There is
no `Windows` variant. Importantly, **the register conventions are identical to Linux AAPCS64** —
the same argument registers (X0–X7, V0–V7), callee-saved set (X19–X28, X29), and frame layout.
The only difference is assembly output format:
- Linux: ELF directives (`.type`, `.size`)
- Windows: PE/COFF directives (no `.type`/`.size`, different unwind annotations)

**Fix:** Add `ABIFormat::Windows`, `makeWindowsTarget()` singleton, and PE/COFF assembly emission
path in `AsmEmitter.cpp`.

---

## High Priority Issues

These issues cause functional failures or security vulnerabilities on one or more platforms.

---

### HIGH-1 — `gethostbyname()` in TLS: Deprecated, Non-Thread-Safe

**Severity:** HIGH
**File:** `src/runtime/network/rt_tls.c` (line 928)

`gethostbyname()` was deprecated in POSIX.1-2001 and removed in POSIX.1-2008. It uses a
static internal buffer and is **not thread-safe**. All other network code (`rt_network.c`,
`rt_websocket.c`) correctly uses `getaddrinfo()`. This creates an inconsistency and a
concurrency hazard: two simultaneous TLS connections can corrupt each other's hostname lookup.

**Fix:** Replace with `getaddrinfo()`:
```c
struct addrinfo hints = {0}, *res = NULL;
hints.ai_family = AF_INET;
hints.ai_socktype = SOCK_STREAM;
char port_str[8]; snprintf(port_str, sizeof(port_str), "%d", port);
if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) return NULL;
memcpy(&addr.sin_addr, &((struct sockaddr_in *)res->ai_addr)->sin_addr, sizeof(addr.sin_addr));
freeaddrinfo(res);
```

---

### HIGH-2 — x86-64 Build Scripts: Unix-Only Assumptions

**Severity:** HIGH
**Files:** `scripts/build_viper.sh`, `scripts/build_demos.sh`

The primary build script hardcodes Unix-specific behaviour:
- Uses `clang`/`clang++` explicitly (fails on MSVC-only systems)
- References `/usr/local` for install prefix
- Uses `sync` command (POSIX-only)
- `build_demos.sh` uses `uname -m`, `/tmp`, `ls -lh | awk`, and ANSI escape codes

**Fix:** Add OS detection. Use CMake's `--build` / `--install` uniformly. Avoid shell utilities
(awk, uname) in favour of `cmake -E` cross-platform commands.

---

### HIGH-3 — No POSIX Signal Handler / Windows Console Handler in VM

**Severity:** HIGH
**File:** `src/vm/VM.cpp` (missing)

Neither SIGINT (Unix Ctrl-C) nor `SetConsoleCtrlHandler` (Windows Ctrl-C) is registered.
Pressing Ctrl-C during execution of an infinite loop terminates the process immediately with
no cleanup, no error message, and no graceful trap propagation through the Viper exception system.

**Fix:**
```cpp
// Unix
signal(SIGINT, [](int){ s_interrupted.store(true, std::memory_order_relaxed); });
// Windows
SetConsoleCtrlHandler([](DWORD) -> BOOL { s_interrupted.store(true); return TRUE; }, TRUE);
```
Check the flag at function call boundaries in the dispatch loop.

---

### HIGH-4 — Linker Support: Hardcoded `.a` Archive Extension

**Severity:** HIGH
**File:** `src/codegen/common/LinkerSupport.cpp` (lines 109, 111, 210, 212)

The runtime library path is constructed with hardcoded `.a` extension in both
`runtimeArchivePath()` and `appendGraphicsLibs()`. On Windows, MSVC static libraries use `.lib`.

**Fix:**
```cpp
#ifdef _WIN32
    const char *libPrefix = "", *libExt = ".lib";
#else
    const char *libPrefix = "lib", *libExt = ".a";
#endif
```

---

### HIGH-5 — `strcpy` Buffer Overflows in HTTP URL Handling

**Severity:** HIGH (security + cross-platform stability)
**File:** `src/runtime/network/rt_network_http.c` (lines 982, 2195, 2472+)

Three `strcpy()` calls write into heap-allocated buffers in URL parsing/merging code. While
allocation sizes are computed from `strlen()` before the copy, any intervening mutation of the
source string (or future code changes) results in a write beyond the buffer. This is a security
vulnerability on all platforms.

**Fix:** Replace with `memcpy(dst, src, len + 1)` using the already-measured length.

---

## Medium Priority Issues

These issues cause subtle behavioural differences between platforms, build breakage under
non-standard configurations, or incomplete feature coverage on Windows.

---

### MED-1 — VM: No Windows Structured Exception Handling (SEH)

**Severity:** MEDIUM
**File:** `src/vm/VM.cpp`, `runFunctionLoop()` (lines 618–647)

Hardware exceptions on Windows (access violations, divide-by-zero, stack overflow) are not
caught via SEH (`__try` / `__except`). A bad memory access in native runtime code will
generate an unhandled exception dialog rather than a clean Viper trap message.

**Fix:** Wrap the dispatch loop in a Windows SEH handler that converts hardware exceptions to
`TrapDispatchSignal`.

---

### MED-2 — TUI Subsystem: Windows Console API

**Severity:** MEDIUM → **RESOLVED**
**File:** `src/tui/src/term/session.cpp`

**Status: Already fully implemented.** Lines 109–135 correctly call `GetConsoleMode()`/
`SetConsoleMode()` with `ENABLE_VIRTUAL_TERMINAL_PROCESSING` and `ENABLE_VIRTUAL_TERMINAL_INPUT`.
Lines 177–188 restore original modes in the destructor. No changes needed.

---

### MED-3 — WSAStartup Without WSACleanup

**Severity:** MEDIUM
**File:** `src/runtime/network/rt_network.c` (line 214)

`WSAStartup()` initialises the Windows Sockets library but there is no corresponding
`WSACleanup()` call at process shutdown. While Windows cleans up on process exit, this
violates the API contract and can cause issues in long-running or test-harness contexts.

**Fix:** Register cleanup with `atexit()` after successful `WSAStartup`.

---

### MED-4 — Windows MAX_PATH / PATH_MAX Not Handled Gracefully

**Severity:** MEDIUM
**Files:** `src/runtime/io/rt_tempfile.c`, `src/runtime/io/rt_dir.c`

Stack-allocated `char pattern[PATH_MAX]` buffers become 260 bytes on Windows (`MAX_PATH`).
Windows 10+ supports long paths when enabled, but these functions do not use the wide-char
Unicode APIs that support them. Paths exceeding 260 characters silently fail.

**Fix:** Document the 260-character limit or switch to `GetTempPathW`/`MultiByteToWideChar`.

---

### MED-5 — Golden Test Files: Line Ending Mismatch on Windows

**Severity:** MEDIUM
**File:** `src/tests/golden/CMakeLists.txt`

`file(READ ...)` preserves native line endings. Golden `.stderr` / `.out` files committed with
LF endings will not match CRLF output on Windows, causing all golden tests to fail.

**Fix:**
```cmake
string(REGEX REPLACE "\r\n" "\n" EXPECTED "${EXPECTED}")
string(REGEX REPLACE "\r\n" "\n" ACTUAL   "${ACTUAL}")
```

---

### MED-6 — MSVC Linker Dead-Code Elimination Flags Missing

**Severity:** MEDIUM
**File:** `src/runtime/CMakeLists.txt`

`-ffunction-sections -fdata-sections` are correctly applied for GCC/Clang, but MSVC equivalents
(`/Gy`, `/Gw`) and linker flags (`/OPT:REF /OPT:ICF`) are never set. Windows release builds
will be significantly larger than necessary.

---

### MED-7 — `-lm` Linked as PUBLIC

**Severity:** MEDIUM
**File:** `src/runtime/CMakeLists.txt`

`target_link_libraries(viper_runtime PUBLIC m)` propagates `-lm` to all consumers of
`viper_runtime`. This is an implementation detail that should be `PRIVATE`.

---

### MED-8 — Graphics / Audio Libraries Fail Silently on Missing Dependencies

**Severity:** MEDIUM
**Files:** `src/lib/graphics/CMakeLists.txt`, `src/lib/audio/CMakeLists.txt`

When X11 (Linux) or ALSA (Linux audio) is not found, `return()` silently aborts target
creation. Any code that later links to `vipergfx` gets a confusing "target not found" error.

---

### MED-9 — ARM64 Tests Incorrectly Gated on `NOT WIN32`

**Severity:** MEDIUM
**File:** `src/tests/CMakeLists.txt`

AArch64 codegen tests are wrapped in `if(NOT WIN32)` — the assumption being that Windows
implies x86-64. This breaks for Windows on ARM64 (Surface Pro X, Snapdragon X laptops).

**Fix:** Gate on `CMAKE_SYSTEM_PROCESSOR` matching `aarch64|arm64|ARM64` instead.

---

### MED-10 — ViperDOS POSIX Assumptions Untested

**Severity:** MEDIUM

Several `#elif defined(__viperdos__)` blocks assume BSD-style POSIX APIs. There are no CI-level
tests that compile and run the ViperDOS-targeted build.

---

### MED-11 — `viper init` and CLI Tools: No ARM64 Architecture Detection

**Severity:** MEDIUM
**Files:** `src/tools/viper/main.cpp`, `src/tools/viper/cmd_init.cpp`

The CLI defaults to `cc`/`clang` for the linker driver without detecting the host architecture.
On Apple Silicon cross-compiling to x86-64, or on Windows ARM64, the wrong assembler/linker
may be picked silently.

---

## Low Priority Issues

---

### LOW-1 — `ssize_t` Defined as `int` on 32-bit Windows

**File:** `src/runtime/rt_platform.h`

On 32-bit Windows, `ssize_t` is `int` (32-bit), limiting I/O return values to ±2 GB.

---

### LOW-2 — Assembly Output is GNU Assembler Only

**Files:** `src/codegen/x86_64/AsmEmitter.cpp`, `src/codegen/aarch64/AsmEmitter.cpp`

Both backends emit GNU AS syntax. No MASM/NASM output option exists. This should be documented.

---

### LOW-3 — Clang Detection Does Not Include `clang-cl`

**File:** `CMakeLists.txt`

`find_program(CLANG_EXECUTABLE NAMES clang clang-17 ...)` does not include `clang-cl`, the
MSVC-compatible front-end used on Windows.

---

### LOW-4 — Symbol Visibility Not Set on Unix Targets

**File:** `CMakeLists.txt`

No `-fvisibility=hidden` is applied to shared library targets. All symbols are exported by
default on Linux/macOS, increasing binary size and startup time.

---

## What Is Working Well

| Area | Notes |
|---|---|
| **`rt_platform.h`** | Excellent MSVC/GCC/Clang detection; full atomic suite; POSIX shims; thread-local storage; high-res timers |
| **Threading (runtime)** | C++17 `std::thread`/`std::mutex`/`std::condition_variable` throughout |
| **File I/O** | Comprehensive `open`→`_open`, `lseek`→`_lseeki64`, `ssize_t`, `errno` mappings |
| **Graphics (ViperGFX)** | Separate Win32 (GDI), Cocoa (Objective-C), and X11 backends |
| **Audio (ViperAUD)** | Separate WASAPI, Core Audio, and ALSA/PulseAudio backends |
| **Compiler intrinsics** | `rt_bits.c` and `rt_bitset.c` correctly guard `__builtin_popcount`/`__popcnt64` with MSVC alternatives |
| **IL core** | Pure C++17 `std::filesystem` throughout; no platform types |
| **Frontends (Zia, BASIC)** | Handle both `\r\n` and `\n` line endings; `std::filesystem` for paths |
| **Test framework** | `PosixCompat.h` provides `fork()` stub, `mkstemp()`, `pipe()`, SKIP macros |
| **Process execution** | `RunProcess.cpp` has correct Windows command-line quoting |
| **TUI (Windows)** | `session.cpp` correctly implements GetConsoleMode/SetConsoleMode for ANSI support |
| **Networking (core)** | `fcntl`/`ioctlsocket` properly guarded; `getaddrinfo` used in network/websocket modules |
| **x86-64 Win64 ABI** | `win64TargetInstance` fully defined with correct registers, shadow space, `__chkstk` |

---

## Recommended Roadmap

### Tier 1 — Before Claiming Full Windows Support

1. Fix incoming stack arg offset for Win64 ABI (`src/codegen/x86_64/LowerILToMIR.cpp`) — CRIT-1
2. Add Windows CI job and re-enable CI — CRIT-2
3. Add AArch64 Windows ARM64 assembly target — CRIT-3
4. Fix `strcpy` overflows in HTTP code — HIGH-5
5. Fix linker archive extension — HIGH-4

### Tier 2 — Correctness and Stability

6. Replace `gethostbyname` with `getaddrinfo` in TLS — HIGH-1
7. Add SIGINT / `SetConsoleCtrlHandler` to VM — HIGH-3
8. Add WSACleanup via atexit — MED-3
9. TUI Windows console — MED-2 (**already done**)
10. Add Windows SEH to VM execution loop — MED-1

### Tier 3 — Build Infrastructure Quality

11. Fix golden test CRLF/LF mismatch — MED-5
12. Fix ARM64 test gating — MED-9
13. Fix graphics/audio silent failure messaging — MED-8
14. Fix `-lm` PUBLIC linkage — MED-7
15. Add MSVC `/OPT:REF /OPT:ICF` linker flags — MED-6
16. Add MAX_PATH handling — MED-4
17. Cross-platform build scripts — HIGH-2

### Tier 4 — Polish

18. Symbol visibility flags on Unix targets — LOW-4
19. Add `clang-cl` to clang detection — LOW-3
20. Document assembly format dependency — LOW-2
21. ViperDOS build/test CI job — MED-10

---

## Issue Index

| ID | Title | Severity | File(s) | Status |
|---|---|---|---|---|
| CRIT-1 | Win64 stack arg offset bug | Critical | `src/codegen/x86_64/LowerILToMIR.cpp` | Open |
| CRIT-2 | No Windows CI job | Critical | `.github/workflows/ci.yml` | Open |
| CRIT-3 | AArch64 Windows ARM64 target missing | Critical | `src/codegen/aarch64/TargetAArch64.*` | Open |
| HIGH-1 | `gethostbyname` deprecated/non-thread-safe | High | `src/runtime/network/rt_tls.c` | Open |
| HIGH-2 | Build scripts Unix-only | High | `scripts/build_viper.sh` | Open |
| HIGH-3 | No SIGINT/Ctrl-C handler in VM | High | `src/vm/VM.cpp` | Open |
| HIGH-4 | Linker support hardcodes `.a` extension | High | `src/codegen/common/LinkerSupport.cpp` | Open |
| HIGH-5 | `strcpy` buffer overflows in HTTP code | High | `src/runtime/network/rt_network_http.c` | Open |
| MED-1 | No Windows SEH in VM | Medium | `src/vm/VM.cpp` | Open |
| MED-2 | TUI Windows console API incomplete | Medium | `src/tui/src/term/session.cpp` | **Resolved** |
| MED-3 | WSAStartup without WSACleanup | Medium | `src/runtime/network/rt_network.c` | Open |
| MED-4 | MAX_PATH not handled gracefully | Medium | `src/runtime/io/rt_tempfile.c`, `rt_dir.c` | Open |
| MED-5 | Golden test CRLF/LF mismatch | Medium | `src/tests/golden/CMakeLists.txt` | Open |
| MED-6 | MSVC `/OPT:REF /OPT:ICF` missing | Medium | `src/runtime/CMakeLists.txt` | Open |
| MED-7 | `-lm` linked as PUBLIC | Medium | `src/runtime/CMakeLists.txt` | Open |
| MED-8 | Graphics/audio fail silently | Medium | `src/lib/*/CMakeLists.txt` | Open |
| MED-9 | ARM64 tests gated on `NOT WIN32` | Medium | `src/tests/CMakeLists.txt` | Open |
| MED-10 | ViperDOS POSIX assumptions untested | Medium | Various `src/runtime/` files | Open |
| MED-11 | CLI has no ARM64 architecture detection | Medium | `src/tools/viper/main.cpp` | Open |
| LOW-1 | `ssize_t` is `int` on 32-bit Windows | Low | `src/runtime/rt_platform.h` | Open |
| LOW-2 | Assembly output is GNU Assembler only | Low | `src/codegen/*/AsmEmitter.cpp` | Open |
| LOW-3 | `clang-cl` not in clang detection | Low | `CMakeLists.txt` | Open |
| LOW-4 | Symbol visibility not set on Unix | Low | `CMakeLists.txt` | Open |
