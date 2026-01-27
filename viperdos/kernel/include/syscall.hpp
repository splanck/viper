//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "error.hpp"
#include "syscall_nums.hpp"
#include "types.hpp"

/**
 * @file syscall.hpp
 * @brief AArch64 syscall invocation helpers and high-level syscall wrappers.
 *
 * @details
 * ViperDOS uses an SVC-based syscall ABI on AArch64:
 * - The syscall number is placed in x8.
 * - Up to six arguments are placed in x0-x5.
 * - The return value is provided in x0.
 *
 * This header provides two layers:
 * 1. Low-level `syscallN` helpers that execute `svc #0` with a fixed argument
 *    count.
 * 2. Higher-level wrappers in the `sys::` namespace for common kernel/user
 *    operations (tasks, IPC channels, time, file I/O, sockets, DNS).
 *
 * These routines are designed for freestanding code and avoid libc
 * dependencies. They are usable from user-mode code and from kernel-side test
 * code that intentionally exercises the syscall path.
 */
namespace sys {

// Low-level syscall invocation
// Syscall number in x8, args in x0-x5, result in x0
/**
 * @brief Invoke a syscall with no arguments.
 *
 * @details
 * Places the syscall number in x8 and executes `svc #0`. The return value is
 * read from x0.
 *
 * @param num Syscall number (see @ref syscall::Number).
 * @return Result value in x0 (non-negative on success, negative on error).
 */
inline i64 syscall0(u64 num) {
    register u64 x8 asm("x8") = num;
    register i64 x0 asm("x0");
    asm volatile("svc #0" : "=r"(x0) : "r"(x8) : "memory");
    return x0;
}

/**
 * @brief Invoke a syscall with one argument.
 *
 * @details
 * Loads x8 with the syscall number and x0 with the argument, then executes
 * `svc #0`.
 *
 * @param num Syscall number.
 * @param arg0 First argument (x0).
 * @return Result value in x0.
 */
inline i64 syscall1(u64 num, u64 arg0) {
    register u64 x8 asm("x8") = num;
    register u64 r0 asm("x0") = arg0;
    register i64 result asm("x0");
    asm volatile("svc #0" : "=r"(result) : "r"(x8), "r"(r0) : "memory");
    return result;
}

/**
 * @brief Invoke a syscall with two arguments.
 *
 * @param num Syscall number.
 * @param arg0 First argument (x0).
 * @param arg1 Second argument (x1).
 * @return Result value in x0.
 */
inline i64 syscall2(u64 num, u64 arg0, u64 arg1) {
    register u64 x8 asm("x8") = num;
    register u64 r0 asm("x0") = arg0;
    register u64 r1 asm("x1") = arg1;
    register i64 result asm("x0");
    asm volatile("svc #0" : "=r"(result) : "r"(x8), "r"(r0), "r"(r1) : "memory");
    return result;
}

/**
 * @brief Invoke a syscall with three arguments.
 *
 * @param num Syscall number.
 * @param arg0 First argument (x0).
 * @param arg1 Second argument (x1).
 * @param arg2 Third argument (x2).
 * @return Result value in x0.
 */
inline i64 syscall3(u64 num, u64 arg0, u64 arg1, u64 arg2) {
    register u64 x8 asm("x8") = num;
    register u64 r0 asm("x0") = arg0;
    register u64 r1 asm("x1") = arg1;
    register u64 r2 asm("x2") = arg2;
    register i64 result asm("x0");
    asm volatile("svc #0" : "=r"(result) : "r"(x8), "r"(r0), "r"(r1), "r"(r2) : "memory");
    return result;
}

// Task syscalls
/**
 * @brief Yield execution to the scheduler.
 *
 * @details
 * Requests that the kernel scheduler select another runnable task. The exact
 * semantics depend on scheduler policy; typically the current task is moved to
 * the end of its run queue.
 *
 * @return Result code (usually @ref error::VOK on success).
 */
inline i64 yield() {
    return syscall0(syscall::TASK_YIELD);
}

/**
 * @brief Terminate the current task/process with an exit code.
 *
 * @details
 * Requests that the kernel end execution of the calling task. This call does
 * not return on success. The exit code is passed to any join/wait mechanism and
 * may be reported for diagnostics.
 *
 * @param code Exit status code.
 */
inline void exit(i64 code) {
    syscall1(syscall::TASK_EXIT, static_cast<u64>(code));
    // Never returns
    __builtin_unreachable();
}

/**
 * @brief Query the current task identifier.
 *
 * @details
 * Returns an ID that can be used for debugging or for task management APIs.
 *
 * @return Task ID on success, or negative error code on failure.
 */
inline i64 current_task_id() {
    return syscall0(syscall::TASK_CURRENT);
}

// Debug syscalls
/**
 * @brief Print a debug message via the kernel debug output.
 *
 * @details
 * This is intended for early bring-up and debug logging. The implementation may
 * forward the message to serial output, a log buffer, or other sinks depending
 * on configuration.
 *
 * @param msg Pointer to a NUL-terminated string.
 * @return Result code.
 */
inline i64 debug_print(const char *msg) {
    return syscall1(syscall::DEBUG_PRINT, reinterpret_cast<u64>(msg));
}

// Channel syscalls
/**
 * @brief Create a new IPC channel.
 *
 * @details
 * Requests the kernel to allocate a new channel object and returns its handle
 * or identifier.
 *
 * @return Channel handle/ID on success, or negative error code on failure.
 */
inline i64 channel_create() {
    return syscall0(syscall::CHANNEL_CREATE);
}

/**
 * @brief Send a message over an IPC channel.
 *
 * @details
 * Sends `size` bytes from `data` to the channel identified by `channel_id`.
 * The kernel may copy the message into an internal buffer or may validate and
 * map user buffers depending on implementation.
 *
 * @param channel_id Channel handle/ID.
 * @param data Pointer to message bytes.
 * @param size Number of bytes to send.
 * @return Result code (non-negative on success, negative on error).
 */
inline i64 channel_send(u32 channel_id, const void *data, u32 size) {
    return syscall3(syscall::CHANNEL_SEND,
                    static_cast<u64>(channel_id),
                    reinterpret_cast<u64>(data),
                    static_cast<u64>(size));
}

/**
 * @brief Receive a message from an IPC channel.
 *
 * @details
 * Attempts to read the next queued message from the channel into `buffer`.
 * If the call succeeds, the return value may represent the number of bytes
 * written.
 *
 * @param channel_id Channel handle/ID.
 * @param buffer Destination buffer for message bytes.
 * @param buffer_size Size of `buffer` in bytes.
 * @return Non-negative value on success (often bytes received), negative error
 *         code on failure.
 */
inline i64 channel_recv(u32 channel_id, void *buffer, u32 buffer_size) {
    return syscall3(syscall::CHANNEL_RECV,
                    static_cast<u64>(channel_id),
                    reinterpret_cast<u64>(buffer),
                    static_cast<u64>(buffer_size));
}

/**
 * @brief Close an IPC channel.
 *
 * @details
 * Releases the channel handle from the current task. The kernel may destroy the
 * channel object when all references are closed.
 *
 * @param channel_id Channel handle/ID.
 * @return Result code.
 */
inline i64 channel_close(u32 channel_id) {
    return syscall1(syscall::CHANNEL_CLOSE, static_cast<u64>(channel_id));
}

// Time syscalls
/**
 * @brief Query the current time/tick count from the kernel.
 *
 * @details
 * Returns a monotonic time value defined by the kernel (often milliseconds
 * since boot in early bring-up).
 *
 * @return Time value on success, or negative error code.
 */
inline i64 time_now() {
    return syscall0(syscall::TIME_NOW);
}

/**
 * @brief Sleep for a number of milliseconds.
 *
 * @details
 * Blocks the calling task for at least `ms` milliseconds (as defined by the
 * kernel's timer tick).
 *
 * @param ms Duration to sleep in milliseconds.
 * @return Result code.
 */
inline i64 sleep(u64 ms) {
    return syscall1(syscall::SLEEP, ms);
}

// File syscalls
/**
 * @brief Open a file path and return a file descriptor.
 *
 * @details
 * Requests the kernel VFS to open the file at `path` with `flags`. The flag
 * values are defined in @ref sys::file.
 *
 * @param path NUL-terminated path string.
 * @param flags Open flags bitmask.
 * @return File descriptor on success, or negative error code (cast to i32).
 */
inline i32 open(const char *path, u32 flags) {
    return static_cast<i32>(
        syscall2(syscall::OPEN, reinterpret_cast<u64>(path), static_cast<u64>(flags)));
}

/**
 * @brief Close an open file descriptor.
 *
 * @param fd File descriptor.
 * @return Result code (0 on success, negative on error).
 */
inline i32 close(i32 fd) {
    return static_cast<i32>(syscall1(syscall::CLOSE, static_cast<u64>(fd)));
}

/**
 * @brief Read bytes from a file descriptor into a buffer.
 *
 * @param fd File descriptor.
 * @param buf Destination buffer.
 * @param len Maximum number of bytes to read.
 * @return Non-negative value on success (often bytes read), negative error code
 *         on failure.
 */
inline i64 read(i32 fd, void *buf, usize len) {
    return syscall3(
        syscall::READ, static_cast<u64>(fd), reinterpret_cast<u64>(buf), static_cast<u64>(len));
}

/**
 * @brief Write bytes to a file descriptor from a buffer.
 *
 * @param fd File descriptor.
 * @param buf Source buffer.
 * @param len Number of bytes to write.
 * @return Non-negative value on success (often bytes written), negative error
 *         code on failure.
 */
inline i64 write(i32 fd, const void *buf, usize len) {
    return syscall3(
        syscall::WRITE, static_cast<u64>(fd), reinterpret_cast<u64>(buf), static_cast<u64>(len));
}

/**
 * @brief Change the current file offset for a file descriptor.
 *
 * @param fd File descriptor.
 * @param offset Offset value.
 * @param whence Seek base (implementation-defined, typically SEEK_SET/SEEK_CUR/SEEK_END).
 * @return New offset on success, or negative error code.
 */
inline i64 lseek(i32 fd, i64 offset, i32 whence) {
    return syscall3(
        syscall::LSEEK, static_cast<u64>(fd), static_cast<u64>(offset), static_cast<u64>(whence));
}

// File open flags (for userspace)
namespace file {
/** @brief Open read-only. */
constexpr u32 O_RDONLY = 0x0000;
/** @brief Open write-only. */
constexpr u32 O_WRONLY = 0x0001;
/** @brief Open for read/write. */
constexpr u32 O_RDWR = 0x0002;
/** @brief Create the file if it does not exist. */
constexpr u32 O_CREAT = 0x0040;
/** @brief Truncate the file to zero length on open. */
constexpr u32 O_TRUNC = 0x0200;
/** @brief Append writes to the end of the file. */
constexpr u32 O_APPEND = 0x0400;
} // namespace file

// Socket syscalls
/**
 * @brief Create a socket handle.
 *
 * @return Socket descriptor/handle on success, or negative error code.
 */
inline i32 socket_create() {
    return static_cast<i32>(syscall0(syscall::SOCKET_CREATE));
}

/**
 * @brief Connect a socket to a remote IPv4 endpoint.
 *
 * @param sock Socket descriptor.
 * @param ip Remote IPv4 address packed into a u32 (see @ref ip_pack).
 * @param port Remote port number in host byte order (ABI-defined).
 * @return Result code.
 */
inline i32 socket_connect(i32 sock, u32 ip, u16 port) {
    return static_cast<i32>(syscall3(syscall::SOCKET_CONNECT,
                                     static_cast<u64>(sock),
                                     static_cast<u64>(ip),
                                     static_cast<u64>(port)));
}

/**
 * @brief Send bytes on a connected socket.
 *
 * @param sock Socket descriptor.
 * @param data Pointer to bytes to send.
 * @param len Number of bytes to send.
 * @return Non-negative value on success (often bytes sent), negative error code.
 */
inline i64 socket_send(i32 sock, const void *data, usize len) {
    return syscall3(syscall::SOCKET_SEND,
                    static_cast<u64>(sock),
                    reinterpret_cast<u64>(data),
                    static_cast<u64>(len));
}

/**
 * @brief Receive bytes from a socket.
 *
 * @param sock Socket descriptor.
 * @param buffer Destination buffer.
 * @param max_len Maximum number of bytes to receive.
 * @return Non-negative value on success (often bytes received), negative error code.
 */
inline i64 socket_recv(i32 sock, void *buffer, usize max_len) {
    return syscall3(syscall::SOCKET_RECV,
                    static_cast<u64>(sock),
                    reinterpret_cast<u64>(buffer),
                    static_cast<u64>(max_len));
}

/**
 * @brief Close a socket descriptor.
 *
 * @param sock Socket descriptor.
 * @return Result code.
 */
inline i32 socket_close(i32 sock) {
    return static_cast<i32>(syscall1(syscall::SOCKET_CLOSE, static_cast<u64>(sock)));
}

/**
 * @brief Resolve a hostname to an IPv4 address via the kernel DNS service.
 *
 * @details
 * Requests that the kernel resolve `hostname` and store the resulting IPv4
 * address in `ip_out`.
 *
 * @param hostname NUL-terminated hostname string.
 * @param ip_out Output pointer where the resolved packed IPv4 address is stored.
 * @return Result code.
 */
inline i32 dns_resolve(const char *hostname, u32 *ip_out) {
    return static_cast<i32>(syscall2(
        syscall::DNS_RESOLVE, reinterpret_cast<u64>(hostname), reinterpret_cast<u64>(ip_out)));
}

/**
 * @brief Pack four IPv4 octets into a 32-bit integer.
 *
 * @details
 * Produces a packed IPv4 address in network byte order (a.b.c.d mapped to
 * `0xAABBCCDD`). This is a convenience helper for constructing addresses at
 * call sites without requiring byte-order utilities.
 *
 * @param a First octet.
 * @param b Second octet.
 * @param c Third octet.
 * @param d Fourth octet.
 * @return Packed IPv4 address.
 */
inline u32 ip_pack(u8 a, u8 b, u8 c, u8 d) {
    return (static_cast<u32>(a) << 24) | (static_cast<u32>(b) << 16) | (static_cast<u32>(c) << 8) |
           static_cast<u32>(d);
}

} // namespace sys
