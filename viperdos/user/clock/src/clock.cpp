//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file clock.cpp
 * @brief Implementation of clock time management functions.
 *
 * This file implements the time retrieval, formatting, and angle calculation
 * functions declared in clock.hpp. It interfaces with the standard C library
 * for system time access.
 *
 * ## Time Source
 *
 * The clock uses the standard C `time()` and `localtime()` functions to obtain
 * the current system time. On ViperDOS, this ultimately queries:
 * 1. The real-time clock (RTC) if available
 * 2. Boot time plus system uptime as a fallback
 *
 * ## Angle Calculation Details
 *
 * All clock hand angles use integer arithmetic for efficiency on systems
 * without floating-point hardware. The calculations include fractional
 * contributions from smaller time units for smooth hand movement:
 *
 * - **Hour hand**: Moves 30° per hour, plus 0.5° per minute
 * - **Minute hand**: Moves 6° per minute, plus 0.1° per second
 * - **Second hand**: Moves 6° per second (discrete jumps)
 *
 * @see clock.hpp for function declarations and documentation
 */
//===----------------------------------------------------------------------===//

#include "../include/clock.hpp"
#include <stdio.h>
#include <time.h>

namespace clockapp {

//===----------------------------------------------------------------------===//
// Time Retrieval
//===----------------------------------------------------------------------===//

/**
 * @brief Retrieves the current system time.
 *
 * Queries the operating system for the current local time and populates
 * the Time structure. Uses standard C library functions for portability.
 *
 * ## Implementation Details
 *
 * 1. Calls `time(nullptr)` to get seconds since Unix epoch
 * 2. Calls `localtime()` to convert to broken-down time structure
 * 3. Extracts fields from `struct tm` into our Time structure
 * 4. Adjusts month (0-based to 1-based) and year (offset to full year)
 *
 * ## Error Handling
 *
 * If `localtime()` returns NULL (which can happen on some systems for
 * out-of-range times), the function falls back to a safe default of
 * midnight on January 1, 2024.
 *
 * @param[out] time Reference to Time structure to populate.
 */
void getCurrentTime(Time &time) {
    // Get current time using standard C library
    time_t now = ::time(nullptr);
    struct tm *tm_info = localtime(&now);

    if (tm_info) {
        time.hours = tm_info->tm_hour;
        time.minutes = tm_info->tm_min;
        time.seconds = tm_info->tm_sec;
        time.day = tm_info->tm_mday;
        time.month = tm_info->tm_mon + 1;
        time.year = tm_info->tm_year + 1900;
    } else {
        // Fallback to midnight
        time.hours = 0;
        time.minutes = 0;
        time.seconds = 0;
        time.day = 1;
        time.month = 1;
        time.year = 2024;
    }
}

//===----------------------------------------------------------------------===//
// Time Formatting
//===----------------------------------------------------------------------===//

/**
 * @brief Formats time in 12-hour format with AM/PM indicator.
 *
 * Converts 24-hour time to 12-hour format with appropriate AM/PM suffix.
 * The hour field uses leading spaces (not zeros) for single-digit hours,
 * matching traditional clock display conventions.
 *
 * ## Conversion Rules
 *
 * - Hours 0-11 are AM, 12-23 are PM
 * - Hour 0 (midnight) displays as "12 AM"
 * - Hour 12 (noon) displays as "12 PM"
 * - Hours 1-11 display as themselves with AM
 * - Hours 13-23 display as (hour-12) with PM
 *
 * @param time    The time to format.
 * @param buf     Output buffer for the formatted string.
 * @param bufSize Size of the output buffer in bytes.
 */
void formatTime12(const Time &time, char *buf, int bufSize) {
    int hour12 = time.hours % 12;
    if (hour12 == 0) {
        hour12 = 12;
    }
    const char *ampm = (time.hours < 12) ? "AM" : "PM";
    snprintf(buf, bufSize, "%2d:%02d:%02d %s", hour12, time.minutes, time.seconds, ampm);
}

/**
 * @brief Formats time in 24-hour (military) format.
 *
 * Produces a simple HH:MM:SS string with all fields zero-padded to
 * two digits. This format is unambiguous and commonly used in
 * technical and international contexts.
 *
 * @param time    The time to format.
 * @param buf     Output buffer for the formatted string.
 * @param bufSize Size of the output buffer in bytes.
 */
void formatTime24(const Time &time, char *buf, int bufSize) {
    snprintf(buf, bufSize, "%02d:%02d:%02d", time.hours, time.minutes, time.seconds);
}

/**
 * @brief Formats the date as "Mon DD, YYYY".
 *
 * Uses abbreviated three-letter month names for a compact but readable
 * date format. The month names are stored in a static lookup table.
 *
 * ## Month Lookup
 *
 * The month field (1-12) is converted to a 0-based index for the
 * lookup table. Invalid month values are clamped to January.
 *
 * @param time    The time structure containing the date.
 * @param buf     Output buffer for the formatted string.
 * @param bufSize Size of the output buffer in bytes.
 */
void formatDate(const Time &time, char *buf, int bufSize) {
    const char *months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int monthIdx = time.month - 1;
    if (monthIdx < 0 || monthIdx > 11) {
        monthIdx = 0;
    }
    snprintf(buf, bufSize, "%s %d, %d", months[monthIdx], time.day, time.year);
}

//===----------------------------------------------------------------------===//
// Clock Hand Angle Calculations
//===----------------------------------------------------------------------===//

/**
 * @brief Calculates the hour hand angle including minute contribution.
 *
 * The hour hand doesn't just jump to the hour position; it moves
 * continuously based on the current minutes as well. This provides
 * a more natural appearance where the hour hand is "between" hours
 * during the hour.
 *
 * ## Calculation Breakdown
 *
 * - Base: `(hours % 12) * 30` degrees (30° per hour)
 * - Adjustment: `minutes / 2` degrees (0.5° per minute)
 *
 * ## Integer Division
 *
 * The `minutes / 2` calculation uses integer division, so the hand
 * moves in 2-minute increments (1° per 2 minutes). This is imperceptible
 * to users and avoids floating-point arithmetic.
 *
 * @param time The current time.
 * @return Angle in degrees (0-359), where 0° points to 12 o'clock.
 */
int hourHandAngle(const Time &time) {
    // 360 degrees / 12 hours = 30 degrees per hour
    // Plus additional movement based on minutes
    int angle = (time.hours % 12) * 30 + time.minutes / 2;
    return angle;
}

/**
 * @brief Calculates the minute hand angle including second contribution.
 *
 * Similar to the hour hand, the minute hand moves continuously based
 * on the current seconds, providing smooth movement between minute marks.
 *
 * ## Calculation Breakdown
 *
 * - Base: `minutes * 6` degrees (6° per minute)
 * - Adjustment: `seconds / 10` degrees (0.6° per 10 seconds)
 *
 * ## Integer Division
 *
 * The `seconds / 10` calculation means the minute hand advances in
 * 10-second increments between minute positions. This is a reasonable
 * approximation that avoids floating-point math.
 *
 * @param time The current time.
 * @return Angle in degrees (0-359), where 0° points to 12 o'clock.
 */
int minuteHandAngle(const Time &time) {
    // 360 degrees / 60 minutes = 6 degrees per minute
    int angle = time.minutes * 6 + time.seconds / 10;
    return angle;
}

/**
 * @brief Calculates the second hand angle.
 *
 * Unlike the hour and minute hands, the second hand moves in discrete
 * jumps rather than continuously. Each second, it advances exactly 6°.
 *
 * ## Calculation
 *
 * `seconds * 6` degrees (6° per second = 360° / 60 seconds)
 *
 * @param time The current time.
 * @return Angle in degrees (0-354, in 6° increments), where 0° points to 12 o'clock.
 */
int secondHandAngle(const Time &time) {
    // 360 degrees / 60 seconds = 6 degrees per second
    return time.seconds * 6;
}

} // namespace clockapp
