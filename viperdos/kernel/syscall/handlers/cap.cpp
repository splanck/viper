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

SyscallResult sys_cap_get_bound(u64, u64, u64, u64, u64, u64) {
    viper::Viper *v = viper::current();
    if (!v) {
        return err_not_found();
    }

    u32 bounding_set = viper::get_cap_bounding_set(v);
    return ok_u64(static_cast<u64>(bounding_set));
}

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

SyscallResult sys_getrlimit(u64 a0, u64, u64, u64, u64, u64) {
    viper::ResourceLimit resource = static_cast<viper::ResourceLimit>(a0);

    i64 result = viper::get_rlimit(resource);
    if (result < 0) {
        return err_code(result);
    }

    return ok_u64(static_cast<u64>(result));
}

SyscallResult sys_setrlimit(u64 a0, u64 a1, u64, u64, u64, u64) {
    viper::ResourceLimit resource = static_cast<viper::ResourceLimit>(a0);
    u64 new_limit = a1;

    i64 result = viper::set_rlimit(resource, new_limit);
    if (result < 0) {
        return err_code(result);
    }

    return SyscallResult::ok();
}

SyscallResult sys_getrusage(u64 a0, u64, u64, u64, u64, u64) {
    viper::ResourceLimit resource = static_cast<viper::ResourceLimit>(a0);

    i64 result = viper::get_rusage(resource);
    if (result < 0) {
        return err_code(result);
    }

    return ok_u64(static_cast<u64>(result));
}

} // namespace syscall
