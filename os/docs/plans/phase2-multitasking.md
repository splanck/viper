# ViperOS Phase 2: Multitasking

## Detailed Implementation Plan (C++)

**Duration:** 12 weeks (Months 4-6)  
**Goal:** Multiple kernel tasks with IPC  
**Milestone:** Two tasks ping-pong messages over a channel  
**Prerequisites:** Phase 1 complete (boot, console, PMM, VMM, heap, GIC, timer)

---

## Executive Summary

Phase 2 transforms ViperOS from a single-threaded boot demo into a preemptive multitasking kernel. We implement:

1. **Tasks** — Kernel threads with saved context and scheduling state
2. **Scheduler** — Round-robin with timer preemption
3. **Context Switching** — ARM64 register save/restore
4. **Syscall Infrastructure** — SVC dispatch, ABI enforcement
5. **Channels** — Non-blocking IPC with message queues
6. **Poll Sets** — Event multiplexing with PollWait (the only blocking point)
7. **Timers** — Deadline-based timers as pollable objects

By the end, two kernel tasks can exchange messages, demonstrating that the foundation for user-space Vipers (Phase 3) is
solid.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                        Kernel                                │
├─────────────────────────────────────────────────────────────┤
│  Task A          Task B          Task C         idle_task   │
│  ┌─────┐        ┌─────┐        ┌─────┐        ┌─────┐      │
│  │ctx  │        │ctx  │        │ctx  │        │ctx  │      │
│  │stack│        │stack│        │stack│        │stack│      │
│  └─────┘        └─────┘        └─────┘        └─────┘      │
│     │              │              │              │          │
│     └──────────────┴──────────────┴──────────────┘          │
│                         │                                    │
│                    ┌────┴────┐                               │
│                    │Scheduler│                               │
│                    └────┬────┘                               │
│                         │                                    │
│              ┌──────────┴──────────┐                        │
│              │                     │                        │
│         Timer IRQ            Syscall (SVC)                  │
│              │                     │                        │
│         preempt()            dispatch()                     │
└─────────────────────────────────────────────────────────────┘
```

---

## Project Structure Additions

```
kernel/
├── sched/
│   ├── task.cpp/.hpp           # VTask structure and management
│   ├── scheduler.cpp/.hpp      # Round-robin scheduler
│   └── context.S               # Context switch assembly
├── ipc/
│   ├── channel.cpp/.hpp        # Channel implementation
│   └── message.hpp             # Message buffer types
├── sync/
│   ├── poll.cpp/.hpp           # Poll sets
│   ├── timer.cpp/.hpp          # Kernel timers (pollable)
│   └── wait_queue.cpp/.hpp     # Blocked task queues
├── syscall/
│   ├── dispatch.cpp/.hpp       # Syscall dispatcher
│   ├── syscall.S               # SVC entry/exit
│   ├── task_syscalls.cpp       # Task-related syscalls
│   ├── channel_syscalls.cpp    # Channel syscalls
│   └── poll_syscalls.cpp       # Poll syscalls
└── include/
    ├── syscall_nums.hpp        # Syscall number definitions
    └── error.hpp               # VError codes
```

---

## Implementation Milestones

| # | Milestone              | Duration   | Deliverable                             |
|---|------------------------|------------|-----------------------------------------|
| 1 | Task Infrastructure    | Week 1-2   | VTask, kernel stacks, task states       |
| 2 | Context Switching      | Week 3     | Save/restore, switch assembly           |
| 3 | Scheduler              | Week 4     | Ready queue, round-robin, preemption    |
| 4 | Syscall Infrastructure | Week 5-6   | SVC entry, dispatch, basic syscalls     |
| 5 | Channels               | Week 7-8   | Non-blocking send/recv, message buffers |
| 6 | Poll & Timers          | Week 9-10  | PollWait, timer objects                 |
| 7 | Integration            | Week 11-12 | Ping-pong test, stability               |

---

## Milestone 1: Task Infrastructure

**Duration:** Weeks 1-2  
**Deliverable:** VTask structure, kernel stack allocation, task lifecycle

### 1.1 Task States

```cpp
// kernel/sched/task.hpp
#pragma once

#include "../lib/types.hpp"
#include "../include/error.hpp"

namespace viper::sched {

enum class TaskState : u32 {
    Invalid = 0,
    Ready,          // In ready queue, can be scheduled
    Running,        // Currently executing
    Blocked,        // Waiting for event (in wait queue)
    Exited,         // Terminated, awaiting join
};

// Forward declarations
struct Task;
struct WaitQueue;

// Saved CPU context for context switch
struct TaskContext {
    u64 x19, x20, x21, x22, x23, x24, x25, x26, x27, x28;  // Callee-saved
    u64 x29;    // Frame pointer
    u64 x30;    // Link register (return address)
    u64 sp;     // Stack pointer
    // x0-x18, pc are handled by interrupt frame or initial setup
};

// Task Control Block
struct Task {
    // Identity
    u64 id;
    const char* name;       // Debug name (optional)
    
    // Saved context
    TaskContext context;
    
    // Full register state (saved on interrupt/syscall)
    struct {
        u64 x[31];          // X0-X30
        u64 sp;             // SP_EL0 (user) or saved SP
        u64 pc;             // ELR_EL1
        u64 pstate;         // SPSR_EL1
    } trap_frame;
    
    // Kernel state
    VirtAddr kernel_stack_base;
    VirtAddr kernel_stack_top;
    TaskState state;
    u32 flags;
    
    // Scheduling
    Task* next_ready;       // Ready queue linkage
    u64 wake_time;          // Absolute time for timed waits
    u32 time_slice;         // Remaining ticks
    u32 priority;           // Reserved for future use
    
    // Structured concurrency
    Task* parent;
    Task* first_child;
    Task* next_sibling;
    i32 exit_code;
    
    // Wait state
    WaitQueue* wait_queue;  // Queue we're blocked on (if any)
    Task* next_waiting;     // Linkage in wait queue
    u64 wait_token;         // Opaque token for wait condition
    
    // Statistics
    u64 total_ticks;        // Total ticks consumed
    u64 start_time;         // When task was created
};

// Task flags
constexpr u32 TASK_FLAG_KERNEL = 1 << 0;    // Kernel task (Phase 2)
constexpr u32 TASK_FLAG_IDLE   = 1 << 1;    // Idle task

// Configuration
constexpr usize KERNEL_STACK_SIZE = 16 * 1024;  // 16KB per task
constexpr u32 TIME_SLICE_DEFAULT = 10;          // 10ms at 1000Hz

// Task management functions
void init();
Result<Task*, i32> create(void (*entry)(void*), void* arg, const char* name = nullptr);
void exit(i32 code);
void yield();
Task* current();

// Internal
Task* allocate_task();
void free_task(Task* task);
void setup_kernel_stack(Task* task, void (*entry)(void*), void* arg);

} // namespace viper::sched
```

### 1.2 Task Implementation

```cpp
// kernel/sched/task.cpp
#include "task.hpp"
#include "scheduler.hpp"
#include "../mm/heap.hpp"
#include "../mm/pmm.hpp"
#include "../mm/vmm.hpp"
#include "../lib/format.hpp"
#include "../lib/string.hpp"

namespace viper::sched {

namespace {
    u64 next_task_id = 1;
    Task* current_task = nullptr;
    Task idle_task_storage;
    
    // Task free list for recycling
    Task* free_list = nullptr;
    
    // Simple task table (fixed size for Phase 2)
    constexpr usize MAX_TASKS = 64;
    Task* task_table[MAX_TASKS] = {};
}

void init() {
    // Initialize idle task
    auto& idle = idle_task_storage;
    memset(&idle, 0, sizeof(Task));
    idle.id = 0;
    idle.name = "idle";
    idle.state = TaskState::Ready;
    idle.flags = TASK_FLAG_KERNEL | TASK_FLAG_IDLE;
    idle.time_slice = TIME_SLICE_DEFAULT;
    
    // Idle task uses the boot stack (already set up)
    // We'll switch away from it on first schedule()
    
    current_task = &idle;
    task_table[0] = &idle;
    
    kprintf("Tasks: Initialized (max %lu tasks)\n", MAX_TASKS);
}

Task* allocate_task() {
    // Check free list first
    if (free_list) {
        Task* task = free_list;
        free_list = task->next_ready;
        memset(task, 0, sizeof(Task));
        return task;
    }
    
    // Allocate new task
    Task* task = new Task;
    if (!task) return nullptr;
    
    memset(task, 0, sizeof(Task));
    return task;
}

void free_task(Task* task) {
    // Free kernel stack
    if (task->kernel_stack_base.raw() != 0) {
        // Return pages to PMM
        usize pages = KERNEL_STACK_SIZE / pmm::PAGE_SIZE;
        for (usize i = 0; i < pages; i++) {
            VirtAddr va = task->kernel_stack_base + i * pmm::PAGE_SIZE;
            auto phys = vmm::translate(va);
            if (phys.has_value()) {
                vmm::unmap_page(va);
                pmm::free_page(*phys);
            }
        }
    }
    
    // Add to free list
    task->next_ready = free_list;
    free_list = task;
}

void setup_kernel_stack(Task* task, void (*entry)(void*), void* arg) {
    // Stack grows down, so we set up the initial frame at the top
    u64* sp = reinterpret_cast<u64*>(task->kernel_stack_top.raw());
    
    // Push a fake "return address" that will call task_exit if entry() returns
    extern "C" void task_entry_trampoline();
    extern "C" void task_exit_trampoline();
    
    // Set up context so that when we "return" to this task,
    // it starts executing at task_entry_trampoline
    task->context.sp = task->kernel_stack_top.raw() - 16;  // Alignment
    task->context.x30 = reinterpret_cast<u64>(task_entry_trampoline);
    task->context.x29 = 0;  // Frame pointer
    
    // Pass entry and arg in callee-saved registers
    task->context.x19 = reinterpret_cast<u64>(entry);
    task->context.x20 = reinterpret_cast<u64>(arg);
}

Result<Task*, i32> create(void (*entry)(void*), void* arg, const char* name) {
    Task* task = allocate_task();
    if (!task) {
        return Result<Task*, i32>::failure(VERR_OUT_OF_MEMORY);
    }
    
    // Assign ID
    task->id = next_task_id++;
    task->name = name;
    
    // Allocate kernel stack
    usize pages = KERNEL_STACK_SIZE / pmm::PAGE_SIZE;
    auto stack_base = pmm::alloc_pages(pages);
    if (!stack_base.is_ok()) {
        free_task(task);
        return Result<Task*, i32>::failure(VERR_OUT_OF_MEMORY);
    }
    
    // Map kernel stack
    // Use a region in kernel virtual space for task stacks
    constexpr u64 TASK_STACKS_BASE = 0xFFFF'8001'0000'0000ULL;
    VirtAddr stack_virt{TASK_STACKS_BASE + (task->id - 1) * KERNEL_STACK_SIZE};
    
    for (usize i = 0; i < pages; i++) {
        PhysAddr pa = stack_base.unwrap() + i * pmm::PAGE_SIZE;
        VirtAddr va = stack_virt + i * pmm::PAGE_SIZE;
        if (!vmm::map_page(va, pa, vmm::PageFlags::Write | vmm::PageFlags::NoExecute)) {
            // Rollback
            for (usize j = 0; j < i; j++) {
                vmm::unmap_page(stack_virt + j * pmm::PAGE_SIZE);
            }
            pmm::free_pages(stack_base.unwrap(), pages);
            free_task(task);
            return Result<Task*, i32>::failure(VERR_OUT_OF_MEMORY);
        }
    }
    
    task->kernel_stack_base = stack_virt;
    task->kernel_stack_top = stack_virt + KERNEL_STACK_SIZE;
    
    // Set up initial context
    setup_kernel_stack(task, entry, arg);
    
    // Initialize state
    task->state = TaskState::Ready;
    task->flags = TASK_FLAG_KERNEL;
    task->time_slice = TIME_SLICE_DEFAULT;
    task->parent = current_task;
    
    // Add to parent's child list
    if (current_task && !(current_task->flags & TASK_FLAG_IDLE)) {
        task->next_sibling = current_task->first_child;
        current_task->first_child = task;
    }
    
    // Register in task table
    for (usize i = 0; i < MAX_TASKS; i++) {
        if (task_table[i] == nullptr) {
            task_table[i] = task;
            break;
        }
    }
    
    // Add to scheduler
    scheduler::enqueue(task);
    
    kprintf("Tasks: Created task %lu '%s'\n", task->id, name ? name : "(unnamed)");
    
    return Result<Task*, i32>::success(task);
}

Task* current() {
    return current_task;
}

void set_current(Task* task) {
    current_task = task;
}

void exit(i32 code) {
    Task* task = current_task;
    
    task->exit_code = code;
    task->state = TaskState::Exited;
    
    // Wake any tasks waiting on our exit
    // (Will be implemented with wait queues)
    
    kprintf("Tasks: Task %lu exited with code %d\n", task->id, code);
    
    // Yield to scheduler (never returns)
    scheduler::schedule();
    
    // Should never reach here
    __builtin_unreachable();
}

void yield() {
    // Give up remaining time slice
    current_task->time_slice = 0;
    scheduler::schedule();
}

} // namespace viper::sched
```

### 1.3 Task Entry Trampoline (Assembly)

```asm
// kernel/sched/context.S

.section .text

// Entry point for new tasks
// X19 = entry function
// X20 = argument
.global task_entry_trampoline
task_entry_trampoline:
    mov     x0, x20             // arg -> first parameter
    blr     x19                 // Call entry(arg)
    
    // If entry() returns, call exit(0)
    mov     x0, #0
    bl      task_exit_wrapper
    
    // Should never return
1:  wfi
    b       1b

// Wrapper for task exit (C++ linkage)
.global task_exit_wrapper
task_exit_wrapper:
    // x0 = exit code
    b       _ZN5viper5sched4exitEi   // viper::sched::exit(i32)
```

**Exit Criteria:**

- [ ] Task structure defined with all required fields
- [ ] Kernel stack allocation working
- [ ] Task creation and setup functional
- [ ] Entry trampoline correctly calls task function

---

## Milestone 2: Context Switching

**Duration:** Week 3  
**Deliverable:** Full context save/restore, switch_to() function

### 2.1 Context Switch Assembly

```asm
// kernel/sched/context.S

.section .text

// void context_switch(TaskContext* old_ctx, TaskContext* new_ctx)
// Save current context to old_ctx, load context from new_ctx
//
// TaskContext layout:
//   0x00: x19
//   0x08: x20
//   0x10: x21
//   0x18: x22
//   0x20: x23
//   0x28: x24
//   0x30: x25
//   0x38: x26
//   0x40: x27
//   0x48: x28
//   0x50: x29 (fp)
//   0x58: x30 (lr)
//   0x60: sp

.global context_switch
context_switch:
    // Save callee-saved registers to old context
    stp     x19, x20, [x0, #0x00]
    stp     x21, x22, [x0, #0x10]
    stp     x23, x24, [x0, #0x20]
    stp     x25, x26, [x0, #0x30]
    stp     x27, x28, [x0, #0x40]
    stp     x29, x30, [x0, #0x50]
    mov     x9, sp
    str     x9, [x0, #0x60]
    
    // Load callee-saved registers from new context
    ldp     x19, x20, [x1, #0x00]
    ldp     x21, x22, [x1, #0x10]
    ldp     x23, x24, [x1, #0x20]
    ldp     x25, x26, [x1, #0x30]
    ldp     x27, x28, [x1, #0x40]
    ldp     x29, x30, [x1, #0x50]
    ldr     x9, [x1, #0x60]
    mov     sp, x9
    
    // Return to new context
    // (x30 was loaded, so 'ret' goes to the right place)
    ret


// Save full trap frame on syscall/interrupt entry
// Called with: x0 = pointer to TrapFrame storage
// Clobbers: x1, x2
.global save_trap_frame
save_trap_frame:
    // Save x0-x29 (x30 saved separately)
    stp     x2, x3, [x0, #16]
    stp     x4, x5, [x0, #32]
    stp     x6, x7, [x0, #48]
    stp     x8, x9, [x0, #64]
    stp     x10, x11, [x0, #80]
    stp     x12, x13, [x0, #96]
    stp     x14, x15, [x0, #112]
    stp     x16, x17, [x0, #128]
    stp     x18, x19, [x0, #144]
    stp     x20, x21, [x0, #160]
    stp     x22, x23, [x0, #176]
    stp     x24, x25, [x0, #192]
    stp     x26, x27, [x0, #208]
    stp     x28, x29, [x0, #224]
    str     x30, [x0, #240]
    
    // Save SP, ELR, SPSR
    mrs     x1, sp_el0
    str     x1, [x0, #248]
    mrs     x1, elr_el1
    str     x1, [x0, #256]
    mrs     x1, spsr_el1
    str     x1, [x0, #264]
    
    ret


// Restore full trap frame on return to task
// Called with: x0 = pointer to TrapFrame
.global restore_trap_frame
restore_trap_frame:
    // Restore SP, ELR, SPSR first
    ldr     x1, [x0, #248]
    msr     sp_el0, x1
    ldr     x1, [x0, #256]
    msr     elr_el1, x1
    ldr     x1, [x0, #264]
    msr     spsr_el1, x1
    
    // Restore x2-x30 (x0, x1 last)
    ldp     x2, x3, [x0, #16]
    ldp     x4, x5, [x0, #32]
    ldp     x6, x7, [x0, #48]
    ldp     x8, x9, [x0, #64]
    ldp     x10, x11, [x0, #80]
    ldp     x12, x13, [x0, #96]
    ldp     x14, x15, [x0, #112]
    ldp     x16, x17, [x0, #128]
    ldp     x18, x19, [x0, #144]
    ldp     x20, x21, [x0, #160]
    ldp     x22, x23, [x0, #176]
    ldp     x24, x25, [x0, #192]
    ldp     x26, x27, [x0, #208]
    ldp     x28, x29, [x0, #224]
    ldr     x30, [x0, #240]
    
    // Restore x0, x1 last
    ldp     x0, x1, [x0, #0]
    
    eret
```

### 2.2 Context Switch Wrapper

```cpp
// kernel/sched/scheduler.hpp
#pragma once

#include "task.hpp"

namespace viper::sched::scheduler {

void init();
void enqueue(Task* task);
Task* dequeue();
void schedule();
void preempt();      // Called from timer interrupt
void tick();         // Called every timer tick

} // namespace viper::sched::scheduler
```

```cpp
// kernel/sched/scheduler.cpp
#include "scheduler.hpp"
#include "task.hpp"
#include "../arch/aarch64/cpu.hpp"
#include "../lib/format.hpp"

// Assembly functions
extern "C" void context_switch(void* old_ctx, void* new_ctx);

namespace viper::sched::scheduler {

namespace {
    // Simple FIFO ready queue
    struct ReadyQueue {
        Task* head = nullptr;
        Task* tail = nullptr;
        usize count = 0;
    };
    
    ReadyQueue ready_queue;
    bool scheduler_active = false;
}

void init() {
    scheduler_active = true;
    kprintf("Scheduler: Initialized (round-robin)\n");
}

void enqueue(Task* task) {
    if (task->state != TaskState::Ready) {
        task->state = TaskState::Ready;
    }
    
    task->next_ready = nullptr;
    
    if (ready_queue.tail) {
        ready_queue.tail->next_ready = task;
        ready_queue.tail = task;
    } else {
        ready_queue.head = ready_queue.tail = task;
    }
    ready_queue.count++;
}

Task* dequeue() {
    if (!ready_queue.head) return nullptr;
    
    Task* task = ready_queue.head;
    ready_queue.head = task->next_ready;
    if (!ready_queue.head) {
        ready_queue.tail = nullptr;
    }
    ready_queue.count--;
    task->next_ready = nullptr;
    return task;
}

void schedule() {
    if (!scheduler_active) return;
    
    Task* old_task = current();
    Task* new_task = dequeue();
    
    // If no ready tasks, use idle task
    if (!new_task) {
        extern Task idle_task_storage;
        new_task = &idle_task_storage;
    }
    
    // If old task is still runnable, re-enqueue it
    if (old_task && old_task->state == TaskState::Running) {
        old_task->state = TaskState::Ready;
        if (!(old_task->flags & TASK_FLAG_IDLE)) {
            enqueue(old_task);
        }
    }
    
    // Switch to new task
    if (new_task != old_task) {
        new_task->state = TaskState::Running;
        new_task->time_slice = TIME_SLICE_DEFAULT;
        set_current(new_task);
        
        context_switch(&old_task->context, &new_task->context);
    }
}

void tick() {
    Task* task = current();
    if (!task) return;
    
    task->total_ticks++;
    
    if (task->time_slice > 0) {
        task->time_slice--;
    }
}

void preempt() {
    Task* task = current();
    if (!task) return;
    
    // Don't preempt if time slice remaining
    if (task->time_slice > 0) return;
    
    // Preempt: schedule another task
    schedule();
}

} // namespace viper::sched::scheduler
```

### 2.3 Timer Integration

```cpp
// Update kernel/arch/aarch64/timer.cpp to call scheduler

namespace viper::timer {

namespace {
    void timer_handler() {
        ticks++;
        
        // Reschedule timer
        u64 cval;
        asm volatile("mrs %0, cntp_cval_el0" : "=r"(cval));
        cval += frequency / 1000;
        asm volatile("msr cntp_cval_el0, %0" :: "r"(cval));
        
        // Notify scheduler
        sched::scheduler::tick();
        
        // Check for preemption
        sched::scheduler::preempt();
    }
}

}
```

**Exit Criteria:**

- [ ] context_switch saves/restores all callee-saved registers
- [ ] Tasks can switch between each other
- [ ] Stack pointer is correctly saved/restored
- [ ] Return address (x30) is correctly handled

---

## Milestone 3: Scheduler

**Duration:** Week 4  
**Deliverable:** Fully working round-robin scheduler with preemption

### 3.1 Wait Queues

```cpp
// kernel/sync/wait_queue.hpp
#pragma once

#include "../sched/task.hpp"

namespace viper::sync {

// A wait queue holds tasks blocked on some condition
struct WaitQueue {
    sched::Task* head = nullptr;
    sched::Task* tail = nullptr;
    usize count = 0;
    
    // Block current task on this queue
    void block_current();
    
    // Block with timeout (returns false if timed out)
    bool block_current_timeout(u64 deadline_ns);
    
    // Wake one task from the queue
    sched::Task* wake_one();
    
    // Wake all tasks from the queue
    void wake_all();
    
    // Check if queue is empty
    bool empty() const { return head == nullptr; }
};

} // namespace viper::sync
```

```cpp
// kernel/sync/wait_queue.cpp
#include "wait_queue.hpp"
#include "../sched/scheduler.hpp"
#include "../arch/aarch64/timer.hpp"

namespace viper::sync {

void WaitQueue::block_current() {
    auto* task = sched::current();
    
    task->state = sched::TaskState::Blocked;
    task->wait_queue = this;
    task->next_waiting = nullptr;
    
    // Add to wait queue
    if (tail) {
        tail->next_waiting = task;
        tail = task;
    } else {
        head = tail = task;
    }
    count++;
    
    // Switch to another task
    sched::scheduler::schedule();
}

bool WaitQueue::block_current_timeout(u64 deadline_ns) {
    auto* task = sched::current();
    
    task->state = sched::TaskState::Blocked;
    task->wait_queue = this;
    task->wake_time = deadline_ns;
    task->next_waiting = nullptr;
    
    // Add to wait queue
    if (tail) {
        tail->next_waiting = task;
        tail = task;
    } else {
        head = tail = task;
    }
    count++;
    
    // Switch to another task
    sched::scheduler::schedule();
    
    // When we return, check if we timed out
    // (wake_time will be cleared by wake_one if woken normally)
    return task->wake_time == 0;
}

sched::Task* WaitQueue::wake_one() {
    if (!head) return nullptr;
    
    auto* task = head;
    head = task->next_waiting;
    if (!head) tail = nullptr;
    count--;
    
    task->next_waiting = nullptr;
    task->wait_queue = nullptr;
    task->wake_time = 0;  // Clear timeout
    task->state = sched::TaskState::Ready;
    
    sched::scheduler::enqueue(task);
    
    return task;
}

void WaitQueue::wake_all() {
    while (head) {
        wake_one();
    }
}

} // namespace viper::sync
```

### 3.2 Scheduler Test

```cpp
// kernel/test/sched_test.cpp
#include "../sched/task.hpp"
#include "../sched/scheduler.hpp"
#include "../lib/format.hpp"

namespace viper::test {

namespace {
    volatile int counter_a = 0;
    volatile int counter_b = 0;
    
    void task_a_func(void*) {
        for (int i = 0; i < 10; i++) {
            counter_a++;
            kprintf("Task A: count = %d\n", counter_a);
            sched::yield();
        }
        kprintf("Task A: done\n");
    }
    
    void task_b_func(void*) {
        for (int i = 0; i < 10; i++) {
            counter_b++;
            kprintf("Task B: count = %d\n", counter_b);
            sched::yield();
        }
        kprintf("Task B: done\n");
    }
}

void run_scheduler_test() {
    kprintf("\n=== Scheduler Test ===\n");
    
    auto result_a = sched::create(task_a_func, nullptr, "test_a");
    auto result_b = sched::create(task_b_func, nullptr, "test_b");
    
    if (!result_a.is_ok() || !result_b.is_ok()) {
        kprintf("Failed to create test tasks\n");
        return;
    }
    
    kprintf("Test tasks created, scheduler will run them\n");
}

} // namespace viper::test
```

**Exit Criteria:**

- [ ] Round-robin scheduling works
- [ ] Timer preemption interrupts long-running tasks
- [ ] Wait queues block and wake tasks correctly
- [ ] Test shows interleaved output from multiple tasks

---

## Milestone 4: Syscall Infrastructure

**Duration:** Weeks 5-6  
**Deliverable:** SVC handling, syscall dispatch, basic task syscalls

### 4.1 Syscall Numbers

```cpp
// kernel/include/syscall_nums.hpp
#pragma once

#include "../lib/types.hpp"

namespace viper {

enum class Syscall : u64 {
    // Tasks (0x001x)
    TaskSpawn       = 0x0010,
    TaskYield       = 0x0011,
    TaskExit        = 0x0012,
    TaskJoin        = 0x0014,
    TaskCurrent     = 0x0015,
    
    // Channels (0x002x)
    ChannelCreate   = 0x0020,
    ChannelSend     = 0x0021,
    ChannelRecv     = 0x0022,
    ChannelClose    = 0x0023,
    
    // Polling (0x006x)
    PollCreate      = 0x0060,
    PollAdd         = 0x0061,
    PollRemove      = 0x0062,
    PollWait        = 0x0063,
    
    // Time (0x007x)
    TimeNow         = 0x0070,
    TimerCreate     = 0x0071,
    TimerCancel     = 0x0072,
    
    // Debug (0x00Fx)
    DebugPrint      = 0x00F0,
};

} // namespace viper
```

### 4.2 Error Codes

```cpp
// kernel/include/error.hpp
#pragma once

#include "../lib/types.hpp"

namespace viper {

// Error codes (negative values)
constexpr i32 VOK                   =  0;
constexpr i32 VERR_INVALID_HANDLE   = -1;
constexpr i32 VERR_INVALID_ARG      = -2;
constexpr i32 VERR_OUT_OF_MEMORY    = -3;
constexpr i32 VERR_PERMISSION       = -4;
constexpr i32 VERR_WOULD_BLOCK      = -5;
constexpr i32 VERR_CHANNEL_CLOSED   = -6;
constexpr i32 VERR_NOT_FOUND        = -7;
constexpr i32 VERR_ALREADY_EXISTS   = -8;
constexpr i32 VERR_IO               = -9;
constexpr i32 VERR_CANCELLED        = -10;
constexpr i32 VERR_TIMEOUT          = -11;
constexpr i32 VERR_NOT_SUPPORTED    = -12;
constexpr i32 VERR_MSG_TOO_LARGE    = -13;
constexpr i32 VERR_BUFFER_TOO_SMALL = -14;

} // namespace viper
```

### 4.3 Syscall Entry (Assembly)

```asm
// kernel/syscall/syscall.S

.section .text

// SVC entry point - called from exception vector
// On entry:
//   X0-X5 = syscall arguments
//   X8 = syscall number
//   SP = kernel stack (set up by exception handler)
//
// We need to:
//   1. Save user registers to current task's trap_frame
//   2. Call syscall_dispatch(syscall_num, args...)
//   3. Restore registers and eret

.global syscall_entry
syscall_entry:
    // Get current task's trap_frame pointer
    bl      get_current_trap_frame  // Returns pointer in x0
    mov     x9, x0                  // Save trap_frame pointer
    
    // Save caller-saved registers we'll clobber
    // (x0-x7 are args, x8 is syscall num, x9-x15 are temps)
    stp     x0, x1, [x9, #0]
    stp     x2, x3, [x9, #16]
    stp     x4, x5, [x9, #32]
    stp     x6, x7, [x9, #48]
    str     x8, [x9, #64]           // syscall number
    
    // Save SP_EL0, ELR_EL1, SPSR_EL1
    mrs     x10, sp_el0
    mrs     x11, elr_el1
    mrs     x12, spsr_el1
    str     x10, [x9, #248]
    str     x11, [x9, #256]
    str     x12, [x9, #264]
    
    // Call C++ dispatcher
    // syscall_dispatch(num, arg0, arg1, arg2, arg3, arg4, arg5)
    mov     x0, x8                  // syscall number
    ldp     x1, x2, [x9, #0]        // arg0, arg1 (reload from frame)
    ldp     x3, x4, [x9, #16]       // arg2, arg3
    ldp     x5, x6, [x9, #32]       // arg4, arg5
    ldr     x7, [x9, #48]           // arg6 (if needed)
    bl      syscall_dispatch
    
    // x0 = error code (VError)
    // x1 = result0
    // x2 = result1
    // x3 = result2
    
    // Store results back to trap frame for return
    bl      get_current_trap_frame
    mov     x9, x0
    
    // Results are already in x0-x3 from dispatch
    // Just need to restore and return
    
    // Restore SP_EL0, ELR_EL1, SPSR_EL1
    ldr     x10, [x9, #248]
    ldr     x11, [x9, #256]
    ldr     x12, [x9, #264]
    msr     sp_el0, x10
    msr     elr_el1, x11
    msr     spsr_el1, x12
    
    // x0-x3 already have return values
    // Restore x4-x30 from trap frame
    ldp     x4, x5, [x9, #32]
    ldp     x6, x7, [x9, #48]
    // ... (restore remaining if needed)
    
    eret


// Helper to get current task's trap_frame pointer
.global get_current_trap_frame
get_current_trap_frame:
    // This will be implemented to return &current_task->trap_frame
    bl      _ZN5viper5sched7currentEv   // viper::sched::current()
    // Task* is in x0, trap_frame offset is known
    add     x0, x0, #TRAP_FRAME_OFFSET
    ret

.set TRAP_FRAME_OFFSET, 104  // Offset of trap_frame in Task struct
```

### 4.4 Syscall Dispatcher

```cpp
// kernel/syscall/dispatch.hpp
#pragma once

#include "../lib/types.hpp"

namespace viper::syscall {

struct SyscallResult {
    i64 error;      // X0: VError
    u64 result0;    // X1
    u64 result1;    // X2
    u64 result2;    // X3
};

SyscallResult dispatch(u64 num, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5);

} // namespace viper::syscall

// C linkage for assembly
extern "C" viper::syscall::SyscallResult syscall_dispatch(
    viper::u64 num, 
    viper::u64 a0, viper::u64 a1, viper::u64 a2,
    viper::u64 a3, viper::u64 a4, viper::u64 a5
);
```

```cpp
// kernel/syscall/dispatch.cpp
#include "dispatch.hpp"
#include "../include/syscall_nums.hpp"
#include "../include/error.hpp"
#include "../lib/format.hpp"

// Syscall implementations
#include "task_syscalls.hpp"
#include "channel_syscalls.hpp"
#include "poll_syscalls.hpp"
#include "time_syscalls.hpp"

namespace viper::syscall {

SyscallResult dispatch(u64 num, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5) {
    SyscallResult result = {VOK, 0, 0, 0};
    
    switch (static_cast<Syscall>(num)) {
        // Task syscalls
        case Syscall::TaskYield:
            result = sys_task_yield();
            break;
        case Syscall::TaskExit:
            result = sys_task_exit(static_cast<i32>(a0));
            break;
        case Syscall::TaskCurrent:
            result = sys_task_current();
            break;
            
        // Channel syscalls
        case Syscall::ChannelCreate:
            result = sys_channel_create();
            break;
        case Syscall::ChannelSend:
            result = sys_channel_send(
                static_cast<u32>(a0),   // handle
                a1,                      // data ptr
                a2,                      // len
                a3,                      // handles ptr
                a4                       // num_handles
            );
            break;
        case Syscall::ChannelRecv:
            result = sys_channel_recv(
                static_cast<u32>(a0),   // handle
                a1,                      // buf ptr
                a2,                      // buf_len
                a3,                      // handles_out ptr
                a4                       // handles_cap
            );
            break;
        case Syscall::ChannelClose:
            result = sys_channel_close(static_cast<u32>(a0));
            break;
            
        // Poll syscalls
        case Syscall::PollCreate:
            result = sys_poll_create();
            break;
        case Syscall::PollAdd:
            result = sys_poll_add(
                static_cast<u32>(a0),   // poll handle
                static_cast<u32>(a1),   // target handle
                static_cast<u32>(a2),   // events
                a3                       // token
            );
            break;
        case Syscall::PollRemove:
            result = sys_poll_remove(static_cast<u32>(a0), static_cast<u32>(a1));
            break;
        case Syscall::PollWait:
            result = sys_poll_wait(
                static_cast<u32>(a0),   // poll handle
                a1,                      // events buffer
                a2,                      // max_events
                a3                       // timeout_ns
            );
            break;
            
        // Time syscalls
        case Syscall::TimeNow:
            result = sys_time_now();
            break;
        case Syscall::TimerCreate:
            result = sys_timer_create(a0);  // deadline_ns
            break;
        case Syscall::TimerCancel:
            result = sys_timer_cancel(static_cast<u32>(a0));
            break;
            
        // Debug
        case Syscall::DebugPrint:
            result = sys_debug_print(a0, a1);
            break;
            
        default:
            kprintf("Syscall: Unknown syscall 0x%lx\n", num);
            result.error = VERR_NOT_SUPPORTED;
            break;
    }
    
    return result;
}

} // namespace viper::syscall

extern "C" viper::syscall::SyscallResult syscall_dispatch(
    viper::u64 num,
    viper::u64 a0, viper::u64 a1, viper::u64 a2,
    viper::u64 a3, viper::u64 a4, viper::u64 a5
) {
    return viper::syscall::dispatch(num, a0, a1, a2, a3, a4, a5);
}
```

### 4.5 Task Syscalls

```cpp
// kernel/syscall/task_syscalls.hpp
#pragma once

#include "dispatch.hpp"

namespace viper::syscall {

SyscallResult sys_task_yield();
SyscallResult sys_task_exit(i32 code);
SyscallResult sys_task_current();

}
```

```cpp
// kernel/syscall/task_syscalls.cpp
#include "task_syscalls.hpp"
#include "../sched/task.hpp"
#include "../sched/scheduler.hpp"

namespace viper::syscall {

SyscallResult sys_task_yield() {
    sched::yield();
    return {VOK, 0, 0, 0};
}

SyscallResult sys_task_exit(i32 code) {
    sched::exit(code);
    // Never returns
    __builtin_unreachable();
}

SyscallResult sys_task_current() {
    auto* task = sched::current();
    return {VOK, task->id, 0, 0};
}

}
```

**Exit Criteria:**

- [ ] SVC exceptions route to syscall_entry
- [ ] Syscall number in X8 dispatches correctly
- [ ] Arguments passed in X0-X5
- [ ] Results returned in X0-X3 (X0=error)
- [ ] TaskYield, TaskExit, TaskCurrent work

---

## Milestone 5: Channels

**Duration:** Weeks 7-8  
**Deliverable:** Non-blocking message passing with send/recv

### 5.1 Message Buffer

```cpp
// kernel/ipc/message.hpp
#pragma once

#include "../lib/types.hpp"

namespace viper::ipc {

constexpr usize MAX_MSG_SIZE = 2048;
constexpr usize MAX_MSG_HANDLES = 4;
constexpr usize CHANNEL_BUFFER_MSGS = 16;

struct Message {
    u8 data[MAX_MSG_SIZE];
    usize len;
    u32 handles[MAX_MSG_HANDLES];
    usize num_handles;
};

// Ring buffer for messages
struct MessageQueue {
    Message messages[CHANNEL_BUFFER_MSGS];
    usize head = 0;
    usize tail = 0;
    usize count = 0;
    
    bool empty() const { return count == 0; }
    bool full() const { return count >= CHANNEL_BUFFER_MSGS; }
    
    bool push(const Message& msg) {
        if (full()) return false;
        messages[tail] = msg;
        tail = (tail + 1) % CHANNEL_BUFFER_MSGS;
        count++;
        return true;
    }
    
    bool pop(Message& msg) {
        if (empty()) return false;
        msg = messages[head];
        head = (head + 1) % CHANNEL_BUFFER_MSGS;
        count--;
        return true;
    }
    
    const Message* peek() const {
        if (empty()) return nullptr;
        return &messages[head];
    }
};

} // namespace viper::ipc
```

### 5.2 Channel Implementation

```cpp
// kernel/ipc/channel.hpp
#pragma once

#include "../lib/types.hpp"
#include "../sync/wait_queue.hpp"
#include "message.hpp"

namespace viper::ipc {

struct ChannelEndpoint;

// A channel is a bidirectional message queue
struct Channel {
    ChannelEndpoint* send_end = nullptr;
    ChannelEndpoint* recv_end = nullptr;
    MessageQueue queue;
    sync::WaitQueue readers;   // Tasks blocked on recv
    sync::WaitQueue writers;   // Tasks blocked on send (when full)
    bool closed = false;
    u32 ref_count = 0;
};

struct ChannelEndpoint {
    Channel* channel;
    bool is_send_end;
    u32 handle;  // Handle in owner's table
};

// Channel management
Channel* create_channel();
void destroy_channel(Channel* ch);

// Operations
i32 channel_send(ChannelEndpoint* ep, const void* data, usize len,
                 const u32* handles, usize num_handles);
i32 channel_recv(ChannelEndpoint* ep, void* buf, usize buf_len,
                 usize* actual_len, u32* handles_out, usize* num_handles_out);
void channel_close(ChannelEndpoint* ep);

} // namespace viper::ipc
```

```cpp
// kernel/ipc/channel.cpp
#include "channel.hpp"
#include "../mm/heap.hpp"
#include "../include/error.hpp"
#include "../lib/string.hpp"

namespace viper::ipc {

Channel* create_channel() {
    auto* ch = new Channel;
    if (!ch) return nullptr;
    
    ch->send_end = new ChannelEndpoint{ch, true, 0};
    ch->recv_end = new ChannelEndpoint{ch, false, 0};
    
    if (!ch->send_end || !ch->recv_end) {
        delete ch->send_end;
        delete ch->recv_end;
        delete ch;
        return nullptr;
    }
    
    ch->ref_count = 2;  // Two endpoints
    return ch;
}

void destroy_channel(Channel* ch) {
    delete ch->send_end;
    delete ch->recv_end;
    delete ch;
}

i32 channel_send(ChannelEndpoint* ep, const void* data, usize len,
                 const u32* handles, usize num_handles) {
    if (!ep || !ep->channel) return VERR_INVALID_HANDLE;
    if (!ep->is_send_end) return VERR_PERMISSION;
    
    auto* ch = ep->channel;
    
    if (ch->closed) return VERR_CHANNEL_CLOSED;
    if (len > MAX_MSG_SIZE) return VERR_MSG_TOO_LARGE;
    if (num_handles > MAX_MSG_HANDLES) return VERR_INVALID_ARG;
    
    // Check if buffer is full
    if (ch->queue.full()) {
        return VERR_WOULD_BLOCK;
    }
    
    // Construct message
    Message msg;
    if (data && len > 0) {
        memcpy(msg.data, data, len);
    }
    msg.len = len;
    msg.num_handles = num_handles;
    if (handles && num_handles > 0) {
        memcpy(msg.handles, handles, num_handles * sizeof(u32));
    }
    
    // Queue message
    ch->queue.push(msg);
    
    // Wake any waiting receivers
    ch->readers.wake_one();
    
    return VOK;
}

i32 channel_recv(ChannelEndpoint* ep, void* buf, usize buf_len,
                 usize* actual_len, u32* handles_out, usize* num_handles_out) {
    if (!ep || !ep->channel) return VERR_INVALID_HANDLE;
    if (ep->is_send_end) return VERR_PERMISSION;
    
    auto* ch = ep->channel;
    
    // Check for message
    const Message* msg = ch->queue.peek();
    if (!msg) {
        if (ch->closed) return VERR_CHANNEL_CLOSED;
        return VERR_WOULD_BLOCK;
    }
    
    // Check buffer size
    if (buf_len < msg->len) {
        if (actual_len) *actual_len = msg->len;
        return VERR_BUFFER_TOO_SMALL;
    }
    
    // Copy message
    Message m;
    ch->queue.pop(m);
    
    if (buf && m.len > 0) {
        memcpy(buf, m.data, m.len);
    }
    if (actual_len) *actual_len = m.len;
    
    if (handles_out && num_handles_out) {
        usize copy_handles = m.num_handles;
        if (copy_handles > *num_handles_out) {
            copy_handles = *num_handles_out;
        }
        memcpy(handles_out, m.handles, copy_handles * sizeof(u32));
        *num_handles_out = m.num_handles;
    }
    
    // Wake any waiting senders
    ch->writers.wake_one();
    
    return VOK;
}

void channel_close(ChannelEndpoint* ep) {
    if (!ep || !ep->channel) return;
    
    auto* ch = ep->channel;
    ch->ref_count--;
    
    if (ch->ref_count == 0) {
        destroy_channel(ch);
    } else {
        ch->closed = true;
        // Wake all waiters so they see the close
        ch->readers.wake_all();
        ch->writers.wake_all();
    }
    
    if (ep->is_send_end) {
        ch->send_end = nullptr;
    } else {
        ch->recv_end = nullptr;
    }
}

} // namespace viper::ipc
```

### 5.3 Channel Syscalls

```cpp
// kernel/syscall/channel_syscalls.hpp
#pragma once

#include "dispatch.hpp"

namespace viper::syscall {

SyscallResult sys_channel_create();
SyscallResult sys_channel_send(u32 handle, u64 data, u64 len, u64 handles, u64 num_handles);
SyscallResult sys_channel_recv(u32 handle, u64 buf, u64 buf_len, u64 handles_out, u64 handles_cap);
SyscallResult sys_channel_close(u32 handle);

}
```

```cpp
// kernel/syscall/channel_syscalls.cpp
#include "channel_syscalls.hpp"
#include "../ipc/channel.hpp"
#include "../include/error.hpp"

namespace viper::syscall {

// Simple handle table for Phase 2 (proper capability table in Phase 3)
namespace {
    constexpr usize MAX_HANDLES = 256;
    void* handle_table[MAX_HANDLES] = {};
    u16 handle_kinds[MAX_HANDLES] = {};
    u32 next_handle = 1;
    
    constexpr u16 KIND_CHANNEL_SEND = 1;
    constexpr u16 KIND_CHANNEL_RECV = 2;
    
    u32 alloc_handle(void* obj, u16 kind) {
        for (u32 i = 1; i < MAX_HANDLES; i++) {
            if (handle_table[i] == nullptr) {
                handle_table[i] = obj;
                handle_kinds[i] = kind;
                return i;
            }
        }
        return 0;
    }
    
    void* get_handle(u32 h, u16 expected_kind) {
        if (h == 0 || h >= MAX_HANDLES) return nullptr;
        if (handle_kinds[h] != expected_kind) return nullptr;
        return handle_table[h];
    }
    
    void free_handle(u32 h) {
        if (h > 0 && h < MAX_HANDLES) {
            handle_table[h] = nullptr;
            handle_kinds[h] = 0;
        }
    }
}

SyscallResult sys_channel_create() {
    auto* ch = ipc::create_channel();
    if (!ch) {
        return {VERR_OUT_OF_MEMORY, 0, 0, 0};
    }
    
    u32 send_h = alloc_handle(ch->send_end, KIND_CHANNEL_SEND);
    u32 recv_h = alloc_handle(ch->recv_end, KIND_CHANNEL_RECV);
    
    if (send_h == 0 || recv_h == 0) {
        free_handle(send_h);
        free_handle(recv_h);
        ipc::destroy_channel(ch);
        return {VERR_OUT_OF_MEMORY, 0, 0, 0};
    }
    
    ch->send_end->handle = send_h;
    ch->recv_end->handle = recv_h;
    
    return {VOK, send_h, recv_h, 0};
}

SyscallResult sys_channel_send(u32 handle, u64 data, u64 len, u64 handles, u64 num_handles) {
    auto* ep = static_cast<ipc::ChannelEndpoint*>(get_handle(handle, KIND_CHANNEL_SEND));
    if (!ep) {
        return {VERR_INVALID_HANDLE, 0, 0, 0};
    }
    
    i32 err = ipc::channel_send(ep,
                                reinterpret_cast<void*>(data), len,
                                reinterpret_cast<u32*>(handles), num_handles);
    return {err, 0, 0, 0};
}

SyscallResult sys_channel_recv(u32 handle, u64 buf, u64 buf_len, u64 handles_out, u64 handles_cap) {
    auto* ep = static_cast<ipc::ChannelEndpoint*>(get_handle(handle, KIND_CHANNEL_RECV));
    if (!ep) {
        return {VERR_INVALID_HANDLE, 0, 0, 0};
    }
    
    usize actual_len = 0;
    usize num_handles = handles_cap;
    
    i32 err = ipc::channel_recv(ep,
                                reinterpret_cast<void*>(buf), buf_len,
                                &actual_len,
                                reinterpret_cast<u32*>(handles_out), &num_handles);
    
    return {err, actual_len, num_handles, 0};
}

SyscallResult sys_channel_close(u32 handle) {
    // Try both kinds
    auto* ep_send = static_cast<ipc::ChannelEndpoint*>(get_handle(handle, KIND_CHANNEL_SEND));
    auto* ep_recv = static_cast<ipc::ChannelEndpoint*>(get_handle(handle, KIND_CHANNEL_RECV));
    
    ipc::ChannelEndpoint* ep = ep_send ? ep_send : ep_recv;
    if (!ep) {
        return {VERR_INVALID_HANDLE, 0, 0, 0};
    }
    
    ipc::channel_close(ep);
    free_handle(handle);
    
    return {VOK, 0, 0, 0};
}

}
```

**Exit Criteria:**

- [ ] ChannelCreate returns two handles
- [ ] ChannelSend queues message
- [ ] ChannelRecv dequeues message
- [ ] WOULD_BLOCK returned when appropriate
- [ ] Close propagates to other end

---

## Milestone 6: Poll & Timers

**Duration:** Weeks 9-10  
**Deliverable:** PollWait (the only blocking syscall), timer objects

### 6.1 Poll Set

```cpp
// kernel/sync/poll.hpp
#pragma once

#include "../lib/types.hpp"
#include "wait_queue.hpp"

namespace viper::sync {

constexpr u32 VPOLL_READABLE   = 1 << 0;
constexpr u32 VPOLL_WRITABLE   = 1 << 1;
constexpr u32 VPOLL_ERROR      = 1 << 2;
constexpr u32 VPOLL_HANGUP     = 1 << 3;
constexpr u32 VPOLL_TIMER      = 1 << 4;
constexpr u32 VPOLL_TASK_EXIT  = 1 << 6;

struct PollEntry {
    u32 handle;
    u32 events;         // Requested events
    u64 token;          // User token
    PollEntry* next;
};

struct PollEvent {
    u32 handle;
    u32 events;         // Triggered events
    i32 status;
    u32 _pad;
    u64 token;
    u64 result;
};
static_assert(sizeof(PollEvent) == 32);

struct PollSet {
    PollEntry* entries = nullptr;
    usize count = 0;
    WaitQueue waiters;
    
    i32 add(u32 handle, u32 events, u64 token);
    i32 remove(u32 handle);
    
    // Check which entries have ready events
    usize poll_ready(PollEvent* out, usize max_events);
};

// The only blocking syscall
i32 poll_wait(PollSet* ps, PollEvent* events, usize max_events,
              usize* num_events, u64 timeout_ns);

} // namespace viper::sync
```

```cpp
// kernel/sync/poll.cpp
#include "poll.hpp"
#include "../ipc/channel.hpp"
#include "timer.hpp"
#include "../arch/aarch64/timer.hpp"
#include "../sched/scheduler.hpp"
#include "../include/error.hpp"
#include "../mm/heap.hpp"

namespace viper::sync {

i32 PollSet::add(u32 handle, u32 events, u64 token) {
    // Check for duplicate
    for (auto* e = entries; e; e = e->next) {
        if (e->handle == handle) {
            e->events = events;
            e->token = token;
            return VOK;
        }
    }
    
    auto* entry = new PollEntry{handle, events, token, entries};
    if (!entry) return VERR_OUT_OF_MEMORY;
    
    entries = entry;
    count++;
    return VOK;
}

i32 PollSet::remove(u32 handle) {
    PollEntry** pp = &entries;
    while (*pp) {
        if ((*pp)->handle == handle) {
            auto* e = *pp;
            *pp = e->next;
            delete e;
            count--;
            return VOK;
        }
        pp = &(*pp)->next;
    }
    return VERR_NOT_FOUND;
}

usize PollSet::poll_ready(PollEvent* out, usize max_events) {
    usize found = 0;
    
    for (auto* e = entries; e && found < max_events; e = e->next) {
        u32 triggered = 0;
        u64 result = 0;
        
        // Check what kind of object this handle is and poll it
        // (This is simplified - real version checks handle table)
        
        // For channels: check if readable/writable
        // For timers: check if expired
        // etc.
        
        // Placeholder: assume we have a way to poll handles
        extern bool poll_handle(u32 handle, u32 events, u32* triggered, u64* result);
        
        if (poll_handle(e->handle, e->events, &triggered, &result)) {
            out[found].handle = e->handle;
            out[found].events = triggered;
            out[found].status = VOK;
            out[found].token = e->token;
            out[found].result = result;
            found++;
        }
    }
    
    return found;
}

i32 poll_wait(PollSet* ps, PollEvent* events, usize max_events,
              usize* num_events, u64 timeout_ns) {
    // First, check if any events are already ready
    usize ready = ps->poll_ready(events, max_events);
    if (ready > 0) {
        *num_events = ready;
        return VOK;
    }
    
    // If timeout is 0, return immediately
    if (timeout_ns == 0) {
        *num_events = 0;
        return VOK;
    }
    
    // Calculate deadline
    u64 deadline = (timeout_ns == ~0ULL) ? ~0ULL : 
                   timer::get_ns() + timeout_ns;
    
    // Block until an event fires or timeout
    while (true) {
        // Check for events
        ready = ps->poll_ready(events, max_events);
        if (ready > 0) {
            *num_events = ready;
            return VOK;
        }
        
        // Check timeout
        if (timeout_ns != ~0ULL && timer::get_ns() >= deadline) {
            *num_events = 0;
            return VERR_TIMEOUT;
        }
        
        // Block on the poll set's wait queue
        // (Will be woken when an event source signals)
        if (timeout_ns == ~0ULL) {
            ps->waiters.block_current();
        } else {
            if (!ps->waiters.block_current_timeout(deadline)) {
                // Timed out
                *num_events = 0;
                return VERR_TIMEOUT;
            }
        }
    }
}

} // namespace viper::sync
```

### 6.2 Timer Objects

```cpp
// kernel/sync/timer.hpp
#pragma once

#include "../lib/types.hpp"
#include "wait_queue.hpp"

namespace viper::sync {

struct Timer {
    u64 deadline_ns;
    u64 expiration_count;
    bool expired;
    bool cancelled;
    WaitQueue* poll_set_waiters;  // Wake poll sets when expired
    Timer* next_active;           // Global active timer list
};

Timer* timer_create(u64 deadline_ns);
void timer_cancel(Timer* t);
void timer_destroy(Timer* t);

// Called by timer interrupt handler
void timer_check_expirations();

} // namespace viper::sync
```

```cpp
// kernel/sync/timer_obj.cpp
#include "timer.hpp"
#include "../arch/aarch64/timer.hpp"
#include "../mm/heap.hpp"

namespace viper::sync {

namespace {
    Timer* active_timers = nullptr;
}

Timer* timer_create(u64 deadline_ns) {
    auto* t = new Timer;
    if (!t) return nullptr;
    
    t->deadline_ns = deadline_ns;
    t->expiration_count = 0;
    t->expired = false;
    t->cancelled = false;
    t->poll_set_waiters = nullptr;
    
    // Insert into active timer list (sorted by deadline)
    Timer** pp = &active_timers;
    while (*pp && (*pp)->deadline_ns < deadline_ns) {
        pp = &(*pp)->next_active;
    }
    t->next_active = *pp;
    *pp = t;
    
    return t;
}

void timer_cancel(Timer* t) {
    if (!t) return;
    t->cancelled = true;
    
    // Remove from active list
    Timer** pp = &active_timers;
    while (*pp) {
        if (*pp == t) {
            *pp = t->next_active;
            break;
        }
        pp = &(*pp)->next_active;
    }
}

void timer_destroy(Timer* t) {
    timer_cancel(t);
    delete t;
}

void timer_check_expirations() {
    u64 now = timer::get_ns();
    
    while (active_timers && active_timers->deadline_ns <= now) {
        Timer* t = active_timers;
        active_timers = t->next_active;
        
        t->expired = true;
        t->expiration_count++;
        t->next_active = nullptr;
        
        // Wake any poll sets watching this timer
        // (Implementation depends on how poll sets register)
    }
}

} // namespace viper::sync
```

### 6.3 Poll Syscalls

```cpp
// kernel/syscall/poll_syscalls.cpp
#include "poll_syscalls.hpp"
#include "../sync/poll.hpp"
#include "../include/error.hpp"
#include "../mm/heap.hpp"

namespace viper::syscall {

namespace {
    constexpr u16 KIND_POLL = 10;
    
    // Reuse handle table from channel_syscalls
    extern u32 alloc_handle(void* obj, u16 kind);
    extern void* get_handle(u32 h, u16 expected_kind);
    extern void free_handle(u32 h);
}

SyscallResult sys_poll_create() {
    auto* ps = new sync::PollSet;
    if (!ps) {
        return {VERR_OUT_OF_MEMORY, 0, 0, 0};
    }
    
    u32 handle = alloc_handle(ps, KIND_POLL);
    if (handle == 0) {
        delete ps;
        return {VERR_OUT_OF_MEMORY, 0, 0, 0};
    }
    
    return {VOK, handle, 0, 0};
}

SyscallResult sys_poll_add(u32 poll_handle, u32 target, u32 events, u64 token) {
    auto* ps = static_cast<sync::PollSet*>(get_handle(poll_handle, KIND_POLL));
    if (!ps) {
        return {VERR_INVALID_HANDLE, 0, 0, 0};
    }
    
    i32 err = ps->add(target, events, token);
    return {err, 0, 0, 0};
}

SyscallResult sys_poll_remove(u32 poll_handle, u32 target) {
    auto* ps = static_cast<sync::PollSet*>(get_handle(poll_handle, KIND_POLL));
    if (!ps) {
        return {VERR_INVALID_HANDLE, 0, 0, 0};
    }
    
    i32 err = ps->remove(target);
    return {err, 0, 0, 0};
}

SyscallResult sys_poll_wait(u32 poll_handle, u64 events_buf, u64 max_events, u64 timeout_ns) {
    auto* ps = static_cast<sync::PollSet*>(get_handle(poll_handle, KIND_POLL));
    if (!ps) {
        return {VERR_INVALID_HANDLE, 0, 0, 0};
    }
    
    auto* events = reinterpret_cast<sync::PollEvent*>(events_buf);
    usize num_events = 0;
    
    i32 err = sync::poll_wait(ps, events, max_events, &num_events, timeout_ns);
    
    return {err, num_events, 0, 0};
}

}
```

**Exit Criteria:**

- [ ] PollCreate returns poll set handle
- [ ] PollAdd registers handles for events
- [ ] PollWait blocks until event or timeout
- [ ] Timer objects expire and wake poll sets

---

## Milestone 7: Integration & Testing

**Duration:** Weeks 11-12  
**Deliverable:** Ping-pong test passes, system stable

### 7.1 Ping-Pong Test

```cpp
// kernel/test/pingpong_test.cpp
#include "../sched/task.hpp"
#include "../syscall/dispatch.hpp"
#include "../include/syscall_nums.hpp"
#include "../lib/format.hpp"

namespace viper::test {

namespace {
    u32 send_handle = 0;
    u32 recv_handle = 0;
    volatile bool test_done = false;
    
    // Helper to make syscalls from kernel tasks
    syscall::SyscallResult do_syscall(Syscall num, u64 a0 = 0, u64 a1 = 0,
                                       u64 a2 = 0, u64 a3 = 0, u64 a4 = 0) {
        return syscall::dispatch(static_cast<u64>(num), a0, a1, a2, a3, a4, 0);
    }
    
    void ping_task(void*) {
        kprintf("Ping: Starting\n");
        
        char msg[64];
        usize msg_len;
        
        for (int i = 0; i < 5; i++) {
            // Send "ping N"
            int len = snprintf(msg, sizeof(msg), "ping %d", i);
            auto r = do_syscall(Syscall::ChannelSend, send_handle,
                               reinterpret_cast<u64>(msg), len, 0, 0);
            if (r.error != VOK) {
                kprintf("Ping: Send failed: %d\n", r.error);
                break;
            }
            kprintf("Ping: Sent '%s'\n", msg);
            
            // Receive response
            while (true) {
                r = do_syscall(Syscall::ChannelRecv, recv_handle,
                              reinterpret_cast<u64>(msg), sizeof(msg), 0, 0);
                if (r.error == VOK) {
                    msg_len = r.result0;
                    msg[msg_len] = '\0';
                    kprintf("Ping: Received '%s'\n", msg);
                    break;
                } else if (r.error == VERR_WOULD_BLOCK) {
                    sched::yield();
                } else {
                    kprintf("Ping: Recv failed: %d\n", r.error);
                    break;
                }
            }
        }
        
        kprintf("Ping: Done\n");
        test_done = true;
    }
    
    void pong_task(void*) {
        kprintf("Pong: Starting\n");
        
        // Pong uses the opposite ends
        u32 my_recv = send_handle;  // Ping's send is our recv
        u32 my_send = recv_handle;  // Ping's recv is our send
        
        // Actually, we need a second channel pair for bidirectional
        // Let's create another channel
        auto r = do_syscall(Syscall::ChannelCreate);
        if (r.error != VOK) {
            kprintf("Pong: Failed to create response channel\n");
            return;
        }
        
        // For simplicity, let's just use polling
        char msg[64];
        
        for (int i = 0; i < 5; i++) {
            // Wait for message from ping
            while (true) {
                r = do_syscall(Syscall::ChannelRecv, my_recv,
                              reinterpret_cast<u64>(msg), sizeof(msg), 0, 0);
                if (r.error == VOK) {
                    usize len = r.result0;
                    msg[len] = '\0';
                    kprintf("Pong: Received '%s'\n", msg);
                    break;
                } else if (r.error == VERR_WOULD_BLOCK) {
                    sched::yield();
                } else {
                    kprintf("Pong: Recv failed: %d\n", r.error);
                    return;
                }
            }
            
            // Send "pong N"
            int len = snprintf(msg, sizeof(msg), "pong %d", i);
            r = do_syscall(Syscall::ChannelSend, my_send,
                          reinterpret_cast<u64>(msg), len, 0, 0);
            if (r.error != VOK) {
                kprintf("Pong: Send failed: %d\n", r.error);
                break;
            }
            kprintf("Pong: Sent '%s'\n", msg);
        }
        
        kprintf("Pong: Done\n");
    }
}

void run_pingpong_test() {
    kprintf("\n=== Ping-Pong IPC Test ===\n");
    
    // Create channel pair for ping->pong
    auto r = syscall::dispatch(static_cast<u64>(Syscall::ChannelCreate),
                               0, 0, 0, 0, 0, 0);
    if (r.error != VOK) {
        kprintf("Failed to create channel: %d\n", r.error);
        return;
    }
    
    u32 ping_to_pong_send = r.result0;
    u32 ping_to_pong_recv = r.result1;
    
    // Create channel pair for pong->ping
    r = syscall::dispatch(static_cast<u64>(Syscall::ChannelCreate),
                         0, 0, 0, 0, 0, 0);
    if (r.error != VOK) {
        kprintf("Failed to create channel: %d\n", r.error);
        return;
    }
    
    u32 pong_to_ping_send = r.result0;
    u32 pong_to_ping_recv = r.result1;
    
    // Ping uses: send=ping_to_pong_send, recv=pong_to_ping_recv
    // Pong uses: send=pong_to_ping_send, recv=ping_to_pong_recv
    
    // Create ping and pong tasks with their handles
    struct PingPongArgs {
        u32 send_h;
        u32 recv_h;
    };
    
    static PingPongArgs ping_args = {ping_to_pong_send, pong_to_ping_recv};
    static PingPongArgs pong_args = {pong_to_ping_send, ping_to_pong_recv};
    
    // Simplified: use globals
    send_handle = ping_to_pong_send;
    recv_handle = pong_to_ping_recv;
    
    auto ping_result = sched::create(ping_task, nullptr, "ping");
    auto pong_result = sched::create(pong_task, nullptr, "pong");
    
    if (!ping_result.is_ok() || !pong_result.is_ok()) {
        kprintf("Failed to create test tasks\n");
        return;
    }
    
    kprintf("Test tasks created, running...\n");
    
    // Let the scheduler run them
    while (!test_done) {
        sched::yield();
    }
    
    kprintf("=== Ping-Pong Test Complete ===\n\n");
}

} // namespace viper::test
```

### 7.2 Updated Kernel Main

```cpp
// kernel/main.cpp (Phase 2 additions)

// ... Phase 1 initialization ...

// === Phase 2: Multitasking ===
kprintf("[BOOT] Initializing task system...\n");
sched::init();

kprintf("[BOOT] Initializing scheduler...\n");
sched::scheduler::init();

// Run tests
#ifdef VIPER_RUN_TESTS
kprintf("\n[TEST] Running scheduler test...\n");
test::run_scheduler_test();

kprintf("\n[TEST] Running ping-pong test...\n");
test::run_pingpong_test();
#endif

kprintf("\nBoot complete. Entering idle loop.\n");

// Idle loop - scheduler will preempt as needed
for (;;) {
    arch::halt();
}
```

**Exit Criteria:**

- [ ] Two tasks exchange 5 messages each direction
- [ ] No deadlocks or crashes
- [ ] Output shows interleaved ping/pong
- [ ] System remains stable after test

---

## Weekly Schedule

| Week | Focus             | Deliverables                         |
|------|-------------------|--------------------------------------|
| 1    | Task structure    | VTask, states, kernel stack alloc    |
| 2    | Task lifecycle    | Create, exit, task table             |
| 3    | Context switch    | Save/restore, switch_to assembly     |
| 4    | Scheduler         | Ready queue, round-robin, preemption |
| 5    | Syscall entry     | SVC handling, dispatch table         |
| 6    | Basic syscalls    | Yield, Exit, Current, Debug          |
| 7    | Channel structure | Message queue, endpoints             |
| 8    | Channel syscalls  | Create, Send, Recv, Close            |
| 9    | Poll sets         | PollSet, PollAdd, PollRemove         |
| 10   | PollWait & timers | Blocking wait, timer objects         |
| 11   | Integration       | Ping-pong test, bug fixes            |
| 12   | Stability         | Stress testing, documentation        |

---

## Definition of Done (Phase 2)

- [ ] Tasks can be created and scheduled
- [ ] Context switching preserves all registers
- [ ] Timer preemption works at 1000Hz
- [ ] SVC syscalls dispatch correctly
- [ ] Channels send/receive messages
- [ ] PollWait blocks and wakes on events
- [ ] Ping-pong test passes (5 exchanges)
- [ ] System stable for 10+ minutes under load
- [ ] No memory leaks in task create/exit cycle
- [ ] Code compiles with `-Wall -Wextra -Werror`

---

## Phase 3 Preview

Phase 3 (User Space) builds on Phase 2:

1. **Per-Viper address spaces** — TTBR0 switching
2. **Capability tables** — Replace simple handle table
3. **User/kernel boundary** — Proper EL0/EL1 transitions
4. **KHeap** — Kernel-managed shared memory
5. **vinit** — First user-space process

Phase 2's syscall infrastructure handles the kernel side.
Phase 2's scheduler handles user task scheduling.
Phase 2's channels become the IPC mechanism for Vipers.

---

*"Two tasks talking is the first step to a thousand."*
