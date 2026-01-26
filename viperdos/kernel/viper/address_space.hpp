#pragma once

/**
 * @file address_space.hpp
 * @brief User address space management for AArch64.
 *
 * @details
 * This header defines the core primitives used to manage EL0 (user) address
 * spaces in ViperDOS. It includes:
 * - A small ASID allocator (to tag TLB entries per-process).
 * - An @ref viper::AddressSpace class that owns a page table root and provides
 *   mapping/unmapping helpers for 4 KiB pages.
 * - Helpers to switch TTBR0 and invalidate TLB entries.
 *
 * The implementation is intentionally minimal and optimized for early bring-up.
 * Notably, page table reclamation is incomplete and many routines assume a
 * single-core environment (no locking around ASID allocation).
 */

#include "../include/types.hpp"

namespace viper {

/**
 * @brief Protection flags used when mapping pages in a user address space.
 *
 * @details
 * These flags are passed to @ref AddressSpace::map and @ref AddressSpace::alloc_map.
 * The mapping code translates them into AArch64 page table permission bits.
 *
 * The flags express desired access at EL0. The kernel still retains access via
 * its own mappings and exception handling context.
 */
namespace prot {
/** @brief No access (rarely used; typically map with explicit permissions). */
constexpr u32 NONE = 0;
/** @brief Page is readable at EL0. */
constexpr u32 READ = 1 << 0;
/** @brief Page is writable at EL0. */
constexpr u32 WRITE = 1 << 1;
/** @brief Page is executable at EL0. */
constexpr u32 EXEC = 1 << 2;

// Common combinations
/** @brief Read/write mapping. */
constexpr u32 RW = READ | WRITE;
/** @brief Read/execute mapping (typical text/code). */
constexpr u32 RX = READ | EXEC;
/** @brief Read/write/execute mapping (generally discouraged). */
constexpr u32 RWX = READ | WRITE | EXEC;
} // namespace prot

/**
 * @brief AArch64 page table entry (PTE) bit definitions used by the mapper.
 *
 * @details
 * These constants encode a subset of the ARMv8-A translation table format for
 * 4 KiB granules and 4-level page tables. They are used when constructing
 * entries in @ref AddressSpace::map.
 *
 * Only the bits required by the current kernel are defined here. Memory
 * attribute indices refer to entries in MAIR_EL1 configured by the MMU setup.
 */
namespace pte {
/** @brief Entry is valid. */
constexpr u64 VALID = 1ULL << 0; // Entry is valid
/** @brief Entry points to the next-level table (non-leaf). */
constexpr u64 TABLE = 1ULL << 1; // Points to next level table (non-leaf)
/** @brief Entry is a page mapping at level 3 (leaf). */
constexpr u64 PAGE = 1ULL << 1; // Points to page (L3 leaf)
/** @brief Access flag; must be set for normal access. */
constexpr u64 AF = 1ULL << 10; // Access flag
/** @brief Inner-shareable memory. */
constexpr u64 SH_INNER = 3ULL << 8; // Inner shareable
/** @brief Allow EL0 access (as opposed to kernel-only). */
constexpr u64 AP_EL0 = 1ULL << 6; // EL0 accessible
/** @brief Read-only access permission. */
constexpr u64 AP_RO = 2ULL << 6; // Read-only
/** @brief User execute-never (disallow EL0 execution). */
constexpr u64 UXN = 1ULL << 54; // User execute never
/** @brief Privileged execute-never (disallow EL1 execution). */
constexpr u64 PXN = 1ULL << 53; // Privileged execute never
/** @brief MAIR index 0: device memory attributes. */
constexpr u64 ATTR_DEVICE = 0ULL << 2; // MAIR index 0 (device memory)
/** @brief MAIR index 1: normal memory attributes. */
constexpr u64 ATTR_NORMAL = 1ULL << 2; // MAIR index 1 (normal memory)

// Address mask for 4KB pages
/** @brief Mask extracting the output-address bits from an entry (4 KiB granule). */
constexpr u64 ADDR_MASK = 0x0000'FFFF'FFFF'F000ULL;
} // namespace pte

/**
 * @brief Maximum supported ASID count.
 *
 * @details
 * The allocator tracks ASIDs in a small bitmap. ASID 0 is reserved for the
 * kernel, leaving 255 available for user address spaces.
 */
constexpr u16 MAX_ASID = 256;
/** @brief Sentinel value returned when ASID allocation fails. */
constexpr u16 ASID_INVALID = 0;

/**
 * @brief Initialize the ASID allocator.
 *
 * @details
 * Clears the global ASID bitmap and reserves ASID 0 for the kernel. Must be
 * called before @ref asid_alloc.
 */
void asid_init();

/**
 * @brief Allocate an ASID for a new address space.
 *
 * @details
 * Searches the ASID bitmap for a free value (excluding ASID 0). On success the
 * ASID is marked as in-use until returned by @ref asid_free.
 *
 * This routine is not thread-safe; the current kernel assumes single-core
 * initialization when allocating address spaces.
 *
 * @return Allocated ASID value, or @ref ASID_INVALID if none are available.
 */
u16 asid_alloc();

/**
 * @brief Free a previously allocated ASID.
 *
 * @details
 * Marks the ASID as available for reuse. ASID 0 is ignored to preserve the
 * kernel reservation.
 *
 * @param asid ASID value to free.
 */
void asid_free(u16 asid);

/**
 * @brief Own and manipulate a user-space translation table hierarchy.
 *
 * @details
 * An AddressSpace represents the page-table state needed to execute user-mode
 * code. It owns:
 * - `root_`: the physical address of the L0 translation table used in TTBR0.
 * - `asid_`: an ASID allocated from the global allocator to tag TLB entries.
 *
 * The mapping routines assume 4 KiB pages and 4-level translation tables. The
 * current implementation maps user pages as "normal memory" and sets entries as
 * inner-shareable. Permission bits are derived from @ref prot flags.
 *
 * During initialization, the implementation may copy kernel mappings into the
 * user's table so that exceptions taken from EL0 can execute kernel code
 * reliably.
 */
class AddressSpace {
  public:
    /**
     * @brief Initialize a new address space.
     *
     * @details
     * Allocates an ASID and a root (L0) page table, clears it, and then installs
     * a minimal set of kernel mappings required to run the kernel when an
     * exception is taken from user mode.
     *
     * On failure, any partially allocated resources are released.
     *
     * @return `true` on success, otherwise `false`.
     */
    bool init();

    /**
     * @brief Destroy this address space and release owned resources.
     *
     * @details
     * Flushes TLB entries for the address space and frees the root page table,
     * then returns the ASID to the allocator.
     *
     * Note: recursive page-table freeing is not yet implemented; only the root
     * is freed during bring-up.
     */
    void destroy();

    /**
     * @brief Map a physical range into this address space.
     *
     * @details
     * Creates PTEs for `size` bytes starting at `virt` and backed by pages
     * starting at `phys`. The size is rounded up to a whole number of pages.
     *
     * The mapping code creates any missing translation tables along the walk.
     * After installing each page mapping it invalidates the corresponding TLB
     * entry for this ASID.
     *
     * @param virt Virtual address start (need not be page-aligned; rounded down internally).
     * @param phys Physical address start (assumed page-aligned by the caller).
     * @param size Size in bytes.
     * @param prot_flags Protection flags from @ref prot.
     * @return `true` on success, otherwise `false`.
     */
    bool map(u64 virt, u64 phys, usize size, u32 prot_flags);

    /**
     * @brief Unmap a virtual address range.
     *
     * @details
     * Clears leaf PTEs covering `size` bytes starting at `virt`. The operation
     * does not currently reclaim now-empty intermediate page tables.
     *
     * @param virt Virtual address start.
     * @param size Size in bytes.
     */
    void unmap(u64 virt, usize size);

    /**
     * @brief Allocate physical pages and map them at a requested virtual address.
     *
     * @details
     * Allocates a contiguous physical page range from the physical memory
     * manager and maps it into this address space at `virt` with the provided
     * protections. The allocated pages are zeroed before returning.
     *
     * If mapping fails, the physical pages are freed and 0 is returned.
     *
     * @param virt Virtual address at which to create the mapping.
     * @param size Size in bytes to allocate/map (rounded up to pages).
     * @param prot_flags Protection flags from @ref prot.
     * @return The virtual address (`virt`) on success, or 0 on failure.
     */
    u64 alloc_map(u64 virt, usize size, u32 prot_flags);

    /**
     * @brief Translate a virtual address to a physical address.
     *
     * @details
     * Walks the translation tables rooted at @ref root and returns the physical
     * address corresponding to `virt` if a valid mapping exists.
     *
     * @param virt Virtual address to translate.
     * @return Physical address, or 0 if not mapped.
     */
    u64 translate(u64 virt);

    /** @brief Get the physical address of the root translation table. */
    u64 root() const {
        return root_;
    }

    /** @brief Get the ASID associated with this address space. */
    u16 asid() const {
        return asid_;
    }

    /** @brief Whether this address space has a valid root and ASID. */
    bool is_valid() const {
        return asid_ != ASID_INVALID && root_ != 0;
    }

    /**
     * @brief Read the raw PTE value for a virtual address.
     *
     * @details
     * Walks the page tables and returns the raw L3 entry value, which may be
     * a valid mapping, a swap entry, or zero (unmapped). This is used by the
     * swap subsystem to detect swap entries.
     *
     * @param virt Virtual address to look up.
     * @return Raw PTE value, or 0 if not mapped at L3.
     */
    u64 read_pte(u64 virt);

    /**
     * @brief Write a raw PTE value for a virtual address.
     *
     * @details
     * Walks (and allocates if needed) page tables to L3 and writes the given
     * entry value. Used to store swap entries or update mappings directly.
     *
     * @param virt Virtual address to update.
     * @param entry PTE value to write.
     * @return true on success, false if table allocation fails.
     */
    bool write_pte(u64 virt, u64 entry);

    /**
     * @brief Clone mappings from another address space for COW fork.
     *
     * @details
     * Walks the parent's user-space page tables and creates read-only copies
     * of all mappings in this address space. Both parent and child pages are
     * marked read-only; the COW fault handler will copy on write.
     *
     * Page reference counts must be incremented by the caller for shared pages.
     *
     * @param parent Address space to clone from.
     * @return true on success, false on failure.
     */
    bool clone_cow_from(AddressSpace *parent);

    /**
     * @brief Make all writable user mappings read-only.
     *
     * @details
     * Used during fork to convert parent's writable pages to COW.
     * Walks all L3 entries and clears the write permission bit.
     */
    void make_cow_readonly();

  private:
    u64 root_{0}; /**< Physical address of the L0 page table (TTBR0 root). */
    u16 asid_{0}; /**< Address Space ID allocated for this space. */

    /**
     * @brief Get or allocate a child page table for a parent entry.
     *
     * @details
     * If `parent[index]` is not valid, allocates a new page for the child table,
     * zeros it, and installs a table descriptor. Returns a pointer to the child
     * table mapped in the kernel.
     *
     * @param parent Pointer to the parent translation table (kernel-mapped).
     * @param index Index into the parent table.
     * @return Pointer to the child table (kernel-mapped), or `nullptr` on allocation failure.
     */
    u64 *get_or_alloc_table(u64 *parent, int index);

    /**
     * @brief Convert a physical address to a kernel-accessible pointer.
     *
     * @details
     * The current kernel uses an identity mapping during bring-up, so the
     * conversion is a simple cast. If the kernel moves to a higher-half layout,
     * this function is the central hook to adjust physical-to-virtual mapping.
     *
     * @param phys Physical address.
     * @return Kernel virtual pointer to the same underlying memory.
     */
    static u64 *phys_to_virt(u64 phys);
};

/**
 * @brief Switch the CPU to a user address space (TTBR0 + ASID).
 *
 * @details
 * Writes `ttbr0` and `asid` into TTBR0_EL1 in the format expected by AArch64,
 * then issues an ISB to ensure the change takes effect before subsequent memory
 * accesses.
 *
 * @param ttbr0 Physical address of the new root translation table.
 * @param asid ASID to associate with the new TTBR0.
 */
void switch_address_space(u64 ttbr0, u16 asid);

/**
 * @brief Flush all TLB entries tagged with an ASID.
 *
 * @details
 * Issues a TLBI ASIDE1IS to invalidate all stage-1 EL1 entries associated with
 * the given ASID, followed by appropriate barriers.
 *
 * @param asid ASID to invalidate.
 */
void tlb_flush_asid(u16 asid);

/**
 * @brief Flush a single page translation for an ASID.
 *
 * @details
 * Invalidates the TLB entry for `virt` (page-granular) associated with `asid`.
 * This is typically used after updating a leaf PTE.
 *
 * @param virt Virtual address whose translation should be invalidated.
 * @param asid ASID associated with the translation.
 */
void tlb_flush_page(u64 virt, u16 asid);

/**
 * @brief Debug: Register vinit's page table addresses for corruption detection.
 */
void debug_set_vinit_tables(u64 l0, u64 l1, u64 l2);

/**
 * @brief Debug: Verify vinit's page tables haven't been corrupted.
 * @param context String describing the check location (e.g., "after spawn").
 * @return true if tables are intact, false if corruption detected.
 */
bool debug_verify_vinit_tables(const char *context);

} // namespace viper
