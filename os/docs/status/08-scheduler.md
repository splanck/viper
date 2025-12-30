# Scheduler and Task Management

**Status:** Complete cooperative/preemptive scheduler with wait queues, priorities, and signals
**Location:** `kernel/sched/`
**SLOC:** ~2,900

## Overview

The scheduler subsystem provides task management and context switching for both kernel and user-mode tasks. It implements a simple FIFO ready queue with time-slice based preemption.

---

## Components

### 1. Task Management (`task.cpp`, `task.hpp`)

**Status:** Complete kernel and user task support

**Implemented:**
- Fixed-size task table (64 tasks)
- Task lifecycle states (Ready, Running, Blocked, Exited)
- Kernel-mode task creation
- User-mode task creation (EL0)
- Per-task kernel stack (16KB)
- Stack guard pages (4KB unmapped region)
- Saved context for context switch
- Trap frame for syscall/interrupt handling
- Idle task (task ID 0, priority 255)
- Task enumeration for syscall

**Task States:**
| State | Value | Description |
|-------|-------|-------------|
| Invalid | 0 | Slot not in use |
| Ready | 1 | Runnable, in queue |
| Running | 2 | Currently executing |
| Blocked | 3 | Waiting on event |
| Zombie | 4 | Exited, waiting for parent wait() |
| Exited | 5 | Terminated and reaped |

**Task Flags:**
| Flag | Value | Description |
|------|-------|-------------|
| TASK_FLAG_KERNEL | 1 | Kernel privilege |
| TASK_FLAG_IDLE | 2 | Idle task |
| TASK_FLAG_USER | 4 | User mode (EL0) |

**Task Structure:**
| Field | Type | Description |
|-------|------|-------------|
| id | u32 | Unique task ID |
| name[32] | char | Human-readable name |
| state | TaskState | Current lifecycle state |
| flags | u32 | Task type flags |
| context | TaskContext | Saved CPU context |
| trap_frame | TrapFrame* | Exception frame |
| kernel_stack | u8* | Stack base |
| kernel_stack_top | u8* | Stack top (initial SP) |
| time_slice | u32 | Remaining ticks |
| priority | u8 | Priority (0=highest, 255=lowest) |
| policy | SchedPolicy | SCHED_OTHER, SCHED_FIFO, or SCHED_RR |
| next/prev | Task* | Queue linkage |
| viper | ViperProcess* | User process |
| user_entry | u64 | User entry point |
| user_stack | u64 | User stack pointer |
| cpu_ticks | u64 | Total CPU ticks consumed |
| switch_count | u64 | Number of times scheduled |
| parent_id | u32 | Parent task ID |
| exit_code | i32 | Exit status (for zombies) |
| cwd[256] | char | Current working directory |
| signals.handlers[32] | u64 | Signal handler addresses |
| signals.handler_flags[32] | u32 | Handler flags (SA_*) |
| signals.handler_mask[32] | u32 | Handler signal masks |
| signals.blocked | u32 | Blocked signal mask |
| signals.pending | u32 | Pending signals bitmap |
| signals.saved_frame | TrapFrame* | Saved frame for sigreturn |

**TaskContext (Saved Registers):**
```
┌─────────────────────────────────────┐
│  x19, x20, x21, x22, x23, x24       │  Callee-saved
│  x25, x26, x27, x28, x29 (FP)       │  registers
│  x30 (LR), SP                       │
└─────────────────────────────────────┘
```

**TrapFrame (Full CPU State):**
```
┌─────────────────────────────────────┐
│  x0 - x30 (31 registers)            │
│  SP_EL0 (user stack)                │
│  ELR_EL1 (return address)           │
│  SPSR_EL1 (saved status)            │
└─────────────────────────────────────┘
```

**API:**
| Function | Description |
|----------|-------------|
| `init()` | Initialize task subsystem |
| `create(name, entry, arg, flags)` | Create kernel task |
| `create_user_task(name, viper, entry, stack)` | Create user task |
| `current()` | Get current task |
| `set_current(t)` | Set current task |
| `exit(code)` | Terminate task (becomes zombie) |
| `wait(status)` | Wait for child task exit |
| `yield()` | Yield CPU |
| `get_by_id(id)` | Lookup by ID |
| `list_tasks(buf, max)` | Enumerate tasks with stats |
| `set_priority(t, priority)` | Set task priority (0-255) |
| `get_priority(t)` | Get task priority |
| `set_policy(t, policy)` | Set scheduling policy |
| `get_policy(t)` | Get scheduling policy |

---

### 2. Scheduler (`scheduler.cpp`, `scheduler.hpp`)

**Status:** Complete priority-based scheduler with RT support and per-CPU queues

**Implemented:**
- 8 priority queues (0=highest, 7=lowest)
- Priority-based preemption (higher priority always preempts)
- Time-slice counter with priority-based duration
- Real-time scheduling classes (SCHED_FIFO, SCHED_RR)
- Per-CPU run queues for SMP scalability
- Work-stealing load balancer
- Context switch via assembly routine
- Idle task fallback when queue empty
- Context switch counter (statistics)
- Per-task CPU tick tracking
- Per-task context switch counting
- Interrupt masking during critical sections

**Priority Queues:**
```
Queue 0 (priority 0-31):   [RT/High priority tasks]
Queue 1 (priority 32-63):  [High priority tasks]
Queue 2 (priority 64-95):  [Above normal tasks]
Queue 3 (priority 96-127): [Normal tasks]
Queue 4 (priority 128-159): [Default priority - most tasks]
Queue 5 (priority 160-191): [Below normal tasks]
Queue 6 (priority 192-223): [Low priority tasks]
Queue 7 (priority 224-255): [Idle task]
```

**Scheduling Policies:**
| Policy | Description |
|--------|-------------|
| SCHED_OTHER | Normal time-sharing (default) |
| SCHED_FIFO | Real-time FIFO - runs until yield/block |
| SCHED_RR | Real-time round-robin - time sliced |

RT tasks (SCHED_FIFO/SCHED_RR) always preempt SCHED_OTHER tasks.

**Per-CPU Queues:**
```
CPU 0                  CPU 1                  CPU 2                  CPU 3
┌──────────────┐       ┌──────────────┐       ┌──────────────┐       ┌──────────────┐
│ Priority Q0  │       │ Priority Q0  │       │ Priority Q0  │       │ Priority Q0  │
│ Priority Q1  │       │ Priority Q1  │       │ Priority Q1  │       │ Priority Q1  │
│ ...          │       │ ...          │       │ ...          │       │ ...          │
│ Priority Q7  │       │ Priority Q7  │       │ Priority Q7  │       │ Priority Q7  │
└──────────────┘       └──────────────┘       └──────────────┘       └──────────────┘
        ↑                      ↑                      ↑                      ↑
        └──────────────────────┴──────────────────────┴──────────────────────┘
                              Load Balancer (work stealing)
```

**Scheduling Algorithm:**
```
schedule():
  1. Check per-CPU queue (current CPU)
  2. Dequeue highest priority task
  3. If empty, try work stealing from other CPUs
  4. If still empty, select idle task
  5. If same as current, return
  6. Re-enqueue current task if still runnable
  7. Set next task as Running
  8. Reset time slice based on policy:
     - SCHED_FIFO: unlimited (runs until yield)
     - SCHED_RR: fixed RT time slice (100ms)
     - SCHED_OTHER: priority-based slice
  9. Perform context switch
```

**Time Slice by Priority:**
| Queue | Priority Range | Time Slice |
|-------|----------------|------------|
| 0 | 0-31 | 5ms |
| 1 | 32-63 | 10ms |
| 2 | 64-95 | 15ms |
| 3 | 96-127 | 20ms |
| 4 | 128-159 | 25ms |
| 5 | 160-191 | 30ms |
| 6 | 192-223 | 35ms |
| 7 | 224-255 | 40ms |

**API:**
| Function | Description |
|----------|-------------|
| `init()` | Initialize scheduler |
| `init_cpu(cpu_id)` | Initialize per-CPU state |
| `enqueue(t)` | Add task to ready queue |
| `enqueue_on_cpu(t, cpu_id)` | Add task to specific CPU queue |
| `dequeue()` | Remove next task |
| `schedule()` | Select and switch |
| `tick()` | Timer tick handler |
| `preempt()` | Preemption check |
| `start()` | Start scheduling |
| `balance_load()` | Run load balancer |
| `get_context_switches()` | Statistics |
| `get_percpu_stats(cpu_id, stats)` | Per-CPU statistics |

---

### 3. Context Switch (`context.S`)

**Status:** Complete AArch64 context switch

**Implemented:**
- Save/restore callee-saved registers (x19-x30)
- Save/restore stack pointer
- Task entry trampoline for new tasks
- IRQ unmasking on task entry

**context_switch(old_ctx, new_ctx):**
```asm
// Save old context
stp x19, x20, [x0, #0x00]
stp x21, x22, [x0, #0x10]
...
str sp, [x0, #0x60]

// Restore new context
ldp x19, x20, [x1, #0x00]
ldp x21, x22, [x1, #0x10]
...
mov sp, x2

ret  // Returns to x30 from new context
```

**task_entry_trampoline:**
```asm
// Enable interrupts
msr daifclr, #2

// Load entry and arg from stack
ldr x9, [sp, #0]   // entry function
ldr x0, [sp, #8]   // arg

// Call entry
blr x9

// If returns, exit
mov x0, #0
bl task::exit
```

---

### 4. Wait Queues (`wait.cpp`, `wait.hpp`)

**Status:** Complete blocking/waking mechanism

**Implemented:**
- FIFO wait queue for blocked tasks
- Task enqueue with automatic state change to Blocked
- Task dequeue (abort without waking)
- Wake one (FIFO order)
- Wake all
- Queue count and empty check
- Per-task wait channel tracking for debugging

**WaitQueue Structure:**
| Field | Type | Description |
|-------|------|-------------|
| head | Task* | First waiter (woken first) |
| tail | Task* | Last waiter |
| count | u32 | Number of waiters |

**API:**
| Function | Description |
|----------|-------------|
| `wait_init(wq)` | Initialize wait queue |
| `wait_enqueue(wq, t)` | Add task to queue, set Blocked |
| `wait_dequeue(wq, t)` | Remove task without waking |
| `wait_wake_one(wq)` | Wake first waiter |
| `wait_wake_all(wq)` | Wake all waiters |
| `wait_empty(wq)` | Check if queue empty |
| `wait_count(wq)` | Get waiter count |

**Wait Queue Usage Pattern:**
```cpp
WaitQueue wq;
wait_init(&wq);

// To block:
wait_enqueue(&wq, task::current());
if (condition_not_met) {
    scheduler::schedule();  // Task is now blocked
} else {
    wait_dequeue(&wq, task::current());  // Abort, don't sleep
}

// To wake (from another context):
wait_wake_one(&wq);   // Wake first waiter
wait_wake_all(&wq);   // Wake all waiters
```

**Users:**
- VirtIO-net RX (packet arrival wakes receiver)
- Sleep syscall (timer expiry wakes sleeper)
- IPC channel blocking receive

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     Timer Interrupt                          │
│              (1000 Hz from ARM architected timer)            │
└──────────────────────────────┬──────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────┐
│                    scheduler::tick()                         │
│        Decrement time slice, check preemption                │
└──────────────────────────────┬──────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────┐
│                   scheduler::schedule()                      │
│  ┌───────────────────────────────────────────────────────┐  │
│  │  1. Dequeue next from ready queue                     │  │
│  │  2. Re-enqueue current if runnable                    │  │
│  │  3. Call context_switch(old, new)                     │  │
│  └───────────────────────────────────────────────────────┘  │
└──────────────────────────────┬──────────────────────────────┘
                               │
        ┌──────────────────────┴──────────────────────┐
        ▼                                              ▼
┌─────────────────┐                          ┌─────────────────┐
│   Kernel Task   │                          │    User Task    │
│  (EL1, kernel   │                          │  (EL0, user     │
│   stack only)   │                          │   + kernel SP)  │
└─────────────────┘                          └─────────────────┘
```

---

## Memory Layout

**Stack Pool:**
```
0x44000000  ┌─────────────────────────────────────────┐
            │  Task 0: Guard Page (4KB, unmapped)     │
            ├─────────────────────────────────────────┤
            │  Task 0: Kernel Stack (16KB)            │
            ├─────────────────────────────────────────┤
            │  Task 1: Guard Page (4KB, unmapped)     │
            ├─────────────────────────────────────────┤
            │  Task 1: Kernel Stack (16KB)            │
            ├─────────────────────────────────────────┤
            │  ...                                    │
            └─────────────────────────────────────────┘
```

**Stack Slot Size:** 20KB (4KB guard + 16KB usable)
**Maximum Stacks:** 64 (MAX_TASKS)
**Total Pool:** 1.25MB

---

## User Task Lifecycle

1. **Creation:** `create_user_task(name, viper, entry, stack)`
   - Allocate kernel stack (for syscalls)
   - Set TASK_FLAG_USER
   - Store viper, user_entry, user_stack

2. **First Schedule:**
   - `task_entry_trampoline` calls `user_task_entry_trampoline`
   - Switch to user address space (TTBR0)
   - Call `enter_user_mode(entry, stack, 0)`
   - ERET to EL0

3. **Syscall:**
   - SVC exception traps to EL1
   - Kernel uses task's kernel_stack
   - Handle syscall, then ERET back to EL0

4. **Exit:**
   - Task calls exit syscall
   - Mark task Exited
   - Schedule next task

---

## Testing

The scheduler is tested via:
- `qemu_scheduler_start` - Verifies scheduler starts and runs tasks
- All tests implicitly exercise scheduling via timer preemption

---

## Files

| File | Lines | Description |
|------|-------|-------------|
| `task.cpp` | ~882 | Task management |
| `task.hpp` | ~454 | Task structures |
| `scheduler.cpp` | ~551 | FIFO scheduler |
| `scheduler.hpp` | ~140 | Scheduler interface |
| `wait.cpp` | ~69 | Wait queue implementation |
| `wait.hpp` | ~197 | Wait queue interface |
| `context.S` | ~114 | Context switch asm |

---

## Priority Recommendations

1. ~~**High:** Add priority-based scheduling~~ ✅ **Completed** - 8 priority queues with priority-based preemption
2. ~~**High:** Implement multiprocessor support (SMP)~~ ✅ **Completed** - 4 CPUs boot via PSCI
3. ~~**Medium:** Implement proper TIME_WAIT cleanup~~ ✅ **Completed** - 2MSL timer via timer wheel
4. ~~**Low:** Add real-time scheduling class~~ ✅ **Completed** - SCHED_FIFO and SCHED_RR policies
5. ~~**Low:** Per-CPU run queues for scalability~~ ✅ **Completed** - Per-CPU queues with load balancing

## Recent Additions

- **Per-task signal state**: handlers, masks, pending signals for POSIX signal support
- **SMP boot**: Secondary CPUs boot via PSCI and initialize GIC/timer
- **Timer wheel**: O(1) timeout management integrated with poll subsystem
