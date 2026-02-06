//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/syscall/handlers/cap.cpp
// Purpose: Capability syscall handlers (0x70-0x7F).
//
//===----------------------------------------------------------------------===//

#include "../../cap/handle.hpp"
#include "../../cap/rights.hpp"
#include "../../include/viperdos/cap_info.hpp"
#include "../../viper/viper.hpp"
#include "handlers_internal.hpp"

namespace syscall {

/// @brief Derive a new capability handle with reduced rights from an existing one.
/// @details Generation-based handle validation ensures stale handles are rejected.
///   The new handle can only have a subset of the source handle's rights.
SyscallResult sys_cap_derive(u64 a0, u64 a1, u64, u64, u64, u64) {
    cap::Handle src = static_cast<cap::Handle>(a0);
    cap::Rights new_rights = static_cast<cap::Rights>(a1);

    GET_CAP_TABLE_OR_RETURN();

    cap::Handle new_handle = table->derive(src, new_rights);
    if (new_handle == cap::HANDLE_INVALID) {
        return err_invalid_handle();
    }

    return ok_u64(static_cast<u64>(new_handle));
}

/// @brief Revoke a capability and all handles derived from it.
/// @details Returns the number of handles revoked (including descendants).
///   Uses generation-based validation to detect stale handles.
SyscallResult sys_cap_revoke(u64 a0, u64, u64, u64, u64, u64) {
    cap::Handle handle = static_cast<cap::Handle>(a0);

    GET_CAP_TABLE_OR_RETURN();

    cap::Entry *entry = table->get(handle);
    if (!entry) {
        return err_invalid_handle();
    }

    u32 revoked = table->revoke(handle);
    return ok_u64(static_cast<u64>(revoked));
}

/// @brief Query the kind, rights, and generation of a capability handle.
/// @details Writes a CapInfo structure to the user-supplied output pointer.
///   Generation-based validation rejects handles whose slot has been reused.
SyscallResult sys_cap_query(u64 a0, u64 a1, u64, u64, u64, u64) {
    cap::Handle handle = static_cast<cap::Handle>(a0);
    CapInfo *info = reinterpret_cast<CapInfo *>(a1);

    VALIDATE_USER_WRITE(info, sizeof(CapInfo));

    GET_CAP_TABLE_OR_RETURN();

    cap::Entry *entry = table->get(handle);
    if (!entry) {
        return err_invalid_handle();
    }

    info->handle = handle;
    info->kind = static_cast<u32>(entry->kind);
    info->rights = entry->rights;
    info->generation = entry->generation;
    return SyscallResult::ok();
}

/// @brief List all valid capabilities in the current process's cap table.
/// @details Iterates the cap table, filling the user buffer with handle/kind/rights
///   tuples. Reconstructs full handles using slot index and generation counter.
SyscallResult sys_cap_list(u64 a0, u64 a1, u64, u64, u64, u64) {
    CapListEntry *entries = reinterpret_cast<CapListEntry *>(a0);
    u32 max_entries = static_cast<u32>(a1);

    VALIDATE_USER_WRITE(entries, max_entries * sizeof(CapListEntry));

    GET_CAP_TABLE_OR_RETURN();

    u32 count = 0;
    for (usize i = 0; i < table->capacity() && count < max_entries; i++) {
        cap::Entry *e = table->entry_at(i);
        if (e && e->kind != cap::Kind::Invalid) {
            entries[count].handle = cap::make_handle(static_cast<u32>(i), e->generation);
            entries[count].kind = static_cast<u32>(e->kind);
            entries[count].rights = e->rights;
            count++;
        }
    }
    return ok_u64(static_cast<u64>(count));
}

/// @brief Get the current process's capability bounding set.
/// @details The bounding set limits which rights can appear in new capabilities.
SyscallResult sys_cap_get_bound(u64, u64, u64, u64, u64, u64) {
    viper::Viper *v = viper::current();
    if (!v) {
        return err_not_found();
    }

    u32 bounding_set = viper::get_cap_bounding_set(v);
    return ok_u64(static_cast<u64>(bounding_set));
}

/// @brief Irrevocably drop rights from the capability bounding set.
/// @details Once dropped, these rights cannot be regained by the process.
SyscallResult sys_cap_drop_bound(u64 a0, u64, u64, u64, u64, u64) {
    u32 rights_to_drop = static_cast<u32>(a0);

    viper::Viper *v = viper::current();
    if (!v) {
        return err_not_found();
    }

    i64 result = viper::drop_cap_bounding_set(v, rights_to_drop);
    if (result < 0) {
        return err_code(result);
    }

    return SyscallResult::ok();
}

/// @brief Query the current value of a resource limit for the calling process.
SyscallResult sys_getrlimit(u64 a0, u64, u64, u64, u64, u64) {
    viper::ResourceLimit resource = static_cast<viper::ResourceLimit>(a0);

    i64 result = viper::get_rlimit(resource);
    if (result < 0) {
        return err_code(result);
    }

    return ok_u64(static_cast<u64>(result));
}

/// @brief Set a resource limit for the calling process.
SyscallResult sys_setrlimit(u64 a0, u64 a1, u64, u64, u64, u64) {
    viper::ResourceLimit resource = static_cast<viper::ResourceLimit>(a0);
    u64 new_limit = a1;

    i64 result = viper::set_rlimit(resource, new_limit);
    if (result < 0) {
        return err_code(result);
    }

    return SyscallResult::ok();
}

/// @brief Query the current resource usage for a given resource type.
SyscallResult sys_getrusage(u64 a0, u64, u64, u64, u64, u64) {
    viper::ResourceLimit resource = static_cast<viper::ResourceLimit>(a0);

    i64 result = viper::get_rusage(resource);
    if (result < 0) {
        return err_code(result);
    }

    return ok_u64(static_cast<u64>(result));
}

} // namespace syscall
