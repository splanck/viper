//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_reltime.h
// Purpose: Human-readable relative time formatting producing strings like '2 hours ago', 'in 3
// days', and '45s' from Unix timestamps or millisecond durations.
//
// Key invariants:
//   - Input timestamps are Unix timestamps in seconds since epoch (UTC).
//   - rt_reltime_format computes relative to the current wall-clock time.
//   - rt_reltime_format_from uses an explicit reference timestamp for deterministic output.
//   - Short variants produce compact representations suitable for UI labels.
//
// Ownership/Lifetime:
//   - Returned strings are newly allocated and must be released by the caller.
//   - Input timestamps are plain int64_t values with no ownership transfer.
//
// Links: src/runtime/core/rt_reltime.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Format timestamp relative to current time.
    /// @details Produces strings like "2 hours ago", "in 3 days", "just now".
    /// @param timestamp Unix timestamp in seconds.
    /// @return Human-readable relative time string.
    rt_string rt_reltime_format(int64_t timestamp);

    /// @brief Format timestamp relative to a reference time.
    /// @param timestamp Unix timestamp in seconds.
    /// @param reference Reference Unix timestamp in seconds.
    /// @return Human-readable relative time string.
    rt_string rt_reltime_format_from(int64_t timestamp, int64_t reference);

    /// @brief Format a duration in milliseconds as human-readable.
    /// @details Produces strings like "2h 30m", "1d 5h 20m", "45s".
    /// @param duration_ms Duration in milliseconds.
    /// @return Human-readable duration string.
    rt_string rt_reltime_format_duration(int64_t duration_ms);

    /// @brief Format timestamp relative to now in short form.
    /// @details Produces strings like "2h", "3d", "5m", "now".
    /// @param timestamp Unix timestamp in seconds.
    /// @return Short relative time string.
    rt_string rt_reltime_format_short(int64_t timestamp);

#ifdef __cplusplus
}
#endif
