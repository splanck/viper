# Memory Management Subsystem

**Status:** Functional with room for optimization
**Location:** `kernel/mm/`
**SLOC:** ~912

## Overview

The memory management subsystem provides physical page allocation, virtual memory mapping, and kernel heap services. It is designed for simplicity during bring-up, with a path to more sophisticated features.

## Components

### 1. Physical Memory Manager (`pmm.cpp`, `pmm.hpp`)

**Status:** Fully functional bitmap allocator

**Implemented:**
- Bitmap-based page tracking (1 bit per 4KB page)
- Single contiguous RAM region support
- First-fit page allocation
- Contiguous multi-page allocation (`alloc_pages`)
- Page freeing with double-free detection
- Memory statistics (total/free/used pages)
- Automatic bitmap placement after kernel image
- Framebuffer region reservation (8MB at 0x41000000)

**Memory Layout (QEMU virt with 128MB):**
```
0x40000000 ┌──────────────────────┐
           │     Kernel Image     │
           ├──────────────────────┤
           │    PMM Bitmap        │  (~4KB for 128MB)
           ├──────────────────────┤
           │    Free RAM          │
           ├──────────────────────┤
0x41000000 │    Framebuffer       │  (8MB reserved)
           ├──────────────────────┤
           │    Free RAM          │
0x48000000 └──────────────────────┘
```

**Not Implemented:**
- Multiple memory regions (only single contiguous range)
- NUMA awareness
- Memory zones (DMA, normal, high)
- Page coloring
- Memory hotplug
- Large page (2MB, 1GB) tracking

**Recommendations:**
- Add memory zone support for device DMA requirements
- Implement buddy allocator for O(1) allocation
- Support multiple memory regions from DTB/UEFI
- Add page reference counting for shared pages

---

### 2. Virtual Memory Manager (`vmm.cpp`, `vmm.hpp`)

**Status:** Basic 4-level page table support

**Implemented:**
- 4KB page granule with 4-level tables (L0→L3)
- 48-bit virtual address space
- Page table allocation from PMM
- Single page mapping (`map_page`)
- Range mapping (`map_range`)
- Page unmapping (`unmap_page`)
- Virtual-to-physical translation (`virt_to_phys`)
- TLB invalidation per-page and global
- Block descriptor support (1GB, 2MB) in translation

**Page Table Entry Flags (`pte::`):**
| Flag | Value | Description |
|------|-------|-------------|
| VALID | bit 0 | Entry is valid |
| TABLE | bit 1 | Points to next-level table |
| AF | bit 10 | Access flag (set on first access) |
| SH_INNER | bits 8-9 | Inner shareable |
| AP_RW_EL1 | bits 6-7 | EL1 read/write |
| AP_RW_EL0 | bits 6-7 | EL0 read/write |
| ATTR_NORMAL | bits 2-4 | Normal memory (MAIR index 1) |
| ATTR_DEVICE | bits 2-4 | Device memory (MAIR index 0) |
| UXN | bit 54 | User execute never |
| PXN | bit 53 | Privileged execute never |

**Not Implemented:**
- Page fault handling
- Demand paging
- Copy-on-write
- Shared memory mappings
- Guard pages
- Memory-mapped files
- Large page (2MB, 1GB) mapping creation
- ASID management for address space switching
- Page table deallocation on unmap

**Known Limitations:**
- No rollback if page table allocation fails mid-walk
- Intermediate tables not freed when mappings removed
- Identity mapping assumed (no kernel higher-half)

**Recommendations:**
- Implement page fault handler for demand paging
- Add page table garbage collection
- Support large page mapping for performance
- Implement proper ASID management for context switches

---

### 3. Kernel Heap (`kheap.cpp`, `kheap.hpp`)

**Status:** Fully functional with coalescing

**Implemented:**
- Free-list allocator with first-fit strategy
- Immediate coalescing of adjacent free blocks
- Block splitting when allocation leaves sufficient space
- Dynamic heap expansion from PMM
- Thread-safe with spinlock protection
- Standard C++ operator new/delete support
- Zero-fill allocation (`kzalloc`)
- Reallocation (`krealloc`)
- Heap statistics and debugging dump
- Double-free detection

**Block Header Structure:**
```
+----------------+
| size | in_use  |  <- 8-byte header (size includes header, bit 0 = in_use)
+----------------+
| user data...   |  <- returned pointer points here
| ...            |
+----------------+
| next_free      |  <- only present in free blocks
+----------------+
```

**Configuration:**
- Initial size: 64KB (16 pages)
- Maximum size: 64MB
- Minimum block: 24 bytes (header + next pointer)
- Alignment: 16 bytes

**API:**
| Function | Description |
|----------|-------------|
| `kmalloc(size)` | Allocate memory |
| `kzalloc(size)` | Allocate zeroed memory |
| `krealloc(ptr, size)` | Resize allocation |
| `kfree(ptr)` | Free memory |
| `get_used()` | Get allocated bytes |
| `get_available()` | Get free bytes |
| `get_stats(...)` | Get detailed statistics |
| `dump()` | Print heap state to serial |

**Not Implemented:**
- Slab allocator for common object sizes
- Memory pools for specific subsystems
- Memory pressure callbacks
- Memory leak detection
- Heap corruption detection beyond double-free
- Per-CPU caches for reduced contention

**Recommendations:**
- Add slab allocator for frequent small allocations (task structs, inodes, etc.)
- Implement memory pressure notification
- Add red zones for corruption detection in debug builds
- Consider per-CPU free lists for scalability

---

## Memory Layout Summary

```
Virtual Address Space (Identity Mapped):
┌─────────────────────────────────────────┐ 0x48000000
│                                         │
│              Unmapped                   │
│                                         │
├─────────────────────────────────────────┤ 0x41800000
│         Framebuffer (8MB)               │
├─────────────────────────────────────────┤ 0x41000000
│                                         │
│         Free RAM / Kernel Heap          │
│                                         │
├─────────────────────────────────────────┤ ~0x40150000
│         VMM Page Tables                 │
├─────────────────────────────────────────┤ ~0x40143000
│         Kernel Heap Initial             │
├─────────────────────────────────────────┤ ~0x40142000
│         PMM Bitmap                      │
├─────────────────────────────────────────┤ ~0x40141000
│         Kernel BSS/Data                 │
├─────────────────────────────────────────┤
│         Kernel Code                     │
├─────────────────────────────────────────┤ 0x40080000
│         Kernel Stack (16KB)             │
└─────────────────────────────────────────┘ 0x40000000

Device Region (Identity Mapped):
┌─────────────────────────────────────────┐ 0x40000000
│              RAM Region                 │
├─────────────────────────────────────────┤ 0x09000000
│           UART (PL011)                  │
├─────────────────────────────────────────┤ 0x09020000
│           fw_cfg                        │
├─────────────────────────────────────────┤ 0x0a000000
│           VirtIO MMIO                   │
├─────────────────────────────────────────┤ 0x08000000
│           GIC (Distributor)             │
├─────────────────────────────────────────┤ 0x08010000
│           GIC (CPU Interface)           │
└─────────────────────────────────────────┘ 0x00000000
```

---

## Testing

The memory management subsystem is tested via:
- `qemu_kernel_boot` - Verifies kernel starts (PMM/heap working)
- `qemu_storage_tests` - File operations require heap allocations
- All tests implicitly exercise memory allocation

**Test functions in `storage_tests.cpp`:**
- File creation/deletion exercises heap
- Directory operations use dynamic allocation

---

## Files

| File | Lines | Description |
|------|-------|-------------|
| `pmm.cpp` | ~300 | Physical page allocator |
| `pmm.hpp` | ~60 | PMM interface |
| `vmm.cpp` | ~290 | Virtual memory manager |
| `vmm.hpp` | ~80 | VMM interface and PTE flags |
| `kheap.cpp` | ~540 | Kernel heap allocator |
| `kheap.hpp` | ~50 | Heap interface |

---

## Statistics API

**Physical Memory:**
```cpp
u64 pmm::get_total_pages();  // Total RAM pages
u64 pmm::get_free_pages();   // Available pages
u64 pmm::get_used_pages();   // Allocated pages
```

**Kernel Heap:**
```cpp
u64 kheap::get_used();       // Allocated bytes
u64 kheap::get_available();  // Free bytes in heap
void kheap::get_stats(&total, &used, &free, &blocks);
```

**Syscall Access:**
- `mem_info` (0xE0) - Returns `MemInfo` struct to user space

---

## Priority Recommendations

1. **High:** Implement page fault handler for demand paging
2. **High:** Add slab allocator for common kernel objects
3. **Medium:** Support multiple memory regions from device tree
4. **Medium:** Implement proper ASID management
5. **Low:** Add memory pressure callbacks
6. **Low:** Per-CPU heap caches for scalability
