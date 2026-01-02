# ViperOS Kernel Refactoring Plan

**Document Version:** 3.0
**Date:** 2026-01-01
**Scope:** Comprehensive kernel code quality review (Updated with thorough re-analysis)

---

## Implementation Progress

### Summary (as of 2026-01-01)
- **Completed:** 10 tasks
- **Deferred:** 2 tasks (complex multi-day efforts with high risk)
- **Estimated lines refactored:** ~500 lines improved/consolidated
- **New findings from comprehensive review:** 100+ additional items identified

| Task | Status | Date | Notes |
|------|--------|------|-------|
| Create `kernel/include/constants.hpp` | DONE | 2026-01-01 | Created with 14 namespaces |
| Migrate constants from individual files | DONE | 2026-01-01 | Migrated gcon.cpp, serial.cpp, blk.cpp to use kc:: aliases |
| Split `kernel_main()` | DONE | 2026-01-01 | Extracted init_memory_subsystem(), init_interrupts(), init_task_subsystem(), init_virtio_subsystem(), init_network_subsystem() |
| Extract signal state init helper | DONE | 2026-01-01 | Added init_signal_state() in task.cpp |
| Replace byte-copy loops with memcpy | DONE | 2026-01-01 | Updated channel.cpp to use lib::memcpy |
| Extract page table walker | DONE | 2026-01-01 | Added walk_tables_readonly() in vmm.cpp, simplified unmap_page/unmap_block_2mb |
| Create RAII wrappers | DONE | 2026-01-01 | Added InodeGuard in viperfs.hpp for automatic inode release |
| Extract VirtIO init pattern | DONE | 2026-01-01 | Added basic_init() to Device class, updated blk/gpu/rng drivers |
| Extract LRU cache template | DEFERRED | | Complex multi-day effort - BlockCache/InodeCache differ too much |
| Split syscall/table.cpp | REVIEWED | 2026-01-01 | File already organized with 17 section comments; full split deferred (risky, file works well as-is) |
| Add unified error codes | DONE | 2026-01-01 | Removed duplicate result.hpp, added filesystem error codes to error::Code |

This document identifies refactoring opportunities across the entire ViperOS kernel codebase, including code duplication, large functions, readability issues, and microkernel architecture violations.

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Microkernel Architecture Violations](#2-microkernel-architecture-violations)
3. [Large Functions Requiring Decomposition](#3-large-functions-requiring-decomposition)
4. [Code Duplication](#4-code-duplication)
5. [Magic Numbers Consolidation Plan](#5-magic-numbers-consolidation-plan)
6. [Complete Magic Numbers Inventory](#6-complete-magic-numbers-inventory)
7. [Readability Issues](#7-readability-issues)
8. [Error Handling Inconsistencies](#8-error-handling-inconsistencies)
9. [Performance Anti-Patterns](#9-performance-anti-patterns)
10. [Header Organization Issues](#10-header-organization-issues)
11. [Recommended Refactoring Priority](#11-recommended-refactoring-priority)

---

## 1. Executive Summary

### Key Findings

| Category | Count | Severity |
|----------|-------|----------|
| Microkernel violations | 4 major | Critical |
| Functions >50 lines | 25+ | High |
| Code duplication instances | 30+ | Medium-High |
| Magic number occurrences | 200+ | Medium |
| Inconsistent error handling | Throughout | Medium |

### Immediate Actions Required

1. **Constants**: Consolidate all magic numbers into `kernel/include/constants.hpp`
2. **Architecture**: Move filesystem (ViperFS) to user-space server
3. **Architecture**: Evaluate moving VirtIO drivers to user-space
4. **Code Quality**: Decompose `kernel_main()` (990 lines)
5. **Code Quality**: Extract common patterns (LRU cache, page table walking, VirtIO init)

---

## 2. Microkernel Architecture Violations

ViperOS is designed as a microkernel, but several subsystems violate this principle by residing in kernel space.

### 2.1 Filesystem in Kernel (CRITICAL)

**Location:** `kernel/fs/` entire subsystem (~2000 lines)

**Issue:** Complete ViperFS implementation lives in kernel space. Traditional microkernels (Mach, QNX, seL4) move filesystems to user-space servers.

**Impact:**
- Increases kernel complexity and attack surface
- Reduces modularity and replaceability
- Violates microkernel principle of minimal kernel
- Makes debugging filesystem issues harder (crashes affect entire system)

**Files Affected:**
- `kernel/fs/viperfs.cpp` (2000+ lines)
- `kernel/fs/viperfs.hpp`
- `kernel/fs/cache.cpp` (267 lines)
- `kernel/fs/cache.hpp`
- `kernel/fs/journal.cpp`
- `kernel/vfs/vfs.cpp` (991 lines)

**Recommendation:** Plan filesystem migration to user-space as a long-term architectural goal. Create an abstract filesystem interface layer to decouple VFS from ViperFS.

### 2.2 Block Device Testing in kernel_main()

**Location:** `kernel/main.cpp:439-712` (274 lines)

**Issue:** Full filesystem mount, file I/O, and persistence code in kernel initialization.

**Details:**
- `virtio::blk_device()->read_sectors()`
- Write operations
- ViperFS mount and readdir operations mixed with kernel bring-up

**Recommendation:** Move to test harness or init process in user-space.

### 2.3 TLS Session State in Syscall Dispatcher

**Location:** `kernel/syscall/table.cpp:218-223`

```cpp
#if VIPER_KERNEL_ENABLE_TLS
static constexpr int MAX_TLS_SESSIONS = 16;
static viper::tls::TlsSession tls_sessions[MAX_TLS_SESSIONS];
static bool tls_session_active[MAX_TLS_SESSIONS] = {false};
static kernel::Spinlock tls_lock;
#endif
```

**Issue:** TLS state management in kernel syscall dispatcher is inappropriate for microkernel. TLS should be user-space library; kernel provides only syscall bridges.

### 2.4 VirtIO Drivers in Kernel

**Location:** `kernel/drivers/virtio/` (blk.cpp, gpu.cpp, rng.cpp, input.cpp)

**Issue:** Device drivers sit in kernel space. While some microkernels keep simple drivers in kernel for performance, complex drivers like GPU should be user-space.

**Recommendation:** Review architecture decision. Consider moving GPU and complex drivers to user-space with minimal in-kernel virtqueue support.

---

## 3. Large Functions Requiring Decomposition

### 3.1 kernel/main.cpp

#### `kernel_main()` - 990 lines (lines 118-1107) - MONOLITHIC

**Issues:**
- Single entry point contains initialization for 15+ subsystems sequentially
- No separation of concerns: memory init, exceptions, task system, virtio, network, filesystem, Viper
- Repeated patterns: Serial logging on every subsystem initialization
- Graphics console output duplication (lines 188-237, 305-323, 769-780, 1080-1087)
- Dead code potential: Block device/filesystem tests mixed with actual initialization

**Refactoring:**
```cpp
// Extract into focused init functions:
void init_memory_subsystem();
void init_exception_handlers();
void init_task_system();
void init_virtio_devices();
void init_filesystem();
void init_graphics_console();
void run_init_tests();  // Optional, conditional

void kernel_main() {
    init_memory_subsystem();
    init_exception_handlers();
    init_task_system();
    init_virtio_devices();
    init_filesystem();
    init_graphics_console();
    #ifdef KERNEL_RUN_INIT_TESTS
    run_init_tests();
    #endif
    scheduler::start();
}
```

### 3.2 kernel/syscall/table.cpp

**File Size:** 4096 lines with ~150 syscall handlers defined inline

**Issue:** All syscall implementations crammed into one file; no logical grouping. Expected: 500-800 lines max with clear sections.

**Refactoring:** Split into:
- `syscall_file.cpp` - File operations
- `syscall_process.cpp` - Process/task operations
- `syscall_ipc.cpp` - IPC operations
- `syscall_memory.cpp` - Memory operations
- `syscall_network.cpp` - Network operations

### 3.3 kernel/fs/viperfs.cpp

| Function | Lines | Issue |
|----------|-------|-------|
| `write_data()` | 60 | Combines allocation, indirect setup, writing |
| `free_inode_blocks()` | 72 | 3 levels of nested loops for indirection |
| `set_block_ptr()` | 51 | Complex conditional for 3 indirection levels |
| `create_file()` | 89 | Mixes allocation, init, journaling, dir entry |
| `create_dir()` | 104 | Even larger with `.` and `..` entry creation |
| `rename()` | 90 | Complex state management |
| `add_dir_entry()` | 82 | Complex buffer manipulation |
| `remove_dir_entry()` | 68 | Nested scanning loops |

**Refactoring for `free_inode_blocks()`:**
```cpp
// Extract per-level helpers:
void free_direct_blocks(Inode *inode);
void free_indirect_blocks(u64 indirect_block);
void free_double_indirect_blocks(u64 double_indirect_block);

void free_inode_blocks(Inode *inode) {
    free_direct_blocks(inode);
    if (inode->indirect) free_indirect_blocks(inode->indirect);
    if (inode->double_indirect) free_double_indirect_blocks(inode->double_indirect);
}
```

### 3.4 kernel/vfs/vfs.cpp

| Function | Lines | Issue |
|----------|-------|-------|
| `normalize_path()` | 115 | Complex path component stack management |
| `open()` | 66 | Mixes resolution, creation, flag handling |

### 3.5 kernel/sched/scheduler.cpp

| Function | Lines | Issue |
|----------|-------|-------|
| `dequeue_locked()` | 29 | Duplicated in `dequeue_percpu_locked()` |
| `enqueue_locked()` | 36 | Duplicated in `enqueue_percpu_locked()` |
| Task stealing algorithm | 52 | Nested loops with magic behavior |

### 3.6 kernel/mm/vmm.cpp

| Function | Lines | Issue |
|----------|-------|-------|
| `map_page()` | 42 | Table walking duplicated elsewhere |
| `map_block_2mb()` | 57 | Similar logic to `map_page()` |
| `virt_to_phys()` | 42 | Duplicated table walking |

### 3.7 kernel/drivers/

| File | Function | Lines | Issue |
|------|----------|-------|-------|
| `blk.cpp` | `init()` | 100+ | Device init with many concerns |
| `gpu.cpp` | `init()` | 120+ | Similar pattern to blk |
| `gic.cpp` | Multiple | 600+ | Entire file needs decomposition |

---

## 4. Code Duplication

### 4.1 Byte-Copy Loops in IPC (Should Use memcpy)

**Location:** `kernel/ipc/channel.cpp`

**Instances:**
- Lines 286-289: Message data copy
- Lines 370-373: Same copy operation
- Lines 439-442: Duplicated in helper function
- Lines 587: Fourth instance in legacy function

```cpp
// Current (4 instances):
for (u32 i = 0; i < size; i++) {
    msg->data[i] = static_cast<const u8 *>(data)[i];
}

// Should be:
memcpy(msg->data, data, size);
```

### 4.2 Linear Search Patterns in IPC

**Location:** `kernel/ipc/channel.cpp` and `kernel/ipc/poll.cpp`

| Function | Location | Issue |
|----------|----------|-------|
| `channel::get()` | lines 63-71 | O(n) scan for channel |
| `channel::find_free_slot()` | lines 79-87 | O(n) scan for free slot |
| `channel::find_channel_by_id_locked()` | lines 95-103 | O(n) scan |
| `poll::find_timer()` | lines 101-111 | O(n) called in hot path |
| `poll::register_wait()` | lines 340-357 | O(n) scan |
| `poll::unregister_wait()` | lines 386-399 | O(n) scan |

**Recommendation:** Implement handle tables with O(1) lookup, or use bitmaps for free slot allocation.

### 4.3 Scheduler Queue Operations

**Location:** `kernel/sched/scheduler.cpp`

**Duplicated Pairs:**
- `any_ready_locked()` (100-108) ↔ `any_ready_percpu()` (330-344)
- `dequeue_locked()` (154-183) ↔ `dequeue_percpu_locked()` (231-267)
- `enqueue_locked()` (113-149) ↔ `enqueue_percpu_locked()` (189-225)

**Refactoring:**
```cpp
// Single parameterized function:
template<typename QueueArray>
Task *dequeue_from_queues(QueueArray &queues, u8 num_queues);

// Used by both:
Task *dequeue_locked() { return dequeue_from_queues(priority_queues, NUM_PRIORITY_QUEUES); }
Task *dequeue_percpu_locked(u32 cpu) { return dequeue_from_queues(per_cpu_sched[cpu].queues, NUM_PRIORITY_QUEUES); }
```

### 4.4 Signal State Initialization

**Location:** `kernel/sched/task.cpp`

**Duplicated at:**
- Lines 234-262 (idle task)
- Lines 361-369 (normal task)

```cpp
// Extract helper:
void init_signal_state(Task::Signals &signals) {
    for (int i = 0; i < 32; i++) {
        signals.handlers[i] = 0;
        signals.handler_flags[i] = 0;
        signals.handler_mask[i] = 0;
    }
    signals.blocked = 0;
    signals.pending = 0;
}
```

### 4.5 VirtIO Driver Initialization Pattern

**Location:** All VirtIO drivers (`blk.cpp`, `gpu.cpp`, `rng.cpp`, `input.cpp`)

**Repeated Pattern:**
```cpp
// Find device
u64 base = find_device(device_type::XXX);
if (!base) { serial::puts(...); return false; }
// Initialize base
if (!Device::init(base)) { serial::puts(...); return false; }
// Reset
reset();
// Set guest page size (legacy only)
if (is_legacy()) write32(reg::GUEST_PAGE_SIZE, 4096);
// Add status bits
add_status(status::ACKNOWLEDGE);
add_status(status::DRIVER);
// Negotiate features
if (!negotiate_features(...)) { ... return false; }
// Mark ready
add_status(status::DRIVER_OK);
```

**Refactoring:**
```cpp
class VirtioDeviceInitializer {
    Device *dev;
public:
    VirtioDeviceInitializer(u32 type);
    bool init_with_features(u64 required);
    Device *get() { return dev; }
};

// Usage:
VirtioDeviceInitializer init(device_type::BLK);
if (!init.init_with_features(required_features)) return false;
```

### 4.6 Page Table Walking (VMM)

**Location:** `kernel/mm/vmm.cpp`

**Five separate implementations:**
- `map_page()` walks L0→L1→L2→L3
- `map_block_2mb()` walks L0→L1→L2
- `unmap_block_2mb()` walks L0→L1→L2
- `unmap_page()` walks L0→L1→L2→L3
- `virt_to_phys()` walks L0→L1→L2→L3

**Refactoring:**
```cpp
// Generic table walker:
u64 *walk_page_tables(u64 virt, int target_level,
                      bool allocate_if_missing = false,
                      TableAllocation *alloc = nullptr);

// Usage:
bool map_page(u64 virt, u64 phys, u64 flags) {
    TableAllocation alloc;
    u64 *l3e = walk_page_tables(virt, 3, true, &alloc);
    if (!l3e) { alloc.rollback(); return false; }
    *l3e = (phys & PHYS_MASK) | flags;
    invalidate_page(virt);
    return true;
}
```

### 4.7 LRU Cache Implementation (Filesystem)

**Location:** `kernel/fs/cache.cpp` and `kernel/fs/viperfs.hpp`

**Issue:** `BlockCache` and `InodeCache` have nearly identical implementations:
- Both implement `find()`, `evict()`, `touch()`
- Both implement `insert_hash()`, `remove_hash()`
- Both implement `remove_from_lru()`, `add_to_lru_head()`
- ~200+ lines duplicated

**Refactoring:**
```cpp
template<typename Key, typename Value>
class LRUCache {
    // Hash table + LRU list + eviction policy
};

using BlockCache = LRUCache<u64, CacheBlock>;
using InodeCache = LRUCache<u64, CachedInode>;
// Eliminates ~400 lines of duplication
```

### 4.8 Directory Scanning

**Location:** `kernel/fs/viperfs.cpp`

**Three similar implementations:**
- `lookup()` (754-808)
- `readdir()` (811-850)
- `remove_dir_entry()` (1625-1694)

**Refactoring:**
```cpp
// Unified scanner:
i32 scan_dir_entries(Inode *dir, u64 offset, Callback<DirEntry> for_each);
```

### 4.9 Path Resolution

**Location:** `kernel/vfs/vfs.cpp`

**Three similar path-walking implementations:**
- `resolve_path()` (79-137)
- `resolve_parent()` (156-205)
- `normalize_path()` (876-991)

**Refactoring:**
```cpp
struct PathWalker {
    using ComponentCallback = bool(*)(const char* name, usize len, void* ctx);
    static bool walk(const char* path, ComponentCallback cb, void* ctx);
};
```

### 4.10 Number Printing (kernel_main)

**Location:** `kernel/main.cpp`

**Duplicated digit extraction logic:**
- Lines 223-232
- Lines 317-322
- Lines 772-774

```cpp
// Pattern: '0' + (val / divisor) % 10
// Should extract to utility function
```

---

## 5. Magic Numbers Consolidation Plan

### 5.1 Goal

**Consolidate ALL magic numbers into a single header file**: `kernel/include/constants.hpp`

Exceptions (constants that should remain in their subsystem headers):
- Subsystem-internal constants only used in one file
- Hardware register offsets that form a logical group (e.g., GIC registers in `gic.hpp`)
- File format constants (e.g., ViperFS magic in `format.hpp`)

### 5.2 Proposed Header Structure

```cpp
// kernel/include/constants.hpp
#pragma once
#include <cstdint>

namespace kernel::constants {

// =============================================================================
// SECTION 1: MEMORY LAYOUT (QEMU virt machine)
// =============================================================================
namespace mem {
    constexpr u64 RAM_BASE           = 0x40000000;   // QEMU virt RAM start
    constexpr u64 RAM_SIZE           = 128 * 1024 * 1024;  // 128MB
    constexpr u64 FB_BASE            = 0x41000000;   // Framebuffer
    constexpr u64 FB_SIZE            = 8 * 1024 * 1024;    // 8MB max
    constexpr u64 STACK_POOL_BASE    = 0x44000000;   // Kernel stack pool
    constexpr u64 USER_CODE_BASE     = 0x80000000;   // 2GB user space start
    constexpr u64 KERNEL_SPACE_START = 0xFFFF000000000000ULL;
}

// =============================================================================
// SECTION 2: HARDWARE DEVICE ADDRESSES
// =============================================================================
namespace hw {
    // UART
    constexpr u64 UART_BASE = 0x09000000;
    constexpr u32 UART_IRQ  = 33;

    // GIC (Generic Interrupt Controller)
    constexpr u64 GICD_BASE   = 0x08000000;  // Distributor
    constexpr u64 GICC_BASE   = 0x08010000;  // CPU Interface
    constexpr u64 GICR_BASE   = 0x080A0000;  // Redistributor
    constexpr u64 GICR_STRIDE = 0x20000;     // 128KB per CPU

    // Firmware Config
    constexpr u64 FWCFG_BASE = 0x09020000;

    // VirtIO MMIO
    constexpr u64 VIRTIO_MMIO_BASE   = 0x0a000000;
    constexpr u64 VIRTIO_DEVICE_SIZE = 0x200;
    constexpr u32 VIRTIO_IRQ_BASE    = 48;  // IRQs 48-79 for 32 devices
    constexpr u32 VIRTIO_MAX_DEVICES = 32;

    // RTC and GPIO
    constexpr u64 RTC_BASE  = 0x09010000;
    constexpr u32 RTC_IRQ   = 34;
    constexpr u64 GPIO_BASE = 0x09030000;
    constexpr u32 GPIO_IRQ  = 35;
}

// =============================================================================
// SECTION 3: PAGE AND BLOCK SIZES
// =============================================================================
namespace page {
    constexpr u64 SIZE       = 4096;
    constexpr u64 SHIFT      = 12;
    constexpr u64 MASK       = SIZE - 1;
    constexpr u64 BLOCK_2MB  = 2 * 1024 * 1024;
}

namespace block {
    constexpr u64 SECTOR_SIZE = 512;
    constexpr u64 BLOCK_SIZE  = 4096;
}

// =============================================================================
// SECTION 4: KERNEL LIMITS
// =============================================================================
namespace limits {
    // Stack sizes
    constexpr u64 KERNEL_STACK_SIZE = 16 * 1024;   // 16KB
    constexpr u64 USER_STACK_SIZE   = 1024 * 1024; // 1MB
    constexpr u64 GUARD_PAGE_SIZE   = 4096;

    // Memory limits
    constexpr u64 DEFAULT_MEMORY_LIMIT = 64 * 1024 * 1024;  // 64MB per process
    constexpr u64 MAX_ALLOCATION_SIZE  = 16 * 1024 * 1024;  // 16MB max alloc

    // Path and string limits
    constexpr u32 MAX_PATH        = 256;
    constexpr u32 MAX_ASSIGN_NAME = 31;

    // IPC limits
    constexpr u32 MAX_CHANNELS         = 64;
    constexpr u32 MAX_MSG_SIZE         = 256;
    constexpr u32 MAX_HANDLES_PER_MSG  = 4;
    constexpr u32 DEFAULT_PENDING_MSGS = 16;
    constexpr u32 MAX_POLL_EVENTS      = 16;

    // Scheduler limits
    constexpr u32 MAX_TASKS = 256;
    constexpr u32 MAX_CPUS  = 8;

    // Filesystem limits
    constexpr u32 MAX_DIRECT_BLOCKS    = 12;
    constexpr u32 INODE_CACHE_SIZE     = 256;
    constexpr u32 BLOCK_CACHE_SIZE     = 64;
    constexpr u32 MAX_ASSIGNS          = 64;

    // Capability limits
    constexpr u32 DEFAULT_CAP_CAPACITY = 256;
    constexpr u32 DEFAULT_HANDLE_LIMIT = 1024;

    // IRQ limits
    constexpr u32 MAX_IRQS = 256;
}

// =============================================================================
// SECTION 5: SPECIAL HANDLES AND SENTINELS
// =============================================================================
namespace handle {
    constexpr u32 INVALID        = 0xFFFFFFFF;
    constexpr u32 NO_PARENT      = 0xFFFFFFFF;
    constexpr u32 CONSOLE_INPUT  = 0xFFFF0001;  // Pseudo-handle
    constexpr u32 NETWORK_RX     = 0xFFFF0002;  // Pseudo-handle
}

// =============================================================================
// SECTION 6: DISPLAY AND GRAPHICS
// =============================================================================
namespace display {
    constexpr u32 DEFAULT_WIDTH  = 1024;
    constexpr u32 DEFAULT_HEIGHT = 768;
    constexpr u32 DEFAULT_BPP    = 32;

    // Console layout
    constexpr u32 BORDER_WIDTH   = 20;
    constexpr u32 BORDER_PADDING = 8;
    constexpr u32 TEXT_INSET     = BORDER_WIDTH + BORDER_PADDING;

    // Font metrics
    constexpr u32 FONT_BASE_WIDTH  = 8;
    constexpr u32 FONT_BASE_HEIGHT = 16;
    constexpr u32 FONT_SCALE_NUM   = 3;
    constexpr u32 FONT_SCALE_DEN   = 2;

    // Cursor
    constexpr u32 CURSOR_BLINK_MS = 500;
}

// =============================================================================
// SECTION 7: TIMING
// =============================================================================
namespace timing {
    constexpr u32 DEFAULT_NETWORK_TIMEOUT_MS = 5000;
    constexpr u32 PING_TIMEOUT_MS            = 3000;
    constexpr u32 INTERRUPT_WAIT_ITERATIONS  = 100000;

    // Timer wheel
    constexpr u32 TIMER_WHEEL_SLOTS = 256;
}

// =============================================================================
// SECTION 8: DEBUG MAGIC NUMBERS
// =============================================================================
namespace magic {
    constexpr u32 HEAP_ALLOCATED = 0xCAFEBABE;
    constexpr u32 HEAP_FREED     = 0xDEADBEEF;
    constexpr u32 HEAP_POISONED  = 0xFEEDFACE;
    constexpr u32 VIPERFS_MAGIC  = 0x53465056;  // "VPFS"
    constexpr u32 JOURNAL_MAGIC  = 0x4A524E4C;  // "JRNL"
    constexpr u32 FDT_MAGIC      = 0xD00DFEED;
    constexpr u32 FWCFG_QEMU     = 0x554D4551;  // "QEMU"
}

// =============================================================================
// SECTION 9: COLORS (ARGB format)
// =============================================================================
namespace color {
    constexpr u32 BLACK         = 0xFF000000;
    constexpr u32 RED           = 0xFFCC3333;
    constexpr u32 GREEN         = 0xFF00AA44;  // VIPER_GREEN
    constexpr u32 YELLOW        = 0xFFCCAA00;
    constexpr u32 BLUE          = 0xFF3366CC;
    constexpr u32 MAGENTA       = 0xFFCC33CC;
    constexpr u32 CYAN          = 0xFF33CCCC;
    constexpr u32 WHITE         = 0xFFEEEEEE;
    constexpr u32 GRAY          = 0xFF666666;
    constexpr u32 BRIGHT_WHITE  = 0xFFFFFFFF;

    // Theme colors
    constexpr u32 VIPER_GREEN      = 0xFF00AA44;
    constexpr u32 VIPER_DARK_BROWN = 0xFF1A1208;
    constexpr u32 VIPER_YELLOW     = 0xFFFFDD00;
    constexpr u32 VIPER_RED        = 0xFFCC3333;
}

} // namespace kernel::constants
```

### 5.3 Migration Strategy

1. **Create `kernel/include/constants.hpp`** with all constants
2. **Update each source file** to include and use the new header
3. **Remove duplicate definitions** from individual files
4. **Use namespace aliases** for convenience:
   ```cpp
   namespace kc = kernel::constants;
   // Then use: kc::mem::RAM_BASE, kc::limits::MAX_PATH, etc.
   ```

### 5.4 Constants That Should STAY in Place

These constants are tightly coupled to their subsystems and should remain:

| Location | Constants | Reason |
|----------|-----------|--------|
| `gic.hpp` | GIC register offsets (GICD_*, GICC_*, GICR_*) | 25+ hardware offsets form logical group |
| `virtio.hpp` | VirtIO register offsets | Device-specific hardware interface |
| `format.hpp` | ViperFS on-disk format | Filesystem format specification |
| `fdt.hpp` | FDT tokens (BEGIN_NODE, END_NODE, etc.) | Device tree parsing |
| `signal.hpp` | Signal numbers (SIGHUP, SIGINT, etc.) | POSIX compatibility |
| `rights.hpp` | Capability right bits | Cap system internal |

---

## 6. Complete Magic Numbers Inventory

This section provides a complete audit of all magic numbers found in the kernel.

### 6.1 Memory Addresses (60+ instances)

| Value | Current Location | Proposed Constant |
|-------|------------------|-------------------|
| `0x09000000` | serial.cpp:31 | `hw::UART_BASE` |
| `0x08000000` | gic.cpp:37 | `hw::GICD_BASE` |
| `0x08010000` | gic.cpp:40 | `hw::GICC_BASE` |
| `0x080A0000` | gic.cpp:44 | `hw::GICR_BASE` |
| `0x09020000` | fwcfg.cpp:25 | `hw::FWCFG_BASE` |
| `0x40000000` | main.cpp:253, bootinfo.cpp:172 | `mem::RAM_BASE` |
| `0x41000000` | ramfb.cpp:56, pmm.cpp:175 | `mem::FB_BASE` |
| `0x44000000` | task.cpp:92 | `mem::STACK_POOL_BASE` |
| `0x80000000` | viper.hpp:147 | `mem::USER_CODE_BASE` |
| `0x0a000000` | syscall/table.cpp:2942-2958 | `hw::VIRTIO_MMIO_BASE` |
| `0x20000` | gic.cpp:45 | `hw::GICR_STRIDE` |

### 6.2 Sizes and Limits (50+ instances)

| Value | Current Location | Proposed Constant |
|-------|------------------|-------------------|
| `4096` | Many files | `page::SIZE` |
| `4095` | loader.cpp:91, slab.cpp:81 | `page::MASK` |
| `512` | blk.hpp:308 | `block::SECTOR_SIZE` |
| `256` | console.hpp:27, cap/table.hpp:103 | Various limits |
| `1024` | console.hpp:24 | `limits::INPUT_BUFFER_SIZE` |
| `16384` / `16 * 1024` | task.hpp:97, cpu.hpp:22 | `limits::KERNEL_STACK_SIZE` |
| `1048576` / `1 * 1024 * 1024` | viper.hpp:157 | `limits::USER_STACK_SIZE` |
| `67108864` / `64 * 1024 * 1024` | viper.hpp:162, viper.cpp:75,191 | `limits::DEFAULT_MEMORY_LIMIT` |
| `134217728` / `128 * 1024 * 1024` | main.cpp:254, bootinfo.cpp:173 | `mem::RAM_SIZE` |
| `8388608` / `8 * 1024 * 1024` | ramfb.cpp:59 | `mem::FB_SIZE` |
| `16777216` / `16 * 1024 * 1024` | table.cpp:2996,3309 | `limits::MAX_ALLOCATION_SIZE` |

### 6.3 IRQ Numbers (20+ instances)

| Value | Current Location | Proposed Constant |
|-------|------------------|-------------------|
| `30` | table.cpp:2392 | Timer0 IRQ |
| `32` | Various | SPI start boundary |
| `33` | table.cpp device table | `hw::UART_IRQ` |
| `34` | table.cpp device table | `hw::RTC_IRQ` |
| `35` | table.cpp device table | `hw::GPIO_IRQ` |
| `48-79` | Various | `hw::VIRTIO_IRQ_BASE` + index |
| `256` | gic.hpp:52 | `limits::MAX_IRQS` |

### 6.4 Hardware Register Offsets

**UART (serial.cpp):**
| Offset | Register | Status |
|--------|----------|--------|
| `0x00` | UART_DR | Already constexpr |
| `0x18` | UART_FR | Already constexpr |

**GIC Distributor (gic.cpp):** 15+ offsets - keep in `gic.hpp`

**GIC CPU Interface (gic.cpp):** 5 offsets - keep in `gic.hpp`

**GIC Redistributor (gic.cpp):** 10 offsets - keep in `gic.hpp`

### 6.5 Bit Positions and Masks (40+ instances)

| Value | Current Location | Purpose |
|-------|------------------|---------|
| `1 << 4` | serial.cpp:38 | UART_FR_RXFE |
| `1 << 5` | serial.cpp:39 | UART_FR_TXFF |
| `1 << 0..12` | rights.hpp | CAP_* permissions |
| `0x1FF` | address_space.cpp:322 | Page table index mask |
| `39, 30, 21, 12` | address_space.cpp | VA bit positions |

### 6.6 Debug/Sentinel Values

| Value | Current Location | Purpose |
|-------|------------------|---------|
| `0xCAFEBABE` | kheap.cpp:49 | Allocated block marker |
| `0xDEADBEEF` | kheap.cpp:50, main.cpp:919 | Freed block marker / test data |
| `0xFEEDFACE` | kheap.cpp:51 | Poisoned block marker |
| `0xFFFFFFFF` | handle.hpp:30, table.hpp:59 | HANDLE_INVALID / NO_PARENT |
| `0xFFFF0001` | poll.hpp:35 | HANDLE_CONSOLE_INPUT |
| `0xFFFF0002` | poll.hpp:44 | HANDLE_NETWORK_RX |

### 6.7 File Format Magic Numbers

| Value | Current Location | Purpose |
|-------|------------------|---------|
| `0x53465056` | format.hpp:25 | VIPERFS_MAGIC ("VPFS") |
| `0x4A524E4C` | format.hpp:76 | JOURNAL_MAGIC ("JRNL") |
| `0xD00DFEED` | fdt.hpp:21 | FDT_MAGIC |
| `0x554D4551` | fwcfg.cpp:53 | FWCFG_SIGNATURE ("QEMU") |
| `0x34325258` | ramfb.cpp:34 | DRM_FORMAT_XRGB8888 |

### 6.8 Display and Graphics

| Value | Current Location | Purpose |
|-------|------------------|---------|
| `1024, 768` | main.cpp:173 | Default resolution |
| `20` | gcon.cpp:38 | BORDER_WIDTH |
| `8` | gcon.cpp:39 | BORDER_PADDING |
| `28` | gcon.cpp:40 | TEXT_INSET |
| `8, 16` | font.hpp:22-23 | Font base dimensions |
| `3/2` | font.hpp:27-28 | Font scale ratio |
| `500` | gcon.cpp:52 | Cursor blink interval (ms) |

### 6.9 Timing Values

| Value | Current Location | Purpose |
|-------|------------------|---------|
| `3000` | main.cpp:391 | ICMP ping timeout (ms) |
| `5000` | table.cpp:2316 | Default network timeout (ms) |
| `100000` | blk.cpp:287,439,641 | Interrupt wait iterations |
| `256` | timerwheel.hpp:31 | Level 0 slots |

### 6.10 PSCI Function IDs (cpu.cpp)

| Value | Purpose |
|-------|---------|
| `0x84000000` | PSCI_VERSION |
| `0xC4000003` | CPU_ON_64 |
| `0x84000002` | CPU_OFF |
| `0x84000008` | SYSTEM_OFF |
| `0x84000009` | SYSTEM_RESET |

### 6.11 File Flags and Permissions

| Value | Current Location | Purpose |
|-------|------------------|---------|
| `0x0000` | syscall.hpp:351 | O_RDONLY |
| `0x0001` | syscall.hpp:352 | O_WRONLY |
| `0x0002` | syscall.hpp:353 | O_RDWR |
| `0x0040` | syscall.hpp:354 | O_CREAT |
| `0x0200` | syscall.hpp:355 | O_TRUNC |
| `0x0400` | syscall.hpp:356 | O_APPEND |
| `0xF000` | format.hpp:177 | TYPE_MASK |
| `0x8000` | format.hpp:178 | TYPE_FILE |
| `0x4000` | format.hpp:179 | TYPE_DIR |
| `0xA000` | format.hpp:180 | TYPE_LINK |

### 6.12 Summary Statistics

| Category | Count | Already Constexpr | Needs Migration |
|----------|-------|-------------------|-----------------|
| Memory addresses | 15 | 12 | 3 |
| Sizes/limits | 50+ | 40 | 10 |
| IRQ numbers | 20+ | 15 | 5 |
| Register offsets | 50+ | 50 | 0 (keep in place) |
| Bit masks | 40+ | 35 | 5 |
| Debug sentinels | 6 | 6 | 0 |
| File format magic | 5 | 5 | 0 (keep in place) |
| Display/graphics | 15 | 10 | 5 |
| Timing values | 8 | 4 | 4 |
| **TOTAL** | **200+** | **~180** | **~30** |

**Current compliance: ~90%** (most constants already defined, just scattered)

**Action needed:** Consolidate 30+ scattered constants into single header.

---

## 7. Readability Issues

### 7.1 Unclear Variable Names

| Variable | Location | Better Name |
|----------|----------|-------------|
| `l1_idx`, `l2_idx` | viperfs.cpp:1071-1072 | `level1_table_index`, `level2_block_index` |
| `comp_len`, `comp_start` | vfs.cpp:942,939 | `component_length`, `component_start` |
| `r`, `h`, `f`, `p` | Throughout | Use descriptive names |
| `rec_len` | viperfs.cpp | `record_length` or document |

### 7.2 Inconsistent Comment Style

**Issues:**
- Mix of `//` and `/* */` comments
- Some functions have docstring headers, others don't
- Some commented code left in (debug paths)
- Duplicate documentation (generated vs hand-written)

### 7.3 Complex Nested Conditionals

**Location:** `viperfs.cpp:1095-1139` (write_data loop)

```cpp
// Current - allocation logic obscures write loop:
while (written < len) {
    // ... calculate offsets ...
    u64 block_num = get_block_ptr(inode, block_idx);
    if (block_num == 0) {
        // Nested allocation logic - 20+ lines
    }
    // Write to block...
}

// Better - extract helper:
while (written < len) {
    // ... calculate offsets ...
    u64 block_num = get_or_allocate_block(inode, block_idx);
    if (block_num == 0) return ...;
    // Write block...
}
```

### 7.4 Unclear Locking Scope

**Issues:**
- `_unlocked` suffix suggests caller holds lock, but not consistently enforced
- Example: `alloc_block_unlocked()` exists but no public `alloc_block()`
- SpinlockGuard pattern used inconsistently

**Recommendation:**
- Create public locked versions only
- Document locking requirements clearly
- Use RAII lock guards consistently

### 7.5 Reimplemented Standard Functions

| Function | Location | Standard Equivalent |
|----------|----------|---------------------|
| `strcpy_safe()` | task.cpp:53-62, slab.cpp | `strncpy()` with NUL |
| `memzero()` | slab.cpp | `memset(p, 0, n)` |
| `cpu_to_be32/64()` | ramfb.cpp:40-52 | `htonl()`/`htonll()` |

---

## 8. Error Handling Inconsistencies

### 8.1 Mixed Return Conventions

| Layer | Success | Failure | Issue |
|-------|---------|---------|-------|
| VFS | 0 or positive | -1 | No error codes |
| ViperFS | non-zero | 0 | Inverted from VFS |
| Cache | pointer | nullptr | Different again |

**Recommendation:** Define unified error codes:
```cpp
enum class Error {
    OK = 0,
    NOT_FOUND = -1,
    NO_SPACE = -2,
    PERMISSION_DENIED = -3,
    // ...
};
```

### 8.2 Incomplete Error Cleanup

**Location:** Various functions allocate resources, then fail without cleanup on later errors.

**Recommendation:** Use RAII wrappers:
```cpp
class InodeGuard {
    Inode *inode;
public:
    InodeGuard(u64 ino) : inode(viperfs().read_inode(ino)) {}
    ~InodeGuard() { if (inode) viperfs().release_inode(inode); }
    Inode *get() { return inode; }
    Inode *release() { Inode *tmp = inode; inode = nullptr; return tmp; }
};
```

### 8.3 TODO Comments (Incomplete Implementation)

| Location | Issue |
|----------|-------|
| `table.cpp:168` | "When user mode is implemented, also check memory is mapped" |
| `table.cpp:185` | Same TODO in `validate_user_write()` |
| `table.cpp:3132` | TODO for timeout implementation in poll |
| `vfs.cpp:259` | O_TRUNC not implemented |

### 8.4 Inconsistent Logging

**Issues:**
- Some errors print serial messages, others are silent
- No consistent log level (debug, info, warn, error)
- Tracing infrastructure duplicates serial output

**Recommendation:** Unified logging macro:
```cpp
#define KLOG_ERROR(fmt, ...) serial::printf("[ERROR] " fmt "\n", ##__VA_ARGS__)
#define KLOG_WARN(fmt, ...)  serial::printf("[WARN] " fmt "\n", ##__VA_ARGS__)
#define KLOG_DEBUG(fmt, ...) do { if (g_debug) serial::printf("[DEBUG] " fmt "\n", ##__VA_ARGS__); } while(0)
```

---

## 9. Performance Anti-Patterns

### 9.1 O(n) Hot Path Lookups

| Function | Called From | Impact |
|----------|-------------|--------|
| `find_timer()` | Every poll iteration | O(MAX_TIMERS) per poll |
| `channel::get()` | Every IPC operation | O(MAX_CHANNELS) per message |
| `wait_queue` scan | Every event notify | O(MAX_WAIT_ENTRIES) |

**Fix:** Use hash tables or handle tables for O(1) lookup.

### 9.2 Repeated Inode Loads

**Location:** `vfs.cpp:265-270`

```cpp
if (oflags & flags::O_APPEND) {
    viperfs::Inode *inode = viperfs::viperfs().read_inode(ino);
    if (inode) {
        desc->offset = inode->size;
        viperfs::viperfs().release_inode(inode);
    }
}
```

**Issue:** Loads inode just for size. Cache size in FileDesc.

### 9.3 Block Cache Too Small

**Location:** `cache.hpp:31`

```cpp
constexpr usize CACHE_BLOCKS = 64; // 256KB cache
```

**Issues:**
- 256 KB is very small
- Single miss causes full LRU scan
- No statistics on cache effectiveness
- Read-ahead may evict immediately

### 9.4 Inefficient Zeroing

**Location:** `viperfs.cpp:446-450`

```cpp
// Byte-by-byte zeroing:
for (usize i = 0; i < BLOCK_SIZE; i++) {
    block->data[i] = 0;
}

// Should be:
memset(block->data, 0, BLOCK_SIZE);
```

### 9.5 Drawing Primitives

**Location:** `ramfb.cpp:216-223`

```cpp
// Element-at-a-time write:
void fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color) {
    for (u32 j = y; j < y + h; j++) {
        for (u32 i = x; i < x + w; i++) {
            put_pixel(i, j, color);
        }
    }
}

// Better: memset per row for solid colors
```

### 9.6 TLB Invalidation

**Location:** `vmm.cpp:307-311`

```cpp
// Full TLB flush for 2MB mapping:
invalidate_all();

// Should use range-based invalidation
```

---

## 10. Header Organization Issues

### 10.1 Duplicated Constants

**Same constants defined in multiple places:**

| Constant | Location 1 | Location 2 |
|----------|------------|------------|
| PTE flags | `vmm.hpp:29-82` | `mmu.cpp:94-106` |
| TCR values | `mmu.cpp:34-70` | Possibly elsewhere |
| VirtIO regs | `virtio.hpp:36-68` | Individual drivers |

**Fix:** Create `arch/aarch64/registers.hpp` with all architecture constants.

### 10.2 Missing Utility Headers

**Common operations without shared utilities:**
- Memory I/O access (manual volatile casts everywhere)
- Register bit manipulation
- Byte swapping

**Recommendation:**
```cpp
// include/util/mmio.hpp
template<typename T> T mmio_read(volatile void *addr);
template<typename T> void mmio_write(volatile void *addr, T val);

// include/util/bits.hpp
#define SET_BIT(val, bit) ((val) | (1ULL << (bit)))
#define CLEAR_BIT(val, bit) ((val) & ~(1ULL << (bit)))
#define TEST_BIT(val, bit) (((val) >> (bit)) & 1)
```

---

## 11. Recommended Refactoring Priority

### Tier 1: Critical Architecture (Weeks)

| Task | Impact | Effort | Files |
|------|--------|--------|-------|
| **Create `kernel/include/constants.hpp`** | High | Medium | All files |
| Plan ViperFS user-space migration | High | High | fs/, vfs/ |
| Split `kernel_main()` into subsystem inits | High | Medium | main.cpp |
| Create abstract BlockDevice interface | High | Medium | cache.cpp, drivers/ |

### Tier 2: High Impact (Days)

| Task | Impact | Effort | Files |
|------|--------|--------|-------|
| Extract generic LRU cache template | Medium | Medium | cache.cpp, viperfs.cpp |
| Extract page table walker | Medium | Medium | vmm.cpp |
| Extract VirtIO init pattern | Medium | Low | drivers/virtio/*.cpp |
| Replace byte-copy loops with memcpy | Low | Low | channel.cpp |
| Split syscall/table.cpp | Medium | Medium | syscall/ |

### Tier 3: Medium Impact (Hours-Days)

| Task | Impact | Effort | Files |
|------|--------|--------|-------|
| Migrate scattered constants to `constants.hpp` | Medium | Low | Throughout |
| Extract directory scanning helper | Medium | Low | viperfs.cpp |
| Parameterize scheduler queue ops | Low | Low | scheduler.cpp |
| Add unified error codes | Medium | Medium | All layers |
| Create RAII wrappers | Medium | Low | Various |

### Tier 4: Quality of Life (Hours)

| Task | Impact | Effort | Files |
|------|--------|--------|-------|
| Rename unclear variables | Low | Low | Throughout |
| Implement O_TRUNC | Low | Low | vfs.cpp |
| Document locking hierarchy | Medium | Low | Documentation |
| Standardize error messages | Low | Low | Throughout |
| Add cache statistics | Low | Low | cache.cpp |

---

## Appendix A: Code Metrics Summary

| Subsystem | Files | Lines | Large Functions | Duplication Score |
|-----------|-------|-------|-----------------|-------------------|
| kernel/main.cpp | 1 | 1107 | 1 (990 lines) | Medium |
| kernel/syscall/ | 1 | 4096 | Many inline | Low |
| kernel/ipc/ | 3 | ~1500 | 2 | High |
| kernel/sched/ | 2 | ~1200 | 6 | High |
| kernel/mm/ | 4 | ~1500 | 3 | Medium |
| kernel/fs/ | 4 | ~3000 | 10+ | High |
| kernel/vfs/ | 1 | 991 | 3 | Medium |
| kernel/drivers/ | 8 | ~2500 | 5 | High |
| kernel/arch/ | 6 | ~1500 | 2 | Medium |

**Total Estimated Duplicate Code:** ~1500-2000 lines (15-20% of kernel)

---

## Appendix B: Refactoring Examples

### B.1 VirtIO Device Initialization

**Current Pattern (repeated in 4+ drivers):**
```cpp
bool BlkDevice::init() {
    u64 base = find_device(device_type::BLK);
    if (!base) { serial::puts("BLK: not found\n"); return false; }
    if (!Device::init(base)) return false;
    reset();
    if (is_legacy()) write32(reg::GUEST_PAGE_SIZE, 4096);
    add_status(status::ACKNOWLEDGE);
    add_status(status::DRIVER);
    if (!negotiate_features(FEATURE_RO | FEATURE_BLK_SIZE)) return false;
    add_status(status::DRIVER_OK);
    return true;
}
```

**Proposed Pattern:**
```cpp
bool BlkDevice::init() {
    VirtioInitializer vi(device_type::BLK, "BLK");
    if (!vi.basic_init()) return false;
    if (!vi.negotiate(FEATURE_RO | FEATURE_BLK_SIZE)) return false;
    return vi.finalize();
}
```

### B.2 Page Table Walking

**Current (in 5 places):**
```cpp
u64 *l0 = pgt_root;
u64 l0e = l0[L0_INDEX(virt)];
if (!(l0e & PTE_VALID)) { /* allocate */ }
u64 *l1 = phys_to_virt(l0e & PHYS_MASK);
// ... repeat for L1, L2, L3 ...
```

**Proposed:**
```cpp
u64 *pte = walk_tables(virt, LEVEL_L3, ALLOC_IF_MISSING);
if (!pte) return false;
*pte = phys | flags;
```

---

## Appendix C: Files Requiring Most Attention

1. **kernel/main.cpp** - Decompose monolithic init
2. **kernel/syscall/table.cpp** - Split by category
3. **kernel/fs/viperfs.cpp** - Extract helpers, reduce nesting
4. **kernel/ipc/channel.cpp** - Use memcpy, reduce O(n)
5. **kernel/sched/scheduler.cpp** - Parameterize duplicates
6. **kernel/mm/vmm.cpp** - Extract table walker
7. **kernel/drivers/virtio/*.cpp** - Extract init pattern

---

## Appendix D: Comprehensive Review Findings (2026-01-01)

This appendix contains detailed findings from a thorough re-review of the entire kernel codebase.

### D.1 Large Functions (>40 lines) Identified

| File | Function | Lines | Priority | Suggested Split |
|------|----------|-------|----------|-----------------|
| fs/vfs/vfs.cpp | normalize_path() | 115 | HIGH | Extract component processing |
| fs/viperfs/journal.cpp | Journal::replay() | 203 | HIGH | Extract validation, checksum, apply helpers |
| mm/fault.cpp | handle_page_fault() | 143 | HIGH | Extract demand fault, permission fault handlers |
| mm/fault.cpp | handle_cow_fault() | 132 | HIGH | Extract precondition checks, page copy logic |
| mm/fault.cpp | kernel_panic() | 111 | MEDIUM | Extract banner, backtrace printing |
| fs/viperfs/viperfs.cpp | create_dir() | 104 | MEDIUM | Extract dot entry creation |
| sched/task.cpp | create() | 93 | MEDIUM | Extract stack allocation, context setup |
| fs/viperfs/viperfs.cpp | rename() | 90 | MEDIUM | Extract dotdot update logic |
| fs/viperfs/viperfs.cpp | create_file() | 89 | MEDIUM | Extract transaction helpers |
| mm/buddy.cpp | init() | 86 | MEDIUM | Extract validation, free list setup |

### D.2 Code Duplication Patterns

#### D.2.1 Helper Functions to Extract

| Pattern | Files | Action |
|---------|-------|--------|
| `strcpy_safe()` | task.cpp, slab.cpp | Extract to lib/str.hpp |
| `memzero()` | slab.cpp, blob.cpp | Use lib::memset instead |
| Table initialization loops | channel.cpp, pollset.cpp, viper.cpp, task.cpp, timerwheel.cpp | Create template `init_table_slots<T>()` |
| Table lookup O(n) scans | channel.cpp, pollset.cpp, timerwheel.cpp, task.cpp | Create template `find_table_slot<T>()` |
| Slot allocation loops | channel.cpp, pollset.cpp, viper.cpp, timerwheel.cpp | Create template `alloc_table_slot<T>()` |
| Serial logging sequences | 50+ files (451 instances) | Create log macros in lib/log.hpp |

#### D.2.2 Proposed Template Helpers for lib/table.hpp

```cpp
template<typename T, typename Predicate>
T *find_table_slot(T *table, u32 count, Predicate pred);

template<typename T, typename IsFreeFn>
T *alloc_table_slot(T *table, u32 count, IsFreeFn is_free);

template<typename T, typename InitFn>
void init_table_slots(T *table, u32 count, InitFn init);
```

### D.3 Magic Numbers to Add to constants.hpp

The codebase is **well-organized** (90%+ already named). Add these remaining constants:

```cpp
// DMA regions (syscall/table.cpp)
namespace dma {
    constexpr u64 REGION_1_BASE = 0x100000000ULL;  // 4GB
    constexpr u64 REGION_2_BASE = 0x200000000ULL;  // 8GB
    constexpr u64 SEARCH_BASE = 0x7000000000ULL;   // 448GB
}

// Timeout iterations (drivers/virtio/blk.cpp)
namespace timeout {
    constexpr u32 INTERRUPT_POLL_ITERATIONS = 100000;
    constexpr u32 BLOCK_DEVICE_POLL_MAX = 10000000;
}

// Scheduler (sched/scheduler.cpp)
namespace sched {
    constexpr u32 LOAD_BALANCE_INTERVAL = 100;
}

// Console/ANSI (console/gcon.cpp)
namespace ansi {
    constexpr u8 ESCAPE_CHAR = 0x1B;
    constexpr u8 DELETE_CHAR = 0x7F;
}
```

### D.4 Readability Issues Summary

| Category | Count | Severity | Key Files |
|----------|-------|----------|-----------|
| Missing comments | 4 | MEDIUM | syscall/table.cpp, journal.cpp |
| Unclear variable names | 4 | MEDIUM | cap/table.cpp (uses `e`), vmm.cpp (uses `va`) |
| Deep nesting (>3 levels) | 3 | MEDIUM | gcon.cpp, main.cpp, dir.cpp |
| Inconsistent naming | 3 | MEDIUM | Globals: g_ prefix inconsistent |
| Long parameter lists | 2 | LOW | ramfb init, syscall handlers |
| Complex expressions | 3 | MEDIUM | Address validation, digit extraction |

### D.5 Architecture Issues

| Issue | Severity | Files | Fix |
|-------|----------|-------|-----|
| Viper ↔ Task circular dependency | HIGH | viper.hpp, task.hpp | Extract TaskGroup abstraction |
| Task depends on Viper (layering) | HIGH | task.cpp | Use callback interfaces |
| Scheduler depends on arch | MEDIUM | scheduler.cpp | Create CPU abstraction |
| Global state not encapsulated | HIGH | scheduler.cpp, task.cpp | Create KernelRuntime singleton |
| Viper is god object | HIGH | viper.hpp | Extract subsystem-specific state |
| IPC ↔ Task coupling | MEDIUM | channel.hpp | Use capability handles |
| Mixed error return conventions | MEDIUM | All modules | Standardize on Result<T> |
| Missing RAII patterns | MEDIUM | wait.hpp, cap module | Add guard classes |
| Race in task ID allocation | HIGH | task.cpp | Use atomic operations |
| No per-CPU current_task | HIGH | task.cpp | Use per-CPU or thread-local |

### D.6 Error Handling Statistics

| Pattern | Count | Location |
|---------|-------|----------|
| `return -1;` (raw) | 121 | 16 files, mostly fs/vfs/vfs.cpp (66) |
| `return nullptr;` | 110 | 31 files |
| Hex literals in code | 122 | 20 files |

### D.7 Recommended New Tasks

#### Phase 1: Quick Wins (1-2 hours each)
1. Extract `strcpy_safe()` to lib/str.hpp
2. Add remaining magic numbers to constants.hpp
3. Replace `memzero()` with `lib::memset()`
4. Add ANSI escape character constants

#### Phase 2: Template Helpers (2-4 hours each)
5. Create `lib/table.hpp` with generic table operations
6. Migrate channel.cpp to use table helpers
7. Migrate pollset.cpp to use table helpers
8. Migrate task.cpp to use table helpers

#### Phase 3: Readability (2-4 hours each)
9. Rename single-letter variables in cap/table.cpp
10. Extract complex conditions in gcon.cpp to named bools
11. Document all TODO comments with context

#### Phase 4: Architecture (1-2 days each)
12. Create CPU abstraction layer
13. Create KernelRuntime singleton for global state
14. Add RAII WaitGuard class
15. Make task ID allocation atomic

#### Phase 5: Long-term (Multi-day)
16. Decouple Task from Viper (major refactoring)
17. Standardize all error handling to Result<T>
18. Split Viper into subsystem-specific modules
