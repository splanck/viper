//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#include "poll.hpp"
#include "../arch/aarch64/timer.hpp"
#include "../cap/table.hpp"
#include "../console/serial.hpp"
#include "../kobj/channel.hpp"
#include "../lib/spinlock.hpp"
#include "../lib/timerwheel.hpp"
#include "../sched/scheduler.hpp"
#include "../sched/task.hpp"
#include "../tty/tty.hpp"
#include "../viper/viper.hpp"
#include "channel.hpp"

/**
 * @file poll.cpp
 * @brief Polling and timer implementation for cooperative scheduling.
 *
 * @details
 * This translation unit implements the timer table and the `poll()` loop used
 * to wait for readiness conditions. The current approach is deliberately
 * simple: it periodically checks conditions and yields between checks.
 *
 * Timers are stored as absolute expiration times in milliseconds based on the
 * system tick counter.
 */
namespace poll {

// Timer structure
/**
 * @brief Internal one-shot timer representation.
 *
 * @details
 * Each timer entry records:
 * - A unique ID exposed to callers as the timer handle.
 * - An absolute expiration time in milliseconds.
 * - A pointer to a task waiting on the timer (for sleep semantics).
 */
struct Timer {
    u32 id;
    u64 expire_time; // Absolute time in ms when timer expires
    bool active;
    task::Task *waiter; // Task waiting on this timer
};

// Timer table
constexpr u32 MAX_TIMERS = 32;
static Timer timers[MAX_TIMERS];
static u32 next_timer_id = 1;

/**
 * @brief Wait queue entry for event notification.
 *
 * @details
 * Records a task waiting on a specific handle for specific events.
 * For channel handles, we also store the channel_id for matching
 * since notify_handle is called with channel_id, not capability handle.
 */
struct WaitEntry {
    task::Task *task; // Waiting task
    u32 handle;       // Handle being waited on (capability handle)
    u32 channel_id;   // Resolved channel ID (for matching notify_handle)
    EventType events; // Events being waited for
    bool active;      // Entry is in use
};

// Wait queue table
constexpr u32 MAX_WAIT_ENTRIES = 32;
static WaitEntry wait_queue[MAX_WAIT_ENTRIES];

// Spinlock protecting timers[], wait_queue[], and next_timer_id
static Spinlock poll_lock;

/** @copydoc poll::init */
void init() {
    serial::puts("[poll] Initializing poll subsystem\n");

    for (u32 i = 0; i < MAX_TIMERS; i++) {
        timers[i].id = 0;
        timers[i].active = false;
        timers[i].waiter = nullptr;
    }

    for (u32 i = 0; i < MAX_WAIT_ENTRIES; i++) {
        wait_queue[i].task = nullptr;
        wait_queue[i].channel_id = 0xFFFFFFFF;
        wait_queue[i].active = false;
    }

    // Initialize the timer wheel for O(1) timeout management
    timerwheel::init(timer::get_ticks());

    serial::puts("[poll] Poll subsystem initialized\n");
}

/** @copydoc poll::time_now_ms */
u64 time_now_ms() {
    return timer::get_ticks();
}

/**
 * @brief Find an active timer by ID.
 *
 * @param timer_id Timer handle.
 * @return Pointer to the timer entry, or `nullptr` if not found/active.
 */
static Timer *find_timer(u32 timer_id) {
    for (u32 i = 0; i < MAX_TIMERS; i++) {
        if (timers[i].id == timer_id && timers[i].active) {
            return &timers[i];
        }
    }
    return nullptr;
}

/**
 * @brief Allocate an unused timer slot from the timer table.
 *
 * @return Pointer to a free timer entry, or `nullptr` if table is full.
 */
static Timer *alloc_timer() {
    for (u32 i = 0; i < MAX_TIMERS; i++) {
        if (!timers[i].active) {
            return &timers[i];
        }
    }
    return nullptr;
}

/** @copydoc poll::timer_create */
i64 timer_create(u64 timeout_ms) {
    static u32 create_count = 0;

    u64 saved_daif = poll_lock.acquire();

    Timer *t = alloc_timer();
    if (!t) {
        poll_lock.release(saved_daif);
        serial::puts("[poll] timer_create FAILED: no free slots\n");
        return error::VERR_OUT_OF_MEMORY;
    }

    t->id = next_timer_id++;
    t->expire_time = time_now_ms() + timeout_ms;
    t->active = true;
    t->waiter = nullptr;

    u32 id = t->id;
    create_count++;

    // Debug: print occasionally
    if (create_count <= 5 || (create_count >= 40 && create_count <= 50)) {
        serial::puts("[poll] timer_create id=");
        serial::put_dec(id);
        serial::puts(" expire=");
        serial::put_dec(t->expire_time);
        serial::puts(" create#");
        serial::put_dec(create_count);
        serial::puts("\n");
    }

    poll_lock.release(saved_daif);
    return static_cast<i64>(id);
}

/** @copydoc poll::timer_expired */
bool timer_expired(u32 timer_id) {
    u64 saved_daif = poll_lock.acquire();

    Timer *t = find_timer(timer_id);
    if (!t) {
        poll_lock.release(saved_daif);
        return true; // Non-existent timer is "expired"
    }
    bool expired = time_now_ms() >= t->expire_time;
    poll_lock.release(saved_daif);
    return expired;
}

/** @copydoc poll::timer_cancel */
i64 timer_cancel(u32 timer_id) {
    u64 saved_daif = poll_lock.acquire();

    Timer *t = find_timer(timer_id);
    if (!t) {
        poll_lock.release(saved_daif);
        return error::VERR_NOT_FOUND;
    }

    // Capture waiter before clearing timer
    task::Task *waiter = t->waiter;

    t->waiter = nullptr;
    t->active = false;
    t->id = 0;

    poll_lock.release(saved_daif);

    // Wake up any waiter (outside lock to avoid nested lock issues).
    // Only wake if the waiter is actually blocked - if the task exited the
    // sleep loop due to timer_expired() check, it may already be Running.
    if (waiter && waiter->state == task::TaskState::Blocked) {
        waiter->state = task::TaskState::Ready;
        scheduler::enqueue(waiter);
    }

    return error::VOK;
}

/** @copydoc poll::sleep_ms */
i64 sleep_ms(u64 ms) {
    if (ms == 0) {
        return error::VOK;
    }

    // Create a timer (acquires lock internally)
    i64 timer_result = timer_create(ms);
    if (timer_result < 0) {
        return timer_result;
    }
    u32 timer_id = static_cast<u32>(timer_result);

    // Block until timer expires
    task::Task *current = task::current();
    if (!current) {
        // No current task (shouldn't happen)
        timer_cancel(timer_id);
        return error::VERR_UNKNOWN;
    }

    // Wait for timer using proper sleep/wakeup protocol to avoid lost wakeups.
    // The key invariant is: state must be set to Blocked BEFORE releasing the lock
    // that protects the waiter registration. Otherwise, the waker can see the waiter
    // and set state to Ready, which then gets overwritten back to Blocked.
    while (true) {
        // Check under lock if timer expired - this avoids TOCTOU race with check_timers
        u64 saved_daif = poll_lock.acquire();
        Timer *t = find_timer(timer_id);

        if (!t) {
            poll_lock.release(saved_daif);
            break; // Timer was cancelled or already cleaned up
        }

        u64 now = time_now_ms();
        if (now >= t->expire_time) {
            poll_lock.release(saved_daif);
            break; // Timer expired
        }

        // Register as waiter AND set state to Blocked while holding lock.
        // This ensures that if check_timers() sees us as a waiter, we are
        // guaranteed to be in Blocked state and the Ready transition is valid.
        t->waiter = current;
        current->state = task::TaskState::Blocked;
        poll_lock.release(saved_daif);

        task::yield();
        // Loop will re-check timer expiration
    }

    // Clean up timer (acquires lock internally)
    timer_cancel(timer_id);

    return error::VOK;
}

/** @copydoc poll::poll */
i64 poll(PollEvent *events, u32 count, i64 timeout_ms) {
    if (!events || count == 0 || count > MAX_POLL_EVENTS) {
        return error::VERR_INVALID_ARG;
    }

    u64 deadline = 0;
    if (timeout_ms > 0) {
        deadline = time_now_ms() + static_cast<u64>(timeout_ms);
    }

    // Poll loop
    while (true) {
        u32 ready_count = 0;

        // Check each event
        for (u32 i = 0; i < count; i++) {
            // Clear triggered output field (preserve input events!)
            events[i].triggered = EventType::NONE;

            // Read requested events from input (NOT the cleared field!)
            EventType requested = events[i].events;
            u32 handle = events[i].handle;

            // Check for channel read readiness
            if (has_event(requested, EventType::CHANNEL_READ)) {
                if (channel::has_message(handle)) {
                    events[i].triggered = events[i].triggered | EventType::CHANNEL_READ;
                    ready_count++;
                }
            }

            // Check for channel write readiness (has space for more messages)
            if (has_event(requested, EventType::CHANNEL_WRITE)) {
                if (channel::has_space(handle)) {
                    events[i].triggered = events[i].triggered | EventType::CHANNEL_WRITE;
                    ready_count++;
                }
            }

            // Check for timer expiry
            if (has_event(requested, EventType::TIMER)) {
                if (timer_expired(handle)) {
                    events[i].triggered = events[i].triggered | EventType::TIMER;
                    ready_count++;
                }
            }

            // Check for console input (kernel TTY buffer)
            if (handle == HANDLE_CONSOLE_INPUT && has_event(requested, EventType::CONSOLE_INPUT)) {
                if (tty::has_input()) {
                    events[i].triggered = events[i].triggered | EventType::CONSOLE_INPUT;
                    ready_count++;
                }
            }

            // Network RX events removed - networking handled in kernel
        }

        // Return if any events are ready
        if (ready_count > 0) {
            return static_cast<i64>(ready_count);
        }

        // Non-blocking mode: return immediately
        if (timeout_ms == 0) {
            return 0;
        }

        // Check timeout
        if (timeout_ms > 0 && time_now_ms() >= deadline) {
            return 0;
        }

        // Yield and try again (or busy-wait if scheduler not running)
        if (scheduler::is_running()) {
            task::yield();
        } else {
            // Before scheduler starts, busy-wait with small delay
            // This allows pre-scheduler tests to work
            timer::delay_us(100);
        }
    }
}

/** @copydoc poll::check_timers */
void check_timers() {
    u64 now = time_now_ms();

    // NOTE: The timer wheel is not currently used - poll::timer_create() uses
    // the simple timer table above. Calling timerwheel::tick() was causing
    // panics due to interaction with heap corruption. Disabled until the
    // timer systems are unified.
    // timerwheel::tick(now);

    // Collect expired timer waiters under lock, then wake outside lock
    task::Task *waiters_to_wake[MAX_TIMERS];
    u32 wake_count = 0;

    u64 saved_daif = poll_lock.acquire();
    for (u32 i = 0; i < MAX_TIMERS; i++) {
        if (timers[i].active && timers[i].waiter && now >= timers[i].expire_time) {
            waiters_to_wake[wake_count++] = timers[i].waiter;
            // Fully deactivate expired timer to free the slot.
            // The woken task will call timer_cancel() but that's now a no-op.
            timers[i].waiter = nullptr;
            timers[i].active = false;
            timers[i].id = 0;
        }
    }
    poll_lock.release(saved_daif);

    // Wake all expired timer waiters outside lock.
    // Only wake tasks that are actually blocked to avoid corrupting Running tasks.
    for (u32 i = 0; i < wake_count; i++) {
        task::Task *waiter = waiters_to_wake[i];
        if (waiter->state == task::TaskState::Blocked) {
            // DEBUG: track timer wakeups
            static u32 wake_debug_count = 0;
            wake_debug_count++;
            if (wake_debug_count <= 10) {
                serial::puts("[poll] waking '");
                serial::puts(waiter->name);
                serial::puts("' heap_idx=");
                serial::put_dec(waiter->heap_index);
                serial::puts("\n");
            }

            waiter->state = task::TaskState::Ready;
            scheduler::enqueue(waiter);
        }
    }
}

/** @copydoc poll::register_wait */
void register_wait(u32 handle, EventType events) {
    task::Task *current = task::current();
    if (!current)
        return;

    // Resolve capability handle to channel_id for proper notification matching
    // (Done outside lock since cap table has its own synchronization)
    u32 channel_id = 0xFFFFFFFF;
    if (has_event(events, EventType::CHANNEL_READ) || has_event(events, EventType::CHANNEL_WRITE)) {
        cap::Table *ct = viper::current_cap_table();
        if (ct) {
            cap::Entry *entry = ct->get(handle);
            if (entry && entry->kind == cap::Kind::Channel) {
                kobj::Channel *ch = static_cast<kobj::Channel *>(entry->object);
                if (ch) {
                    channel_id = ch->id();
                }
            }
        }
    }

    // Find an empty slot under lock
    u64 saved_daif = poll_lock.acquire();
    for (u32 i = 0; i < MAX_WAIT_ENTRIES; i++) {
        if (!wait_queue[i].active) {
            wait_queue[i].task = current;
            wait_queue[i].handle = handle;
            wait_queue[i].channel_id = channel_id;
            wait_queue[i].events = events;
            wait_queue[i].active = true;
            poll_lock.release(saved_daif);
            return;
        }
    }
    poll_lock.release(saved_daif);
}

/** @copydoc poll::notify_handle */
void notify_handle(u32 handle, EventType events) {
    // 'handle' here is actually a channel_id from channel::try_send/try_recv
    // Match against stored channel_id (resolved from capability handles)
    // Collect waiters under lock, wake them outside lock
    task::Task *waiters_to_wake[MAX_WAIT_ENTRIES];
    u32 wake_count = 0;

    u64 saved_daif = poll_lock.acquire();
    for (u32 i = 0; i < MAX_WAIT_ENTRIES; i++) {
        if (wait_queue[i].active &&
            (wait_queue[i].handle == handle || wait_queue[i].channel_id == handle)) {
            // Check if any requested events match
            if (has_event(wait_queue[i].events, events)) {
                task::Task *waiter = wait_queue[i].task;
                wait_queue[i].active = false;
                wait_queue[i].task = nullptr;

                if (waiter && waiter->state == task::TaskState::Blocked) {
                    waiters_to_wake[wake_count++] = waiter;
                }
            }
        }
    }
    poll_lock.release(saved_daif);

    // Wake waiters outside lock
    for (u32 i = 0; i < wake_count; i++) {
        waiters_to_wake[i]->state = task::TaskState::Ready;
        scheduler::enqueue(waiters_to_wake[i]);
    }
}

/** @copydoc poll::unregister_wait */
void unregister_wait() {
    task::Task *current = task::current();
    if (!current)
        return;

    u64 saved_daif = poll_lock.acquire();
    for (u32 i = 0; i < MAX_WAIT_ENTRIES; i++) {
        if (wait_queue[i].active && wait_queue[i].task == current) {
            wait_queue[i].active = false;
            wait_queue[i].task = nullptr;
        }
    }
    poll_lock.release(saved_daif);
}

/** @copydoc poll::clear_task_waiters */
void clear_task_waiters(task::Task *t) {
    if (!t)
        return;

    u64 saved_daif = poll_lock.acquire();

    // Clear and deactivate all timers waiting on this task.
    // Previously we only cleared the waiter, leaving the timer active
    // but orphaned. This caused timer slot leaks when tasks exited
    // while blocked on timers.
    for (u32 i = 0; i < MAX_TIMERS; i++) {
        if (timers[i].active && timers[i].waiter == t) {
            timers[i].waiter = nullptr;
            timers[i].active = false;
            timers[i].id = 0;
        }
    }

    // Clear all wait queue entries for this task
    for (u32 i = 0; i < MAX_WAIT_ENTRIES; i++) {
        if (wait_queue[i].active && wait_queue[i].task == t) {
            wait_queue[i].active = false;
            wait_queue[i].task = nullptr;
        }
    }

    poll_lock.release(saved_daif);
}

/** @copydoc poll::register_timer_wait_and_block */
void register_timer_wait_and_block(u32 timer_id) {
    task::Task *current = task::current();
    if (!current)
        return;

    u64 saved_daif = poll_lock.acquire();

    Timer *t = find_timer(timer_id);
    if (t) {
        // Register as timer waiter AND set state to Blocked atomically.
        // This ensures check_timers() sees a consistent state: if it finds
        // us as a waiter, we are guaranteed to be in Blocked state.
        t->waiter = current;
        current->state = task::TaskState::Blocked;
    }

    poll_lock.release(saved_daif);
}

/** @copydoc poll::test_poll */
void test_poll() {
    serial::puts("[poll] Testing poll functionality...\n");

    // Create a test channel
    i64 ch_result = channel::create();
    if (ch_result < 0) {
        serial::puts("[poll] Failed to create test channel\n");
        return;
    }
    u32 ch_id = static_cast<u32>(ch_result);
    serial::puts("[poll] Created test channel ");
    serial::put_dec(ch_id);
    serial::puts("\n");

    // Test 1: Empty channel should not be readable, but should be writable
    PollEvent ev1;
    ev1.handle = ch_id;
    ev1.events = EventType::CHANNEL_READ | EventType::CHANNEL_WRITE;
    ev1.triggered = EventType::NONE;

    i64 result = poll(&ev1, 1, 0); // Non-blocking poll
    serial::puts("[poll] Test 1 (empty channel): poll returned ");
    serial::put_dec(result);
    serial::puts(", triggered=");
    serial::put_hex(static_cast<u32>(ev1.triggered));
    serial::puts("\n");

    if (result == 1 && has_event(ev1.triggered, EventType::CHANNEL_WRITE) &&
        !has_event(ev1.triggered, EventType::CHANNEL_READ)) {
        serial::puts("[poll] Test 1 PASSED: writable but not readable\n");
    } else {
        serial::puts("[poll] Test 1 FAILED\n");
    }

    // Test 2: Send a message, channel should be readable
    const char *msg = "test";
    channel::send(ch_id, msg, 5);

    ev1.triggered = EventType::NONE;
    result = poll(&ev1, 1, 0);
    serial::puts("[poll] Test 2 (message queued): poll returned ");
    serial::put_dec(result);
    serial::puts(", triggered=");
    serial::put_hex(static_cast<u32>(ev1.triggered));
    serial::puts("\n");

    if (result >= 1 && has_event(ev1.triggered, EventType::CHANNEL_READ)) {
        serial::puts("[poll] Test 2 PASSED: readable after message sent\n");
    } else {
        serial::puts("[poll] Test 2 FAILED\n");
    }

    // Clean up
    channel::close(ch_id);
    serial::puts("[poll] Poll tests complete\n");
}

} // namespace poll
