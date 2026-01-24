//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/syscall/handlers/channel.cpp
// Purpose: Channel IPC syscall handlers (0x10-0x1F).
// Key invariants: All handlers validate user pointers before access.
// Ownership/Lifetime: Static functions; linked at compile time.
// Links: kernel/syscall/table.cpp, kernel/ipc/channel.cpp
//
//===----------------------------------------------------------------------===//

#include "handlers_internal.hpp"
#include "../../cap/handle.hpp"
#include "../../cap/rights.hpp"
#include "../../ipc/channel.hpp"
#include "../../kobj/channel.hpp"

namespace syscall
{

// =============================================================================
// Channel IPC Syscalls (0x10-0x1F)
// =============================================================================

SyscallResult sys_channel_create(u64, u64, u64, u64, u64, u64)
{
    cap::Table *table = get_current_cap_table();
    if (!table)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    i64 channel_id = channel::create();
    if (channel_id < 0)
    {
        return SyscallResult::err(channel_id);
    }

    kobj::Channel *send_ep =
        kobj::Channel::adopt(static_cast<u32>(channel_id), kobj::Channel::ENDPOINT_SEND);
    kobj::Channel *recv_ep =
        kobj::Channel::adopt(static_cast<u32>(channel_id), kobj::Channel::ENDPOINT_RECV);

    if (!send_ep || !recv_ep)
    {
        delete send_ep;
        delete recv_ep;
        channel::close(static_cast<u32>(channel_id));
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    cap::Handle send_handle = table->insert(
        send_ep, cap::Kind::Channel, cap::CAP_WRITE | cap::CAP_TRANSFER | cap::CAP_DERIVE);
    if (send_handle == cap::HANDLE_INVALID)
    {
        delete send_ep;
        delete recv_ep;
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    cap::Handle recv_handle = table->insert(
        recv_ep, cap::Kind::Channel, cap::CAP_READ | cap::CAP_TRANSFER | cap::CAP_DERIVE);
    if (recv_handle == cap::HANDLE_INVALID)
    {
        table->remove(send_handle);
        delete send_ep;
        delete recv_ep;
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    return SyscallResult::ok(static_cast<u64>(send_handle), static_cast<u64>(recv_handle));
}

SyscallResult sys_channel_send(u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64)
{
    cap::Handle handle = static_cast<cap::Handle>(a0);
    const void *data = reinterpret_cast<const void *>(a1);
    u32 size = static_cast<u32>(a2);
    const cap::Handle *handles = reinterpret_cast<const cap::Handle *>(a3);
    u32 handle_count = static_cast<u32>(a4);

    if (!validate_user_read(data, size))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }
    if (handle_count > channel::MAX_HANDLES_PER_MSG)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }
    if (handle_count > 0 && !validate_user_read(handles, handle_count * sizeof(cap::Handle)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    cap::Table *table = get_current_cap_table();
    if (!table)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    cap::Entry *entry = table->get_with_rights(handle, cap::Kind::Channel, cap::CAP_WRITE);
    if (!entry)
    {
        return SyscallResult::err(error::VERR_INVALID_HANDLE);
    }

    kobj::Channel *ch = static_cast<kobj::Channel *>(entry->object);

    i64 result = channel::try_send(ch->id(), data, size, handles, handle_count);
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok(static_cast<u64>(result));
}

SyscallResult sys_channel_recv(u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64)
{
    cap::Handle handle = static_cast<cap::Handle>(a0);
    void *data = reinterpret_cast<void *>(a1);
    u32 size = static_cast<u32>(a2);
    cap::Handle *handles = reinterpret_cast<cap::Handle *>(a3);
    u32 *handle_count = reinterpret_cast<u32 *>(a4);

    if (!validate_user_write(data, size))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    u32 max_handles = 0;
    if (handle_count)
    {
        if (!validate_user_read(handle_count, sizeof(u32)) ||
            !validate_user_write(handle_count, sizeof(u32)))
        {
            return SyscallResult::err(error::VERR_INVALID_ARG);
        }
        max_handles = *handle_count;
    }
    if (max_handles > channel::MAX_HANDLES_PER_MSG)
    {
        max_handles = channel::MAX_HANDLES_PER_MSG;
    }
    if (max_handles > 0 && handles &&
        !validate_user_write(handles, max_handles * sizeof(cap::Handle)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    cap::Table *table = get_current_cap_table();
    if (!table)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    cap::Entry *entry = table->get_with_rights(handle, cap::Kind::Channel, cap::CAP_READ);
    if (!entry)
    {
        return SyscallResult::err(error::VERR_INVALID_HANDLE);
    }

    kobj::Channel *ch = static_cast<kobj::Channel *>(entry->object);

    cap::Handle tmp_handles[channel::MAX_HANDLES_PER_MSG];
    u32 tmp_handle_count = 0;

    i64 result = channel::try_recv(ch->id(), data, size, tmp_handles, &tmp_handle_count);
    if (result < 0)
    {
        return SyscallResult::err(result);
    }

    if (handle_count)
    {
        *handle_count = tmp_handle_count;
    }

    u32 copy_count = (tmp_handle_count > max_handles) ? max_handles : tmp_handle_count;
    if (handles && copy_count > 0)
    {
        for (u32 i = 0; i < copy_count; i++)
        {
            handles[i] = tmp_handles[i];
        }
    }

    return SyscallResult::ok(static_cast<u64>(result), static_cast<u64>(tmp_handle_count));
}

SyscallResult sys_channel_close(u64 a0, u64, u64, u64, u64, u64)
{
    cap::Handle handle = static_cast<cap::Handle>(a0);

    cap::Table *table = get_current_cap_table();
    if (!table)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    cap::Entry *entry = table->get_checked(handle, cap::Kind::Channel);
    if (!entry)
    {
        return SyscallResult::err(error::VERR_INVALID_HANDLE);
    }

    kobj::Channel *ch = static_cast<kobj::Channel *>(entry->object);
    delete ch;

    table->remove(handle);
    return SyscallResult::ok();
}

} // namespace syscall
