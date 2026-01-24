//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/syscall/handlers/dir.cpp
// Purpose: Directory/filesystem syscall handlers (0x60-0x6F).
//
//===----------------------------------------------------------------------===//

#include "../../fs/vfs/vfs.hpp"
#include "../../include/constants.hpp"
#include "../../sched/task.hpp"
#include "handlers_internal.hpp"

namespace syscall {

namespace kc = kernel::constants;

SyscallResult sys_readdir(u64 a0, u64 a1, u64 a2, u64, u64, u64) {
    i32 fd = static_cast<i32>(a0);
    void *buf = reinterpret_cast<void *>(a1);
    usize count = static_cast<usize>(a2);

    VALIDATE_USER_WRITE(buf, count);

    i64 result = fs::vfs::getdents(fd, buf, count);
    if (result < 0) {
        return err_code(result);
    }
    return ok_u64(static_cast<u64>(result));
}

SyscallResult sys_mkdir(u64 a0, u64, u64, u64, u64, u64) {
    const char *path = reinterpret_cast<const char *>(a0);

    VALIDATE_USER_STRING(path, kc::limits::MAX_PATH);

    i64 result = fs::vfs::mkdir(path);
    if (result < 0) {
        return err_code(result);
    }
    return SyscallResult::ok();
}

SyscallResult sys_rmdir(u64 a0, u64, u64, u64, u64, u64) {
    const char *path = reinterpret_cast<const char *>(a0);

    VALIDATE_USER_STRING(path, kc::limits::MAX_PATH);

    i64 result = fs::vfs::rmdir(path);
    if (result < 0) {
        return err_code(result);
    }
    return SyscallResult::ok();
}

SyscallResult sys_unlink(u64 a0, u64, u64, u64, u64, u64) {
    const char *path = reinterpret_cast<const char *>(a0);

    VALIDATE_USER_STRING(path, kc::limits::MAX_PATH);

    i64 result = fs::vfs::unlink(path);
    if (result < 0) {
        return err_code(result);
    }
    return SyscallResult::ok();
}

SyscallResult sys_rename(u64 a0, u64 a1, u64, u64, u64, u64) {
    const char *old_path = reinterpret_cast<const char *>(a0);
    const char *new_path = reinterpret_cast<const char *>(a1);

    VALIDATE_USER_STRING(old_path, kc::limits::MAX_PATH);
    VALIDATE_USER_STRING(new_path, kc::limits::MAX_PATH);

    i64 result = fs::vfs::rename(old_path, new_path);
    if (result < 0) {
        return err_code(result);
    }
    return SyscallResult::ok();
}

SyscallResult sys_symlink(u64 a0, u64 a1, u64, u64, u64, u64) {
    const char *target = reinterpret_cast<const char *>(a0);
    const char *linkpath = reinterpret_cast<const char *>(a1);

    VALIDATE_USER_STRING(target, kc::limits::MAX_PATH);
    VALIDATE_USER_STRING(linkpath, kc::limits::MAX_PATH);

    i64 result = fs::vfs::symlink(target, linkpath);
    if (result < 0) {
        return err_code(result);
    }
    return SyscallResult::ok();
}

SyscallResult sys_readlink(u64 a0, u64 a1, u64 a2, u64, u64, u64) {
    const char *path = reinterpret_cast<const char *>(a0);
    char *buf = reinterpret_cast<char *>(a1);
    usize bufsiz = static_cast<usize>(a2);

    VALIDATE_USER_STRING(path, kc::limits::MAX_PATH);
    VALIDATE_USER_WRITE(buf, bufsiz);

    i64 result = fs::vfs::readlink(path, buf, bufsiz);
    if (result < 0) {
        return err_code(result);
    }
    return ok_u64(static_cast<u64>(result));
}

SyscallResult sys_getcwd(u64 a0, u64 a1, u64, u64, u64, u64) {
    char *buf = reinterpret_cast<char *>(a0);
    usize size = static_cast<usize>(a1);

    VALIDATE_USER_WRITE(buf, size);

    task::Task *t = task::current();
    if (!t) {
        return err_not_found();
    }

    usize len = 0;
    while (len < sizeof(t->cwd) && t->cwd[len]) {
        len++;
    }

    if (len + 1 > size) {
        return err_code(error::VERR_BUFFER_TOO_SMALL);
    }

    for (usize i = 0; i <= len; i++) {
        buf[i] = t->cwd[i];
    }

    return ok_u64(len);
}

SyscallResult sys_chdir(u64 a0, u64, u64, u64, u64, u64) {
    const char *path = reinterpret_cast<const char *>(a0);

    VALIDATE_USER_STRING(path, kc::limits::MAX_PATH);

    task::Task *t = task::current();
    if (!t) {
        return err_not_found();
    }

    char normalized[kc::limits::MAX_PATH];
    if (!fs::vfs::normalize_path(path, t->cwd, normalized, sizeof(normalized))) {
        return err_invalid_arg();
    }

    i64 result = fs::vfs::open(normalized, 0);
    if (result < 0) {
        return err_code(result);
    }
    fs::vfs::close(static_cast<i32>(result));

    usize len = 0;
    while (len < sizeof(t->cwd) - 1 && normalized[len]) {
        t->cwd[len] = normalized[len];
        len++;
    }
    t->cwd[len] = '\0';

    return SyscallResult::ok();
}

} // namespace syscall
