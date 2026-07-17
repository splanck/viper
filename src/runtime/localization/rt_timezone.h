//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/localization/rt_timezone.h
// Purpose: Public runtime API for deterministic IANA time-zone lookup and
//          DateTime formatting in named zones.
// Key invariants:
//   - Zone lookup uses an embedded, checked-in data subset, never host zoneinfo.
//   - Timestamp inputs are Unix seconds in UTC.
//   - Offsets are seconds east of UTC and include DST when active.
// Ownership/Lifetime:
//   - TimeZone handles point at static runtime data and must not be freed.
//   - Returned strings are newly allocated rt_string values.
// Links: src/runtime/localization/rt_timezone.c, docs/zannalib/time.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Resolve an IANA time-zone name from the embedded subset.
/// @param name Zone name such as "America/New_York".
/// @return Opaque TimeZone handle, or traps when @p name is unknown.
void *rt_tz_find(rt_string name);

/// @brief Return the canonical IANA name for a TimeZone handle.
/// @param timezone Opaque TimeZone handle returned by @ref rt_tz_find.
/// @return Newly allocated runtime string.
rt_string rt_tz_name(void *timezone_obj);

/// @brief Return the UTC offset active at @p utc_timestamp.
/// @param timezone Opaque TimeZone handle returned by @ref rt_tz_find.
/// @param utc_timestamp Unix seconds in UTC.
/// @return Offset in seconds east of UTC.
int64_t rt_tz_offset_at(void *timezone_obj, int64_t utc_timestamp);

/// @brief Return whether the zone is observing daylight saving time at @p utc_timestamp.
/// @param timezone Opaque TimeZone handle returned by @ref rt_tz_find.
/// @param utc_timestamp Unix seconds in UTC.
/// @return 1 when DST is active, 0 otherwise.
int8_t rt_tz_is_dst_at(void *timezone_obj, int64_t utc_timestamp);

/// @brief Format an instant as ISO local wall time plus numeric zone offset.
/// @param timestamp Unix seconds in UTC.
/// @param timezone Opaque TimeZone handle returned by @ref rt_tz_find.
/// @return String like "2025-03-09T03:00:00-04:00".
rt_string rt_datetime_to_zone(int64_t timestamp, void *timezone_obj);

/// @brief Format an instant using a deterministic strftime-like subset in a zone.
/// @param timestamp Unix seconds in UTC.
/// @param timezone Opaque TimeZone handle returned by @ref rt_tz_find.
/// @param format Format string. Supports %Y, %m, %d, %H, %M, %S, %F, %T, %z, %Z, and %%.
/// @return Newly allocated formatted string, or empty string for invalid format input.
rt_string rt_datetime_format_in_zone(int64_t timestamp, void *timezone_obj, rt_string format);

#ifdef __cplusplus
}
#endif
