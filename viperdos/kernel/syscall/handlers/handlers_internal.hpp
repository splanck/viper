//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/syscall/handlers/handlers_internal.hpp
// Purpose: Internal declarations for syscall handler functions.
// Key invariants: All handlers follow SyscallHandler signature.
// Ownership/Lifetime: Static functions; linked at compile time.
// Links: kernel/syscall/table.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../../cap/table.hpp"
#include "../table.hpp"

namespace syscall {

// =============================================================================
// Common Utilities
// =============================================================================

/// Get capability table for current task
cap::Table *get_current_cap_table();

// =============================================================================
// Handle Lookup Helpers (reduce boilerplate in handlers)
// =============================================================================

/**
 * @brief Get capability table or return VERR_NOT_FOUND.
 *
 * @details
 * Use at the start of syscall handlers that need the capability table.
 * Declares a local variable `table` of type `cap::Table*`.
 */
#define GET_CAP_TABLE_OR_RETURN()                                                                  \
    cap::Table *table = get_current_cap_table();                                                   \
    if (!table) {                                                                                  \
        return err_not_found();                                                                    \
    }

/**
 * @brief Look up a handle with rights check and cast to typed object.
 *
 * @details
 * Validates the handle, checks kind and rights, then casts to the target type.
 * Declares a local variable with the given name of type T*.
 * Returns VERR_INVALID_HANDLE on failure.
 *
 * @param var_name Name for the local variable to declare.
 * @param T Target object type (e.g., kobj::Channel).
 * @param table Capability table pointer.
 * @param handle Handle to look up.
 * @param kind Expected object kind (e.g., cap::Kind::Channel).
 * @param rights Required rights mask.
 */
#define GET_OBJECT_WITH_RIGHTS(var_name, T, table, handle, kind, rights)                           \
    cap::Entry *var_name##_entry_ = (table)->get_with_rights((handle), (kind), (rights));          \
    if (!var_name##_entry_) {                                                                      \
        return err_invalid_handle();                                                               \
    }                                                                                              \
    T *var_name = static_cast<T *>(var_name##_entry_->object)

/**
 * @brief Look up a handle with kind check only and cast to typed object.
 *
 * @details
 * Validates the handle and checks kind, then casts to the target type.
 * Declares a local variable with the given name of type T*.
 * Returns VERR_INVALID_HANDLE on failure.
 *
 * @param var_name Name for the local variable to declare.
 * @param T Target object type (e.g., kobj::DirObject).
 * @param table Capability table pointer.
 * @param handle Handle to look up.
 * @param kind Expected object kind (e.g., cap::Kind::Directory).
 */
#define GET_OBJECT_CHECKED(var_name, T, table, handle, kind)                                       \
    cap::Entry *var_name##_entry_ = (table)->get_checked((handle), (kind));                        \
    if (!var_name##_entry_) {                                                                      \
        return err_invalid_handle();                                                               \
    }                                                                                              \
    T *var_name = static_cast<T *>(var_name##_entry_->object)

// =============================================================================
// Task Management (0x00-0x0F)
// =============================================================================

SyscallResult sys_task_yield(u64, u64, u64, u64, u64, u64);
SyscallResult sys_task_exit(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_task_current(u64, u64, u64, u64, u64, u64);
SyscallResult sys_task_spawn(u64 a0, u64 a1, u64 a2, u64, u64, u64);
SyscallResult sys_task_spawn_shm(u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64);
SyscallResult sys_replace(u64 a0, u64 a1, u64 a2, u64, u64, u64);
SyscallResult sys_task_list(u64 a0, u64 a1, u64, u64, u64, u64);
SyscallResult sys_task_set_priority(u64 a0, u64 a1, u64, u64, u64, u64);
SyscallResult sys_task_get_priority(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_sched_setaffinity(u64 a0, u64 a1, u64, u64, u64, u64);
SyscallResult sys_sched_getaffinity(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_wait(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_waitpid(u64 a0, u64 a1, u64, u64, u64, u64);
SyscallResult sys_fork(u64, u64, u64, u64, u64, u64);
SyscallResult sys_sbrk(u64 a0, u64, u64, u64, u64, u64);

// =============================================================================
// Channel IPC (0x10-0x1F)
// =============================================================================

SyscallResult sys_channel_create(u64, u64, u64, u64, u64, u64);
SyscallResult sys_channel_send(u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64);
SyscallResult sys_channel_recv(u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64);
SyscallResult sys_channel_close(u64 a0, u64, u64, u64, u64, u64);

// =============================================================================
// Poll (0x20-0x2F)
// =============================================================================

SyscallResult sys_poll_create(u64, u64, u64, u64, u64, u64);
SyscallResult sys_poll_add(u64 a0, u64 a1, u64 a2, u64, u64, u64);
SyscallResult sys_poll_remove(u64 a0, u64 a1, u64, u64, u64, u64);
SyscallResult sys_poll_wait(u64 a0, u64 a1, u64 a2, u64 a3, u64, u64);

// =============================================================================
// Time (0x30-0x3F)
// =============================================================================

SyscallResult sys_time_now(u64, u64, u64, u64, u64, u64);
SyscallResult sys_sleep(u64 a0, u64, u64, u64, u64, u64);

// =============================================================================
// File I/O (0x40-0x4F)
// =============================================================================

SyscallResult sys_open(u64 a0, u64 a1, u64, u64, u64, u64);
SyscallResult sys_close(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_read(u64 a0, u64 a1, u64 a2, u64, u64, u64);
SyscallResult sys_write(u64 a0, u64 a1, u64 a2, u64, u64, u64);
SyscallResult sys_lseek(u64 a0, u64 a1, u64 a2, u64, u64, u64);
SyscallResult sys_stat(u64 a0, u64 a1, u64, u64, u64, u64);
SyscallResult sys_fstat(u64 a0, u64 a1, u64, u64, u64, u64);
SyscallResult sys_dup(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_dup2(u64 a0, u64 a1, u64, u64, u64, u64);
SyscallResult sys_fsync(u64 a0, u64, u64, u64, u64, u64);

// =============================================================================
// Networking (0x50-0x5F)
// =============================================================================

SyscallResult sys_socket_create(u64, u64, u64, u64, u64, u64);
SyscallResult sys_socket_connect(u64 a0, u64 a1, u64 a2, u64, u64, u64);
SyscallResult sys_socket_send(u64 a0, u64 a1, u64 a2, u64, u64, u64);
SyscallResult sys_socket_recv(u64 a0, u64 a1, u64 a2, u64, u64, u64);
SyscallResult sys_socket_close(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_dns_resolve(u64 a0, u64 a1, u64, u64, u64, u64);
SyscallResult sys_socket_poll(u64 a0, u64 a1, u64 a2, u64, u64, u64);

// =============================================================================
// Directory/FS (0x60-0x6F)
// =============================================================================

SyscallResult sys_readdir(u64 a0, u64 a1, u64 a2, u64, u64, u64);
SyscallResult sys_mkdir(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_rmdir(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_unlink(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_rename(u64 a0, u64 a1, u64, u64, u64, u64);
SyscallResult sys_symlink(u64 a0, u64 a1, u64, u64, u64, u64);
SyscallResult sys_readlink(u64 a0, u64 a1, u64 a2, u64, u64, u64);
SyscallResult sys_getcwd(u64 a0, u64 a1, u64, u64, u64, u64);
SyscallResult sys_chdir(u64 a0, u64, u64, u64, u64, u64);

// =============================================================================
// Capability (0x70-0x7F)
// =============================================================================

SyscallResult sys_cap_derive(u64 a0, u64 a1, u64, u64, u64, u64);
SyscallResult sys_cap_revoke(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_cap_query(u64 a0, u64 a1, u64, u64, u64, u64);
SyscallResult sys_cap_list(u64 a0, u64 a1, u64, u64, u64, u64);
SyscallResult sys_cap_get_bound(u64, u64, u64, u64, u64, u64);
SyscallResult sys_cap_drop_bound(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_getrlimit(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_setrlimit(u64 a0, u64 a1, u64, u64, u64, u64);
SyscallResult sys_getrusage(u64 a0, u64, u64, u64, u64, u64);

// =============================================================================
// Handle-based FS (0x80-0x8F)
// =============================================================================

SyscallResult sys_fs_open_root(u64, u64, u64, u64, u64, u64);
SyscallResult sys_fs_open(u64 a0, u64 a1, u64 a2, u64 a3, u64, u64);
SyscallResult sys_io_read(u64 a0, u64 a1, u64 a2, u64, u64, u64);
SyscallResult sys_io_write(u64 a0, u64 a1, u64 a2, u64, u64, u64);
SyscallResult sys_io_seek(u64 a0, u64 a1, u64 a2, u64, u64, u64);
SyscallResult sys_fs_read_dir(u64 a0, u64 a1, u64, u64, u64, u64);
SyscallResult sys_fs_close(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_fs_rewind_dir(u64 a0, u64, u64, u64, u64, u64);

// =============================================================================
// Signal (0x90-0x9F)
// =============================================================================

SyscallResult sys_sigaction(u64 a0, u64 a1, u64 a2, u64, u64, u64);
SyscallResult sys_sigprocmask(u64 a0, u64 a1, u64 a2, u64, u64, u64);
SyscallResult sys_sigreturn(u64, u64, u64, u64, u64, u64);
SyscallResult sys_kill(u64 a0, u64 a1, u64, u64, u64, u64);
SyscallResult sys_sigpending(u64 a0, u64, u64, u64, u64, u64);

// =============================================================================
// Process Groups/Sessions (0xA0-0xAF)
// =============================================================================

SyscallResult sys_getpid(u64, u64, u64, u64, u64, u64);
SyscallResult sys_getppid(u64, u64, u64, u64, u64, u64);
SyscallResult sys_getpgid(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_setpgid(u64 a0, u64 a1, u64, u64, u64, u64);
SyscallResult sys_getsid(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_setsid(u64, u64, u64, u64, u64, u64);
SyscallResult sys_get_args(u64 a0, u64 a1, u64, u64, u64, u64);

// =============================================================================
// Assign (0xC0-0xCF)
// =============================================================================

SyscallResult sys_assign_set(u64 a0, u64 a1, u64, u64, u64, u64);
SyscallResult sys_assign_get(u64 a0, u64 a1, u64 a2, u64, u64, u64);
SyscallResult sys_assign_remove(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_assign_list(u64 a0, u64 a1, u64, u64, u64, u64);
SyscallResult sys_assign_resolve(u64 a0, u64 a1, u64 a2, u64, u64, u64);

// =============================================================================
// TLS (0xD0-0xDF)
// =============================================================================

SyscallResult sys_tls_create(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_tls_handshake(u64 a0, u64 a1, u64, u64, u64, u64);
SyscallResult sys_tls_send(u64 a0, u64 a1, u64 a2, u64, u64, u64);
SyscallResult sys_tls_recv(u64 a0, u64 a1, u64 a2, u64, u64, u64);
SyscallResult sys_tls_close(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_tls_info(u64 a0, u64 a1, u64, u64, u64, u64);

// =============================================================================
// System Info (0xE0-0xEF)
// =============================================================================

SyscallResult sys_mem_info(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_net_stats(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_ping(u64 a0, u64 a1, u64, u64, u64, u64);
SyscallResult sys_device_list(u64 a0, u64 a1, u64, u64, u64, u64);

// =============================================================================
// Debug/Console (0xF0-0xFF)
// =============================================================================

SyscallResult sys_debug_print(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_getchar(u64, u64, u64, u64, u64, u64);
SyscallResult sys_putchar(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_uptime(u64, u64, u64, u64, u64, u64);

// =============================================================================
// Device Management (0x100-0x10F)
// =============================================================================

SyscallResult sys_map_device(u64 a0, u64 a1, u64 a2, u64, u64, u64);
SyscallResult sys_irq_register(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_irq_wait(u64 a0, u64 a1, u64, u64, u64, u64);
SyscallResult sys_irq_ack(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_dma_alloc(u64 a0, u64 a1, u64, u64, u64, u64);
SyscallResult sys_dma_free(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_virt_to_phys(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_device_enum(u64 a0, u64 a1, u64, u64, u64, u64);
SyscallResult sys_irq_unregister(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_shm_create(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_shm_map(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_shm_unmap(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_shm_close(u64 a0, u64, u64, u64, u64, u64);

// =============================================================================
// GUI/Display (0x110-0x11F)
// =============================================================================

SyscallResult sys_get_mouse_state(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_map_framebuffer(u64, u64, u64, u64, u64, u64);
SyscallResult sys_set_mouse_bounds(u64 a0, u64 a1, u64, u64, u64, u64);
SyscallResult sys_input_has_event(u64, u64, u64, u64, u64, u64);
SyscallResult sys_input_get_event(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_gcon_set_gui_mode(u64 a0, u64, u64, u64, u64, u64);

// =============================================================================
// TTY (0x120-0x12F)
// =============================================================================

SyscallResult sys_tty_read(u64 a0, u64 a1, u64, u64, u64, u64);
SyscallResult sys_tty_write(u64 a0, u64 a1, u64, u64, u64, u64);
SyscallResult sys_tty_push_input(u64 a0, u64, u64, u64, u64, u64);
SyscallResult sys_tty_has_input(u64, u64, u64, u64, u64, u64);

} // namespace syscall
