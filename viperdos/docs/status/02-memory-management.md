# Memory Management Subsystem

**Status:** Complete with demand paging, COW, buddy allocator, slab allocator, and shared memory
**Location:** `kernel/mm/`, `kernel/kobj/shm.*`
**SLOC:** ~5,550

## Overview

The memory management subsystem is a core kernel service in the ViperDOS microkernel. It provides physical page
allocation, virtual memory mapping, kernel heap services, slab allocation, demand paging, copy-on-write page sharing,
and shared memory for IPC.

In microkernel mode, memory management remains in the kernel while user-space servers use shared memory for efficient
large data transfers.

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

**Recommendations:**

- Add memory zone support for device DMA requirements
- Support multiple memory regions from DTB/UEFI

---

### 2. Buddy Allocator (`buddy.cpp`, `buddy.hpp`)

**Status:** Complete O(log n) page allocator

**Implemented:**

- Binary buddy allocation algorithm
- Order-based allocation (2^order pages)
- Orders 0-9 supported (4KB to 2MB)
- Block splitting for smaller allocations
- Block coalescing on free
- Per-order free lists
- Split bitmap tracking
- Spinlock protection for SMP safety
- Statistics tracking

**API:**
| Function | Description |
|----------|-------------|
| `init(start, end, reserved)` | Initialize allocator |
| `alloc_pages(order)` | Allocate 2^order pages |
| `free_pages(addr, order)` | Free 2^order pages |
| `alloc_page()` | Allocate single page (order 0) |
| `free_page(addr)` | Free single page |
| `free_pages_count()` | Get available page count |
| `total_pages_count()` | Get total managed pages |

**Buddy Algorithm:**

```
Allocation:
1. Find smallest order with free blocks >= requested
2. If larger order found, split recursively
3. Mark buddy bit, return block address

Deallocation:
1. Check if buddy block is free
2. If free, coalesce and repeat at higher order
3. Add to appropriate free list
```

**Performance:**

- Allocation: O(log n) worst case
- Deallocation: O(log n) with coalescing
- No external fragmentation for power-of-2 sizes

---

### 3. Slab Allocator (`slab.cpp`, `slab.hpp`)

**Status:** Complete fixed-size object allocator

**Implemented:**

- Object caches for common sizes
- Per-cache slab lists (full, partial, empty)
- Object freelist within slabs
- Constructor/destructor callbacks
- Cache statistics
- Automatic slab allocation from PMM
- Spinlock protection

**Standard Caches:**
| Cache | Object Size | Per Slab |
|-------|-------------|----------|
| slab-32 | 32 bytes | 128 |
| slab-64 | 64 bytes | 64 |
| slab-128 | 128 bytes | 32 |
| slab-256 | 256 bytes | 16 |
| slab-512 | 512 bytes | 8 |
| slab-1024 | 1024 bytes | 4 |
| slab-2048 | 2048 bytes | 2 |

**API:**
| Function | Description |
|----------|-------------|
| `slab_cache_create(name, size, ctor, dtor)` | Create object cache |
| `slab_cache_destroy(cache)` | Destroy cache |
| `slab_alloc(cache)` | Allocate object |
| `slab_free(cache, obj)` | Free object |
| `slab_cache_shrink(cache)` | Release empty slabs |

**Performance:**

- Allocation: O(1) from partial slab
- Deallocation: O(1)
- No internal fragmentation for cached sizes

---

### 4. Virtual Memory Manager (`vmm.cpp`, `vmm.hpp`)

**Status:** Complete 4-level page table support with COW

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
- **Copy-on-write page marking and fault handling**

**Page Table Entry Flags (`pte::`):**
| Flag | Value | Description |
|------|-------|-------------|
| VALID | bit 0 | Entry is valid |
| TABLE | bit 1 | Points to next-level table |
| AF | bit 10 | Access flag (set on first access) |
| SH_INNER | bits 8-9 | Inner shareable |
| AP_RW_EL1 | bits 6-7 | EL1 read/write |
| AP_RW_EL0 | bits 6-7 | EL0 read/write |
| AP_RO_EL0 | bits 6-7 | EL0 read-only (for COW) |
| ATTR_NORMAL | bits 2-4 | Normal memory (MAIR index 1) |
| ATTR_DEVICE | bits 2-4 | Device memory (MAIR index 0) |
| UXN | bit 54 | User execute never |
| PXN | bit 53 | Privileged execute never |

**Not Implemented:**

- Shared memory mappings (beyond COW)
- Memory-mapped files
- Large page (2MB, 1GB) mapping creation
- Page table deallocation on unmap

**Known Limitations:**

- Intermediate tables not freed when mappings removed
- Identity mapping assumed (no kernel higher-half)

**Recommendations:**

- Add page table garbage collection
- Support large page mapping for performance

---

### 5. Virtual Memory Areas (`vma.cpp`, `vma.hpp`)

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

### 6. Page Fault Handler (`fault.cpp`, `fault.hpp`)

**Status:** Complete demand paging and COW implementation

**Implemented:**

- AArch64 data abort and instruction abort handling
- ESR parsing (fault status code, write/read, level)
- Fault classification (translation, permission, alignment, etc.)
- User-mode demand fault handling
- VMA lookup and validation
- Physical page allocation for valid faults
- Page table mapping with correct permissions
- Stack growth detection and automatic extension
- **Copy-on-write fault handling**
- Kernel panic on unrecoverable kernel faults
- Detailed fault logging to serial console
- Graceful task termination on unrecoverable user faults

**Fault Types:**
| Type | Description | Handling |
|------|-------------|----------|
| TRANSLATION | Page not mapped | Demand paging if valid VMA |
| PERMISSION | Write to read-only | COW copy if COW page, else terminate |
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

**COW Fault Flow:**

```
1. Permission fault on write to read-only page
2. Check if page is marked COW
3. If COW and refcount == 1: just make writable
4. If COW and refcount > 1:
   a. Allocate new physical page
   b. Copy contents from original
   c. Map new page with write permission
   d. Decrement original page refcount
5. Resume execution
```

**Handled Scenarios:**

- Heap access before sbrk extends region
- Stack growth into guard area
- First access to anonymous memory
- Access to uninitialized data segment
- Write to shared COW page

---

### 7. Kernel Heap (`kheap.cpp`, `kheap.hpp`)

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

- Memory pools for specific subsystems
- Memory pressure callbacks
- Memory leak detection
- Per-CPU caches for reduced contention

**Recommendations:**

- Implement memory pressure notification
- Consider per-CPU free lists for scalability

---

### 8. Copy-on-Write (`cow.cpp`, `cow.hpp`)

**Status:** Complete page sharing with reference counting

**Implemented:**

- Per-page reference counting
- COW page metadata tracking
- Reference increment/decrement with atomic safety
- COW state marking per page
- Automatic page freeing when refcount reaches zero
- Integration with page fault handler

**CowManager API:**
| Method | Description |
|--------|-------------|
| `init(max_pages)` | Initialize COW tracking |
| `inc_ref(phys)` | Increment page refcount |
| `dec_ref(phys)` | Decrement refcount, returns true if page should be freed |
| `get_ref(phys)` | Get current refcount |
| `mark_cow(phys)` | Mark page as COW |
| `clear_cow(phys)` | Clear COW marking |
| `is_cow(phys)` | Check if page is COW |

**Memory Overhead:**

- 4 bytes per physical page (2-byte refcount + 2-byte flags)
- ~64KB for 128MB RAM (32K pages)

**Use Cases:**

- Efficient fork() implementation
- Shared read-only pages between processes
- Deferred copy semantics

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

| File        | Lines | Description                      |
|-------------|-------|----------------------------------|
| `pmm.cpp`   | ~305  | Physical page allocator (bitmap) |
| `pmm.hpp`   | ~31   | PMM interface                    |
| `buddy.cpp` | ~258  | Buddy allocator                  |
| `buddy.hpp` | ~87   | Buddy interface                  |
| `slab.cpp`  | ~334  | Slab allocator                   |
| `slab.hpp`  | ~35   | Slab interface                   |
| `vmm.cpp`   | ~272  | Virtual memory manager           |
| `vmm.hpp`   | ~53   | VMM interface and PTE flags      |
| `vma.cpp`   | ~296  | Virtual memory areas             |
| `vma.hpp`   | ~85   | VMA interface                    |
| `fault.cpp` | ~462  | Page fault handler               |
| `fault.hpp` | ~32   | Fault interface                  |
| `cow.cpp`   | ~145  | Copy-on-write manager            |
| `cow.hpp`   | ~67   | COW interface                    |
| `kheap.cpp` | ~517  | Kernel heap allocator            |
| `kheap.hpp` | ~86   | Heap interface                   |

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

## Shared Memory (Microkernel IPC)

**Location:** `kernel/kobj/shm.cpp`, `kernel/kobj/shm.hpp`
**SLOC:** ~150

Shared memory provides efficient large data transfer between user-space processes in the microkernel architecture.

### Syscalls

| Syscall    | Number | Description                  |
|------------|--------|------------------------------|
| shm_create | 0x110  | Create shared memory region  |
| shm_map    | 0x111  | Map SHM into address space   |
| shm_unmap  | 0x112  | Unmap SHM from address space |
| shm_close  | 0x113  | Close SHM handle             |

### ShmRegion Structure

| Field     | Type | Description                |
|-----------|------|----------------------------|
| id        | u32  | Unique region ID           |
| phys_addr | u64  | Physical memory address    |
| size      | u64  | Region size (page-aligned) |
| refcount  | u32  | Reference count            |

### Usage Pattern (Server IPC)

```cpp
// Client: Create SHM for large write
u32 shm = sys::shm_create(size);
void *addr = sys::shm_map(shm);
memcpy(addr, data, size);
// Send shm handle via channel
sys::channel_send(server, &msg, sizeof(msg), &shm, 1);

// Server: Receive and map SHM
u32 client_shm;
sys::channel_recv(ch, &msg, sizeof(msg), &client_shm, &count);
void *data = sys::shm_map(client_shm);
// Process data...
sys::shm_unmap(client_shm);
sys::shm_close(client_shm);
```

---

## Priority Recommendations: Next 5 Steps

### 1. mmap() for File-Backed Mappings

**Impact:** Standard POSIX memory-mapped I/O

- Map file contents directly into virtual memory
- VMA tracking with file inode and offset
- Page fault handler reads from file on demand
- Enables memory-mapped file I/O for databases and large files

### 2. Multiple Memory Regions from Device Tree/UEFI

**Impact:** Correct memory management on real hardware

- Parse multiple USABLE_RAM regions from boot info
- Non-contiguous memory support in PMM
- Proper handling of holes in physical address space
- Required for systems with memory above 4GB

### 3. Page Table Garbage Collection

**Impact:** Memory efficiency for long-running processes

- Track empty intermediate page tables (L1/L2/L3)
- Free unused tables when all entries unmapped
- Reclaim memory from destroyed address spaces
- Reduces memory fragmentation over time

### 4. Large Page Support (2MB, 1GB)

**Impact:** Performance improvement for large allocations

- Block descriptor mapping at L1 (1GB) and L2 (2MB)
- Reduced TLB pressure for large regions
- Use for framebuffer, DMA buffers, kernel direct map
- Significant performance gain for memory-intensive workloads

### 5. Memory Pressure Notifications

**Impact:** Graceful handling of low memory conditions

- Callback system when free pages below threshold
- Cache trimming (block cache, inode cache, slab)
- OOM killer for memory exhaustion
- Prevents system lockup under memory pressure
