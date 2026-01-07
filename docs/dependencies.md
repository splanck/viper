# ViperOS Dependencies for Viper Compiler Toolchain

This document catalogs all external environment dependencies required to port the Viper compiler toolchain (vbasic, ilc, runtime/VM) to ViperOS. Dependencies are grouped by category and prioritized by criticality.

## Quick Reference: Minimum Viable Port

To get basic compilation and VM execution working, ViperOS must provide:

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
| `malloc(size)` | `<stdlib.h>` | All | Heap allocation |
| `free(ptr)` | `<stdlib.h>` | All | Heap deallocation |
| `calloc(n, size)` | `<stdlib.h>` | Runtime | Zero-initialized allocation |
| `realloc(ptr, size)` | `<stdlib.h>` | Runtime | Resize allocation |
| `memset(ptr, val, n)` | `<string.h>` | All | Memory fill |
| `memcpy(dst, src, n)` | `<string.h>` | All | Memory copy |
| `memmove(dst, src, n)` | `<string.h>` | Runtime | Overlapping memory copy |

#### Atomic Operations (Required for threading)

| Builtin | Component(s) | Purpose |
|---------|--------------|---------|
| `__atomic_load_n` | Runtime | Atomic read |
| `__atomic_store_n` | Runtime | Atomic write |
| `__atomic_fetch_add` | Runtime | Atomic increment |
| `__atomic_fetch_sub` | Runtime | Atomic decrement |
| `__atomic_thread_fence` | Runtime | Memory barrier |

**Source locations:**
- `src/runtime/rt_heap.c:49,65,123,150,160,182,191` - Reference counting atomics
- `src/runtime/rt_threads.c:251` - Thread ID generation

**Notes:**
- No `mmap`/`munmap` usage - all allocation through malloc
- Atomics use GCC/Clang builtins; ARM64 has native support

---

### 2. File I/O

#### Critical (Required for compilation)

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `open(path, flags, mode)` | `<fcntl.h>` | All | Open file descriptor |
| `close(fd)` | `<unistd.h>` | All | Close file descriptor |
| `read(fd, buf, n)` | `<unistd.h>` | All | Read from file |
| `write(fd, buf, n)` | `<unistd.h>` | All | Write to file |
| `lseek(fd, offset, whence)` | `<unistd.h>` | Runtime | Seek in file |

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
- `src/runtime/rt_file_io.c:328-340` - File open implementation
- `src/runtime/rt_file_io.c:397-416` - Read byte
- `src/runtime/rt_file_io.c:585-629` - Write with retry

#### Extended (Directory operations)

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `stat(path, buf)` | `<sys/stat.h>` | Runtime | Get file metadata |
| `mkdir(path, mode)` | `<sys/stat.h>` | Runtime | Create directory |
| `rmdir(path)` | `<unistd.h>` | Runtime | Remove directory |
| `unlink(path)` | `<unistd.h>` | Runtime | Delete file |
| `rename(old, new)` | `<stdio.h>` | Runtime | Rename file |
| `getcwd(buf, size)` | `<unistd.h>` | Runtime | Get current directory |
| `chdir(path)` | `<unistd.h>` | Runtime | Change directory |
| `opendir(path)` | `<dirent.h>` | Runtime | Open directory stream |
| `readdir(dir)` | `<dirent.h>` | Runtime | Read directory entry |
| `closedir(dir)` | `<dirent.h>` | Runtime | Close directory stream |
| `access(path, mode)` | `<unistd.h>` | Runtime | Check file access |

**Source locations:**
- `src/runtime/rt_dir.c` - All directory operations
- `src/runtime/rt_file_ext.c` - File metadata operations

---

### 3. Console/Terminal I/O

#### Critical (Required for output)

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `fwrite(ptr, size, n, stream)` | `<stdio.h>` | All | Buffered write |
| `fputs(str, stream)` | `<stdio.h>` | All | Write string |
| `fflush(stream)` | `<stdio.h>` | All | Flush buffer |
| `setvbuf(stream, buf, mode, size)` | `<stdio.h>` | Runtime | Set buffering mode |

**Required streams:**
- `stdin` - Standard input (FILE*)
- `stdout` - Standard output (FILE*)
- `stderr` - Standard error (FILE*)

**Source locations:**
- `src/runtime/rt_output.c:57` - `setvbuf` for output buffering
- `src/runtime/rt_output.c:66-78` - `fputs`, `fwrite`, `fflush`
- `src/runtime/rt_io.c` - Core I/O operations

#### Terminal Control (Optional - for interactive programs)

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `isatty(fd)` | `<unistd.h>` | Runtime | Check if terminal |
| `fileno(stream)` | `<stdio.h>` | Runtime | Get file descriptor |
| `tcgetattr(fd, termios)` | `<termios.h>` | Runtime | Get terminal settings |
| `tcsetattr(fd, when, termios)` | `<termios.h>` | Runtime | Set terminal settings |
| `select(nfds, read, write, except, timeout)` | `<sys/select.h>` | Runtime | I/O multiplexing |
| `ioctl(fd, request, ...)` | `<sys/ioctl.h>` | Runtime | Terminal control |

**Termios constants needed:**
```c
ICANON    // Canonical mode
ECHO      // Echo input
VMIN      // Min chars for non-canonical read
VTIME     // Timeout for non-canonical read
TCSANOW   // Apply changes immediately
```

**Source locations:**
- `src/runtime/rt_term.c:100-140` - Terminal raw mode handling
- `src/runtime/rt_term.c:432-508` - Non-blocking key input
- `src/runtime/rt_term.c:163-169` - TTY detection

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
- `src/runtime/rt_time.c:243-266` - `clock_gettime` for timer
- `src/runtime/rt_time.c:203-216` - `nanosleep` with EINTR retry
- `src/runtime/rt_datetime.c:111-114` - `time()` for timestamps

#### Date/Time (Optional - for DateTime class)

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `localtime(timer)` | `<time.h>` | Runtime | Convert to local time |
| `gmtime(timer)` | `<time.h>` | Runtime | Convert to UTC |
| `mktime(tm)` | `<time.h>` | Runtime | Create timestamp |
| `strftime(buf, max, fmt, tm)` | `<time.h>` | Runtime | Format time string |
| `gettimeofday(tv, tz)` | `<sys/time.h>` | Runtime (macOS) | Millisecond time |

**Source locations:**
- `src/runtime/rt_datetime.c:151-164` - Platform-specific ms time
- `src/runtime/rt_datetime.c:187-397` - Date component extraction
- `src/runtime/rt_datetime.c:450-476` - Time formatting

---

### 5. Threading and Synchronization

#### Thread Management (Optional - for threaded programs)

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `pthread_create(thread, attr, fn, arg)` | `<pthread.h>` | Runtime | Create thread |
| `pthread_detach(thread)` | `<pthread.h>` | Runtime | Detach thread |
| `pthread_self()` | `<pthread.h>` | Runtime | Get current thread ID |
| `pthread_equal(t1, t2)` | `<pthread.h>` | Runtime | Compare thread IDs |
| `sched_yield()` | `<sched.h>` | Runtime | Yield CPU |

**Source locations:**
- `src/runtime/rt_threads.c:375-421` - Thread creation with `pthread_create`
- `src/runtime/rt_threads.c:737-740` - `sched_yield` wrapper

#### Mutex Operations

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `pthread_mutex_init(mutex, attr)` | `<pthread.h>` | Runtime | Initialize mutex |
| `pthread_mutex_destroy(mutex)` | `<pthread.h>` | Runtime | Destroy mutex |
| `pthread_mutex_lock(mutex)` | `<pthread.h>` | Runtime | Lock mutex |
| `pthread_mutex_unlock(mutex)` | `<pthread.h>` | Runtime | Unlock mutex |

**Source locations:**
- `src/runtime/rt_threads.c:392-393` - Mutex init
- `src/runtime/rt_monitor.c:268,290-291` - Global mutex usage

#### Condition Variables

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `pthread_cond_init(cond, attr)` | `<pthread.h>` | Runtime | Initialize condition var |
| `pthread_cond_destroy(cond)` | `<pthread.h>` | Runtime | Destroy condition var |
| `pthread_cond_wait(cond, mutex)` | `<pthread.h>` | Runtime | Wait on condition |
| `pthread_cond_timedwait(cond, mutex, ts)` | `<pthread.h>` | Runtime | Wait with timeout |
| `pthread_cond_signal(cond)` | `<pthread.h>` | Runtime | Signal one waiter |
| `pthread_cond_broadcast(cond)` | `<pthread.h>` | Runtime | Signal all waiters |

**Source locations:**
- `src/runtime/rt_threads.c:470` - `pthread_cond_wait`
- `src/runtime/rt_threads.c:611` - `pthread_cond_timedwait`
- `src/runtime/rt_monitor.c:316,477` - Condition signaling

---

### 6. Math Functions

#### Core Math (Required for numeric operations)

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `sqrt(x)` | `<math.h>` | Runtime | Square root |
| `floor(x)` | `<math.h>` | Runtime | Round down |
| `ceil(x)` | `<math.h>` | Runtime | Round up |
| `fabs(x)` | `<math.h>` | Runtime | Absolute value |
| `fmod(x, y)` | `<math.h>` | Runtime | Floating-point modulo |
| `isnan(x)` | `<math.h>` | Runtime | Test for NaN |

#### Trigonometric Functions

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `sin(x)` | `<math.h>` | Runtime | Sine |
| `cos(x)` | `<math.h>` | Runtime | Cosine |
| `tan(x)` | `<math.h>` | Runtime | Tangent |
| `asin(x)` | `<math.h>` | Runtime | Arc sine |
| `acos(x)` | `<math.h>` | Runtime | Arc cosine |
| `atan(x)` | `<math.h>` | Runtime | Arc tangent |
| `atan2(y, x)` | `<math.h>` | Runtime | Arc tangent of y/x |
| `sinh(x)` | `<math.h>` | Runtime | Hyperbolic sine |
| `cosh(x)` | `<math.h>` | Runtime | Hyperbolic cosine |
| `tanh(x)` | `<math.h>` | Runtime | Hyperbolic tangent |

#### Exponential/Logarithmic Functions

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `exp(x)` | `<math.h>` | Runtime | Exponential |
| `log(x)` | `<math.h>` | Runtime | Natural log |
| `log10(x)` | `<math.h>` | Runtime | Base-10 log |
| `log2(x)` | `<math.h>` | Runtime | Base-2 log |
| `hypot(x, y)` | `<math.h>` | Runtime | Hypotenuse |

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
| `exit(code)` | `<stdlib.h>` | All | Terminate process |
| `atexit(fn)` | `<stdlib.h>` | Runtime | Register exit handler |

**Source locations:**
- `src/runtime/rt_args.c:460-462` - `rt_env_exit` wrapper
- `src/runtime/rt_term.c:124` - `atexit` for terminal cleanup
- `src/runtime/rt_io.c:64` - Fatal error exit

#### Process Spawning (Optional - for Exec class)

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `posix_spawn(pid, path, actions, attr, argv, envp)` | `<spawn.h>` | Runtime | Spawn process |
| `waitpid(pid, status, options)` | `<sys/wait.h>` | Runtime | Wait for child |
| `pipe(fds)` | `<unistd.h>` | Runtime | Create pipe |
| `popen(cmd, type)` | `<stdio.h>` | Runtime | Open pipe to process |
| `pclose(stream)` | `<stdio.h>` | Runtime | Close pipe |

**Source locations:**
- `src/runtime/rt_exec.c:196,253` - `posix_spawn` usage
- `src/runtime/rt_exec.c:107` - `environ` global

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
| `strlen(str)` | `<string.h>` | All | String length |
| `strcmp(s1, s2)` | `<string.h>` | All | Compare strings |
| `strncmp(s1, s2, n)` | `<string.h>` | All | Compare n chars |
| `strcpy(dst, src)` | `<string.h>` | All | Copy string |
| `strncpy(dst, src, n)` | `<string.h>` | All | Copy n chars |
| `strcat(dst, src)` | `<string.h>` | Runtime | Concatenate strings |
| `memcmp(s1, s2, n)` | `<string.h>` | All | Compare memory |

#### Conversion (Required for parsing)

| Function | Header | Component(s) | Purpose |
|----------|--------|--------------|---------|
| `strtol(str, endptr, base)` | `<stdlib.h>` | All | String to long |
| `strtoll(str, endptr, base)` | `<stdlib.h>` | All | String to long long |
| `strtod(str, endptr)` | `<stdlib.h>` | All | String to double |
| `snprintf(buf, size, fmt, ...)` | `<stdio.h>` | All | Formatted print |

---

### 10. Error Handling

#### Required

| Symbol | Header | Component(s) | Purpose |
|--------|--------|--------------|---------|
| `errno` | `<errno.h>` | All | Error code |
| `assert(expr)` | `<assert.h>` | All | Debug assertions |

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
- `src/runtime/rt_file_io.c:39-91` - Windows CRT mappings
- `src/runtime/rt_term.c:31-43` - Terminal headers
- `src/runtime/rt_time.c:58-168` - Time implementations
- `src/runtime/rt_threads.c:124-178` - Windows thread stubs
- `src/runtime/rt_exec.c:97-108` - Process spawning
- `src/runtime/rt_dir.c:92-107` - Directory operations

### macOS-Specific

```c
#if RT_PLATFORM_MACOS
    // macOS-specific (e.g., gettimeofday)
#endif
```

**Source locations:**
- `src/runtime/rt_datetime.c:83-86,153-158` - macOS time functions
- `src/runtime/rt_platform.h` - Platform detection macros

---

## C++ Standard Library Usage

The runtime uses C++ for some threading primitives:

### Required C++ Headers (for threading only)

| Header | Usage |
|--------|-------|
| `<mutex>` | `std::mutex`, `std::unique_lock` |
| `<condition_variable>` | `std::condition_variable` |
| `<thread>` | `std::thread::id`, `std::this_thread::get_id()` |
| `<chrono>` | `std::chrono::steady_clock`, `std::chrono::milliseconds` |
| `<deque>` | Waiter queue management |
| `<algorithm>` | `std::find` for queue removal |
| `<new>` | `std::nothrow` placement new |

**Source locations:**
- `src/runtime/rt_threads_primitives.cpp:34-42` - C++ headers for threading

**Note:** These are only needed for the advanced threading primitives (Gate, Barrier, RwLock). The core threading (`rt_threads.c`) uses pthreads directly.

---

## Third-Party Libraries

**None required.** The Viper runtime is self-contained and only depends on:
- Standard C library (libc)
- POSIX threads (pthreads) - for threading support
- C++ standard library - for some threading primitives

---

## Implementation Priority for ViperOS

### Phase 1: Minimal Compiler (No Runtime)
Get vbasic and ilc working to compile programs:
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
