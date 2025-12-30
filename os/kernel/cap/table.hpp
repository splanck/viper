#pragma once

#include "../include/types.hpp"
#include "handle.hpp"
#include "rights.hpp"

/**
 * @file table.hpp
 * @brief Capability table implementation used by a Viper/task.
 *
 * @details
 * A capability table maps opaque handles to kernel objects along with:
 * - Object kind/type information.
 * - A rights bitmask restricting permitted operations.
 * - A generation counter for detecting stale handles after slot reuse.
 *
 * Each Viper/task can own a capability table that represents its view of kernel
 * objects. This file defines the table entry format and the `cap::Table` class
 * that manages allocation, lookup, and derivation.
 */
namespace cap
{

// Object kinds (kernel object types)
/**
 * @brief Enumerates the kinds of kernel objects that can be referenced.
 *
 * @details
 * Kind tagging enables runtime type checking when resolving handles and is used
 * by syscall implementations to ensure a handle refers to the expected object
 * type.
 */
enum class Kind : u16
{
    Invalid = 0,
    // KHeap objects
    String = 1,
    Array = 2,
    Blob = 3,
    // IPC objects
    Channel = 16,
    Poll = 17,
    Timer = 18,
    // Process objects
    Task = 19,
    Viper = 20,
    // I/O objects
    File = 21,
    Directory = 22,
    Surface = 23,
    Input = 24,
    // Memory objects
    SharedMemory = 25,
};

/// Sentinel value indicating no parent (root capability)
constexpr u32 NO_PARENT = 0xFFFFFFFFU;

// Capability table entry
/**
 * @brief One slot in a capability table.
 *
 * @details
 * When `kind` is @ref Kind::Invalid, the entry is considered free/unused and
 * `object` is repurposed by the implementation to store the next free index.
 *
 * The `parent_index` field enables revocation propagation: when a capability
 * is revoked, all capabilities derived from it are also revoked.
 */
struct Entry
{
    void *object;     // Pointer to kernel object
    u32 rights;       // Rights bitmap
    u32 parent_index; // Index of parent capability (NO_PARENT if root)
    Kind kind;        // Object type
    u8 generation;    // For ABA detection
    u8 _pad;
};

// Capability table - manages handles for a Viper
/**
 * @brief Capability table mapping handles to objects.
 *
 * @details
 * The table manages a fixed-capacity array of entries and a free-list of unused
 * slots. Handles are encoded as an index + generation. Slot reuse increments
 * the generation to invalidate stale handles.
 *
 * Allocation strategy:
 * - `init()` allocates and zeroes the entry array and builds the free list.
 * - `insert()` pops an index from the free list and fills the entry.
 * - `remove()` invalidates the entry, increments generation, and pushes the
 *   slot back onto the free list.
 *
 * The table does not own the underlying objects; it only stores pointers and
 * metadata.
 */
class Table
{
  public:
    static constexpr usize DEFAULT_CAPACITY = 256;

    /**
     * @brief Initialize the table with the given capacity.
     *
     * @details
     * Allocates and zero-initializes the entry array from the kernel heap and
     * builds the internal free list.
     *
     * @param capacity Number of entries to allocate.
     * @return `true` on success, `false` on allocation failure.
     */
    bool init(usize capacity = DEFAULT_CAPACITY);

    /**
     * @brief Destroy the table and release its memory.
     *
     * @details
     * Frees the entry array. The table does not destroy objects referenced by
     * entries; object lifetime management is handled elsewhere.
     */
    void destroy();

    /**
     * @brief Allocate a new handle for an object pointer.
     *
     * @details
     * Allocates a free slot, records the object pointer, kind, and rights, and
     * returns a handle encoding the slot index and current generation.
     *
     * @param object Pointer to kernel object to reference.
     * @param kind Kind tag for the object.
     * @param rights Rights mask granted to the handle.
     * @return New handle, or @ref HANDLE_INVALID if the table is full.
     */
    Handle insert(void *object, Kind kind, Rights rights);

    /**
     * @brief Look up a handle and validate its index/generation.
     *
     * @details
     * Rejects invalid handles, out-of-range indices, unused slots, and stale
     * generation values.
     *
     * @param h Handle to resolve.
     * @return Pointer to the entry, or `nullptr` if invalid.
     */
    Entry *get(Handle h);

    /**
     * @brief Look up a handle and verify its kind tag.
     *
     * @param h Handle to resolve.
     * @param expected_kind Kind required by the caller.
     * @return Pointer to the entry if valid and kind matches, otherwise `nullptr`.
     */
    Entry *get_checked(Handle h, Kind expected_kind);

    /**
     * @brief Look up a handle and verify kind and rights.
     *
     * @details
     * Performs a kind check and then verifies that the entry's rights include
     * all rights in `required`.
     *
     * @param h Handle to resolve.
     * @param kind Expected kind.
     * @param required Rights required for the operation.
     * @return Pointer to entry if valid and authorized, otherwise `nullptr`.
     */
    Entry *get_with_rights(Handle h, Kind kind, Rights required);

    /**
     * @brief Release a handle and return its slot to the free list.
     *
     * @details
     * Invalidates the entry, increments its generation counter (preventing stale
     * handles from resolving), and pushes the slot index onto the free list.
     *
     * This does NOT propagate revocation to derived handles. Use revoke() for
     * recursive revocation.
     *
     * @param h Handle to remove.
     */
    void remove(Handle h);

    /**
     * @brief Revoke a handle and all handles derived from it.
     *
     * @details
     * Recursively invalidates the specified handle and any handles that were
     * derived from it (directly or transitively). This implements the capability
     * revocation propagation pattern.
     *
     * @param h Handle to revoke.
     * @return Number of handles revoked (including the original).
     */
    u32 revoke(Handle h);

    /**
     * @brief Derive a new handle to the same object with reduced rights.
     *
     * @details
     * Requires that the original handle includes @ref CAP_DERIVE. The derived
     * handle points to the same object and kind, but its rights are restricted
     * to the intersection of the original rights and `new_rights`.
     *
     * @param h Original handle.
     * @param new_rights Requested rights for the derived handle.
     * @return New handle, or @ref HANDLE_INVALID on failure/authorization error.
     */
    Handle derive(Handle h, Rights new_rights);

    /**
     * @brief Get the entry at a given index directly (for iteration).
     *
     * @details
     * Unlike `get()`, this does not validate the handle's generation. It returns
     * the raw entry at the given index if the index is in range, regardless of
     * whether the entry is currently valid/allocated.
     *
     * @param index Entry index (0 to capacity-1).
     * @return Pointer to the entry, or `nullptr` if index is out of range.
     */
    Entry *entry_at(usize index);

    /**
     * @brief Get the generation counter for a given index.
     *
     * @param index Entry index.
     * @return Current generation counter at that index.
     */
    u8 generation_at(usize index) const;

    // Accessors
    /// @brief Number of currently allocated entries.
    usize count() const
    {
        return count_;
    }

    /// @brief Total capacity of the table.
    usize capacity() const
    {
        return capacity_;
    }

  private:
    Entry *entries_ = nullptr;
    usize capacity_ = 0;
    usize count_ = 0;
    u32 free_head_ = 0; // Free list head (index)
};

} // namespace cap
