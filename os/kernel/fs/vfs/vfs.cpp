/**
 * @file vfs.cpp
 * @brief VFS implementation on top of ViperFS.
 *
 * @details
 * This translation unit implements the ViperOS virtual file system (VFS) layer
 * using ViperFS as the backing filesystem.
 *
 * Responsibilities:
 * - Maintain a file descriptor table (currently global; intended to become per-process).
 * - Resolve paths to inodes by walking directories from the root.
 * - Implement basic file operations (open/close/read/write/seek/stat).
 * - Implement directory operations (getdents/mkdir/rmdir/unlink/rename).
 *
 * Many operations are intentionally simple and return `-1` on error rather than
 * rich error codes; syscall wrappers translate these as needed during bring-up.
 */
#include "vfs.hpp"
#include "../../console/serial.hpp"
#include "../../lib/str.hpp"
#include "../../viper/viper.hpp"
#include "../viperfs/viperfs.hpp"

namespace fs::vfs
{

// Global FD table for kernel-mode operations and backward compatibility
static FDTable g_kernel_fdt;

/** @copydoc fs::vfs::init */
void init()
{
    g_kernel_fdt.init();
    serial::puts("[vfs] VFS initialized\n");
}

/** @copydoc fs::vfs::kernel_fdt */
FDTable *kernel_fdt()
{
    return &g_kernel_fdt;
}

/** @copydoc fs::vfs::current_fdt */
FDTable *current_fdt()
{
    // Get current process's FD table if available
    viper::Viper *v = viper::current();
    if (v && v->fd_table)
    {
        return v->fd_table;
    }

    // Fall back to kernel FD table for compatibility
    return &g_kernel_fdt;
}

/** @copydoc fs::vfs::close_all_fds */
void close_all_fds(FDTable *fdt)
{
    if (!fdt)
        return;

    for (usize i = 0; i < MAX_FDS; i++)
    {
        if (fdt->fds[i].in_use)
        {
            fdt->free(static_cast<i32>(i));
        }
    }
}

// Use lib::strlen for string length operations

// Resolve path to inode number
/** @copydoc fs::vfs::resolve_path */
u64 resolve_path(const char *path)
{
    if (!path || !viperfs::viperfs().is_mounted())
        return 0;

    // Start from root
    viperfs::Inode *current = viperfs::viperfs().read_inode(viperfs::ROOT_INODE);
    if (!current)
        return 0;

    // Skip leading slashes
    while (*path == '/')
        path++;

    // Empty path means root
    if (*path == '\0')
    {
        u64 ino = current->inode_num;
        viperfs::viperfs().release_inode(current);
        return ino;
    }

    // Parse path components
    while (*path)
    {
        // Skip slashes
        while (*path == '/')
            path++;
        if (*path == '\0')
            break;

        // Find end of component
        const char *start = path;
        while (*path && *path != '/')
            path++;
        usize len = path - start;

        // Lookup component
        if (!viperfs::is_directory(current))
        {
            viperfs::viperfs().release_inode(current);
            return 0; // Not a directory
        }

        u64 next_ino = viperfs::viperfs().lookup(current, start, len);
        viperfs::viperfs().release_inode(current);

        if (next_ino == 0)
            return 0; // Not found

        current = viperfs::viperfs().read_inode(next_ino);
        if (!current)
            return 0;
    }

    u64 result = current->inode_num;
    viperfs::viperfs().release_inode(current);
    return result;
}

/**
 * @brief Resolve a path to its parent directory inode and final component name.
 *
 * @details
 * Splits `path` at the final `/` and resolves the parent directory path via
 * @ref resolve_path. The returned `name` pointer refers into the original path
 * string; it is not copied.
 *
 * This helper is used by create/remove/rename operations that need to operate
 * on a parent directory and entry name.
 *
 * @param path Full path string.
 * @param parent_ino Output: parent directory inode number.
 * @param name Output: pointer to the final component name within `path`.
 * @param name_len Output: length of the final component.
 * @return `true` on success, otherwise `false`.
 */
static bool resolve_parent(const char *path, u64 *parent_ino, const char **name, usize *name_len)
{
    if (!path || !parent_ino || !name || !name_len)
        return false;

    // Skip leading slashes
    while (*path == '/')
        path++;

    // Find the last component
    const char *last_slash = nullptr;
    const char *p = path;
    while (*p)
    {
        if (*p == '/')
            last_slash = p;
        p++;
    }

    if (!last_slash)
    {
        // No slash - file in root
        *parent_ino = viperfs::ROOT_INODE;
        *name = path;
        *name_len = lib::strlen(path);
        return true;
    }

    // Get parent path
    usize parent_len = last_slash - path;
    char parent_path[MAX_PATH];
    if (parent_len >= MAX_PATH)
        return false;

    for (usize i = 0; i < parent_len; i++)
    {
        parent_path[i] = path[i];
    }
    parent_path[parent_len] = '\0';

    // Resolve parent
    u64 pino = resolve_path(parent_path);
    if (pino == 0)
        return false;

    *parent_ino = pino;
    *name = last_slash + 1;
    *name_len = lib::strlen(last_slash + 1);
    return true;
}

/** @copydoc fs::vfs::open */
i32 open(const char *path, u32 oflags)
{
    if (!path || !viperfs::viperfs().is_mounted())
        return -1;

    FDTable *fdt = current_fdt();
    if (!fdt)
        return -1;

    u64 ino = resolve_path(path);

    // Handle O_CREAT
    if (ino == 0 && (oflags & flags::O_CREAT))
    {
        u64 parent_ino;
        const char *name;
        usize name_len;

        if (!resolve_parent(path, &parent_ino, &name, &name_len))
        {
            return -1;
        }

        viperfs::Inode *parent = viperfs::viperfs().read_inode(parent_ino);
        if (!parent)
            return -1;

        ino = viperfs::viperfs().create_file(parent, name, name_len);
        viperfs::viperfs().release_inode(parent);

        if (ino == 0)
            return -1;
    }

    if (ino == 0)
        return -1; // File not found

    // Allocate file descriptor
    i32 fd = fdt->alloc();
    if (fd < 0)
        return -1;

    FileDesc *desc = fdt->get(fd);
    desc->inode_num = ino;
    desc->offset = 0;
    desc->flags = oflags;

    // Handle O_TRUNC
    if (oflags & flags::O_TRUNC)
    {
        // TODO: Truncate file
    }

    // Handle O_APPEND
    if (oflags & flags::O_APPEND)
    {
        viperfs::Inode *inode = viperfs::viperfs().read_inode(ino);
        if (inode)
        {
            desc->offset = inode->size;
            viperfs::viperfs().release_inode(inode);
        }
    }

    return fd;
}

/** @copydoc fs::vfs::dup */
i32 dup(i32 oldfd)
{
    FDTable *fdt = current_fdt();
    if (!fdt)
        return -1;

    FileDesc *old_desc = fdt->get(oldfd);
    if (!old_desc)
        return -1;

    // Find lowest available fd
    i32 newfd = fdt->alloc();
    if (newfd < 0)
        return -1;

    // Copy the fd entry
    FileDesc *new_desc = fdt->get(newfd);
    new_desc->inode_num = old_desc->inode_num;
    new_desc->offset = old_desc->offset;
    new_desc->flags = old_desc->flags;

    return newfd;
}

/** @copydoc fs::vfs::dup2 */
i32 dup2(i32 oldfd, i32 newfd)
{
    FDTable *fdt = current_fdt();
    if (!fdt)
        return -1;

    // Validate oldfd
    FileDesc *old_desc = fdt->get(oldfd);
    if (!old_desc)
        return -1;

    // Validate newfd range
    if (newfd < 0 || static_cast<usize>(newfd) >= MAX_FDS)
        return -1;

    // If same fd, just return it
    if (oldfd == newfd)
        return newfd;

    // Close newfd if it's open
    if (fdt->fds[newfd].in_use)
    {
        fdt->free(newfd);
    }

    // Mark newfd as in use and copy
    fdt->fds[newfd].in_use = true;
    fdt->fds[newfd].inode_num = old_desc->inode_num;
    fdt->fds[newfd].offset = old_desc->offset;
    fdt->fds[newfd].flags = old_desc->flags;

    return newfd;
}

/** @copydoc fs::vfs::close */
i32 close(i32 fd)
{
    FDTable *fdt = current_fdt();
    if (!fdt)
        return -1;

    FileDesc *desc = fdt->get(fd);
    if (!desc)
        return -1;

    fdt->free(fd);
    return 0;
}

/** @copydoc fs::vfs::read */
i64 read(i32 fd, void *buf, usize len)
{
    FDTable *fdt = current_fdt();
    if (!fdt)
        return -1;

    FileDesc *desc = fdt->get(fd);
    if (!desc)
        return -1;

    // Check read permission
    u32 access = desc->flags & 0x3;
    if (access == flags::O_WRONLY)
        return -1;

    viperfs::Inode *inode = viperfs::viperfs().read_inode(desc->inode_num);
    if (!inode)
        return -1;

    i64 bytes = viperfs::viperfs().read_data(inode, desc->offset, buf, len);
    if (bytes > 0)
    {
        desc->offset += bytes;
    }

    viperfs::viperfs().release_inode(inode);
    return bytes;
}

/** @copydoc fs::vfs::write */
i64 write(i32 fd, const void *buf, usize len)
{
    FDTable *fdt = current_fdt();
    if (!fdt)
        return -1;

    FileDesc *desc = fdt->get(fd);
    if (!desc)
        return -1;

    // Check write permission
    u32 access = desc->flags & 0x3;
    if (access == flags::O_RDONLY)
        return -1;

    viperfs::Inode *inode = viperfs::viperfs().read_inode(desc->inode_num);
    if (!inode)
        return -1;

    i64 bytes = viperfs::viperfs().write_data(inode, desc->offset, buf, len);
    if (bytes > 0)
    {
        desc->offset += bytes;
        viperfs::viperfs().write_inode(inode);
    }

    viperfs::viperfs().release_inode(inode);
    return bytes;
}

/** @copydoc fs::vfs::lseek */
i64 lseek(i32 fd, i64 offset, i32 whence)
{
    FDTable *fdt = current_fdt();
    if (!fdt)
        return -1;

    FileDesc *desc = fdt->get(fd);
    if (!desc)
        return -1;

    i64 new_offset;

    switch (whence)
    {
        case seek::SET:
            new_offset = offset;
            break;
        case seek::CUR:
            new_offset = static_cast<i64>(desc->offset) + offset;
            break;
        case seek::END:
        {
            viperfs::Inode *inode = viperfs::viperfs().read_inode(desc->inode_num);
            if (!inode)
                return -1;
            new_offset = static_cast<i64>(inode->size) + offset;
            viperfs::viperfs().release_inode(inode);
            break;
        }
        default:
            return -1;
    }

    if (new_offset < 0)
        return -1;

    desc->offset = static_cast<u64>(new_offset);
    return new_offset;
}

/** @copydoc fs::vfs::stat */
i32 stat(const char *path, Stat *st)
{
    if (!path || !st)
        return -1;

    u64 ino = resolve_path(path);
    if (ino == 0)
        return -1;

    viperfs::Inode *inode = viperfs::viperfs().read_inode(ino);
    if (!inode)
        return -1;

    st->ino = inode->inode_num;
    st->mode = inode->mode;
    st->size = inode->size;
    st->blocks = inode->blocks;
    st->atime = inode->atime;
    st->mtime = inode->mtime;
    st->ctime = inode->ctime;

    viperfs::viperfs().release_inode(inode);
    return 0;
}

/** @copydoc fs::vfs::fstat */
i32 fstat(i32 fd, Stat *st)
{
    if (!st)
        return -1;

    FDTable *fdt = current_fdt();
    if (!fdt)
        return -1;

    FileDesc *desc = fdt->get(fd);
    if (!desc)
        return -1;

    viperfs::Inode *inode = viperfs::viperfs().read_inode(desc->inode_num);
    if (!inode)
        return -1;

    st->ino = inode->inode_num;
    st->mode = inode->mode;
    st->size = inode->size;
    st->blocks = inode->blocks;
    st->atime = inode->atime;
    st->mtime = inode->mtime;
    st->ctime = inode->ctime;

    viperfs::viperfs().release_inode(inode);
    return 0;
}

// Context for getdents callback
/**
 * @brief Context object used while building a getdents result buffer.
 *
 * @details
 * The readdir callback appends fixed-size @ref DirEnt records into the caller
 * buffer and tracks whether the buffer has overflowed.
 */
struct GetdentsCtx
{
    u8 *buf;
    usize buf_len;
    usize bytes_written;
    bool overflow;
};

// Callback for readdir
/**
 * @brief ViperFS readdir callback that appends entries into a getdents buffer.
 *
 * @param name Entry name bytes.
 * @param name_len Length of entry name.
 * @param ino Inode number.
 * @param file_type Entry file type code.
 * @param ctx Pointer to @ref GetdentsCtx.
 */
static void getdents_callback(const char *name, usize name_len, u64 ino, u8 file_type, void *ctx)
{
    auto *gctx = static_cast<GetdentsCtx *>(ctx);

    if (gctx->overflow)
        return;

    // Calculate record length (aligned to 8 bytes)
    usize reclen = sizeof(DirEnt);

    // Check if we have space
    if (gctx->bytes_written + reclen > gctx->buf_len)
    {
        gctx->overflow = true;
        return;
    }

    // Fill in the directory entry
    DirEnt *ent = reinterpret_cast<DirEnt *>(gctx->buf + gctx->bytes_written);
    ent->ino = ino;
    ent->reclen = static_cast<u16>(reclen);
    ent->type = file_type;
    ent->namelen = static_cast<u8>(name_len > 255 ? 255 : name_len);

    // Copy name
    for (usize i = 0; i < ent->namelen; i++)
    {
        ent->name[i] = name[i];
    }
    ent->name[ent->namelen] = '\0';

    gctx->bytes_written += reclen;
}

/** @copydoc fs::vfs::getdents */
i64 getdents(i32 fd, void *buf, usize len)
{
    if (!buf || len == 0)
        return -1;

    FDTable *fdt = current_fdt();
    if (!fdt)
        return -1;

    FileDesc *desc = fdt->get(fd);
    if (!desc)
        return -1;

    viperfs::Inode *inode = viperfs::viperfs().read_inode(desc->inode_num);
    if (!inode)
        return -1;

    // Check if it's a directory
    if (!viperfs::is_directory(inode))
    {
        viperfs::viperfs().release_inode(inode);
        return -1;
    }

    // Set up context
    GetdentsCtx ctx;
    ctx.buf = static_cast<u8 *>(buf);
    ctx.buf_len = len;
    ctx.bytes_written = 0;
    ctx.overflow = false;

    // Read directory entries
    viperfs::viperfs().readdir(inode, desc->offset, getdents_callback, &ctx);

    // Update file offset based on entries read
    // Each call reads all entries, so set offset to indicate EOF
    if (ctx.bytes_written > 0)
    {
        desc->offset = inode->size; // Mark as fully read
    }

    viperfs::viperfs().release_inode(inode);

    return static_cast<i64>(ctx.bytes_written);
}

/** @copydoc fs::vfs::mkdir */
i32 mkdir(const char *path)
{
    if (!path || !viperfs::viperfs().is_mounted())
        return -1;

    // Check if already exists
    u64 existing = resolve_path(path);
    if (existing != 0)
        return -1; // Already exists

    // Get parent directory and name
    u64 parent_ino;
    const char *name;
    usize name_len;
    if (!resolve_parent(path, &parent_ino, &name, &name_len))
    {
        return -1;
    }

    viperfs::Inode *parent = viperfs::viperfs().read_inode(parent_ino);
    if (!parent)
        return -1;

    u64 new_ino = viperfs::viperfs().create_dir(parent, name, name_len);
    viperfs::viperfs().release_inode(parent);

    if (new_ino == 0)
        return -1;

    viperfs::viperfs().sync();
    return 0;
}

/** @copydoc fs::vfs::rmdir */
i32 rmdir(const char *path)
{
    if (!path || !viperfs::viperfs().is_mounted())
        return -1;

    // Get parent directory and name
    u64 parent_ino;
    const char *name;
    usize name_len;
    if (!resolve_parent(path, &parent_ino, &name, &name_len))
    {
        return -1;
    }

    viperfs::Inode *parent = viperfs::viperfs().read_inode(parent_ino);
    if (!parent)
        return -1;

    bool ok = viperfs::viperfs().rmdir(parent, name, name_len);
    viperfs::viperfs().release_inode(parent);

    if (!ok)
        return -1;

    viperfs::viperfs().sync();
    return 0;
}

/** @copydoc fs::vfs::unlink */
i32 unlink(const char *path)
{
    if (!path || !viperfs::viperfs().is_mounted())
        return -1;

    // Get parent directory and name
    u64 parent_ino;
    const char *name;
    usize name_len;
    if (!resolve_parent(path, &parent_ino, &name, &name_len))
    {
        return -1;
    }

    viperfs::Inode *parent = viperfs::viperfs().read_inode(parent_ino);
    if (!parent)
        return -1;

    bool ok = viperfs::viperfs().unlink_file(parent, name, name_len);
    viperfs::viperfs().release_inode(parent);

    if (!ok)
        return -1;

    viperfs::viperfs().sync();
    return 0;
}

/** @copydoc fs::vfs::symlink */
i32 symlink(const char *target, const char *linkpath)
{
    if (!target || !linkpath || !viperfs::viperfs().is_mounted())
        return -1;

    // Get parent directory and name for the linkpath
    u64 parent_ino;
    const char *name;
    usize name_len;
    if (!resolve_parent(linkpath, &parent_ino, &name, &name_len))
    {
        return -1;
    }

    viperfs::Inode *parent = viperfs::viperfs().read_inode(parent_ino);
    if (!parent)
        return -1;

    // Calculate target length
    usize target_len = 0;
    while (target[target_len])
        target_len++;

    u64 ino = viperfs::viperfs().create_symlink(parent, name, name_len, target, target_len);
    viperfs::viperfs().release_inode(parent);

    if (ino == 0)
        return -1;

    viperfs::viperfs().sync();
    return 0;
}

/** @copydoc fs::vfs::readlink */
i64 readlink(const char *path, char *buf, usize bufsiz)
{
    if (!path || !buf || bufsiz == 0 || !viperfs::viperfs().is_mounted())
        return -1;

    u64 ino = resolve_path(path);
    if (ino == 0)
        return -1;

    viperfs::Inode *inode = viperfs::viperfs().read_inode(ino);
    if (!inode)
        return -1;

    i64 result = viperfs::viperfs().read_symlink(inode, buf, bufsiz);
    viperfs::viperfs().release_inode(inode);

    return result;
}

/** @copydoc fs::vfs::rename */
i32 rename(const char *old_path, const char *new_path)
{
    if (!old_path || !new_path || !viperfs::viperfs().is_mounted())
        return -1;

    // Get old parent directory and name
    u64 old_parent_ino;
    const char *old_name;
    usize old_name_len;
    if (!resolve_parent(old_path, &old_parent_ino, &old_name, &old_name_len))
    {
        return -1;
    }

    // Get new parent directory and name
    u64 new_parent_ino;
    const char *new_name;
    usize new_name_len;
    if (!resolve_parent(new_path, &new_parent_ino, &new_name, &new_name_len))
    {
        return -1;
    }

    viperfs::Inode *old_parent = viperfs::viperfs().read_inode(old_parent_ino);
    if (!old_parent)
        return -1;

    viperfs::Inode *new_parent = nullptr;
    if (old_parent_ino == new_parent_ino)
    {
        new_parent = old_parent; // Same directory
    }
    else
    {
        new_parent = viperfs::viperfs().read_inode(new_parent_ino);
        if (!new_parent)
        {
            viperfs::viperfs().release_inode(old_parent);
            return -1;
        }
    }

    bool ok = viperfs::viperfs().rename(
        old_parent, old_name, old_name_len, new_parent, new_name, new_name_len);

    if (old_parent != new_parent)
    {
        viperfs::viperfs().release_inode(new_parent);
    }
    viperfs::viperfs().release_inode(old_parent);

    if (!ok)
        return -1;

    viperfs::viperfs().sync();
    return 0;
}

} // namespace fs::vfs
