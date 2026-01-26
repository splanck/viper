# ViperDOS Task Scheduling: Comprehensive Review and Recommendations

**Version:** 1.0
**Date:** January 2026
**Total Scheduling SLOC:** ~3,200 lines

---

## Executive Summary

ViperDOS implements a sophisticated multi-policy preemptive scheduler with support for real-time tasks (SCHED_FIFO, SCHED_RR), deadline scheduling (SCHED_DEADLINE/EDF), and completely fair scheduling (CFS). The system is designed for both single-CPU and SMP systems with per-CPU scheduling queues and work stealing.

**Key Finding:** The scheduler is well-architected for current use cases but has several scalability limitations that will become problematic with larger binaries and more complex workloads.

**Critical Concerns:**
1. Fixed 64-task limit will be exhausted by complex applications
2. O(n) task table scans for lookups
3. No user-space signal handler execution
4. Limited priority inheritance (single-owner only)

---

## 1. How Task Scheduling Works in ViperDOS

### 1.1 Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         Scheduling Hierarchy                             │
├─────────────────────────────────────────────────────────────────────────┤
│  SCHED_DEADLINE (Highest)  │  Earliest Deadline First (EDF)             │
│    - Admission control     │  - Tasks with dl_runtime/dl_deadline/period│
│    - 95% bandwidth limit   │  - Always runs before RT and SCHED_OTHER   │
├────────────────────────────┼────────────────────────────────────────────┤
│  Real-Time (SCHED_FIFO/RR) │  Priority-based FIFO within queues         │
│    - FIFO: runs until yield│  - Higher priority than time-sharing       │
│    - RR: 100ms quantum     │  - 8 priority queues (0-7)                 │
├────────────────────────────┼────────────────────────────────────────────┤
│  SCHED_OTHER (Lowest)      │  Completely Fair Scheduler (CFS)           │
│    - vruntime tracking     │  - Nice values (-20 to +19)                │
│    - Weight-based fairness │  - Time slice: 5-20ms by priority          │
└─────────────────────────────────────────────────────────────────────────┘
```

### 1.2 Task Control Block (TCB)

Each task is represented by a `Task` structure containing:

```cpp
struct Task {
    // Identity
    u32 id;                    // Unique task ID
    char name[32];             // Debug name
    TaskState state;           // Invalid, Ready, Running, Blocked, Exited
    u32 flags;                 // TASK_FLAG_KERNEL, TASK_FLAG_USER, TASK_FLAG_IDLE

    // Scheduling
    u8 priority;               // 0-255 (0=highest, 255=lowest)
    SchedPolicy policy;        // SCHED_OTHER, SCHED_FIFO, SCHED_RR, SCHED_DEADLINE
    u32 time_slice;            // Remaining ticks for this quantum
    u32 cpu_affinity;          // Bitmask of allowed CPUs

    // CFS
    u64 vruntime;              // Virtual runtime (ns)
    i8 nice;                   // -20 to +19 (default 0)

    // Deadline (EDF)
    u64 dl_runtime;            // Max runtime per period (ns)
    u64 dl_deadline;           // Relative deadline (ns)
    u64 dl_period;             // Period length (ns)
    u64 dl_abs_deadline;       // Absolute deadline (timer ticks)

    // Queue linkage
    Task *next, *prev;         // Doubly-linked list pointers
    void *wait_channel;        // What we're waiting on (debug)

    // Execution context
    TaskContext context;       // Saved x19-x30, SP
    u8 *kernel_stack;          // Kernel stack allocation
    TrapFrame *trap_frame;     // For exceptions

    // Process association
    ViperProcess *viper;       // User process (if any)
    u64 user_entry;            // User mode entry point
    u64 user_stack;            // User mode stack pointer

    // Signal handling
    SignalState signals;       // Handlers, pending, blocked masks
};
```

### 1.3 Priority Queue Organization

Tasks are organized into 8 priority queues:

```
Priority Range    Queue    Time Slice
─────────────────────────────────────
0-31   (Highest)    0        20 ms
32-63               1        18 ms
64-95               2        15 ms
96-127              3        12 ms
128-159 (Default)   4        10 ms
160-191             5         8 ms
192-223             6         5 ms
224-255 (Lowest)    7         5 ms
```

Each queue is a doubly-linked list with head/tail pointers for O(1) enqueue/dequeue.

### 1.4 Scheduling Flow

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        Timer Interrupt (tick)                            │
├─────────────────────────────────────────────────────────────────────────┤
│  1. Check if idle task and work available → preempt                      │
│  2. Check if higher-priority task ready → preempt                        │
│  3. Decrement time_slice (except SCHED_FIFO)                            │
│  4. Update vruntime for CFS tasks                                        │
│  5. If time_slice == 0 → call preempt()                                 │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                           schedule()                                     │
├─────────────────────────────────────────────────────────────────────────┤
│  1. Acquire scheduler lock                                               │
│  2. Try per-CPU queue first (reduces contention)                        │
│  3. Fall back to global queue if empty                                   │
│  4. If no task: use idle task                                           │
│  5. Account CPU time to outgoing task                                    │
│  6. Put current task back in ready queue (if still runnable)            │
│  7. Calculate new time slice based on policy                            │
│  8. Switch address space if different process                           │
│  9. Release lock                                                         │
│ 10. context_switch(&old->context, &new->context)                        │
└─────────────────────────────────────────────────────────────────────────┘
```

### 1.5 Task Selection Algorithm (dequeue)

```cpp
Task* dequeue_locked() {
    // Pass 1: SCHED_DEADLINE (EDF)
    // Scan ALL queues for task with earliest dl_abs_deadline
    Task *earliest = nullptr;
    for each queue q in 0..7:
        for each task t in q:
            if t.policy == SCHED_DEADLINE:
                if earliest == nullptr || t.dl_abs_deadline < earliest.dl_abs_deadline:
                    earliest = t;
    if earliest != nullptr:
        remove(earliest);
        return earliest;

    // Pass 2: Real-Time (SCHED_FIFO/SCHED_RR)
    // Take first RT task from highest-priority non-empty queue
    for q in 0..7:
        for each task t in q:
            if t.policy == SCHED_FIFO or t.policy == SCHED_RR:
                if t.cpu_affinity & (1 << current_cpu):
                    remove(t);
                    return t;

    // Pass 3: SCHED_OTHER (CFS)
    // Find task with lowest vruntime in each queue
    for q in 0..7:
        Task *best = nullptr;
        for each task t in q:
            if t.policy == SCHED_OTHER:
                if t.cpu_affinity & (1 << current_cpu):
                    if best == nullptr || t.vruntime < best.vruntime:
                        best = t;
        if best != nullptr:
            remove(best);
            return best;

    return nullptr;  // No runnable task
}
```

### 1.6 Context Switching

Context switch is implemented in assembly (`context.S`):

```asm
context_switch:
    // x0 = pointer to old TaskContext (save destination)
    // x1 = pointer to new TaskContext (restore source)

    // Save callee-saved registers
    stp x19, x20, [x0, #0x00]
    stp x21, x22, [x0, #0x10]
    stp x23, x24, [x0, #0x20]
    stp x25, x26, [x0, #0x30]
    stp x27, x28, [x0, #0x40]
    stp x29, x30, [x0, #0x50]   // x30 = return address
    mov x2, sp
    str x2, [x0, #0x60]         // Save stack pointer

    // Restore new task's registers
    ldp x19, x20, [x1, #0x00]
    ldp x21, x22, [x1, #0x10]
    ldp x23, x24, [x1, #0x20]
    ldp x25, x26, [x1, #0x30]
    ldp x27, x28, [x1, #0x40]
    ldp x29, x30, [x1, #0x50]
    ldr x2, [x1, #0x60]
    mov sp, x2

    ret                         // Branch to x30 (new task's PC)
```

### 1.7 Per-CPU Scheduling and Work Stealing

```
CPU 0                    CPU 1                    CPU 2
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│ Per-CPU Lock    │     │ Per-CPU Lock    │     │ Per-CPU Lock    │
├─────────────────┤     ├─────────────────┤     ├─────────────────┤
│ Queue 0 (RT)    │     │ Queue 0 (RT)    │     │ Queue 0 (RT)    │
│ Queue 1         │     │ Queue 1         │     │ Queue 1         │
│ Queue 2         │     │ Queue 2         │     │ Queue 2         │
│ Queue 3         │     │ Queue 3         │     │ Queue 3         │
│ Queue 4         │◄────│ Queue 4         │     │ Queue 4         │
│ Queue 5         │steal│ Queue 5         │     │ Queue 5         │
│ Queue 6         │     │ Queue 6         │     │ Queue 6         │
│ Queue 7         │     │ Queue 7         │     │ Queue 7         │
└─────────────────┘     └─────────────────┘     └─────────────────┘
         ▲                                              │
         └──────────────── Work Stealing ───────────────┘
                    (only from queues 4-7)
```

**Work Stealing Rules:**
- Only steal from low-priority queues (4-7) to preserve RT guarantees
- Use non-blocking try_acquire on victim's lock
- Only steal tasks with compatible CPU affinity
- Load balance every 100 timer ticks

### 1.8 Wait Queues and Blocking

```cpp
// Blocking pattern
void wait_for_data(WaitQueue *wq) {
    u64 saved = lock.acquire();
    while (no_data_available()) {
        wait_enqueue(wq, current());    // Add to wait queue
        lock.release(saved);
        yield();                         // Reschedule
        saved = lock.acquire();
    }
    // Data available, consume it
    lock.release(saved);
}

// Waking pattern
void produce_data(WaitQueue *wq) {
    u64 saved = lock.acquire();
    add_data_to_buffer();
    wait_wake_one(wq);                   // Wake first waiter
    lock.release(saved);
}
```

---

## 2. Component Analysis

### 2.1 Task Management (task.cpp)

**Files:** `kernel/sched/task.hpp` (289 lines), `kernel/sched/task.cpp` (548 lines)

**Strengths:**
- Clean task lifecycle management
- Guard pages for stack overflow detection
- Stack pool recycling prevents fragmentation
- Per-task signal state for POSIX compatibility

**Weaknesses:**

| Issue | Location | Impact |
|-------|----------|--------|
| Fixed 64-task limit | `task.hpp:189` | Complex apps will exhaust |
| O(n) task lookup by ID | `task.cpp:get_by_id` | Slow with many tasks |
| Kernel stack fixed at 16KB | `task.hpp:63` | May overflow for deep recursion |
| No task groups/cgroups | Fundamental | No resource limits per group |

### 2.2 Scheduler (scheduler.cpp)

**Files:** `kernel/sched/scheduler.hpp` (185 lines), `kernel/sched/scheduler.cpp` (724 lines)

**Strengths:**
- Multi-policy support (FIFO, RR, Deadline, CFS)
- Per-CPU queues reduce SMP contention
- Work stealing for load balancing
- Proper lock ordering prevents deadlocks

**Weaknesses:**

| Issue | Location | Impact |
|-------|----------|--------|
| O(n) deadline scan | `scheduler.cpp:dequeue_locked` | Slow with many deadline tasks |
| O(n) CFS vruntime scan | `scheduler.cpp:dequeue_locked` | Slow with many tasks per queue |
| Global sched_lock contention | `scheduler.cpp:81` | SMP bottleneck |
| No priority queue bitmap | Fundamental | Must scan all 8 queues |

### 2.3 Wait Queues (wait.cpp)

**Files:** `kernel/sched/wait.hpp` (117 lines), `kernel/sched/wait.cpp` (149 lines)

**Strengths:**
- Clean FIFO ordering for fairness
- Uses task's intrusive list pointers (no allocation)
- Atomic wake operations

**Weaknesses:**

| Issue | Location | Impact |
|-------|----------|--------|
| O(n) dequeue search | `wait.cpp:wait_dequeue` | Slow for many waiters |
| No priority ordering | Fundamental | High-priority task may wait behind low-priority |
| No timeout support | Fundamental | Cannot wait with deadline |

### 2.4 Priority Inheritance (pi.cpp)

**Files:** `kernel/sched/pi.hpp` (75 lines), `kernel/sched/pi.cpp` (126 lines)

**Strengths:**
- Prevents basic priority inversion
- Clean API with try_lock/unlock semantics
- Automatic priority restoration

**Weaknesses:**

| Issue | Location | Impact |
|-------|----------|--------|
| Single-owner only | `pi.hpp:42` | No chain inheritance |
| No waiter tracking | Fundamental | Can't boost to multiple waiters |
| No deadlock detection | Fundamental | Lock ordering bugs not caught |

### 2.5 Deadline Scheduling (deadline.cpp)

**Files:** `kernel/sched/deadline.hpp` (98 lines), `kernel/sched/deadline.cpp` (142 lines)

**Strengths:**
- Proper EDF implementation
- Admission control (95% bandwidth cap)
- Parameter validation

**Weaknesses:**

| Issue | Location | Impact |
|-------|----------|--------|
| No CBS (Constant Bandwidth Server) | Fundamental | Overruns affect other tasks |
| No deadline miss detection | Fundamental | Silent failures |
| O(n) earliest deadline search | `scheduler.cpp` | Slow with many deadline tasks |

### 2.6 Signal Handling (signal.cpp)

**Files:** `kernel/sched/signal.hpp` (159 lines), `kernel/sched/signal.cpp` (341 lines)

**Strengths:**
- POSIX signal numbers and semantics
- Per-signal handler registration
- Signal masking support

**Weaknesses:**

| Issue | Location | Impact |
|-------|----------|--------|
| No user handler execution | `signal.cpp:process_pending` | Apps can't handle signals |
| No sigaltstack support | Fundamental | Stack overflow in handlers |
| No sigqueue (RT signals) | Fundamental | Limited IPC capability |

---

## 3. Scalability Analysis

### 3.1 O(n) Operations

| Operation | Complexity | Trigger Frequency |
|-----------|------------|-------------------|
| Task lookup by ID | O(MAX_TASKS) | Every kill/wait syscall |
| Deadline task selection | O(total_tasks) | Every schedule() |
| CFS vruntime minimum | O(queue_length) | Every schedule() |
| Work stealing | O(MAX_CPUS × 4) | Every 100 ticks |
| Wait queue search | O(waiters) | Every timeout/cancel |
| Task table enumeration | O(MAX_TASKS) | Debug/stats only |

### 3.2 Lock Contention Points

| Lock | Scope | Contention Level |
|------|-------|------------------|
| `task_lock` | Task creation/destruction | Medium |
| `sched_lock` | Global queue operations | High on SMP |
| `per_cpu_sched[i].lock` | Per-CPU queues | Low (good) |
| Wait queue locks | Per-queue | Low-Medium |

### 3.3 Fixed Limits

| Resource | Limit | Concern |
|----------|-------|---------|
| Tasks | 64 | **Critical** - will exhaust |
| Priority queues | 8 | Acceptable |
| Kernel stack | 16KB | May need increase |
| CPUs | 4 | Sufficient for current hardware |
| Deadline bandwidth | 95% | Acceptable |

### 3.4 Memory Usage

```
Per-Task Memory:
  Task struct:     ~1,024 bytes
  Kernel stack:    16,384 bytes
  Guard page:       4,096 bytes (unmapped)
  ─────────────────────────────
  Total per task:  ~21,504 bytes

For 64 tasks:  ~1.3 MB
For 256 tasks: ~5.3 MB
For 1024 tasks: ~21 MB
```

---

## 4. Reliability Concerns

### 4.1 Error Handling

| Scenario | Current Behavior | Risk |
|----------|------------------|------|
| Stack overflow | Guard page fault → SIGSEGV | Good |
| Task table full | create() returns nullptr | Caller must check |
| Deadline admission fail | set_deadline() returns false | Caller must check |
| Work stealing fail | Falls back to idle | Acceptable |
| Signal delivery fail | Task killed | May lose data |

### 4.2 Potential Race Conditions

| Location | Issue | Risk Level |
|----------|-------|------------|
| `scheduler.cpp:tick()` | Reads time_slice without lock | Low (single writer) |
| `task.cpp:current()` | Per-CPU variable access | Low (correct barriers) |
| `signal.cpp:send_signal` | Pending bitmap modification | Low (atomic ops) |

### 4.3 Resource Leak Risks

| Resource | Leak Scenario | Prevention |
|----------|---------------|------------|
| Task slots | Exit without reap | `reap_exited()` periodic call |
| Kernel stacks | Exit without cleanup | Stack pool tracks free list |
| Wait queue entries | Task killed while waiting | Cleanup in `task::exit()` |

---

## 5. Performance Recommendations

### 5.1 Immediate Priority (Before Large Binaries)

#### 1. Increase Task Limit to 256

**Current:** `MAX_TASKS = 64`

**Problem:** Complex applications with threads, child processes, and daemons will exhaust this limit quickly.

**Recommendation:**
```cpp
// In kernel/sched/task.hpp
constexpr usize MAX_TASKS = 256;

// In kernel/sched/task.cpp
// Increase stack pool accordingly
constexpr usize STACK_POOL_SLOTS = 256;
```

**Impact:** ~5.3 MB additional memory, 4x more task capacity.

#### 2. Add Task ID Hash Table for O(1) Lookup

**Current:** Linear scan of task table for `get_by_id()`.

**Problem:** O(64) now, O(256) after increase.

**Recommendation:**
```cpp
// Add hash table indexed by task ID
constexpr usize TASK_HASH_SIZE = 64;  // Power of 2
Task* task_hash[TASK_HASH_SIZE];

Task* get_by_id(u32 id) {
    u32 bucket = id % TASK_HASH_SIZE;
    Task* t = task_hash[bucket];
    while (t && t->id != id) t = t->hash_next;
    return t;
}
```

**Impact:** O(1) average lookup, slight memory increase.

#### 3. Add Priority Queue Bitmap

**Current:** Scan all 8 queues to find non-empty.

**Problem:** Wastes cycles checking empty queues.

**Recommendation:**
```cpp
struct PerCpuScheduler {
    PriorityQueue queues[8];
    u8 queue_bitmap;  // Bit i set if queue i non-empty
    // ...
};

// In enqueue:
queue_bitmap |= (1 << queue_index);

// In dequeue:
while (queue_bitmap) {
    int q = __builtin_ffs(queue_bitmap) - 1;
    if (queues[q].head) return dequeue_from(q);
    queue_bitmap &= ~(1 << q);  // Queue emptied
}
```

**Impact:** O(1) finding highest-priority non-empty queue.

#### 4. Implement User Signal Handler Execution

**Current:** Only default signal actions work.

**Problem:** Applications cannot handle SIGINT, SIGTERM, etc.

**Recommendation:**
```cpp
void deliver_user_signal(Task* t, int signum) {
    u64 handler = t->signals.handlers[signum];
    if (handler == SIG_DFL || handler == SIG_IGN) {
        // Current behavior
        return;
    }

    // Save current trap frame
    t->signals.saved_frame = *t->trap_frame;

    // Set up signal frame on user stack
    u64 sp = t->trap_frame->sp - sizeof(SignalFrame);
    SignalFrame* frame = (SignalFrame*)sp;
    frame->signum = signum;
    frame->return_addr = SIGRETURN_TRAMPOLINE;

    // Redirect execution to handler
    t->trap_frame->elr = handler;
    t->trap_frame->sp = sp;
    t->trap_frame->x[0] = signum;  // First argument
}
```

**Impact:** POSIX-compliant signal handling.

### 5.2 Medium Priority (For Production Use)

#### 5. Replace O(n) CFS Selection with Red-Black Tree

**Current:** Linear scan for minimum vruntime.

**Problem:** O(n) per schedule() with many SCHED_OTHER tasks.

**Recommendation:**
```cpp
// Use red-black tree keyed by vruntime
struct CfsRunQueue {
    RBTree<u64, Task*> tasks;  // vruntime → task
    u64 min_vruntime;          // Cache minimum
};

Task* pick_next_cfs() {
    return tasks.minimum();  // O(log n)
}
```

**Impact:** O(log n) task selection, matches Linux CFS.

#### 6. Replace O(n) Deadline Selection with Deadline Queue

**Current:** Scan all tasks for earliest deadline.

**Problem:** O(n) even with few deadline tasks.

**Recommendation:**
```cpp
// Separate queue for deadline tasks, sorted by dl_abs_deadline
struct DeadlineQueue {
    Task* head;  // Earliest deadline first
};

void deadline_enqueue(Task* t) {
    // Insert in sorted order by dl_abs_deadline
}

Task* deadline_dequeue() {
    return head;  // O(1)
}
```

**Impact:** O(1) deadline task selection.

#### 7. Implement Priority-Ordered Wait Queues

**Current:** FIFO ordering in wait queues.

**Problem:** High-priority task may wait behind low-priority.

**Recommendation:**
```cpp
void wait_enqueue_priority(WaitQueue* wq, Task* t) {
    Task** pp = &wq->head;
    while (*pp && (*pp)->priority <= t->priority) {
        pp = &(*pp)->next;
    }
    t->next = *pp;
    *pp = t;
}
```

**Impact:** Priority inversion prevention in wait queues.

#### 8. Add Wait Timeout Support

**Current:** Blocking waits have no timeout.

**Problem:** Cannot implement `select()` with timeout.

**Recommendation:**
```cpp
bool wait_with_timeout(WaitQueue* wq, u64 timeout_ticks) {
    Task* t = current();
    t->wake_time = timer::ticks() + timeout_ticks;

    wait_enqueue(wq, t);
    timer::add_wakeup(t, timeout_ticks);

    yield();

    // Check if woken by timeout or event
    return (t->state != TaskState::Ready);  // true if event, false if timeout
}
```

**Impact:** Enables timed waits, `select()`, `poll()` timeouts.

### 5.3 Long-Term (For Full OS)

#### 9. Implement Full Priority Inheritance Chain

**Current:** Single-owner priority inheritance.

**Problem:** Nested lock priority inversion not handled.

**Recommendation:**
```cpp
struct PiMutex {
    Task* owner;
    Task* waiters_head;  // Linked list of waiters
    PiMutex* next_held;  // Next mutex held by owner
};

void boost_chain(Task* waiter, PiMutex* mutex) {
    Task* owner = mutex->owner;
    while (owner && waiter->priority < owner->priority) {
        owner->boosted_priority = waiter->priority;
        // Follow to mutex that owner is blocked on
        if (owner->blocked_on) {
            mutex = owner->blocked_on;
            owner = mutex->owner;
        } else {
            break;
        }
    }
}
```

**Impact:** Complete priority inversion solution.

#### 10. Add Deadline Miss Detection and Handling

**Current:** No detection of deadline misses.

**Problem:** Silent failures in real-time applications.

**Recommendation:**
```cpp
void check_deadline_miss(Task* t) {
    if (t->policy != SCHED_DEADLINE) return;

    u64 now = timer::ticks();
    if (now > t->dl_abs_deadline) {
        t->dl_missed_count++;
        serial::puts("[deadline] MISS: ");
        serial::puts(t->name);
        serial::puts(" by ");
        serial::put_dec(now - t->dl_abs_deadline);
        serial::puts(" ticks\n");

        // Option: kill task, lower priority, or notify
        send_signal(t, SIGXCPU);
    }
}
```

**Impact:** Visibility into real-time performance.

#### 11. Implement Lock-Free Run Queues

**Current:** Spinlock-protected linked lists.

**Problem:** Lock contention on SMP, latency spikes.

**Recommendation:**
- Use lock-free MPSC (multi-producer, single-consumer) queues for enqueue
- Each CPU consumes from its own queue without locking
- Cross-CPU migration uses lock-free handoff

**Impact:** Reduced latency, better SMP scaling.

#### 12. Add CPU Bandwidth Control (cgroups-like)

**Current:** No resource limits per process group.

**Problem:** One process can starve others.

**Recommendation:**
```cpp
struct BandwidthController {
    u64 quota;      // Max CPU time per period
    u64 period;     // Period length
    u64 used;       // CPU time used this period
    Task* throttled; // Throttled tasks queue
};

void account_time(Task* t, u64 delta) {
    BandwidthController* bw = t->viper->bandwidth;
    bw->used += delta;
    if (bw->used >= bw->quota) {
        throttle_task(t);
    }
}
```

**Impact:** Fair resource allocation, DoS prevention.

---

## 6. Implementation Priority Matrix

| Recommendation | Priority | Effort | Impact |
|----------------|----------|--------|--------|
| 1. Increase task limit to 256 | **Critical** | Low | High |
| 2. Task ID hash table | **Critical** | Low | Medium |
| 3. Priority queue bitmap | High | Low | Medium |
| 4. User signal handlers | High | Medium | High |
| 5. CFS red-black tree | Medium | Medium | Medium |
| 6. Deadline queue | Medium | Low | Medium |
| 7. Priority-ordered waits | Medium | Low | Medium |
| 8. Wait timeout support | Medium | Medium | High |
| 9. Full PI chain | Low | High | Medium |
| 10. Deadline miss detection | Low | Low | Medium |
| 11. Lock-free queues | Low | High | Medium |
| 12. Bandwidth control | Low | High | Medium |

---

## 7. Files Summary

| File | Lines | Purpose |
|------|-------|---------|
| `kernel/sched/task.hpp` | 289 | Task structure and constants |
| `kernel/sched/task.cpp` | 548 | Task lifecycle management |
| `kernel/sched/scheduler.hpp` | 185 | Scheduler interface |
| `kernel/sched/scheduler.cpp` | 724 | Core scheduling logic |
| `kernel/sched/context.S` | 72 | Context switch assembly |
| `kernel/sched/wait.hpp` | 117 | Wait queue interface |
| `kernel/sched/wait.cpp` | 149 | Wait queue implementation |
| `kernel/sched/signal.hpp` | 159 | Signal definitions |
| `kernel/sched/signal.cpp` | 341 | Signal handling |
| `kernel/sched/deadline.hpp` | 98 | Deadline scheduling interface |
| `kernel/sched/deadline.cpp` | 142 | EDF implementation |
| `kernel/sched/pi.hpp` | 75 | Priority inheritance interface |
| `kernel/sched/pi.cpp` | 126 | Priority inheritance logic |
| `kernel/sched/idle.hpp` | 52 | Idle task interface |
| `kernel/sched/idle.cpp` | 68 | Idle task implementation |
| **Total** | **~3,145** | |

---

## 8. Testing Recommendations

### 8.1 Stress Tests Needed

```cpp
// Test 1: Task creation stress
TEST(Scheduler, TaskCreationStress) {
    // Create MAX_TASKS tasks
    // Verify all succeed
    // Exit half, create new ones
    // Verify reuse works
}

// Test 2: Priority inversion
TEST(Scheduler, PriorityInversion) {
    // Low-priority task holds mutex
    // High-priority task blocks on mutex
    // Medium-priority task tries to run
    // Verify high-priority runs before medium
}

// Test 3: Deadline scheduling
TEST(Scheduler, DeadlineScheduling) {
    // Create tasks with deadlines
    // Verify EDF ordering
    // Verify admission control
    // Detect deadline misses
}

// Test 4: Work stealing
TEST(Scheduler, WorkStealing) {
    // Pin tasks to one CPU
    // Verify other CPUs steal
    // Measure load balance
}

// Test 5: Signal handling
TEST(Scheduler, SignalHandling) {
    // Install signal handler
    // Send signal
    // Verify handler called
    // Verify sigreturn works
}
```

---

## 9. Conclusion

The ViperDOS scheduler is well-designed for current use cases but requires several enhancements for larger binaries:

**Must Fix Before Large Binaries:**
1. Increase MAX_TASKS from 64 to 256
2. Add task ID hash table for O(1) lookup
3. Add priority queue bitmap for O(1) queue selection
4. Implement user signal handler execution

**Recommended for Production:**
5. Replace CFS linear scan with red-black tree
6. Add separate deadline queue
7. Implement priority-ordered wait queues
8. Add wait timeout support

The codebase is clean, well-documented, and follows consistent patterns. With the critical recommendations implemented, ViperDOS should handle larger binaries effectively.

---

## Appendix: Quick Reference

### Task States
```
Invalid  → Created (not yet initialized)
Ready    → In run queue, waiting for CPU
Running  → Currently executing
Blocked  → Waiting for event
Exited   → Terminated, awaiting reap
```

### Scheduling Policies
```
SCHED_OTHER   → CFS (default, nice-based)
SCHED_FIFO    → Real-time FIFO (no preemption by time)
SCHED_RR      → Real-time Round-Robin (100ms quantum)
SCHED_DEADLINE → EDF (earliest deadline first)
```

### Key Constants
```cpp
MAX_TASKS = 64              // Maximum concurrent tasks
NUM_PRIORITY_QUEUES = 8     // Priority queue count
PRIORITIES_PER_QUEUE = 32   // Priorities mapped to each queue
KERNEL_STACK_SIZE = 16384   // 16KB kernel stack
TIME_SLICE_DEFAULT = 10     // 10ms default quantum
RT_TIME_SLICE_DEFAULT = 100 // 100ms RT quantum
LOAD_BALANCE_INTERVAL = 100 // Ticks between load balance
```
