//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/syscall/handlers/tty.cpp
// Purpose: TTY syscall handlers (0x120-0x12F).
//
//===----------------------------------------------------------------------===//

#include "../../tty/tty.hpp"
#include "../../console/gcon.hpp"
#include "handlers_internal.hpp"

namespace syscall {

SyscallResult sys_tty_read(u64 a0, u64 a1, u64, u64, u64, u64) {
    void *buf = reinterpret_cast<void *>(a0);
    u32 size = static_cast<u32>(a1);

    if (!validate_user_write(buf, size, false)) {
        return err_invalid_arg();
    }

    i64 result = tty::read(buf, size);
    if (result < 0) {
        return err_code(result);
    }
    return ok_u64(static_cast<u64>(result));
}

SyscallResult sys_tty_write(u64 a0, u64 a1, u64, u64, u64, u64) {
    const void *buf = reinterpret_cast<const void *>(a0);
    u32 size = static_cast<u32>(a1);

    if (!validate_user_read(buf, size, false)) {
        return err_invalid_arg();
    }

    i64 result = tty::write(buf, size);
    if (result < 0) {
        return err_code(result);
    }
    return ok_u64(static_cast<u64>(result));
}

SyscallResult sys_tty_push_input(u64 a0, u64, u64, u64, u64, u64) {
    tty::push_input(static_cast<char>(a0));
    return SyscallResult::ok();
}

SyscallResult sys_tty_has_input(u64, u64, u64, u64, u64, u64) {
    return SyscallResult::ok(tty::has_input() ? 1 : 0);
}

SyscallResult sys_tty_get_size(u64, u64, u64, u64, u64, u64) {
    u32 cols = 80, rows = 25;
    if (gcon::is_available()) {
        gcon::get_size(cols, rows);
    }
    // Pack cols (low 32) and rows (high 32) into single u64
    u64 packed = (static_cast<u64>(rows) << 32) | static_cast<u64>(cols);
    return SyscallResult::ok(packed);
}

} // namespace syscall
