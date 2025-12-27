/**
 * @file dispatch.cpp
 * @brief Kernel implementation of the AArch64 syscall dispatch table.
 *
 * @details
 * This translation unit implements the syscall dispatcher invoked from the
 * EL1 synchronous exception handler when the CPU executes `svc #0`.
 *
 * ## ViperOS Syscall ABI (AArch64)
 *
 * **Input registers:**
 * - x8: Syscall number (SYS_* constant)
 * - x0-x5: Up to 6 input arguments
 *
 * **Output registers:**
 * - x0: VError code (0 = success, negative = error)
 * - x1: Result value 0 (if syscall produces a result)
 * - x2: Result value 1 (if syscall produces multiple results)
 * - x3: Result value 2 (if syscall produces multiple results)
 *
 * This convention ensures error checking is always `if (x0 != 0)` and results
 * are in consistent registers x1-x3.
 */
#include "dispatch.hpp"
#include "../../include/viperos/cap_info.hpp"
#include "../../include/viperos/fs_types.hpp"
#include "../../include/viperos/mem_info.hpp"
#include "../arch/aarch64/timer.hpp"
#include "../assign/assign.hpp"
#include "../cap/handle.hpp"
#include "../cap/rights.hpp"
#include "../cap/table.hpp"
#include "../console/gcon.hpp"
#include "../console/serial.hpp"
#include "../drivers/virtio/input.hpp"
#include "../fs/vfs/vfs.hpp"
#include "../fs/viperfs/viperfs.hpp"
#include "../include/error.hpp"
#include "../include/syscall_nums.hpp"
#include "../input/input.hpp"
#include "../ipc/channel.hpp"
#include "../ipc/poll.hpp"
#include "../ipc/pollset.hpp"
#include "../kobj/dir.hpp"
#include "../kobj/file.hpp"
#include "../lib/spinlock.hpp"
#include "../loader/loader.hpp"
#include "../mm/pmm.hpp"
#include "../net/dns/dns.hpp"
#include "../net/ip/tcp.hpp"
#include "../net/network.hpp"
#include "../net/tls/tls.hpp"
#include "../sched/scheduler.hpp"
#include "../sched/task.hpp"
#include "../viper/viper.hpp"

namespace syscall
{

// =============================================================================
// User Pointer Validation
// =============================================================================
// These utilities validate that user-provided pointers are safe to access.
// Currently ViperOS runs all code in EL1 with identity mapping, but this
// infrastructure is essential for when user mode (EL0) is implemented.

/**
 * @brief Validate a user-provided pointer for reading.
 *
 * @details
 * Checks that the pointer:
 * - Is not null (unless null_ok is true)
 * - The range [ptr, ptr+size) doesn't overflow
 * - Points to valid accessible memory (when user mode is implemented)
 *
 * @param ptr User-provided pointer.
 * @param size Size of the buffer in bytes.
 * @param null_ok Whether null is acceptable.
 * @return true if the pointer is valid for reading.
 */
static bool validate_user_read(const void *ptr, usize size, bool null_ok = false)
{
    if (!ptr)
    {
        return null_ok && size == 0;
    }

    // Check for overflow
    u64 addr = reinterpret_cast<u64>(ptr);
    if (addr + size < addr)
    {
        return false; // Overflow
    }

    // TODO: When user mode is implemented, check:
    // 1. Address is in user-space range
    // 2. Memory is mapped and readable
    // For now, accept all non-null, non-overflow pointers

    return true;
}

/**
 * @brief Validate a user-provided pointer for writing.
 *
 * @details
 * Same checks as validate_user_read, plus ensures the memory is writable.
 *
 * @param ptr User-provided pointer.
 * @param size Size of the buffer in bytes.
 * @param null_ok Whether null is acceptable.
 * @return true if the pointer is valid for writing.
 */
static bool validate_user_write(void *ptr, usize size, bool null_ok = false)
{
    if (!ptr)
    {
        return null_ok && size == 0;
    }

    // Check for overflow
    u64 addr = reinterpret_cast<u64>(ptr);
    if (addr + size < addr)
    {
        return false; // Overflow
    }

    // TODO: When user mode is implemented, check:
    // 1. Address is in user-space range
    // 2. Memory is mapped and writable
    // For now, accept all non-null, non-overflow pointers

    return true;
}

/**
 * @brief Validate a user-provided string.
 *
 * @details
 * Checks that the string is not null and is null-terminated within
 * a reasonable length. Returns the string length if valid.
 *
 * @param str User-provided string.
 * @param max_len Maximum allowed length (not including null terminator).
 * @return String length, or -1 if invalid.
 */
static i64 validate_user_string(const char *str, usize max_len)
{
    if (!str)
    {
        return -1;
    }

    // TODO: When user mode is implemented, validate memory access
    // For now, just find the null terminator within max_len

    for (usize i = 0; i <= max_len; i++)
    {
        if (str[i] == '\0')
        {
            return static_cast<i64>(i);
        }
    }

    return -1; // No null terminator within max_len
}

// Task syscalls
/**
 * @brief Implementation of `SYS_TASK_YIELD`.
 *
 * @details
 * Yields the current task's execution to the scheduler. This is typically used
 * by cooperative user code or by kernel test tasks to allow other work to run.
 *
 * @return @ref error::VOK on success.
 */
static i64 sys_task_yield()
{
    task::yield();
    return error::VOK;
}

/**
 * @brief Implementation of `SYS_TASK_EXIT`.
 *
 * @details
 * Terminates the current task with the provided exit code. This call is not
 * expected to return; if it does, the caller treats it as success and resumes.
 *
 * @param code Exit status provided by the caller.
 * @return @ref error::VOK (unreachable if task exit is truly non-returning).
 */
static i64 sys_task_exit(i64 code)
{
    task::exit(static_cast<i32>(code));
    // Never returns
    return error::VOK;
}

/**
 * @brief Implementation of `SYS_TASK_CURRENT`.
 *
 * @details
 * Returns the numeric task identifier of the current task as maintained by the
 * scheduler/task subsystem.
 *
 * @return Task ID on success, or @ref error::VERR_TASK_NOT_FOUND if no current
 *         task is active.
 */
static i64 sys_task_current()
{
    task::Task *t = task::current();
    if (t)
    {
        return static_cast<i64>(t->id);
    }
    return error::VERR_TASK_NOT_FOUND;
}

// Debug syscalls
/**
 * @brief Implementation of `SYS_DEBUG_PRINT`.
 *
 * @details
 * Writes a caller-provided NUL-terminated string to the kernel's debug output
 * sinks. During bring-up this is used heavily for tracing and diagnostics and
 * is typically forwarded to both serial and the graphics console when
 * available.
 *
 * @param msg Pointer to a NUL-terminated message string.
 * @return @ref error::VOK.
 */
static i64 sys_debug_print(const char *msg)
{
    // Validate user string (allow null for no-op)
    if (msg)
    {
        if (validate_user_string(msg, 4096) < 0)
        {
            return error::VERR_INVALID_ARG;
        }
        serial::puts(msg);
        if (gcon::is_available())
        {
            gcon::puts(msg);
        }
    }
    return error::VOK;
}

// =============================================================================
// Channel syscalls (capability-aware with legacy fallback)
// =============================================================================

/**
 * @brief Implementation of `SYS_CHANNEL_CREATE`.
 *
 * @details
 * Allocates a new channel IPC object. If a capability table exists, creates
 * handles for both endpoints (send/recv). Otherwise falls back to legacy
 * ID-based API.
 *
 * @param out_send Output for send handle (via res0).
 * @param out_recv Output for recv handle (via res1).
 * @return VOK on success with handles in out params, or negative error code.
 */
static i64 sys_channel_create_cap(u64 &out_send, u64 &out_recv)
{
    cap::Table *ct = viper::current_cap_table();
    if (!ct)
    {
        // Legacy: no capability table, use ID-based API
        i64 result = channel::create();
        if (result < 0)
        {
            return result;
        }
        out_send = static_cast<u64>(result);
        out_recv = static_cast<u64>(result); // Same ID for legacy
        return error::VOK;
    }

    // Create the channel with both endpoint handles
    channel::ChannelPair pair;
    i64 result = channel::create(&pair);
    if (result < 0)
    {
        return result;
    }

    out_send = static_cast<u64>(pair.send_handle);
    out_recv = static_cast<u64>(pair.recv_handle);
    return error::VOK;
}

/**
 * @brief Implementation of `SYS_CHANNEL_SEND` (capability-aware, non-blocking).
 *
 * @details
 * Attempts to enqueue a message into the channel. If capability table exists,
 * looks up the handle and validates CAP_WRITE rights. Supports handle transfer.
 *
 * @param ch_handle Channel handle or ID.
 * @param data Pointer to message bytes.
 * @param size Length of the message in bytes.
 * @param handles Optional handles to transfer.
 * @param handle_count Number of handles to transfer.
 * @return Result code.
 */
static i64 sys_channel_send_cap(cap::Handle ch_handle, const void *data, u32 size,
                                const cap::Handle *handles, u32 handle_count)
{
    // Validate user pointer
    if (!validate_user_read(data, size, size == 0))
    {
        return error::VERR_INVALID_ARG;
    }

    cap::Table *ct = viper::current_cap_table();
    if (!ct)
    {
        // Legacy: no capability table, use ID directly (no handle transfer)
        return channel::try_send(ch_handle, data, size);
    }

    // Look up the channel handle (requires CAP_WRITE for send endpoint)
    cap::Entry *entry = ct->get_with_rights(ch_handle, cap::Kind::Channel, cap::CAP_WRITE);
    if (!entry)
    {
        return error::VERR_INVALID_HANDLE;
    }

    channel::Channel *ch = static_cast<channel::Channel *>(entry->object);
    return channel::try_send(ch, data, size, handles, handle_count);
}

/**
 * @brief Implementation of `SYS_CHANNEL_RECV` (capability-aware, non-blocking).
 *
 * @details
 * Attempts to dequeue a message. If capability table exists, looks up handle
 * and validates CAP_READ rights. Supports receiving transferred handles.
 *
 * @param ch_handle Channel handle or ID.
 * @param buffer Destination buffer for message bytes.
 * @param buffer_size Size of the destination buffer.
 * @param out_handles Optional buffer for received handles.
 * @param out_handle_count Output count of received handles.
 * @return Message size or error code.
 */
static i64 sys_channel_recv_cap(cap::Handle ch_handle, void *buffer, u32 buffer_size,
                                cap::Handle *out_handles, u32 *out_handle_count)
{
    // Validate user pointer
    if (!validate_user_write(buffer, buffer_size, buffer_size == 0))
    {
        return error::VERR_INVALID_ARG;
    }

    cap::Table *ct = viper::current_cap_table();
    if (!ct)
    {
        // Legacy: no capability table, use ID directly (no handle transfer)
        return channel::try_recv(ch_handle, buffer, buffer_size);
    }

    // Look up the channel handle (requires CAP_READ for recv endpoint)
    cap::Entry *entry = ct->get_with_rights(ch_handle, cap::Kind::Channel, cap::CAP_READ);
    if (!entry)
    {
        return error::VERR_INVALID_HANDLE;
    }

    channel::Channel *ch = static_cast<channel::Channel *>(entry->object);
    return channel::try_recv(ch, buffer, buffer_size, out_handles, out_handle_count);
}

/**
 * @brief Implementation of `SYS_CHANNEL_CLOSE` (capability-aware).
 *
 * @details
 * Closes the channel endpoint. If capability table exists, removes the handle
 * from the table and decrements the channel reference count.
 *
 * @param ch_handle Channel handle or ID.
 * @return Result code.
 */
static i64 sys_channel_close_cap(cap::Handle ch_handle)
{
    cap::Table *ct = viper::current_cap_table();
    if (!ct)
    {
        // Legacy: no capability table, use ID directly
        return channel::close(ch_handle);
    }

    // Look up the channel handle
    cap::Entry *entry = ct->get_checked(ch_handle, cap::Kind::Channel);
    if (!entry)
    {
        return error::VERR_INVALID_HANDLE;
    }

    channel::Channel *ch = static_cast<channel::Channel *>(entry->object);

    // Determine if this is send or recv endpoint based on rights
    bool is_send = cap::has_rights(entry->rights, cap::CAP_WRITE);

    // Close the endpoint (decrements ref count)
    i64 result = channel::close_endpoint(ch, is_send);

    // Remove from cap_table
    ct->remove(ch_handle);

    return result;
}

// =============================================================================
// Poll syscalls (capability-aware with legacy fallback)
// =============================================================================

/**
 * @brief Implementation of `SYS_POLL_CREATE` (capability-aware).
 */
static i64 sys_poll_create_cap(u64 &out_handle)
{
    cap::Table *ct = viper::current_cap_table();
    if (!ct)
    {
        // Legacy: no capability table, use ID-based API
        i64 result = pollset::create();
        if (result >= 0)
        {
            out_handle = static_cast<u64>(result);
            return error::VOK;
        }
        return result;
    }

    // Create the pollset
    i64 result = pollset::create();
    if (result < 0)
    {
        return result;
    }

    // Get the pollset pointer and insert into cap_table
    pollset::PollSet *ps = pollset::get(static_cast<u32>(result));
    if (!ps)
    {
        return error::VERR_NOT_FOUND;
    }

    cap::Handle h = ct->insert(ps, cap::Kind::Poll, cap::CAP_READ | cap::CAP_WRITE);
    if (h == cap::HANDLE_INVALID)
    {
        pollset::destroy(static_cast<u32>(result));
        return error::VERR_OUT_OF_MEMORY;
    }

    out_handle = static_cast<u64>(h);
    return error::VOK;
}

/**
 * @brief Implementation of `SYS_POLL_ADD` (capability-aware).
 */
static i64 sys_poll_add_cap(cap::Handle poll_handle, u32 target_handle, u32 mask)
{
    cap::Table *ct = viper::current_cap_table();
    if (!ct)
    {
        // Legacy: no capability table, use ID directly
        return pollset::add(poll_handle, target_handle, mask);
    }

    // Look up the pollset handle
    cap::Entry *entry = ct->get_with_rights(poll_handle, cap::Kind::Poll, cap::CAP_WRITE);
    if (!entry)
    {
        return error::VERR_INVALID_HANDLE;
    }

    pollset::PollSet *ps = static_cast<pollset::PollSet *>(entry->object);
    return pollset::add(ps->id, target_handle, mask);
}

/**
 * @brief Implementation of `SYS_POLL_REMOVE` (capability-aware).
 */
static i64 sys_poll_remove_cap(cap::Handle poll_handle, u32 target_handle)
{
    cap::Table *ct = viper::current_cap_table();
    if (!ct)
    {
        // Legacy: no capability table, use ID directly
        return pollset::remove(poll_handle, target_handle);
    }

    // Look up the pollset handle
    cap::Entry *entry = ct->get_with_rights(poll_handle, cap::Kind::Poll, cap::CAP_WRITE);
    if (!entry)
    {
        return error::VERR_INVALID_HANDLE;
    }

    pollset::PollSet *ps = static_cast<pollset::PollSet *>(entry->object);
    return pollset::remove(ps->id, target_handle);
}

/**
 * @brief Implementation of `SYS_POLL_WAIT` (capability-aware).
 */
static i64 sys_poll_wait_cap(cap::Handle poll_handle, poll::PollEvent *out_events,
                             u32 max_events, i64 timeout_ms)
{
    cap::Table *ct = viper::current_cap_table();
    if (!ct)
    {
        // Legacy: no capability table, use ID directly
        return pollset::wait(poll_handle, out_events, max_events, timeout_ms);
    }

    // Look up the pollset handle
    cap::Entry *entry = ct->get_with_rights(poll_handle, cap::Kind::Poll, cap::CAP_READ);
    if (!entry)
    {
        return error::VERR_INVALID_HANDLE;
    }

    pollset::PollSet *ps = static_cast<pollset::PollSet *>(entry->object);
    return pollset::wait(ps->id, out_events, max_events, timeout_ms);
}

// =============================================================================
// Capability syscalls (0x70-0x73)
// =============================================================================

/**
 * @brief Implementation of `SYS_CAP_DERIVE` - derive handle with reduced rights.
 */
static i64 sys_cap_derive(cap::Handle parent_handle, cap::Rights new_rights, u64 &out_handle)
{
    cap::Table *ct = viper::current_cap_table();
    if (!ct)
    {
        return error::VERR_NOT_SUPPORTED;
    }

    cap::Handle derived = ct->derive(parent_handle, new_rights);
    if (derived == cap::HANDLE_INVALID)
    {
        return error::VERR_PERMISSION;
    }

    out_handle = static_cast<u64>(derived);
    return error::VOK;
}

/**
 * @brief Implementation of `SYS_CAP_REVOKE` - revoke/remove a handle.
 */
static i64 sys_cap_revoke(cap::Handle handle)
{
    cap::Table *ct = viper::current_cap_table();
    if (!ct)
    {
        return error::VERR_NOT_SUPPORTED;
    }

    cap::Entry *entry = ct->get(handle);
    if (!entry)
    {
        return error::VERR_INVALID_HANDLE;
    }

    ct->remove(handle);
    return error::VOK;
}

/**
 * @brief Implementation of `SYS_CAP_QUERY` - query handle info.
 */
static i64 sys_cap_query(cap::Handle handle, CapInfo *info_out)
{
    cap::Table *ct = viper::current_cap_table();
    if (!ct)
    {
        return error::VERR_NOT_SUPPORTED;
    }

    if (!validate_user_write(info_out, sizeof(CapInfo)))
    {
        return error::VERR_INVALID_ARG;
    }

    cap::Entry *entry = ct->get(handle);
    if (!entry)
    {
        return error::VERR_INVALID_HANDLE;
    }

    info_out->handle = handle;
    info_out->kind = static_cast<unsigned short>(entry->kind);
    info_out->generation = entry->generation;
    info_out->_reserved = 0;
    info_out->rights = entry->rights;

    return error::VOK;
}

/**
 * @brief Implementation of `SYS_CAP_LIST` - list all capabilities.
 */
static i64 sys_cap_list(CapListEntry *buffer, u32 max_count, u64 &out_count)
{
    cap::Table *ct = viper::current_cap_table();
    if (!ct)
    {
        return error::VERR_NOT_SUPPORTED;
    }

    if (!buffer || max_count == 0)
    {
        // Return count only
        out_count = static_cast<u64>(ct->count());
        return error::VOK;
    }

    if (!validate_user_write(buffer, max_count * sizeof(CapListEntry)))
    {
        return error::VERR_INVALID_ARG;
    }

    // Enumerate valid entries
    u32 count = 0;
    usize capacity = ct->capacity();

    for (usize idx = 0; idx < capacity && count < max_count; idx++)
    {
        cap::Entry *entry = ct->entry_at(idx);
        if (entry && entry->kind != cap::Kind::Invalid)
        {
            cap::Handle h = cap::make_handle(static_cast<u32>(idx), entry->generation);
            buffer[count].handle = h;
            buffer[count].kind = static_cast<unsigned short>(entry->kind);
            buffer[count].generation = entry->generation;
            buffer[count]._reserved = 0;
            buffer[count].rights = entry->rights;
            count++;
        }
    }

    out_count = static_cast<u64>(count);
    return error::VOK;
}

// =============================================================================
// Handle-based Filesystem syscalls (0x80-0x87)
// =============================================================================

/**
 * @brief Implementation of `SYS_FS_OPEN_ROOT` - get handle to root directory.
 */
static i64 sys_fs_open_root(u64 &out_handle)
{
    cap::Table *ct = viper::current_cap_table();
    if (!ct)
    {
        return error::VERR_NOT_SUPPORTED;
    }

    // Create directory object for root
    kobj::DirObject *dir = kobj::DirObject::create(fs::viperfs::ROOT_INODE);
    if (!dir)
    {
        return error::VERR_NOT_FOUND;
    }

    // Insert into cap_table with read/traverse rights
    cap::Rights rights = cap::CAP_READ | cap::CAP_TRAVERSE;
    cap::Handle h = ct->insert(dir, cap::Kind::Directory, rights);
    if (h == cap::HANDLE_INVALID)
    {
        delete dir;
        return error::VERR_OUT_OF_MEMORY;
    }

    out_handle = static_cast<u64>(h);
    return error::VOK;
}

/**
 * @brief Implementation of `SYS_FS_OPEN` - open file/dir relative to dir handle.
 */
static i64 sys_fs_open(cap::Handle dir_h, const char *name, usize name_len, u32 flags,
                       u64 &out_handle)
{
    cap::Table *ct = viper::current_cap_table();
    if (!ct)
    {
        return error::VERR_NOT_SUPPORTED;
    }

    if (!validate_user_read(name, name_len) || name_len == 0)
    {
        return error::VERR_INVALID_ARG;
    }

    // Look up the directory handle
    cap::Entry *entry = ct->get_checked(dir_h, cap::Kind::Directory);
    if (!entry)
    {
        return error::VERR_INVALID_HANDLE;
    }

    kobj::DirObject *dir = static_cast<kobj::DirObject *>(entry->object);

    // Lookup the child entry
    u64 child_inode;
    u8 child_type;
    if (!dir->lookup(name, name_len, &child_inode, &child_type))
    {
        return error::VERR_NOT_FOUND;
    }

    // Determine if it's a file or directory
    if (child_type == fs::viperfs::file_type::DIR)
    {
        // Create directory object
        kobj::DirObject *child_dir = kobj::DirObject::create(child_inode);
        if (!child_dir)
        {
            return error::VERR_OUT_OF_MEMORY;
        }

        cap::Rights rights = cap::CAP_READ | cap::CAP_TRAVERSE;
        cap::Handle h = ct->insert(child_dir, cap::Kind::Directory, rights);
        if (h == cap::HANDLE_INVALID)
        {
            delete child_dir;
            return error::VERR_OUT_OF_MEMORY;
        }

        out_handle = static_cast<u64>(h);
    }
    else
    {
        // Create file object
        kobj::FileObject *file = kobj::FileObject::create(child_inode, flags);
        if (!file)
        {
            return error::VERR_OUT_OF_MEMORY;
        }

        // Determine rights based on open flags
        u32 access = flags & 0x3;
        cap::Rights rights = cap::CAP_NONE;
        if (access == kobj::file_flags::O_RDONLY || access == kobj::file_flags::O_RDWR)
        {
            rights = rights | cap::CAP_READ;
        }
        if (access == kobj::file_flags::O_WRONLY || access == kobj::file_flags::O_RDWR)
        {
            rights = rights | cap::CAP_WRITE;
        }

        cap::Handle h = ct->insert(file, cap::Kind::File, rights);
        if (h == cap::HANDLE_INVALID)
        {
            delete file;
            return error::VERR_OUT_OF_MEMORY;
        }

        out_handle = static_cast<u64>(h);
    }
    return error::VOK;
}

/**
 * @brief Implementation of `SYS_IO_READ` - read from file handle.
 */
static i64 sys_io_read(cap::Handle file_h, void *buffer, usize len)
{
    cap::Table *ct = viper::current_cap_table();
    if (!ct)
    {
        return error::VERR_NOT_SUPPORTED;
    }

    if (!validate_user_write(buffer, len))
    {
        return error::VERR_INVALID_ARG;
    }

    // Look up the file handle (requires CAP_READ)
    cap::Entry *entry = ct->get_with_rights(file_h, cap::Kind::File, cap::CAP_READ);
    if (!entry)
    {
        return error::VERR_INVALID_HANDLE;
    }

    kobj::FileObject *file = static_cast<kobj::FileObject *>(entry->object);
    return file->read(buffer, len);
}

/**
 * @brief Implementation of `SYS_IO_WRITE` - write to file handle.
 */
static i64 sys_io_write(cap::Handle file_h, const void *buffer, usize len)
{
    cap::Table *ct = viper::current_cap_table();
    if (!ct)
    {
        return error::VERR_NOT_SUPPORTED;
    }

    if (!validate_user_read(buffer, len))
    {
        return error::VERR_INVALID_ARG;
    }

    // Look up the file handle (requires CAP_WRITE)
    cap::Entry *entry = ct->get_with_rights(file_h, cap::Kind::File, cap::CAP_WRITE);
    if (!entry)
    {
        return error::VERR_INVALID_HANDLE;
    }

    kobj::FileObject *file = static_cast<kobj::FileObject *>(entry->object);
    return file->write(buffer, len);
}

/**
 * @brief Implementation of `SYS_IO_SEEK` - seek within file handle.
 */
static i64 sys_io_seek(cap::Handle file_h, i64 offset, i32 whence)
{
    cap::Table *ct = viper::current_cap_table();
    if (!ct)
    {
        return error::VERR_NOT_SUPPORTED;
    }

    // Look up the file handle (no special rights needed for seek)
    cap::Entry *entry = ct->get_checked(file_h, cap::Kind::File);
    if (!entry)
    {
        return error::VERR_INVALID_HANDLE;
    }

    kobj::FileObject *file = static_cast<kobj::FileObject *>(entry->object);
    return file->seek(offset, whence);
}

/**
 * @brief Implementation of `SYS_FS_READ_DIR` - read next directory entry.
 */
static i64 sys_fs_read_dir(cap::Handle dir_h, kobj::FsDirEnt *out_ent)
{
    cap::Table *ct = viper::current_cap_table();
    if (!ct)
    {
        return error::VERR_NOT_SUPPORTED;
    }

    if (!validate_user_write(out_ent, sizeof(kobj::FsDirEnt)))
    {
        return error::VERR_INVALID_ARG;
    }

    // Look up the directory handle (requires CAP_READ)
    cap::Entry *entry = ct->get_with_rights(dir_h, cap::Kind::Directory, cap::CAP_READ);
    if (!entry)
    {
        return error::VERR_INVALID_HANDLE;
    }

    kobj::DirObject *dir = static_cast<kobj::DirObject *>(entry->object);
    bool has_more = dir->read_next(out_ent);
    return has_more ? 1 : 0;
}

/**
 * @brief Implementation of `SYS_FS_CLOSE` - close file or directory handle.
 */
static i64 sys_fs_close(cap::Handle h)
{
    cap::Table *ct = viper::current_cap_table();
    if (!ct)
    {
        return error::VERR_NOT_SUPPORTED;
    }

    // Look up the handle (could be File or Directory)
    cap::Entry *entry = ct->get(h);
    if (!entry)
    {
        return error::VERR_INVALID_HANDLE;
    }

    // Check that it's a file or directory
    if (entry->kind != cap::Kind::File && entry->kind != cap::Kind::Directory)
    {
        return error::VERR_INVALID_HANDLE;
    }

    // Release the object
    kobj::Object *obj = static_cast<kobj::Object *>(entry->object);
    kobj::release(obj);

    // Remove from cap_table
    ct->remove(h);

    return error::VOK;
}

/**
 * @brief Implementation of `SYS_FS_REWIND_DIR` - reset directory enumeration.
 */
static i64 sys_fs_rewind_dir(cap::Handle dir_h)
{
    cap::Table *ct = viper::current_cap_table();
    if (!ct)
    {
        return error::VERR_NOT_SUPPORTED;
    }

    // Look up the directory handle
    cap::Entry *entry = ct->get_checked(dir_h, cap::Kind::Directory);
    if (!entry)
    {
        return error::VERR_INVALID_HANDLE;
    }

    kobj::DirObject *dir = static_cast<kobj::DirObject *>(entry->object);
    dir->rewind();
    return error::VOK;
}

// =============================================================================
// Task syscalls
// =============================================================================

/**
 * @brief Implementation of `SYS_TASK_LIST` - enumerate running tasks.
 */
static i64 sys_task_list(TaskInfo *buffer, u32 max_count)
{
    if (!validate_user_write(buffer, max_count * sizeof(TaskInfo)))
    {
        return error::VERR_INVALID_ARG;
    }
    return static_cast<i64>(task::list_tasks(buffer, max_count));
}

/**
 * @brief Implementation of `SYS_TASK_SET_PRIORITY` - set task priority.
 *
 * @details
 * User processes can only lower their own priority (increase the numeric value).
 * They cannot raise their priority above the default (128) unless they started
 * with a higher priority. This prevents priority inversion attacks.
 *
 * @param task_id Task ID to modify (0 = current task).
 * @param priority New priority (0=highest, 255=lowest).
 * @return 0 on success, negative error code on failure.
 */
static i64 sys_task_set_priority(u32 task_id, u8 priority)
{
    task::Task *t = nullptr;

    if (task_id == 0)
    {
        // Modify current task
        t = task::current();
    }
    else
    {
        // Modify specific task (only allowed for own process's tasks in future)
        t = task::get_by_id(task_id);
    }

    if (!t)
    {
        return error::VERR_NOT_FOUND;
    }

    // User tasks can only lower priority (increase numeric value)
    // This prevents priority escalation attacks
    if (t->flags & task::TASK_FLAG_USER)
    {
        // User tasks cannot set priority above default (128)
        if (priority < task::PRIORITY_DEFAULT)
        {
            priority = task::PRIORITY_DEFAULT;
        }
    }

    i32 result = task::set_priority(t, priority);
    return result == 0 ? error::VOK : error::VERR_INVALID_ARG;
}

/**
 * @brief Implementation of `SYS_TASK_GET_PRIORITY` - get task priority.
 *
 * @param task_id Task ID to query (0 = current task).
 * @return Priority value (0-255), or negative error code.
 */
static i64 sys_task_get_priority(u32 task_id)
{
    task::Task *t = nullptr;

    if (task_id == 0)
    {
        t = task::current();
    }
    else
    {
        t = task::get_by_id(task_id);
    }

    if (!t)
    {
        return error::VERR_NOT_FOUND;
    }

    return static_cast<i64>(task::get_priority(t));
}

/**
 * @brief Implementation of `SYS_TASK_SPAWN` - spawn a new process.
 *
 * @details
 * Spawns a new process from an ELF file on the filesystem. The new process
 * runs independently with its own address space and is scheduled concurrently.
 *
 * @param path User pointer to NUL-terminated path to ELF executable.
 * @param name User pointer to NUL-terminated process name (for debugging).
 * @param out_pid Output: process ID of the new process.
 * @param out_tid Output: task ID of the main thread.
 * @return 0 on success, negative error code on failure.
 */
static i64 sys_task_spawn(const char *path, const char *name, u64 &out_pid, u64 &out_tid)
{
    // Validate path string
    i64 path_len = validate_user_string(path, fs::vfs::MAX_PATH);
    if (path_len < 0)
    {
        return error::VERR_INVALID_ARG;
    }

    // Validate name string (optional, use path basename if null)
    const char *proc_name = name;
    if (name)
    {
        i64 name_len = validate_user_string(name, 32);
        if (name_len < 0)
        {
            return error::VERR_INVALID_ARG;
        }
    }
    else
    {
        // Use path as name if no name provided
        proc_name = path;
    }

    // Get current process as parent
    viper::Viper *parent = viper::current();

    // Spawn the process
    loader::SpawnResult result = loader::spawn_process(path, proc_name, parent);

    if (!result.success)
    {
        return error::VERR_IO;
    }

    out_pid = result.viper->id;
    out_tid = result.task_id;

    return error::VOK;
}

// Time/poll syscalls
/**
 * @brief Implementation of `SYS_TIME_NOW`.
 *
 * @details
 * Returns the current kernel time in milliseconds as defined by the poll/timer
 * subsystem.
 *
 * @return Milliseconds since boot (or other monotonic epoch) as i64.
 */
static i64 sys_time_now()
{
    return static_cast<i64>(poll::time_now_ms());
}

/**
 * @brief Implementation of `SYS_SLEEP`.
 *
 * @details
 * Sleeps the calling task for `ms` milliseconds using the poll/timer subsystem.
 * This is one of the syscalls that may block.
 *
 * @param ms Number of milliseconds to sleep.
 * @return Result code.
 */
static i64 sys_sleep(u64 ms)
{
    return poll::sleep_ms(ms);
}

// Socket syscalls
/**
 * @brief Implementation of `SYS_SOCKET_CREATE`.
 *
 * @details
 * Creates a TCP socket using the network stack and returns a socket handle.
 *
 * @return Socket handle on success, or negative error code.
 */
static i64 sys_socket_create()
{
    return net::tcp::socket_create();
}

/**
 * @brief Implementation of `SYS_SOCKET_CONNECT`.
 *
 * @details
 * Connects a TCP socket to a remote endpoint. The IP address is passed in
 * packed `0xAABBCCDD` form (network byte order) from user space.
 *
 * @param sock Socket handle.
 * @param ip_packed Packed IPv4 address.
 * @param port Remote TCP port.
 * @return `0` on success or negative error code on failure.
 */
static i64 sys_socket_connect(i32 sock, u32 ip_packed, u16 port)
{
    // IP is passed as packed u32 (network byte order)
    net::Ipv4Addr addr;
    addr.bytes[0] = (ip_packed >> 24) & 0xFF;
    addr.bytes[1] = (ip_packed >> 16) & 0xFF;
    addr.bytes[2] = (ip_packed >> 8) & 0xFF;
    addr.bytes[3] = ip_packed & 0xFF;

    if (net::tcp::socket_connect(sock, addr, port))
    {
        return 0;
    }
    return error::VERR_TIMEOUT;
}

/**
 * @brief Implementation of `SYS_SOCKET_SEND`.
 *
 * @details
 * Sends bytes on a connected TCP socket.
 *
 * @param sock Socket handle.
 * @param data Pointer to bytes to send.
 * @param len Number of bytes to send.
 * @return Non-negative on success (often bytes sent), negative error code on failure.
 */
static i64 sys_socket_send(i32 sock, const void *data, usize len)
{
    // Validate user pointer
    if (!validate_user_read(data, len, len == 0))
    {
        return error::VERR_INVALID_ARG;
    }

    return net::tcp::socket_send(sock, data, len);
}

/**
 * @brief Implementation of `SYS_SOCKET_RECV`.
 *
 * @details
 * Polls the network stack to bring in any pending packets and then attempts to
 * read bytes from the socket receive buffer.
 *
 * @param sock Socket handle.
 * @param buffer Destination buffer.
 * @param max_len Maximum number of bytes to receive.
 * @return Non-negative on success (often bytes received), negative error code on failure.
 */
static i64 sys_socket_recv(i32 sock, void *buffer, usize max_len)
{
    // Validate user pointer
    if (!validate_user_write(buffer, max_len, max_len == 0))
    {
        return error::VERR_INVALID_ARG;
    }

    // Poll network first
    net::network_poll();
    return net::tcp::socket_recv(sock, buffer, max_len);
}

/**
 * @brief Implementation of `SYS_SOCKET_CLOSE`.
 *
 * @details
 * Closes the socket and releases any associated resources.
 *
 * @param sock Socket handle.
 * @return `0` on success.
 */
static i64 sys_socket_close(i32 sock)
{
    net::tcp::socket_close(sock);
    return 0;
}

/**
 * @brief Implementation of `SYS_DNS_RESOLVE`.
 *
 * @details
 * Resolves a hostname to an IPv4 address using the kernel DNS client and
 * writes the packed result to `ip_out`.
 *
 * @param hostname NUL-terminated hostname string.
 * @param ip_out Output pointer to store packed IPv4 address.
 * @return `0` on success or negative error code on failure.
 */
static i64 sys_dns_resolve(const char *hostname, u32 *ip_out)
{
    // Validate user pointers
    if (validate_user_string(hostname, 256) < 0)
    {
        return error::VERR_INVALID_ARG;
    }
    if (!validate_user_write(ip_out, sizeof(u32)))
    {
        return error::VERR_INVALID_ARG;
    }

    net::Ipv4Addr addr;
    if (!net::dns::resolve(hostname, &addr, 5000))
    {
        return error::VERR_NOT_FOUND;
    }

    // Pack IP address into u32 (network byte order)
    *ip_out = (static_cast<u32>(addr.bytes[0]) << 24) | (static_cast<u32>(addr.bytes[1]) << 16) |
              (static_cast<u32>(addr.bytes[2]) << 8) | static_cast<u32>(addr.bytes[3]);
    return 0;
}

// Assign syscalls (v0.2.0)
/**
 * @brief Implementation of `SYS_ASSIGN_SET`.
 *
 * @details
 * Creates or updates an "assign" mapping from a name to a directory handle.
 * The assign system provides a convenient name→capability indirection used by
 * higher-level components. The directory handle is looked up in the caller's
 * cap_table and the inode is stored in the assign entry.
 *
 * @param name NUL-terminated assign name.
 * @param name_len Length of the name (currently validated for non-zero).
 * @param dir_handle Capability handle representing a directory.
 * @return Result code.
 */
static i64 sys_assign_set(const char *name, usize name_len, u32 dir_handle)
{
    // Validate user string
    if (!validate_user_read(name, name_len) || name_len == 0)
    {
        return error::VERR_INVALID_ARG;
    }
    // Use set_from_handle which looks up the handle in the caller's cap_table
    auto result = viper::assign::set_from_handle(name, dir_handle);
    switch (result)
    {
        case viper::assign::AssignError::OK:
            return error::VOK;
        case viper::assign::AssignError::InvalidHandle:
            return error::VERR_INVALID_HANDLE;
        case viper::assign::AssignError::ReadOnly:
            return error::VERR_PERMISSION;
        default:
            return error::VERR_IO;
    }
}

/**
 * @brief Implementation of `SYS_ASSIGN_GET`.
 *
 * @details
 * Looks up an assign mapping and writes the resolved handle to `handle_out`.
 *
 * @param name NUL-terminated assign name.
 * @param name_len Length of the name.
 * @param handle_out Output pointer for the resolved handle.
 * @return Result code.
 */
static i64 sys_assign_get(const char *name, usize name_len, u32 *handle_out)
{
    // Validate user pointers
    if (!validate_user_read(name, name_len) || name_len == 0)
    {
        return error::VERR_INVALID_ARG;
    }
    if (!validate_user_write(handle_out, sizeof(u32)))
    {
        return error::VERR_INVALID_ARG;
    }

    cap::Handle h = viper::assign::get(name);
    if (h == cap::HANDLE_INVALID)
    {
        return error::VERR_NOT_FOUND;
    }
    *handle_out = h;
    return error::VOK;
}

/**
 * @brief Implementation of `SYS_ASSIGN_REMOVE`.
 *
 * @details
 * Removes an assign mapping by name.
 *
 * @param name NUL-terminated assign name.
 * @param name_len Length of the name.
 * @return Result code.
 */
static i64 sys_assign_remove(const char *name, usize name_len)
{
    // Validate user string
    if (!validate_user_read(name, name_len) || name_len == 0)
    {
        return error::VERR_INVALID_ARG;
    }

    auto result = viper::assign::remove(name);
    switch (result)
    {
        case viper::assign::AssignError::OK:
            return error::VOK;
        case viper::assign::AssignError::NotFound:
            return error::VERR_NOT_FOUND;
        case viper::assign::AssignError::ReadOnly:
            return error::VERR_PERMISSION;
        default:
            return error::VERR_IO;
    }
}

/**
 * @brief Implementation of `SYS_ASSIGN_LIST`.
 *
 * @details
 * Enumerates known assign mappings into the caller-provided buffer.
 *
 * @param buffer Output buffer to receive assign entries.
 * @param max_count Maximum number of entries the buffer can hold.
 * @param count_out Output pointer receiving the number of entries written.
 * @return Result code.
 */
static i64 sys_assign_list(viper::assign::AssignInfo *buffer, usize max_count, usize *count_out)
{
    // Validate user pointers
    if (!validate_user_write(buffer, max_count * sizeof(viper::assign::AssignInfo)))
    {
        return error::VERR_INVALID_ARG;
    }
    if (!validate_user_write(count_out, sizeof(usize)))
    {
        return error::VERR_INVALID_ARG;
    }

    int count = viper::assign::list(buffer, static_cast<int>(max_count));
    *count_out = static_cast<usize>(count);
    return error::VOK;
}

/**
 * @brief Implementation of `SYS_ASSIGN_RESOLVE`.
 *
 * @details
 * Resolves a path that may begin with an assign prefix into a concrete
 * capability handle. The handle is inserted into the caller's cap_table and
 * can be used with IORead/IOWrite for files or FsReadDir for directories.
 *
 * @param path Path string to resolve (e.g., "SYS:certs/roots.der").
 * @param path_len Length of the path string.
 * @param handle_out Output pointer for the resolved handle.
 * @return Result code.
 */
static i64 sys_assign_resolve(const char *path, usize path_len, u32 *handle_out)
{
    // Validate user pointers
    if (!validate_user_read(path, path_len) || path_len == 0)
    {
        return error::VERR_INVALID_ARG;
    }
    if (!validate_user_write(handle_out, sizeof(u32)))
    {
        return error::VERR_INVALID_ARG;
    }

    // Resolve with read-only access by default
    cap::Handle h = viper::assign::resolve_path(path, 0); // O_RDONLY
    if (h == cap::HANDLE_INVALID)
    {
        return error::VERR_NOT_FOUND;
    }
    *handle_out = h;
    return error::VOK;
}

// TLS syscalls (v0.2.0)
// TLS session pool (simple fixed array)
static constexpr int MAX_TLS_SESSIONS = 8;
static Spinlock tls_session_lock;
static viper::tls::TlsSession tls_sessions[MAX_TLS_SESSIONS];
static bool tls_session_active[MAX_TLS_SESSIONS] = {false};

/**
 * @brief Implementation of `SYS_TLS_CREATE`.
 *
 * @details
 * Creates a TLS session object associated with an existing TCP socket. The
 * kernel maintains a small fixed-size session pool; the returned session ID is
 * an index into that pool.
 *
 * @param socket_fd Underlying TCP socket descriptor.
 * @param hostname Optional hostname used for SNI and certificate verification.
 * @param verify Whether to verify server certificates.
 * @return Session ID on success, or negative error code on failure.
 */
static i64 sys_tls_create(i32 socket_fd, const char *hostname, bool verify)
{
    // Validate optional hostname (null is OK)
    if (hostname && validate_user_string(hostname, 256) < 0)
    {
        return error::VERR_INVALID_ARG;
    }

    SpinlockGuard guard(tls_session_lock);

    // Find free session slot
    int slot = -1;
    for (int i = 0; i < MAX_TLS_SESSIONS; i++)
    {
        if (!tls_session_active[i])
        {
            slot = i;
            break;
        }
    }

    if (slot < 0)
    {
        return error::VERR_NO_RESOURCE;
    }

    viper::tls::TlsConfig config;
    config.hostname = hostname;
    config.verify_certificates = verify; // Use caller-specified verification mode

    if (!viper::tls::tls_init(&tls_sessions[slot], socket_fd, &config))
    {
        return error::VERR_IO;
    }

    tls_session_active[slot] = true;
    return slot; // Return session ID
}

/**
 * @brief Implementation of `SYS_TLS_HANDSHAKE`.
 *
 * @details
 * Performs the TLS handshake for an existing TLS session. On failure, a
 * human-readable error string is printed to the serial console for debugging.
 *
 * @param session_id Session ID returned by @ref sys_tls_create.
 * @return Result code.
 */
static i64 sys_tls_handshake(i32 session_id)
{
    SpinlockGuard guard(tls_session_lock);

    if (session_id < 0 || session_id >= MAX_TLS_SESSIONS || !tls_session_active[session_id])
    {
        return error::VERR_INVALID_ARG;
    }

    if (viper::tls::tls_handshake(&tls_sessions[session_id]))
    {
        return error::VOK;
    }

    serial::puts("[tls] Handshake failed: ");
    serial::puts(viper::tls::tls_error(&tls_sessions[session_id]));
    serial::puts("\n");
    return error::VERR_IO;
}

/**
 * @brief Implementation of `SYS_TLS_SEND`.
 *
 * @details
 * Encrypts and sends application data over the TLS session.
 *
 * @param session_id Session ID.
 * @param data Pointer to plaintext bytes.
 * @param len Number of bytes to send.
 * @return Result code or byte count depending on TLS implementation.
 */
static i64 sys_tls_send(i32 session_id, const void *data, usize len)
{
    // Validate user pointer before taking lock
    if (!validate_user_read(data, len, len == 0))
    {
        return error::VERR_INVALID_ARG;
    }

    SpinlockGuard guard(tls_session_lock);

    if (session_id < 0 || session_id >= MAX_TLS_SESSIONS || !tls_session_active[session_id])
    {
        return error::VERR_INVALID_ARG;
    }

    return viper::tls::tls_send(&tls_sessions[session_id], data, len);
}

/**
 * @brief Implementation of `SYS_TLS_RECV`.
 *
 * @details
 * Polls the network stack for incoming packets and then attempts to receive and
 * decrypt TLS application data into the caller's buffer.
 *
 * @param session_id Session ID.
 * @param buffer Destination buffer for plaintext.
 * @param max_len Maximum number of bytes to read.
 * @return Result code or byte count depending on TLS implementation.
 */
static i64 sys_tls_recv(i32 session_id, void *buffer, usize max_len)
{
    // Validate user pointer before taking lock
    if (!validate_user_write(buffer, max_len, max_len == 0))
    {
        return error::VERR_INVALID_ARG;
    }

    {
        SpinlockGuard guard(tls_session_lock);
        if (session_id < 0 || session_id >= MAX_TLS_SESSIONS || !tls_session_active[session_id])
        {
            return error::VERR_INVALID_ARG;
        }
    }

    // Poll network to receive any pending data (without holding TLS lock)
    net::network_poll();

    SpinlockGuard guard(tls_session_lock);
    // Re-validate session is still active after poll
    if (!tls_session_active[session_id])
    {
        return error::VERR_INVALID_ARG;
    }

    return viper::tls::tls_recv(&tls_sessions[session_id], buffer, max_len);
}

/**
 * @brief Implementation of `SYS_TLS_CLOSE`.
 *
 * @details
 * Closes a TLS session and marks its slot free for reuse.
 *
 * @param session_id Session ID.
 * @return Result code.
 */
static i64 sys_tls_close(i32 session_id)
{
    SpinlockGuard guard(tls_session_lock);

    if (session_id < 0 || session_id >= MAX_TLS_SESSIONS || !tls_session_active[session_id])
    {
        return error::VERR_INVALID_ARG;
    }

    viper::tls::tls_close(&tls_sessions[session_id]);
    tls_session_active[session_id] = false;
    return error::VOK;
}

// Include TLSInfo struct for sys_tls_info
#include "../../include/viperos/tls_info.hpp"

/**
 * @brief Implementation of `SYS_MEM_INFO`.
 *
 * @details
 * Returns physical memory statistics including total, free, and used pages/bytes.
 * The caller provides a pointer to a MemInfo structure which is filled with
 * current memory usage data from the physical memory manager.
 *
 * @param info_out Output pointer for memory information structure.
 * @return Result code.
 */
static i64 sys_mem_info(MemInfo *info_out)
{
    // Validate user pointer
    if (!validate_user_write(info_out, sizeof(MemInfo)))
    {
        return error::VERR_INVALID_ARG;
    }

    info_out->total_pages = pmm::get_total_pages();
    info_out->free_pages = pmm::get_free_pages();
    info_out->used_pages = pmm::get_used_pages();
    info_out->page_size = 4096; // PAGE_SIZE
    info_out->total_bytes = info_out->total_pages * info_out->page_size;
    info_out->free_bytes = info_out->free_pages * info_out->page_size;
    info_out->used_bytes = info_out->used_pages * info_out->page_size;

    return error::VOK;
}

/**
 * @brief Implementation of `SYS_TLS_INFO`.
 *
 * @details
 * Fills a shared @ref TLSInfo structure with the kernel's current view of a
 * TLS session. This is intended for diagnostics and UI (e.g., printing the
 * negotiated TLS version/cipher in a user-space tool).
 *
 * The session is identified by the small integer ID returned by
 * `SYS_TLS_CREATE`. The call validates that the session is active and that the
 * output pointer is non-null.
 *
 * @param session_id TLS session ID.
 * @param out_info Output pointer to a @ref TLSInfo structure.
 * @return @ref error::VOK on success, or a negative @ref error::Code on failure.
 */
static i64 sys_tls_info(i32 session_id, TLSInfo *out_info)
{
    // Validate user pointer before taking lock
    if (!validate_user_write(out_info, sizeof(TLSInfo)))
    {
        return error::VERR_INVALID_ARG;
    }

    SpinlockGuard guard(tls_session_lock);

    if (session_id < 0 || session_id >= MAX_TLS_SESSIONS || !tls_session_active[session_id])
    {
        return error::VERR_INVALID_ARG;
    }

    if (viper::tls::tls_get_info(&tls_sessions[session_id], out_info))
    {
        return error::VOK;
    }

    return error::VERR_IO;
}

/**
 * @brief Dispatch the syscall described by the exception frame.
 *
 * @details
 * Extracts syscall number and arguments from the saved registers and executes
 * the corresponding syscall implementation.
 *
 * ## ABI Contract
 * - x0: VError (0 = success, negative = error)
 * - x1: Result value 0
 * - x2: Result value 1
 * - x3: Result value 2
 *
 * @param frame Exception frame captured at the SVC instruction.
 */
void dispatch(exceptions::ExceptionFrame *frame)
{
    // Get syscall number from x8
    u64 syscall_num = frame->x[8];

    // Get arguments from x0-x5
    u64 arg0 = frame->x[0];
    u64 arg1 = frame->x[1];
    u64 arg2 = frame->x[2];
    u64 arg3 = frame->x[3];
    u64 arg4 = frame->x[4];
    u64 arg5 = frame->x[5];

    // Suppress unused warnings
    (void)arg3;
    (void)arg4;
    (void)arg5;

    // ABI: x0 = VError, x1-x3 = results
    i64 verr = error::VERR_NOT_SUPPORTED;
    u64 res0 = 0, res1 = 0, res2 = 0;

// Helper macro for syscalls that return a value in old ABI (negative = error, non-negative =
// result) Converts to new ABI: error in verr, result in res0
#define SYSCALL_RESULT(call)                                                                       \
    do                                                                                             \
    {                                                                                              \
        i64 _r = (call);                                                                           \
        if (_r < 0)                                                                                \
        {                                                                                          \
            verr = _r;                                                                             \
            res0 = 0;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            verr = error::VOK;                                                                     \
            res0 = static_cast<u64>(_r);                                                           \
        }                                                                                          \
    } while (0)

// Helper for syscalls that just return error code (0 = success)
#define SYSCALL_VOID(call)                                                                         \
    do                                                                                             \
    {                                                                                              \
        verr = (call);                                                                             \
    } while (0)

    switch (syscall_num)
    {
        // Task syscalls
        case SYS_TASK_YIELD:
            SYSCALL_VOID(sys_task_yield());
            break;

        case SYS_TASK_EXIT:
            SYSCALL_VOID(sys_task_exit(static_cast<i64>(arg0)));
            break;

        case SYS_TASK_CURRENT:
            SYSCALL_RESULT(sys_task_current());
            break;

        // Debug syscalls
        case SYS_DEBUG_PRINT:
            SYSCALL_VOID(sys_debug_print(reinterpret_cast<const char *>(arg0)));
            break;

        // Channel syscalls (capability-aware)
        case SYS_CHANNEL_CREATE:
            verr = sys_channel_create_cap(res0, res1);
            break;

        case SYS_CHANNEL_SEND:
            SYSCALL_RESULT(sys_channel_send_cap(static_cast<cap::Handle>(arg0),
                                                reinterpret_cast<const void *>(arg1),
                                                static_cast<u32>(arg2),
                                                reinterpret_cast<const cap::Handle *>(arg3),
                                                static_cast<u32>(arg4)));
            break;

        case SYS_CHANNEL_RECV:
            SYSCALL_RESULT(sys_channel_recv_cap(static_cast<cap::Handle>(arg0),
                                                reinterpret_cast<void *>(arg1),
                                                static_cast<u32>(arg2),
                                                reinterpret_cast<cap::Handle *>(arg3),
                                                reinterpret_cast<u32 *>(arg4)));
            break;

        case SYS_CHANNEL_CLOSE:
            SYSCALL_VOID(sys_channel_close_cap(static_cast<cap::Handle>(arg0)));
            break;

        // Poll syscalls (capability-aware)
        case SYS_POLL_CREATE:
            verr = sys_poll_create_cap(res0);
            break;

        case SYS_POLL_ADD:
            SYSCALL_VOID(sys_poll_add_cap(static_cast<cap::Handle>(arg0),
                                          static_cast<u32>(arg1),
                                          static_cast<u32>(arg2)));
            break;

        case SYS_POLL_REMOVE:
            SYSCALL_VOID(sys_poll_remove_cap(static_cast<cap::Handle>(arg0),
                                             static_cast<u32>(arg1)));
            break;

        case SYS_POLL_WAIT:
            SYSCALL_RESULT(sys_poll_wait_cap(static_cast<cap::Handle>(arg0),
                                             reinterpret_cast<poll::PollEvent *>(arg1),
                                             static_cast<u32>(arg2),
                                             static_cast<i64>(arg3)));
            break;

        // Time syscalls
        case SYS_TIME_NOW:
            // time_now always succeeds, returns time in res0
            verr = error::VOK;
            res0 = static_cast<u64>(sys_time_now());
            break;

        case SYS_SLEEP:
            SYSCALL_VOID(sys_sleep(arg0));
            break;

        // File syscalls
        case SYS_OPEN:
        {
            const char *path = reinterpret_cast<const char *>(arg0);
            if (validate_user_string(path, viper::MAX_PATH) < 0)
            {
                verr = error::VERR_INVALID_ARG;
                break;
            }
            SYSCALL_RESULT(fs::vfs::open(path, static_cast<u32>(arg1)));
            break;
        }

        case SYS_CLOSE:
            SYSCALL_VOID(fs::vfs::close(static_cast<i32>(arg0)));
            break;

        case SYS_READ:
            SYSCALL_RESULT(fs::vfs::read(
                static_cast<i32>(arg0), reinterpret_cast<void *>(arg1), static_cast<usize>(arg2)));
            break;

        case SYS_WRITE:
            SYSCALL_RESULT(fs::vfs::write(static_cast<i32>(arg0),
                                          reinterpret_cast<const void *>(arg1),
                                          static_cast<usize>(arg2)));
            break;

        case SYS_LSEEK:
            SYSCALL_RESULT(fs::vfs::lseek(
                static_cast<i32>(arg0), static_cast<i64>(arg1), static_cast<i32>(arg2)));
            break;

        case SYS_STAT:
        {
            const char *path = reinterpret_cast<const char *>(arg0);
            fs::vfs::Stat *st = reinterpret_cast<fs::vfs::Stat *>(arg1);
            if (validate_user_string(path, viper::MAX_PATH) < 0 ||
                !validate_user_write(st, sizeof(fs::vfs::Stat)))
            {
                verr = error::VERR_INVALID_ARG;
                break;
            }
            SYSCALL_VOID(fs::vfs::stat(path, st));
            break;
        }

        case SYS_FSTAT:
            SYSCALL_VOID(
                fs::vfs::fstat(static_cast<i32>(arg0), reinterpret_cast<fs::vfs::Stat *>(arg1)));
            break;

        // Directory syscalls
        case SYS_READDIR:
            SYSCALL_RESULT(fs::vfs::getdents(
                static_cast<i32>(arg0), reinterpret_cast<void *>(arg1), static_cast<usize>(arg2)));
            break;

        case SYS_MKDIR:
        {
            const char *path = reinterpret_cast<const char *>(arg0);
            if (validate_user_string(path, viper::MAX_PATH) < 0)
            {
                verr = error::VERR_INVALID_ARG;
                break;
            }
            SYSCALL_VOID(fs::vfs::mkdir(path));
            break;
        }

        case SYS_RMDIR:
        {
            const char *path = reinterpret_cast<const char *>(arg0);
            if (validate_user_string(path, viper::MAX_PATH) < 0)
            {
                verr = error::VERR_INVALID_ARG;
                break;
            }
            SYSCALL_VOID(fs::vfs::rmdir(path));
            break;
        }

        case SYS_UNLINK:
        {
            const char *path = reinterpret_cast<const char *>(arg0);
            if (validate_user_string(path, viper::MAX_PATH) < 0)
            {
                verr = error::VERR_INVALID_ARG;
                break;
            }
            SYSCALL_VOID(fs::vfs::unlink(path));
            break;
        }

        case SYS_RENAME:
        {
            const char *old_path = reinterpret_cast<const char *>(arg0);
            const char *new_path = reinterpret_cast<const char *>(arg1);
            if (validate_user_string(old_path, viper::MAX_PATH) < 0 ||
                validate_user_string(new_path, viper::MAX_PATH) < 0)
            {
                verr = error::VERR_INVALID_ARG;
                break;
            }
            SYSCALL_VOID(fs::vfs::rename(old_path, new_path));
            break;
        }

        // Socket syscalls
        case SYS_SOCKET_CREATE:
            SYSCALL_RESULT(sys_socket_create());
            break;

        case SYS_SOCKET_CONNECT:
            SYSCALL_VOID(sys_socket_connect(
                static_cast<i32>(arg0), static_cast<u32>(arg1), static_cast<u16>(arg2)));
            break;

        case SYS_SOCKET_SEND:
            SYSCALL_RESULT(sys_socket_send(static_cast<i32>(arg0),
                                           reinterpret_cast<const void *>(arg1),
                                           static_cast<usize>(arg2)));
            break;

        case SYS_SOCKET_RECV:
            SYSCALL_RESULT(sys_socket_recv(
                static_cast<i32>(arg0), reinterpret_cast<void *>(arg1), static_cast<usize>(arg2)));
            break;

        case SYS_SOCKET_CLOSE:
            SYSCALL_VOID(sys_socket_close(static_cast<i32>(arg0)));
            break;

        case SYS_DNS_RESOLVE:
            SYSCALL_VOID(sys_dns_resolve(reinterpret_cast<const char *>(arg0),
                                         reinterpret_cast<u32 *>(arg1)));
            break;

        // Console syscalls
        case SYS_GETCHAR:
            while (true)
            {
                // Check virtio-keyboard first
                if (virtio::keyboard)
                {
                    input::poll();
                    i32 c = input::getchar();
                    if (c >= 0)
                    {
                        verr = error::VOK;
                        res0 = static_cast<u64>(c);
                        break;
                    }
                }
                // Check serial
                if (serial::has_char())
                {
                    verr = error::VOK;
                    res0 = static_cast<u64>(static_cast<u8>(serial::getc()));
                    break;
                }
                // Yield to let other things run
                asm volatile("wfe");
            }
            break;

        case SYS_PUTCHAR:
        {
            char c = static_cast<char>(arg0);
            serial::putc(c);
            if (gcon::is_available())
            {
                gcon::putc(c);
            }
        }
            verr = error::VOK;
            break;

        case SYS_UPTIME:
            // uptime always succeeds
            verr = error::VOK;
            res0 = timer::get_ticks();
            break;

        // Assign syscalls (v0.2.0)
        case SYS_ASSIGN_SET:
            SYSCALL_VOID(sys_assign_set(reinterpret_cast<const char *>(arg0),
                                        static_cast<usize>(arg1),
                                        static_cast<u32>(arg2)));
            break;

        case SYS_ASSIGN_GET:
            SYSCALL_VOID(sys_assign_get(reinterpret_cast<const char *>(arg0),
                                        static_cast<usize>(arg1),
                                        reinterpret_cast<u32 *>(arg2)));
            break;

        case SYS_ASSIGN_REMOVE:
            SYSCALL_VOID(
                sys_assign_remove(reinterpret_cast<const char *>(arg0), static_cast<usize>(arg1)));
            break;

        case SYS_ASSIGN_LIST:
            SYSCALL_VOID(sys_assign_list(reinterpret_cast<viper::assign::AssignInfo *>(arg0),
                                         static_cast<usize>(arg1),
                                         reinterpret_cast<usize *>(arg2)));
            break;

        case SYS_ASSIGN_RESOLVE:
            SYSCALL_VOID(sys_assign_resolve(reinterpret_cast<const char *>(arg0),
                                            static_cast<usize>(arg1),
                                            reinterpret_cast<u32 *>(arg2)));
            break;

        // TLS syscalls (v0.2.0)
        case SYS_TLS_CREATE:
            SYSCALL_RESULT(sys_tls_create(static_cast<i32>(arg0),
                                          reinterpret_cast<const char *>(arg1),
                                          arg2 != 0)); // verify: nonzero => true
            break;

        case SYS_TLS_HANDSHAKE:
            SYSCALL_VOID(sys_tls_handshake(static_cast<i32>(arg0)));
            break;

        case SYS_TLS_SEND:
            SYSCALL_RESULT(sys_tls_send(static_cast<i32>(arg0),
                                        reinterpret_cast<const void *>(arg1),
                                        static_cast<usize>(arg2)));
            break;

        case SYS_TLS_RECV:
            SYSCALL_RESULT(sys_tls_recv(
                static_cast<i32>(arg0), reinterpret_cast<void *>(arg1), static_cast<usize>(arg2)));
            break;

        case SYS_TLS_CLOSE:
            SYSCALL_VOID(sys_tls_close(static_cast<i32>(arg0)));
            break;

        case SYS_TLS_INFO:
            SYSCALL_VOID(sys_tls_info(static_cast<i32>(arg0), reinterpret_cast<TLSInfo *>(arg1)));
            break;

        // System Info syscalls (v0.2.0)
        case SYS_MEM_INFO:
            SYSCALL_VOID(sys_mem_info(reinterpret_cast<MemInfo *>(arg0)));
            break;

        // Task syscalls
        case SYS_TASK_LIST:
            SYSCALL_RESULT(sys_task_list(reinterpret_cast<TaskInfo *>(arg0),
                                         static_cast<u32>(arg1)));
            break;

        case SYS_TASK_SET_PRIORITY:
            SYSCALL_VOID(sys_task_set_priority(static_cast<u32>(arg0),
                                               static_cast<u8>(arg1)));
            break;

        case SYS_TASK_GET_PRIORITY:
            SYSCALL_RESULT(sys_task_get_priority(static_cast<u32>(arg0)));
            break;

        case SYS_TASK_SPAWN:
        {
            u64 out_pid = 0, out_tid = 0;
            verr = sys_task_spawn(reinterpret_cast<const char *>(arg0),
                                  reinterpret_cast<const char *>(arg1),
                                  out_pid, out_tid);
            res0 = out_pid;
            res1 = out_tid;
            break;
        }

        // Capability syscalls (0x70-0x73)
        case SYS_CAP_DERIVE:
            verr = sys_cap_derive(static_cast<cap::Handle>(arg0),
                                  static_cast<cap::Rights>(arg1),
                                  res0);
            break;

        case SYS_CAP_REVOKE:
            SYSCALL_VOID(sys_cap_revoke(static_cast<cap::Handle>(arg0)));
            break;

        case SYS_CAP_QUERY:
            SYSCALL_VOID(sys_cap_query(static_cast<cap::Handle>(arg0),
                                       reinterpret_cast<CapInfo *>(arg1)));
            break;

        case SYS_CAP_LIST:
            verr = sys_cap_list(reinterpret_cast<CapListEntry *>(arg0),
                                static_cast<u32>(arg1),
                                res0);
            break;

        // Handle-based Filesystem syscalls (0x80-0x87)
        case SYS_FS_OPEN_ROOT:
            verr = sys_fs_open_root(res0);
            break;

        case SYS_FS_OPEN:
            verr = sys_fs_open(static_cast<cap::Handle>(arg0),
                               reinterpret_cast<const char *>(arg1),
                               static_cast<usize>(arg2),
                               static_cast<u32>(arg3),
                               res0);
            break;

        case SYS_IO_READ:
            SYSCALL_RESULT(sys_io_read(static_cast<cap::Handle>(arg0),
                                       reinterpret_cast<void *>(arg1),
                                       static_cast<usize>(arg2)));
            break;

        case SYS_IO_WRITE:
            SYSCALL_RESULT(sys_io_write(static_cast<cap::Handle>(arg0),
                                        reinterpret_cast<const void *>(arg1),
                                        static_cast<usize>(arg2)));
            break;

        case SYS_IO_SEEK:
            SYSCALL_RESULT(sys_io_seek(static_cast<cap::Handle>(arg0),
                                       static_cast<i64>(arg1),
                                       static_cast<i32>(arg2)));
            break;

        case SYS_FS_READ_DIR:
            SYSCALL_RESULT(sys_fs_read_dir(static_cast<cap::Handle>(arg0),
                                           reinterpret_cast<kobj::FsDirEnt *>(arg1)));
            break;

        case SYS_FS_CLOSE:
            SYSCALL_VOID(sys_fs_close(static_cast<cap::Handle>(arg0)));
            break;

        case SYS_FS_REWIND_DIR:
            SYSCALL_VOID(sys_fs_rewind_dir(static_cast<cap::Handle>(arg0)));
            break;

        default:
            serial::puts("[syscall] Unknown syscall: ");
            serial::put_hex(syscall_num);
            serial::puts("\n");
            verr = error::VERR_NOT_SUPPORTED;
            break;
    }

#undef SYSCALL_RESULT
#undef SYSCALL_VOID

    // Store results per ABI: x0=VError, x1-x3=results
    frame->x[0] = static_cast<u64>(verr);
    frame->x[1] = res0;
    frame->x[2] = res1;
    frame->x[3] = res2;
}

} // namespace syscall
