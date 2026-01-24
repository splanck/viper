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

#include "handlers_internal.hpp"
#include "../../include/constants.hpp"
#include "../../include/viperdos/task_info.hpp"
#include "../../cap/handle.hpp"
#include "../../cap/rights.hpp"
#include "../../ipc/channel.hpp"
#include "../../kobj/channel.hpp"
#include "../../kobj/shm.hpp"
#include "../../loader/loader.hpp"
#include "../../mm/pmm.hpp"
#include "../../sched/scheduler.hpp"
#include "../../sched/task.hpp"
#include "../../viper/address_space.hpp"
#include "../../viper/viper.hpp"

namespace syscall
{

// Forward declarations for helper functions
static cap::Handle create_bootstrap_channel(viper::Viper *parent, viper::Viper *child);
static void copy_args_to_viper(viper::Viper *v, const char *args);

// =============================================================================
// Task Management Syscalls (0x00-0x0F)
// =============================================================================

SyscallResult sys_task_yield(u64, u64, u64, u64, u64, u64)
{
    task::yield();
    return SyscallResult::ok();
}

SyscallResult sys_task_exit(u64 a0, u64, u64, u64, u64, u64)
{
    task::exit(static_cast<i32>(a0));
    return SyscallResult::ok(); // Never reached
}

SyscallResult sys_task_current(u64, u64, u64, u64, u64, u64)
{
    task::Task *t = task::current();
    if (!t)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }
    return SyscallResult::ok(static_cast<u64>(t->id));
}

SyscallResult sys_task_spawn(u64 a0, u64 a1, u64 a2, u64, u64, u64)
{
    const char *path = reinterpret_cast<const char *>(a0);
    const char *name = reinterpret_cast<const char *>(a1);
    const char *args = reinterpret_cast<const char *>(a2);

    if (validate_user_string(path, kernel::constants::limits::MAX_PATH) < 0)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }
    if (name && validate_user_string(name, 64) < 0)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }
    if (args && validate_user_string(args, 256) < 0)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    task::Task *current_task = task::current();
    viper::Viper *parent_viper = nullptr;
    if (current_task && current_task->viper)
    {
        parent_viper = reinterpret_cast<viper::Viper *>(current_task->viper);
    }

    const char *display_name = name ? name : path;

    loader::SpawnResult result = loader::spawn_process(path, display_name, parent_viper);
    if (!result.success)
    {
        return SyscallResult::err(error::VERR_IO);
    }

    cap::Handle bootstrap_send = create_bootstrap_channel(parent_viper, result.viper);

    copy_args_to_viper(result.viper, args);

    return SyscallResult::ok(static_cast<u64>(result.viper->id),
                             static_cast<u64>(result.task_id),
                             static_cast<u64>(bootstrap_send));
}

SyscallResult sys_task_spawn_shm(u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64)
{
    cap::Handle shm_handle = static_cast<cap::Handle>(a0);
    u64 offset = a1;
    u64 length = a2;
    const char *name = reinterpret_cast<const char *>(a3);
    const char *args = reinterpret_cast<const char *>(a4);

    if (name && validate_user_string(name, 64) < 0)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }
    if (args && validate_user_string(args, 256) < 0)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    task::Task *current_task = task::current();
    viper::Viper *parent_viper = nullptr;
    if (current_task && current_task->viper)
    {
        parent_viper = reinterpret_cast<viper::Viper *>(current_task->viper);
    }

    viper::Viper *v = viper::current();
    if (!v || !v->cap_table)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    cap::Entry *entry = v->cap_table->get_checked(shm_handle, cap::Kind::SharedMemory);
    if (!entry)
    {
        return SyscallResult::err(error::VERR_INVALID_HANDLE);
    }
    if (!cap::has_rights(entry->rights, cap::CAP_READ))
    {
        return SyscallResult::err(error::VERR_PERMISSION);
    }

    kobj::SharedMemory *shm = static_cast<kobj::SharedMemory *>(entry->object);
    if (!shm)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    if (length == 0 || offset > shm->size() || offset + length > shm->size() ||
        offset + length < offset)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    const char *display_name = name ? name : "shm_spawn";
    const void *elf_data = pmm::phys_to_virt(shm->phys_addr() + offset);

    loader::SpawnResult result = loader::spawn_process_from_blob(
        elf_data, static_cast<usize>(length), display_name, parent_viper);
    if (!result.success)
    {
        return SyscallResult::err(error::VERR_IO);
    }

    cap::Handle bootstrap_send = create_bootstrap_channel(parent_viper, result.viper);

    copy_args_to_viper(result.viper, args);

    return SyscallResult::ok(static_cast<u64>(result.viper->id),
                             static_cast<u64>(result.task_id),
                             static_cast<u64>(bootstrap_send));
}

SyscallResult sys_replace(u64 a0, u64 a1, u64 a2, u64, u64, u64)
{
    const char *path = reinterpret_cast<const char *>(a0);
    const cap::Handle *preserve_handles = reinterpret_cast<const cap::Handle *>(a1);
    u32 preserve_count = static_cast<u32>(a2);

    if (validate_user_string(path, 256) < 0)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    if (preserve_handles && preserve_count > 0)
    {
        if (!validate_user_read(preserve_handles, preserve_count * sizeof(cap::Handle)))
        {
            return SyscallResult::err(error::VERR_INVALID_ARG);
        }
    }

    loader::ReplaceResult result = loader::replace_process(path, preserve_handles, preserve_count);
    if (!result.success)
    {
        return SyscallResult::err(error::VERR_IO);
    }

    task::Task *t = task::current();
    if (t && t->trap_frame)
    {
        t->trap_frame->x[30] = result.entry_point;
        t->trap_frame->elr = result.entry_point;
        t->trap_frame->sp = viper::layout::USER_STACK_TOP;
    }

    return SyscallResult::ok(result.entry_point);
}

SyscallResult sys_task_list(u64 a0, u64 a1, u64, u64, u64, u64)
{
    TaskInfo *buf = reinterpret_cast<TaskInfo *>(a0);
    u32 max_tasks = static_cast<u32>(a1);

    if (!validate_user_write(buf, max_tasks * sizeof(TaskInfo)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    u32 count = task::list_tasks(buf, max_tasks);
    return SyscallResult::ok(static_cast<u64>(count));
}

SyscallResult sys_task_set_priority(u64 a0, u64 a1, u64, u64, u64, u64)
{
    u32 task_id = static_cast<u32>(a0);
    u8 priority = static_cast<u8>(a1);

    if (priority >= 8)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    task::Task *cur = task::current();
    if (!cur)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    task::Task *target = task::get_by_id(task_id);
    if (!target)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    if (target->id != cur->id && target->parent_id != cur->id)
    {
        return SyscallResult::err(error::VERR_PERMISSION);
    }

    task::set_priority(target, priority);
    return SyscallResult::ok();
}

SyscallResult sys_task_get_priority(u64 a0, u64, u64, u64, u64, u64)
{
    u32 task_id = static_cast<u32>(a0);

    task::Task *target = task::get_by_id(task_id);
    if (!target)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    return SyscallResult::ok(static_cast<u64>(task::get_priority(target)));
}

SyscallResult sys_sched_setaffinity(u64 a0, u64 a1, u64, u64, u64, u64)
{
    u32 task_id = static_cast<u32>(a0);
    u32 mask = static_cast<u32>(a1);

    task::Task *cur = task::current();
    if (!cur)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    task::Task *target = (task_id == 0) ? cur : task::get_by_id(task_id);
    if (!target)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    if (target->id != cur->id && target->parent_id != cur->id)
    {
        return SyscallResult::err(error::VERR_PERMISSION);
    }

    i32 result = task::set_affinity(target, mask);
    if (result < 0)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }
    return SyscallResult::ok();
}

SyscallResult sys_sched_getaffinity(u64 a0, u64, u64, u64, u64, u64)
{
    u32 task_id = static_cast<u32>(a0);

    task::Task *cur = task::current();
    if (!cur)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    task::Task *target = (task_id == 0) ? cur : task::get_by_id(task_id);
    if (!target)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    return SyscallResult::ok(static_cast<u64>(task::get_affinity(target)));
}

SyscallResult sys_wait(u64 a0, u64, u64, u64, u64, u64)
{
    i32 *status = reinterpret_cast<i32 *>(a0);

    if (status && !validate_user_write(status, sizeof(i32)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    i64 result = viper::wait(-1, status);
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok(static_cast<u64>(result));
}

SyscallResult sys_waitpid(u64 a0, u64 a1, u64, u64, u64, u64)
{
    i64 pid = static_cast<i64>(a0);
    i32 *status = reinterpret_cast<i32 *>(a1);

    if (status && !validate_user_write(status, sizeof(i32)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    i64 result = viper::wait(pid, status);
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok(static_cast<u64>(result));
}

SyscallResult sys_fork(u64, u64, u64, u64, u64, u64)
{
    viper::Viper *child = viper::fork();
    if (!child)
    {
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    task::Task *parent_task = task::current();
    if (!parent_task)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    task::Task *child_task = task::create_user_task(
        child->name, child, parent_task->user_entry, parent_task->user_stack);
    if (!child_task)
    {
        viper::destroy(child);
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    if (parent_task->trap_frame && child_task->trap_frame)
    {
        for (int i = 0; i < 31; i++)
        {
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

SyscallResult sys_sbrk(u64 a0, u64, u64, u64, u64, u64)
{
    i64 increment = static_cast<i64>(a0);

    task::Task *t = task::current();
    if (!t || !t->viper)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    viper::Viper *v = reinterpret_cast<viper::Viper *>(t->viper);
    u64 old_break = v->heap_break;

    if (increment == 0)
    {
        return SyscallResult::ok(old_break);
    }

    u64 new_break = old_break + increment;

    if (increment > 0 && new_break < old_break)
    {
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }
    if (increment < 0 && new_break > old_break)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }
    if (new_break < v->heap_start)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }
    if (new_break > v->heap_max)
    {
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    if (increment > 0)
    {
        u64 old_page = (old_break + 0xFFF) & ~0xFFFULL;
        u64 new_page = (new_break + 0xFFF) & ~0xFFFULL;

        viper::AddressSpace *as = viper::get_address_space(v);
        if (!as)
        {
            return SyscallResult::err(error::VERR_NOT_FOUND);
        }

        while (old_page < new_page)
        {
            u64 phys = pmm::alloc_page();
            if (!phys)
            {
                return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
            }

            if (!as->map(old_page, phys, 0x1000, viper::prot::RW))
            {
                pmm::free_page(phys);
                return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
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
static cap::Handle create_bootstrap_channel(viper::Viper *parent, viper::Viper *child)
{
    if (!parent || !parent->cap_table || !child || !child->cap_table)
    {
        return cap::HANDLE_INVALID;
    }

    i64 channel_id = channel::create();
    if (channel_id < 0)
    {
        return cap::HANDLE_INVALID;
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
        return cap::HANDLE_INVALID;
    }

    cap::Handle child_recv = child->cap_table->insert(
        recv_ep, cap::Kind::Channel, cap::CAP_READ | cap::CAP_TRANSFER);
    if (child_recv == cap::HANDLE_INVALID)
    {
        delete send_ep;
        delete recv_ep;
        return cap::HANDLE_INVALID;
    }

    cap::Handle parent_send = parent->cap_table->insert(
        send_ep, cap::Kind::Channel, cap::CAP_WRITE | cap::CAP_TRANSFER);
    if (parent_send == cap::HANDLE_INVALID)
    {
        child->cap_table->remove(child_recv);
        delete send_ep;
        delete recv_ep;
        return cap::HANDLE_INVALID;
    }

    return parent_send;
}

/// Copy arguments string to a viper's args buffer
static void copy_args_to_viper(viper::Viper *v, const char *args)
{
    if (!v)
        return;

    if (args)
    {
        usize i = 0;
        while (i < 255 && args[i])
        {
            v->args[i] = args[i];
            i++;
        }
        v->args[i] = '\0';
    }
    else
    {
        v->args[0] = '\0';
    }
}

} // namespace syscall
