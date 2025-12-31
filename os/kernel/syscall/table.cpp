/**
 * @file table.cpp
 * @brief Syscall dispatch table and handler implementations.
 *
 * @details
 * This file contains:
 * 1. User pointer validation helpers
 * 2. Individual syscall handler implementations
 * 3. The static syscall dispatch table
 * 4. Table lookup and dispatch functions
 *
 * All handlers conform to the SyscallHandler signature:
 *   SyscallResult handler(u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5)
 */
#include "table.hpp"
#include "../../include/viperos/cap_info.hpp"
#include "../../include/viperos/fs_types.hpp"
#include "../../include/viperos/mem_info.hpp"
#include "../../include/viperos/net_stats.hpp"
#include "../../include/viperos/task_info.hpp"
#include "../arch/aarch64/gic.hpp"
#include "../arch/aarch64/timer.hpp"
#include "../assign/assign.hpp"
#include "../cap/handle.hpp"
#include "../cap/rights.hpp"
#include "../cap/table.hpp"
#include "../console/console.hpp"
#include "../console/gcon.hpp"
#include "../console/serial.hpp"
#include "../drivers/virtio/input.hpp"
#include "../fs/vfs/vfs.hpp"
#include "../fs/viperfs/viperfs.hpp"
#include "../include/config.hpp"
#include "../include/error.hpp"
#include "../include/syscall_nums.hpp"
#include "../input/input.hpp"
#include "../ipc/channel.hpp"
#include "../ipc/poll.hpp"
#include "../ipc/pollset.hpp"
#include "../kobj/channel.hpp"
#include "../kobj/dir.hpp"
#include "../kobj/file.hpp"
#include "../kobj/shm.hpp"
#include "../lib/spinlock.hpp"
#include "../loader/loader.hpp"
#include "../mm/pmm.hpp"
#include "../net/dns/dns.hpp"
#include "../net/ip/icmp.hpp"
#include "../net/ip/tcp.hpp"
#include "../net/network.hpp"
#include "../net/tls/tls.hpp"
#include "../sched/scheduler.hpp"
#include "../sched/signal.hpp"
#include "../sched/task.hpp"
#include "../viper/address_space.hpp"
#include "../viper/viper.hpp"

namespace syscall
{

// =============================================================================
// Configuration
// =============================================================================

#ifdef CONFIG_SYSCALL_TRACE
static bool g_tracing_enabled = false;

void set_tracing(bool enabled)
{
    g_tracing_enabled = enabled;
}

bool is_tracing()
{
    return g_tracing_enabled;
}

static void trace_entry(const SyscallEntry *entry, u64 a0, u64 a1, u64 a2)
{
    if (!g_tracing_enabled || !entry)
        return;

    task::Task *t = task::current();
    serial::puts("[syscall] pid=");
    serial::put_dec(t ? t->id : 0);
    serial::puts(" ");
    serial::puts(entry->name);
    serial::puts("(");
    serial::put_hex(a0);
    if (entry->argcount > 1)
    {
        serial::puts(", ");
        serial::put_hex(a1);
    }
    if (entry->argcount > 2)
    {
        serial::puts(", ");
        serial::put_hex(a2);
    }
    serial::puts(")\n");
}

static void trace_exit(const SyscallEntry *entry, const SyscallResult &result)
{
    if (!g_tracing_enabled || !entry)
        return;

    serial::puts("[syscall] ");
    serial::puts(entry->name);
    serial::puts(" => err=");
    serial::put_dec(result.verr);
    serial::puts(" res=");
    serial::put_hex(result.res0);
    serial::puts("\n");
}
#endif

// =============================================================================
// User Pointer Validation
// =============================================================================

// Helper to check if an address is in a valid user-space range
static bool is_valid_user_address(u64 addr, usize size)
{
    // Check for overflow
    if (addr + size < addr)
    {
        return false;
    }

    // Reject null page (first 4KB)
    if (addr < 0x1000)
    {
        return false;
    }

    // Reject addresses in kernel space (upper half on AArch64)
    if (addr >= 0xFFFF000000000000ULL)
    {
        return false;
    }

    // Reject uncanonical addresses (bits 48-63 must match bit 47)
    // This catches addresses like 0xDEAD000000000000
    u64 top_bits = addr >> 48;
    u64 bit47 = (addr >> 47) & 1;
    if (bit47 == 0 && top_bits != 0)
    {
        return false;
    }
    if (bit47 == 1 && top_bits != 0xFFFF)
    {
        return false;
    }

    return true;
}

bool validate_user_read(const void *ptr, usize size, bool null_ok)
{
    if (!ptr)
    {
        return null_ok && size == 0;
    }

    u64 addr = reinterpret_cast<u64>(ptr);
    if (!is_valid_user_address(addr, size))
    {
        return false;
    }

    // TODO: When user mode is implemented, also check memory is mapped
    return true;
}

bool validate_user_write(void *ptr, usize size, bool null_ok)
{
    if (!ptr)
    {
        return null_ok && size == 0;
    }

    u64 addr = reinterpret_cast<u64>(ptr);
    if (!is_valid_user_address(addr, size))
    {
        return false;
    }

    // TODO: When user mode is implemented, also check memory is mapped
    return true;
}

i64 validate_user_string(const char *str, usize max_len)
{
    if (!str)
    {
        return -1;
    }

    // Check if the pointer is in a valid user-space range
    u64 addr = reinterpret_cast<u64>(str);
    if (!is_valid_user_address(addr, 1))
    {
        return -1;
    }

    for (usize i = 0; i <= max_len; i++)
    {
        if (str[i] == '\0')
        {
            return static_cast<i64>(i);
        }
    }

    return -1;
}

// =============================================================================
// TLS Session State (from dispatch.cpp)
// =============================================================================

#if VIPER_KERNEL_ENABLE_TLS
static constexpr int MAX_TLS_SESSIONS = 16;
static viper::tls::TlsSession tls_sessions[MAX_TLS_SESSIONS];
static bool tls_session_active[MAX_TLS_SESSIONS] = {false};
static kernel::Spinlock tls_lock;
#endif

// =============================================================================
// Syscall Handler Implementations
// =============================================================================

// --- Task Management (0x00-0x0F) ---

static SyscallResult sys_task_yield(u64, u64, u64, u64, u64, u64)
{
    task::yield();
    return SyscallResult::ok();
}

static SyscallResult sys_task_exit(u64 a0, u64, u64, u64, u64, u64)
{
    task::exit(static_cast<i32>(a0));
    return SyscallResult::ok(); // Never reached
}

static SyscallResult sys_task_current(u64, u64, u64, u64, u64, u64)
{
    task::Task *t = task::current();
    if (!t)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }
    return SyscallResult::ok(static_cast<u64>(t->id));
}

static SyscallResult sys_task_spawn(u64 a0, u64 a1, u64 a2, u64, u64, u64)
{
    const char *path = reinterpret_cast<const char *>(a0);
    const char *name = reinterpret_cast<const char *>(a1);
    const char *args = reinterpret_cast<const char *>(a2);

    if (validate_user_string(path, viper::MAX_PATH) < 0)
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

    // Get current task's viper as parent
    task::Task *current_task = task::current();
    viper::Viper *parent_viper = nullptr;
    if (current_task && current_task->viper)
    {
        parent_viper = reinterpret_cast<viper::Viper *>(current_task->viper);
    }

    // Use the display name or extract from path
    const char *display_name = name ? name : path;

    // Spawn the process using loader
    loader::SpawnResult result = loader::spawn_process(path, display_name, parent_viper);
    if (!result.success)
    {
        return SyscallResult::err(error::VERR_IO);
    }

    // Copy args to the new process if provided
    if (args && result.viper)
    {
        usize i = 0;
        while (i < 255 && args[i])
        {
            result.viper->args[i] = args[i];
            i++;
        }
        result.viper->args[i] = '\0';
    }
    else if (result.viper)
    {
        result.viper->args[0] = '\0';
    }

    return SyscallResult::ok(static_cast<u64>(result.viper->id), static_cast<u64>(result.task_id));
}

static SyscallResult sys_task_list(u64 a0, u64 a1, u64, u64, u64, u64)
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

static SyscallResult sys_task_set_priority(u64 a0, u64 a1, u64, u64, u64, u64)
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

    // Only allow setting own priority or children's priority
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

static SyscallResult sys_task_get_priority(u64 a0, u64, u64, u64, u64, u64)
{
    u32 task_id = static_cast<u32>(a0);

    task::Task *target = task::get_by_id(task_id);
    if (!target)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    return SyscallResult::ok(static_cast<u64>(task::get_priority(target)));
}

static SyscallResult sys_wait(u64 a0, u64, u64, u64, u64, u64)
{
    i32 *status = reinterpret_cast<i32 *>(a0);

    if (status && !validate_user_write(status, sizeof(i32)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    // Wait for any child (-1)
    i64 result = viper::wait(-1, status);
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok(static_cast<u64>(result));
}

static SyscallResult sys_waitpid(u64 a0, u64 a1, u64, u64, u64, u64)
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

static SyscallResult sys_fork(u64, u64, u64, u64, u64, u64)
{
    // Fork creates a child process with copy-on-write semantics
    viper::Viper *child = viper::fork();
    if (!child)
    {
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    // Create a task for the child process
    // The child task will return 0 from fork, parent returns child pid
    task::Task *parent_task = task::current();
    if (!parent_task)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    // Create child task that starts at same instruction as parent
    task::Task *child_task = task::create_user_task(
        child->name, child, parent_task->user_entry, parent_task->user_stack);
    if (!child_task)
    {
        viper::destroy(child);
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    // Copy parent's trap frame to child for register state
    if (parent_task->trap_frame && child_task->trap_frame)
    {
        // Copy all registers from parent
        for (int i = 0; i < 31; i++)
        {
            child_task->trap_frame->x[i] = parent_task->trap_frame->x[i];
        }
        child_task->trap_frame->sp = parent_task->trap_frame->sp;
        child_task->trap_frame->elr = parent_task->trap_frame->elr;
        child_task->trap_frame->spsr = parent_task->trap_frame->spsr;

        // Child returns 0 from fork
        child_task->trap_frame->x[0] = 0;
    }

    // Enqueue child task to run
    scheduler::enqueue(child_task);

    // Parent returns child's process ID
    return SyscallResult::ok(child->id);
}

static SyscallResult sys_sbrk(u64 a0, u64, u64, u64, u64, u64)
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

    // Validate new break
    if (increment > 0 && new_break < old_break)
    {
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY); // Overflow
    }
    if (increment < 0 && new_break > old_break)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG); // Underflow
    }
    if (new_break < v->heap_start)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }
    if (new_break > v->heap_max)
    {
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    // Allocate pages for positive increment
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

// Helper to get cap_table from current task
static cap::Table *get_current_cap_table()
{
    task::Task *t = task::current();
    if (!t || !t->viper)
        return nullptr;
    viper::Viper *v = reinterpret_cast<viper::Viper *>(t->viper);
    return v->cap_table;
}

// --- Channel IPC (0x10-0x1F) ---

static SyscallResult sys_channel_create(u64, u64, u64, u64, u64, u64)
{
    cap::Table *table = get_current_cap_table();
    if (!table)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    // Create a new legacy channel ID (send_refs=1, recv_refs=1).
    i64 channel_id = channel::create();
    if (channel_id < 0)
    {
        return SyscallResult::err(channel_id);
    }

    // Create distinct kobj::Channel wrappers for each endpoint without changing refcounts.
    kobj::Channel *send_ep =
        kobj::Channel::adopt(static_cast<u32>(channel_id), kobj::Channel::ENDPOINT_SEND);
    kobj::Channel *recv_ep =
        kobj::Channel::adopt(static_cast<u32>(channel_id), kobj::Channel::ENDPOINT_RECV);
    if (!send_ep || !recv_ep)
    {
        delete send_ep;
        delete recv_ep;
        (void)channel::close(static_cast<u32>(channel_id));
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    // Insert send endpoint handle.
    cap::Handle send_handle = table->insert(
        send_ep, cap::Kind::Channel, cap::CAP_WRITE | cap::CAP_TRANSFER | cap::CAP_DERIVE);
    if (send_handle == cap::HANDLE_INVALID)
    {
        delete send_ep;
        delete recv_ep;
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    // Insert recv endpoint handle.
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

static SyscallResult sys_channel_send(u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64)
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
    channel::Channel *low_ch = channel::get(ch->id());
    if (!low_ch)
    {
        return SyscallResult::err(error::VERR_INVALID_HANDLE);
    }

    i64 result = channel::try_send(low_ch, data, size, handles, handle_count);

    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok(static_cast<u64>(result));
}

static SyscallResult sys_channel_recv(u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64)
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
    channel::Channel *low_ch = channel::get(ch->id());
    if (!low_ch)
    {
        return SyscallResult::err(error::VERR_INVALID_HANDLE);
    }

    cap::Handle tmp_handles[channel::MAX_HANDLES_PER_MSG];
    u32 tmp_handle_count = 0;
    i64 result = channel::try_recv(low_ch, data, size, tmp_handles, &tmp_handle_count);

    if (result < 0)
    {
        return SyscallResult::err(result);
    }

    if (handle_count)
    {
        *handle_count = tmp_handle_count;
    }
    u32 copy_count = tmp_handle_count;
    if (copy_count > max_handles)
    {
        copy_count = max_handles;
    }
    if (handles && copy_count > 0)
    {
        for (u32 i = 0; i < copy_count; i++)
        {
            handles[i] = tmp_handles[i];
        }
    }

    return SyscallResult::ok(static_cast<u64>(result), static_cast<u64>(tmp_handle_count));
}

static SyscallResult sys_channel_close(u64 a0, u64, u64, u64, u64, u64)
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

    // Delete the kobj::Channel object
    kobj::Channel *ch = static_cast<kobj::Channel *>(entry->object);
    delete ch;

    table->remove(handle);
    return SyscallResult::ok();
}

// --- Poll (0x20-0x2F) ---

static SyscallResult sys_poll_create(u64, u64, u64, u64, u64, u64)
{
    // Create a poll set and return its ID directly
    i64 ps_id = pollset::create();
    if (ps_id < 0)
    {
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    return SyscallResult::ok(static_cast<u64>(ps_id));
}

static SyscallResult sys_poll_add(u64 a0, u64 a1, u64 a2, u64, u64, u64)
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

static SyscallResult sys_poll_remove(u64 a0, u64 a1, u64, u64, u64, u64)
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

static SyscallResult sys_poll_wait(u64 a0, u64 a1, u64 a2, u64 a3, u64, u64)
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

// --- Time (0x30-0x3F) ---

static SyscallResult sys_time_now(u64, u64, u64, u64, u64, u64)
{
    return SyscallResult::ok(timer::get_ms());
}

static SyscallResult sys_sleep(u64 a0, u64, u64, u64, u64, u64)
{
    u64 ms = a0;
    if (ms == 0)
    {
        task::yield();
    }
    else
    {
        poll::sleep_ms(ms);
    }
    return SyscallResult::ok();
}

// --- File I/O (0x40-0x4F) ---

static SyscallResult sys_open(u64 a0, u64 a1, u64, u64, u64, u64)
{
    const char *path = reinterpret_cast<const char *>(a0);
    u32 flags = static_cast<u32>(a1);

    if (validate_user_string(path, viper::MAX_PATH) < 0)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    i64 result = fs::vfs::open(path, flags);
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok(static_cast<u64>(result));
}

static SyscallResult sys_close(u64 a0, u64, u64, u64, u64, u64)
{
    i32 fd = static_cast<i32>(a0);

    // stdin/stdout/stderr are pseudo-FDs backed by the console.
    if (fd >= 0 && fd <= 2)
    {
        return SyscallResult::ok();
    }

    i64 result = fs::vfs::close(fd);
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok();
}

static SyscallResult sys_read(u64 a0, u64 a1, u64 a2, u64, u64, u64)
{
    i32 fd = static_cast<i32>(a0);
    void *buf = reinterpret_cast<void *>(a1);
    usize count = static_cast<usize>(a2);

    if (count == 0)
    {
        return SyscallResult::ok(0);
    }

    if (!validate_user_write(buf, count))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    // stdin: read from console input (blocking until at least 1 byte).
    if (fd == 0)
    {
        char *out = reinterpret_cast<char *>(buf);
        usize n = 0;
        while (n < count)
        {
            console::poll_input();
            i32 c = console::getchar();
            if (c < 0)
            {
                if (n > 0)
                {
                    break;
                }
                task::yield();
                continue;
            }
            out[n++] = static_cast<char>(c);
        }
        return SyscallResult::ok(static_cast<u64>(n));
    }

    i64 result = fs::vfs::read(fd, buf, count);
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok(static_cast<u64>(result));
}

static SyscallResult sys_write(u64 a0, u64 a1, u64 a2, u64, u64, u64)
{
    i32 fd = static_cast<i32>(a0);
    const void *buf = reinterpret_cast<const void *>(a1);
    usize count = static_cast<usize>(a2);

    if (count == 0)
    {
        return SyscallResult::ok(0);
    }

    if (!validate_user_read(buf, count))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    // stdout/stderr: write to console output.
    if (fd == 1 || fd == 2)
    {
        const char *p = reinterpret_cast<const char *>(buf);
        for (usize i = 0; i < count; i++)
        {
            serial::putc(p[i]);
            if (gcon::is_available())
            {
                gcon::putc(p[i]);
            }
        }
        return SyscallResult::ok(static_cast<u64>(count));
    }

    i64 result = fs::vfs::write(fd, buf, count);
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok(static_cast<u64>(result));
}

static SyscallResult sys_lseek(u64 a0, u64 a1, u64 a2, u64, u64, u64)
{
    i32 fd = static_cast<i32>(a0);
    i64 offset = static_cast<i64>(a1);
    i32 whence = static_cast<i32>(a2);

    i64 result = fs::vfs::lseek(fd, offset, whence);
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok(static_cast<u64>(result));
}

static SyscallResult sys_stat(u64 a0, u64 a1, u64, u64, u64, u64)
{
    const char *path = reinterpret_cast<const char *>(a0);
    fs::vfs::Stat *st = reinterpret_cast<fs::vfs::Stat *>(a1);

    if (validate_user_string(path, viper::MAX_PATH) < 0 ||
        !validate_user_write(st, sizeof(fs::vfs::Stat)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    i64 result = fs::vfs::stat(path, st);
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok();
}

static SyscallResult sys_fstat(u64 a0, u64 a1, u64, u64, u64, u64)
{
    i32 fd = static_cast<i32>(a0);
    fs::vfs::Stat *st = reinterpret_cast<fs::vfs::Stat *>(a1);

    if (!validate_user_write(st, sizeof(fs::vfs::Stat)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    i64 result = fs::vfs::fstat(fd, st);
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok();
}

static SyscallResult sys_dup(u64 a0, u64, u64, u64, u64, u64)
{
    i32 fd = static_cast<i32>(a0);
    i64 result = fs::vfs::dup(fd);
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok(static_cast<u64>(result));
}

static SyscallResult sys_dup2(u64 a0, u64 a1, u64, u64, u64, u64)
{
    i32 oldfd = static_cast<i32>(a0);
    i32 newfd = static_cast<i32>(a1);
    i64 result = fs::vfs::dup2(oldfd, newfd);
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok(static_cast<u64>(result));
}

// --- Networking (0x50-0x5F) ---

#if VIPER_KERNEL_ENABLE_NET
static SyscallResult sys_socket_create(u64, u64, u64, u64, u64, u64)
{
    viper::Viper *v = viper::current();
    if (!v)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    i64 result = net::tcp::socket_create(static_cast<u32>(v->id));
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok(static_cast<u64>(result));
}

static SyscallResult sys_socket_connect(u64 a0, u64 a1, u64 a2, u64, u64, u64)
{
    i32 sock = static_cast<i32>(a0);
    u32 ip_raw = static_cast<u32>(a1);
    u16 port = static_cast<u16>(a2);

    viper::Viper *v = viper::current();
    if (!v || !net::tcp::socket_owned_by(sock, static_cast<u32>(v->id)))
    {
        return SyscallResult::err(error::VERR_INVALID_HANDLE);
    }

    // Convert u32 IP to Ipv4Addr
    net::Ipv4Addr ip;
    ip.bytes[0] = (ip_raw >> 24) & 0xFF;
    ip.bytes[1] = (ip_raw >> 16) & 0xFF;
    ip.bytes[2] = (ip_raw >> 8) & 0xFF;
    ip.bytes[3] = ip_raw & 0xFF;

#if VIPER_KERNEL_DEBUG_NET_SYSCALL
    serial::puts("[syscall] socket_connect: sock=");
    serial::put_dec(sock);
    serial::puts(" ip=");
    serial::put_dec(ip.bytes[0]);
    serial::putc('.');
    serial::put_dec(ip.bytes[1]);
    serial::putc('.');
    serial::put_dec(ip.bytes[2]);
    serial::putc('.');
    serial::put_dec(ip.bytes[3]);
    serial::puts(" port=");
    serial::put_dec(port);
    serial::putc('\n');
#endif

    bool result = net::tcp::socket_connect(sock, ip, port);
#if VIPER_KERNEL_DEBUG_NET_SYSCALL
    serial::puts("[syscall] socket_connect: result=");
    serial::puts(result ? "true" : "false");
    serial::putc('\n');
#endif

    if (!result)
    {
        return SyscallResult::err(error::VERR_CONNECTION);
    }
    return SyscallResult::ok();
}

static SyscallResult sys_socket_send(u64 a0, u64 a1, u64 a2, u64, u64, u64)
{
    i32 sock = static_cast<i32>(a0);
    const void *buf = reinterpret_cast<const void *>(a1);
    usize len = static_cast<usize>(a2);

    viper::Viper *v = viper::current();
    if (!v || !net::tcp::socket_owned_by(sock, static_cast<u32>(v->id)))
    {
        return SyscallResult::err(error::VERR_INVALID_HANDLE);
    }

#if VIPER_KERNEL_DEBUG_NET_SYSCALL
    serial::puts("[syscall] socket_send: sock=");
    serial::put_dec(sock);
    serial::puts(" len=");
    serial::put_dec(len);
    serial::putc('\n');
#endif

    if (!validate_user_read(buf, len))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    i64 result = net::tcp::socket_send(sock, buf, len);
#if VIPER_KERNEL_DEBUG_NET_SYSCALL
    serial::puts("[syscall] socket_send: result=");
    serial::put_dec(result);
    serial::putc('\n');
#endif

    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok(static_cast<u64>(result));
}

static SyscallResult sys_socket_recv(u64 a0, u64 a1, u64 a2, u64, u64, u64)
{
    i32 sock = static_cast<i32>(a0);
    void *buf = reinterpret_cast<void *>(a1);
    usize len = static_cast<usize>(a2);

    viper::Viper *v = viper::current();
    if (!v || !net::tcp::socket_owned_by(sock, static_cast<u32>(v->id)))
    {
        return SyscallResult::err(error::VERR_INVALID_HANDLE);
    }

#if VIPER_KERNEL_DEBUG_NET_SYSCALL
    serial::puts("[syscall] socket_recv: sock=");
    serial::put_dec(sock);
    serial::puts(" len=");
    serial::put_dec(len);
    serial::putc('\n');
#endif

    if (!validate_user_write(buf, len))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    i64 result = net::tcp::socket_recv(sock, buf, len);
#if VIPER_KERNEL_DEBUG_NET_SYSCALL
    serial::puts("[syscall] socket_recv: result=");
    serial::put_dec(result);
    serial::putc('\n');
#endif

    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok(static_cast<u64>(result));
}

static SyscallResult sys_socket_close(u64 a0, u64, u64, u64, u64, u64)
{
    i32 sock = static_cast<i32>(a0);

    viper::Viper *v = viper::current();
    if (!v || !net::tcp::socket_owned_by(sock, static_cast<u32>(v->id)))
    {
        return SyscallResult::err(error::VERR_INVALID_HANDLE);
    }

    net::tcp::socket_close(sock);
    return SyscallResult::ok();
}

static SyscallResult sys_dns_resolve(u64 a0, u64 a1, u64, u64, u64, u64)
{
    const char *hostname = reinterpret_cast<const char *>(a0);
    u32 *ip_out = reinterpret_cast<u32 *>(a1);

    if (validate_user_string(hostname, 256) < 0 || !validate_user_write(ip_out, sizeof(u32)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    net::Ipv4Addr result_ip;
    if (!net::dns::resolve(hostname, &result_ip, 5000))
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    // Convert Ipv4Addr to u32 in network byte order (for struct in_addr.s_addr)
    // On little-endian, s_addr stores bytes[0] in lowest address = lowest bits
    *ip_out = static_cast<u32>(result_ip.bytes[0]) | (static_cast<u32>(result_ip.bytes[1]) << 8) |
              (static_cast<u32>(result_ip.bytes[2]) << 16) |
              (static_cast<u32>(result_ip.bytes[3]) << 24);
    return SyscallResult::ok();
}
#else
static SyscallResult sys_socket_create(u64, u64, u64, u64, u64, u64)
{
    return SyscallResult::err(error::VERR_NOT_SUPPORTED);
}

static SyscallResult sys_socket_connect(u64, u64, u64, u64, u64, u64)
{
    return SyscallResult::err(error::VERR_NOT_SUPPORTED);
}

static SyscallResult sys_socket_send(u64, u64, u64, u64, u64, u64)
{
    return SyscallResult::err(error::VERR_NOT_SUPPORTED);
}

static SyscallResult sys_socket_recv(u64, u64, u64, u64, u64, u64)
{
    return SyscallResult::err(error::VERR_NOT_SUPPORTED);
}

static SyscallResult sys_socket_close(u64, u64, u64, u64, u64, u64)
{
    return SyscallResult::err(error::VERR_NOT_SUPPORTED);
}

static SyscallResult sys_dns_resolve(u64, u64, u64, u64, u64, u64)
{
    return SyscallResult::err(error::VERR_NOT_SUPPORTED);
}
#endif

// --- Directory/FS (0x60-0x6F) ---

static SyscallResult sys_readdir(u64 a0, u64 a1, u64 a2, u64, u64, u64)
{
    i32 fd = static_cast<i32>(a0);
    void *buf = reinterpret_cast<void *>(a1);
    usize count = static_cast<usize>(a2);

    if (!validate_user_write(buf, count))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    i64 result = fs::vfs::getdents(fd, buf, count);
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok(static_cast<u64>(result));
}

static SyscallResult sys_mkdir(u64 a0, u64, u64, u64, u64, u64)
{
    const char *path = reinterpret_cast<const char *>(a0);

    if (validate_user_string(path, viper::MAX_PATH) < 0)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    i64 result = fs::vfs::mkdir(path);
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok();
}

static SyscallResult sys_rmdir(u64 a0, u64, u64, u64, u64, u64)
{
    const char *path = reinterpret_cast<const char *>(a0);

    if (validate_user_string(path, viper::MAX_PATH) < 0)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    i64 result = fs::vfs::rmdir(path);
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok();
}

static SyscallResult sys_unlink(u64 a0, u64, u64, u64, u64, u64)
{
    const char *path = reinterpret_cast<const char *>(a0);

    if (validate_user_string(path, viper::MAX_PATH) < 0)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    i64 result = fs::vfs::unlink(path);
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok();
}

static SyscallResult sys_rename(u64 a0, u64 a1, u64, u64, u64, u64)
{
    const char *old_path = reinterpret_cast<const char *>(a0);
    const char *new_path = reinterpret_cast<const char *>(a1);

    if (validate_user_string(old_path, viper::MAX_PATH) < 0 ||
        validate_user_string(new_path, viper::MAX_PATH) < 0)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    i64 result = fs::vfs::rename(old_path, new_path);
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok();
}

static SyscallResult sys_symlink(u64 a0, u64 a1, u64, u64, u64, u64)
{
    const char *target = reinterpret_cast<const char *>(a0);
    const char *linkpath = reinterpret_cast<const char *>(a1);

    if (validate_user_string(target, viper::MAX_PATH) < 0 ||
        validate_user_string(linkpath, viper::MAX_PATH) < 0)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    i64 result = fs::vfs::symlink(target, linkpath);
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok();
}

static SyscallResult sys_readlink(u64 a0, u64 a1, u64 a2, u64, u64, u64)
{
    const char *path = reinterpret_cast<const char *>(a0);
    char *buf = reinterpret_cast<char *>(a1);
    usize bufsiz = static_cast<usize>(a2);

    if (validate_user_string(path, viper::MAX_PATH) < 0 || !validate_user_write(buf, bufsiz))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    i64 result = fs::vfs::readlink(path, buf, bufsiz);
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok(static_cast<u64>(result));
}

static SyscallResult sys_getcwd(u64 a0, u64 a1, u64, u64, u64, u64)
{
    char *buf = reinterpret_cast<char *>(a0);
    usize size = static_cast<usize>(a1);

    if (!validate_user_write(buf, size))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    task::Task *t = task::current();
    if (!t)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    // Copy cwd to user buffer
    usize len = 0;
    while (len < sizeof(t->cwd) && t->cwd[len])
    {
        len++;
    }

    if (len + 1 > size)
    {
        return SyscallResult::err(error::VERR_BUFFER_TOO_SMALL);
    }

    for (usize i = 0; i <= len; i++)
    {
        buf[i] = t->cwd[i];
    }

    return SyscallResult::ok(static_cast<u64>(len));
}

static SyscallResult sys_chdir(u64 a0, u64, u64, u64, u64, u64)
{
    const char *path = reinterpret_cast<const char *>(a0);

    if (validate_user_string(path, viper::MAX_PATH) < 0)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    task::Task *t = task::current();
    if (!t)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    // Normalize and validate the path
    char normalized[viper::MAX_PATH];
    if (!fs::vfs::normalize_path(path, t->cwd, normalized, sizeof(normalized)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    // Verify path exists and is a directory
    i64 result = fs::vfs::open(normalized, 0);
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    fs::vfs::close(static_cast<i32>(result));

    // Update task's cwd
    usize len = 0;
    while (len < sizeof(t->cwd) - 1 && normalized[len])
    {
        t->cwd[len] = normalized[len];
        len++;
    }
    t->cwd[len] = '\0';

    return SyscallResult::ok();
}

// --- Capability (0x70-0x7F) ---

static SyscallResult sys_cap_derive(u64 a0, u64 a1, u64, u64, u64, u64)
{
    cap::Handle src = static_cast<cap::Handle>(a0);
    cap::Rights new_rights = static_cast<cap::Rights>(a1);

    cap::Table *table = get_current_cap_table();
    if (!table)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    cap::Handle new_handle = table->derive(src, new_rights);
    if (new_handle == cap::HANDLE_INVALID)
    {
        return SyscallResult::err(error::VERR_INVALID_HANDLE);
    }

    return SyscallResult::ok(static_cast<u64>(new_handle));
}

static SyscallResult sys_cap_revoke(u64 a0, u64, u64, u64, u64, u64)
{
    cap::Handle handle = static_cast<cap::Handle>(a0);

    cap::Table *table = get_current_cap_table();
    if (!table)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    // Check if handle is valid before revoking
    cap::Entry *entry = table->get(handle);
    if (!entry)
    {
        return SyscallResult::err(error::VERR_INVALID_HANDLE);
    }

    // Revoke with propagation - also revokes all derived handles
    u32 revoked = table->revoke(handle);
    return SyscallResult::ok(static_cast<u64>(revoked));
}

static SyscallResult sys_cap_query(u64 a0, u64 a1, u64, u64, u64, u64)
{
    cap::Handle handle = static_cast<cap::Handle>(a0);
    CapInfo *info = reinterpret_cast<CapInfo *>(a1);

    if (!validate_user_write(info, sizeof(CapInfo)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    cap::Table *table = get_current_cap_table();
    if (!table)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    cap::Entry *entry = table->get(handle);
    if (!entry)
    {
        return SyscallResult::err(error::VERR_INVALID_HANDLE);
    }

    info->handle = handle;
    info->kind = static_cast<u32>(entry->kind);
    info->rights = entry->rights;
    info->generation = entry->generation;
    return SyscallResult::ok();
}

static SyscallResult sys_cap_list(u64 a0, u64 a1, u64, u64, u64, u64)
{
    CapListEntry *entries = reinterpret_cast<CapListEntry *>(a0);
    u32 max_entries = static_cast<u32>(a1);

    if (!validate_user_write(entries, max_entries * sizeof(CapListEntry)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    cap::Table *table = get_current_cap_table();
    if (!table)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    // Manually iterate through entries
    u32 count = 0;
    for (usize i = 0; i < table->capacity() && count < max_entries; i++)
    {
        cap::Entry *e = table->entry_at(i);
        if (e && e->kind != cap::Kind::Invalid)
        {
            entries[count].handle = cap::make_handle(static_cast<u32>(i), e->generation);
            entries[count].kind = static_cast<u32>(e->kind);
            entries[count].rights = e->rights;
            count++;
        }
    }
    return SyscallResult::ok(static_cast<u64>(count));
}

// --- Handle-based FS (0x80-0x8F) ---

static SyscallResult sys_fs_open_root(u64, u64, u64, u64, u64, u64)
{
    cap::Table *table = get_current_cap_table();
    if (!table)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    // Create directory object for root inode (inode 2 is typically root)
    kobj::DirObject *dir = kobj::DirObject::create(2);
    if (!dir)
    {
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    cap::Handle h =
        table->insert(dir, cap::Kind::Directory, cap::CAP_READ | cap::CAP_WRITE | cap::CAP_DERIVE);
    if (h == cap::HANDLE_INVALID)
    {
        delete dir;
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    return SyscallResult::ok(static_cast<u64>(h));
}

static SyscallResult sys_fs_open(u64 a0, u64 a1, u64 a2, u64 a3, u64, u64)
{
    cap::Handle dir_handle = static_cast<cap::Handle>(a0);
    const char *name = reinterpret_cast<const char *>(a1);
    usize name_len = static_cast<usize>(a2);
    u32 flags = static_cast<u32>(a3);

    if (!validate_user_read(name, name_len))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    cap::Table *table = get_current_cap_table();
    if (!table)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    cap::Entry *entry = table->get_checked(dir_handle, cap::Kind::Directory);
    if (!entry)
    {
        return SyscallResult::err(error::VERR_INVALID_HANDLE);
    }

    kobj::DirObject *dir = static_cast<kobj::DirObject *>(entry->object);

    // Look up the child entry in the directory
    u64 child_inode = 0;
    u8 child_type = 0;
    if (!dir->lookup(name, name_len, &child_inode, &child_type))
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    // Create appropriate object based on type
    kobj::Object *new_obj = nullptr;
    cap::Kind kind;
    if (child_type == 2)
    { // Directory
        new_obj = kobj::DirObject::create(child_inode);
        kind = cap::Kind::Directory;
    }
    else
    { // File
        new_obj = kobj::FileObject::create(child_inode, flags);
        kind = cap::Kind::File;
    }

    if (!new_obj)
    {
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    cap::Handle h = table->insert(new_obj, kind, cap::CAP_READ | cap::CAP_WRITE);
    if (h == cap::HANDLE_INVALID)
    {
        delete new_obj;
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    return SyscallResult::ok(static_cast<u64>(h));
}

static SyscallResult sys_io_read(u64 a0, u64 a1, u64 a2, u64, u64, u64)
{
    cap::Handle handle = static_cast<cap::Handle>(a0);
    void *buf = reinterpret_cast<void *>(a1);
    usize count = static_cast<usize>(a2);

    if (!validate_user_write(buf, count))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    cap::Table *table = get_current_cap_table();
    if (!table)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    cap::Entry *entry = table->get_with_rights(handle, cap::Kind::File, cap::CAP_READ);
    if (!entry)
    {
        return SyscallResult::err(error::VERR_INVALID_HANDLE);
    }

    kobj::FileObject *file = static_cast<kobj::FileObject *>(entry->object);
    i64 result = file->read(buf, count);
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok(static_cast<u64>(result));
}

static SyscallResult sys_io_write(u64 a0, u64 a1, u64 a2, u64, u64, u64)
{
    cap::Handle handle = static_cast<cap::Handle>(a0);
    const void *buf = reinterpret_cast<const void *>(a1);
    usize count = static_cast<usize>(a2);

    if (!validate_user_read(buf, count))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    cap::Table *table = get_current_cap_table();
    if (!table)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    cap::Entry *entry = table->get_with_rights(handle, cap::Kind::File, cap::CAP_WRITE);
    if (!entry)
    {
        return SyscallResult::err(error::VERR_INVALID_HANDLE);
    }

    kobj::FileObject *file = static_cast<kobj::FileObject *>(entry->object);
    i64 result = file->write(buf, count);
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok(static_cast<u64>(result));
}

static SyscallResult sys_io_seek(u64 a0, u64 a1, u64 a2, u64, u64, u64)
{
    cap::Handle handle = static_cast<cap::Handle>(a0);
    i64 offset = static_cast<i64>(a1);
    i32 whence = static_cast<i32>(a2);

    cap::Table *table = get_current_cap_table();
    if (!table)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    cap::Entry *entry = table->get_checked(handle, cap::Kind::File);
    if (!entry)
    {
        return SyscallResult::err(error::VERR_INVALID_HANDLE);
    }

    kobj::FileObject *file = static_cast<kobj::FileObject *>(entry->object);
    i64 result = file->seek(offset, whence);
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok(static_cast<u64>(result));
}

static SyscallResult sys_fs_read_dir(u64 a0, u64 a1, u64, u64, u64, u64)
{
    cap::Handle handle = static_cast<cap::Handle>(a0);
    kobj::FsDirEnt *ent = reinterpret_cast<kobj::FsDirEnt *>(a1);

    if (!validate_user_write(ent, sizeof(kobj::FsDirEnt)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    cap::Table *table = get_current_cap_table();
    if (!table)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    cap::Entry *entry = table->get_with_rights(handle, cap::Kind::Directory, cap::CAP_READ);
    if (!entry)
    {
        return SyscallResult::err(error::VERR_INVALID_HANDLE);
    }

    kobj::DirObject *dir = static_cast<kobj::DirObject *>(entry->object);
    if (!dir->read_next(ent))
    {
        return SyscallResult::ok(0); // End of directory
    }
    return SyscallResult::ok(1);
}

static SyscallResult sys_fs_close(u64 a0, u64, u64, u64, u64, u64)
{
    cap::Handle handle = static_cast<cap::Handle>(a0);

    cap::Table *table = get_current_cap_table();
    if (!table)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    cap::Entry *entry = table->get(handle);
    if (!entry)
    {
        return SyscallResult::err(error::VERR_INVALID_HANDLE);
    }

    table->remove(handle);
    return SyscallResult::ok();
}

static SyscallResult sys_fs_rewind_dir(u64 a0, u64, u64, u64, u64, u64)
{
    cap::Handle handle = static_cast<cap::Handle>(a0);

    cap::Table *table = get_current_cap_table();
    if (!table)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    cap::Entry *entry = table->get_checked(handle, cap::Kind::Directory);
    if (!entry)
    {
        return SyscallResult::err(error::VERR_INVALID_HANDLE);
    }

    kobj::DirObject *dir = static_cast<kobj::DirObject *>(entry->object);
    dir->rewind();
    return SyscallResult::ok();
}

// --- Assign (0xC0-0xCF) ---

static SyscallResult sys_assign_set(u64 a0, u64 a1, u64 a2, u64, u64, u64)
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

static SyscallResult sys_assign_get(u64 a0, u64, u64, u64, u64, u64)
{
    const char *name = reinterpret_cast<const char *>(a0);

    if (validate_user_string(name, viper::assign::MAX_ASSIGN_NAME) < 0)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    // First check if it's a service assign (creates new send endpoint)
    cap::Handle channel = viper::assign::get_channel(name);
    if (channel != cap::HANDLE_INVALID)
    {
        return SyscallResult::ok(static_cast<u64>(channel));
    }

    // Not a service - try as directory assign
    cap::Handle handle = viper::assign::get(name);
    if (handle == cap::HANDLE_INVALID)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }
    return SyscallResult::ok(static_cast<u64>(handle));
}

static SyscallResult sys_assign_remove(u64 a0, u64, u64, u64, u64, u64)
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

static SyscallResult sys_assign_list(u64 a0, u64 a1, u64, u64, u64, u64)
{
    viper::assign::AssignInfo *buf = reinterpret_cast<viper::assign::AssignInfo *>(a0);
    int max_count = static_cast<int>(a1);

    if (max_count > 0 && !validate_user_write(buf, max_count * sizeof(viper::assign::AssignInfo)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    int count = viper::assign::list(buf, max_count);
    return SyscallResult::ok(static_cast<u64>(count));
}

static SyscallResult sys_assign_resolve(u64 a0, u64 a1, u64, u64, u64, u64)
{
    const char *path = reinterpret_cast<const char *>(a0);
    u32 flags = static_cast<u32>(a1);

    if (validate_user_string(path, viper::MAX_PATH) < 0)
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

// --- TLS (0xD0-0xDF) ---

#if VIPER_KERNEL_ENABLE_TLS
static SyscallResult sys_tls_create(u64 a0, u64, u64, u64, u64, u64)
{
    i32 socket_fd = static_cast<i32>(a0);

    kernel::SpinlockGuard lock(tls_lock);

    // Find free slot
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
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    if (!viper::tls::tls_init(&tls_sessions[slot], socket_fd, nullptr))
    {
        return SyscallResult::err(error::VERR_IO);
    }

    tls_session_active[slot] = true;
    return SyscallResult::ok(static_cast<u64>(slot));
}

static SyscallResult sys_tls_handshake(u64 a0, u64 a1, u64, u64, u64, u64)
{
    i32 session_id = static_cast<i32>(a0);
    const char *hostname = reinterpret_cast<const char *>(a1);

    if (session_id < 0 || session_id >= MAX_TLS_SESSIONS || !tls_session_active[session_id])
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    if (hostname && validate_user_string(hostname, 256) < 0)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    if (viper::tls::tls_handshake(&tls_sessions[session_id]))
    {
        return SyscallResult::ok();
    }
    return SyscallResult::err(error::VERR_IO);
}

static SyscallResult sys_tls_send(u64 a0, u64 a1, u64 a2, u64, u64, u64)
{
    i32 session_id = static_cast<i32>(a0);
    const void *data = reinterpret_cast<const void *>(a1);
    usize len = static_cast<usize>(a2);

    if (session_id < 0 || session_id >= MAX_TLS_SESSIONS || !tls_session_active[session_id])
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    if (!validate_user_read(data, len))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    i64 result = viper::tls::tls_send(&tls_sessions[session_id], data, len);
    if (result < 0)
    {
        return SyscallResult::err(error::VERR_IO);
    }
    return SyscallResult::ok(static_cast<u64>(result));
}

static SyscallResult sys_tls_recv(u64 a0, u64 a1, u64 a2, u64, u64, u64)
{
    i32 session_id = static_cast<i32>(a0);
    void *buf = reinterpret_cast<void *>(a1);
    usize len = static_cast<usize>(a2);

    if (session_id < 0 || session_id >= MAX_TLS_SESSIONS || !tls_session_active[session_id])
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    if (!validate_user_write(buf, len))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    i64 result = viper::tls::tls_recv(&tls_sessions[session_id], buf, len);
    if (result < 0)
    {
        return SyscallResult::err(error::VERR_IO);
    }
    return SyscallResult::ok(static_cast<u64>(result));
}

static SyscallResult sys_tls_close(u64 a0, u64, u64, u64, u64, u64)
{
    i32 session_id = static_cast<i32>(a0);

    if (session_id < 0 || session_id >= MAX_TLS_SESSIONS || !tls_session_active[session_id])
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    kernel::SpinlockGuard lock(tls_lock);
    viper::tls::tls_close(&tls_sessions[session_id]);
    tls_session_active[session_id] = false;
    return SyscallResult::ok();
}

static SyscallResult sys_tls_info(u64 a0, u64 a1, u64, u64, u64, u64)
{
    i32 session_id = static_cast<i32>(a0);
    TLSInfo *out_info = reinterpret_cast<TLSInfo *>(a1);

    if (session_id < 0 || session_id >= MAX_TLS_SESSIONS || !tls_session_active[session_id])
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    if (!validate_user_write(out_info, sizeof(TLSInfo)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    if (viper::tls::tls_get_info(&tls_sessions[session_id], out_info))
    {
        return SyscallResult::ok();
    }
    return SyscallResult::err(error::VERR_IO);
}
#else
static SyscallResult sys_tls_create(u64, u64, u64, u64, u64, u64)
{
    return SyscallResult::err(error::VERR_NOT_SUPPORTED);
}

static SyscallResult sys_tls_handshake(u64, u64, u64, u64, u64, u64)
{
    return SyscallResult::err(error::VERR_NOT_SUPPORTED);
}

static SyscallResult sys_tls_send(u64, u64, u64, u64, u64, u64)
{
    return SyscallResult::err(error::VERR_NOT_SUPPORTED);
}

static SyscallResult sys_tls_recv(u64, u64, u64, u64, u64, u64)
{
    return SyscallResult::err(error::VERR_NOT_SUPPORTED);
}

static SyscallResult sys_tls_close(u64, u64, u64, u64, u64, u64)
{
    return SyscallResult::err(error::VERR_NOT_SUPPORTED);
}

static SyscallResult sys_tls_info(u64, u64, u64, u64, u64, u64)
{
    return SyscallResult::err(error::VERR_NOT_SUPPORTED);
}
#endif

// --- System Info (0xE0-0xEF) ---

static SyscallResult sys_mem_info(u64 a0, u64, u64, u64, u64, u64)
{
    MemInfo *info = reinterpret_cast<MemInfo *>(a0);

    if (!validate_user_write(info, sizeof(MemInfo)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    info->total_pages = pmm::get_total_pages();
    info->free_pages = pmm::get_free_pages();
    info->used_pages = info->total_pages - info->free_pages;
    info->page_size = 4096;

    // Populate byte fields from page counts
    info->total_bytes = info->total_pages * info->page_size;
    info->free_bytes = info->free_pages * info->page_size;
    info->used_bytes = info->used_pages * info->page_size;

    return SyscallResult::ok();
}

#if VIPER_KERNEL_ENABLE_NET
static SyscallResult sys_net_stats(u64 a0, u64, u64, u64, u64, u64)
{
    NetStats *stats = reinterpret_cast<NetStats *>(a0);

    if (!validate_user_write(stats, sizeof(NetStats)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    net::get_stats(stats);
    return SyscallResult::ok();
}

/**
 * @brief Send ICMP ping and return RTT.
 *
 * @param a0 IPv4 address (network byte order, big-endian)
 * @param a1 Timeout in milliseconds
 * @return RTT in milliseconds on success, negative error on failure
 */
static SyscallResult sys_ping(u64 a0, u64 a1, u64, u64, u64, u64)
{
    u32 ip_be = static_cast<u32>(a0);
    u32 timeout_ms = static_cast<u32>(a1);

    if (timeout_ms == 0)
        timeout_ms = 5000; // Default 5 second timeout

    // Convert from big-endian to our Ipv4Addr format
    net::Ipv4Addr dst;
    dst.bytes[0] = (ip_be >> 24) & 0xFF;
    dst.bytes[1] = (ip_be >> 16) & 0xFF;
    dst.bytes[2] = (ip_be >> 8) & 0xFF;
    dst.bytes[3] = ip_be & 0xFF;

    i32 rtt = net::icmp::ping(dst, timeout_ms);
    if (rtt < 0)
    {
        return SyscallResult::err(rtt);
    }
    return SyscallResult::ok(static_cast<u64>(rtt));
}
#else
static SyscallResult sys_net_stats(u64, u64, u64, u64, u64, u64)
{
    return SyscallResult::err(error::VERR_NOT_SUPPORTED);
}

static SyscallResult sys_ping(u64, u64, u64, u64, u64, u64)
{
    return SyscallResult::err(error::VERR_NOT_SUPPORTED);
}
#endif

/**
 * @brief List detected hardware devices.
 *
 * @param a0 Pointer to DeviceInfo array
 * @param a1 Maximum number of entries
 * @return Number of devices on success, negative error on failure
 */
static SyscallResult sys_device_list(u64 a0, u64 a1, u64, u64, u64, u64)
{
    struct DeviceInfo
    {
        char name[32];
        char type[16];
        u32 flags;
        u32 irq;
    };

    // Helper to copy a string into a fixed-size buffer
    auto copy_str = [](char *dst, const char *src, usize max)
    {
        usize i = 0;
        while (i < max - 1 && src[i])
        {
            dst[i] = src[i];
            i++;
        }
        dst[i] = '\0';
    };

    DeviceInfo *devices = reinterpret_cast<DeviceInfo *>(a0);
    u32 max_count = static_cast<u32>(a1);

    if (max_count > 0 && !validate_user_write(devices, max_count * sizeof(DeviceInfo)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    // Device table - static list of known devices
    struct DeviceEntry
    {
        const char *name;
        const char *type;
        u32 flags;
        u32 irq;
    };

    static const DeviceEntry device_table[] = {
        {"cpu0", "cpu", 1, 0},
        {"timer0", "timer", 1, 30},
        {"gic0", "intc", 1, 0},
        {"uart0", "serial", 1, 33},
        {"virtio-blk0", "block", 1, 48},
        {"virtio-net0", "network", 1, 49},
        {"virtio-rng0", "rng", 1, 50},
    };

    constexpr u32 total_devices = sizeof(device_table) / sizeof(device_table[0]);

    // If devices is null, just return count
    if (!devices)
    {
        return SyscallResult::ok(total_devices);
    }

    // Copy devices to user buffer
    u32 count = 0;
    for (u32 i = 0; i < total_devices && count < max_count; i++)
    {
        copy_str(devices[count].name, device_table[i].name, sizeof(devices[count].name));
        copy_str(devices[count].type, device_table[i].type, sizeof(devices[count].type));
        devices[count].flags = device_table[i].flags;
        devices[count].irq = device_table[i].irq;
        count++;
    }

    return SyscallResult::ok(count);
}

// --- Debug/Console (0xF0-0xFF) ---

static SyscallResult sys_debug_print(u64 a0, u64, u64, u64, u64, u64)
{
    const char *msg = reinterpret_cast<const char *>(a0);

    if (validate_user_string(msg, 4096) < 0)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    serial::puts(msg);
    if (gcon::is_available())
    {
        gcon::puts(msg);
    }
    return SyscallResult::ok();
}

static SyscallResult sys_getchar(u64, u64, u64, u64, u64, u64)
{
    // Poll input devices to move characters from virtio-input to console buffer
    console::poll_input();

    int c = console::getchar();
    if (c < 0)
    {
        return SyscallResult::err(error::VERR_WOULD_BLOCK);
    }
    return SyscallResult::ok(static_cast<u64>(c));
}

static SyscallResult sys_putchar(u64 a0, u64, u64, u64, u64, u64)
{
    char c = static_cast<char>(a0);
    serial::putc(c);
    if (gcon::is_available())
    {
        gcon::putc(c);
    }
    return SyscallResult::ok();
}

static SyscallResult sys_uptime(u64, u64, u64, u64, u64, u64)
{
    return SyscallResult::ok(timer::get_ms());
}

// --- Signal (0x90-0x9F) ---

/**
 * @brief Set signal action for a given signal.
 *
 * @param a0 Signal number
 * @param a1 Pointer to new SigAction structure (or null)
 * @param a2 Pointer to old SigAction structure to store previous (or null)
 * @return 0 on success, error code on failure
 */
static SyscallResult sys_sigaction(u64 a0, u64 a1, u64 a2, u64, u64, u64)
{
    i32 signum = static_cast<i32>(a0);
    const signal::SigAction *act = reinterpret_cast<const signal::SigAction *>(a1);
    signal::SigAction *oldact = reinterpret_cast<signal::SigAction *>(a2);

    // Validate signal number
    if (signum <= 0 || signum >= signal::sig::NSIG)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    // SIGKILL and SIGSTOP cannot be caught or ignored
    if (signum == signal::sig::SIGKILL || signum == signal::sig::SIGSTOP)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    // Validate user pointers
    if (act && !validate_user_read(act, sizeof(signal::SigAction)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }
    if (oldact && !validate_user_write(oldact, sizeof(signal::SigAction)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    task::Task *t = task::current();
    if (!t)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    // Store old action if requested
    if (oldact)
    {
        oldact->handler = t->signals.handlers[signum];
        oldact->flags = t->signals.handler_flags[signum];
        oldact->mask = t->signals.handler_mask[signum];
    }

    // Set new action if provided
    if (act)
    {
        t->signals.handlers[signum] = act->handler;
        t->signals.handler_flags[signum] = act->flags;
        t->signals.handler_mask[signum] = act->mask;
    }

    return SyscallResult::ok();
}

/**
 * @brief Set or get the blocked signal mask.
 *
 * @param a0 How: 0=SIG_BLOCK, 1=SIG_UNBLOCK, 2=SIG_SETMASK
 * @param a1 Pointer to new mask (or null)
 * @param a2 Pointer to old mask storage (or null)
 * @return 0 on success, error code on failure
 */
static SyscallResult sys_sigprocmask(u64 a0, u64 a1, u64 a2, u64, u64, u64)
{
    i32 how = static_cast<i32>(a0);
    const u32 *set = reinterpret_cast<const u32 *>(a1);
    u32 *oldset = reinterpret_cast<u32 *>(a2);

    // Validate user pointers
    if (set && !validate_user_read(set, sizeof(u32)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }
    if (oldset && !validate_user_write(oldset, sizeof(u32)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    task::Task *t = task::current();
    if (!t)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    // Store old mask if requested
    if (oldset)
    {
        *oldset = t->signals.blocked;
    }

    // Apply new mask if provided
    if (set)
    {
        u32 new_mask = *set;

        // Cannot block SIGKILL or SIGSTOP
        new_mask &= ~((1u << signal::sig::SIGKILL) | (1u << signal::sig::SIGSTOP));

        switch (how)
        {
            case 0: // SIG_BLOCK - add signals to blocked set
                t->signals.blocked |= new_mask;
                break;
            case 1: // SIG_UNBLOCK - remove signals from blocked set
                t->signals.blocked &= ~new_mask;
                break;
            case 2: // SIG_SETMASK - set blocked set to new mask
                t->signals.blocked = new_mask;
                break;
            default:
                return SyscallResult::err(error::VERR_INVALID_ARG);
        }
    }

    return SyscallResult::ok();
}

/**
 * @brief Return from signal handler, restoring original context.
 *
 * @details
 * This syscall is called by the signal trampoline after a signal handler
 * returns. It restores the original trap frame and resumes execution.
 *
 * @return Does not return normally
 */
static SyscallResult sys_sigreturn(u64, u64, u64, u64, u64, u64)
{
    task::Task *t = task::current();
    if (!t)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    // Check if we have a saved frame from signal delivery
    if (!t->signals.saved_frame)
    {
        serial::puts("[signal] sigreturn with no saved frame\n");
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    // Restore the original context
    // This would typically be done by copying saved_frame back to the
    // exception frame, but that requires access to the current frame.
    // For now, just clear the saved frame pointer.

    serial::puts("[signal] sigreturn - restoring context\n");
    t->signals.saved_frame = nullptr;

    // In a full implementation, we would restore the saved trap frame
    // and return to user mode via eret. For now, this is a placeholder.
    return SyscallResult::ok();
}

/**
 * @brief Send a signal to a process/task.
 *
 * @param a0 Process ID (or task ID)
 * @param a1 Signal number
 * @return 0 on success, error code on failure
 */
static SyscallResult sys_kill(u64 a0, u64 a1, u64, u64, u64, u64)
{
    i64 pid = static_cast<i64>(a0);
    i32 signum = static_cast<i32>(a1);

    // Validate signal number
    if (signum <= 0 || signum >= signal::sig::NSIG)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    // Special cases for pid
    if (pid == 0)
    {
        // Send to all processes in caller's process group (not implemented)
        return SyscallResult::err(error::VERR_NOT_SUPPORTED);
    }
    else if (pid == -1)
    {
        // Send to all processes (not implemented)
        return SyscallResult::err(error::VERR_NOT_SUPPORTED);
    }
    else if (pid < -1)
    {
        // Send to process group (not implemented)
        return SyscallResult::err(error::VERR_NOT_SUPPORTED);
    }

    // Find target task
    task::Task *target = task::get_by_id(static_cast<u32>(pid));
    if (!target)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    // Send the signal
    i32 result = signal::send_signal(target, signum);
    if (result < 0)
    {
        return SyscallResult::err(error::VERR_PERMISSION);
    }

    return SyscallResult::ok();
}

/**
 * @brief Get the set of pending signals.
 *
 * @param a0 Pointer to output mask
 * @return 0 on success, error code on failure
 */
static SyscallResult sys_sigpending(u64 a0, u64, u64, u64, u64, u64)
{
    u32 *set = reinterpret_cast<u32 *>(a0);

    if (!validate_user_write(set, sizeof(u32)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    task::Task *t = task::current();
    if (!t)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    *set = t->signals.pending;
    return SyscallResult::ok();
}

// --- Process Groups/Sessions (0xA0-0xAF) ---

/**
 * @brief Get the process ID of the calling process.
 *
 * @return Process ID
 */
static SyscallResult sys_getpid(u64, u64, u64, u64, u64, u64)
{
    viper::Viper *v = viper::current();
    if (!v)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }
    return SyscallResult::ok(v->id);
}

/**
 * @brief Get the parent process ID of the calling process.
 *
 * @return Parent process ID, or 0 if no parent
 */
static SyscallResult sys_getppid(u64, u64, u64, u64, u64, u64)
{
    viper::Viper *v = viper::current();
    if (!v)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }
    if (!v->parent)
    {
        return SyscallResult::ok(0);
    }
    return SyscallResult::ok(v->parent->id);
}

/**
 * @brief Get the process group ID of a process.
 *
 * @param a0 Process ID to query (0 for current process)
 * @return Process group ID
 */
static SyscallResult sys_getpgid(u64 a0, u64, u64, u64, u64, u64)
{
    u64 pid = a0;
    i64 result = viper::getpgid(pid);
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok(static_cast<u64>(result));
}

/**
 * @brief Set the process group ID of a process.
 *
 * @param a0 Process ID to modify (0 for current process)
 * @param a1 New process group ID (0 to use target's PID)
 * @return 0 on success
 */
static SyscallResult sys_setpgid(u64 a0, u64 a1, u64, u64, u64, u64)
{
    u64 pid = a0;
    u64 pgid = a1;
    i64 result = viper::setpgid(pid, pgid);
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok();
}

/**
 * @brief Get the session ID of a process.
 *
 * @param a0 Process ID to query (0 for current process)
 * @return Session ID
 */
static SyscallResult sys_getsid(u64 a0, u64, u64, u64, u64, u64)
{
    u64 pid = a0;
    i64 result = viper::getsid(pid);
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok(static_cast<u64>(result));
}

/**
 * @brief Create a new session with the calling process as leader.
 *
 * @return New session ID on success
 */
static SyscallResult sys_setsid(u64, u64, u64, u64, u64, u64)
{
    i64 result = viper::setsid();
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok(static_cast<u64>(result));
}

/**
 * @brief Get command-line arguments for the current process.
 *
 * @param a0 Buffer to receive the arguments string
 * @param a1 Buffer size
 * @return Length of args on success (not including NUL), negative error on failure
 */
static SyscallResult sys_get_args(u64 a0, u64 a1, u64, u64, u64, u64)
{
    char *buf = reinterpret_cast<char *>(a0);
    usize bufsize = static_cast<usize>(a1);

    if (bufsize > 0 && !validate_user_write(buf, bufsize))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    viper::Viper *v = viper::current();
    if (!v)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    // Calculate length of args
    usize len = 0;
    while (len < 255 && v->args[len])
        len++;

    // If just querying length (null buffer or zero size)
    if (!buf || bufsize == 0)
    {
        return SyscallResult::ok(len);
    }

    // Copy args to buffer
    usize copy_len = (len < bufsize - 1) ? len : bufsize - 1;
    for (usize i = 0; i < copy_len; i++)
    {
        buf[i] = v->args[i];
    }
    buf[copy_len] = '\0';

    return SyscallResult::ok(len);
}

// =============================================================================
// Device Management (0x100-0x10F) - Microkernel support
// =============================================================================

/**
 * @brief IRQ ownership and wait state for user-space drivers.
 *
 * @details
 * Each IRQ can be registered to at most one user-space task.
 * When an IRQ fires, the kernel wakes any waiting task.
 */
struct IrqState
{
    u32 owner_task_id;        ///< Task ID that owns this IRQ (0 = unowned)
    u32 owner_viper_id;       ///< Viper ID that owns this IRQ
    sched::WaitQueue waiters; ///< Tasks waiting for this IRQ
    bool pending;             ///< IRQ fired but not yet delivered
    bool enabled;             ///< Whether IRQ delivery is enabled
    Spinlock lock;            ///< Per-IRQ lock
};

/// IRQ state table for user-space accessible IRQs (32-255)
static IrqState irq_states[gic::MAX_IRQS];
static bool irq_states_initialized = false;

/// Initialize IRQ states (called lazily)
static void init_irq_states()
{
    if (irq_states_initialized)
        return;

    for (u32 i = 0; i < gic::MAX_IRQS; i++)
    {
        irq_states[i].owner_task_id = 0;
        irq_states[i].owner_viper_id = 0;
        sched::wait_init(&irq_states[i].waiters);
        irq_states[i].pending = false;
        irq_states[i].enabled = false;
    }
    irq_states_initialized = true;
}

/// GIC handler for user-space IRQs - sets pending and wakes waiters.
static void user_irq_handler(u32 irq)
{
    if (irq >= gic::MAX_IRQS)
        return;

    // If nobody registered this IRQ yet, mask it to prevent interrupt storms.
    if (!irq_states_initialized)
    {
        gic::disable_irq(irq);
        return;
    }

    IrqState *state = &irq_states[irq];
    SpinlockGuard guard(state->lock);

    if (state->owner_task_id == 0)
    {
        gic::disable_irq(irq);
        state->enabled = false;
        return;
    }

    // Mask the IRQ until the owner explicitly acknowledges it via SYS_IRQ_ACK.
    gic::disable_irq(irq);
    state->enabled = false;
    state->pending = true;
    sched::wait_wake_one(&state->waiters);
}

/// Known device MMIO regions (QEMU virt machine)
struct DeviceMmioRegion
{
    const char *name;
    u64 phys_base;
    u64 size;
    u32 irq;
};

static const DeviceMmioRegion known_devices[] = {
    {"uart0", 0x09000000, 0x1000, 33},   {"rtc", 0x09010000, 0x1000, 34},
    {"gpio", 0x09030000, 0x1000, 35},    {"virtio0", 0x0a000000, 0x200, 48},
    {"virtio1", 0x0a000200, 0x200, 49},  {"virtio2", 0x0a000400, 0x200, 50},
    {"virtio3", 0x0a000600, 0x200, 51},  {"virtio4", 0x0a000800, 0x200, 52},
    {"virtio5", 0x0a000a00, 0x200, 53},  {"virtio6", 0x0a000c00, 0x200, 54},
    {"virtio7", 0x0a000e00, 0x200, 55},  {"virtio8", 0x0a001000, 0x200, 56},
    {"virtio9", 0x0a001200, 0x200, 57},  {"virtio10", 0x0a001400, 0x200, 58},
    {"virtio11", 0x0a001600, 0x200, 59}, {"virtio12", 0x0a001800, 0x200, 60},
    {"virtio13", 0x0a001a00, 0x200, 61}, {"virtio14", 0x0a001c00, 0x200, 62},
    {"virtio15", 0x0a001e00, 0x200, 63}, {"virtio16", 0x0a002000, 0x200, 64},
    {"virtio17", 0x0a002200, 0x200, 65}, {"virtio18", 0x0a002400, 0x200, 66},
    {"virtio19", 0x0a002600, 0x200, 67}, {"virtio20", 0x0a002800, 0x200, 68},
    {"virtio21", 0x0a002a00, 0x200, 69}, {"virtio22", 0x0a002c00, 0x200, 70},
    {"virtio23", 0x0a002e00, 0x200, 71}, {"virtio24", 0x0a003000, 0x200, 72},
    {"virtio25", 0x0a003200, 0x200, 73}, {"virtio26", 0x0a003400, 0x200, 74},
    {"virtio27", 0x0a003600, 0x200, 75}, {"virtio28", 0x0a003800, 0x200, 76},
    {"virtio29", 0x0a003a00, 0x200, 77}, {"virtio30", 0x0a003c00, 0x200, 78},
    {"virtio31", 0x0a003e00, 0x200, 79},
};
static constexpr u32 KNOWN_DEVICE_COUNT = sizeof(known_devices) / sizeof(known_devices[0]);

static bool device_syscalls_allowed(viper::Viper *v)
{
    // Temporary policy for bring-up: allow init (Viper ID 1) and its descendants.
    // Proper device capabilities will replace this.
    while (v)
    {
        if (v->id == 1)
        {
            return true;
        }
        v = v->parent;
    }
    return false;
}

/**
 * @brief Map device MMIO region into user address space.
 *
 * @param a0 Device physical address
 * @param a1 Size of region to map
 * @param a2 User virtual address to map at (0 = kernel chooses)
 * @return Virtual address on success, negative error on failure
 */
static SyscallResult sys_map_device(u64 a0, u64 a1, u64 a2, u64, u64, u64)
{
    u64 phys_addr = a0;
    u64 size = a1;
    u64 user_virt = a2;

    // Validate size
    if (size == 0 || size > 16 * 1024 * 1024)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    // Check capability
    viper::Viper *v = viper::current();
    if (!v || !v->cap_table)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    // Require CAP_DEVICE_ACCESS
    // For now, check if the process has any entry with device access rights
    // In a full implementation, we'd have a device capability handle
    bool has_device_access = false;
    for (usize i = 0; i < v->cap_table->capacity(); i++)
    {
        cap::Entry *e = v->cap_table->entry_at(i);
        if (e && e->kind != cap::Kind::Invalid &&
            cap::has_rights(e->rights, cap::CAP_DEVICE_ACCESS))
        {
            has_device_access = true;
            break;
        }
    }

    // Also allow init process and its descendants (bring-up policy)
    if (device_syscalls_allowed(v))
    {
        has_device_access = true;
    }

    if (!has_device_access)
    {
        return SyscallResult::err(error::VERR_PERMISSION);
    }

    // Verify this is a known device region (security check)
    bool valid_device = false;
    for (u32 i = 0; i < KNOWN_DEVICE_COUNT; i++)
    {
        if (phys_addr >= known_devices[i].phys_base &&
            phys_addr + size <= known_devices[i].phys_base + known_devices[i].size)
        {
            valid_device = true;
            break;
        }
    }

    if (!valid_device)
    {
        return SyscallResult::err(error::VERR_PERMISSION);
    }

    // Get address space
    viper::AddressSpace *as = viper::get_address_space(v);
    if (!as)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    // Choose virtual address if not specified
    if (user_virt == 0)
    {
        // Use a fixed region for device mappings (0x1_0000_0000 to 0x1_1000_0000)
        user_virt = 0x100000000ULL + (phys_addr & 0x0FFFFFFFULL);
    }

    // Align addresses
    u64 phys_aligned = pmm::page_align_down(phys_addr);
    u64 virt_aligned = pmm::page_align_down(user_virt);
    u64 size_aligned = pmm::page_align_up(size + (phys_addr - phys_aligned));

    // Map as device memory (non-cacheable)
    // Use device memory attributes and user-accessible
    if (!as->map(virt_aligned, phys_aligned, size_aligned, viper::prot::RW))
    {
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    // Return the actual virtual address (including offset)
    return SyscallResult::ok(virt_aligned + (phys_addr - phys_aligned));
}

/**
 * @brief Register to receive a specific IRQ.
 *
 * @param a0 IRQ number
 * @return 0 on success, negative error on failure
 */
static SyscallResult sys_irq_register(u64 a0, u64, u64, u64, u64, u64)
{
    u32 irq = static_cast<u32>(a0);

    // Validate IRQ number (only allow SPIs, 32-255)
    if (irq < 32 || irq >= gic::MAX_IRQS)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    // Check capability
    viper::Viper *v = viper::current();
    task::Task *t = task::current();
    if (!v || !t)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    // Require CAP_IRQ_ACCESS
    bool has_irq_access = false;
    if (v->cap_table)
    {
        for (usize i = 0; i < v->cap_table->capacity(); i++)
        {
            cap::Entry *e = v->cap_table->entry_at(i);
            if (e && e->kind != cap::Kind::Invalid &&
                cap::has_rights(e->rights, cap::CAP_IRQ_ACCESS))
            {
                has_irq_access = true;
                break;
            }
        }
    }
    if (device_syscalls_allowed(v))
    {
        has_irq_access = true;
    }

    if (!has_irq_access)
    {
        return SyscallResult::err(error::VERR_PERMISSION);
    }

    init_irq_states();

    IrqState *state = &irq_states[irq];
    SpinlockGuard guard(state->lock);

    // Don't allow userspace to steal IRQs already owned by kernel drivers.
    if (gic::has_handler(irq))
    {
        return SyscallResult::err(error::VERR_BUSY);
    }

    // Check if already owned
    if (state->owner_task_id != 0)
    {
        return SyscallResult::err(error::VERR_BUSY);
    }

    // Register ownership
    state->owner_task_id = t->id;
    state->owner_viper_id = v->id;
    state->pending = false;
    state->enabled = true;

    // Register handler and enable the IRQ in the GIC.
    gic::register_handler(irq, user_irq_handler);
    gic::enable_irq(irq);

    return SyscallResult::ok();
}

/**
 * @brief Wait for a registered IRQ to fire.
 *
 * @param a0 IRQ number
 * @param a1 Timeout in milliseconds (0 = no timeout)
 * @return 0 on success, negative error on failure
 */
static SyscallResult sys_irq_wait(u64 a0, u64 a1, u64, u64, u64, u64)
{
    u32 irq = static_cast<u32>(a0);
    u64 timeout_ms = a1;
    (void)timeout_ms; // TODO: implement timeout

    if (irq < 32 || irq >= gic::MAX_IRQS)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    task::Task *t = task::current();
    viper::Viper *v = viper::current();
    if (!t || !v)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    init_irq_states();

    IrqState *state = &irq_states[irq];

    // Check ownership
    {
        SpinlockGuard guard(state->lock);
        if (state->owner_task_id != t->id)
        {
            return SyscallResult::err(error::VERR_PERMISSION);
        }

        // If already pending, consume it immediately
        if (state->pending)
        {
            state->pending = false;
            return SyscallResult::ok();
        }

        // Add to wait queue
        sched::wait_enqueue(&state->waiters, t);
    }

    // Yield to let other tasks run while we wait
    task::yield();

    // After waking, check if IRQ fired
    {
        SpinlockGuard guard(state->lock);
        if (state->pending)
        {
            state->pending = false;
            return SyscallResult::ok();
        }
    }

    // Woken for some other reason (signal, timeout, etc.)
    return SyscallResult::ok();
}

/**
 * @brief Acknowledge an IRQ after handling.
 *
 * @param a0 IRQ number
 * @return 0 on success, negative error on failure
 */
static SyscallResult sys_irq_ack(u64 a0, u64, u64, u64, u64, u64)
{
    u32 irq = static_cast<u32>(a0);

    if (irq < 32 || irq >= gic::MAX_IRQS)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    task::Task *t = task::current();
    if (!t)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    init_irq_states();

    IrqState *state = &irq_states[irq];
    SpinlockGuard guard(state->lock);

    if (state->owner_task_id != t->id)
    {
        return SyscallResult::err(error::VERR_PERMISSION);
    }

    // Re-enable the IRQ
    state->enabled = true;
    gic::enable_irq(irq);

    return SyscallResult::ok();
}

/**
 * @brief Unregister from an IRQ.
 *
 * @param a0 IRQ number
 * @return 0 on success, negative error on failure
 */
static SyscallResult sys_irq_unregister(u64 a0, u64, u64, u64, u64, u64)
{
    u32 irq = static_cast<u32>(a0);

    if (irq < 32 || irq >= gic::MAX_IRQS)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    task::Task *t = task::current();
    if (!t)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    init_irq_states();

    IrqState *state = &irq_states[irq];
    SpinlockGuard guard(state->lock);

    if (state->owner_task_id != t->id)
    {
        return SyscallResult::err(error::VERR_PERMISSION);
    }

    // Disable the IRQ
    gic::disable_irq(irq);
    gic::register_handler(irq, nullptr);

    // Clear ownership
    state->owner_task_id = 0;
    state->owner_viper_id = 0;
    state->pending = false;
    state->enabled = false;

    // Wake any remaining waiters
    sched::wait_wake_all(&state->waiters);

    return SyscallResult::ok();
}

/// Tracking for DMA buffer allocations
struct DmaAllocation
{
    u64 phys_addr;
    u64 virt_addr;
    u64 size;
    u32 owner_viper_id;
    bool in_use;
};

static constexpr u32 MAX_DMA_ALLOCATIONS = 64;
static DmaAllocation dma_allocations[MAX_DMA_ALLOCATIONS];
static Spinlock dma_lock;
static bool dma_initialized = false;

static void init_dma_allocations()
{
    if (dma_initialized)
        return;
    for (u32 i = 0; i < MAX_DMA_ALLOCATIONS; i++)
    {
        dma_allocations[i].in_use = false;
    }
    dma_initialized = true;
}

/**
 * @brief Allocate a physically contiguous DMA buffer.
 *
 * @param a0 Size of buffer in bytes
 * @param a1 Pointer to receive physical address
 * @return Virtual address on success, negative error on failure
 */
static SyscallResult sys_dma_alloc(u64 a0, u64 a1, u64, u64, u64, u64)
{
    u64 size = a0;
    u64 *phys_out = reinterpret_cast<u64 *>(a1);

    if (size == 0 || size > 16 * 1024 * 1024)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    if (phys_out && !validate_user_write(phys_out, sizeof(u64)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    viper::Viper *v = viper::current();
    if (!v)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    // Check CAP_DMA_ACCESS
    bool has_dma_access = false;
    if (v->cap_table)
    {
        for (usize i = 0; i < v->cap_table->capacity(); i++)
        {
            cap::Entry *e = v->cap_table->entry_at(i);
            if (e && e->kind != cap::Kind::Invalid &&
                cap::has_rights(e->rights, cap::CAP_DMA_ACCESS))
            {
                has_dma_access = true;
                break;
            }
        }
    }
    if (device_syscalls_allowed(v))
    {
        has_dma_access = true;
    }

    if (!has_dma_access)
    {
        return SyscallResult::err(error::VERR_PERMISSION);
    }

    init_dma_allocations();

    // Allocate physical pages
    u64 num_pages = (size + pmm::PAGE_SIZE - 1) / pmm::PAGE_SIZE;
    u64 phys_addr = pmm::alloc_pages(num_pages);
    if (phys_addr == 0)
    {
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    // Get address space
    viper::AddressSpace *as = viper::get_address_space(v);
    if (!as)
    {
        pmm::free_pages(phys_addr, num_pages);
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    // Map into user space at a fixed DMA region (0x2_0000_0000+)
    u64 virt_addr = 0x200000000ULL;

    // Find a free slot and virtual address
    SpinlockGuard guard(dma_lock);
    u32 slot = MAX_DMA_ALLOCATIONS;
    for (u32 i = 0; i < MAX_DMA_ALLOCATIONS; i++)
    {
        if (!dma_allocations[i].in_use)
        {
            slot = i;
            break;
        }
        // Track highest used address to avoid overlap
        if (dma_allocations[i].virt_addr + dma_allocations[i].size > virt_addr)
        {
            virt_addr = pmm::page_align_up(dma_allocations[i].virt_addr + dma_allocations[i].size);
        }
    }

    if (slot == MAX_DMA_ALLOCATIONS)
    {
        pmm::free_pages(phys_addr, num_pages);
        return SyscallResult::err(error::VERR_NO_RESOURCE);
    }

    // Map the pages
    if (!as->map(virt_addr, phys_addr, num_pages * pmm::PAGE_SIZE, viper::prot::RW))
    {
        pmm::free_pages(phys_addr, num_pages);
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    // Record allocation
    dma_allocations[slot].phys_addr = phys_addr;
    dma_allocations[slot].virt_addr = virt_addr;
    dma_allocations[slot].size = num_pages * pmm::PAGE_SIZE;
    dma_allocations[slot].owner_viper_id = v->id;
    dma_allocations[slot].in_use = true;

    // Return physical address if requested
    if (phys_out)
    {
        *phys_out = phys_addr;
    }

    return SyscallResult::ok(virt_addr);
}

/**
 * @brief Free a DMA buffer.
 *
 * @param a0 Virtual address of buffer
 * @return 0 on success, negative error on failure
 */
static SyscallResult sys_dma_free(u64 a0, u64, u64, u64, u64, u64)
{
    u64 virt_addr = a0;

    viper::Viper *v = viper::current();
    if (!v)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    init_dma_allocations();

    SpinlockGuard guard(dma_lock);

    // Find the allocation
    u32 slot = MAX_DMA_ALLOCATIONS;
    for (u32 i = 0; i < MAX_DMA_ALLOCATIONS; i++)
    {
        if (dma_allocations[i].in_use && dma_allocations[i].virt_addr == virt_addr &&
            dma_allocations[i].owner_viper_id == v->id)
        {
            slot = i;
            break;
        }
    }

    if (slot == MAX_DMA_ALLOCATIONS)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    // Unmap from address space
    viper::AddressSpace *as = viper::get_address_space(v);
    if (as)
    {
        as->unmap(virt_addr, dma_allocations[slot].size);
    }

    // Free physical pages
    u64 num_pages = dma_allocations[slot].size / pmm::PAGE_SIZE;
    pmm::free_pages(dma_allocations[slot].phys_addr, num_pages);

    // Clear allocation
    dma_allocations[slot].in_use = false;

    return SyscallResult::ok();
}

/**
 * @brief Translate virtual address to physical address.
 *
 * @param a0 Virtual address
 * @return Physical address on success, negative error on failure
 */
static SyscallResult sys_virt_to_phys(u64 a0, u64, u64, u64, u64, u64)
{
    u64 virt_addr = a0;

    viper::Viper *v = viper::current();
    if (!v)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    // Check CAP_DMA_ACCESS (needed for physical address translation)
    bool has_dma_access = false;
    if (v->cap_table)
    {
        for (usize i = 0; i < v->cap_table->capacity(); i++)
        {
            cap::Entry *e = v->cap_table->entry_at(i);
            if (e && e->kind != cap::Kind::Invalid &&
                cap::has_rights(e->rights, cap::CAP_DMA_ACCESS))
            {
                has_dma_access = true;
                break;
            }
        }
    }
    if (device_syscalls_allowed(v))
    {
        has_dma_access = true;
    }

    if (!has_dma_access)
    {
        return SyscallResult::err(error::VERR_PERMISSION);
    }

    // Get address space and translate
    viper::AddressSpace *as = viper::get_address_space(v);
    if (!as)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    u64 phys_addr = as->translate(virt_addr);
    if (phys_addr == 0)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    return SyscallResult::ok(phys_addr);
}

/**
 * @brief Enumerate available devices.
 *
 * @param a0 Pointer to DeviceInfo array
 * @param a1 Maximum number of entries
 * @return Number of devices on success, negative error on failure
 */
static SyscallResult sys_device_enum(u64 a0, u64 a1, u64, u64, u64, u64)
{
    struct DeviceEnumInfo
    {
        char name[32];
        u64 phys_addr;
        u64 size;
        u32 irq;
        u32 flags;
    };

    DeviceEnumInfo *devices = reinterpret_cast<DeviceEnumInfo *>(a0);
    u32 max_count = static_cast<u32>(a1);

    if (max_count > 0 && !validate_user_write(devices, max_count * sizeof(DeviceEnumInfo)))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    // If devices is null, just return count
    if (!devices)
    {
        return SyscallResult::ok(KNOWN_DEVICE_COUNT);
    }

    // Copy devices to user buffer
    u32 count = 0;
    for (u32 i = 0; i < KNOWN_DEVICE_COUNT && count < max_count; i++)
    {
        // Copy name
        const char *src = known_devices[i].name;
        usize j = 0;
        while (j < 31 && src[j])
        {
            devices[count].name[j] = src[j];
            j++;
        }
        devices[count].name[j] = '\0';

        devices[count].phys_addr = known_devices[i].phys_base;
        devices[count].size = known_devices[i].size;
        devices[count].irq = known_devices[i].irq;
        devices[count].flags = 1; // Available
        count++;
    }

    return SyscallResult::ok(count);
}

// =============================================================================
// Shared Memory Syscalls
// =============================================================================

/**
 * @brief Create a shared memory object.
 *
 * @param a0 Size of shared memory in bytes
 * @return Handle on success (in res0), negative error on failure
 */
static SyscallResult sys_shm_create(u64 a0, u64, u64, u64, u64, u64)
{
    u64 size = a0;

    if (size == 0 || size > 64 * 1024 * 1024) // Max 64MB
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    viper::Viper *v = viper::current();
    if (!v || !v->cap_table)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    // Create the shared memory object
    kobj::SharedMemory *shm = kobj::SharedMemory::create(size);
    if (!shm)
    {
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    // Map into creator's address space
    viper::AddressSpace *as = viper::get_address_space(v);
    if (!as)
    {
        delete shm;
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    // Find free virtual address for the mapping
    // Use a simple approach: start from a fixed base and check VMAs
    u64 virt_base = 0x7000000000ULL; // Start searching from 448GB
    u64 virt_addr = 0;
    u64 aligned_size = pmm::page_align_up(size);

    // Simple search - in a real implementation, would check VMA list
    for (u64 try_addr = virt_base; try_addr < 0x8000000000ULL; try_addr += aligned_size)
    {
        // Check if address range is free by trying to translate
        // If translation returns 0, the address is likely unmapped
        if (as->translate(try_addr) == 0)
        {
            virt_addr = try_addr;
            break;
        }
    }

    if (virt_addr == 0)
    {
        delete shm;
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    // Map the physical memory into the address space
    if (!as->map(virt_addr, shm->phys_addr(), aligned_size, viper::prot::RW))
    {
        delete shm;
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    shm->set_creator_virt(virt_addr);

    // Insert into capability table
    cap::Handle handle = v->cap_table->insert(
        shm, cap::Kind::SharedMemory, cap::CAP_READ | cap::CAP_WRITE | cap::CAP_TRANSFER);
    if (handle == cap::HANDLE_INVALID)
    {
        as->unmap(virt_addr, aligned_size);
        delete shm;
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    // Return handle and virtual address
    SyscallResult result;
    result.verr = 0;
    result.res0 = handle;
    result.res1 = virt_addr;
    result.res2 = shm->size();
    return result;
}

/**
 * @brief Map a shared memory object into the calling process's address space.
 *
 * @param a0 Handle to shared memory object
 * @return Virtual address on success (in res0), negative error on failure
 */
static SyscallResult sys_shm_map(u64 a0, u64, u64, u64, u64, u64)
{
    cap::Handle handle = static_cast<cap::Handle>(a0);

    viper::Viper *v = viper::current();
    if (!v || !v->cap_table)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    // Look up the handle
    cap::Entry *entry = v->cap_table->get_checked(handle, cap::Kind::SharedMemory);
    if (!entry)
    {
        return SyscallResult::err(error::VERR_INVALID_HANDLE);
    }

    // Check read permission
    if (!cap::has_rights(entry->rights, cap::CAP_READ))
    {
        return SyscallResult::err(error::VERR_PERMISSION);
    }

    kobj::SharedMemory *shm = static_cast<kobj::SharedMemory *>(entry->object);
    if (!shm)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    // Get the address space
    viper::AddressSpace *as = viper::get_address_space(v);
    if (!as)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    // Find a free virtual address
    u64 virt_base = 0x7000000000ULL;
    u64 virt_addr = 0;
    u64 aligned_size = shm->size();

    for (u64 try_addr = virt_base; try_addr < 0x8000000000ULL; try_addr += aligned_size)
    {
        if (as->translate(try_addr) == 0)
        {
            virt_addr = try_addr;
            break;
        }
    }

    if (virt_addr == 0)
    {
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    // Determine protection based on rights
    u32 prot = viper::prot::READ;
    if (cap::has_rights(entry->rights, cap::CAP_WRITE))
    {
        prot |= viper::prot::WRITE;
    }

    // Map the physical memory
    if (!as->map(virt_addr, shm->phys_addr(), aligned_size, prot))
    {
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    // Increment reference count
    shm->ref();

    SyscallResult result;
    result.verr = 0;
    result.res0 = virt_addr;
    result.res1 = shm->size();
    return result;
}

/**
 * @brief Unmap a shared memory region from the calling process's address space.
 *
 * @param a0 Virtual address of mapped region
 * @return 0 on success, negative error on failure
 */
static SyscallResult sys_shm_unmap(u64 a0, u64, u64, u64, u64, u64)
{
    u64 virt_addr = a0;

    viper::Viper *v = viper::current();
    if (!v)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    viper::AddressSpace *as = viper::get_address_space(v);
    if (!as)
    {
        return SyscallResult::err(error::VERR_NOT_FOUND);
    }

    // For now, just unmap a page - a full implementation would track
    // shared memory mappings and unmap the entire region
    as->unmap(virt_addr, pmm::PAGE_SIZE);

    return SyscallResult::ok();
}

// =============================================================================
// Syscall Dispatch Table
// =============================================================================

/**
 * @brief Static syscall dispatch table.
 *
 * @details
 * Each entry contains {number, handler, name, argcount}.
 * Entries are sorted by syscall number for efficient lookup.
 */
static const SyscallEntry syscall_table[] = {
    // Task Management (0x00-0x0F)
    {SYS_TASK_YIELD, sys_task_yield, "task_yield", 0},
    {SYS_TASK_EXIT, sys_task_exit, "task_exit", 1},
    {SYS_TASK_CURRENT, sys_task_current, "task_current", 0},
    {SYS_TASK_SPAWN, sys_task_spawn, "task_spawn", 3},
    {SYS_TASK_LIST, sys_task_list, "task_list", 2},
    {SYS_TASK_SET_PRIORITY, sys_task_set_priority, "task_set_priority", 2},
    {SYS_TASK_GET_PRIORITY, sys_task_get_priority, "task_get_priority", 1},
    {SYS_WAIT, sys_wait, "wait", 1},
    {SYS_WAITPID, sys_waitpid, "waitpid", 2},
    {SYS_SBRK, sys_sbrk, "sbrk", 1},
    {SYS_FORK, sys_fork, "fork", 0},

    // Channel IPC (0x10-0x1F)
    {SYS_CHANNEL_CREATE, sys_channel_create, "channel_create", 0},
    {SYS_CHANNEL_SEND, sys_channel_send, "channel_send", 5},
    {SYS_CHANNEL_RECV, sys_channel_recv, "channel_recv", 5},
    {SYS_CHANNEL_CLOSE, sys_channel_close, "channel_close", 1},

    // Poll (0x20-0x2F)
    {SYS_POLL_CREATE, sys_poll_create, "poll_create", 0},
    {SYS_POLL_ADD, sys_poll_add, "poll_add", 3},
    {SYS_POLL_REMOVE, sys_poll_remove, "poll_remove", 2},
    {SYS_POLL_WAIT, sys_poll_wait, "poll_wait", 4},

    // Time (0x30-0x3F)
    {SYS_TIME_NOW, sys_time_now, "time_now", 0},
    {SYS_SLEEP, sys_sleep, "sleep", 1},

    // File I/O (0x40-0x4F)
    {SYS_OPEN, sys_open, "open", 2},
    {SYS_CLOSE, sys_close, "close", 1},
    {SYS_READ, sys_read, "read", 3},
    {SYS_WRITE, sys_write, "write", 3},
    {SYS_LSEEK, sys_lseek, "lseek", 3},
    {SYS_STAT, sys_stat, "stat", 2},
    {SYS_FSTAT, sys_fstat, "fstat", 2},
    {SYS_DUP, sys_dup, "dup", 1},
    {SYS_DUP2, sys_dup2, "dup2", 2},

    // Networking (0x50-0x5F)
    {SYS_SOCKET_CREATE, sys_socket_create, "socket_create", 0},
    {SYS_SOCKET_CONNECT, sys_socket_connect, "socket_connect", 3},
    {SYS_SOCKET_SEND, sys_socket_send, "socket_send", 3},
    {SYS_SOCKET_RECV, sys_socket_recv, "socket_recv", 3},
    {SYS_SOCKET_CLOSE, sys_socket_close, "socket_close", 1},
    {SYS_DNS_RESOLVE, sys_dns_resolve, "dns_resolve", 2},

    // Directory/FS (0x60-0x6F)
    {SYS_READDIR, sys_readdir, "readdir", 3},
    {SYS_MKDIR, sys_mkdir, "mkdir", 1},
    {SYS_RMDIR, sys_rmdir, "rmdir", 1},
    {SYS_UNLINK, sys_unlink, "unlink", 1},
    {SYS_RENAME, sys_rename, "rename", 2},
    {SYS_SYMLINK, sys_symlink, "symlink", 2},
    {SYS_READLINK, sys_readlink, "readlink", 3},
    {SYS_GETCWD, sys_getcwd, "getcwd", 2},
    {SYS_CHDIR, sys_chdir, "chdir", 1},

    // Capability (0x70-0x7F)
    {SYS_CAP_DERIVE, sys_cap_derive, "cap_derive", 2},
    {SYS_CAP_REVOKE, sys_cap_revoke, "cap_revoke", 1},
    {SYS_CAP_QUERY, sys_cap_query, "cap_query", 2},
    {SYS_CAP_LIST, sys_cap_list, "cap_list", 2},

    // Handle-based FS (0x80-0x8F)
    {SYS_FS_OPEN_ROOT, sys_fs_open_root, "fs_open_root", 0},
    {SYS_FS_OPEN, sys_fs_open, "fs_open", 4},
    {SYS_IO_READ, sys_io_read, "io_read", 3},
    {SYS_IO_WRITE, sys_io_write, "io_write", 3},
    {SYS_IO_SEEK, sys_io_seek, "io_seek", 3},
    {SYS_FS_READ_DIR, sys_fs_read_dir, "fs_read_dir", 2},
    {SYS_FS_CLOSE, sys_fs_close, "fs_close", 1},
    {SYS_FS_REWIND_DIR, sys_fs_rewind_dir, "fs_rewind_dir", 1},

    // Signal (0x90-0x9F)
    {SYS_SIGACTION, sys_sigaction, "sigaction", 3},
    {SYS_SIGPROCMASK, sys_sigprocmask, "sigprocmask", 3},
    {SYS_SIGRETURN, sys_sigreturn, "sigreturn", 0},
    {SYS_KILL, sys_kill, "kill", 2},
    {SYS_SIGPENDING, sys_sigpending, "sigpending", 1},

    // Process Groups/Sessions (0xA0-0xAF)
    {SYS_GETPID, sys_getpid, "getpid", 0},
    {SYS_GETPPID, sys_getppid, "getppid", 0},
    {SYS_GETPGID, sys_getpgid, "getpgid", 1},
    {SYS_SETPGID, sys_setpgid, "setpgid", 2},
    {SYS_GETSID, sys_getsid, "getsid", 1},
    {SYS_SETSID, sys_setsid, "setsid", 0},
    {SYS_GET_ARGS, sys_get_args, "get_args", 2},

    // Assign (0xC0-0xCF)
    {SYS_ASSIGN_SET, sys_assign_set, "assign_set", 2},
    {SYS_ASSIGN_GET, sys_assign_get, "assign_get", 3},
    {SYS_ASSIGN_REMOVE, sys_assign_remove, "assign_remove", 1},
    {SYS_ASSIGN_LIST, sys_assign_list, "assign_list", 2},
    {SYS_ASSIGN_RESOLVE, sys_assign_resolve, "assign_resolve", 3},

    // TLS (0xD0-0xDF)
    {SYS_TLS_CREATE, sys_tls_create, "tls_create", 1},
    {SYS_TLS_HANDSHAKE, sys_tls_handshake, "tls_handshake", 2},
    {SYS_TLS_SEND, sys_tls_send, "tls_send", 3},
    {SYS_TLS_RECV, sys_tls_recv, "tls_recv", 3},
    {SYS_TLS_CLOSE, sys_tls_close, "tls_close", 1},
    {SYS_TLS_INFO, sys_tls_info, "tls_info", 2},

    // System Info (0xE0-0xEF)
    {SYS_MEM_INFO, sys_mem_info, "mem_info", 1},
    {SYS_NET_STATS, sys_net_stats, "net_stats", 1},
    {SYS_PING, sys_ping, "ping", 2},
    {SYS_DEVICE_LIST, sys_device_list, "device_list", 2},

    // Debug/Console (0xF0-0xFF)
    {SYS_DEBUG_PRINT, sys_debug_print, "debug_print", 1},
    {SYS_GETCHAR, sys_getchar, "getchar", 0},
    {SYS_PUTCHAR, sys_putchar, "putchar", 1},
    {SYS_UPTIME, sys_uptime, "uptime", 0},

    // Device Management (0x100-0x10F) - Microkernel support
    {SYS_MAP_DEVICE, sys_map_device, "map_device", 3},
    {SYS_IRQ_REGISTER, sys_irq_register, "irq_register", 1},
    {SYS_IRQ_WAIT, sys_irq_wait, "irq_wait", 2},
    {SYS_IRQ_ACK, sys_irq_ack, "irq_ack", 1},
    {SYS_DMA_ALLOC, sys_dma_alloc, "dma_alloc", 2},
    {SYS_DMA_FREE, sys_dma_free, "dma_free", 1},
    {SYS_VIRT_TO_PHYS, sys_virt_to_phys, "virt_to_phys", 1},
    {SYS_DEVICE_ENUM, sys_device_enum, "device_enum", 2},
    {SYS_IRQ_UNREGISTER, sys_irq_unregister, "irq_unregister", 1},
    {SYS_SHM_CREATE, sys_shm_create, "shm_create", 1},
    {SYS_SHM_MAP, sys_shm_map, "shm_map", 1},
    {SYS_SHM_UNMAP, sys_shm_unmap, "shm_unmap", 1},
};

static constexpr usize SYSCALL_TABLE_SIZE = sizeof(syscall_table) / sizeof(syscall_table[0]);

// =============================================================================
// Table Access Functions
// =============================================================================

const SyscallEntry *get_table()
{
    return syscall_table;
}

usize get_table_size()
{
    return SYSCALL_TABLE_SIZE;
}

const SyscallEntry *lookup(u32 number)
{
    // Linear search (table is small enough)
    for (usize i = 0; i < SYSCALL_TABLE_SIZE; i++)
    {
        if (syscall_table[i].number == number)
        {
            return &syscall_table[i];
        }
    }
    return nullptr;
}

SyscallResult dispatch_syscall(u32 number, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5)
{
    const SyscallEntry *entry = lookup(number);

    if (!entry || !entry->handler)
    {
        // Unknown syscall - return ENOSYS equivalent
        return SyscallResult::err(error::VERR_NOT_SUPPORTED);
    }

#ifdef CONFIG_SYSCALL_TRACE
    trace_entry(entry, a0, a1, a2);
#endif

    SyscallResult result = entry->handler(a0, a1, a2, a3, a4, a5);

#ifdef CONFIG_SYSCALL_TRACE
    trace_exit(entry, result);
#endif

    return result;
}

} // namespace syscall
