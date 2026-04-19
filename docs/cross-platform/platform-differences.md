---
status: active
audience: public
last-verified: 2026-04-09
---

# Platform Behavioral Differences

This document describes the intentional and incidental behavioral differences in the Viper compiler and runtime across **Windows (x86-64)**, **macOS (x86-64 and ARM64)**, and **Linux (x86-64)**. It is intended for developers and advanced users who need to understand what Viper does differently per platform at runtime.

For a contributor-oriented checklist of which source files to modify when adding platform-sensitive code, see [platform-checklist.md](platform-checklist.md).

---

## Summary Table

| Feature Area | Windows | macOS | Linux |
|---|---|---|---|
| Core Runtime (VM, IL, Zia) | ✅ Full | ✅ Full | ✅ Full |
| Terminal I/O | ✅ Full | ✅ Full | ✅ Full |
| Filesystem I/O | ⚠️ Partial [1] | ✅ Full | ✅ Full |
| File Watching | ✅ Full | ✅ Full | ✅ Full |
| Networking (TCP/UDP/HTTP) | ✅ Full | ✅ Full | ✅ Full |
| TLS/SSL | ✅ Full (Schannel + in-tree handshake) | ✅ Full (in-tree TLS/X.509 runtime) | ✅ Full (in-tree TLS/X.509 runtime) |
| Threading | ✅ Full | ✅ Full | ✅ Full |
| Graphics | ✅ Full (GDI) | ✅ Full (Cocoa) | ⚠️ Partial [2] |
| Audio | ✅ Full (WASAPI) | ✅ Full (AudioQueue) | ⚠️ Partial [3] |
| Game Controllers | ✅ Full (XInput) | ✅ Full (IOKit HID) | ⚠️ Partial [4] |
| Native Codegen (x86-64) | ✅ Full | ✅ Full | ✅ Full |
| Native Codegen (AArch64) | ⚠️ Partial [6] | ✅ Full | ✅ Full |
| Process Execution | ✅ Full | ✅ Full | ✅ Full |

**Footnotes:**

1. Windows directory operations limited to `MAX_PATH` (260 characters). No Unicode long-path support.
2. Requires `libx11-dev` at build time. If missing, graphics library is silently omitted.
3. Requires `libasound2-dev` at build time. If missing, audio library is silently omitted.
4. Linux evdev backend lacks vibration/rumble support.
5. ~~x86-64 native codegen emits SysV ABI only.~~ **Fixed:** Both System V AMD64 and Windows x64 ABIs are implemented.
6. AArch64 Windows PE/COFF target is defined but not tested in CI.
7. ~~Resolved.~~ Windows test infrastructure uses `CreateProcess` self-relaunch + Job Object instead of `fork()`.

---

## 1. Runtime Library (Viper.\* Namespace)

The Viper runtime presents a uniform API across platforms. Underneath, each module dispatches to platform-native implementations. The subsections below document where behavior diverges.

### 1.1 Terminal I/O

The `Viper.Terminal` module (`Say`, `Print`, `ReadKey`, etc.) is functionally equivalent across platforms, but the underlying implementation differs.

| Aspect | Windows | macOS / Linux |
|--------|---------|---------------|
| Key input | `_kbhit()` / `_getch()` via `<conio.h>` | `termios` raw mode + `select()` with zero timeout |
| ANSI escape support | Enabled at startup via `SetConsoleMode(ENABLE_VIRTUAL_TERMINAL_PROCESSING)` | Native — no setup required |
| Terminal width query | `GetConsoleScreenBufferInfo()` | `ioctl(TIOCGWINSZ)` |
| Blocking key wait | `WaitForSingleObject()` with timeout on stdin | `select()` with timeout on fd 0 |
| Beep | Optional `Beep()` WinAPI (controlled by `VIPER_BEEP_WINAPI` env var) | Terminal bell character (`\a`) |

**User-visible difference:** On older Windows terminals (pre-Windows 10 1607), ANSI escape sequences may not render correctly. The runtime enables VT processing automatically, but third-party terminal emulators may behave differently.

### 1.2 System Information (Viper.Machine)

The `Viper.Machine` module returns platform-specific values for several queries.

| Function | Windows | macOS | Linux |
|----------|---------|-------|-------|
| `Machine.Os()` | `"windows"` | `"macos"` | `"linux"` |
| `Machine.OsVersion()` | `GetVersionExA()` → e.g. `"10.0.19045"` | `sw_vers` command → e.g. `"14.2.1"` | `/etc/os-release` → e.g. `"Ubuntu 22.04"` |
| `Machine.Host()` | `GetComputerNameA()` | `gethostname()` | `gethostname()` |
| `Machine.User()` | `GetUserNameA()` with SID fallback | `getpwuid(getuid())` | `getpwuid()` with `$USER` env fallback |
| `Machine.Home()` | `%USERPROFILE%` (e.g. `C:\Users\alice`) | `$HOME` or `/var/root` | `$HOME` or `getpwuid()` fallback |
| `Machine.Temp()` | `GetTempPathA()` (e.g. `C:\Users\alice\AppData\Local\Temp`) | `$TMPDIR` or `/tmp` | `$TMPDIR` or `/tmp` |
| `Machine.Cores()` | `GetSystemInfo().dwNumberOfProcessors` | `sysctlbyname("hw.logicalcpu")` | `sysconf(_SC_NPROCESSORS_ONLN)` |
| `Machine.MemTotal()` | `GlobalMemoryStatusEx()` | `sysctl(HW_MEMSIZE)` | `/proc/meminfo` MemTotal |
| `Machine.MemFree()` | `GlobalMemoryStatusEx().ullAvailPhys` | `vm_stat` page parsing | `/proc/meminfo` MemAvailable |

**User-visible differences:**

- `OsVersion()` format varies significantly between platforms. Do not parse the string for version comparison — use `Machine.Os()` for platform detection.
- `Home()` uses `%USERPROFILE%` on Windows (typically `C:\Users\<name>`) vs `$HOME` on Unix (typically `/home/<name>` or `/Users/<name>`). The path separator in the returned string matches the platform convention.
- `Temp()` returns a platform-native path. On Windows this often includes the user's AppData directory; on Unix it is typically `/tmp` unless `$TMPDIR` is set.

### 1.3 File Watching

The `Viper.Watcher` module uses three completely separate backends, but presents a unified API.

| Aspect | Windows | macOS | Linux |
|--------|---------|-------|-------|
| Backend | `ReadDirectoryChangesW` + overlapped I/O | `kqueue` + `kevent` with `EVFILT_VNODE` | `inotify` + `poll()` |
| Event granularity | Per-file notifications via `FILE_NOTIFY_INFORMATION` | Per-directory `NOTE_WRITE` / `NOTE_DELETE` / `NOTE_RENAME` | Per-file `IN_CREATE` / `IN_DELETE` / `IN_MODIFY` / `IN_MOVED_*` |
| Filename in events | UTF-16 → UTF-8 conversion from `FILE_NOTIFY_INFORMATION.FileName` | Not provided by kqueue (directory-level only) | Provided in `inotify_event.name` |
| Cleanup | `CancelIo()` + `CloseHandle()` | `close(kqueue_fd)` + `close(watched_fd)` | `inotify_rm_watch()` + `close()` |

**User-visible difference:** On macOS, kqueue reports directory-level changes without identifying the specific file. The runtime works around this where possible, but some event details may be less granular than on Windows or Linux.

### 1.4 Temporary Files and Entropy

| Aspect | Windows | macOS / Linux |
|--------|---------|---------------|
| Random bytes | `CryptAcquireContext()` + `CryptGenRandom()` | `/dev/urandom` via `open()` + `read()` |
| Fallback entropy | `GetTickCount64()` XOR'd with stack address | `getpid()` XOR'd with stack address |
| Temp directory | `GetTempPathA()` with trailing `\` stripped | `$TMPDIR` or `/tmp` with trailing `/` stripped |

Both paths produce cryptographically unpredictable temp filenames. The fallback entropy path is only used if the primary source fails (extremely rare).

### 1.5 Process Execution

| Aspect | Windows | macOS / Linux |
|--------|---------|---------------|
| Shell command | `system()` dispatches to `cmd.exe /c` | `system()` dispatches to `/bin/sh -c` |
| Pipe open | `_popen()` / `_pclose()` | `popen()` / `pclose()` |
| Environment get | `GetEnvironmentVariableA()` | `getenv()` |
| Environment set | `SetEnvironmentVariableA()` | Not exposed (process-local `setenv()` only) |
| `fork()` | Not available — test infra uses `CreateProcess` self-relaunch instead | Available |

**User-visible difference:** Shell commands passed to `Viper.Exec.Shell()` are interpreted by `cmd.exe` on Windows and `/bin/sh` on Unix. Shell syntax (pipes, redirects, quoting rules) differs between these interpreters.

### 1.6 Networking and TLS

The socket API is functionally equivalent, but initialization and error handling differ.

| Aspect | Windows | macOS | Linux |
|--------|---------|-------|-------|
| Socket init | `WSAStartup()` required (called automatically) | None | None |
| Socket close | `closesocket()` | `close()` | `close()` |
| Error retrieval | `WSAGetLastError()` | `errno` | `errno` |
| Non-blocking mode | `ioctlsocket(FIONBIO)` | `fcntl(F_SETFL, O_NONBLOCK)` | `fcntl(F_SETFL, O_NONBLOCK)` |
| SIGPIPE suppression | Not needed (no SIGPIPE on Windows) | `SO_NOSIGPIPE` socket option | `MSG_NOSIGNAL` flag per-send |
| Would-block error | `WSAEWOULDBLOCK` | `EAGAIN` | `EAGAIN` / `EWOULDBLOCK` |

**TLS/SSL backends:**

| Platform | Backend | Certificate Store |
|----------|---------|-------------------|
| Windows | Schannel via WinCrypt API | Windows Certificate Store (`CertOpenStore`) |
| macOS | Custom built-in TLS/X.509 runtime | Configured PEM bundle override or the system PEM bundle (`/etc/ssl/cert.pem` on current macOS releases) |
| Linux | Custom built-in TLS/X.509 runtime | Configured PEM bundle override or the system PEM bundle (typically `/etc/ssl/certs/...`) |

The macOS and Linux TLS implementations are entirely custom and built-in. TLS 1.3 handshake processing, certificate-chain parsing, hostname verification, RSA-PSS CertificateVerify, and HTTPS/WSS server signing all run in-tree without OpenSSL, Security.framework, LibreSSL, mbedTLS, or runtime `dlopen()` fallbacks.

All three backends validate server certificates against a trusted root source. Windows uses the platform certificate store; macOS and Linux use a PEM trust bundle discovered from standard system locations unless a PEM bundle override is configured by the caller.

### 1.7 Game Controller Input

| Aspect | Windows | macOS | Linux |
|--------|---------|-------|-------|
| Backend | XInput API | IOKit HID Manager | evdev (`/dev/input/event*`) |
| Button mapping | `XINPUT_GAMEPAD_*` constants | HID element parsing via `IOHIDDeviceGetValue` | Raw `BTN_*` / `ABS_*` event codes |
| Vibration/Rumble | ✅ `XInputSetState()` | ✅ `IOHIDDeviceSetReport()` | ❌ Not supported |
| Hot-plug detection | Automatic via XInput polling | `IOHIDManagerRegisterDeviceMatchingCallback` | Manual `/dev/input/` scanning |

**User-visible difference:** Vibration is not available on Linux. Calling rumble functions on Linux is a no-op.

### 1.8 Graphics

| Aspect | Windows | macOS | Linux |
|--------|---------|-------|-------|
| Backend | Win32 GDI (DIB sections + `BitBlt`) | Cocoa (`NSWindow` + `NSView` + `CGImage`) | X11 Xlib (`XCreateWindow` + `XPutImage`) |
| Pixel format | BGRA via DIB section | BGRA via `CGBitmapContextCreate` | BGRA buffer → XImage |
| Keyboard input | `VK_*` virtual key codes | `NSEvent` key codes | `XLookupKeysym()` |
| Timer source | `QueryPerformanceCounter` | `mach_absolute_time` | `clock_gettime(CLOCK_MONOTONIC)` |
| Build dependency | Built-in (`user32`, `gdi32`) | Built-in (`-framework Cocoa`) | Requires `libx11-dev` at build time |

**User-visible difference:** On Linux, if X11 development headers are not installed at build time, the graphics library is silently omitted. Programs that call `Viper.Canvas.*` functions will fail at link time (native codegen) or trap at runtime (VM) on such builds.

### 1.9 Audio

| Aspect | Windows | macOS | Linux |
|--------|---------|-------|-------|
| Backend | WASAPI via COM (`IAudioClient`) | AudioQueue (`AudioQueueRef`) | ALSA (`snd_pcm_t`) |
| Threading model | Dedicated `HANDLE` thread + `WaitForMultipleObjects` | AudioQueue callback (OS-managed thread) | Dedicated `pthread_t` + mutex/cond |
| Synchronization | `CRITICAL_SECTION` | AudioQueue internal | `pthread_mutex_t` + `pthread_cond_t` |
| Build dependency | Built-in (`ole32`) | Built-in (`-framework AudioToolbox`) | Requires `libasound2-dev` at build time |

**User-visible difference:** Same as graphics — on Linux, if ALSA development headers are not installed, the audio library is omitted and audio functions are unavailable.

### 1.10 Numeric Parsing (Locale)

Float-to-string and string-to-float conversions use locale-independent parsing to ensure deterministic behavior, but the implementation varies.

| Platform | Method |
|----------|--------|
| Windows | `_create_locale()` + `_strtod_l()` (MSVC C locale) |
| macOS | `strtod_l()` with `<xlocale.h>` |
| Linux | `strtod()` with `setlocale()` guards |

**User-visible difference:** None — all three paths produce identical results for well-formed numeric strings. The runtime guarantees that `Viper.Fmt.Num()` and `Viper.Parse.Num()` are locale-independent and round-trip consistent.

---

## 2. Native Codegen

Viper compiles Zia programs to native machine code via its built-in code generator. The generated code differs by target architecture and operating system.

### 2.1 x86-64: SysV vs Win64 ABI

The x86-64 backend supports two ABIs. The pipeline selects the correct ABI automatically via `hostTarget()` — Win64 on Windows, SysV on macOS/Linux.

| Property | SysV AMD64 (macOS, Linux) | Win64 (Windows) |
|----------|---------------------------|-----------------|
| Integer argument registers | 6: RDI, RSI, RDX, RCX, R8, R9 | 4: RCX, RDX, R8, R9 |
| FP argument registers | 8: XMM0–XMM7 | 4: XMM0–XMM3 |
| Callee-saved GPRs | 6: RBX, R12–R15, RBP | 8: RBX, RBP, RDI, RSI, R12–R15 |
| Callee-saved FPRs | None | 10: XMM6–XMM15 |
| Red zone | 128 bytes below RSP | None |
| Shadow space | None | 32 bytes above return address |
| Varargs | `%AL` must hold XMM count | Not required |
| Stack alignment | 16-byte aligned before `CALL` | 16-byte aligned before `CALL` |
| Return registers | RAX (int), XMM0 (float) | RAX (int), XMM0 (float) |

**Implication:** Native compilation produces executables that follow the platform's native calling convention — Win64 on Windows, SysV on macOS/Linux. The generated code and runtime share the same ABI on each platform.

### 2.2 AArch64: Darwin vs Linux vs Windows

The AArch64 backend uses the AAPCS64 calling convention on all three platforms. The register allocation, argument passing, and stack frame layout are **identical**. Differences are limited to assembly-level directives and symbol naming.

| Property | Darwin (macOS) | Linux | Windows |
|----------|----------------|-------|---------|
| Symbol prefix | `_` (underscore) | None | None |
| `.type` directive | Not emitted | `.type name, @function` | Not emitted |
| `.size` directive | Not emitted | `.size name, .-name` | Not emitted |
| Object format | Mach-O | ELF | PE/COFF |

Register convention (all platforms): 8 integer args (X0–X7), 8 FP args (V0–V7), callee-saved X19–X28 + V8–V15, 16-byte stack alignment.

### 2.3 Object Formats and Symbol Mangling

| Aspect | macOS | Linux | Windows |
|--------|-------|-------|---------|
| Object format | Mach-O | ELF | PE/COFF |
| Read-only data section | `.section __TEXT,__const` | `.section .rodata` | `.section .rdata` |
| Text section | `.section __TEXT,__code` | `.text` | `.text` |
| C symbol prefix | `_main`, `_rt_print` | `main`, `rt_print` | `main`, `rt_print` |
| Alignment syntax | `.align 2` (power-of-2) | `.align 8` (bytes) | `.align 8` (bytes) |

### 2.4 Linker Integration

The native codegen pipeline uses the built-in native linker to produce the final executable on every supported host path. The host assembler may still be used for `.s -> .o` when explicitly requested, but final executable linking does not fall back to `cc` or `ld`.

| Aspect | Windows | macOS / Linux |
|--------|---------|---------------|
| Library naming | `viper_rt_core.lib` | `libviper_rt_core.a` |
| Graphics library | `vipergfx.lib` + `user32` + `gdi32` | `libvipergfx.a` + `-framework Cocoa` (macOS) or `-lX11` (Linux) |
| Audio library | `viperaud.lib` + `ole32` | `libviperaud.a` + `-framework AudioToolbox` (macOS) or `-lasound` (Linux) |
| Network library | `ws2_32.lib` | (system sockets, no extra lib) |
| Final link driver | Native PE writer + import metadata | Native Mach-O / ELF writers + import metadata |
| Executable naming | `.exe` extension | `.out`-style host convention |

---

## 3. Filesystem Path Handling

### Path Separators

| Platform | Separator | Macro |
|----------|-----------|-------|
| Windows | `\` (backslash) | `RT_PATH_SEPARATOR = '\\'` |
| macOS | `/` (forward slash) | `RT_PATH_SEPARATOR = '/'` |
| Linux | `/` (forward slash) | `RT_PATH_SEPARATOR = '/'` |

The runtime's path functions (`Viper.Path.*`) normalize paths using the platform's native separator. Windows additionally recognizes forward slashes in most contexts, but returned paths always use backslashes.

### Case Sensitivity

| Platform | Default Filesystem | Behavior |
|----------|-------------------|----------|
| Windows | NTFS | Case-insensitive (case-preserving) |
| macOS | APFS / HFS+ | Case-insensitive (case-preserving) by default |
| Linux | ext4 / XFS | Case-sensitive |

**User-visible difference:** A file created as `Hello.zia` can be opened as `hello.zia` on Windows and macOS (default), but not on Linux. Write portable code by matching filename case exactly.

### Home Directory Resolution

| Platform | Source | Typical Value |
|----------|--------|---------------|
| Windows | `%USERPROFILE%` environment variable | `C:\Users\alice` |
| macOS | `$HOME` env, fallback to `/var/root` if root | `/Users/alice` |
| Linux | `$HOME` env, fallback to `getpwuid()` | `/home/alice` |

### Temporary Directory Resolution

| Platform | Source | Typical Value |
|----------|--------|---------------|
| Windows | `GetTempPathA()` | `C:\Users\alice\AppData\Local\Temp` |
| macOS | `$TMPDIR` env, fallback to `/tmp` | `/var/folders/xx/.../T` (randomized) |
| Linux | `$TMPDIR` env, fallback to `/tmp` | `/tmp` |

### Maximum Path Length

| Platform | Limit | Constant |
|----------|-------|----------|
| Windows | 260 characters | `MAX_PATH` |
| macOS | 1024 characters | `PATH_MAX` |
| Linux | 4096 characters | `PATH_MAX` |

**User-visible difference:** Windows directory operations will fail for paths exceeding 260 characters. This is a known limitation (GAP-2). macOS and Linux support significantly longer paths.

---

## 4. Threading and Synchronization

The `Viper.Threads` module provides a uniform threading API. The underlying primitives differ by platform.

### Thread Creation and Synchronization

| Primitive | Windows | macOS / Linux |
|-----------|---------|---------------|
| Thread create | `CreateThread()` | `pthread_create()` |
| Mutex | `CRITICAL_SECTION` | `pthread_mutex_t` |
| Condition variable | `CONDITION_VARIABLE` | `pthread_cond_t` |
| Thread join signaling | `WakeAllConditionVariable()` | `pthread_cond_signal()` |
| Atomic thread ID | `InterlockedIncrement64()` | `__atomic_fetch_add()` |

### Thread-Local Storage

| Compiler | Keyword |
|----------|---------|
| MSVC | `__declspec(thread)` |
| C11-compliant (GCC/Clang) | `_Thread_local` |
| Pre-C11 GCC/Clang | `__thread` |

The runtime abstracts this via the `RT_THREAD_LOCAL` macro.

### Atomic Operations

| Operation | MSVC (Windows) | GCC / Clang (macOS / Linux) |
|-----------|---------------|----------------------------|
| Exchange | `_InterlockedExchange()` | `__atomic_exchange_n()` |
| Compare-and-swap | `_InterlockedCompareExchange()` | `__atomic_compare_exchange_n()` |
| Fetch-and-add | `_InterlockedExchangeAdd()` | `__atomic_fetch_add()` |
| Memory fence | `_mm_mfence()` (x86) / `__dmb()` (ARM64) | `__atomic_thread_fence()` |

### CPU Core Count Detection

| Platform | Method |
|----------|--------|
| Windows | `GetSystemInfo().dwNumberOfProcessors` |
| macOS | `sysctlbyname("hw.logicalcpu")` |
| Linux | `sysconf(_SC_NPROCESSORS_ONLN)` |

The parallel task pool (`Viper.Parallel`) uses this to size its worker thread pool.

---

## 5. Timers and Clock Resolution

### Sleep

| Platform | API | Typical Resolution |
|----------|-----|-------------------|
| Windows | `Sleep(ms)` | ~10–15 ms (system timer granularity) |
| macOS | `nanosleep()` with EINTR retry | ~1 ms |
| Linux | `nanosleep()` with EINTR retry | ~1 ms (kernel `CONFIG_HZ` dependent) |

**User-visible difference:** `Viper.Time.Sleep(1)` on Windows may sleep for up to 15 ms due to the default timer resolution. On macOS and Linux, the actual sleep duration is much closer to the requested value.

### Monotonic Clock

| Platform | Primary Source | Fallback | Precision |
|----------|---------------|----------|-----------|
| Windows | `QueryPerformanceCounter` | `GetTickCount64()` (~15 ms) | Sub-microsecond |
| macOS | `clock_gettime(CLOCK_MONOTONIC)` | `CLOCK_REALTIME` | Nanosecond |
| Linux | `clock_gettime(CLOCK_MONOTONIC)` | `CLOCK_REALTIME` | Nanosecond |

Used by `Viper.Time.Timer()`, `Viper.Time.ClockUs()`, and `Viper.Stopwatch`.

### Wall-Clock Time

| Platform | API |
|----------|-----|
| Windows | `rt_windows_time_ms()` (internally `GetSystemTimeAsFileTime`) |
| macOS | `gettimeofday()` |
| Linux | `clock_gettime(CLOCK_REALTIME)` |

Used by `Viper.DateTime.Now()`. All platforms return milliseconds since the Unix epoch. Results are consistent across platforms for the same wall-clock instant.

### Thread-Safe Time Formatting

| Function | Windows (MSVC) | macOS / Linux |
|----------|----------------|---------------|
| Local time | `localtime_s()` (args reversed) | `localtime_r()` |
| GMT time | `gmtime_s()` (args reversed) | `gmtime_r()` |
| String tokenize | `strtok_s()` | `strtok_r()` |

The runtime wraps these behind `rt_localtime_r()`, `rt_gmtime_r()`, and `rt_strtok_r()` for uniform usage.

---

## Known Gaps

These are known platform-specific limitations, tracked across the project.

| ID | Category | Description | Severity |
|----|----------|-------------|----------|
| GAP-1 | Filesystem | ViperDOS file watcher is a stub (no kernel inotify support) | Low |
| GAP-2 | Filesystem | Windows `MAX_PATH` (260 char) limit on directory operations | Medium |
| ~~GAP-3~~ | ~~Process~~ | ~~Resolved: Windows tests use CreateProcess self-relaunch + Job Object~~ | ~~Resolved~~ |
| ~~GAP-4~~ | ~~Codegen~~ | ~~Resolved: x86-64 native codegen now supports both SysV and Win64 ABIs~~ | ~~Resolved~~ |
| GAP-5 | Graphics | Linux requires X11 dev headers; configure fails in `REQUIRE` mode and reports explicitly in `AUTO` mode | Low |
| GAP-6 | Audio | Linux requires ALSA dev headers; configure fails in `REQUIRE` mode and reports explicitly in `AUTO` mode | Low |
| GAP-7 | Packaging | ViperDOS Windows build cannot create UEFI ESP images | Low |
