# User Space

**Status:** Complete with comprehensive libc, C++ STL headers, and interactive shell
**Location:** `user/`
**SLOC:** ~32,000

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
| `scanf`, `fscanf` | Formatted input |
| `puts(s)` | Print string with newline |
| `fputs(s, stream)` | Print string to stream |
| `putchar(c)` / `fputc(c, stream)` | Print character |
| `getchar()` / `fgetc(stream)` | Read character |
| `getc(stream)` | Read character (macro) |
| `fgets(s, n, stream)` | Read line |
| `gets(s)` | Read line (deprecated) |
| `ungetc(c, stream)` | Push character back |
| `fopen(path, mode)` | Open file |
| `freopen(path, mode, stream)` | Reopen file |
| `fdopen(fd, mode)` | Open from file descriptor |
| `fclose(stream)` | Close file |
| `fileno(stream)` | Get file descriptor |
| `fread(ptr, size, n, stream)` | Read binary data |
| `fwrite(ptr, size, n, stream)` | Write binary data |
| `fseek(stream, off, whence)` | Seek position |
| `ftell(stream)` | Get position |
| `rewind(stream)` | Reset to beginning |
| `fgetpos(stream, pos)` | Get position (fpos_t) |
| `fsetpos(stream, pos)` | Set position (fpos_t) |
| `ferror(stream)` / `feof(stream)` | Error/EOF check |
| `clearerr(stream)` | Clear error state |
| `fflush(stream)` | Flush output |
| `setvbuf(stream, buf, mode, size)` | Set buffering mode |
| `setbuf(stream, buf)` | Set buffer |
| `setlinebuf(stream)` | Set line buffering |
| `perror(s)` | Print error message |
| `remove(path)` | Delete file |
| `rename(old, new)` | Rename file |
| `tmpnam(s)` | Generate temp filename |
| `tmpfile()` | Create temp file |
| `getline(lineptr, n, stream)` | Read line (dynamic) |
| `getdelim(lineptr, n, delim, stream)` | Read until delimiter |

**FILE pool:** 20 simultaneous open files (excluding stdin/stdout/stderr)

**Buffering modes:** `_IOFBF` (full), `_IOLBF` (line), `_IONBF` (none)

**Format specifiers:** `%d`, `%i`, `%u`, `%x`, `%X`, `%p`, `%s`, `%c`, `%%`, `%ld`, `%lu`, `%lx`, `%lld`, `%llu`

**`<string.h>` - String Operations:**
| Function | Description |
|----------|-------------|
| `strlen(s)` / `strnlen(s, n)` | String length |
| `strcpy` / `strncpy` / `strlcpy` | Copy string |
| `strcat` / `strncat` / `strlcat` | Concatenate string |
| `strcmp` / `strncmp` | Compare strings |
| `strcoll(s1, s2)` | Locale-aware compare |
| `strcasecmp` / `strncasecmp` | Case-insensitive compare |
| `strchr` / `strrchr` | Find character |
| `strstr` | Find substring |
| `strpbrk` | Find any of characters |
| `strspn` / `strcspn` | Span of characters |
| `strtok(s, delim)` | Tokenize string |
| `strtok_r` | Tokenize string (reentrant) |
| `strdup` / `strndup` | Duplicate string |
| `strrev(s)` | Reverse string in place |
| `strerror(errnum)` | Error string for errno |
| `strerrorlen_s(errnum)` | Error string length |
| `memcpy` / `memmove` | Copy memory |
| `memset` / `memchr` / `memcmp` | Memory operations |
| `memrchr(s, c, n)` | Find character from end |
| `memmem(haystack, needle)` | Find memory pattern |

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
| `atof(s)` | String to double |
| `strtol` / `strtoul` | String to long with base |
| `strtoll` / `strtoull` | String to long long |
| `strtod(s, endptr)` | String to double |
| `strtof(s, endptr)` | String to float |
| `strtold(s, endptr)` | String to long double |
| `itoa(n, buf, base)` | Integer to string |
| `ltoa(n, buf, base)` | Long to string |
| `ultoa(n, buf, base)` | Unsigned long to string |
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
| `getuid()` / `geteuid()` | User IDs (always 0) |
| `getgid()` / `getegid()` | Group IDs (always 0) |
| `setuid(uid)` / `setgid(gid)` | Set user/group ID |
| `getpgrp()` | Get process group |
| `setpgid(pid, pgid)` | Set process group |
| `setsid()` | Create session |
| `sleep(sec)` / `usleep(usec)` | Sleep |
| `getcwd(buf, size)` | Get working directory |
| `chdir(path)` | Change directory |
| `access(path, mode)` | Check file access |
| `unlink(path)` | Delete file |
| `rmdir(path)` | Remove directory |
| `mkdir(path, mode)` | Create directory |
| `link(old, new)` | Create hard link (stub) |
| `symlink(target, linkpath)` | Create symbolic link |
| `readlink(path, buf, size)` | Read symbolic link |
| `gethostname(buf, len)` | Get hostname |
| `sethostname(name, len)` | Set hostname |
| `fork()` | Create child process |
| `execv`, `execve`, `execvp` | Execute program (stubs) |
| `pipe(fds)` | Create pipe (stub) |
| `truncate` / `ftruncate` | Truncate file (stubs) |
| `fsync(fd)` | Sync file to disk |
| `alarm(seconds)` | Set alarm (stub) |
| `pause()` | Wait for signal |
| `isatty(fd)` | Terminal check |
| `sysconf(name)` | System configuration |
| `pathconf` / `fpathconf` | Path configuration (stubs) |
| `sbrk(increment)` | Adjust program break |

**`<errno.h>` - Error Handling:**
- Thread-local `errno` variable
- All standard POSIX error codes (ENOENT, EINVAL, ENOMEM, etc.)
- Network error codes (ECONNREFUSED, ETIMEDOUT, etc.)

**`<signal.h>` - Signal Handling:**
| Function | Description |
|----------|-------------|
| `signal(sig, handler)` | Set signal handler |
| `raise(sig)` | Send signal to self |
| `kill(pid, sig)` | Send signal to process |
| `sigaction(sig, act, oldact)` | Extended signal handling |
| `sigemptyset(set)` | Clear signal set |
| `sigfillset(set)` | Fill signal set |
| `sigaddset(set, sig)` | Add signal to set |
| `sigdelset(set, sig)` | Remove signal from set |
| `sigismember(set, sig)` | Test signal in set |
| `sigprocmask(how, set, old)` | Block/unblock signals |
| `sigpending(set)` | Get pending signals |
| `sigsuspend(mask)` | Wait for signal (stub) |
| `strsignal(sig)` | Signal name string |
| `psignal(sig, s)` | Print signal message |

**Signals:** SIGHUP, SIGINT, SIGQUIT, SIGILL, SIGTRAP, SIGABRT, SIGBUS, SIGFPE, SIGKILL, SIGUSR1, SIGSEGV, SIGUSR2, SIGPIPE, SIGALRM, SIGTERM, SIGCHLD, SIGCONT, SIGSTOP, SIGTSTP, SIGTTIN, SIGTTOU, SIGURG, SIGXCPU, SIGXFSZ, SIGVTALRM, SIGPROF, SIGWINCH, SIGIO, SIGPWR, SIGSYS

**Flags:** SA_NOCLDSTOP, SA_NOCLDWAIT, SA_SIGINFO, SA_ONSTACK, SA_RESTART, SA_NODEFER, SA_RESETHAND

**`<fcntl.h>` - File Control:**
| Function | Description |
|----------|-------------|
| `open(path, flags, ...)` | Open file |
| `creat(path, mode)` | Create file |
| `fcntl(fd, cmd, ...)` | File control operations |
| `openat(dirfd, path, flags)` | Open relative to directory |
| `posix_fadvise(fd, off, len, adv)` | File access advisory |
| `posix_fallocate(fd, off, len)` | Allocate space (stub) |

**Open flags:** O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, O_EXCL, O_TRUNC, O_APPEND, O_NONBLOCK, O_DIRECTORY, O_NOFOLLOW, O_CLOEXEC

**fcntl commands:** F_DUPFD, F_GETFD, F_SETFD, F_GETFL, F_SETFL, F_GETLK, F_SETLK, F_SETLKW

**`<sys/stat.h>` - File Status:**
| Function | Description |
|----------|-------------|
| `stat(path, statbuf)` | Get file status |
| `fstat(fd, statbuf)` | Get file status by fd |
| `lstat(path, statbuf)` | Get symlink status |
| `chmod(path, mode)` | Change file mode |
| `fchmod(fd, mode)` | Change mode by fd |
| `mkdir(path, mode)` | Create directory |
| `umask(mask)` | Set file creation mask |
| `mkfifo(path, mode)` | Create FIFO |
| `mknod(path, mode, dev)` | Create special file |

**File type macros:** S_ISREG, S_ISDIR, S_ISCHR, S_ISBLK, S_ISFIFO, S_ISLNK, S_ISSOCK

**Permission bits:** S_IRWXU, S_IRUSR, S_IWUSR, S_IXUSR, S_IRWXG, S_IRGRP, S_IWGRP, S_IXGRP, S_IRWXO, S_IROTH, S_IWOTH, S_IXOTH

**`<sys/types.h>` - System Types:**
- `ssize_t`, `size_t`, `off_t`, `pid_t`, `uid_t`, `gid_t`, `mode_t`
- `dev_t`, `ino_t`, `nlink_t`, `blksize_t`, `blkcnt_t`, `time_t`
- Fixed-width types: int8_t through int64_t, uint8_t through uint64_t

**`<inttypes.h>` - Integer Formatting:**
| Function | Description |
|----------|-------------|
| `imaxabs(j)` | Absolute value of intmax_t |
| `imaxdiv(n, d)` | Division with remainder |
| `strtoimax(s, end, base)` | String to intmax_t |
| `strtoumax(s, end, base)` | String to uintmax_t |

**Format macros:** PRId8, PRId16, PRId32, PRId64, PRIu8, PRIu16, PRIu32, PRIu64, PRIx8, PRIx16, PRIx32, PRIx64, etc.

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

**`<setjmp.h>` - Non-local Jumps:**
| Function | Description |
|----------|-------------|
| `setjmp(env)` | Save execution context |
| `longjmp(env, val)` | Restore execution context |
| `_setjmp(env)` | setjmp without signal mask |
| `_longjmp(env, val)` | longjmp without signal mask |
| `sigsetjmp(env, savesigs)` | setjmp with optional signal mask |
| `siglongjmp(env, val)` | longjmp with signal mask |

**AArch64 implementation:** Saves x19-x28, x29 (FP), x30 (LR), SP, and d8-d15 floating-point registers.

**`<sys/wait.h>` - Process Waiting:**
| Function/Macro | Description |
|----------------|-------------|
| `wait(status)` | Wait for any child |
| `waitpid(pid, status, opts)` | Wait for specific child |
| `WIFEXITED(status)` | True if normal exit |
| `WEXITSTATUS(status)` | Exit code |
| `WIFSIGNALED(status)` | True if killed by signal |
| `WTERMSIG(status)` | Terminating signal |
| `WIFSTOPPED(status)` | True if stopped |
| `WSTOPSIG(status)` | Stopping signal |
| `WIFCONTINUED(status)` | True if continued |

**Flags:** WNOHANG, WUNTRACED, WCONTINUED

**`<poll.h>` - I/O Multiplexing:**
| Function | Description |
|----------|-------------|
| `poll(fds, nfds, timeout)` | Wait for I/O events |
| `ppoll(fds, nfds, timeout, sigmask)` | poll with timeout and signal mask |

**Event flags:** POLLIN, POLLOUT, POLLERR, POLLHUP, POLLNVAL, POLLPRI, POLLRDNORM, POLLRDBAND, POLLWRNORM, POLLWRBAND

**`<sys/select.h>` - select() Multiplexing:**
| Function/Macro | Description |
|----------------|-------------|
| `select(nfds, rd, wr, ex, timeout)` | Wait for I/O |
| `pselect(nfds, rd, wr, ex, timeout, sigmask)` | select with signal mask |
| `FD_ZERO(set)` | Clear set |
| `FD_SET(fd, set)` | Add fd to set |
| `FD_CLR(fd, set)` | Remove fd from set |
| `FD_ISSET(fd, set)` | Test fd in set |

**Maximum fds:** FD_SETSIZE = 1024

**`<sys/socket.h>` - BSD Sockets:**
| Function | Description |
|----------|-------------|
| `socket(domain, type, proto)` | Create socket |
| `bind(sockfd, addr, addrlen)` | Bind to address |
| `listen(sockfd, backlog)` | Listen for connections |
| `accept(sockfd, addr, addrlen)` | Accept connection |
| `accept4(sockfd, addr, addrlen, flags)` | Accept with flags |
| `connect(sockfd, addr, addrlen)` | Connect to remote |
| `send(sockfd, buf, len, flags)` | Send data |
| `recv(sockfd, buf, len, flags)` | Receive data |
| `sendto(sockfd, buf, len, flags, dest, addrlen)` | Send to address |
| `recvfrom(sockfd, buf, len, flags, src, addrlen)` | Receive with source |
| `sendmsg(sockfd, msg, flags)` | Send message |
| `recvmsg(sockfd, msg, flags)` | Receive message |
| `shutdown(sockfd, how)` | Shutdown connection |
| `getsockopt(sockfd, level, opt, val, len)` | Get socket option |
| `setsockopt(sockfd, level, opt, val, len)` | Set socket option |
| `getsockname(sockfd, addr, addrlen)` | Get local address |
| `getpeername(sockfd, addr, addrlen)` | Get peer address |
| `socketpair(domain, type, proto, sv)` | Create socket pair (stub) |

**Address families:** AF_UNIX, AF_INET, AF_INET6
**Socket types:** SOCK_STREAM, SOCK_DGRAM, SOCK_RAW
**Flags:** MSG_OOB, MSG_PEEK, MSG_DONTWAIT, MSG_NOSIGNAL

**`<netinet/in.h>` - Internet Protocol:**
| Type/Constant | Description |
|---------------|-------------|
| `struct sockaddr_in` | IPv4 address structure |
| `struct sockaddr_in6` | IPv6 address structure |
| `struct in_addr` | IPv4 address |
| `struct in6_addr` | IPv6 address |
| `INADDR_ANY` | Bind to any address |
| `INADDR_LOOPBACK` | Loopback (127.0.0.1) |
| `INADDR_BROADCAST` | Broadcast address |
| `IPPROTO_IP`, `IPPROTO_TCP`, `IPPROTO_UDP` | Protocol numbers |

**`<arpa/inet.h>` - Address Conversion:**
| Function | Description |
|----------|-------------|
| `htons(n)` / `ntohs(n)` | Host/network byte order (16-bit) |
| `htonl(n)` / `ntohl(n)` | Host/network byte order (32-bit) |
| `inet_addr(cp)` | Dotted decimal to in_addr_t |
| `inet_aton(cp, inp)` | Dotted decimal to in_addr |
| `inet_ntoa(in)` | in_addr to dotted decimal |
| `inet_pton(af, src, dst)` | Presentation to network |
| `inet_ntop(af, src, dst, size)` | Network to presentation |
| `inet_network(cp)` | Parse network address |
| `inet_makeaddr(net, host)` | Construct address |
| `inet_lnaof(in)` | Local network address |
| `inet_netof(in)` | Network number |

**`<netdb.h>` - Network Database:**
| Function | Description |
|----------|-------------|
| `gethostbyname(name)` | Resolve hostname |
| `gethostbyaddr(addr, len, type)` | Reverse lookup (stub) |
| `gethostbyname_r(...)` | Reentrant version |
| `getaddrinfo(node, svc, hints, res)` | Modern address resolution |
| `freeaddrinfo(res)` | Free getaddrinfo result |
| `gai_strerror(errcode)` | getaddrinfo error string |
| `getnameinfo(addr, addrlen, host, hostlen, serv, servlen, flags)` | Reverse lookup |
| `getservbyname(name, proto)` | Service by name |
| `getservbyport(port, proto)` | Service by port |
| `getprotobyname(name)` | Protocol by name |
| `getprotobynumber(proto)` | Protocol by number |
| `herror(s)` / `hstrerror(err)` | Host error messages |

**Error codes:** EAI_AGAIN, EAI_BADFLAGS, EAI_FAIL, EAI_FAMILY, EAI_MEMORY, EAI_NONAME, EAI_SERVICE, EAI_SOCKTYPE, EAI_SYSTEM, EAI_OVERFLOW
**Flags:** AI_PASSIVE, AI_CANONNAME, AI_NUMERICHOST, AI_NUMERICSERV, NI_NUMERICHOST, NI_NUMERICSERV, NI_DGRAM

**Static tables:** Known services (http, https, ftp, ssh, telnet, smtp, dns, ntp) and protocols (ip, icmp, tcp, udp)

**`<locale.h>` - Localization:**
| Function | Description |
|----------|-------------|
| `setlocale(category, locale)` | Set/query locale |
| `localeconv()` | Get numeric formatting |

**Categories:** LC_ALL, LC_COLLATE, LC_CTYPE, LC_MESSAGES, LC_MONETARY, LC_NUMERIC, LC_TIME
**Supported locales:** "C", "POSIX", "" (default)

**`<wchar.h>` - Wide Characters:**
| Function | Description |
|----------|-------------|
| `iswalnum`, `iswalpha`, `iswdigit`, etc. | Wide character classification |
| `towlower`, `towupper` | Wide character conversion |
| `wcscpy`, `wcsncpy`, `wcscat`, `wcsncat` | Wide string copy/concat |
| `wcslen`, `wcscmp`, `wcsncmp`, `wcscoll` | Wide string compare |
| `wcschr`, `wcsrchr`, `wcsstr`, `wcstok` | Wide string search |
| `wmemcpy`, `wmemmove`, `wmemset`, `wmemcmp`, `wmemchr` | Wide memory ops |
| `mbrtowc`, `wcrtomb` | Multibyte/wide conversion |
| `mbsrtowcs`, `wcsrtombs` | String conversion |
| `mbsinit` | Check conversion state |
| `mbtowc`, `wctomb`, `mbstowcs`, `wcstombs` | Non-restartable versions |
| `wcstol`, `wcstoul`, `wcstod` | Wide numeric conversion |
| `fgetwc`, `fputwc`, `fgetws`, `fputws` | Wide I/O |
| `wcsdup` | Duplicate wide string |

**UTF-8 support:** Full UTF-8 encoding/decoding up to 4 bytes (U+10FFFF)

**`<sys/mman.h>` - Memory Mapping:**
| Function | Description |
|----------|-------------|
| `mmap(addr, len, prot, flags, fd, off)` | Map memory |
| `munmap(addr, len)` | Unmap memory |
| `mprotect(addr, len, prot)` | Change protection |
| `msync(addr, len, flags)` | Sync mapped region |
| `madvise(addr, len, advice)` | Memory advice |
| `posix_madvise(addr, len, advice)` | POSIX memory advice |
| `mlock(addr, len)` | Lock memory |
| `munlock(addr, len)` | Unlock memory |
| `mlockall(flags)` | Lock all memory (stub) |
| `munlockall()` | Unlock all memory (stub) |
| `mincore(addr, len, vec)` | Check residency (stub) |
| `shm_open(name, oflag, mode)` | Open shared memory (stub) |
| `shm_unlink(name)` | Unlink shared memory (stub) |

**Protection flags:** PROT_NONE, PROT_READ, PROT_WRITE, PROT_EXEC
**Map flags:** MAP_SHARED, MAP_PRIVATE, MAP_FIXED, MAP_ANONYMOUS
**msync flags:** MS_ASYNC, MS_SYNC, MS_INVALIDATE
**madvise flags:** MADV_NORMAL, MADV_RANDOM, MADV_SEQUENTIAL, MADV_WILLNEED, MADV_DONTNEED, MADV_FREE

**`<sys/utsname.h>` - System Identification:**
| Function | Description |
|----------|-------------|
| `uname(buf)` | Get system identification |

**Fields:** sysname ("ViperOS"), nodename ("viper"), release ("0.1.0"), version ("#1 SMP"), machine ("aarch64")

**`<fenv.h>` - Floating-Point Environment:**
| Function | Description |
|----------|-------------|
| `feclearexcept(excepts)` | Clear floating-point exception flags |
| `fegetexceptflag(flagp, excepts)` | Get exception flags |
| `feraiseexcept(excepts)` | Raise floating-point exceptions |
| `fesetexceptflag(flagp, excepts)` | Set exception flags |
| `fetestexcept(excepts)` | Test exception flags |
| `fegetround()` | Get current rounding mode |
| `fesetround(round)` | Set rounding mode |
| `fegetenv(envp)` | Get floating-point environment |
| `feholdexcept(envp)` | Save environment and clear exceptions |
| `fesetenv(envp)` | Set floating-point environment |
| `feupdateenv(envp)` | Set environment and raise saved exceptions |
| `feenableexcept(excepts)` | Enable exception traps (GNU extension) |
| `fedisableexcept(excepts)` | Disable exception traps (GNU extension) |
| `fegetexcept()` | Get enabled exceptions (GNU extension) |

**Exception flags:** FE_INVALID, FE_DIVBYZERO, FE_OVERFLOW, FE_UNDERFLOW, FE_INEXACT, FE_ALL_EXCEPT
**Rounding modes:** FE_TONEAREST, FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO

**`<sys/resource.h>` - Resource Limits:**
| Function | Description |
|----------|-------------|
| `getrlimit(resource, rlim)` | Get resource limits |
| `setrlimit(resource, rlim)` | Set resource limits |
| `prlimit(pid, resource, new, old)` | Get/set limits (Linux extension) |
| `getrusage(who, usage)` | Get resource usage |
| `getpriority(which, who)` | Get process priority |
| `setpriority(which, who, prio)` | Set process priority |

**Resources:** RLIMIT_CPU, RLIMIT_FSIZE, RLIMIT_DATA, RLIMIT_STACK, RLIMIT_CORE, RLIMIT_NOFILE, RLIMIT_AS, etc.

**`<syslog.h>` - System Logging:**
| Function | Description |
|----------|-------------|
| `openlog(ident, option, facility)` | Open connection to logger |
| `syslog(priority, format, ...)` | Generate log message |
| `vsyslog(priority, format, ap)` | Generate log message (va_list) |
| `closelog()` | Close connection to logger |
| `setlogmask(mask)` | Set log priority mask |

**Priorities:** LOG_EMERG, LOG_ALERT, LOG_CRIT, LOG_ERR, LOG_WARNING, LOG_NOTICE, LOG_INFO, LOG_DEBUG
**Facilities:** LOG_KERN, LOG_USER, LOG_DAEMON, LOG_AUTH, LOG_LOCAL0-7
**Options:** LOG_PID, LOG_CONS, LOG_NDELAY, LOG_PERROR

**`<fnmatch.h>` - Filename Pattern Matching:**
| Function | Description |
|----------|-------------|
| `fnmatch(pattern, string, flags)` | Match filename against pattern |

**Pattern syntax:** `*` (any sequence), `?` (any character), `[...]` (character class), `\c` (escape)
**Flags:** FNM_PATHNAME, FNM_NOESCAPE, FNM_PERIOD, FNM_LEADING_DIR, FNM_CASEFOLD

**`<pwd.h>` - Password File Access:**
| Function | Description |
|----------|-------------|
| `getpwnam(name)` | Get entry by username |
| `getpwuid(uid)` | Get entry by user ID |
| `getpwnam_r(...)` | Reentrant version |
| `getpwuid_r(...)` | Reentrant version |
| `setpwent()` | Open/rewind password file |
| `endpwent()` | Close password file |
| `getpwent()` | Get next entry |

**struct passwd:** pw_name, pw_passwd, pw_uid, pw_gid, pw_gecos, pw_dir, pw_shell

**`<grp.h>` - Group File Access:**
| Function | Description |
|----------|-------------|
| `getgrnam(name)` | Get entry by group name |
| `getgrgid(gid)` | Get entry by group ID |
| `getgrnam_r(...)` | Reentrant version |
| `getgrgid_r(...)` | Reentrant version |
| `setgrent()` | Open/rewind group file |
| `endgrent()` | Close group file |
| `getgrent()` | Get next entry |
| `getgrouplist(user, group, groups, ngroups)` | Get user's groups |
| `initgroups(user, group)` | Initialize supplementary groups |

**struct group:** gr_name, gr_passwd, gr_gid, gr_mem

**`<unistd.h>` - getopt (Command-line Parsing):**
| Function/Variable | Description |
|-------------------|-------------|
| `getopt(argc, argv, optstring)` | Parse short options |
| `getopt_long(...)` | Parse long options |
| `getopt_long_only(...)` | Parse long options with single dash |
| `optarg` | Argument to current option |
| `optind` | Index of next argument |
| `opterr` | Error message control |
| `optopt` | Current option character |

**`<libgen.h>` - Path Manipulation:**
| Function | Description |
|----------|-------------|
| `basename(path)` | Extract filename from path |
| `dirname(path)` | Extract directory from path |

**`<search.h>` - Hash and Tree Search:**
| Function | Description |
|----------|-------------|
| `hcreate(nel)` | Create hash table |
| `hdestroy()` | Destroy hash table |
| `hsearch(item, action)` | Search hash table |
| `hcreate_r(nel, htab)` | Create hash table (reentrant) |
| `hdestroy_r(htab)` | Destroy hash table (reentrant) |
| `hsearch_r(item, action, retval, htab)` | Search hash table (reentrant) |
| `tsearch(key, rootp, compar)` | Insert in binary tree |
| `tfind(key, rootp, compar)` | Find in binary tree |
| `tdelete(key, rootp, compar)` | Delete from binary tree |
| `twalk(root, action)` | Walk binary tree |
| `twalk_r(root, action, closure)` | Walk binary tree with closure |
| `tdestroy(root, free_node)` | Destroy binary tree |
| `lfind(key, base, nmemb, size, compar)` | Linear search |
| `lsearch(key, base, nmemb, size, compar)` | Linear search with insert |
| `insque(element, pred)` | Insert in queue |
| `remque(element)` | Remove from queue |

**`<glob.h>` - Filename Globbing:**
| Function | Description |
|----------|-------------|
| `glob(pattern, flags, errfunc, pglob)` | Find pathnames matching pattern |
| `globfree(pglob)` | Free glob results |

**Flags:** GLOB_ERR, GLOB_MARK, GLOB_NOSORT, GLOB_DOOFFS, GLOB_NOCHECK, GLOB_APPEND, GLOB_NOESCAPE, GLOB_PERIOD, GLOB_TILDE, GLOB_ONLYDIR

**`<ftw.h>` - File Tree Walk:**
| Function | Description |
|----------|-------------|
| `ftw(path, fn, nopenfd)` | Walk directory tree (legacy) |
| `nftw(path, fn, nopenfd, flags)` | Walk directory tree (extended) |

**Type flags:** FTW_F (file), FTW_D (directory), FTW_DNR (unreadable dir), FTW_DP (post-order dir), FTW_NS (stat failed), FTW_SL (symlink), FTW_SLN (dangling symlink)
**Flags:** FTW_PHYS, FTW_MOUNT, FTW_DEPTH, FTW_CHDIR

**`<spawn.h>` - POSIX Spawn:**
| Function | Description |
|----------|-------------|
| `posix_spawn(pid, path, file_actions, attrp, argv, envp)` | Spawn process (stub) |
| `posix_spawnp(pid, file, file_actions, attrp, argv, envp)` | Spawn with PATH (stub) |
| `posix_spawnattr_init/destroy` | Initialize/destroy attributes |
| `posix_spawnattr_getflags/setflags` | Get/set attribute flags |
| `posix_spawnattr_getpgroup/setpgroup` | Get/set process group |
| `posix_spawnattr_getsigdefault/setsigdefault` | Get/set default signals |
| `posix_spawnattr_getsigmask/setsigmask` | Get/set signal mask |
| `posix_spawn_file_actions_init/destroy` | Initialize/destroy file actions |
| `posix_spawn_file_actions_addclose` | Add close action |
| `posix_spawn_file_actions_adddup2` | Add dup2 action |
| `posix_spawn_file_actions_addopen` | Add open action |

**`<sched.h>` - Process Scheduling:**
| Function | Description |
|----------|-------------|
| `sched_yield()` | Yield processor |
| `sched_get_priority_max(policy)` | Get max priority |
| `sched_get_priority_min(policy)` | Get min priority |
| `sched_getscheduler(pid)` | Get scheduling policy |
| `sched_setscheduler(pid, policy, param)` | Set scheduling policy (stub) |
| `sched_getparam(pid, param)` | Get scheduling parameters |
| `sched_setparam(pid, param)` | Set scheduling parameters (stub) |
| `sched_getaffinity(pid, size, mask)` | Get CPU affinity |
| `sched_setaffinity(pid, size, mask)` | Set CPU affinity |

**Policies:** SCHED_OTHER, SCHED_FIFO, SCHED_RR, SCHED_BATCH, SCHED_IDLE

**`<wordexp.h>` - Word Expansion:**
| Function | Description |
|----------|-------------|
| `wordexp(words, pwordexp, flags)` | Perform shell-style word expansion |
| `wordfree(pwordexp)` | Free word expansion results |

**Expansion features:** Tilde expansion, variable expansion, field splitting, quote handling
**Flags:** WRDE_APPEND, WRDE_DOOFFS, WRDE_NOCMD, WRDE_REUSE, WRDE_SHOWERR, WRDE_UNDEF

**`<sys/ipc.h>` - IPC Base Definitions:**
| Function/Type | Description |
|---------------|-------------|
| `ftok(pathname, proj_id)` | Generate IPC key from pathname |
| `struct ipc_perm` | Permission structure |
| `key_t` | IPC key type |

**Flags:** IPC_CREAT, IPC_EXCL, IPC_NOWAIT, IPC_RMID, IPC_SET, IPC_STAT

**`<sys/shm.h>` - Shared Memory:**
| Function | Description |
|----------|-------------|
| `shmget(key, size, shmflg)` | Get shared memory segment (stub) |
| `shmat(shmid, shmaddr, shmflg)` | Attach shared memory (stub) |
| `shmdt(shmaddr)` | Detach shared memory (stub) |
| `shmctl(shmid, cmd, buf)` | Shared memory control (stub) |

**Flags:** SHM_RDONLY, SHM_RND, SHM_REMAP, SHM_EXEC

**`<monetary.h>` - Monetary Formatting:**
| Function | Description |
|----------|-------------|
| `strfmon(s, maxsize, format, ...)` | Format monetary value |
| `strfmon_l(s, maxsize, locale, format, ...)` | Format with locale |

**Format specifiers:** `%n` (national format), `%i` (international format)
**Modifiers:** `=f` (fill char), `^` (no grouping), `(` (parentheses for negative), `+` (sign), `!` (no symbol), `-` (left justify)

**`<iconv.h>` - Character Set Conversion:**
| Function | Description |
|----------|-------------|
| `iconv_open(tocode, fromcode)` | Open conversion descriptor |
| `iconv(cd, inbuf, inbytesleft, outbuf, outbytesleft)` | Convert characters |
| `iconv_close(cd)` | Close conversion descriptor |

**Supported encodings:** UTF-8, ASCII, ISO-8859-1, UTF-16BE, UTF-16LE, UTF-32BE, UTF-32LE

**`<nl_types.h>` - Message Catalogs:**
| Function | Description |
|----------|-------------|
| `catopen(name, flag)` | Open message catalog |
| `catgets(catd, set_id, msg_id, s)` | Read message from catalog |
| `catclose(catd)` | Close message catalog |

**Note:** Stub implementation - returns default strings

**`<sys/sem.h>` - System V Semaphores:**
| Function | Description |
|----------|-------------|
| `semget(key, nsems, semflg)` | Get semaphore set identifier |
| `semop(semid, sops, nsops)` | Perform semaphore operations |
| `semtimedop(semid, sops, nsops, timeout)` | Timed semaphore operations |
| `semctl(semid, semnum, cmd, ...)` | Semaphore control operations |

**Commands:** GETVAL, SETVAL, GETALL, SETALL, GETPID, GETNCNT, GETZCNT, IPC_STAT, IPC_SET, IPC_RMID
**Flags:** SEM_UNDO

**`<sys/msg.h>` - System V Message Queues:**
| Function | Description |
|----------|-------------|
| `msgget(key, msgflg)` | Get message queue identifier |
| `msgsnd(msqid, msgp, msgsz, msgflg)` | Send message to queue |
| `msgrcv(msqid, msgp, msgsz, msgtyp, msgflg)` | Receive message from queue |
| `msgctl(msqid, cmd, buf)` | Message queue control |

**Flags:** MSG_NOERROR, MSG_EXCEPT, MSG_COPY, IPC_NOWAIT

**`<semaphore.h>` - POSIX Semaphores:**
| Function | Description |
|----------|-------------|
| `sem_init(sem, pshared, value)` | Initialize unnamed semaphore |
| `sem_destroy(sem)` | Destroy unnamed semaphore |
| `sem_open(name, oflag, ...)` | Open named semaphore |
| `sem_close(sem)` | Close named semaphore |
| `sem_unlink(name)` | Remove named semaphore |
| `sem_wait(sem)` | Lock semaphore |
| `sem_trywait(sem)` | Try to lock semaphore |
| `sem_timedwait(sem, abstime)` | Timed lock |
| `sem_post(sem)` | Unlock semaphore |
| `sem_getvalue(sem, sval)` | Get semaphore value |

**`<langinfo.h>` - Language Information:**
| Function | Description |
|----------|-------------|
| `nl_langinfo(item)` | Get locale-specific string |
| `nl_langinfo_l(item, locale)` | Get string for specific locale |

**Items:** CODESET, D_T_FMT, D_FMT, T_FMT, AM_STR, PM_STR, DAY_1-DAY_7, ABDAY_1-ABDAY_7, MON_1-MON_12, ABMON_1-ABMON_12, RADIXCHAR, THOUSEP, YESEXPR, NOEXPR

**`<netinet/tcp.h>` - TCP Protocol Definitions:**
| Type/Constant | Description |
|---------------|-------------|
| `struct tcphdr` | TCP header structure |
| `struct tcp_info` | TCP connection information |
| `TCP_NODELAY` | Disable Nagle algorithm |
| `TCP_MAXSEG` | Maximum segment size |
| `TCP_KEEPIDLE/INTVL/CNT` | Keepalive parameters |
| `TCP_CORK` | Cork/uncork (Linux) |
| `TCP_FASTOPEN` | TCP Fast Open |
| `TCP_CONGESTION` | Congestion control algorithm |

**States:** TCP_ESTABLISHED, TCP_SYN_SENT, TCP_FIN_WAIT1, TCP_TIME_WAIT, TCP_CLOSE, TCP_LISTEN, etc.
**Flags:** TH_FIN, TH_SYN, TH_RST, TH_PUSH, TH_ACK, TH_URG, TH_ECE, TH_CWR

**`<cpio.h>` - cpio Archive Format:**
| Constant | Description |
|----------|-------------|
| `MAGIC` | ASCII format magic ("070707") |
| `CMS_ASC` / `CMS_CRC` | SVR4 format magic |
| `C_IRUSR`, `C_IWUSR`, etc. | File mode bits |
| `C_ISDIR`, `C_ISREG`, etc. | File type constants |
| `CPIO_TRAILER` | End-of-archive marker |

**`<tar.h>` - tar Archive Format:**
| Constant | Description |
|----------|-------------|
| `TMAGIC` / `TVERSION` | USTAR magic and version |
| `REGTYPE`, `DIRTYPE`, `SYMTYPE` | File type flags |
| `TSUID`, `TSGID`, `TSVTX` | Special mode bits |
| `TUREAD`, `TUWRITE`, `TUEXEC` | Permission bits |
| `struct posix_header` | USTAR header structure |

**`<fmtmsg.h>` - Message Display:**
| Function | Description |
|----------|-------------|
| `fmtmsg(class, label, sev, text, action, tag)` | Display formatted message |
| `addseverity(severity, string)` | Add custom severity level |

**Classifications:** MM_HARD, MM_SOFT, MM_FIRM, MM_APPL, MM_UTIL, MM_OPSYS, MM_PRINT, MM_CONSOLE
**Severities:** MM_HALT, MM_ERROR, MM_WARNING, MM_INFO

**`<aio.h>` - Asynchronous I/O:**
| Function | Description |
|----------|-------------|
| `aio_read(aiocbp)` | Submit async read request |
| `aio_write(aiocbp)` | Submit async write request |
| `lio_listio(mode, list, nent, sig)` | Submit list of I/O requests |
| `aio_error(aiocbp)` | Get error status |
| `aio_return(aiocbp)` | Get return status |
| `aio_suspend(list, nent, timeout)` | Wait for completion |
| `aio_cancel(fd, aiocbp)` | Cancel I/O request |
| `aio_fsync(op, aiocbp)` | Async file sync |

**Note:** Synchronous fallback implementation

**`<mqueue.h>` - POSIX Message Queues:**
| Function | Description |
|----------|-------------|
| `mq_open(name, oflag, ...)` | Open message queue |
| `mq_close(mqdes)` | Close message queue |
| `mq_unlink(name)` | Remove message queue |
| `mq_send(mqdes, msg, len, prio)` | Send message |
| `mq_receive(mqdes, msg, len, prio)` | Receive message |
| `mq_getattr(mqdes, attr)` | Get queue attributes |
| `mq_setattr(mqdes, newattr, oldattr)` | Set queue attributes |
| `mq_notify(mqdes, sevp)` | Register for notification |

**`<regex.h>` - POSIX Regular Expressions:**
| Function | Description |
|----------|-------------|
| `regcomp(preg, regex, cflags)` | Compile regex pattern |
| `regexec(preg, string, nmatch, pmatch, eflags)` | Execute regex match |
| `regfree(preg)` | Free compiled regex |
| `regerror(errcode, preg, errbuf, size)` | Get error message |

**Flags:** REG_EXTENDED, REG_ICASE, REG_NOSUB, REG_NEWLINE, REG_NOTBOL, REG_NOTEOL

**`<ndbm.h>` - Database Functions:**
| Function | Description |
|----------|-------------|
| `dbm_open(file, flags, mode)` | Open database |
| `dbm_close(db)` | Close database |
| `dbm_fetch(db, key)` | Fetch record |
| `dbm_store(db, key, content, mode)` | Store record |
| `dbm_delete(db, key)` | Delete record |
| `dbm_firstkey(db)` / `dbm_nextkey(db)` | Iterate keys |

**Note:** In-memory hash table implementation

**`<utmpx.h>` - User Accounting:**
| Function | Description |
|----------|-------------|
| `setutxent()` / `endutxent()` | Open/close database |
| `getutxent()` | Read next entry |
| `getutxid(id)` | Search by ID |
| `getutxline(line)` | Search by terminal |
| `pututxline(utmpx)` | Write entry |

**Entry types:** EMPTY, BOOT_TIME, USER_PROCESS, DEAD_PROCESS, etc.

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

**Status:** Freestanding C++ library headers with algorithm and functional support

**`<type_traits>` - Type Traits:**
| Trait | Description |
|-------|-------------|
| `integral_constant`, `true_type`, `false_type` | Constants |
| `is_void`, `is_null_pointer`, `is_integral`, `is_floating_point` | Type checks |
| `is_array`, `is_pointer`, `is_reference`, `is_const`, `is_volatile` | Type properties |
| `is_function`, `is_same<T, U>` | Type comparison |
| `is_convertible`, `is_constructible`, `is_assignable` | Type relationships |
| `remove_const`, `remove_volatile`, `remove_cv` | Remove qualifiers |
| `remove_reference`, `remove_pointer`, `remove_extent` | Remove modifiers |
| `add_const`, `add_pointer`, `add_lvalue_reference` | Add modifiers |
| `conditional<B, T, F>` | Conditional type |
| `enable_if<B, T>` | SFINAE helper |
| `decay<T>` | Decay transformation |
| `void_t<T...>` | Void alias for SFINAE |
| `invoke_result<F, Args...>` | Callable result type |
| `common_type<T...>` | Common type |
| `extent<T>`, `remove_all_extents` | Array extent traits |

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

**`<algorithm>` - Algorithms:**
| Function | Description |
|----------|-------------|
| `min`, `max`, `clamp` | Value comparison |
| `swap`, `iter_swap` | Swap elements |
| `fill`, `fill_n` | Fill range |
| `copy`, `copy_if`, `copy_n`, `copy_backward` | Copy elements |
| `move`, `move_backward` | Move elements |
| `find`, `find_if`, `find_if_not` | Search elements |
| `count`, `count_if` | Count elements |
| `all_of`, `any_of`, `none_of` | Predicates |
| `for_each` | Apply function |
| `equal` | Range comparison |
| `reverse`, `rotate` | Reorder elements |
| `transform` | Transform elements |
| `replace`, `replace_if` | Replace elements |
| `remove`, `remove_if`, `unique` | Remove elements |
| `lower_bound`, `upper_bound`, `binary_search` | Binary search |
| `sort`, `stable_sort`, `is_sorted` | Sorting |
| `min_element`, `max_element` | Find extremes |
| `lexicographical_compare` | Lexicographic compare |
| `distance`, `advance` | Iterator utilities |

**`<functional>` - Function Objects:**
| Type | Description |
|------|-------------|
| `plus`, `minus`, `multiplies`, `divides`, `modulus`, `negate` | Arithmetic |
| `equal_to`, `not_equal_to`, `greater`, `less`, `greater_equal`, `less_equal` | Comparison |
| `logical_and`, `logical_or`, `logical_not` | Logical |
| `bit_and`, `bit_or`, `bit_xor`, `bit_not` | Bitwise |
| `identity` | Identity functor (C++20) |
| `reference_wrapper<T>` | Reference wrapper |
| `ref(t)`, `cref(t)` | Create reference wrapper |
| `hash<T>` | Hash function objects |
| `not_fn(f)` | Negate predicate (C++17) |

**`<iterator>` - Iterators:**
| Type/Function | Description |
|---------------|-------------|
| `iterator_traits<T>` | Iterator type traits |
| `input_iterator_tag` through `contiguous_iterator_tag` | Iterator categories |
| `reverse_iterator<It>` | Reverse iterator adapter |
| `back_insert_iterator`, `front_insert_iterator` | Insert iterators |
| `begin`, `end`, `cbegin`, `cend` | Range access |
| `rbegin`, `rend` | Reverse range access |
| `size`, `empty`, `data` | Container access |
| `distance`, `advance`, `next`, `prev` | Iterator operations |

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

**`<limits>` - Numeric Limits:**
| Template | Description |
|----------|-------------|
| `std::numeric_limits<T>` | Type limits traits |

**Specializations:** bool, char, signed char, unsigned char, short, unsigned short, int, unsigned int, long, unsigned long, long long, unsigned long long, float, double, long double

**Members:** min(), max(), lowest(), epsilon(), digits, digits10, is_signed, is_integer, is_exact, radix, infinity(), quiet_NaN(), signaling_NaN(), denorm_min(), is_iec559, has_infinity, has_quiet_NaN, round_style

**`<memory>` - Smart Pointers:**
| Type | Description |
|------|-------------|
| `std::unique_ptr<T>` | Single-ownership pointer |
| `std::unique_ptr<T[]>` | Single-ownership array |
| `std::shared_ptr<T>` | Shared-ownership pointer |
| `std::weak_ptr<T>` | Non-owning observer |
| `std::default_delete<T>` | Default deleter |
| `std::allocator<T>` | Default allocator |
| `std::pointer_traits<Ptr>` | Pointer type traits |

**Functions:** make_unique, make_shared, addressof, swap, uninitialized_copy, uninitialized_fill, uninitialized_fill_n

**unique_ptr members:** get(), release(), reset(), swap(), operator*, operator->, operator[], operator bool

**shared_ptr members:** get(), use_count(), unique(), reset(), swap(), operator*, operator->

**weak_ptr members:** use_count(), expired(), lock(), reset(), swap()

**`<array>` - Fixed-Size Array:**
| Member | Description |
|--------|-------------|
| `std::array<T, N>` | Fixed-size container |
| `at(pos)` | Bounds-checked access |
| `operator[]` | Array access |
| `front()` / `back()` | First/last element |
| `data()` | Raw pointer |
| `begin()` / `end()` | Iterators |
| `rbegin()` / `rend()` | Reverse iterators |
| `size()` / `max_size()` / `empty()` | Capacity |
| `fill(value)` | Fill with value |
| `swap(other)` | Swap contents |

**Non-member:** std::get<I>, std::swap, comparison operators, std::to_array (C++20)

**Structured bindings:** tuple_size, tuple_element specializations

**`<string_view>` - Non-owning String Reference:**
| Type | Description |
|------|-------------|
| `std::basic_string_view<CharT>` | Non-owning string reference |
| `std::string_view` | Alias for basic_string_view<char> |
| `std::wstring_view` | Alias for basic_string_view<wchar_t> |

**Operations:** size(), length(), empty(), data(), substr(), compare(), starts_with(), ends_with(), contains(), find(), rfind(), find_first_of(), find_last_of(), find_first_not_of(), find_last_not_of(), remove_prefix(), remove_suffix(), copy()

**Literal suffix:** "hello"_sv

**`<utility>` (Enhanced) - Optional:**
| Type | Description |
|------|-------------|
| `std::optional<T>` | Optional value container |
| `std::nullopt` | Empty optional marker |
| `std::in_place` | In-place construction tag |

**optional members:** has_value(), value(), value_or(default), operator*, operator->, operator bool, reset(), emplace(), swap()

**Additional utilities:** as_const(), to_underlying() (C++23), cmp_equal/cmp_less/etc. (C++20 integer comparison)

**`<vector>` - Dynamic Array:**
| Member | Description |
|--------|-------------|
| `std::vector<T>` | Dynamic array container |
| `push_back(value)` | Add element |
| `emplace_back(args...)` | Construct in-place |
| `pop_back()` | Remove last element |
| `insert(pos, value)` | Insert element |
| `erase(pos)` / `erase(first, last)` | Remove element(s) |
| `clear()` | Remove all elements |
| `resize(count)` / `resize(count, value)` | Change size |
| `reserve(cap)` | Reserve capacity |
| `shrink_to_fit()` | Reduce capacity |
| `at(pos)` / `operator[]` | Element access |
| `front()` / `back()` | First/last element |
| `data()` | Raw pointer |
| `begin()` / `end()` | Iterators |
| `size()` / `capacity()` / `empty()` | Capacity |
| `swap(other)` | Swap contents |

**Non-member:** std::swap, comparison operators, std::erase/std::erase_if (C++20)

**`<string>` - Dynamic String:**
| Type | Description |
|------|-------------|
| `std::basic_string<CharT>` | Dynamic string container |
| `std::string` | Alias for basic_string<char> |
| `std::wstring` | Alias for basic_string<wchar_t> |
| `std::char_traits<CharT>` | Character traits |

**Operations:** c_str(), data(), size(), length(), empty(), capacity(), reserve(), shrink_to_fit(), clear(), insert(), erase(), push_back(), pop_back(), append(), operator+=, compare(), substr(), copy(), resize(), swap(), find(), rfind(), find_first_of(), find_last_of(), find_first_not_of(), find_last_not_of(), starts_with(), ends_with(), contains()

**Non-member:** operator+, comparison operators, std::swap, std::hash<std::string>, std::stoi/stol/stoll/stoul/stoull/stof/stod/stold, std::to_string

**Literal suffix:** "hello"_s

**`<tuple>` - Heterogeneous Fixed-Size Collection:**
| Type/Function | Description |
|---------------|-------------|
| `std::tuple<T...>` | Heterogeneous collection |
| `std::get<I>(t)` | Access element by index |
| `std::get<T>(t)` | Access element by type |
| `std::make_tuple(args...)` | Create tuple |
| `std::tie(refs...)` | Create tuple of references |
| `std::forward_as_tuple(args...)` | Create tuple of forwarded references |
| `std::tuple_cat(tuples...)` | Concatenate tuples |
| `std::apply(f, tuple)` | Apply function to tuple elements |
| `std::make_from_tuple<T>(tuple)` | Construct T from tuple |
| `std::tuple_size<T>` | Number of elements |
| `std::tuple_element<I, T>` | Type of element I |

**Structured bindings:** Full support via tuple_size and tuple_element

**`<map>` - Sorted Associative Container:**
| Member | Description |
|--------|-------------|
| `std::map<Key, Value>` | Sorted key-value container |
| `operator[]` | Access with insertion |
| `at(key)` | Bounds-checked access |
| `insert(pair)` / `insert(hint, pair)` | Insert element |
| `emplace(args...)` | Construct in-place |
| `erase(key)` / `erase(pos)` | Remove element |
| `find(key)` | Find element |
| `count(key)` | Count elements |
| `contains(key)` | Check presence (C++20) |
| `lower_bound(key)` / `upper_bound(key)` | Range search |
| `equal_range(key)` | Find range of equal elements |
| `begin()` / `end()` | Bidirectional iterators |
| `size()` / `empty()` / `clear()` | Capacity |

**Implementation:** Red-black tree for O(log n) operations

**`<set>` - Sorted Unique Key Container:**
| Member | Description |
|--------|-------------|
| `std::set<Key>` | Sorted unique key container |
| `insert(key)` / `insert(hint, key)` | Insert element |
| `emplace(args...)` | Construct in-place |
| `erase(key)` / `erase(pos)` | Remove element |
| `find(key)` | Find element |
| `count(key)` | Count elements (0 or 1) |
| `contains(key)` | Check presence (C++20) |
| `lower_bound(key)` / `upper_bound(key)` | Range search |
| `equal_range(key)` | Find range |
| `begin()` / `end()` | Bidirectional iterators (const) |
| `size()` / `empty()` / `clear()` | Capacity |

**Implementation:** Red-black tree for O(log n) operations

**`<deque>` - Double-Ended Queue:**
| Member | Description |
|--------|-------------|
| `std::deque<T>` | Double-ended queue |
| `push_back(value)` / `push_front(value)` | Add elements |
| `pop_back()` / `pop_front()` | Remove elements |
| `emplace_back(args...)` / `emplace_front(args...)` | Construct in-place |
| `insert(pos, value)` | Insert element |
| `erase(pos)` / `erase(first, last)` | Remove element(s) |
| `at(pos)` / `operator[]` | Element access |
| `front()` / `back()` | First/last element |
| `begin()` / `end()` | Random access iterators |
| `size()` / `empty()` / `clear()` | Capacity |
| `resize(count)` / `shrink_to_fit()` | Size management |

**Implementation:** Block-based with 4KB blocks for O(1) front/back operations

**`<list>` - Doubly-Linked List:**
| Member | Description |
|--------|-------------|
| `std::list<T>` | Doubly-linked list |
| `push_back(value)` / `push_front(value)` | Add elements |
| `pop_back()` / `pop_front()` | Remove elements |
| `insert(pos, value)` | Insert element (O(1)) |
| `erase(pos)` | Remove element (O(1)) |
| `splice(pos, other)` | Transfer elements (O(1)) |
| `merge(other)` | Merge sorted lists |
| `sort()` | Sort elements (merge sort) |
| `reverse()` | Reverse order |
| `unique()` | Remove consecutive duplicates |
| `remove(value)` / `remove_if(pred)` | Remove by value |
| `begin()` / `end()` | Bidirectional iterators |
| `size()` / `empty()` / `clear()` | Capacity |

**Implementation:** Sentinel node with merge sort for O(n log n) sorting

**`<ratio>` - Compile-Time Rational Arithmetic:**
| Type | Description |
|------|-------------|
| `std::ratio<Num, Denom>` | Compile-time rational |
| `ratio_add<R1, R2>` | Addition |
| `ratio_subtract<R1, R2>` | Subtraction |
| `ratio_multiply<R1, R2>` | Multiplication |
| `ratio_divide<R1, R2>` | Division |
| `ratio_equal<R1, R2>` | Equality comparison |
| `ratio_less<R1, R2>` | Less-than comparison |

**SI prefixes:** atto, femto, pico, nano, micro, milli, centi, deci, deca, hecto, kilo, mega, giga, tera, peta, exa

**`<chrono>` - Time Utilities:**
| Type | Description |
|------|-------------|
| `std::chrono::duration<Rep, Period>` | Time duration |
| `std::chrono::time_point<Clock, Duration>` | Point in time |
| `std::chrono::system_clock` | Wall-clock time |
| `std::chrono::steady_clock` | Monotonic clock |
| `std::chrono::high_resolution_clock` | High-resolution clock |
| `duration_cast<ToDuration>(d)` | Duration conversion |
| `time_point_cast<ToDuration>(tp)` | Time point conversion |

**Duration types:** nanoseconds, microseconds, milliseconds, seconds, minutes, hours, days (C++20)

**User-defined literals:** `_h`, `_min`, `_s`, `_ms`, `_us`, `_ns` (in `std::chrono_literals`)

**`<bitset>` - Fixed-Size Bit Sequence:**
| Member | Description |
|--------|-------------|
| `std::bitset<N>` | Fixed-size bit sequence |
| `operator[]` | Access bit |
| `test(pos)` | Bounds-checked access |
| `set()` / `set(pos)` | Set bit(s) to 1 |
| `reset()` / `reset(pos)` | Set bit(s) to 0 |
| `flip()` / `flip(pos)` | Toggle bit(s) |
| `all()` / `any()` / `none()` | Test all bits |
| `count()` | Count set bits |
| `size()` | Number of bits |
| `to_ulong()` / `to_ullong()` | Convert to integer |
| `to_string()` | Convert to string |
| `operator&` / `operator|` / `operator^` | Bitwise operations |
| `operator<<` / `operator>>` | Shift operations |

**`<queue>` - Queue Container Adapter:**
| Member | Description |
|--------|-------------|
| `std::queue<T>` | FIFO queue |
| `push(value)` | Add to back |
| `pop()` | Remove from front |
| `front()` / `back()` | Access front/back |
| `empty()` / `size()` | Capacity |

**`<stack>` - Stack Container Adapter:**
| Member | Description |
|--------|-------------|
| `std::stack<T>` | LIFO stack |
| `push(value)` | Add to top |
| `pop()` | Remove from top |
| `top()` | Access top element |
| `empty()` / `size()` | Capacity |

**`<priority_queue>` - Priority Queue (in `<queue>`):**
| Member | Description |
|--------|-------------|
| `std::priority_queue<T>` | Heap-based priority queue |
| `push(value)` | Add element |
| `pop()` | Remove highest priority |
| `top()` | Access highest priority |
| `empty()` / `size()` | Capacity |

**Implementation:** Max-heap with O(log n) push/pop

**`<forward_list>` - Singly-Linked List:**
| Member | Description |
|--------|-------------|
| `std::forward_list<T>` | Singly-linked list |
| `push_front(value)` / `pop_front()` | Modify front |
| `insert_after(pos, value)` | Insert after position |
| `erase_after(pos)` | Erase after position |
| `splice_after(pos, other)` | Transfer elements |
| `merge(other)` | Merge sorted lists |
| `sort()` | Sort elements |
| `reverse()` | Reverse order |
| `unique()` | Remove consecutive duplicates |
| `remove(value)` / `remove_if(pred)` | Remove by value |
| `before_begin()` | Iterator before first element |

**Implementation:** Merge sort for O(n log n) sorting

**`<unordered_map>` - Hash Map:**
| Member | Description |
|--------|-------------|
| `std::unordered_map<Key, Value>` | Hash-based key-value container |
| `operator[]` / `at(key)` | Element access |
| `insert(pair)` | Insert element |
| `emplace(args...)` | Construct in-place |
| `erase(key)` / `erase(pos)` | Remove element |
| `find(key)` | Find element |
| `count(key)` / `contains(key)` | Check presence |
| `bucket_count()` | Number of buckets |
| `load_factor()` / `max_load_factor()` | Load factor |
| `rehash(count)` / `reserve(count)` | Rehashing |

**Implementation:** Separate chaining with automatic rehashing

**`<unordered_set>` - Hash Set:**
| Member | Description |
|--------|-------------|
| `std::unordered_set<Key>` | Hash-based set |
| `insert(key)` | Insert element |
| `emplace(args...)` | Construct in-place |
| `erase(key)` / `erase(pos)` | Remove element |
| `find(key)` | Find element |
| `count(key)` / `contains(key)` | Check presence |
| `bucket_count()` | Number of buckets |
| `load_factor()` | Load factor |

**Implementation:** Separate chaining with automatic rehashing

**`<any>` - Type-Erased Container (C++17):**
| Type/Function | Description |
|---------------|-------------|
| `std::any` | Type-erased value container |
| `has_value()` | Check if contains value |
| `reset()` | Clear stored value |
| `emplace<T>(args...)` | Construct value in-place |
| `any_cast<T>(any)` | Extract value |
| `make_any<T>(args...)` | Create any |

**`<variant>` - Type-Safe Union (C++17):**
| Type/Function | Description |
|---------------|-------------|
| `std::variant<Types...>` | Type-safe union |
| `index()` | Get current alternative index |
| `valueless_by_exception()` | Check if valueless |
| `emplace<T>(args...)` | Construct alternative |
| `get<I>(v)` / `get<T>(v)` | Access by index/type |
| `get_if<I>(v)` / `get_if<T>(v)` | Safe access |
| `holds_alternative<T>(v)` | Check current type |
| `visit(vis, v)` | Apply visitor |
| `std::monostate` | Empty alternative type |

**`<span>` - Non-Owning View (C++20):**
| Member | Description |
|--------|-------------|
| `std::span<T>` / `std::span<T, N>` | Non-owning contiguous view |
| `data()` | Pointer to data |
| `size()` / `size_bytes()` | Size |
| `empty()` | Check if empty |
| `front()` / `back()` / `operator[]` | Element access |
| `first<Count>()` / `last<Count>()` | Subspan from start/end |
| `subspan<Offset, Count>()` | General subspan |
| `as_bytes(s)` / `as_writable_bytes(s)` | Byte views |

**`<exception>` - Exception Handling:**
| Type/Function | Description |
|---------------|-------------|
| `std::exception` | Base exception class |
| `std::bad_exception` | Unexpected exception |
| `std::nested_exception` | Nested exception support |
| `terminate_handler` | Terminate handler type |
| `get_terminate()` / `set_terminate()` | Manage terminate handler |
| `terminate()` | Terminate program |
| `uncaught_exceptions()` | Count uncaught exceptions |
| `current_exception()` | Get current exception |
| `rethrow_exception(ptr)` | Rethrow exception |

**`<stdexcept>` - Standard Exceptions:**
| Exception | Description |
|-----------|-------------|
| `std::logic_error` | Logic error base |
| `std::domain_error` | Domain error |
| `std::invalid_argument` | Invalid argument |
| `std::length_error` | Length exceeded |
| `std::out_of_range` | Index out of range |
| `std::runtime_error` | Runtime error base |
| `std::range_error` | Range error |
| `std::overflow_error` | Overflow error |
| `std::underflow_error` | Underflow error |
| `std::system_error` | System error with error code |
| `std::bad_alloc` | Allocation failure |
| `std::bad_cast` | Dynamic cast failure |
| `std::bad_typeid` | typeid failure |
| `std::bad_function_call` | Empty function call |
| `std::bad_optional_access` | Empty optional access |

**`<numeric>` - Numeric Algorithms:**
| Function | Description |
|----------|-------------|
| `iota(first, last, value)` | Fill with incrementing values |
| `accumulate(first, last, init)` | Sum elements |
| `reduce(first, last, init)` | Parallel-friendly sum (C++17) |
| `inner_product(f1, l1, f2, init)` | Dot product |
| `transform_reduce(f1, l1, f2, init)` | Combined transform and reduce |
| `partial_sum(first, last, d_first)` | Running sum |
| `inclusive_scan(first, last, d_first)` | Inclusive scan (C++17) |
| `exclusive_scan(first, last, d_first, init)` | Exclusive scan (C++17) |
| `adjacent_difference(first, last, d_first)` | Adjacent differences |
| `gcd(m, n)` / `lcm(m, n)` | GCD/LCM (C++17) |
| `midpoint(a, b)` | Midpoint (C++20) |
| `lerp(a, b, t)` | Linear interpolation (C++20) |
| `add_sat(x, y)` / `sub_sat(x, y)` | Saturating arithmetic |

**`<mutex>` - Mutual Exclusion:**
| Type | Description |
|------|-------------|
| `std::mutex` | Basic mutex |
| `std::recursive_mutex` | Recursive mutex |
| `std::timed_mutex` | Mutex with timed operations |
| `std::recursive_timed_mutex` | Recursive timed mutex |
| `std::lock_guard<Mutex>` | RAII lock wrapper |
| `std::unique_lock<Mutex>` | Movable lock wrapper |
| `std::scoped_lock<Mutex...>` | Multi-mutex RAII lock (C++17) |
| `std::once_flag` / `call_once(flag, f)` | One-time initialization |
| `std::lock(l1, l2, ...)` | Lock multiple mutexes (deadlock-free) |
| `std::try_lock(l1, l2, ...)` | Try to lock multiple mutexes |

**Lock tags:** defer_lock, try_to_lock, adopt_lock

**`<condition_variable>` - Condition Variables:**
| Type/Function | Description |
|---------------|-------------|
| `std::condition_variable` | Condition variable for mutex |
| `std::condition_variable_any` | Condition variable for any lock |
| `wait(lock)` | Wait for notification |
| `wait(lock, pred)` | Wait with predicate |
| `wait_for(lock, duration)` | Timed wait |
| `wait_until(lock, time_point)` | Wait until time point |
| `notify_one()` / `notify_all()` | Wake waiting threads |
| `std::cv_status` | Wait return status |

**Note:** Stub implementation for single-threaded environment

**`<atomic>` - Atomic Operations:**
| Type | Description |
|------|-------------|
| `std::atomic<T>` | Atomic type wrapper |
| `std::atomic_flag` | Lock-free boolean flag |
| `std::memory_order` | Memory ordering constraints |
| `atomic_thread_fence(order)` | Memory fence |
| `atomic_signal_fence(order)` | Signal fence |

**Operations:** load(), store(), exchange(), compare_exchange_weak(), compare_exchange_strong(), fetch_add(), fetch_sub(), fetch_and(), fetch_or(), fetch_xor(), ++, --, +=, -=, &=, |=, ^=

**Memory orders:** relaxed, consume, acquire, release, acq_rel, seq_cst

**Type aliases:** atomic_bool, atomic_char, atomic_int, atomic_long, atomic_size_t, atomic_intptr_t, etc.

**`<thread>` - Thread Support:**
| Type/Function | Description |
|---------------|-------------|
| `std::thread` | Thread of execution |
| `std::thread::id` | Thread identifier |
| `get_id()` | Get thread ID |
| `joinable()` | Check if joinable |
| `join()` | Wait for thread |
| `detach()` | Detach thread |
| `hardware_concurrency()` | Number of CPU cores |
| `std::this_thread::get_id()` | Current thread ID |
| `std::this_thread::yield()` | Yield execution |
| `std::this_thread::sleep_for(duration)` | Sleep for duration |
| `std::this_thread::sleep_until(time)` | Sleep until time point |

**Note:** Stub implementation - thread creation not supported (single-threaded environment)

**`<complex>` - Complex Numbers:**
| Type/Function | Description |
|---------------|-------------|
| `std::complex<T>` | Complex number type |
| `real()` / `imag()` | Get real/imaginary parts |
| `abs(z)` / `arg(z)` | Magnitude/phase |
| `norm(z)` / `conj(z)` | Squared magnitude/conjugate |
| `polar(rho, theta)` | Construct from polar |
| `proj(z)` | Riemann projection |
| `exp`, `log`, `log10` | Exponential/logarithmic |
| `pow`, `sqrt` | Power functions |
| `sin`, `cos`, `tan` | Trigonometric |
| `asin`, `acos`, `atan` | Inverse trigonometric |
| `sinh`, `cosh`, `tanh` | Hyperbolic |
| `asinh`, `acosh`, `atanh` | Inverse hyperbolic |

**Specializations:** complex<float>, complex<double>, complex<long double>
**Literals:** `_i`, `_if`, `_il` (in `std::complex_literals`)

**`<valarray>` - Numeric Arrays:**
| Type/Function | Description |
|---------------|-------------|
| `std::valarray<T>` | Numeric array |
| `operator[]` | Element/slice access |
| `operator+`, `-`, `*`, `/`, `%` | Element-wise arithmetic |
| `operator&`, `|`, `^`, `<<`, `>>` | Element-wise bitwise |
| `operator==`, `!=`, `<`, `<=`, `>`, `>=` | Element-wise comparison |
| `sum()` / `min()` / `max()` | Reduction operations |
| `shift(n)` / `cshift(n)` | Shift/circular shift |
| `apply(func)` | Apply function |
| `resize(n)` | Change size |
| `std::slice` | BLAS-like slice |
| `std::gslice` | Generalized slice |
| `std::slice_array<T>` | Slice reference |
| `std::mask_array<T>` | Masked reference |
| `std::indirect_array<T>` | Indirect reference |

**Transcendental functions:** abs, exp, log, log10, sqrt, sin, cos, tan, asin, acos, atan, sinh, cosh, tanh, pow, atan2

**`<regex>` - Regular Expressions (Basic):**
| Type | Description |
|------|-------------|
| `std::basic_regex<CharT>` | Regular expression |
| `std::regex` / `std::wregex` | Type aliases |
| `std::match_results<It>` | Match results container |
| `std::smatch` / `std::cmatch` | String/C-string matches |
| `std::sub_match<It>` | Sub-expression match |
| `regex_match(s, m, e)` | Match entire string |
| `regex_search(s, m, e)` | Search for match |
| `regex_replace(s, e, fmt)` | Replace matches |
| `std::regex_error` | Regex exception |

**Syntax options:** icase, nosubs, optimize, ECMAScript, basic, extended, awk, grep, egrep
**Match flags:** match_not_bol, match_not_eol, match_any, format_sed, format_first_only

**Note:** Simplified implementation supporting literal pattern matching

**`<shared_mutex>` - Shared Mutex (Reader-Writer Locks):**
| Type | Description |
|------|-------------|
| `std::shared_mutex` | Reader-writer mutex |
| `std::shared_timed_mutex` | Timed reader-writer mutex |
| `std::shared_lock<Mutex>` | RAII shared (read) lock wrapper |
| `lock()` / `unlock()` | Exclusive (write) locking |
| `lock_shared()` / `unlock_shared()` | Shared (read) locking |
| `try_lock()` / `try_lock_shared()` | Non-blocking lock attempts |
| `try_lock_for()` / `try_lock_until()` | Timed lock attempts |

**Note:** Stub implementation for single-threaded environment

**`<future>` - Futures and Promises:**
| Type | Description |
|------|-------------|
| `std::future<T>` | Asynchronous result handle |
| `std::shared_future<T>` | Shareable async result |
| `std::promise<T>` | Promise to set a value |
| `std::packaged_task<R(Args...)>` | Packaged callable task |
| `std::async(policy, f, args...)` | Launch async task |
| `std::future_status` | Status enum (ready, timeout, deferred) |
| `std::launch` | Launch policy (async, deferred) |
| `std::future_error` | Future exception |

**Operations:** get(), wait(), wait_for(), wait_until(), valid(), share()

**`<filesystem>` - Filesystem Library (C++17):**
| Type | Description |
|------|-------------|
| `std::filesystem::path` | Filesystem path |
| `std::filesystem::directory_entry` | Directory entry |
| `std::filesystem::directory_iterator` | Directory iteration |
| `std::filesystem::file_status` | File type and permissions |
| `std::filesystem::space_info` | Disk space information |

| Function | Description |
|----------|-------------|
| `exists(p)` | Check if path exists |
| `is_directory(p)` / `is_regular_file(p)` | Type checks |
| `file_size(p)` | Get file size |
| `create_directory(p)` | Create directory |
| `remove(p)` / `remove_all(p)` | Remove file/directory |
| `rename(old, new)` | Rename file |
| `copy(from, to)` | Copy file/directory |
| `current_path()` | Get/set current directory |
| `absolute(p)` / `canonical(p)` | Path resolution |
| `equivalent(p1, p2)` | Check if paths are same file |

**Path operations:** root_name(), root_path(), parent_path(), filename(), stem(), extension(), is_absolute(), is_relative()

**`<stop_token>` - Cooperative Cancellation (C++20):**
| Type | Description |
|------|-------------|
| `std::stop_token` | Cancellation token |
| `std::stop_source` | Cancellation source |
| `std::stop_callback<F>` | Callback on cancellation |
| `std::nostopstate_t` | No-state tag type |

**Operations:** stop_requested(), stop_possible(), request_stop(), get_token()

**`<latch>` - Single-Use Barrier (C++20):**
| Type | Description |
|------|-------------|
| `std::latch` | Downward counting latch |
| `count_down(n)` | Decrement counter |
| `try_wait()` | Check if counter is zero |
| `wait()` | Block until zero |
| `arrive_and_wait(n)` | Decrement and wait |
| `max()` | Maximum counter value |

**`<barrier>` - Reusable Barrier (C++20):**
| Type | Description |
|------|-------------|
| `std::barrier<F>` | Reusable thread barrier |
| `arrive(n)` | Arrive at barrier |
| `wait(token)` | Wait for phase completion |
| `arrive_and_wait()` | Arrive and wait |
| `arrive_and_drop()` | Leave barrier permanently |

**Note:** Stub implementations for single-threaded environment

**`<semaphore>` - Counting Semaphores (C++20):**
| Type | Description |
|------|-------------|
| `std::counting_semaphore<N>` | Counting semaphore |
| `std::binary_semaphore` | Binary semaphore (max=1) |
| `release(n)` | Increment counter |
| `acquire()` | Decrement (blocks if zero) |
| `try_acquire()` | Non-blocking acquire |
| `try_acquire_for(duration)` | Timed acquire |

**`<source_location>` - Source Location (C++20):**
| Member | Description |
|--------|-------------|
| `current()` | Get current location |
| `line()` | Line number |
| `column()` | Column number |
| `file_name()` | Source file name |
| `function_name()` | Function name |

**`<numbers>` - Mathematical Constants (C++20):**
| Constant | Value |
|----------|-------|
| `std::numbers::e` | Euler's number (2.718...) |
| `std::numbers::pi` | Pi (3.14159...) |
| `std::numbers::sqrt2` | Square root of 2 |
| `std::numbers::sqrt3` | Square root of 3 |
| `std::numbers::phi` | Golden ratio |
| `std::numbers::ln2` / `ln10` | Natural logarithms |
| `std::numbers::egamma` | Euler-Mascheroni constant |

Variable templates: `e_v<T>`, `pi_v<T>`, etc. for float/double/long double

**`<concepts>` - Concepts Library (C++20):**
| Concept | Description |
|---------|-------------|
| `same_as<T, U>` | Types are identical |
| `derived_from<D, B>` | D derives from B |
| `convertible_to<From, To>` | Implicit conversion exists |
| `integral<T>` | Integral type |
| `floating_point<T>` | Floating-point type |
| `destructible<T>` | Has accessible destructor |
| `constructible_from<T, Args...>` | Can construct from Args |
| `default_initializable<T>` | Default constructible |
| `move_constructible<T>` | Move constructible |
| `copy_constructible<T>` | Copy constructible |
| `equality_comparable<T>` | Supports == and != |
| `totally_ordered<T>` | Supports all comparisons |
| `movable<T>` / `copyable<T>` | Move/copy semantics |
| `regular<T>` | Regular type |
| `invocable<F, Args...>` | Callable |
| `predicate<F, Args...>` | Returns bool |

**`<bit>` - Bit Manipulation (C++20):**
| Function | Description |
|----------|-------------|
| `bit_cast<To>(from)` | Type-punning cast |
| `has_single_bit(x)` | Power of 2 check |
| `bit_ceil(x)` / `bit_floor(x)` | Round to power of 2 |
| `bit_width(x)` | Bits needed to represent |
| `rotl(x, s)` / `rotr(x, s)` | Rotate left/right |
| `countl_zero(x)` / `countr_zero(x)` | Count leading/trailing zeros |
| `countl_one(x)` / `countr_one(x)` | Count leading/trailing ones |
| `popcount(x)` | Count set bits |
| `byteswap(x)` | Byte reversal |
| `std::endian` | Endianness detection |

**`<charconv>` - Character Conversion (C++17):**
| Function | Description |
|----------|-------------|
| `to_chars(first, last, value)` | Value to string |
| `to_chars(first, last, value, base)` | With custom base |
| `from_chars(first, last, value)` | String to value |
| `from_chars(first, last, value, base)` | With custom base |
| `std::chars_format` | Floating-point format |
| `std::to_chars_result` | Result structure |
| `std::from_chars_result` | Result structure |

**C++ Wrapper Headers:**
| Header | Wraps |
|--------|-------|
| `<cstddef>` | stddef.h with std::byte |
| `<cstdint>` | Fixed-width integers |
| `<cstring>` | string.h functions |
| `<cstdlib>` | stdlib.h functions |
| `<cstdio>` | stdio.h functions |
| `<cmath>` | math.h functions with overloads |
| `<ctime>` | time.h functions |
| `<climits>` | limits.h macros |
| `<cerrno>` | errno.h |
| `<cassert>` | assert.h |
| `<cctype>` | ctype.h functions |
| `<csignal>` | signal.h functions |
| `<cinttypes>` | inttypes.h functions |
| `<cwchar>` | wchar.h functions |
| `<clocale>` | locale.h functions |

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
| `libc/src/stdio.c` | ~800 | Standard I/O with FILE pool and buffering |
| `libc/src/string.c` | ~500 | String operations with strerror, strtok |
| `libc/src/stdlib.c` | ~750 | Standard library with strtod, itoa |
| `libc/src/ctype.c` | ~79 | Character classification |
| `libc/src/unistd.c` | ~350 | POSIX functions with fork, access, symlink |
| `libc/src/time.c` | ~220 | Time functions with clock_gettime |
| `libc/src/math.c` | ~900 | Complete math library |
| `libc/src/dirent.c` | ~140 | Directory operations |
| `libc/src/termios.c` | ~180 | Terminal control |
| `libc/src/pthread.c` | ~350 | POSIX threads stubs |
| `libc/src/errno.c` | ~23 | Error handling |
| `libc/src/signal.c` | ~205 | Signal handling |
| `libc/src/stat.c` | ~170 | File status and fcntl operations |
| `libc/src/inttypes.c` | ~170 | Integer format functions |
| `libc/src/setjmp.S` | ~80 | Non-local jumps (AArch64 asm) |
| `libc/src/wait.c` | ~100 | Process wait functions |
| `libc/src/poll.c` | ~120 | I/O multiplexing |
| `libc/src/socket.c` | ~350 | BSD socket functions |
| `libc/src/netdb.c` | ~500 | Network database functions |
| `libc/src/locale.c` | ~100 | Localization |
| `libc/src/wchar.c` | ~550 | Wide character functions |
| `libc/src/mman.c` | ~200 | Memory mapping |
| `libc/src/utsname.c` | ~50 | System identification |
| `libc/src/fenv.c` | ~200 | Floating-point environment |
| `libc/src/resource.c` | ~200 | Resource limits |
| `libc/src/syslog.c` | ~150 | System logging |
| `libc/src/fnmatch.c` | ~200 | Filename pattern matching |
| `libc/src/getopt.c` | ~200 | Command-line option parsing |
| `libc/src/pwd.c` | ~200 | Password file access |
| `libc/src/grp.c` | ~300 | Group file access |
| `libc/src/libgen.c` | ~100 | Path manipulation (basename, dirname) |
| `libc/src/search.c` | ~400 | Hash table and binary tree search |
| `libc/src/glob.c` | ~250 | Filename globbing |
| `libc/src/ftw.c` | ~200 | File tree walk |
| `libc/src/spawn.c` | ~450 | POSIX spawn (stubs) |
| `libc/src/sched.c` | ~160 | Process scheduling |
| `libc/src/wordexp.c` | ~300 | Word expansion |
| `libc/src/ipc.c` | ~50 | IPC key generation |
| `libc/src/shm.c` | ~70 | Shared memory (stubs) |
| `libc/src/monetary.c` | ~250 | Monetary formatting |
| `libc/src/iconv.c` | ~400 | Character set conversion |
| `libc/src/nl_types.c` | ~100 | Message catalogs (stubs) |
| `libc/src/sem.c` | ~250 | System V semaphores |
| `libc/src/msg.c` | ~200 | System V message queues |
| `libc/src/semaphore.c` | ~180 | POSIX semaphores |
| `libc/src/langinfo.c` | ~80 | Locale information |
| `libc/src/fmtmsg.c` | ~130 | Message display |
| `libc/src/aio.c` | ~180 | Asynchronous I/O (sync fallback) |
| `libc/src/mqueue.c` | ~280 | POSIX message queues |
| `libc/src/regex.c` | ~350 | POSIX regular expressions |
| `libc/src/ndbm.c` | ~200 | Database functions |
| `libc/src/utmpx.c` | ~150 | User accounting |
| `libc/src/new.cpp` | ~98 | C++ new/delete |
| `libc/include/c++/*` | ~20,000 | C++ headers (type_traits, algorithm, functional, iterator, memory, array, string_view, optional, vector, string, tuple, map, set, deque, list, ratio, chrono, bitset, queue, stack, forward_list, unordered_map, unordered_set, any, variant, span, exception, stdexcept, numeric, mutex, condition_variable, atomic, thread, complex, valarray, regex, shared_mutex, future, filesystem, stop_token, latch, barrier, semaphore, source_location, numbers, concepts, bit, charconv, etc.) |
| `hello/hello.cpp` | ~294 | Hello world test program |
| `sysinfo/sysinfo.cpp` | ~392 | System info utility |

---

## Not Implemented

- Shared libraries / dynamic linking
- Job control (bg/fg)
- Pipes between commands
- Shell scripting
- Command aliases
- Thread-safe errno (currently per-process only)
- Full locale support
- Real multi-threading (pthreads are stubs)
- exec() family (stubs only)
- pipe() (stub only)

---

## Priority Recommendations

1. **High:** Add shell scripting support
2. **High:** Implement pipes for command chaining
3. **Medium:** Add more user-space applications
4. **Medium:** Implement real multi-threading in kernel
5. **Low:** Add job control (background processes)
6. **Low:** Implement shared library support
