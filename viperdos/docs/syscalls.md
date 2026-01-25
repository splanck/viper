# ViperDOS System Call Reference

This document describes the ViperDOS system call interface for user-space programs.

---

## Calling Convention

ViperDOS uses the AArch64 SVC (Supervisor Call) instruction for system calls.

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

ViperDOS syscalls return a `VError` in `x0`:

- `0` on success
- negative values on failure

The canonical list of error codes lives in `include/viperdos/syscall_abi.hpp`. Common codes:

| Code | Name                | Description                               |
|------|---------------------|-------------------------------------------|
| 0    | VERR_OK             | Success                                   |
| -1   | VERR_UNKNOWN        | Unknown error                             |
| -2   | VERR_INVALID_ARG    | Invalid argument                          |
| -3   | VERR_OUT_OF_MEMORY  | Out of memory                             |
| -4   | VERR_NOT_FOUND      | Resource not found                        |
| -5   | VERR_ALREADY_EXISTS | Resource already exists                   |
| -6   | VERR_PERMISSION     | Permission denied                         |
| -7   | VERR_NOT_SUPPORTED  | Operation not supported                   |
| -8   | VERR_BUSY           | Resource busy                             |
| -9   | VERR_TIMEOUT        | Operation timed out                       |
| -100 | VERR_INVALID_HANDLE | Invalid handle                            |
| -300 | VERR_WOULD_BLOCK    | Operation would block (non-blocking APIs) |
| -301 | VERR_CHANNEL_CLOSED | Channel closed                            |
| -302 | VERR_MSG_TOO_LARGE  | Message too large                         |
| -400 | VERR_POLL_FULL      | Poll set full                             |
| -500 | VERR_IO             | I/O error                                 |
| -501 | VERR_NO_RESOURCE    | No resource available                     |
| -502 | VERR_CONNECTION     | Connection error                          |

---

## Syscall Categories

| Range       | Category   | Description                     |
|-------------|------------|---------------------------------|
| 0x00-0x0F   | Task       | Process/task management         |
| 0x10-0x1F   | Channel    | IPC channels                    |
| 0x20-0x2F   | Poll       | Event multiplexing              |
| 0x30-0x3F   | Time       | Timers and sleep                |
| 0x40-0x4F   | File I/O   | POSIX-like file descriptors     |
| 0x50-0x5F   | Network    | TCP/UDP sockets, DNS            |
| 0x60-0x6F   | Directory  | Directory operations            |
| 0x70-0x7F   | Capability | Handle management               |
| 0x80-0x8F   | Handle FS  | Handle-based filesystem         |
| 0x90-0x9F   | Signal     | POSIX signals                   |
| 0xA0-0xAF   | Process    | Process groups, PIDs            |
| 0xC0-0xCF   | Assign     | Logical device assigns          |
| 0xD0-0xDF   | TLS        | TLS sessions                    |
| 0xE0-0xEF   | System     | System information              |
| 0xF0-0xFF   | Debug      | Console I/O, debug              |
| 0x100-0x10F | Device     | Device management, shared memory|
| 0x110-0x11F | GUI        | Display, mouse, input events    |
| 0x120-0x12F | TTY        | Kernel TTY buffer I/O           |

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
- x1: Display name (const char*) or 0 (kernel uses the path)
- x2: Arguments string (const char*) or 0

**Returns:**

- x0: VError (0 on success)
- x1: Process ID (u64) on success
- x2: Task ID (u64) on success
- x3: Bootstrap channel send endpoint handle (u32) on success (may be `0xFFFFFFFF` if unavailable)

**Example:**

```cpp
u64 pid, tid;
i64 err = sys::spawn("/c/hello.prg", nullptr, &pid, &tid, "arg1 arg2");
```

---

### SYS_TASK_SPAWN_SHM (0x0C)

Spawn a new user process from an ELF image stored in a shared memory object.

**Arguments:**

- x0: Shared memory handle (u32)
- x1: Byte offset within the shared memory object (u64)
- x2: ELF image length in bytes (u64)
- x3: Display name (const char*) or 0 (kernel uses a default)
- x4: Arguments string (const char*) or 0

**Returns:**

- x0: VError (0 on success)
- x1: Process ID (u64) on success
- x2: Task ID (u64) on success
- x3: Bootstrap channel send endpoint handle (u32) on success (may be `0xFFFFFFFF` if unavailable)

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

- x1: PID of exited child (u64)

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

### SYS_FORK (0x0B)

Create a child process via copy-on-write fork.

**Arguments:** None

**Returns:**

- In parent: x1 = child PID (u64)
- In child: x1 = 0

**Example:**

```cpp
i64 pid = sys::fork();
if (pid == 0) {
    // Child process
    sys::print("I am the child\n");
} else if (pid > 0) {
    // Parent process
    sys::print("Child PID: ");
    // Wait for child...
}
```

**Notes:**

- Child inherits copy-on-write mappings of parent's address space
- Child gets a new capability table (copy of parent's)
- Child's PID and PPID differ from parent

---

### SYS_TASK_LIST (0x05)

Enumerate running tasks into a buffer.

**Arguments:**

- x0: Buffer for TaskInfo structures (void*) or nullptr
- x1: Maximum entries (u32)

**Returns:**

- x1: Number of tasks (when buffer is nullptr: total count)

---

### SYS_TASK_SET_PRIORITY (0x06)

Set the priority of a task.

**Arguments:**

- x0: Task ID (u64)
- x1: Priority (u8, 0=highest, 255=lowest)

**Returns:** 0 on success

---

### SYS_TASK_GET_PRIORITY (0x07)

Get the priority of a task.

**Arguments:**

- x0: Task ID (u64)

**Returns:**

- x1: Priority (u8)

---

### SYS_REPLACE (0x0F)

Replace current process image with a new executable (exec-like).

**Arguments:**

- x0: Path to ELF file (const char*)
- x1: Display name (const char*) or 0
- x2: Arguments string (const char*) or 0

**Returns:** Does not return on success

---

### SYS_SCHED_SETAFFINITY (0x0D)

Set CPU affinity mask for a task.

**Arguments:**

- x0: Task ID (u64, 0 = current task)
- x1: CPU affinity mask (u64)

**Returns:** 0 on success

---

### SYS_SCHED_GETAFFINITY (0x0E)

Get CPU affinity mask for a task.

**Arguments:**

- x0: Task ID (u64, 0 = current task)

**Returns:**

- x1: CPU affinity mask (u64)

---

## Channel IPC (0x10-0x1F)

### SYS_CHANNEL_CREATE (0x10)

Create a new IPC channel.

**Arguments:** None

**Returns:**

- x1: Send endpoint handle (u32)
- x2: Recv endpoint handle (u32)

**Example:**

```cpp
auto ch = sys::channel_create();
u32 send_handle = static_cast<u32>(ch.val0);
u32 recv_handle = static_cast<u32>(ch.val1);
```

---

### SYS_CHANNEL_SEND (0x11)

Send a message on a channel.

**Arguments:**

- x0: Channel handle (u32)
- x1: Message buffer (const void*)
- x2: Message length (usize, max 256 bytes)
- x3: Handles to transfer (const u32*) or 0
- x4: Handle count (u32, max 4)

**Returns:** `x0 = 0` on success, `x0 = VERR_WOULD_BLOCK` if channel is full

**Example:**

```cpp
const char* msg = "hello";
i64 err = sys::channel_send(send_handle, msg, 6);
```

---

### SYS_CHANNEL_RECV (0x12)

Receive a message from a channel.

**Arguments:**

- x0: Channel handle (u32)
- x1: Buffer to receive into (void*)
- x2: Buffer size (usize)
- x3: Output handle array (u32*) or 0
- x4: In/out handle count (u32*) or 0
    - Input: maximum handles the output array can hold
    - Output: number of handles transferred

**Returns:**

- x1: Number of bytes received (u64)
- x2: Number of handles transferred (u32)

**Example:**

```cpp
char buf[256];
u32 handles[4];
u32 handle_count = 4;
i64 len = sys::channel_recv(recv_handle, buf, sizeof(buf), handles, &handle_count);
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

### SYS_POLL_REMOVE (0x22)

Remove a handle from a poll set.

**Arguments:**

- x0: Poll set handle (u32)
- x1: Handle to remove (u32)

**Returns:** 0 on success

---

### SYS_POLL_WAIT (0x23)

Wait for events in a poll set.

**Arguments:**

- x0: Poll set handle (u32)
- x1: Output PollEvent array (PollEvent*)
- x2: Maximum events to write (u32)
- x3: Timeout in milliseconds (i64, -1 = infinite)

**Returns:**

- x1: Number of PollEvent records written (i64, may be 0)

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

- x1: File descriptor (i32) on success

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

### SYS_FSTAT (0x46)

Get file information from an open file descriptor.

**Arguments:**

- x0: File descriptor (i32)
- x1: Stat structure (Stat*)

**Returns:** 0 on success

---

### SYS_DUP (0x47)

Duplicate a file descriptor to the lowest available slot.

**Arguments:**

- x0: File descriptor (i32)

**Returns:**

- x1: New file descriptor (i32)

---

### SYS_DUP2 (0x48)

Duplicate a file descriptor to a specific slot.

**Arguments:**

- x0: Source file descriptor (i32)
- x1: Target file descriptor (i32)

**Returns:**

- x1: Target file descriptor (i32) on success

---

### SYS_FSYNC (0x49)

Sync file data to storage.

**Arguments:**

- x0: File descriptor (i32)

**Returns:** 0 on success

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

### SYS_SOCKET_POLL (0x56)

Poll a socket for readiness.

**Arguments:**

- x0: Socket descriptor (i32)
- x1: Events to poll for (u32, POLLIN/POLLOUT flags)
- x2: Timeout in milliseconds (i32, -1 = infinite)

**Returns:**

- x1: Returned events (u32, POLLIN/POLLOUT/POLLHUP flags)

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

### SYS_RMDIR (0x62)

Remove an empty directory.

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

### SYS_SYMLINK (0x65)

Create a symbolic link.

**Arguments:**

- x0: Target path (const char*)
- x1: Link path (const char*)

**Returns:** 0 on success

---

### SYS_READLINK (0x66)

Read the target of a symbolic link.

**Arguments:**

- x0: Link path (const char*)
- x1: Buffer (char*)
- x2: Buffer size (usize)

**Returns:**

- x1: Length of target path (i64)

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

### SYS_CAP_DERIVE (0x70)

Derive a new handle with reduced rights from an existing handle.

**Arguments:**

- x0: Source handle (u32)
- x1: Rights mask to retain (u32)

**Returns:**

- x1: New handle (u32)

---

### SYS_CAP_REVOKE (0x71)

Revoke/close a capability handle.

**Arguments:**

- x0: Handle (u32)

**Returns:** 0 on success

---

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

### SYS_CAP_GET_BOUND (0x74)

Get the capability bounding set for the current process.

**Arguments:** None

**Returns:**

- x1: Bounding set mask (u64)

---

### SYS_CAP_DROP_BOUND (0x75)

Drop rights from the capability bounding set (irreversible).

**Arguments:**

- x0: Rights to drop (u64)

**Returns:** 0 on success

---

### SYS_GETRLIMIT (0x76)

Get a resource limit for the current process.

**Arguments:**

- x0: Resource type (u32)

**Returns:**

- x1: Current limit (u64)
- x2: Maximum limit (u64)

---

### SYS_SETRLIMIT (0x77)

Set a resource limit for the current process (can only reduce).

**Arguments:**

- x0: Resource type (u32)
- x1: New limit (u64)

**Returns:** 0 on success

---

### SYS_GETRUSAGE (0x78)

Get current resource usage for the current process.

**Arguments:**

- x0: Who (0 = self, 1 = children)

**Returns:**

- x1: User time in milliseconds (u64)
- x2: System time in milliseconds (u64)

---

## Handle-based Filesystem (0x80-0x8F)

These syscalls provide a capability-based filesystem API using directory/file handles rather than global integer file descriptors.

### SYS_FS_OPEN_ROOT (0x80)

Open the filesystem root directory and return a directory handle.

**Arguments:** None

**Returns:**

- x1: Root directory handle (u32)

---

### SYS_FS_OPEN (0x81)

Open a file or directory relative to a directory handle.

**Arguments:**

- x0: Directory handle (u32)
- x1: Name (const char*)
- x2: Flags (u32)
- x3: Mode (u32)

**Returns:**

- x1: File/directory handle (u32)

---

### SYS_IO_READ (0x82)

Read bytes from a file handle.

**Arguments:**

- x0: File handle (u32)
- x1: Buffer (void*)
- x2: Count (usize)

**Returns:**

- x1: Bytes read (i64)

---

### SYS_IO_WRITE (0x83)

Write bytes to a file handle.

**Arguments:**

- x0: File handle (u32)
- x1: Buffer (const void*)
- x2: Count (usize)

**Returns:**

- x1: Bytes written (i64)

---

### SYS_IO_SEEK (0x84)

Seek within a file handle.

**Arguments:**

- x0: File handle (u32)
- x1: Offset (i64)
- x2: Whence (i32)

**Returns:**

- x1: New position (i64)

---

### SYS_FS_READ_DIR (0x85)

Read the next directory entry from a directory handle.

**Arguments:**

- x0: Directory handle (u32)
- x1: Output DirEnt structure (DirEnt*)

**Returns:** 0 on success, VERR_NOT_FOUND when no more entries

---

### SYS_FS_CLOSE (0x86)

Close a file or directory handle.

**Arguments:**

- x0: Handle (u32)

**Returns:** 0 on success

---

### SYS_FS_REWIND_DIR (0x87)

Reset directory enumeration to the beginning.

**Arguments:**

- x0: Directory handle (u32)

**Returns:** 0 on success

---

## Signal Handling (0x90-0x9F)

POSIX-like signal handling for user-space tasks.

### SYS_SIGACTION (0x90)

Set signal action (handler, mask, flags).

**Arguments:**

- x0: Signal number (i32)
- x1: New action (const sigaction*)
- x2: Old action output (sigaction*) or nullptr

**Returns:** 0 on success

---

### SYS_SIGPROCMASK (0x91)

Set or get the blocked signal mask.

**Arguments:**

- x0: How (i32): SIG_BLOCK, SIG_UNBLOCK, or SIG_SETMASK
- x1: New mask (const sigset_t*) or nullptr
- x2: Old mask output (sigset_t*) or nullptr

**Returns:** 0 on success

---

### SYS_SIGRETURN (0x92)

Return from signal handler (restores original context).

**Arguments:** None

**Returns:** Does not return normally

**Notes:**

- Called automatically by signal trampoline
- Restores saved register state

---

### SYS_KILL (0x93)

Send a signal to a task or process.

**Arguments:**

- x0: Process ID (i64, 0 = current process group)
- x1: Signal number (i32)

**Returns:** 0 on success

---

### SYS_SIGPENDING (0x94)

Get the set of pending signals.

**Arguments:**

- x0: Output signal set (sigset_t*)

**Returns:** 0 on success

---

## Process Groups/Sessions (0xA0-0xAF)

POSIX-like process groups and sessions for job control.

### SYS_GETPID (0xA0)

Get the process ID of the calling process.

**Arguments:** None

**Returns:**

- x1: Process ID (u64)

---

### SYS_GETPPID (0xA1)

Get the parent process ID of the calling process.

**Arguments:** None

**Returns:**

- x1: Parent process ID (u64)

---

### SYS_GETPGID (0xA2)

Get the process group ID of a process.

**Arguments:**

- x0: Process ID (u64, 0 = calling process)

**Returns:**

- x1: Process group ID (u64)

---

### SYS_SETPGID (0xA3)

Set the process group ID of a process.

**Arguments:**

- x0: Process ID (u64, 0 = calling process)
- x1: Process group ID (u64, 0 = use process ID)

**Returns:** 0 on success

---

### SYS_GETSID (0xA4)

Get the session ID of a process.

**Arguments:**

- x0: Process ID (u64, 0 = calling process)

**Returns:**

- x1: Session ID (u64)

---

### SYS_SETSID (0xA5)

Create a new session with the calling process as leader.

**Arguments:** None

**Returns:**

- x1: New session ID (u64)

---

### SYS_GET_ARGS (0xA6)

Get command-line arguments for the current process.

**Arguments:**

- x0: Buffer (char*)
- x1: Buffer size (usize)

**Returns:**

- x1: Length of arguments string (i64)

---

## Assign System (0xC0-0xCF)

The assign system maps short names (e.g., `SYS`) to directory handles, allowing paths like `SYS:foo/bar` to be resolved by the kernel.

### SYS_ASSIGN_SET (0xC0)

Create or update an assign mapping.

**Arguments:**

- x0: Assign name (const char*, e.g., "SYS")
- x1: Directory handle (u32)

**Returns:** 0 on success

---

### SYS_ASSIGN_GET (0xC1)

Query an assign mapping.

**Arguments:**

- x0: Assign name (const char*)
- x1: Output handle (u32*)
- x2: Output flags (u32*) or nullptr

**Returns:** 0 on success

---

### SYS_ASSIGN_REMOVE (0xC2)

Remove an assign mapping.

**Arguments:**

- x0: Assign name (const char*)

**Returns:** 0 on success

---

### SYS_ASSIGN_LIST (0xC3)

List all assigns.

**Arguments:**

- x0: Buffer for AssignInfo structures (void*) or nullptr
- x1: Max entries (u32)

**Returns:**

- x1: Number of assigns (when buffer is nullptr: total count)

---

### SYS_ASSIGN_RESOLVE (0xC4)

Resolve an assign-prefixed path into a capability handle.

**Arguments:**

- x0: Assign name (const char*, e.g., "SYS")
- x1: Path within assign (const char*, e.g., "foo/bar")
- x2: Output handle (u32*)

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

### SYS_TLS_INFO (0xD5)

Query TLS session metadata.

**Arguments:**

- x0: TLS session handle (i32)
- x1: Output TLSInfo structure (TLSInfo*)

**Returns:** 0 on success

**TLSInfo Structure:**

```cpp
struct TLSInfo {
    char cipher_suite[64];     // Negotiated cipher suite name
    char protocol_version[16]; // TLS version (e.g., "TLS 1.3")
    bool client_mode;          // true if client, false if server
};
```

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

### SYS_NET_STATS (0xE1)

Get network statistics.

**Arguments:**

- x0: Output NetStats structure (NetStats*)

**Returns:** 0 on success

**NetStats Structure:**

```cpp
struct NetStats {
    u64 packets_sent;
    u64 packets_recv;
    u64 bytes_sent;
    u64 bytes_recv;
    u64 tcp_connections;
    u64 udp_sockets;
};
```

---

### SYS_PING (0xE2)

Send ICMP ping and measure RTT.

**Arguments:**

- x0: IPv4 address (u32)
- x1: Timeout in milliseconds (u32)

**Returns:**

- x1: Round-trip time in milliseconds (u64) on success

---

### SYS_DEVICE_LIST (0xE3)

List detected hardware devices.

**Arguments:**

- x0: Output DeviceInfo array (DeviceInfo*) or nullptr
- x1: Maximum entries (u32)

**Returns:**

- x1: Number of devices (u64)

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

- x0: VError (0 on success, `VERR_WOULD_BLOCK` if none available)
- x1: Character (i32) on success

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

## Device Management + Shared Memory (0x100-0x10F)

These syscalls are used by user-space display servers (consoled, displayd).

> Note: The security model is currently in flux. Some device syscalls are gated by a temporary
> "allow init descendants" bring-up policy in the kernel.

### SYS_MAP_DEVICE (0x100)

Map a device MMIO region into the calling process's address space.

**Arguments:**

- x0: Device physical address (u64)
- x1: Size of region to map (u64)
- x2: User virtual address hint (u64, 0 = kernel chooses)

**Returns:**

- x1: Virtual address of mapped region (u64) on success

---

### SYS_IRQ_REGISTER (0x101)

Register to receive a specific IRQ.

**Arguments:**

- x0: IRQ number (u32, SPIs typically 32-255)

**Returns:** 0 on success

---

### SYS_IRQ_WAIT (0x102)

Wait for a registered IRQ to fire.

**Arguments:**

- x0: IRQ number (u32)
- x1: Timeout in milliseconds (u64, currently TODO in kernel)

**Returns:** 0 on success

---

### SYS_IRQ_ACK (0x103)

Acknowledge an IRQ after handling (re-enables the IRQ).

**Arguments:**

- x0: IRQ number (u32)

**Returns:** 0 on success

---

### SYS_DMA_ALLOC (0x104)

Allocate a physically contiguous DMA buffer.

**Arguments:**

- x0: Size in bytes (u64)
- x1: Output pointer for physical address (u64*)

**Returns:**

- x1: Virtual address of mapped buffer (u64) on success

---

### SYS_DMA_FREE (0x105)

Free a DMA buffer.

**Arguments:**

- x0: Virtual address returned by `SYS_DMA_ALLOC` (u64)

**Returns:** 0 on success

---

### SYS_VIRT_TO_PHYS (0x106)

Translate a user virtual address to a physical address (for DMA programming).

**Arguments:**

- x0: Virtual address (u64)

**Returns:**

- x1: Physical address (u64) on success

---

### SYS_DEVICE_ENUM (0x107)

Enumerate known device MMIO regions for the platform.

**Arguments:**

- x0: Output array pointer (DeviceInfo*) or 0
- x1: Max entries (u32)

**Returns:**

- x1: Number of devices (u64)

---

### SYS_IRQ_UNREGISTER (0x108)

Unregister from an IRQ and release ownership.

**Arguments:**

- x0: IRQ number (u32)

**Returns:** 0 on success

---

### SYS_SHM_CREATE (0x109)

Create a shared memory object, map it into the creator, and return a transferable handle.

**Arguments:**

- x0: Size in bytes (u64)

**Returns:**

- x1: Shared memory handle (u32)
- x2: Virtual address where it was mapped (u64)
- x3: Size in bytes (u64)

---

### SYS_SHM_MAP (0x10A)

Map a shared memory object into the calling process.

**Arguments:**

- x0: Shared memory handle (u32)

**Returns:**

- x1: Virtual address where it was mapped (u64)
- x2: Size in bytes (u64)

---

### SYS_SHM_UNMAP (0x10B)

Unmap a previously mapped shared memory region.

**Arguments:**

- x0: Virtual address of mapping (u64)

**Returns:** 0 on success

---

### SYS_SHM_CLOSE (0x10C)

Close/release a shared memory handle.

**Arguments:**

- x0: Shared memory handle (u32)

**Returns:** 0 on success

---

## GUI/Display (0x110-0x11F)

GUI-related primitives for display servers and input handling.

### SYS_GET_MOUSE_STATE (0x110)

Get current mouse state (position, buttons, deltas).

**Arguments:**

- x0: Output MouseState structure (MouseState*)

**Returns:** 0 on success

**MouseState Structure:**

```cpp
struct MouseState {
    i32 x;          // Current X position
    i32 y;          // Current Y position
    i32 dx;         // X delta since last read
    i32 dy;         // Y delta since last read
    u32 buttons;    // Button state (bit 0 = left, bit 1 = right, bit 2 = middle)
};
```

---

### SYS_MAP_FRAMEBUFFER (0x111)

Map the framebuffer into user address space.

**Arguments:** None

**Returns:**

- x1: Virtual address of framebuffer (u64)
- x2: Width in pixels (u32)
- x3: Height in pixels (u32)

**Notes:**

- Stride can be calculated as width * 4 (32-bit BGRA)
- Used by display servers to access the screen directly

---

### SYS_SET_MOUSE_BOUNDS (0x112)

Set mouse cursor bounds (for display server).

**Arguments:**

- x0: Maximum X (u32, typically screen width - 1)
- x1: Maximum Y (u32, typically screen height - 1)

**Returns:** 0 on success

---

### SYS_INPUT_HAS_EVENT (0x113)

Check if input events are available.

**Arguments:** None

**Returns:**

- x1: 1 if event available, 0 otherwise

---

### SYS_INPUT_GET_EVENT (0x114)

Get next input event from kernel queue.

**Arguments:**

- x0: Output InputEvent structure (InputEvent*)

**Returns:** 0 on success, VERR_WOULD_BLOCK if no events

**InputEvent Structure:**

```cpp
struct InputEvent {
    u32 type;       // Event type (KEY_DOWN, KEY_UP, MOUSE_MOVE, etc.)
    u32 code;       // Key code or button code
    i32 value;      // Value (e.g., delta for mouse)
    u32 modifiers;  // Modifier key state (shift, ctrl, alt)
};
```

---

### SYS_GCON_SET_GUI_MODE (0x115)

Enable or disable GUI mode for the graphics console.

**Arguments:**

- x0: Enable (bool, 1 = GUI mode, 0 = text mode)

**Returns:** 0 on success

**Notes:**

- When GUI mode is enabled, gcon writes to serial only (not framebuffer)
- Used when displayd takes over the framebuffer

---

## TTY (0x120-0x12F)

Kernel TTY buffer for text-mode console I/O.

### SYS_TTY_READ (0x120)

Read characters from TTY input buffer.

**Arguments:**

- x0: Buffer (char*)
- x1: Buffer size (usize)

**Returns:**

- x1: Bytes read (i64)

**Notes:**

- Blocks until at least one character is available
- Used by consoled for keyboard input

---

### SYS_TTY_WRITE (0x121)

Write characters to TTY output.

**Arguments:**

- x0: Buffer (const char*)
- x1: Length (usize)

**Returns:**

- x1: Bytes written (i64)

**Notes:**

- Renders directly to framebuffer via gcon
- Bypasses IPC for performance

---

### SYS_TTY_PUSH_INPUT (0x122)

Push a character into TTY input buffer.

**Arguments:**

- x0: Character (char)

**Returns:** 0 on success

**Notes:**

- Used internally by timer interrupt to buffer keyboard input
- Not typically called by user applications

---

### SYS_TTY_HAS_INPUT (0x123)

Check if TTY has input available.

**Arguments:** None

**Returns:**

- x1: 1 if input available, 0 otherwise

---

## Using Syscalls in C++

The `syscall.hpp` header provides convenient wrapper functions:

```cpp
#include <syscall.hpp>

int main() {
    // Print to console
    sys::print("Hello from ViperDOS!\n");

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
