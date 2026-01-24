#include "blob.hpp"
#include "../console/serial.hpp"

/**
 * @file blob.cpp
 * @brief Blob object implementation.
 *
 * @details
 * Blob creation allocates contiguous physical pages using the PMM, zeros the
 * resulting buffer, and constructs a heap-allocated `kobj::Blob` that owns the
 * pages. Destruction frees the backing pages.
 */
namespace kobj {

/** @copydoc kobj::Blob::create */
Blob *Blob::create(usize size) {
    if (size == 0) {
        return nullptr;
    }

    // Round up to page size
    usize pages = (size + pmm::PAGE_SIZE - 1) / pmm::PAGE_SIZE;
    usize aligned_size = pages * pmm::PAGE_SIZE;

    // Allocate physical pages
    u64 phys = pmm::alloc_pages(pages);
    if (phys == 0) {
        serial::puts("[blob] Failed to allocate ");
        serial::put_dec(pages);
        serial::puts(" pages\n");
        return nullptr;
    }

    // Zero the memory
    void *data = pmm::phys_to_virt(phys);
    u8 *ptr = static_cast<u8 *>(data);
    for (usize i = 0; i < aligned_size; i++) {
        ptr[i] = 0;
    }

    // Create blob object
    // Note: Using placement new would be better, but kmalloc works for now
    Blob *blob = new Blob(data, phys, aligned_size);

    // Check if allocation failed - if so, free the physical pages to avoid leak
    if (!blob) {
        serial::puts("[blob] Failed to allocate Blob object, freeing pages\n");
        pmm::free_pages(phys, pages);
        return nullptr;
    }

    serial::puts("[blob] Created blob: ");
    serial::put_dec(aligned_size);
    serial::puts(" bytes at phys ");
    serial::put_hex(phys);
    serial::puts("\n");

    return blob;
}

/** @copydoc kobj::Blob::~Blob */
Blob::~Blob() {
    if (phys_ != 0) {
        usize pages = (size_ + pmm::PAGE_SIZE - 1) / pmm::PAGE_SIZE;
        pmm::free_pages(phys_, pages);

        serial::puts("[blob] Freed blob: ");
        serial::put_dec(size_);
        serial::puts(" bytes at phys ");
        serial::put_hex(phys_);
        serial::puts("\n");
    }
}

} // namespace kobj
