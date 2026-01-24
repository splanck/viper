/**
 * @file table.cpp
 * @brief Syscall dispatch table and utility functions.
 *
 * @details
 * This file contains:
 * 1. User pointer validation helpers
 * 2. The static syscall dispatch table
 * 3. Table lookup and dispatch functions
 *
 * Individual syscall handlers are implemented in handlers/ subdirectory.
 */
#include "table.hpp"
#include "handlers/handlers_internal.hpp"
#include "../cap/table.hpp"
#include "../console/serial.hpp"
#include "../include/error.hpp"
#include "../include/syscall_nums.hpp"
#include "../sched/task.hpp"
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

static bool is_valid_user_address(u64 addr, usize size)
{
    if (addr + size < addr)
    {
        return false;
    }

    if (addr < 0x1000)
    {
        return false;
    }

    if (addr >= 0xFFFF000000000000ULL)
    {
        return false;
    }

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

    return true;
}

i64 validate_user_string(const char *str, usize max_len)
{
    if (!str)
    {
        return -1;
    }

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
// Capability Table Helper
// =============================================================================

cap::Table *get_current_cap_table()
{
    task::Task *t = task::current();
    if (!t || !t->viper)
        return nullptr;
    viper::Viper *v = reinterpret_cast<viper::Viper *>(t->viper);
    return v->cap_table;
}

// =============================================================================
// Syscall Dispatch Table
// =============================================================================

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
    {SYS_TASK_SPAWN_SHM, sys_task_spawn_shm, "task_spawn_shm", 5},
    {SYS_REPLACE, sys_replace, "replace", 3},
    {SYS_SCHED_SETAFFINITY, sys_sched_setaffinity, "sched_setaffinity", 2},
    {SYS_SCHED_GETAFFINITY, sys_sched_getaffinity, "sched_getaffinity", 1},

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
    {SYS_FSYNC, sys_fsync, "fsync", 1},

    // Networking (0x50-0x5F)
    {SYS_SOCKET_CREATE, sys_socket_create, "socket_create", 0},
    {SYS_SOCKET_CONNECT, sys_socket_connect, "socket_connect", 3},
    {SYS_SOCKET_SEND, sys_socket_send, "socket_send", 3},
    {SYS_SOCKET_RECV, sys_socket_recv, "socket_recv", 3},
    {SYS_SOCKET_CLOSE, sys_socket_close, "socket_close", 1},
    {SYS_DNS_RESOLVE, sys_dns_resolve, "dns_resolve", 2},
    {SYS_SOCKET_POLL, sys_socket_poll, "socket_poll", 3},

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
    {SYS_CAP_GET_BOUND, sys_cap_get_bound, "cap_get_bound", 0},
    {SYS_CAP_DROP_BOUND, sys_cap_drop_bound, "cap_drop_bound", 1},
    {SYS_GETRLIMIT, sys_getrlimit, "getrlimit", 1},
    {SYS_SETRLIMIT, sys_setrlimit, "setrlimit", 2},
    {SYS_GETRUSAGE, sys_getrusage, "getrusage", 1},

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

    // Device Management (0x100-0x10F)
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
    {SYS_SHM_CLOSE, sys_shm_close, "shm_close", 1},

    // GUI/Display (0x110-0x11F)
    {SYS_GET_MOUSE_STATE, sys_get_mouse_state, "get_mouse_state", 1},
    {SYS_MAP_FRAMEBUFFER, sys_map_framebuffer, "map_framebuffer", 0},
    {SYS_SET_MOUSE_BOUNDS, sys_set_mouse_bounds, "set_mouse_bounds", 2},
    {SYS_INPUT_HAS_EVENT, sys_input_has_event, "input_has_event", 0},
    {SYS_INPUT_GET_EVENT, sys_input_get_event, "input_get_event", 1},
    {SYS_GCON_SET_GUI_MODE, sys_gcon_set_gui_mode, "gcon_set_gui_mode", 1},

    // TTY (0x120-0x12F)
    {SYS_TTY_READ, sys_tty_read, "tty_read", 2},
    {SYS_TTY_WRITE, sys_tty_write, "tty_write", 2},
    {SYS_TTY_PUSH_INPUT, sys_tty_push_input, "tty_push_input", 1},
    {SYS_TTY_HAS_INPUT, sys_tty_has_input, "tty_has_input", 0},
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
