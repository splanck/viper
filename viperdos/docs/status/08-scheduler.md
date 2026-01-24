# Scheduler and Task Management

**Status:** Complete priority-based preemptive scheduler with SMP support, CFS fair scheduling, deadline scheduling (
EDF), and priority inheritance
**Location:** `kernel/sched/`
**SLOC:** ~4,500

## Overview

The scheduler subsystem provides priority-based preemptive scheduling for both kernel and user-mode tasks. It
implements:

- **8 priority queues** with configurable time slices per priority level
- **CFS (Completely Fair Scheduler)** with vruntime tracking and nice values for SCHED_OTHER tasks
- **Real-time scheduling** (SCHED_FIFO and SCHED_RR) with priority over normal tasks
- **Deadline scheduling** (SCHED_DEADLINE) with EDF ordering and bandwidth reservation
- **CPU affinity** for binding tasks to specific CPUs
- **Priority inheritance** mutexes to prevent priority inversion
- **SMP support** with per-CPU run queues, work stealing, and load balancing
- **Idle state tracking** with WFI (Wait For Interrupt) power management

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                       Scheduler                                  │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │                   Priority Queues                          │  │
│  │  Queue 0 (0-31):   [T1]─[T2]─[T3]    ← Highest priority    │  │
│  │  Queue 1 (32-63):  [T4]─[T5]                               │  │
│  │  Queue 2 (64-95):  [ ]                                     │  │
│  │  Queue 3 (96-127): [T6]                                    │  │
│  │  Queue 4 (128-159):[T7]─[T8]─[T9]─...← Default priority    │  │
│  │  Queue 5 (160-191):[ ]                                     │  │
│  │  Queue 6 (192-223):[ ]                                     │  │
│  │  Queue 7 (224-255):[idle]            ← Lowest priority     │  │
│  └───────────────────────────────────────────────────────────┘  │
│                                                                  │
│  ┌─────────────────────┐ ┌─────────────────────┐               │
│  │ Time Slice Manager │ │ Preemption Logic    │               │
│  │ • Per-queue slices │ │ • Priority-based    │               │
│  │ • Tick accounting  │ │ • Deadline-driven   │               │
│  └─────────────────────┘ └─────────────────────┘               │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                     Context Switch                               │
│  • Save callee-saved registers (x19-x30)                        │
│  • Save/restore stack pointer (sp)                              │
│  • Switch page tables (TTBR0 for user tasks)                    │
│  • ASID management for TLB isolation                            │
└─────────────────────────────────────────────────────────────────┘
```

---

## Priority System

### 8 Priority Queues

| Queue | Priority Range | Time Slice | Description                     |
|-------|----------------|------------|---------------------------------|
| 0     | 0-31           | 20ms       | Highest priority (system tasks) |
| 1     | 32-63          | 18ms       | High priority                   |
| 2     | 64-95          | 15ms       | Above normal                    |
| 3     | 96-127         | 12ms       | Normal-high                     |
| 4     | 128-159        | 10ms       | **Default** (normal tasks)      |
| 5     | 160-191        | 8ms        | Below normal                    |
| 6     | 192-223        | 5ms        | Low priority                    |
| 7     | 224-255        | 5ms        | Lowest (idle task)              |

### Priority Constants

```cpp
constexpr u8 PRIORITY_HIGHEST = 0;     // Most urgent
constexpr u8 PRIORITY_DEFAULT = 128;   // Normal tasks
constexpr u8 PRIORITY_LOWEST = 255;    // Idle task
constexpr u8 NUM_PRIORITY_QUEUES = 8;
constexpr u8 PRIORITIES_PER_QUEUE = 32;
```

### Time Slice Calculation

```cpp
constexpr u32 TIME_SLICE_BY_QUEUE[8] = {
    20, // Queue 0 - highest priority
    18, // Queue 1
    15, // Queue 2
    12, // Queue 3
    10, // Queue 4 - default
    8,  // Queue 5
    5,  // Queue 6
    5,  // Queue 7 - idle
};

inline u32 time_slice_for_priority(u8 priority) {
    u8 queue = priority / PRIORITIES_PER_QUEUE;
    return TIME_SLICE_BY_QUEUE[queue];
}
```

---

## Scheduling Policies

The scheduler supports four scheduling policies with the following priority hierarchy:

```
SCHED_DEADLINE > SCHED_FIFO/SCHED_RR > SCHED_OTHER
```

### SCHED_DEADLINE (Highest Priority)

- **Earliest Deadline First (EDF)** ordering
- Tasks with earliest absolute deadline run first
- Bandwidth reservation (runtime/period/deadline parameters)
- Admission control ensures CPU capacity isn't overcommitted
- Time slice derived from runtime budget

### SCHED_FIFO (Real-time)

- Run until yield or block
- No time slicing
- Always preempts SCHED_OTHER tasks
- FIFO ordering within priority

### SCHED_RR (Real-time Round-Robin)

- Real-time with time slicing
- 100ms default time slice
- Preempts SCHED_OTHER tasks
- Round-robin at same priority

### SCHED_OTHER (Default)

- **CFS (Completely Fair Scheduler)** with vruntime
- Tasks with lowest vruntime selected first
- Nice values (-20 to +19) affect vruntime growth rate
- Higher nice = faster vruntime growth = less CPU time
- Priority-based queue selection for coarse-grained priority

```cpp
enum class SchedPolicy : u8 {
    SCHED_OTHER = 0,    // Normal time-sharing (CFS)
    SCHED_FIFO = 1,     // Real-time FIFO
    SCHED_RR = 2,       // Real-time round-robin
    SCHED_DEADLINE = 3  // Deadline scheduling (EDF)
};
```

---

## Task Management

### Task States

| State   | Value | Description         |
|---------|-------|---------------------|
| Invalid | 0     | Slot not in use     |
| Ready   | 1     | Runnable, in queue  |
| Running | 2     | Currently executing |
| Blocked | 3     | Waiting on event    |
| Exited  | 4     | Terminated          |

### Task Flags

| Flag             | Bit | Description               |
|------------------|-----|---------------------------|
| TASK_FLAG_KERNEL | 0   | Runs in kernel mode (EL1) |
| TASK_FLAG_IDLE   | 1   | Idle task                 |
| TASK_FLAG_USER   | 2   | Runs in user mode (EL0)   |

### Task Structure (Key Fields)

```cpp
struct Task {
    u32 id;                    // Unique task ID
    TaskState state;           // Current state
    u32 flags;                 // TASK_FLAG_*
    u8 priority;               // 0-255 priority
    SchedPolicy policy;        // Scheduling policy
    u32 time_slice;            // Remaining time slice
    char name[32];             // Human-readable name

    // CPU affinity
    u32 cpu_affinity;          // Bitmask of allowed CPUs (bit N = CPU N)

    // CFS (Completely Fair Scheduler) fields
    u64 vruntime;              // Virtual runtime (nanoseconds, scaled by weight)
    i8 nice;                   // Nice value (-20 to +19, default 0)

    // SCHED_DEADLINE fields (EDF)
    u64 dl_runtime;            // Maximum runtime per period (nanoseconds)
    u64 dl_deadline;           // Relative deadline (nanoseconds)
    u64 dl_period;             // Period length (nanoseconds)
    u64 dl_abs_deadline;       // Absolute deadline (current deadline tick)

    // CPU context
    TaskContext context;       // Saved callee-saved registers
    TrapFrame *trap_frame;     // For syscalls/interrupts

    // Memory
    u8 *kernel_stack;          // 16KB kernel stack
    u8 *kernel_stack_top;      // Stack top (initial SP)
    viper::Viper *viper;       // Owning process

    // Statistics
    u64 cpu_ticks;             // CPU time consumed
    u64 switch_count;          // Context switch count

    // Process hierarchy
    u32 parent_id;             // Parent task ID
    i32 exit_code;             // Exit status
};
```

### CPU Affinity Constants

```cpp
constexpr u32 CPU_AFFINITY_ALL = 0xFFFFFFFF;  // Can run on any CPU
```

### Stack Configuration

```cpp
constexpr usize KERNEL_STACK_SIZE = 16 * 1024; // 16KB per task
```

---

## Scheduler API

### Core Functions

```cpp
// Initialize scheduler
void init();

// Start scheduling (never returns)
[[noreturn]] void start();

// Add task to ready queue
void enqueue(task::Task *t);

// Remove and return highest-priority ready task
task::Task *dequeue();

// Select next task and context switch
void schedule();

// Timer tick accounting
void tick();

// Check and perform preemption
void preempt();
```

### Statistics

```cpp
struct Stats {
    u64 context_switches;  // Total context switches
    u32 queue_lengths[8];  // Per-queue task counts
    u32 total_ready;       // Total ready tasks
    u32 blocked_tasks;     // Blocked task count
    u32 exited_tasks;      // Zombie task count
};

void get_stats(Stats *stats);
u32 get_queue_length(u8 queue_idx);
void dump_stats();
```

### Multicore Support

```cpp
// Enqueue on specific CPU
void enqueue_on_cpu(task::Task *t, u32 cpu_id);

// Per-CPU statistics
struct PerCpuStats {
    u64 context_switches;
    u32 queue_length;
    u32 steals;
    u32 migrations;
};

void get_percpu_stats(u32 cpu_id, PerCpuStats *stats);
void balance_load();
void init_cpu(u32 cpu_id);
```

---

## Context Switch

### Saved Context

```cpp
struct Context {
    // Callee-saved registers
    u64 x19, x20, x21, x22, x23, x24, x25, x26, x27, x28;
    u64 x29;  // Frame pointer
    u64 x30;  // Link register
    u64 sp;   // Stack pointer
    u64 elr;  // Exception link register (return address)
    u64 spsr; // Saved program status register
};
```

### Context Switch Flow

1. Save current task's context (x19-x30, sp)
2. Update current task pointer
3. If switching to user task:
    - Switch TTBR0 to user page table
    - Update ASID for TLB isolation
    - Issue TLB invalidation if needed
4. Restore new task's context
5. Return to new task's execution point

### Assembly Implementation (`context.S`)

```asm
context_switch:
    // Save callee-saved registers to old context
    stp x19, x20, [x0, #0]
    stp x21, x22, [x0, #16]
    ...
    mov x9, sp
    str x9, [x0, #96]  // Save sp

    // Restore from new context
    ldp x19, x20, [x1, #0]
    ldp x21, x22, [x1, #16]
    ...
    ldr x9, [x1, #96]  // Restore sp
    mov sp, x9
    ret
```

---

## Wait Queues

### Purpose

Wait queues allow tasks to block waiting for events and be woken when events occur.

### API

```cpp
struct WaitQueue {
    Task *head;
    Task *tail;
};

// Block current task on wait queue
void wait_on(WaitQueue *wq);

// Wake first waiter
void wake_one(WaitQueue *wq);

// Wake all waiters
void wake_all(WaitQueue *wq);

// Wake waiters with specific reason
void wake_one_with_result(WaitQueue *wq, i64 result);
```

### Usage

```cpp
// Blocking operation
void channel_recv_blocking(Channel *ch, void *buf) {
    while (ch->count == 0) {
        wait_on(&ch->recv_waiters);
    }
    // Process message...
}

// Waking waiters
void channel_send(Channel *ch, const void *data) {
    // Add message to queue...
    wake_one(&ch->recv_waiters);
}
```

---

## CFS Fair Scheduling

The CFS (Completely Fair Scheduler) implementation provides fair CPU time distribution for SCHED_OTHER tasks based on
virtual runtime tracking.

### Virtual Runtime (vruntime)

Each task maintains a `vruntime` that tracks weighted CPU time:

```cpp
// Update vruntime on each tick (1ms)
u64 delta_ns = 1000000;  // 1ms in nanoseconds
current->vruntime += cfs::calc_vruntime_delta(delta_ns, current->nice);
```

Tasks with **lower vruntime** are selected first, ensuring fair CPU distribution.

### Nice Values

Nice values range from -20 (highest priority) to +19 (lowest priority):

| Nice | Weight | Effect                     |
|------|--------|----------------------------|
| -20  | 88761  | Runs ~15x more than nice 0 |
| -10  | 9548   | Runs ~9x more than nice 0  |
| 0    | 1024   | Default weight             |
| +10  | 110    | Runs ~9x less than nice 0  |
| +19  | 15     | Runs ~68x less than nice 0 |

### Weight Tables

```cpp
// Nice to weight mapping (partial)
constexpr u32 NICE_TO_WEIGHT[40] = {
    /* -20 */ 88761, 71755, 56483, 46273, 36291,
    /* -15 */ 29154, 23254, 18705, 14949, 11916,
    /* -10 */ 9548, 7620, 6100, 4904, 3906,
    /*  -5 */ 3121, 2501, 1991, 1586, 1277,
    /*   0 */ 1024, 820, 655, 526, 423,
    /*   5 */ 335, 272, 215, 172, 137,
    /*  10 */ 110, 87, 70, 56, 45,
    /*  15 */ 36, 29, 23, 18, 15,
};
```

### Task Selection

```cpp
// In dequeue: select SCHED_OTHER task with lowest vruntime
task::Task *best = nullptr;
for (task::Task *t = queue.head; t; t = t->next) {
    if (t->policy == task::SchedPolicy::SCHED_OTHER) {
        if (!best || t->vruntime < best->vruntime) {
            best = t;
        }
    }
}
```

---

## Deadline Scheduling (SCHED_DEADLINE)

SCHED_DEADLINE implements Earliest Deadline First (EDF) scheduling with bandwidth reservation.

### Deadline Parameters

```cpp
struct DeadlineParams {
    u64 runtime;   // Maximum runtime per period (nanoseconds)
    u64 deadline;  // Relative deadline (nanoseconds)
    u64 period;    // Period length (nanoseconds)
};
```

**Constraints:** `runtime <= deadline <= period`

### Bandwidth Reservation

```cpp
// Bandwidth = runtime / period (as fraction of CPU)
constexpr u64 MAX_TOTAL_BANDWIDTH = 950;  // 95% max (reserve 5% for non-DL tasks)

inline u64 calc_bandwidth(const DeadlineParams *params) {
    return (params->runtime * 1000) / params->period;  // Parts per thousand
}

inline bool can_admit(u64 new_bandwidth) {
    return (total_bandwidth + new_bandwidth) <= MAX_TOTAL_BANDWIDTH;
}
```

### EDF Task Selection

SCHED_DEADLINE tasks are selected **before** all other policies:

```cpp
// First pass: find deadline task with earliest absolute deadline
task::Task *dl_best = nullptr;
for (u8 i = 0; i < NUM_PRIORITY_QUEUES; i++) {
    for (task::Task *t = queues[i].head; t; t = t->next) {
        if (t->policy == task::SchedPolicy::SCHED_DEADLINE) {
            if (!dl_best || t->dl_abs_deadline < dl_best->dl_abs_deadline) {
                dl_best = t;
            }
        }
    }
}
if (dl_best) return dl_best;  // Deadline tasks have highest priority
```

### API

```cpp
namespace deadline {
    i32 set_deadline(task::Task *t, const DeadlineParams *params);
    void clear_deadline(task::Task *t);
    void replenish(task::Task *t, u64 current_time);
    bool earlier_deadline(const task::Task *a, const task::Task *b);
}
```

---

## CPU Affinity

Tasks can be restricted to run on specific CPUs using a bitmask.

### Affinity Mask

```cpp
u32 cpu_affinity;  // Bit N set = task can run on CPU N
constexpr u32 CPU_AFFINITY_ALL = 0xFFFFFFFF;  // Default: all CPUs
```

### Scheduler Integration

The scheduler checks affinity when selecting tasks:

```cpp
u32 cpu_mask = (1u << cpu::current_id());

// Only select tasks that can run on this CPU
if (t->cpu_affinity & cpu_mask) {
    // Task can run on current CPU
}
```

Work stealing also respects affinity:

```cpp
// Only steal tasks that can run on the stealing CPU
if (t->cpu_affinity & cpu_mask) {
    // Safe to steal
}
```

### API

```cpp
i32 task::set_affinity(Task *t, u32 mask);  // Returns 0 on success, -1 on error
u32 task::get_affinity(Task *t);            // Returns mask (CPU_AFFINITY_ALL if null)
```

### Syscalls

```cpp
// SYS_SCHED_SETAFFINITY (0x0D)
// a0 = task_id (0 = current), a1 = affinity mask
// Returns 0 on success

// SYS_SCHED_GETAFFINITY (0x0E)
// a0 = task_id (0 = current)
// Returns affinity mask
```

---

## Priority Inheritance

Priority inheritance (PI) mutexes prevent priority inversion by temporarily boosting the priority of a mutex holder when
a higher-priority task is waiting.

### PI Mutex Structure

```cpp
struct PiMutex {
    Spinlock lock;              // Protects mutex state
    task::Task *owner;          // Current owner (nullptr if unlocked)
    u8 owner_original_priority; // Owner's priority before boost
    u8 boosted_priority;        // Current boosted priority
    bool initialized;
};
```

### Priority Boosting

When a high-priority task fails to acquire a mutex:

```cpp
void contend(PiMutex *m, task::Task *waiter) {
    task::Task *owner = m->owner;
    if (waiter->priority < owner->priority) {
        // Boost owner to waiter's priority
        owner->priority = waiter->priority;
        m->boosted_priority = waiter->priority;
    }
}
```

### Priority Restoration

When the mutex is released:

```cpp
void unlock(PiMutex *m) {
    task::Task *cur = task::current();
    if (cur->priority != m->owner_original_priority) {
        cur->priority = m->owner_original_priority;  // Restore
    }
    m->owner = nullptr;
}
```

### API

```cpp
namespace pi {
    void init_mutex(PiMutex *m);
    bool try_lock(PiMutex *m);
    void contend(PiMutex *m, task::Task *waiter);  // Boost if needed
    void unlock(PiMutex *m);
    bool is_locked(PiMutex *m);
    task::Task *get_owner(PiMutex *m);
    void boost_priority(task::Task *t, u8 new_priority);
    void restore_priority(task::Task *t);
}
```

---

## Idle State Management

The idle subsystem tracks CPU idle states for power management.

### Idle Statistics

```cpp
struct IdleStats {
    u64 wfi_count;     // Number of WFI instructions executed
    u64 wakeup_count;  // Number of wakeups from idle
};
```

### Idle Task Integration

The idle task calls tracking functions around WFI:

```cpp
void idle_task_fn(void *) {
    while (true) {
        u32 cpu_id = cpu::current_id();
        idle::enter(cpu_id);   // Record entering idle
        asm volatile("wfi");   // Wait For Interrupt
        idle::exit(cpu_id);    // Record wakeup
    }
}
```

### API

```cpp
namespace idle {
    void init();
    void enter(u32 cpu_id);
    void exit(u32 cpu_id);
    void get_stats(u32 cpu_id, IdleStats *stats);
}
```

---

## Syscalls

| Syscall               | Number | Description              |
|-----------------------|--------|--------------------------|
| SYS_TASK_YIELD        | 0x00   | Yield CPU to scheduler   |
| SYS_TASK_EXIT         | 0x01   | Terminate with exit code |
| SYS_TASK_CURRENT      | 0x02   | Get current task ID      |
| SYS_TASK_SPAWN        | 0x03   | Create new task          |
| SYS_TASK_JOIN         | 0x04   | Wait for task completion |
| SYS_TASK_LIST         | 0x05   | List all tasks           |
| SYS_TASK_SET_PRIORITY | 0x06   | Set task priority        |
| SYS_TASK_GET_PRIORITY | 0x07   | Get task priority        |
| SYS_WAIT              | 0x08   | Wait for any child       |
| SYS_WAITPID           | 0x09   | Wait for specific child  |
| SYS_SCHED_SETAFFINITY | 0x0D   | Set CPU affinity mask    |
| SYS_SCHED_GETAFFINITY | 0x0E   | Get CPU affinity mask    |
| SYS_SLEEP             | 0x31   | Sleep for duration       |

---

## Implementation Files

| File            | Lines | Description                                         |
|-----------------|-------|-----------------------------------------------------|
| `task.hpp`      | ~550  | Task structures, constants, affinity/nice APIs      |
| `task.cpp`      | ~900  | Task management, affinity, nice value handling      |
| `scheduler.hpp` | ~200  | Scheduler interface                                 |
| `scheduler.cpp` | ~1100 | Priority queues, CFS vruntime, EDF, affinity checks |
| `context.S`     | ~150  | Context switch assembly                             |
| `wait.hpp`      | ~100  | Wait queue interface                                |
| `wait.cpp`      | ~200  | Wait queue implementation                           |
| `signal.hpp`    | ~150  | Signal definitions                                  |
| `signal.cpp`    | ~400  | Signal handling                                     |
| `cfs.hpp`       | ~120  | CFS weight tables and vruntime calculation          |
| `deadline.hpp`  | ~130  | Deadline parameters and EDF utilities               |
| `deadline.cpp`  | ~90   | Deadline admission control and replenishment        |
| `pi.hpp`        | ~110  | Priority inheritance mutex interface                |
| `pi.cpp`        | ~170  | PI mutex implementation                             |
| `idle.hpp`      | ~60   | Idle state tracking interface                       |
| `idle.cpp`      | ~70   | Per-CPU idle statistics                             |

---

## Performance

### Context Switch Timing

| Operation             | Typical Time |
|-----------------------|--------------|
| Register save/restore | ~200ns       |
| Page table switch     | ~300ns       |
| TLB invalidation      | ~500ns       |
| Full context switch   | ~1-2μs       |

### Scheduler Overhead

- Queue insertion: O(1) (tail insertion)
- Queue dequeue: O(1) (head removal)
- Priority search: O(k) where k = number of non-empty queues
- Typical scheduling decision: <500ns

---

## Multicore/SMP Support

### Architecture

The scheduler now includes full SMP support with per-CPU run queues and load balancing:

```
┌────────────────────────────────────────────────────────────────────┐
│                     SMP Scheduler Architecture                       │
│                                                                      │
│  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐  │
│  │      CPU 0       │  │      CPU 1       │  │      CPU 2/3     │  │
│  │  ┌────────────┐  │  │  ┌────────────┐  │  │  ┌────────────┐  │  │
│  │  │ Run Queues │  │  │  │ Run Queues │  │  │  │ Run Queues │  │  │
│  │  │  [0-7]     │  │  │  │  [0-7]     │  │  │  │  [0-7]     │  │  │
│  │  └────────────┘  │  │  └────────────┘  │  │  └────────────┘  │  │
│  │       ↑          │  │       ↑          │  │       ↑          │  │
│  │   ┌───┴───┐      │  │   ┌───┴───┐      │  │   ┌───┴───┐      │  │
│  │   │ Work  │←─────┼──┼───│ Steal │──────┼──┼───│       │      │  │
│  │   │ Steal │      │  │   │       │      │  │   │       │      │  │
│  │   └───────┘      │  │   └───────┘      │  │   └───────┘      │  │
│  └──────────────────┘  └──────────────────┘  └──────────────────┘  │
│                              │                                      │
│                    ┌─────────▼─────────┐                           │
│                    │   Load Balancer   │                           │
│                    │  (every 100 ticks)│                           │
│                    └───────────────────┘                           │
└────────────────────────────────────────────────────────────────────┘
```

### Per-CPU Scheduler State

```cpp
struct PerCpuScheduler {
    PriorityQueue queues[8];  // Private run queues
    Spinlock lock;            // Per-CPU lock (reduced contention)
    u64 context_switches;     // Per-CPU statistics
    u32 total_tasks;
    u32 steals;              // Tasks stolen from other CPUs
    u32 migrations;          // Tasks migrated away
    bool initialized;
};
```

### Work Stealing

When a CPU's run queue is empty:

1. Try each other CPU in sequence
2. Use non-blocking lock acquisition (`try_acquire()`)
3. Steal from **lowest priority queues** (4-7) first
4. Steal **oldest task** (tail of queue) for cache locality
5. Update statistics on both CPUs

```cpp
task::Task* steal_task() {
    for (u32 cpu = 0; cpu < cpu::MAX_CPUS; cpu++) {
        if (cpu == current_cpu()) continue;

        auto& sched = per_cpu_schedulers[cpu];
        if (!sched.lock.try_acquire()) continue;

        // Steal from low priority queues first
        for (int q = 7; q >= 4; q--) {
            if (Task* t = sched.queues[q].steal_tail()) {
                sched.migrations++;
                current_sched().steals++;
                sched.lock.release();
                return t;
            }
        }
        sched.lock.release();
    }
    return nullptr;
}
```

### Load Balancing

Periodic load balancing runs every 100 timer ticks (100ms at 1kHz):

```cpp
void balance_load() {
    static u64 last_balance = 0;
    if (timer_ticks - last_balance < 100) return;
    last_balance = timer_ticks;

    // Find most and least loaded CPUs
    u32 max_load = 0, min_load = UINT32_MAX;
    u32 max_cpu = 0, min_cpu = 0;

    for (u32 cpu = 0; cpu < cpu::MAX_CPUS; cpu++) {
        u32 load = per_cpu_schedulers[cpu].total_tasks;
        if (load > max_load) { max_load = load; max_cpu = cpu; }
        if (load < min_load) { min_load = load; min_cpu = cpu; }
    }

    // Migrate if imbalance > 2 tasks
    if (max_load - min_load > 2) {
        migrate_task(max_cpu, min_cpu);
    }
}
```

### CPU Affinity

Tasks can be enqueued on specific CPUs:

```cpp
void enqueue_on_cpu(task::Task *t, u32 cpu_id) {
    auto& sched = per_cpu_schedulers[cpu_id];
    sched.lock.acquire();
    u8 queue = t->priority / PRIORITIES_PER_QUEUE;
    sched.queues[queue].push(t);
    sched.total_tasks++;
    sched.lock.release();

    // Send IPI to wake target CPU if different
    if (cpu_id != current_cpu()) {
        cpu::send_ipi(cpu_id, cpu::IPI_RESCHEDULE);
    }
}
```

### Inter-Processor Interrupts (IPI)

```cpp
namespace cpu {
    enum IpiType : u32 {
        RESCHEDULE = 0,  // Trigger reschedule
        STOP = 1,        // Stop CPU (panic/shutdown)
        TLB_FLUSH = 2,   // Flush TLB
    };

    void send_ipi(u32 cpu_id, IpiType type);
    void broadcast_ipi(IpiType type);
}
```

### Secondary CPU Boot

Uses PSCI (Power State Coordination Interface):

```cpp
void boot_secondaries() {
    for (u32 cpu = 1; cpu < MAX_CPUS; cpu++) {
        psci_cpu_on(cpu, secondary_entry_point);
    }
}

void secondary_cpu_init(u32 cpu_id) {
    gic::init_cpu();              // GIC interface
    timer::init_cpu();            // Per-CPU timer
    scheduler::init_cpu(cpu_id);  // Scheduler state

    // Enter scheduling loop
    scheduler::start();
}
```

### SMP Statistics

```cpp
struct PerCpuStats {
    u64 context_switches;  // Switches on this CPU
    u32 queue_length;      // Current tasks
    u32 steals;            // Tasks stolen from others
    u32 migrations;        // Tasks taken by others
};

void get_percpu_stats(u32 cpu_id, PerCpuStats *stats);
void dump_smp_stats();
```

### What's Working

- Per-CPU run queues with private spinlocks
- Work stealing when queue empty
- Periodic load balancing (100ms intervals)
- CPU affinity via `enqueue_on_cpu()`
- IPI-based reschedule notifications
- Secondary CPU boot via PSCI
- Per-CPU timer interrupts
- Per-CPU statistics tracking

### Current Limitations

- Tasks primarily run on CPU 0 by default
- No NUMA awareness
- No CPU hotplug support
- Simple load balancing heuristic

---

## Completed Advanced Features

All five priority scheduler enhancements have been implemented and are fully operational:

### 1. CPU Affinity ✅

**Status:** Complete

- Bitmask-based CPU affinity per task (`cpu_affinity` field)
- `SYS_SCHED_SETAFFINITY` (0x0D) / `SYS_SCHED_GETAFFINITY` (0x0E) syscalls
- Scheduler respects affinity in both `dequeue_locked()` and `dequeue_percpu_locked()`
- Work stealing only steals tasks compatible with stealing CPU's mask
- **Files:** `task.hpp`, `task.cpp`, `scheduler.cpp`

### 2. Deadline Scheduler (SCHED_DEADLINE) ✅

**Status:** Complete

- EDF (Earliest Deadline First) scheduling via `dl_abs_deadline`
- Bandwidth reservation parameters: `dl_runtime`, `dl_deadline`, `dl_period`
- Admission control with 95% max bandwidth cap
- SCHED_DEADLINE tasks preempt RT and SCHED_OTHER tasks
- **Files:** `deadline.hpp`, `deadline.cpp`, `scheduler.cpp`

### 3. CFS Fair Scheduling ✅

**Status:** Complete

- Virtual runtime tracking (`vruntime` field, nanoseconds)
- Nice value support (-20 to +19) with weight tables
- `tick()` updates vruntime using `cfs::calc_vruntime_delta()`
- Dequeue selects lowest vruntime among SCHED_OTHER tasks
- **Files:** `cfs.hpp`, `scheduler.cpp`

### 4. CPU Idle State Tracking ✅

**Status:** Complete

- WFI enter/exit tracking in idle task via `idle::enter()`/`idle::exit()`
- Per-CPU idle statistics (WFI count, wakeup count)
- Integrated with idle_task_fn in task.cpp
- **Files:** `idle.hpp`, `idle.cpp`, `task.cpp`

### 5. Priority Inheritance Mutexes ✅

**Status:** Complete

- `PiMutex` structure with owner tracking and priority storage
- `pi::contend()` boosts owner when high-priority task waits
- `pi::unlock()` restores original priority
- Prevents priority inversion in critical sections
- **Files:** `pi.hpp`, `pi.cpp`
