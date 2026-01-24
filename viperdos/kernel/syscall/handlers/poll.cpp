//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/syscall/handlers/poll.cpp
// Purpose: Poll syscall handlers (0x20-0x2F).
//
//===----------------------------------------------------------------------===//

#include "handlers_internal.hpp"
#include "../../ipc/poll.hpp"
#include "../../ipc/pollset.hpp"

namespace syscall
{

SyscallResult sys_poll_create(u64, u64, u64, u64, u64, u64)
{
    i64 ps_id = pollset::create();
    if (ps_id < 0)
    {
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }
    return SyscallResult::ok(static_cast<u64>(ps_id));
}

SyscallResult sys_poll_add(u64 a0, u64 a1, u64 a2, u64, u64, u64)
{
    u32 ps_id = static_cast<u32>(a0);
    u32 key = static_cast<u32>(a1);
    u32 events = static_cast<u32>(a2);

    i64 result = pollset::add(ps_id, key, events);
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok();
}

SyscallResult sys_poll_remove(u64 a0, u64 a1, u64, u64, u64, u64)
{
    u32 ps_id = static_cast<u32>(a0);
    u32 key = static_cast<u32>(a1);

    i64 result = pollset::remove(ps_id, key);
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok();
}

SyscallResult sys_poll_wait(u64 a0, u64 a1, u64 a2, u64 a3, u64, u64)
{
    u32 ps_id = static_cast<u32>(a0);
    poll::PollEvent *events = reinterpret_cast<poll::PollEvent *>(a1);
    u32 max_events = static_cast<u32>(a2);
    i64 timeout_ms = static_cast<i64>(a3);

    if (!validate_user_write(events, max_events * sizeof(poll::PollEvent)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    i64 result = pollset::wait(ps_id, events, max_events, timeout_ms);
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok(static_cast<u64>(result));
}

} // namespace syscall
