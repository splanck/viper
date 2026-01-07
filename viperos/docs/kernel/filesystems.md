# Filesystems: Block Cache, ViperFS, and VFS

ViperOS provides filesystem access through a dual architecture supporting both kernel-mode and microkernel-mode
operation.

## Architecture Modes

### Microkernel Mode (Default)

In microkernel mode (`VIPER_MICROKERNEL_MODE=1`), filesystem operations run in user space:

```
┌─────────────────────────────────────────────────────────────┐
│                    User Applications                         │
│              (vinit, cat, ls, editors, etc.)                │
├─────────────────────────────────────────────────────────────┤
│                         libc                                 │
│     open() / read() / write() / close() / stat()            │
│                  IPC to fsd server                           │
├─────────────────────────────────────────────────────────────┤
│                      fsd Server                              │
│        ViperFS + VFS + Block Cache (~3,100 SLOC)            │
├─────────────────────────────────────────────────────────────┤
│                     blkd Server                              │
│              VirtIO-blk driver (~800 SLOC)                  │
├─────────────────────────────────────────────────────────────┤
│                    Microkernel                               │
│        IPC channels, device syscalls, shared memory          │
└─────────────────────────────────────────────────────────────┘
```

**Key components:**

- **fsd server** (`user/servers/fsd/`): User-space filesystem server
- **blkd server** (`user/servers/blkd/`): User-space block device driver
- **libc** (`user/libc/`): POSIX-like file API that IPCs to fsd

### Kernel Mode (Boot/Fallback)

The kernel retains a complete filesystem implementation for:

- Boot-time file access (loading init process)
- Fallback when microkernel servers aren't running
- Development and debugging

Key files (kernel mode):

- `kernel/fs/cache.*`: Block cache
- `kernel/fs/viperfs/*`: ViperFS implementation
- `kernel/vfs/vfs.*`: VFS layer
- `kernel/drivers/virtio/blk.*`: VirtIO block driver

## fsd Server Protocol

Applications communicate with fsd via IPC channels:

### File Operations

| Message | Code | Description |
|---------|------|-------------|
| `FS_OPEN` | 0x01 | Open file by path |
| `FS_CLOSE` | 0x02 | Close file handle |
| `FS_READ` | 0x03 | Read from file |
| `FS_WRITE` | 0x04 | Write to file |
| `FS_SEEK` | 0x05 | Seek to position |
| `FS_STAT` | 0x06 | Get file metadata |
| `FS_FSTAT` | 0x07 | Get metadata by handle |
| `FS_TRUNCATE` | 0x08 | Truncate file |
| `FS_SYNC` | 0x09 | Sync to disk |

### Directory Operations

| Message | Code | Description |
|---------|------|-------------|
| `FS_OPENDIR` | 0x10 | Open directory |
| `FS_READDIR` | 0x11 | Read directory entry |
| `FS_CLOSEDIR` | 0x12 | Close directory |
| `FS_MKDIR` | 0x13 | Create directory |
| `FS_RMDIR` | 0x14 | Remove directory |
| `FS_UNLINK` | 0x15 | Delete file |
| `FS_RENAME` | 0x16 | Rename file/directory |

## Filesystem Layers

### Layer 0: Block Device (virtio-blk or blkd)

The lowest layer provides sector-based reads/writes:

- **Microkernel mode**: blkd server with user-space VirtIO driver
- **Kernel mode**: `kernel/drivers/virtio/blk.*`

The filesystem treats the disk as 4 KiB logical blocks; the driver handles 512-byte sector translation.

### Layer 1: Block Cache

A fixed-size LRU cache of filesystem blocks:

- Blocks addressed by logical block number
- Each cache block has: valid flag, dirty flag, reference count
- LRU eviction with write-back for dirty blocks
- Hash table for O(1) lookup

**Cache operations:**

```cpp
CacheBlock *cache.get(block_num);    // Get block (loads if needed)
cache.release(block);                 // Release reference
cache.mark_dirty(block);              // Mark for write-back
cache.sync();                         // Flush all dirty blocks
```

Key files:

- `kernel/fs/cache.*` (kernel mode)
- `user/servers/fsd/cache.*` (user mode)

### Layer 2: ViperFS (On-Disk Format)

ViperFS is the native filesystem with these features:

**Superblock (Block 0):**

| Field | Size | Description |
|-------|------|-------------|
| magic | 4 | 0x53465056 ("VPFS") |
| version | 4 | Format version |
| block_size | 4 | Always 4096 |
| total_blocks | 8 | Filesystem size |
| inode_count | 4 | Number of inodes |
| free_blocks | 4 | Free block count |
| root_inode | 4 | Root directory inode |

**Inode structure:**

| Field | Description |
|-------|-------------|
| mode | File type and permissions |
| size | File size in bytes |
| blocks | Block count |
| direct[12] | Direct block pointers |
| indirect | Single indirect pointer |
| double_indirect | Double indirect pointer |
| mtime/ctime/atime | Timestamps |

**Directory entries:**

| Field | Size | Description |
|-------|------|-------------|
| inode | 4 | Inode number |
| rec_len | 2 | Record length |
| name_len | 1 | Name length |
| file_type | 1 | Entry type |
| name | variable | Filename |

Key files:

- `kernel/fs/viperfs/format.hpp`: On-disk structures
- `kernel/fs/viperfs/viperfs.*`: Filesystem implementation

### Layer 3: VFS (Path Resolution)

The VFS layer provides the user-facing API:

**Path resolution:**

1. Start from root inode (or current directory)
2. Split path into components
3. For each component: lookup in directory, load next inode
4. Return final inode number

**File descriptors:**

- Per-process file descriptor table (capability-based in microkernel mode)
- Each descriptor stores: inode, offset, flags, rights

**Capability-based handles:**

In microkernel mode, file handles are capabilities:

```cpp
// Open returns a capability handle
handle_t h = fs_open("/path/to/file", O_RDONLY);

// Read using capability
fs_read(h, buffer, size);

// Derive read-only handle for sharing
handle_t ro = cap_derive(h, CAP_READ);
```

Key files:

- `kernel/vfs/vfs.*` (kernel mode)
- `user/servers/fsd/vfs.*` (user mode)

## Assigns Integration

Assigns (`SYS:`, `C:`, `HOME:`, etc.) integrate with the filesystem:

1. Path parsing detects assign prefix (e.g., `SYS:foo/bar`)
2. Assign table resolves prefix to directory capability
3. Remaining path resolved relative to that directory

This allows logical device names independent of physical mount points.

Key files:

- `kernel/assign/assign.*`
- `user/servers/fsd/assigns.*`

## Device Syscalls for blkd

The blkd server uses device syscalls:

| Syscall | Number | Description |
|---------|--------|-------------|
| `map_device` | 0x100 | Map VirtIO MMIO |
| `irq_register` | 0x101 | Register for block IRQ |
| `irq_wait` | 0x102 | Wait for I/O completion |
| `irq_ack` | 0x103 | Acknowledge interrupt |
| `dma_alloc` | 0x104 | Allocate DMA buffer |
| `virt_to_phys` | 0x106 | Get physical address |

## blkd Server Protocol

fsd communicates with blkd via IPC:

| Message | Code | Description |
|---------|------|-------------|
| `BLK_READ` | 0x01 | Read sectors |
| `BLK_WRITE` | 0x02 | Write sectors |
| `BLK_FLUSH` | 0x03 | Flush to disk |
| `BLK_INFO` | 0x04 | Get disk info |

## Current Limitations

- No journaling (fsck required after crash)
- No symbolic links (hard links only)
- No extended attributes
- Single filesystem type (ViperFS only)
- No disk quotas
- Triple indirect blocks not implemented (max file size ~4GB)

