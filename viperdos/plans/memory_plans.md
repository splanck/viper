# ViperDOS Memory Management: Comprehensive Review and Recommendations

**Version:** 0.3.2
**Date:** January 2026
**Total Memory Management SLOC:** ~5,600 lines

---

## Executive Summary

ViperDOS implements a complete memory management subsystem for AArch64 with multiple allocators, page table management,
and support infrastructure for demand paging and copy-on-write. The system is **functional for current use cases** but
has several areas requiring attention before scaling to larger binaries and more complex workloads.

**Critical Finding:** The demand paging and COW infrastructure exists but is **not fully wired** to the page fault
handler for all scenarios. Stack growth works, but general demand paging for anonymous VMAs needs testing and
verification.

---

## 1. Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           User Applications                                  │
├─────────────────────────────────────────────────────────────────────────────┤
│                              Syscalls                                        │
│          mmap / brk / munmap / fork                                         │
├─────────────────────────────────────────────────────────────────────────────┤
│                              Kernel                                          │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐│
│  │   VmaList   │  │  COW Mgr    │  │  Fault Hdlr │  │   Address Space     ││
│  │  (vma.cpp)  │  │  (cow.cpp)  │  │ (fault.cpp) │  │ (address_space.cpp) ││
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘  └─────────┬───────────┘│
│         └────────────────┴────────────────┴───────────────────┘            │
│  ┌─────────────────────────────────────────────────────────────────────────┐│
│  │                         VMM (Page Tables)                                ││
│  │     4-level AArch64 page tables, 4KB/2MB mappings, TLB management       ││
│  ├─────────────────────────────────────────────────────────────────────────┤│
│  │                       Physical Memory Allocators                         ││
│  │  ┌───────────────────────┐    ┌────────────────────────────────────────┐││
│  │  │   Bitmap Allocator    │    │       Buddy Allocator                  │││
│  │  │ (pre-framebuffer RAM) │    │    (post-framebuffer RAM)              │││
│  │  │     O(n) first-fit    │    │  O(log n) power-of-two blocks          │││
│  │  │                       │    │  + Per-CPU page cache (8 pages/CPU)    │││
│  │  └───────────────────────┘    └────────────────────────────────────────┘││
│  ├─────────────────────────────────────────────────────────────────────────┤│
│  │                      Kernel Memory Allocators                            ││
│  │  ┌───────────────────────┐    ┌────────────────────────────────────────┐││
│  │  │     Kernel Heap       │    │        Slab Allocator                  │││
│  │  │   Free-list + coal.   │    │    O(1) fixed-size objects             │││
│  │  │   Max 64MB, first-fit │    │    16 caches, per-cache locks          │││
│  │  └───────────────────────┘    └────────────────────────────────────────┘││
│  └─────────────────────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Component Analysis

### 2.1 Physical Memory Manager (PMM)

**Files:** `kernel/mm/pmm.hpp` (179 lines), `kernel/mm/pmm.cpp` (395 lines)

**Architecture:**

- Hybrid bitmap + buddy allocator design
- RAM split at framebuffer boundary:
    - Pre-framebuffer: Bitmap allocator
    - Post-framebuffer: Buddy allocator
- Global spinlock protects bitmap state

**Strengths:**

- Clean separation of memory regions
- Buddy allocator handles bulk of allocations efficiently
- Good diagnostic output during initialization

**Weaknesses:**

1. **O(n) Bitmap Scan:** `alloc_page()` scans entire bitmap word-by-word
    - Impact: Degrades as memory fragments
    - Location: `pmm.cpp:245-263`

2. **Single Global Lock:** `pmm_lock` serializes all bitmap operations
    - Impact: Contention under heavy allocation load
    - Location: `pmm.cpp:26`

3. **No Free Page Count Tracking:** Must compute free pages by summing buddy + bitmap
    - Minor issue, but adds overhead to statistics queries

**Recommendations:**

- **Immediate:** Add a "next likely free" hint to avoid full bitmap scan
- **Medium-term:** Replace bitmap with a second buddy allocator for the pre-FB region
- **Long-term:** Consider NUMA-aware allocation if targeting multi-socket systems

---

### 2.2 Buddy Allocator

**Files:** `kernel/mm/buddy.hpp` (222 lines), `kernel/mm/buddy.cpp` (409 lines)

**Architecture:**

- Power-of-two blocks: 4KB (order 0) to 2MB (order 9)
- Per-order free lists with block coalescing
- Per-CPU page cache for order-0 allocations (8 pages/CPU)

**Strengths:**

- O(log n) allocation via block splitting
- O(log n) deallocation via buddy coalescing
- Per-CPU cache reduces lock contention for single-page allocations
- Clean block address calculation using XOR

**Weaknesses:**

1. **Linear Free List Search for Removal:** `remove_from_free_list()` is O(n)
    - Impact: Coalescing degrades with many free blocks
    - Location: `buddy.cpp:336-349`

2. **No Order Tracking on Allocation:** Cannot efficiently free multi-page allocations
    - Impact: `pmm::free_pages()` frees pages one at a time
    - Location: `pmm.cpp:337-339`

3. **Per-CPU Cache Not Drained:** No mechanism to return cached pages to buddy
    - Impact: 32 pages (8 * 4 CPUs) permanently held in caches
    - Minor impact but worth noting

**Recommendations:**

- **Immediate:** Store allocation order in a per-page metadata array
- **Medium-term:** Use a red-black tree or bitmap for free block tracking
- **Long-term:** Add memory pressure callbacks to drain per-CPU caches

---

### 2.3 Virtual Memory Manager (VMM)

**Files:** `kernel/mm/vmm.hpp` (218 lines), `kernel/mm/vmm.cpp` (445 lines)

**Architecture:**

- 4-level AArch64 page table walker
- Supports 4KB pages and 2MB blocks
- Automatic intermediate table allocation with rollback on failure
- TLB invalidation (per-page and full flush)

**Strengths:**

- Correct AArch64 page table format
- Proper TLB maintenance sequences (DSB + ISB)
- Rollback support prevents memory leaks on mapping failures
- Support for both page and block mappings

**Weaknesses:**

1. **Identity Mapping Only:** Still uses VA == PA from boot
    - Impact: No user/kernel address space separation
    - Location: `vmm.cpp:216-220`

2. **No ASID Support:** All TLB invalidations are global
    - Impact: Cross-process TLB pollution
    - Location: `vmm.cpp:430-434`

3. **Full TLB Flush for 2MB Blocks:** Could use range invalidation
    - Impact: Performance hit on large mappings
    - Location: `vmm.cpp:341`

4. **No Page Table Freeing:** `unmap_page()` clears entries but doesn't free empty tables
    - Impact: Memory leak for heavily used/freed regions
    - Location: `vmm.cpp:366-378`

**Recommendations:**

- **Immediate:** Implement page table reference counting for proper cleanup
- **Medium-term:** Enable ASID for per-process TLB entries
- **Long-term:** Implement high/low split (kernel in upper half, user in lower)

---

### 2.4 Kernel Heap (kheap)

**Files:** `kernel/mm/kheap.hpp` (273 lines), `kernel/mm/kheap.cpp` (636 lines)

**Architecture:**

- Free-list allocator with immediate coalescing
- 16-byte aligned allocations
- Magic number validation for corruption detection
- Support for non-contiguous heap regions (up to 16)
- Maximum heap size: 64MB

**Strengths:**

- Robust corruption detection (magic numbers, bounds checks)
- Double-free and use-after-free detection
- Block coalescing reduces fragmentation
- RAII `UniquePtr` wrapper for exception-safe allocation

**Weaknesses:**

1. **O(n) Free List Traversal:** First-fit search on every allocation
    - Impact: Degrades linearly with fragmentation
    - Location: `kheap.cpp:346-353`

2. **Single Global Lock:** All heap operations serialized
    - Impact: Contention under heavy concurrent allocation
    - Location: `kheap.cpp:135`

3. **No Size Classes:** All allocations use same free list
    - Impact: Small allocations fragment with large ones
    - Location: Fundamental design issue

4. **O(n) Coalescing:** Walks entire free list on every free
    - Impact: Free operations get slower over time
    - Location: `kheap.cpp:269-286`

**Recommendations:**

- **Immediate:** Add segregated free lists by size class (8, 16, 32, 64, 128, 256+ bytes)
- **Medium-term:** Consider replacing with slab-based allocation for common sizes
- **Long-term:** Implement per-CPU arenas to eliminate lock contention

---

### 2.5 Slab Allocator

**Files:** `kernel/mm/slab.hpp` (192 lines), `kernel/mm/slab.cpp` (435 lines)

**Architecture:**

- O(1) allocation/deallocation for fixed-size objects
- Per-cache spinlocks for SMP scalability
- Objects packed in 4KB slabs
- Partial slab list for fast allocation path
- Maximum 16 caches

**Strengths:**

- True O(1) allocation via free list pop
- Per-cache locking is good for SMP
- O(1) ownership verification via slab->cache pointer
- Double-free detection

**Weaknesses:**

1. **Limited Cache Count:** Only 16 caches allowed
    - Impact: May run out for complex kernel subsystems
    - Location: `slab.hpp:57`

2. **No Slab Reclaim:** Empty slabs never returned to PMM
    - Impact: Memory not reclaimed after workload changes
    - Location: `slab.cpp:339-341` (explicit TODO comment)

3. **O(n) Double-Free Detection:** Walks free list on every free
    - Impact: Correctness check degrades performance
    - Location: `slab.cpp:312-319`

4. **Only One Pre-defined Cache:** Just inode cache (256 bytes)
    - Impact: Other kernel objects use slower heap
    - Location: `slab.cpp:426`

**Recommendations:**

- **Immediate:** Add caches for Task, Viper, Channel, and other common objects
- **Medium-term:** Implement slab reclaim under memory pressure
- **Long-term:** Add debug-only double-free check (disable in release)

---

### 2.6 Copy-on-Write Manager (COW)

**Files:** `kernel/mm/cow.hpp` (170 lines), `kernel/mm/cow.cpp` (192 lines)

**Architecture:**

- Per-page metadata array indexed by physical frame number
- 16-bit reference count per page
- COW flag tracking for write fault handling
- Global spinlock protects all operations

**Strengths:**

- Simple and correct reference counting
- Clear separation of refcount and COW flag
- Proper saturation handling (max 65535 refs)

**Weaknesses:**

1. **Global Lock for All Operations:** Every ref inc/dec takes the lock
    - Impact: Contention during fork() heavy workloads
    - Location: `cow.cpp:102`, `cow.cpp:120`

2. **16-bit Reference Count:** Maximum 65535 references per page
    - Impact: Unlikely limit, but hard limit exists
    - Location: `cow.hpp:25`

3. **Large Memory Overhead:** 4 bytes per page (1MB for 1GB RAM)
    - Impact: Acceptable, but could be compressed
    - Location: `cow.cpp:54-55`

**Recommendations:**

- **Immediate:** Consider per-page spinlocks or RCU for high-contention scenarios
- **Medium-term:** Lazily allocate PageInfo array only when COW is used
- **Long-term:** Use atomic operations for refcount to avoid lock for simple inc/dec

---

### 2.7 Virtual Memory Areas (VMA)

**Files:** `kernel/mm/vma.hpp` (283 lines), `kernel/mm/vma.cpp` (388 lines)

**Architecture:**

- Per-process linked list of VMAs
- Fixed pool of 64 VMAs per address space
- Supports anonymous, file-backed, stack, and guard regions
- Sorted by start address for efficient lookup

**Strengths:**

- TOCTOU-safe: Lock held during lookup and property copy
- Stack growth with configurable limit (8MB max)
- Clean VMA type system

**Weaknesses:**

1. **O(n) Lookup:** Linear search through VMA list
    - Impact: Degrades with many mappings (e.g., many mmap calls)
    - Location: `vma.cpp:71-84`

2. **Fixed Pool Size:** Maximum 64 VMAs per process
    - Impact: Limits complex applications with many mappings
    - Location: `vma.hpp:109`

3. **No VMA Merging:** Adjacent VMAs with same properties not merged
    - Impact: More VMAs than necessary
    - Location: Fundamental design issue

4. **Single Stack VMA Assumption:** Stack growth only works for one stack
    - Impact: Multi-threaded applications need per-thread stacks
    - Location: `vma.cpp:262-314`

**Recommendations:**

- **Immediate:** Increase VMA pool to 256 entries
- **Medium-term:** Use a red-black tree or interval tree for O(log n) lookup
- **Long-term:** Implement VMA merging and splitting for efficient mmap

---

### 2.8 Page Fault Handler

**Files:** `kernel/mm/fault.hpp` (127 lines), `kernel/mm/fault.cpp` (606 lines)

**Architecture:**

- ESR_EL1 parsing for fault classification
- Supports translation, permission, alignment, and external faults
- Demand paging for translation faults
- COW handling for permission faults on write
- SIGSEGV delivery for unhandled user faults
- Kernel panic for kernel-mode faults

**Strengths:**

- Correct ESR parsing for AArch64
- Good diagnostic output with backtrace
- Proper integration with VMA and COW subsystems
- Signal delivery for clean process termination

**Weaknesses:**

1. **Stack Growth Only Extends Start:** Doesn't allocate multiple pages at once
    - Impact: Multiple faults for large stack growth
    - Location: `vma.cpp:287-291`

2. **No Prefaulting:** Only faults in one page at a time
    - Impact: Many faults for sequential access patterns
    - Location: `vma.cpp:340-377`

3. **File-backed VMA Not Implemented:** Zero-fills instead of reading file
    - Impact: mmap of files doesn't work
    - Location: `vma.cpp:358-364`

**Recommendations:**

- **Immediate:** Implement multi-page stack growth
- **Medium-term:** Add prefaulting for sequential access patterns
- **Long-term:** Implement file-backed VMA page-in

---

## 3. Critical Gaps for Large Binaries

### 3.1 Address Space Layout

**Current State:** Identity mapping (VA == PA)

**Issue:** No separation between kernel and user address space. All processes share the same address space view.

**Impact for Large Binaries:**

- Cannot detect kernel memory access from user code
- Security vulnerability: user code can read/write kernel memory
- No ASLR possible

**Recommendation:** Implement high/low split:

- Kernel: `0xFFFF_0000_0000_0000` - `0xFFFF_FFFF_FFFF_FFFF`
- User: `0x0000_0000_0000_0000` - `0x0000_7FFF_FFFF_FFFF`

### 3.2 Demand Paging Integration

**Current State:** Infrastructure exists but may not be fully connected

**Issue:** The VMA and fault handler exist, but:

- Anonymous VMA demand paging needs testing
- File-backed VMA not implemented
- BSS section may not be demand-paged

**Impact for Large Binaries:**

- Large binaries may not load correctly if demand paging fails
- All pages allocated upfront wastes memory

**Recommendation:**

1. Add test cases for anonymous VMA demand paging
2. Verify BSS section is created as anonymous VMA
3. Implement file-backed VMA for executable segments

### 3.3 Memory Pressure Handling

**Current State:** No memory pressure detection or response

**Issue:** When memory runs low:

- No OOM killer
- No page cache eviction
- Slabs never reclaimed
- Allocation simply fails

**Impact for Large Binaries:**

- System may run out of memory without warning
- No graceful degradation

**Recommendation:**

1. Implement memory watermark monitoring
2. Add slab reclaim when memory is low
3. Add OOM killer for emergency situations

---

## 4. Scalability Bottlenecks

### 4.1 Lock Contention Points

| Component   | Lock        | Scope     | Impact                           |
|-------------|-------------|-----------|----------------------------------|
| PMM bitmap  | `pmm_lock`  | Global    | High under concurrent alloc      |
| Kernel heap | `heap_lock` | Global    | High under new/delete heavy load |
| COW manager | `cow.lock_` | Global    | High during fork storms          |
| VMM         | `vmm_lock`  | Global    | Medium for map/unmap             |
| Slab        | Per-cache   | Per-cache | Low, good design                 |
| Buddy       | `lock_`     | Global    | Medium, per-CPU cache helps      |

### 4.2 O(n) Operations

| Operation              | Location        | Trigger                     |
|------------------------|-----------------|-----------------------------|
| Bitmap page alloc      | `pmm.cpp:245`   | Every alloc when buddy full |
| Heap free-list search  | `kheap.cpp:346` | Every kmalloc               |
| Heap coalescing        | `kheap.cpp:269` | Every kfree                 |
| VMA lookup             | `vma.cpp:71`    | Every page fault            |
| Slab double-free check | `slab.cpp:312`  | Every slab free             |

---

## 5. Reliability Concerns

### 5.1 Error Handling

**Strengths:**

- Magic number validation in heap
- Double-free detection in heap and slab
- Rollback on VMM mapping failure

**Weaknesses:**

- No error codes returned from many functions (just nullptr)
- Serial output for errors may be lost if console fails
- No memory allocation failure metrics

### 5.2 Potential Data Races

| Location            | Issue                                  | Risk                          |
|---------------------|----------------------------------------|-------------------------------|
| `buddy.cpp:283-297` | `free_pages_count()` doesn't take lock | Low - stats query             |
| `kheap.cpp:429-438` | Reads header without lock in krealloc  | Medium - potential corruption |

### 5.3 Memory Leaks

| Source              | Description                        |
|---------------------|------------------------------------|
| VMM unmapped tables | Page tables not freed when emptied |
| Slab empty slabs    | Never returned to PMM              |
| Per-CPU page cache  | Not drained, 32 pages stuck        |

---

## 6. Performance Recommendations

### 6.1 Immediate (Before Large Binary Support)

1. **Verify Demand Paging Works**
    - Add test that creates anonymous VMA and triggers fault
    - Confirm page is allocated and mapped correctly

2. **Increase VMA Pool Size**
    - Change `MAX_VMAS` from 64 to 256
    - Large binaries may have many mapped regions

3. **Add More Slab Caches**
    - Task struct
    - Viper struct
    - Channel struct
    - File descriptor entries

4. **Fix krealloc Race**
    - Hold lock while reading old size
    - Already partially fixed, verify complete

### 6.2 Medium-term (For Production Use)

1. **Replace O(n) Operations**
    - Heap: Segregated free lists by size class
    - VMA: Red-black tree or interval tree
    - PMM: Replace bitmap with buddy

2. **Reduce Lock Contention**
    - Heap: Per-CPU arenas
    - COW: Atomic refcount operations
    - PMM: Already has per-CPU cache, sufficient

3. **Implement Memory Reclaim**
    - Slab cache reaping
    - Page table cleanup

4. **Add Memory Pressure Handling**
    - Low memory watermark detection
    - Callback system for reclaim

### 6.3 Long-term (For Full OS)

1. **User/Kernel Address Space Separation**
    - High/low split
    - ASID support
    - KPTI for security

2. **Swap Support**
    - Page-out to disk
    - Page-in on fault

3. **NUMA Awareness**
    - Per-node allocators
    - Local allocation preference

4. **Huge Page Support**
    - 2MB and 1GB pages
    - Transparent huge pages

---

## 7. Testing Recommendations

### 7.1 Unit Tests Needed

```cpp
// Test demand paging
TEST(VMA, DemandPagingAllocatesPage) {
    VmaList vmas;
    vmas.add(0x1000, 0x2000, vma_prot::READ | vma_prot::WRITE, VmaType::ANONYMOUS);
    // Trigger fault, verify page allocated
}

// Test COW
TEST(COW, WriteToSharedPageCopies) {
    // Fork, write to shared page, verify separate pages
}

// Test heap stress
TEST(Heap, FragmentationStress) {
    // Many random alloc/free, verify no corruption
}
```

### 7.2 Stress Tests Needed

1. **Allocation stress:** 10000 concurrent alloc/free cycles
2. **Fork stress:** 100 rapid forks
3. **VMA stress:** 200 mmap/munmap cycles
4. **Large allocation:** Single 50MB allocation

---

## 8. Summary

ViperDOS memory management is **well-structured and functional** for current use cases. The main concerns for supporting
larger binaries are:

| Priority | Issue                              | Effort |
|----------|------------------------------------|--------|
| Critical | Verify demand paging integration   | Low    |
| High     | Increase VMA pool size             | Low    |
| High     | Add slab caches for kernel objects | Medium |
| Medium   | Replace O(n) heap free-list        | Medium |
| Medium   | User/kernel address split          | High   |
| Low      | Memory pressure handling           | Medium |

The codebase is clean, well-documented, and follows consistent patterns. With the immediate recommendations implemented,
ViperDOS should handle larger binaries effectively.

---

## Appendix: SLOC Summary

| Component   | Header    | Implementation | Total     |
|-------------|-----------|----------------|-----------|
| PMM         | 179       | 395            | 574       |
| VMM         | 218       | 445            | 663       |
| Buddy       | 222       | 409            | 631       |
| Kernel Heap | 273       | 636            | 909       |
| Slab        | 192       | 435            | 627       |
| COW         | 170       | 192            | 362       |
| VMA         | 283       | 388            | 671       |
| Fault       | 127       | 606            | 733       |
| **Total**   | **1,664** | **3,506**      | **5,170** |

*Note: Additional code exists in address_space.cpp and mmu.cpp not counted above.*
