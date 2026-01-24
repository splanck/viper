//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/syscall/handlers/procgroup.cpp
// Purpose: Process group/session syscall handlers (0xA0-0xAF).
//
//===----------------------------------------------------------------------===//

#include "../../viper/viper.hpp"
#include "handlers_internal.hpp"

namespace syscall {

SyscallResult sys_getpid(u64, u64, u64, u64, u64, u64) {
    viper::Viper *v = viper::current();
    if (!v) {
        return err_not_found();
    }
    return SyscallResult::ok(v->id);
}

SyscallResult sys_getppid(u64, u64, u64, u64, u64, u64) {
    viper::Viper *v = viper::current();
    if (!v) {
        return err_not_found();
    }
    if (!v->parent) {
        return SyscallResult::ok(0);
    }
    return SyscallResult::ok(v->parent->id);
}

SyscallResult sys_getpgid(u64 a0, u64, u64, u64, u64, u64) {
    u64 pid = a0;
    i64 result = viper::getpgid(pid);
    if (result < 0) {
        return err_code(result);
    }
    return ok_u64(static_cast<u64>(result));
}

SyscallResult sys_setpgid(u64 a0, u64 a1, u64, u64, u64, u64) {
    u64 pid = a0;
    u64 pgid = a1;
    i64 result = viper::setpgid(pid, pgid);
    if (result < 0) {
        return err_code(result);
    }
    return SyscallResult::ok();
}

SyscallResult sys_getsid(u64 a0, u64, u64, u64, u64, u64) {
    u64 pid = a0;
    i64 result = viper::getsid(pid);
    if (result < 0) {
        return err_code(result);
    }
    return ok_u64(static_cast<u64>(result));
}

SyscallResult sys_setsid(u64, u64, u64, u64, u64, u64) {
    i64 result = viper::setsid();
    if (result < 0) {
        return err_code(result);
    }
    return ok_u64(static_cast<u64>(result));
}

SyscallResult sys_get_args(u64 a0, u64 a1, u64, u64, u64, u64) {
    char *buf = reinterpret_cast<char *>(a0);
    usize bufsize = static_cast<usize>(a1);

    if (bufsize > 0 && !validate_user_write(buf, bufsize)) {
        return err_invalid_arg();
    }

    viper::Viper *v = viper::current();
    if (!v) {
        return err_not_found();
    }

    // Calculate length of args
    usize len = 0;
    while (len < 255 && v->args[len])
        len++;

    // If just querying length (null buffer or zero size)
    if (!buf || bufsize == 0) {
        return SyscallResult::ok(len);
    }

    // Copy args to buffer
    usize copy_len = (len < bufsize - 1) ? len : bufsize - 1;
    for (usize i = 0; i < copy_len; i++) {
        buf[i] = v->args[i];
    }
    buf[copy_len] = '\0';

    return SyscallResult::ok(len);
}

} // namespace syscall
