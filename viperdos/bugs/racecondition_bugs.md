# ViperDOS Race Condition Audit Report

**Generated:** 2026-01-20
**Status:** COMPLETE
**Last Updated:** 2026-01-20

This report documents all potential race conditions found in the ViperDOS C++ codebase through systematic file-by-file review.

---

## Fixed Issues (2026-01-20)

The following high-priority issues have been fixed:

| Issue | Severity | Status | Fix Summary |
|-------|----------|--------|-------------|
| RC-001 | CRITICAL | **FIXED** | Spinlock now returns saved_daif from acquire() and accepts it in release(). SpinlockGuard stores saved_daif per-instance. |
| RC-002 | CRITICAL | **FIXED** | Added task_lock spinlock protecting task table allocation, kernel stack pool, and next_task_id. |
| RC-003 | CRITICAL | **FIXED** | Added poll_lock spinlock protecting timers[], wait_queue[], and all related operations. |
| RC-004 | HIGH | **FIXED** | Added documentation warning about AtomicFlag interrupt safety limitations. |
| RC-005 | HIGH | **FIXED** | Added comprehensive documentation about WaitQueue external locking requirements. |
| RC-006 | HIGH | **FIXED** | context_switch_count now uses __atomic builtins for SMP safety. |
| RC-007 | HIGH | **PARTIAL** | load_balance_counter uses atomics; running flag still non-atomic (typically set once at boot). |
| RC-014 | HIGH | **FIXED** | next_task_id is now protected by task_lock (part of RC-002 fix). |

**Files Modified:**
- `kernel/lib/spinlock.hpp` - Spinlock API change, AtomicFlag documentation
- `kernel/sched/task.cpp` - task_lock protection for allocation
- `kernel/sched/wait.hpp` - WaitQueue locking documentation
- `kernel/sched/scheduler.cpp` - Atomic counters
- `kernel/ipc/poll.cpp` - poll_lock protection
- `kernel/sched/pi.cpp` - Updated for new Spinlock API
- `kernel/ipc/channel.cpp` - Updated for new Spinlock API

---

## Summary

| Severity | Count | Description |
|----------|-------|-------------|
| CRITICAL | 3 | Data corruption, security bypass |
| HIGH | 8 | Deadlock, resource leak under race |
| MEDIUM | 9 | Incorrect behavior, recoverable |
| LOW | 4 | Minor issues, edge cases |

**Total Issues Found:** 24

---

## CRITICAL Issues

### RC-001: Spinlock saved_daif_ Race Condition

**File:** `kernel/lib/spinlock.hpp`
**Lines:** 76, 123, 177, 198
**Severity:** CRITICAL

**Description:**
The `Spinlock` class stores the saved interrupt state (`saved_daif_`) as a single member variable of the lock object itself. When multiple CPUs contend for the same lock:

1. CPU 0 calls `acquire()`, saves its DAIF state to `saved_daif_` (line 76)
2. CPU 0 gets a ticket and waits
3. CPU 1 calls `acquire()`, overwrites `saved_daif_` with its own DAIF state
4. CPU 1 gets a ticket and waits
5. CPU 0 acquires the lock, executes critical section
6. CPU 0 calls `release()`, restores `saved_daif_` - but this is CPU 1's saved state!

This causes interrupts to be incorrectly enabled/disabled after lock release, potentially causing:
- Interrupt handlers running when they shouldn't (security issue)
- Interrupts disabled when they should be enabled (deadlock/hang)
- Nested interrupt corruption

**Recommendation:**
Return saved DAIF from `acquire()` and pass to `release()`, or have `SpinlockGuard` store it:
```cpp
class SpinlockGuard {
    Spinlock &lock_;
    u64 saved_daif_;
public:
    SpinlockGuard(Spinlock &lock) : lock_(lock) {
        saved_daif_ = lock_.acquire();  // returns saved DAIF
    }
    ~SpinlockGuard() { lock_.release(saved_daif_); }
};
```

---

### RC-002: Task Allocation Race Condition

**File:** `kernel/sched/task.cpp`
**Lines:** 75-85, 119-158
**Severity:** CRITICAL

**Description:**
The `allocate_task()` function iterates through the global task table looking for `Invalid` slots without any lock. Two CPUs calling `task::create()` simultaneously could:
1. Both see the same Invalid slot
2. Both allocate and initialize it
3. One task overwrites the other's data

Similarly, `allocate_kernel_stack()` and `free_kernel_stack()` manipulate a global free list without synchronization.

**Code Evidence:**
```cpp
// Lines 75-85 - no lock
Task *allocate_task() {
    for (u32 i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TaskState::Invalid) {
            return &tasks[i];  // Race: another CPU could see same slot
        }
    }
    return nullptr;
}
```

**Recommendation:**
Add a task subsystem spinlock that protects:
- Task table allocation/deallocation
- `next_task_id` counter
- Kernel stack pool and free list

---

### RC-003: Poll Subsystem Unsynchronized

**File:** `kernel/ipc/poll.cpp`
**Lines:** 48-49, 69-70, all timer/wait functions
**Severity:** CRITICAL

**Description:**
The poll subsystem has no lock protecting its global state:
- `timers[]` array (line 48)
- `wait_queue[]` array (line 70)
- `next_timer_id` counter (line 49)

Functions like `timer_create()`, `timer_cancel()`, `register_wait()`, `notify_handle()` all access these structures without synchronization. On SMP systems, this causes:
- Lost timers
- Double-free of timer slots
- Corrupted wait queue entries
- Tasks never being woken or woken incorrectly

**Recommendation:**
Add a `poll_lock` spinlock protecting all timer and wait queue operations.

---

## HIGH Issues

### RC-004: AtomicFlag Missing Interrupt Disable

**File:** `kernel/lib/spinlock.hpp`
**Lines:** 243-294
**Severity:** HIGH

**Description:**
`AtomicFlag` provides spinlock semantics but does not disable interrupts. If code holding an `AtomicFlag` is interrupted and the interrupt handler attempts to acquire the same flag, deadlock occurs.

**Recommendation:**
Document that `AtomicFlag` must not be used where interrupt handlers may contend, or add DAIF save/restore.

---

### RC-005: Wait Queue Operations Unsynchronized

**File:** `kernel/sched/wait.hpp`, `kernel/sched/wait.cpp`
**Lines:** 75-98, 111-150, 11-67
**Severity:** HIGH

**Description:**
The `WaitQueue` structure has no lock, and all inline functions (`wait_enqueue`, `wait_dequeue`) and cpp functions (`wait_wake_one`, `wait_wake_all`) operate on the queue without synchronization. While callers often hold a higher-level lock, this is not enforced.

**Code Evidence:**
```cpp
// wait.hpp lines 75-98 - no lock
inline void wait_enqueue(WaitQueue *wq, task::Task *t) {
    // ... manipulates wq->head, wq->tail, wq->count ...
}
```

**Recommendation:**
Either:
1. Add a spinlock to `WaitQueue` struct, or
2. Document and enforce that caller must hold an appropriate lock

---

### RC-006: Scheduler Statistics Race

**File:** `kernel/sched/scheduler.cpp`
**Lines:** 83, 90-91, 1037, 1191-1194
**Severity:** HIGH

**Description:**
Several scheduler globals are accessed without locks:
- `context_switch_count` (line 83): Incremented under `sched_lock` but read without lock in `get_context_switches()` (line 1037)
- `load_balance_counter` (lines 90-91, 1191-1194): Incremented without any lock in `balance_load()`

**Recommendation:**
Use atomic operations for counters, or ensure lock is held for all accesses.

---

### RC-007: Scheduler `running` Flag Race

**File:** `kernel/sched/scheduler.cpp`
**Lines:** 86, 827, 939, 970
**Severity:** HIGH

**Description:**
The `running` flag is checked without lock in `tick()` and `preempt()`, but set without lock in `start()`. On SMP, CPUs could see stale values.

**Recommendation:**
Use `std::atomic<bool>` or protect with lock.

---

### RC-008: Task State Protected by Multiple Locks

**File:** `kernel/sched/scheduler.cpp`, `kernel/sched/task.cpp`, `kernel/ipc/channel.cpp`
**Lines:** Various
**Severity:** HIGH

**Description:**
Task `state` field is modified by:
- Scheduler code (under `sched_lock`)
- Channel wait code (under `channel_lock`)
- Poll/timer code (no lock)

Since different locks protect the same field, races can occur where:
1. Channel code sets `state = Blocked`
2. Scheduler simultaneously sets `state = Running`

**Recommendation:**
Task state should be protected by a consistent lock. Options:
1. Always acquire `sched_lock` when modifying task state, or
2. Use atomic state transitions with proper ordering

---

### RC-009: `time_slice` Accessed Without Lock

**File:** `kernel/sched/scheduler.cpp`
**Lines:** 908, 914, 916, 955
**Severity:** HIGH

**Description:**
`task->time_slice` is decremented in `tick()` under `sched_lock`, but read in `preempt()` without any lock. The comment claims "No lock needed - time_slice is only modified by the owning task or tick()" but this is incorrect on SMP systems.

**Recommendation:**
Either use atomic operations for `time_slice` or acquire lock in `preempt()`.

---

### RC-010: steal_task() Statistics Race

**File:** `kernel/sched/scheduler.cpp`
**Lines:** 508
**Severity:** HIGH

**Description:**
`steal_task()` increments `per_cpu_sched[current_cpu].steals++` without holding the current CPU's lock (only the victim's lock is held).

**Recommendation:**
Acquire current CPU's lock before incrementing statistics, or use atomic increment.

---

### RC-011: balance_load() Statistics Read Race

**File:** `kernel/sched/scheduler.cpp`
**Lines:** 1205-1215
**Severity:** HIGH

**Description:**
`balance_load()` reads `per_cpu_sched[i].total_tasks` for all CPUs without acquiring per-CPU locks, potentially seeing inconsistent values.

**Recommendation:**
Acquire per-CPU locks when reading statistics, or accept approximate values for load balancing decisions.

---

## MEDIUM Issues

### RC-012: VMM virt_to_phys() Not Synchronized

**File:** `kernel/mm/vmm.cpp`
**Lines:** 427-469
**Severity:** MEDIUM

**Description:**
`virt_to_phys()` reads page table entries without holding `vmm_lock`. If another CPU is modifying page tables, torn reads could occur.

**Recommendation:**
Acquire `vmm_lock` for read operations, or use read-write lock.

---

### RC-013: Buddy Allocator free_pages_count() Unsynchronized

**File:** `kernel/mm/buddy.cpp`
**Lines:** 318-336
**Severity:** MEDIUM

**Description:**
`free_pages_count()` reads multiple `free_areas_[i].count` values without holding the lock. Comment explicitly says "we don't acquire the lock here" - may read inconsistent values.

**Recommendation:**
Accept approximate value for statistics, or acquire lock.

---

### RC-014: Task next_task_id Race

**File:** `kernel/sched/task.cpp`
**Lines:** 39, 294, 445
**Severity:** MEDIUM

**Description:**
`next_task_id++` is executed without any lock during task creation, potentially producing duplicate task IDs.

**Recommendation:**
Protect with task subsystem lock or use atomic fetch-and-add.

---

### RC-015: current_task Global Pointer Race

**File:** `kernel/sched/task.cpp`
**Lines:** 42, 521-530
**Severity:** MEDIUM

**Description:**
`current_task` is a global pointer read by `task::current()` and written by `task::set_current()` without synchronization. On SMP, each CPU should have its own current task pointer.

**Recommendation:**
Use per-CPU storage for current task pointer.

---

### RC-016: reap_exited() Race

**File:** `kernel/sched/task.cpp`
**Lines:** 834-879
**Severity:** MEDIUM

**Description:**
`reap_exited()` iterates through all tasks checking state and freeing resources without any lock. Could race with:
- Scheduler putting a task in queue
- Another reaper running concurrently
- Task state transitions

**Recommendation:**
Acquire task subsystem lock during reaping.

---

### RC-017: wakeup() State Transition Race

**File:** `kernel/sched/task.cpp`
**Lines:** 933-954
**Severity:** MEDIUM

**Description:**
`wakeup()` checks and modifies task state without any lock:
```cpp
if (t->state != TaskState::Blocked) return false;
// ... window for race ...
t->state = TaskState::Ready;
scheduler::enqueue(t);
```

**Recommendation:**
Acquire `sched_lock` before modifying state and enqueueing.

---

### RC-018: kill() Viper Manipulation Race

**File:** `kernel/sched/task.cpp`
**Lines:** 1005-1030
**Severity:** MEDIUM

**Description:**
`kill()` modifies viper's children list (reparenting) without any lock. Could race with other processes doing fork/exit.

**Recommendation:**
Acquire viper subsystem lock when modifying process tree.

---

## LOW Issues

### RC-019: Channel get() Returns Pointer After Lock Release

**File:** `kernel/ipc/channel.cpp`
**Lines:** 61-72
**Severity:** LOW

**Description:**
`channel::get()` acquires lock, finds channel, releases lock, returns pointer. After return, another thread could close the channel, making the pointer stale. However, callers typically re-validate under their own lock.

**Recommendation:**
Document that returned pointer validity is limited, or keep lock held.

---

### RC-020: Capability Table Entry Pointer Lifetime

**File:** `kernel/cap/table.cpp`
**Lines:** 128-160
**Severity:** LOW

**Description:**
`Table::get()`, `get_checked()`, `get_with_rights()` return pointers to entries after releasing the lock. If another thread removes the entry, the pointer becomes stale.

**Recommendation:**
Either hold lock longer or return copies instead of pointers.

---

### RC-021: init() Functions Generally Unsafe for Concurrent Calls

**Files:** Various `init()` functions
**Severity:** LOW

**Description:**
Most `init()` functions assume single-threaded boot and don't protect against concurrent initialization. This is typically acceptable but should be documented.

**Recommendation:**
Add `initialized` flag check with atomic test-and-set if concurrent init is possible.

---

### RC-022: Serial Output Interleaving

**File:** `kernel/console/serial.cpp` (implied)
**Severity:** LOW

**Description:**
Serial output functions likely don't have fine-grained locking, causing interleaved output from multiple CPUs during debugging.

**Recommendation:**
Add per-line buffering or spinlock for serial output, or accept interleaved debug output.

---

### RC-023: VirtIO Block Driver async_requests_ Race

**File:** `kernel/drivers/virtio/blk.cpp`
**Lines:** 250-257, 551-558
**Severity:** MEDIUM

**Description:**
The `async_requests_[]` array is searched and modified without any lock. Multiple concurrent requests could find the same "free" slot:
```cpp
for (usize i = 0; i < MAX_PENDING; i++) {
    if (!async_requests_[i].in_use) {
        req_idx = i;
        break;
    }
}
```

**Recommendation:**
Add a spinlock protecting `async_requests_[]` slot allocation and deallocation.

---

### RC-024: VirtIO Block Driver io_complete_ Not Atomic

**File:** `kernel/drivers/virtio/blk.cpp`
**Lines:** 229-230, 325-326, 344, 494, 707
**Severity:** MEDIUM

**Description:**
The `io_complete_` and `completed_desc_` fields are written by the interrupt handler and read by synchronous I/O functions without proper memory barriers or atomic operations. On ARM64, this could cause:
- Stale reads of completion status
- Incorrect descriptor matching
- Missed completions or double completions

**Recommendation:**
Use `std::atomic` or volatile with explicit memory barriers for `io_complete_` and `completed_desc_`.

---

## Files Reviewed

| File | Status | Issues Found |
|------|--------|--------------|
| kernel/lib/spinlock.hpp | DONE | RC-001, RC-004 |
| kernel/sched/scheduler.cpp | DONE | RC-006, RC-007, RC-008, RC-009, RC-010, RC-011 |
| kernel/sched/scheduler.hpp | DONE | (interface only) |
| kernel/sched/task.cpp | DONE | RC-002, RC-014, RC-015, RC-016, RC-017, RC-018 |
| kernel/sched/task.hpp | DONE | (interface only) |
| kernel/sched/wait.hpp | DONE | RC-005 |
| kernel/sched/wait.cpp | DONE | RC-005 |
| kernel/ipc/channel.cpp | DONE | RC-008, RC-019 |
| kernel/ipc/poll.cpp | DONE | RC-003 |
| kernel/mm/pmm.cpp | DONE | (properly locked) |
| kernel/mm/buddy.cpp | DONE | RC-013 |
| kernel/mm/vmm.cpp | DONE | RC-012 |
| kernel/cap/table.cpp | DONE | RC-020 |
| kernel/drivers/virtio/blk.cpp | DONE | RC-023, RC-024 |
| kernel/drivers/virtio/virtqueue.cpp | DONE | (properly locked) |
| kernel/drivers/virtio/gpu.cpp | DONE | (single-threaded) |
| kernel/drivers/virtio/input.cpp | DONE | (single-threaded) |
| kernel/drivers/virtio/rng.cpp | DONE | (single-threaded) |
| kernel/fs/cache.cpp | DONE | (properly locked) |
| kernel/fs/viperfs/*.cpp | DONE | (uses cache lock) |
| user/servers/blkd/*.cpp | DONE | (single-threaded server) |
| user/servers/fsd/*.cpp | DONE | (single-threaded server) |
| user/servers/consoled/*.cpp | DONE | (single-threaded server) |
| user/servers/displayd/*.cpp | DONE | (single-threaded server) |
| user/servers/netd/*.cpp | DONE | (single-threaded server) |

---

## Detailed File Reviews

### kernel/lib/spinlock.hpp

**Review Date:** 2026-01-20
**Lines of Code:** 302
**Patterns Used:** Ticket spinlock, LDAXR/STXR atomics, RAII guard

**Correct Implementations:**
- Ticket lock algorithm is correctly implemented for fairness
- Load-acquire/store-release semantics properly used
- SpinlockGuard RAII pattern is correct
- `try_acquire()` correctly handles CAS failure

**Issues Found:** RC-001 (CRITICAL), RC-004 (HIGH)

---

### kernel/sched/scheduler.cpp

**Review Date:** 2026-01-20
**Lines of Code:** 1231
**Patterns Used:** Priority queues, per-CPU scheduling, work stealing

**Correct Implementations:**
- `sched_lock` properly protects queue operations
- Per-CPU lock ordering documented and mostly followed
- Context switch timing is correct

**Issues Found:** RC-006, RC-007, RC-008, RC-009, RC-010, RC-011

---

### kernel/sched/task.cpp

**Review Date:** 2026-01-20
**Lines of Code:** 1051
**Patterns Used:** Fixed task table, kernel stack pool

**Correct Implementations:**
- Task context setup is correct
- User task entry trampoline is properly implemented

**Issues Found:** RC-002 (CRITICAL), RC-014, RC-015, RC-016, RC-017, RC-018

---

### kernel/ipc/channel.cpp

**Review Date:** 2026-01-20
**Lines of Code:** 756
**Patterns Used:** Ring buffer, capability-based handles

**Correct Implementations:**
- `channel_lock` properly protects channel operations
- Blocking send/recv correctly release lock before yield
- Handle transfer logic is correct

**Issues Found:** RC-008, RC-019

---

### kernel/ipc/poll.cpp

**Review Date:** 2026-01-20
**Lines of Code:** 541
**Patterns Used:** Timer table, wait queue

**Issues Found:** RC-003 (CRITICAL)

**Note:** This file has the most severe synchronization problems in the codebase.

---

### kernel/mm/pmm.cpp

**Review Date:** 2026-01-20
**Lines of Code:** 488
**Patterns Used:** Bitmap allocator, buddy allocator integration

**Correct Implementations:**
- `pmm_lock` properly protects bitmap operations
- Buddy allocator fallback is correctly locked

**Issues Found:** None (properly synchronized)

---

### kernel/mm/buddy.cpp

**Review Date:** 2026-01-20
**Lines of Code:** 462
**Patterns Used:** Buddy allocation, per-CPU page cache

**Correct Implementations:**
- `lock_` properly protects allocation/deallocation
- Per-CPU cache has its own per-CPU lock

**Issues Found:** RC-013 (statistics read race)

---

### kernel/mm/vmm.cpp

**Review Date:** 2026-01-20
**Lines of Code:** 490
**Patterns Used:** 4-level page tables, TLB invalidation

**Correct Implementations:**
- `vmm_lock` properly protects map/unmap operations
- TLB invalidation barriers are correct

**Issues Found:** RC-012 (virt_to_phys read race)

---

### kernel/cap/table.cpp

**Review Date:** 2026-01-20
**Lines of Code:** 311
**Patterns Used:** Generation counters, free list

**Correct Implementations:**
- `lock_` properly protects all table operations
- Generation counter correctly detects stale handles

**Issues Found:** RC-020 (returned pointer lifetime)

---

## Recommendations Summary

### Immediate Actions (Critical)

1. **Fix spinlock saved_daif_ race (RC-001):** Modify `Spinlock` to return saved DAIF from `acquire()` and accept it in `release()`. Update `SpinlockGuard` to store the saved state.

2. **Add task subsystem lock (RC-002):** Create a spinlock protecting `allocate_task()`, `free_kernel_stack()`, and `next_task_id`.

3. **Add poll subsystem lock (RC-003):** Create a spinlock protecting all timer and wait queue operations.

### Short-term Actions (High)

4. **Document AtomicFlag limitations (RC-004):** Add comments that `AtomicFlag` must not be used when interrupt handlers may contend.

5. **Document WaitQueue locking requirements (RC-005):** Add documentation that callers must hold an appropriate higher-level lock.

6. **Use atomics for counters (RC-006, RC-007):** Replace `context_switch_count`, `running`, `load_balance_counter` with `std::atomic` or equivalent.

7. **Unify task state locking (RC-008):** All code modifying task state should acquire `sched_lock`.

### Medium-term Actions

8. **Per-CPU current task (RC-015):** Move `current_task` to per-CPU storage.

9. **Review VMM read operations (RC-012):** Consider read-write lock for page table operations.

10. **Audit all returned pointers:** Ensure callers understand lifetime constraints.

---

## Conclusion

This audit identified **24 potential race conditions** in the ViperDOS kernel and user-space components. The most critical issues are:

1. **RC-001 (Spinlock saved_daif_):** The foundational synchronization primitive has a race that affects all users of spinlocks on multi-core systems.

2. **RC-002 (Task Allocation):** Task creation without proper synchronization could cause task slot corruption on SMP.

3. **RC-003 (Poll Subsystem):** The timer and wait queue subsystem has no synchronization, affecting all blocking I/O operations.

### Positive Findings

Several components are properly synchronized:
- **PMM (Physical Memory Manager):** Uses `pmm_lock` correctly
- **Capability Table:** Uses `lock_` for all operations
- **Block Cache:** Uses `cache_lock` properly
- **Channel IPC:** Uses `channel_lock` for channel operations

### Recommended Priority

1. **Immediate (before SMP testing):** Fix RC-001, RC-002, RC-003
2. **High (before production):** Fix RC-004 through RC-011
3. **Medium (quality improvement):** Fix RC-012 through RC-024
4. **Low (optional):** Document or accept RC-019 through RC-022

---

**Audit Completed:** 2026-01-20
**Files Analyzed:** 35+ kernel and user-space source files
**Total Lines Reviewed:** ~15,000 lines of C++ code
