//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: include/viperdos/mem_info.hpp
// Purpose: Memory accounting information for SYS_MEM_INFO syscall.
// Key invariants: ABI-stable; snapshot of global allocator state.
// Ownership/Lifetime: Shared; included by kernel and user-space.
// Links: kernel/mem/pmm.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

/**
 * @file mem_info.hpp
 * @brief Shared physical memory accounting information returned by `SYS_MEM_INFO`.
 *
 * @details
 * This header is part of the ViperDOS user/kernel ABI: both the kernel and
 * user-space include the same definition of @ref MemInfo so the kernel can
 * fill it in and user-space can interpret the results reliably.
 *
 * The structure intentionally uses only plain integer types and a fixed layout
 * suitable for freestanding code. The fields provide coarse global allocator
 * statistics (total/free/used) expressed both in pages and bytes.
 *
 * This is a snapshot taken at the moment the syscall executes. It is meant for
 * diagnostics and human-facing reporting (e.g., a shell `Avail` command), not
 * for high-frequency profiling or precise accounting of every subsystem.
 */

/**
 * @brief Snapshot of global physical memory usage.
 *
 * @details
 * The kernel fills this structure in response to the `SYS_MEM_INFO` syscall.
 * Values are derived from the physical memory manager's view of page frames.
 *
 * Notes and expectations:
 * - `total_pages` counts all page frames the kernel considers managed RAM.
 * - `free_pages` and `used_pages` refer to the allocator's free list / in-use
 *   accounting and may not include firmware/boot-reserved ranges that were
 *   never added to the allocator.
 * - `page_size` is the base page granule in bytes (typically 4096 on AArch64).
 * - Byte counts are redundant convenience values computed from page counts and
 *   `page_size`.
 *
 * The `_reserved` field exists so the ABI can grow in the future without
 * immediately changing the structure size observed by existing binaries.
 */
struct MemInfo {
    unsigned long long total_pages; /**< Total number of managed physical pages. */
    unsigned long long free_pages;  /**< Pages currently available for allocation. */
    unsigned long long used_pages;  /**< Pages currently allocated/in-use. */
    unsigned long long page_size;   /**< Base page size in bytes (commonly 4096). */
    unsigned long long total_bytes; /**< `total_pages * page_size`. */
    unsigned long long free_bytes;  /**< `free_pages * page_size`. */
    unsigned long long used_bytes;  /**< `used_pages * page_size`. */
    unsigned char _reserved[8];     /**< Reserved for future ABI extension; set to 0. */
};

// ABI size guard — this struct crosses the kernel/user syscall boundary
static_assert(sizeof(MemInfo) == 64, "MemInfo ABI size mismatch");
