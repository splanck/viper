# Memory Management Subsystem

**Status:** Complete with demand paging and VMA tracking
**Location:** `kernel/mm/`
**SLOC:** ~1,400

## Overview

The memory management subsystem provides physical page allocation, virtual memory mapping, kernel heap services, and demand paging with VMA tracking. The implementation supports full virtual memory for user processes with automatic page allocation on fault.

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
- Copy-on-write
- Shared memory mappings
- Memory-mapped files
- Large page (2MB, 1GB) mapping creation
- Page table deallocation on unmap

**Known Limitations:**
- Intermediate tables not freed when mappings removed
- Identity mapping assumed (no kernel higher-half)

**Recommendations:**
- Add page table garbage collection
- Support large page mapping for performance
- Implement copy-on-write for fork()

---

### 4. Virtual Memory Areas (`vma.cpp`, `vma.hpp`)

**Status:** Complete VMA tracking for demand paging

**Implemented:**
- Per-process VMA list (linked list, sorted by address)
- VMA types: Anonymous, Stack, Heap, File-backed
- Protection flags (read, write, execute)
- VMA lookup by address (`vma_find`)
- VMA insertion with sorted order maintenance
- Stack growth detection (guard page distance check)
- Automatic stack VMA extension on fault

**VMA Structure:**
| Field | Type | Description |
|-------|------|-------------|
| start | u64 | Start address (page-aligned) |
| end | u64 | End address (exclusive) |
| prot | u32 | Protection flags |
| type | VmaType | Backing type (ANONYMOUS, STACK, etc.) |
| file_inode | u64 | Inode for file-backed mappings |
| file_offset | u64 | File offset |
| next | Vma* | Next VMA in list |

**VMA Types:**
| Type | Description |
|------|-------------|
| ANONYMOUS | Zero-filled on demand |
| STACK | User stack (grows down) |
| HEAP | User heap (sbrk region) |
| FILE | File-backed mapping |

**Protection Flags (vma_prot):**
| Flag | Value | Description |
|------|-------|-------------|
| READ | 0x1 | Readable |
| WRITE | 0x2 | Writable |
| EXEC | 0x4 | Executable |

---

### 5. Page Fault Handler (`fault.cpp`, `fault.hpp`)

**Status:** Complete demand paging implementation

**Implemented:**
- AArch64 data abort and instruction abort handling
- ESR parsing (fault status code, write/read, level)
- Fault classification (translation, permission, alignment, etc.)
- User-mode demand fault handling
- VMA lookup and validation
- Physical page allocation for valid faults
- Page table mapping with correct permissions
- Stack growth detection and automatic extension
- Kernel panic on unrecoverable kernel faults
- Detailed fault logging to serial console

**Fault Types:**
| Type | Description | Handling |
|------|-------------|----------|
| TRANSLATION | Page not mapped | Demand paging if valid VMA |
| PERMISSION | Access denied | Terminate task |
| ALIGNMENT | Misaligned access | Terminate task |
| ADDRESS_SIZE | Invalid address bits | Terminate task |

**Demand Paging Flow:**
```
1. Page fault occurs (data abort at EL0)
2. Parse ESR to get fault type and address
3. Look up VMA containing faulting address
4. If no VMA: terminate task (SIGSEGV equivalent)
5. If VMA found and type is TRANSLATION:
   a. Allocate physical page from PMM
   b. Zero-fill page (anonymous) or load from file
   c. Map page in process address space
   d. Resume execution
6. For stack VMA: check if within growth limit, extend if needed
```

**Handled Scenarios:**
- Heap access before sbrk extends region
- Stack growth into guard area
- First access to anonymous memory
- Access to uninitialized data segment

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
- Enhanced debug mode with:
  - Magic number validation (CAFEBABE for allocated, DEADBEEF for freed)
  - Double-free detection with detailed reporting
  - Bounds checking (pointer within heap range)
  - Alignment validation (16-byte boundaries)
  - Block poisoning on double-free (FEEDFACE pattern)
  - Use-after-free detection via poison patterns

**Block Header Structure:**
```
+----------------+
| magic          |  <- 4-byte magic number for validation
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
- Per-CPU caches for reduced contention

**Recommendations:**
- Add slab allocator for frequent small allocations (task structs, inodes, etc.)
- Implement memory pressure notification
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

1. **High:** Add copy-on-write for efficient fork()
2. **Medium:** Support multiple memory regions from device tree
3. **Medium:** Implement mmap() for file-backed mappings
4. **Low:** Add memory pressure callbacks
5. **Low:** Per-CPU heap caches for scalability
6. **Low:** Large page support (2MB, 1GB)
