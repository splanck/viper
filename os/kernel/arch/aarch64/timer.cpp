#include "timer.hpp"
#include "../../console/gcon.hpp"
#include "../../console/serial.hpp"
#include "../../include/config.hpp"
#include "../../input/input.hpp"
#include "../../ipc/poll.hpp"
#include "../../net/network.hpp"
#include "../../sched/scheduler.hpp"
#include "gic.hpp"

#ifndef CONFIG_TIMER_HEARTBEAT
#define CONFIG_TIMER_HEARTBEAT 0
#endif

// Suppress warnings for timer register read helpers that may not be used yet
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

/**
 * @file timer.cpp
 * @brief AArch64 architected timer configuration and tick handling.
 *
 * @details
 * The timer module programs the EL1 physical timer to generate periodic
 * interrupts and keeps a simple global tick counter. The interrupt handler is
 * also used as a convenient heartbeat during bring-up and currently performs
 * periodic polling work (input, networking, sleep timers) and scheduler
 * preemption checks.
 *
 * In a more mature kernel these responsibilities may be separated so the
 * interrupt handler does minimal work and defers heavier processing to bottom
 * halves or kernel threads.
 */
namespace timer
{

namespace
{
// Physical timer PPI (Private Peripheral Interrupt)
constexpr u32 TIMER_IRQ = 30;

// Maximum number of one-shot timers
constexpr u32 MAX_ONESHOT_TIMERS = 16;

// Timer state
u64 frequency = 0;
volatile u64 ticks = 0;
u64 interval = 0; // Ticks per interrupt

// Precomputed conversion factors for high-resolution timing
// These are set during init() based on actual frequency
u64 ns_per_tick_q32 = 0; // Fixed-point (Q32) nanoseconds per tick
u64 ticks_per_us = 0;    // Ticks per microsecond (for fast conversion)

// One-shot timer queue entry
struct OneshotTimer
{
    Timestamp deadline;
    TimerCallback callback;
    void *context;
    u32 id;
    bool active;
};

// Timer queue (sorted by deadline)
OneshotTimer oneshot_timers[MAX_ONESHOT_TIMERS];
u32 next_timer_id = 1;

// Read counter frequency
/**
 * @brief Read the architected timer frequency register.
 *
 * @return The value of `CNTFRQ_EL0` (ticks per second).
 */
inline u64 read_cntfrq()
{
    u64 val;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(val));
    return val;
}

// Read current counter value
/**
 * @brief Read the current physical counter value.
 *
 * @return The value of `CNTPCT_EL0`.
 */
inline u64 read_cntpct()
{
    u64 val;
    asm volatile("mrs %0, cntpct_el0" : "=r"(val));
    return val;
}

// Read timer compare value
/**
 * @brief Read the current timer compare value.
 *
 * @return The value of `CNTP_CVAL_EL0`.
 */
inline u64 read_cntp_cval()
{
    u64 val;
    asm volatile("mrs %0, cntp_cval_el0" : "=r"(val));
    return val;
}

// Write timer compare value
/**
 * @brief Program the timer compare value.
 *
 * @param val New compare value for `CNTP_CVAL_EL0`.
 */
inline void write_cntp_cval(u64 val)
{
    asm volatile("msr cntp_cval_el0, %0" : : "r"(val));
}

// Read timer control register
/**
 * @brief Read the timer control register.
 *
 * @return The value of `CNTP_CTL_EL0`.
 */
inline u64 read_cntp_ctl()
{
    u64 val;
    asm volatile("mrs %0, cntp_ctl_el0" : "=r"(val));
    return val;
}

// Write timer control register
/**
 * @brief Write the timer control register.
 *
 * @param val Value to write to `CNTP_CTL_EL0`.
 */
inline void write_cntp_ctl(u64 val)
{
    asm volatile("msr cntp_ctl_el0, %0" : : "r"(val));
}

/**
 * @brief Check and fire any expired one-shot timers.
 *
 * Called from the timer interrupt handler.
 */
void check_oneshot_timers()
{
    u64 current = read_cntpct();

    for (u32 i = 0; i < MAX_ONESHOT_TIMERS; i++)
    {
        OneshotTimer &t = oneshot_timers[i];
        if (t.active && current >= t.deadline)
        {
            // Mark as inactive before calling to allow re-scheduling
            t.active = false;
            TimerCallback cb = t.callback;
            void *ctx = t.context;

            // Call the callback
            if (cb)
            {
                cb(ctx);
            }
        }
    }
}

// Timer interrupt handler
/**
 * @brief IRQ handler invoked on each timer tick.
 *
 * @details
 * Increments the global tick count, re-arms the compare value for the next
 * interval, and performs periodic maintenance/polling tasks used during
 * bring-up:
 * - Debug heartbeat prints once per second.
 * - Input polling to feed higher-level subsystems.
 * - Network polling for packet reception.
 * - Timer management for sleep/poll timeouts.
 * - Scheduler tick accounting and preemption checks.
 * - One-shot timer callbacks.
 *
 * Because this runs in interrupt context, work done here should remain
 * bounded and non-blocking.
 */
void timer_irq_handler(u32)
{
    ticks = ticks + 1;

    // Schedule next interrupt
    u64 current = read_cntpct();
    write_cntp_cval(current + interval);

    // Debug output every second
#if CONFIG_TIMER_HEARTBEAT
    if (ticks % 1000 == 0)
    {
        serial::puts("[timer] ");
        serial::put_dec(ticks / 1000);
        serial::puts("s\n");
    }
#endif

    // Update cursor blink (ticks are in milliseconds)
    gcon::update_cursor_blink(ticks);

    // Poll for input events
    input::poll();

    // Poll for network packets
#if VIPER_KERNEL_ENABLE_NET
    net::network_poll();
#endif

    // Check for expired timers (poll/sleep)
    poll::check_timers();

    // Check one-shot high-resolution timers
    check_oneshot_timers();

    // Notify scheduler of tick and check for preemption
    scheduler::tick();
    scheduler::preempt();
}
} // namespace

/** @copydoc timer::init */
void init()
{
    serial::puts("[timer] Initializing ARM architected timer\n");

    // Read timer frequency
    frequency = read_cntfrq();
    serial::puts("[timer] Frequency: ");
    serial::put_dec(frequency / 1000000);
    serial::puts(" MHz\n");

    // Calculate interval for 1ms ticks (1000 Hz)
    interval = frequency / 1000;
    serial::puts("[timer] Interval: ");
    serial::put_dec(interval);
    serial::puts(" ticks/ms\n");

    // Precompute conversion factors for high-resolution timing
    // ns_per_tick_q32 = (1e9 * 2^32) / frequency
    // Using 128-bit style arithmetic to avoid overflow:
    // ns_per_tick_q32 = (1000000000ULL << 32) / frequency
    // This can overflow, so we compute it carefully:
    // = ((1e9 / frequency) << 32) + ((1e9 % frequency) << 32) / frequency
    u64 ns_whole = 1000000000ULL / frequency;
    u64 ns_frac = 1000000000ULL % frequency;
    ns_per_tick_q32 = (ns_whole << 32) | ((ns_frac << 32) / frequency);

    ticks_per_us = frequency / 1000000; // Ticks per microsecond
    if (ticks_per_us == 0)
        ticks_per_us = 1; // Minimum

    serial::puts("[timer] ns/tick (Q32): ");
    serial::put_hex(ns_per_tick_q32);
    serial::puts("\n");
    serial::puts("[timer] ticks/us: ");
    serial::put_dec(ticks_per_us);
    serial::puts("\n");

    // Initialize one-shot timer array
    for (u32 i = 0; i < MAX_ONESHOT_TIMERS; i++)
    {
        oneshot_timers[i].active = false;
        oneshot_timers[i].id = 0;
    }

    // Register interrupt handler
    gic::register_handler(TIMER_IRQ, timer_irq_handler);

    // Set priority and enable the interrupt
    gic::set_priority(TIMER_IRQ, 0x80);
    gic::enable_irq(TIMER_IRQ);

    // Set initial compare value
    u64 current = read_cntpct();
    write_cntp_cval(current + interval);

    // Enable the timer (bit 0 = enable, bit 1 = mask output)
    write_cntp_ctl(1);

    serial::puts("[timer] Timer started (1000 Hz, high-resolution enabled)\n");
}

/** @copydoc timer::init_secondary */
void init_secondary()
{
    // Note: frequency, interval, and conversion factors are already set by boot CPU
    // The timer handler is already registered globally

    // Enable timer interrupt for this CPU (PPI - per-CPU)
    gic::set_priority(TIMER_IRQ, 0x80);
    gic::enable_irq(TIMER_IRQ);

    // Set initial compare value
    u64 current = read_cntpct();
    write_cntp_cval(current + interval);

    // Enable the timer (bit 0 = enable, bit 1 = mask output)
    write_cntp_ctl(1);
}

/** @copydoc timer::get_ticks */
u64 get_ticks()
{
    return ticks;
}

/** @copydoc timer::get_frequency */
u64 get_frequency()
{
    return frequency;
}

// ============================================================================
// High-Resolution Time Functions
// ============================================================================

/** @copydoc timer::now */
Timestamp now()
{
    return read_cntpct();
}

/** @copydoc timer::ticks_to_ns */
u64 ticks_to_ns(Timestamp timer_ticks)
{
    // Use Q32 fixed-point multiplication for precision
    // ns = ticks * ns_per_tick_q32 / 2^32
    // We need 128-bit multiplication, but we can approximate:
    // Split ticks into high and low 32-bit parts
    u64 ticks_hi = timer_ticks >> 32;
    u64 ticks_lo = timer_ticks & 0xFFFFFFFFULL;

    // Multiply and accumulate
    u64 result_lo = (ticks_lo * ns_per_tick_q32) >> 32;
    u64 result_hi = ticks_hi * ns_per_tick_q32;

    return result_hi + result_lo;
}

/** @copydoc timer::ns_to_ticks */
Timestamp ns_to_ticks(u64 ns)
{
    // ticks = ns * frequency / 1e9
    // Use microsecond intermediate to avoid overflow
    if (ns < 1000000000ULL)
    {
        // For sub-second values, compute directly
        return (ns * frequency) / 1000000000ULL;
    }
    else
    {
        // For larger values, split to avoid overflow
        u64 seconds = ns / 1000000000ULL;
        u64 remainder_ns = ns % 1000000000ULL;
        return (seconds * frequency) + (remainder_ns * frequency) / 1000000000ULL;
    }
}

/** @copydoc timer::get_ns */
u64 get_ns()
{
    return ticks_to_ns(read_cntpct());
}

/** @copydoc timer::get_us */
u64 get_us()
{
    return ticks_to_ns(read_cntpct()) / 1000;
}

/** @copydoc timer::get_ms */
u64 get_ms()
{
    return ticks_to_ns(read_cntpct()) / 1000000;
}

// ============================================================================
// High-Resolution Delay Functions
// ============================================================================

/** @copydoc timer::delay_ns */
void delay_ns(u64 ns)
{
    Timestamp deadline = read_cntpct() + ns_to_ticks(ns);

    // Spin on the counter for precise timing
    while (read_cntpct() < deadline)
    {
        // Tight spin for nanosecond precision
        asm volatile("" ::: "memory"); // Prevent optimization
    }
}

/** @copydoc timer::delay_us */
void delay_us(u64 us)
{
    Timestamp deadline = read_cntpct() + (us * ticks_per_us);

    // Spin for microsecond delays
    while (read_cntpct() < deadline)
    {
        asm volatile("" ::: "memory");
    }
}

/** @copydoc timer::delay_ms */
void delay_ms(u32 ms)
{
    // For millisecond delays, use wfi for power efficiency
    Timestamp deadline = read_cntpct() + ns_to_ticks(static_cast<u64>(ms) * 1000000ULL);

    while (read_cntpct() < deadline)
    {
        asm volatile("wfi");
    }
}

/** @copydoc timer::wait_until */
void wait_until(Timestamp deadline)
{
    u64 current = read_cntpct();

    if (current >= deadline)
        return;

    // Calculate remaining time
    u64 remaining_ticks = deadline - current;
    u64 remaining_us = remaining_ticks / ticks_per_us;

    if (remaining_us < 100)
    {
        // Short wait - spin
        while (read_cntpct() < deadline)
        {
            asm volatile("" ::: "memory");
        }
    }
    else
    {
        // Longer wait - use wfi
        while (read_cntpct() < deadline)
        {
            asm volatile("wfi");
        }
    }
}

// ============================================================================
// One-Shot Timer Support
// ============================================================================

/** @copydoc timer::schedule_oneshot */
u32 schedule_oneshot(Timestamp deadline, TimerCallback callback, void *context)
{
    // Find a free slot
    for (u32 i = 0; i < MAX_ONESHOT_TIMERS; i++)
    {
        if (!oneshot_timers[i].active)
        {
            u32 id = next_timer_id++;
            if (next_timer_id == 0)
                next_timer_id = 1; // Skip 0

            oneshot_timers[i].deadline = deadline;
            oneshot_timers[i].callback = callback;
            oneshot_timers[i].context = context;
            oneshot_timers[i].id = id;
            oneshot_timers[i].active = true;

            return id;
        }
    }

    // No free slots
    serial::puts("[timer] WARNING: No free one-shot timer slots\n");
    return 0;
}

/** @copydoc timer::cancel_oneshot */
bool cancel_oneshot(u32 timer_id)
{
    if (timer_id == 0)
        return false;

    for (u32 i = 0; i < MAX_ONESHOT_TIMERS; i++)
    {
        if (oneshot_timers[i].active && oneshot_timers[i].id == timer_id)
        {
            oneshot_timers[i].active = false;
            return true;
        }
    }

    return false;
}

} // namespace timer

#pragma GCC diagnostic pop
