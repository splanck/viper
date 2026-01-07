//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/ipc/poll.hpp
// Purpose: Polling and timer primitives for cooperative task scheduling.
// Key invariants: Timers monotonic; poll returns ready count.
// Ownership/Lifetime: Fixed timer table; managed by poll subsystem.
// Links: kernel/ipc/poll.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "../include/error.hpp"
#include "../include/types.hpp"

// Forward declaration
namespace task
{
struct Task;
}

/**
 * @file poll.hpp
 * @brief Polling and timer primitives for cooperative task scheduling.
 *
 * @details
 * The poll subsystem provides:
 * - A `poll()` function for checking readiness of multiple handles (channels,
 *   timers, and pseudo-handles like console input).
 * - A simple timer facility used to implement sleep and timeouts.
 *
 * The design is intentionally simple for early bring-up:
 * - Polling is implemented as a loop that checks readiness and yields.
 * - Timers are stored in a fixed-size table.
 * - Time is measured in milliseconds using the system tick counter.
 */
namespace poll
{

/** @brief Maximum number of events that can be polled in a single call. */
constexpr u32 MAX_POLL_EVENTS = 16;

/**
 * @brief Special pseudo-handle representing console input readiness.
 *
 * @details
 * When a poll entry uses this handle and includes @ref EventType::CONSOLE_INPUT,
 * the poll logic checks keyboard/serial input availability rather than a
 * concrete channel/timer object.
 */
constexpr u32 HANDLE_CONSOLE_INPUT = 0xFFFF0001;

/**
 * @brief Special pseudo-handle representing network receive readiness.
 *
 * @details
 * When a poll entry uses this handle and includes @ref EventType::NETWORK_RX,
 * the poll logic checks if the network device has received data available.
 */
constexpr u32 HANDLE_NETWORK_RX = 0xFFFF0002;

/**
 * @brief Bitmask of event types that can be requested/triggered by polling.
 *
 * @details
 * `EventType` is used both as an input mask (events to watch) and as an output
 * mask (events that are currently ready). It is treated as a bitfield; helper
 * operators are provided for combining and testing flags.
 */
enum class EventType : u32
{
    NONE = 0,
    CHANNEL_READ = (1 << 0),  // Channel has data to read
    CHANNEL_WRITE = (1 << 1), // Channel has space to write
    TIMER = (1 << 2),         // Timer expired
    CONSOLE_INPUT = (1 << 3), // Console has input ready
    NETWORK_RX = (1 << 4),    // Network has received data available
};

/**
 * @brief Polling mode flags for entries.
 */
enum class PollFlags : u32
{
    NONE = 0,
    EDGE_TRIGGERED = (1 << 0), // Only report edge transitions, not level
    ONESHOT = (1 << 1),        // Auto-remove after first trigger
};

/**
 * @brief Combine two event masks with bitwise OR.
 *
 * @param a First mask.
 * @param b Second mask.
 * @return Combined mask.
 */
inline EventType operator|(EventType a, EventType b)
{
    return static_cast<EventType>(static_cast<u32>(a) | static_cast<u32>(b));
}

/**
 * @brief Intersect two event masks with bitwise AND.
 *
 * @param a First mask.
 * @param b Second mask.
 * @return Intersection mask.
 */
inline EventType operator&(EventType a, EventType b)
{
    return static_cast<EventType>(static_cast<u32>(a) & static_cast<u32>(b));
}

/**
 * @brief Test whether an event mask contains a particular flag.
 *
 * @param events Mask to test.
 * @param check Flag(s) to check for.
 * @return `true` if any checked bits are set in `events`.
 */
inline bool has_event(EventType events, EventType check)
{
    return (static_cast<u32>(events) & static_cast<u32>(check)) != 0;
}

/**
 * @brief Combine two poll flags with bitwise OR.
 */
inline PollFlags operator|(PollFlags a, PollFlags b)
{
    return static_cast<PollFlags>(static_cast<u32>(a) | static_cast<u32>(b));
}

/**
 * @brief Test whether poll flags contain a particular flag.
 */
inline bool has_flag(PollFlags flags, PollFlags check)
{
    return (static_cast<u32>(flags) & static_cast<u32>(check)) != 0;
}

// Poll event structure - input and output for poll operations
/**
 * @brief Input/output structure for polling readiness.
 *
 * @details
 * Callers fill in `handle` and `events` as the request. The poll implementation
 * preserves `events` and writes readiness results into `triggered`.
 */
struct PollEvent
{
    u32 handle;          // Channel ID or timer handle
    EventType events;    // Requested events (input) - preserved
    EventType triggered; // Triggered events (output) - set by poll()
};

/**
 * @brief Initialize the poll subsystem.
 *
 * @details
 * Resets the internal timer table and prepares the module for use. Called once
 * during kernel boot.
 */
void init();

/**
 * @brief Poll for readiness events on multiple handles.
 *
 * @details
 * Checks each requested handle for the specified event types. If any events
 * are ready, returns the count of ready entries and sets each entry's
 * `triggered` mask accordingly.
 *
 * Blocking behavior:
 * - `timeout_ms == 0`: non-blocking, returns immediately.
 * - `timeout_ms > 0`: polls until timeout expires.
 * - `timeout_ms < 0`: polls indefinitely.
 *
 * The current implementation yields to the scheduler between checks rather than
 * using interrupt-driven wakeups for all event types.
 *
 * @param events Array of @ref PollEvent structures (in/out).
 * @param count Number of entries in `events`.
 * @param timeout_ms Timeout in milliseconds (0 = non-blocking, -1 = infinite).
 * @return Number of ready entries (>0), 0 on timeout, or negative error code.
 */
i64 poll(PollEvent *events, u32 count, i64 timeout_ms);

/**
 * @brief Create a one-shot timer that expires after `timeout_ms` milliseconds.
 *
 * @details
 * Allocates a timer from a fixed-size table and sets its expiration time based
 * on the current monotonic time. The returned handle can be used with polling
 * (`EventType::TIMER`) or with @ref timer_expired / @ref timer_cancel.
 *
 * @param timeout_ms Relative timeout in milliseconds.
 * @return Non-negative timer ID on success, or negative error code.
 */
i64 timer_create(u64 timeout_ms);

/**
 * @brief Check whether a timer has expired.
 *
 * @param timer_id Timer handle returned by @ref timer_create.
 * @return `true` if expired or not found, `false` if still pending.
 */
bool timer_expired(u32 timer_id);

/**
 * @brief Cancel and destroy a timer.
 *
 * @details
 * Cancels the timer and wakes any task currently waiting on it.
 *
 * @param timer_id Timer handle.
 * @return Result code.
 */
i64 timer_cancel(u32 timer_id);

/**
 * @brief Get current monotonic time in milliseconds.
 *
 * @details
 * This is the time base used for timers and poll timeouts.
 *
 * @return Milliseconds since boot.
 */
u64 time_now_ms();

/**
 * @brief Sleep the current task for `ms` milliseconds.
 *
 * @details
 * Implements sleep by creating a timer and blocking the current task until the
 * timer expires (or is cancelled). In the current cooperative scheduler model,
 * the task yields while waiting.
 *
 * @param ms Duration to sleep in milliseconds.
 * @return Result code.
 */
i64 sleep_ms(u64 ms);

/**
 * @brief Wake tasks whose sleep timers have expired.
 *
 * @details
 * Called from the periodic timer interrupt handler to move tasks waiting on
 * timers back to the Ready state once their expiration time has been reached.
 */
void check_timers();

/**
 * @brief Run a simple self-test of the poll subsystem.
 *
 * @details
 * Creates a test channel and verifies that poll readiness reporting matches
 * expected behavior for empty vs non-empty channels.
 *
 * This function is intended for kernel bring-up and debugging.
 */
void test_poll();

/**
 * @brief Register current task as waiting on a handle for specific events.
 *
 * @details
 * Adds the calling task to the wait queue for the specified handle.
 * When notify_handle is called for this handle with matching events,
 * the task will be woken.
 *
 * @param handle Handle to wait on (channel ID, timer ID, etc.).
 * @param events Event mask to wait for.
 */
void register_wait(u32 handle, EventType events);

/**
 * @brief Notify waiters that events are ready on a handle.
 *
 * @details
 * Wakes any tasks that are waiting on the specified handle for the
 * given events. This is called by event sources (channels, timers)
 * when state changes occur.
 *
 * @param handle Handle that has events ready.
 * @param events Event mask of ready events.
 */
void notify_handle(u32 handle, EventType events);

/**
 * @brief Remove current task from all wait queues.
 *
 * @details
 * Called when a task is done waiting (either due to event or timeout).
 */
void unregister_wait();

/**
 * @brief Clear all wait entries and timers referencing a given task.
 *
 * @details
 * Called during task cleanup (exit/kill) to prevent use-after-free
 * when a timer fires for an exited task.
 *
 * @param t Task whose references should be cleared.
 */
void clear_task_waiters(task::Task *t);

} // namespace poll
