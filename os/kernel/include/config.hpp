#pragma once

/**
 * @file config.hpp
 * @brief Build-time feature toggles for the ViperOS kernel.
 *
 * @details
 * These macros are intended to make microkernel migration safer by allowing
 * incremental, reversible changes. Defaults preserve current behavior.
 *
 * Values are `0` (disabled) or `1` (enabled). They may be overridden via the
 * build system (e.g., `target_compile_definitions`).
 */

// -----------------------------------------------------------------------------
// Build mode
// -----------------------------------------------------------------------------

/// When enabled, the kernel identifies itself as “microkernel mode” at boot.
/// This does not imply that all kernel services have been removed yet.
#ifndef VIPER_MICROKERNEL_MODE
#define VIPER_MICROKERNEL_MODE 0
#endif

// -----------------------------------------------------------------------------
// Kernel service toggles (HYBRID compatibility layer)
// -----------------------------------------------------------------------------

/// Enable the in-kernel network stack and socket/DNS syscalls.
#ifndef VIPER_KERNEL_ENABLE_NET
#define VIPER_KERNEL_ENABLE_NET 1
#endif

/// Enable kernel-managed TLS sessions and `SYS_TLS_*` syscalls.
#ifndef VIPER_KERNEL_ENABLE_TLS
#define VIPER_KERNEL_ENABLE_TLS 1
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
