/**
 * @file device.hpp
 * @brief User-space device access syscall wrappers.
 *
 * @details
 * Provides wrappers for the device management syscalls (0x100-0x10F) that
 * enable user-space drivers to:
 * - Map device MMIO regions into their address space
 * - Register for and wait on hardware interrupts
 * - Allocate and free DMA-capable memory
 * - Translate virtual addresses to physical for DMA programming
 * - Enumerate available devices
 *
 * These syscalls require CAP_DEVICE_ACCESS, CAP_IRQ_ACCESS, and/or
 * CAP_DMA_ACCESS capabilities.
 */
#pragma once

#include "../../syscall.hpp"

namespace device {

/**
 * @brief Device information returned by sys_device_enum.
 */
struct DeviceInfo {
    char name[32]; ///< Device name string
    u64 phys_addr; ///< MMIO base physical address
    u64 size;      ///< MMIO region size
    u32 irq;       ///< IRQ number (0 if none)
    u32 flags;     ///< Kernel-provided flags
};

/**
 * @brief DMA buffer allocation result.
 */
struct DmaBuffer {
    u64 virt_addr; ///< Virtual address (user-accessible)
    u64 phys_addr; ///< Physical address (for DMA programming)
    u64 size;      ///< Allocated size in bytes
};

/**
 * @brief Map a device MMIO region into user address space.
 *
 * @param phys_addr Physical base address of the device MMIO region.
 * @param size Size of the region to map.
 * @return Virtual address of the mapped region, or 0 on error.
 *
 * @note Requires CAP_DEVICE_ACCESS capability.
 */
inline u64 map_device(u64 phys_addr, u64 size) {
    auto result = sys::syscall3(SYS_MAP_DEVICE, phys_addr, size, 0);
    if (result.error != 0) {
        return 0;
    }
    return result.val0;
}

/**
 * @brief Register to receive a specific IRQ.
 *
 * @param irq IRQ number to register for.
 * @return 0 on success, negative error code on failure.
 *
 * @note Requires CAP_IRQ_ACCESS capability.
 * @note Only one task can register for each IRQ.
 */
inline i64 irq_register(u32 irq) {
    auto result = sys::syscall1(SYS_IRQ_REGISTER, irq);
    return result.error;
}

/**
 * @brief Wait for a registered IRQ to fire.
 *
 * @param irq IRQ number to wait for.
 * @param timeout_ms Timeout in milliseconds (0 = wait forever).
 * @return 0 on success (IRQ received), negative error code on failure/timeout.
 *
 * @note Must have previously registered for this IRQ.
 */
inline i64 irq_wait(u32 irq, u64 timeout_ms) {
    auto result = sys::syscall2(SYS_IRQ_WAIT, irq, timeout_ms);
    return result.error;
}

/**
 * @brief Acknowledge an IRQ after handling.
 *
 * @param irq IRQ number to acknowledge.
 * @return 0 on success, negative error code on failure.
 *
 * @note Must be called after handling an IRQ to re-enable it.
 */
inline i64 irq_ack(u32 irq) {
    auto result = sys::syscall1(SYS_IRQ_ACK, irq);
    return result.error;
}

/**
 * @brief Unregister from an IRQ.
 *
 * @param irq IRQ number to unregister.
 * @return 0 on success, negative error code on failure.
 */
inline i64 irq_unregister(u32 irq) {
    auto result = sys::syscall1(SYS_IRQ_UNREGISTER, irq);
    return result.error;
}

/**
 * @brief Allocate a DMA-capable buffer.
 *
 * @param size Size in bytes to allocate.
 * @param buf Output: filled with allocation details.
 * @return 0 on success, negative error code on failure.
 *
 * @note Requires CAP_DMA_ACCESS capability.
 * @note Allocated memory is physically contiguous.
 */
inline i64 dma_alloc(u64 size, DmaBuffer *buf) {
    u64 phys_addr = 0;
    auto result = sys::syscall2(SYS_DMA_ALLOC, size, reinterpret_cast<u64>(&phys_addr));
    if (result.error != 0) {
        return result.error;
    }
    buf->virt_addr = result.val0;
    buf->phys_addr = phys_addr;
    buf->size = size;
    return 0;
}

/**
 * @brief Free a DMA buffer.
 *
 * @param virt_addr Virtual address of the buffer to free.
 * @return 0 on success, negative error code on failure.
 */
inline i64 dma_free(u64 virt_addr) {
    auto result = sys::syscall1(SYS_DMA_FREE, virt_addr);
    return result.error;
}

/**
 * @brief Translate a virtual address to physical.
 *
 * @param virt_addr Virtual address to translate.
 * @return Physical address, or 0 on failure.
 *
 * @note Requires CAP_DMA_ACCESS capability.
 */
inline u64 virt_to_phys(u64 virt_addr) {
    auto result = sys::syscall1(SYS_VIRT_TO_PHYS, virt_addr);
    if (result.error != 0) {
        return 0;
    }
    return result.val0;
}

/**
 * @brief Enumerate available devices.
 *
 * @param buf Buffer to receive device info entries.
 * @param max_entries Maximum number of entries the buffer can hold.
 * @return Number of devices enumerated, or negative error code.
 */
inline i64 enumerate(DeviceInfo *buf, usize max_entries) {
    auto result = sys::syscall2(SYS_DEVICE_ENUM, reinterpret_cast<u64>(buf), max_entries);
    if (result.error != 0) {
        return result.error;
    }
    return static_cast<i64>(result.val0);
}

} // namespace device
