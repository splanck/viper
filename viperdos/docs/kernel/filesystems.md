# Filesystems: Block Cache, ViperFS, and VFS

ViperDOS provides filesystem access through the kernel-based VFS and ViperFS implementation.

## Architecture

The filesystem is implemented entirely in the kernel:

```
┌─────────────────────────────────────────────────────────────┐
│                    User Applications                         │
│              (vinit, cat, ls, editors, etc.)                │
├─────────────────────────────────────────────────────────────┤
│                         libc                                 │
│     open() / read() / write() / close() / stat()            │
│                  → Kernel syscalls                           │
├─────────────────────────────────────────────────────────────┤
│                        Kernel                                │
│  ┌─────────────────────────────────────────────────────────┐│
│  │                         VFS                              ││
│  │        Path resolution, FD table, syscall handlers      ││
│  ├─────────────────────────────────────────────────────────┤│
│  │                       ViperFS                            ││
│  │        Journaling, inodes, directories (~2,400 SLOC)    ││
│  ├─────────────────────────────────────────────────────────┤│
│  │                     Block Cache                          ││
│  │              LRU, write-back (~600 SLOC)                ││
│  ├─────────────────────────────────────────────────────────┤│
│  │                    VirtIO-blk Driver                     ││
│  │              Sector I/O, IRQ handling                   ││
│  └─────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────┘
```

**Key components:**

- **VFS layer** (`kernel/fs/vfs/`): Path resolution, FD table, syscall dispatch
- **ViperFS** (`kernel/fs/viperfs/`): Journaling filesystem implementation
- **Block cache** (`kernel/fs/cache.*`): LRU cache with write-back
- **VirtIO-blk** (`kernel/drivers/virtio/blk.*`): Block device driver
- **libc** (`user/libc/`): POSIX-like file API calling kernel syscalls

## Filesystem Syscalls

Applications access the filesystem via kernel syscalls:

### File Operations

| Syscall       | Number | Description            |
|---------------|--------|------------------------|
| `SYS_OPEN`    | 0x40   | Open file by path      |
| `SYS_CLOSE`   | 0x41   | Close file descriptor  |
| `SYS_READ`    | 0x42   | Read from file         |
| `SYS_WRITE`   | 0x43   | Write to file          |
| `SYS_LSEEK`   | 0x44   | Seek to position       |
| `SYS_STAT`    | 0x45   | Get file metadata      |
| `SYS_FSTAT`   | 0x46   | Get metadata by FD     |
| `SYS_DUP`     | 0x47   | Duplicate FD           |
| `SYS_DUP2`    | 0x48   | Duplicate to specific  |

### Directory Operations

| Syscall       | Number | Description           |
|---------------|--------|-----------------------|
| `SYS_READDIR` | 0x60   | Read directory entry  |
| `SYS_MKDIR`   | 0x61   | Create directory      |
| `SYS_RMDIR`   | 0x62   | Remove directory      |
| `SYS_UNLINK`  | 0x63   | Delete file           |
| `SYS_RENAME`  | 0x64   | Rename file/directory |
| `SYS_SYMLINK` | 0x65   | Create symbolic link  |
| `SYS_READLINK`| 0x66   | Read symlink target   |
| `SYS_GETCWD`  | 0x67   | Get working directory |
| `SYS_CHDIR`   | 0x68   | Change directory      |

## Filesystem Layers

### Layer 0: Block Device (VirtIO-blk)

The lowest layer provides sector-based reads/writes:

- **Kernel driver**: `kernel/drivers/virtio/blk.*`

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

- `kernel/fs/cache.*`

### Layer 2: ViperFS (On-Disk Format)

ViperFS is the native filesystem with these features:

**Superblock (Block 0):**

| Field        | Size | Description          |
|--------------|------|----------------------|
| magic        | 4    | 0x53465056 ("VPFS")  |
| version      | 4    | Format version       |
| block_size   | 4    | Always 4096          |
| total_blocks | 8    | Filesystem size      |
| inode_count  | 4    | Number of inodes     |
| free_blocks  | 4    | Free block count     |
| root_inode   | 4    | Root directory inode |

**Inode structure:**

| Field             | Description               |
|-------------------|---------------------------|
| mode              | File type and permissions |
| size              | File size in bytes        |
| blocks            | Block count               |
| direct[12]        | Direct block pointers     |
| indirect          | Single indirect pointer   |
| double_indirect   | Double indirect pointer   |
| mtime/ctime/atime | Timestamps                |

**Directory entries:**

| Field     | Size     | Description   |
|-----------|----------|---------------|
| inode     | 4        | Inode number  |
| rec_len   | 2        | Record length |
| name_len  | 1        | Name length   |
| file_type | 1        | Entry type    |
| name      | variable | Filename      |

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

- Per-process file descriptor table (capability-based)
- Each descriptor stores: inode, offset, flags, rights

**Capability-based handles:**

File handles are capabilities:

```cpp
// Open returns a capability handle
handle_t h = fs_open("/path/to/file", O_RDONLY);

// Read using capability
fs_read(h, buffer, size);

// Derive read-only handle for sharing
handle_t ro = cap_derive(h, CAP_READ);
```

Key files:

- `kernel/fs/vfs/*`

## Assigns Integration

Assigns (`SYS:`, `C:`, `HOME:`, etc.) integrate with the filesystem:

1. Path parsing detects assign prefix (e.g., `SYS:foo/bar`)
2. Assign table resolves prefix to directory capability
3. Remaining path resolved relative to that directory

This allows logical device names independent of physical mount points.

Key files:

- `kernel/assign/assign.*`

## Current Limitations

- No journaling (fsck required after crash)
- No symbolic links (hard links only)
- No extended attributes
- Single filesystem type (ViperFS only)
- No disk quotas
- Triple indirect blocks not implemented (max file size ~4GB)

