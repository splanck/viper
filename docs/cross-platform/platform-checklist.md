---
status: active
audience: contributors
last-verified: 2026-03-04
---

# Cross-Platform Developer Checklist

When adding or modifying platform-sensitive functionality in Viper, use this
reference to identify every file that must be touched. The codebase targets
**Windows (x86-64)**, **macOS (x86-64 + ARM64)**, **Linux (x86-64)**, and
**ViperDOS (AArch64)**.

For a user-facing reference of runtime behavioral differences across platforms, see
[platform-differences.md](platform-differences.md).

---

## Platform Abstraction Layer

All platform detection is centralized in one header:

| Macro | Detects | Header |
|-------|---------|--------|
| `RT_PLATFORM_WINDOWS` | `_WIN32 \|\| _WIN64` | `src/runtime/rt_platform.h` |
| `RT_PLATFORM_MACOS` | `__APPLE__` | `src/runtime/rt_platform.h` |
| `RT_PLATFORM_LINUX` | `__linux__` | `src/runtime/rt_platform.h` |
| `RT_PLATFORM_VIPERDOS` | `__viperdos__` | `src/runtime/rt_platform.h` |
| `RT_COMPILER_MSVC` | MSVC (not Clang-CL) | `src/runtime/rt_platform.h` |
| `RT_COMPILER_GCC_LIKE` | GCC or Clang | `src/runtime/rt_platform.h` |

This header also provides cross-platform shims for TLS (`RT_THREAD_LOCAL`),
weak symbols, atomics (MSVC intrinsics vs `__atomic_*` builtins), and POSIX
compatibility mappings on Windows (`mkdir`, `unlink`, `ssize_t`, `S_ISDIR`,
etc.).

---

## Category Checklists

### 1. Filesystem

**When adding filesystem functionality, you must also update:**

| File | Reason |
|------|--------|
| `src/runtime/io/rt_file_io.c` | File open/read/write/seek — Windows uses `_open`/`_close`/`_read`/`_write`/`_lseeki64`; POSIX uses native APIs. Permission macros differ (`_S_IREAD` vs `S_IRUSR`). Missing errno values shimmed on Windows. |
| `src/runtime/io/rt_dir.c` | Directory listing/creation/removal — Windows uses `FindFirstFileA`/`FindNextFileA`/`_mkdir`/`_rmdir`; POSIX uses `opendir`/`readdir`/`mkdir`/`rmdir`. Path separator: `\\` on Windows, `/` on Unix. |
| `src/runtime/io/rt_watcher.c` | Filesystem watching — three completely separate backends: **Linux** (inotify + poll), **macOS** (kqueue + kevent), **Windows** (ReadDirectoryChangesW + overlapped I/O). |
| `src/runtime/rt_platform.h` | Path separator macro, POSIX compat shims for Windows, `mode_t` typedef. |

**[GAP]** ViperDOS file watcher is a stub — no kernel inotify support yet.
**[GAP]** Windows directory paths limited to `MAX_PATH` (260 chars); no Unicode long-path support.

---

### 2. Threading

**When adding threading functionality, you must also update:**

| File | Reason |
|------|--------|
| `src/runtime/threads/rt_threads.c` | Thread create/join/sync — Windows uses `CreateThread`/`CRITICAL_SECTION`/`CONDITION_VARIABLE`; POSIX uses `pthread_create`/`pthread_mutex_t`/`pthread_cond_t`. Timed join uses `GetTickCount64()` on Windows vs `clock_gettime()` + absolute timespec on POSIX. |
| `src/runtime/rt_platform.h` | TLS macro (`__declspec(thread)` vs `_Thread_local` vs `__thread`), atomic operations (MSVC `_InterlockedExchange*` vs GCC `__atomic_*`), memory barriers. |
| `src/runtime/core/rt_atomic_compat.h` | GCC/Clang `__atomic_*` builtin compatibility layer for MSVC — maps to Interlocked intrinsics via C11 `_Generic` dispatch. |
| `src/runtime/core/rt_context.c` | Per-thread context storage via `RT_THREAD_LOCAL`. |

---

### 3. Networking

**When adding networking functionality, you must also update:**

| File | Reason |
|------|--------|
| `src/runtime/network/rt_network.c` | ~15 platform-conditional blocks. Windows: `<winsock2.h>`, `WSAStartup`, `closesocket()`, `WSAGetLastError()`. POSIX: `<sys/socket.h>`, `close()`, `errno`. SIGPIPE suppression differs: Linux uses `MSG_NOSIGNAL` per-send; macOS uses `SO_NOSIGPIPE` socket option; Windows needs neither. |
| `src/runtime/network/rt_tls.c` | TLS/SSL — platform-specific crypto. macOS: Security.framework. Linux: custom built-in (ECDSA P-256, ECDHE) with `dlopen` fallback to system libcrypto for RSA-PSS only. Windows: Schannel (via WinCrypt). No external library is linked at build time on any platform. |
| `src/runtime/CMakeLists.txt` | Windows links `ws2_32`; macOS links `-framework Security`. |
| `CMakeLists.txt` | Feature flag `VIPER_ENABLE_NETWORK` and `VIPER_ENABLE_TLS`. |

---

### 4. Process / IPC

**When adding process or IPC functionality, you must also update:**

| File | Reason |
|------|--------|
| `src/runtime/system/rt_exec.c` | Process execution — Windows: `_popen`/`_pclose`, `cmd.exe /c` for shell mode. POSIX: `posix_spawn()` with argv array, `/bin/sh -c` for shell mode. `extern char **environ` is POSIX-only. |
| `src/runtime/system/rt_machine.c` | System info — four separate implementations: Windows (`GetSystemInfo`, `GlobalMemoryStatusEx`, `GetComputerNameA`), macOS (`sysctl`, `host_statistics64`), Linux (`sysconf`, `sysinfo`), ViperDOS (POSIX `sysconf`/`uname`). |
| `src/vm/VM.cpp` | Ctrl-C / interrupt handling — Windows: `SetConsoleCtrlHandler` + `windowsCtrlHandler`. POSIX: `sigaction(SIGINT)` + `posixSigintHandler` with `SA_RESTART`. |
| `src/tests/common/PosixCompat.h` | Test compatibility layer — Windows stubs for `fork()` (returns -1), `waitpid()`, `pipe()` → `_pipe()`, `usleep()` → `Sleep()`, `mkstemp()` → `_mktemp_s()`. Uses `SKIP_TEST_NO_FORK()` macro. |

~~**[GAP]** Windows has no `fork()`.~~ **Resolved:** Tests use `ProcessIsolation` (CreateProcess self-relaunch + Job Object on Windows, fork on POSIX). See `src/tests/common/ProcessIsolation.hpp`.

---

### 5. Native Codegen

**When adding codegen functionality, you must also update:**

| File | Reason |
|------|--------|
| `src/codegen/x86_64/CodegenPipeline.cpp` | Compiler invocation — Windows: `clang`, `.exe` extension, direct return code. Unix: `cc`, `.out` extension, `WIFEXITED`/`WEXITSTATUS` from `<sys/wait.h>`. |
| `src/codegen/x86_64/Backend.cpp` | Symbol naming — Linux requires different relocation patterns than macOS Mach-O. |
| `src/codegen/x86_64/AsmEmitter.cpp` | Assembly emission — Mach-O symbol prefixes (`_` on macOS), ELF on Linux. |
| `src/codegen/x86_64/FrameLowering.cpp` | Stack frame layout — SysV AMD64 ABI (all Unix). Windows x86-64 ABI differs in shadow space and red zone. |
| `src/codegen/x86_64/CallLowering.cpp` | Calling convention — SysV: rdi/rsi/rdx/rcx/r8/r9 for integer args. Win64: rcx/rdx/r8/r9 (different order, shadow space). |
| `src/codegen/aarch64/FrameBuilder.cpp` | AArch64 AAPCS64 stack frame — 16-byte SP alignment, x29/x30 pair, callee-saved x19-x28/d8-d15. |
| `src/codegen/aarch64/RodataPool.cpp` | Mach-O rodata pool handling (macOS-specific addressing). |
| `src/codegen/common/LinkerSupport.cpp` | Archive naming (`.lib` vs `lib*.a`), framework flags on macOS, X11 on Linux, user32/gdi32 on Windows. |

**[GAP]** x86-64 codegen currently uses SysV ABI only — Win64 ABI (shadow space, different register order) is not implemented. Native compilation targets Unix hosts.

---

### 6. Graphics / Input

**When adding graphics or input functionality, you must also update:**

| File | Reason |
|------|--------|
| `src/lib/graphics/src/vgfx_platform_macos.m` | macOS backend — Cocoa: NSWindow, NSView, CGImage, mach_absolute_time. Objective-C. |
| `src/lib/graphics/src/vgfx_platform_linux.c` | Linux backend — X11: XOpenDisplay, XCreateWindow, XImage, XPutImage, KeySym. |
| `src/lib/graphics/src/vgfx_platform_win32.c` | Windows backend — Win32 GDI: HWND, HDC, DIB sections, BitBlt, CreateWindowEx, VK_* keycodes. |
| `src/lib/graphics/CMakeLists.txt` | Platform source selection and library linking: `-framework Cocoa` (macOS), X11 (Linux), `user32`/`gdi32` (Windows). FATAL_ERROR on unknown platform. |
| `src/lib/gui/CMakeLists.txt` | GUI additions — macOS: `vg_filedialog_native.m` (native file dialogs), `-framework UniformTypeIdentifiers`. Linux: `-lm`, `_GNU_SOURCE`. |
| `src/runtime/core/rt_term.c` | Terminal input — Windows: `<conio.h>`, `_kbhit()`, `_getch()`, `ENABLE_VIRTUAL_TERMINAL_PROCESSING`. POSIX: `<termios.h>`, `select()` with zero timeout, raw mode caching. |
| `src/repl/ReplLineEditor.cpp` | REPL terminal width — Windows: `GetConsoleScreenBufferInfo()`. POSIX: `ioctl(TIOCGWINSZ)`. Raw I/O: Windows `WriteConsoleA()` vs POSIX `::write()`. |

**[GAP]** Linux graphics requires X11 dev headers (`libx11-dev`). If not found, ViperGFX is silently omitted — downstream targets linking it will fail.

---

### 7. Audio

**When adding audio functionality, you must also update:**

| File | Reason |
|------|--------|
| `src/lib/audio/src/vaud_platform_macos.m` | macOS backend — AudioQueue: AudioQueueRef, AudioStreamBasicDescription. Objective-C. |
| `src/lib/audio/src/vaud_platform_linux.c` | Linux backend — ALSA: snd_pcm_t, snd_pcm_writei, pthread for audio thread. |
| `src/lib/audio/src/vaud_platform_win32.c` | Windows backend — WASAPI: IMMDevice, IAudioClient, IAudioRenderClient, COM initialization. |
| `src/lib/audio/CMakeLists.txt` | Platform source selection: `-framework AudioToolbox` (macOS), ALSA (Linux), `ole32` (Windows). FATAL_ERROR on unknown platform. |
| `src/runtime/audio/rt_audio.c` | Runtime bridge — delegates to ViperAUD. Gated by `VIPER_ENABLE_AUDIO`; stubs provided when disabled. |

**[GAP]** Linux audio requires ALSA dev headers (`libasound2-dev`). If not found, ViperAUD is silently omitted.

---

### 8. Time / Clocks

**When adding time or timer functionality, you must also update:**

| File | Reason |
|------|--------|
| `src/runtime/core/rt_time.c` | Sleep and monotonic clocks — Windows: `Sleep()` (10-15ms resolution), `QueryPerformanceCounter()` with `GetTickCount64()` fallback. POSIX: `nanosleep()` with EINTR retry, `clock_gettime(CLOCK_MONOTONIC)` with `CLOCK_REALTIME` fallback. |
| `src/runtime/rt_platform.h` | Windows time helpers: `rt_windows_time_ms()`, `rt_windows_time_us()`, `rt_windows_sleep_ms()`. Thread-safe `rt_localtime_r()` / `rt_gmtime_r()` / `rt_strtok_r()` wrappers. |

---

### 9. Installer / Packaging

**When adding packaging or installation functionality, you must also update:**

| File | Reason |
|------|--------|
| `CMakeLists.txt` (lines 480-498) | CPack generator selection — macOS: `productbuild` (.pkg), Windows: ZIP + NSIS, Linux: DEB + RPM. Output filename patterns differ per platform. |
| `scripts/build_viper.sh` | Install location — Windows: `$LOCALAPPDATA/viper`, macOS/Linux: `/usr/local` (with sudo). Compiler selection: Windows clang-cl/MSVC, Unix clang/gcc. Job count: `nproc` / `sysctl` / `NUMBER_OF_PROCESSORS`. |
| `viperdos/scripts/build_viperdos.sh` | Auto-installs prerequisites per OS: Homebrew (macOS), apt (Debian), yum (RedHat). UEFI ESP image creation uses platform-specific tools. |
| `viperdos/scripts/build_viperdos.cmd` | Windows batch equivalent — QEMU/CMake/Clang detection with Windows-specific paths. |

**[GAP]** ViperDOS Windows build has no UEFI ESP creation — falls back to direct boot (explicitly acknowledged in script).

---

### 10. Build System / Compiler

**When adding build configuration, you must also update:**

| File | Reason |
|------|--------|
| `CMakeLists.txt` | Compiler flags — MSVC: `/FS`, `/utf-8`, `/W4`, `/permissive-`. GCC/Clang: `-Wall -Wextra -Wpedantic`, sanitizers, LTO. Symbol visibility (`-fvisibility=hidden`) on non-Windows. LLD linker gated on `IL_USE_LLD`. Apple-specific: suppress ld64 warnings, ARM64 cross-compilation flags. |
| `src/runtime/CMakeLists.txt` | Feature definitions — `_POSIX_C_SOURCE=200809L` on non-Windows, `_DEFAULT_SOURCE` on Linux (not macOS), `_GNU_SOURCE` on Linux. MSVC link optimization `/OPT:REF /OPT:ICF`. macOS frameworks: IOKit, CoreFoundation, Security. |
| `src/tests/CMakeLists.txt` | Test gating — ARM64 tests enabled only on `arm64|aarch64|ARM64` processors. Windows tests add `WinDialogSuppress.c` to suppress crash dialogs. |

---

## Platform Gap Summary

| ID | Category | Gap Description | Severity |
|----|----------|----------------|----------|
| GAP-1 | Filesystem | ViperDOS file watcher is a stub (no kernel inotify) | Low |
| GAP-2 | Filesystem | Windows `MAX_PATH` (260 char) limit on directory operations | Medium |
| ~~GAP-3~~ | ~~Process~~ | ~~Resolved: Windows uses CreateProcess self-relaunch + Job Object~~ | ~~Resolved~~ |
| GAP-4 | Codegen | x86-64 codegen uses SysV ABI only; Win64 ABI not implemented | Medium |
| GAP-5 | Graphics | Linux requires X11 dev headers; silently omitted if missing | Low |
| GAP-6 | Audio | Linux requires ALSA dev headers; silently omitted if missing | Low |
| GAP-7 | Packaging | ViperDOS Windows build cannot create UEFI ESP images | Low |

---

## Quick-Reference: Platform Backend Files

For features that require a per-platform implementation file, follow this
pattern (used by graphics and audio):

```text
src/lib/<feature>/
  CMakeLists.txt          # if(APPLE) / elseif(UNIX) / elseif(WIN32) / else FATAL_ERROR
  src/
    <feature>_common.c    # Shared logic
    <feature>_platform_macos.m    # Cocoa / CoreAudio (Objective-C)
    <feature>_platform_linux.c    # X11 / ALSA
    <feature>_platform_win32.c    # Win32 GDI / WASAPI
  include/
    <feature>.h           # Public API (platform-agnostic)
```

For features that use `#ifdef` blocks within a single file (used by runtime):

```text
#ifdef RT_PLATFORM_WINDOWS
    // Windows implementation
#elif defined(RT_PLATFORM_MACOS)
    // macOS-specific (kqueue, mach_*, etc.)
#else
    // POSIX fallback (Linux + ViperDOS)
#endif
```

---

## Build Dependencies by Platform

### macOS
- Xcode (Apple Clang + Objective-C)
- Frameworks: Cocoa, AudioToolbox, IOKit, CoreFoundation, Security, UniformTypeIdentifiers

### Linux (Debian/Ubuntu)
- `build-essential`, `cmake`, `clang` or `gcc`
- `libx11-dev` (graphics)
- `libasound2-dev` (audio)
- `libssl-dev` (optional; only needed at runtime for RSA-PSS TLS cipher suites)

### Linux (RedHat/Fedora)
- `gcc`, `cmake`, `make`
- `libX11-devel` (graphics)
- `alsa-lib-devel` (audio)
- `openssl-devel` (optional; only needed at runtime for RSA-PSS TLS cipher suites)

### Windows
- MSVC or Clang-CL
- CMake
- Linked at build: `ws2_32`, `Xinput9_1_0`, `ole32`, `user32`, `gdi32`
