# Microkernel Servers

**Status:** Partial implementation for user-space drivers
**Location:** `user/servers/`
**SLOC:** ~3,500

## Overview

ViperOS follows a microkernel architecture where device drivers and system services run in user-space rather than in the kernel. This provides better fault isolation and security at the cost of some IPC overhead.

Currently, three user-space servers are implemented:
- **blkd**: Block device server (VirtIO-blk)
- **fsd**: Filesystem server (ViperFS)
- **netd**: Network device server (VirtIO-net)

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      User Applications                       │
│   (vinit, ssh, sftp, utilities)                             │
└───────────────────────────┬─────────────────────────────────┘
                            │ IPC (Channels)
            ┌───────────────┼───────────────┐
            ▼               ▼               ▼
┌───────────────┐  ┌───────────────┐  ┌───────────────┐
│     fsd       │  │     blkd      │  │     netd      │
│  (filesystem) │  │ (block device)│  │  (network)    │
│  FSD: assign  │  │  BLKD: assign │  │  NETD: assign │
└───────┬───────┘  └───────┬───────┘  └───────┬───────┘
        │                  │                  │
        └──────────────────┼──────────────────┘
                           │ Device Syscalls
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                          Kernel                              │
│   MAP_DEVICE, IRQ_REGISTER, IRQ_WAIT, DMA_ALLOC, SHM_*      │
└─────────────────────────────────────────────────────────────┘
```

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
| SYS_SHM_CREATE | 0x109 | Create shared memory object |
| SYS_SHM_MAP | 0x10A | Map shared memory into address space |
| SYS_SHM_UNMAP | 0x10B | Unmap shared memory |

---

## Block Device Server (blkd)

**Location:** `user/servers/blkd/`
**Status:** Partial (~40% complete)

### Files

| File | Lines | Description |
|------|-------|-------------|
| `main.cpp` | ~300 | Server entry point and main loop |
| `blk_protocol.hpp` | ~100 | IPC message definitions |

### Features Implemented

- VirtIO-blk device discovery and initialization
- MMIO mapping via MAP_DEVICE syscall
- Service channel creation
- Assign registration (BLKD:)
- Block read operations
- Block write operations
- Device info query

### IPC Protocol

```cpp
namespace blk {
    // Request types
    enum {
        BLK_READ = 1,
        BLK_WRITE = 2,
        BLK_FLUSH = 3,
        BLK_INFO = 4,
    };

    struct ReadRequest {
        u32 type;          // BLK_READ
        u32 request_id;
        u64 sector;
        u32 count;
    };

    struct ReadReply {
        u32 type;
        u32 request_id;
        i32 status;
        u32 bytes_read;
        u8 data[512 * 8];  // Up to 8 sectors
    };

    // Similar for Write, Flush, Info...
}
```

### Not Implemented

- Interrupt-driven I/O (currently polling)
- Multiple device support
- Partition table parsing
- Request queuing and coalescing
- Error recovery

### Recommendations

1. **High Priority**: Add IRQ-driven I/O using IRQ_WAIT syscall
2. **Medium Priority**: Support multiple block devices
3. **Low Priority**: Add partition support (GPT/MBR)

---

## Filesystem Server (fsd)

**Location:** `user/servers/fsd/`
**Status:** Partial (~50% complete)

### Files

| File | Lines | Description |
|------|-------|-------------|
| `main.cpp` | ~780 | Server entry point, request handlers |
| `fs_protocol.hpp` | ~200 | IPC message definitions |
| `viperfs.hpp` | ~150 | ViperFS client implementation |
| `viperfs.cpp` | ~400 | ViperFS block I/O layer |

### Features Implemented

- Connection to blkd via IPC
- ViperFS mounting and initialization
- Service channel creation
- Assign registration (FSD:)
- File open/close operations
- File read/write operations
- File seeking
- Stat/fstat operations
- Directory creation (mkdir)
- Directory removal (rmdir)
- File deletion (unlink)
- Path resolution

### IPC Protocol

```cpp
namespace fs {
    // Request types
    enum {
        FS_OPEN = 1,
        FS_CLOSE = 2,
        FS_READ = 3,
        FS_WRITE = 4,
        FS_SEEK = 5,
        FS_STAT = 6,
        FS_FSTAT = 7,
        FS_MKDIR = 8,
        FS_RMDIR = 9,
        FS_UNLINK = 10,
        FS_READDIR = 11,
        FS_RENAME = 12,
    };

    struct OpenRequest {
        u32 type;
        u32 request_id;
        u32 flags;
        u16 path_len;
        char path[MAX_PATH];
    };

    struct OpenReply {
        u32 type;
        u32 request_id;
        i32 status;
        u32 file_id;
    };

    // Similar for other operations...
}
```

### Open File Table

The server maintains per-client file descriptors:

```cpp
struct OpenFile {
    bool in_use;
    u64 inode_num;
    u64 offset;
    u32 flags;
};

static OpenFile g_open_files[MAX_OPEN_FILES];  // 64 files
```

### Not Implemented

- Directory enumeration (readdir)
- File rename
- Symbolic link operations
- File permissions/ownership
- File locking
- Multiple client support (single global file table)
- Caching (relies on blkd caching)

### Recommendations

1. **High Priority**: Implement readdir for directory listing
2. **High Priority**: Per-client file tables
3. **Medium Priority**: File rename support
4. **Medium Priority**: Add local caching for performance
5. **Low Priority**: Extended attributes

---

## Network Device Server (netd)

**Location:** `user/servers/netd/`
**Status:** Partial (~30% complete)

### Files

| File | Lines | Description |
|------|-------|-------------|
| `main.cpp` | ~200 | Server entry point |
| `net_protocol.hpp` | ~100 | IPC message definitions |

### Features Implemented

- VirtIO-net device discovery
- MMIO mapping
- Service channel creation
- Assign registration (NETD:)
- Basic packet receive
- Basic packet send

### Not Implemented

- Full protocol stack (currently in kernel)
- TCP/IP handling
- Socket abstraction
- Multiple interface support
- DHCP client
- Interrupt-driven RX

### Current Architecture Note

The network stack currently runs entirely in the kernel for performance reasons. The netd server is a placeholder for future migration of networking to user-space.

### Recommendations

1. **High Priority**: Complete IRQ-driven packet handling
2. **Medium Priority**: Move ARP/ICMP to netd
3. **Low Priority**: Full TCP/IP migration (significant effort)

---

## libvirtio - User-Space VirtIO Library

**Location:** `user/libvirtio/`
**Status:** Partial (~60% complete)

### Files

| File | Lines | Description |
|------|-------|-------------|
| `device.cpp` | ~200 | Device discovery and mapping |
| `device.hpp` | ~50 | Device interface |
| `blk.cpp` | ~300 | VirtIO-blk driver |
| `blk.hpp` | ~80 | Block device interface |
| `queue.cpp` | ~250 | VirtIO queue management |
| `queue.hpp` | ~100 | Queue interface |

### Features

- VirtIO MMIO device detection
- VirtQueue setup and management
- Descriptor chain building
- Device configuration space access
- Block device read/write

### Not Implemented

- VirtIO-net driver (networking)
- VirtIO-gpu driver (graphics)
- VirtIO-rng driver (random)
- VirtIO-input driver (keyboard/mouse)
- MSI-X interrupt support

---

## Shared Memory IPC

For high-performance data transfer between servers, ViperOS supports shared memory:

### Syscalls

```cpp
// Create shared memory object
i64 shm_create(u64 size);

// Map shared memory into address space
i64 shm_map(u32 shm_handle, u64 *addr);

// Unmap shared memory
i64 shm_unmap(u32 shm_handle);
```

### Usage Pattern

```
1. Server creates shared memory region (SHM_CREATE)
2. Server sends handle to client via IPC channel
3. Client maps shared memory (SHM_MAP)
4. Both parties read/write shared region
5. Synchronization via IPC messages
6. Cleanup with SHM_UNMAP
```

This is used for zero-copy data transfer between blkd and fsd.

---

## Current vs Target Architecture

### Current (Hybrid)

Most functionality remains in the kernel for development convenience:

```
User Space: vinit, utilities
            │
            │ Syscalls (80+ calls)
            ▼
Kernel:     VFS, ViperFS, TCP/IP, VirtIO drivers, Block cache
```

### Target (Microkernel)

Services migrated to user-space:

```
User Space: Applications
            │
            │ IPC (Channels)
            ▼
            fsd ←→ blkd ←→ netd
            │
            │ Device syscalls only
            ▼
Kernel:     Memory, Scheduling, IPC, Capabilities
```

---

## Migration Roadmap

### Phase 1: Block Device (Current)
- [x] blkd basic implementation
- [ ] blkd IRQ-driven I/O
- [ ] blkd request queuing

### Phase 2: Filesystem
- [x] fsd basic implementation
- [ ] fsd full operation support
- [ ] fsd per-client isolation
- [ ] fsd caching layer

### Phase 3: Networking
- [ ] netd packet handling
- [ ] ARP/ICMP in netd
- [ ] UDP in netd
- [ ] TCP in netd (complex)

### Phase 4: Other Drivers
- [ ] VirtIO-GPU user-space driver
- [ ] VirtIO-input user-space driver
- [ ] VirtIO-RNG user-space driver

---

## Performance Considerations

### IPC Overhead

Each operation requires:
1. Message copy to channel buffer
2. Context switch to server
3. Processing
4. Message copy for reply
5. Context switch back to client

Mitigation strategies:
- Shared memory for bulk data
- Batching requests
- Asynchronous operations

### Measured Latency (Approximate)

| Operation | In-Kernel | Via IPC Server |
|-----------|-----------|----------------|
| Block read (4KB) | ~50μs | ~150μs |
| File stat | ~10μs | ~80μs |
| Directory lookup | ~20μs | ~120μs |

### Recommendations

1. Use shared memory for block data transfer
2. Implement request batching in fsd
3. Add client-side caching for metadata
4. Consider zero-copy buffer management
