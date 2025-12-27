#pragma once

/**
 * @file syscall_nums.hpp
 * @brief Kernel-side syscall number mapping.
 *
 * @details
 * Syscall numbers are shared between kernel and user components via the common
 * header in `include/viperos/syscall_nums.hpp`. This wrapper provides a kernel
 * namespace (`syscall::Number`) with the same values, making call sites more
 * self-documenting and ensuring the kernel uses the same ABI contract as
 * user-space.
 */

// Include shared syscall numbers
#include "../../include/viperos/syscall_nums.hpp"

#include "types.hpp"

namespace syscall
{

/**
 * @brief Syscall numbers used by the kernel syscall dispatcher.
 *
 * @details
 * The numeric values are defined in the shared `SYS_*` constants so that both
 * kernel and user-space agree on the syscall ABI. This enum simply re-exports
 * the shared values under kernel-friendly names and groups them by function.
 */
enum Number : u64
{
    // Task management (0x00 - 0x0F)
    TASK_YIELD = SYS_TASK_YIELD,
    TASK_EXIT = SYS_TASK_EXIT,
    TASK_CURRENT = SYS_TASK_CURRENT,
    TASK_SPAWN = SYS_TASK_SPAWN,
    TASK_JOIN = SYS_TASK_JOIN,
    TASK_LIST = SYS_TASK_LIST,
    TASK_SET_PRIORITY = SYS_TASK_SET_PRIORITY,
    TASK_GET_PRIORITY = SYS_TASK_GET_PRIORITY,

    // Channel IPC (0x10 - 0x1F)
    CHANNEL_CREATE = SYS_CHANNEL_CREATE,
    CHANNEL_SEND = SYS_CHANNEL_SEND,
    CHANNEL_RECV = SYS_CHANNEL_RECV,
    CHANNEL_CLOSE = SYS_CHANNEL_CLOSE,

    // Poll (0x20 - 0x2F)
    POLL_CREATE = SYS_POLL_CREATE,
    POLL_ADD = SYS_POLL_ADD,
    POLL_REMOVE = SYS_POLL_REMOVE,
    POLL_WAIT = SYS_POLL_WAIT,

    // Time (0x30 - 0x3F)
    TIME_NOW = SYS_TIME_NOW,
    SLEEP = SYS_SLEEP,
    TIMER_CREATE = SYS_TIMER_CREATE,
    TIMER_CANCEL = SYS_TIMER_CANCEL,

    // File I/O (0x40 - 0x4F)
    OPEN = SYS_OPEN,
    CLOSE = SYS_CLOSE,
    READ = SYS_READ,
    WRITE = SYS_WRITE,
    LSEEK = SYS_LSEEK,
    STAT = SYS_STAT,
    FSTAT = SYS_FSTAT,

    // Network/Socket (0x50 - 0x5F)
    SOCKET_CREATE = SYS_SOCKET_CREATE,
    SOCKET_CONNECT = SYS_SOCKET_CONNECT,
    SOCKET_SEND = SYS_SOCKET_SEND,
    SOCKET_RECV = SYS_SOCKET_RECV,
    SOCKET_CLOSE = SYS_SOCKET_CLOSE,
    DNS_RESOLVE = SYS_DNS_RESOLVE,

    // Directory (0x60 - 0x6F)
    READDIR = SYS_READDIR,
    MKDIR = SYS_MKDIR,
    RMDIR = SYS_RMDIR,
    UNLINK = SYS_UNLINK,
    RENAME = SYS_RENAME,

    // Assign system (0xC0 - 0xCF) - v0.2.0
    ASSIGN_SET = SYS_ASSIGN_SET,
    ASSIGN_GET = SYS_ASSIGN_GET,
    ASSIGN_REMOVE = SYS_ASSIGN_REMOVE,
    ASSIGN_LIST = SYS_ASSIGN_LIST,
    ASSIGN_RESOLVE = SYS_ASSIGN_RESOLVE,

    // TLS (0xD0 - 0xDF) - v0.2.0
    TLS_CREATE = SYS_TLS_CREATE,
    TLS_HANDSHAKE = SYS_TLS_HANDSHAKE,
    TLS_SEND = SYS_TLS_SEND,
    TLS_RECV = SYS_TLS_RECV,
    TLS_CLOSE = SYS_TLS_CLOSE,
    TLS_INFO = SYS_TLS_INFO,

    // System Info (0xE0 - 0xEF) - v0.2.0
    MEM_INFO = SYS_MEM_INFO,

    // Debug (0xF0 - 0xFF)
    DEBUG_PRINT = SYS_DEBUG_PRINT,
    GETCHAR = SYS_GETCHAR,
    PUTCHAR = SYS_PUTCHAR,
    UPTIME = SYS_UPTIME,
};

} // namespace syscall
