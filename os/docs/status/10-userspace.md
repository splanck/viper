# User Space

**Status:** Complete with libc, C++ runtime, and interactive shell
**Location:** `user/`
**SLOC:** ~7,300

## Overview

User space consists of the `vinit` init process, a complete freestanding C library (`libc`), C++ standard library headers, and syscall wrappers. `vinit` is loaded from the disk image as the first user-space process and provides an Amiga-inspired interactive shell for debugging and demonstration. The libc enables portable POSIX-like application development without external dependencies.

---

## Components

### 1. Init Process (`vinit/vinit.cpp`)

**Status:** Complete interactive shell for bring-up

`vinit` is a freestanding user-space program that:
- Runs as the first user-mode process (started by the kernel)
- Provides an Amiga-inspired interactive command shell
- Exercises kernel syscalls for filesystem, networking, TLS, and capabilities
- Operates without libc or a hosted C++ runtime

**Entry Point:** `_start()` → prints banner → runs `shell_loop()` → `sys::exit(0)`

**Shell Features:**

| Feature | Description |
|---------|-------------|
| Line Editing | Left/right cursor, insert/delete |
| History | 16-entry ring buffer with up/down navigation |
| Tab Completion | Command-name completion for built-in commands |
| ANSI Terminal | Escape sequence handling for cursor control |
| Return Codes | Amiga-style: OK=0, WARN=5, ERROR=10, FAIL=20 |

**Built-in Commands:**

| Command | Description |
|---------|-------------|
| Help | Show available commands |
| chdir \<path\> | Change current directory |
| cwd | Print current working directory |
| Dir [path] | Brief directory listing |
| List [path] | Detailed directory listing |
| Type \<file\> | Display file contents |
| Copy \<src\> \<dst\> | Copy files |
| Delete \<path\> | Delete file/directory |
| MakeDir \<dir\> | Create directory |
| Rename \<old\> \<new\> | Rename files |
| Fetch \<url\> | HTTP/HTTPS GET request |
| Run \<program\> | Execute user program |
| Cls | Clear screen (ANSI) |
| Echo [text] | Print text |
| Version | Show OS version |
| Uptime | Show system uptime |
| Avail | Show memory availability |
| Status | Show running tasks |
| Caps [handle] | Show/test capabilities |
| Assign | List logical device assigns |
| Path [path] | Resolve assign-prefixed path |
| History | Show command history |
| Why | Explain last error |
| Date / Time | (Placeholder - not yet implemented) |
| EndShell | Exit shell |

**Networking Demo:**
The `Fetch` command demonstrates the full networking stack:
```
Fetch https://example.com
```
1. Parses URL (scheme, host, port, path)
2. Resolves hostname via DNS
3. Creates TCP socket and connects
4. For HTTPS: performs TLS 1.3 handshake
5. Sends HTTP/1.0 GET request
6. Displays response and TLS session info

**Minimal Runtime:**
`vinit` implements minimal freestanding helpers:
- `strlen()`, `streq()`, `strstart()` - string utilities
- `memcpy()`, `memmove()` - memory operations
- `puts()`, `putchar()`, `put_num()`, `put_hex()` - console output
- `readline()` - line editing with history

---

### 2. Syscall Wrapper Library (`syscall.hpp`)

**Status:** Complete header-only syscall bindings

A freestanding-friendly header providing:
- Low-level `svc #0` inline assembly wrappers
- Typed syscall wrappers for all kernel APIs
- Shared ABI structures from `include/viperos/`

**Syscall ABI (AArch64):**
| Register | Purpose |
|----------|---------|
| x8 | Syscall number |
| x0-x5 | Input arguments |
| x0 | VError code (output, 0=success) |
| x1-x3 | Result values (output) |

**Syscall Invokers:**
```cpp
SyscallResult syscall0(u64 num);
SyscallResult syscall1(u64 num, u64 arg0);
SyscallResult syscall2(u64 num, u64 arg0, u64 arg1);
SyscallResult syscall3(u64 num, u64 arg0, u64 arg1, u64 arg2);
SyscallResult syscall4(u64 num, u64 arg0, u64 arg1, u64 arg2, u64 arg3);
```

**Syscall Categories:**

**Task/Process:**
| Wrapper | Syscall | Description |
|---------|---------|-------------|
| `exit(code)` | SYS_TASK_EXIT | Terminate process |
| `task_list(buf, max)` | SYS_TASK_LIST | Enumerate tasks |

**Console I/O:**
| Wrapper | Syscall | Description |
|---------|---------|-------------|
| `print(msg)` | SYS_DEBUG_PRINT | Write debug string |
| `putchar(c)` | SYS_PUTCHAR | Write character |
| `getchar()` | SYS_GETCHAR | Read character (blocking via poll) |
| `try_getchar()` | SYS_GETCHAR | Read character (non-blocking) |

**Path-based File I/O:**
| Wrapper | Syscall | Description |
|---------|---------|-------------|
| `open(path, flags)` | SYS_OPEN | Open file/directory |
| `close(fd)` | SYS_CLOSE | Close descriptor |
| `read(fd, buf, len)` | SYS_READ | Read data |
| `write(fd, buf, len)` | SYS_WRITE | Write data |
| `lseek(fd, off, whence)` | SYS_LSEEK | Seek position |
| `stat(path, st)` | SYS_STAT | Get file info |
| `fstat(fd, st)` | SYS_FSTAT | Get fd info |
| `readdir(fd, buf, len)` | SYS_READDIR | Read directory entries |
| `mkdir(path)` | SYS_MKDIR | Create directory |
| `rmdir(path)` | SYS_RMDIR | Remove directory |
| `unlink(path)` | SYS_UNLINK | Delete file |
| `rename(old, new)` | SYS_RENAME | Rename/move |

**Handle-based File I/O:**
| Wrapper | Syscall | Description |
|---------|---------|-------------|
| `fs_open_root()` | SYS_FS_OPEN_ROOT | Get root handle |
| `fs_open(dir, name, flags)` | SYS_FS_OPEN | Open relative to dir |
| `io_read(h, buf, len)` | SYS_IO_READ | Read from handle |
| `io_write(h, buf, len)` | SYS_IO_WRITE | Write to handle |
| `io_seek(h, off, whence)` | SYS_IO_SEEK | Seek handle |
| `fs_read_dir(h, entry)` | SYS_FS_READ_DIR | Read dir entry |
| `fs_rewind_dir(h)` | SYS_FS_REWIND_DIR | Reset enumeration |
| `fs_close(h)` | SYS_FS_CLOSE | Close handle |

**Polling:**
| Wrapper | Syscall | Description |
|---------|---------|-------------|
| `poll_create()` | SYS_POLL_CREATE | Create poll set |
| `poll_add(id, h, mask)` | SYS_POLL_ADD | Add to poll set |
| `poll_remove(id, h)` | SYS_POLL_REMOVE | Remove from poll set |
| `poll_wait(id, ev, max, timeout)` | SYS_POLL_WAIT | Wait for events |

**Networking:**
| Wrapper | Syscall | Description |
|---------|---------|-------------|
| `socket_create()` | SYS_SOCKET_CREATE | Create TCP socket |
| `socket_connect(s, ip, port)` | SYS_SOCKET_CONNECT | Connect socket |
| `socket_send(s, data, len)` | SYS_SOCKET_SEND | Send data |
| `socket_recv(s, buf, len)` | SYS_SOCKET_RECV | Receive data |
| `socket_close(s)` | SYS_SOCKET_CLOSE | Close socket |
| `dns_resolve(host, ip)` | SYS_DNS_RESOLVE | Resolve hostname |

**TLS:**
| Wrapper | Syscall | Description |
|---------|---------|-------------|
| `tls_create(sock, host, verify)` | SYS_TLS_CREATE | Create TLS session |
| `tls_handshake(s)` | SYS_TLS_HANDSHAKE | Perform handshake |
| `tls_send(s, data, len)` | SYS_TLS_SEND | Send encrypted |
| `tls_recv(s, buf, len)` | SYS_TLS_RECV | Receive decrypted |
| `tls_close(s)` | SYS_TLS_CLOSE | Close session |
| `tls_info(s, info)` | SYS_TLS_INFO | Get session info |

**Capabilities:**
| Wrapper | Syscall | Description |
|---------|---------|-------------|
| `cap_derive(h, rights)` | SYS_CAP_DERIVE | Derive handle |
| `cap_revoke(h)` | SYS_CAP_REVOKE | Revoke handle |
| `cap_query(h, info)` | SYS_CAP_QUERY | Query handle |
| `cap_list(buf, max)` | SYS_CAP_LIST | List capabilities |

**Assigns:**
| Wrapper | Syscall | Description |
|---------|---------|-------------|
| `assign_set(name, h)` | SYS_ASSIGN_SET | Create assign |
| `assign_get(name, h)` | SYS_ASSIGN_GET | Lookup assign |
| `assign_remove(name)` | SYS_ASSIGN_REMOVE | Remove assign |
| `assign_list(buf, max, count)` | SYS_ASSIGN_LIST | List assigns |
| `assign_resolve(path, h)` | SYS_ASSIGN_RESOLVE | Resolve path |

**System Info:**
| Wrapper | Syscall | Description |
|---------|---------|-------------|
| `uptime()` | SYS_UPTIME | Get tick count |
| `mem_info(info)` | SYS_MEM_INFO | Get memory stats |

**Memory Management:**
| Wrapper | Syscall | Description |
|---------|---------|-------------|
| `sbrk(increment)` | SYS_SBRK | Adjust program break |

---

### 3. Complete Freestanding libc (`libc/`)

**Status:** Full C standard library for freestanding environment

A complete freestanding C library providing POSIX-like functionality for user-space programs. All functions work without external dependencies.

**Implemented Headers:**

**`<stdio.h>` - Standard I/O:**
| Function | Description |
|----------|-------------|
| `printf(fmt, ...)` | Formatted output to stdout |
| `fprintf(stream, fmt, ...)` | Formatted output to stream |
| `sprintf(str, fmt, ...)` | Formatted output to string |
| `snprintf(str, n, fmt, ...)` | Safe formatted output |
| `vprintf`, `vfprintf`, `vsprintf`, `vsnprintf` | Variadic versions |
| `sscanf(str, fmt, ...)` | Parse formatted input |
| `puts(s)` | Print string with newline |
| `fputs(s, stream)` | Print string to stream |
| `putchar(c)` / `fputc(c, stream)` | Print character |
| `getchar()` / `fgetc(stream)` | Read character |
| `fgets(s, n, stream)` | Read line |
| `ferror(stream)` / `feof(stream)` | Error/EOF check |
| `clearerr(stream)` | Clear error state |
| `fflush(stream)` | Flush output |
| `setvbuf(stream, buf, mode, size)` | Set buffering mode |
| `setbuf(stream, buf)` | Set buffer |
| `setlinebuf(stream)` | Set line buffering |

**Buffering modes:** `_IOFBF` (full), `_IOLBF` (line), `_IONBF` (none)

**Format specifiers:** `%d`, `%i`, `%u`, `%x`, `%X`, `%p`, `%s`, `%c`, `%%`, `%ld`, `%lu`, `%lx`, `%lld`, `%llu`

**`<string.h>` - String Operations:**
| Function | Description |
|----------|-------------|
| `strlen(s)` / `strnlen(s, n)` | String length |
| `strcpy` / `strncpy` / `strlcpy` | Copy string |
| `strcat` / `strncat` / `strlcat` | Concatenate string |
| `strcmp` / `strncmp` | Compare strings |
| `strcasecmp` / `strncasecmp` | Case-insensitive compare |
| `strchr` / `strrchr` | Find character |
| `strstr` | Find substring |
| `strpbrk` | Find any of characters |
| `strspn` / `strcspn` | Span of characters |
| `strtok_r` | Tokenize string (reentrant) |
| `strdup` / `strndup` | Duplicate string |
| `memcpy` / `memmove` | Copy memory |
| `memset` / `memchr` / `memcmp` | Memory operations |

**`<stdlib.h>` - Utilities:**
| Function | Description |
|----------|-------------|
| `malloc(size)` / `free(ptr)` | Heap allocation |
| `calloc(n, size)` / `realloc(ptr, size)` | Extended allocation |
| `exit(code)` / `abort()` / `_Exit()` | Process termination |
| `atexit(func)` | Register exit handler |
| `getenv(name)` | Get environment variable |
| `setenv(name, value, overwrite)` | Set environment variable |
| `unsetenv(name)` | Remove environment variable |
| `putenv(string)` | Add to environment |
| `atoi` / `atol` / `atoll` | String to integer |
| `strtol` / `strtoul` | String to long with base |
| `strtoll` / `strtoull` | String to long long |
| `abs` / `labs` / `llabs` | Absolute value |
| `div` / `ldiv` / `lldiv` | Integer division |
| `qsort(base, n, size, cmp)` | Quicksort (insertion sort) |
| `bsearch(key, base, n, size, cmp)` | Binary search |
| `rand()` / `srand(seed)` | Random numbers (LCG) |

**`<ctype.h>` - Character Classification:**
| Function | Description |
|----------|-------------|
| `isalnum` / `isalpha` | Alphanumeric/alphabetic |
| `isdigit` / `isxdigit` | Decimal/hex digit |
| `islower` / `isupper` | Case check |
| `isspace` / `isblank` | Whitespace |
| `isprint` / `isgraph` | Printable |
| `iscntrl` / `ispunct` | Control/punctuation |
| `tolower` / `toupper` | Case conversion |

**`<time.h>` - Time Functions:**
| Function | Description |
|----------|-------------|
| `clock()` | CPU time (ms since boot) |
| `time(tloc)` | Current time (seconds) |
| `difftime(t1, t0)` | Time difference |
| `nanosleep(req, rem)` | High-precision sleep |
| `clock_gettime(clk_id, tp)` | POSIX clock access |
| `clock_getres(clk_id, res)` | Clock resolution |
| `gettimeofday(tv, tz)` | BSD time function |
| `gmtime(t)` / `localtime(t)` | Break down time |
| `mktime(tm)` | Construct time |
| `strftime(s, max, fmt, tm)` | Format time string |

**Clock IDs:** `CLOCK_REALTIME`, `CLOCK_MONOTONIC`

**`<unistd.h>` - POSIX Functions:**
| Function | Description |
|----------|-------------|
| `read` / `write` / `close` | File I/O |
| `lseek(fd, off, whence)` | Seek position |
| `dup` / `dup2` | Duplicate file descriptor |
| `getpid()` / `getppid()` | Process IDs |
| `sleep(sec)` / `usleep(usec)` | Sleep |
| `getcwd(buf, size)` | Get working directory |
| `chdir(path)` | Change directory |
| `isatty(fd)` | Terminal check |
| `sysconf(name)` | System configuration |
| `sbrk(increment)` | Adjust program break |

**`<errno.h>` - Error Handling:**
- Thread-local `errno` variable
- All standard POSIX error codes (ENOENT, EINVAL, ENOMEM, etc.)
- Network error codes (ECONNREFUSED, ETIMEDOUT, etc.)

**`<math.h>` - Math Functions:**
| Function | Description |
|----------|-------------|
| `sin`, `cos`, `tan` | Trigonometric functions |
| `asin`, `acos`, `atan`, `atan2` | Inverse trigonometric |
| `sinh`, `cosh`, `tanh` | Hyperbolic functions |
| `asinh`, `acosh`, `atanh` | Inverse hyperbolic |
| `exp`, `exp2`, `expm1` | Exponential functions |
| `log`, `log2`, `log10`, `log1p` | Logarithmic functions |
| `pow`, `sqrt`, `cbrt`, `hypot` | Power functions |
| `fabs`, `fmod`, `remainder` | Basic operations |
| `floor`, `ceil`, `round`, `trunc` | Rounding |
| `fmax`, `fmin`, `fdim` | Min/max/difference |
| `copysign`, `nan`, `ldexp`, `frexp` | Manipulation |
| `erf`, `erfc`, `tgamma`, `lgamma` | Special functions |

**Constants:** `M_PI`, `M_E`, `M_SQRT2`, `M_LN2`, `INFINITY`, `NAN`
**Macros:** `isnan()`, `isinf()`, `isfinite()`, `fpclassify()`

**`<dirent.h>` - Directory Operations:**
| Function | Description |
|----------|-------------|
| `opendir(path)` | Open directory stream |
| `readdir(dirp)` | Read next entry |
| `closedir(dirp)` | Close directory |
| `rewinddir(dirp)` | Reset to beginning |
| `dirfd(dirp)` | Get underlying fd |

**Types:** `DIR`, `struct dirent` (d_ino, d_type, d_name)

**`<termios.h>` - Terminal Control:**
| Function | Description |
|----------|-------------|
| `tcgetattr(fd, termios)` | Get terminal attributes |
| `tcsetattr(fd, action, termios)` | Set terminal attributes |
| `cfmakeraw(termios)` | Configure raw mode |
| `cfgetispeed`, `cfgetospeed` | Get baud rate |
| `cfsetispeed`, `cfsetospeed` | Set baud rate |
| `isatty(fd)` | Check if terminal |
| `ttyname(fd)` | Get terminal name |

**Modes:** `ICANON`, `ECHO`, `ISIG`, `OPOST`, etc.

**`<pthread.h>` - POSIX Threads (Stubs):**
| Function | Description |
|----------|-------------|
| `pthread_create` | Create thread (returns ENOSYS) |
| `pthread_join`, `pthread_exit` | Thread lifecycle |
| `pthread_self`, `pthread_equal` | Thread identity |
| `pthread_mutex_init/lock/unlock/destroy` | Mutex operations |
| `pthread_cond_init/wait/signal/broadcast` | Condition variables |
| `pthread_rwlock_*` | Read-write locks |
| `pthread_once` | One-time initialization |
| `pthread_key_create/getspecific/setspecific` | Thread-local storage |

**Note:** Single-threaded stubs; mutexes work, thread creation returns ENOSYS.

**Additional Headers:**
| Header | Contents |
|--------|----------|
| `<stddef.h>` | size_t, ptrdiff_t, NULL, offsetof |
| `<stdbool.h>` | bool, true, false |
| `<limits.h>` | INT_MAX, LONG_MAX, PATH_MAX, etc. |
| `<assert.h>` | assert() macro, static_assert |

**Heap Implementation:**
| Component | Description |
|-----------|-------------|
| `sbrk(increment)` | Syscall to adjust program break |
| Allocator | First-fit free-list |
| Alignment | 16-byte aligned allocations |
| Block header | size, next pointer, free flag |

---

### 4. C++ Standard Library (`libc/include/c++/`)

**Status:** Freestanding C++ library headers

**`<type_traits>` - Type Traits:**
| Trait | Description |
|-------|-------------|
| `integral_constant`, `true_type`, `false_type` | Constants |
| `is_void`, `is_null_pointer`, `is_integral`, `is_floating_point` | Type checks |
| `is_array`, `is_pointer`, `is_reference`, `is_const`, `is_volatile` | Type properties |
| `is_same<T, U>` | Type comparison |
| `remove_const`, `remove_volatile`, `remove_cv` | Remove qualifiers |
| `remove_reference`, `remove_pointer` | Remove modifiers |
| `add_const`, `add_pointer`, `add_lvalue_reference` | Add modifiers |
| `conditional<B, T, F>` | Conditional type |
| `enable_if<B, T>` | SFINAE helper |
| `decay<T>` | Decay transformation |

**`<utility>` - Utilities:**
| Function/Type | Description |
|---------------|-------------|
| `std::move(t)` | Cast to rvalue |
| `std::forward<T>(t)` | Perfect forwarding |
| `std::swap(a, b)` | Swap values |
| `std::exchange(obj, new_val)` | Replace and return old |
| `std::pair<T1, T2>` | Pair container |
| `std::make_pair(a, b)` | Create pair |
| `integer_sequence`, `index_sequence` | Compile-time sequences |

**`<new>` - Dynamic Memory:**
| Function | Description |
|----------|-------------|
| `operator new` / `operator delete` | Allocation operators |
| `operator new[]` / `operator delete[]` | Array versions |
| Placement new | Construct at address |
| `std::nothrow` | Non-throwing allocation |
| `std::launder(p)` | Pointer optimization barrier |

**`<initializer_list>` - Brace Initialization:**
| Member | Description |
|--------|-------------|
| `std::initializer_list<T>` | Brace-init container |
| `begin()` / `end()` | Iterator access |
| `size()` | Element count |

**`<cstddef>` / `<cstdint>` - C++ Wrappers:**
- `std::size_t`, `std::ptrdiff_t`, `std::nullptr_t`
- `std::byte` (C++17) with bitwise operators
- `std::int8_t` through `std::int64_t`
- `std::uint8_t` through `std::uint64_t`

**Build:**
The libc is compiled as a static library (`libviperlibc.a`). User programs are automatically linked via the `add_user_program()` CMake function.

---

## Shared ABI Headers (`include/viperos/`)

User and kernel share type definitions:

| Header | Contents |
|--------|----------|
| `types.hpp` | Basic types (u8, i64, usize, etc.) |
| `syscall_nums.hpp` | Syscall number constants |
| `syscall_abi.hpp` | SyscallResult structure |
| `fs_types.hpp` | Stat, DirEnt, open flags, seek whence |
| `mem_info.hpp` | MemInfo structure |
| `task_info.hpp` | TaskInfo structure, task flags |
| `cap_info.hpp` | CapInfo, CapListEntry, kind/rights |
| `tls_info.hpp` | TLSInfo structure, version/cipher |

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                       vinit.cpp                              │
│  ┌────────────────┐  ┌────────────────┐  ┌──────────────┐   │
│  │  Shell Loop    │  │  Commands      │  │  Helpers     │   │
│  │  readline()    │  │  cmd_dir()     │  │  strlen()    │   │
│  │  dispatch()    │  │  cmd_fetch()   │  │  memcpy()    │   │
│  │  history       │  │  cmd_caps()    │  │  puts()      │   │
│  └────────────────┘  └────────────────┘  └──────────────┘   │
└──────────────────────────────┬──────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────┐
│                      syscall.hpp                             │
│  ┌────────────────┐  ┌────────────────┐  ┌──────────────┐   │
│  │ syscall0..4()  │  │  sys::open()   │  │  sys::print()│   │
│  │ inline asm     │  │  sys::write()  │  │  sys::exit() │   │
│  │ svc #0         │  │  sys::fetch()  │  │  etc.        │   │
│  └────────────────┘  └────────────────┘  └──────────────┘   │
└──────────────────────────────┬──────────────────────────────┘
                               │ SVC #0
                               ▼
┌─────────────────────────────────────────────────────────────┐
│                    Kernel (EL1)                              │
│               Exception Handler → Syscall Dispatch           │
└─────────────────────────────────────────────────────────────┘
```

---

## Disk Image Layout

`vinit.elf` is bundled into `disk.img` by `mkfs.viperfs`:

```
disk.img (8MB ViperFS):
  /vinit.elf        - User-space init process
  /SYS/             - System directory
  /SYS/certs/       - Certificate storage
  /SYS/certs/roots.der - CA root certificates
```

---

## Testing

User space is tested via:
- `qemu_kernel_boot` - Verifies vinit starts and prints banner
- Manual interactive testing via shell commands
- All networking and capability tests use vinit shell

---

## Files

| File | Lines | Description |
|------|-------|-------------|
| `vinit/vinit.cpp` | ~3,324 | Init process + shell |
| `syscall.hpp` | ~1,677 | Low-level syscall wrappers |
| `libc/src/stdio.c` | ~560 | Standard I/O with FILE and buffering |
| `libc/src/string.c` | ~410 | String operations |
| `libc/src/stdlib.c` | ~625 | Standard library with env vars |
| `libc/src/ctype.c` | ~79 | Character classification |
| `libc/src/unistd.c` | ~122 | POSIX functions |
| `libc/src/time.c` | ~220 | Time functions with clock_gettime |
| `libc/src/math.c` | ~900 | Complete math library |
| `libc/src/dirent.c` | ~140 | Directory operations |
| `libc/src/termios.c` | ~180 | Terminal control |
| `libc/src/pthread.c` | ~350 | POSIX threads stubs |
| `libc/src/errno.c` | ~23 | Error handling |
| `libc/src/new.cpp` | ~98 | C++ new/delete |
| `libc/include/c++/*` | ~811 | C++ headers |
| `hello/hello.cpp` | ~294 | Hello world test program |
| `sysinfo/sysinfo.cpp` | ~392 | System info utility |

---

## Not Implemented

- Shared libraries / dynamic linking
- Signal handling
- Job control (bg/fg)
- Pipes between commands
- Shell scripting
- Command aliases
- Thread-safe errno (currently per-process only)
- Full locale support
- Real multi-threading (pthreads are stubs)

---

## Priority Recommendations

1. **High:** Add shell scripting support
2. **High:** Implement pipes for command chaining
3. **Medium:** Add more user-space applications
4. **Medium:** Implement real multi-threading in kernel
5. **Low:** Add job control (background processes)
6. **Low:** Implement shared library support
