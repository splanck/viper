#include "poll.hpp"
#include "../arch/aarch64/timer.hpp"
#include "../console/serial.hpp"
#include "../lib/timerwheel.hpp"
#include "../sched/scheduler.hpp"
#include "../sched/task.hpp"
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
namespace poll
{

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
struct Timer
{
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
 */
struct WaitEntry
{
    task::Task *task; // Waiting task
    u32 handle;       // Handle being waited on
    EventType events; // Events being waited for
    bool active;      // Entry is in use
};

// Wait queue table
constexpr u32 MAX_WAIT_ENTRIES = 32;
static WaitEntry wait_queue[MAX_WAIT_ENTRIES];

/** @copydoc poll::init */
void init()
{
    serial::puts("[poll] Initializing poll subsystem\n");

    for (u32 i = 0; i < MAX_TIMERS; i++)
    {
        timers[i].id = 0;
        timers[i].active = false;
        timers[i].waiter = nullptr;
    }

    for (u32 i = 0; i < MAX_WAIT_ENTRIES; i++)
    {
        wait_queue[i].task = nullptr;
        wait_queue[i].active = false;
    }

    // Initialize the timer wheel for O(1) timeout management
    timerwheel::init(timer::get_ticks());

    serial::puts("[poll] Poll subsystem initialized\n");
}

/** @copydoc poll::time_now_ms */
u64 time_now_ms()
{
    return timer::get_ticks();
}

/**
 * @brief Find an active timer by ID.
 *
 * @param timer_id Timer handle.
 * @return Pointer to the timer entry, or `nullptr` if not found/active.
 */
static Timer *find_timer(u32 timer_id)
{
    for (u32 i = 0; i < MAX_TIMERS; i++)
    {
        if (timers[i].id == timer_id && timers[i].active)
        {
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
static Timer *alloc_timer()
{
    for (u32 i = 0; i < MAX_TIMERS; i++)
    {
        if (!timers[i].active)
        {
            return &timers[i];
        }
    }
    return nullptr;
}

/** @copydoc poll::timer_create */
i64 timer_create(u64 timeout_ms)
{
    Timer *t = alloc_timer();
    if (!t)
    {
        return error::VERR_OUT_OF_MEMORY;
    }

    t->id = next_timer_id++;
    t->expire_time = time_now_ms() + timeout_ms;
    t->active = true;
    t->waiter = nullptr;

    return static_cast<i64>(t->id);
}

/** @copydoc poll::timer_expired */
bool timer_expired(u32 timer_id)
{
    Timer *t = find_timer(timer_id);
    if (!t)
    {
        return true; // Non-existent timer is "expired"
    }
    return time_now_ms() >= t->expire_time;
}

/** @copydoc poll::timer_cancel */
i64 timer_cancel(u32 timer_id)
{
    Timer *t = find_timer(timer_id);
    if (!t)
    {
        return error::VERR_NOT_FOUND;
    }

    // Wake up any waiter
    if (t->waiter)
    {
        t->waiter->state = task::TaskState::Ready;
        scheduler::enqueue(t->waiter);
        t->waiter = nullptr;
    }

    t->active = false;
    t->id = 0;

    return error::VOK;
}

/** @copydoc poll::sleep_ms */
i64 sleep_ms(u64 ms)
{
    if (ms == 0)
    {
        return error::VOK;
    }

    // Create a timer
    i64 timer_result = timer_create(ms);
    if (timer_result < 0)
    {
        return timer_result;
    }
    u32 timer_id = static_cast<u32>(timer_result);

    Timer *t = find_timer(timer_id);
    if (!t)
    {
        return error::VERR_UNKNOWN;
    }

    // Block until timer expires
    task::Task *current = task::current();
    if (!current)
    {
        // No current task (shouldn't happen)
        timer_cancel(timer_id);
        return error::VERR_UNKNOWN;
    }

    // Wait for timer
    while (!timer_expired(timer_id))
    {
        current->state = task::TaskState::Blocked;
        t->waiter = current;
        task::yield();

        // Re-check timer (may have been woken by something else)
        t = find_timer(timer_id);
        if (!t)
        {
            break; // Timer was cancelled
        }
    }

    // Clean up timer
    timer_cancel(timer_id);

    return error::VOK;
}

/** @copydoc poll::poll */
i64 poll(PollEvent *events, u32 count, i64 timeout_ms)
{
    if (!events || count == 0 || count > MAX_POLL_EVENTS)
    {
        return error::VERR_INVALID_ARG;
    }

    u64 deadline = 0;
    if (timeout_ms > 0)
    {
        deadline = time_now_ms() + static_cast<u64>(timeout_ms);
    }

    // Poll loop
    while (true)
    {
        u32 ready_count = 0;

        // Check each event
        for (u32 i = 0; i < count; i++)
        {
            // Clear triggered output field (preserve input events!)
            events[i].triggered = EventType::NONE;

            // Read requested events from input (NOT the cleared field!)
            EventType requested = events[i].events;
            u32 handle = events[i].handle;

            // Check for channel read readiness
            if (has_event(requested, EventType::CHANNEL_READ))
            {
                if (channel::has_message(handle))
                {
                    events[i].triggered = events[i].triggered | EventType::CHANNEL_READ;
                    ready_count++;
                }
            }

            // Check for channel write readiness (has space for more messages)
            if (has_event(requested, EventType::CHANNEL_WRITE))
            {
                if (channel::has_space(handle))
                {
                    events[i].triggered = events[i].triggered | EventType::CHANNEL_WRITE;
                    ready_count++;
                }
            }

            // Check for timer expiry
            if (has_event(requested, EventType::TIMER))
            {
                if (timer_expired(handle))
                {
                    events[i].triggered = events[i].triggered | EventType::TIMER;
                    ready_count++;
                }
            }

            // Network RX events removed - use netd user-space server instead
        }

        // Return if any events are ready
        if (ready_count > 0)
        {
            return static_cast<i64>(ready_count);
        }

        // Non-blocking mode: return immediately
        if (timeout_ms == 0)
        {
            return 0;
        }

        // Check timeout
        if (timeout_ms > 0 && time_now_ms() >= deadline)
        {
            return 0;
        }

        // Yield and try again
        task::yield();
    }
}

/** @copydoc poll::check_timers */
void check_timers()
{
    u64 now = time_now_ms();

    // Process the timer wheel (O(1) amortized)
    timerwheel::tick(now);

    // Also check legacy timers for backward compatibility
    for (u32 i = 0; i < MAX_TIMERS; i++)
    {
        if (timers[i].active && timers[i].waiter && now >= timers[i].expire_time)
        {
            task::Task *waiter = timers[i].waiter;
            timers[i].waiter = nullptr;
            waiter->state = task::TaskState::Ready;
            scheduler::enqueue(waiter);
        }
    }
}

/** @copydoc poll::register_wait */
void register_wait(u32 handle, EventType events)
{
    task::Task *current = task::current();
    if (!current)
        return;

    // Find an empty slot
    for (u32 i = 0; i < MAX_WAIT_ENTRIES; i++)
    {
        if (!wait_queue[i].active)
        {
            wait_queue[i].task = current;
            wait_queue[i].handle = handle;
            wait_queue[i].events = events;
            wait_queue[i].active = true;
            return;
        }
    }
}

/** @copydoc poll::notify_handle */
void notify_handle(u32 handle, EventType events)
{
    for (u32 i = 0; i < MAX_WAIT_ENTRIES; i++)
    {
        if (wait_queue[i].active && wait_queue[i].handle == handle)
        {
            // Check if any requested events match
            if (has_event(wait_queue[i].events, events))
            {
                task::Task *waiter = wait_queue[i].task;
                wait_queue[i].active = false;
                wait_queue[i].task = nullptr;

                // Wake the task
                if (waiter && waiter->state == task::TaskState::Blocked)
                {
                    waiter->state = task::TaskState::Ready;
                    scheduler::enqueue(waiter);
                }
            }
        }
    }
}

/** @copydoc poll::unregister_wait */
void unregister_wait()
{
    task::Task *current = task::current();
    if (!current)
        return;

    for (u32 i = 0; i < MAX_WAIT_ENTRIES; i++)
    {
        if (wait_queue[i].active && wait_queue[i].task == current)
        {
            wait_queue[i].active = false;
            wait_queue[i].task = nullptr;
        }
    }
}

/** @copydoc poll::clear_task_waiters */
void clear_task_waiters(task::Task *t)
{
    if (!t)
        return;

    // Clear all timer waiters for this task
    for (u32 i = 0; i < MAX_TIMERS; i++)
    {
        if (timers[i].active && timers[i].waiter == t)
        {
            timers[i].waiter = nullptr;
        }
    }

    // Clear all wait queue entries for this task
    for (u32 i = 0; i < MAX_WAIT_ENTRIES; i++)
    {
        if (wait_queue[i].active && wait_queue[i].task == t)
        {
            wait_queue[i].active = false;
            wait_queue[i].task = nullptr;
        }
    }
}

/** @copydoc poll::test_poll */
void test_poll()
{
    serial::puts("[poll] Testing poll functionality...\n");

    // Create a test channel
    i64 ch_result = channel::create();
    if (ch_result < 0)
    {
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
        !has_event(ev1.triggered, EventType::CHANNEL_READ))
    {
        serial::puts("[poll] Test 1 PASSED: writable but not readable\n");
    }
    else
    {
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

    if (result >= 1 && has_event(ev1.triggered, EventType::CHANNEL_READ))
    {
        serial::puts("[poll] Test 2 PASSED: readable after message sent\n");
    }
    else
    {
        serial::puts("[poll] Test 2 FAILED\n");
    }

    // Clean up
    channel::close(ch_id);
    serial::puts("[poll] Poll tests complete\n");
}

} // namespace poll
