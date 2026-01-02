//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/fsd_backend.cpp
// Purpose: libc-to-fsd bridge for file and directory operations.
// Key invariants: FDs 64-127 map to fsd; global client connects on demand.
// Ownership/Lifetime: Library; global client persists for process lifetime.
// Links: user/libfsclient, user/servers/fsd
//
//===----------------------------------------------------------------------===//

/**
 * @file fsd_backend.cpp
 * @brief libc-to-fsd bridge for file and directory operations.
 *
 * @details
 * This file provides the bridge between libc file functions and fsd:
 *
 * File Descriptor Management:
 * - __viper_fsd_is_fd: Check if FD is managed by fsd (64-127)
 * - __viper_fsd_open: Open file via fsd, returning fsd-managed FD
 * - __viper_fsd_close: Close fsd-managed FD
 * - __viper_fsd_dup: Duplicate fsd-managed FD
 *
 * File Operations:
 * - __viper_fsd_read/write: Read/write data
 * - __viper_fsd_seek: Seek to position
 * - __viper_fsd_stat/fstat: Get file information
 * - __viper_fsd_truncate: Truncate file
 *
 * Directory Operations:
 * - __viper_fsd_mkdir/rmdir: Create/remove directories
 * - __viper_fsd_opendir/readdir/closedir: Directory iteration
 * - __viper_fsd_unlink/rename: Remove/rename files
 *
 * Path prefixes /vfs/ or containing ':' are routed to fsd.
 */

#include "fsclient.hpp"
#include "servers/fsd/fs_protocol.hpp"

#include "../include/dirent.h"
#include "../include/fcntl.h"
#include "../include/sys/stat.h"
#include "../include/sys/types.h"

namespace
{

constexpr int FSD_FD_BASE = 64;
constexpr int FSD_MAX_FDS = 64;

struct FsdObject
{
    bool in_use;
    u32 file_id;
    u32 refs;
};

struct FsdFdSlot
{
    bool in_use;
    u16 obj_index;
};

static FsdObject g_objs[FSD_MAX_FDS];
static FsdFdSlot g_fds[FSD_MAX_FDS];

// Global client instance - avoid static initialization issues with __cxa_guard_acquire
static fsclient::Client g_fsd_client;

static bool is_prefix(const char *s, const char *prefix)
{
    if (!s || !prefix)
        return false;
    while (*prefix)
    {
        if (*s++ != *prefix++)
            return false;
    }
    return true;
}

static bool contains_char(const char *s, char needle)
{
    if (!s)
        return false;
    while (*s)
    {
        if (*s++ == needle)
            return true;
    }
    return false;
}

static usize bounded_strlen(const char *s, usize max_len)
{
    if (!s)
        return 0;
    usize n = 0;
    while (n < max_len && s[n])
        n++;
    return n;
}

static bool fd_in_range(int fd)
{
    return fd >= FSD_FD_BASE && fd < (FSD_FD_BASE + FSD_MAX_FDS);
}

static int fd_index(int fd)
{
    return fd - FSD_FD_BASE;
}

static FsdObject *get_obj_for_fd(int fd)
{
    if (!fd_in_range(fd))
        return nullptr;

    int idx = fd_index(fd);
    if (idx < 0 || idx >= FSD_MAX_FDS)
        return nullptr;

    if (!g_fds[idx].in_use)
        return nullptr;

    int obj = static_cast<int>(g_fds[idx].obj_index);
    if (obj < 0 || obj >= FSD_MAX_FDS)
        return nullptr;
    if (!g_objs[obj].in_use)
        return nullptr;
    return &g_objs[obj];
}

static i32 alloc_obj(u32 file_id)
{
    for (int i = 0; i < FSD_MAX_FDS; i++)
    {
        if (!g_objs[i].in_use)
        {
            g_objs[i].in_use = true;
            g_objs[i].file_id = file_id;
            g_objs[i].refs = 1;
            return i;
        }
    }
    return VERR_OUT_OF_MEMORY;
}

static void release_obj(i32 obj)
{
    if (obj < 0 || obj >= FSD_MAX_FDS)
        return;
    g_objs[obj].in_use = false;
    g_objs[obj].file_id = 0;
    g_objs[obj].refs = 0;
}

static i32 alloc_fd_slot(u16 obj)
{
    for (int i = 0; i < FSD_MAX_FDS; i++)
    {
        if (!g_fds[i].in_use)
        {
            g_fds[i].in_use = true;
            g_fds[i].obj_index = obj;
            return FSD_FD_BASE + i;
        }
    }
    return VERR_OUT_OF_MEMORY;
}

static void free_fd_slot(int fd)
{
    if (!fd_in_range(fd))
        return;
    int idx = fd_index(fd);
    if (idx < 0 || idx >= FSD_MAX_FDS)
        return;
    g_fds[idx].in_use = false;
    g_fds[idx].obj_index = 0;
}

static void fill_posix_stat(struct stat *out, const sys::Stat &in)
{
    // ViperOS currently does not provide full POSIX ownership/link/dev fields over fsd.
    out->st_dev = 0;
    out->st_ino = static_cast<ino_t>(in.ino);
    out->st_mode = static_cast<mode_t>(in.mode);
    out->st_nlink = 1;
    out->st_uid = 0;
    out->st_gid = 0;
    out->st_rdev = 0;
    out->st_size = static_cast<off_t>(in.size);
    out->st_blksize = 4096;
    out->st_blocks = static_cast<blkcnt_t>(in.blocks);
    out->st_atime = static_cast<time_t>(in.atime);
    out->st_mtime = static_cast<time_t>(in.mtime);
    out->st_ctime = static_cast<time_t>(in.ctime);
}

static bool kernel_path_only(const char *path)
{
    if (!path)
        return true;

    // Keep kernel-backed pseudo-files in the kernel for now.
    if (is_prefix(path, "/dev") && (path[4] == '\0' || path[4] == '/'))
        return true;
    if (is_prefix(path, "/proc") && (path[5] == '\0' || path[5] == '/'))
        return true;

    // Assign-style paths (e.g., SYS:) are currently kernel-only.
    if (contains_char(path, ':'))
        return true;

    return false;
}

} // namespace

extern "C" int __viper_fsd_is_available()
{
    return (g_fsd_client.connect() == 0) ? 1 : 0;
}

extern "C" int __viper_fsd_is_fd(int fd)
{
    return get_obj_for_fd(fd) ? 1 : 0;
}

extern "C" int __viper_fsd_prepare_path(const char *in, char *out, size_t out_cap)
{
    if (!in || !out || out_cap == 0)
        return VERR_INVALID_ARG;

    if (kernel_path_only(in))
        return 0;

    // Already absolute.
    if (in[0] == '/')
    {
        usize n = bounded_strlen(in, fs::MAX_PATH_LEN + 1);
        if (n == 0 || n > fs::MAX_PATH_LEN)
            return 0;
        if (n + 1 > out_cap)
            return VERR_INVALID_ARG;
        for (usize i = 0; i < n; i++)
            out[i] = in[i];
        out[n] = '\0';
        return 1;
    }

    // Relative: build absolute using kernel getcwd for now.
    char cwd[256];
    i64 cwd_len = sys::getcwd(cwd, sizeof(cwd));
    if (cwd_len < 0)
        return 0;

    // cwd is expected to be NUL-terminated; guard regardless.
    cwd[sizeof(cwd) - 1] = '\0';
    usize cwd_n = bounded_strlen(cwd, sizeof(cwd) - 1);
    usize rel_n = bounded_strlen(in, fs::MAX_PATH_LEN + 1);
    if (rel_n == 0 || rel_n > fs::MAX_PATH_LEN)
        return 0;

    // Join: cwd + "/" + in
    usize pos = 0;
    if (cwd_n == 0)
    {
        if (out_cap < 2)
            return VERR_INVALID_ARG;
        out[pos++] = '/';
    }
    else
    {
        if (cwd_n + 1 > out_cap)
            return VERR_INVALID_ARG;
        for (usize i = 0; i < cwd_n; i++)
            out[pos++] = cwd[i];
    }

    if (pos == 0 || out[pos - 1] != '/')
    {
        if (pos + 1 >= out_cap)
            return VERR_INVALID_ARG;
        out[pos++] = '/';
    }

    if (pos + rel_n + 1 > out_cap)
        return VERR_INVALID_ARG;
    for (usize i = 0; i < rel_n; i++)
        out[pos++] = in[i];
    out[pos] = '\0';

    if (pos > fs::MAX_PATH_LEN)
    {
        // Too long for the current fsd protocol.
        return 0;
    }

    return 1;
}

extern "C" int __viper_fsd_open(const char *abs_path, int flags)
{
    if (!abs_path)
        return VERR_INVALID_ARG;

    u32 file_id = 0;
    i32 err = g_fsd_client.open(abs_path, static_cast<u32>(flags), &file_id);
    if (err != 0)
        return err;

    i32 obj = alloc_obj(file_id);
    if (obj < 0)
    {
        (void)g_fsd_client.close(file_id);
        return obj;
    }

    i32 fd = alloc_fd_slot(static_cast<u16>(obj));
    if (fd < 0)
    {
        release_obj(obj);
        (void)g_fsd_client.close(file_id);
        return fd;
    }

    return fd;
}

extern "C" ssize_t __viper_fsd_read(int fd, void *buf, size_t count)
{
    FsdObject *obj = get_obj_for_fd(fd);
    if (!obj)
        return VERR_INVALID_HANDLE;

    if (count > 0xFFFFFFFFul)
        return VERR_INVALID_ARG;
    return static_cast<ssize_t>(g_fsd_client.read(obj->file_id, buf, static_cast<u32>(count)));
}

extern "C" ssize_t __viper_fsd_write(int fd, const void *buf, size_t count)
{
    FsdObject *obj = get_obj_for_fd(fd);
    if (!obj)
        return VERR_INVALID_HANDLE;

    if (count > 0xFFFFFFFFul)
        return VERR_INVALID_ARG;
    return static_cast<ssize_t>(g_fsd_client.write(obj->file_id, buf, static_cast<u32>(count)));
}

extern "C" int __viper_fsd_close(int fd)
{
    if (!fd_in_range(fd))
        return VERR_INVALID_HANDLE;

    int idx = fd_index(fd);
    if (idx < 0 || idx >= FSD_MAX_FDS || !g_fds[idx].in_use)
        return VERR_INVALID_HANDLE;

    i32 obj = static_cast<i32>(g_fds[idx].obj_index);
    if (obj < 0 || obj >= FSD_MAX_FDS || !g_objs[obj].in_use)
        return VERR_INVALID_HANDLE;

    free_fd_slot(fd);

    if (g_objs[obj].refs > 0)
        g_objs[obj].refs--;
    if (g_objs[obj].refs == 0)
    {
        u32 file_id = g_objs[obj].file_id;
        release_obj(obj);
        return g_fsd_client.close(file_id);
    }

    return 0;
}

extern "C" long __viper_fsd_lseek(int fd, long offset, int whence)
{
    FsdObject *obj = get_obj_for_fd(fd);
    if (!obj)
        return VERR_INVALID_HANDLE;

    i64 new_off = 0;
    i64 rc = g_fsd_client.seek(
        obj->file_id, static_cast<i64>(offset), static_cast<i32>(whence), &new_off);
    if (rc < 0)
        return static_cast<long>(rc);
    return static_cast<long>(new_off);
}

extern "C" int __viper_fsd_dup(int oldfd)
{
    FsdObject *obj = get_obj_for_fd(oldfd);
    if (!obj)
        return VERR_INVALID_HANDLE;

    i32 newfd = alloc_fd_slot(static_cast<u16>(obj - g_objs));
    if (newfd < 0)
        return newfd;

    obj->refs++;
    return newfd;
}

extern "C" int __viper_fsd_dup2(int oldfd, int newfd)
{
    FsdObject *obj = get_obj_for_fd(oldfd);
    if (!obj)
        return VERR_INVALID_HANDLE;

    if (!fd_in_range(newfd))
    {
        // Cross-namespace dup2 (to kernel FDs) is not supported yet.
        return VERR_NOT_SUPPORTED;
    }

    if (oldfd == newfd)
        return newfd;

    // If newfd is already open (fsd), close it first.
    if (__viper_fsd_is_fd(newfd))
    {
        (void)__viper_fsd_close(newfd);
    }

    int idx = fd_index(newfd);
    g_fds[idx].in_use = true;
    g_fds[idx].obj_index = static_cast<u16>(obj - g_objs);
    obj->refs++;
    return newfd;
}

extern "C" int __viper_fsd_stat(const char *abs_path, struct stat *statbuf)
{
    if (!abs_path || !statbuf)
        return VERR_INVALID_ARG;

    sys::Stat st = {};
    i32 err = g_fsd_client.stat(abs_path, &st);
    if (err != 0)
        return err;

    fill_posix_stat(statbuf, st);
    return 0;
}

extern "C" int __viper_fsd_fstat(int fd, struct stat *statbuf)
{
    if (!statbuf)
        return VERR_INVALID_ARG;

    FsdObject *obj = get_obj_for_fd(fd);
    if (!obj)
        return VERR_INVALID_HANDLE;

    sys::Stat st = {};
    i32 err = g_fsd_client.fstat(obj->file_id, &st);
    if (err != 0)
        return err;

    fill_posix_stat(statbuf, st);
    return 0;
}

extern "C" int __viper_fsd_fsync(int fd)
{
    FsdObject *obj = get_obj_for_fd(fd);
    if (!obj)
        return VERR_INVALID_HANDLE;

    return g_fsd_client.fsync(obj->file_id);
}

extern "C" int __viper_fsd_mkdir(const char *abs_path)
{
    if (!abs_path)
        return VERR_INVALID_ARG;
    return g_fsd_client.mkdir(abs_path);
}

extern "C" int __viper_fsd_rmdir(const char *abs_path)
{
    if (!abs_path)
        return VERR_INVALID_ARG;
    return g_fsd_client.rmdir(abs_path);
}

extern "C" int __viper_fsd_unlink(const char *abs_path)
{
    if (!abs_path)
        return VERR_INVALID_ARG;
    return g_fsd_client.unlink(abs_path);
}

extern "C" int __viper_fsd_rename(const char *abs_old, const char *abs_new)
{
    if (!abs_old || !abs_new)
        return VERR_INVALID_ARG;
    return g_fsd_client.rename(abs_old, abs_new);
}

extern "C" int __viper_fsd_readdir(int fd, struct dirent *out_ent)
{
    if (!out_ent)
        return VERR_INVALID_ARG;

    FsdObject *obj = get_obj_for_fd(fd);
    if (!obj)
        return VERR_INVALID_HANDLE;

    u64 ino = 0;
    u8 type = 0;
    char name_buf[NAME_MAX + 1];
    i32 rc = g_fsd_client.readdir_one(obj->file_id, &ino, &type, name_buf, sizeof(name_buf));
    if (rc <= 0)
        return rc;

    out_ent->d_ino = static_cast<unsigned long>(ino);
    out_ent->d_type =
        (type == fs::file_type::FILE || type == fs::file_type::DIR) ? type : DT_UNKNOWN;

    usize n = bounded_strlen(name_buf, NAME_MAX);
    for (usize i = 0; i < n; i++)
        out_ent->d_name[i] = name_buf[i];
    out_ent->d_name[n] = '\0';

    return 1;
}
