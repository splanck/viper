# Filesystem Subsystem

**Status:** Fully functional for bring-up
**Location:** `kernel/fs/`
**SLOC:** ~3,400 (unique files)

## Overview

The filesystem subsystem provides a virtual filesystem (VFS) layer, the ViperFS on-disk filesystem implementation, and a block cache for efficient I/O. The design is intentionally simple to support early development while remaining extensible.

---

## Components

### 1. Block Cache (`cache.cpp`, `cache.hpp`)

**Status:** Complete LRU cache with write-back

**Implemented:**
- Fixed-size block cache (64 blocks = 256KB)
- LRU eviction policy
- Reference counting to prevent eviction of in-use blocks
- Hash table for O(1) block lookup
- Write-back dirty block handling
- Automatic sync before eviction
- Spinlock protection for thread safety
- Cache statistics (hits/misses)
- Sequential read-ahead prefetching (up to 4 blocks)
- Automatic detection of sequential access patterns

**Block Size:** 4096 bytes (matches ViperFS block size)

**Cache Structure:**
```
┌─────────────────────────────────────────────────┐
│              Hash Table (32 buckets)            │
│         [bucket] → block → block → ...          │
└─────────────────────────────────────────────────┘
                        ↕
┌─────────────────────────────────────────────────┐
│              LRU Doubly-Linked List             │
│  [HEAD] ←→ [MRU] ←→ ... ←→ [LRU] ←→ [TAIL]     │
└─────────────────────────────────────────────────┘
```

**CacheBlock Structure:**
| Field | Type | Description |
|-------|------|-------------|
| block_num | u64 | Logical block number |
| data | u8[4096] | Block data |
| valid | bool | Data is valid |
| dirty | bool | Needs write-back |
| refcount | u32 | Reference count |
| lru_prev/next | CacheBlock* | LRU list pointers |
| hash_next | CacheBlock* | Hash chain pointer |

**API:**
| Function | Description |
|----------|-------------|
| `get(block_num)` | Get block, load from disk if needed |
| `get_for_write(block_num)` | Get block and mark dirty |
| `release(block)` | Release block reference |
| `sync()` | Write all dirty blocks to disk |
| `invalidate(block_num)` | Invalidate cached block |

**Not Implemented:**
- Async I/O
- Per-file caching
- Memory-pressure callbacks
- Background writeback thread

**Recommendations:**
- Implement background writeback for better latency
- Add adaptive cache sizing based on memory pressure

---

### 2. ViperFS Filesystem (`viperfs/viperfs.cpp`, `viperfs.hpp`)

**Status:** Fully functional on-disk filesystem

**Implemented:**
- Superblock validation (magic: `0x53465056` = "VPFS", version 1)
- Inode table with 256-byte inodes
- Block bitmap allocation (first-fit)
- Directory entries (variable-length, 8-byte aligned)
- Path resolution via directory walking
- File operations:
  - Read data (direct + single/double indirect)
  - Write data (auto-allocates blocks)
  - Create file
  - Unlink file
- Directory operations:
  - Create directory (with `.` and `..`)
  - Remove empty directory
  - Rename/move entries
  - List entries (readdir callback)
- Symbolic link operations:
  - Create symlink (target stored in data blocks)
  - Read symlink target
- Inode lifecycle:
  - Read inode (heap-allocated copy)
  - Write inode back to disk
  - Free inode and associated blocks
- Filesystem sync (superblock + cache)

**On-Disk Layout:**
```
Block 0:  Superblock
Blocks 1-N: Block bitmap (1 bit per block)
Blocks N+1-M: Inode table (16 inodes per block)
Blocks M+1-end: Data blocks
```

**Superblock Structure (4096 bytes):**
| Field | Offset | Description |
|-------|--------|-------------|
| magic | 0 | Magic number (0x53465056) |
| version | 4 | Format version (1) |
| block_size | 8 | Block size (4096) |
| total_blocks | 16 | Total block count |
| free_blocks | 24 | Free block count |
| inode_count | 32 | Total inodes |
| root_inode | 40 | Root directory inode (2) |
| bitmap_start | 48 | Bitmap start block |
| inode_table_start | 64 | Inode table start block |
| data_start | 80 | Data blocks start |
| uuid | 88 | 16-byte volume UUID |
| label | 104 | 64-byte volume label |

**Inode Structure (256 bytes):**
| Field | Size | Description |
|-------|------|-------------|
| inode_num | 8 | Inode number |
| mode | 4 | Type + permissions |
| size | 8 | File size in bytes |
| blocks | 8 | Allocated block count |
| atime/mtime/ctime | 8 each | Timestamps |
| direct[12] | 96 | Direct block pointers |
| indirect | 8 | Single indirect |
| double_indirect | 8 | Double indirect |
| triple_indirect | 8 | Triple indirect (reserved) |

**Block Addressing:**
| Range | Type | Blocks Covered |
|-------|------|----------------|
| 0-11 | Direct | 12 × 4KB = 48KB |
| 12-523 | Single Indirect | 512 × 4KB = 2MB |
| 524-262,667 | Double Indirect | 512² × 4KB = 1GB |

**Inode Types:**
| Mode | Value | Description |
|------|-------|-------------|
| TYPE_FILE | 0x8000 | Regular file |
| TYPE_DIR | 0x4000 | Directory |
| TYPE_LINK | 0xA000 | Symbolic link |

**Directory Entry (variable length):**
| Field | Size | Description |
|-------|------|-------------|
| inode | 8 | Inode number (0 = deleted) |
| rec_len | 2 | Total record length |
| name_len | 1 | Name length |
| file_type | 1 | Entry type |
| name[] | variable | Entry name (not NUL-terminated) |

**Not Implemented:**
- Hard links / link count
- Permissions enforcement
- Timestamps update
- Triple indirect blocks
- Extended attributes
- Journal/transaction logging

**Recommendations:**
- Add journaling for crash consistency
- Add proper timestamp updates
- Consider extent-based allocation for large files

---

### 3. Virtual File System (`vfs/vfs.cpp`, `vfs.hpp`)

**Status:** Complete POSIX-like interface

**Implemented:**
- File descriptor table (global, 32 FDs max)
- Path resolution (absolute paths from root)
- Open flags support:
  - `O_RDONLY`, `O_WRONLY`, `O_RDWR`
  - `O_CREAT` (create if missing)
  - `O_APPEND` (seek to end on open)
  - `O_TRUNC` (defined but not implemented)
- File operations:
  - `open(path, flags)`
  - `close(fd)`
  - `read(fd, buf, len)`
  - `write(fd, buf, len)`
  - `lseek(fd, offset, whence)`
  - `stat(path, st)` / `fstat(fd, st)`
  - `getdents(fd, buf, len)`
  - `dup(oldfd)` / `dup2(oldfd, newfd)`
- Symbolic link operations:
  - `symlink(target, linkpath)`
  - `readlink(path, buf, size)`
- Directory operations:
  - `mkdir(path)`
  - `rmdir(path)`
  - `unlink(path)`
  - `rename(old, new)`

**File Descriptor Entry:**
| Field | Description |
|-------|-------------|
| in_use | Slot is allocated |
| inode_num | Backing inode |
| offset | Current file position |
| flags | Open flags |

**Seek Modes:**
| Mode | Value | Description |
|------|-------|-------------|
| SEEK_SET | 0 | Absolute offset |
| SEEK_CUR | 1 | Relative to current |
| SEEK_END | 2 | Relative to file end |

**Stat Structure:**
| Field | Description |
|-------|-------------|
| ino | Inode number |
| mode | Type + permissions |
| size | File size |
| blocks | Block count |
| atime/mtime/ctime | Timestamps |

**DirEnt Structure:**
| Field | Description |
|-------|-------------|
| ino | Inode number |
| reclen | Record length |
| type | Entry type |
| namelen | Name length |
| name[256] | Entry name |

**Not Implemented:**
- Per-process FD tables (currently global)
- File descriptor inheritance
- File locking (flock)
- Truncate (ftruncate)
- Access mode checking (everything readable/writable)
- File system mounting (single filesystem only)
- Symlink resolution in path traversal
- Relative paths (current working directory)

**Recommendations:**
- Implement per-process FD tables
- Add file locking primitives
- Implement proper O_TRUNC support
- Add symlink resolution in path traversal

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    User Space / Syscalls                     │
│         (open, close, read, write, stat, mkdir, etc.)        │
└────────────────────────────┬────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────┐
│                     VFS Layer (vfs.cpp)                      │
│  ┌──────────────┐  ┌───────────────┐  ┌───────────────────┐ │
│  │ FD Table     │  │ Path Resolver │  │ File/Dir Ops     │ │
│  │ (32 slots)   │  │ (walk dirs)   │  │ (open/read/etc)  │ │
│  └──────────────┘  └───────────────┘  └───────────────────┘ │
└────────────────────────────┬────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────┐
│                  ViperFS Driver (viperfs.cpp)                │
│  ┌──────────────┐  ┌───────────────┐  ┌───────────────────┐ │
│  │ Superblock   │  │ Inode Table   │  │ Block Bitmap      │ │
│  │ (mount info) │  │ (256b inodes) │  │ (allocation)      │ │
│  └──────────────┘  └───────────────┘  └───────────────────┘ │
│  ┌──────────────────────────────────────────────────────────┐│
│  │ Directory Entry Management                               ││
│  │ (add/remove/lookup entries, . and .. handling)          ││
│  └──────────────────────────────────────────────────────────┘│
└────────────────────────────┬────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────┐
│                   Block Cache (cache.cpp)                    │
│  ┌──────────────┐  ┌───────────────┐  ┌───────────────────┐ │
│  │ Hash Table   │  │ LRU List      │  │ Write-back        │ │
│  │ (32 buckets) │  │ (64 blocks)   │  │ (dirty tracking)  │ │
│  └──────────────┘  └───────────────┘  └───────────────────┘ │
└────────────────────────────┬────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────┐
│                  VirtIO Block Device                         │
│                (read_sectors / write_sectors)                │
└─────────────────────────────────────────────────────────────┘
```

---

## Syscall Interface

The filesystem is accessed via these syscalls:

| Syscall | Number | VFS Function |
|---------|--------|--------------|
| open | 0x40 | `vfs::open(path, flags)` |
| close | 0x41 | `vfs::close(fd)` |
| read | 0x42 | `vfs::read(fd, buf, len)` |
| write | 0x43 | `vfs::write(fd, buf, len)` |
| lseek | 0x44 | `vfs::lseek(fd, off, whence)` |
| stat | 0x45 | `vfs::stat(path, st)` |
| fstat | 0x46 | `vfs::fstat(fd, st)` |
| readdir | 0x60 | `vfs::getdents(fd, buf, len)` |
| mkdir | 0x61 | `vfs::mkdir(path)` |
| rmdir | 0x62 | `vfs::rmdir(path)` |
| unlink | 0x63 | `vfs::unlink(path)` |
| rename | 0x64 | `vfs::rename(old, new)` |

---

## Testing

The filesystem is tested via:
- `qemu_storage_tests` - Comprehensive file/directory tests
- Tests cover: file create/read/write, directory create/list/remove, rename

**Test Assertions:**
- File read returns correct data after write
- Directory listing shows created entries
- Rename moves entries between directories
- Unlink removes files
- Rmdir removes empty directories
- Cache survives sequential operations

---

## Files

| File | Lines | Description |
|------|-------|-------------|
| `cache.cpp` | ~424 | Block cache implementation |
| `cache.hpp` | ~221 | Cache interface |
| `cache_guard.hpp` | ~135 | RAII cache guard |
| `vfs/vfs.cpp` | ~674 | VFS layer |
| `vfs/vfs.hpp` | ~379 | VFS interface |
| `viperfs/viperfs.cpp` | ~1332 | ViperFS driver |
| `viperfs/viperfs.hpp` | ~352 | ViperFS interface |
| `viperfs/format.hpp` | ~220 | On-disk format |
| `viperfs/inode_guard.hpp` | ~122 | RAII inode guard |

---

## Priority Recommendations

1. **High:** Add per-process FD tables for proper process isolation
2. **High:** Implement journaling for crash consistency
3. **Medium:** Add symlink resolution in path traversal
4. **Low:** Implement background writeback thread
