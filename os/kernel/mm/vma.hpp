#pragma once

#include "../include/types.hpp"

namespace mm
{

/**
 * @file vma.hpp
 * @brief Virtual Memory Area (VMA) tracking for demand paging.
 *
 * @details
 * VMAs describe regions of a process's virtual address space, including
 * their permissions and backing type. The page fault handler uses VMAs
 * to determine whether a fault should allocate a page on demand or
 * terminate the process for an invalid access.
 *
 * VMA types:
 * - ANONYMOUS: Zero-filled memory (heap, BSS)
 * - FILE: File-backed mapping (for mmap or executable segments)
 * - STACK: Growable stack region
 * - GUARD: Guard pages that should never be mapped (trap on access)
 */

/**
 * @brief VMA backing type.
 */
enum class VmaType : u8
{
    ANONYMOUS = 0, // Zero-filled memory
    FILE = 1,      // File-backed mapping
    STACK = 2,     // Growable stack
    GUARD = 3,     // Guard page (always faults)
};

/**
 * @brief VMA protection flags (matches prot:: namespace).
 */
namespace vma_prot
{
constexpr u32 NONE = 0;
constexpr u32 READ = 1;
constexpr u32 WRITE = 2;
constexpr u32 EXEC = 4;
} // namespace vma_prot

/**
 * @brief Virtual Memory Area descriptor.
 *
 * @details
 * Describes a contiguous region of virtual address space with uniform
 * permissions and backing. VMAs are stored in a linked list per address space.
 */
struct Vma
{
    u64 start;        // Start address (page-aligned)
    u64 end;          // End address (exclusive, page-aligned)
    u32 prot;         // Protection flags (vma_prot)
    VmaType type;     // Backing type
    u8 _padding[3];

    // For file-backed VMAs
    u64 file_inode;   // Inode number (0 if anonymous)
    u64 file_offset;  // Offset within file

    // Linked list
    Vma *next;

    /**
     * @brief Check if this VMA contains an address.
     */
    bool contains(u64 addr) const
    {
        return addr >= start && addr < end;
    }

    /**
     * @brief Get the size of this VMA in bytes.
     */
    u64 size() const
    {
        return end - start;
    }
};

/**
 * @brief Maximum number of VMAs per address space.
 */
constexpr usize MAX_VMAS = 64;

/**
 * @brief VMA list manager for an address space.
 *
 * @details
 * Maintains a sorted list of VMAs for efficient lookup. Supports
 * insertion, removal, and lookup operations.
 */
class VmaList
{
  public:
    /**
     * @brief Initialize the VMA list.
     */
    void init();

    /**
     * @brief Find the VMA containing an address.
     *
     * @param addr Virtual address to look up.
     * @return Pointer to VMA, or nullptr if not found.
     */
    Vma *find(u64 addr);

    /**
     * @brief Find the VMA containing an address (const version).
     */
    const Vma *find(u64 addr) const;

    /**
     * @brief Add a new VMA to the list.
     *
     * @param start Start address (must be page-aligned).
     * @param end End address (must be page-aligned).
     * @param prot Protection flags.
     * @param type VMA type.
     * @return Pointer to new VMA, or nullptr if allocation failed.
     */
    Vma *add(u64 start, u64 end, u32 prot, VmaType type);

    /**
     * @brief Add a file-backed VMA.
     *
     * @param start Start address.
     * @param end End address.
     * @param prot Protection flags.
     * @param inode File inode number.
     * @param offset Offset within file.
     * @return Pointer to new VMA, or nullptr on failure.
     */
    Vma *add_file(u64 start, u64 end, u32 prot, u64 inode, u64 offset);

    /**
     * @brief Remove a VMA from the list.
     *
     * @param vma VMA to remove.
     * @return true if removed, false if not found.
     */
    bool remove(Vma *vma);

    /**
     * @brief Remove all VMAs in a range.
     *
     * @param start Start of range to remove.
     * @param end End of range to remove.
     */
    void remove_range(u64 start, u64 end);

    /**
     * @brief Get the head of the VMA list.
     */
    Vma *head()
    {
        return head_;
    }

    /**
     * @brief Get the number of VMAs.
     */
    usize count() const
    {
        return count_;
    }

    /**
     * @brief Clear all VMAs.
     */
    void clear();

  private:
    Vma pool_[MAX_VMAS]; // Static pool of VMA structures
    bool used_[MAX_VMAS]; // Which pool entries are in use
    Vma *head_{nullptr};  // Head of sorted linked list
    usize count_{0};      // Number of active VMAs

    /**
     * @brief Allocate a VMA from the pool.
     */
    Vma *alloc_vma();

    /**
     * @brief Free a VMA back to the pool.
     */
    void free_vma(Vma *vma);

    /**
     * @brief Insert a VMA in sorted order.
     */
    void insert_sorted(Vma *vma);
};

/**
 * @brief Flags for demand fault handling result.
 */
enum class FaultResult
{
    HANDLED,      // Fault was handled, resume execution
    UNHANDLED,    // Fault was not in a VMA, terminate process
    STACK_GROW,   // Stack was grown, resume execution
    ERROR,        // Error occurred during handling
};

/**
 * @brief Handle a demand page fault.
 *
 * @param vma_list VMA list for the faulting address space.
 * @param fault_addr The faulting virtual address.
 * @param is_write Whether the fault was a write access.
 * @param map_callback Callback to map a physical page.
 * @return Result indicating how the fault was handled.
 */
FaultResult handle_demand_fault(VmaList *vma_list, u64 fault_addr, bool is_write,
                                bool (*map_callback)(u64 virt, u64 phys, u32 prot));

} // namespace mm
