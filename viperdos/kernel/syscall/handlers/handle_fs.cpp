//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/syscall/handlers/handle_fs.cpp
// Purpose: Handle-based filesystem syscall handlers (0x80-0x8F).
//
//===----------------------------------------------------------------------===//

#include "../../cap/handle.hpp"
#include "../../cap/rights.hpp"
#include "../../kobj/dir.hpp"
#include "../../kobj/file.hpp"
#include "handlers_internal.hpp"

namespace syscall {

SyscallResult sys_fs_open_root(u64, u64, u64, u64, u64, u64) {
    GET_CAP_TABLE_OR_RETURN();

    kobj::DirObject *dir = kobj::DirObject::create(2);
    if (!dir) {
        return err_out_of_memory();
    }

    cap::Handle h =
        table->insert(dir, cap::Kind::Directory, cap::CAP_READ | cap::CAP_WRITE | cap::CAP_DERIVE);
    if (h == cap::HANDLE_INVALID) {
        delete dir;
        return err_out_of_memory();
    }

    return ok_u64(static_cast<u64>(h));
}

SyscallResult sys_fs_open(u64 a0, u64 a1, u64 a2, u64 a3, u64, u64) {
    cap::Handle dir_handle = static_cast<cap::Handle>(a0);
    const char *name = reinterpret_cast<const char *>(a1);
    usize name_len = static_cast<usize>(a2);
    u32 flags = static_cast<u32>(a3);

    VALIDATE_USER_READ(name, name_len);

    GET_CAP_TABLE_OR_RETURN();
    GET_OBJECT_CHECKED(dir, kobj::DirObject, table, dir_handle, cap::Kind::Directory);

    u64 child_inode = 0;
    u8 child_type = 0;
    if (!dir->lookup(name, name_len, &child_inode, &child_type)) {
        return err_not_found();
    }

    kobj::Object *new_obj = nullptr;
    cap::Kind kind;
    if (child_type == 2) {
        new_obj = kobj::DirObject::create(child_inode);
        kind = cap::Kind::Directory;
    } else {
        new_obj = kobj::FileObject::create(child_inode, flags);
        kind = cap::Kind::File;
    }

    if (!new_obj) {
        return err_out_of_memory();
    }

    cap::Handle h = table->insert(new_obj, kind, cap::CAP_READ | cap::CAP_WRITE);
    if (h == cap::HANDLE_INVALID) {
        delete new_obj;
        return err_out_of_memory();
    }

    return ok_u64(static_cast<u64>(h));
}

SyscallResult sys_io_read(u64 a0, u64 a1, u64 a2, u64, u64, u64) {
    cap::Handle handle = static_cast<cap::Handle>(a0);
    void *buf = reinterpret_cast<void *>(a1);
    usize count = static_cast<usize>(a2);

    VALIDATE_USER_WRITE(buf, count);

    GET_CAP_TABLE_OR_RETURN();
    GET_OBJECT_WITH_RIGHTS(file, kobj::FileObject, table, handle, cap::Kind::File, cap::CAP_READ);

    i64 result = file->read(buf, count);
    if (result < 0) {
        return err_code(result);
    }
    return ok_u64(static_cast<u64>(result));
}

SyscallResult sys_io_write(u64 a0, u64 a1, u64 a2, u64, u64, u64) {
    cap::Handle handle = static_cast<cap::Handle>(a0);
    const void *buf = reinterpret_cast<const void *>(a1);
    usize count = static_cast<usize>(a2);

    VALIDATE_USER_READ(buf, count);

    GET_CAP_TABLE_OR_RETURN();
    GET_OBJECT_WITH_RIGHTS(file, kobj::FileObject, table, handle, cap::Kind::File, cap::CAP_WRITE);

    i64 result = file->write(buf, count);
    if (result < 0) {
        return err_code(result);
    }
    return ok_u64(static_cast<u64>(result));
}

SyscallResult sys_io_seek(u64 a0, u64 a1, u64 a2, u64, u64, u64) {
    cap::Handle handle = static_cast<cap::Handle>(a0);
    i64 offset = static_cast<i64>(a1);
    i32 whence = static_cast<i32>(a2);

    GET_CAP_TABLE_OR_RETURN();
    GET_OBJECT_CHECKED(file, kobj::FileObject, table, handle, cap::Kind::File);

    i64 result = file->seek(offset, whence);
    if (result < 0) {
        return err_code(result);
    }
    return ok_u64(static_cast<u64>(result));
}

SyscallResult sys_fs_read_dir(u64 a0, u64 a1, u64, u64, u64, u64) {
    cap::Handle handle = static_cast<cap::Handle>(a0);
    kobj::FsDirEnt *ent = reinterpret_cast<kobj::FsDirEnt *>(a1);

    VALIDATE_USER_WRITE(ent, sizeof(kobj::FsDirEnt));

    GET_CAP_TABLE_OR_RETURN();
    GET_OBJECT_WITH_RIGHTS(
        dir, kobj::DirObject, table, handle, cap::Kind::Directory, cap::CAP_READ);

    if (!dir->read_next(ent)) {
        return SyscallResult::ok(0);
    }
    return SyscallResult::ok(1);
}

SyscallResult sys_fs_close(u64 a0, u64, u64, u64, u64, u64) {
    cap::Handle handle = static_cast<cap::Handle>(a0);

    GET_CAP_TABLE_OR_RETURN();

    cap::Entry *entry = table->get(handle);
    if (!entry) {
        return err_invalid_handle();
    }

    table->remove(handle);
    return SyscallResult::ok();
}

SyscallResult sys_fs_rewind_dir(u64 a0, u64, u64, u64, u64, u64) {
    cap::Handle handle = static_cast<cap::Handle>(a0);

    GET_CAP_TABLE_OR_RETURN();
    GET_OBJECT_CHECKED(dir, kobj::DirObject, table, handle, cap::Kind::Directory);

    dir->rewind();
    return SyscallResult::ok();
}

} // namespace syscall
