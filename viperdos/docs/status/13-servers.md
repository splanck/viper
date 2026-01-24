# Microkernel Servers

**Status:** Complete implementation for user-space services
**Location:** `user/servers/`
**SLOC:** ~10,500

## Overview

ViperDOS follows a microkernel architecture where device drivers and system services run in user-space rather than in
the kernel. This provides better fault isolation and security. Six user-space servers are implemented:

| Server       | Assign   | SLOC   | Purpose                |
|--------------|----------|--------|------------------------|
| **netd**     | NETD:    | ~3,200 | TCP/IP network stack   |
| **fsd**      | FSD:     | ~3,100 | Filesystem operations  |
| **blkd**     | BLKD:    | ~700   | Block device access    |
| **consoled** | CONSOLED | ~1,600 | GUI terminal emulator  |
| **inputd**   | INPUTD   | ~1,000 | Keyboard/mouse input   |
| **displayd** | DISPLAY  | ~1,700 | Window management, GUI |

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                      User Applications                           │
│   (vinit, ssh, sftp, edit, hello_gui, utilities)                │
└───────────────────────────┬─────────────────────────────────────┘
                            │ IPC (Channels)
        ┌───────────────────┼───────────────────┬─────────────────┐
        ▼                   ▼                   ▼                 ▼
┌───────────────┐  ┌───────────────┐  ┌───────────────┐  ┌───────────────┐
│     netd      │  │     fsd       │  │   consoled    │  │   displayd    │
│  TCP/IP stack │  │  Filesystem   │  │  Console I/O  │  │  Window/GUI   │
│  NETD: assign │  │  FSD: assign  │  │ CONSOLED:     │  │  DISPLAY:     │
└───────┬───────┘  └───────┬───────┘  └───────────────┘  └───────┬───────┘
        │                  │                                     │
        │          ┌───────┴───────┐                             │
        │          ▼               ▼                             │
        │  ┌───────────────┐  ┌───────────────┐                  │
        │  │     blkd      │  │    inputd     │ ◄────────────────┘
        │  │  Block device │  │  Keyboard/    │  (mouse events)
        │  │  BLKD: assign │  │  Mouse input  │
        │  └───────┬───────┘  └───────────────┘
        │          │
        └──────────┼──────────────────────────────────┐
                   │ Device Syscalls                   │
                   ▼                                   ▼
┌─────────────────────────────────────────────────────────────────┐
│                          Kernel                                  │
│   MAP_DEVICE │ IRQ_REGISTER │ IRQ_WAIT │ DMA_ALLOC │ SHM_*      │
│   MAP_FRAMEBUFFER │                                              │
└─────────────────────────────────────────────────────────────────┘
```

---

## Kernel Device Management Syscalls

The kernel provides syscalls for user-space drivers to access hardware:

| Syscall            | Number | Description                               |
|--------------------|--------|-------------------------------------------|
| SYS_MAP_DEVICE     | 0x100  | Map device MMIO into user address space   |
| SYS_IRQ_REGISTER   | 0x101  | Register to receive a specific IRQ        |
| SYS_IRQ_WAIT       | 0x102  | Wait for registered IRQ to fire           |
| SYS_IRQ_ACK        | 0x103  | Acknowledge IRQ after handling            |
| SYS_DMA_ALLOC      | 0x104  | Allocate physically contiguous DMA buffer |
| SYS_DMA_FREE       | 0x105  | Free DMA buffer                           |
| SYS_VIRT_TO_PHYS   | 0x106  | Get physical address for DMA programming  |
| SYS_DEVICE_ENUM    | 0x107  | Enumerate available devices               |
| SYS_IRQ_UNREGISTER | 0x108  | Unregister from an IRQ                    |

---

## Network Server (netd)

**Location:** `user/servers/netd/`
**Status:** Complete
**Registration:** `sys::assign_set("NETD", channel_handle)`

### Files

| File               | Lines | Description                      |
|--------------------|-------|----------------------------------|
| `main.cpp`         | ~915  | Server entry point and main loop |
| `net_protocol.hpp` | ~447  | IPC message definitions          |
| `netstack.hpp`     | ~596  | TCP/IP stack implementation      |

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

| File              | Lines | Description                          |
|-------------------|-------|--------------------------------------|
| `main.cpp`        | ~968  | Server entry point, request handlers |
| `fs_protocol.hpp` | ~455  | IPC message definitions              |
| `viperfs.hpp`     | ~199  | ViperFS client implementation        |
| `blk_client.hpp`  | ~363  | Client for blkd communication        |
| `format.hpp`      | ~163  | ViperFS on-disk format               |

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

| File               | Lines | Description                      |
|--------------------|-------|----------------------------------|
| `main.cpp`         | ~546  | Server entry point and main loop |
| `blk_protocol.hpp` | ~149  | IPC message definitions          |

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
**Status:** Complete - Full GUI terminal emulator
**Registration:** `sys::assign_set("CONSOLED", channel_handle)`

### Files

| File                   | Lines  | Description                             |
|------------------------|--------|-----------------------------------------|
| `main.cpp`             | ~1,394 | GUI terminal emulator with ANSI support |
| `console_protocol.hpp` | ~225   | IPC message definitions                 |

### Overview

Consoled is a GUI-based terminal emulator that runs as a window within displayd. It provides:

- A graphical window displaying text in a scrollable terminal
- Bidirectional IPC with connected clients (output and keyboard input)
- Full ANSI escape sequence processing for colors and cursor control
- Keyboard input forwarding from displayd to connected clients

### Features

**Terminal Emulation:**

- 106x50 character grid (12x12 pixel cells at 1.5x font scaling)
- Per-cell foreground and background colors with attributes (bold, dim, italic, underline, blink, reverse, hidden,
  strikethrough)
- Block cursor with blinking animation (500ms interval)
- ANSI escape sequence parsing (CSI sequences)

**ANSI Escape Sequences Supported:**

- `CSI n m` - SGR (Select Graphic Rendition): colors 0-7, bright 90-97/100-107, 256-color (38;5;n), 24-bit RGB (
  38;2;r;g;b)
- `CSI n A/B/C/D` - Cursor movement (up/down/forward/back)
- `CSI n;m H` or `CSI n;m f` - Cursor positioning
- `CSI n J` - Erase display (0=below, 1=above, 2=all)
- `CSI n K` - Erase line (0=right, 1=left, 2=all)
- `CSI s/u` - Save/restore cursor position
- `CSI ?25h/l` - Show/hide cursor

**Display:**

- Creates GUI window via libgui (DISPLAY service)
- 1.5x scaled font (12x12 pixels, half-unit scaling: scale=3)
- Window positioned at (20, 20) for visibility
- Dirty cell tracking for efficient partial updates
- Row-based damage coalescing

**Bidirectional IPC:**

- Clients connect via CON_CONNECT with a channel handle for receiving input
- Text output via CON_WRITE with ANSI sequence processing
- Keyboard input forwarded via CON_INPUT events
- Console dimensions reported in CON_CONNECT_REPLY

### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        consoled                              │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │ Cell Grid   │  │ ANSI Parser │  │ Keyboard Translator │  │
│  │ 106x50      │  │ CSI/SGR     │  │ Keycode → ASCII     │  │
│  └──────┬──────┘  └──────┬──────┘  └──────────┬──────────┘  │
│         │                │                     │             │
│         ▼                │                     │             │
│  ┌─────────────────────────────────────────────┐            │
│  │              Main Event Loop                 │            │
│  │  1. Drain ALL client messages               │            │
│  │  2. Present dirty cells to window           │            │
│  │  3. Poll GUI events (keyboard)              │            │
│  │  4. Forward keys to connected client        │            │
│  └─────────────────────────────────────────────┘            │
└─────────────────────────────────────────────────────────────┘
         │ libgui                        ▲ IPC
         ▼                               │
    ┌─────────┐                    ┌──────────┐
    │displayd │                    │  vinit   │
    │(window) │                    │ (client) │
    └─────────┘                    └──────────┘
```

### IPC Protocol

```cpp
namespace console_protocol {
    // Request types
    constexpr uint32_t CON_WRITE = 0x1001;       // Write text (with ANSI)
    constexpr uint32_t CON_CLEAR = 0x1002;       // Clear screen
    constexpr uint32_t CON_SET_CURSOR = 0x1003;  // Set cursor position
    constexpr uint32_t CON_GET_CURSOR = 0x1004;  // Get cursor position
    constexpr uint32_t CON_SET_COLORS = 0x1005;  // Set default colors
    constexpr uint32_t CON_GET_SIZE = 0x1006;    // Get dimensions
    constexpr uint32_t CON_SHOW_CURSOR = 0x1007; // Show cursor
    constexpr uint32_t CON_HIDE_CURSOR = 0x1008; // Hide cursor
    constexpr uint32_t CON_CONNECT = 0x1009;     // Connect with input channel

    // Events (consoled → client)
    constexpr uint32_t CON_INPUT = 0x3001;       // Keyboard input event

    // Reply types (0x2000 + request)
    constexpr uint32_t CON_CONNECT_REPLY = 0x2009;

    struct ConnectRequest {
        uint32_t type;       // CON_CONNECT
        uint32_t request_id;
        // handle[0] = reply channel (send endpoint)
        // handle[1] = input channel (send endpoint for consoled to use)
    };

    struct ConnectReply {
        uint32_t type;       // CON_CONNECT_REPLY
        uint32_t request_id;
        int32_t status;      // 0 = success
        uint32_t cols;       // Console columns (106)
        uint32_t rows;       // Console rows (50)
    };

    struct WriteRequest {
        uint32_t type;       // CON_WRITE
        uint32_t request_id;
        uint32_t length;     // Text length
        uint32_t reserved;
        // Followed by text data (up to 4080 bytes)
    };

    struct InputEvent {
        uint32_t type;       // CON_INPUT
        char ch;             // ASCII character (0 for special keys)
        uint8_t pressed;     // 1 = key down, 0 = key up
        uint16_t keycode;    // Raw evdev keycode
        uint8_t modifiers;   // Shift=1, Ctrl=2, Alt=4
        uint8_t _pad[3];
    };
}
```

### Connection Flow

1. Client creates a channel pair for receiving input events
2. Client creates a channel pair for receiving the connect reply
3. Client sends CON_CONNECT with both send endpoints as handles
4. Consoled stores the input channel for keyboard forwarding
5. Consoled sends CON_CONNECT_REPLY with console dimensions
6. Client can now:
    - Send CON_WRITE to output text
    - Receive CON_INPUT events for keyboard input

### Keycode Translation

Consoled translates Linux evdev keycodes to ASCII characters:

| Keycode Range | Description          |
|---------------|----------------------|
| 2-11          | Number keys (1-9, 0) |
| 16-25         | QWERTYUIOP row       |
| 30-38         | ASDFGHJKL row        |
| 44-50         | ZXCVBNM row          |
| 28            | Enter (→ '\n')       |
| 14            | Backspace (→ '\b')   |
| 57            | Space                |
| 15            | Tab (→ '\t')         |

Shift modifier produces uppercase letters and symbols. Special keys (arrows, function keys) are passed as raw keycodes
with `ch=0`.

---

## Input Server (inputd)

**Location:** `user/servers/inputd/`
**Status:** Complete
**Registration:** `sys::assign_set("INPUTD", channel_handle)`

### Files

| File                 | Lines | Description                          |
|----------------------|-------|--------------------------------------|
| `main.cpp`           | ~751  | Server entry point, event processing |
| `input_protocol.hpp` | ~159  | IPC message definitions              |
| `keycodes.hpp`       | ~307  | Linux evdev keycode mappings         |

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

## Display Server (displayd)

**Location:** `user/servers/displayd/`
**Status:** Complete (desktop shell framework operational)
**Registration:** `sys::assign_set("DISPLAY", channel_handle)`

See [16-gui.md](16-gui.md) for complete GUI documentation including:

- displayd architecture and IPC protocol
- libgui client library API
- Taskbar desktop shell
- Window management and compositing

### Summary

| File                   | Lines  | Description                               |
|------------------------|--------|-------------------------------------------|
| `main.cpp`             | ~1,460 | Server entry, compositing, event handling |
| `display_protocol.hpp` | ~260   | IPC message definitions                   |

### Key Features

- Up to 32 concurrent window surfaces
- Per-surface event queues (32 events each)
- Z-ordering for window stacking
- Window decorations with minimize/maximize/close buttons
- Shared memory pixel buffers (zero-copy)
- Software mouse cursor
- Desktop taskbar support via window list protocol

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

### libgui

**Location:** `user/libgui/`
**Purpose:** Client library for displayd communication

See [16-gui.md](16-gui.md) for complete API documentation.

Key functions:

- `gui_init()` / `gui_shutdown()` - Initialization
- `gui_create_window()` / `gui_create_window_ex()` - Window creation
- `gui_get_pixels()` - Direct pixel buffer access
- `gui_present()` - Display update
- `gui_poll_event()` / `gui_wait_event()` - Event handling
- `gui_list_windows()` / `gui_restore_window()` - Taskbar support
- Drawing helpers: `gui_fill_rect()`, `gui_draw_text()`, etc.

---

## Shared Memory IPC

For high-performance data transfer between servers, ViperDOS supports shared memory:

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

| Operation           | In-Kernel | Via IPC Server |
|---------------------|-----------|----------------|
| Block read (4KB)    | ~50μs     | ~150μs         |
| File stat           | ~10μs     | ~80μs          |
| Directory lookup    | ~20μs     | ~120μs         |
| Socket send (small) | ~30μs     | ~100μs         |

---

## Priority Recommendations: Next 5 Steps

### 1. displayd Window Move/Resize via Mouse

**Impact:** Desktop-like window management

- Title bar drag for window move
- Edge/corner drag for resize
- Minimum window size constraints
- Live resize with damage tracking

### 2. displayd Keyboard Event Delivery

**Impact:** Interactive text input in GUI apps

- Route key events from inputd to displayd
- Forward to focused window's event queue
- Enable text editors and terminals in GUI

### 3. fsd Per-Process FD Tables

**Impact:** Correct multi-process file handling

- Move FD tracking to per-client state
- Proper FD inheritance on fork notification
- FD cleanup on client disconnect
- Required for fork/exec workflow

### 4. netd Listen/Accept Improvements

**Impact:** Server-side network applications

- Multiple concurrent listening sockets
- Accept queue depth configuration
- Non-blocking accept with poll integration
- Foundation for HTTP/SSH servers

### 5. Server Health Monitoring

**Impact:** System reliability

- Heartbeat protocol between servers
- Automatic server restart on crash
- Dependency-aware startup ordering
- vinit supervision of critical servers
