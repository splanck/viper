//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/world/rt_timeofday3d.h
// Purpose: Public C ABI for the deterministic time-of-day clock driving a sun
//   Light3D, a Sky3D, and reflection-probe re-capture flags.
// Key invariants:
//   - Advance(dt) is the only clock input; no wall-clock reads.
// Ownership/Lifetime:
//   - GC-managed; bound consumers are retained until rebound/finalized.
// Links: rt_timeofday3d.c, misc/plans/thirdpersonupgrade/16-timeofday-weather.md
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Create a paused clock at solar noon (latitude 35, refresh 2 degrees).
void *rt_timeofday3d_new(void);
/// @brief Clock hour in [0, 24); wraps.
void rt_timeofday3d_set_hours(void *tod, double hours);
double rt_timeofday3d_get_hours(void *tod);
/// @brief Real seconds per 24h day; 0 pauses the clock (drive Hours manually).
void rt_timeofday3d_set_day_length_seconds(void *tod, double seconds);
double rt_timeofday3d_get_day_length_seconds(void *tod);
/// @brief Latitude tilt applied to the sun arc (-85..85 degrees).
void rt_timeofday3d_set_latitude_degrees(void *tod, double degrees);
double rt_timeofday3d_get_latitude_degrees(void *tod);
/// @brief Sun movement (degrees) that triggers sky/probe refresh (throttle).
void rt_timeofday3d_set_refresh_degrees(void *tod, double degrees);
double rt_timeofday3d_get_refresh_degrees(void *tod);
/// @brief Bind the driven sun light / procedural sky / re-capture probe (NULL clears).
void rt_timeofday3d_set_sun_light(void *tod, void *light);
void rt_timeofday3d_set_sky(void *tod, void *sky);
void rt_timeofday3d_set_reflection_probe(void *tod, void *probe);
/// @brief Direction TOWARD the sun for the current hour (raw and Vec3 forms).
void rt_timeofday3d_get_sun_direction_raw(void *tod, double out_dir[3]);
void *rt_timeofday3d_get_sun_direction(void *tod);
/// @brief Advance by @p dt seconds and drive bound consumers (canvas may be NULL).
void rt_timeofday3d_advance(void *tod, double dt, void *canvas);

#ifdef __cplusplus
}
#endif
