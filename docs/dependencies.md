# ViperDOS Dependencies for Viper Compiler Toolchain

This document catalogs all external environment dependencies required to port the Viper compiler toolchain (vbasic, zia, viper, runtime/VM) to ViperDOS. Dependencies are grouped by category and prioritized by criticality.

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

**Source locations:**
- `src/runtime/rt_heap.c:51,67,146,169,178,201,210` - Reference counting atomics
- `src/runtime/rt_threads.c:500` - Thread ID generation (POSIX)

**Notes:**
- No `mmap`/`munmap` usage - all allocation through malloc
- Atomics use GCC/Clang builtins; ARM64 has native support

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
- `src/runtime/rt_file_io.c:323-360` - File open implementation
- `src/runtime/rt_file_io.c:397-439` - Read byte
- `src/runtime/rt_file_io.c:599-643` - Write with retry

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
- `src/runtime/rt_dir.c` - All directory operations
- `src/runtime/rt_file_ext.c` - File metadata operations

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
- `src/runtime/rt_output.c:57` - `setvbuf` for output buffering
- `src/runtime/rt_output.c:66-78` - `fputs`, `fwrite`, `fflush` (exact lines)
- `src/runtime/rt_io.c` - Core I/O operations

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
- `src/runtime/rt_term.c:99-148` - Terminal raw mode handling (`init_term_cache`, `rt_term_enable_raw_mode`)
- `src/runtime/rt_term.c:397-508` - Non-blocking key input (`readkey_nonblocking`)
- `src/runtime/rt_term.c:171-181` - TTY detection (`stdout_isatty`)

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
- `src/runtime/rt_time.c:281-335` - `clock_gettime` for timer (CLOCK_MONOTONIC and CLOCK_REALTIME)
- `src/runtime/rt_time.c:182-244` - `nanosleep` with EINTR retry
- `src/runtime/rt_datetime.c:197` - `localtime` for timestamps

#### Date/Time (Optional - for DateTime class)

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `gettimeofday(tv, tz)` | `<sys/time.h>` | Runtime (macOS) | Millisecond time |
| `gmtime(timer)` | `<time.h>` | Runtime | Convert to UTC |
| `localtime(timer)` | `<time.h>` | Runtime | Convert to local time |
| `mktime(tm)` | `<time.h>` | Runtime | Create timestamp |
| `strftime(buf, max, fmt, tm)` | `<time.h>` | Runtime | Format time string |

**Source locations:**
- `src/runtime/rt_datetime.c:144-165` - Platform-specific ms time (gettimeofday on macOS)
- `src/runtime/rt_datetime.c:197-408` - Date component extraction
- `src/runtime/rt_datetime.c:450-482` - Time formatting

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
- `src/runtime/rt_threads.c:604-660` - Thread creation with `pthread_create`
- `src/runtime/rt_threads.c:984-988` - `sched_yield` wrapper

#### Mutex Operations

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `pthread_mutex_destroy(mutex)` | `<pthread.h>` | Runtime | Destroy mutex |
| `pthread_mutex_init(mutex, attr)` | `<pthread.h>` | Runtime | Initialize mutex |
| `pthread_mutex_lock(mutex)` | `<pthread.h>` | Runtime | Lock mutex |
| `pthread_mutex_unlock(mutex)` | `<pthread.h>` | Runtime | Unlock mutex |

**Source locations:**
- `src/runtime/rt_threads.c:641` - Mutex init (`pthread_mutex_init`)
- `src/runtime/rt_monitor.c:794,810-835` - Global monitor table mutex usage

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
- `src/runtime/rt_threads.c:719` - `pthread_cond_wait`
- `src/runtime/rt_threads.c:860` - `pthread_cond_timedwait`
- `src/runtime/rt_monitor.c:858,1436,1502` - Condition signaling (`pthread_cond_signal`)

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
- `src/runtime/rt_math.c:41-464` - All math wrappers

---

### 7. Process Control

#### Critical (Required)

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `atexit(fn)` | `<stdlib.h>` | Runtime | Register exit handler |
| `exit(code)` | `<stdlib.h>` | All | Terminate process |

**Source locations:**
- `src/runtime/core/rt_heap.c` - `atexit(rt_global_shutdown)` — master shutdown handler (GC finalizer sweep, audio, legacy context, string intern, GC tables, pool slabs)
- `src/runtime/core/rt_term.c` - `atexit` for terminal raw-mode cleanup
- `src/runtime/core/rt_args.c` - `rt_env_exit` wrapper
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
- `src/runtime/rt_exec.c:203,253` - `posix_spawn` usage
- `src/runtime/rt_exec.c:108,114` - `environ` global (conditional declarations)

---

### 8. Environment Variables

#### Optional (For environment access)

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `getenv(name)` | `<stdlib.h>` | All | Get environment variable |
| `setenv(name, value, overwrite)` | `<stdlib.h>` | Runtime | Set environment variable |
| `unsetenv(name)` | `<stdlib.h>` | Runtime | Unset environment variable |

**Source locations:**
- `src/runtime/rt_args.c:400` - Get environment variable
- `src/runtime/rt_args.c:449` - Set environment variable
- `src/runtime/rt_machine.c:196-265` - Get user/home/temp directories

---

### 9. String Functions

#### Critical (Required)

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `memcmp(s1, s2, n)` | `<string.h>` | All | Compare memory |
| `strcat(dst, src)` | `<string.h>` | Runtime | Concatenate strings |
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
- `src/runtime/rt_dir.c:92-107` - Directory operations
- `src/runtime/rt_exec.c:97-108` - Process spawning (platform conditional)
- `src/runtime/rt_file_io.c:40-91` - Windows CRT mappings
- `src/runtime/rt_term.c:31-71` - Terminal headers (platform conditionals)
- `src/runtime/rt_threads.c:127` - Windows thread stubs (`#if defined(_WIN32)`)
- `src/runtime/rt_time.c:58-168` - Time implementations (platform selection)

### macOS-Specific

```c
#if RT_PLATFORM_MACOS
    // macOS-specific (e.g., gettimeofday)
#endif
```

**Source locations:**
- `src/runtime/rt_datetime.c:82,161-163` - macOS time functions (`RT_PLATFORM_MACOS`, `gettimeofday`)
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
- `src/runtime/rt_threads_primitives.cpp:34-41` - C++ headers for threading

**Note:** These are only needed for the advanced threading primitives (Gate, Barrier, RwLock). The core threading (`rt_threads.c`) uses pthreads directly.

---

## Third-Party Libraries

**None required.** The Viper runtime is self-contained and only depends on:
- Standard C library (libc)
- POSIX threads (pthreads) - for threading support
- C++ standard library - for some threading primitives

---

## Implementation Priority for ViperDOS

### Phase 1: Minimal Compiler (No Runtime)
Get vbasic and viper working to compile programs:
1. Memory: `malloc`, `free`, `calloc`, `realloc`
2. String: `memcpy`, `memset`, `strlen`, `strcmp`, `strcpy`, `snprintf`
3. File I/O: `open`, `close`, `read`, `write`
4. Console: `stdout`, `fwrite`, `fputs`, `fflush`
5. Exit: `exit`
6. Error: `errno`

### Phase 2: Basic VM Execution
Run simple programs without I/O:
1. Math: Core `<math.h>` functions
2. Time: `clock_gettime(CLOCK_MONOTONIC)`, `nanosleep`
3. Heap: Atomic operations for reference counting

### Phase 3: Interactive Programs
Support terminal I/O:
1. Terminal: `isatty`, `tcgetattr`, `tcsetattr`, `select`
2. Time: Date/time functions

### Phase 4: Full Runtime
Complete functionality:
1. Threading: Full pthreads support
2. Files: Directory operations, file metadata
3. Process: `posix_spawn`, `waitpid`
4. Environment: `getenv`, `setenv`

---

## Summary Statistics

| Category | Critical | Extended | Total |
|----------|----------|----------|-------|
| Memory | 7 | 5 atomics | 12 |
| File I/O | 5 | 11 | 16 |
| Console | 4 | 6 | 10 |
| Time | 3 | 5 | 8 |
| Threading | 0 | 13 | 13 |
| Math | 6 | 18 | 24 |
| Process | 2 | 5 | 7 |
| Environment | 0 | 3 | 3 |
| String | 11 | 0 | 11 |
| Error | 2 + errno values | 0 | ~15 |

**Total unique functions: ~120**
**Critical path functions: ~35**
