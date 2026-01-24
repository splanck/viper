//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/syscall/handlers/debug.cpp
// Purpose: Debug/Console syscall handlers (0xF0-0xFF).
//
//===----------------------------------------------------------------------===//

#include "handlers_internal.hpp"
#include "../../arch/aarch64/timer.hpp"
#include "../../console/gcon.hpp"
#include "../../console/serial.hpp"

namespace syscall
{

SyscallResult sys_debug_print(u64 a0, u64, u64, u64, u64, u64)
{
    const char *str = reinterpret_cast<const char *>(a0);

    if (validate_user_string(str, 4096) < 0)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    serial::puts(str);
    if (gcon::is_available())
    {
        gcon::puts(str);
    }
    return SyscallResult::ok();
}

SyscallResult sys_getchar(u64, u64, u64, u64, u64, u64)
{
    int c = serial::getc_nonblock();
    if (c < 0)
    {
        return SyscallResult::err(error::VERR_WOULD_BLOCK);
    }
    return SyscallResult::ok(static_cast<u64>(c));
}

SyscallResult sys_putchar(u64 a0, u64, u64, u64, u64, u64)
{
    char c = static_cast<char>(a0);
    serial::putc(c);
    if (gcon::is_available())
    {
        gcon::putc(c);
    }
    return SyscallResult::ok();
}

SyscallResult sys_uptime(u64, u64, u64, u64, u64, u64)
{
    return SyscallResult::ok(timer::get_ms());
}

} // namespace syscall
