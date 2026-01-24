/**
 * @file virtqueue.cpp
 * @brief User-space Virtqueue allocation, setup, and polling implementation.
 *
 * @details
 * This file implements the user-space `Virtqueue` helper for both legacy and
 * modern virtio-mmio devices.
 *
 * Memory allocation uses the DMA allocation syscall to obtain physically
 * contiguous memory suitable for virtqueue rings.
 */
#include "../include/virtqueue.hpp"

namespace virtio {

// Calculate legacy vring size (contiguous layout)
static usize vring_size(u32 num, usize align) {
    usize desc_size = num * sizeof(VringDesc);
    usize avail_size = sizeof(VringAvail) + num * sizeof(u16) + sizeof(u16);
    usize used_size = sizeof(VringUsed) + num * sizeof(VringUsedElem) + sizeof(u16);

    // Align the used ring
    usize size = desc_size + avail_size;
    size = (size + align - 1) & ~(align - 1);
    size += used_size;

    return size;
}

bool Virtqueue::init(Device *dev, u32 queue_idx, u32 queue_size) {
    dev_ = dev;
    queue_idx_ = queue_idx;
    legacy_ = dev->is_legacy();

    // Select this queue
    dev->write32(reg::QUEUE_SEL, queue_idx);

    // Check queue isn't already in use
    if (legacy_) {
        if (dev->read32(reg::QUEUE_PFN) != 0) {
            return false;
        }
    } else {
        if (dev->read32(reg::QUEUE_READY)) {
            return false;
        }
    }

    // Get max queue size
    u32 max_size = dev->read32(reg::QUEUE_NUM_MAX);
    if (max_size == 0) {
        return false;
    }

    // Use requested size or max, whichever is smaller
    if (queue_size > max_size || queue_size == 0) {
        queue_size = max_size;
    }
    size_ = queue_size;

    if (legacy_) {
        // Legacy mode: allocate contiguous vring using DMA
        constexpr usize VRING_ALIGN = 4096;
        usize total_size = vring_size(size_, VRING_ALIGN);
        usize alloc_size = (total_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

        device::DmaBuffer buf;
        if (device::dma_alloc(alloc_size, &buf) != 0) {
            return false;
        }

        desc_phys_ = buf.phys_addr;
        desc_virt_ = buf.virt_addr;

        // Zero entire region
        u8 *vring_mem = reinterpret_cast<u8 *>(desc_virt_);
        for (usize i = 0; i < alloc_size; i++) {
            vring_mem[i] = 0;
        }

        // Set up pointers within the contiguous region
        desc_ = reinterpret_cast<VringDesc *>(vring_mem);

        usize avail_offset = size_ * sizeof(VringDesc);
        avail_ = reinterpret_cast<VringAvail *>(vring_mem + avail_offset);
        avail_phys_ = desc_phys_ + avail_offset;
        avail_virt_ = desc_virt_ + avail_offset;

        usize used_offset = avail_offset + sizeof(VringAvail) + size_ * sizeof(u16) + sizeof(u16);
        used_offset = (used_offset + VRING_ALIGN - 1) & ~(VRING_ALIGN - 1);
        used_ = reinterpret_cast<VringUsed *>(vring_mem + used_offset);
        used_phys_ = desc_phys_ + used_offset;
        used_virt_ = desc_virt_ + used_offset;

        // Set guest page size (required for legacy virtio-mmio)
        dev->write32(reg::GUEST_PAGE_SIZE, 4096);

        // Set queue size and page frame number
        dev->write32(reg::QUEUE_NUM, size_);
        dev->write32(reg::QUEUE_ALIGN, VRING_ALIGN);
        dev->write32(reg::QUEUE_PFN, desc_phys_ >> 12);
    } else {
        // Modern mode: separate allocations

        // Allocate descriptor table
        usize desc_bytes = size_ * sizeof(VringDesc);
        usize desc_alloc = (desc_bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

        device::DmaBuffer desc_buf;
        if (device::dma_alloc(desc_alloc, &desc_buf) != 0) {
            return false;
        }
        desc_phys_ = desc_buf.phys_addr;
        desc_virt_ = desc_buf.virt_addr;
        desc_ = reinterpret_cast<VringDesc *>(desc_virt_);

        // Zero descriptor table
        for (usize i = 0; i < desc_alloc; i++) {
            reinterpret_cast<u8 *>(desc_)[i] = 0;
        }

        // Allocate available ring
        usize avail_bytes = sizeof(VringAvail) + size_ * sizeof(u16) + sizeof(u16);
        usize avail_alloc = (avail_bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

        device::DmaBuffer avail_buf;
        if (device::dma_alloc(avail_alloc, &avail_buf) != 0) {
            device::dma_free(desc_virt_);
            return false;
        }
        avail_phys_ = avail_buf.phys_addr;
        avail_virt_ = avail_buf.virt_addr;
        avail_ = reinterpret_cast<VringAvail *>(avail_virt_);

        // Zero available ring
        for (usize i = 0; i < avail_alloc; i++) {
            reinterpret_cast<u8 *>(avail_)[i] = 0;
        }

        // Allocate used ring
        usize used_bytes = sizeof(VringUsed) + size_ * sizeof(VringUsedElem) + sizeof(u16);
        usize used_alloc = (used_bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

        device::DmaBuffer used_buf;
        if (device::dma_alloc(used_alloc, &used_buf) != 0) {
            device::dma_free(desc_virt_);
            device::dma_free(avail_virt_);
            return false;
        }
        used_phys_ = used_buf.phys_addr;
        used_virt_ = used_buf.virt_addr;
        used_ = reinterpret_cast<VringUsed *>(used_virt_);

        // Zero used ring
        for (usize i = 0; i < used_alloc; i++) {
            reinterpret_cast<u8 *>(used_)[i] = 0;
        }

        // Set queue size
        dev->write32(reg::QUEUE_NUM, size_);

        // Set queue addresses
        dev->write32(reg::QUEUE_DESC_LOW, desc_phys_ & 0xFFFFFFFF);
        dev->write32(reg::QUEUE_DESC_HIGH, desc_phys_ >> 32);
        dev->write32(reg::QUEUE_AVAIL_LOW, avail_phys_ & 0xFFFFFFFF);
        dev->write32(reg::QUEUE_AVAIL_HIGH, avail_phys_ >> 32);
        dev->write32(reg::QUEUE_USED_LOW, used_phys_ & 0xFFFFFFFF);
        dev->write32(reg::QUEUE_USED_HIGH, used_phys_ >> 32);

        // Enable queue
        dev->write32(reg::QUEUE_READY, 1);
    }

    // Initialize free list (all descriptors chained)
    for (u32 i = 0; i < size_ - 1; i++) {
        desc_[i].next = i + 1;
        desc_[i].flags = desc_flags::NEXT;
    }
    desc_[size_ - 1].next = 0xFFFF;
    desc_[size_ - 1].flags = 0;
    free_head_ = 0;
    num_free_ = size_;

    return true;
}

void Virtqueue::destroy() {
    if (!dev_) {
        return;
    }

    // Disable queue
    dev_->write32(reg::QUEUE_SEL, queue_idx_);
    dev_->write32(reg::QUEUE_READY, 0);

    // Free DMA memory
    if (desc_virt_) {
        device::dma_free(desc_virt_);
    }
    if (!legacy_) {
        if (avail_virt_) {
            device::dma_free(avail_virt_);
        }
        if (used_virt_) {
            device::dma_free(used_virt_);
        }
    }

    dev_ = nullptr;
    desc_ = nullptr;
    avail_ = nullptr;
    used_ = nullptr;
}

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

void Virtqueue::free_desc(u32 idx) {
    if (idx >= size_) {
        return;
    }

    desc_[idx].next = free_head_;
    desc_[idx].flags = desc_flags::NEXT;
    free_head_ = idx;
    num_free_++;
}

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

void Virtqueue::set_desc(u32 idx, u64 addr, u32 len, u16 flags) {
    if (idx >= size_) {
        return;
    }

    desc_[idx].addr = addr;
    desc_[idx].len = len;
    desc_[idx].flags = flags;
}

void Virtqueue::chain_desc(u32 idx, u32 next_idx) {
    if (idx >= size_ || next_idx >= size_) {
        return;
    }

    desc_[idx].next = next_idx;
    desc_[idx].flags |= desc_flags::NEXT;
}

void Virtqueue::submit(u32 head) {
    u16 avail_idx = avail_->idx;
    avail_->ring[avail_idx % size_] = head;

    // Memory barrier to ensure descriptor writes are visible
    asm volatile("dmb sy" ::: "memory");

    avail_->idx = avail_idx + 1;
}

void Virtqueue::kick() {
    // Memory barrier before notify
    asm volatile("dmb sy" ::: "memory");

    dev_->write32(reg::QUEUE_NOTIFY, queue_idx_);
}

i32 Virtqueue::poll_used() {
    // Memory barrier to ensure we see latest used index
    asm volatile("dmb sy" ::: "memory");

    if (last_used_idx_ == used_->idx) {
        return -1;
    }

    u32 ring_idx = last_used_idx_ % size_;
    u32 head = used_->ring[ring_idx].id;
    last_used_len_ = used_->ring[ring_idx].len;
    last_used_idx_++;

    return head;
}

u32 Virtqueue::get_used_len(u32 /* idx */) {
    return last_used_len_;
}

} // namespace virtio
