//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
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
#include "../fat32/fat32.hpp"
#include "../viperfs/viperfs.hpp"

namespace fs::vfs {

// Global FD table for kernel-mode operations and backward compatibility
static FDTable g_kernel_fdt;

// User disk filesystem type tracking
static FsType g_user_fs_type = FsType::VIPERFS;
static bool g_user_fat32_available = false;

/** @copydoc fs::vfs::init */
void init() {
    g_kernel_fdt.init();
    serial::puts("[vfs] VFS initialized\n");
}

/** @copydoc fs::vfs::kernel_fdt */
FDTable *kernel_fdt() {
    return &g_kernel_fdt;
}

/** @copydoc fs::vfs::current_fdt */
FDTable *current_fdt() {
    // Get current process's FD table if available
    viper::Viper *v = viper::current();
    if (v && v->fd_table) {
        return v->fd_table;
    }

    // Fall back to kernel FD table for compatibility
    return &g_kernel_fdt;
}

/** @copydoc fs::vfs::close_all_fds */
void close_all_fds(FDTable *fdt) {
    if (!fdt)
        return;

    for (usize i = 0; i < MAX_FDS; i++) {
        if (fdt->fds[i].in_use) {
            fdt->free(static_cast<i32>(i));
        }
    }
}

// Use lib::strlen for string length operations

/**
 * @brief Check if path is a /sys path and strip the prefix.
 *
 * @details
 * In the two-disk architecture, /sys paths map to the system disk.
 *
 * @param path The input path to check.
 * @param stripped Output: pointer to the path after /sys prefix.
 * @return true if path starts with /sys/, false otherwise.
 */
static bool is_sys_path(const char *path, const char **stripped) {
    if (!path || path[0] != '/')
        return false;

    // Check for /sys/ prefix (5 chars: /sys/)
    if (path[1] == 's' && path[2] == 'y' && path[3] == 's' && path[4] == '/') {
        // Strip /sys, keep the / after it -> /filename becomes /filename on disk
        *stripped = path + 4;
        return true;
    }

    // Check for /sys alone (maps to root of system disk)
    if (path[1] == 's' && path[2] == 'y' && path[3] == 's' && path[4] == '\0') {
        *stripped = "/";
        return true;
    }

    return false;
}

/**
 * @brief Check if path is a user disk path and strip the prefix.
 *
 * @details
 * User paths include: /c/, /certs/, /s/, /t/
 * These map to the root of the user disk.
 *
 * @param path The input path to check.
 * @param stripped Output: pointer to the effective path on user disk.
 * @return true if path is a user disk path, false otherwise.
 */
static bool is_user_path(const char *path, const char **stripped) {
    if (!path || path[0] != '/')
        return false;

    // All non-/sys paths go to user disk (user disk root = /)
    // The user disk contains: /c, /certs, /s, /t directories
    const char *effective_path = nullptr;
    if (!is_sys_path(path, &effective_path)) {
        *stripped = path; // Use path as-is on user disk
        return true;
    }

    return false;
}

// Resolve path to inode number
/** @copydoc fs::vfs::resolve_path */
u64 resolve_path(const char *path) {
    if (!path)
        return 0;

    // Determine which filesystem to use
    const char *effective_path = nullptr;
    ::fs::viperfs::ViperFS *fs = nullptr;

    if (is_sys_path(path, &effective_path)) {
        // /sys path -> system disk
        if (!::fs::viperfs::viperfs().is_mounted())
            return 0;
        fs = &::fs::viperfs::viperfs();
    } else if (is_user_path(path, &effective_path)) {
        // User path -> user disk
        if (!::fs::viperfs::user_viperfs_available())
            return 0;
        fs = &::fs::viperfs::user_viperfs();
    } else {
        return 0;
    }

    // Start from root of the appropriate disk
    ::fs::viperfs::Inode *current = fs->read_inode(::fs::viperfs::ROOT_INODE);
    if (!current)
        return 0;

    // Use the stripped path (effective_path) for resolution
    path = effective_path;

    // Skip leading slashes
    while (*path == '/')
        path++;

    // Empty path means root
    if (*path == '\0') {
        u64 ino = current->inode_num;
        fs->release_inode(current);
        return ino;
    }

    // Parse path components
    while (*path) {
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
        if (!::fs::viperfs::is_directory(current)) {
            fs->release_inode(current);
            return 0; // Not a directory
        }

        u64 next_ino = fs->lookup(current, start, len);
        fs->release_inode(current);

        if (next_ino == 0)
            return 0; // Not found

        current = fs->read_inode(next_ino);
        if (!current)
            return 0;
    }

    u64 result = current->inode_num;
    fs->release_inode(current);
    return result;
}

// =============================================================================
// VFS Open Helpers
// =============================================================================

/**
 * @brief Get absolute path from relative or absolute input.
 */
static bool get_absolute_path(const char *path, char *abs_path, usize abs_size) {
    if (path[0] == '/') {
        usize len = lib::strlen(path);
        if (len >= abs_size)
            return false;
        for (usize i = 0; i <= len; i++)
            abs_path[i] = path[i];
        return true;
    }

    const char *cwd = "/";
    task::Task *t = task::current();
    if (t && t->cwd[0])
        cwd = t->cwd;

    return normalize_path(path, cwd, abs_path, abs_size);
}

/**
 * @brief Filesystem selection result.
 */
struct FsSelection {
    FsType fs_type;
    ::fs::viperfs::ViperFS *viperfs; // non-null for VIPERFS
    ::fs::fat32::FAT32 *fat32;       // non-null for FAT32
    bool writable;
    bool valid;
};

/**
 * @brief Select filesystem based on path prefix.
 */
static FsSelection select_filesystem(const char *abs_path) {
    const char *effective_path = nullptr;

    if (is_sys_path(abs_path, &effective_path)) {
        if (::fs::viperfs::viperfs().is_mounted())
            return {FsType::VIPERFS, &::fs::viperfs::viperfs(), nullptr, false, true};
    } else if (is_user_path(abs_path, &effective_path)) {
        if (g_user_fat32_available && g_user_fs_type == FsType::FAT32) {
            return {FsType::FAT32, nullptr, &::fs::fat32::fat32(), true, true};
        }
        if (::fs::viperfs::user_viperfs_available())
            return {FsType::VIPERFS, &::fs::viperfs::user_viperfs(), nullptr, true, true};
    }
    return {FsType::VIPERFS, nullptr, nullptr, false, false};
}

/// Notify VFS that user disk uses FAT32.
void set_user_fs_fat32() {
    g_user_fs_type = FsType::FAT32;
    g_user_fat32_available = true;
}

/// Check if user disk is FAT32.
bool user_fs_is_fat32() {
    return g_user_fat32_available && g_user_fs_type == FsType::FAT32;
}

/**
 * @brief Helper: get ViperFS pointer from file descriptor.
 */
static ::fs::viperfs::ViperFS *fd_viperfs(FileDesc *desc) {
    if (desc->fs_type == FsType::VIPERFS)
        return desc->fs.viperfs ? desc->fs.viperfs : &::fs::viperfs::viperfs();
    return nullptr;
}

/**
 * @brief Split path into parent directory and filename.
 */
static void split_path(const char *path, char *parent, char *filename) {
    usize path_len = lib::strlen(path);
    usize last_slash = 0;
    for (usize i = 0; i < path_len; i++) {
        if (path[i] == '/')
            last_slash = i;
    }

    if (last_slash == 0) {
        parent[0] = '/';
        parent[1] = '\0';
    } else {
        for (usize i = 0; i < last_slash; i++)
            parent[i] = path[i];
        parent[last_slash] = '\0';
    }

    usize fn_len = path_len - last_slash - 1;
    for (usize i = 0; i <= fn_len; i++)
        filename[i] = path[last_slash + 1 + i];
}

/**
 * @brief Create file if O_CREAT and file doesn't exist.
 */
static u64 create_file_if_needed(::fs::viperfs::ViperFS *fs, const char *abs_path) {
    char parent_path[MAX_PATH];
    char filename[256];
    split_path(abs_path, parent_path, filename);

    u64 parent_ino = resolve_path(parent_path);
    if (parent_ino == 0)
        return 0;

    ::fs::viperfs::Inode *parent = fs->read_inode(parent_ino);
    if (!parent)
        return 0;

    u64 ino = fs->create_file(parent, filename, lib::strlen(filename));
    fs->release_inode(parent);
    return ino;
}

// =============================================================================
// VFS Open
// =============================================================================

/** @copydoc fs::vfs::open */
i32 open(const char *path, u32 oflags) {
    if (!path)
        return -1;

    FDTable *fdt = current_fdt();
    if (!fdt)
        return -1;

    char abs_path[MAX_PATH];
    if (!get_absolute_path(path, abs_path, sizeof(abs_path)))
        return -1;

    FsSelection sel = select_filesystem(abs_path);
    if (!sel.valid)
        return -1;

    bool wants_write = (oflags & flags::O_WRONLY) || (oflags & flags::O_RDWR) ||
                       (oflags & flags::O_CREAT) || (oflags & flags::O_TRUNC);
    if (wants_write && !sel.writable)
        return -1;

    // FAT32 handles its own open/create below (after FD alloc)
    u64 ino = 0;
    if (sel.fs_type == FsType::VIPERFS) {
        ino = resolve_path(abs_path);
        if (ino == 0 && (oflags & flags::O_CREAT)) {
            ino = create_file_if_needed(sel.viperfs, abs_path);
        }
        if (ino == 0)
            return -1;
    }

    i32 fd = fdt->alloc();
    if (fd < 0)
        return -1;

    FileDesc *desc = fdt->get(fd);
    desc->inode_num = ino;
    desc->offset = 0;
    desc->flags = oflags;
    desc->fs_type = sel.fs_type;
    if (sel.fs_type == FsType::FAT32) {
        desc->fs.fat32 = sel.fat32;
    } else {
        desc->fs.viperfs = sel.viperfs;
    }

    if (sel.fs_type == FsType::FAT32) {
        // FAT32: open and cache file info
        ::fs::fat32::FileInfo fi{};
        const char *effective = nullptr;
        if (is_user_path(abs_path, &effective)) {
            if (sel.fat32->open(effective, &fi)) {
                desc->inode_num = fi.first_cluster;
                desc->fat32_size = fi.size;
                desc->fat32_attr = fi.attr;
                desc->fat32_is_dir = fi.is_directory;
                if (oflags & flags::O_APPEND) {
                    desc->offset = fi.size;
                }
            } else if (oflags & flags::O_CREAT) {
                if (sel.fat32->create_file(effective, &fi)) {
                    desc->inode_num = fi.first_cluster;
                    desc->fat32_size = fi.size;
                    desc->fat32_attr = fi.attr;
                    desc->fat32_is_dir = fi.is_directory;
                } else {
                    fdt->free(fd);
                    return -1;
                }
            } else {
                fdt->free(fd);
                return -1;
            }
        } else {
            fdt->free(fd);
            return -1;
        }
    } else {
        if (oflags & flags::O_APPEND) {
            ::fs::viperfs::Inode *inode = sel.viperfs->read_inode(ino);
            if (inode) {
                desc->offset = inode->size;
                sel.viperfs->release_inode(inode);
            }
        }
    }

    return fd;
}

/** @copydoc fs::vfs::dup */
i32 dup(i32 oldfd) {
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
    new_desc->fs_type = old_desc->fs_type;
    new_desc->fs = old_desc->fs;
    new_desc->fat32_size = old_desc->fat32_size;
    new_desc->fat32_attr = old_desc->fat32_attr;
    new_desc->fat32_is_dir = old_desc->fat32_is_dir;

    return newfd;
}

/** @copydoc fs::vfs::dup2 */
i32 dup2(i32 oldfd, i32 newfd) {
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
    if (fdt->fds[newfd].in_use) {
        fdt->free(newfd);
    }

    // Mark newfd as in use and copy
    fdt->fds[newfd].in_use = true;
    fdt->fds[newfd].inode_num = old_desc->inode_num;
    fdt->fds[newfd].offset = old_desc->offset;
    fdt->fds[newfd].flags = old_desc->flags;
    fdt->fds[newfd].fs_type = old_desc->fs_type;
    fdt->fds[newfd].fs = old_desc->fs;
    fdt->fds[newfd].fat32_size = old_desc->fat32_size;
    fdt->fds[newfd].fat32_attr = old_desc->fat32_attr;
    fdt->fds[newfd].fat32_is_dir = old_desc->fat32_is_dir;

    return newfd;
}

/** @copydoc fs::vfs::close */
i32 close(i32 fd) {
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
i64 read(i32 fd, void *buf, usize len) {
    FDTable *fdt = current_fdt();
    if (!fdt)
        return -1;

    FileDesc *desc = fdt->get(fd);
    if (!desc) {
        // Special handling for stdin - read from console
        if (fd == 0) {
            char *s = static_cast<char *>(buf);
            usize count = 0;
            if (len == 0) {
                return 0;
            }

            // Block until at least one character is available.
            while (!console::has_input()) {
                console::poll_input();
                task::yield();
            }

            while (count < len) {
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

    if (desc->fs_type == FsType::FAT32) {
        // FAT32 read
        ::fs::fat32::FAT32 *fat = desc->fs.fat32;
        if (!fat)
            return -1;
        ::fs::fat32::FileInfo fi{};
        fi.first_cluster = static_cast<u32>(desc->inode_num);
        fi.size = desc->fat32_size;
        fi.attr = desc->fat32_attr;
        fi.is_directory = desc->fat32_is_dir;
        i64 bytes = fat->read(&fi, desc->offset, buf, len);
        if (bytes > 0)
            desc->offset += bytes;
        return bytes;
    }

    // ViperFS read
    ::fs::viperfs::ViperFS *fs = fd_viperfs(desc);
    if (!fs)
        return -1;

    ::fs::viperfs::Inode *inode = fs->read_inode(desc->inode_num);
    if (!inode)
        return -1;

    i64 bytes = fs->read_data(inode, desc->offset, buf, len);
    if (bytes > 0) {
        desc->offset += bytes;
    }

    fs->release_inode(inode);
    return bytes;
}

/** @copydoc fs::vfs::write */
i64 write(i32 fd, const void *buf, usize len) {
    // Special handling for stdout/stderr - write to console
    // This is always allowed regardless of filesystem state
    if (fd == 1 || fd == 2) {
        const char *s = static_cast<const char *>(buf);
        for (usize i = 0; i < len; i++) {
            serial::putc(s[i]);
            if (gcon::is_available()) {
                gcon::putc(s[i]);
            }
        }
        return static_cast<i64>(len);
    }

    FDTable *fdt = current_fdt();
    if (!fdt)
        return -1;

    FileDesc *desc = fdt->get(fd);
    if (!desc)
        return -1;

    // Check if write is permitted (system disk is read-only)
    if (desc->fs_type == FsType::VIPERFS) {
        if (!desc->fs.viperfs || desc->fs.viperfs == &::fs::viperfs::viperfs()) {
            return -1; // System disk is read-only
        }
    }

    // Check flags
    if (!(desc->flags & flags::O_WRONLY) && !(desc->flags & flags::O_RDWR))
        return -1;

    if (desc->fs_type == FsType::FAT32) {
        // FAT32 write
        ::fs::fat32::FAT32 *fat = desc->fs.fat32;
        if (!fat)
            return -1;
        ::fs::fat32::FileInfo fi{};
        fi.first_cluster = static_cast<u32>(desc->inode_num);
        fi.size = desc->fat32_size;
        fi.attr = desc->fat32_attr;
        fi.is_directory = desc->fat32_is_dir;
        i64 written = fat->write(&fi, desc->offset, buf, len);
        if (written > 0) {
            desc->offset += static_cast<u64>(written);
            // Update cached size (FAT32::write may extend the file)
            desc->fat32_size = fi.size;
            desc->inode_num = fi.first_cluster; // May change if file was empty
        }
        return written;
    }

    // ViperFS write
    ::fs::viperfs::ViperFS *fs = fd_viperfs(desc);
    if (!fs)
        return -1;

    ::fs::viperfs::Inode *inode = fs->read_inode(desc->inode_num);
    if (!inode)
        return -1;

    i64 written = fs->write_data(inode, desc->offset, buf, len);
    if (written > 0) {
        desc->offset += static_cast<u64>(written);
    }

    // Write inode to persist size changes
    fs->write_inode(inode);
    fs->release_inode(inode);

    return written;
}

/** @copydoc fs::vfs::lseek */
i64 lseek(i32 fd, i64 offset, i32 whence) {
    FDTable *fdt = current_fdt();
    if (!fdt)
        return -1;

    FileDesc *desc = fdt->get(fd);
    if (!desc)
        return -1;

    i64 new_offset;

    switch (whence) {
        case seek::SET:
            new_offset = offset;
            break;
        case seek::CUR:
            new_offset = static_cast<i64>(desc->offset) + offset;
            break;
        case seek::END: {
            if (desc->fs_type == FsType::FAT32) {
                new_offset = static_cast<i64>(desc->fat32_size) + offset;
            } else {
                ::fs::viperfs::ViperFS *fs = fd_viperfs(desc);
                if (!fs)
                    return -1;
                ::fs::viperfs::Inode *inode = fs->read_inode(desc->inode_num);
                if (!inode)
                    return -1;
                new_offset = static_cast<i64>(inode->size) + offset;
                fs->release_inode(inode);
            }
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

/**
 * @brief Get the appropriate filesystem for a path.
 */
static ::fs::viperfs::ViperFS *get_fs_for_path(const char *path) {
    const char *effective_path = nullptr;
    if (is_sys_path(path, &effective_path)) {
        if (::fs::viperfs::viperfs().is_mounted())
            return &::fs::viperfs::viperfs();
    } else if (is_user_path(path, &effective_path)) {
        if (::fs::viperfs::user_viperfs_available())
            return &::fs::viperfs::user_viperfs();
    }
    return nullptr;
}

/** @copydoc fs::vfs::stat */
i32 stat(const char *path, Stat *st) {
    if (!path || !st)
        return -1;

    // Check if this is a FAT32 user path
    const char *effective = nullptr;
    if (g_user_fat32_available && !is_sys_path(path, &effective) && is_user_path(path, &effective)) {
        ::fs::fat32::FileInfo fi{};
        if (!::fs::fat32::fat32().open(effective, &fi))
            return -1;
        st->ino = fi.first_cluster;
        st->mode = fi.is_directory ? 0040755 : 0100644;
        if (fi.attr & ::fs::fat32::attr::READ_ONLY)
            st->mode &= ~0222;
        st->size = fi.size;
        st->blocks = (fi.size + 511) / 512;
        st->atime = fi.atime;
        st->mtime = fi.mtime;
        st->ctime = fi.ctime;
        return 0;
    }

    u64 ino = resolve_path_cwd(path);
    if (ino == 0)
        return -1;

    // Determine which filesystem based on path
    ::fs::viperfs::ViperFS *fs = get_fs_for_path(path);
    if (!fs)
        return -1;

    ::fs::viperfs::Inode *inode = fs->read_inode(ino);
    if (!inode)
        return -1;

    st->ino = inode->inode_num;
    st->mode = inode->mode;
    st->size = inode->size;
    st->blocks = inode->blocks;
    st->atime = inode->atime;
    st->mtime = inode->mtime;
    st->ctime = inode->ctime;

    fs->release_inode(inode);
    return 0;
}

/** @copydoc fs::vfs::fstat */
i32 fstat(i32 fd, Stat *st) {
    if (!st)
        return -1;

    FDTable *fdt = current_fdt();
    if (!fdt)
        return -1;

    FileDesc *desc = fdt->get(fd);
    if (!desc)
        return -1;

    if (desc->fs_type == FsType::FAT32) {
        // FAT32 fstat: return cached info
        st->ino = desc->inode_num; // first cluster
        st->mode = desc->fat32_is_dir ? 0040755u : 0100644u;
        if (desc->fat32_attr & ::fs::fat32::attr::READ_ONLY)
            st->mode &= ~0222u;
        st->size = desc->fat32_size;
        st->blocks = (desc->fat32_size + 511) / 512;
        st->atime = 0;
        st->mtime = 0;
        st->ctime = 0;
        return 0;
    }

    ::fs::viperfs::ViperFS *fs = fd_viperfs(desc);
    if (!fs)
        return -1;

    ::fs::viperfs::Inode *inode = fs->read_inode(desc->inode_num);
    if (!inode)
        return -1;

    st->ino = inode->inode_num;
    st->mode = inode->mode;
    st->size = inode->size;
    st->blocks = inode->blocks;
    st->atime = inode->atime;
    st->mtime = inode->mtime;
    st->ctime = inode->ctime;

    fs->release_inode(inode);
    return 0;
}

/** @copydoc fs::vfs::fsync */
i32 fsync(i32 fd) {
    FDTable *fdt = current_fdt();
    if (!fdt)
        return -1;

    FileDesc *desc = fdt->get(fd);
    if (!desc)
        return -1;

    if (desc->fs_type == FsType::FAT32) {
        ::fs::fat32::FAT32 *fat = desc->fs.fat32;
        if (fat) fat->sync();
        return 0;
    }

    ::fs::viperfs::ViperFS *fs = fd_viperfs(desc);
    if (!fs)
        return -1;

    ::fs::viperfs::Inode *inode = fs->read_inode(desc->inode_num);
    if (!inode)
        return -1;

    // Use ViperFS fsync to write inode and sync dirty blocks
    bool ok = fs->fsync(inode);
    fs->release_inode(inode);

    return ok ? 0 : -1;
}

// Context for getdents callback
/**
 * @brief Context object used while building a getdents result buffer.
 *
 * @details
 * The readdir callback appends fixed-size @ref DirEnt records into the caller
 * buffer and tracks whether the buffer has overflowed. Uses entry-count-based
 * offset tracking to support reading directories larger than one buffer.
 */
struct GetdentsCtx {
    u8 *buf;
    usize buf_len;
    usize bytes_written;
    usize entries_to_skip; ///< Entries to skip (from previous reads)
    usize entries_seen;    ///< Total entries seen during this scan
    usize entries_written; ///< Entries successfully written to buffer
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
static void getdents_callback(const char *name, usize name_len, u64 ino, u8 file_type, void *ctx) {
    auto *gctx = static_cast<GetdentsCtx *>(ctx);

    // Track all entries seen
    gctx->entries_seen++;

    // Skip entries from previous reads
    if (gctx->entries_seen <= gctx->entries_to_skip)
        return;

    if (gctx->overflow)
        return;

    // Calculate record length (aligned to 8 bytes)
    usize reclen = sizeof(DirEnt);

    // Check if we have space
    if (gctx->bytes_written + reclen > gctx->buf_len) {
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
    for (usize i = 0; i < ent->namelen; i++) {
        ent->name[i] = name[i];
    }
    ent->name[ent->namelen] = '\0';

    gctx->bytes_written += reclen;
    gctx->entries_written++;
}

/** @copydoc fs::vfs::getdents */
i64 getdents(i32 fd, void *buf, usize len) {
    if (!buf || len == 0)
        return -1;

    FDTable *fdt = current_fdt();
    if (!fdt)
        return -1;

    FileDesc *desc = fdt->get(fd);
    if (!desc)
        return -1;

    if (desc->fs_type == FsType::FAT32) {
        // FAT32 directory listing
        if (!desc->fat32_is_dir)
            return -1;

        ::fs::fat32::FAT32 *fat = desc->fs.fat32;
        if (!fat)
            return -1;

        // Read FAT32 directory entries
        constexpr i32 MAX_FAT_ENTRIES = 128;
        static ::fs::fat32::FileInfo fat_entries[MAX_FAT_ENTRIES];
        u32 dir_cluster = static_cast<u32>(desc->inode_num);
        i32 count = fat->read_dir(dir_cluster, fat_entries, MAX_FAT_ENTRIES);
        if (count < 0)
            return -1;

        // Pack into DirEnt format, skipping already-read entries
        u8 *out = static_cast<u8 *>(buf);
        usize bytes_written = 0;
        usize entries_to_skip = desc->offset;
        usize entries_written = 0;

        for (i32 i = 0; i < count; i++) {
            if (static_cast<usize>(i) < entries_to_skip)
                continue;

            usize reclen = sizeof(DirEnt);
            if (bytes_written + reclen > len)
                break;

            DirEnt *ent = reinterpret_cast<DirEnt *>(out + bytes_written);
            ent->ino = fat_entries[i].first_cluster;
            ent->reclen = static_cast<u16>(reclen);
            ent->type = fat_entries[i].is_directory ? 2 : 1;
            usize name_len = lib::strlen(fat_entries[i].name);
            ent->namelen = static_cast<u8>(name_len > 255 ? 255 : name_len);
            for (usize j = 0; j < ent->namelen; j++)
                ent->name[j] = fat_entries[i].name[j];
            ent->name[ent->namelen] = '\0';

            bytes_written += reclen;
            entries_written++;
        }

        desc->offset += entries_written;
        return static_cast<i64>(bytes_written);
    }

    // ViperFS getdents
    ::fs::viperfs::ViperFS *fs = fd_viperfs(desc);
    if (!fs)
        return -1;

    ::fs::viperfs::Inode *inode = fs->read_inode(desc->inode_num);
    if (!inode)
        return -1;

    // Check if it's a directory
    if (!::fs::viperfs::is_directory(inode)) {
        fs->release_inode(inode);
        return -1;
    }

    // Set up context - use entry-count-based offset for multi-buffer reads
    GetdentsCtx ctx;
    ctx.buf = static_cast<u8 *>(buf);
    ctx.buf_len = len;
    ctx.bytes_written = 0;
    ctx.entries_to_skip = desc->offset; // offset tracks entry count, not bytes
    ctx.entries_seen = 0;
    ctx.entries_written = 0;
    ctx.overflow = false;

    // Read directory entries from the beginning, skipping already-read entries
    fs->readdir(inode, 0, getdents_callback, &ctx);

    // Update offset by number of entries successfully written
    desc->offset += ctx.entries_written;

    fs->release_inode(inode);

    return static_cast<i64>(ctx.bytes_written);
}

/** @copydoc fs::vfs::mkdir */
i32 mkdir(const char *path) {
    if (!path)
        return -1;

    // Normalize path
    char abs_path[MAX_PATH];
    if (path[0] == '/') {
        usize len = lib::strlen(path);
        if (len >= MAX_PATH)
            return -1;
        for (usize i = 0; i <= len; i++)
            abs_path[i] = path[i];
    } else {
        const char *cwd = "/";
        task::Task *t = task::current();
        if (t && t->cwd[0])
            cwd = t->cwd;
        if (!normalize_path(path, cwd, abs_path, sizeof(abs_path)))
            return -1;
    }

    // Check if path is on user disk (writable)
    const char *effective_path = nullptr;
    if (is_sys_path(abs_path, &effective_path))
        return -1; // System disk is read-only

    if (!is_user_path(abs_path, &effective_path))
        return -1;

    // FAT32 mkdir
    if (g_user_fat32_available) {
        return ::fs::fat32::fat32().create_dir(effective_path) ? 0 : -1;
    }

    if (!::fs::viperfs::user_viperfs_available())
        return -1;

    auto *fs = &::fs::viperfs::user_viperfs();

    // Find parent directory and new directory name
    char parent_path[MAX_PATH];
    char dirname[256];
    usize path_len = lib::strlen(abs_path);
    usize last_slash = 0;
    for (usize i = 0; i < path_len; i++) {
        if (abs_path[i] == '/')
            last_slash = i;
    }

    if (last_slash == 0) {
        parent_path[0] = '/';
        parent_path[1] = '\0';
    } else {
        for (usize i = 0; i < last_slash; i++)
            parent_path[i] = abs_path[i];
        parent_path[last_slash] = '\0';
    }

    usize dn_len = path_len - last_slash - 1;
    for (usize i = 0; i <= dn_len; i++)
        dirname[i] = abs_path[last_slash + 1 + i];

    // Resolve parent directory
    u64 parent_ino = resolve_path(parent_path);
    if (parent_ino == 0)
        return -1;

    ::fs::viperfs::Inode *parent = fs->read_inode(parent_ino);
    if (!parent)
        return -1;

    u64 new_ino = fs->create_dir(parent, dirname, dn_len);
    fs->release_inode(parent);

    return (new_ino != 0) ? 0 : -1;
}

/** @copydoc fs::vfs::rmdir */
i32 rmdir(const char *path) {
    if (!path)
        return -1;

    // Normalize path
    char abs_path[MAX_PATH];
    if (path[0] == '/') {
        usize len = lib::strlen(path);
        if (len >= MAX_PATH)
            return -1;
        for (usize i = 0; i <= len; i++)
            abs_path[i] = path[i];
    } else {
        const char *cwd = "/";
        task::Task *t = task::current();
        if (t && t->cwd[0])
            cwd = t->cwd;
        if (!normalize_path(path, cwd, abs_path, sizeof(abs_path)))
            return -1;
    }

    // Check if path is on user disk
    const char *effective_path = nullptr;
    if (is_sys_path(abs_path, &effective_path))
        return -1; // System disk is read-only

    if (!is_user_path(abs_path, &effective_path))
        return -1;

    // FAT32 rmdir
    if (g_user_fat32_available) {
        return ::fs::fat32::fat32().remove(effective_path) ? 0 : -1;
    }

    if (!::fs::viperfs::user_viperfs_available())
        return -1;

    auto *fs = &::fs::viperfs::user_viperfs();

    // Find parent directory and directory name
    char parent_path[MAX_PATH];
    char dirname[256];
    usize path_len = lib::strlen(abs_path);
    usize last_slash = 0;
    for (usize i = 0; i < path_len; i++) {
        if (abs_path[i] == '/')
            last_slash = i;
    }

    if (last_slash == 0) {
        parent_path[0] = '/';
        parent_path[1] = '\0';
    } else {
        for (usize i = 0; i < last_slash; i++)
            parent_path[i] = abs_path[i];
        parent_path[last_slash] = '\0';
    }

    usize dn_len = path_len - last_slash - 1;
    for (usize i = 0; i <= dn_len; i++)
        dirname[i] = abs_path[last_slash + 1 + i];

    u64 parent_ino = resolve_path(parent_path);
    if (parent_ino == 0)
        return -1;

    ::fs::viperfs::Inode *parent = fs->read_inode(parent_ino);
    if (!parent)
        return -1;

    bool ok = fs->rmdir(parent, dirname, dn_len);
    fs->release_inode(parent);

    return ok ? 0 : -1;
}

/** @copydoc fs::vfs::unlink */
i32 unlink(const char *path) {
    if (!path)
        return -1;

    // Normalize path
    char abs_path[MAX_PATH];
    if (path[0] == '/') {
        usize len = lib::strlen(path);
        if (len >= MAX_PATH)
            return -1;
        for (usize i = 0; i <= len; i++)
            abs_path[i] = path[i];
    } else {
        const char *cwd = "/";
        task::Task *t = task::current();
        if (t && t->cwd[0])
            cwd = t->cwd;
        if (!normalize_path(path, cwd, abs_path, sizeof(abs_path)))
            return -1;
    }

    // Check if path is on user disk
    const char *effective_path = nullptr;
    if (is_sys_path(abs_path, &effective_path))
        return -1; // System disk is read-only

    if (!is_user_path(abs_path, &effective_path))
        return -1;

    // FAT32 unlink
    if (g_user_fat32_available) {
        return ::fs::fat32::fat32().remove(effective_path) ? 0 : -1;
    }

    if (!::fs::viperfs::user_viperfs_available())
        return -1;

    auto *fs = &::fs::viperfs::user_viperfs();

    // Find parent directory and filename
    char parent_path[MAX_PATH];
    char filename[256];
    usize path_len = lib::strlen(abs_path);
    usize last_slash = 0;
    for (usize i = 0; i < path_len; i++) {
        if (abs_path[i] == '/')
            last_slash = i;
    }

    if (last_slash == 0) {
        parent_path[0] = '/';
        parent_path[1] = '\0';
    } else {
        for (usize i = 0; i < last_slash; i++)
            parent_path[i] = abs_path[i];
        parent_path[last_slash] = '\0';
    }

    usize fn_len = path_len - last_slash - 1;
    for (usize i = 0; i <= fn_len; i++)
        filename[i] = abs_path[last_slash + 1 + i];

    u64 parent_ino = resolve_path(parent_path);
    if (parent_ino == 0)
        return -1;

    ::fs::viperfs::Inode *parent = fs->read_inode(parent_ino);
    if (!parent)
        return -1;

    bool ok = fs->unlink_file(parent, filename, fn_len);
    fs->release_inode(parent);

    return ok ? 0 : -1;
}

/** @copydoc fs::vfs::symlink */
i32 symlink(const char *target, const char *linkpath) {
    if (!target || !linkpath)
        return -1;

    // Normalize linkpath
    char abs_path[MAX_PATH];
    if (linkpath[0] == '/') {
        usize len = lib::strlen(linkpath);
        if (len >= MAX_PATH)
            return -1;
        for (usize i = 0; i <= len; i++)
            abs_path[i] = linkpath[i];
    } else {
        const char *cwd = "/";
        task::Task *t = task::current();
        if (t && t->cwd[0])
            cwd = t->cwd;
        if (!normalize_path(linkpath, cwd, abs_path, sizeof(abs_path)))
            return -1;
    }

    // Check if linkpath is on user disk
    const char *effective_path = nullptr;
    if (is_sys_path(abs_path, &effective_path))
        return -1; // System disk is read-only

    if (!is_user_path(abs_path, &effective_path))
        return -1;

    if (!::fs::viperfs::user_viperfs_available())
        return -1;

    auto *fs = &::fs::viperfs::user_viperfs();

    // Find parent directory and link name
    char parent_path[MAX_PATH];
    char linkname[256];
    usize path_len = lib::strlen(abs_path);
    usize last_slash = 0;
    for (usize i = 0; i < path_len; i++) {
        if (abs_path[i] == '/')
            last_slash = i;
    }

    if (last_slash == 0) {
        parent_path[0] = '/';
        parent_path[1] = '\0';
    } else {
        for (usize i = 0; i < last_slash; i++)
            parent_path[i] = abs_path[i];
        parent_path[last_slash] = '\0';
    }

    usize ln_len = path_len - last_slash - 1;
    for (usize i = 0; i <= ln_len; i++)
        linkname[i] = abs_path[last_slash + 1 + i];

    u64 parent_ino = resolve_path(parent_path);
    if (parent_ino == 0)
        return -1;

    ::fs::viperfs::Inode *parent = fs->read_inode(parent_ino);
    if (!parent)
        return -1;

    u64 link_ino = fs->create_symlink(parent, linkname, ln_len, target, lib::strlen(target));
    fs->release_inode(parent);

    return (link_ino != 0) ? 0 : -1;
}

/** @copydoc fs::vfs::readlink */
i64 readlink(const char *path, char *buf, usize bufsiz) {
    if (!path || !buf || bufsiz == 0)
        return -1;

    ::fs::viperfs::ViperFS *fs = get_fs_for_path(path);
    if (!fs)
        return -1;

    u64 ino = resolve_path_cwd(path);
    if (ino == 0)
        return -1;

    ::fs::viperfs::Inode *inode = fs->read_inode(ino);
    if (!inode)
        return -1;

    i64 result = fs->read_symlink(inode, buf, bufsiz);
    fs->release_inode(inode);

    return result;
}

/** @copydoc fs::vfs::rename */
i32 rename(const char *old_path, const char *new_path) {
    // Two-disk architecture: kernel VFS (/sys) is read-only
    // File renaming is rejected - userspace uses fsd for writable storage
    (void)old_path;
    (void)new_path;
    return -1;
}

namespace {

/**
 * @brief Build combined path from CWD and relative path.
 * @return Position in buffer after building.
 */
usize build_combined_path(const char *path, const char *cwd, char *combined) {
    usize pos = 0;

    if (path[0] != '/') {
        if (cwd && cwd[0]) {
            for (usize i = 0; cwd[i] && pos < MAX_PATH - 1; i++)
                combined[pos++] = cwd[i];
            if (pos > 0 && combined[pos - 1] != '/' && pos < MAX_PATH - 1)
                combined[pos++] = '/';
        } else {
            combined[pos++] = '/';
        }
    }

    for (usize i = 0; path[i] && pos < MAX_PATH - 1; i++)
        combined[pos++] = path[i];
    combined[pos] = '\0';

    return pos;
}

/**
 * @brief Process path components and write normalized result.
 * @return true on success.
 */
bool process_path_components(char *src, char *out, usize out_size) {
    usize out_pos = 0;
    usize component_starts[64];
    usize stack_depth = 0;

    if (out_size > 0)
        out[out_pos++] = '/';

    while (*src) {
        while (*src == '/')
            src++;
        if (*src == '\0')
            break;

        const char *comp_start = src;
        while (*src && *src != '/')
            src++;
        usize comp_len = static_cast<usize>(src - comp_start);

        if (comp_len == 1 && comp_start[0] == '.')
            continue;

        if (comp_len == 2 && comp_start[0] == '.' && comp_start[1] == '.') {
            if (stack_depth > 0)
                out_pos = component_starts[--stack_depth];
            continue;
        }

        if (out_pos + comp_len + 1 >= out_size)
            return false;

        if (stack_depth < 64)
            component_starts[stack_depth++] = out_pos;

        for (usize i = 0; i < comp_len; i++)
            out[out_pos++] = comp_start[i];
        out[out_pos++] = '/';
    }

    if (out_pos > 1 && out[out_pos - 1] == '/')
        out_pos--;

    out[out_pos] = '\0';
    return true;
}

} // anonymous namespace

/** @copydoc fs::vfs::normalize_path */
bool normalize_path(const char *path, const char *cwd, char *out, usize out_size) {
    if (!path || !out || out_size < 2)
        return false;

    char combined[MAX_PATH];
    build_combined_path(path, cwd, combined);
    return process_path_components(combined, out, out_size);
}

/** @copydoc fs::vfs::resolve_path_cwd */
u64 resolve_path_cwd(const char *path) {
    if (!path)
        return 0;

    // Two-disk architecture: kernel VFS only handles absolute /sys/* paths
    // Relative paths cannot access kernel filesystem - userspace handles via fsd
    if (path[0] != '/') {
        return 0;
    }

    // Delegate to resolve_path which handles /sys prefix check
    return resolve_path(path);
}

} // namespace fs::vfs
