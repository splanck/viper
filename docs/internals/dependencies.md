---
status: active
audience: contributors
last-verified: 2026-07-14
---

# ViperDOS Dependencies for Viper Compiler Toolchain

This document is the core host-interface checklist for porting the Viper compiler toolchain
(`vbasic`, `zia`, `viper`, runtime, and VM) to ViperDOS. It focuses on the minimum compiler,
VM, and runtime substrate; optional graphics, audio, networking, GUI, and installer backends
also require the platform libraries called out near the end of this document.

## Quick Reference: Minimum Viable Port

To get basic compilation and VM execution working, ViperDOS must provide:

### Critical Path (Phase 1)
1. **Memory**: `malloc`, `free`, `calloc`, `realloc`
2. **Strings**: `memcpy`, `memset`, `memmove`, `strlen`, `strcmp`, `strcpy`
3. **I/O**: `read`, `write`, `open`, `close` (for file descriptors)
4. **Console**: `stdout`/`stderr` file handles, `fwrite`, `fputs`
5. **Exit**: `exit(code)`
6. **Math**: Basic floating-point support (libm core functions)

### Extended Functionality (Phase 2)
7. **Time**: `clock_gettime(CLOCK_MONOTONIC)`, `nanosleep`
8. **Terminal**: `isatty`, `tcgetattr`, `tcsetattr`, `select`
9. **Files**: `stat`, `lseek`, `mkdir`, `opendir`/`readdir`

### Threading (Phase 3)
10. **Threads**: `pthread_create`, mutex, condition variables

---

## Detailed Dependency Analysis

### 1. Memory Management

#### Critical (Required for basic operation)

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `calloc(n, size)` | `<stdlib.h>` | Runtime | Zero-initialized allocation |
| `free(ptr)` | `<stdlib.h>` | All | Heap deallocation |
| `malloc(size)` | `<stdlib.h>` | All | Heap allocation |
| `memcpy(dst, src, n)` | `<string.h>` | All | Memory copy |
| `memmove(dst, src, n)` | `<string.h>` | Runtime | Overlapping memory copy |
| `memset(ptr, val, n)` | `<string.h>` | All | Memory fill |
| `realloc(ptr, size)` | `<stdlib.h>` | Runtime | Resize allocation |

#### Atomic Operations (Required for threading)

| Builtin | Component(s) | Purpose |
|---------|--------------|---------|
| `__atomic_fetch_add` | Runtime | Atomic increment |
| `__atomic_fetch_sub` | Runtime | Atomic decrement |
| `__atomic_load_n` | Runtime | Atomic read |
| `__atomic_store_n` | Runtime | Atomic write |
| `__atomic_thread_fence` | Runtime | Memory barrier |
| `atomic_exchange_explicit` | Graphics | Event queue spin lock acquire |
| `atomic_store_explicit` | Graphics | Event queue spin lock release |

**Source locations:**
- `src/runtime/core/rt_heap.c` - Reference counting and heap-registry atomics
- `src/runtime/threads/rt_threads_posix.c` - POSIX thread ID generation
- `src/lib/graphics/src/vgfx_internal.h` - C11 `atomic_flag` for the synchronized VGFX event queue

**Notes:**
- No `mmap`/`munmap` usage - all allocation through malloc
- Runtime atomics use GCC/Clang builtins (with MSVC compatibility shims); VGFX uses an
  `atomic_bool` wrapper on C11 compilers and Interlocked operations on MSVC.

---

### 2. File I/O

#### Critical (Required for compilation)

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `close(fd)` | `<unistd.h>` | All | Close file descriptor |
| `lseek(fd, offset, whence)` | `<unistd.h>` | Runtime | Seek in file |
| `open(path, flags, mode)` | `<fcntl.h>` | All | Open file descriptor |
| `read(fd, buf, n)` | `<unistd.h>` | All | Read from file |
| `write(fd, buf, n)` | `<unistd.h>` | All | Write to file |

**Constants needed:**
```c
// Open flags
O_RDONLY, O_WRONLY, O_RDWR
O_CREAT, O_TRUNC, O_APPEND
O_EXCL

// Seek whence
SEEK_SET, SEEK_CUR, SEEK_END

// Mode bits
S_IRUSR, S_IWUSR, S_IRGRP, S_IWGRP, S_IROTH, S_IWOTH
```

**Source locations:**
- `src/runtime/io/rt_file_io.c` - File open/read/write implementation and retry logic

#### Extended (Directory operations)

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `access(path, mode)` | `<unistd.h>` | Runtime | Check file access |
| `chdir(path)` | `<unistd.h>` | Runtime | Change directory |
| `closedir(dir)` | `<dirent.h>` | Runtime | Close directory stream |
| `getcwd(buf, size)` | `<unistd.h>` | Runtime | Get current directory |
| `mkdir(path, mode)` | `<sys/stat.h>` | Runtime | Create directory |
| `opendir(path)` | `<dirent.h>` | Runtime | Open directory stream |
| `readdir(dir)` | `<dirent.h>` | Runtime | Read directory entry |
| `rename(old, new)` | `<stdio.h>` | Runtime | Rename file |
| `rmdir(path)` | `<unistd.h>` | Runtime | Remove directory |
| `stat(path, buf)` | `<sys/stat.h>` | Runtime | Get file metadata |
| `unlink(path)` | `<unistd.h>` | Runtime | Delete file |

**Source locations:**
- `src/runtime/io/rt_dir.c` - All directory operations
- `src/runtime/io/rt_file_ext.c` - File metadata operations

---

### 3. Console/Terminal I/O

#### Critical (Required for output)

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `fflush(stream)` | `<stdio.h>` | All | Flush buffer |
| `fputs(str, stream)` | `<stdio.h>` | All | Write string |
| `fwrite(ptr, size, n, stream)` | `<stdio.h>` | All | Buffered write |
| `setvbuf(stream, buf, mode, size)` | `<stdio.h>` | Runtime | Set buffering mode |

**Required streams:**
- `stdin` - Standard input (FILE*)
- `stdout` - Standard output (FILE*)
- `stderr` - Standard error (FILE*)

**Source locations:**
- `src/runtime/core/rt_output.c` - Output buffering, `fputs`, `fwrite`, and `fflush`
- `src/runtime/core/rt_io.c` - Core I/O operations

#### Terminal Control (Optional - for interactive programs)

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `fileno(stream)` | `<stdio.h>` | Runtime | Get file descriptor |
| `ioctl(fd, request, ...)` | `<sys/ioctl.h>` | Runtime | Terminal control |
| `isatty(fd)` | `<unistd.h>` | Runtime | Check if terminal |
| `select(nfds, read, write, except, timeout)` | `<sys/select.h>` | Runtime | I/O multiplexing |
| `tcgetattr(fd, termios)` | `<termios.h>` | Runtime | Get terminal settings |
| `tcsetattr(fd, when, termios)` | `<termios.h>` | Runtime | Set terminal settings |

**Termios constants needed:**
```c
ICANON    // Canonical mode
ECHO      // Echo input
VMIN      // Min chars for non-canonical read
VTIME     // Timeout for non-canonical read
TCSANOW   // Apply changes immediately
```

**Source locations:**
- `src/runtime/core/rt_term.c` - Raw mode, non-blocking key input, and TTY detection

---

### 4. Time and Clock

#### Critical (Required for timing)

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `clock_gettime(clockid, ts)` | `<time.h>` | Runtime | High-resolution time |
| `nanosleep(req, rem)` | `<time.h>` | Runtime | Sleep with nanosecond precision |
| `time(tloc)` | `<time.h>` | Runtime | Get Unix timestamp |

**Clock IDs needed:**
```c
CLOCK_MONOTONIC   // For elapsed time measurement
CLOCK_REALTIME    // For wall-clock time (fallback)
```

**Source locations:**
- `src/runtime/core/rt_time.c` - `clock_gettime` timers and `nanosleep` with EINTR retry
- `src/runtime/core/rt_datetime.c` - Local/UTC timestamp conversion

#### Date/Time (Optional - for DateTime class)

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `gettimeofday(tv, tz)` | `<sys/time.h>` | Runtime (macOS) | Millisecond time |
| `gmtime(timer)` | `<time.h>` | Runtime | Convert to UTC |
| `localtime(timer)` | `<time.h>` | Runtime | Convert to local time |
| `mktime(tm)` | `<time.h>` | Runtime | Create timestamp |
| `strftime(buf, max, fmt, tm)` | `<time.h>` | Runtime | Format time string |

**Source locations:**
- `src/runtime/core/rt_datetime.c` - Platform time acquisition, date components, and formatting

---

### 5. Threading and Synchronization

#### Thread Management (Optional - for threaded programs)

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `pthread_create(thread, attr, fn, arg)` | `<pthread.h>` | Runtime | Create thread |
| `pthread_detach(thread)` | `<pthread.h>` | Runtime | Detach thread |
| `pthread_equal(t1, t2)` | `<pthread.h>` | Runtime | Compare thread IDs |
| `pthread_self()` | `<pthread.h>` | Runtime | Get current thread ID |
| `sched_yield()` | `<sched.h>` | Runtime | Yield CPU |

**Source locations:**
- `src/runtime/threads/rt_threads_posix.c` - `pthread_create`, detach/join, and `sched_yield`

#### Mutex Operations

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `pthread_mutex_destroy(mutex)` | `<pthread.h>` | Runtime | Destroy mutex |
| `pthread_mutex_init(mutex, attr)` | `<pthread.h>` | Runtime | Initialize mutex |
| `pthread_mutex_lock(mutex)` | `<pthread.h>` | Runtime | Lock mutex |
| `pthread_mutex_unlock(mutex)` | `<pthread.h>` | Runtime | Unlock mutex |

**Source locations:**
- `src/runtime/threads/rt_threads_posix.c` - Per-thread mutex initialization and locking
- `src/runtime/threads/rt_monitor_posix.c` - Monitor-table and per-monitor mutexes

#### Condition Variables

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `pthread_cond_broadcast(cond)` | `<pthread.h>` | Runtime | Signal all waiters |
| `pthread_cond_destroy(cond)` | `<pthread.h>` | Runtime | Destroy condition var |
| `pthread_cond_init(cond, attr)` | `<pthread.h>` | Runtime | Initialize condition var |
| `pthread_cond_signal(cond)` | `<pthread.h>` | Runtime | Signal one waiter |
| `pthread_cond_timedwait(cond, mutex, ts)` | `<pthread.h>` | Runtime | Wait with timeout |
| `pthread_cond_wait(cond, mutex)` | `<pthread.h>` | Runtime | Wait on condition |

**Source locations:**
- `src/runtime/threads/rt_threads_posix.c` - Join condition waits and timed waits
- `src/runtime/threads/rt_monitor_posix.c` - Monitor condition waits and signaling

---

### 6. Math Functions

#### Core Math (Required for numeric operations)

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `ceil(x)` | `<math.h>` | Runtime | Round up |
| `fabs(x)` | `<math.h>` | Runtime | Absolute value |
| `floor(x)` | `<math.h>` | Runtime | Round down |
| `fmod(x, y)` | `<math.h>` | Runtime | Floating-point modulo |
| `isnan(x)` | `<math.h>` | Runtime | Test for NaN |
| `sqrt(x)` | `<math.h>` | Runtime | Square root |

#### Trigonometric Functions

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `acos(x)` | `<math.h>` | Runtime | Arc cosine |
| `asin(x)` | `<math.h>` | Runtime | Arc sine |
| `atan(x)` | `<math.h>` | Runtime | Arc tangent |
| `atan2(y, x)` | `<math.h>` | Runtime | Arc tangent of y/x |
| `cos(x)` | `<math.h>` | Runtime | Cosine |
| `cosh(x)` | `<math.h>` | Runtime | Hyperbolic cosine |
| `sin(x)` | `<math.h>` | Runtime | Sine |
| `sinh(x)` | `<math.h>` | Runtime | Hyperbolic sine |
| `tan(x)` | `<math.h>` | Runtime | Tangent |
| `tanh(x)` | `<math.h>` | Runtime | Hyperbolic tangent |

#### Exponential/Logarithmic Functions

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `exp(x)` | `<math.h>` | Runtime | Exponential |
| `hypot(x, y)` | `<math.h>` | Runtime | Hypotenuse |
| `log(x)` | `<math.h>` | Runtime | Natural log |
| `log10(x)` | `<math.h>` | Runtime | Base-10 log |
| `log2(x)` | `<math.h>` | Runtime | Base-2 log |

#### Rounding Functions

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `round(x)` | `<math.h>` | Runtime | Round to nearest |
| `trunc(x)` | `<math.h>` | Runtime | Truncate toward zero |

**Source locations:**
- `src/runtime/core/rt_math.c` - Math wrappers

---

### 7. Process Control

#### Critical (Required)

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `atexit(fn)` | `<stdlib.h>` | Runtime | Register exit handler |
| `exit(code)` | `<stdlib.h>` | All | Terminate process |

**Source locations:**
- `src/runtime/core/rt_heap.c` - `atexit(rt_global_shutdown)` master shutdown handler on non-Windows runtime paths (Windows builds skip CRT `atexit` and rely on process teardown)
- `src/runtime/core/rt_term.c` - `atexit` for terminal raw-mode cleanup
- `src/runtime/core/rt_args.c` - `rt_env_exit` wrapper (`ExitProcess` on Windows native PE, `exit` elsewhere)
- `src/runtime/core/rt_io.c` - Fatal error exit
- `src/runtime/network/rt_network.c` - `atexit(rt_net_cleanup_wsa)` (Windows only)

#### Process Spawning (Optional - for Exec class)

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `pclose(stream)` | `<stdio.h>` | Runtime | Close pipe |
| `pipe(fds)` | `<unistd.h>` | Runtime | Create pipe |
| `popen(cmd, type)` | `<stdio.h>` | Runtime | Open pipe to process |
| `posix_spawn(pid, path, actions, attr, argv, envp)` | `<spawn.h>` | Runtime | Spawn process |
| `waitpid(pid, status, options)` | `<sys/wait.h>` | Runtime | Wait for child |

**Source locations:**
- `src/runtime/system/rt_exec.c` - `posix_spawn`, capture pipes, waits, and environment handling

---

### 8. Environment Variables

#### Optional (For environment access)

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `getenv(name)` | `<stdlib.h>` | All | Get environment variable |
| `setenv(name, value, overwrite)` | `<stdlib.h>` | Runtime | Set environment variable |

**Source locations:**
- `src/runtime/core/rt_args.c` - Get and set environment variables
- `src/runtime/system/rt_machine.c` - User, home, and temporary-directory queries

---

### 9. String Functions

#### Critical (Required)

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `memcmp(s1, s2, n)` | `<string.h>` | All | Compare memory |
| `strcmp(s1, s2)` | `<string.h>` | All | Compare strings |
| `strcpy(dst, src)` | `<string.h>` | All | Copy string |
| `strlen(str)` | `<string.h>` | All | String length |
| `strncmp(s1, s2, n)` | `<string.h>` | All | Compare n chars |
| `strncpy(dst, src, n)` | `<string.h>` | All | Copy n chars |

#### Conversion (Required for parsing)

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `snprintf(buf, size, fmt, ...)` | `<stdio.h>` | All | Formatted print |
| `strtod(str, endptr)` | `<stdlib.h>` | All | String to double |
| `strtol(str, endptr, base)` | `<stdlib.h>` | All | String to long |
| `strtoll(str, endptr, base)` | `<stdlib.h>` | All | String to long long |

---

### 10. Error Handling

#### Required

| Symbol | Header | Component(s) | Purpose |
|--------|--------|--------------|---------|
| `assert(expr)` | `<assert.h>` | All | Debug assertions |
| `errno` | `<errno.h>` | All | Error code |

**Common errno values needed:**
```c
ENOENT    // No such file or directory
EINVAL    // Invalid argument
EACCES    // Permission denied
EPERM     // Operation not permitted
EBADF     // Bad file descriptor
EIO       // I/O error
ENOMEM    // Out of memory
EEXIST    // File exists
ENOTDIR   // Not a directory
EISDIR    // Is a directory
ENOSPC    // No space left on device
EINTR     // Interrupted system call
EAGAIN    // Try again
ERANGE    // Result too large
ETIMEDOUT // Connection timed out
```

---

## Platform-Specific Code Paths

The codebase uses `#ifdef` blocks for platform differences:

### Windows vs POSIX

```c
#if defined(_WIN32)
    // Windows-specific code
#else
    // POSIX/Unix code (Linux, macOS)
#endif
```

**Files with platform conditionals:**
- `src/runtime/io/rt_dir.c` - Directory operations
- `src/runtime/system/rt_exec.c` - Process spawning
- `src/runtime/io/rt_file_io.c` - Windows CRT mappings
- `src/runtime/core/rt_term.c` - Terminal adapters
- `src/runtime/threads/rt_threads_posix.c` and `rt_threads_win.c` - Native thread backends
- `src/runtime/core/rt_time.c` - Time implementations

### macOS-Specific

```c
#if RT_PLATFORM_MACOS
    // macOS-specific (e.g., gettimeofday)
#endif
```

**Source locations:**
- `src/runtime/core/rt_datetime.c` - macOS `gettimeofday` path
- `src/runtime/rt_platform.h` - Platform detection macros

---

## C++ Standard Library Usage

The runtime uses C++ for some threading primitives:

### Required C++ Headers (for threading only)

| Header | Usage |
|--------|-------|
| `<algorithm>` | `std::find` for queue removal |
| `<chrono>` | `std::chrono::steady_clock`, `std::chrono::milliseconds` |
| `<condition_variable>` | `std::condition_variable` |
| `<deque>` | Waiter queue management |
| `<mutex>` | `std::mutex`, `std::unique_lock` |
| `<new>` | `std::nothrow` placement new |
| `<thread>` | `std::thread::id`, `std::this_thread::get_id()` |

**Source locations:**
- `src/runtime/threads/rt_threads_primitives.cpp` - C++ threading primitives

**Note:** These are only needed for the advanced threading primitives (Gate, Barrier,
RwLock). Core threads use `rt_threads_posix.c` on POSIX and `rt_threads_win.c` on Windows.

---

## Third-Party Libraries

Viper has no downloaded or vendored product dependencies. It does use host libraries and OS
frameworks:

- standard C/C++ libraries and the platform thread API
- X11 and ALSA when Linux graphics/audio are enabled
- Cocoa, AudioToolbox, IOKit, CoreFoundation, and Security on macOS as required by enabled features
- Win32 system libraries such as `ws2_32`, `ole32`, `user32`, and `gdi32` on Windows

Feature-disabled builds omit their corresponding optional host libraries.

---

## Maintenance Note

This checklist intentionally groups interfaces by porting capability instead of publishing a
brittle function count. Re-audit the source files named above whenever a ViperDOS port changes
its enabled runtime, graphics, audio, network, GUI, or packaging feature set.
