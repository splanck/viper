#include "vmm.hpp"
#include "../console/serial.hpp"
#include "pmm.hpp"

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
namespace vmm
{

namespace
{
// Page table root (TTBR0 for identity mapping)
u64 *pgt_root = nullptr;

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

inline u64 l0_index(u64 va)
{
    return (va >> L0_SHIFT) & INDEX_MASK;
}

inline u64 l1_index(u64 va)
{
    return (va >> L1_SHIFT) & INDEX_MASK;
}

inline u64 l2_index(u64 va)
{
    return (va >> L2_SHIFT) & INDEX_MASK;
}

inline u64 l3_index(u64 va)
{
    return (va >> L3_SHIFT) & INDEX_MASK;
}

// Physical address mask for table entries
constexpr u64 PHYS_MASK = 0x0000FFFFFFFFF000ULL;

// Get or allocate next-level table
/**
 * @brief Retrieve or allocate the next-level page table.
 *
 * @details
 * For a given table level, the entry at `index` either references a valid
 * next-level table (VALID+TABLE) or is empty. When empty, this function
 * allocates a new page from the PMM, zeros it, installs the descriptor, and
 * returns the new table pointer.
 *
 * @param table Current level table.
 * @param index Index into `table`.
 * @return Next-level table pointer, or `nullptr` if allocation fails.
 */
u64 *get_or_create_table(u64 *table, u64 index)
{
    u64 entry = table[index];

    if (entry & pte::VALID)
    {
        // Table already exists
        return reinterpret_cast<u64 *>(entry & PHYS_MASK);
    }

    // Allocate new table
    u64 new_table = pmm::alloc_page();
    if (new_table == 0)
    {
        serial::puts("[vmm] ERROR: Failed to allocate page table!\n");
        return nullptr;
    }

    // Zero the new table
    u64 *ptr = reinterpret_cast<u64 *>(new_table);
    for (u64 i = 0; i < ENTRIES_PER_TABLE; i++)
    {
        ptr[i] = 0;
    }

    // Install table entry
    table[index] = new_table | pte::VALID | pte::TABLE;

    return ptr;
}
} // namespace

/** @copydoc vmm::init */
void init()
{
    serial::puts("[vmm] Initializing virtual memory manager\n");

    // Allocate root page table
    u64 root_phys = pmm::alloc_page();
    if (root_phys == 0)
    {
        serial::puts("[vmm] ERROR: Failed to allocate root page table!\n");
        return;
    }

    pgt_root = reinterpret_cast<u64 *>(root_phys);

    // Zero the root table
    for (u64 i = 0; i < ENTRIES_PER_TABLE; i++)
    {
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

/** @copydoc vmm::map_page */
bool map_page(u64 virt, u64 phys, u64 flags)
{
    if (!pgt_root)
    {
        serial::puts("[vmm] ERROR: VMM not initialized!\n");
        return false;
    }

    // Walk/create page tables
    //
    // NOTE: If allocation fails at L2 or L3 level, previously-allocated
    // intermediate tables are not rolled back. This is a known limitation.
    // In practice, page table allocation failure only occurs when the system
    // is critically low on memory, at which point leaked page tables are a
    // minor concern. Full rollback would require tracking which tables were
    // newly allocated vs already existed, adding significant complexity.
    u64 *l0 = pgt_root;
    u64 *l1 = get_or_create_table(l0, l0_index(virt));
    if (!l1)
        return false;

    u64 *l2 = get_or_create_table(l1, l1_index(virt));
    if (!l2)
        return false;

    u64 *l3 = get_or_create_table(l2, l2_index(virt));
    if (!l3)
        return false;

    // Install page entry
    u64 idx = l3_index(virt);
    l3[idx] = (phys & PHYS_MASK) | flags;

    // Invalidate TLB for this address
    invalidate_page(virt);

    return true;
}

/** @copydoc vmm::map_range */
bool map_range(u64 virt, u64 phys, u64 size, u64 flags)
{
    u64 pages = (size + pmm::PAGE_SIZE - 1) / pmm::PAGE_SIZE;

    for (u64 i = 0; i < pages; i++)
    {
        if (!map_page(virt + i * pmm::PAGE_SIZE, phys + i * pmm::PAGE_SIZE, flags))
        {
            return false;
        }
    }

    return true;
}

/** @copydoc vmm::unmap_page */
void unmap_page(u64 virt)
{
    if (!pgt_root)
        return;

    // Walk page tables
    u64 *l0 = pgt_root;
    u64 l0e = l0[l0_index(virt)];
    if (!(l0e & pte::VALID))
        return;

    u64 *l1 = reinterpret_cast<u64 *>(l0e & PHYS_MASK);
    u64 l1e = l1[l1_index(virt)];
    if (!(l1e & pte::VALID))
        return;

    u64 *l2 = reinterpret_cast<u64 *>(l1e & PHYS_MASK);
    u64 l2e = l2[l2_index(virt)];
    if (!(l2e & pte::VALID))
        return;

    u64 *l3 = reinterpret_cast<u64 *>(l2e & PHYS_MASK);

    // Clear the entry
    l3[l3_index(virt)] = 0;

    // Invalidate TLB
    invalidate_page(virt);
}

/** @copydoc vmm::virt_to_phys */
u64 virt_to_phys(u64 virt)
{
    if (!pgt_root)
    {
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
    if (!(l1e & pte::TABLE))
    {
        return (l1e & PHYS_MASK) | (virt & ((1ULL << L1_SHIFT) - 1));
    }

    u64 *l2 = reinterpret_cast<u64 *>(l1e & PHYS_MASK);
    u64 l2e = l2[l2_index(virt)];
    if (!(l2e & pte::VALID))
        return 0;

    // Check for 2MB block
    if (!(l2e & pte::TABLE))
    {
        return (l2e & PHYS_MASK) | (virt & ((1ULL << L2_SHIFT) - 1));
    }

    u64 *l3 = reinterpret_cast<u64 *>(l2e & PHYS_MASK);
    u64 l3e = l3[l3_index(virt)];
    if (!(l3e & pte::VALID))
        return 0;

    return (l3e & PHYS_MASK) | (virt & (pmm::PAGE_SIZE - 1));
}

/** @copydoc vmm::invalidate_page */
void invalidate_page(u64 virt)
{
    asm volatile("tlbi vaae1is, %0" : : "r"(virt >> 12));
    asm volatile("dsb sy");
    asm volatile("isb");
}

/** @copydoc vmm::invalidate_all */
void invalidate_all()
{
    asm volatile("tlbi vmalle1is");
    asm volatile("dsb sy");
    asm volatile("isb");
}

} // namespace vmm
