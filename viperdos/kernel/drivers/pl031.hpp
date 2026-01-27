//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "../include/types.hpp"

/**
 * @file pl031.hpp
 * @brief PL031 Real-Time Clock (RTC) driver interface.
 *
 * @details
 * The PL031 is an ARM PrimeCell RTC that provides wall-clock time as
 * seconds since the Unix epoch (1970-01-01 00:00:00 UTC). On QEMU's
 * virt machine it is mapped at 0x09010000.
 *
 * The driver reads the RTC data register to provide calendar time,
 * enabling time(), CLOCK_REALTIME, and accurate gmtime()/localtime().
 */
namespace pl031 {

/**
 * @brief Initialize the PL031 RTC driver.
 *
 * @return true if the RTC was found and initialized.
 */
bool init();

/**
 * @brief Check whether the RTC is available.
 *
 * @return true if initialized and usable.
 */
bool is_available();

/**
 * @brief Read the current wall-clock time.
 *
 * @return Seconds since Unix epoch (1970-01-01 00:00:00 UTC),
 *         or 0 if RTC is not available.
 */
u64 read_time();

} // namespace pl031
