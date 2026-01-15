//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/include/config.hpp
// Purpose: Build-time feature toggles for the ViperDOS kernel.
// Key invariants: All macros are 0/1 toggles; overridable via build system.
// Ownership/Lifetime: Header-only; preprocessor definitions only.
// Links: docs/os_refactor_plan.md
//
//===----------------------------------------------------------------------===//

#pragma once

/**
 * @file config.hpp
 * @brief Build-time feature toggles for the ViperDOS kernel.
 *
 * @details
 * These macros are intended to make microkernel migration safer by allowing
 * incremental, reversible changes. Defaults preserve current behavior.
 *
 * Values are `0` (disabled) or `1` (enabled). They may be overridden via the
 * build system (e.g., `target_compile_definitions`).
 */

/* -----------------------------------------------------------------------------
 * Build mode
 * -------------------------------------------------------------------------- */

/* When enabled, the kernel identifies itself as "microkernel mode" at boot.
 * In microkernel mode, kernel services (net, tls) are disabled by default. */
#ifndef VIPER_MICROKERNEL_MODE
#define VIPER_MICROKERNEL_MODE 1
#endif

// -----------------------------------------------------------------------------
// Kernel service toggles (HYBRID compatibility layer)
// -----------------------------------------------------------------------------

/// Enable the in-kernel filesystem (VFS + ViperFS).
/// When disabled, file/dir syscalls return VERR_NOT_SUPPORTED.
/// The kernel loader can still load from memory via SYS_TASK_SPAWN_SHM.
#ifndef VIPER_KERNEL_ENABLE_FS
#define VIPER_KERNEL_ENABLE_FS 1
#endif

/// Enable the in-kernel network stack and socket/DNS syscalls.
/// Disabled by default in microkernel mode - use netd instead.
#ifndef VIPER_KERNEL_ENABLE_NET
#define VIPER_KERNEL_ENABLE_NET 0
#endif

/// Enable kernel-managed TLS sessions and `SYS_TLS_*` syscalls.
/// Disabled by default in microkernel mode - use libtls instead.
#ifndef VIPER_KERNEL_ENABLE_TLS
#define VIPER_KERNEL_ENABLE_TLS 0
#endif

/// Enable the in-kernel block device driver (virtio-blk).
/// The kernel always needs this to load vinit from disk.
/// After vinit loads blkd, blkd will take over block device management.
#ifndef VIPER_KERNEL_ENABLE_BLK
#define VIPER_KERNEL_ENABLE_BLK 1
#endif

// -----------------------------------------------------------------------------
// Debug / tracing toggles (keep OFF by default)
// -----------------------------------------------------------------------------

/// Emit verbose logs for socket/DNS syscalls (may spam interactive apps).
#ifndef VIPER_KERNEL_DEBUG_NET_SYSCALL
#define VIPER_KERNEL_DEBUG_NET_SYSCALL 0
#endif

/// Emit verbose logs in the in-kernel TCP implementation (may spam heavily).
#ifndef VIPER_KERNEL_DEBUG_TCP
#define VIPER_KERNEL_DEBUG_TCP 0
#endif

/// Emit verbose virtio-net RX IRQ logs.
#ifndef VIPER_KERNEL_DEBUG_VIRTIO_NET_IRQ
#define VIPER_KERNEL_DEBUG_VIRTIO_NET_IRQ 0
#endif
