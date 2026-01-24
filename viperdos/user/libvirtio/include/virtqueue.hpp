/**
 * @file virtqueue.hpp
 * @brief User-space Virtqueue (vring) structures and management class.
 *
 * @details
 * Virtio devices communicate with drivers using virtqueues ("vrings"):
 * - A descriptor table describing buffers.
 * - An available ring where the driver publishes descriptor chain heads.
 * - A used ring where the device reports completed descriptor chains.
 *
 * This user-space implementation uses DMA allocation syscalls to obtain
 * physically contiguous memory for the ring structures.
 */
#pragma once

#include "virtio.hpp"

namespace virtio {

// Descriptor flags
namespace desc_flags {
constexpr u16 NEXT = 1;     // Buffer continues via next field
constexpr u16 WRITE = 2;    // Device writes (vs reads)
constexpr u16 INDIRECT = 4; // Data is list of buffer descriptors
} // namespace desc_flags

// Virtqueue descriptor
struct VringDesc {
    u64 addr;  // Physical address of buffer
    u32 len;   // Length of buffer
    u16 flags; // NEXT, WRITE, INDIRECT
    u16 next;  // Next descriptor if NEXT flag set
};

// Available ring header
struct VringAvail {
    u16 flags;
    u16 idx;
    u16 ring[];
};

// Used ring element
struct VringUsedElem {
    u32 id;  // Descriptor chain head
    u32 len; // Bytes written by device
};

// Used ring header
struct VringUsed {
    u16 flags;
    u16 idx;
    VringUsedElem ring[];
};

/**
 * @brief User-space helper for managing a virtqueue's rings.
 *
 * @details
 * This class supports both legacy and modern virtio-mmio devices. It allocates
 * vring memory using the DMA allocation syscall, initializes the device queue
 * registers, and keeps a simple descriptor free list for building chains.
 */
class Virtqueue {
  public:
    /**
     * @brief Initialize a virtqueue for a device and queue index.
     *
     * @param dev Device owning the queue.
     * @param queue_idx Queue index (0-based).
     * @param queue_size Requested descriptor count (0 means use device max).
     * @return `true` on success.
     */
    bool init(Device *dev, u32 queue_idx, u32 queue_size = 0);

    /**
     * @brief Disable and free resources associated with the queue.
     */
    void destroy();

    /**
     * @brief Allocate one descriptor index from the free list.
     *
     * @return Descriptor index, or -1 if none available.
     */
    i32 alloc_desc();

    /**
     * @brief Return a descriptor to the free list.
     */
    void free_desc(u32 idx);

    /**
     * @brief Free a chain of descriptors starting at `head`.
     */
    void free_chain(u32 head);

    /**
     * @brief Set descriptor fields for a buffer.
     *
     * @param idx Descriptor index.
     * @param addr Physical address of the buffer.
     * @param len Length of the buffer in bytes.
     * @param flags Descriptor flags.
     */
    void set_desc(u32 idx, u64 addr, u32 len, u16 flags);

    /**
     * @brief Link one descriptor to another to form a chain.
     */
    void chain_desc(u32 idx, u32 next_idx);

    /**
     * @brief Publish a descriptor chain head to the available ring.
     */
    void submit(u32 head);

    /**
     * @brief Notify the device that new descriptors are available.
     */
    void kick();

    /**
     * @brief Poll the used ring for completed descriptor chains.
     *
     * @return Head descriptor index of a completed chain, or -1 if none ready.
     */
    i32 poll_used();

    /**
     * @brief Get the byte length associated with the most recent completion.
     */
    u32 get_used_len(u32 idx);

    // Queue properties
    u32 size() const {
        return size_;
    }

    u32 num_free() const {
        return num_free_;
    }

    u16 avail_idx() const {
        return avail_->idx;
    }

    u16 used_idx() const {
        return used_->idx;
    }

    u16 last_used() const {
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
    u64 desc_virt_{0};

    // Available ring
    VringAvail *avail_{nullptr};
    u64 avail_phys_{0};
    u64 avail_virt_{0};

    // Used ring
    VringUsed *used_{nullptr};
    u64 used_phys_{0};
    u64 used_virt_{0};

    // Free list management
    u16 free_head_{0};
    u16 num_free_{0};

    // Last seen used index
    u16 last_used_idx_{0};

    // Length of last retrieved used element
    u32 last_used_len_{0};
};

} // namespace virtio
