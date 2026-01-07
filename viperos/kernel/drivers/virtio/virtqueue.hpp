//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/drivers/virtio/virtqueue.hpp
// Purpose: Virtqueue (vring) structures and management class.
// Key invariants: Ring memory contiguous; free list tracks available descriptors.
// Ownership/Lifetime: Owned by parent virtio device.
// Links: kernel/drivers/virtio/virtqueue.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../../include/types.hpp"
#include "virtio.hpp"

/**
 * @file virtqueue.hpp
 * @brief Virtqueue (vring) structures and management class.
 *
 * @details
 * Virtio devices communicate with drivers using virtqueues ("vrings"):
 * - A descriptor table describing buffers.
 * - An available ring where the driver publishes descriptor chain heads.
 * - A used ring where the device reports completed descriptor chains.
 *
 * This header defines the on-memory ring structures and a `Virtqueue` helper
 * class that:
 * - Allocates and initializes ring memory for legacy and modern virtio devices.
 * - Maintains a simple free list of descriptors for building request chains.
 * - Provides methods to submit work, notify ("kick") the device, and poll for
 *   completions.
 */
namespace virtio
{

// Descriptor flags
/**
 * @brief Flags for vring descriptors.
 *
 * @details
 * These flags are written into `VringDesc::flags` and describe chaining and
 * directionality.
 */
namespace desc_flags
{
constexpr u16 NEXT = 1;     // Buffer continues via next field
constexpr u16 WRITE = 2;    // Device writes (vs reads)
constexpr u16 INDIRECT = 4; // Data is list of buffer descriptors
} // namespace desc_flags

// Virtqueue descriptor
/**
 * @brief One vring descriptor entry.
 *
 * @details
 * Descriptors describe a buffer by physical address and length. Descriptor
 * chains are built by setting NEXT and using the `next` field.
 */
struct VringDesc
{
    u64 addr;  // Physical address of buffer
    u32 len;   // Length of buffer
    u16 flags; // NEXT, WRITE, INDIRECT
    u16 next;  // Next descriptor if NEXT flag set
};

// Available ring header
/**
 * @brief Available ring header.
 *
 * @details
 * The `ring[]` array contains descriptor chain heads (indices into the
 * descriptor table). The driver increments `idx` after publishing new entries.
 */
struct VringAvail
{
    u16 flags;
    u16 idx;
    u16 ring[]; // Array of descriptor chain heads
    // Followed by: u16 used_event (if VIRTIO_F_EVENT_IDX)
};

// Used ring element
/**
 * @brief One used-ring element reported by the device.
 *
 * @details
 * `id` identifies the head descriptor index of a completed chain and `len`
 * provides the number of bytes written/used by the device.
 */
struct VringUsedElem
{
    u32 id;  // Descriptor chain head
    u32 len; // Bytes written by device
};

// Used ring header
/**
 * @brief Used ring header.
 *
 * @details
 * The device increments `idx` when it posts new used elements.
 */
struct VringUsed
{
    u16 flags;
    u16 idx;
    VringUsedElem ring[];
    // Followed by: u16 avail_event (if VIRTIO_F_EVENT_IDX)
};

// Virtqueue management
/**
 * @brief Helper for managing a virtqueue's rings and descriptor allocation.
 *
 * @details
 * This class supports both legacy and modern virtio-mmio devices. It allocates
 * vring memory from the PMM, initializes the device queue registers, and keeps
 * a simple descriptor free list so drivers can build descriptor chains.
 *
 * Completion handling is polling-based: drivers call @ref poll_used to check
 * whether the device has produced any used-ring entries.
 */
class Virtqueue
{
  public:
    // Initialize virtqueue for a device
    /**
     * @brief Initialize a virtqueue for a device and queue index.
     *
     * @details
     * Selects the queue, determines maximum size, allocates ring memory, and
     * programs either legacy or modern queue registers depending on the device
     * mode.
     *
     * @param dev Device owning the queue.
     * @param queue_idx Queue index (0-based).
     * @param queue_size Requested descriptor count (0 means use device max).
     * @return `true` on success, otherwise `false`.
     */
    bool init(Device *dev, u32 queue_idx, u32 queue_size);

    // Cleanup
    /**
     * @brief Disable and free resources associated with the queue.
     *
     * @details
     * Clears QUEUE_READY and frees ring memory allocations. Callers should
     * ensure the device is quiesced before destroying the queue.
     */
    void destroy();

    // Allocate a descriptor from free list
    // Returns descriptor index, or -1 if none available
    /**
     * @brief Allocate one descriptor index from the free list.
     *
     * @return Descriptor index, or -1 if none available.
     */
    i32 alloc_desc();

    // Free a descriptor back to free list
    /**
     * @brief Return a descriptor to the free list.
     *
     * @param idx Descriptor index.
     */
    void free_desc(u32 idx);

    // Free an entire descriptor chain
    /**
     * @brief Free a chain of descriptors starting at `head`.
     *
     * @details
     * Walks NEXT links until the end of the chain and returns each descriptor
     * to the free list.
     *
     * @param head Head descriptor index.
     */
    void free_chain(u32 head);

    // Configure a descriptor
    /**
     * @brief Set descriptor fields for a buffer.
     *
     * @param idx Descriptor index.
     * @param addr Physical address of the buffer.
     * @param len Length of the buffer in bytes.
     * @param flags Descriptor flags (see @ref desc_flags).
     */
    void set_desc(u32 idx, u64 addr, u32 len, u16 flags);

    // Chain this descriptor to next
    /**
     * @brief Link one descriptor to another to form a chain.
     *
     * @param idx Current descriptor.
     * @param next_idx Next descriptor.
     */
    void chain_desc(u32 idx, u32 next_idx);

    // Submit a descriptor chain to the available ring
    /**
     * @brief Publish a descriptor chain head to the available ring.
     *
     * @details
     * Writes `head` into the avail ring and increments the avail index. Drivers
     * typically call @ref kick afterwards to notify the device.
     *
     * @param head Head descriptor index.
     */
    void submit(u32 head);

    // Notify the device about new available descriptors
    /**
     * @brief Notify the device that new descriptors are available.
     *
     * @details
     * For virtio-mmio, this writes the queue index to QUEUE_NOTIFY.
     */
    void kick();

    // Poll for completed descriptors
    // Returns head of completed chain, or -1 if none ready
    /**
     * @brief Poll the used ring for completed descriptor chains.
     *
     * @details
     * Compares the device's used index with the last observed used index. When
     * a new used element is available, returns its head descriptor index and
     * stores the associated used length.
     *
     * @return Head descriptor index of a completed chain, or -1 if none ready.
     */
    i32 poll_used();

    // Get length written by device for a used element
    /**
     * @brief Get the byte length associated with the most recent completion.
     *
     * @details
     * Returns the length recorded from the last @ref poll_used call.
     *
     * @param idx Unused parameter retained for API symmetry.
     * @return Number of bytes reported by the device for the last completion.
     */
    u32 get_used_len(u32 idx);

    // Queue properties
    /** @brief Number of descriptors in the queue. */
    u32 size() const
    {
        return size_;
    }

    /** @brief Number of currently free descriptors. */
    u32 num_free() const
    {
        return num_free_;
    }

    /** @brief Current avail ring index value. */
    u16 avail_idx() const
    {
        return avail_->idx;
    }

    /** @brief Current used ring index value. */
    u16 used_idx() const
    {
        return used_->idx;
    }

    /** @brief Last used index observed by @ref poll_used. */
    u16 last_used() const
    {
        return last_used_idx_;
    }

  private:
    Device *dev_{nullptr};
    u32 queue_idx_{0};
    u32 size_{0};
    bool legacy_{false};

    // Descriptor table
    VringDesc *desc_{nullptr};
    u64 desc_phys_{0};

    // Available ring
    VringAvail *avail_{nullptr};
    u64 avail_phys_{0};

    // Used ring
    VringUsed *used_{nullptr};
    u64 used_phys_{0};

    // Free list management
    u16 free_head_{0};
    u16 num_free_{0};

    // Last seen used index
    u16 last_used_idx_{0};

    // Length of last retrieved used element
    u32 last_used_len_{0};

    // Legacy mode allocation size (pages) - only valid when legacy_ is true
    // In legacy mode, desc_phys_/avail_phys_/used_phys_ all point into one allocation
    usize legacy_alloc_pages_{0};
};

} // namespace virtio
