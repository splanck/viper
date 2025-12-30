# Scheduler and Task Management

**Status:** Complete cooperative/preemptive scheduler with wait queues
**Location:** `kernel/sched/`
**SLOC:** ~2,400

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
| priority | u32 | Priority (lower = higher) |
| next/prev | Task* | Queue linkage |
| viper | ViperProcess* | User process |
| user_entry | u64 | User entry point |
| user_stack | u64 | User stack pointer |
| cpu_ticks | u64 | Total CPU ticks consumed |
| switch_count | u64 | Number of times scheduled |
| parent_id | u32 | Parent task ID |
| exit_code | i32 | Exit status (for zombies) |
| cwd[256] | char | Current working directory |

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

---

### 2. Scheduler (`scheduler.cpp`, `scheduler.hpp`)

**Status:** Complete FIFO scheduler with preemption

**Implemented:**
- FIFO ready queue (doubly-linked list)
- Time-slice counter (10 ticks = 10ms at 1000Hz)
- Preemption check on timer tick
- Context switch via assembly routine
- Idle task fallback when queue empty
- Context switch counter (statistics)
- Per-task CPU tick tracking
- Per-task context switch counting
- Interrupt masking during critical sections

**Ready Queue:**
```
┌────────┐   ┌────────┐   ┌────────┐
│ Task A │◄─►│ Task B │◄─►│ Task C │
└────────┘   └────────┘   └────────┘
     ▲                          ▲
   head                       tail
```

**Scheduling Algorithm:**
```
schedule():
  1. Dequeue next task from ready queue
  2. If queue empty, select idle task
  3. If same as current, return
  4. Re-enqueue current task if still runnable
  5. Set next task as Running
  6. Reset next task's time slice
  7. Perform context switch
```

**Time Slice:**
- Default: 10 ticks (10ms)
- Decremented on each timer tick
- Schedule triggered when reaches 0

**API:**
| Function | Description |
|----------|-------------|
| `init()` | Initialize scheduler |
| `enqueue(t)` | Add task to ready queue |
| `dequeue()` | Remove next task |
| `schedule()` | Select and switch |
| `tick()` | Timer tick handler |
| `preempt()` | Preemption check |
| `start()` | Start scheduling |
| `get_context_switches()` | Statistics |

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

1. **High:** Add priority-based scheduling
2. **High:** Implement multiprocessor support (SMP)
3. **Medium:** Implement proper TIME_WAIT cleanup
4. **Low:** Add real-time scheduling class
5. **Low:** Per-CPU run queues for scalability
