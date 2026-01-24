//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/syscall/handlers/assign.cpp
// Purpose: Assign syscall handlers (0xC0-0xCF).
//
//===----------------------------------------------------------------------===//

#include "handlers_internal.hpp"
#include "../../include/constants.hpp"
#include "../../assign/assign.hpp"
#include "../../cap/handle.hpp"

namespace syscall
{

namespace kc = kernel::constants;

SyscallResult sys_assign_set(u64 a0, u64 a1, u64 a2, u64, u64, u64)
{
    const char *name = reinterpret_cast<const char *>(a0);
    cap::Handle dir_handle = static_cast<cap::Handle>(a1);
    u32 flags = static_cast<u32>(a2);

    if (validate_user_string(name, viper::assign::MAX_ASSIGN_NAME) < 0)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    viper::assign::AssignError result = viper::assign::set_from_handle(name, dir_handle, flags);
    if (result != viper::assign::AssignError::OK)
    {
        return SyscallResult::err(static_cast<i64>(result));
    }
    return SyscallResult::ok();
}

SyscallResult sys_assign_get(u64 a0, u64, u64, u64, u64, u64)
{
    const char *name = reinterpret_cast<const char *>(a0);

    if (validate_user_string(name, viper::assign::MAX_ASSIGN_NAME) < 0)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    cap::Handle channel = viper::assign::get_channel(name);
    if (channel != cap::HANDLE_INVALID)
    {
        return SyscallResult::ok(static_cast<u64>(channel));
    }

    cap::Handle handle = viper::assign::get(name);
    if (handle == cap::HANDLE_INVALID)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }
    return SyscallResult::ok(static_cast<u64>(handle));
}

SyscallResult sys_assign_remove(u64 a0, u64, u64, u64, u64, u64)
{
    const char *name = reinterpret_cast<const char *>(a0);

    if (validate_user_string(name, viper::assign::MAX_ASSIGN_NAME) < 0)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    viper::assign::AssignError result = viper::assign::remove(name);
    if (result != viper::assign::AssignError::OK)
    {
        return SyscallResult::err(static_cast<i64>(result));
    }
    return SyscallResult::ok();
}

SyscallResult sys_assign_list(u64 a0, u64 a1, u64, u64, u64, u64)
{
    viper::assign::AssignInfo *buf = reinterpret_cast<viper::assign::AssignInfo *>(a0);
    int max_count = static_cast<int>(a1);

    usize byte_size;
    if (max_count > 0)
    {
        if (__builtin_mul_overflow(static_cast<usize>(max_count),
                                   sizeof(viper::assign::AssignInfo), &byte_size))
        {
            return SyscallResult::err(error::VERR_INVALID_ARG);
        }
        if (!validate_user_write(buf, byte_size))
        {
            return SyscallResult::err(error::VERR_INVALID_ARG);
        }
    }

    int count = viper::assign::list(buf, max_count);
    return SyscallResult::ok(static_cast<u64>(count));
}

SyscallResult sys_assign_resolve(u64 a0, u64 a1, u64, u64, u64, u64)
{
    const char *path = reinterpret_cast<const char *>(a0);
    u32 flags = static_cast<u32>(a1);

    if (validate_user_string(path, kc::limits::MAX_PATH) < 0)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    cap::Handle handle = viper::assign::resolve_path(path, flags);
    if (handle == cap::HANDLE_INVALID)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }
    return SyscallResult::ok(static_cast<u64>(handle));
}

} // namespace syscall
