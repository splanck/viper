#pragma once
//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file clock.hpp
 * @brief Clock application time management and formatting utilities.
 *
 * This file defines the core time handling functionality for the ViperDOS
 * clock application. It provides structures and functions for obtaining the
 * current system time, formatting time strings for display, and calculating
 * clock hand angles for the analog clock face.
 *
 * ## Architecture
 *
 * The clock application separates time logic from rendering:
 * - **clock.hpp/cpp**: Time retrieval, formatting, and angle calculations
 * - **ui.hpp/cpp**: Visual rendering of analog and digital clocks
 * - **main.cpp**: Event loop and user interaction
 *
 * ## Time Sources
 *
 * The clock obtains time from the standard C library `time()` function,
 * which in ViperDOS queries the system's real-time clock or falls back to
 * boot time plus uptime if no RTC is available.
 *
 * ## Angle Calculation
 *
 * Clock hand angles are calculated in degrees with 0° representing 12 o'clock:
 * - Hour hand: Moves 30° per hour (360°/12), plus fractional movement from minutes
 * - Minute hand: Moves 6° per minute (360°/60), plus fractional from seconds
 * - Second hand: Moves 6° per second (360°/60)
 *
 * @see ui.hpp for clock rendering
 */
//===----------------------------------------------------------------------===//

#include <stdint.h>

namespace clockapp {

//===----------------------------------------------------------------------===//
// Time Structure
//===----------------------------------------------------------------------===//

/**
 * @brief Represents a point in time with date and time components.
 *
 * This structure holds the current time broken down into individual components
 * for easy access by formatting functions and clock hand calculations. All
 * fields use local time (adjusted for timezone if the system supports it).
 *
 * ## Value Ranges
 *
 * | Field   | Range        | Description            |
 * |---------|--------------|------------------------|
 * | hours   | 0-23         | Hour in 24-hour format |
 * | minutes | 0-59         | Minutes past the hour  |
 * | seconds | 0-59         | Seconds past the minute|
 * | day     | 1-31         | Day of the month       |
 * | month   | 1-12         | Month (1=January)      |
 * | year    | 1970+        | Full 4-digit year      |
 *
 * @note The `hours` field is always in 24-hour format internally. The UI
 *       handles conversion to 12-hour format for display when requested.
 */
struct Time {
    int hours;   /**< Hour of day (0-23, 24-hour format). */
    int minutes; /**< Minutes past the hour (0-59). */
    int seconds; /**< Seconds past the minute (0-59). */
    int day;     /**< Day of the month (1-31). */
    int month;   /**< Month of the year (1-12, 1=January). */
    int year;    /**< Full year (e.g., 2024). */
};

//===----------------------------------------------------------------------===//
// Time Retrieval
//===----------------------------------------------------------------------===//

/**
 * @brief Retrieves the current system time.
 *
 * Queries the operating system for the current local time and populates
 * the Time structure with the result. On ViperDOS, this uses the standard
 * C library `time()` and `localtime()` functions.
 *
 * ## Fallback Behavior
 *
 * If the system time cannot be obtained (e.g., `localtime()` returns NULL),
 * the function falls back to a default time of midnight on January 1, 2024.
 * This ensures the clock always displays something reasonable even if the
 * system clock is not available.
 *
 * @param[out] time Reference to Time structure to populate with current time.
 *
 * @note The time returned is in local time, not UTC.
 *
 * @code
 * Time current;
 * getCurrentTime(current);
 * printf("Current time: %02d:%02d:%02d\n",
 *        current.hours, current.minutes, current.seconds);
 * @endcode
 */
void getCurrentTime(Time &time);

//===----------------------------------------------------------------------===//
// Time Formatting
//===----------------------------------------------------------------------===//

/**
 * @brief Formats the time in 12-hour format with AM/PM suffix.
 *
 * Produces a time string in the format "HH:MM:SS AM" or "HH:MM:SS PM".
 * Hours are displayed as 1-12 (not 0-11), and the appropriate AM/PM
 * suffix is appended based on whether the time is before or after noon.
 *
 * ## Output Format
 *
 * - Hours: 1-12 with leading space for single digits (e.g., " 9:30:00")
 * - Minutes/Seconds: Always two digits with leading zeros
 * - AM/PM: Uppercase, separated by single space
 *
 * ## Examples
 *
 * | 24-hour | 12-hour output   |
 * |---------|------------------|
 * | 00:00   | 12:00:00 AM      |
 * | 09:30   |  9:30:00 AM      |
 * | 12:00   | 12:00:00 PM      |
 * | 13:45   |  1:45:00 PM      |
 * | 23:59   | 11:59:00 PM      |
 *
 * @param time    The Time structure containing the time to format.
 * @param buf     Output buffer to receive the formatted string.
 * @param bufSize Size of the output buffer in bytes.
 *
 * @note The buffer should be at least 12 bytes to hold the full string
 *       plus null terminator ("12:00:00 PM\0").
 *
 * @see formatTime24() for 24-hour format
 */
void formatTime12(const Time &time, char *buf, int bufSize);

/**
 * @brief Formats the time in 24-hour (military) format.
 *
 * Produces a time string in the format "HH:MM:SS" using 24-hour notation.
 * All components are zero-padded to two digits.
 *
 * ## Output Format
 *
 * - Hours: 00-23 with leading zeros
 * - Minutes/Seconds: 00-59 with leading zeros
 * - No AM/PM suffix
 *
 * ## Examples
 *
 * | Time        | Output    |
 * |-------------|-----------|
 * | Midnight    | 00:00:00  |
 * | 9:30 AM     | 09:30:00  |
 * | Noon        | 12:00:00  |
 * | 1:45 PM     | 13:45:00  |
 * | 11:59 PM    | 23:59:00  |
 *
 * @param time    The Time structure containing the time to format.
 * @param buf     Output buffer to receive the formatted string.
 * @param bufSize Size of the output buffer in bytes.
 *
 * @note The buffer should be at least 9 bytes to hold the full string
 *       plus null terminator ("00:00:00\0").
 *
 * @see formatTime12() for 12-hour format with AM/PM
 */
void formatTime24(const Time &time, char *buf, int bufSize);

/**
 * @brief Formats the date in a human-readable format.
 *
 * Produces a date string in the format "Mon DD, YYYY" where:
 * - Mon: 3-letter abbreviated month name
 * - DD: Day of month (no leading zero)
 * - YYYY: Full 4-digit year
 *
 * ## Examples
 *
 * | Date       | Output        |
 * |------------|---------------|
 * | 2024-01-01 | Jan 1, 2024   |
 * | 2024-07-04 | Jul 4, 2024   |
 * | 2024-12-25 | Dec 25, 2024  |
 *
 * @param time    The Time structure containing the date to format.
 * @param buf     Output buffer to receive the formatted string.
 * @param bufSize Size of the output buffer in bytes.
 *
 * @note The buffer should be at least 13 bytes to hold strings like
 *       "Dec 25, 2024\0".
 *
 * @note If the month value is out of range (not 1-12), "Jan" is used
 *       as a fallback.
 */
void formatDate(const Time &time, char *buf, int bufSize);

//===----------------------------------------------------------------------===//
// Clock Hand Angles
//===----------------------------------------------------------------------===//

/**
 * @brief Calculates the angle of the hour hand.
 *
 * Computes the rotation angle for the hour hand based on the current time.
 * The hour hand moves continuously (not just at the hour mark), advancing
 * based on both hours and minutes for a smooth sweep.
 *
 * ## Calculation
 *
 * ```
 * angle = (hours % 12) * 30 + minutes / 2
 * ```
 *
 * - 30° per hour (360° / 12 hours)
 * - 0.5° per minute (30° / 60 minutes)
 *
 * ## Examples
 *
 * | Time  | Angle |
 * |-------|-------|
 * | 12:00 | 0°    |
 * | 3:00  | 90°   |
 * | 6:00  | 180°  |
 * | 9:00  | 270°  |
 * | 12:30 | 15°   |
 *
 * @param time The Time structure containing the current time.
 * @return The angle in degrees (0-359), where 0° is 12 o'clock.
 *
 * @see minuteHandAngle() for minute hand calculation
 * @see secondHandAngle() for second hand calculation
 */
int hourHandAngle(const Time &time);

/**
 * @brief Calculates the angle of the minute hand.
 *
 * Computes the rotation angle for the minute hand based on the current time.
 * The minute hand moves continuously, advancing based on both minutes and
 * seconds for a smooth sweep.
 *
 * ## Calculation
 *
 * ```
 * angle = minutes * 6 + seconds / 10
 * ```
 *
 * - 6° per minute (360° / 60 minutes)
 * - 0.1° per second (6° / 60 seconds)
 *
 * ## Examples
 *
 * | Time  | Angle |
 * |-------|-------|
 * | :00   | 0°    |
 * | :15   | 90°   |
 * | :30   | 180°  |
 * | :45   | 270°  |
 *
 * @param time The Time structure containing the current time.
 * @return The angle in degrees (0-359), where 0° is 12 o'clock.
 *
 * @see hourHandAngle() for hour hand calculation
 * @see secondHandAngle() for second hand calculation
 */
int minuteHandAngle(const Time &time);

/**
 * @brief Calculates the angle of the second hand.
 *
 * Computes the rotation angle for the second hand based on the current
 * seconds value. Unlike the hour and minute hands, the second hand moves
 * in discrete 6° increments (once per second) rather than continuously.
 *
 * ## Calculation
 *
 * ```
 * angle = seconds * 6
 * ```
 *
 * - 6° per second (360° / 60 seconds)
 *
 * ## Examples
 *
 * | Seconds | Angle |
 * |---------|-------|
 * | 0       | 0°    |
 * | 15      | 90°   |
 * | 30      | 180°  |
 * | 45      | 270°  |
 *
 * @param time The Time structure containing the current time.
 * @return The angle in degrees (0-354), where 0° is 12 o'clock.
 *
 * @see hourHandAngle() for hour hand calculation
 * @see minuteHandAngle() for minute hand calculation
 */
int secondHandAngle(const Time &time);

} // namespace clockapp
