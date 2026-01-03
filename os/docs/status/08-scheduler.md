# Scheduler and Task Management

**Status:** Complete priority-based preemptive scheduler with SMP support
**Location:** `kernel/sched/`
**SLOC:** ~3,600

## Overview

The scheduler subsystem provides priority-based preemptive scheduling for both kernel and user-mode tasks. It implements 8 priority queues with FIFO ordering within each priority level, time-slice based preemption, and support for real-time scheduling policies.

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

| Queue | Priority Range | Time Slice | Description |
|-------|----------------|------------|-------------|
| 0 | 0-31 | 20ms | Highest priority (system tasks) |
| 1 | 32-63 | 18ms | High priority |
| 2 | 64-95 | 15ms | Above normal |
| 3 | 96-127 | 12ms | Normal-high |
| 4 | 128-159 | 10ms | **Default** (normal tasks) |
| 5 | 160-191 | 8ms | Below normal |
| 6 | 192-223 | 5ms | Low priority |
| 7 | 224-255 | 5ms | Lowest (idle task) |

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

### SCHED_OTHER (Default)

- Normal time-sharing scheduling
- Priority-based queue selection
- Time-slice preemption
- Round-robin within priority level

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

```cpp
enum class SchedPolicy : u8 {
    SCHED_OTHER = 0, // Normal time-sharing
    SCHED_FIFO = 1,  // Real-time FIFO
    SCHED_RR = 2     // Real-time round-robin
};
```

---

## Task Management

### Task States

| State | Value | Description |
|-------|-------|-------------|
| Invalid | 0 | Slot not in use |
| Ready | 1 | Runnable, in queue |
| Running | 2 | Currently executing |
| Blocked | 3 | Waiting on event |
| Exited | 4 | Terminated |

### Task Flags

| Flag | Bit | Description |
|------|-----|-------------|
| TASK_FLAG_KERNEL | 0 | Runs in kernel mode (EL1) |
| TASK_FLAG_IDLE | 1 | Idle task |
| TASK_FLAG_USER | 2 | Runs in user mode (EL0) |

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

    // CPU context
    u64 sp;                    // Stack pointer
    u64 pc;                    // Program counter
    Context context;           // Saved registers

    // Memory
    void *kernel_stack;        // 16KB kernel stack
    viper::Viper *viper;       // Owning process

    // Statistics
    u64 cpu_ticks;             // CPU time consumed
    u64 switch_count;          // Context switch count

    // Process hierarchy
    u32 parent_id;             // Parent task ID
    i32 exit_code;             // Exit status
};
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

## Syscalls

| Syscall | Number | Description |
|---------|--------|-------------|
| SYS_TASK_YIELD | 0x00 | Yield CPU to scheduler |
| SYS_TASK_EXIT | 0x01 | Terminate with exit code |
| SYS_TASK_CURRENT | 0x02 | Get current task ID |
| SYS_TASK_SPAWN | 0x03 | Create new task |
| SYS_TASK_JOIN | 0x04 | Wait for task completion |
| SYS_TASK_LIST | 0x05 | List all tasks |
| SYS_TASK_SET_PRIORITY | 0x06 | Set task priority |
| SYS_TASK_GET_PRIORITY | 0x07 | Get task priority |
| SYS_WAIT | 0x08 | Wait for any child |
| SYS_WAITPID | 0x09 | Wait for specific child |
| SYS_SLEEP | 0x31 | Sleep for duration |

---

## Implementation Files

| File | Lines | Description |
|------|-------|-------------|
| `task.hpp` | ~350 | Task structures and constants |
| `task.cpp` | ~800 | Task management |
| `scheduler.hpp` | ~200 | Scheduler interface |
| `scheduler.cpp` | ~500 | Priority queue scheduling |
| `context.S` | ~150 | Context switch assembly |
| `wait.hpp` | ~100 | Wait queue interface |
| `wait.cpp` | ~200 | Wait queue implementation |
| `signal.hpp` | ~150 | Signal definitions |
| `signal.cpp` | ~400 | Signal handling |

---

## Performance

### Context Switch Timing

| Operation | Typical Time |
|-----------|-------------|
| Register save/restore | ~200ns |
| Page table switch | ~300ns |
| TLB invalidation | ~500ns |
| Full context switch | ~1-2μs |

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

## Priority Recommendations: Next 5 Steps

### 1. CPU Affinity Syscalls (sched_setaffinity)
**Impact:** User-space control over task placement
- Bitmask-based CPU affinity per task
- sched_setaffinity()/sched_getaffinity() syscalls
- Honor affinity in scheduler and work stealing
- Required for performance-critical applications

### 2. Deadline Scheduler (SCHED_DEADLINE)
**Impact:** Real-time scheduling for latency-sensitive tasks
- EDF (Earliest Deadline First) scheduling
- Bandwidth reservation (runtime/period/deadline)
- Admission control for CPU capacity
- Better real-time guarantees than SCHED_FIFO

### 3. CFS-style Fair Scheduling
**Impact:** Better fairness for interactive workloads
- Virtual runtime tracking per task
- Red-black tree for O(log n) scheduling
- Nice value support (-20 to +19)
- Better responsiveness under load

### 4. CPU Idle States
**Impact:** Power efficiency
- WFI (Wait For Interrupt) when queue empty
- Deeper sleep states via PSCI
- Idle governor for state selection
- Reduced power consumption

### 5. Priority Inheritance for Mutexes
**Impact:** Avoid priority inversion
- Track mutex ownership in kernel
- Boost holder priority when high-priority waiter
- Restore original priority on release
- Required for correct real-time behavior
