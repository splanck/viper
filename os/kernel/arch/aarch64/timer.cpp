#include "timer.hpp"
#include "../../console/gcon.hpp"
#include "../../console/serial.hpp"
#include "../../input/input.hpp"
#include "../../ipc/poll.hpp"
#include "../../net/network.hpp"
#include "../../sched/scheduler.hpp"
#include "gic.hpp"

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

// Timer state
u64 frequency = 0;
volatile u64 ticks = 0;
u64 interval = 0; // Ticks per interrupt

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
 *
 * Because this runs in interrupt context, work done here should remain
 * bounded and non-blocking.
 */
void timer_irq_handler()
{
    ticks = ticks + 1;

    // Schedule next interrupt
    u64 current = read_cntpct();
    write_cntp_cval(current + interval);

    // Debug output every second
    if (ticks % 1000 == 0)
    {
        serial::puts("[timer] ");
        serial::put_dec(ticks / 1000);
        serial::puts("s\n");
    }

    // Poll for input events
    input::poll();

    // Poll for network packets
    net::network_poll();

    // Check for expired timers (poll/sleep)
    poll::check_timers();

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

    serial::puts("[timer] Timer started (1000 Hz)\n");
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

/** @copydoc timer::get_ns */
u64 get_ns()
{
    u64 count = read_cntpct();
    // Avoid overflow by dividing first, losing some precision
    return (count / (frequency / 1000000)) * 1000;
}

/** @copydoc timer::get_ms */
u64 get_ms()
{
    return get_ns() / 1000000;
}

/** @copydoc timer::delay_ms */
void delay_ms(u32 ms)
{
    u64 target = get_ticks() + ms;
    while (get_ticks() < target)
    {
        asm volatile("wfi");
    }
}

} // namespace timer
