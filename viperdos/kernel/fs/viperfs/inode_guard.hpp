#pragma once

#include "viperfs.hpp"

/**
 * @file inode_guard.hpp
 * @brief RAII wrapper for ViperFS inodes.
 *
 * @details
 * The InodeGuard class provides automatic release of inodes allocated by
 * ViperFS::read_inode(). This ensures inodes are properly released even when
 * early returns or errors occur.
 *
 * Usage:
 * @code
 * InodeGuard guard(viperfs::viperfs().read_inode(ino));
 * if (!guard) {
 *     return -1;  // null inode
 * }
 * // Use guard.get() or guard-> to access the inode
 * // Inode is automatically released when guard goes out of scope
 * @endcode
 */
namespace fs::viperfs {

/**
 * @brief RAII guard for ViperFS inode pointers.
 *
 * @details
 * Takes ownership of an Inode* and calls release_inode() on destruction.
 * Non-copyable, move-only semantics.
 */
class InodeGuard {
  public:
    /// Default constructor - creates null guard
    InodeGuard() : inode_(nullptr) {}

    /// Construct from raw inode pointer, taking ownership
    explicit InodeGuard(Inode *inode) : inode_(inode) {}

    /// Move constructor
    InodeGuard(InodeGuard &&other) noexcept : inode_(other.inode_) {
        other.inode_ = nullptr;
    }

    /// Destructor - releases the inode
    ~InodeGuard() {
        if (inode_) {
            viperfs().release_inode(inode_);
        }
    }

    // Non-copyable
    InodeGuard(const InodeGuard &) = delete;
    InodeGuard &operator=(const InodeGuard &) = delete;

    /// Move assignment
    InodeGuard &operator=(InodeGuard &&other) noexcept {
        if (this != &other) {
            if (inode_) {
                viperfs().release_inode(inode_);
            }
            inode_ = other.inode_;
            other.inode_ = nullptr;
        }
        return *this;
    }

    /// Reset to a new inode (releases old)
    void reset(Inode *inode = nullptr) {
        if (inode_) {
            viperfs().release_inode(inode_);
        }
        inode_ = inode;
    }

    /// Release ownership and return raw pointer
    Inode *release() {
        Inode *ptr = inode_;
        inode_ = nullptr;
        return ptr;
    }

    /// Dereference operators
    Inode *operator->() const {
        return inode_;
    }

    Inode &operator*() const {
        return *inode_;
    }

    /// Get raw pointer
    Inode *get() const {
        return inode_;
    }

    /// Boolean conversion
    explicit operator bool() const {
        return inode_ != nullptr;
    }

  private:
    Inode *inode_;
};

} // namespace fs::viperfs
