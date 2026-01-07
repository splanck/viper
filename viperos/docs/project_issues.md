# ViperOS Kernel Code Review - Project Issues

**Date:** January 2026
**Scope:** Full kernel source code review (`/os/kernel/`)
**Version:** 0.3.1
**Last Updated:** January 2026
**Status:** 49 of 94 issues fixed

This document catalogs issues found during a comprehensive kernel code review. Issues are categorized by severity and subsystem, with detailed work scope for each fix.

**Legend:** ✅ = Fixed, ⏳ = In Progress, ❌ = Not Started

---

## Table of Contents

1. [Critical Issues](#critical-issues)
2. [High Priority Issues](#high-priority-issues)
3. [Medium Priority Issues](#medium-priority-issues)
4. [Low Priority Issues](#low-priority-issues)
5. [Code Quality Issues](#code-quality-issues)

---

## Critical Issues

Issues that can cause crashes, data corruption, or security vulnerabilities.

### ✅ 1. Deadlock: Inconsistent Lock Ordering in Scheduler

**Location:** `kernel/sched/scheduler.cpp:461-469, 624-702`

**Description:** The scheduler acquires per-CPU lock then `sched_lock` in `schedule()`, but `tick()` acquires `sched_lock` first then accesses per-CPU data without its lock. Classic ABBA deadlock pattern on SMP systems.

**Analysis:**
- In `schedule()` (line ~469): Acquires `per_cpu_sched[cpu_id].lock`, then later may access global `sched_lock`
- In `tick()` (line ~646): Acquires `sched_lock` first, then reads `per_cpu_sched[cpu_id].queues[i].head` without per-CPU lock
- Under load on SMP, CPU 0 can hold per-CPU lock waiting for `sched_lock` while CPU 1 holds `sched_lock` waiting for CPU 0's per-CPU lock

**Work Scope:**
1. Define and document lock ordering hierarchy: `sched_lock` → per-CPU locks (global before local)
2. Modify `schedule()` to acquire `sched_lock` first if global operations needed
3. Modify `tick()` to acquire per-CPU lock before accessing per-CPU queue data
4. Add lock ordering assertions in debug builds
5. Review all scheduler functions for consistent ordering

**Files to Modify:**
- `kernel/sched/scheduler.cpp` - Fix lock ordering in `schedule()`, `tick()`, `steal_task()`
- `kernel/sched/scheduler.hpp` - Document lock hierarchy

**Complexity:** Medium (2-3 hours)
**Risk:** High - incorrect fix could cause new deadlocks or races

---

### ✅ 2. Use-After-Release in Journal

**Location:** `kernel/fs/viperfs/journal.cpp:80-81`

**Description:** `cache().release(block)` is called before `cache().sync_block(block)`, causing use-after-free. The block could be evicted and reused by another thread.

**Analysis:**
In `write_header()`:
```cpp
cache().release(block);   // Line 80 - block may be evicted here
cache().sync_block(block); // Line 81 - use-after-free!
```

The `release()` decrements refcount, allowing eviction. `sync_block()` then writes stale/freed memory.

**Work Scope:**
1. Reorder operations: call `sync_block()` before `release()`
2. Audit all journal functions for same pattern
3. Audit block cache usage throughout codebase for similar issues

**Files to Modify:**
- `kernel/fs/viperfs/journal.cpp` - Fix `write_header()` lines 79-81
- Audit: `write_transaction()`, `write_commit()` for same pattern

**Fix:**
```cpp
// Before:
cache().release(block);
cache().sync_block(block);

// After:
cache().sync_block(block);
cache().release(block);
```

**Complexity:** Low (30 minutes)
**Risk:** Low - straightforward reorder

---

### ✅ 3. Journal Wraparound Discards Committed Transactions

**Location:** `kernel/fs/viperfs/journal.cpp:420-428`

**Description:** When journal space is exhausted, `header_.head` and `header_.tail` are reset to 0, discarding all pending transactions. This violates crash consistency guarantees.

**Analysis:**
In `write_transaction()` (lines 420-428):
```cpp
if (available < space_needed) {
    // Journal is full - need to wrap or compact
    // For now, reset the journal (simple approach)
    header_.head = 0;
    header_.tail = 0;
}
```

This discards committed-but-not-completed transactions, breaking crash recovery.

**Work Scope:**
1. Implement proper journal reclamation:
   - Track completed transactions (already applied to filesystem)
   - Only reclaim space from completed transactions
   - Add `complete()` tracking per transaction
2. Add journal space reservation before transaction start
3. Implement journal compaction or circular wrap-around
4. Add journal full error handling (abort transaction, retry later)

**Files to Modify:**
- `kernel/fs/viperfs/journal.cpp` - Rewrite space management
- `kernel/fs/viperfs/journal.hpp` - Add completion tracking fields

**Complexity:** High (4-6 hours)
**Dependencies:** Requires understanding of all journal callers

---

### ✅ 4. Legacy VirtIO Vring Double-Free on Destroy

**Location:** `kernel/drivers/virtio/virtqueue.cpp:240-268`

**Description:** In legacy mode, `avail_phys_` and `used_phys_` point into the same allocation as `desc_phys_`. The `destroy()` function tries to free all three separately.

**Analysis:**
In legacy mode initialization, a single contiguous allocation holds all vring components:
```
[descriptors][avail ring][used ring]
```
All three pointers point into this single allocation. But `destroy()` calls:
```cpp
pmm::free_pages(desc_phys_, ...);
pmm::free_pages(avail_phys_, ...);  // Double-free!
pmm::free_pages(used_phys_, ...);   // Triple-free!
```

**Work Scope:**
1. Add `legacy_mode_` flag to track allocation type
2. In legacy mode `destroy()`, only free `desc_phys_` (the base allocation)
3. In modern mode, free each separately (or document they're also unified)
4. Add allocation tracking to prevent future confusion

**Files to Modify:**
- `kernel/drivers/virtio/virtqueue.cpp` - Fix `destroy()` around line 240-268
- `kernel/drivers/virtio/virtqueue.hpp` - Add `legacy_mode_` member if not present

**Fix Pattern:**
```cpp
void Virtqueue::destroy() {
    if (!desc_phys_) return;

    if (legacy_mode_) {
        // Single allocation in legacy mode
        pmm::free_pages(desc_phys_, legacy_alloc_pages_);
    } else {
        // Separate allocations in modern mode
        pmm::free_pages(desc_phys_, desc_pages_);
        pmm::free_pages(avail_phys_, avail_pages_);
        pmm::free_pages(used_phys_, used_pages_);
    }
}
```

**Complexity:** Low-Medium (1 hour)
**Risk:** Medium - must test both legacy and modern paths

---

### ✅ 5. Infinite Loop in VirtIO Block Flush

**Location:** `kernel/drivers/virtio/blk.cpp:503-514`

**Description:** The `flush()` polling fallback has no timeout, unlike `do_request()`. Device failure causes infinite hang.

**Analysis:**
In `flush()`:
```cpp
while (!flush_complete_) {
    // No timeout check!
    asm volatile("wfi");
}
```

Compare to `do_request()` which has proper timeout handling.

**Work Scope:**
1. Add timeout constant `FLUSH_TIMEOUT_MS` (e.g., 30000ms)
2. Add timestamp capture before loop
3. Check elapsed time in loop, return error on timeout
4. Consider consolidating timeout logic into helper function

**Files to Modify:**
- `kernel/drivers/virtio/blk.cpp` - Add timeout to `flush()` loop

**Fix Pattern:**
```cpp
bool VirtioBlk::flush() {
    // ... setup ...

    u64 deadline = timer::get_ticks() + FLUSH_TIMEOUT_MS;
    while (!flush_complete_) {
        if (timer::get_ticks() >= deadline) {
            serial::puts("[virtio-blk] Flush timeout!\n");
            return false;
        }
        asm volatile("wfi");
    }
    return true;
}
```

**Complexity:** Low (30 minutes)
**Risk:** Low

---

### ✅ 6. Task Slot Leak on Stack Allocation Failure

**Location:** `kernel/sched/task.cpp:271, 411`

**Description:** If `allocate_kernel_stack()` fails after `allocate_task()` succeeds, the task slot is never returned.

**Analysis:**
In task creation:
```cpp
Task* t = allocate_task();  // Line ~271 - allocates slot
if (!t) return nullptr;

u64 stack = allocate_kernel_stack();  // Line ~411
if (stack == 0) {
    return nullptr;  // Task slot leaked!
}
```

The task slot remains allocated but unusable.

**Work Scope:**
1. Add cleanup path to free task slot on stack allocation failure
2. Create helper function `release_task()` if not existing
3. Audit other task creation paths for similar leaks
4. Consider RAII wrapper for task allocation

**Files to Modify:**
- `kernel/sched/task.cpp` - Add cleanup in `create()` or `spawn()` failure paths

**Fix Pattern:**
```cpp
Task* t = allocate_task();
if (!t) return nullptr;

u64 stack = allocate_kernel_stack();
if (stack == 0) {
    release_task(t);  // Return slot to free list
    return nullptr;
}
```

**Complexity:** Low (30 minutes)
**Risk:** Low

---

### ✅ 7. Secondary CPUs Don't Configure Exception Vectors or MMU

**Location:** `kernel/arch/aarch64/cpu.cpp:234-280`

**Description:** `secondary_main()` doesn't set VBAR_EL1, TTBR, TCR, or MAIR. Secondary CPUs will fault immediately on exception.

**Analysis:**
The `secondary_main()` function initializes GIC and timer but skips:
- `VBAR_EL1` - Exception vector base (exceptions go to address 0!)
- `TTBR0_EL1/TTBR1_EL1` - Page tables (MMU uses garbage)
- `TCR_EL1` - Translation control (may be garbage from boot)
- `MAIR_EL1` - Memory attributes (wrong caching)

First exception on secondary CPU crashes or corrupts memory.

**Work Scope:**
1. Extract MMU/exception setup from boot CPU into reusable functions
2. Call these functions from `secondary_main()`:
   - `set_vbar_el1(exception_vectors)`
   - `set_ttbr0_el1(kernel_ttbr0)`
   - `set_ttbr1_el1(kernel_ttbr1)`
   - `set_tcr_el1(kernel_tcr)`
   - `set_mair_el1(kernel_mair)`
   - `isb` after each
3. Enable MMU if not already enabled (`SCTLR_EL1.M`)
4. Add cache/TLB maintenance as needed

**Files to Modify:**
- `kernel/arch/aarch64/cpu.cpp` - Add setup in `secondary_main()`
- `kernel/arch/aarch64/mmu.cpp` - Extract reusable setup functions
- `kernel/arch/aarch64/exceptions.cpp` - Export vector address

**Complexity:** Medium (2-3 hours)
**Risk:** High - incorrect setup causes hard-to-debug crashes

---

### ✅ 8. COW Pages Not Properly Freed on Address Space Destroy

**Location:** `kernel/viper/address_space.cpp:240-251`

**Description:** `destroy()` calls `pmm::free_page()` without checking/decrementing COW reference counts. Shared pages get corrupted.

**Analysis:**
When a forked process exits, `AddressSpace::destroy()` walks page tables and frees physical pages. But if pages are COW-shared with another process:
- Decrementing refcount without checking causes underflow
- Freeing while refcount > 1 corrupts other process's memory
- Other process later writes to freed/reused page

**Work Scope:**
1. In `destroy()`, for each user page:
   - Check if page is COW-shared via `cow_manager().get_ref(phys)`
   - If refcount > 1: decrement only, don't free
   - If refcount == 1: decrement and free
   - If refcount == 0: just free (wasn't COW)
2. Update COW manager to handle edge cases
3. Add debug assertions for refcount consistency

**Files to Modify:**
- `kernel/viper/address_space.cpp` - Fix `destroy()` page freeing logic
- `kernel/mm/cow.cpp` - May need `dec_ref_and_maybe_free()` helper

**Fix Pattern:**
```cpp
void AddressSpace::destroy() {
    // For each mapped user page...
    for_each_user_page([](u64 virt, u64 phys) {
        u16 ref = cow_manager().get_ref(phys);
        if (ref > 1) {
            cow_manager().dec_ref(phys);
            // Don't free - still shared
        } else {
            cow_manager().dec_ref(phys);  // Sets to 0
            pmm::free_page(phys);
        }
    });
    // ... rest of cleanup
}
```

**Complexity:** Medium (2 hours)
**Dependencies:** Requires understanding of COW flow in fork

---

## High Priority Issues

Issues that cause incorrect behavior or have security implications.

### Memory Management

| # | Issue | Location | Description | Work Scope | Status |
|---|-------|----------|-------------|------------|--------|
| 1 | ✅ Memory leak on heap expansion failure | `mm/kheap.cpp:256-262` | Pages allocated but not freed when `add_heap_region()` fails | Add cleanup: free allocated pages on region add failure | Fixed |
| 2 | ✅ VMM `map_range()` no rollback | `mm/vmm.cpp:286-299` | Partial mapping remains on failure | Track mapped pages, unmap all on any failure | Fixed |
| 3 | ✅ No locking in VMM | `mm/vmm.cpp` | No synchronization for page table modifications | Add `vmm_lock` spinlock, protect all page table modifications | Fixed |
| 4 | ✅ No locking in VmaList | `mm/vma.cpp` | Concurrent add/remove/find causes corruption | Add per-VmaList spinlock, protect all operations | Fixed |
| 5 | ✅ `krealloc()` race condition | `mm/kheap.cpp:473-513` | Reads block header without heap lock | Move header read inside lock, or use atomic operations | Fixed |

### Scheduler

| # | Issue | Location | Description | Work Scope | Status |
|---|-------|----------|-------------|------------|--------|
| 1 | ✅ Per-CPU queue accessed without lock | `sched/scheduler.cpp:646-653` | `tick()` reads per-CPU queue head with only global lock | Acquire per-CPU lock before accessing per-CPU queue | Fixed |
| 2 | ✅ Signal pending bitmap not atomic | `sched/signal.cpp:181` | `pending |= (1u << signum)` can lose signals | Use `__atomic_fetch_or` or protect with task lock | Fixed |
| 3 | ✅ Task killed without cleanup | `sched/task.cpp:888-918` | Non-current task marked Exited without freeing resources | Call resource cleanup (stack, FPU state) before marking Exited | Fixed |
| 4 | ✅ Lock held too long | `sched/scheduler.cpp:469-594` | Critical section includes serial I/O | Move I/O outside lock, use deferred logging | Fixed |
| 5 | Signal handlers never invoked | `sched/signal.cpp:343-363` | User handlers ignored, default action applied | Implement signal trampoline: save context, set up user stack, jump to handler | High |

### IPC and Capabilities

| # | Issue | Location | Description | Work Scope | Status |
|---|-------|----------|-------------|------------|--------|
| 1 | TOCTOU in channel::get() | `ipc/channel.cpp:61-72` | Lock released before pointer used | Keep lock until operation complete, or use refcounting | Medium |
| 2 | ✅ Capability table not thread-safe | `cap/table.cpp` | No internal locking, concurrent access corrupts | Add spinlock to Table class, protect all operations | Fixed |
| 3 | Legacy channel ops bypass capability checks | `ipc/channel.cpp:412-423` | Anyone can send/recv by guessing channel ID | Remove legacy channel ID access, require capability handles | Medium |
| 4 | ✅ Timer/poll waiter not cleared on task exit | `ipc/poll.cpp:204-231` | Use-after-free when timer fires | Clear all wait entries for exiting task in task cleanup | Fixed |
| 5 | Handle leak in channel cleanup | `ipc/channel.cpp:472-505` | Transferred handles not released | Track and release handles on channel close | Medium |

### Syscalls

| # | Issue | Location | Description | Work Scope | Status |
|---|-------|----------|-------------|------------|--------|
| 1 | User pointer validation incomplete | `syscall/table.cpp:169-170` | Only checks address range, not page table mapping | Add `is_user_mapped()` check that walks page tables | Medium |
| 2 | ✅ Integer overflow in size validation | `syscall/table.cpp:500+` | `count * sizeof()` can overflow | Use `__builtin_mul_overflow` or check before multiply | Fixed |
| 3 | TOCTOU on user strings | `syscall/table.cpp:190-213` | String validated but read again later | Copy to kernel buffer once, use copy for all operations | Medium |
| 4 | ✅ `sys_kill` no permission check | `syscall/table.cpp:2670-2713` | Can signal any task | Add check: same UID, or CAP_KILL capability | Fixed |
| 5 | ✅ `sys_map_framebuffer` no capability check | `syscall/table.cpp:3962-4021` | Any process can map display | Require CAP_DEVICE_ACCESS or framebuffer capability | Fixed |

### VirtIO Drivers

| # | Issue | Location | Description | Work Scope | Status |
|---|-------|----------|-------------|------------|--------|
| 1 | GPU buffers never freed | `drivers/virtio/gpu.cpp:89-120` | No cleanup function exists | Implement `VirtioGpu::destroy()`, free all allocated buffers | Medium |
| 2 | Interrupt handler race | `drivers/virtio/blk.cpp:226-231` | Only one completion tracked | Use completion queue or bitmap for multiple in-flight requests | High |
| 3 | ✅ VirtIO buffers submitted before DRIVER_OK | `drivers/virtio/input.cpp:223-227` | Violates spec | Move buffer submission after DRIVER_OK status write | Fixed |
| 4 | ✅ Global device table not atomic | `drivers/virtio/virtio.cpp:290-301` | `find_device()` has TOCTOU | Add spinlock protecting device table | Fixed |

### Process/Viper

| # | Issue | Location | Description | Work Scope | Status |
|---|-------|----------|-------------|------------|--------|
| 1 | No locking in fork() | `viper/viper.cpp:588-658` | Parent PTEs modified without synchronization | Add address space lock, hold during COW setup | Medium |
| 2 | Race between exit() and wait() | `viper/viper.cpp:472-558` | Child list modification not synchronized | Add viper_lock for child list modifications | Medium |
| 3 | COW refcount not properly initialized | `viper/address_space.cpp:559-574` | Pages start at refcount 0 | Initialize refcount to 1 on first mapping, increment on COW share | Medium |
| 4 | sbrk partial allocation leak | `viper/viper.cpp:710-736` | Failed allocation doesn't unmap | Track allocated pages, unmap all on failure | Medium |
| 5 | ✅ setpgid allows cross-process modification | `viper/viper.cpp:786-826` | No permission check | Add check: target must be self, child, or same session | Fixed |

### Filesystem

| # | Issue | Location | Description | Work Scope | Status |
|---|-------|----------|-------------|------------|--------|
| 1 | ✅ Block leak in alloc_zeroed_block | `fs/viperfs/viperfs.cpp:431-455` | Block leaked on cache failure | Free allocated block if cache().get() fails | Fixed |
| 2 | Symlink block leak on failure | `fs/viperfs/viperfs.cpp:1529-1605` | Data blocks not freed | Track allocated blocks, free all on symlink creation failure | Medium |
| 3 | ✅ Missing rec_len validation | `fs/viperfs/viperfs.cpp:776-801` | Can cause infinite loop or overflow | Validate rec_len >= MIN_DIRENT_SIZE and <= remaining space | Fixed |
| 4 | ✅ Indirect block no bounds check | `fs/viperfs/viperfs.cpp:637-651` | index >= 512 reads out of bounds | Add bounds check: index < (BLOCK_SIZE / sizeof(u64)) | Fixed |
| 5 | Inode modifications without lock | `fs/viperfs/viperfs.cpp:691+` | atime/mtime modified without fs_lock | Hold fs_lock or use per-inode lock for metadata updates | Medium |

### Architecture

| # | Issue | Location | Description | Work Scope | Status |
|---|-------|----------|-------------|------------|--------|
| 1 | ✅ Non-atomic tick counter | `arch/aarch64/timer.cpp:190` | Shared variable corrupted on SMP | Use `__atomic_fetch_add` or per-CPU counters | Fixed |
| 2 | EOI before handler for level-triggered | `arch/aarch64/gic.cpp:537-538` | Can cause interrupt storm | For level-triggered IRQs, EOI after handler (or document why before) | Medium |
| 3 | ✅ Missing cache maintenance before secondary boot | `arch/aarch64/cpu.cpp:189-231` | Data not visible to secondary | Add `dc civac` / `dsb ish` before waking secondary | Fixed |
| 4 | ✅ GICv2 SGIR address hardcoded | `arch/aarch64/cpu.cpp:290` | Not using GICD_BASE constant | Replace magic address with `GICD_BASE + GICD_SGIR` | Fixed |

---

## Medium Priority Issues

Issues that affect functionality or maintainability.

### Duplicate Code and Definitions

| # | Issue | Location | Description | Work Scope | Complexity |
|---|-------|----------|-------------|------------|------------|
| 1 | ✅ Duplicate signal constants | `sched/task.hpp`, `sched/signal.hpp` | SIGKILL, etc defined twice | Move all signal constants to signal.hpp, remove from task.hpp | Fixed |
| 2 | Duplicate color constants | `console/gcon.hpp`, `include/constants.hpp` | VIPER_GREEN, etc | Keep in one location (constants.hpp), include where needed | Low |
| 3 | Duplicate NO_PARENT constant | `cap/table.hpp`, `include/constants.hpp` | 0xFFFFFFFF | Keep in constants.hpp, include in table.hpp | Low |
| 4 | Duplicate InodeGuard class | `fs/viperfs/viperfs.hpp`, `fs/viperfs/inode_guard.hpp` | ODR violation | Remove duplicate, keep one authoritative definition | Low |
| 5 | Duplicate spawn code | `syscall/table.cpp:299-343, 428-472` | Bootstrap channel creation | Extract common code into helper function | Medium |

### Version and Documentation Inconsistencies

| # | Issue | Location | Description | Status |
|---|-------|----------|-------------|--------|
| 1 | ✅ Version mismatch | `kernel/main.cpp:127` | Says "v0.2.0" but docs say "v0.3.1" | Fixed |
| 2 | Outdated v0.2.0 references | `kernel/assign/*.cpp/hpp` | Multiple files reference old version | Pending |

### Hardcoded Values

| # | Issue | Location | Description | Work Scope |
|---|-------|----------|-------------|------------|
| 1 | VirtIO MMIO scan addresses | `drivers/virtio/virtio.cpp:229` | Should use constants | Define VIRTIO_MMIO_BASE_* constants |
| 2 | ✅ Page size magic numbers | `drivers/virtio/input.cpp:208` | Uses 4096 instead of PAGE_SIZE | Replace with pmm::PAGE_SIZE |
| 3 | Timeout values | Multiple VirtIO files | Arbitrary numeric values | Define timeout constants (VIRTIO_TIMEOUT_MS) |
| 4 | ISR bits | `drivers/virtio/blk.cpp:221-236` | Magic 0x1, 0x2 values | Define ISR_QUEUE_NOTIFY, ISR_CONFIG_CHANGE |

### Memory Barrier Issues

| # | Issue | Location | Description | Work Scope |
|---|-------|----------|-------------|------------|
| 1 | Inconsistent dmb vs dsb usage | VirtIO drivers | Should use dsb for MMIO | Audit and fix: use `dsb st` before MMIO writes |
| 2 | ✅ Missing barrier after timer enable | `arch/aarch64/timer.cpp:286` | No isb after enable | Add `isb` after `write_cntp_ctl(1)` | Fixed |
| 3 | Missing GIC configuration barriers | `arch/aarch64/gic.cpp:229-262` | No dsb after config | Add `dsb sy` after GIC register writes |

### Error Handling

| # | Issue | Location | Description | Work Scope |
|---|-------|----------|-------------|------------|
| 1 | Silent failures in COW manager | `mm/cow.cpp:103-110` | Returns without logging | Add debug logging for invalid page addresses |
| 2 | Ignored write_inode returns | `fs/viperfs/viperfs.cpp` multiple | 6+ locations | Check and handle write_inode failures |
| 3 | Inconsistent error codes | Throughout | Same condition, different errors | Audit and standardize error codes |

---

## Low Priority Issues

Minor issues, optimizations, and cleanup.

### Dead Code

| # | Issue | Location | Description | Work Scope |
|---|-------|----------|-------------|------------|
| 1 | Unused trampoline_msg | `sched/context.S:111-112` | String defined but never used | Remove if truly unused |
| 2 | make_cow_readonly() never called | `viper/address_space.cpp:588-638` | Dead function | Remove or document future use |
| 3 | mem_entries_ in GPU | `drivers/virtio/gpu.cpp:288-308` | Set but never used | Remove or implement usage |
| 4 | events_[] array in input | `drivers/virtio/input.cpp:217-221` | Copied but never read | Remove or implement event processing |
| 5 | pending_count_ unused | `drivers/virtio/input.hpp:246` | `[[maybe_unused]]` tag | Remove if not needed |
| 6 | VA_BITS constant unused | `mm/vmm.cpp:36` | Declared, never used | Remove |
| 7 | args field in Viper | `viper/viper.hpp:82` | Never set or used | Remove or implement |

### Performance Optimizations

| # | Issue | Location | Description | Work Scope | Impact |
|---|-------|----------|-------------|------------|--------|
| 1 | O(n) bitmap allocation | `mm/pmm.cpp:313-332` | Could use `__builtin_ctzll` | Use bit scan for first-fit | Medium |
| 2 | O(n) slab partial search | `mm/slab.cpp:301-308` | Maintain proper partial list | Add partial list head pointer | Medium |
| 3 | O(n) slab free verification | `mm/slab.cpp:340-354` | Every free does linear search | Remove verification or use debug-only | Low |
| 4 | O(n) heap coalesce | `mm/kheap.cpp:301-323` | Full traversal on every free | Maintain sorted free list | Medium |
| 5 | O(n) timer check | `arch/aarch64/timer.cpp:151-167` | Check all 16 slots per tick | Use sorted list or timer wheel | Medium |

### Style/Consistency

| # | Issue | Location | Description | Work Scope |
|---|-------|----------|-------------|------------|
| 1 | Magic page mask 0xFFF | `mm/vma.cpp:121,258,272` | Should use PAGE_MASK constant | Replace with (PAGE_SIZE - 1) or PAGE_MASK |
| 2 | Infinite loops without timeout | Multiple locations | Polling loops | Add reasonable timeouts to all polling |
| 3 | `(void)` unused parameter casts | Multiple locations | Some with comments, some without | Standardize: use `[[maybe_unused]]` |

---

## Code Quality Issues

General patterns that should be improved across the codebase.

### 1. Missing Synchronization for SMP

Many subsystems lack proper synchronization for multi-CPU operation:
- VMM page tables
- VMA lists
- Capability tables
- Poll wait queues
- Device allocation

**Recommended Approach:**
1. Add spinlock to each data structure
2. Document lock ordering
3. Use RAII guards (SpinlockGuard) consistently
4. Consider RCU for read-heavy structures

### 2. Inconsistent Error Return Values

Different return types and values for similar conditions:
- `-1` vs `error::VERR_*` vs `0`
- Some functions return success/failure, others return count/handle

**Recommended Approach:**
1. Standardize on `i64` return with `error::VERR_*` codes
2. Use positive values for success (handle/count)
3. Use negative values for errors
4. Document return semantics per function

### 3. TOCTOU Patterns

Multiple places where data is validated then accessed separately:
- User string validation
- Handle lookups
- Channel get/use patterns

**Recommended Approach:**
1. Copy user data to kernel buffer before validation
2. Hold locks across check-then-use sequences
3. Use reference counting to prevent object destruction

### 4. Resource Cleanup on Failure

Many functions don't properly clean up on partial failure:
- Memory allocations
- Page mappings
- Handle transfers
- Journal transactions

**Recommended Approach:**
1. Use RAII for all resources
2. Track all allocations for rollback
3. Implement proper error-unwinding in complex operations

### 5. Lock Ordering Documentation

No documented lock ordering hierarchy. Observed orderings:
- `sched_lock` → per-CPU locks (in schedule)
- per-CPU locks → `sched_lock` (in tick) — **CONFLICT**
- `inode_cache_lock` → `cache_lock`
- `fs_lock` → `txn_lock`

**Recommended Approach:**
1. Document global lock hierarchy
2. Add lock ordering assertions in debug builds
3. Fix identified conflicts (scheduler)

---

## Recommended Priority Order

### Immediate (Security/Stability)
1. Fix deadlock in scheduler lock ordering
2. Fix journal use-after-release and wraparound
3. Fix VirtIO vring double-free
4. Add secondary CPU exception vector setup
5. Fix COW reference counting

### Short-Term (Correctness)
1. Add synchronization to VMM/VMA/cap tables
2. Fix memory leaks in heap/slab/vma
3. Complete user pointer validation
4. Fix signal handler invocation
5. Add proper cleanup on failure paths

### Medium-Term (Quality)
1. Remove duplicate code/constants
2. Replace hardcoded values with constants
3. Add consistent error codes
4. Improve barrier usage in drivers
5. Document lock ordering

### Long-Term (Performance/Polish)
1. Optimize allocation algorithms
2. Add per-CPU caches
3. Implement lazy FPU save/restore
4. Add timer wheel optimization
5. Clean up dead code

---

## Statistics

| Category | Critical | High | Medium | Low |
|----------|----------|------|--------|-----|
| Memory Management | 1 | 5 | 3 | 4 |
| Scheduler | 1 | 5 | 2 | 1 |
| IPC/Capabilities | 0 | 5 | 2 | 3 |
| Syscalls | 0 | 5 | 3 | 0 |
| VirtIO Drivers | 2 | 4 | 5 | 3 |
| Process/Viper | 1 | 5 | 2 | 2 |
| Filesystem | 1 | 6 | 4 | 3 |
| Architecture | 1 | 4 | 5 | 5 |
| **Total** | **8** | **39** | **26** | **21** |

---

## Effort Estimates

| Priority | Items | Total Effort |
|----------|-------|--------------|
| Critical | 8 | ~15-20 hours |
| High | 39 | ~60-80 hours |
| Medium | 26 | ~20-30 hours |
| Low | 21 | ~10-15 hours |

---

*This document was generated through comprehensive static analysis of the ViperOS kernel codebase.*
