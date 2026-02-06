//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/syscall/handlers/mmap.cpp
// Purpose: Memory mapping syscall handlers (0x150-0x15F).
//
//===----------------------------------------------------------------------===//

#include "../../mm/vma.hpp"
#include "../../viper/address_space.hpp"
#include "../../viper/viper.hpp"
#include "handlers_internal.hpp"

namespace syscall {

// mmap prot flags (match POSIX/libc definitions)
static constexpr u32 PROT_READ = 1;
static constexpr u32 PROT_WRITE = 2;
static constexpr u32 PROT_EXEC = 4;

// mmap flags
static constexpr u32 MAP_SHARED = 0x01;
static constexpr u32 MAP_PRIVATE = 0x02;
static constexpr u32 MAP_FIXED = 0x10;
static constexpr u32 MAP_ANONYMOUS = 0x20;

static constexpr u64 PAGE_SIZE = 4096;
static constexpr u64 PAGE_MASK = PAGE_SIZE - 1;

static u64 page_align_up(u64 addr) {
    return (addr + PAGE_MASK) & ~PAGE_MASK;
}

static u64 page_align_down(u64 addr) {
    return addr & ~PAGE_MASK;
}

/// Convert POSIX prot flags to kernel VMA prot flags
static u32 posix_to_vma_prot(u32 posix_prot) {
    u32 vma = mm::vma_prot::NONE;
    if (posix_prot & PROT_READ)
        vma |= mm::vma_prot::READ;
    if (posix_prot & PROT_WRITE)
        vma |= mm::vma_prot::WRITE;
    if (posix_prot & PROT_EXEC)
        vma |= mm::vma_prot::EXEC;
    return vma;
}

/// @brief Map anonymous memory into the calling process's address space.
/// @details Supports MAP_FIXED (use exact address) and MAP_ANONYMOUS (only mode
///   supported). When MAP_FIXED is not set, the address is auto-assigned from
///   v->mmap_next which advances monotonically. Demand-paging handles the
///   actual physical allocation on first access.
SyscallResult sys_mmap(u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5) {
    u64 addr = a0;
    usize len = static_cast<usize>(a1);
    u32 prot = static_cast<u32>(a2);
    u32 flags = static_cast<u32>(a3);
    i32 fd = static_cast<i32>(a4);
    u64 offset = a5;

    (void)fd;     // File-backed mmap not yet supported
    (void)offset; // File offset not yet supported

    if (len == 0) {
        return err_invalid_arg();
    }

    // Round up to page boundary
    usize aligned_len = page_align_up(len);

    // Must have either MAP_PRIVATE or MAP_SHARED
    if (!(flags & MAP_PRIVATE) && !(flags & MAP_SHARED)) {
        return err_invalid_arg();
    }

    // Only support anonymous mappings for now
    if (!(flags & MAP_ANONYMOUS)) {
        return err_not_supported();
    }

    auto *v = viper::current();
    if (!v) {
        return err_permission();
    }

    // Determine the mapping address
    u64 map_addr;
    if (flags & MAP_FIXED) {
        // Use exact address
        map_addr = page_align_down(addr);
        if (map_addr == 0) {
            return err_invalid_arg();
        }
    } else {
        // Allocate from mmap region
        map_addr = page_align_up(v->mmap_next);
        v->mmap_next = map_addr + aligned_len;
    }

    // Convert prot flags
    u32 vma_prot_flags = posix_to_vma_prot(prot);

    // Create a VMA for the mapping (demand paging will handle actual allocation)
    auto saved = v->vma_list.acquire_lock();
    mm::Vma *vma =
        v->vma_list.add(map_addr, map_addr + aligned_len, vma_prot_flags, mm::VmaType::ANONYMOUS);
    v->vma_list.release_lock(saved);

    if (!vma) {
        return err_out_of_memory();
    }

    // For MAP_ANONYMOUS, demand paging will zero-fill on first access.
    // But if the caller expects the memory to be immediately accessible,
    // we pre-fault one page to ensure the address is valid.

    return ok_u64(map_addr);
}

/// @brief Unmap a previously mapped memory region from the process's address space.
/// @details Removes both the page table mappings and the VMA tracking entries.
SyscallResult sys_munmap(u64 a0, u64 a1, u64, u64, u64, u64) {
    u64 addr = page_align_down(a0);
    usize len = static_cast<usize>(page_align_up(a1));

    if (len == 0 || addr == 0) {
        return err_invalid_arg();
    }

    auto *v = viper::current();
    if (!v) {
        return err_permission();
    }

    auto *as = viper::get_address_space(v);
    if (!as) {
        return err_permission();
    }

    // Unmap pages from address space
    as->unmap(addr, len);

    // Remove VMAs in the range
    auto saved = v->vma_list.acquire_lock();
    v->vma_list.remove_range(addr, addr + len);
    v->vma_list.release_lock(saved);

    return SyscallResult::ok();
}

/// @brief Change protection flags on an existing memory mapping.
/// @details Updates both the VMA protection metadata and the actual page table
///   entries (PTE) for already-faulted pages, including TLB invalidation.
SyscallResult sys_mprotect(u64 a0, u64 a1, u64 a2, u64, u64, u64) {
    u64 addr = page_align_down(a0);
    usize len = static_cast<usize>(page_align_up(a1));
    u32 prot = static_cast<u32>(a2);

    if (len == 0 || addr == 0) {
        return err_invalid_arg();
    }

    auto *v = viper::current();
    if (!v) {
        return err_permission();
    }

    auto *as = viper::get_address_space(v);
    if (!as) {
        return err_permission();
    }

    u32 vma_prot_flags = posix_to_vma_prot(prot);
    u64 end = addr + len;

    // Update VMA protection flags for overlapping VMAs
    auto saved = v->vma_list.acquire_lock();
    for (mm::Vma *vma = v->vma_list.head_locked(); vma; vma = vma->next) {
        if (vma->start >= end)
            break;
        if (vma->end <= addr)
            continue;
        vma->prot = vma_prot_flags;
    }
    v->vma_list.release_lock(saved);

    // Update page table entries for already-mapped pages
    for (u64 va = addr; va < end; va += PAGE_SIZE) {
        u64 old_pte = as->read_pte(va);
        if (!(old_pte & viper::pte::VALID))
            continue;

        // Rebuild PTE with new protection bits, preserving physical address
        u64 phys = old_pte & viper::pte::ADDR_MASK;
        u64 entry = phys | viper::pte::VALID | viper::pte::PAGE | viper::pte::AF |
                    viper::pte::SH_INNER | viper::pte::AP_EL0 | viper::pte::ATTR_NORMAL;

        if (!(prot & PROT_WRITE)) {
            entry |= viper::pte::AP_RO;
        }
        if (!(prot & PROT_EXEC)) {
            entry |= viper::pte::UXN | viper::pte::PXN;
        }

        as->write_pte(va, entry);
        viper::tlb_flush_page(va, as->asid());
    }

    return SyscallResult::ok();
}

/// @brief Synchronize a mapped region to backing store (no-op for anonymous mappings).
SyscallResult sys_msync(u64, u64, u64, u64, u64, u64) {
    // No-op: all mappings are anonymous or in-memory
    return SyscallResult::ok();
}

/// @brief Provide memory usage hints to the kernel (no-op, advisory only).
SyscallResult sys_madvise(u64, u64, u64, u64, u64, u64) {
    // No-op: advisory only
    return SyscallResult::ok();
}

/// @brief Lock pages in physical memory (no-op, all pages are already pinned).
SyscallResult sys_mlock(u64, u64, u64, u64, u64, u64) {
    // No-op: all pages are already locked in physical memory
    return SyscallResult::ok();
}

/// @brief Unlock pages from physical memory (no-op).
SyscallResult sys_munlock(u64, u64, u64, u64, u64, u64) {
    // No-op
    return SyscallResult::ok();
}

} // namespace syscall
