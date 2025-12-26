# Tasks and Scheduling

ViperOS currently uses a small, bring-up-focused scheduler:

- a global fixed-size task table
- a FIFO ready queue
- a periodic timer interrupt that drives time slicing

The goal is “good enough to run multiple things predictably” rather than sophisticated fairness or SMP scalability.

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

The idle task is always runnable; it loops in `wfi` so the CPU sleeps when there’s nothing else to do.

### Creating a kernel task

`task::create(name, entry, arg, flags)`:

- finds a free slot in the global task array
- allocates a kernel stack from the stack pool
- prepares an initial saved context so that the first dispatch jumps to `task_entry_trampoline`
- the trampoline pulls `(entry, arg)` from the stack and calls the real function

This design keeps task creation cheap and keeps the “first run” path explicit in code.

### Creating a user task

The task subsystem also has a “user task” notion used by the Viper process system:

- a user task associates a task with a `viper::Viper*` and an entry point in that Viper’s address space
- on first run, it will switch to user mode via the exception/eret path (`enter_user_mode(...)`)

The narrative here is: “tasks are the scheduler’s unit of execution; a Viper is the process container that owns the
address space and capabilities”.

Key files:

- `kernel/sched/task.cpp`
- `kernel/arch/aarch64/exceptions.*` (user-mode entry helpers)
- `kernel/viper/*`

## Scheduling narrative: FIFO ready queue and time slices

The scheduler in `kernel/sched/scheduler.cpp` maintains a FIFO ready queue:

- `scheduler::enqueue(task*)` adds to the tail
- `scheduler::dequeue()` removes from the head

When a reschedule happens, `scheduler::schedule()`:

1. Picks the next runnable task from the queue, or falls back to the idle task.
2. If the current task is still running, it is moved back into the ready queue.
3. The next task becomes the current task and gets a fresh time slice counter.
4. `context_switch(old, next)` swaps register state and jumps into the new task.

### Timer-driven preemption

The periodic timer interrupt calls:

- `scheduler::tick()` to decrement `current->time_slice`
- `scheduler::preempt()` to trigger a schedule when the slice reaches zero

This is a “simple timeslice” model: no priorities, no complex block queues, just a basic preemption trigger.

Key files:

- `kernel/sched/scheduler.cpp`
- `kernel/arch/aarch64/timer.cpp`

## Blocking and wakeups (current model)

Bring-up blocking primitives exist, but they are intentionally simple:

- Channels can mark a sender/receiver task as blocked and wake it when space/data becomes available.
- The poll/sleep system blocks by marking a task blocked and yielding in a loop, waking it when a timer expires.

Over time, these pieces would typically evolve into explicit wait queues and a more structured “blocked state”
lifecycle.

Key files:

- `kernel/ipc/channel.cpp`
- `kernel/ipc/poll.cpp`

## Current limitations and next steps

- There is no real priority scheduling despite `Task::priority` existing.
- The kernel stack pool is a fixed region with no guard pages and no reuse (tasks never free stacks).
- Blocking is cooperative in style and is not yet a general kernel wait queue system.

