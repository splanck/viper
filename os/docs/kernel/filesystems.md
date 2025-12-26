# Filesystems: Block Cache, ViperFS, and VFS

ViperOS’ filesystem stack is currently a pragmatic three-layer design:

1. **Block device**: virtio-blk (QEMU `virt`)
2. **Block cache**: small fixed-size LRU cache (`fs::cache`)
3. **On-disk filesystem**: ViperFS (`fs::viperfs`)
4. **POSIX-ish façade**: VFS (`fs::vfs`) with file descriptors and path traversal

This page tells the story of how a path like `/hello.txt` turns into a series of cached block reads and inode/directory
operations.

## Layer 0: virtio-blk (block device)

The lowest layer is the virtio block device driver under `kernel/drivers/virtio/blk.*`. It provides sector-based
reads/writes to the disk image.

The cache and filesystem treat the disk as a sequence of 4 KiB logical blocks; the virtio driver exposes 512-byte
sectors, so the cache converts block numbers to sector numbers.

Key files:

- `kernel/drivers/virtio/blk.hpp`
- `kernel/drivers/virtio/blk.cpp`

## Layer 1: the block cache (`fs::cache`)

The cache in `kernel/fs/cache.*` is a fixed-size LRU cache of filesystem blocks:

- blocks are addressed by logical block number
- each cache block has:
    - `valid` flag
    - `dirty` flag
    - reference count (so in-use blocks are not evicted)
    - LRU pointers and a hash-chain link

### Cache narrative: reading a block

When `fs::cache().get(block_num)` is called:

1. Look up block in hash table.
2. If present: increment refcount, record a hit, move it to the LRU head.
3. If absent: evict the least-recently-used refcount==0 block.
    - if dirty, write it back first
4. Read the requested block from virtio-blk into the cache block.
5. Insert into hash table and return it to the caller (refcount==1).

Callers must `release()` blocks when done to decrement refcount.

Key files:

- `kernel/fs/cache.hpp`
- `kernel/fs/cache.cpp`

## Layer 2: ViperFS (on-disk filesystem)

ViperFS lives in `kernel/fs/viperfs/*` and is described by the on-disk format structs in `kernel/fs/viperfs/format.hpp`.

### Mount narrative

`fs::viperfs::ViperFS::mount()`:

1. Reads block 0 through the cache.
2. Interprets it as a superblock.
3. Validates magic and version.
4. Stores a copy of the superblock in memory and marks the filesystem mounted.

This puts the filesystem into a state where inodes and data blocks can be read via cache-backed accessors.

### Inodes and directories

ViperFS operations are expressed in terms of inodes:

- `read_inode(ino)` reads the inode table block containing `ino` and copies the inode into a heap-allocated `Inode`
  struct.
- directory data is read via `read_data(inode, offset, buf, len)` and parsed into directory entries.
- file data is read/written via direct and indirect block pointers (single and double indirect are implemented; triple
  indirect is not).

Key files:

- `kernel/fs/viperfs/format.hpp`
- `kernel/fs/viperfs/viperfs.hpp`
- `kernel/fs/viperfs/viperfs.cpp`

## Layer 3: VFS (path traversal + file descriptors)

The VFS layer in `kernel/fs/vfs/*` provides a familiar “open/read/write/seek” interface and resolves string paths into
inode numbers by walking directories.

### Path resolution narrative

`fs::vfs::resolve_path("/a/b/c")`:

1. starts from the ViperFS root inode
2. splits the path into components
3. for each component:
    - ensures current inode is a directory
    - looks up the component name in that directory (`viperfs::lookup`)
    - loads the next inode and continues
4. returns the inode number for the final component

### File descriptors (current model)

VFS currently uses a **global file descriptor table** (`FDTable g_fdt`). This is explicitly marked as “will be
per-process later”.

`open()` allocates an FD slot, stores inode + offset + flags, and returns an integer fd.

Key files:

- `kernel/fs/vfs/vfs.hpp`
- `kernel/fs/vfs/vfs.cpp`

## How Assigns relate to the filesystem

Assigns (`SYS:`, `C:`, etc.) are not part of VFS path traversal. They are a separate name-resolution layer implemented
in `kernel/assign/assign.cpp` that can produce directory/file handles by resolving into ViperFS inodes.

See [Assigns](assigns.md) for that story.

## Current limitations and next steps

- VFS errors are sometimes `-1` internally rather than rich `VError` values.
- File descriptor tables are global rather than per-Viper/per-task.
- ViperFS is intentionally small and bring-up focused; features like permissions, journaling, and concurrency control
  are not complete.

