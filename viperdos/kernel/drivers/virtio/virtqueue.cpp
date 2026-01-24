#include "virtqueue.hpp"
#include "../../console/serial.hpp"
#include "../../mm/pmm.hpp"

/**
 * @file virtqueue.cpp
 * @brief Virtqueue allocation, setup, and polling implementation.
 *
 * @details
 * This file implements the `Virtqueue` helper for both legacy and modern
 * virtio-mmio devices.
 *
 * Memory allocation:
 * - Legacy devices expect a single contiguous vring region; the code allocates
 *   enough pages to cover descriptor/avail/used structures and computes offsets
 *   within that region.
 * - Modern devices accept separate physical addresses for descriptor, avail,
 *   and used regions; the code allocates each component independently.
 *
 * A simple descriptor free list is built by chaining descriptor `next` fields.
 */
namespace virtio {

// Calculate legacy vring size (contiguous layout)
/**
 * @brief Compute total bytes required for a legacy contiguous vring.
 *
 * @details
 * Legacy virtio-mmio places the descriptor table, avail ring, and used ring in
 * one contiguous memory region with alignment requirements for the used ring.
 *
 * @param num Number of descriptors.
 * @param align Alignment requirement for the used ring.
 * @return Total bytes to allocate.
 */
static usize vring_size(u32 num, usize align) {
    // Descriptor table + available ring (with padding) + used ring
    usize desc_size = num * sizeof(VringDesc);
    usize avail_size = sizeof(VringAvail) + num * sizeof(u16) + sizeof(u16);
    usize used_size = sizeof(VringUsed) + num * sizeof(VringUsedElem) + sizeof(u16);

    // Align the used ring
    usize size = desc_size + avail_size;
    size = (size + align - 1) & ~(align - 1);
    size += used_size;

    return size;
}

// =============================================================================
// Virtqueue Initialization Helpers
// =============================================================================

bool Virtqueue::init_legacy_vring() {
    constexpr usize VRING_ALIGN = 4096;
    usize total_size = vring_size(size_, VRING_ALIGN);
    usize total_pages = (total_size + pmm::PAGE_SIZE - 1) / pmm::PAGE_SIZE;
    legacy_alloc_pages_ = total_pages;

    desc_phys_ = pmm::alloc_pages(total_pages);
    if (!desc_phys_) {
        serial::puts("[virtqueue] Failed to allocate vring\n");
        return false;
    }

    // Zero entire region
    u8 *vring_mem = reinterpret_cast<u8 *>(pmm::phys_to_virt(desc_phys_));
    for (usize i = 0; i < total_pages * pmm::PAGE_SIZE; i++) {
        vring_mem[i] = 0;
    }

    // Set up pointers within the contiguous region
    desc_ = reinterpret_cast<VringDesc *>(vring_mem);

    usize avail_offset = size_ * sizeof(VringDesc);
    avail_ = reinterpret_cast<VringAvail *>(vring_mem + avail_offset);
    avail_phys_ = desc_phys_ + avail_offset;

    usize used_offset = avail_offset + sizeof(VringAvail) + size_ * sizeof(u16) + sizeof(u16);
    used_offset = (used_offset + VRING_ALIGN - 1) & ~(VRING_ALIGN - 1);
    used_ = reinterpret_cast<VringUsed *>(vring_mem + used_offset);
    used_phys_ = desc_phys_ + used_offset;

    // Set guest page size (required for legacy virtio-mmio)
    dev_->write32(reg::GUEST_PAGE_SIZE, 4096);

    // Set queue size and page frame number
    dev_->write32(reg::QUEUE_NUM, size_);
    dev_->write32(reg::QUEUE_ALIGN, VRING_ALIGN);
    dev_->write32(reg::QUEUE_PFN, desc_phys_ >> 12);

    return true;
}

bool Virtqueue::init_modern_vring() {
    // Allocate descriptor table (16 bytes per entry, page aligned)
    usize desc_bytes = size_ * sizeof(VringDesc);
    usize desc_pages = (desc_bytes + pmm::PAGE_SIZE - 1) / pmm::PAGE_SIZE;
    desc_phys_ = pmm::alloc_pages(desc_pages);
    if (!desc_phys_) {
        serial::puts("[virtqueue] Failed to allocate descriptor table\n");
        return false;
    }
    desc_ = reinterpret_cast<VringDesc *>(pmm::phys_to_virt(desc_phys_));

    // Zero descriptor table
    for (usize i = 0; i < desc_pages * pmm::PAGE_SIZE / sizeof(u64); i++) {
        reinterpret_cast<u64 *>(desc_)[i] = 0;
    }

    // Allocate available ring (6 + 2*size bytes, page aligned)
    usize avail_bytes = sizeof(VringAvail) + size_ * sizeof(u16) + sizeof(u16);
    usize avail_pages = (avail_bytes + pmm::PAGE_SIZE - 1) / pmm::PAGE_SIZE;
    avail_phys_ = pmm::alloc_pages(avail_pages);
    if (!avail_phys_) {
        pmm::free_pages(desc_phys_, desc_pages);
        serial::puts("[virtqueue] Failed to allocate available ring\n");
        return false;
    }
    avail_ = reinterpret_cast<VringAvail *>(pmm::phys_to_virt(avail_phys_));

    // Zero available ring
    for (usize i = 0; i < avail_pages * pmm::PAGE_SIZE / sizeof(u64); i++) {
        reinterpret_cast<u64 *>(avail_)[i] = 0;
    }

    // Allocate used ring (6 + 8*size bytes, page aligned)
    usize used_bytes = sizeof(VringUsed) + size_ * sizeof(VringUsedElem) + sizeof(u16);
    usize used_pages = (used_bytes + pmm::PAGE_SIZE - 1) / pmm::PAGE_SIZE;
    used_phys_ = pmm::alloc_pages(used_pages);
    if (!used_phys_) {
        pmm::free_pages(desc_phys_, desc_pages);
        pmm::free_pages(avail_phys_, avail_pages);
        serial::puts("[virtqueue] Failed to allocate used ring\n");
        return false;
    }
    used_ = reinterpret_cast<VringUsed *>(pmm::phys_to_virt(used_phys_));

    // Zero used ring
    for (usize i = 0; i < used_pages * pmm::PAGE_SIZE / sizeof(u64); i++) {
        reinterpret_cast<u64 *>(used_)[i] = 0;
    }

    // Set queue size
    dev_->write32(reg::QUEUE_NUM, size_);

    // Set queue addresses
    dev_->write32(reg::QUEUE_DESC_LOW, desc_phys_ & 0xFFFFFFFF);
    dev_->write32(reg::QUEUE_DESC_HIGH, desc_phys_ >> 32);
    dev_->write32(reg::QUEUE_AVAIL_LOW, avail_phys_ & 0xFFFFFFFF);
    dev_->write32(reg::QUEUE_AVAIL_HIGH, avail_phys_ >> 32);
    dev_->write32(reg::QUEUE_USED_LOW, used_phys_ & 0xFFFFFFFF);
    dev_->write32(reg::QUEUE_USED_HIGH, used_phys_ >> 32);

    // Enable queue
    dev_->write32(reg::QUEUE_READY, 1);

    return true;
}

void Virtqueue::init_free_list() {
    for (u32 i = 0; i < size_ - 1; i++) {
        desc_[i].next = i + 1;
        desc_[i].flags = desc_flags::NEXT;
    }
    desc_[size_ - 1].next = 0xFFFF; // End of list
    desc_[size_ - 1].flags = 0;
    free_head_ = 0;
    num_free_ = size_;
}

// =============================================================================
// Virtqueue Main Initialization
// =============================================================================

/** @copydoc virtio::Virtqueue::init */
bool Virtqueue::init(Device *dev, u32 queue_idx, u32 queue_size) {
    dev_ = dev;
    queue_idx_ = queue_idx;
    legacy_ = dev->is_legacy();

    // Select this queue
    dev->write32(reg::QUEUE_SEL, queue_idx);

    // Check queue isn't already in use
    if (legacy_) {
        if (dev->read32(reg::QUEUE_PFN) != 0) {
            serial::puts("[virtqueue] Queue ");
            serial::put_dec(queue_idx);
            serial::puts(" already in use\n");
            return false;
        }
    } else {
        if (dev->read32(reg::QUEUE_READY)) {
            serial::puts("[virtqueue] Queue ");
            serial::put_dec(queue_idx);
            serial::puts(" already in use\n");
            return false;
        }
    }

    // Get max queue size
    u32 max_size = dev->read32(reg::QUEUE_NUM_MAX);
    if (max_size == 0) {
        serial::puts("[virtqueue] Queue ");
        serial::put_dec(queue_idx);
        serial::puts(" not available\n");
        return false;
    }

    // Use requested size or max, whichever is smaller
    if (queue_size > max_size || queue_size == 0) {
        queue_size = max_size;
    }
    size_ = queue_size;

    // Allocate vring based on device mode
    if (legacy_) {
        if (!init_legacy_vring())
            return false;
    } else {
        if (!init_modern_vring())
            return false;
    }

    init_free_list();

    serial::puts("[virtqueue] Initialized queue ");
    serial::put_dec(queue_idx);
    serial::puts(" with ");
    serial::put_dec(size_);
    serial::puts(" descriptors");
    if (legacy_) {
        serial::puts(" (legacy)");
    }
    serial::puts("\n");

    return true;
}

/** @copydoc virtio::Virtqueue::destroy */
void Virtqueue::destroy() {
    if (!dev_)
        return;

    // Disable queue
    dev_->write32(reg::QUEUE_SEL, queue_idx_);
    dev_->write32(reg::QUEUE_READY, 0);

    // Free memory - handle legacy vs modern mode differently
    if (legacy_) {
        // Legacy mode: all three pointers are within a single contiguous allocation
        // Only free desc_phys_ which is the base of the allocation
        if (desc_phys_) {
            pmm::free_pages(desc_phys_, legacy_alloc_pages_);
        }
    } else {
        // Modern mode: separate allocations for each ring
        if (desc_phys_) {
            usize desc_pages = (size_ * sizeof(VringDesc) + pmm::PAGE_SIZE - 1) / pmm::PAGE_SIZE;
            pmm::free_pages(desc_phys_, desc_pages);
        }
        if (avail_phys_) {
            usize avail_bytes = sizeof(VringAvail) + size_ * sizeof(u16) + sizeof(u16);
            pmm::free_pages(avail_phys_, (avail_bytes + pmm::PAGE_SIZE - 1) / pmm::PAGE_SIZE);
        }
        if (used_phys_) {
            usize used_bytes = sizeof(VringUsed) + size_ * sizeof(VringUsedElem) + sizeof(u16);
            pmm::free_pages(used_phys_, (used_bytes + pmm::PAGE_SIZE - 1) / pmm::PAGE_SIZE);
        }
    }

    desc_phys_ = 0;
    avail_phys_ = 0;
    used_phys_ = 0;
    dev_ = nullptr;
}

/** @copydoc virtio::Virtqueue::alloc_desc */
i32 Virtqueue::alloc_desc() {
    if (num_free_ == 0) {
        return -1;
    }

    u32 idx = free_head_;
    free_head_ = desc_[idx].next;
    num_free_--;

    // Clear the descriptor
    desc_[idx].addr = 0;
    desc_[idx].len = 0;
    desc_[idx].flags = 0;
    desc_[idx].next = 0;

    return idx;
}

/** @copydoc virtio::Virtqueue::free_desc */
void Virtqueue::free_desc(u32 idx) {
    if (idx >= size_)
        return;

    desc_[idx].next = free_head_;
    desc_[idx].flags = desc_flags::NEXT;
    free_head_ = idx;
    num_free_++;
}

/** @copydoc virtio::Virtqueue::free_chain */
void Virtqueue::free_chain(u32 head) {
    u32 idx = head;
    while (true) {
        u16 flags = desc_[idx].flags;
        u16 next = desc_[idx].next;

        free_desc(idx);

        if (!(flags & desc_flags::NEXT)) {
            break;
        }
        idx = next;
    }
}

/** @copydoc virtio::Virtqueue::set_desc */
void Virtqueue::set_desc(u32 idx, u64 addr, u32 len, u16 flags) {
    if (idx >= size_)
        return;

    desc_[idx].addr = addr;
    desc_[idx].len = len;
    desc_[idx].flags = flags;
}

/** @copydoc virtio::Virtqueue::chain_desc */
void Virtqueue::chain_desc(u32 idx, u32 next_idx) {
    if (idx >= size_ || next_idx >= size_)
        return;

    desc_[idx].next = next_idx;
    desc_[idx].flags |= desc_flags::NEXT;
}

/** @copydoc virtio::Virtqueue::submit */
void Virtqueue::submit(u32 head) {
    u16 avail_idx = avail_->idx;
    avail_->ring[avail_idx % size_] = head;

    // Memory barrier to ensure descriptor writes are visible
    asm volatile("dmb sy" ::: "memory");

    avail_->idx = avail_idx + 1;
}

/** @copydoc virtio::Virtqueue::kick */
void Virtqueue::kick() {
    // Memory barrier before notify
    asm volatile("dmb sy" ::: "memory");

    dev_->write32(reg::QUEUE_NOTIFY, queue_idx_);
}

/** @copydoc virtio::Virtqueue::poll_used */
i32 Virtqueue::poll_used() {
    // Memory barrier to ensure we see latest used index
    asm volatile("dmb sy" ::: "memory");

    if (last_used_idx_ == used_->idx) {
        return -1; // No new completions
    }

    u32 ring_idx = last_used_idx_ % size_;
    u32 head = used_->ring[ring_idx].id;
    last_used_len_ = used_->ring[ring_idx].len;
    last_used_idx_++;

    return head;
}

/** @copydoc virtio::Virtqueue::get_used_len */
u32 Virtqueue::get_used_len(u32 /* idx */) {
    // Returns the length from the last poll_used() call
    return last_used_len_;
}

} // namespace virtio
