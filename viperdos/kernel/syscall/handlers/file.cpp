//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/syscall/handlers/file.cpp
// Purpose: File I/O syscall handlers (0x40-0x4F).
//
//===----------------------------------------------------------------------===//

#include "../../console/console.hpp"
#include "../../console/gcon.hpp"
#include "../../console/serial.hpp"
#include "../../fs/vfs/vfs.hpp"
#include "../../include/constants.hpp"
#include "../../sched/task.hpp"
#include "handlers_internal.hpp"

namespace syscall {

SyscallResult sys_open(u64 a0, u64 a1, u64, u64, u64, u64) {
    const char *path = reinterpret_cast<const char *>(a0);
    u32 flags = static_cast<u32>(a1);

    VALIDATE_USER_STRING(path, kernel::constants::limits::MAX_PATH);

    i64 result = fs::vfs::open(path, flags);
    if (result < 0) {
        return err_code(result);
    }
    return ok_u64(static_cast<u64>(result));
}

SyscallResult sys_close(u64 a0, u64, u64, u64, u64, u64) {
    i32 fd = static_cast<i32>(a0);

    // stdin/stdout/stderr are pseudo-FDs backed by the console.
    if (fd >= 0 && fd <= 2) {
        return SyscallResult::ok();
    }

    i64 result = fs::vfs::close(fd);
    if (result < 0) {
        return err_code(result);
    }
    return SyscallResult::ok();
}

SyscallResult sys_fsync(u64 a0, u64, u64, u64, u64, u64) {
    i32 fd = static_cast<i32>(a0);

    if (fd >= 0 && fd <= 2) {
        return SyscallResult::ok();
    }

    i32 result = fs::vfs::fsync(fd);
    if (result < 0) {
        return err_code(result);
    }
    return SyscallResult::ok();
}

SyscallResult sys_read(u64 a0, u64 a1, u64 a2, u64, u64, u64) {
    i32 fd = static_cast<i32>(a0);
    void *buf = reinterpret_cast<void *>(a1);
    usize count = static_cast<usize>(a2);

    if (count == 0) {
        return SyscallResult::ok(0);
    }

    VALIDATE_USER_WRITE(buf, count);

    // stdin: read from console input (blocking until at least 1 byte).
    if (fd == 0) {
        char *out = reinterpret_cast<char *>(buf);
        usize n = 0;
        while (n < count) {
            console::poll_input();
            i32 c = console::getchar();
            if (c < 0) {
                if (n > 0) {
                    break;
                }
                task::yield();
                continue;
            }
            out[n++] = static_cast<char>(c);
        }
        return ok_u64(n);
    }

    i64 result = fs::vfs::read(fd, buf, count);
    if (result < 0) {
        return err_code(result);
    }
    return ok_u64(static_cast<u64>(result));
}

SyscallResult sys_write(u64 a0, u64 a1, u64 a2, u64, u64, u64) {
    i32 fd = static_cast<i32>(a0);
    const void *buf = reinterpret_cast<const void *>(a1);
    usize count = static_cast<usize>(a2);

    if (count == 0) {
        return SyscallResult::ok(0);
    }

    VALIDATE_USER_READ(buf, count);

    // stdout/stderr: write to console output.
    if (fd == 1 || fd == 2) {
        const char *p = reinterpret_cast<const char *>(buf);
        for (usize i = 0; i < count; i++) {
            serial::putc(p[i]);
            if (gcon::is_available()) {
                gcon::putc(p[i]);
            }
        }
        return ok_u64(count);
    }

    i64 result = fs::vfs::write(fd, buf, count);
    if (result < 0) {
        return err_code(result);
    }
    return ok_u64(static_cast<u64>(result));
}

SyscallResult sys_lseek(u64 a0, u64 a1, u64 a2, u64, u64, u64) {
    i32 fd = static_cast<i32>(a0);
    i64 offset = static_cast<i64>(a1);
    i32 whence = static_cast<i32>(a2);

    i64 result = fs::vfs::lseek(fd, offset, whence);
    if (result < 0) {
        return err_code(result);
    }
    return ok_u64(static_cast<u64>(result));
}

SyscallResult sys_stat(u64 a0, u64 a1, u64, u64, u64, u64) {
    const char *path = reinterpret_cast<const char *>(a0);
    fs::vfs::Stat *st = reinterpret_cast<fs::vfs::Stat *>(a1);

    VALIDATE_USER_STRING(path, kernel::constants::limits::MAX_PATH);
    VALIDATE_USER_WRITE(st, sizeof(fs::vfs::Stat));

    i64 result = fs::vfs::stat(path, st);
    if (result < 0) {
        return err_code(result);
    }
    return SyscallResult::ok();
}

SyscallResult sys_fstat(u64 a0, u64 a1, u64, u64, u64, u64) {
    i32 fd = static_cast<i32>(a0);
    fs::vfs::Stat *st = reinterpret_cast<fs::vfs::Stat *>(a1);

    VALIDATE_USER_WRITE(st, sizeof(fs::vfs::Stat));

    i64 result = fs::vfs::fstat(fd, st);
    if (result < 0) {
        return err_code(result);
    }
    return SyscallResult::ok();
}

SyscallResult sys_dup(u64 a0, u64, u64, u64, u64, u64) {
    i32 fd = static_cast<i32>(a0);
    i64 result = fs::vfs::dup(fd);
    if (result < 0) {
        return err_code(result);
    }
    return ok_u64(static_cast<u64>(result));
}

SyscallResult sys_dup2(u64 a0, u64 a1, u64, u64, u64, u64) {
    i32 oldfd = static_cast<i32>(a0);
    i32 newfd = static_cast<i32>(a1);
    i64 result = fs::vfs::dup2(oldfd, newfd);
    if (result < 0) {
        return err_code(result);
    }
    return ok_u64(static_cast<u64>(result));
}

} // namespace syscall
