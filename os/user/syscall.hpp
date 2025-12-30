#pragma once

/**
 * @file syscall.hpp
 * @brief Header-only user-space syscall wrappers for ViperOS (AArch64).
 *
 * @details
 * This file provides a small, freestanding-friendly interface to the ViperOS
 * syscall ABI. It intentionally avoids libc dependencies and implements the
 * lowest-level `svc #0` helpers directly in inline assembly.
 *
 * The wrappers in this header are designed for early user-space programs such
 * as `vinit` where the runtime environment is minimal:
 * - No dynamic allocation is required.
 * - No standard library headers are required.
 * - All APIs are plain C++ constructs (enums/structs/inlines) that compile in
 *   a freestanding configuration.
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

/** @def __VIPEROS_USERSPACE__
 *  @brief Marker macro used by shared ABI headers.
 *
 *  @details
 *  Several headers under `include/viperos/` are shared between kernel and
 *  user-space. Defining `__VIPEROS_USERSPACE__` before including them enables
 *  user-space convenience macros (for example `TASK_FLAG_*` values).
 */
#define __VIPEROS_USERSPACE__

// Include shared types (single source of truth for basic types)
#include "../include/viperos/types.hpp"

// Include shared filesystem types (Stat, DirEnt, open flags, seek whence)
#include "../include/viperos/fs_types.hpp"

// Include shared syscall numbers (single source of truth for numeric IDs).
#include "../include/viperos/syscall_nums.hpp"

// Include shared ABI structures used by some syscalls (outside namespace).
#include "../include/viperos/cap_info.hpp"
#include "../include/viperos/mem_info.hpp"
#include "../include/viperos/syscall_abi.hpp"
#include "../include/viperos/task_info.hpp"
#include "../include/viperos/tls_info.hpp"

/**
 * @namespace sys
 * @brief User-space syscall wrapper namespace.
 *
 * @details
 * Everything in this namespace is intended to be callable from user-mode code.
 * The API style is "thin wrapper": functions generally pass parameters through
 * to the kernel with minimal transformation, and return the kernel result
 * directly so callers can handle negative error codes as needed.
 */
namespace sys
{

/**
 * @brief Seek origin selector for @ref lseek and @ref io_seek.
 *
 * @details
 * Re-exported from shared fs_types.hpp for convenience in the sys namespace.
 * We undef any existing macros to avoid conflicts with libc headers.
 */
#ifdef SEEK_SET
#undef SEEK_SET
#endif
#ifdef SEEK_CUR
#undef SEEK_CUR
#endif
#ifdef SEEK_END
#undef SEEK_END
#endif
constexpr i32 SEEK_SET = viper::seek_whence::SET;
constexpr i32 SEEK_CUR = viper::seek_whence::CUR;
constexpr i32 SEEK_END = viper::seek_whence::END;

// Re-export shared Stat and DirEnt types into sys namespace
using viper::DirEnt;
using viper::Stat;

/**
 * @brief Flags accepted by @ref open and @ref fs_open.
 *
 * @details
 * Re-exported from shared fs_types.hpp for convenience in the sys namespace.
 */
constexpr u32 O_RDONLY = viper::open_flags::O_RDONLY;
constexpr u32 O_WRONLY = viper::open_flags::O_WRONLY;
constexpr u32 O_RDWR = viper::open_flags::O_RDWR;
constexpr u32 O_CREAT = viper::open_flags::O_CREAT;
constexpr u32 O_TRUNC = viper::open_flags::O_TRUNC;

/**
 * @brief Flags describing an assign entry.
 *
 * @details
 * Assigns are name → directory mappings used to build Amiga-style logical
 * device paths such as `SYS:certs/roots.der`.
 *
 * The meanings mirror the kernel assign subsystem and are primarily used for
 * introspection (`assign_list`) and future policy decisions.
 */
enum AssignFlags : u32
{
    ASSIGN_NONE = 0,            /**< No special behavior. */
    ASSIGN_SYSTEM = (1 << 0),   /**< System assign (treated as read-only/pinned by kernel). */
    ASSIGN_DEFERRED = (1 << 1), /**< Deferred/path-based assign resolved on access. */
    ASSIGN_MULTI = (1 << 2),    /**< Multi-directory assign (search path semantics). */
};

/**
 * @brief Assign metadata returned by @ref assign_list.
 *
 * @details
 * The kernel writes an array of these records into a user-provided buffer.
 * `name` is the assign name without the trailing colon. `handle` is a directory
 * capability handle suitable for use with handle-based filesystem syscalls.
 */
struct AssignInfo
{
    char name[32];    /**< Assign name (without trailing ':'). */
    u32 handle;       /**< Directory handle backing this assign. */
    u32 flags;        /**< Bitmask of @ref AssignFlags values. */
    u8 _reserved[24]; /**< Reserved for future ABI extension; set to 0. */
};

/**
 * @brief Compute the length of a NUL-terminated string.
 *
 * @details
 * This is a minimal replacement for `strlen(3)` for freestanding user-space.
 * It performs a linear scan until the first `\\0` byte.
 *
 * @param s Pointer to a NUL-terminated string.
 * @return Number of bytes before the terminating NUL.
 */
inline usize strlen(const char *s)
{
    usize len = 0;
    while (s[len])
        len++;
    return len;
}

/**
 * @brief Syscall result structure capturing error and result values.
 *
 * @details
 * Re-exported from syscall_abi.hpp. Per the ViperOS ABI, syscalls return:
 * - x0: VError (0 = success, negative = error)
 * - x1: Result value 0
 * - x2: Result value 1
 * - x3: Result value 2
 */
using SyscallResult = viper::SyscallResult;

/**
 * @name Low-level syscall invokers
 * @brief Minimal `svc #0` helpers used by higher-level wrappers.
 *
 * @details
 * These functions implement the core ViperOS syscall ABI using inline AArch64
 * assembly. They capture the full syscall result per the ABI:
 * - x0: VError code (0 = success, negative = error)
 * - x1-x3: Result values
 *
 * The `"memory"` clobber prevents the compiler from reordering memory accesses
 * across the syscall boundary, which is important when passing pointers to
 * buffers that the kernel reads/writes.
 * @{
 */

/**
 * @brief Invoke a syscall with no arguments.
 */
inline SyscallResult syscall0(u64 num)
{
    register u64 x8 asm("x8") = num;
    register i64 r0 asm("x0");
    register u64 r1 asm("x1");
    register u64 r2 asm("x2");
    register u64 r3 asm("x3");
    asm volatile("svc #0" : "=r"(r0), "=r"(r1), "=r"(r2), "=r"(r3) : "r"(x8) : "memory");
    return {r0, r1, r2, r3};
}

/**
 * @brief Invoke a syscall with one argument.
 */
inline SyscallResult syscall1(u64 num, u64 arg0)
{
    register u64 x8 asm("x8") = num;
    register u64 a0 asm("x0") = arg0;
    register i64 r0 asm("x0");
    register u64 r1 asm("x1");
    register u64 r2 asm("x2");
    register u64 r3 asm("x3");
    asm volatile("svc #0" : "=r"(r0), "=r"(r1), "=r"(r2), "=r"(r3) : "r"(x8), "0"(a0) : "memory");
    return {r0, r1, r2, r3};
}

/**
 * @brief Invoke a syscall with two arguments.
 */
inline SyscallResult syscall2(u64 num, u64 arg0, u64 arg1)
{
    register u64 x8 asm("x8") = num;
    register u64 a0 asm("x0") = arg0;
    register u64 a1 asm("x1") = arg1;
    register i64 r0 asm("x0");
    register u64 r1 asm("x1");
    register u64 r2 asm("x2");
    register u64 r3 asm("x3");
    asm volatile("svc #0"
                 : "=r"(r0), "=r"(r1), "=r"(r2), "=r"(r3)
                 : "r"(x8), "0"(a0), "1"(a1)
                 : "memory");
    return {r0, r1, r2, r3};
}

/**
 * @brief Invoke a syscall with three arguments.
 */
inline SyscallResult syscall3(u64 num, u64 arg0, u64 arg1, u64 arg2)
{
    register u64 x8 asm("x8") = num;
    register u64 a0 asm("x0") = arg0;
    register u64 a1 asm("x1") = arg1;
    register u64 a2 asm("x2") = arg2;
    register i64 r0 asm("x0");
    register u64 r1 asm("x1");
    register u64 r2 asm("x2");
    register u64 r3 asm("x3");
    asm volatile("svc #0"
                 : "=r"(r0), "=r"(r1), "=r"(r2), "=r"(r3)
                 : "r"(x8), "0"(a0), "1"(a1), "2"(a2)
                 : "memory");
    return {r0, r1, r2, r3};
}

/**
 * @brief Invoke a syscall with four arguments.
 */
inline SyscallResult syscall4(u64 num, u64 arg0, u64 arg1, u64 arg2, u64 arg3)
{
    register u64 x8 asm("x8") = num;
    register u64 a0 asm("x0") = arg0;
    register u64 a1 asm("x1") = arg1;
    register u64 a2 asm("x2") = arg2;
    register u64 a3 asm("x3") = arg3;
    register i64 r0 asm("x0");
    register u64 r1 asm("x1");
    register u64 r2 asm("x2");
    register u64 r3 asm("x3");
    asm volatile("svc #0"
                 : "=r"(r0), "=r"(r1), "=r"(r2), "=r"(r3)
                 : "r"(x8), "0"(a0), "1"(a1), "2"(a2), "3"(a3)
                 : "memory");
    return {r0, r1, r2, r3};
}

/** @} */

/**
 * @name Task helpers
 * @brief Minimal process/task syscalls.
 * @{
 */

/**
 * @brief Terminate the calling task/process with an exit code.
 *
 * @details
 * This is the user-space entry point for `SYS_TASK_EXIT`. On success the kernel
 * does not return to the caller. If the kernel were to return for any reason,
 * this function treats it as unreachable and invokes compiler builtins to
 * communicate the non-returning contract.
 *
 * @param code Exit status for diagnostics and potential join/wait mechanisms.
 */
[[noreturn]] inline void exit(i32 code)
{
    (void)syscall1(SYS_TASK_EXIT, static_cast<u64>(code));
    __builtin_unreachable();
}

/**
 * @brief Spawn a new process from an ELF file.
 *
 * @details
 * Creates a new process by loading an ELF executable from the filesystem.
 * The new process runs in its own address space and is scheduled concurrently.
 *
 * @param path Filesystem path to the ELF executable.
 * @param name Human-readable process name (optional, uses path basename if null).
 * @param out_pid Output: Process ID of the spawned process (optional).
 * @param out_tid Output: Task ID of the spawned process's main thread (optional).
 * @return 0 on success, negative error code on failure.
 */
inline i64 spawn(const char *path, const char *name = nullptr,
                 u64 *out_pid = nullptr, u64 *out_tid = nullptr)
{
    SyscallResult r = syscall2(SYS_TASK_SPAWN,
                               reinterpret_cast<u64>(path),
                               reinterpret_cast<u64>(name));
    if (r.error == 0)
    {
        if (out_pid)
            *out_pid = r.val0;
        if (out_tid)
            *out_tid = r.val1;
    }
    return r.error;
}

/**
 * @brief Wait for any child process to exit.
 *
 * @details
 * Blocks until a child process exits and returns its exit status.
 *
 * @param status Output: Exit status of the child (optional).
 * @return Process ID of the exited child on success, negative error on failure.
 */
inline i64 wait(i32 *status = nullptr)
{
    SyscallResult r = syscall1(SYS_WAIT, reinterpret_cast<u64>(status));
    if (r.error == 0)
    {
        return static_cast<i64>(r.val0); // Return PID
    }
    return r.error;
}

/**
 * @brief Wait for a specific child process to exit.
 *
 * @param pid Process ID to wait for.
 * @param status Output: Exit status of the child (optional).
 * @return Process ID of the exited child on success, negative error on failure.
 */
inline i64 waitpid(u64 pid, i32 *status = nullptr)
{
    SyscallResult r = syscall2(SYS_WAITPID, pid, reinterpret_cast<u64>(status));
    if (r.error == 0)
    {
        return static_cast<i64>(r.val0);
    }
    return r.error;
}

/** @} */

/**
 * @name Poll / Event multiplexing
 * @brief Poll set API used to wait for readiness and timers.
 * @{
 */

/**
 * @brief Poll event bitmask values.
 *
 * @details
 * These bits describe what kind of readiness is being requested/returned.
 * The same mask is used both as an input (requested events) and output
 * (triggered events).
 */
enum PollEventType : u32
{
    POLL_NONE = 0,
    POLL_CHANNEL_READ = (1 << 0),  /**< Channel has data available to read. */
    POLL_CHANNEL_WRITE = (1 << 1), /**< Channel has space available for writing. */
    POLL_TIMER = (1 << 2),         /**< Timer has expired/fired. */
    POLL_CONSOLE_INPUT = (1 << 3), /**< Console input has a character available. */
};

/**
 * @brief Pseudo-handle used to represent console input in a poll set.
 *
 * @details
 * This is not a real capability handle. The kernel recognizes this magic value
 * when polling and treats it as an "input ready" source.
 */
constexpr u32 HANDLE_CONSOLE_INPUT = 0xFFFF0001;

/**
 * @brief One poll event record used by @ref poll_wait.
 *
 * @details
 * User-space supplies an array of these records to `poll_wait`. For each entry:
 * - Set `handle` to the handle/pseudo-handle of interest.
 * - Set `events` to the requested event mask.
 * - The kernel writes `triggered` to indicate what happened.
 */
struct PollEvent
{
    u32 handle;    /**< Handle/pseudo-handle being waited on. */
    u32 events;    /**< Requested events mask (input). */
    u32 triggered; /**< Triggered events mask (output). */
};

/**
 * @brief Create a new poll set.
 *
 * @details
 * Allocates a kernel poll set object and returns an integer identifier that
 * can be used with @ref poll_add, @ref poll_remove, and @ref poll_wait.
 *
 * @return Non-negative poll set ID on success, or negative error code on failure.
 */
inline i32 poll_create()
{
    auto r = syscall0(SYS_POLL_CREATE);
    return r.ok() ? static_cast<i32>(r.val0) : static_cast<i32>(r.error);
}

/**
 * @brief Add a handle to a poll set with a requested event mask.
 *
 * @details
 * Once added, the handle contributes readiness events to the poll set and can
 * be returned by @ref poll_wait.
 *
 * @param poll_id Poll set identifier returned by @ref poll_create.
 * @param handle Handle/pseudo-handle to watch.
 * @param mask Bitmask of @ref PollEventType values to request.
 * @return `0` on success, or negative error code on failure.
 */
inline i32 poll_add(u32 poll_id, u32 handle, u32 mask)
{
    auto r = syscall3(
        SYS_POLL_ADD, static_cast<u64>(poll_id), static_cast<u64>(handle), static_cast<u64>(mask));
    return static_cast<i32>(r.error);
}

/**
 * @brief Remove a handle from a poll set.
 *
 * @details
 * After removal, the handle no longer contributes readiness events to the poll
 * set. Removing a handle that is not present returns an error.
 *
 * @param poll_id Poll set identifier returned by @ref poll_create.
 * @param handle Handle/pseudo-handle to remove.
 * @return `0` on success, or negative error code on failure.
 */
inline i32 poll_remove(u32 poll_id, u32 handle)
{
    auto r = syscall2(SYS_POLL_REMOVE, static_cast<u64>(poll_id), static_cast<u64>(handle));
    return static_cast<i32>(r.error);
}

/**
 * @brief Wait for readiness events on a poll set.
 *
 * @details
 * This syscall is the primary blocking primitive in ViperOS. The kernel waits
 * until at least one event triggers or the timeout expires, then fills the
 * caller-provided @ref PollEvent array with triggered masks.
 *
 * The exact semantics of `timeout_ms` are kernel-defined but typically follow:
 * - `timeout_ms < 0`: wait indefinitely.
 * - `timeout_ms == 0`: poll without blocking.
 * - `timeout_ms > 0`: wait up to the given number of milliseconds.
 *
 * @param poll_id Poll set identifier returned by @ref poll_create.
 * @param events Pointer to an array of @ref PollEvent records.
 * @param max_events Maximum number of records the `events` array can hold.
 * @param timeout_ms Timeout in milliseconds, or negative to wait forever.
 * @return Number of events written on success (may be 0), or negative error code on failure.
 */
inline i32 poll_wait(u32 poll_id, PollEvent *events, u32 max_events, i64 timeout_ms)
{
    auto r = syscall4(SYS_POLL_WAIT,
                      static_cast<u64>(poll_id),
                      reinterpret_cast<u64>(events),
                      static_cast<u64>(max_events),
                      static_cast<u64>(timeout_ms));
    return r.ok() ? static_cast<i32>(r.val0) : static_cast<i32>(r.error);
}

/** @} */

/**
 * @name Debug and Console I/O
 * @brief Early bring-up output and basic console input.
 * @{
 */

/**
 * @brief Write a debug message to the kernel's debug output.
 *
 * @details
 * The kernel typically forwards debug output to serial and/or a graphical
 * console depending on configuration. This function expects `msg` to be a
 * valid NUL-terminated string in user address space.
 *
 * @param msg Pointer to a NUL-terminated message string.
 */
inline void print(const char *msg)
{
    (void)syscall1(SYS_DEBUG_PRINT, reinterpret_cast<u64>(msg));
}

/**
 * @brief Attempt to read a character from the console without blocking.
 *
 * @details
 * This wrapper calls `SYS_GETCHAR` and returns immediately:
 * - On success: returns the character value as a non-negative integer.
 * - If no input is currently available: returns a negative error code
 *   (commonly `VERR_WOULD_BLOCK`).
 *
 * This is useful for implementing custom polling loops or integrating console
 * input into a larger event loop.
 *
 * @return Character value (0–255) on success, or negative error code on failure.
 */
inline i32 try_getchar()
{
    auto r = syscall0(SYS_GETCHAR);
    return r.ok() ? static_cast<i32>(r.val0) : static_cast<i32>(r.error);
}

/**
 * @brief Read a character from the console, blocking until one is available.
 *
 * @details
 * This higher-level helper uses the poll API when available:
 * - It lazily creates a poll set and adds the console pseudo-handle.
 * - It then waits indefinitely for console readiness.
 * - After a wakeup it calls @ref try_getchar to consume the character.
 *
 * If poll creation fails (e.g., kernel does not support polling yet), the
 * function falls back to a simple busy-wait loop calling @ref try_getchar.
 *
 * @return The next character read from the console.
 */
inline char getchar()
{
    // Static poll set - created once, reused for all getchar calls
    static i32 console_poll_set = -1;
    static bool poll_initialized = false;

    if (!poll_initialized)
    {
        console_poll_set = poll_create();
        if (console_poll_set >= 0)
        {
            poll_add(static_cast<u32>(console_poll_set), HANDLE_CONSOLE_INPUT, POLL_CONSOLE_INPUT);
        }
        poll_initialized = true;
    }

    if (console_poll_set < 0)
    {
        // Fallback to busy-wait if poll unavailable
        while (true)
        {
            i32 c = try_getchar();
            if (c >= 0)
                return static_cast<char>(c);
        }
    }

    PollEvent ev;
    while (true)
    {
        // Wait for console input (infinite timeout)
        poll_wait(static_cast<u32>(console_poll_set), &ev, 1, -1);

        // Try to read character
        i32 c = try_getchar();
        if (c >= 0)
        {
            return static_cast<char>(c);
        }
        // Spurious wakeup, wait again
    }
}

/**
 * @brief Write a single character to the console.
 *
 * @details
 * Sends the character to `SYS_PUTCHAR`. This is typically routed to serial and
 * any available console output devices.
 *
 * @param c Character to write.
 */
inline void putchar(char c)
{
    (void)syscall1(SYS_PUTCHAR, static_cast<u64>(static_cast<u8>(c)));
}

/** @} */

/**
 * @name Path-based File I/O (bring-up API)
 * @brief POSIX-like wrappers operating on integer file descriptors.
 * @{
 */

/**
 * @brief Open a filesystem path and return a file descriptor.
 *
 * @details
 * This is a thin wrapper over `SYS_OPEN`. The interpretation of `flags` is
 * kernel-defined but commonly includes @ref OpenFlags values.
 *
 * @param path NUL-terminated path string.
 * @param flags Open flags bitmask.
 * @return Non-negative file descriptor on success, or negative error code on failure.
 */
inline i32 open(const char *path, u32 flags)
{
    auto r = syscall2(SYS_OPEN, reinterpret_cast<u64>(path), flags);
    return r.ok() ? static_cast<i32>(r.val0) : static_cast<i32>(r.error);
}

/**
 * @brief Close a file descriptor.
 *
 * @details
 * Releases the kernel resources associated with the file descriptor.
 *
 * @param fd File descriptor returned by @ref open.
 * @return `0` on success, or negative error code on failure.
 */
inline i32 close(i32 fd)
{
    auto r = syscall1(SYS_CLOSE, static_cast<u64>(fd));
    return static_cast<i32>(r.error);
}

/**
 * @brief Read bytes from a file descriptor.
 *
 * @details
 * Attempts to read up to `len` bytes into `buf`. On success the return value is
 * the number of bytes read (which may be 0 at end-of-file). On failure, a
 * negative error code is returned.
 *
 * @param fd File descriptor.
 * @param buf Destination buffer.
 * @param len Maximum number of bytes to read.
 * @return Number of bytes read on success, or negative error code on failure.
 */
inline i64 read(i32 fd, void *buf, usize len)
{
    auto r = syscall3(SYS_READ, static_cast<u64>(fd), reinterpret_cast<u64>(buf), len);
    return r.ok() ? static_cast<i64>(r.val0) : r.error;
}

/**
 * @brief Write bytes to a file descriptor.
 *
 * @details
 * Attempts to write up to `len` bytes from `buf`. The return value is the
 * number of bytes written on success, or a negative error code on failure.
 *
 * @param fd File descriptor.
 * @param buf Source buffer.
 * @param len Number of bytes to write.
 * @return Number of bytes written on success, or negative error code on failure.
 */
inline i64 write(i32 fd, const void *buf, usize len)
{
    auto r = syscall3(SYS_WRITE, static_cast<u64>(fd), reinterpret_cast<u64>(buf), len);
    return r.ok() ? static_cast<i64>(r.val0) : r.error;
}

/**
 * @brief Seek within a file descriptor.
 *
 * @details
 * Moves the file position according to the given offset and origin.
 *
 * @param fd File descriptor.
 * @param offset Offset in bytes.
 * @param whence One of @ref SeekWhence values (`SEEK_SET`, `SEEK_CUR`, `SEEK_END`).
 * @return New file position on success, or negative error code on failure.
 */
inline i64 lseek(i32 fd, i64 offset, i32 whence)
{
    auto r = syscall3(
        SYS_LSEEK, static_cast<u64>(fd), static_cast<u64>(offset), static_cast<u64>(whence));
    return r.ok() ? static_cast<i64>(r.val0) : r.error;
}

/**
 * @brief Query file metadata by path.
 *
 * @details
 * Fills a @ref Stat structure for the object located at `path`.
 *
 * @param path NUL-terminated path string.
 * @param st Output pointer to receive metadata.
 * @return `0` on success, or negative error code on failure.
 */
inline i32 stat(const char *path, Stat *st)
{
    auto r = syscall2(SYS_STAT, reinterpret_cast<u64>(path), reinterpret_cast<u64>(st));
    return static_cast<i32>(r.error);
}

/**
 * @brief Query file metadata by file descriptor.
 *
 * @details
 * Fills a @ref Stat structure for the object referenced by `fd`.
 *
 * @param fd File descriptor.
 * @param st Output pointer to receive metadata.
 * @return `0` on success, or negative error code on failure.
 */
inline i32 fstat(i32 fd, Stat *st)
{
    auto r = syscall2(SYS_FSTAT, static_cast<u64>(fd), reinterpret_cast<u64>(st));
    return static_cast<i32>(r.error);
}

/**
 * @brief Read directory entries into a raw buffer.
 *
 * @details
 * This is the path-based directory enumeration syscall (`SYS_READDIR`) which
 * returns a packed stream of directory entry records. Callers typically treat
 * `buf` as a byte array and walk it using each record's `reclen`.
 *
 * @param fd File descriptor for an open directory.
 * @param buf Destination buffer for packed entries.
 * @param len Buffer size in bytes.
 * @return Number of bytes written on success, or negative error code on failure.
 */
inline i64 readdir(i32 fd, void *buf, usize len)
{
    auto r = syscall3(SYS_READDIR, static_cast<u64>(fd), reinterpret_cast<u64>(buf), len);
    return r.ok() ? static_cast<i64>(r.val0) : r.error;
}

/**
 * @brief Create a directory at a path.
 *
 * @param path NUL-terminated directory path.
 * @return `0` on success, or negative error code on failure.
 */
inline i32 mkdir(const char *path)
{
    auto r = syscall1(SYS_MKDIR, reinterpret_cast<u64>(path));
    return static_cast<i32>(r.error);
}

/**
 * @brief Remove an empty directory at a path.
 *
 * @param path NUL-terminated directory path.
 * @return `0` on success, or negative error code on failure.
 */
inline i32 rmdir(const char *path)
{
    auto r = syscall1(SYS_RMDIR, reinterpret_cast<u64>(path));
    return static_cast<i32>(r.error);
}

/**
 * @brief Delete (unlink) a file at a path.
 *
 * @param path NUL-terminated file path.
 * @return `0` on success, or negative error code on failure.
 */
inline i32 unlink(const char *path)
{
    auto r = syscall1(SYS_UNLINK, reinterpret_cast<u64>(path));
    return static_cast<i32>(r.error);
}

/**
 * @brief Rename or move a filesystem object.
 *
 * @details
 * Both `old_path` and `new_path` are NUL-terminated strings. Semantics are
 * filesystem-defined and may include replacing an existing destination.
 *
 * @param old_path Current path of the object.
 * @param new_path New path for the object.
 * @return `0` on success, or negative error code on failure.
 */
inline i32 rename(const char *old_path, const char *new_path)
{
    auto r = syscall2(SYS_RENAME, reinterpret_cast<u64>(old_path), reinterpret_cast<u64>(new_path));
    return static_cast<i32>(r.error);
}

/**
 * @brief Get the current working directory.
 *
 * @details
 * Retrieves the absolute path of the current working directory for the calling
 * process. The path is always an absolute path starting with '/'.
 *
 * @param buf Buffer to receive the path.
 * @param size Size of the buffer in bytes.
 * @return Length of the path on success (not including terminating NUL),
 *         or negative error code on failure.
 */
inline i64 getcwd(char *buf, usize size)
{
    auto r = syscall2(SYS_GETCWD, reinterpret_cast<u64>(buf), size);
    return r.ok() ? static_cast<i64>(r.val0) : r.error;
}

/**
 * @brief Change the current working directory.
 *
 * @details
 * Changes the current working directory to the specified path. The path can be
 * absolute or relative to the current working directory. The kernel normalizes
 * the path, resolving "." and ".." components.
 *
 * @param path Path to the new working directory.
 * @return `0` on success, or negative error code on failure.
 */
inline i32 chdir(const char *path)
{
    auto r = syscall1(SYS_CHDIR, reinterpret_cast<u64>(path));
    return static_cast<i32>(r.error);
}

/** @} */

/**
 * @name System information
 * @brief Introspection helpers (uptime, memory statistics, etc.).
 * @{
 */

/**
 * @brief Return the kernel tick count / uptime value.
 *
 * @details
 * The unit is kernel-defined. In early bring-up it is commonly milliseconds
 * since boot, but callers should treat it as a monotonic "ticks since boot"
 * value unless the kernel guarantees a specific unit.
 *
 * @return Uptime/tick count as an unsigned 64-bit value.
 */
inline u64 uptime()
{
    auto r = syscall0(SYS_UPTIME);
    return r.ok() ? r.val0 : 0;
}

/** @} */

/**
 * @name Networking (TCP sockets + DNS)
 * @brief Minimal TCP and DNS wrappers for kernel network stack.
 * @{
 */

/**
 * @brief Create a TCP socket.
 *
 * @return Non-negative socket descriptor on success, or negative error code on failure.
 */
inline i32 socket_create()
{
    auto r = syscall0(SYS_SOCKET_CREATE);
    return r.ok() ? static_cast<i32>(r.val0) : static_cast<i32>(r.error);
}

/**
 * @brief Connect a socket to a remote IPv4 address and port.
 *
 * @details
 * `ip` is a packed IPv4 address in network byte order: `0xAABBCCDD` corresponds
 * to `AA.BB.CC.DD`.
 *
 * @param sock Socket descriptor returned by @ref socket_create.
 * @param ip Packed IPv4 address in network byte order.
 * @param port TCP port number (host byte order).
 * @return `0` on success, or negative error code on failure.
 */
inline i32 socket_connect(i32 sock, u32 ip, u16 port)
{
    auto r = syscall3(
        SYS_SOCKET_CONNECT, static_cast<u64>(sock), static_cast<u64>(ip), static_cast<u64>(port));
    return static_cast<i32>(r.error);
}

/**
 * @brief Send bytes on a connected socket.
 *
 * @param sock Socket descriptor.
 * @param data Pointer to bytes to send.
 * @param len Number of bytes to send.
 * @return Number of bytes sent on success, or negative error code on failure.
 */
inline i64 socket_send(i32 sock, const void *data, usize len)
{
    auto r = syscall3(SYS_SOCKET_SEND, static_cast<u64>(sock), reinterpret_cast<u64>(data), len);
    return r.ok() ? static_cast<i64>(r.val0) : r.error;
}

/**
 * @brief Receive bytes from a connected socket.
 *
 * @details
 * The kernel network stack may internally poll hardware/virtio to bring in
 * pending packets before attempting to read from the socket buffer.
 *
 * @param sock Socket descriptor.
 * @param buf Destination buffer.
 * @param len Maximum number of bytes to read.
 * @return Number of bytes received on success, or negative error code on failure.
 */
inline i64 socket_recv(i32 sock, void *buf, usize len)
{
    auto r = syscall3(SYS_SOCKET_RECV, static_cast<u64>(sock), reinterpret_cast<u64>(buf), len);
    return r.ok() ? static_cast<i64>(r.val0) : r.error;
}

/**
 * @brief Close a socket descriptor.
 *
 * @param sock Socket descriptor.
 * @return `0` on success, or negative error code on failure.
 */
inline i32 socket_close(i32 sock)
{
    auto r = syscall1(SYS_SOCKET_CLOSE, static_cast<u64>(sock));
    return static_cast<i32>(r.error);
}

/**
 * @brief Resolve a hostname to an IPv4 address.
 *
 * @details
 * The kernel DNS client performs the query and writes the resulting IPv4
 * address (packed in network byte order) to `ip_out`.
 *
 * @param hostname NUL-terminated hostname string.
 * @param ip_out Output pointer to receive packed IPv4 address.
 * @return `0` on success, or negative error code on failure.
 */
inline i32 dns_resolve(const char *hostname, u32 *ip_out)
{
    auto r =
        syscall2(SYS_DNS_RESOLVE, reinterpret_cast<u64>(hostname), reinterpret_cast<u64>(ip_out));
    return static_cast<i32>(r.error);
}

/**
 * @brief Pack four IPv4 octets into a `u32` in network byte order.
 *
 * @details
 * This helper matches the kernel ABI used by @ref socket_connect and
 * @ref dns_resolve: `ip_pack(192, 0, 2, 1)` produces `0xC0000201`.
 *
 * @param a First octet.
 * @param b Second octet.
 * @param c Third octet.
 * @param d Fourth octet.
 * @return Packed IPv4 address value in network byte order.
 */
inline u32 ip_pack(u8 a, u8 b, u8 c, u8 d)
{
    return (static_cast<u32>(a) << 24) | (static_cast<u32>(b) << 16) | (static_cast<u32>(c) << 8) |
           static_cast<u32>(d);
}

/** @} */

/**
 * @name TLS (Transport Layer Security)
 * @brief Kernel-managed TLS sessions layered over TCP sockets.
 * @{
 */

/**
 * @brief Create a TLS session over an existing TCP socket.
 *
 * @details
 * The kernel allocates a TLS session object and associates it with the given
 * socket descriptor. The returned session ID is then used with @ref tls_handshake,
 * @ref tls_send, @ref tls_recv, and @ref tls_close.
 *
 * `hostname` is used for SNI and (when verification is enabled) for certificate
 * name checks. For early bring-up, callers may disable verification, but doing
 * so removes protection against active network attackers.
 *
 * @param sock Connected TCP socket descriptor.
 * @param hostname Optional hostname for SNI/verification (NUL-terminated).
 * @param verify Whether to verify the server certificate chain and hostname.
 * @return Non-negative TLS session ID on success, or negative error code on failure.
 */
inline i32 tls_create(i32 sock, const char *hostname, bool verify = true)
{
    auto r = syscall3(SYS_TLS_CREATE,
                      static_cast<u64>(sock),
                      reinterpret_cast<u64>(hostname),
                      static_cast<u64>(verify ? 1 : 0));
    return r.ok() ? static_cast<i32>(r.val0) : static_cast<i32>(r.error);
}

/**
 * @brief Perform the TLS handshake for an existing TLS session.
 *
 * @details
 * This drives the protocol handshake until the session is ready for
 * application data. On failure, the kernel may log additional diagnostic
 * information to serial output.
 *
 * @param tls_session Session ID returned by @ref tls_create.
 * @return `0` on success, or negative error code on failure.
 */
inline i32 tls_handshake(i32 tls_session)
{
    auto r = syscall1(SYS_TLS_HANDSHAKE, static_cast<u64>(tls_session));
    return static_cast<i32>(r.error);
}

/**
 * @brief Send application data over a TLS session.
 *
 * @details
 * The kernel encrypts the plaintext bytes and transmits them on the underlying
 * socket. The return value is typically the number of plaintext bytes consumed.
 *
 * @param tls_session Session ID.
 * @param data Pointer to plaintext bytes.
 * @param len Number of bytes to send.
 * @return Number of bytes sent on success, or negative error code on failure.
 */
inline i64 tls_send(i32 tls_session, const void *data, usize len)
{
    auto r =
        syscall3(SYS_TLS_SEND, static_cast<u64>(tls_session), reinterpret_cast<u64>(data), len);
    return r.ok() ? static_cast<i64>(r.val0) : r.error;
}

/**
 * @brief Receive application data from a TLS session.
 *
 * @details
 * The kernel reads records from the underlying socket, decrypts them, and
 * writes plaintext into `buf`. The return value is the number of plaintext
 * bytes produced, or a negative error code.
 *
 * @param tls_session Session ID.
 * @param buf Destination buffer for plaintext.
 * @param len Maximum number of bytes to read.
 * @return Number of bytes received on success, or negative error code on failure.
 */
inline i64 tls_recv(i32 tls_session, void *buf, usize len)
{
    auto r = syscall3(SYS_TLS_RECV, static_cast<u64>(tls_session), reinterpret_cast<u64>(buf), len);
    return r.ok() ? static_cast<i64>(r.val0) : r.error;
}

/**
 * @brief Close a TLS session.
 *
 * @details
 * Releases kernel resources associated with the TLS session and detaches it
 * from the underlying socket.
 *
 * @param tls_session Session ID.
 * @return `0` on success, or negative error code on failure.
 */
inline i32 tls_close(i32 tls_session)
{
    auto r = syscall1(SYS_TLS_CLOSE, static_cast<u64>(tls_session));
    return static_cast<i32>(r.error);
}

/**
 * @brief Query metadata for a TLS session.
 *
 * @details
 * Fills a shared @ref TLSInfo structure with the kernel's view of the
 * negotiated protocol parameters and verification status.
 *
 * @param tls_session Session ID.
 * @param info Output pointer to receive session information.
 * @return `0` on success, or negative error code on failure.
 */
inline i32 tls_info(i32 tls_session, ::TLSInfo *info)
{
    auto r = syscall2(SYS_TLS_INFO, static_cast<u64>(tls_session), reinterpret_cast<u64>(info));
    return static_cast<i32>(r.error);
}

/** @} */

/**
 * @brief Query global physical memory usage statistics.
 *
 * @details
 * Calls `SYS_MEM_INFO` and fills a shared @ref MemInfo structure with page and
 * byte counts. This is a snapshot and is intended for diagnostics rather than
 * strict accounting.
 *
 * @param info Output pointer to receive memory statistics.
 * @return `0` on success, or negative error code on failure.
 */
inline i32 mem_info(::MemInfo *info)
{
    auto r = syscall1(SYS_MEM_INFO, reinterpret_cast<u64>(info));
    return static_cast<i32>(r.error);
}

/**
 * @brief Request a snapshot of runnable tasks/processes.
 *
 * @details
 * The kernel fills up to `max_count` entries in `buffer` with @ref TaskInfo
 * records and returns the number of entries written.
 *
 * This syscall may be reserved or not yet implemented depending on the kernel
 * build; in that case it may return `VERR_NOT_SUPPORTED`.
 *
 * @param buffer Output buffer for @ref TaskInfo entries.
 * @param max_count Maximum number of entries the buffer can hold.
 * @return Number of tasks written on success, or negative error code on failure.
 */
inline i32 task_list(::TaskInfo *buffer, u32 max_count)
{
    auto r = syscall2(SYS_TASK_LIST, reinterpret_cast<u64>(buffer), static_cast<u64>(max_count));
    return r.ok() ? static_cast<i32>(r.val0) : static_cast<i32>(r.error);
}

/**
 * @name Assign system
 * @brief Name → directory-handle mappings used for logical device paths.
 * @{
 */

/**
 * @brief Create or update an assign mapping.
 *
 * @details
 * Associates `name` with a directory handle. The name must not include the
 * trailing colon (use `"SYS"`, not `"SYS:"`).
 *
 * @param name NUL-terminated assign name.
 * @param dir_handle Directory handle to associate with the name.
 * @return `0` on success, or negative error code on failure.
 */
inline i32 assign_set(const char *name, u32 dir_handle)
{
    auto r = syscall3(
        SYS_ASSIGN_SET, reinterpret_cast<u64>(name), strlen(name), static_cast<u64>(dir_handle));
    return static_cast<i32>(r.error);
}

/**
 * @brief Look up an assign and return its directory handle.
 *
 * @param name NUL-terminated assign name (without colon).
 * @param out_handle Output pointer to receive the resolved directory handle.
 * @return `0` on success, or negative error code on failure.
 */
inline i32 assign_get(const char *name, u32 *out_handle)
{
    auto r = syscall3(SYS_ASSIGN_GET,
                      reinterpret_cast<u64>(name),
                      strlen(name),
                      reinterpret_cast<u64>(out_handle));
    return static_cast<i32>(r.error);
}

/**
 * @brief Remove an assign mapping.
 *
 * @param name NUL-terminated assign name (without colon).
 * @return `0` on success, or negative error code on failure.
 */
inline i32 assign_remove(const char *name)
{
    auto r = syscall2(SYS_ASSIGN_REMOVE, reinterpret_cast<u64>(name), strlen(name));
    return static_cast<i32>(r.error);
}

/**
 * @brief Enumerate known assigns.
 *
 * @details
 * The kernel writes up to `max` entries into `buf` and writes the number of
 * entries produced to `out_count`.
 *
 * @param buf Output array for assign entries.
 * @param max Maximum number of entries `buf` can hold.
 * @param out_count Output pointer receiving the number of entries written.
 * @return `0` on success, or negative error code on failure.
 */
inline i32 assign_list(AssignInfo *buf, u32 max, usize *out_count)
{
    auto r = syscall3(SYS_ASSIGN_LIST,
                      reinterpret_cast<u64>(buf),
                      static_cast<u64>(max),
                      reinterpret_cast<u64>(out_count));
    return static_cast<i32>(r.error);
}

/**
 * @brief Resolve an assign-prefixed path into a capability handle.
 *
 * @details
 * The kernel resolves a path that may begin with an assign prefix such as
 * `SYS:` and returns a capability handle to the resolved object.
 *
 * The returned handle can then be used with the handle-based filesystem API
 * (if implemented by the kernel).
 *
 * @param path NUL-terminated path (e.g., `"SYS:certs/roots.der"`).
 * @param out_handle Output pointer receiving the resolved handle.
 * @return `0` on success, or negative error code on failure.
 */
inline i32 assign_resolve(const char *path, u32 *out_handle)
{
    auto r = syscall3(SYS_ASSIGN_RESOLVE,
                      reinterpret_cast<u64>(path),
                      strlen(path),
                      reinterpret_cast<u64>(out_handle));
    return static_cast<i32>(r.error);
}

/** @} */

/**
 * @name Capability table helpers
 * @brief Inspect and manipulate capability handles (if supported by the kernel).
 *
 * @details
 * These wrappers correspond to `SYS_CAP_*` syscalls. Depending on kernel
 * maturity, they may be unimplemented and return `VERR_NOT_SUPPORTED`.
 * @{
 */

/**
 * @brief Derive a new handle with reduced rights from an existing handle.
 *
 * @details
 * Capability derivation is a core least-privilege mechanism: a process can
 * create a child handle with fewer rights and then pass that handle to another
 * component without granting broader access.
 *
 * The kernel typically requires the parent handle to include `CAP_RIGHT_DERIVE`.
 *
 * @param parent_handle Existing handle to derive from.
 * @param new_rights Rights mask for the derived handle.
 * @return New handle on success, or negative error code on failure.
 */
inline i32 cap_derive(u32 parent_handle, u32 new_rights)
{
    auto r =
        syscall2(SYS_CAP_DERIVE, static_cast<u64>(parent_handle), static_cast<u64>(new_rights));
    return r.ok() ? static_cast<i32>(r.val0) : static_cast<i32>(r.error);
}

/**
 * @brief Revoke/close a capability handle.
 *
 * @details
 * This removes the handle from the current process. After revocation, further
 * use of the handle should fail with an invalid-handle error.
 *
 * @param handle Handle to revoke.
 * @return `0` on success, or negative error code on failure.
 */
inline i32 cap_revoke(u32 handle)
{
    auto r = syscall1(SYS_CAP_REVOKE, static_cast<u64>(handle));
    return static_cast<i32>(r.error);
}

/**
 * @brief Query capability metadata for a handle.
 *
 * @details
 * On success, the kernel fills the provided @ref CapInfo with kind, rights,
 * and generation information for the handle.
 *
 * @param handle Handle to query.
 * @param info Output pointer to receive metadata.
 * @return `0` on success, or negative error code on failure.
 */
inline i32 cap_query(u32 handle, ::CapInfo *info)
{
    auto r = syscall2(SYS_CAP_QUERY, static_cast<u64>(handle), reinterpret_cast<u64>(info));
    return static_cast<i32>(r.error);
}

/**
 * @brief Enumerate the calling process capability table.
 *
 * @details
 * If `buffer` is null or `max_count` is 0, the kernel may return the number of
 * capabilities without writing entries (count-only query).
 *
 * Otherwise, the kernel writes up to `max_count` @ref CapListEntry records into
 * `buffer` and returns the number of entries written.
 *
 * @param buffer Output buffer for entries, or null for count-only query.
 * @param max_count Maximum number of entries `buffer` can hold.
 * @return Non-negative count on success, or negative error code on failure.
 */
inline i32 cap_list(::CapListEntry *buffer, u32 max_count)
{
    auto r = syscall2(SYS_CAP_LIST, reinterpret_cast<u64>(buffer), static_cast<u64>(max_count));
    return r.ok() ? static_cast<i32>(r.val0) : static_cast<i32>(r.error);
}

/**
 * @brief Convert a capability kind value to a human-readable string.
 *
 * @details
 * This helper is intended for diagnostics and UI. Unknown kinds return
 * `"Unknown"`.
 *
 * @param kind One of `CAP_KIND_*` values.
 * @return Pointer to a static string literal.
 */
inline const char *cap_kind_name(u16 kind)
{
    switch (kind)
    {
        case CAP_KIND_INVALID:
            return "Invalid";
        case CAP_KIND_STRING:
            return "String";
        case CAP_KIND_ARRAY:
            return "Array";
        case CAP_KIND_BLOB:
            return "Blob";
        case CAP_KIND_CHANNEL:
            return "Channel";
        case CAP_KIND_POLL:
            return "Poll";
        case CAP_KIND_TIMER:
            return "Timer";
        case CAP_KIND_TASK:
            return "Task";
        case CAP_KIND_VIPER:
            return "Viper";
        case CAP_KIND_FILE:
            return "File";
        case CAP_KIND_DIRECTORY:
            return "Directory";
        case CAP_KIND_SURFACE:
            return "Surface";
        case CAP_KIND_INPUT:
            return "Input";
        default:
            return "Unknown";
    }
}

/**
 * @brief Format a rights mask as a compact `rwx...` string.
 *
 * @details
 * Produces a fixed 9-character representation plus a terminating NUL. Each
 * position corresponds to one right, emitting the right's letter when present
 * and `'-'` when absent.
 *
 * The output layout is:
 * - `r` `w` `x` `l` `c` `d` `D` `t` `s`
 *
 * @param rights Rights bitmask (`CAP_RIGHT_*`).
 * @param buf Destination buffer.
 * @param buf_size Size of the destination buffer in bytes.
 */
inline void cap_rights_str(u32 rights, char *buf, usize buf_size)
{
    if (buf_size < 10)
        return;
    usize i = 0;
    buf[i++] = (rights & CAP_RIGHT_READ) ? 'r' : '-';
    buf[i++] = (rights & CAP_RIGHT_WRITE) ? 'w' : '-';
    buf[i++] = (rights & CAP_RIGHT_EXECUTE) ? 'x' : '-';
    buf[i++] = (rights & CAP_RIGHT_LIST) ? 'l' : '-';
    buf[i++] = (rights & CAP_RIGHT_CREATE) ? 'c' : '-';
    buf[i++] = (rights & CAP_RIGHT_DELETE) ? 'd' : '-';
    buf[i++] = (rights & CAP_RIGHT_DERIVE) ? 'D' : '-';
    buf[i++] = (rights & CAP_RIGHT_TRANSFER) ? 't' : '-';
    buf[i++] = (rights & CAP_RIGHT_SPAWN) ? 's' : '-';
    buf[i] = '\0';
}

/** @} */

/**
 * @name Handle-based filesystem API
 * @brief Filesystem operations on capability handles (experimental/bring-up).
 *
 * @details
 * This API is intended to operate on capability handles rather than global
 * integer file descriptors. In the fully-capability-based design:
 * - A "directory handle" names a directory object.
 * - `fs_open` opens a child relative to that directory and returns a new handle.
 * - `io_read`/`io_write` operate on file handles.
 *
 * Depending on kernel maturity, these syscalls may not yet be implemented and
 * may return `VERR_NOT_SUPPORTED`.
 * @{
 */

/**
 * @brief Directory entry record returned by @ref fs_read_dir.
 *
 * @details
 * Unlike @ref DirEnt (used by the path-based `readdir`), this structure is
 * returned one entry at a time by the handle-based API.
 */
struct FsDirEnt
{
    u64 inode;      /**< Inode number for the entry. */
    u8 type;        /**< Entry type (implementation-defined; commonly 1=file, 2=dir). */
    u8 name_len;    /**< Length of @ref name in bytes (excluding NUL). */
    char name[256]; /**< NUL-terminated name (may be truncated). */
};

/**
 * @brief Open the filesystem root directory.
 *
 * @details
 * Returns a directory capability handle representing the root. Callers should
 * eventually release the handle with @ref fs_close.
 *
 * @return Directory handle on success, or negative error code on failure.
 */
inline i32 fs_open_root()
{
    auto r = syscall0(SYS_FS_OPEN_ROOT);
    return r.ok() ? static_cast<i32>(r.val0) : static_cast<i32>(r.error);
}

/**
 * @brief Open a file or directory relative to an existing directory handle.
 *
 * @details
 * This is the fundamental operation for walking directories without a global
 * process-wide current working directory. The kernel interprets `name` as a
 * single path component (not a full path string).
 *
 * @param dir_handle Directory handle to open relative to.
 * @param name Entry name (single path component).
 * @param name_len Length of `name` in bytes.
 * @param flags Open flags (subset of @ref OpenFlags).
 * @return File/directory handle on success, or negative error code on failure.
 */
inline i32 fs_open(u32 dir_handle, const char *name, usize name_len, u32 flags)
{
    auto r = syscall4(SYS_FS_OPEN,
                      static_cast<u64>(dir_handle),
                      reinterpret_cast<u64>(name),
                      static_cast<u64>(name_len),
                      static_cast<u64>(flags));
    return r.ok() ? static_cast<i32>(r.val0) : static_cast<i32>(r.error);
}

/**
 * @brief Convenience overload of @ref fs_open for NUL-terminated names.
 *
 * @param dir_handle Directory handle to open relative to.
 * @param name NUL-terminated entry name.
 * @param flags Open flags.
 * @return File/directory handle on success, or negative error code on failure.
 */
inline i32 fs_open(u32 dir_handle, const char *name, u32 flags)
{
    return fs_open(dir_handle, name, strlen(name), flags);
}

/**
 * @brief Read bytes from a file handle.
 *
 * @details
 * Reads up to `len` bytes into `buffer`. Returns the number of bytes read, or
 * 0 at end-of-file.
 *
 * @param file_handle File handle.
 * @param buffer Destination buffer.
 * @param len Maximum number of bytes to read.
 * @return Number of bytes read on success, 0 at EOF, or negative error code on failure.
 */
inline i64 io_read(u32 file_handle, void *buffer, usize len)
{
    auto r = syscall3(SYS_IO_READ,
                      static_cast<u64>(file_handle),
                      reinterpret_cast<u64>(buffer),
                      static_cast<u64>(len));
    return r.ok() ? static_cast<i64>(r.val0) : r.error;
}

/**
 * @brief Write bytes to a file handle.
 *
 * @details
 * Writes up to `len` bytes from `buffer`. Returns the number of bytes written
 * on success.
 *
 * @param file_handle File handle.
 * @param buffer Source buffer.
 * @param len Number of bytes to write.
 * @return Number of bytes written on success, or negative error code on failure.
 */
inline i64 io_write(u32 file_handle, const void *buffer, usize len)
{
    auto r = syscall3(SYS_IO_WRITE,
                      static_cast<u64>(file_handle),
                      reinterpret_cast<u64>(buffer),
                      static_cast<u64>(len));
    return r.ok() ? static_cast<i64>(r.val0) : r.error;
}

/**
 * @brief Seek within a file handle.
 *
 * @details
 * Adjusts the file's current position. `whence` uses the same values as the
 * path-based @ref lseek wrapper.
 *
 * @param file_handle File handle.
 * @param offset Offset in bytes.
 * @param whence One of @ref SeekWhence values.
 * @return New position on success, or negative error code on failure.
 */
inline i64 io_seek(u32 file_handle, i64 offset, i32 whence)
{
    auto r = syscall3(SYS_IO_SEEK,
                      static_cast<u64>(file_handle),
                      static_cast<u64>(offset),
                      static_cast<u64>(whence));
    return r.ok() ? static_cast<i64>(r.val0) : r.error;
}

/**
 * @brief Read the next directory entry from a directory handle.
 *
 * @details
 * On success, the kernel writes one entry into `entry` and returns:
 * - `1` if an entry was produced.
 * - `0` if the end of the directory was reached.
 *
 * @param dir_handle Directory handle.
 * @param entry Output pointer to receive a directory entry record.
 * @return 1 if an entry was returned, 0 on end-of-directory, or negative error code on failure.
 */
inline i32 fs_read_dir(u32 dir_handle, FsDirEnt *entry)
{
    auto r = syscall2(SYS_FS_READ_DIR, static_cast<u64>(dir_handle), reinterpret_cast<u64>(entry));
    return r.ok() ? static_cast<i32>(r.val0) : static_cast<i32>(r.error);
}

/**
 * @brief Reset directory enumeration to the beginning.
 *
 * @details
 * After calling this function, the next @ref fs_read_dir call returns the first
 * entry in the directory again.
 *
 * @param dir_handle Directory handle.
 * @return `0` on success, or negative error code on failure.
 */
inline i32 fs_rewind_dir(u32 dir_handle)
{
    auto r = syscall1(SYS_FS_REWIND_DIR, static_cast<u64>(dir_handle));
    return static_cast<i32>(r.error);
}

/**
 * @brief Close a file or directory handle.
 *
 * @details
 * Releases the handle from the current process. Once closed, the handle value
 * should not be used again.
 *
 * @param handle File or directory handle.
 * @return `0` on success, or negative error code on failure.
 */
inline i32 fs_close(u32 handle)
{
    auto r = syscall1(SYS_FS_CLOSE, static_cast<u64>(handle));
    return static_cast<i32>(r.error);
}

/**
 * @brief Convenience helper to open a slash-separated path starting at root.
 *
 * @details
 * This helper is implemented entirely in user-space by repeatedly calling
 * @ref fs_open on each path component:
 * 1. Open the root directory (`fs_open_root`).
 * 2. Split `path` by `'/'`.
 * 3. Open each component relative to the previous directory handle.
 * 4. Close intermediate directory handles so only the final handle remains.
 *
 * The final component is opened with `flags`; intermediate components are
 * opened read-only.
 *
 * @param path NUL-terminated slash-separated path (e.g., `"dir/subdir/file.txt"`).
 * @param flags Open flags for the final component.
 * @return Handle for the final component on success, or negative error code on failure.
 */
inline i32 fs_open_path(const char *path, u32 flags)
{
    // Start from root
    i32 result = fs_open_root();
    if (result < 0)
        return result;
    u32 current_handle = static_cast<u32>(result);

    // Skip leading slashes
    while (*path == '/')
        path++;

    // Parse path components
    while (*path)
    {
        // Find end of component
        const char *start = path;
        while (*path && *path != '/')
            path++;
        usize len = path - start;

        if (len == 0)
        {
            // Skip multiple slashes
            while (*path == '/')
                path++;
            continue;
        }

        // Is this the last component?
        bool is_last = (*path == '\0' || *(path + 1) == '\0');
        while (*path == '/')
            path++;
        is_last = is_last || (*path == '\0');

        // Open this component
        u32 open_flags = is_last ? flags : O_RDONLY;
        result = fs_open(current_handle, start, len, open_flags);

        // Close the previous directory handle (unless it's root on first iteration)
        fs_close(current_handle);

        if (result < 0)
            return result;
        current_handle = static_cast<u32>(result);
    }

    return static_cast<i32>(current_handle);
}

/** @} */

} // namespace sys
