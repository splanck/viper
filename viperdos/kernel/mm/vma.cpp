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
#include "../fs/viperfs/viperfs.hpp"
#include "pmm.hpp"

namespace mm {

void VmaList::init() {
    SpinlockGuard guard(lock_);
    for (usize i = 0; i < MAX_VMAS; i++) {
        used_[i] = false;
        pool_[i].next = nullptr;
        pool_[i].left = nullptr;
        pool_[i].right = nullptr;
        pool_[i].parent = nullptr;
        pool_[i].color = RBColor::BLACK;
    }
    head_ = nullptr;
    root_ = nullptr;
    count_ = 0;
}

// ============================================================================
// Red-Black Tree Operations for O(log n) VMA Lookup
// ============================================================================

void VmaList::rb_rotate_left(Vma *x) {
    Vma *y = x->right;
    x->right = y->left;
    if (y->left) {
        y->left->parent = x;
    }
    y->parent = x->parent;
    if (!x->parent) {
        root_ = y;
    } else if (x == x->parent->left) {
        x->parent->left = y;
    } else {
        x->parent->right = y;
    }
    y->left = x;
    x->parent = y;
}

void VmaList::rb_rotate_right(Vma *x) {
    Vma *y = x->left;
    x->left = y->right;
    if (y->right) {
        y->right->parent = x;
    }
    y->parent = x->parent;
    if (!x->parent) {
        root_ = y;
    } else if (x == x->parent->right) {
        x->parent->right = y;
    } else {
        x->parent->left = y;
    }
    y->right = x;
    x->parent = y;
}

void VmaList::rb_insert_fixup(Vma *z) {
    while (z->parent && z->parent->color == RBColor::RED) {
        if (z->parent->parent && z->parent == z->parent->parent->left) {
            Vma *y = z->parent->parent->right;
            if (y && y->color == RBColor::RED) {
                z->parent->color = RBColor::BLACK;
                y->color = RBColor::BLACK;
                z->parent->parent->color = RBColor::RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->right) {
                    z = z->parent;
                    rb_rotate_left(z);
                }
                z->parent->color = RBColor::BLACK;
                if (z->parent->parent) {
                    z->parent->parent->color = RBColor::RED;
                    rb_rotate_right(z->parent->parent);
                }
            }
        } else if (z->parent->parent) {
            Vma *y = z->parent->parent->left;
            if (y && y->color == RBColor::RED) {
                z->parent->color = RBColor::BLACK;
                y->color = RBColor::BLACK;
                z->parent->parent->color = RBColor::RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->left) {
                    z = z->parent;
                    rb_rotate_right(z);
                }
                z->parent->color = RBColor::BLACK;
                if (z->parent->parent) {
                    z->parent->parent->color = RBColor::RED;
                    rb_rotate_left(z->parent->parent);
                }
            }
        }
    }
    root_->color = RBColor::BLACK;
}

void VmaList::rb_insert(Vma *z) {
    z->left = nullptr;
    z->right = nullptr;
    z->color = RBColor::RED;

    Vma *y = nullptr;
    Vma *x = root_;

    while (x) {
        y = x;
        if (z->start < x->start) {
            x = x->left;
        } else {
            x = x->right;
        }
    }

    z->parent = y;
    if (!y) {
        root_ = z;
    } else if (z->start < y->start) {
        y->left = z;
    } else {
        y->right = z;
    }

    rb_insert_fixup(z);
}

Vma *VmaList::rb_minimum(Vma *x) const {
    while (x && x->left) {
        x = x->left;
    }
    return x;
}

void VmaList::rb_transplant(Vma *u, Vma *v) {
    if (!u->parent) {
        root_ = v;
    } else if (u == u->parent->left) {
        u->parent->left = v;
    } else {
        u->parent->right = v;
    }
    if (v) {
        v->parent = u->parent;
    }
}

void VmaList::rb_remove_fixup(Vma *x, Vma *parent) {
    while (x != root_ && (!x || x->color == RBColor::BLACK)) {
        if (x == (parent ? parent->left : nullptr)) {
            Vma *w = parent ? parent->right : nullptr;
            if (w && w->color == RBColor::RED) {
                w->color = RBColor::BLACK;
                parent->color = RBColor::RED;
                rb_rotate_left(parent);
                w = parent->right;
            }
            if (w && (!w->left || w->left->color == RBColor::BLACK) &&
                (!w->right || w->right->color == RBColor::BLACK)) {
                w->color = RBColor::RED;
                x = parent;
                parent = x ? x->parent : nullptr;
            } else if (w) {
                if (!w->right || w->right->color == RBColor::BLACK) {
                    if (w->left) w->left->color = RBColor::BLACK;
                    w->color = RBColor::RED;
                    rb_rotate_right(w);
                    w = parent->right;
                }
                if (w) {
                    w->color = parent->color;
                    parent->color = RBColor::BLACK;
                    if (w->right) w->right->color = RBColor::BLACK;
                    rb_rotate_left(parent);
                }
                x = root_;
                break;
            } else {
                break;
            }
        } else {
            Vma *w = parent ? parent->left : nullptr;
            if (w && w->color == RBColor::RED) {
                w->color = RBColor::BLACK;
                parent->color = RBColor::RED;
                rb_rotate_right(parent);
                w = parent->left;
            }
            if (w && (!w->right || w->right->color == RBColor::BLACK) &&
                (!w->left || w->left->color == RBColor::BLACK)) {
                w->color = RBColor::RED;
                x = parent;
                parent = x ? x->parent : nullptr;
            } else if (w) {
                if (!w->left || w->left->color == RBColor::BLACK) {
                    if (w->right) w->right->color = RBColor::BLACK;
                    w->color = RBColor::RED;
                    rb_rotate_left(w);
                    w = parent->left;
                }
                if (w) {
                    w->color = parent->color;
                    parent->color = RBColor::BLACK;
                    if (w->left) w->left->color = RBColor::BLACK;
                    rb_rotate_right(parent);
                }
                x = root_;
                break;
            } else {
                break;
            }
        }
    }
    if (x) x->color = RBColor::BLACK;
}

void VmaList::rb_remove(Vma *z) {
    if (!z) return;

    Vma *y = z;
    Vma *x = nullptr;
    Vma *x_parent = nullptr;
    RBColor y_original_color = y->color;

    if (!z->left) {
        x = z->right;
        x_parent = z->parent;
        rb_transplant(z, z->right);
    } else if (!z->right) {
        x = z->left;
        x_parent = z->parent;
        rb_transplant(z, z->left);
    } else {
        y = rb_minimum(z->right);
        y_original_color = y->color;
        x = y->right;
        if (y->parent == z) {
            x_parent = y;
        } else {
            x_parent = y->parent;
            rb_transplant(y, y->right);
            y->right = z->right;
            if (y->right) y->right->parent = y;
        }
        rb_transplant(z, y);
        y->left = z->left;
        if (y->left) y->left->parent = y;
        y->color = z->color;
    }

    if (y_original_color == RBColor::BLACK) {
        rb_remove_fixup(x, x_parent);
    }
}

Vma *VmaList::rb_find(u64 addr) const {
    Vma *node = root_;
    while (node) {
        if (addr < node->start) {
            node = node->left;
        } else if (addr >= node->end) {
            node = node->right;
        } else {
            // addr is within [node->start, node->end)
            return node;
        }
    }
    return nullptr;
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
    // Use O(log n) red-black tree lookup
    return rb_find(addr);
}

Vma *VmaList::find_locked(u64 addr) {
    // Caller must hold lock_
    // Use O(log n) red-black tree lookup
    return rb_find(addr);
}

const Vma *VmaList::find(u64 addr) const {
    SpinlockGuard guard(lock_);
    // Use O(log n) red-black tree lookup
    return rb_find(addr);
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
    vma->flags = vma_flags::NONE;
    vma->file_inode = 0;
    vma->file_offset = 0;
    vma->next = nullptr;
    vma->left = nullptr;
    vma->right = nullptr;
    vma->parent = nullptr;
    vma->color = RBColor::RED;

    // Insert into both data structures
    insert_sorted(vma); // Linked list for iteration
    rb_insert(vma);     // Red-black tree for O(log n) lookup

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

    // Remove from red-black tree
    rb_remove(target);

    // Remove from linked list
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

            // Remove from red-black tree
            rb_remove(vma);

            // Remove from linked list
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
    root_ = nullptr;
    for (usize i = 0; i < MAX_VMAS; i++) {
        used_[i] = false;
        pool_[i].left = nullptr;
        pool_[i].right = nullptr;
        pool_[i].parent = nullptr;
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
        // Stack grows downward, so check if address is below a stack VMA
        Vma *v = vma_list->head_locked();
        while (v) {
            if (v->type == VmaType::STACK) {
                // Stack growth: allow faults within MAX_STACK_GROW_PAGES of the stack bottom
                // This enables multi-page stack growth for large local allocations
                constexpr u64 MAX_STACK_GROW_PAGES = 16; // Grow up to 64KB at a time
                constexpr u64 MAX_STACK_GROW = MAX_STACK_GROW_PAGES * 4096;

                if (fault_addr >= v->start - MAX_STACK_GROW && fault_addr < v->start) {
                    // Calculate how many pages we need to grow
                    u64 new_start = page_addr;
                    u64 pages_to_grow = (v->start - new_start) / 4096;

                    // Check stack size limit (vma->end is fixed stack top, vma->start grows down)
                    u64 new_stack_size = v->end - new_start;
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
                    serial::put_hex(new_start);
                    serial::puts(" (");
                    serial::put_dec(pages_to_grow);
                    serial::puts(" pages)\n");

                    // Save old start and extend the VMA (under lock)
                    u64 old_start = v->start;
                    v->start = new_start;

                    // Copy VMA properties before releasing lock
                    u32 stack_prot = v->prot;
                    vma_list->release_lock(saved_daif);

                    // Allocate and map all new stack pages (outside lock)
                    for (u64 addr = new_start; addr < old_start; addr += 4096) {
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

                        if (!map_callback(addr, phys, stack_prot)) {
                            pmm::free_page(phys);
                            return FaultResult::ERROR;
                        }
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
    u64 file_inode_copy = vma->file_inode;
    u64 file_offset_copy = vma->file_offset;
    u64 vma_start_copy = vma->start;
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

        case VmaType::FILE: {
            // Read from file if we have a valid inode
            bool read_ok = false;
            if (file_inode_copy != 0) {
                // Calculate offset within file for this page
                u64 page_offset_in_vma = page_addr - vma_start_copy;
                u64 file_read_offset = file_offset_copy + page_offset_in_vma;

                // Try to read from ViperFS
                fs::viperfs::ViperFS &vfs = fs::viperfs::viperfs();
                fs::viperfs::Inode *inode = vfs.read_inode(file_inode_copy);
                if (inode) {
                    // Zero the page first (in case file is shorter than page)
                    for (usize i = 0; i < 4096; i++) {
                        ptr[i] = 0;
                    }

                    // Read file data into the page
                    i64 bytes_read = vfs.read_data(inode, file_read_offset, ptr, 4096);
                    vfs.release_inode(inode);

                    if (bytes_read >= 0) {
                        read_ok = true;
                        serial::puts("[vma] File page-in: inode ");
                        serial::put_dec(file_inode_copy);
                        serial::puts(" offset ");
                        serial::put_dec(file_read_offset);
                        serial::puts(" read ");
                        serial::put_dec(bytes_read);
                        serial::puts(" bytes\n");
                    }
                }
            }

            // Fall back to zero-fill if read failed
            if (!read_ok) {
                for (usize i = 0; i < 4096; i++) {
                    ptr[i] = 0;
                }
            }
            break;
        }

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

    // Prefaulting: speculatively allocate pages ahead to reduce future faults
    // Only for anonymous mappings (not file-backed or stack)
    if (vma_type_copy == VmaType::ANONYMOUS) {
        constexpr u64 PREFAULT_PAGES = 4; // Prefetch up to 4 pages ahead

        // Re-acquire lock to check VMA bounds for prefaulting
        saved_daif = vma_list->acquire_lock();
        vma = vma_list->find_locked(page_addr);
        if (vma && vma->type == VmaType::ANONYMOUS) {
            u64 vma_end = vma->end;
            u32 prefault_prot = vma->prot;
            vma_list->release_lock(saved_daif);

            u64 prefaulted = 0;
            for (u64 i = 1; i <= PREFAULT_PAGES; i++) {
                u64 prefault_addr = page_addr + (i * 4096);
                if (prefault_addr >= vma_end) {
                    break; // Beyond VMA bounds
                }

                // Allocate and map prefault page
                u64 pf_phys = pmm::alloc_page();
                if (pf_phys == 0) {
                    break; // Out of memory, stop prefaulting
                }

                // Zero the page
                u8 *pf_ptr = reinterpret_cast<u8 *>(pmm::phys_to_virt(pf_phys));
                for (usize j = 0; j < 4096; j++) {
                    pf_ptr[j] = 0;
                }

                if (!map_callback(prefault_addr, pf_phys, prefault_prot)) {
                    // Page might already be mapped or mapping failed
                    pmm::free_page(pf_phys);
                    break;
                }
                prefaulted++;
            }

            if (prefaulted > 0) {
                serial::puts("[vma] Prefaulted ");
                serial::put_dec(prefaulted);
                serial::puts(" pages\n");
            }
        } else {
            vma_list->release_lock(saved_daif);
        }
    }

    return FaultResult::HANDLED;
}

} // namespace mm
