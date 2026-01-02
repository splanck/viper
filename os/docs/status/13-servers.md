# Microkernel Servers

**Status:** Complete implementation for user-space services
**Location:** `user/servers/`
**SLOC:** ~8,900

## Overview

ViperOS follows a microkernel architecture where device drivers and system services run in user-space rather than in the kernel. This provides better fault isolation and security. Five user-space servers are implemented:

| Server | Assign | SLOC | Purpose |
|--------|--------|------|---------|
| **netd** | NETD: | ~1,500 | TCP/IP network stack |
| **fsd** | FSD: | ~1,800 | Filesystem operations |
| **blkd** | BLKD: | ~700 | Block device access |
| **consoled** | CONSOLED: | ~600 | Console output |
| **inputd** | INPUTD: | ~1,000 | Keyboard/mouse input |

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                      User Applications                           │
│   (vinit, ssh, sftp, utilities)                                  │
└───────────────────────────┬─────────────────────────────────────┘
                            │ IPC (Channels)
        ┌───────────────────┼───────────────────┐
        ▼                   ▼                   ▼
┌───────────────┐  ┌───────────────┐  ┌───────────────┐
│     netd      │  │     fsd       │  │   consoled    │
│  TCP/IP stack │  │  Filesystem   │  │  Console I/O  │
│  NETD: assign │  │  FSD: assign  │  │ CONSOLED:     │
└───────┬───────┘  └───────┬───────┘  └───────────────┘
        │                  │
        │          ┌───────┴───────┐
        │          ▼               ▼
        │  ┌───────────────┐  ┌───────────────┐
        │  │     blkd      │  │    inputd     │
        │  │  Block device │  │  Keyboard/    │
        │  │  BLKD: assign │  │  Mouse input  │
        │  └───────┬───────┘  └───────────────┘
        │          │
        └──────────┼──────────────────────────────────┐
                   │ Device Syscalls                   │
                   ▼                                   ▼
┌─────────────────────────────────────────────────────────────────┐
│                          Kernel                                  │
│   MAP_DEVICE │ IRQ_REGISTER │ IRQ_WAIT │ DMA_ALLOC │ SHM_*      │
└─────────────────────────────────────────────────────────────────┘
```

---

## Kernel Device Management Syscalls

The kernel provides syscalls for user-space drivers to access hardware:

| Syscall | Number | Description |
|---------|--------|-------------|
| SYS_MAP_DEVICE | 0x100 | Map device MMIO into user address space |
| SYS_IRQ_REGISTER | 0x101 | Register to receive a specific IRQ |
| SYS_IRQ_WAIT | 0x102 | Wait for registered IRQ to fire |
| SYS_IRQ_ACK | 0x103 | Acknowledge IRQ after handling |
| SYS_DMA_ALLOC | 0x104 | Allocate physically contiguous DMA buffer |
| SYS_DMA_FREE | 0x105 | Free DMA buffer |
| SYS_VIRT_TO_PHYS | 0x106 | Get physical address for DMA programming |
| SYS_DEVICE_ENUM | 0x107 | Enumerate available devices |
| SYS_IRQ_UNREGISTER | 0x108 | Unregister from an IRQ |

---

## Network Server (netd)

**Location:** `user/servers/netd/`
**Status:** Complete
**Registration:** `sys::assign_set("NETD", channel_handle)`

### Files

| File | Lines | Description |
|------|-------|-------------|
| `main.cpp` | ~915 | Server entry point and main loop |
| `net_protocol.hpp` | ~447 | IPC message definitions |
| `netstack.hpp` | ~596 | TCP/IP stack implementation |

### Features

- VirtIO-net device initialization
- Complete TCP/IP stack (Ethernet, ARP, IPv4, ICMP, TCP, UDP)
- Socket lifecycle (create, connect, bind, listen, accept, send, recv, close)
- DNS resolution
- Ping diagnostics
- Network statistics
- Event subscription for async I/O notifications
- Shared memory transfers for large payloads (> 200 bytes)
- TCP congestion control (slow start, congestion avoidance)
- TCP retransmission

### IPC Protocol

```cpp
namespace netproto {
    // Request types
    enum MsgType : u32 {
        NET_SOCKET_CREATE = 1,
        NET_SOCKET_CONNECT = 2,
        NET_SOCKET_BIND = 3,
        NET_SOCKET_LISTEN = 4,
        NET_SOCKET_ACCEPT = 5,
        NET_SOCKET_SEND = 6,
        NET_SOCKET_RECV = 7,
        NET_SOCKET_CLOSE = 8,
        NET_SOCKET_STATUS = 10,
        NET_DNS_RESOLVE = 20,
        NET_PING = 40,
        NET_STATS = 41,
        // ... replies are 0x80 + request type
    };

    struct SocketCreateRequest {
        u32 type;       // NET_SOCKET_CREATE
        u32 request_id;
        u16 family;     // AF_INET
        u16 sock_type;  // SOCK_STREAM or SOCK_DGRAM
        u32 protocol;
    };

    struct SocketSendRequest {
        u32 type;       // NET_SOCKET_SEND
        u32 request_id;
        u32 socket_id;
        u32 len;
        u32 flags;
        u8 data[200];   // Inline for small payloads
        // For larger sends: handle[0] = shared memory
    };
}
```

### Usage Example

```cpp
// Create socket
netproto::SocketCreateRequest req{};
req.type = netproto::NET_SOCKET_CREATE;
req.family = netproto::AF_INET;
req.sock_type = netproto::SOCK_STREAM;
sys::channel_send(netd_channel, &req, sizeof(req), nullptr, 0);

// Receive reply
netproto::SocketCreateReply reply;
sys::channel_recv(reply_channel, &reply, sizeof(reply), nullptr, nullptr);
u32 socket_id = reply.socket_id;
```

---

## Filesystem Server (fsd)

**Location:** `user/servers/fsd/`
**Status:** Complete
**Registration:** `sys::assign_set("FSD", channel_handle)`

### Files

| File | Lines | Description |
|------|-------|-------------|
| `main.cpp` | ~968 | Server entry point, request handlers |
| `fs_protocol.hpp` | ~455 | IPC message definitions |
| `viperfs.hpp` | ~199 | ViperFS client implementation |
| `blk_client.hpp` | ~363 | Client for blkd communication |
| `format.hpp` | ~163 | ViperFS on-disk format |

### Features

- Connects to blkd for block device access
- ViperFS mounting and initialization
- File operations: open, close, read, write, seek
- Stat/fstat for file metadata
- Directory operations: readdir, mkdir, rmdir
- File deletion (unlink)
- File rename
- Symlink create/read
- Up to 64 concurrent open files
- Inode-based access with offset tracking

### IPC Protocol

```cpp
namespace fs {
    enum MsgType : u32 {
        FS_OPEN = 1,
        FS_CLOSE = 2,
        FS_READ = 3,
        FS_WRITE = 4,
        FS_SEEK = 5,
        FS_STAT = 6,
        FS_FSTAT = 7,
        FS_FSYNC = 8,
        FS_READDIR = 10,
        FS_MKDIR = 11,
        FS_RMDIR = 12,
        FS_UNLINK = 13,
        FS_RENAME = 14,
        FS_SYMLINK = 20,
        FS_READLINK = 21,
        // ... replies are 0x80 + request type
    };

    struct OpenRequest {
        u32 type;       // FS_OPEN
        u32 request_id;
        u32 flags;      // O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, etc.
        u16 path_len;
        char path[200]; // Path (not null-terminated)
    };
}
```

### Open File Table

```cpp
struct OpenFile {
    bool in_use;
    u64 inode_num;
    u64 offset;
    u32 flags;
};

static OpenFile g_open_files[64];
```

---

## Block Device Server (blkd)

**Location:** `user/servers/blkd/`
**Status:** Complete
**Registration:** `sys::assign_set("BLKD", channel_handle)`

### Files

| File | Lines | Description |
|------|-------|-------------|
| `main.cpp` | ~546 | Server entry point and main loop |
| `blk_protocol.hpp` | ~149 | IPC message definitions |

### Features

- VirtIO-blk device discovery and initialization
- MMIO mapping via MAP_DEVICE syscall
- IRQ-driven I/O via IRQ_REGISTER/IRQ_WAIT
- DMA buffer allocation
- Block read operations (up to 128 sectors per request)
- Block write operations
- Flush/sync operations
- Device info query (sector size, capacity)

### IPC Protocol

```cpp
namespace blk {
    enum MsgType : u32 {
        BLK_READ = 1,
        BLK_WRITE = 2,
        BLK_FLUSH = 3,
        BLK_INFO = 4,
        // Replies: 0x80 + request type
    };

    struct ReadRequest {
        u32 type;       // BLK_READ
        u32 request_id;
        u64 sector;     // Starting sector
        u32 count;      // Number of sectors
    };

    struct ReadReply {
        u32 type;       // BLK_READ_REPLY
        u32 request_id;
        i32 status;     // 0 = success
        u32 bytes_read;
        // handle[0] = shared memory with data
    };

    constexpr u32 MAX_SECTORS_PER_REQUEST = 128;
    constexpr u32 SECTOR_SIZE = 512;
}
```

---

## Console Server (consoled)

**Location:** `user/servers/consoled/`
**Status:** Complete
**Registration:** `sys::assign_set("CONSOLED:", channel_handle)`

### Files

| File | Lines | Description |
|------|-------|-------------|
| `main.cpp` | ~397 | Server entry point |
| `console_protocol.hpp` | ~184 | IPC message definitions |

### Features

- Text output to graphics console (ramfb)
- Cursor position control
- Color settings (foreground/background ARGB)
- Screen clearing
- Show/hide cursor
- Console dimension query (80x25 default)
- Tab stops (8-column boundaries)
- Newline/carriage return/backspace handling

### IPC Protocol

```cpp
namespace console_protocol {
    // Message types
    constexpr uint32_t CON_WRITE = 0x1001;
    constexpr uint32_t CON_CLEAR = 0x1002;
    constexpr uint32_t CON_SET_CURSOR = 0x1003;
    constexpr uint32_t CON_GET_CURSOR = 0x1004;
    constexpr uint32_t CON_SET_COLORS = 0x1005;
    constexpr uint32_t CON_GET_SIZE = 0x1006;
    constexpr uint32_t CON_SHOW_CURSOR = 0x1007;
    constexpr uint32_t CON_HIDE_CURSOR = 0x1008;

    struct WriteRequest {
        uint32_t type;   // CON_WRITE
        uint32_t request_id;
        uint32_t length;
        // Followed by text data
    };

    struct SetColorsRequest {
        uint32_t type;       // CON_SET_COLORS
        uint32_t request_id;
        uint32_t foreground; // ARGB
        uint32_t background; // ARGB
    };
}
```

---

## Input Server (inputd)

**Location:** `user/servers/inputd/`
**Status:** Complete
**Registration:** `sys::assign_set("INPUTD:", channel_handle)`

### Files

| File | Lines | Description |
|------|-------|-------------|
| `main.cpp` | ~751 | Server entry point, event processing |
| `input_protocol.hpp` | ~159 | IPC message definitions |
| `keycodes.hpp` | ~307 | Linux evdev keycode mappings |

### Features

- VirtIO-input device initialization (keyboard)
- IRQ-driven event polling
- ASCII character translation
- Modifier key tracking (Shift, Ctrl, Alt, Meta, Caps Lock)
- Character buffer (64 entries)
- Event queue (64 entries)
- Escape sequence generation for special keys (arrows, home, end, etc.)
- Event subscription for async notifications
- Linux evdev keycode compatibility

### IPC Protocol

```cpp
namespace input_protocol {
    enum MsgType : uint32_t {
        INP_SUBSCRIBE = 1,      // Subscribe to input events
        INP_UNSUBSCRIBE = 2,    // Unsubscribe
        INP_GET_CHAR = 10,      // Get translated character
        INP_GET_EVENT = 11,     // Get raw input event
        INP_GET_MODIFIERS = 12, // Query modifier state
        INP_HAS_INPUT = 13,     // Check availability
        INP_EVENT_NOTIFY = 0x80,// Async notification
        // Replies: 0x80 + request type
    };

    // Modifier key bits
    namespace modifier {
        constexpr uint8_t SHIFT = 0x01;
        constexpr uint8_t CTRL = 0x02;
        constexpr uint8_t ALT = 0x04;
        constexpr uint8_t META = 0x08;
        constexpr uint8_t CAPS_LOCK = 0x10;
    }

    struct InputEvent {
        EventType type;    // KEY_PRESS, KEY_RELEASE, etc.
        uint8_t modifiers;
        uint16_t code;     // Linux evdev keycode
        int32_t value;     // 1=press, 0=release
    };
}
```

---

## Client Libraries

### libnetclient

**Location:** `user/libnetclient/`
**Purpose:** Client library for netd communication

```cpp
// API
int socket_create(int domain, int type, int protocol);
int socket_connect(int socket_id, uint32_t ip, uint16_t port);
ssize_t socket_send(int socket_id, const void *buf, size_t len);
ssize_t socket_recv(int socket_id, void *buf, size_t len);
int socket_close(int socket_id);
int socket_status(int socket_id, uint32_t *flags, uint32_t *rx_avail);
int dns_resolve(const char *hostname, uint32_t *ip_out);
```

### libfsclient

**Location:** `user/libfsclient/`
**Purpose:** Client library for fsd communication

```cpp
// API
int fs_open(const char *path, uint32_t flags);
int fs_close(int file_id);
ssize_t fs_read(int file_id, void *buf, size_t count);
ssize_t fs_write(int file_id, const void *buf, size_t count);
off_t fs_seek(int file_id, off_t offset, int whence);
int fs_stat(const char *path, struct stat *st);
int fs_mkdir(const char *path);
int fs_unlink(const char *path);
int fs_readdir_one(int dir_id, struct dirent *entry);
```

---

## Shared Memory IPC

For high-performance data transfer between servers, ViperOS supports shared memory:

### Pattern

```
1. Server creates shared memory region (SYS_SHM_CREATE)
2. Server sends handle to client via IPC channel
3. Client maps shared memory (SYS_SHM_MAP)
4. Both parties read/write shared region
5. Synchronization via IPC messages
6. Cleanup with SYS_SHM_UNMAP
```

### Usage in netd/blkd

- Data > 200 bytes uses shared memory handles
- Client creates SHM for writes
- Server creates SHM for reads
- Handles transferred in IPC message handles array

---

## Service Discovery

All servers register using the assign system:

```cpp
// Server-side registration
u32 service_channel = /* create channel */;
sys::assign_set("NETD", service_channel);

// Client-side discovery
u32 netd_handle;
i64 result = sys::assign_get("NETD", &netd_handle);
if (result == 0) {
    // netd_handle is valid channel to netd
}
```

---

## libc Integration

The C library routes standard functions to the appropriate server:

### Socket Functions (via netd)

```c
// libc/src/netd_backend.cpp
int socket(int domain, int type, int protocol);
int connect(int sockfd, const struct sockaddr *addr, socklen_t len);
ssize_t send(int sockfd, const void *buf, size_t len, int flags);
ssize_t recv(int sockfd, void *buf, size_t len, int flags);
```

### File Functions (via fsd)

```c
// libc/src/fsd_backend.cpp
int open(const char *path, int flags);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);
```

### FD Routing

```
FDs 0-63:   Reserved for libc (stdio, kernel handles)
FDs 64-127: Routed to fsd (file operations)
Sockets:    Routed to netd (separate namespace)
```

---

## Performance Considerations

### IPC Overhead

Each operation requires:
1. Message copy to channel buffer
2. Context switch to server
3. Processing
4. Message copy for reply
5. Context switch back to client

### Mitigation Strategies

- Shared memory for bulk data (>200 bytes)
- Inline data for small transfers
- Request batching where possible
- Async event subscriptions

### Measured Latency (Approximate)

| Operation | In-Kernel | Via IPC Server |
|-----------|-----------|----------------|
| Block read (4KB) | ~50μs | ~150μs |
| File stat | ~10μs | ~80μs |
| Directory lookup | ~20μs | ~120μs |
| Socket send (small) | ~30μs | ~100μs |
