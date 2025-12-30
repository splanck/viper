# ViperOS System Call Reference

This document describes the ViperOS system call interface for user-space programs.

---

## Calling Convention

ViperOS uses the AArch64 SVC (Supervisor Call) instruction for system calls.

**Input Registers:**
| Register | Purpose |
|----------|---------|
| x8 | Syscall number |
| x0-x5 | Arguments (up to 6) |

**Output Registers:**
| Register | Purpose |
|----------|---------|
| x0 | Error code (0 = success, negative = error) |
| x1 | Result value 0 |
| x2 | Result value 1 |
| x3 | Result value 2 |

**Example (inline assembly):**
```c
register u64 x8 asm("x8") = SYS_TASK_YIELD;
register i64 r0 asm("x0");
asm volatile("svc #0" : "=r"(r0) : "r"(x8) : "memory");
```

---

## Error Codes

| Code | Name | Description |
|------|------|-------------|
| 0 | VERR_OK | Success |
| -1 | VERR_UNKNOWN | Unknown error |
| -2 | VERR_INVALID_ARG | Invalid argument |
| -3 | VERR_NOT_FOUND | Resource not found |
| -4 | VERR_NO_MEMORY | Out of memory |
| -5 | VERR_PERMISSION | Permission denied |
| -6 | VERR_WOULD_BLOCK | Operation would block |
| -7 | VERR_NOT_SUPPORTED | Operation not supported |
| -8 | VERR_BUSY | Resource busy |
| -9 | VERR_EXISTS | Resource already exists |
| -10 | VERR_IO | I/O error |
| -11 | VERR_TIMEOUT | Operation timed out |
| -12 | VERR_INTERRUPTED | Operation interrupted |

---

## Syscall Categories

| Range | Category | Description |
|-------|----------|-------------|
| 0x00-0x0F | Task | Process/task management |
| 0x10-0x1F | Channel | IPC channels |
| 0x20-0x2F | Poll | Event multiplexing |
| 0x30-0x3F | Time | Timers and sleep |
| 0x40-0x4F | File I/O | POSIX-like file descriptors |
| 0x50-0x5F | Network | TCP/UDP sockets, DNS |
| 0x60-0x6F | Directory | Directory operations |
| 0x70-0x7F | Capability | Handle management |
| 0x80-0x8F | Handle FS | Handle-based filesystem |
| 0x90-0x9F | Signal | POSIX signals |
| 0xA0-0xAF | Process | Process groups, PIDs |
| 0xC0-0xCF | Assign | Logical device assigns |
| 0xD0-0xDF | TLS | TLS sessions |
| 0xE0-0xEF | System | System information |
| 0xF0-0xFF | Debug | Console I/O, debug |
| 0x100-0x10F | Device | Device management (microkernel) |

---

## Task Management (0x00-0x0F)

### SYS_TASK_YIELD (0x00)

Yield the CPU to the scheduler.

**Arguments:** None

**Returns:** 0 on success

**Example:**
```cpp
sys::yield();
```

---

### SYS_TASK_EXIT (0x01)

Terminate the calling task with an exit code.

**Arguments:**
- x0: Exit code (i32)

**Returns:** Does not return

**Example:**
```cpp
sys::exit(0);  // Exit with success
sys::exit(1);  // Exit with error
```

---

### SYS_TASK_CURRENT (0x02)

Get the ID of the calling task.

**Arguments:** None

**Returns:**
- x1: Task ID (u64)

**Example:**
```cpp
u64 tid = sys::current();
```

---

### SYS_TASK_SPAWN (0x03)

Spawn a new user process from an ELF file.

**Arguments:**
- x0: Path to ELF file (const char*)
- x1: Parent directory handle or 0 for current (u32)
- x2: Output process ID (u64*)
- x3: Output task ID (u64*)
- x4: Arguments string (const char*) or nullptr

**Returns:** 0 on success, negative on error

**Example:**
```cpp
u64 pid, tid;
i64 err = sys::spawn("/c/hello.elf", 0, &pid, &tid, "arg1 arg2");
```

---

### SYS_WAIT (0x08)

Wait for any child process to exit.

**Arguments:**
- x0: Output exit status (i32*)

**Returns:**
- x1: PID of exited child

**Example:**
```cpp
i32 status;
i64 pid = sys::wait(&status);
```

---

### SYS_WAITPID (0x09)

Wait for a specific child process to exit.

**Arguments:**
- x0: Process ID to wait for (u64)
- x1: Output exit status (i32*)

**Returns:**
- x1: PID of exited child (or negative error)

**Example:**
```cpp
i32 status;
i64 result = sys::waitpid(child_pid, &status);
```

---

### SYS_SBRK (0x0A)

Adjust the program heap break.

**Arguments:**
- x0: Increment in bytes (i64, can be negative)

**Returns:**
- x1: Previous break address

**Example:**
```cpp
void* old_break = sys::sbrk(4096);  // Grow heap by 4KB
```

---

## Channel IPC (0x10-0x1F)

### SYS_CHANNEL_CREATE (0x10)

Create a new IPC channel.

**Arguments:** None

**Returns:**
- x1: Channel handle (u32)

**Example:**
```cpp
u32 ch = sys::channel_create();
```

---

### SYS_CHANNEL_SEND (0x11)

Send a message on a channel.

**Arguments:**
- x0: Channel handle (u32)
- x1: Message buffer (const void*)
- x2: Message length (usize, max 256 bytes)

**Returns:** 0 on success, VERR_WOULD_BLOCK if channel full

**Example:**
```cpp
const char* msg = "hello";
i64 err = sys::channel_send(ch, msg, 6);
```

---

### SYS_CHANNEL_RECV (0x12)

Receive a message from a channel.

**Arguments:**
- x0: Channel handle (u32)
- x1: Buffer to receive into (void*)
- x2: Buffer size (usize)

**Returns:**
- x1: Number of bytes received

**Example:**
```cpp
char buf[256];
i64 len = sys::channel_recv(ch, buf, sizeof(buf));
```

---

### SYS_CHANNEL_CLOSE (0x13)

Close a channel handle.

**Arguments:**
- x0: Channel handle (u32)

**Returns:** 0 on success

---

## Poll/Event Multiplexing (0x20-0x2F)

### SYS_POLL_CREATE (0x20)

Create a new poll set.

**Arguments:** None

**Returns:**
- x1: Poll set handle (u32)

---

### SYS_POLL_ADD (0x21)

Add a handle to a poll set.

**Arguments:**
- x0: Poll set handle (u32)
- x1: Handle to monitor (u32)
- x2: Event mask (u32)

**Returns:** 0 on success

---

### SYS_POLL_WAIT (0x23)

Wait for events in a poll set.

**Arguments:**
- x0: Poll set handle (u32)
- x1: Timeout in milliseconds (i64, -1 = infinite)

**Returns:**
- x1: Triggered event mask (u32)

---

## Time (0x30-0x3F)

### SYS_TIME_NOW (0x30)

Get monotonic time since boot.

**Arguments:** None

**Returns:**
- x1: Milliseconds since boot (u64)

---

### SYS_SLEEP (0x31)

Sleep for a duration.

**Arguments:**
- x0: Milliseconds to sleep (u64)

**Returns:** 0 when sleep completes

**Example:**
```cpp
sys::sleep(1000);  // Sleep for 1 second
```

---

## File Descriptor I/O (0x40-0x4F)

### SYS_OPEN (0x40)

Open a file by path.

**Arguments:**
- x0: Path (const char*)
- x1: Flags (u32)
  - O_RDONLY (0x00): Read only
  - O_WRONLY (0x01): Write only
  - O_RDWR (0x02): Read/write
  - O_CREAT (0x40): Create if not exists
  - O_TRUNC (0x200): Truncate to zero

**Returns:**
- x1: File descriptor (i32, negative on error)

**Example:**
```cpp
i32 fd = sys::open("/path/to/file", O_RDONLY);
i32 fd = sys::open("/new/file", O_WRONLY | O_CREAT);
```

---

### SYS_CLOSE (0x41)

Close a file descriptor.

**Arguments:**
- x0: File descriptor (i32)

**Returns:** 0 on success

---

### SYS_READ (0x42)

Read from a file descriptor.

**Arguments:**
- x0: File descriptor (i32)
- x1: Buffer (void*)
- x2: Count (usize)

**Returns:**
- x1: Bytes read (i64, 0 = EOF)

**Example:**
```cpp
char buf[1024];
i64 n = sys::read(fd, buf, sizeof(buf));
```

---

### SYS_WRITE (0x43)

Write to a file descriptor.

**Arguments:**
- x0: File descriptor (i32)
- x1: Buffer (const void*)
- x2: Count (usize)

**Returns:**
- x1: Bytes written (i64)

---

### SYS_LSEEK (0x44)

Seek within a file.

**Arguments:**
- x0: File descriptor (i32)
- x1: Offset (i64)
- x2: Whence (i32)
  - SEEK_SET (0): Absolute position
  - SEEK_CUR (1): Relative to current
  - SEEK_END (2): Relative to end

**Returns:**
- x1: New position (i64)

---

### SYS_STAT (0x45)

Get file information by path.

**Arguments:**
- x0: Path (const char*)
- x1: Stat structure (Stat*)

**Returns:** 0 on success

**Stat Structure:**
```cpp
struct Stat {
    u64 size;      // File size in bytes
    u32 mode;      // File mode (type + permissions)
    u32 nlink;     // Number of hard links
    u64 atime;     // Access time
    u64 mtime;     // Modification time
    u64 ctime;     // Creation time
};
```

---

## Networking (0x50-0x5F)

### SYS_SOCKET_CREATE (0x50)

Create a TCP socket.

**Arguments:** None

**Returns:**
- x1: Socket descriptor (i32)

---

### SYS_SOCKET_CONNECT (0x51)

Connect to a remote endpoint.

**Arguments:**
- x0: Socket descriptor (i32)
- x1: IPv4 address (u32, big-endian)
- x2: Port (u16)

**Returns:** 0 on success

**Example:**
```cpp
i32 sock = sys::socket_create();
sys::socket_connect(sock, 0x5DB8D70E, 80);  // 93.184.215.14:80
```

---

### SYS_SOCKET_SEND (0x52)

Send data on a socket.

**Arguments:**
- x0: Socket descriptor (i32)
- x1: Buffer (const void*)
- x2: Length (usize)

**Returns:**
- x1: Bytes sent (i64)

---

### SYS_SOCKET_RECV (0x53)

Receive data from a socket.

**Arguments:**
- x0: Socket descriptor (i32)
- x1: Buffer (void*)
- x2: Buffer size (usize)

**Returns:**
- x1: Bytes received (i64, 0 = connection closed)

---

### SYS_SOCKET_CLOSE (0x54)

Close a socket.

**Arguments:**
- x0: Socket descriptor (i32)

**Returns:** 0 on success

---

### SYS_DNS_RESOLVE (0x55)

Resolve a hostname to IPv4 address.

**Arguments:**
- x0: Hostname (const char*)
- x1: Output IP address (u32*)

**Returns:** 0 on success

**Example:**
```cpp
u32 ip;
sys::dns_resolve("example.com", &ip);
```

---

## Directory Operations (0x60-0x6F)

### SYS_READDIR (0x60)

Read directory entries.

**Arguments:**
- x0: Directory file descriptor (i32)
- x1: Buffer for DirEnt structures (void*)
- x2: Buffer size (usize)

**Returns:**
- x1: Bytes written to buffer

**DirEnt Structure:**
```cpp
struct DirEnt {
    u32 inode;      // Inode number
    u16 reclen;     // Record length
    u8  type;       // Entry type (1=file, 2=directory)
    u8  namelen;    // Name length
    char name[256]; // Entry name (NUL-terminated)
};
```

---

### SYS_MKDIR (0x61)

Create a directory.

**Arguments:**
- x0: Path (const char*)

**Returns:** 0 on success

---

### SYS_UNLINK (0x63)

Delete a file.

**Arguments:**
- x0: Path (const char*)

**Returns:** 0 on success

---

### SYS_RENAME (0x64)

Rename a file or directory.

**Arguments:**
- x0: Old path (const char*)
- x1: New path (const char*)

**Returns:** 0 on success

---

### SYS_GETCWD (0x67)

Get current working directory.

**Arguments:**
- x0: Buffer (char*)
- x1: Buffer size (usize)

**Returns:**
- x1: Length of path

---

### SYS_CHDIR (0x68)

Change current working directory.

**Arguments:**
- x0: Path (const char*)

**Returns:** 0 on success

---

## Capability Management (0x70-0x7F)

### SYS_CAP_QUERY (0x72)

Query capability information.

**Arguments:**
- x0: Handle (u32)
- x1: Output CapInfo structure (CapInfo*)

**Returns:** 0 on success

**CapInfo Structure:**
```cpp
struct CapInfo {
    u32 kind;        // Capability kind
    u32 rights;      // Access rights
    u32 generation;  // Generation number
    u32 reserved;
};
```

---

### SYS_CAP_LIST (0x73)

List all capabilities in current process.

**Arguments:**
- x0: Buffer for CapListEntry structures (void*) or nullptr
- x1: Buffer size / max entries

**Returns:**
- x1: Number of capabilities (when buffer is nullptr: total count)

---

## Assign System (0xC0-0xCF)

### SYS_ASSIGN_LIST (0xC3)

List all assigns.

**Arguments:**
- x0: Buffer for AssignInfo structures (void*)
- x1: Max entries
- x2: Output count (usize*)

**Returns:** 0 on success

---

### SYS_ASSIGN_RESOLVE (0xC4)

Resolve an assign name to a handle.

**Arguments:**
- x0: Assign name (const char*, e.g., "SYS")
- x1: Output handle (u32*)

**Returns:** 0 on success

---

## TLS Sessions (0xD0-0xDF)

### SYS_TLS_CREATE (0xD0)

Create a TLS session over a socket.

**Arguments:**
- x0: Socket descriptor (i32)
- x1: Server hostname (const char*)
- x2: Is server mode (bool)

**Returns:**
- x1: TLS session handle (i32)

---

### SYS_TLS_HANDSHAKE (0xD1)

Perform TLS handshake.

**Arguments:**
- x0: TLS session handle (i32)

**Returns:** 0 on success

---

### SYS_TLS_SEND (0xD2)

Send encrypted data.

**Arguments:**
- x0: TLS session handle (i32)
- x1: Buffer (const void*)
- x2: Length (usize)

**Returns:**
- x1: Bytes sent (i64)

---

### SYS_TLS_RECV (0xD3)

Receive and decrypt data.

**Arguments:**
- x0: TLS session handle (i32)
- x1: Buffer (void*)
- x2: Buffer size (usize)

**Returns:**
- x1: Bytes received (i64)

---

### SYS_TLS_CLOSE (0xD4)

Close a TLS session.

**Arguments:**
- x0: TLS session handle (i32)

**Returns:** 0 on success

---

## System Information (0xE0-0xEF)

### SYS_MEM_INFO (0xE0)

Get memory statistics.

**Arguments:**
- x0: Output MemInfo structure (MemInfo*)

**Returns:** 0 on success

**MemInfo Structure:**
```cpp
struct MemInfo {
    u64 total_bytes;   // Total physical memory
    u64 free_bytes;    // Free physical memory
    u64 used_bytes;    // Used physical memory
    u64 total_pages;   // Total pages
    u64 free_pages;    // Free pages
    u64 page_size;     // Page size (typically 4096)
};
```

---

### SYS_PING (0xE2)

Send ICMP ping and measure RTT.

**Arguments:**
- x0: IPv4 address (u32)
- x1: Timeout in milliseconds (u32)

**Returns:**
- x1: Round-trip time in milliseconds (or negative on timeout)

---

## Debug/Console (0xF0-0xFF)

### SYS_DEBUG_PRINT (0xF0)

Print a string to console.

**Arguments:**
- x0: NUL-terminated string (const char*)

**Returns:** 0 on success

---

### SYS_GETCHAR (0xF1)

Read a character from console.

**Arguments:** None

**Returns:**
- x1: Character (i32, or VERR_WOULD_BLOCK if none available)

---

### SYS_PUTCHAR (0xF2)

Write a character to console.

**Arguments:**
- x0: Character (char)

**Returns:** 0 on success

---

### SYS_UPTIME (0xF3)

Get system uptime.

**Arguments:** None

**Returns:**
- x1: Milliseconds since boot (u64)

---

## Using Syscalls in C++

The `syscall.hpp` header provides convenient wrapper functions:

```cpp
#include <syscall.hpp>

int main() {
    // Print to console
    sys::print("Hello from ViperOS!\n");

    // Open and read a file
    i32 fd = sys::open("/path/to/file", sys::O_RDONLY);
    if (fd >= 0) {
        char buf[256];
        i64 n = sys::read(fd, buf, sizeof(buf));
        sys::close(fd);
    }

    // Network request
    i32 sock = sys::socket_create();
    u32 ip;
    sys::dns_resolve("example.com", &ip);
    sys::socket_connect(sock, ip, 80);

    const char* req = "GET / HTTP/1.0\r\n\r\n";
    sys::socket_send(sock, req, sys::strlen(req));

    char response[4096];
    i64 len = sys::socket_recv(sock, response, sizeof(response));
    sys::socket_close(sock);

    return 0;
}
```

---

## Linking User Programs

User programs link against the minimal syscall wrappers:

```cmake
add_executable(myprogram myprogram.cpp)
target_link_libraries(myprogram viperlibc)
set_target_properties(myprogram PROPERTIES
    LINK_FLAGS "-T ${CMAKE_SOURCE_DIR}/user/user.ld"
)
```

The linker script (`user.ld`) places code at the user-space base address and sets up the proper entry point.
