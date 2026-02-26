//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_dateonly.h
// Purpose: DateOnly type representing a calendar date without time or timezone components,
// providing creation, parsing, arithmetic, and formatting operations.
//
// Key invariants:
//   - Month is 1-indexed (1=January, 12=December); day is 1-indexed.
//   - Days since epoch are counted from 1970-01-01 (Unix epoch, day 0).
//   - ISO format for parsing and formatting is YYYY-MM-DD.
//   - Date arithmetic (add/subtract days) wraps correctly across month and year boundaries.
//
// Ownership/Lifetime:
//   - DateOnly objects are heap-allocated opaque pointers.
//   - Callers are responsible for managing object lifetime; no reference counting.
//
// Links: src/runtime/core/rt_dateonly.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=========================================================================
    // DateOnly Creation
    //=========================================================================

    /// @brief Create a DateOnly from year, month, day.
    /// @param year Year (e.g., 2024).
    /// @param month Month (1-12).
    /// @param day Day (1-31).
    /// @return Opaque DateOnly object pointer.
    void *rt_dateonly_create(int64_t year, int64_t month, int64_t day);

    /// @brief Get today's date.
    /// @return Opaque DateOnly object pointer.
    void *rt_dateonly_today(void);

    /// @brief Parse a date from ISO format string (YYYY-MM-DD).
    /// @param s Date string.
    /// @return Opaque DateOnly object pointer, or NULL if invalid.
    void *rt_dateonly_parse(rt_string s);

    /// @brief Create from days since epoch (Jan 1, 1970).
    /// @param days Days since epoch.
    /// @return Opaque DateOnly object pointer.
    void *rt_dateonly_from_days(int64_t days);

    //=========================================================================
    // Component Access
    //=========================================================================

    /// @brief Get the year component.
    /// @param obj Opaque DateOnly object pointer.
    /// @return Year (e.g., 2024).
    int64_t rt_dateonly_year(void *obj);

    /// @brief Get the month component.
    /// @param obj Opaque DateOnly object pointer.
    /// @return Month (1-12).
    int64_t rt_dateonly_month(void *obj);

    /// @brief Get the day component.
    /// @param obj Opaque DateOnly object pointer.
    /// @return Day (1-31).
    int64_t rt_dateonly_day(void *obj);

    /// @brief Get the day of week (0=Sunday, 6=Saturday).
    /// @param obj Opaque DateOnly object pointer.
    /// @return Day of week.
    int64_t rt_dateonly_day_of_week(void *obj);

    /// @brief Get the day of year (1-366).
    /// @param obj Opaque DateOnly object pointer.
    /// @return Day of year.
    int64_t rt_dateonly_day_of_year(void *obj);

    /// @brief Get days since epoch (Jan 1, 1970).
    /// @param obj Opaque DateOnly object pointer.
    /// @return Days since epoch.
    int64_t rt_dateonly_to_days(void *obj);

    //=========================================================================
    // Date Arithmetic
    //=========================================================================

    /// @brief Add days to the date.
    /// @param obj Opaque DateOnly object pointer.
    /// @param days Number of days to add (can be negative).
    /// @return New DateOnly object.
    void *rt_dateonly_add_days(void *obj, int64_t days);

    /// @brief Add months to the date.
    /// @param obj Opaque DateOnly object pointer.
    /// @param months Number of months to add (can be negative).
    /// @return New DateOnly object.
    void *rt_dateonly_add_months(void *obj, int64_t months);

    /// @brief Add years to the date.
    /// @param obj Opaque DateOnly object pointer.
    /// @param years Number of years to add (can be negative).
    /// @return New DateOnly object.
    void *rt_dateonly_add_years(void *obj, int64_t years);

    /// @brief Get the difference in days between two dates.
    /// @param a First date.
    /// @param b Second date.
    /// @return Number of days (a - b).
    int64_t rt_dateonly_diff_days(void *a, void *b);

    //=========================================================================
    // Date Queries
    //=========================================================================

    /// @brief Check if the year is a leap year.
    /// @param obj Opaque DateOnly object pointer.
    /// @return 1 if leap year, 0 otherwise.
    int8_t rt_dateonly_is_leap_year(void *obj);

    /// @brief Get the number of days in the month.
    /// @param obj Opaque DateOnly object pointer.
    /// @return Number of days (28-31).
    int64_t rt_dateonly_days_in_month(void *obj);

    /// @brief Get the first day of the month.
    /// @param obj Opaque DateOnly object pointer.
    /// @return New DateOnly for first of month.
    void *rt_dateonly_start_of_month(void *obj);

    /// @brief Get the last day of the month.
    /// @param obj Opaque DateOnly object pointer.
    /// @return New DateOnly for last of month.
    void *rt_dateonly_end_of_month(void *obj);

    /// @brief Get the first day of the year.
    /// @param obj Opaque DateOnly object pointer.
    /// @return New DateOnly for Jan 1.
    void *rt_dateonly_start_of_year(void *obj);

    /// @brief Get the last day of the year.
    /// @param obj Opaque DateOnly object pointer.
    /// @return New DateOnly for Dec 31.
    void *rt_dateonly_end_of_year(void *obj);

    //=========================================================================
    // Comparison
    //=========================================================================

    /// @brief Compare two dates.
    /// @param a First date.
    /// @param b Second date.
    /// @return -1 if a < b, 0 if equal, 1 if a > b.
    int64_t rt_dateonly_cmp(void *a, void *b);

    /// @brief Check equality of two dates.
    /// @param a First date.
    /// @param b Second date.
    /// @return 1 if equal, 0 otherwise.
    int8_t rt_dateonly_equals(void *a, void *b);

    //=========================================================================
    // Formatting
    //=========================================================================

    /// @brief Convert to ISO format string (YYYY-MM-DD).
    /// @param obj Opaque DateOnly object pointer.
    /// @return ISO format string.
    rt_string rt_dateonly_to_string(void *obj);

    /// @brief Format using custom format string.
    /// @param obj Opaque DateOnly object pointer.
    /// @param fmt Format string (supports %Y, %m, %d, %B, %b, %A, %a).
    /// @return Formatted string.
    rt_string rt_dateonly_format(void *obj, rt_string fmt);

#ifdef __cplusplus
}
#endif
