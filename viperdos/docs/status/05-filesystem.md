# Filesystem Subsystem

**Status:** Complete kernel-based filesystem
**Architecture:** Kernel VFS + ViperFS driver + Block cache
**Total SLOC:** ~6,600 (kernel)

## Overview

ViperDOS implements the filesystem entirely in the kernel:

1. **VFS Layer** (~1,200 SLOC): Path-based and handle-based file operations
2. **ViperFS Driver** (~2,400 SLOC): Native filesystem with journaling
3. **Block Cache** (~600 SLOC): LRU caching with read-ahead
4. **Headers** (~2,400 SLOC): Type definitions and interfaces

Applications use standard POSIX-like syscalls, which are handled directly by the kernel.

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
│                         libc                                     │
│         POSIX wrappers → kernel syscalls                        │
└────────────────────────────────┬────────────────────────────────┘
                                 │ Syscalls (SVC)
                                 ▼
┌─────────────────────────────────────────────────────────────────┐
│                       Kernel (EL1)                               │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │                         VFS Layer                            ││
│  │  ┌───────────┐  ┌───────────┐  ┌───────────┐  ┌───────────┐ ││
│  │  │    FD     │  │   Path    │  │  Handle   │  │   Inode   │ ││
│  │  │Table (64) │  │ Resolver  │  │    API    │  │   Cache   │ ││
│  │  └───────────┘  └───────────┘  └───────────┘  └───────────┘ ││
│  └─────────────────────────────────────────────────────────────┘│
│  ┌─────────────────────────────────────────────────────────────┐│
│  │                    ViperFS Driver                            ││
│  │  ┌───────────┐  ┌───────────┐  ┌───────────┐  ┌───────────┐ ││
│  │  │ Superblock│  │   Block   │  │  Journal  │  │ Directory │ ││
│  │  │Validation │  │  Alloc    │  │   (WAL)   │  │  Entries  │ ││
│  │  └───────────┘  └───────────┘  └───────────┘  └───────────┘ ││
│  └─────────────────────────────────────────────────────────────┘│
│  ┌─────────────────────────────────────────────────────────────┐│
│  │                      Block Cache                             ││
│  │         64 blocks (256KB) │ LRU │ Read-ahead                ││
│  └─────────────────────────────────────────────────────────────┘│
│                              │                                   │
│                    VirtIO-blk Driver                            │
└────────────────────────────────┬────────────────────────────────┘
                                 │
                                 ▼
┌─────────────────────────────────────────────────────────────────┐
│                    VirtIO-blk Hardware                           │
└─────────────────────────────────────────────────────────────────┘
```

---

## Kernel Filesystem Syscalls

### Path-based Operations

| Syscall      | Number | Description                   |
|--------------|--------|-------------------------------|
| SYS_OPEN     | 0x40   | Open file by path             |
| SYS_CLOSE    | 0x41   | Close file descriptor         |
| SYS_READ     | 0x42   | Read from file descriptor     |
| SYS_WRITE    | 0x43   | Write to file descriptor      |
| SYS_LSEEK    | 0x44   | Seek file position            |
| SYS_STAT     | 0x45   | Get file info by path         |
| SYS_FSTAT    | 0x46   | Get file info by descriptor   |
| SYS_DUP      | 0x47   | Duplicate file descriptor     |
| SYS_DUP2     | 0x48   | Duplicate to specific FD      |
| SYS_READDIR  | 0x60   | Read directory entries        |
| SYS_MKDIR    | 0x61   | Create directory              |
| SYS_RMDIR    | 0x62   | Remove directory              |
| SYS_UNLINK   | 0x63   | Delete file                   |
| SYS_RENAME   | 0x64   | Rename file/directory         |
| SYS_SYMLINK  | 0x65   | Create symbolic link          |
| SYS_READLINK | 0x66   | Read symbolic link target     |
| SYS_GETCWD   | 0x67   | Get current working directory |
| SYS_CHDIR    | 0x68   | Change working directory      |

### Handle-based Operations

| Syscall           | Number | Description                 |
|-------------------|--------|-----------------------------|
| SYS_FS_OPEN_ROOT  | 0x80   | Get root directory handle   |
| SYS_FS_OPEN       | 0x81   | Open relative to directory  |
| SYS_IO_READ       | 0x82   | Read from handle            |
| SYS_IO_WRITE      | 0x83   | Write to handle             |
| SYS_IO_SEEK       | 0x84   | Seek handle position        |
| SYS_FS_STAT       | 0x85   | Stat via handle             |
| SYS_FS_READ_DIR   | 0x86   | Read directory entry        |
| SYS_FS_REWIND_DIR | 0x87   | Reset directory enumeration |
| SYS_FS_CLOSE      | 0x88   | Close handle                |
| SYS_FS_MKDIR      | 0x89   | Create directory via handle |
| SYS_FS_UNLINK     | 0x8A   | Delete file via handle      |
| SYS_FS_RENAME     | 0x8B   | Rename via handle           |
| SYS_FS_SYNC       | 0x8C   | Sync file to disk           |
| SYS_FS_TRUNCATE   | 0x8D   | Truncate file               |

---

## VFS Layer

**Location:** `kernel/fs/vfs/`
**SLOC:** ~1,200

### Features

- Global file descriptor table (64 FDs max)
- Path resolution from root
- Open flags (O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, O_APPEND, O_TRUNC)
- File operations: open, close, read, write, lseek, stat, fstat
- Directory operations: mkdir, rmdir, unlink, rename, getdents
- Symlink operations: symlink, readlink
- dup, dup2 for FD duplication
- Per-process working directory tracking

### File Descriptor Table

| Field    | Type | Description         |
|----------|------|---------------------|
| inode    | u32  | Inode number        |
| offset   | u64  | Current file offset |
| flags    | u32  | Open flags          |
| refcount | u32  | Reference count     |
| in_use   | bool | Slot is active      |

---

## ViperFS Driver

**Location:** `kernel/fs/viperfs/`
**SLOC:** ~2,400

### Features

- Superblock validation (magic: 0x53465056 = "VPFS")
- 256-byte inodes
- 32-entry inode cache with LRU eviction
- Block bitmap allocation (first-fit)
- Variable-length directory entries
- Direct + single/double indirect blocks
- Write-ahead journaling for metadata

### On-Disk Layout

```
Block 0:      Superblock
Blocks 1-N:   Block bitmap
Blocks N+1-M: Inode table (16 inodes/block)
Blocks M+1-:  Data blocks
```

### Inode Structure (256 bytes)

| Field             | Size | Description           |
|-------------------|------|-----------------------|
| inode_num         | 8    | Inode number          |
| mode              | 4    | Type + permissions    |
| size              | 8    | File size in bytes    |
| blocks            | 8    | Allocated blocks      |
| atime/mtime/ctime | 24   | Timestamps            |
| direct[12]        | 96   | Direct block pointers |
| indirect          | 8    | Single indirect       |
| double_indirect   | 8    | Double indirect       |

### Block Addressing

| Range       | Type            | Capacity |
|-------------|-----------------|----------|
| 0-11        | Direct          | 48KB     |
| 12-523      | Single Indirect | 2MB      |
| 524-262,667 | Double Indirect | 1GB      |

---

## Block Cache

**Location:** `kernel/fs/cache.cpp`, `cache.hpp`
**SLOC:** ~600

### Features

- 64 block cache (256KB total)
- LRU eviction policy
- Reference counting
- Hash table for O(1) lookup
- Write-back dirty handling
- Block pinning for critical metadata
- Sequential read-ahead (up to 4 blocks)
- Statistics tracking

### CacheBlock Structure

| Field     | Type     | Description          |
|-----------|----------|----------------------|
| block_num | u64      | Logical block number |
| data      | u8[4096] | Block data           |
| valid     | bool     | Data is valid        |
| dirty     | bool     | Needs write-back     |
| pinned    | bool     | Cannot be evicted    |
| refcount  | u32      | Reference count      |

---

## Journal

**Location:** `kernel/fs/viperfs/journal.cpp`
**SLOC:** ~525

### Features

- Write-ahead logging (WAL)
- Transaction-based updates
- Checksum validation
- Crash recovery replay

### Journaled Operations

- create_file: Inode + parent directory
- unlink: Inode + parent + bitmap
- mkdir: New inode + parent
- rename: Old parent + new parent + inode

---

## libc Integration

**Location:** `user/libc/src/unistd.c`, `stat.c`, `dirent.c`

The libc file functions call kernel syscalls directly:

| libc Function       | Kernel Syscall         |
|---------------------|------------------------|
| open()              | SYS_OPEN               |
| close()             | SYS_CLOSE              |
| read()              | SYS_READ               |
| write()             | SYS_WRITE              |
| lseek()             | SYS_LSEEK              |
| stat()              | SYS_STAT               |
| fstat()             | SYS_FSTAT              |
| mkdir()             | SYS_MKDIR              |
| rmdir()             | SYS_RMDIR              |
| unlink()            | SYS_UNLINK             |
| rename()            | SYS_RENAME             |
| opendir()/readdir() | SYS_OPEN + SYS_READDIR |
| getcwd()            | SYS_GETCWD             |
| chdir()             | SYS_CHDIR              |
| dup()/dup2()        | SYS_DUP/SYS_DUP2       |

---

## Performance

### Latency (QEMU)

| Operation      | Typical Time |
|----------------|--------------|
| File open      | ~20μs        |
| Read 4KB       | ~50μs        |
| Write 4KB      | ~80μs        |
| Directory list | ~30μs        |

### Advantages of Kernel Filesystem

- Lower latency (no IPC overhead)
- Direct access to block cache
- Integrated journaling
- Simpler application code

### Resource Limits

| Resource      | Limit             |
|---------------|-------------------|
| Open FDs      | 64                |
| Block cache   | 64 blocks (256KB) |
| Inode cache   | 32 entries        |
| Path length   | 256 chars         |
| Max file size | ~1GB              |

---

## Open Flags

| Flag     | Value | Description       |
|----------|-------|-------------------|
| O_RDONLY | 0     | Read only         |
| O_WRONLY | 1     | Write only        |
| O_RDWR   | 2     | Read/write        |
| O_CREAT  | 0x40  | Create if missing |
| O_TRUNC  | 0x200 | Truncate to zero  |
| O_APPEND | 0x400 | Append mode       |

---

## Not Implemented

### High Priority

- Per-process FD tables (currently global)
- Symlink resolution in path traversal
- Full O_TRUNC support

### Medium Priority

- Hard links / link count
- File locking (flock, fcntl)
- Permissions enforcement

### Low Priority

- Extended attributes
- Async I/O
- Background writeback thread
- Multiple filesystems / mounting

---

## Priority Recommendations: Next Steps

### 1. Per-Process File Descriptor Tables

**Impact:** Correct process isolation for file handles

- Move FD table from global to per-process structure
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

### 4. Background Writeback Thread

**Impact:** Improved write performance

- Dirty buffer tracking with age timestamps
- Periodic flush of aged dirty blocks
- Separate writeback from synchronous path
- Better write latency for applications
