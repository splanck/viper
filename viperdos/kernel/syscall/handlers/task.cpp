//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/syscall/handlers/task.cpp
// Purpose: Task management syscall handlers (0x00-0x0F).
// Key invariants: All handlers validate user pointers before access.
// Ownership/Lifetime: Static functions; linked at compile time.
// Links: kernel/syscall/table.cpp, kernel/sched/task.cpp
//
//===----------------------------------------------------------------------===//

#include "../../sched/task.hpp"
#include "../../cap/handle.hpp"
#include "../../cap/rights.hpp"
#include "../../console/serial.hpp"
#include "../../include/constants.hpp"
#include "../../include/viperdos/task_info.hpp"
#include "../../ipc/channel.hpp"
#include "../../kobj/channel.hpp"
#include "../../kobj/shm.hpp"
#include "../../loader/loader.hpp"
#include "../../mm/kheap.hpp"
#include "../../mm/pmm.hpp"
#include "../../sched/scheduler.hpp"
#include "../../viper/address_space.hpp"
#include "../../viper/viper.hpp"
#include "handlers_internal.hpp"

namespace syscall {

// Forward declarations for helper functions
static cap::Handle create_bootstrap_channel(viper::Viper *parent, viper::Viper *child);
static void copy_args_to_viper(viper::Viper *v, const char *args);

// =============================================================================
// Task Management Syscalls (0x00-0x0F)
// =============================================================================

/// @brief Voluntarily yield the CPU to the scheduler.
SyscallResult sys_task_yield(u64, u64, u64, u64, u64, u64) {
    task::yield();
    return SyscallResult::ok();
}

/// @brief Terminate the calling task with the given exit code (does not return).
SyscallResult sys_task_exit(u64 a0, u64, u64, u64, u64, u64) {
    task::exit(static_cast<i32>(a0));
    return SyscallResult::ok(); // Never reached
}

/// @brief Return the task ID of the calling task.
SyscallResult sys_task_current(u64, u64, u64, u64, u64, u64) {
    task::Task *t = task::current();
    if (!t) {
        return err_not_found();
    }
    return ok_u64(static_cast<u64>(t->id));
}

/// @brief Spawn a new process from an ELF at the given filesystem path.
/// @details User-space strings (path, name, args) are copied into kernel-side
///   buffers before any operation that may context-switch, because user pointers
///   become invalid if TTBR0 switches to another process's page tables.
///   Creates a bootstrap IPC channel between parent and child.
SyscallResult sys_task_spawn(u64 a0, u64 a1, u64 a2, u64, u64, u64) {
    const char *path = reinterpret_cast<const char *>(a0);
    const char *name = reinterpret_cast<const char *>(a1);
    const char *args = reinterpret_cast<const char *>(a2);

    VALIDATE_USER_STRING(path, kernel::constants::limits::MAX_PATH);
    if (name) {
        VALIDATE_USER_STRING(name, 64);
    }
    if (args) {
        VALIDATE_USER_STRING(args, 256);
    }

    // Copy user strings into kernel buffers BEFORE any operations that might
    // cause a context switch. User pointers become invalid if TTBR0 switches
    // to another process's page tables.
    char path_buf[kernel::constants::limits::MAX_PATH];
    char name_buf[64];
    char args_buf[256];

    // Copy path (required)
    usize i = 0;
    while (i < sizeof(path_buf) - 1 && path[i]) {
        path_buf[i] = path[i];
        i++;
    }
    path_buf[i] = '\0';

    // Copy name (optional)
    const char *display_name = path_buf;
    if (name) {
        i = 0;
        while (i < sizeof(name_buf) - 1 && name[i]) {
            name_buf[i] = name[i];
            i++;
        }
        name_buf[i] = '\0';
        display_name = name_buf;
    }

    // Copy args (optional)
    const char *args_ptr = nullptr;
    if (args) {
        i = 0;
        while (i < sizeof(args_buf) - 1 && args[i]) {
            args_buf[i] = args[i];
            i++;
        }
        args_buf[i] = '\0';
        args_ptr = args_buf;
    }

    task::Task *current_task = task::current();
    viper::Viper *parent_viper = nullptr;
    if (current_task && current_task->viper) {
        parent_viper = reinterpret_cast<viper::Viper *>(current_task->viper);
    }

    // Verify vinit's page tables before spawn
    viper::debug_verify_vinit_tables("before spawn_process");

    loader::SpawnResult result = loader::spawn_process(path_buf, display_name, parent_viper);
    if (!result.success) {
        return err_code(error::VERR_IO);
    }

    // Verify vinit's page tables after spawn
    viper::debug_verify_vinit_tables("after spawn_process");

    // DEBUG: Also show parent's L1[2] directly
    if (parent_viper) {
        viper::AddressSpace *parent_as = viper::get_address_space(parent_viper);
        if (parent_as) {
            u64 *l0 = reinterpret_cast<u64 *>(pmm::phys_to_virt(parent_as->root()));
            if (l0[0] & 0x1) {
                u64 *l1 = reinterpret_cast<u64 *>(pmm::phys_to_virt(l0[0] & ~0xFFFULL));
                serial::puts("[spawn_debug] Parent L1[2]=");
                serial::put_hex(l1[2]);
                serial::puts("\n");
            }
        }
    }

    cap::Handle bootstrap_send = create_bootstrap_channel(parent_viper, result.viper);

    copy_args_to_viper(result.viper, args_ptr);

    return SyscallResult::ok(static_cast<u64>(result.viper->id),
                             static_cast<u64>(result.task_id),
                             static_cast<u64>(bootstrap_send));
}

/// @brief Spawn a new process from an ELF image stored in shared memory.
/// @details Similar to sys_task_spawn but reads the ELF from a shared memory
///   region at the given offset/length instead of the filesystem.
SyscallResult sys_task_spawn_shm(u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64) {
    cap::Handle shm_handle = static_cast<cap::Handle>(a0);
    u64 offset = a1;
    u64 length = a2;
    const char *name = reinterpret_cast<const char *>(a3);
    const char *args = reinterpret_cast<const char *>(a4);

    if (name) {
        VALIDATE_USER_STRING(name, 64);
    }
    if (args) {
        VALIDATE_USER_STRING(args, 256);
    }

    // Copy user strings into kernel buffers before any context-switch-prone operations
    char name_buf[64];
    char args_buf[256];

    const char *display_name = "shm_spawn";
    if (name) {
        usize i = 0;
        while (i < sizeof(name_buf) - 1 && name[i]) {
            name_buf[i] = name[i];
            i++;
        }
        name_buf[i] = '\0';
        display_name = name_buf;
    }

    const char *args_ptr = nullptr;
    if (args) {
        usize i = 0;
        while (i < sizeof(args_buf) - 1 && args[i]) {
            args_buf[i] = args[i];
            i++;
        }
        args_buf[i] = '\0';
        args_ptr = args_buf;
    }

    task::Task *current_task = task::current();
    viper::Viper *parent_viper = nullptr;
    if (current_task && current_task->viper) {
        parent_viper = reinterpret_cast<viper::Viper *>(current_task->viper);
    }

    viper::Viper *v = viper::current();
    if (!v || !v->cap_table) {
        return err_not_found();
    }

    cap::Entry *entry = v->cap_table->get_checked(shm_handle, cap::Kind::SharedMemory);
    if (!entry) {
        return err_invalid_handle();
    }
    if (!cap::has_rights(entry->rights, cap::CAP_READ)) {
        return err_permission();
    }

    kobj::SharedMemory *shm = static_cast<kobj::SharedMemory *>(entry->object);
    if (!shm) {
        return err_not_found();
    }

    if (length == 0 || offset > shm->size() || offset + length > shm->size() ||
        offset + length < offset) {
        return err_invalid_arg();
    }

    const void *elf_data = pmm::phys_to_virt(shm->phys_addr() + offset);

    loader::SpawnResult result = loader::spawn_process_from_blob(
        elf_data, static_cast<usize>(length), display_name, parent_viper);
    if (!result.success) {
        return err_code(error::VERR_IO);
    }

    cap::Handle bootstrap_send = create_bootstrap_channel(parent_viper, result.viper);

    copy_args_to_viper(result.viper, args_ptr);

    return SyscallResult::ok(static_cast<u64>(result.viper->id),
                             static_cast<u64>(result.task_id),
                             static_cast<u64>(bootstrap_send));
}

/// @brief Replace the current process image with a new ELF (exec-style).
/// @details Copies user strings into kernel buffers before the operation.
///   Optionally preserves up to 16 capability handles across the replacement.
SyscallResult sys_replace(u64 a0, u64 a1, u64 a2, u64, u64, u64) {
    const char *path = reinterpret_cast<const char *>(a0);
    const cap::Handle *preserve_handles = reinterpret_cast<const cap::Handle *>(a1);
    u32 preserve_count = static_cast<u32>(a2);

    if (validate_user_string(path, 256) < 0) {
        return err_invalid_arg();
    }

    if (preserve_handles && preserve_count > 0) {
        if (!validate_user_read(preserve_handles, preserve_count * sizeof(cap::Handle))) {
            return err_invalid_arg();
        }
    }

    // Copy path into kernel buffer for safety
    char path_buf[256];
    usize i = 0;
    while (i < sizeof(path_buf) - 1 && path[i]) {
        path_buf[i] = path[i];
        i++;
    }
    path_buf[i] = '\0';

    // Copy preserve_handles into kernel buffer (limit to reasonable count)
    constexpr u32 MAX_PRESERVE = 16;
    cap::Handle handles_buf[MAX_PRESERVE];
    const cap::Handle *handles_ptr = nullptr;
    if (preserve_handles && preserve_count > 0) {
        if (preserve_count > MAX_PRESERVE) {
            preserve_count = MAX_PRESERVE;
        }
        for (u32 j = 0; j < preserve_count; j++) {
            handles_buf[j] = preserve_handles[j];
        }
        handles_ptr = handles_buf;
    }

    loader::ReplaceResult result = loader::replace_process(path_buf, handles_ptr, preserve_count);
    if (!result.success) {
        return err_code(error::VERR_IO);
    }

    task::Task *t = task::current();
    if (t && t->trap_frame) {
        t->trap_frame->x[30] = result.entry_point;
        t->trap_frame->elr = result.entry_point;
        t->trap_frame->sp = viper::layout::USER_STACK_TOP;
    }

    return SyscallResult::ok(result.entry_point);
}

/// @brief List all active tasks into a user-supplied TaskInfo buffer.
SyscallResult sys_task_list(u64 a0, u64 a1, u64, u64, u64, u64) {
    TaskInfo *buf = reinterpret_cast<TaskInfo *>(a0);
    u32 max_tasks = static_cast<u32>(a1);

    if (!validate_user_write(buf, max_tasks * sizeof(TaskInfo))) {
        return err_invalid_arg();
    }

    u32 count = task::list_tasks(buf, max_tasks);
    return SyscallResult::ok(static_cast<u64>(count));
}

/// @brief Set the scheduling priority of a task (0-7, lower is higher priority).
/// @details Only allowed for the calling task or its direct children.
SyscallResult sys_task_set_priority(u64 a0, u64 a1, u64, u64, u64, u64) {
    u32 task_id = static_cast<u32>(a0);
    u8 priority = static_cast<u8>(a1);

    if (priority >= 8) {
        return err_invalid_arg();
    }

    task::Task *cur = task::current();
    if (!cur) {
        return err_not_found();
    }

    task::Task *target = task::get_by_id(task_id);
    if (!target) {
        return err_not_found();
    }

    if (target->id != cur->id && target->parent_id != cur->id) {
        return err_permission();
    }

    task::set_priority(target, priority);
    return SyscallResult::ok();
}

/// @brief Get the scheduling priority of a task.
SyscallResult sys_task_get_priority(u64 a0, u64, u64, u64, u64, u64) {
    u32 task_id = static_cast<u32>(a0);

    task::Task *target = task::get_by_id(task_id);
    if (!target) {
        return err_not_found();
    }

    return SyscallResult::ok(static_cast<u64>(task::get_priority(target)));
}

/// @brief Set the CPU affinity mask for a task (bitmask of allowed CPUs).
/// @details Task ID 0 means the calling task. Only self or children are allowed.
SyscallResult sys_sched_setaffinity(u64 a0, u64 a1, u64, u64, u64, u64) {
    u32 task_id = static_cast<u32>(a0);
    u32 mask = static_cast<u32>(a1);

    task::Task *cur = task::current();
    if (!cur) {
        return err_not_found();
    }

    task::Task *target = (task_id == 0) ? cur : task::get_by_id(task_id);
    if (!target) {
        return err_not_found();
    }

    if (target->id != cur->id && target->parent_id != cur->id) {
        return err_permission();
    }

    i32 result = task::set_affinity(target, mask);
    if (result < 0) {
        return err_invalid_arg();
    }
    return SyscallResult::ok();
}

/// @brief Get the CPU affinity mask for a task (task ID 0 = calling task).
SyscallResult sys_sched_getaffinity(u64 a0, u64, u64, u64, u64, u64) {
    u32 task_id = static_cast<u32>(a0);

    task::Task *cur = task::current();
    if (!cur) {
        return err_not_found();
    }

    task::Task *target = (task_id == 0) ? cur : task::get_by_id(task_id);
    if (!target) {
        return err_not_found();
    }

    return SyscallResult::ok(static_cast<u64>(task::get_affinity(target)));
}

/// @brief Wait for any child process to exit, returning its PID and exit status.
SyscallResult sys_wait(u64 a0, u64, u64, u64, u64, u64) {
    i32 *status = reinterpret_cast<i32 *>(a0);

    if (status && !validate_user_write(status, sizeof(i32))) {
        return err_invalid_arg();
    }

    i64 result = viper::wait(-1, status);
    if (result < 0) {
        return err_code(result);
    }
    return SyscallResult::ok(static_cast<u64>(result));
}

/// @brief Wait for a specific child process (by PID) to exit.
SyscallResult sys_waitpid(u64 a0, u64 a1, u64, u64, u64, u64) {
    i64 pid = static_cast<i64>(a0);
    i32 *status = reinterpret_cast<i32 *>(a1);

    if (status && !validate_user_write(status, sizeof(i32))) {
        return err_invalid_arg();
    }

    i64 result = viper::wait(pid, status);
    if (result < 0) {
        return err_code(result);
    }
    return SyscallResult::ok(static_cast<u64>(result));
}

/// @brief Fork the calling process, creating a child with copied address space.
/// @details The child task receives a copy of all registers with x0 set to 0.
///   The parent receives the child's viper ID.
SyscallResult sys_fork(u64, u64, u64, u64, u64, u64) {
    viper::Viper *child = viper::fork();
    if (!child) {
        return err_out_of_memory();
    }

    task::Task *parent_task = task::current();
    if (!parent_task) {
        return err_not_found();
    }

    task::Task *child_task = task::create_user_task(
        child->name, child, parent_task->user_entry, parent_task->user_stack);
    if (!child_task) {
        viper::destroy(child);
        return err_out_of_memory();
    }

    if (parent_task->trap_frame && child_task->trap_frame) {
        for (int i = 0; i < 31; i++) {
            child_task->trap_frame->x[i] = parent_task->trap_frame->x[i];
        }
        child_task->trap_frame->sp = parent_task->trap_frame->sp;
        child_task->trap_frame->elr = parent_task->trap_frame->elr;
        child_task->trap_frame->spsr = parent_task->trap_frame->spsr;
        child_task->trap_frame->x[0] = 0; // Child returns 0
    }

    scheduler::enqueue(child_task);

    return SyscallResult::ok(child->id);
}

/// @brief Adjust the process heap break by the given increment.
/// @details Returns the old break address. Positive increments allocate new pages
///   on demand; negative increments shrink the heap (but do not unmap pages).
SyscallResult sys_sbrk(u64 a0, u64, u64, u64, u64, u64) {
    i64 increment = static_cast<i64>(a0);

    task::Task *t = task::current();
    if (!t || !t->viper) {
        return err_not_found();
    }

    viper::Viper *v = reinterpret_cast<viper::Viper *>(t->viper);
    u64 old_break = v->heap_break;

    if (increment == 0) {
        return SyscallResult::ok(old_break);
    }

    u64 new_break = old_break + increment;

    if (increment > 0 && new_break < old_break) {
        return err_out_of_memory();
    }
    if (increment < 0 && new_break > old_break) {
        return err_invalid_arg();
    }
    if (new_break < v->heap_start) {
        return err_invalid_arg();
    }
    if (new_break > v->heap_max) {
        return err_out_of_memory();
    }

    if (increment > 0) {
        u64 old_page = (old_break + 0xFFF) & ~0xFFFULL;
        u64 new_page = (new_break + 0xFFF) & ~0xFFFULL;

        viper::AddressSpace *as = viper::get_address_space(v);
        if (!as) {
            return err_not_found();
        }

        while (old_page < new_page) {
            u64 phys = pmm::alloc_page();
            if (!phys) {
                return err_out_of_memory();
            }

            if (!as->map(old_page, phys, 0x1000, viper::prot::RW)) {
                pmm::free_page(phys);
                return err_out_of_memory();
            }

            old_page += 0x1000;
        }
    }

    v->heap_break = new_break;
    return SyscallResult::ok(old_break);
}

// =============================================================================
// Helper Functions
// =============================================================================

/// Create a bootstrap channel between parent and child vipers
static cap::Handle create_bootstrap_channel(viper::Viper *parent, viper::Viper *child) {
    if (!parent || !parent->cap_table || !child || !child->cap_table) {
        return cap::HANDLE_INVALID;
    }

    kheap::debug_check_watch_addr("bootstrap_start");

    i64 channel_id = channel::create();
    if (channel_id < 0) {
        return cap::HANDLE_INVALID;
    }

    kheap::debug_check_watch_addr("after_channel_create");

    kobj::Channel *send_ep =
        kobj::Channel::adopt(static_cast<u32>(channel_id), kobj::Channel::ENDPOINT_SEND);

    kheap::debug_check_watch_addr("after_send_ep_alloc");

    kobj::Channel *recv_ep =
        kobj::Channel::adopt(static_cast<u32>(channel_id), kobj::Channel::ENDPOINT_RECV);

    kheap::debug_check_watch_addr("after_recv_ep_alloc");

    if (!send_ep || !recv_ep) {
        delete send_ep;
        delete recv_ep;
        channel::close(static_cast<u32>(channel_id));
        return cap::HANDLE_INVALID;
    }

    cap::Handle child_recv =
        child->cap_table->insert(recv_ep, cap::Kind::Channel, cap::CAP_READ | cap::CAP_TRANSFER);

    kheap::debug_check_watch_addr("after_child_insert");

    if (child_recv == cap::HANDLE_INVALID) {
        delete send_ep;
        delete recv_ep;
        return cap::HANDLE_INVALID;
    }

    cap::Handle parent_send =
        parent->cap_table->insert(send_ep, cap::Kind::Channel, cap::CAP_WRITE | cap::CAP_TRANSFER);

    kheap::debug_check_watch_addr("after_parent_insert");

    if (parent_send == cap::HANDLE_INVALID) {
        child->cap_table->remove(child_recv);
        delete send_ep;
        delete recv_ep;
        return cap::HANDLE_INVALID;
    }

    return parent_send;
}

/// Copy arguments string to a viper's args buffer
static void copy_args_to_viper(viper::Viper *v, const char *args) {
    if (!v)
        return;

    if (args) {
        usize i = 0;
        while (i < 255 && args[i]) {
            v->args[i] = args[i];
            i++;
        }
        v->args[i] = '\0';
    } else {
        v->args[0] = '\0';
    }
}

} // namespace syscall
