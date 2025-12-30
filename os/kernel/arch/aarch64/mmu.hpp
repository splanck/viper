#pragma once

#include "../../include/types.hpp"

/**
 * @file mmu.hpp
 * @brief AArch64 MMU configuration for kernel/user address spaces.
 *
 * @details
 * This module configures the AArch64 Memory Management Unit (MMU) and creates
 * translation tables for both TTBR0 (user space) and TTBR1 (kernel space).
 *
 * ## Address Space Layout
 *
 * AArch64 provides two translation table base registers:
 * - **TTBR0_EL1**: Lower half (0x0000_0000_0000_0000 to 0x0000_FFFF_FFFF_FFFF)
 *   Used for user-space mappings. Each process has its own TTBR0.
 *
 * - **TTBR1_EL1**: Upper half (0xFFFF_0000_0000_0000 to 0xFFFF_FFFF_FFFF_FFFF)
 *   Used for kernel mappings. Shared across all processes.
 *
 * The kernel virtual base is KERNEL_VIRT_BASE (0xFFFF_0000_0000_0000).
 * Physical memory starting at 0x0 is mapped to this virtual address.
 *
 * ## Memory Layout (QEMU virt)
 *
 * Physical:
 *   0x00000000-0x3FFFFFFF: Device MMIO (GIC, UART, etc.)
 *   0x40000000-0x7FFFFFFF: RAM
 *
 * Virtual (TTBR1 / kernel):
 *   0xFFFF_0000_0000_0000 - 0xFFFF_0000_3FFF_FFFF: Device MMIO
 *   0xFFFF_0000_4000_0000 - 0xFFFF_0000_7FFF_FFFF: RAM
 */
namespace mmu
{

/// Kernel virtual base address (upper half)
constexpr u64 KERNEL_VIRT_BASE = 0xFFFF000000000000ULL;

/// Physical memory base (where RAM starts on QEMU virt)
constexpr u64 PHYS_MEM_BASE = 0x40000000ULL;

/// Convert physical address to kernel virtual address
inline u64 phys_to_virt(u64 phys)
{
    return phys + KERNEL_VIRT_BASE;
}

/// Convert kernel virtual address to physical address
inline u64 virt_to_phys(u64 virt)
{
    return virt - KERNEL_VIRT_BASE;
}

/// Check if an address is in the kernel virtual range
inline bool is_kernel_addr(u64 addr)
{
    return (addr >> 48) == 0xFFFF;
}

/**
 * @brief Configure and enable the MMU.
 *
 * @details
 * Creates translation tables for both TTBR0 (identity-mapped for boot) and
 * TTBR1 (kernel higher-half), programs MAIR/TCR, installs table roots,
 * invalidates TLBs, and enables the MMU and caches via SCTLR_EL1.
 *
 * This routine is expected to run at EL1 during early boot, before the kernel
 * begins running user-mode tasks.
 */
void init();

/**
 * @brief Determine whether the MMU has been initialized for user-space support.
 *
 * @return `true` after successful initialization, otherwise `false`.
 */
bool is_user_space_enabled();

/**
 * @brief Check if TTBR1 (kernel higher-half) is enabled.
 *
 * @return `true` if TTBR1 translations are active.
 */
bool is_ttbr1_enabled();

/**
 * @brief Get the kernel TTBR0 translation table root physical address.
 *
 * @details
 * User address spaces may need to incorporate kernel mappings (e.g. for
 * syscall/trap handling or shared kernel regions). This function exposes the
 * kernel root so higher-level address space code can copy or reference those
 * mappings.
 *
 * @return Physical address of the kernel root translation table.
 */
u64 get_kernel_ttbr0();

/**
 * @brief Get the kernel TTBR1 translation table root physical address.
 *
 * @return Physical address of the kernel higher-half translation table.
 */
u64 get_kernel_ttbr1();

} // namespace mmu
