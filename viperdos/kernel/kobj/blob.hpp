#pragma once

#include "../mm/pmm.hpp"
#include "object.hpp"

/**
 * @file blob.hpp
 * @brief Reference-counted physical memory blob object.
 *
 * @details
 * A blob is a contiguous region of memory backed by one or more physical pages.
 * Blobs are useful for sharing buffers between kernel subsystems or between
 * different protection domains via capabilities.
 *
 * The blob owns its backing pages: it allocates pages on creation and frees
 * them when the blob object is destroyed (after the last reference is released).
 */
namespace kobj {

// Blob - a reference-counted memory region
// Can be shared between Vipers via capabilities
/**
 * @brief Reference-counted contiguous memory buffer.
 *
 * @details
 * The buffer size is rounded up to whole pages. The blob exposes both a kernel
 * virtual pointer for access (`data()`) and the physical base address (`phys()`)
 * for mapping into other address spaces.
 */
class Blob : public Object {
  public:
    static constexpr cap::Kind KIND = cap::Kind::Blob;

    /**
     * @brief Create a new blob.
     *
     * @details
     * Allocates enough pages to cover `size`, zeros the backing memory, and
     * returns a newly allocated blob object.
     *
     * @param size Requested size in bytes (will be rounded up to pages).
     * @return Pointer to a new blob, or `nullptr` on allocation failure.
     */
    static Blob *create(usize size);

    /**
     * @brief Destroy the blob and free its backing pages.
     *
     * @details
     * Called when the last reference is released. Frees the physical pages
     * tracked by the blob.
     */
    ~Blob() override;

    /**
     * @brief Get the kernel virtual pointer to the blob's data.
     *
     * @return Pointer to the beginning of the blob buffer.
     */
    void *data() {
        return data_;
    }

    /** @brief Const overload of @ref data(). */
    const void *data() const {
        return data_;
    }

    /** @brief Size of the blob buffer in bytes (page-aligned). */
    usize size() const {
        return size_;
    }

    /**
     * @brief Physical base address of the blob buffer.
     *
     * @details
     * Used when mapping the blob into a user address space.
     */
    u64 phys() const {
        return phys_;
    }

    /**
     * @brief Number of 4KiB pages backing the blob.
     *
     * @return Page count.
     */
    usize pages() const {
        return (size_ + pmm::PAGE_SIZE - 1) / pmm::PAGE_SIZE;
    }

  private:
    Blob(void *data, u64 phys, usize size) : Object(KIND), data_(data), phys_(phys), size_(size) {}

    void *data_; // Kernel virtual address
    u64 phys_;   // Physical address
    usize size_; // Size in bytes
};

} // namespace kobj
