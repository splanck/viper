/**
 * @file shm.cpp
 * @brief SharedMemory kernel object implementation.
 */

#include "shm.hpp"
#include "../mm/pmm.hpp"

namespace kobj {

SharedMemory *SharedMemory::create(u64 size) {
    if (size == 0) {
        return nullptr;
    }

    // Align size to page boundary
    u64 aligned_size = pmm::page_align_up(size);
    u64 num_pages = aligned_size / pmm::PAGE_SIZE;

    // Allocate contiguous physical pages
    u64 phys_addr = pmm::alloc_pages(num_pages);
    if (phys_addr == 0) {
        return nullptr;
    }

    // Zero the memory
    void *virt = pmm::phys_to_virt(phys_addr);
    u8 *ptr = static_cast<u8 *>(virt);
    for (u64 i = 0; i < aligned_size; i++) {
        ptr[i] = 0;
    }

    // Create the kernel object
    return new SharedMemory(phys_addr, aligned_size, num_pages);
}

SharedMemory::~SharedMemory() {
    // Free the physical pages
    if (phys_addr_ != 0 && num_pages_ > 0) {
        pmm::free_pages(phys_addr_, num_pages_);
    }
}

} // namespace kobj
