//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/syscall/handlers/time.cpp
// Purpose: Time syscall handlers (0x30-0x3F).
//
//===----------------------------------------------------------------------===//

#include "handlers_internal.hpp"
#include "../../arch/aarch64/timer.hpp"
#include "../../ipc/poll.hpp"
#include "../../sched/task.hpp"

namespace syscall
{

SyscallResult sys_time_now(u64, u64, u64, u64, u64, u64)
{
    return SyscallResult::ok(timer::get_ms());
}

SyscallResult sys_sleep(u64 a0, u64, u64, u64, u64, u64)
{
    u64 ms = a0;
    if (ms == 0)
    {
        task::yield();
    }
    else
    {
        poll::sleep_ms(ms);
    }
    return SyscallResult::ok();
}

} // namespace syscall
