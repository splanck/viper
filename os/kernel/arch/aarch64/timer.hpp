#pragma once

#include "../../include/types.hpp"

/**
 * @file timer.hpp
 * @brief AArch64 architected timer interface with high-resolution support.
 *
 * @details
 * The AArch64 architected timer provides a per-CPU counter (`CNTPCT_EL0`) and
 * a programmable compare value (`CNTP_CVAL_EL0`) that can raise periodic
 * interrupts. This module provides:
 *
 * ## Periodic Timer
 * - 1kHz system tick for scheduling and time accounting
 * - Tick counter for coarse-grained timing
 *
 * ## High-Resolution Timer
 * - Nanosecond-precision timestamps using raw counter
 * - High-precision delay functions (us/ns granularity)
 * - One-shot timer callbacks for deadline-based wakeups
 * - Monotonic timestamp access for benchmarking
 *
 * ## Timer Precision
 * On QEMU virt with a typical 62.5 MHz counter:
 * - Resolution: 16 ns per tick
 * - Overflow: ~2900 years (plenty for uptime)
 */
namespace timer
{

/**
 * @brief High-resolution timestamp in raw timer ticks.
 *
 * @details
 * Use this for precise time measurement without the overhead of ns conversion.
 * Compare timestamps using arithmetic or convert to ns with ticks_to_ns().
 */
using Timestamp = u64;

/// Callback type for one-shot timers
using TimerCallback = void (*)(void *context);

/**
 * @brief Initialize and start the architected timer.
 *
 * @details
 * Reads the timer frequency, computes the compare interval for a 1kHz tick,
 * registers the timer IRQ handler with the GIC, programs the initial compare
 * value, and enables the timer interrupt.
 */
void init();

/**
 * @brief Get the current tick count.
 *
 * @details
 * The tick counter is incremented in the timer interrupt handler and represents
 * the number of 1ms intervals elapsed since @ref init completed.
 *
 * @return Tick count (milliseconds since boot under the current configuration).
 */
u64 get_ticks();

/**
 * @brief Get the architected timer frequency in Hz.
 *
 * @details
 * The frequency is read from `CNTFRQ_EL0` during initialization.
 *
 * @return Timer frequency in ticks per second.
 */
u64 get_frequency();

// ============================================================================
// High-Resolution Time Functions
// ============================================================================

/**
 * @brief Read the raw timer counter (high-resolution timestamp).
 *
 * @details
 * Returns the current value of CNTPCT_EL0 with no conversion overhead.
 * Use for precise timing measurements or to compute deadlines.
 *
 * @return Raw counter value (Timestamp).
 */
Timestamp now();

/**
 * @brief Convert timer ticks to nanoseconds.
 *
 * @details
 * Uses 128-bit arithmetic internally to avoid overflow while maintaining
 * precision. Safe for counters up to 2^64-1.
 *
 * @param ticks Timer tick count.
 * @return Equivalent time in nanoseconds.
 */
u64 ticks_to_ns(Timestamp ticks);

/**
 * @brief Convert nanoseconds to timer ticks.
 *
 * @param ns Time in nanoseconds.
 * @return Equivalent timer tick count.
 */
Timestamp ns_to_ticks(u64 ns);

/**
 * @brief Get precise nanoseconds since boot.
 *
 * @details
 * High-precision version using 128-bit arithmetic to avoid overflow.
 * Use now() + ticks_to_ns() for best performance in hot paths.
 *
 * @return Nanoseconds since boot.
 */
u64 get_ns();

/**
 * @brief Get microseconds since boot.
 *
 * @return Microseconds since boot.
 */
u64 get_us();

/**
 * @brief Get milliseconds since boot.
 *
 * @return Milliseconds since boot.
 */
u64 get_ms();

// ============================================================================
// High-Resolution Delay Functions
// ============================================================================

/**
 * @brief Busy-wait for a number of nanoseconds.
 *
 * @details
 * Spins on the raw counter for precise short delays. Does not use
 * interrupts so suitable for very short waits. For delays over 1ms,
 * prefer delay_ms() which uses wfi for power efficiency.
 *
 * @param ns Number of nanoseconds to delay.
 */
void delay_ns(u64 ns);

/**
 * @brief Busy-wait for a number of microseconds.
 *
 * @param us Number of microseconds to delay.
 */
void delay_us(u64 us);

/**
 * @brief Busy-wait for a number of milliseconds.
 *
 * @details
 * Uses wfi in the loop for power efficiency while waiting.
 *
 * @param ms Number of milliseconds to delay.
 */
void delay_ms(u32 ms);

/**
 * @brief Wait until a specific timestamp.
 *
 * @details
 * Blocks until now() >= deadline. For short waits, spins on the counter.
 * For longer waits, uses wfi to save power.
 *
 * @param deadline Target timestamp to wait until.
 */
void wait_until(Timestamp deadline);

// ============================================================================
// One-Shot Timer Support
// ============================================================================

/**
 * @brief Schedule a one-shot timer callback.
 *
 * @details
 * The callback will be invoked from timer interrupt context at or after
 * the specified deadline. Multiple timers can be scheduled; they are
 * executed in deadline order.
 *
 * @param deadline Timestamp at which to fire the callback.
 * @param callback Function to call.
 * @param context User data passed to callback.
 * @return Timer ID for cancellation, or 0 on failure.
 */
u32 schedule_oneshot(Timestamp deadline, TimerCallback callback, void *context);

/**
 * @brief Cancel a scheduled one-shot timer.
 *
 * @param timer_id Timer ID returned by schedule_oneshot().
 * @return true if timer was cancelled, false if already fired or invalid.
 */
bool cancel_oneshot(u32 timer_id);

/**
 * @brief Compute a deadline from current time plus duration.
 *
 * @param ns Duration in nanoseconds.
 * @return Timestamp representing now + ns.
 */
inline Timestamp deadline_ns(u64 ns)
{
    return now() + ns_to_ticks(ns);
}

/**
 * @brief Compute a deadline from current time plus duration.
 *
 * @param us Duration in microseconds.
 * @return Timestamp representing now + us.
 */
inline Timestamp deadline_us(u64 us)
{
    return now() + ns_to_ticks(us * 1000);
}

/**
 * @brief Compute a deadline from current time plus duration.
 *
 * @param ms Duration in milliseconds.
 * @return Timestamp representing now + ms.
 */
inline Timestamp deadline_ms(u64 ms)
{
    return now() + ns_to_ticks(ms * 1000000);
}

} // namespace timer
