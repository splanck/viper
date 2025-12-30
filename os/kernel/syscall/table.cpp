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
#include "../include/error.hpp"
#include "../include/syscall_nums.hpp"
#include "../input/input.hpp"
#include "../ipc/channel.hpp"
#include "../ipc/poll.hpp"
#include "../ipc/pollset.hpp"
#include "../kobj/channel.hpp"
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

static constexpr int MAX_TLS_SESSIONS = 16;
static viper::tls::TlsSession tls_sessions[MAX_TLS_SESSIONS];
static bool tls_session_active[MAX_TLS_SESSIONS] = {false};
static kernel::Spinlock tls_lock;

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

static SyscallResult sys_task_spawn(u64 a0, u64 a1, u64, u64, u64, u64)
{
    const char *path = reinterpret_cast<const char *>(a0);
    const char *name = reinterpret_cast<const char *>(a1);

    if (validate_user_string(path, viper::MAX_PATH) < 0)
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }
    if (name && validate_user_string(name, 64) < 0)
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

    // Create channel object (internally creates low-level channel)
    kobj::Channel *ch = kobj::Channel::create();
    if (!ch)
    {
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    // Insert into capability table with read/write/derive rights
    cap::Handle handle = table->insert(ch, cap::Kind::Channel, cap::CAP_RW | cap::CAP_DERIVE);

    if (handle == cap::HANDLE_INVALID)
    {
        delete ch;
        return SyscallResult::err(error::VERR_OUT_OF_MEMORY);
    }

    return SyscallResult::ok(static_cast<u64>(handle));
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
    i64 result = channel::send(ch->id(), data, size);

    // TODO: Handle transfer handles

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
    i64 result = channel::recv(ch->id(), data, size);

    // Zero out handles for now
    (void)handles;
    if (handle_count && validate_user_write(handle_count, sizeof(u32)))
    {
        *handle_count = 0;
    }

    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok(static_cast<u64>(result));
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

    if (!validate_user_write(buf, count))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
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

    if (!validate_user_read(buf, count))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
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

static SyscallResult sys_socket_create(u64, u64, u64, u64, u64, u64)
{
    i64 result = net::tcp::socket_create();
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

    // Convert u32 IP to Ipv4Addr
    net::Ipv4Addr ip;
    ip.bytes[0] = (ip_raw >> 24) & 0xFF;
    ip.bytes[1] = (ip_raw >> 16) & 0xFF;
    ip.bytes[2] = (ip_raw >> 8) & 0xFF;
    ip.bytes[3] = ip_raw & 0xFF;

    bool result = net::tcp::socket_connect(sock, ip, port);
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

    if (!validate_user_read(buf, len))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    i64 result = net::tcp::socket_send(sock, buf, len);
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

    if (!validate_user_write(buf, len))
    {
        return SyscallResult::err(error::VERR_INVALID_ARG);
    }

    i64 result = net::tcp::socket_recv(sock, buf, len);
    if (result < 0)
    {
        return SyscallResult::err(result);
    }
    return SyscallResult::ok(static_cast<u64>(result));
}

static SyscallResult sys_socket_close(u64 a0, u64, u64, u64, u64, u64)
{
    i32 sock = static_cast<i32>(a0);
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

    // Convert Ipv4Addr to u32 (big-endian)
    *ip_out = (static_cast<u32>(result_ip.bytes[0]) << 24) |
              (static_cast<u32>(result_ip.bytes[1]) << 16) |
              (static_cast<u32>(result_ip.bytes[2]) << 8) | static_cast<u32>(result_ip.bytes[3]);
    return SyscallResult::ok();
}

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

    // Check if handle is valid before removing
    cap::Entry *entry = table->get(handle);
    if (!entry)
    {
        return SyscallResult::err(error::VERR_INVALID_HANDLE);
    }

    table->remove(handle);
    return SyscallResult::ok();
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
    {SYS_TASK_SPAWN, sys_task_spawn, "task_spawn", 2},
    {SYS_TASK_LIST, sys_task_list, "task_list", 2},
    {SYS_TASK_SET_PRIORITY, sys_task_set_priority, "task_set_priority", 2},
    {SYS_TASK_GET_PRIORITY, sys_task_get_priority, "task_get_priority", 1},
    {SYS_WAIT, sys_wait, "wait", 1},
    {SYS_WAITPID, sys_waitpid, "waitpid", 2},
    {SYS_SBRK, sys_sbrk, "sbrk", 1},

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

    // Debug/Console (0xF0-0xFF)
    {SYS_DEBUG_PRINT, sys_debug_print, "debug_print", 1},
    {SYS_GETCHAR, sys_getchar, "getchar", 0},
    {SYS_PUTCHAR, sys_putchar, "putchar", 1},
    {SYS_UPTIME, sys_uptime, "uptime", 0},
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
