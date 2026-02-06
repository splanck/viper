//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#include "scheduler.hpp"
#include "../arch/aarch64/cpu.hpp"
#include "../arch/aarch64/timer.hpp"
#include "../console/serial.hpp"
#include "../lib/spinlock.hpp"
#include "../lib/str.hpp"
#include "../mm/pmm.hpp"
#include "../viper/address_space.hpp"
#include "../viper/viper.hpp"
#include "bandwidth.hpp"
#include "cfs.hpp"
#include "deadline.hpp"
#include "heap.hpp"
#include "idle.hpp"
#include "task.hpp"

/**
 * @file scheduler.cpp
 * @brief Priority-based scheduler implementation.
 *
 * @details
 * This scheduler maintains 8 priority queues (0=highest, 7=lowest) and performs
 * context switches using the assembly `context_switch` routine.
 *
 * Priority mapping:
 * - Task priority 0-31   -> Queue 0 (highest)
 * - Task priority 32-63  -> Queue 1
 * - Task priority 64-95  -> Queue 2
 * - Task priority 96-127 -> Queue 3
 * - Task priority 128-159 -> Queue 4 (default tasks)
 * - Task priority 160-191 -> Queue 5
 * - Task priority 192-223 -> Queue 6
 * - Task priority 224-255 -> Queue 7 (idle task)
 *
 * Time slicing:
 * - Each task is given a fixed number of timer ticks (`TIME_SLICE_DEFAULT`).
 * - The timer interrupt decrements the counter and `preempt()` triggers a
 *   reschedule when it reaches zero.
 * - Tasks are preempted only by higher-priority tasks or when their slice expires.
 *
 * Lock Ordering (to prevent deadlocks):
 * - Always acquire sched_lock before per-CPU locks
 * - Ordering: sched_lock -> per_cpu_sched[N].lock
 * - Release in reverse order
 */
namespace scheduler {

namespace {

/**
 * @brief Per-CPU scheduler state with heaps for O(log n) scheduling.
 *
 * Each CPU has its own heaps and RT queues for reduced contention
 * and efficient task selection in SMP systems.
 *
 * Architecture:
 * - SCHED_DEADLINE tasks: deadline_heap (min-heap by deadline)
 * - SCHED_OTHER tasks: cfs_heap (min-heap by vruntime)
 * - SCHED_FIFO/RR tasks: rt_queues (linked lists for FIFO ordering)
 */
struct PerCpuScheduler {
    // Heaps for O(log n) task selection
    sched::TaskHeap deadline_heap; // SCHED_DEADLINE tasks
    sched::TaskHeap cfs_heap;      // SCHED_OTHER tasks

    // RT queues for FIFO ordering (SCHED_FIFO/RR only)
    struct {
        task::Task *head;
        task::Task *tail;
    } rt_queues[task::NUM_PRIORITY_QUEUES];

    u8 rt_bitmap; // Bitmap of non-empty RT queues

    // Synchronization
    Spinlock lock;

    // Statistics
    u64 context_switches;
    u32 total_tasks;
    u32 steals;
    u32 migrations;

    // State
    bool initialized;
    volatile u32 queue_count; // Atomic for lock-free checks

    // CFS fairness tracking (Linux CFS-style)
    // min_vruntime: monotonically increasing baseline for fair scheduling
    // All task vruntimes are normalized relative to this value
    u64 min_vruntime;
    u32 cfs_nr_running; // Number of CFS tasks in heap (excludes idle)
};

/**
 * @brief Advance min_vruntime based on a task that's about to run.
 *
 * Called when a CFS task is dequeued (picked to run). min_vruntime
 * advances to track the lowest vruntime that has been scheduled,
 * ensuring it monotonically increases as CPU time passes.
 */
inline void advance_min_vruntime(PerCpuScheduler &sched, u64 vruntime) {
    // Only advance, never go backward (monotonicity)
    if (vruntime > sched.min_vruntime) {
        sched.min_vruntime = vruntime;
    }
}

// Per-CPU scheduler state
PerCpuScheduler per_cpu_sched[cpu::MAX_CPUS];

// Global scheduler lock - protects global operations (init, stats)
// Per-CPU locks are used for normal scheduling operations
Spinlock sched_lock;

// Statistics (use atomics for SMP-safe access)
volatile u64 context_switch_count = 0;

// Scheduler running flag
bool running = false;

// Load balancing interval (ticks)
constexpr u32 LOAD_BALANCE_INTERVAL = 100;
volatile u32 load_balance_counter = 0;

/**
 * @brief Map a task priority (0-255) to a queue index (0-7).
 *
 * @param priority Task priority value.
 * @return Queue index (0=highest priority, 7=lowest).
 */
inline u8 priority_to_queue(u8 priority) {
    return priority / task::PRIORITIES_PER_QUEUE;
}

/**
 * @brief Select the best CPU to enqueue a task to.
 *
 * Selection order:
 * 1. If pinned to exactly one CPU, use that CPU
 * 2. If current CPU is in affinity mask, prefer it (cache locality)
 * 3. Otherwise, use least-loaded CPU in affinity mask
 *
 * @param t Task to enqueue
 * @return Target CPU ID
 */
u32 select_target_cpu(task::Task *t) {
    u32 affinity = t->cpu_affinity;
    u32 current_cpu = cpu::current_id();

    // If task is pinned to exactly one CPU, use it
    if (__builtin_popcount(affinity) == 1) {
        return __builtin_ctz(affinity); // Find the set bit
    }

    // If current CPU is in affinity mask and initialized, prefer it
    if ((affinity & (1u << current_cpu)) && per_cpu_sched[current_cpu].initialized) {
        return current_cpu;
    }

    // Find least-loaded CPU in affinity mask
    u32 best_cpu = current_cpu;
    u32 min_load = 0xFFFFFFFF;

    for (u32 i = 0; i < cpu::MAX_CPUS; i++) {
        if (!(affinity & (1u << i)))
            continue;
        if (!per_cpu_sched[i].initialized)
            continue;

        u32 load = __atomic_load_n(&per_cpu_sched[i].queue_count, __ATOMIC_RELAXED);
        if (load < min_load) {
            min_load = load;
            best_cpu = i;
        }
    }

    return best_cpu;
}

/**
 * @brief Check if any tasks are ready on a specific CPU (lock-free fast path).
 * @param cpu_id CPU to check
 * @return true if at least one task is ready.
 */
[[maybe_unused]]
bool any_ready_lockfree(u32 cpu_id) {
    if (cpu_id >= cpu::MAX_CPUS || !per_cpu_sched[cpu_id].initialized) {
        return false;
    }
    return __atomic_load_n(&per_cpu_sched[cpu_id].queue_count, __ATOMIC_RELAXED) > 0;
}

/**
 * @brief Enqueue a task on a specific CPU's scheduler.
 *
 * Routes the task to the appropriate data structure based on scheduling policy:
 * - SCHED_DEADLINE: deadline_heap (min-heap by deadline)
 * - SCHED_OTHER: cfs_heap (min-heap by vruntime)
 * - SCHED_FIFO/RR: rt_queues (linked list for FIFO ordering)
 *
 * @note Caller must hold per_cpu_sched[cpu_id].lock
 * @param t Task to enqueue
 * @param cpu_id Target CPU
 */
void enqueue_percpu_locked(task::Task *t, u32 cpu_id) {
    if (!t || cpu_id >= cpu::MAX_CPUS)
        return;

    PerCpuScheduler &sched = per_cpu_sched[cpu_id];
    if (!sched.initialized) {
        serial::puts("[sched] WARNING: enqueue to uninitialized CPU ");
        serial::put_dec(cpu_id);
        serial::puts("\n");
        return;
    }

    // State validation: only Ready or Running tasks should be enqueued
    if (t->state != task::TaskState::Ready && t->state != task::TaskState::Running) {
        return;
    }

    // Defensive: ensure task is not already in a heap (for heap-scheduled tasks)
    if ((t->policy == task::SchedPolicy::SCHED_DEADLINE ||
         t->policy == task::SchedPolicy::SCHED_OTHER) &&
        t->heap_index != static_cast<u32>(-1)) {
        return;
    }

    bool inserted = false;

    switch (t->policy) {
        case task::SchedPolicy::SCHED_DEADLINE:
            inserted = sched::heap_insert(&sched.deadline_heap, t);
            if (!inserted) {
                serial::puts("[sched] WARNING: heap_insert failed for deadline task '");
                serial::puts(t->name);
                serial::puts("'\n");
                return;
            }
            break;

        case task::SchedPolicy::SCHED_OTHER: {
            // CFS scheduling: use vruntime-based heap for fair scheduling

            // Sleeper fairness: if task slept and fell behind min_vruntime,
            // normalize up to prevent it from stealing CPU time from tasks
            // that have been running.
            // Skip idle task - it should always have low vruntime as fallback.
            if (!(t->flags & task::TASK_FLAG_IDLE)) {
#ifdef VIPER_SCHED_DEBUG
                // Debug: track displayd vruntime
                // Names are paths like "/sys/displayd.sys" - check for substring
                bool is_displayd = lib::strcontains(t->name, "displayd");
                static u32 displayd_enq = 0;
#endif

                if (t->vruntime < sched.min_vruntime) {
#ifdef VIPER_SCHED_DEBUG
                    // Debug: show vruntime normalization for new/waking tasks
                    if (t->vruntime == 0) {
                        // Brand new task - show the bump
                        serial::puts("[cfs] NEW '");
                        serial::puts(t->name);
                        serial::puts("' vrt 0 -> ");
                        serial::put_dec(static_cast<u32>(sched.min_vruntime / 1000000));
                        serial::puts("M\n");
                    }
                    if (is_displayd) {
                        displayd_enq++;
                        if (displayd_enq <= 10 || (displayd_enq % 1000 == 0)) {
                            serial::puts("[cfs] displayd enq#");
                            serial::put_dec(displayd_enq);
                            serial::puts(" vrt ");
                            serial::put_dec(static_cast<u32>(t->vruntime / 1000000));
                            serial::puts("M -> ");
                            serial::put_dec(static_cast<u32>(sched.min_vruntime / 1000000));
                            serial::puts("M\n");
                        }
                    }
#endif
                    t->vruntime = sched.min_vruntime;
                }
#ifdef VIPER_SCHED_DEBUG
                else if (is_displayd) {
                    displayd_enq++;
                    if (displayd_enq <= 10 || (displayd_enq % 1000 == 0)) {
                        serial::puts("[cfs] displayd enq#");
                        serial::put_dec(displayd_enq);
                        serial::puts(" vrt ");
                        serial::put_dec(static_cast<u32>(t->vruntime / 1000000));
                        serial::puts("M (min=");
                        serial::put_dec(static_cast<u32>(sched.min_vruntime / 1000000));
                        serial::puts("M)\n");
                    }
                }
#endif
                // Note: We do NOT cap high vruntime down. High vruntime means
                // the task ran a lot and should wait - that's fair scheduling.
            }

            inserted = sched::heap_insert(&sched.cfs_heap, t);
            if (!inserted) {
                serial::puts("[sched] WARNING: heap_insert failed for CFS task '");
                serial::puts(t->name);
                serial::puts("'\n");
                return;
            }

            // Track CFS task count
            if (!(t->flags & task::TASK_FLAG_IDLE)) {
                sched.cfs_nr_running++;
            }
            break;
        }

        case task::SchedPolicy::SCHED_FIFO:
        case task::SchedPolicy::SCHED_RR: {
            // RT tasks: add to tail of priority queue (FIFO ordering)
            u8 q_idx = priority_to_queue(t->priority);
            auto &queue = sched.rt_queues[q_idx];

            t->next = nullptr;
            t->prev = queue.tail;
            if (queue.tail) {
                queue.tail->next = t;
            } else {
                queue.head = t;
            }
            queue.tail = t;
            sched.rt_bitmap |= (1u << q_idx);
            inserted = true;
            break;
        }
    }

    if (inserted) {
        t->state = task::TaskState::Ready;
        sched.total_tasks++;
        __atomic_fetch_add(&sched.queue_count, 1, __ATOMIC_RELAXED);
    }
}

// =============================================================================
// Task Selection Algorithm (Per-CPU Heap Architecture)
// =============================================================================
//
// Each CPU has its own scheduling structures:
// - deadline_heap: SCHED_DEADLINE tasks (min-heap by deadline)
// - cfs_heap: SCHED_OTHER tasks (min-heap by vruntime)
// - rt_queues[8]: SCHED_FIFO/RR tasks (linked lists for FIFO ordering)
//
// Priority order: SCHED_DEADLINE > SCHED_FIFO/RR > SCHED_OTHER
//
// Since tasks are routed to the correct CPU on enqueue, dequeue does NOT
// need to check CPU affinity - all tasks on a CPU can run on that CPU.
// =============================================================================

/**
 * @brief Dequeue the highest priority task from a CPU's scheduler.
 *
 * Selection order:
 * 1. SCHED_DEADLINE: earliest deadline first (from deadline_heap)
 * 2. SCHED_FIFO/RR: highest priority first, FIFO within priority (from rt_queues)
 * 3. SCHED_OTHER: lowest vruntime first (from cfs_heap)
 *
 * @note Caller must hold per_cpu_sched[cpu_id].lock
 * @note No CPU affinity checks - tasks are already on the correct CPU
 * @param cpu_id CPU to dequeue from
 * @return Next task to run, or nullptr if no runnable tasks
 */
task::Task *dequeue_percpu_locked(u32 cpu_id) {
    if (cpu_id >= cpu::MAX_CPUS)
        return nullptr;

    PerCpuScheduler &sched = per_cpu_sched[cpu_id];
    if (!sched.initialized)
        return nullptr;

    task::Task *t = nullptr;

    // Priority 1: SCHED_DEADLINE (earliest deadline first)
    if (!sched::heap_empty(&sched.deadline_heap)) {
        t = sched::heap_extract_min(&sched.deadline_heap);
        if (t) {
            sched.total_tasks--;
            __atomic_fetch_sub(&sched.queue_count, 1, __ATOMIC_RELAXED);
            return t;
        }
    }

    // Priority 2: RT tasks (SCHED_FIFO/RR) - scan from highest priority
    if (sched.rt_bitmap) {
        for (u8 i = 0; i < task::NUM_PRIORITY_QUEUES; i++) {
            if (!(sched.rt_bitmap & (1u << i)))
                continue;

            auto &queue = sched.rt_queues[i];
            t = queue.head;
            if (t) {
                // Remove from head (FIFO)
                queue.head = t->next;
                if (queue.head) {
                    queue.head->prev = nullptr;
                } else {
                    queue.tail = nullptr;
                    sched.rt_bitmap &= ~(1u << i);
                }
                t->next = nullptr;
                t->prev = nullptr;

                sched.total_tasks--;
                __atomic_fetch_sub(&sched.queue_count, 1, __ATOMIC_RELAXED);
                return t;
            }
        }
    }

    // Priority 3: SCHED_OTHER (CFS - lowest vruntime first)
    if (!sched::heap_empty(&sched.cfs_heap)) {
        t = sched::heap_extract_min(&sched.cfs_heap);
        if (t) {
            // Track CFS task count
            if (!(t->flags & task::TASK_FLAG_IDLE)) {
                sched.cfs_nr_running--;

#ifdef VIPER_SCHED_DEBUG
                // Debug: track key task dequeues
                // Names are paths like "/sys/displayd.sys" - check for substring
                bool is_displayd = lib::strcontains(t->name, "displayd");
                bool is_workbench = lib::strcontains(t->name, "workbench");
                static u32 displayd_deq = 0;
                static u32 workbench_deq = 0;
                static u32 total_deq = 0;
                total_deq++;

                if (is_displayd) {
                    displayd_deq++;
                    if (displayd_deq <= 10 || (displayd_deq % 1000 == 0)) {
                        serial::puts("[cfs] displayd deq#");
                        serial::put_dec(displayd_deq);
                        serial::puts(" vrt=");
                        serial::put_dec(static_cast<u32>(t->vruntime / 1000000));
                        serial::puts("M min=");
                        serial::put_dec(static_cast<u32>(sched.min_vruntime / 1000000));
                        serial::puts("M\n");
                    }
                } else if (is_workbench) {
                    workbench_deq++;
                    if (workbench_deq <= 10 || (workbench_deq % 1000 == 0)) {
                        serial::puts("[cfs] workbench deq#");
                        serial::put_dec(workbench_deq);
                        serial::puts(" vrt=");
                        serial::put_dec(static_cast<u32>(t->vruntime / 1000000));
                        serial::puts("M min=");
                        serial::put_dec(static_cast<u32>(sched.min_vruntime / 1000000));
                        serial::puts("M\n");
                    }
                }

                // Periodic summary of dequeue counts
                if (total_deq == 100 || total_deq == 1000 || total_deq % 5000 == 0) {
                    serial::puts("[cfs] deq summary: disp=");
                    serial::put_dec(displayd_deq);
                    serial::puts(" work=");
                    serial::put_dec(workbench_deq);
                    serial::puts(" total=");
                    serial::put_dec(total_deq);
                    serial::puts("\n");
                }
#endif

                // Advance min_vruntime as this task is about to run
                // This ensures min_vruntime monotonically increases
                advance_min_vruntime(sched, t->vruntime);
            }

            sched.total_tasks--;
            __atomic_fetch_sub(&sched.queue_count, 1, __ATOMIC_RELAXED);
            return t;
        }
    }

    return nullptr;
}

/**
 * @brief Try to steal a task from another CPU's scheduler.
 *
 * Steals from the most-loaded CPU that has 2+ tasks. Only steals CFS and
 * deadline tasks - RT tasks have stricter timing requirements.
 *
 * @param current_cpu CPU that needs work
 * @return Stolen task, or nullptr if none available
 */
task::Task *steal_task(u32 current_cpu) {
    u32 cpu_mask = (1u << current_cpu);

    // Find most-loaded CPU to steal from
    u32 victim_cpu = current_cpu;
    u32 max_load = 0;

    for (u32 i = 0; i < cpu::MAX_CPUS; i++) {
        if (i == current_cpu)
            continue;
        if (!per_cpu_sched[i].initialized)
            continue;

        u32 load = __atomic_load_n(&per_cpu_sched[i].queue_count, __ATOMIC_RELAXED);
        if (load > max_load && load >= 2) { // Only steal if victim has 2+ tasks
            max_load = load;
            victim_cpu = i;
        }
    }

    if (victim_cpu == current_cpu)
        return nullptr;

    PerCpuScheduler &victim = per_cpu_sched[victim_cpu];

    // Try to acquire victim's lock without blocking
    u64 saved_daif;
    if (!victim.lock.try_acquire(saved_daif))
        return nullptr;

    task::Task *stolen = nullptr;

    // Try to steal a CFS task (most common, least time-critical)
    if (!sched::heap_empty(&victim.cfs_heap)) {
        // Peek and check affinity before extracting
        task::Task *candidate = sched::heap_peek(&victim.cfs_heap);
        if (candidate && (candidate->cpu_affinity & cpu_mask)) {
            stolen = sched::heap_extract_min(&victim.cfs_heap);
            victim.total_tasks--;
            __atomic_fetch_sub(&victim.queue_count, 1, __ATOMIC_RELAXED);
            victim.migrations++;
        }
    }

    // If no CFS task, try deadline (less common to steal)
    if (!stolen && !sched::heap_empty(&victim.deadline_heap)) {
        task::Task *candidate = sched::heap_peek(&victim.deadline_heap);
        if (candidate && (candidate->cpu_affinity & cpu_mask)) {
            stolen = sched::heap_extract_min(&victim.deadline_heap);
            victim.total_tasks--;
            __atomic_fetch_sub(&victim.queue_count, 1, __ATOMIC_RELAXED);
            victim.migrations++;
        }
    }

    // Don't steal RT tasks - they have stricter timing requirements

    victim.lock.release(saved_daif);

    if (stolen) {
        __atomic_fetch_add(&per_cpu_sched[current_cpu].steals, 1, __ATOMIC_RELAXED);
    }

    return stolen;
}

/**
 * @brief Check if any tasks are ready on a specific CPU (lock-free fast path).
 * @param cpu_id CPU to check
 * @return true if at least one task is ready on that CPU
 */
bool any_ready_percpu(u32 cpu_id) {
    if (cpu_id >= cpu::MAX_CPUS || !per_cpu_sched[cpu_id].initialized) {
        return false;
    }
    // Lock-free check using atomic counter
    return __atomic_load_n(&per_cpu_sched[cpu_id].queue_count, __ATOMIC_RELAXED) > 0;
}

} // namespace

/** @copydoc scheduler::init */
void init() {
    serial::puts("[sched] Initializing per-CPU heap scheduler\n");

    // Initialize per-CPU scheduler state
    for (u32 c = 0; c < cpu::MAX_CPUS; c++) {
        PerCpuScheduler &sched = per_cpu_sched[c];

        // Initialize heaps
        sched::heap_init(&sched.deadline_heap, sched::deadline_key);
        sched::heap_init(&sched.cfs_heap, sched::cfs_key);

        // Initialize RT queues
        for (u8 i = 0; i < task::NUM_PRIORITY_QUEUES; i++) {
            sched.rt_queues[i].head = nullptr;
            sched.rt_queues[i].tail = nullptr;
        }
        sched.rt_bitmap = 0;

        // Initialize stats
        sched.context_switches = 0;
        sched.total_tasks = 0;
        sched.steals = 0;
        sched.migrations = 0;
        sched.initialized = false;
        sched.queue_count = 0;
    }

    // Initialize boot CPU (CPU 0)
    per_cpu_sched[0].initialized = true;

    __atomic_store_n(&context_switch_count, 0, __ATOMIC_RELAXED);
    running = false;

    // Initialize idle state tracking
    idle::init();

    serial::puts("[sched] Per-CPU heap scheduler initialized\n");
}

/** @copydoc scheduler::init_cpu */
void init_cpu(u32 cpu_id) {
    if (cpu_id >= cpu::MAX_CPUS)
        return;

    PerCpuScheduler &sched = per_cpu_sched[cpu_id];

    // Initialize heaps
    sched::heap_init(&sched.deadline_heap, sched::deadline_key);
    sched::heap_init(&sched.cfs_heap, sched::cfs_key);

    // Initialize RT queues
    for (u8 i = 0; i < task::NUM_PRIORITY_QUEUES; i++) {
        sched.rt_queues[i].head = nullptr;
        sched.rt_queues[i].tail = nullptr;
    }
    sched.rt_bitmap = 0;

    sched.context_switches = 0;
    sched.total_tasks = 0;
    sched.steals = 0;
    sched.migrations = 0;
    sched.queue_count = 0;
    sched.min_vruntime = 0;
    sched.cfs_nr_running = 0;
    sched.initialized = true;

    serial::puts("[sched] CPU ");
    serial::put_dec(cpu_id);
    serial::puts(" scheduler initialized\n");
}

/** @copydoc scheduler::enqueue */
void enqueue(task::Task *t) {
    if (!t)
        return;

    // Select target CPU based on affinity and load
    u32 target_cpu = select_target_cpu(t);

    // Enqueue on target CPU
    SpinlockGuard guard(per_cpu_sched[target_cpu].lock);
    enqueue_percpu_locked(t, target_cpu);
}

/** @copydoc scheduler::dequeue */
task::Task *dequeue() {
    u32 cpu_id = cpu::current_id();

    // Try local CPU first
    {
        SpinlockGuard guard(per_cpu_sched[cpu_id].lock);
        task::Task *t = dequeue_percpu_locked(cpu_id);
        if (t)
            return t;
    }

    // Try work stealing from other CPUs
    return steal_task(cpu_id);
}

/** @copydoc scheduler::schedule */
void schedule() {
    task::Task *current = task::current();
    task::Task *next = nullptr;
    task::Task *old = nullptr;
    u32 cpu_id = cpu::current_id();

    PerCpuScheduler &sched = per_cpu_sched[cpu_id];

    // Acquire per-CPU lock for scheduling operations
    u64 saved_daif = sched.lock.acquire();

    // Try to dequeue from local CPU
    next = dequeue_percpu_locked(cpu_id);

    // If nothing local, try work stealing (need to release lock first)
    if (!next) {
        sched.lock.release(saved_daif);
        next = steal_task(cpu_id);

        if (!next) {
            // No work available, use idle task
            next = task::get_by_id(0);
            if (!next || next == current) {
                return; // Already idle
            }
        }

        // Re-acquire lock for the rest of scheduling
        saved_daif = sched.lock.acquire();
    }

    if (next == current) {
        if (current->state != task::TaskState::Running) {
            current->state = task::TaskState::Running;
        }
        sched.lock.release(saved_daif);
        return;
    }

    // Put current task back in ready queue if it's still runnable
    if (current) {
#ifdef VIPER_SCHED_DEBUG
        // Debug: check if displayd is being skipped
        bool is_displayd = lib::strcontains(current->name, "displayd");
        static u32 displayd_sched = 0;
        if (is_displayd) {
            displayd_sched++;
            if (displayd_sched <= 5 || (displayd_sched >= 26400 && displayd_sched < 26420)) {
                serial::puts("[sched] displayd #");
                serial::put_dec(displayd_sched);
                serial::puts(" state=");
                serial::put_dec(static_cast<u32>(current->state));
                serial::puts(" heap_idx=");
                serial::put_dec(current->heap_index);
                serial::puts("\n");
            }
        }
#endif

        if (current->state == task::TaskState::Running) {
            // Account for CPU time used (consumed time slice)
            u32 original_slice = task::time_slice_for_priority(current->priority);
            u64 ticks_used = original_slice - current->time_slice;
            current->cpu_ticks += ticks_used;

            current->state = task::TaskState::Ready;
            enqueue_percpu_locked(current, cpu_id);
        } else if (current->state == task::TaskState::Exited) {
            // Task exited - don't re-enqueue
            if (__atomic_load_n(&context_switch_count, __ATOMIC_RELAXED) <= 10) {
                serial::puts("[sched] Task '");
                serial::puts(current->name);
                serial::puts("' exited\n");
            }
        }
#ifdef VIPER_SCHED_DEBUG
        else if (is_displayd && displayd_sched >= 26400 && displayd_sched < 26420) {
            serial::puts("[sched] displayd NOT re-enqueued, state=");
            serial::put_dec(static_cast<u32>(current->state));
            serial::puts("\n");
        }
#endif
        // Blocked tasks are on wait queues, not re-enqueued here
    }

    // Validate next task state before switching
    if (next->state != task::TaskState::Ready && next != task::get_by_id(0)) {
        serial::puts("[sched] ERROR: next task '");
        serial::puts(next->name);
        serial::puts("' not Ready (state=");
        serial::put_dec(static_cast<u32>(next->state));
        serial::puts(")\n");
        sched.lock.release(saved_daif);
        return;
    }

    // Switch to next task
    next->state = task::TaskState::Running;

    // Set time slice based on scheduling policy
    if (next->policy == task::SchedPolicy::SCHED_DEADLINE) {
        next->time_slice = static_cast<u32>(next->dl_runtime / 1000000);
        if (next->time_slice == 0)
            next->time_slice = 1;
    } else if (next->policy == task::SchedPolicy::SCHED_FIFO) {
        next->time_slice = 0xFFFFFFFF;
    } else if (next->policy == task::SchedPolicy::SCHED_RR) {
        next->time_slice = task::RT_TIME_SLICE_DEFAULT;
    } else {
        next->time_slice = task::time_slice_for_priority(next->priority);
    }

    next->switch_count++;

    u64 switch_num = __atomic_fetch_add(&context_switch_count, 1, __ATOMIC_RELAXED) + 1;

    // Debug output (first 5 switches only)
    if (switch_num <= 5) {
        serial::puts("[sched] ");
        if (current) {
            serial::puts(current->name);
        } else {
            serial::puts("(none)");
        }
        serial::puts(" -> ");
        serial::puts(next->name);
        serial::puts("\n");
    }

    // Update current task pointer
    old = current;
    task::set_current(next);

    // Verify vinit's page tables before any context switch
    viper::debug_verify_vinit_tables("pre-context-switch");

    // Switch address space if the next task is a user task with a different viper
    if (next->viper) {
        viper::Viper *v = reinterpret_cast<viper::Viper *>(next->viper);

        // DEBUG: Check L1[2] and L2[0] before switching to this viper
        if (v->ttbr0) {
            u64 *l0 = reinterpret_cast<u64 *>(pmm::phys_to_virt(v->ttbr0));
            if (l0[0] & 0x1) { // VALID
                u64 *l1 = reinterpret_cast<u64 *>(pmm::phys_to_virt(l0[0] & ~0xFFFULL));
                if (!(l1[2] & 0x1)) { // L1[2] INVALID!
                    serial::puts("[sched] FATAL: L1[2] invalid for '");
                    serial::puts(next->name);
                    serial::puts("' L1[2]=");
                    serial::put_hex(l1[2]);
                    serial::puts("\n");
                    while (true)
                        asm volatile("wfe");
                } else {
                    u64 *l2 = reinterpret_cast<u64 *>(pmm::phys_to_virt(l1[2] & ~0xFFFULL));
                    if (!(l2[0] & 0x1)) { // L2[0] INVALID!
                        serial::puts("[sched] FATAL: L2[0] invalid for '");
                        serial::puts(next->name);
                        serial::puts("' L2[0]=");
                        serial::put_hex(l2[0]);
                        serial::puts(" L2_phys=");
                        serial::put_hex(l1[2] & ~0xFFFULL);
                        serial::puts("\n");
                        while (true)
                            asm volatile("wfe");
                    }
                }
            }
        }

        viper::switch_address_space(v->ttbr0, v->asid);
        viper::set_current(v);

        // Restore per-thread TLS pointer (TPIDR_EL0)
        asm volatile("msr tpidr_el0, %0" ::"r"(next->thread.tls_base));
    }

    // Release lock before context switch
    sched.lock.release(saved_daif);

    // Perform context switch
    if (old) {
        context_switch(&old->context, &next->context);
    } else {
        context_switch(&next->context, &next->context);
    }
}

// =============================================================================
// Preemption Logic
// =============================================================================
//
// Preemption occurs when a running task is interrupted to allow another task
// to run. The scheduler supports two types of preemption:
//
// 1. Priority Preemption
//    - A higher-priority task becomes ready (e.g., woken from sleep)
//    - Real-time tasks always preempt non-real-time tasks
//    - Checked on every timer tick by scanning higher-priority queues
//
// 2. Time Slice Preemption
//    - A task's time quantum expires
//    - Behavior depends on scheduling policy:
//      - SCHED_FIFO: Never preempted by time slice (runs until yield/block)
//      - SCHED_RR: Preempted when slice expires, moves to end of queue
//      - SCHED_OTHER: Preempted when slice expires, CFS selects next task
//
// Preemption Flow (on timer tick):
//
//   tick()
//     |
//     +--> Is current task idle? --> Yes --> Any ready task? --> Preempt
//     |
//     +--> Check queues 0 to (current_queue - 1) for ready tasks
//              |
//              +--> RT task ready & current is non-RT? --> Preempt
//              +--> Higher priority task ready? --> Preempt
//     |
//     +--> Decrement time_slice (unless SCHED_FIFO)
//              |
//              +--> time_slice == 0? --> Preempt (unless SCHED_FIFO)
//
// =============================================================================

/** @copydoc scheduler::tick */
void tick() {
    // Don't do anything until scheduler has started
    if (!running)
        return;

    task::Task *current = task::current();
    if (!current)
        return;

    bool need_schedule = false;
    u32 cpu_id = cpu::current_id();

    // Preempt idle task if something else is ready (lock-free check)
    if (current->flags & task::TASK_FLAG_IDLE) {
        if (any_ready_percpu(cpu_id)) {
            need_schedule = true;
        }
    } else {
        // Handle time slice based on scheduling policy
        if (current->policy == task::SchedPolicy::SCHED_FIFO) {
            // SCHED_FIFO: Never preempt on time slice (run until yield/block)
        } else if (current->policy == task::SchedPolicy::SCHED_RR) {
            // SCHED_RR: Round-robin with fixed RT time slice
            if (current->time_slice > 0) {
                current->time_slice--;
            }
            if (current->time_slice == 0) {
                need_schedule = true;
            }
        } else {
            // SCHED_OTHER: Normal time-sharing with CFS vruntime
            if (current->time_slice > 0) {
                current->time_slice--;

                // Update vruntime: 1 tick = 1ms = 1,000,000ns
                // Note: current task is NOT in a heap - vruntime will be used on re-enqueue
                u64 delta_ns = 1000000;
                current->vruntime += cfs::calc_vruntime_delta(delta_ns, current->nice);
            }
            if (current->time_slice == 0) {
                need_schedule = true;
            }
        }

        // Check if a deadline or RT task became ready (quick lock-free check)
        if (!need_schedule && any_ready_percpu(cpu_id)) {
            // There are tasks ready - check if we should preempt
            // For simplicity, preempt non-deadline tasks if deadline heap has work
            // or non-RT tasks if RT queue has work
            PerCpuScheduler &sched = per_cpu_sched[cpu_id];

            // Lock-free peek at deadline heap
            if (!sched::heap_empty(&sched.deadline_heap) &&
                current->policy != task::SchedPolicy::SCHED_DEADLINE) {
                need_schedule = true;
            }

            // Check RT bitmap if current is not RT
            if (!need_schedule && sched.rt_bitmap &&
                current->policy == task::SchedPolicy::SCHED_OTHER) {
                need_schedule = true;
            }
        }
    }

    // Schedule outside any lock (schedule() acquires its own lock)
    if (need_schedule) {
        schedule();
    }
}

/** @copydoc scheduler::preempt */
void preempt() {
    // Don't do anything until scheduler has started
    if (!running)
        return;

    task::Task *current = task::current();
    if (!current)
        return;

    // SCHED_FIFO tasks are never preempted by time slice expiry
    // They run until they voluntarily yield or block
    if (current->policy == task::SchedPolicy::SCHED_FIFO) {
        return;
    }

    // Check if time slice expired (read atomically)
    // No lock needed - time_slice is only modified by the owning task or tick()
    if (current->time_slice == 0) {
        schedule();
    }
}

/** @copydoc scheduler::start */
[[noreturn]] void start() {
    serial::puts("[sched] Starting scheduler\n");

    // Disable interrupts while setting up - prevents timer from
    // calling schedule() before we've switched to the first task
    asm volatile("msr daifset, #2"); // Mask IRQ

    running = true;

    // Get first task from ready queue
    task::Task *first = dequeue();
    if (!first) {
        // No tasks, run idle
        first = task::get_by_id(0);
    }

    if (!first) {
        serial::puts("[sched] PANIC: No tasks to run!\n");
        for (;;)
            asm volatile("wfi");
    }

    serial::puts("[sched] First task: ");
    serial::puts(first->name);
    serial::puts("\n");

    // Set as current and running
    first->state = task::TaskState::Running;

    // Set time slice based on scheduling policy
    if (first->policy == task::SchedPolicy::SCHED_DEADLINE) {
        first->time_slice = static_cast<u32>(first->dl_runtime / 1000000);
        if (first->time_slice == 0)
            first->time_slice = 1;
    } else if (first->policy == task::SchedPolicy::SCHED_FIFO) {
        first->time_slice = 0xFFFFFFFF;
    } else if (first->policy == task::SchedPolicy::SCHED_RR) {
        first->time_slice = task::RT_TIME_SLICE_DEFAULT;
    } else {
        first->time_slice = task::time_slice_for_priority(first->priority);
    }

    task::set_current(first);

    __atomic_fetch_add(&context_switch_count, 1, __ATOMIC_RELAXED);

    // Load the first task's context and jump to it
    // We create a dummy "old" context on the stack that we don't care about
    task::TaskContext dummy;

    // Re-enable interrupts just before switch
    // The new task will start with interrupts enabled
    asm volatile("msr daifclr, #2"); // Unmask IRQ

    context_switch(&dummy, &first->context);

    // Should never return
    serial::puts("[sched] PANIC: start() returned!\n");
    for (;;)
        asm volatile("wfi");
}

/** @copydoc scheduler::is_running */
bool is_running() {
    return running;
}

/** @copydoc scheduler::get_context_switches */
u64 get_context_switches() {
    return __atomic_load_n(&context_switch_count, __ATOMIC_RELAXED);
}

/** @copydoc scheduler::get_queue_length */
u32 get_queue_length(u8 queue_idx) {
    if (queue_idx >= task::NUM_PRIORITY_QUEUES)
        return 0;

    // Count RT queue length on current CPU
    u32 cpu_id = cpu::current_id();
    SpinlockGuard guard(per_cpu_sched[cpu_id].lock);

    u32 count = 0;
    task::Task *t = per_cpu_sched[cpu_id].rt_queues[queue_idx].head;
    while (t) {
        count++;
        t = t->next;
    }
    return count;
}

/** @copydoc scheduler::get_stats */
void get_stats(Stats *stats) {
    if (!stats)
        return;

    stats->context_switches = __atomic_load_n(&context_switch_count, __ATOMIC_RELAXED);
    stats->total_ready = 0;
    stats->blocked_tasks = 0;
    stats->exited_tasks = 0;

    // Initialize queue lengths to 0
    for (u8 i = 0; i < task::NUM_PRIORITY_QUEUES; i++) {
        stats->queue_lengths[i] = 0;
    }

    // Count tasks across all CPUs
    for (u32 c = 0; c < cpu::MAX_CPUS; c++) {
        if (!per_cpu_sched[c].initialized)
            continue;

        SpinlockGuard guard(per_cpu_sched[c].lock);
        PerCpuScheduler &sched = per_cpu_sched[c];

        // Count heap tasks
        stats->total_ready += sched.deadline_heap.size;
        stats->total_ready += sched.cfs_heap.size;

        // Count RT queue tasks
        for (u8 i = 0; i < task::NUM_PRIORITY_QUEUES; i++) {
            task::Task *t = sched.rt_queues[i].head;
            while (t) {
                stats->queue_lengths[i]++;
                stats->total_ready++;
                t = t->next;
            }
        }
    }

    // Count blocked and exited tasks by scanning task table
    for (u32 i = 0; i < task::MAX_TASKS; i++) {
        task::Task *t = task::get_by_id(i);
        if (t) {
            if (t->state == task::TaskState::Blocked)
                stats->blocked_tasks++;
            else if (t->state == task::TaskState::Exited)
                stats->exited_tasks++;
        }
    }
}

/** @copydoc scheduler::dump_stats */
void dump_stats() {
    Stats stats;
    get_stats(&stats);

    serial::puts("\n=== Scheduler Statistics ===\n");
    serial::puts("Context switches: ");
    serial::put_dec(stats.context_switches);
    serial::puts("\n");

    serial::puts("Ready queues:\n");
    for (u8 i = 0; i < task::NUM_PRIORITY_QUEUES; i++) {
        serial::puts("  Queue ");
        serial::put_dec(i);
        serial::puts(" (pri ");
        serial::put_dec(i * task::PRIORITIES_PER_QUEUE);
        serial::puts("-");
        serial::put_dec((i + 1) * task::PRIORITIES_PER_QUEUE - 1);
        serial::puts("): ");
        serial::put_dec(stats.queue_lengths[i]);
        serial::puts(" tasks, slice=");
        serial::put_dec(task::TIME_SLICE_BY_QUEUE[i]);
        serial::puts("ms\n");
    }

    serial::puts("Total ready: ");
    serial::put_dec(stats.total_ready);
    serial::puts(", Blocked: ");
    serial::put_dec(stats.blocked_tasks);
    serial::puts(", Exited: ");
    serial::put_dec(stats.exited_tasks);
    serial::puts("\n===========================\n");
}

/** @copydoc scheduler::enqueue_on_cpu */
void enqueue_on_cpu(task::Task *t, u32 cpu_id) {
    if (!t || cpu_id >= cpu::MAX_CPUS)
        return;

    u32 current_cpu = cpu::current_id();

    // Ensure target CPU is initialized, fall back to current CPU if not
    if (!per_cpu_sched[cpu_id].initialized) {
        cpu_id = current_cpu;
    }

    // Enqueue on target CPU
    SpinlockGuard guard(per_cpu_sched[cpu_id].lock);
    enqueue_percpu_locked(t, cpu_id);

    // If enqueuing to a different CPU, send an IPI to trigger reschedule
    if (cpu_id != current_cpu) {
        cpu::send_ipi(cpu_id, cpu::ipi::RESCHEDULE);
    }
}

/** @copydoc scheduler::get_percpu_stats */
void get_percpu_stats(u32 cpu_id, PerCpuStats *stats) {
    if (!stats || cpu_id >= cpu::MAX_CPUS)
        return;

    if (!per_cpu_sched[cpu_id].initialized) {
        stats->context_switches = 0;
        stats->queue_length = 0;
        stats->steals = 0;
        stats->migrations = 0;
        return;
    }

    SpinlockGuard guard(per_cpu_sched[cpu_id].lock);
    stats->context_switches = per_cpu_sched[cpu_id].context_switches;
    stats->queue_length = per_cpu_sched[cpu_id].total_tasks;
    stats->steals = per_cpu_sched[cpu_id].steals;
    stats->migrations = per_cpu_sched[cpu_id].migrations;
}

/** @copydoc scheduler::balance_load */
void balance_load() {
    u32 current_cpu = cpu::current_id();

    // Only run load balancing periodically (atomic increment for SMP safety)
    u32 counter = __atomic_fetch_add(&load_balance_counter, 1, __ATOMIC_RELAXED) + 1;
    if (counter < LOAD_BALANCE_INTERVAL)
        return;
    __atomic_store_n(&load_balance_counter, 0, __ATOMIC_RELAXED);

    // Find the most and least loaded CPUs
    u32 max_load = 0, min_load = 0xFFFFFFFF;
    u32 max_cpu = current_cpu, min_cpu = current_cpu;

    for (u32 i = 0; i < cpu::MAX_CPUS; i++) {
        if (!per_cpu_sched[i].initialized)
            continue;

        u32 load = per_cpu_sched[i].total_tasks;
        if (load > max_load) {
            max_load = load;
            max_cpu = i;
        }
        if (load < min_load) {
            min_load = load;
            min_cpu = i;
        }
    }

    // Migrate tasks if imbalance is significant (>2 tasks difference)
    if (max_load > min_load + 2 && max_cpu != min_cpu) {
        // Try to steal a task from the overloaded CPU
        task::Task *stolen = steal_task(min_cpu);
        if (stolen) {
            enqueue_on_cpu(stolen, min_cpu);
        }
    }
}

} // namespace scheduler
