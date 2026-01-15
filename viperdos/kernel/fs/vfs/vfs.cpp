/**
 * @file vfs.cpp
 * @brief VFS implementation on top of ViperFS.
 *
 * @details
 * This translation unit implements the ViperDOS virtual file system (VFS) layer
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
#include "../../console/console.hpp"
#include "../../console/gcon.hpp"
#include "../../console/serial.hpp"
#include "../../lib/str.hpp"
#include "../../sched/task.hpp"
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

/**
 * @brief Check if path is a /sys path and strip the prefix.
 *
 * @details
 * In the two-disk architecture, the kernel VFS only handles /sys paths.
 * This function checks if a path starts with /sys/ and returns the stripped
 * path (with leading / preserved) for resolution against the system disk.
 *
 * @param path The input path to check.
 * @param stripped Output: pointer to the path after /sys prefix.
 * @return true if path starts with /sys/, false otherwise.
 */
static bool is_sys_path(const char *path, const char **stripped)
{
    if (!path || path[0] != '/')
        return false;

    // Check for /sys/ prefix (5 chars: /sys/)
    if (path[1] == 's' && path[2] == 'y' && path[3] == 's' && path[4] == '/')
    {
        // Strip /sys, keep the / after it -> /filename becomes /filename on disk
        *stripped = path + 4;
        return true;
    }

    // Check for /sys alone (maps to root of system disk)
    if (path[1] == 's' && path[2] == 'y' && path[3] == 's' && path[4] == '\0')
    {
        *stripped = "/";
        return true;
    }

    return false;
}

// Resolve path to inode number
/** @copydoc fs::vfs::resolve_path */
u64 resolve_path(const char *path)
{
    if (!path || !viperfs::viperfs().is_mounted())
        return 0;

    // Two-disk architecture: kernel VFS only handles /sys/* paths
    const char *effective_path = nullptr;
    if (!is_sys_path(path, &effective_path))
    {
        // Not a /sys path - userspace handles via fsd
        return 0;
    }

    // Start from root of system disk
    viperfs::Inode *current = viperfs::viperfs().read_inode(viperfs::ROOT_INODE);
    if (!current)
        return 0;

    // Use the stripped path (effective_path) for resolution
    path = effective_path;

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

// Note: resolve_parent() was removed - no longer needed since kernel VFS is read-only

/** @copydoc fs::vfs::open */
i32 open(const char *path, u32 oflags)
{
    if (!path || !viperfs::viperfs().is_mounted())
        return -1;

    FDTable *fdt = current_fdt();
    if (!fdt)
        return -1;

    // Two-disk architecture: kernel VFS (/sys) is read-only
    // Reject any write operations upfront
    if ((oflags & flags::O_WRONLY) || (oflags & flags::O_RDWR) ||
        (oflags & flags::O_CREAT) || (oflags & flags::O_TRUNC))
    {
        return -1; // Read-only filesystem
    }

    // Normalize path with CWD for relative paths
    char abs_path[MAX_PATH];
    if (path[0] == '/')
    {
        // Already absolute
        usize len = lib::strlen(path);
        if (len >= MAX_PATH)
            return -1;
        for (usize i = 0; i <= len; i++)
            abs_path[i] = path[i];
    }
    else
    {
        // Relative path - build absolute using CWD
        const char *cwd = "/";
        task::Task *t = task::current();
        if (t && t->cwd[0])
        {
            cwd = t->cwd;
        }
        if (!normalize_path(path, cwd, abs_path, sizeof(abs_path)))
        {
            return -1;
        }
    }

    // Use the absolute path for resolution
    u64 ino = resolve_path(abs_path);

    if (ino == 0)
        return -1; // File not found (or not a /sys path)

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
    {
        // Special handling for stdin - read from console
        if (fd == 0)
        {
            char *s = static_cast<char *>(buf);
            usize count = 0;
            if (len == 0)
            {
                return 0;
            }

            // Block until at least one character is available.
            while (!console::has_input())
            {
                console::poll_input();
                task::yield();
            }

            while (count < len)
            {
                console::poll_input();
                i32 c = console::getchar();
                if (c < 0)
                    break; // No more input available
                char ch = static_cast<char>(c);
                if (ch == '\r')
                    ch = '\n';
                s[count++] = ch;
                if (ch == '\n')
                    break; // Line complete
            }
            return static_cast<i64>(count);
        }
        return -1;
    }

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
    // Special handling for stdout/stderr - write to console
    // This is always allowed regardless of filesystem state
    if (fd == 1 || fd == 2)
    {
        const char *s = static_cast<const char *>(buf);
        for (usize i = 0; i < len; i++)
        {
            serial::putc(s[i]);
            if (gcon::is_available())
            {
                gcon::putc(s[i]);
            }
        }
        return static_cast<i64>(len);
    }

    // Two-disk architecture: kernel VFS (/sys) is read-only
    // All file writes are rejected - userspace uses fsd for writable storage
    return -1;
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

    u64 ino = resolve_path_cwd(path);
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

/** @copydoc fs::vfs::fsync */
i32 fsync(i32 fd)
{
    FDTable *fdt = current_fdt();
    if (!fdt)
        return -1;

    FileDesc *desc = fdt->get(fd);
    if (!desc)
        return -1;

    viperfs::Inode *inode = viperfs::viperfs().read_inode(desc->inode_num);
    if (!inode)
        return -1;

    // Use ViperFS fsync to write inode and sync dirty blocks
    bool ok = viperfs::viperfs().fsync(inode);
    viperfs::viperfs().release_inode(inode);

    return ok ? 0 : -1;
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
    // Two-disk architecture: kernel VFS (/sys) is read-only
    // Directory creation is rejected - userspace uses fsd for writable storage
    (void)path;
    return -1;
}

/** @copydoc fs::vfs::rmdir */
i32 rmdir(const char *path)
{
    // Two-disk architecture: kernel VFS (/sys) is read-only
    // Directory removal is rejected - userspace uses fsd for writable storage
    (void)path;
    return -1;
}

/** @copydoc fs::vfs::unlink */
i32 unlink(const char *path)
{
    // Two-disk architecture: kernel VFS (/sys) is read-only
    // File deletion is rejected - userspace uses fsd for writable storage
    (void)path;
    return -1;
}

/** @copydoc fs::vfs::symlink */
i32 symlink(const char *target, const char *linkpath)
{
    // Two-disk architecture: kernel VFS (/sys) is read-only
    // Symlink creation is rejected - userspace uses fsd for writable storage
    (void)target;
    (void)linkpath;
    return -1;
}

/** @copydoc fs::vfs::readlink */
i64 readlink(const char *path, char *buf, usize bufsiz)
{
    if (!path || !buf || bufsiz == 0 || !viperfs::viperfs().is_mounted())
        return -1;

    u64 ino = resolve_path_cwd(path);
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
    // Two-disk architecture: kernel VFS (/sys) is read-only
    // File renaming is rejected - userspace uses fsd for writable storage
    (void)old_path;
    (void)new_path;
    return -1;
}

/** @copydoc fs::vfs::normalize_path */
bool normalize_path(const char *path, const char *cwd, char *out, usize out_size)
{
    if (!path || !out || out_size < 2)
        return false;

    // Buffer to build the combined path
    char combined[MAX_PATH];
    usize pos = 0;

    // If path is relative, start with CWD
    if (path[0] != '/')
    {
        if (cwd && cwd[0])
        {
            // Copy CWD
            for (usize i = 0; cwd[i] && pos < MAX_PATH - 1; i++)
            {
                combined[pos++] = cwd[i];
            }
            // Ensure there's a slash between CWD and path
            if (pos > 0 && combined[pos - 1] != '/' && pos < MAX_PATH - 1)
            {
                combined[pos++] = '/';
            }
        }
        else
        {
            // No CWD, treat as root
            combined[pos++] = '/';
        }
    }

    // Append the input path
    for (usize i = 0; path[i] && pos < MAX_PATH - 1; i++)
    {
        combined[pos++] = path[i];
    }
    combined[pos] = '\0';

    // Now normalize the combined path
    // We process components and build the result
    char *src = combined;
    usize out_pos = 0;

    // Start with root slash
    if (out_size > 0)
    {
        out[out_pos++] = '/';
    }

    // Track component start positions for handling ".."
    usize component_starts[64]; // Stack of component start positions
    usize stack_depth = 0;

    while (*src)
    {
        // Skip leading slashes
        while (*src == '/')
            src++;
        if (*src == '\0')
            break;

        // Find end of component
        const char *comp_start = src;
        while (*src && *src != '/')
            src++;
        usize comp_len = src - comp_start;

        // Handle "." - skip it
        if (comp_len == 1 && comp_start[0] == '.')
        {
            continue;
        }

        // Handle ".." - go up one directory
        if (comp_len == 2 && comp_start[0] == '.' && comp_start[1] == '.')
        {
            if (stack_depth > 0)
            {
                // Pop the last component
                stack_depth--;
                out_pos = component_starts[stack_depth];
            }
            // If at root, stay at root
            continue;
        }

        // Regular component - add it
        if (out_pos + comp_len + 1 >= out_size)
        {
            return false; // Buffer too small
        }

        // Record where this component starts (after the slash)
        if (stack_depth < 64)
        {
            component_starts[stack_depth++] = out_pos;
        }

        // Copy component
        for (usize i = 0; i < comp_len; i++)
        {
            out[out_pos++] = comp_start[i];
        }
        out[out_pos++] = '/';
    }

    // Remove trailing slash (unless root)
    if (out_pos > 1 && out[out_pos - 1] == '/')
    {
        out_pos--;
    }

    out[out_pos] = '\0';
    return true;
}

/** @copydoc fs::vfs::resolve_path_cwd */
u64 resolve_path_cwd(const char *path)
{
    if (!path)
        return 0;

    // Two-disk architecture: kernel VFS only handles absolute /sys/* paths
    // Relative paths cannot access kernel filesystem - userspace handles via fsd
    if (path[0] != '/')
    {
        return 0;
    }

    // Delegate to resolve_path which handles /sys prefix check
    return resolve_path(path);
}

} // namespace fs::vfs
