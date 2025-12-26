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
#include "../../include/viperos/fs_types.hpp"
#include "../../include/viperos/mem_info.hpp"
#include "../arch/aarch64/timer.hpp"
#include "../assign/assign.hpp"
#include "../console/gcon.hpp"
#include "../console/serial.hpp"
#include "../drivers/virtio/input.hpp"
#include "../fs/vfs/vfs.hpp"
#include "../include/error.hpp"
#include "../include/syscall_nums.hpp"
#include "../input/input.hpp"
#include "../ipc/channel.hpp"
#include "../ipc/poll.hpp"
#include "../ipc/pollset.hpp"
#include "../lib/spinlock.hpp"
#include "../mm/pmm.hpp"
#include "../net/dns/dns.hpp"
#include "../net/ip/tcp.hpp"
#include "../net/network.hpp"
#include "../net/tls/tls.hpp"
#include "../sched/scheduler.hpp"
#include "../sched/task.hpp"

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

// Channel syscalls (non-blocking per ARM64 spec - only PollWait blocks)
/**
 * @brief Implementation of `SYS_CHANNEL_CREATE`.
 *
 * @details
 * Allocates a new channel IPC object and returns its identifier/handle.
 *
 * @return Non-negative channel ID on success, or negative error code.
 */
static i64 sys_channel_create()
{
    return channel::create();
}

/**
 * @brief Implementation of `SYS_CHANNEL_SEND` (non-blocking).
 *
 * @details
 * Attempts to enqueue a message into the channel without blocking. If the
 * channel is full, returns @ref error::VERR_WOULD_BLOCK instead of sleeping.
 *
 * @param channel_id Channel identifier.
 * @param data Pointer to message bytes.
 * @param size Length of the message in bytes.
 * @return Result code.
 */
static i64 sys_channel_send(u32 channel_id, const void *data, u32 size)
{
    // Validate user pointer
    if (!validate_user_read(data, size, size == 0))
    {
        return error::VERR_INVALID_ARG;
    }

    // Non-blocking: returns VERR_WOULD_BLOCK if channel buffer is full
    return channel::try_send(channel_id, data, size);
}

/**
 * @brief Implementation of `SYS_CHANNEL_RECV` (non-blocking).
 *
 * @details
 * Attempts to dequeue a message without blocking. If no message is available,
 * returns @ref error::VERR_WOULD_BLOCK.
 *
 * @param channel_id Channel identifier.
 * @param buffer Destination buffer for message bytes.
 * @param buffer_size Size of the destination buffer.
 * @return Result code or byte count depending on channel implementation.
 */
static i64 sys_channel_recv(u32 channel_id, void *buffer, u32 buffer_size)
{
    // Validate user pointer
    if (!validate_user_write(buffer, buffer_size, buffer_size == 0))
    {
        return error::VERR_INVALID_ARG;
    }

    // Non-blocking: returns VERR_WOULD_BLOCK if no messages available
    return channel::try_recv(channel_id, buffer, buffer_size);
}

/**
 * @brief Implementation of `SYS_CHANNEL_CLOSE`.
 *
 * @details
 * Closes the calling task's reference to the channel. The underlying channel
 * object may be destroyed when all references are closed.
 *
 * @param channel_id Channel identifier.
 * @return Result code.
 */
static i64 sys_channel_close(u32 channel_id)
{
    return channel::close(channel_id);
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

        // Channel syscalls
        case SYS_CHANNEL_CREATE:
            SYSCALL_RESULT(sys_channel_create());
            break;

        case SYS_CHANNEL_SEND:
            SYSCALL_RESULT(sys_channel_send(static_cast<u32>(arg0),
                                            reinterpret_cast<const void *>(arg1),
                                            static_cast<u32>(arg2)));
            break;

        case SYS_CHANNEL_RECV:
            SYSCALL_RESULT(sys_channel_recv(
                static_cast<u32>(arg0), reinterpret_cast<void *>(arg1), static_cast<u32>(arg2)));
            break;

        case SYS_CHANNEL_CLOSE:
            SYSCALL_VOID(sys_channel_close(static_cast<u32>(arg0)));
            break;

        // Poll syscalls
        case SYS_POLL_CREATE:
            SYSCALL_RESULT(pollset::create());
            break;

        case SYS_POLL_ADD:
            SYSCALL_VOID(pollset::add(
                static_cast<u32>(arg0), static_cast<u32>(arg1), static_cast<u32>(arg2)));
            break;

        case SYS_POLL_REMOVE:
            SYSCALL_VOID(pollset::remove(static_cast<u32>(arg0), static_cast<u32>(arg1)));
            break;

        case SYS_POLL_WAIT:
            SYSCALL_RESULT(pollset::wait(static_cast<u32>(arg0),
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
