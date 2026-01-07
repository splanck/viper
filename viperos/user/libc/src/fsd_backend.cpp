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

// External getenv declaration
extern "C" char *getenv(const char *name);

namespace
{

constexpr int FSD_FD_BASE = 64;
constexpr int FSD_MAX_FDS = 64;

// Per-process current working directory for fsd paths
static char g_fsd_cwd[256] = "/";
static bool g_fsd_cwd_initialized = false;

static void init_fsd_cwd()
{
    if (g_fsd_cwd_initialized)
        return;
    g_fsd_cwd_initialized = true;

    // Try to get PWD from environment first
    const char *pwd = getenv("PWD");
    if (pwd && pwd[0] == '/')
    {
        usize len = 0;
        while (pwd[len] && len < sizeof(g_fsd_cwd) - 1)
            len++;
        for (usize i = 0; i < len; i++)
            g_fsd_cwd[i] = pwd[i];
        g_fsd_cwd[len] = '\0';
        return;
    }

    // Try to get PWD from spawn args (format: "PWD=/path;actual_args")
    char args[256];
    i64 args_len = sys::get_args(args, sizeof(args));
    if (args_len > 4 && args[0] == 'P' && args[1] == 'W' && args[2] == 'D' && args[3] == '=')
    {
        // Find the semicolon separator or end of string
        usize i = 4;
        while (i < static_cast<usize>(args_len) && args[i] != ';' && args[i] != '\0')
            i++;

        usize pwd_len = i - 4;
        if (pwd_len > 0 && pwd_len < sizeof(g_fsd_cwd) && args[4] == '/')
        {
            for (usize j = 0; j < pwd_len; j++)
                g_fsd_cwd[j] = args[4 + j];
            g_fsd_cwd[pwd_len] = '\0';
        }
    }
}

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

    // Relative: build absolute using libc-tracked cwd.
    init_fsd_cwd();

    usize cwd_n = bounded_strlen(g_fsd_cwd, sizeof(g_fsd_cwd) - 1);
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
            out[pos++] = g_fsd_cwd[i];
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

extern "C" int __viper_fsd_chdir(const char *path)
{
    if (!path)
        return VERR_INVALID_ARG;

    init_fsd_cwd();

    // Build absolute path
    char abs_path[256];
    if (path[0] == '/')
    {
        // Absolute path
        usize len = bounded_strlen(path, sizeof(abs_path) - 1);
        for (usize i = 0; i < len; i++)
            abs_path[i] = path[i];
        abs_path[len] = '\0';
    }
    else
    {
        // Relative path - join with current cwd
        usize cwd_len = bounded_strlen(g_fsd_cwd, sizeof(g_fsd_cwd) - 1);
        usize path_len = bounded_strlen(path, sizeof(abs_path) - cwd_len - 2);
        usize pos = 0;

        for (usize i = 0; i < cwd_len; i++)
            abs_path[pos++] = g_fsd_cwd[i];
        if (pos == 0 || abs_path[pos - 1] != '/')
            abs_path[pos++] = '/';
        for (usize i = 0; i < path_len; i++)
            abs_path[pos++] = path[i];
        abs_path[pos] = '\0';
    }

    // Validate directory exists by trying to open it
    u32 dir_id = 0;
    i32 err = g_fsd_client.open(abs_path, 0, &dir_id);
    if (err != 0)
        return err;
    g_fsd_client.close(dir_id);

    // Update the cwd
    usize len = bounded_strlen(abs_path, sizeof(g_fsd_cwd) - 1);
    for (usize i = 0; i < len; i++)
        g_fsd_cwd[i] = abs_path[i];
    g_fsd_cwd[len] = '\0';

    return 0;
}

extern "C" int __viper_fsd_getcwd(char *buf, size_t size)
{
    if (!buf || size == 0)
        return VERR_INVALID_ARG;

    init_fsd_cwd();

    usize len = bounded_strlen(g_fsd_cwd, sizeof(g_fsd_cwd) - 1);
    if (len + 1 > size)
        return VERR_INVALID_ARG;

    for (usize i = 0; i < len; i++)
        buf[i] = g_fsd_cwd[i];
    buf[len] = '\0';

    return static_cast<int>(len);
}

extern "C" i64 __viper_get_program_args(char *buf, size_t bufsize)
{
    if (!buf || bufsize == 0)
        return VERR_INVALID_ARG;

    // Get raw args from kernel
    char raw_args[512];
    i64 len = sys::get_args(raw_args, sizeof(raw_args));
    if (len <= 0)
    {
        buf[0] = '\0';
        return 0;
    }

    // Check for PWD= prefix and skip it
    const char *start = raw_args;
    if (len > 4 && raw_args[0] == 'P' && raw_args[1] == 'W' && raw_args[2] == 'D' &&
        raw_args[3] == '=')
    {
        // Find the semicolon separator
        usize i = 4;
        while (i < static_cast<usize>(len) && raw_args[i] != ';' && raw_args[i] != '\0')
            i++;

        if (i < static_cast<usize>(len) && raw_args[i] == ';')
        {
            // Skip past the semicolon
            start = raw_args + i + 1;
            len = len - static_cast<i64>(i) - 1;
        }
        else
        {
            // No semicolon means no actual args, just PWD
            buf[0] = '\0';
            return 0;
        }
    }

    // Copy the actual args
    if (static_cast<usize>(len) >= bufsize)
        len = static_cast<i64>(bufsize) - 1;

    for (i64 i = 0; i < len; i++)
        buf[i] = start[i];
    buf[len] = '\0';

    return len;
}
