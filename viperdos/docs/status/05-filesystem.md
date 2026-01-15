# Filesystem Subsystem

**Status:** Complete with kernel FS (boot) and user-space fsd server
**Architecture:** Kernel ViperFS (boot) + fsd server + blkd server
**Total SLOC:** ~9,600 (kernel ~6,500 + fsd ~3,100)

## Overview

The ViperDOS filesystem subsystem has two implementations:

1. **Kernel FS** (~6,500 SLOC): Used during boot for loading initial binaries
   - Enabled via `VIPER_KERNEL_ENABLE_FS=1` (default)
   - Block cache, VFS, ViperFS with journaling
   - Located in `kernel/fs/`

2. **fsd server** (~3,100 SLOC): User-space filesystem server for microkernel mode
   - Connects to blkd for block I/O
   - Implements simplified ViperFS
   - Registered as "FSD:"
   - Located in `user/servers/fsd/`

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                       Applications                               │
│              (open, read, write, mkdir, etc.)                   │
└────────────────────────────────┬────────────────────────────────┘
                                 │
                                 ▼
┌─────────────────────────────────────────────────────────────────┐
│                    libc (fsd_backend.cpp)                        │
│         Routes file operations to fsd via IPC                   │
└────────────────────────────────┬────────────────────────────────┘
                                 │ IPC (channels)
                                 ▼
┌─────────────────────────────────────────────────────────────────┐
│                          fsd Server                              │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │                   User-Space ViperFS                         ││
│  │  ┌───────────┐  ┌───────────┐  ┌───────────┐  ┌───────────┐ ││
│  │  │ Open File │  │   Path    │  │   Inode   │  │   Block   │ ││
│  │  │Table (64) │  │ Resolver  │  │   Cache   │  │  Alloc    │ ││
│  │  └───────────┘  └───────────┘  └───────────┘  └───────────┘ ││
│  └─────────────────────────────────────────────────────────────┘│
└────────────────────────────────┬────────────────────────────────┘
                                 │ IPC (blk protocol)
                                 ▼
┌─────────────────────────────────────────────────────────────────┐
│                          blkd Server                             │
│               (VirtIO-blk device access)                        │
└────────────────────────────────┬────────────────────────────────┘
                                 │ MAP_DEVICE, IRQ
                                 ▼
┌─────────────────────────────────────────────────────────────────┐
│                        Kernel (EL1)                              │
│           Device primitives, IRQ routing, memory mapping         │
└────────────────────────────────┬────────────────────────────────┘
                                 │
                                 ▼
┌─────────────────────────────────────────────────────────────────┐
│                    VirtIO-blk Hardware                           │
└─────────────────────────────────────────────────────────────────┘
```

---

## fsd Server (User-Space)

**Location:** `user/servers/fsd/`
**SLOC:** ~3,100
**Registration:** `FSD:` (assign system)

### Components

| File | Lines | Description |
|------|-------|-------------|
| `main.cpp` | ~970 | Server entry, IPC handling, path resolution |
| `viperfs.cpp` | ~1,000 | ViperFS operations |
| `viperfs.hpp` | ~200 | ViperFS interface |
| `format.hpp` | ~160 | On-disk format definitions |
| `fs_protocol.hpp` | ~460 | IPC protocol definitions |
| `blk_client.hpp` | ~360 | Block device client |

### Initialization Sequence

1. Receive bootstrap capabilities from vinit
2. Connect to blkd via "BLKD:" assign lookup
3. Mount ViperFS (read superblock, validate)
4. Create service channel
5. Register as "FSD:"
6. Enter server loop

### IPC Protocol

**Namespace:** `fs`
**Message Size:** 256 bytes max

#### File Operations

| Message Type | Value | Description |
|--------------|-------|-------------|
| FS_OPEN | 1 | Open file (with O_CREAT support) |
| FS_CLOSE | 2 | Close file |
| FS_READ | 3 | Read data (inline, up to 200 bytes) |
| FS_WRITE | 4 | Write data (inline, up to 200 bytes) |
| FS_SEEK | 5 | Seek (SET, CUR, END) |
| FS_STAT | 6 | Stat by path |
| FS_FSTAT | 7 | Stat by file ID |
| FS_FSYNC | 8 | Sync file to disk |

#### Directory Operations

| Message Type | Value | Description |
|--------------|-------|-------------|
| FS_READDIR | 10 | Read directory entries (2 per reply) |
| FS_MKDIR | 11 | Create directory |
| FS_RMDIR | 12 | Remove directory |
| FS_UNLINK | 13 | Remove file |
| FS_RENAME | 14 | Rename/move entry |

### Open Flags

| Flag | Value | Description |
|------|-------|-------------|
| O_RDONLY | 0 | Read only |
| O_WRONLY | 1 | Write only |
| O_RDWR | 2 | Read/write |
| O_CREAT | 0x40 | Create if missing |
| O_TRUNC | 0x200 | Truncate to zero |
| O_APPEND | 0x400 | Append mode |

### File Descriptor Table

- 64 maximum open files
- Tracks inode number, offset, flags
- Server-side management

---

## Kernel Filesystem (Boot)

**Location:** `kernel/fs/`
**SLOC:** ~6,500
**Config:** `VIPER_KERNEL_ENABLE_FS=1` (default)

### Block Cache

**Location:** `kernel/fs/cache.cpp`, `cache.hpp`
**SLOC:** ~925

**Features:**
- 64 block cache (256KB total)
- LRU eviction policy
- Reference counting
- Hash table for O(1) lookup
- Write-back dirty handling
- Block pinning for critical metadata
- Sequential read-ahead (up to 4 blocks)
- Statistics tracking

**CacheBlock Structure:**

| Field | Type | Description |
|-------|------|-------------|
| block_num | u64 | Logical block number |
| data | u8[4096] | Block data |
| valid | bool | Data is valid |
| dirty | bool | Needs write-back |
| pinned | bool | Cannot be evicted |
| refcount | u32 | Reference count |

### VFS Layer

**Location:** `kernel/fs/vfs/`
**SLOC:** ~1,500

**Features:**
- File descriptor table (32 FDs max)
- Path resolution from root
- Open flags (O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, O_APPEND)
- File operations: open, close, read, write, lseek, stat, fstat
- Directory operations: mkdir, rmdir, unlink, rename, getdents
- Symlink operations: symlink, readlink
- dup, dup2 for FD duplication

### ViperFS Driver

**Location:** `kernel/fs/viperfs/`
**SLOC:** ~4,000

**Features:**
- Superblock validation (magic: 0x53465056 = "VPFS")
- 256-byte inodes
- 32-entry inode cache with LRU eviction
- Block bitmap allocation (first-fit)
- Variable-length directory entries
- Direct + single/double indirect blocks
- Write-ahead journaling for metadata

**On-Disk Layout:**

```
Block 0:      Superblock
Blocks 1-N:   Block bitmap
Blocks N+1-M: Inode table (16 inodes/block)
Blocks M+1-:  Data blocks
```

**Inode Structure (256 bytes):**

| Field | Size | Description |
|-------|------|-------------|
| inode_num | 8 | Inode number |
| mode | 4 | Type + permissions |
| size | 8 | File size in bytes |
| blocks | 8 | Allocated blocks |
| atime/mtime/ctime | 24 | Timestamps |
| direct[12] | 96 | Direct block pointers |
| indirect | 8 | Single indirect |
| double_indirect | 8 | Double indirect |

**Block Addressing:**

| Range | Type | Capacity |
|-------|------|----------|
| 0-11 | Direct | 48KB |
| 12-523 | Single Indirect | 2MB |
| 524-262,667 | Double Indirect | 1GB |

### Journal

**Location:** `kernel/fs/viperfs/journal.cpp`
**SLOC:** ~810

**Features:**
- Write-ahead logging (WAL)
- Transaction-based updates
- Checksum validation
- Crash recovery replay

**Journaled Operations:**
- create_file: Inode + parent directory
- unlink: Inode + parent + bitmap
- mkdir: New inode + parent
- rename: Old parent + new parent + inode

---

## libc Integration

**Location:** `user/libc/src/fsd_backend.cpp`

The libc file functions route to fsd:

| libc Function | fsd Message |
|---------------|-------------|
| open() | FS_OPEN |
| close() | FS_CLOSE |
| read() | FS_READ |
| write() | FS_WRITE |
| lseek() | FS_SEEK |
| stat() | FS_STAT |
| fstat() | FS_FSTAT |
| mkdir() | FS_MKDIR |
| rmdir() | FS_RMDIR |
| unlink() | FS_UNLINK |
| rename() | FS_RENAME |
| opendir()/readdir() | FS_OPEN + FS_READDIR |

---

## Performance

### Latency (QEMU)

| Operation | Typical Time |
|-----------|-------------|
| File open (IPC) | ~50μs |
| Read 200 bytes | ~100μs |
| Write 200 bytes | ~150μs |
| Directory list | ~200μs per batch |

### Limitations

| Resource | Limit |
|----------|-------|
| fsd open files | 64 |
| Kernel FD table | 32 |
| Block cache | 64 blocks (256KB) |
| Inode cache | 32 entries |
| Max inline data | 200 bytes |
| Path length | 200 chars |
| Readdir batch | 2 entries |

---

## Recent Additions

- **fsync support**: `FS_FSYNC` message type for syncing file data to disk
- **Close-on-sync**: fsd properly syncs dirty data when files are closed

## Not Implemented

### High Priority
- Per-process FD tables (currently global)
- Symlink resolution in path traversal
- Full O_TRUNC support

### Medium Priority
- Hard links / link count
- File locking (flock, fcntl)
- Permissions enforcement
- Large data via shared memory

### Low Priority
- Extended attributes
- Async I/O
- Background writeback thread
- Multiple filesystems / mounting

---

## Priority Recommendations: Next 5 Steps

### 1. Per-Process File Descriptor Tables
**Impact:** Correct process isolation for file handles
- Move FD table from global to per-Viper structure
- Proper FD inheritance on fork()
- FD close on exec()
- Required for multi-process file safety

### 2. Symlink Resolution in Path Traversal
**Impact:** Complete symbolic link support
- Follow symlinks during path lookup
- Symlink loop detection (max 40 levels)
- O_NOFOLLOW flag support
- Enables flexible directory structures

### 3. File Locking (flock/fcntl)
**Impact:** Multi-process file coordination
- Advisory whole-file locks (flock)
- POSIX record locks (fcntl F_SETLK)
- Lock inheritance across fork()
- Required for databases and concurrent access

### 4. Large File Support via Shared Memory
**Impact:** Efficient I/O for files > 200 bytes
- Automatic SHM allocation for large reads/writes
- Zero-copy data path through shared buffers
- Batch operations to reduce IPC overhead
- Performance improvement for bulk I/O

### 5. Background Writeback Thread
**Impact:** Improved write performance
- Dirty buffer tracking with age timestamps
- Periodic flush of aged dirty blocks
- Separate writeback from synchronous path
- Better write latency for applications
