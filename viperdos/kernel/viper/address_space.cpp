//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file address_space.cpp
 * @brief AArch64 user address space and ASID allocator implementation.
 *
 * @details
 * Implements the primitives declared in `address_space.hpp`. The mapping logic
 * builds 4-level translation tables for 4 KiB pages and uses an identity-mapped
 * physical memory view to access page table pages directly.
 *
 * Thread safety:
 * - ASID allocation is protected by a spinlock for multi-core correctness.
 * - Page table operations are per-AddressSpace and assumed single-threaded.
 * - Mappings are installed as normal memory with inner-shareable attributes.
 */

#include "address_space.hpp"
#include "../arch/aarch64/mmu.hpp"
#include "../console/serial.hpp"
#include "../lib/spinlock.hpp"
#include "../mm/cow.hpp"
#include "../mm/pmm.hpp"
#include "../mm/swap.hpp"

namespace viper {

// Spinlock protecting ASID bitmap operations
static Spinlock asid_lock;

// ASID bitmap (256 ASIDs, 4 x 64-bit words)
static u64 asid_bitmap[4] = {0, 0, 0, 0};
static u16 asid_next = 1; // Start at 1, 0 is reserved for kernel

/** @copydoc viper::asid_init */
void asid_init() {
    SpinlockGuard guard(asid_lock);

    // Clear bitmap - all ASIDs free
    for (int i = 0; i < 4; i++) {
        asid_bitmap[i] = 0;
    }
    // Reserve ASID 0 for kernel
    asid_bitmap[0] |= 1;
    asid_next = 1;

    serial::puts("[asid] ASID allocator initialized (255 available)\n");
}

/** @copydoc viper::asid_alloc */
u16 asid_alloc() {
    SpinlockGuard guard(asid_lock);

    // Search for a free ASID starting from asid_next
    for (u16 i = 0; i < MAX_ASID; i++) {
        u16 asid = (asid_next + i) % MAX_ASID;
        if (asid == 0)
            continue; // Skip kernel ASID

        u32 word = asid / 64;
        u32 bit = asid % 64;

        if (!(asid_bitmap[word] & (1ULL << bit))) {
            // Found free ASID
            asid_bitmap[word] |= (1ULL << bit);
            asid_next = (asid + 1) % MAX_ASID;
            return asid;
        }
    }

    serial::puts("[asid] ERROR: No free ASIDs!\n");
    return ASID_INVALID;
}

/** @copydoc viper::asid_free */
void asid_free(u16 asid) {
    if (asid == 0 || asid >= MAX_ASID)
        return;

    SpinlockGuard guard(asid_lock);

    u32 word = asid / 64;
    u32 bit = asid % 64;

    asid_bitmap[word] &= ~(1ULL << bit);
}

/** @copydoc viper::AddressSpace::phys_to_virt */
u64 *AddressSpace::phys_to_virt(u64 phys) {
    return reinterpret_cast<u64 *>(pmm::phys_to_virt(phys));
}

/** @copydoc viper::AddressSpace::init */
bool AddressSpace::init() {
    // Verify vinit tables at start of init
    debug_verify_vinit_tables("AddressSpace::init start");

    // Allocate ASID
    asid_ = asid_alloc();
    if (asid_ == ASID_INVALID) {
        return false;
    }

    debug_verify_vinit_tables("after asid_alloc");

    // Allocate root page table (L0)
    u64 l0_page = pmm::alloc_page();
    if (l0_page == 0) {
        asid_free(asid_);
        asid_ = ASID_INVALID;
        return false;
    }

    debug_verify_vinit_tables("after L0 alloc");


    root_ = l0_page;

    // Zero the L0 table
    u64 *l0 = phys_to_virt(root_);
    for (int i = 0; i < 512; i++) {
        l0[i] = 0;
    }

    debug_verify_vinit_tables("after L0 zero");

    // Create user's own L1 table that includes kernel mappings
    // This allows kernel code to run when exceptions occur from user space
    // We can't share the kernel's L1 directly because user mappings would corrupt it
    u64 kernel_ttbr0 = mmu::get_kernel_ttbr0();
    if (kernel_ttbr0 != 0) {
        // Allocate user's L1 table
        u64 l1_page = pmm::alloc_page();
        if (l1_page == 0) {
            pmm::free_page(l0_page);
            asid_free(asid_);
            asid_ = ASID_INVALID;
            return false;
        }

        debug_verify_vinit_tables("after L1 alloc");


        // Zero user's L1 table
        u64 *user_l1 = phys_to_virt(l1_page);
        for (int i = 0; i < 512; i++) {
            user_l1[i] = 0;
        }

        debug_verify_vinit_tables("after L1 zero");

        // Copy kernel's L1[0] and L1[1] entries (0-2GB kernel mappings)
        u64 *kernel_l0 = phys_to_virt(kernel_ttbr0);
        if (kernel_l0[0] & pte::VALID) {
            u64 *kernel_l1 = phys_to_virt(kernel_l0[0] & pte::ADDR_MASK);
            user_l1[0] = kernel_l1[0]; // Device memory 0-1GB
            user_l1[1] = kernel_l1[1]; // RAM 1-2GB
        }

        debug_verify_vinit_tables("after kernel L1 copy");

        // Install user's L1 in user's L0 with proper barriers
        l0[0] = l1_page | pte::VALID | pte::TABLE;

        // Ensure page table writes are visible to hardware table walker
        asm volatile("dc cvau, %0" ::"r"(&l0[0]));
        asm volatile("dc cvau, %0" ::"r"(&user_l1[0]));
        asm volatile("dc cvau, %0" ::"r"(&user_l1[1]));
        asm volatile("dsb ish");
        asm volatile("isb");

        debug_verify_vinit_tables("after cache flush");

        // Debug: show L0 and L1 addresses
        serial::puts("[address_space] L0=");
        serial::put_hex(l0_page);
        serial::puts(" L1=");
        serial::put_hex(l1_page);
        serial::puts("\n");
    }

    serial::puts("[address_space] Created new address space: ASID=");
    serial::put_dec(asid_);
    serial::puts(", root=");
    serial::put_hex(root_);
    serial::puts("\n");

    debug_verify_vinit_tables("AddressSpace::init end");

    return true;
}

/** @copydoc viper::AddressSpace::destroy */
void AddressSpace::destroy() {
    if (root_ == 0)
        return;

    serial::puts("[address_space] Destroying address space: ASID=");
    serial::put_dec(asid_);
    serial::puts("\n");

    // Flush TLB for this ASID first (before freeing tables)
    tlb_flush_asid(asid_);

    // Walk and free all page table levels
    u64 *l0 = phys_to_virt(root_);

    for (int i = 0; i < 512; i++) {
        u64 entry = l0[i];

        if (!(entry & pte::VALID)) {
            continue;
        }

        if (entry & pte::TABLE) {
            u64 l1_addr = entry & pte::ADDR_MASK;

            // The user's L1[0] contains entries copied from kernel
            // We need to be careful not to free kernel page tables
            bool is_user_l1_with_kernel = (i == 0);

            // Walk L1
            u64 *l1 = phys_to_virt(l1_addr);
            for (int j = 0; j < 512; j++) {
                u64 l1_entry = l1[j];

                if (!(l1_entry & pte::VALID)) {
                    continue;
                }

                // Skip kernel mappings in slot 0's L1 (entries 0 and 1 are kernel)
                if (is_user_l1_with_kernel && (j == 0 || j == 1)) {
                    continue;
                }

                if (l1_entry & pte::TABLE) {
                    u64 l2_addr = l1_entry & pte::ADDR_MASK;

                    // Walk L2
                    u64 *l2 = phys_to_virt(l2_addr);
                    for (int k = 0; k < 512; k++) {
                        u64 l2_entry = l2[k];

                        if (!(l2_entry & pte::VALID)) {
                            continue;
                        }

                        if (l2_entry & pte::TABLE) {
                            u64 l3_addr = l2_entry & pte::ADDR_MASK;

                            // Walk L3 and free user pages (or swap entries)
                            u64 *l3 = phys_to_virt(l3_addr);
                            for (int l = 0; l < 512; l++) {
                                u64 l3_entry = l3[l];

                                if (l3_entry & pte::VALID) {
                                    // Free the user data page, respecting COW reference counts
                                    u64 page_addr = l3_entry & pte::ADDR_MASK;
                                    u16 refcount = mm::cow::cow_manager().get_ref(page_addr);
                                    if (refcount > 1) {
                                        // Page is COW-shared with another process - just decrement
                                        mm::cow::cow_manager().dec_ref(page_addr);
                                    } else if (refcount == 1) {
                                        // Last reference - decrement and free
                                        mm::cow::cow_manager().dec_ref(page_addr);
                                        pmm::free_page(page_addr);
                                    } else {
                                        // refcount == 0: page wasn't COW-tracked, just free it
                                        pmm::free_page(page_addr);
                                    }
                                } else if (mm::swap::is_swap_entry(l3_entry)) {
                                    // Page is swapped out - free the swap slot
                                    mm::swap::free_slot(l3_entry);
                                }
                            }

                            // Free the L3 table
                            pmm::free_page(l3_addr);
                        }
                    }

                    // Free the L2 table
                    pmm::free_page(l2_addr);
                }
            }

            // Free the L1 table
            pmm::free_page(l1_addr);
        }
    }

    // Free the root (L0) table
    pmm::free_page(root_);
    root_ = 0;

    // Free ASID
    if (asid_ != ASID_INVALID) {
        asid_free(asid_);
        asid_ = ASID_INVALID;
    }

    serial::puts("[address_space] Address space fully released\n");
}

// Debug: Track physical addresses of vinit's page tables for corruption detection
static u64 vinit_l0_phys = 0;
static u64 vinit_l1_phys = 0;
static u64 vinit_l2_phys = 0;
static u64 vinit_l2_entry0 = 0; // Track initial L2[0] value

/** @copydoc viper::AddressSpace::get_or_alloc_table */
u64 *AddressSpace::get_or_alloc_table(u64 *parent, int index) {
    if (!(parent[index] & pte::VALID)) {
        // Allocate new table
        u64 page = pmm::alloc_page();
        if (page == 0)
            return nullptr;

        // DEBUG: Check if we're about to overwrite vinit's page tables
        if (vinit_l0_phys != 0 && page == vinit_l0_phys) {
            serial::puts("[page_table] CRITICAL: PMM returned vinit's L0!\n");
        }
        if (vinit_l1_phys != 0 && page == vinit_l1_phys) {
            serial::puts("[page_table] CRITICAL: PMM returned vinit's L1!\n");
        }
        if (vinit_l2_phys != 0 && page == vinit_l2_phys) {
            serial::puts("[page_table] CRITICAL: PMM returned vinit's L2!\n");
        }

        // Zero the new table
        u64 *child = phys_to_virt(page);
        for (int i = 0; i < 512; i++) {
            child[i] = 0;
        }

        // Install table entry with proper memory barriers
        // The page table walker may bypass caches, so we need DSB to ensure
        // the write is complete before any subsequent page table walk
        parent[index] = page | pte::VALID | pte::TABLE;

        // Clean the cache line containing the entry to ensure visibility
        asm volatile("dc cvau, %0" ::"r"(&parent[index]));
        asm volatile("dsb ish");
        asm volatile("isb");
    }

    return phys_to_virt(parent[index] & pte::ADDR_MASK);
}

void debug_set_vinit_tables(u64 l0, u64 l1, u64 l2) {
    vinit_l0_phys = l0;
    vinit_l1_phys = l1;
    vinit_l2_phys = l2;

    // Also record the initial L2[0] value
    u64 *l2_ptr = reinterpret_cast<u64 *>(pmm::phys_to_virt(l2));
    vinit_l2_entry0 = l2_ptr[0];

    serial::puts("[page_table] Tracking vinit tables: L0=");
    serial::put_hex(l0);
    serial::puts(" L1=");
    serial::put_hex(l1);
    serial::puts(" L2=");
    serial::put_hex(l2);
    serial::puts(" L2[0]=");
    serial::put_hex(vinit_l2_entry0);
    serial::puts("\n");
}

// Flag to halt after first corruption detected
static bool corruption_detected = false;

// Track last passing checkpoint for debugging
static const char *last_good_checkpoint = nullptr;

bool debug_verify_vinit_tables(const char *context) {
    if (vinit_l0_phys == 0) {
        return true; // Not tracking yet
    }

    // If we already detected corruption, spin forever to preserve output
    if (corruption_detected) {
        while (true) {
            asm volatile("wfe");
        }
    }

    // Read L0[0] and verify it points to L1
    u64 *l0 = reinterpret_cast<u64 *>(pmm::phys_to_virt(vinit_l0_phys));
    u64 l0_entry = l0[0];

    if (!(l0_entry & pte::VALID)) {
        corruption_detected = true;
        serial::puts("\n\n========== CORRUPTION DETECTED ==========\n");
        serial::puts("[page_table] CORRUPTION at ");
        serial::puts(context);
        serial::puts(": L0[0] invalid! was=");
        serial::put_hex(l0_entry);
        serial::puts("\n");
        serial::puts("=========================================\n");
        serial::puts("Halting to preserve boot output...\n");
        while (true) {
            asm volatile("wfe");
        }
        return false;
    }

    u64 l1_from_l0 = l0_entry & pte::ADDR_MASK;
    if (l1_from_l0 != vinit_l1_phys) {
        corruption_detected = true;
        serial::puts("\n\n========== CORRUPTION DETECTED ==========\n");
        serial::puts("[page_table] CORRUPTION at ");
        serial::puts(context);
        serial::puts(": L0[0] changed! expected=");
        serial::put_hex(vinit_l1_phys);
        serial::puts(" got=");
        serial::put_hex(l1_from_l0);
        serial::puts("\n");
        serial::puts("=========================================\n");
        serial::puts("Halting to preserve boot output...\n");
        while (true) {
            asm volatile("wfe");
        }
        return false;
    }

    // Read L1[2] and verify it points to L2
    u64 *l1 = reinterpret_cast<u64 *>(pmm::phys_to_virt(vinit_l1_phys));
    u64 l1_entry = l1[2];

    if (!(l1_entry & pte::VALID)) {
        corruption_detected = true;
        serial::puts("\n\n========== CORRUPTION DETECTED ==========\n");
        serial::puts("[page_table] CORRUPTION at ");
        serial::puts(context);
        serial::puts(": L1[2] invalid! was=");
        serial::put_hex(l1_entry);
        serial::puts("\n");
        serial::puts("=========================================\n");
        serial::puts("Halting to preserve boot output...\n");
        while (true) {
            asm volatile("wfe");
        }
        return false;
    }

    u64 l2_from_l1 = l1_entry & pte::ADDR_MASK;
    if (l2_from_l1 != vinit_l2_phys) {
        corruption_detected = true;
        serial::puts("\n\n========== CORRUPTION DETECTED ==========\n");
        serial::puts("[page_table] CORRUPTION at ");
        serial::puts(context);
        serial::puts(": L1[2] changed! expected=");
        serial::put_hex(vinit_l2_phys);
        serial::puts(" got=");
        serial::put_hex(l2_from_l1);
        serial::puts("\n");
        serial::puts("=========================================\n");
        serial::puts("Halting to preserve boot output...\n");
        while (true) {
            asm volatile("wfe");
        }
        return false;
    }

    // Also check L2[0] - the entry for vinit's code at 0x80000000
    // Check the FULL entry value, not just VALID bit - an address size fault
    // can occur when VALID is set but the physical address is out of range
    u64 *l2 = reinterpret_cast<u64 *>(pmm::phys_to_virt(vinit_l2_phys));
    u64 l2_entry = l2[0];

    if (l2_entry != vinit_l2_entry0) {
        corruption_detected = true;
        serial::puts("\n\n========== CORRUPTION DETECTED ==========\n");
        serial::puts("Last good: ");
        serial::puts(last_good_checkpoint ? last_good_checkpoint : "(none)");
        serial::puts("\nCorrupted at: ");
        serial::puts(context);
        serial::puts("\nL2[0] was=");
        serial::put_hex(l2_entry);
        serial::puts(" expected=");
        serial::put_hex(vinit_l2_entry0);
        serial::puts("\nL2_phys=");
        serial::put_hex(vinit_l2_phys);
        serial::puts("\n=========================================\n");
        while (true) {
            asm volatile("wfe");
        }
        return false;
    }

    // Track this as the last good checkpoint
    last_good_checkpoint = context;

    return true;
}

/** @copydoc viper::AddressSpace::map */
bool AddressSpace::map(u64 virt, u64 phys, usize size, u32 prot_flags) {
    if (root_ == 0)
        return false;

    u64 *l0 = phys_to_virt(root_);
    usize pages = (size + 4095) / 4096;

    for (usize i = 0; i < pages; i++) {
        u64 va = virt + i * 4096;
        u64 pa = phys + i * 4096;

        // Extract page table indices (4KB granule, 4-level tables)
        int i0 = (va >> 39) & 0x1FF; // L0 index
        int i1 = (va >> 30) & 0x1FF; // L1 index
        int i2 = (va >> 21) & 0x1FF; // L2 index
        int i3 = (va >> 12) & 0x1FF; // L3 index

        // Walk/create page tables
        u64 *l1 = get_or_alloc_table(l0, i0);
        if (!l1)
            return false;

        u64 *l2 = get_or_alloc_table(l1, i1);
        if (!l2)
            return false;

        u64 *l3 = get_or_alloc_table(l2, i2);
        if (!l3)
            return false;

        // Build page table entry (choose memory attributes)
        u64 attr = (prot_flags & prot::UNCACHED) ? pte::ATTR_NC : pte::ATTR_NORMAL;
        u64 entry = pa | pte::VALID | pte::PAGE | pte::AF | pte::SH_INNER | pte::AP_EL0 | attr;

        // Set protection bits
        if (!(prot_flags & prot::WRITE)) {
            entry |= pte::AP_RO;
        }
        if (!(prot_flags & prot::EXEC)) {
            entry |= pte::UXN | pte::PXN;
        }

        l3[i3] = entry;

        // Ensure page table write is visible before TLB flush
        asm volatile("dc cvau, %0" ::"r"(&l3[i3]));
        asm volatile("dsb ish");

        // Invalidate TLB for this page
        tlb_flush_page(va, asid_);
    }

    return true;
}

/** @copydoc viper::AddressSpace::unmap */
void AddressSpace::unmap(u64 virt, usize size) {
    if (root_ == 0)
        return;

    u64 *l0 = phys_to_virt(root_);
    usize pages = (size + 4095) / 4096;

    for (usize i = 0; i < pages; i++) {
        u64 va = virt + i * 4096;

        int i0 = (va >> 39) & 0x1FF;
        int i1 = (va >> 30) & 0x1FF;
        int i2 = (va >> 21) & 0x1FF;
        int i3 = (va >> 12) & 0x1FF;

        // Walk page tables (don't allocate)
        if (!(l0[i0] & pte::VALID))
            continue;
        u64 *l1 = phys_to_virt(l0[i0] & pte::ADDR_MASK);

        if (!(l1[i1] & pte::VALID))
            continue;
        u64 *l2 = phys_to_virt(l1[i1] & pte::ADDR_MASK);

        if (!(l2[i2] & pte::VALID))
            continue;
        u64 *l3 = phys_to_virt(l2[i2] & pte::ADDR_MASK);

        // Clear entry
        l3[i3] = 0;

        // Invalidate TLB
        tlb_flush_page(va, asid_);
    }
}

/** @copydoc viper::AddressSpace::alloc_map */
u64 AddressSpace::alloc_map(u64 virt, usize size, u32 prot_flags) {
    usize pages = (size + 4095) / 4096;

    // Allocate physical pages
    u64 phys = pmm::alloc_pages(pages);
    if (phys == 0) {
        return 0;
    }

    // Zero the allocated pages
    u64 *ptr = phys_to_virt(phys);
    for (usize i = 0; i < (pages * 4096) / sizeof(u64); i++) {
        ptr[i] = 0;
    }

    // Map into address space
    if (!map(virt, phys, size, prot_flags)) {
        pmm::free_pages(phys, pages);
        return 0;
    }

    return virt;
}

/** @copydoc viper::AddressSpace::translate */
u64 AddressSpace::translate(u64 virt) {
    if (root_ == 0)
        return 0;

    u64 *l0 = phys_to_virt(root_);

    int i0 = (virt >> 39) & 0x1FF;
    int i1 = (virt >> 30) & 0x1FF;
    int i2 = (virt >> 21) & 0x1FF;
    int i3 = (virt >> 12) & 0x1FF;

    if (!(l0[i0] & pte::VALID))
        return 0;
    u64 *l1 = phys_to_virt(l0[i0] & pte::ADDR_MASK);

    if (!(l1[i1] & pte::VALID))
        return 0;
    u64 *l2 = phys_to_virt(l1[i1] & pte::ADDR_MASK);

    if (!(l2[i2] & pte::VALID))
        return 0;
    u64 *l3 = phys_to_virt(l2[i2] & pte::ADDR_MASK);

    if (!(l3[i3] & pte::VALID))
        return 0;

    // Return physical address with page offset
    return (l3[i3] & pte::ADDR_MASK) | (virt & 0xFFF);
}

/** @copydoc viper::switch_address_space */
void switch_address_space(u64 ttbr0, u16 asid) {
    // TTBR0_EL1 format: ASID in bits [63:48], table address in bits [47:1]
    u64 val = ttbr0 | (static_cast<u64>(asid) << 48);

    asm volatile("msr    ttbr0_el1, %0   \n"
                 "isb                    \n" ::"r"(val)
                 : "memory");
}

/** @copydoc viper::tlb_flush_asid */
void tlb_flush_asid(u16 asid) {
    u64 val = static_cast<u64>(asid) << 48;
    asm volatile("tlbi   aside1is, %0    \n"
                 "dsb    sy              \n"
                 "isb                    \n" ::"r"(val));
}

/** @copydoc viper::tlb_flush_page */
void tlb_flush_page(u64 virt, u16 asid) {
    // TLBI VAE1IS: invalidate by VA and ASID
    u64 val = (virt >> 12) | (static_cast<u64>(asid) << 48);
    asm volatile("tlbi   vae1is, %0      \n"
                 "dsb    sy              \n"
                 "isb                    \n" ::"r"(val));
}

/** @copydoc viper::AddressSpace::read_pte */
u64 AddressSpace::read_pte(u64 virt) {
    if (root_ == 0)
        return 0;

    u64 *l0 = phys_to_virt(root_);

    int i0 = (virt >> 39) & 0x1FF;
    int i1 = (virt >> 30) & 0x1FF;
    int i2 = (virt >> 21) & 0x1FF;
    int i3 = (virt >> 12) & 0x1FF;

    if (!(l0[i0] & pte::VALID))
        return 0;
    u64 *l1 = phys_to_virt(l0[i0] & pte::ADDR_MASK);

    if (!(l1[i1] & pte::VALID))
        return 0;
    u64 *l2 = phys_to_virt(l1[i1] & pte::ADDR_MASK);

    if (!(l2[i2] & pte::VALID))
        return 0;
    u64 *l3 = phys_to_virt(l2[i2] & pte::ADDR_MASK);

    // Return raw PTE value (may be valid, swap entry, or zero)
    return l3[i3];
}

/** @copydoc viper::AddressSpace::write_pte */
bool AddressSpace::write_pte(u64 virt, u64 entry) {
    if (root_ == 0)
        return false;

    u64 *l0 = phys_to_virt(root_);

    int i0 = (virt >> 39) & 0x1FF;
    int i1 = (virt >> 30) & 0x1FF;
    int i2 = (virt >> 21) & 0x1FF;
    int i3 = (virt >> 12) & 0x1FF;

    // Walk/create page tables
    u64 *l1 = get_or_alloc_table(l0, i0);
    if (!l1)
        return false;

    u64 *l2 = get_or_alloc_table(l1, i1);
    if (!l2)
        return false;

    u64 *l3 = get_or_alloc_table(l2, i2);
    if (!l3)
        return false;

    // Write the entry
    l3[i3] = entry;

    // Ensure page table write is visible before TLB flush
    asm volatile("dc cvau, %0" ::"r"(&l3[i3]));
    asm volatile("dsb ish");

    // Invalidate TLB for this page
    tlb_flush_page(virt, asid_);

    return true;
}

/** @copydoc viper::AddressSpace::clone_cow_from */
bool AddressSpace::clone_cow_from(AddressSpace *parent) {
    if (!parent || !parent->is_valid() || !is_valid()) {
        serial::puts("[address_space] clone_cow_from: invalid address space\n");
        return false;
    }

    serial::puts("[address_space] Cloning address space with COW from ASID=");
    serial::put_dec(parent->asid_);
    serial::puts(" to ASID=");
    serial::put_dec(asid_);
    serial::puts("\n");

    u64 *parent_l0 = phys_to_virt(parent->root_);
    u64 *child_l0 = phys_to_virt(root_);

    // Walk parent's page tables and create matching mappings
    // Skip L0[0] which contains kernel mappings (already set up in init)
    for (int i0 = 1; i0 < 512; i0++) {
        if (!(parent_l0[i0] & pte::VALID))
            continue;
        if (!(parent_l0[i0] & pte::TABLE))
            continue; // Skip block mappings

        // Allocate L1 for child
        u64 *child_l1 = get_or_alloc_table(child_l0, i0);
        if (!child_l1)
            return false;

        u64 *parent_l1 = phys_to_virt(parent_l0[i0] & pte::ADDR_MASK);

        for (int i1 = 0; i1 < 512; i1++) {
            if (!(parent_l1[i1] & pte::VALID))
                continue;
            if (!(parent_l1[i1] & pte::TABLE))
                continue; // Skip 1GB block mappings

            u64 *child_l2 = get_or_alloc_table(child_l1, i1);
            if (!child_l2)
                return false;

            u64 *parent_l2 = phys_to_virt(parent_l1[i1] & pte::ADDR_MASK);

            for (int i2 = 0; i2 < 512; i2++) {
                if (!(parent_l2[i2] & pte::VALID))
                    continue;
                if (!(parent_l2[i2] & pte::TABLE))
                    continue; // Skip 2MB block mappings

                u64 *child_l3 = get_or_alloc_table(child_l2, i2);
                if (!child_l3)
                    return false;

                u64 *parent_l3 = phys_to_virt(parent_l2[i2] & pte::ADDR_MASK);

                for (int i3 = 0; i3 < 512; i3++) {
                    u64 entry = parent_l3[i3];
                    if (!(entry & pte::VALID))
                        continue;

                    // Get the physical page being shared
                    u64 phys_page = entry & pte::ADDR_MASK;

                    // Make the entry read-only for COW
                    // Set AP_RO bit to make it read-only
                    u64 cow_entry = entry | pte::AP_RO;

                    // Copy to child
                    child_l3[i3] = cow_entry;

                    // Also make parent's entry read-only
                    parent_l3[i3] = cow_entry;

                    // Increment page reference count
                    mm::cow::cow_manager().inc_ref(phys_page);
                    mm::cow::cow_manager().mark_cow(phys_page);
                }
            }
        }
    }

    // Flush TLBs for both address spaces
    tlb_flush_asid(parent->asid_);
    tlb_flush_asid(asid_);

    serial::puts("[address_space] COW clone complete\n");
    return true;
}

/** @copydoc viper::AddressSpace::make_cow_readonly */
void AddressSpace::make_cow_readonly() {
    if (!is_valid())
        return;

    u64 *l0 = phys_to_virt(root_);

    // Walk all user mappings (skip L0[0] which is kernel)
    for (int i0 = 1; i0 < 512; i0++) {
        if (!(l0[i0] & pte::VALID))
            continue;
        if (!(l0[i0] & pte::TABLE))
            continue;

        u64 *l1 = phys_to_virt(l0[i0] & pte::ADDR_MASK);

        for (int i1 = 0; i1 < 512; i1++) {
            if (!(l1[i1] & pte::VALID))
                continue;
            if (!(l1[i1] & pte::TABLE))
                continue;

            u64 *l2 = phys_to_virt(l1[i1] & pte::ADDR_MASK);

            for (int i2 = 0; i2 < 512; i2++) {
                if (!(l2[i2] & pte::VALID))
                    continue;
                if (!(l2[i2] & pte::TABLE))
                    continue;

                u64 *l3 = phys_to_virt(l2[i2] & pte::ADDR_MASK);

                for (int i3 = 0; i3 < 512; i3++) {
                    if (l3[i3] & pte::VALID) {
                        // Set read-only bit
                        l3[i3] |= pte::AP_RO;
                    }
                }
            }
        }
    }

    // Flush TLB
    tlb_flush_asid(asid_);
}

} // namespace viper
