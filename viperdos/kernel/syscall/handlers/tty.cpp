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

#include "handlers_internal.hpp"
#include "../../tty/tty.hpp"

namespace syscall
{

SyscallResult sys_tty_read(u64 a0, u64 a1, u64, u64, u64, u64)
{
    void *buf = reinterpret_cast<void *>(a0);
    u32 size = static_cast<u32>(a1);

    if (!validate_user_write(buf, size, false))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    i64 result = tty::read(buf, size);
    if (result < 0)
    {
        return SyscallResult::err(static_cast<error::Code>(result));
    }
    return SyscallResult::ok(static_cast<u64>(result));
}

SyscallResult sys_tty_write(u64 a0, u64 a1, u64, u64, u64, u64)
{
    const void *buf = reinterpret_cast<const void *>(a0);
    u32 size = static_cast<u32>(a1);

    if (!validate_user_read(buf, size, false))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    i64 result = tty::write(buf, size);
    if (result < 0)
    {
        return SyscallResult::err(static_cast<error::Code>(result));
    }
    return SyscallResult::ok(static_cast<u64>(result));
}

SyscallResult sys_tty_push_input(u64 a0, u64, u64, u64, u64, u64)
{
    tty::push_input(static_cast<char>(a0));
    return SyscallResult::ok();
}

SyscallResult sys_tty_has_input(u64, u64, u64, u64, u64, u64)
{
    return SyscallResult::ok(tty::has_input() ? 1 : 0);
}

} // namespace syscall
