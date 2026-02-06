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

#include "../../assign/assign.hpp"
#include "../../cap/handle.hpp"
#include "../../include/constants.hpp"
#include "handlers_internal.hpp"

namespace syscall {

namespace kc = kernel::constants;

/// @brief Bind a name to a directory handle in the assign table.
/// @details Validates the user-supplied name string, then delegates to the
///   assign subsystem to associate the name with the given handle and flags.
SyscallResult sys_assign_set(u64 a0, u64 a1, u64 a2, u64, u64, u64) {
    const char *name = reinterpret_cast<const char *>(a0);
    cap::Handle dir_handle = static_cast<cap::Handle>(a1);
    u32 flags = static_cast<u32>(a2);

    VALIDATE_USER_STRING(name, viper::assign::MAX_ASSIGN_NAME);

    viper::assign::AssignError result = viper::assign::set_from_handle(name, dir_handle, flags);
    if (result != viper::assign::AssignError::OK) {
        return err_code(static_cast<i64>(result));
    }
    return SyscallResult::ok();
}

/// @brief Look up a name in the assign table, returning its handle.
/// @details First checks for a channel binding, then falls back to the
///   standard assign entry. Returns the handle on success or VERR_NOT_FOUND.
SyscallResult sys_assign_get(u64 a0, u64, u64, u64, u64, u64) {
    const char *name = reinterpret_cast<const char *>(a0);

    VALIDATE_USER_STRING(name, viper::assign::MAX_ASSIGN_NAME);

    cap::Handle channel = viper::assign::get_channel(name);
    if (channel != cap::HANDLE_INVALID) {
        return ok_u64(static_cast<u64>(channel));
    }

    cap::Handle handle = viper::assign::get(name);
    if (handle == cap::HANDLE_INVALID) {
        return err_not_found();
    }
    return ok_u64(static_cast<u64>(handle));
}

/// @brief Remove a name binding from the assign table.
SyscallResult sys_assign_remove(u64 a0, u64, u64, u64, u64, u64) {
    const char *name = reinterpret_cast<const char *>(a0);

    VALIDATE_USER_STRING(name, viper::assign::MAX_ASSIGN_NAME);

    viper::assign::AssignError result = viper::assign::remove(name);
    if (result != viper::assign::AssignError::OK) {
        return err_code(static_cast<i64>(result));
    }
    return SyscallResult::ok();
}

/// @brief List all current assign table entries into a user-supplied buffer.
/// @details Validates the output buffer for overflow-safe write access, then
///   copies up to max_count entries. Returns the actual count written.
SyscallResult sys_assign_list(u64 a0, u64 a1, u64, u64, u64, u64) {
    viper::assign::AssignInfo *buf = reinterpret_cast<viper::assign::AssignInfo *>(a0);
    int max_count = static_cast<int>(a1);

    usize byte_size;
    if (max_count > 0) {
        if (__builtin_mul_overflow(
                static_cast<usize>(max_count), sizeof(viper::assign::AssignInfo), &byte_size)) {
            return err_invalid_arg();
        }
        if (!validate_user_write(buf, byte_size)) {
            return err_invalid_arg();
        }
    }

    int count = viper::assign::list(buf, max_count);
    return ok_u64(static_cast<u64>(count));
}

/// @brief Resolve a multi-component path through the assign table.
/// @details Walks the path components, resolving each via the assign table,
///   and returns the final handle or VERR_NOT_FOUND if any component is missing.
SyscallResult sys_assign_resolve(u64 a0, u64 a1, u64, u64, u64, u64) {
    const char *path = reinterpret_cast<const char *>(a0);
    u32 flags = static_cast<u32>(a1);

    VALIDATE_USER_STRING(path, kc::limits::MAX_PATH);

    cap::Handle handle = viper::assign::resolve_path(path, flags);
    if (handle == cap::HANDLE_INVALID) {
        return err_not_found();
    }
    return ok_u64(static_cast<u64>(handle));
}

} // namespace syscall
