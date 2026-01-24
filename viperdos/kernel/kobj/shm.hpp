#pragma once

#include "../include/types.hpp"
#include "object.hpp"

/**
 * @file shm.hpp
 * @brief Kernel object for shared memory regions.
 *
 * @details
 * SharedMemory objects represent physical memory regions that can be
 * mapped into multiple address spaces. They are used for zero-copy
 * IPC data transfer between user-space processes.
 *
 * The shared memory object owns the physical pages and can be mapped
 * into process address spaces via SYS_SHM_MAP. When the last reference
 * is released, the physical pages are freed.
 */
namespace kobj {

/**
 * @brief Reference-counted shared memory object.
 *
 * @details
 * Owns a contiguous physical memory region. Multiple processes can
 * map the same region into their address spaces for efficient data
 * sharing.
 */
class SharedMemory : public Object {
  public:
    static constexpr cap::Kind KIND = cap::Kind::SharedMemory;

    /**
     * @brief Create a new shared memory object.
     *
     * @param size Size in bytes (will be page-aligned).
     * @return New shared memory object, or nullptr on failure.
     */
    static SharedMemory *create(u64 size);

    /**
     * @brief Destroy the shared memory object and free physical pages.
     */
    ~SharedMemory() override;

    /** @brief Get the physical address of the region. */
    u64 phys_addr() const {
        return phys_addr_;
    }

    /** @brief Get the size of the region in bytes. */
    u64 size() const {
        return size_;
    }

    /** @brief Get the number of pages. */
    u64 num_pages() const {
        return num_pages_;
    }

    /**
     * @brief Get the creator's virtual mapping (if any).
     *
     * @details
     * When created via SYS_SHM_CREATE, the memory is automatically
     * mapped into the creator's address space. This returns that
     * virtual address.
     */
    u64 creator_virt() const {
        return creator_virt_;
    }

    /**
     * @brief Set the creator's virtual mapping.
     */
    void set_creator_virt(u64 virt) {
        creator_virt_ = virt;
    }

  private:
    SharedMemory(u64 phys, u64 size, u64 pages)
        : Object(KIND), phys_addr_(phys), size_(size), num_pages_(pages), creator_virt_(0) {}

    u64 phys_addr_;    // Physical address of the region
    u64 size_;         // Size in bytes (page-aligned)
    u64 num_pages_;    // Number of physical pages
    u64 creator_virt_; // Creator's virtual mapping (0 if unmapped)
};

} // namespace kobj
