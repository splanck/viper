//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: include/viperdos/syscall_nums.hpp
// Purpose: Shared syscall number assignments (user/kernel ABI).
// Key invariants: Single source of truth for syscall numbers.
// Ownership/Lifetime: Header-only; constant definitions.
// Links: kernel/syscall/table.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

/**
 * @file syscall_nums.hpp
 * @brief Shared syscall number assignments (user/kernel ABI).
 *
 * @details
 * This header is the single source of truth for syscall numeric identifiers.
 * Both the kernel syscall dispatcher and user-space wrappers include this file
 * to ensure they agree on the ABI contract.
 *
 * AArch64 calling convention used by ViperDOS (see `include/viperdos/syscall_abi.hpp`):
 * - The syscall number is placed in register x8.
 * - Up to six arguments are placed in x0-x5.
 * - Return values are:
 *   - x0: `VError` (0 = success, negative = error)
 *   - x1-x3: result values on success (syscall-specific)
 *
 * The identifiers are grouped into ranges by subsystem to keep the table
 * readable and to leave room for future expansion.
 */

/** @name Task Management Syscalls (0x00 - 0x0F)
 *  @details
 *  Task/process management operations. Depending on kernel maturity, only a
 *  subset may be implemented; unimplemented syscalls typically return
 *  `VERR_NOT_SUPPORTED`.
 *  @{
 */
/** @brief Yield the CPU to the scheduler. */
#define SYS_TASK_YIELD 0x00
/** @brief Terminate the calling task with an exit code. */
#define SYS_TASK_EXIT 0x01
/** @brief Return the calling task's ID. */
#define SYS_TASK_CURRENT 0x02
/** @brief Spawn a new user process from an ELF file. */
#define SYS_TASK_SPAWN 0x03
/** @brief Join/wait for another task to exit (reserved for future use). */
#define SYS_TASK_JOIN 0x04
/** @brief Enumerate tasks into a caller-provided buffer (reserved for future use). */
#define SYS_TASK_LIST 0x05
/** @brief Set the priority of a task (0=highest, 255=lowest). */
#define SYS_TASK_SET_PRIORITY 0x06
/** @brief Get the priority of a task. */
#define SYS_TASK_GET_PRIORITY 0x07
/** @brief Wait for any child process to exit. */
#define SYS_WAIT 0x08
/** @brief Wait for a specific child process to exit. */
#define SYS_WAITPID 0x09
/** @brief Adjust process heap break (sbrk). */
#define SYS_SBRK 0x0A
/** @brief Fork the current process (copy-on-write). */
#define SYS_FORK 0x0B
/** @brief Spawn a new user process from a SharedMemory region containing an ELF image. */
#define SYS_TASK_SPAWN_SHM 0x0C
/** @brief Replace current process image with a new executable (exec-like). */
#define SYS_REPLACE 0x0F
/** @brief Set CPU affinity mask for a task. */
#define SYS_SCHED_SETAFFINITY 0x0D
/** @brief Get CPU affinity mask for a task. */
#define SYS_SCHED_GETAFFINITY 0x0E
/** @} */

/** @name Channel IPC Syscalls (0x10 - 0x1F)
 *  @details
 *  Non-blocking message passing primitives. When operations cannot complete
 *  immediately, they return `VERR_WOULD_BLOCK` rather than sleeping.
 *  @{
 */
/** @brief Create a new IPC channel and return send+recv endpoint handles. */
#define SYS_CHANNEL_CREATE 0x10
/** @brief Send a message on a channel. */
#define SYS_CHANNEL_SEND 0x11
/** @brief Receive a message from a channel. */
#define SYS_CHANNEL_RECV 0x12
/** @brief Close a channel handle. */
#define SYS_CHANNEL_CLOSE 0x13
/** @} */

/** @name Poll Syscalls (0x20 - 0x2F)
 *  @details
 *  Event multiplexing primitives used to wait for readiness/expiration.
 *  `SYS_POLL_WAIT` is typically the primary blocking syscall.
 *  @{
 */
/** @brief Create a new poll set and return its handle/ID. */
#define SYS_POLL_CREATE 0x20
/** @brief Add a handle/event mask to a poll set. */
#define SYS_POLL_ADD 0x21
/** @brief Remove a handle from a poll set. */
#define SYS_POLL_REMOVE 0x22
/** @brief Wait for events in a poll set (may block). */
#define SYS_POLL_WAIT 0x23
/** @} */

/** @name Time Syscalls (0x30 - 0x3F)
 *  @details
 *  Basic time and timer primitives. `SYS_SLEEP` may block.
 *  @{
 */
/** @brief Return a monotonic time value (typically milliseconds since boot). */
#define SYS_TIME_NOW 0x30
/** @brief Sleep for a number of milliseconds (may block). */
#define SYS_SLEEP 0x31
/** @brief Create a timer object (reserved for future use). */
#define SYS_TIMER_CREATE 0x32
/** @brief Cancel a timer object (reserved for future use). */
#define SYS_TIMER_CANCEL 0x33
/** @} */

/** @name File Descriptor I/O Syscalls (0x40 - 0x4F)
 *  @details
 *  Path-based, POSIX-like file descriptor operations (bring-up API).
 *  @{
 */
/** @brief Open a path and return an integer file descriptor. */
#define SYS_OPEN 0x40
/** @brief Close an integer file descriptor. */
#define SYS_CLOSE 0x41
/** @brief Read bytes from a file descriptor into a buffer. */
#define SYS_READ 0x42
/** @brief Write bytes from a buffer to a file descriptor. */
#define SYS_WRITE 0x43
/** @brief Seek within a file descriptor. */
#define SYS_LSEEK 0x44
/** @brief Stat a path and fill a stat structure. */
#define SYS_STAT 0x45
/** @brief Stat an open file descriptor and fill a stat structure. */
#define SYS_FSTAT 0x46
/** @brief Duplicate a file descriptor to lowest available slot. */
#define SYS_DUP 0x47
/** @brief Duplicate a file descriptor to a specific slot. */
#define SYS_DUP2 0x48
/** @brief Sync file data to storage. */
#define SYS_FSYNC 0x49
/** @} */

/** @name Networking Syscalls (0x50 - 0x5F)
 *  @details
 *  Socket-like operations for the kernel TCP/IP stack plus DNS resolution.
 *  @{
 */
/** @brief Create a TCP socket and return a socket descriptor. */
#define SYS_SOCKET_CREATE 0x50
/** @brief Connect a socket to a remote IPv4/port endpoint. */
#define SYS_SOCKET_CONNECT 0x51
/** @brief Send bytes on a connected socket. */
#define SYS_SOCKET_SEND 0x52
/** @brief Receive bytes from a connected socket. */
#define SYS_SOCKET_RECV 0x53
/** @brief Close a socket descriptor. */
#define SYS_SOCKET_CLOSE 0x54
/** @brief Resolve a hostname to a packed IPv4 address. */
#define SYS_DNS_RESOLVE 0x55
/** @} */

/** @name Directory / Filesystem Maintenance Syscalls (0x60 - 0x6F)
 *  @details
 *  Path-based directory enumeration and basic maintenance operations.
 *  @{
 */
/** @brief Read directory entries from an open directory file descriptor. */
#define SYS_READDIR 0x60
/** @brief Create a directory at a path. */
#define SYS_MKDIR 0x61
/** @brief Remove an empty directory at a path. */
#define SYS_RMDIR 0x62
/** @brief Unlink (delete) a file at a path. */
#define SYS_UNLINK 0x63
/** @brief Rename/move a path. */
#define SYS_RENAME 0x64
/** @brief Create a symbolic link. */
#define SYS_SYMLINK 0x65
/** @brief Read symbolic link target. */
#define SYS_READLINK 0x66
/** @brief Get current working directory. */
#define SYS_GETCWD 0x67
/** @brief Change current working directory. */
#define SYS_CHDIR 0x68
/** @} */

/** @name Capability Syscalls (0x70 - 0x7F)
 *  @details
 *  Capability table inspection and manipulation. These calls are part of the
 *  handle-based object model. Depending on kernel maturity, they may be
 *  reserved for future use.
 *  @{
 */
/** @brief Derive a new handle with reduced rights from an existing handle. */
#define SYS_CAP_DERIVE 0x70
/** @brief Revoke/close a capability handle. */
#define SYS_CAP_REVOKE 0x71
/** @brief Query the kind/rights/generation of a handle. */
#define SYS_CAP_QUERY 0x72
/** @brief Enumerate handles in the current process capability table. */
#define SYS_CAP_LIST 0x73
/** @brief Get the capability bounding set for the current process. */
#define SYS_CAP_GET_BOUND 0x74
/** @brief Drop rights from the capability bounding set (irreversible). */
#define SYS_CAP_DROP_BOUND 0x75
/** @brief Get a resource limit for the current process. */
#define SYS_GETRLIMIT 0x76
/** @brief Set a resource limit for the current process (can only reduce). */
#define SYS_SETRLIMIT 0x77
/** @brief Get current resource usage for the current process. */
#define SYS_GETRUSAGE 0x78
/** @} */

/** @name Signal Syscalls (0x90 - 0x9F)
 *  @details
 *  POSIX-like signal handling for user-space tasks.
 *  @{
 */
/** @brief Set signal action (handler, mask, flags). */
#define SYS_SIGACTION 0x90
/** @brief Set/get blocked signal mask. */
#define SYS_SIGPROCMASK 0x91
/** @brief Return from signal handler (restores original context). */
#define SYS_SIGRETURN 0x92
/** @brief Send signal to a task/process. */
#define SYS_KILL 0x93
/** @brief Get pending signals. */
#define SYS_SIGPENDING 0x94
/** @} */

/** @name Process Group/Session Syscalls (0xA0 - 0xAF)
 *  @details
 *  POSIX-like process groups and sessions for job control.
 *  @{
 */
/** @brief Get process ID of calling process. */
#define SYS_GETPID 0xA0
/** @brief Get parent process ID of calling process. */
#define SYS_GETPPID 0xA1
/** @brief Get process group ID of a process. */
#define SYS_GETPGID 0xA2
/** @brief Set process group ID of a process. */
#define SYS_SETPGID 0xA3
/** @brief Get session ID of a process. */
#define SYS_GETSID 0xA4
/** @brief Create a new session with calling process as leader. */
#define SYS_SETSID 0xA5
/** @brief Get command-line arguments for the current process. */
#define SYS_GET_ARGS 0xA6
/** @} */

/** @name Handle-based Filesystem Syscalls (0x80 - 0x8F)
 *  @details
 *  Object-capability filesystem API that operates on directory/file handles
 *  rather than global integer file descriptors. These identifiers reserve the
 *  ABI for a future transition away from a global FD table.
 *  @{
 */
/** @brief Open the filesystem root directory and return a directory handle. */
#define SYS_FS_OPEN_ROOT 0x80 // Get root directory handle
/** @brief Open a file/directory relative to a directory handle. */
#define SYS_FS_OPEN 0x81 // Open file/dir relative to dir handle
/** @brief Read bytes from a file handle. */
#define SYS_IO_READ 0x82 // Read from file handle
/** @brief Write bytes to a file handle. */
#define SYS_IO_WRITE 0x83 // Write to file handle
/** @brief Seek within a file handle. */
#define SYS_IO_SEEK 0x84 // Seek within file handle
/** @brief Read the next directory entry from a directory handle. */
#define SYS_FS_READ_DIR 0x85 // Read next directory entry
/** @brief Close a file/directory handle. */
#define SYS_FS_CLOSE 0x86 // Close file/directory handle
/** @brief Reset directory enumeration to the beginning. */
#define SYS_FS_REWIND_DIR 0x87 // Reset directory enumeration
/** @} */

/** @name Assign System Syscalls (0xC0 - 0xCF)
 *  @details
 *  The assign system maps a short name (e.g., `SYS`) to a directory handle and
 *  allows paths like `SYS:foo/bar` to be resolved by the kernel.
 *  @{
 */
/** @brief Create or update an assign mapping. */
#define SYS_ASSIGN_SET 0xC0
/** @brief Query an assign mapping. */
#define SYS_ASSIGN_GET 0xC1
/** @brief Remove an assign mapping. */
#define SYS_ASSIGN_REMOVE 0xC2
/** @brief Enumerate known assigns into a buffer. */
#define SYS_ASSIGN_LIST 0xC3
/** @brief Resolve an assign-prefixed path into a capability handle. */
#define SYS_ASSIGN_RESOLVE 0xC4
/** @} */

/** @name TLS Syscalls (0xD0 - 0xDF)
 *  @details
 *  Kernel-managed TLS sessions layered on top of kernel TCP sockets.
 *  @{
 */
/** @brief Create a TLS session over an existing socket. */
#define SYS_TLS_CREATE 0xD0
/** @brief Perform the TLS handshake for an existing session. */
#define SYS_TLS_HANDSHAKE 0xD1
/** @brief Send application data over a TLS session. */
#define SYS_TLS_SEND 0xD2
/** @brief Receive application data from a TLS session. */
#define SYS_TLS_RECV 0xD3
/** @brief Close a TLS session. */
#define SYS_TLS_CLOSE 0xD4
/** @brief Query TLS session metadata into a @ref TLSInfo structure. */
#define SYS_TLS_INFO 0xD5
/** @} */

/** @name System Information Syscalls (0xE0 - 0xEF)
 *  @details
 *  Introspection APIs that return coarse system-level statistics.
 *  @{
 */
/** @brief Fill a @ref MemInfo structure with physical memory statistics. */
#define SYS_MEM_INFO 0xE0
/** @brief Fill a NetStats structure with network statistics. */
#define SYS_NET_STATS 0xE1
/** @brief Send ICMP ping and get RTT (args: ip_addr, timeout_ms). */
#define SYS_PING 0xE2
/** @brief List detected hardware devices into a DeviceInfo array. */
#define SYS_DEVICE_LIST 0xE3
/** @} */

/** @name Debug / Console Syscalls (0xF0 - 0xFF)
 *  @details
 *  Early bring-up and console primitives. These interfaces are intentionally
 *  simple and may evolve as the kernel grows.
 *  @{
 */
/** @brief Print a NUL-terminated debug string to kernel output. */
#define SYS_DEBUG_PRINT 0xF0
/** @brief Read a character from the console (may return `VERR_WOULD_BLOCK`). */
#define SYS_GETCHAR 0xF1
/** @brief Write a character to the console. */
#define SYS_PUTCHAR 0xF2
/** @brief Return the kernel uptime tick count. */
#define SYS_UPTIME 0xF3
/** @} */

/** @name Device Management Syscalls (0x100 - 0x10F)
 *  @details
 *  Microkernel device access primitives. These allow user-space drivers to
 *  map device MMIO, register for IRQs, and allocate DMA buffers.
 *  Requires CAP_DEVICE_ACCESS capability.
 *  @{
 */
/** @brief Map device MMIO region into user address space. */
#define SYS_MAP_DEVICE 0x100
/** @brief Register to receive a specific IRQ. */
#define SYS_IRQ_REGISTER 0x101
/** @brief Wait for a registered IRQ to fire. */
#define SYS_IRQ_WAIT 0x102
/** @brief Acknowledge an IRQ after handling. */
#define SYS_IRQ_ACK 0x103
/** @brief Allocate physically contiguous DMA buffer. */
#define SYS_DMA_ALLOC 0x104
/** @brief Free a DMA buffer. */
#define SYS_DMA_FREE 0x105
/** @brief Translate virtual address to physical (for DMA programming). */
#define SYS_VIRT_TO_PHYS 0x106
/** @brief Enumerate available devices. */
#define SYS_DEVICE_ENUM 0x107
/** @brief Unregister from an IRQ. */
#define SYS_IRQ_UNREGISTER 0x108
/** @brief Create a shared memory object. */
#define SYS_SHM_CREATE 0x109
/** @brief Map a shared memory object into address space. */
#define SYS_SHM_MAP 0x10A
/** @brief Unmap a shared memory object. */
#define SYS_SHM_UNMAP 0x10B
/** @brief Close/release a shared memory handle. */
#define SYS_SHM_CLOSE 0x10C
/** @} */

/** @name GUI/Display Syscalls (0x110 - 0x11F)
 *  @details
 *  GUI-related primitives for display servers and input handling.
 *  @{
 */
/** @brief Get current mouse state (position, buttons, deltas). */
#define SYS_GET_MOUSE_STATE 0x110
/** @brief Map framebuffer into user address space (returns addr, width, height, stride). */
#define SYS_MAP_FRAMEBUFFER 0x111
/** @brief Set mouse cursor bounds (width, height). */
#define SYS_SET_MOUSE_BOUNDS 0x112
/** @brief Check if input events are available (returns 1 if event available, 0 otherwise). */
#define SYS_INPUT_HAS_EVENT 0x113
/** @brief Get next input event from kernel queue (returns event type, code, value, modifiers). */
#define SYS_INPUT_GET_EVENT 0x114
/** @brief Enable/disable GUI mode (when enabled, gcon writes to serial only). */
#define SYS_GCON_SET_GUI_MODE 0x115
/** @} */

/** @name TTY Syscalls (0x120 - 0x12F)
 *  @details
 *  Kernel TTY buffer for text-mode console input. consoled pushes keyboard
 *  characters to the kernel buffer, clients read via blocking syscalls.
 *  @{
 */
/** @brief Read characters from TTY input buffer (blocks until data available). */
#define SYS_TTY_READ 0x120
/** @brief Write characters to TTY output (currently forwards to serial). */
#define SYS_TTY_WRITE 0x121
/** @brief Push a character into TTY input buffer (used by consoled). */
#define SYS_TTY_PUSH_INPUT 0x122
/** @brief Check if TTY has input available (non-blocking). */
#define SYS_TTY_HAS_INPUT 0x123
/** @} */
