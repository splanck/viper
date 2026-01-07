# Tasks and Scheduling

ViperOS uses a priority-based preemptive scheduler designed for both single-core and SMP operation:

- A global fixed-size task table (256 tasks max)
- 8 priority queues (priorities 0-255 mapped to queues)
- Per-CPU ready queues for SMP scalability
- A periodic timer interrupt that drives time slicing and load balancing

## The moving parts

There are two main modules:

- `kernel/sched/task.*`: task objects, creation, exit/yield, current-task tracking
- `kernel/sched/scheduler.*`: ready queue management and context switching

The actual register save/restore is done in assembly (`kernel/sched/context.S`) and wrapped by `context_switch(...)`.

## Task lifecycle narrative

### Boot: task table and idle task

At boot, `task::init()`:

- clears the global task table
- creates **task 0**, the idle task
- allocates an initial kernel stack for the idle task from a fixed stack pool
- sets up the idle task so that the first time it runs, it enters a trampoline that will call `idle_task_fn()`

The idle task is always runnable; it loops in `wfi` so the CPU sleeps when there's nothing else to do.

### Creating a kernel task

`task::create(name, entry, arg, flags)`:

- finds a free slot in the global task array
- allocates a kernel stack from the stack pool
- prepares an initial saved context so that the first dispatch jumps to `task_entry_trampoline`
- the trampoline pulls `(entry, arg)` from the stack and calls the real function

This design keeps task creation cheap and keeps the "first run" path explicit in code.

### Creating a user task

The task subsystem also has a "user task" notion used by the Viper process system:

- a user task associates a task with a `viper::Viper*` and an entry point in that Viper's address space
- on first run, it will switch to user mode via the exception/eret path (`enter_user_mode(...)`)

The narrative here is: "tasks are the scheduler's unit of execution; a Viper is the process container that owns the
address space and capabilities".

Key files:

- `kernel/sched/task.cpp`
- `kernel/arch/aarch64/exceptions.*` (user-mode entry helpers)
- `kernel/viper/*`

## Scheduling narrative: Priority queues and time slices

The scheduler in `kernel/sched/scheduler.cpp` maintains **8 priority queues**:

| Queue | Priority Range | Time Slice | Use Case |
|-------|----------------|------------|----------|
| 0 | 0-31 (highest) | 5ms | Real-time, interrupt handlers |
| 1 | 32-63 | 7ms | High-priority user tasks |
| 2 | 64-95 | 10ms | Interactive tasks |
| 3 | 96-127 | 12ms | Normal tasks |
| 4 | 128-159 | 15ms | Background tasks |
| 5 | 160-191 | 17ms | Low-priority batch |
| 6 | 192-223 | 20ms | Idle-priority |
| 7 | 224-255 (lowest) | 20ms | Best-effort |

Tasks are mapped to queues via `priority / 32`.

### Queue operations

- `scheduler::enqueue(task*)` adds to the tail of the appropriate priority queue
- `scheduler::dequeue()` removes from the head of the highest-priority non-empty queue

When a reschedule happens, `scheduler::schedule()`:

1. Picks the next runnable task from the highest-priority non-empty queue, or falls back to the idle task.
2. If the current task is still running, it is moved back into its priority queue.
3. The next task becomes the current task and gets a fresh time slice based on its queue.
4. `context_switch(old, next)` swaps register state and jumps into the new task.

### Timer-driven preemption

The periodic timer interrupt calls:

- `scheduler::tick()` to decrement `current->time_slice`
- `scheduler::preempt()` to trigger a schedule when the slice reaches zero

Higher-priority tasks get shorter time slices for better responsiveness; lower-priority tasks get longer slices for
better throughput.

Key files:

- `kernel/sched/scheduler.cpp`
- `kernel/arch/aarch64/timer.cpp`

## SMP support

The scheduler supports symmetric multiprocessing with work stealing:

- **Per-CPU ready queues**: Each CPU maintains its own set of 8 priority queues to reduce lock contention
- **Per-CPU current task**: `CpuData` structure holds the current task pointer for each CPU
- **Work stealing**: When a CPU's queues are empty, it steals tasks from other CPUs' queues
- **Periodic load balancing**: Every 100ms, the timer interrupt triggers load balancing across CPUs
- **CPU affinity**: Tasks can be pinned to specific CPUs via `enqueue_on_cpu()`
- **IPIs**: Inter-Processor Interrupts for reschedule, stop, and TLB flush operations

### Work Stealing Algorithm

When `scheduler::dequeue()` finds all local queues empty:

1. Iterate through other CPUs' scheduler states
2. Find a victim CPU with tasks in its queues
3. Steal one task from the victim's lowest-priority non-empty queue
4. Return the stolen task for execution

### Load Balancing

Periodic load balancing runs every 100 ticks (~100ms):

1. Calculate each CPU's task count
2. Find overloaded and underloaded CPUs
3. Migrate tasks from overloaded to underloaded CPUs
4. Send IPI_RESCHEDULE to affected CPUs

Key structures:

- `kernel/arch/aarch64/cpu.hpp`: `CpuData` with per-CPU scheduler state
- `scheduler::PerCpuScheduler`: Per-CPU ready queues and run counts
- `scheduler::steal_from()`: Work stealing implementation

## Blocking and wakeups

The scheduler provides blocking primitives for synchronization:

- **Wait queues**: Tasks waiting on events (IPC, timers, IRQs) are placed in wait queues
- **Channel blocking**: Senders/receivers block when channels are full/empty
- **Poll/sleep**: The poll system blocks tasks until events occur or timeouts expire
- **IRQ wait**: User-space drivers can block waiting for device interrupts

When an event occurs, `wait_wake_one()` or `wait_wake_all()` moves tasks back to ready queues.

Key files:

- `kernel/ipc/channel.cpp`
- `kernel/ipc/poll.cpp`
- `kernel/sched/wait.hpp`

## Current limitations

- The kernel stack pool is a fixed region with no guard pages and limited reuse
- No real-time scheduling class (soft priorities only)
- Work stealing only takes one task at a time (could be more aggressive)

