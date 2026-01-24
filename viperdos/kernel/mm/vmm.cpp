#include "vmm.hpp"
#include "../console/serial.hpp"
#include "../lib/spinlock.hpp"
#include "pmm.hpp"

// Suppress warnings for address layout constants
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-const-variable"

/**
 * @file vmm.cpp
 * @brief AArch64 page table construction and mapping routines.
 *
 * @details
 * This file implements a minimal AArch64 virtual memory manager sufficient for
 * early kernel bring-up. It allocates translation tables from the PMM and
 * provides routines to map/unmap pages and to translate virtual addresses.
 *
 * Correctness requirements:
 * - Translation tables must be page-aligned and zero-initialized before use.
 * - After modifying a mapping, the relevant TLB entries must be invalidated.
 * - The invalidation must be ordered with DSB/ISB barriers as required by the
 *   architecture to ensure the update is observed.
 */
namespace vmm {

namespace {
// Page table root (TTBR0 for identity mapping)
u64 *pgt_root = nullptr;

// Lock for page table modifications (SMP safety)
static Spinlock vmm_lock;

// Number of entries per table (512 for 4KB pages)
constexpr u64 ENTRIES_PER_TABLE = 512;

// Address bits per level
constexpr u64 VA_BITS = 48;
constexpr u64 PAGE_SHIFT = 12;

// Extract table indices from virtual address
constexpr u64 L0_SHIFT = 39;
constexpr u64 L1_SHIFT = 30;
constexpr u64 L2_SHIFT = 21;
constexpr u64 L3_SHIFT = 12;
constexpr u64 INDEX_MASK = 0x1FF; // 9 bits

inline u64 l0_index(u64 va) {
    return (va >> L0_SHIFT) & INDEX_MASK;
}

inline u64 l1_index(u64 va) {
    return (va >> L1_SHIFT) & INDEX_MASK;
}

inline u64 l2_index(u64 va) {
    return (va >> L2_SHIFT) & INDEX_MASK;
}

inline u64 l3_index(u64 va) {
    return (va >> L3_SHIFT) & INDEX_MASK;
}

// Physical address mask for table entries
constexpr u64 PHYS_MASK = 0x0000FFFFFFFFF000ULL;

/**
 * @brief Tracks newly allocated page tables for rollback on failure.
 *
 * @details
 * When mapping a page, we may need to allocate up to 3 intermediate page
 * tables (L1, L2, L3). If allocation fails partway through, we need to free
 * any tables we already allocated to avoid memory leaks.
 */
struct TableAllocation {
    u64 tables[3]; ///< Physical addresses of allocated tables
    u32 count;     ///< Number of tables allocated

    TableAllocation() : count(0) {
        tables[0] = tables[1] = tables[2] = 0;
    }

    /**
     * @brief Record a newly allocated table.
     */
    void add(u64 table_phys) {
        if (count < 3) {
            tables[count++] = table_phys;
        }
    }

    /**
     * @brief Free all recorded tables (rollback).
     */
    void rollback() {
        for (u32 i = 0; i < count; i++) {
            if (tables[i] != 0) {
                pmm::free_page(tables[i]);
            }
        }
        count = 0;
    }
};

/**
 * @brief Walk page tables to a specific level (read-only, no allocation).
 *
 * @details
 * Traverses the page table hierarchy from L0 down to the specified target level.
 * Returns the table pointer at that level if all intermediate entries are valid,
 * or nullptr if any entry along the path is invalid.
 *
 * @param virt Virtual address to walk.
 * @param target_level Target level: 1=L1, 2=L2, 3=L3.
 * @return Pointer to the table at target_level, or nullptr if path invalid.
 */
u64 *walk_tables_readonly(u64 virt, int target_level) {
    if (!pgt_root || target_level < 1 || target_level > 3)
        return nullptr;

    // L0 to L1
    u64 l0e = pgt_root[l0_index(virt)];
    if (!(l0e & pte::VALID))
        return nullptr;

    u64 *l1 = reinterpret_cast<u64 *>(l0e & PHYS_MASK);
    if (target_level == 1)
        return l1;

    // L1 to L2
    u64 l1e = l1[l1_index(virt)];
    if (!(l1e & pte::VALID))
        return nullptr;

    u64 *l2 = reinterpret_cast<u64 *>(l1e & PHYS_MASK);
    if (target_level == 2)
        return l2;

    // L2 to L3
    u64 l2e = l2[l2_index(virt)];
    if (!(l2e & pte::VALID))
        return nullptr;

    u64 *l3 = reinterpret_cast<u64 *>(l2e & PHYS_MASK);
    return l3;
}

/**
 * @brief Retrieve or allocate the next-level page table with rollback tracking.
 *
 * @details
 * For a given table level, the entry at `index` either references a valid
 * next-level table (VALID+TABLE) or is empty. When empty, this function
 * allocates a new page from the PMM, zeros it, installs the descriptor, and
 * returns the new table pointer. Newly allocated tables are recorded in
 * `allocated` for potential rollback.
 *
 * @param table Current level table.
 * @param index Index into `table`.
 * @param allocated Tracking structure for newly allocated tables.
 * @return Next-level table pointer, or `nullptr` if allocation fails.
 */
u64 *get_or_create_table(u64 *table, u64 index, TableAllocation &allocated) {
    u64 entry = table[index];

    if (entry & pte::VALID) {
        // Table already exists - no allocation needed
        return reinterpret_cast<u64 *>(entry & PHYS_MASK);
    }

    // Allocate new table
    u64 new_table = pmm::alloc_page();
    if (new_table == 0) {
        serial::puts("[vmm] ERROR: Failed to allocate page table!\n");
        return nullptr;
    }

    // Track this allocation for potential rollback
    allocated.add(new_table);

    // Zero the new table
    u64 *ptr = reinterpret_cast<u64 *>(new_table);
    for (u64 i = 0; i < ENTRIES_PER_TABLE; i++) {
        ptr[i] = 0;
    }

    // Install table entry
    table[index] = new_table | pte::VALID | pte::TABLE;

    return ptr;
}
} // namespace

/** @copydoc vmm::init */
void init() {
    serial::puts("[vmm] Initializing virtual memory manager\n");

    // Allocate root page table
    u64 root_phys = pmm::alloc_page();
    if (root_phys == 0) {
        serial::puts("[vmm] ERROR: Failed to allocate root page table!\n");
        return;
    }

    pgt_root = reinterpret_cast<u64 *>(root_phys);

    // Zero the root table
    for (u64 i = 0; i < ENTRIES_PER_TABLE; i++) {
        pgt_root[i] = 0;
    }

    serial::puts("[vmm] Root page table at ");
    serial::put_hex(root_phys);
    serial::puts("\n");

    // Note: We're currently running with the bootloader/QEMU's identity mapping
    // For a full implementation, we'd set up our own page tables and switch to them
    // For now, we just prepare the infrastructure

    serial::puts("[vmm] VMM initialized (identity mapping active)\n");
}

// Internal unlocked version for use when lock is already held
static bool map_page_unlocked(u64 virt, u64 phys, u64 flags) {
    if (!pgt_root) {
        serial::puts("[vmm] ERROR: VMM not initialized!\n");
        return false;
    }

    // Track newly allocated tables for rollback on failure
    TableAllocation allocated;

    // Walk/create page tables with rollback support
    u64 *l0 = pgt_root;
    u64 *l1 = get_or_create_table(l0, l0_index(virt), allocated);
    if (!l1) {
        allocated.rollback();
        return false;
    }

    u64 *l2 = get_or_create_table(l1, l1_index(virt), allocated);
    if (!l2) {
        allocated.rollback();
        return false;
    }

    u64 *l3 = get_or_create_table(l2, l2_index(virt), allocated);
    if (!l3) {
        allocated.rollback();
        return false;
    }

    // Install page entry
    u64 idx = l3_index(virt);
    l3[idx] = (phys & PHYS_MASK) | flags;

    // Invalidate TLB for this address
    invalidate_page(virt);

    return true;
}

// Forward declaration for internal unlocked unmap (used by map_range rollback)
static void unmap_page_unlocked(u64 virt);

/** @copydoc vmm::map_page */
bool map_page(u64 virt, u64 phys, u64 flags) {
    SpinlockGuard guard(vmm_lock);
    return map_page_unlocked(virt, phys, flags);
}

/** @copydoc vmm::map_range */
bool map_range(u64 virt, u64 phys, u64 size, u64 flags) {
    SpinlockGuard guard(vmm_lock);

    u64 pages = (size + pmm::PAGE_SIZE - 1) / pmm::PAGE_SIZE;

    for (u64 i = 0; i < pages; i++) {
        if (!map_page_unlocked(virt + i * pmm::PAGE_SIZE, phys + i * pmm::PAGE_SIZE, flags)) {
            // Rollback: unmap all pages we successfully mapped
            for (u64 j = 0; j < i; j++) {
                unmap_page_unlocked(virt + j * pmm::PAGE_SIZE);
            }
            return false;
        }
    }

    return true;
}

/** @copydoc vmm::map_block_2mb */
bool map_block_2mb(u64 virt, u64 phys, u64 flags) {
    SpinlockGuard guard(vmm_lock);

    if (!pgt_root) {
        serial::puts("[vmm] ERROR: VMM not initialized!\n");
        return false;
    }

    // Alignment checks - both addresses must be 2MB aligned
    if ((virt & (pte::BLOCK_2MB - 1)) != 0) {
        serial::puts("[vmm] ERROR: Virtual address not 2MB aligned: ");
        serial::put_hex(virt);
        serial::puts("\n");
        return false;
    }

    if ((phys & (pte::BLOCK_2MB - 1)) != 0) {
        serial::puts("[vmm] ERROR: Physical address not 2MB aligned: ");
        serial::put_hex(phys);
        serial::puts("\n");
        return false;
    }

    // Track newly allocated tables for rollback on failure
    TableAllocation allocated;

    // Walk/create page tables down to L2 (not L3 - we install block there)
    u64 *l0 = pgt_root;
    u64 *l1 = get_or_create_table(l0, l0_index(virt), allocated);
    if (!l1) {
        allocated.rollback();
        return false;
    }

    u64 *l2 = get_or_create_table(l1, l1_index(virt), allocated);
    if (!l2) {
        allocated.rollback();
        return false;
    }

    // Install 2MB block descriptor at L2
    // Note: For a block, bit 1 = 0 (not TABLE), which is already in the flags
    u64 idx = l2_index(virt);
    l2[idx] = (phys & ~(pte::BLOCK_2MB - 1)) | flags;

    // Invalidate TLB for this range
    // For a 2MB block, we need to invalidate all 512 pages within it
    // Using a range-based invalidation would be more efficient, but
    // for simplicity we use the full TLB invalidation
    invalidate_all();

    return true;
}

/** @copydoc vmm::unmap_block_2mb */
void unmap_block_2mb(u64 virt) {
    SpinlockGuard guard(vmm_lock);

    // Alignment check
    if ((virt & (pte::BLOCK_2MB - 1)) != 0)
        return;

    // Walk page tables to L2
    u64 *l2 = walk_tables_readonly(virt, 2);
    if (!l2)
        return;

    // Clear the L2 entry
    l2[l2_index(virt)] = 0;

    // Invalidate TLB
    invalidate_all();
}

// Internal unlocked unmap implementation
static void unmap_page_unlocked(u64 virt) {
    // Walk page tables to L3
    u64 *l3 = walk_tables_readonly(virt, 3);
    if (!l3)
        return;

    // Clear the entry
    l3[l3_index(virt)] = 0;

    // Invalidate TLB
    invalidate_page(virt);
}

/** @copydoc vmm::unmap_page */
void unmap_page(u64 virt) {
    SpinlockGuard guard(vmm_lock);
    unmap_page_unlocked(virt);
}

/** @copydoc vmm::virt_to_phys */
u64 virt_to_phys(u64 virt) {
    SpinlockGuard guard(vmm_lock);

    if (!pgt_root) {
        // Identity mapping fallback
        return virt;
    }

    // Walk page tables
    u64 *l0 = pgt_root;
    u64 l0e = l0[l0_index(virt)];
    if (!(l0e & pte::VALID))
        return 0;

    u64 *l1 = reinterpret_cast<u64 *>(l0e & PHYS_MASK);
    u64 l1e = l1[l1_index(virt)];
    if (!(l1e & pte::VALID))
        return 0;

    // Check for 1GB block
    if (!(l1e & pte::TABLE)) {
        return (l1e & PHYS_MASK) | (virt & ((1ULL << L1_SHIFT) - 1));
    }

    u64 *l2 = reinterpret_cast<u64 *>(l1e & PHYS_MASK);
    u64 l2e = l2[l2_index(virt)];
    if (!(l2e & pte::VALID))
        return 0;

    // Check for 2MB block
    if (!(l2e & pte::TABLE)) {
        return (l2e & PHYS_MASK) | (virt & ((1ULL << L2_SHIFT) - 1));
    }

    u64 *l3 = reinterpret_cast<u64 *>(l2e & PHYS_MASK);
    u64 l3e = l3[l3_index(virt)];
    if (!(l3e & pte::VALID))
        return 0;

    return (l3e & PHYS_MASK) | (virt & (pmm::PAGE_SIZE - 1));
}

/** @copydoc vmm::invalidate_page */
void invalidate_page(u64 virt) {
    asm volatile("tlbi vaae1is, %0" : : "r"(virt >> 12));
    asm volatile("dsb sy");
    asm volatile("isb");
}

/** @copydoc vmm::invalidate_all */
void invalidate_all() {
    asm volatile("tlbi vmalle1is");
    asm volatile("dsb sy");
    asm volatile("isb");
}

} // namespace vmm

#pragma GCC diagnostic pop
