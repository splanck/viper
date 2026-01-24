/**
 * @file vma.cpp
 * @brief Virtual Memory Area (VMA) tracking implementation.
 *
 * @details
 * Implements the VMA list manager and demand paging fault handler.
 * VMAs describe valid regions of a process's address space, allowing
 * the page fault handler to allocate pages on demand.
 */

#include "vma.hpp"
#include "../console/serial.hpp"
#include "pmm.hpp"

namespace mm {

void VmaList::init() {
    SpinlockGuard guard(lock_);
    for (usize i = 0; i < MAX_VMAS; i++) {
        used_[i] = false;
        pool_[i].next = nullptr;
    }
    head_ = nullptr;
    count_ = 0;
}

Vma *VmaList::alloc_vma() {
    for (usize i = 0; i < MAX_VMAS; i++) {
        if (!used_[i]) {
            used_[i] = true;
            count_++;
            return &pool_[i];
        }
    }
    serial::puts("[vma] ERROR: VMA pool exhausted\n");
    return nullptr;
}

void VmaList::free_vma(Vma *vma) {
    if (!vma)
        return;

    for (usize i = 0; i < MAX_VMAS; i++) {
        if (&pool_[i] == vma) {
            used_[i] = false;
            count_--;
            return;
        }
    }
}

void VmaList::insert_sorted(Vma *vma) {
    if (!head_ || vma->start < head_->start) {
        // Insert at head
        vma->next = head_;
        head_ = vma;
        return;
    }

    // Find insertion point
    Vma *prev = head_;
    while (prev->next && prev->next->start < vma->start) {
        prev = prev->next;
    }

    vma->next = prev->next;
    prev->next = vma;
}

// Internal unlocked find for use when lock is already held
static Vma *find_unlocked(Vma *head, u64 addr) {
    Vma *vma = head;
    while (vma) {
        if (vma->contains(addr)) {
            return vma;
        }
        // Optimization: if we've passed the address, stop searching
        if (vma->start > addr) {
            break;
        }
        vma = vma->next;
    }
    return nullptr;
}

/**
 * @brief Check if a range [start, end) overlaps with any existing VMA.
 * @note Caller must hold lock_.
 * @return Pointer to overlapping VMA, or nullptr if no overlap.
 */
static Vma *find_overlap_unlocked(Vma *head, u64 start, u64 end) {
    Vma *vma = head;
    while (vma) {
        // Two ranges [a, b) and [c, d) overlap iff: a < d && c < b
        // Here: existing VMA is [vma->start, vma->end), new range is [start, end)
        if (start < vma->end && vma->start < end) {
            return vma; // Found overlap
        }
        // Optimization: if existing VMA starts at or after our end, no more overlaps possible
        // (since list is sorted by start address)
        if (vma->start >= end) {
            break;
        }
        vma = vma->next;
    }
    return nullptr;
}

Vma *VmaList::find(u64 addr) {
    SpinlockGuard guard(lock_);
    return find_unlocked(head_, addr);
}

Vma *VmaList::find_locked(u64 addr) {
    // Caller must hold lock_
    return find_unlocked(head_, addr);
}

const Vma *VmaList::find(u64 addr) const {
    SpinlockGuard guard(lock_);
    return find_unlocked(head_, addr);
}

Vma *VmaList::add(u64 start, u64 end, u32 prot, VmaType type) {
    // Validate alignment (no lock needed for validation)
    if ((start & 0xFFF) != 0 || (end & 0xFFF) != 0) {
        serial::puts("[vma] ERROR: Addresses must be page-aligned\n");
        return nullptr;
    }

    if (start >= end) {
        serial::puts("[vma] ERROR: Invalid VMA range\n");
        return nullptr;
    }

    SpinlockGuard guard(lock_);

    // Check for overlaps with any existing VMA in the range [start, end)
    if (find_overlap_unlocked(head_, start, end) != nullptr) {
        serial::puts("[vma] ERROR: VMA overlaps existing region\n");
        return nullptr;
    }

    Vma *vma = alloc_vma();
    if (!vma) {
        return nullptr;
    }

    vma->start = start;
    vma->end = end;
    vma->prot = prot;
    vma->type = type;
    vma->file_inode = 0;
    vma->file_offset = 0;
    vma->next = nullptr;

    insert_sorted(vma);

    return vma;
}

Vma *VmaList::add_file(u64 start, u64 end, u32 prot, u64 inode, u64 offset) {
    Vma *vma = add(start, end, prot, VmaType::FILE);
    if (vma) {
        vma->file_inode = inode;
        vma->file_offset = offset;
    }
    return vma;
}

bool VmaList::remove(Vma *target) {
    SpinlockGuard guard(lock_);

    if (!head_ || !target) {
        return false;
    }

    if (head_ == target) {
        head_ = target->next;
        free_vma(target);
        return true;
    }

    Vma *prev = head_;
    while (prev->next && prev->next != target) {
        prev = prev->next;
    }

    if (prev->next == target) {
        prev->next = target->next;
        free_vma(target);
        return true;
    }

    return false;
}

void VmaList::remove_range(u64 start, u64 end) {
    SpinlockGuard guard(lock_);

    Vma *vma = head_;
    Vma *prev = nullptr;

    while (vma) {
        // Check if VMA overlaps with range
        bool overlaps = !(vma->end <= start || vma->start >= end);

        if (overlaps) {
            Vma *next = vma->next;

            if (prev) {
                prev->next = next;
            } else {
                head_ = next;
            }

            free_vma(vma);
            vma = next;
        } else {
            prev = vma;
            vma = vma->next;
        }
    }
}

void VmaList::clear() {
    SpinlockGuard guard(lock_);

    head_ = nullptr;
    for (usize i = 0; i < MAX_VMAS; i++) {
        used_[i] = false;
    }
    count_ = 0;
}

/**
 * @brief Handle a demand page fault by allocating and mapping a page.
 *
 * @details
 * Fixes RC-007 and RC-008: Holds VMA lock during lookup and iteration to
 * prevent TOCTOU races. Copies VMA properties before releasing lock to
 * avoid holding lock across pmm/map operations (which have their own locks).
 */
FaultResult handle_demand_fault(VmaList *vma_list,
                                u64 fault_addr,
                                bool is_write,
                                bool (*map_callback)(u64 virt, u64 phys, u32 prot)) {
    if (!vma_list || !map_callback) {
        return FaultResult::UNHANDLED;
    }

    // Page-align the fault address
    u64 page_addr = fault_addr & ~0xFFFULL;

    // Acquire VMA lock for the entire lookup phase
    u64 saved_daif = vma_list->acquire_lock();

    // Find the VMA containing this address (under lock)
    Vma *vma = vma_list->find_locked(fault_addr);
    if (!vma) {
        // Check if this is a potential stack growth
        // Stack grows downward, so check if address is just below a stack VMA
        Vma *v = vma_list->head_locked();
        while (v) {
            if (v->type == VmaType::STACK) {
                // Stack growth: allow faults within one page of the stack bottom
                if (fault_addr >= v->start - 4096 && fault_addr < v->start) {
                    // Check stack size limit (vma->end is fixed stack top, vma->start grows down)
                    u64 new_stack_size = v->end - page_addr;
                    if (new_stack_size > MAX_STACK_SIZE) {
                        vma_list->release_lock(saved_daif);
                        serial::puts("[vma] ERROR: Stack growth limit exceeded (");
                        serial::put_dec(new_stack_size / 1024);
                        serial::puts(" KB > ");
                        serial::put_dec(MAX_STACK_SIZE / 1024);
                        serial::puts(" KB)\n");
                        return FaultResult::UNHANDLED;
                    }

                    serial::puts("[vma] Growing stack from ");
                    serial::put_hex(v->start);
                    serial::puts(" to ");
                    serial::put_hex(page_addr);
                    serial::puts("\n");

                    // Extend the VMA (under lock)
                    v->start = page_addr;

                    // Copy VMA properties before releasing lock
                    u32 stack_prot = v->prot;
                    vma_list->release_lock(saved_daif);

                    // Allocate and map the new stack page (outside lock)
                    u64 phys = pmm::alloc_page();
                    if (phys == 0) {
                        serial::puts("[vma] ERROR: Failed to allocate stack page\n");
                        return FaultResult::ERROR;
                    }

                    // Zero the page (convert physical to virtual address)
                    u8 *ptr = reinterpret_cast<u8 *>(pmm::phys_to_virt(phys));
                    for (usize i = 0; i < 4096; i++) {
                        ptr[i] = 0;
                    }

                    if (!map_callback(page_addr, phys, stack_prot)) {
                        pmm::free_page(phys);
                        return FaultResult::ERROR;
                    }

                    return FaultResult::STACK_GROW;
                }
            }
            v = v->next;
        }

        vma_list->release_lock(saved_daif);
        return FaultResult::UNHANDLED;
    }

    // Check access permissions (under lock)
    if (vma->type == VmaType::GUARD) {
        vma_list->release_lock(saved_daif);
        serial::puts("[vma] Access to guard page\n");
        return FaultResult::UNHANDLED;
    }

    if (is_write && !(vma->prot & vma_prot::WRITE)) {
        vma_list->release_lock(saved_daif);
        serial::puts("[vma] Write to read-only region\n");
        return FaultResult::UNHANDLED;
    }

    // Copy VMA properties before releasing lock to avoid TOCTOU
    u32 vma_prot_copy = vma->prot;
    VmaType vma_type_copy = vma->type;
    vma_list->release_lock(saved_daif);

    // Allocate a physical page (outside lock to avoid lock ordering issues)
    u64 phys = pmm::alloc_page();
    if (phys == 0) {
        serial::puts("[vma] ERROR: Failed to allocate page\n");
        return FaultResult::ERROR;
    }

    // Initialize the page based on VMA type (convert physical to virtual address)
    u8 *ptr = reinterpret_cast<u8 *>(pmm::phys_to_virt(phys));

    switch (vma_type_copy) {
        case VmaType::ANONYMOUS:
        case VmaType::STACK:
            // Zero-fill anonymous and stack pages
            for (usize i = 0; i < 4096; i++) {
                ptr[i] = 0;
            }
            break;

        case VmaType::FILE:
            // TODO: Read from file
            // For now, zero-fill file-backed pages too
            for (usize i = 0; i < 4096; i++) {
                ptr[i] = 0;
            }
            break;

        case VmaType::GUARD:
            // Should not reach here (checked above under lock)
            pmm::free_page(phys);
            return FaultResult::UNHANDLED;
    }

    // Map the page
    if (!map_callback(page_addr, phys, vma_prot_copy)) {
        pmm::free_page(phys);
        serial::puts("[vma] ERROR: Failed to map page\n");
        return FaultResult::ERROR;
    }

    serial::puts("[vma] Demand paged ");
    serial::put_hex(page_addr);
    serial::puts(" -> ");
    serial::put_hex(phys);
    serial::puts("\n");

    return FaultResult::HANDLED;
}

} // namespace mm
