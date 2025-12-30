#pragma once

#include "../include/types.hpp"

/**
 * @file timerwheel.hpp
 * @brief Hierarchical timer wheel for O(1) timeout management.
 *
 * @details
 * This timer wheel provides O(1) insertion, O(1) deletion, and amortized O(1)
 * expiration processing. It uses a two-level hierarchical structure:
 *
 * - Level 0: 256 slots at 1ms granularity (covers 0-255ms)
 * - Level 1: 64 slots at 256ms granularity (covers 256ms - 16.4s)
 *
 * Timers beyond 16.4s go into an overflow list and are cascaded down when
 * the wheel advances.
 *
 * This is based on the classic timer wheel algorithm described in:
 * "Hashed and Hierarchical Timing Wheels" by Varghese & Lauck (1987)
 */
namespace timerwheel
{

/// Timer callback function type
using TimerCallback = void (*)(void *context);

/// Maximum number of active timers
constexpr u32 MAX_TIMERS = 64;

/// Level 0: 256 slots at 1ms granularity (covers 0-255ms)
constexpr u32 WHEEL0_BITS = 8;
constexpr u32 WHEEL0_SIZE = 1u << WHEEL0_BITS; // 256 slots
constexpr u32 WHEEL0_MASK = WHEEL0_SIZE - 1;

/// Level 1: 64 slots at 256ms granularity (covers 256ms - 16.4s)
constexpr u32 WHEEL1_BITS = 6;
constexpr u32 WHEEL1_SIZE = 1u << WHEEL1_BITS; // 64 slots
constexpr u32 WHEEL1_MASK = WHEEL1_SIZE - 1;

/// Total coverage: 256 * 64 * 1ms = 16.384 seconds
constexpr u64 MAX_TIMEOUT_MS = WHEEL0_SIZE * WHEEL1_SIZE;

/**
 * @brief Timer entry in the timer wheel.
 *
 * @details
 * Each timer entry contains the expiration time, callback, context,
 * and linkage for the doubly-linked list in each wheel slot.
 */
struct TimerEntry
{
    u64 expire_time;       ///< Absolute expiration time in ms
    TimerCallback callback; ///< Function to call on expiration
    void *context;          ///< User context passed to callback
    u32 id;                 ///< Unique timer ID
    bool active;            ///< Timer is scheduled

    // Linked list pointers for wheel slot
    TimerEntry *next;
    TimerEntry *prev;
};

/**
 * @brief Timer wheel for efficient timeout management.
 *
 * @details
 * Provides O(1) insertion, O(1) deletion, and amortized O(1) tick processing.
 */
class TimerWheel
{
  public:
    /**
     * @brief Initialize the timer wheel.
     *
     * @param current_time_ms Current time in milliseconds.
     */
    void init(u64 current_time_ms);

    /**
     * @brief Schedule a timer to fire at a given time.
     *
     * @param expire_time_ms Absolute expiration time in milliseconds.
     * @param callback Function to call when timer expires.
     * @param context User context passed to callback.
     * @return Timer ID for cancellation, or 0 on failure.
     */
    u32 schedule(u64 expire_time_ms, TimerCallback callback, void *context);

    /**
     * @brief Cancel a scheduled timer.
     *
     * @param timer_id Timer ID returned by schedule().
     * @return true if timer was cancelled, false if not found or already fired.
     */
    bool cancel(u32 timer_id);

    /**
     * @brief Advance the timer wheel and fire expired timers.
     *
     * @details
     * Should be called periodically (typically every 1ms from the timer IRQ).
     * Fires all timers whose deadline has passed.
     *
     * @param current_time_ms Current time in milliseconds.
     */
    void tick(u64 current_time_ms);

    /**
     * @brief Get count of active timers.
     */
    u32 active_count() const { return active_count_; }

  private:
    /// Add timer to appropriate wheel slot
    void add_to_wheel(TimerEntry *entry);

    /// Remove timer from its current wheel slot
    void remove_from_slot(TimerEntry *entry);

    /// Cascade timers from higher level wheel down
    void cascade(u32 level);

    /// Find a timer by ID
    TimerEntry *find_timer(u32 id);

    /// Allocate a free timer entry
    TimerEntry *alloc_timer();

    // Timer storage
    TimerEntry timers_[MAX_TIMERS];
    u32 next_id_;
    u32 active_count_;

    // Wheel structures - heads of doubly-linked lists
    TimerEntry *wheel0_[WHEEL0_SIZE]; ///< Level 0: 1ms slots
    TimerEntry *wheel1_[WHEEL1_SIZE]; ///< Level 1: 256ms slots
    TimerEntry *overflow_;             ///< Timers beyond wheel range

    // Current wheel positions
    u64 current_time_;   ///< Current time in ms
    u32 wheel0_index_;   ///< Current slot in wheel 0
    u32 wheel1_index_;   ///< Current slot in wheel 1
};

/**
 * @brief Get the global timer wheel instance.
 */
TimerWheel &get_wheel();

/**
 * @brief Initialize the global timer wheel.
 *
 * @param current_time_ms Current time in milliseconds.
 */
void init(u64 current_time_ms);

/**
 * @brief Schedule a timer.
 *
 * @param timeout_ms Timeout from now in milliseconds.
 * @param callback Function to call when timer expires.
 * @param context User context passed to callback.
 * @return Timer ID, or 0 on failure.
 */
u32 schedule(u64 timeout_ms, TimerCallback callback, void *context);

/**
 * @brief Cancel a timer.
 *
 * @param timer_id Timer ID to cancel.
 * @return true if cancelled, false if not found.
 */
bool cancel(u32 timer_id);

/**
 * @brief Process timer wheel tick.
 *
 * @param current_time_ms Current time in milliseconds.
 */
void tick(u64 current_time_ms);

} // namespace timerwheel
