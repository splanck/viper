//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/mm/swap.cpp
// Purpose: Swap space management implementation.
// Key invariants: Bitmap protects slot allocation; I/O is synchronous.
// Ownership/Lifetime: Global singleton; initialized once.
// Links: kernel/mm/swap.hpp
//
//===----------------------------------------------------------------------===//

#include "swap.hpp"
#include "../console/serial.hpp"
#include "../drivers/virtio/blk.hpp"
#include "../lib/spinlock.hpp"
#include "pmm.hpp"

namespace mm::swap {

// =============================================================================
// Configuration
// =============================================================================

// Use sectors at the end of the user disk for swap
// User disk is 8MB = 16384 sectors (512 bytes each)
// Reserve the last 4MB (8192 sectors) for swap = 1024 pages
static constexpr u64 SWAP_SECTOR_START = 8192;                // Start at 4MB mark
static constexpr u64 SECTORS_PER_PAGE = pmm::PAGE_SIZE / 512; // 8 sectors per 4KB page
static constexpr usize ACTUAL_SWAP_SLOTS = 1024;              // 4MB of swap = 1024 pages

// =============================================================================
// State
// =============================================================================

static Spinlock swap_lock;
static bool swap_initialized = false;
static virtio::BlkDevice *swap_device = nullptr;

// Bitmap for swap slot allocation (1 = used, 0 = free)
static constexpr usize BITMAP_SIZE = (ACTUAL_SWAP_SLOTS + 63) / 64;
static u64 slot_bitmap[BITMAP_SIZE];
static usize used_slots = 0;

// Statistics
static usize stat_swap_outs = 0;
static usize stat_swap_ins = 0;
static usize stat_io_errors = 0;

// Next-free hint for faster allocation
static usize next_free_hint = 0;

// =============================================================================
// Implementation
// =============================================================================

bool init() {
    if (swap_initialized) {
        return true;
    }

    // Get the user disk device for swap
    swap_device = virtio::user_blk_device();
    if (!swap_device) {
        serial::puts("[swap] No user disk available for swap\n");
        return false;
    }

    // Verify the disk has enough space
    u64 disk_sectors = swap_device->capacity();
    if (disk_sectors < SWAP_SECTOR_START + (ACTUAL_SWAP_SLOTS * SECTORS_PER_PAGE)) {
        serial::puts("[swap] User disk too small for swap space\n");
        return false;
    }

    // Initialize bitmap (all slots free)
    for (usize i = 0; i < BITMAP_SIZE; i++) {
        slot_bitmap[i] = 0;
    }

    used_slots = 0;
    next_free_hint = 0;

    swap_initialized = true;

    serial::puts("[swap] Initialized: ");
    serial::put_dec(ACTUAL_SWAP_SLOTS);
    serial::puts(" slots (");
    serial::put_dec((ACTUAL_SWAP_SLOTS * pmm::PAGE_SIZE) / (1024 * 1024));
    serial::puts(" MB) starting at sector ");
    serial::put_dec(SWAP_SECTOR_START);
    serial::puts("\n");

    return true;
}

bool is_available() {
    return swap_initialized && swap_device != nullptr;
}

usize free_slots() {
    u64 saved = swap_lock.acquire();
    usize free = ACTUAL_SWAP_SLOTS - used_slots;
    swap_lock.release(saved);
    return free;
}

usize total_slots() {
    return ACTUAL_SWAP_SLOTS;
}

// Allocate a swap slot (called with lock held)
static i64 alloc_slot() {
    if (used_slots >= ACTUAL_SWAP_SLOTS) {
        return -1; // No free slots
    }

    // Start searching from the hint
    for (usize i = 0; i < ACTUAL_SWAP_SLOTS; i++) {
        usize slot = (next_free_hint + i) % ACTUAL_SWAP_SLOTS;
        usize word = slot / 64;
        usize bit = slot % 64;

        if ((slot_bitmap[word] & (1ULL << bit)) == 0) {
            // Found a free slot
            slot_bitmap[word] |= (1ULL << bit);
            used_slots++;
            next_free_hint = (slot + 1) % ACTUAL_SWAP_SLOTS;
            return static_cast<i64>(slot);
        }
    }

    return -1; // Should not reach here if used_slots < ACTUAL_SWAP_SLOTS
}

// Free a swap slot (called with lock held)
static void free_slot_internal(usize slot) {
    if (slot >= ACTUAL_SWAP_SLOTS) {
        return;
    }

    usize word = slot / 64;
    usize bit = slot % 64;

    if (slot_bitmap[word] & (1ULL << bit)) {
        slot_bitmap[word] &= ~(1ULL << bit);
        used_slots--;

        // Update hint to point to freed slot for faster reallocation
        if (slot < next_free_hint) {
            next_free_hint = slot;
        }
    }
}

u64 swap_out(u64 phys_addr) {
    if (!is_available()) {
        return 0;
    }

    u64 saved = swap_lock.acquire();

    // Allocate a swap slot
    i64 slot = alloc_slot();
    if (slot < 0) {
        swap_lock.release(saved);
        serial::puts("[swap] Out of swap space\n");
        return 0;
    }

    // Calculate sector offset for this slot
    u64 sector = SWAP_SECTOR_START + (static_cast<u64>(slot) * SECTORS_PER_PAGE);

    // Write the page to disk
    void *page_ptr = pmm::phys_to_virt(phys_addr);
    i32 result = swap_device->write_sectors(sector, SECTORS_PER_PAGE, page_ptr);

    if (result < 0) {
        // Write failed, free the slot
        free_slot_internal(static_cast<usize>(slot));
        stat_io_errors++;
        swap_lock.release(saved);
        serial::puts("[swap] Write error for slot ");
        serial::put_dec(slot);
        serial::puts("\n");
        return 0;
    }

    stat_swap_outs++;
    swap_lock.release(saved);

    // Return the swap entry
    return make_swap_entry(static_cast<u64>(slot));
}

bool swap_in(u64 swap_entry, u64 dest_phys) {
    if (!is_available()) {
        return false;
    }

    if (!is_swap_entry(swap_entry)) {
        return false;
    }

    u64 slot = get_swap_slot(swap_entry);
    if (slot >= ACTUAL_SWAP_SLOTS) {
        return false;
    }

    u64 saved = swap_lock.acquire();

    // Verify the slot is actually in use
    usize word = slot / 64;
    usize bit = slot % 64;
    if ((slot_bitmap[word] & (1ULL << bit)) == 0) {
        swap_lock.release(saved);
        serial::puts("[swap] Swap-in of free slot ");
        serial::put_dec(slot);
        serial::puts("\n");
        return false;
    }

    // Calculate sector offset for this slot
    u64 sector = SWAP_SECTOR_START + (slot * SECTORS_PER_PAGE);

    // Read the page from disk
    void *page_ptr = pmm::phys_to_virt(dest_phys);
    i32 result = swap_device->read_sectors(sector, SECTORS_PER_PAGE, page_ptr);

    if (result < 0) {
        stat_io_errors++;
        swap_lock.release(saved);
        serial::puts("[swap] Read error for slot ");
        serial::put_dec(slot);
        serial::puts("\n");
        return false;
    }

    // Free the swap slot now that the page is in memory
    free_slot_internal(static_cast<usize>(slot));
    stat_swap_ins++;

    swap_lock.release(saved);
    return true;
}

void free_slot(u64 swap_entry) {
    if (!is_available()) {
        return;
    }

    if (!is_swap_entry(swap_entry)) {
        return;
    }

    u64 slot = get_swap_slot(swap_entry);
    if (slot >= ACTUAL_SWAP_SLOTS) {
        return;
    }

    u64 saved = swap_lock.acquire();
    free_slot_internal(static_cast<usize>(slot));
    swap_lock.release(saved);
}

SwapStats get_stats() {
    u64 saved = swap_lock.acquire();
    SwapStats stats{
        .total_slots = ACTUAL_SWAP_SLOTS,
        .used_slots = used_slots,
        .swap_outs = stat_swap_outs,
        .swap_ins = stat_swap_ins,
        .io_errors = stat_io_errors,
    };
    swap_lock.release(saved);
    return stats;
}

} // namespace mm::swap
