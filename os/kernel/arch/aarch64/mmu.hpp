#pragma once

#include "../../include/types.hpp"

/**
 * @file mmu.hpp
 * @brief AArch64 MMU configuration for kernel/user address spaces.
 *
 * @details
 * This module configures the AArch64 Memory Management Unit (MMU) and creates
 * the kernel's base translation tables. The kernel currently relies on an
 * identity-mapped layout during bring-up, but the implementation is structured
 * to support a user/kernel split by maintaining a kernel table root that can
 * be shared into user address spaces.
 *
 * The exported functions provide:
 * - One-time MMU enablement and table creation.
 * - A boolean indicating whether user-space support is enabled.
 * - Access to the kernel TTBR0 root so user address spaces can include kernel
 *   mappings where appropriate.
 */
namespace mmu
{

/**
 * @brief Configure and enable the MMU.
 *
 * @details
 * Creates a kernel identity-mapped set of translation tables, programs MAIR/TCR,
 * installs the table root into TTBR0, invalidates TLBs, and finally enables the
 * MMU and caches via SCTLR_EL1.
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

} // namespace mmu
