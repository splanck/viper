//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/mm/swap.hpp
// Purpose: Swap space management for page-out/page-in operations.
// Key invariants: Swap slots are uniquely assigned; bitmap tracks usage.
// Ownership/Lifetime: Global singleton; initialized once.
// Links: kernel/mm/swap.cpp, kernel/mm/fault.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../include/types.hpp"

/**
 * @file swap.hpp
 * @brief Swap space management for page-out/page-in.
 *
 * @details
 * The swap subsystem manages a region of block storage used to temporarily
 * store pages that have been evicted from RAM. Each 4KB page maps to one
 * swap slot. A bitmap tracks which slots are in use.
 *
 * Swap entries are encoded as special values that can be stored in page table
 * entries when a page is not present. The encoding includes:
 * - A flag indicating the entry is a swap entry (not a normal PTE)
 * - The swap slot index
 *
 * Swap entry format (64-bit):
 *   Bit 0: Present bit (always 0 for swap entries)
 *   Bit 1: Swap entry flag (1 = this is a swap entry)
 *   Bits 12-63: Swap slot index
 */
namespace mm::swap {

/**
 * @brief Maximum number of swap slots (pages).
 *
 * @details
 * With 4KB pages, 16384 slots = 64MB of swap space.
 * This can be adjusted based on available disk space.
 */
constexpr usize MAX_SWAP_SLOTS = 16384;

/**
 * @brief Swap entry encoding constants.
 */
namespace entry {
constexpr u64 PRESENT_BIT = (1ULL << 0);   // Present bit (always 0 for swap)
constexpr u64 SWAP_FLAG = (1ULL << 1);     // Indicates this is a swap entry
constexpr u64 SLOT_SHIFT = 12;             // Slot index starts at bit 12
constexpr u64 SLOT_MASK = 0xFFFFFFFFFFFFF000ULL; // Mask for slot index
} // namespace entry

/**
 * @brief Check if a PTE value is a swap entry.
 *
 * @param pte Page table entry value.
 * @return true if this is a swap entry (not present, swap flag set).
 */
inline bool is_swap_entry(u64 pte) {
    return (pte & entry::PRESENT_BIT) == 0 && (pte & entry::SWAP_FLAG) != 0;
}

/**
 * @brief Create a swap entry from a slot index.
 *
 * @param slot_index The swap slot index.
 * @return A PTE-encodable swap entry value.
 */
inline u64 make_swap_entry(u64 slot_index) {
    return entry::SWAP_FLAG | (slot_index << entry::SLOT_SHIFT);
}

/**
 * @brief Extract the slot index from a swap entry.
 *
 * @param swap_entry The swap entry value.
 * @return The swap slot index.
 */
inline u64 get_swap_slot(u64 swap_entry) {
    return (swap_entry & entry::SLOT_MASK) >> entry::SLOT_SHIFT;
}

/**
 * @brief Initialize the swap subsystem.
 *
 * @details
 * Sets up the swap slot bitmap and prepares for swap I/O.
 * Uses a portion of the user disk for swap space.
 *
 * @return true on success, false if no swap device available.
 */
bool init();

/**
 * @brief Check if swap is available.
 *
 * @return true if swap has been initialized and is usable.
 */
bool is_available();

/**
 * @brief Get the number of free swap slots.
 *
 * @return Number of available swap slots.
 */
usize free_slots();

/**
 * @brief Get the total number of swap slots.
 *
 * @return Total swap slot capacity.
 */
usize total_slots();

/**
 * @brief Swap out a page to disk.
 *
 * @details
 * Allocates a swap slot, writes the page content to disk, and returns
 * the swap entry to be stored in the page table. The caller is responsible
 * for freeing the physical page after the swap-out completes.
 *
 * @param phys_addr Physical address of the page to swap out.
 * @return Swap entry value on success, 0 on failure.
 */
u64 swap_out(u64 phys_addr);

/**
 * @brief Swap in a page from disk.
 *
 * @details
 * Reads a page from the swap slot into a newly allocated physical page.
 * Frees the swap slot after successful read.
 *
 * @param swap_entry The swap entry from the page table.
 * @param dest_phys Physical address of destination page (must be pre-allocated).
 * @return true on success, false on I/O error.
 */
bool swap_in(u64 swap_entry, u64 dest_phys);

/**
 * @brief Free a swap slot without reading it.
 *
 * @details
 * Used when a process exits or unmaps a swapped page without needing
 * to read it back. Frees the swap slot for reuse.
 *
 * @param swap_entry The swap entry to free.
 */
void free_slot(u64 swap_entry);

/**
 * @brief Get swap statistics.
 */
struct SwapStats {
    usize total_slots;
    usize used_slots;
    usize swap_outs;      // Total pages swapped out
    usize swap_ins;       // Total pages swapped in
    usize io_errors;      // I/O errors encountered
};

/**
 * @brief Get current swap statistics.
 *
 * @return SwapStats structure with current values.
 */
SwapStats get_stats();

} // namespace mm::swap
