# User Space

**Status:** Functional init process with interactive shell
**Location:** `user/`
**SLOC:** ~4,472

## Overview

User space consists of the `vinit` init process and a header-only syscall wrapper library. `vinit` is loaded from the disk image as the first user-space process and provides an Amiga-inspired interactive shell for debugging and demonstration.

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
| Dir [path] | Brief directory listing |
| List [path] | Detailed directory listing |
| Type \<file\> | Display file contents |
| Copy \<src\> \<dst\> | Copy files |
| Delete \<path\> | Delete file/directory |
| MakeDir \<dir\> | Create directory |
| Rename \<old\> \<new\> | Rename files |
| Fetch \<url\> | HTTP/HTTPS GET request |
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

### 3. Minimal libc (`libc/`)

**Status:** Basic C library implementation

A freestanding C library providing essential functions for user-space programs.

**Implemented Headers:**

**`<stdio.h>` - Standard I/O:**
| Function | Description |
|----------|-------------|
| `printf(fmt, ...)` | Formatted output (limited format specifiers) |
| `puts(s)` | Print string with newline |
| `putchar(c)` | Print character |
| `getchar()` | Read character |

**`<string.h>` - String Operations:**
| Function | Description |
|----------|-------------|
| `strlen(s)` | String length |
| `strcpy(dst, src)` | Copy string |
| `strncpy(dst, src, n)` | Copy limited string |
| `strcmp(a, b)` | Compare strings |
| `strncmp(a, b, n)` | Compare limited |
| `strchr(s, c)` | Find character |
| `strrchr(s, c)` | Find last character |
| `memcpy(dst, src, n)` | Copy memory |
| `memmove(dst, src, n)` | Copy overlapping |
| `memset(dst, c, n)` | Fill memory |
| `memcmp(a, b, n)` | Compare memory |

**`<stdlib.h>` - Utilities:**
| Function | Description |
|----------|-------------|
| `exit(code)` | Terminate program |
| `abs(n)` | Absolute value |
| `atoi(s)` | String to integer |
| `malloc(size)` | Allocate heap memory (via sbrk) |
| `free(ptr)` | Free heap memory |
| `calloc(n, size)` | Allocate zeroed memory |
| `realloc(ptr, size)` | Resize allocation |

**Heap Implementation:**
The user-space heap uses a simple free-list allocator backed by the `sbrk` syscall:

| Component | Description |
|-----------|-------------|
| `sbrk(increment)` | Syscall to adjust program break |
| Allocator | First-fit free-list with coalescing |
| Alignment | 16-byte aligned allocations |
| Initial heap | Grows on demand via sbrk |

**Build:**
The libc is compiled as a static library (`libviperlibc.a`) that can be linked with user programs.

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
| `vinit/vinit.cpp` | ~2,905 | Init process + shell |
| `syscall.hpp` | ~1,567 | Syscall wrappers |
| `libc/src/stdio.c` | ~350 | Standard I/O |
| `libc/src/string.c` | ~200 | String operations |
| `libc/src/stdlib.c` | ~100 | Standard library |

---

## Not Implemented

- Multiple user programs (hello.elf exists, more could be added)
- Shared libraries / dynamic linking
- Environment variables
- Signal handling
- Job control (bg/fg)
- Pipes between commands
- Shell scripting
- Command aliases

---

## Priority Recommendations

1. **High:** Add more user-space programs / applications
2. **High:** Add shell scripting support
3. **Medium:** Implement pipes for command chaining
4. **Medium:** Add environment variable support
5. **Low:** Add job control (background processes)
6. **Low:** Implement shared library support
