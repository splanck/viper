#pragma once

#include "../../include/types.hpp"

/**
 * @file timer.hpp
 * @brief AArch64 architected timer interface.
 *
 * @details
 * The AArch64 architected timer provides a per-CPU counter (`CNTPCT_EL0`) and
 * a programmable compare value (`CNTP_CVAL_EL0`) that can raise periodic
 * interrupts. This module configures the timer to generate a steady tick
 * interrupt and exposes simple timekeeping helpers to the rest of the kernel.
 *
 * The current kernel configuration uses a 1kHz tick (1ms granularity) and
 * increments an internal tick counter from the timer IRQ handler.
 */
namespace timer
{

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

/**
 * @brief Get an approximate number of nanoseconds since boot.
 *
 * @details
 * Converts the current counter value to nanoseconds using the configured timer
 * frequency. The conversion is designed to avoid overflow in 64-bit arithmetic
 * by reducing precision when necessary.
 *
 * @return Approximate nanoseconds since boot.
 */
u64 get_ns();

/**
 * @brief Get milliseconds since boot.
 *
 * @details
 * Convenience wrapper over @ref get_ns.
 *
 * @return Milliseconds since boot.
 */
u64 get_ms();

/**
 * @brief Busy-wait for a number of milliseconds.
 *
 * @details
 * Uses the tick counter maintained by the timer IRQ to wait for at least `ms`
 * milliseconds. The implementation uses `wfi` in the loop to reduce power
 * consumption while waiting for interrupts.
 *
 * @param ms Number of milliseconds to delay.
 */
void delay_ms(u32 ms);

} // namespace timer
