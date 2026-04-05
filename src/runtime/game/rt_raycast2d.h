//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_raycast2d.h
// Purpose: 2D raycasting for tilemap line-of-sight and line intersection.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Cast ray through tilemap. Returns 1 if solid tile hit.
int8_t rt_raycast_tilemap(
    void *tilemap, int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t *hit_x, int64_t *hit_y);

/// @brief Check line of sight: returns 1 if NO solid tile blocks the path.
int8_t rt_has_line_of_sight(void *tilemap, int64_t x1, int64_t y1, int64_t x2, int64_t y2);

/// @brief Line segment vs AABB intersection test.
int8_t rt_collision_line_rect(
    double x1, double y1, double x2, double y2, double rx, double ry, double rw, double rh);

/// @brief Line segment vs circle intersection test.
int8_t rt_collision_line_circle(
    double x1, double y1, double x2, double y2, double cx, double cy, double r);

#ifdef __cplusplus
}
#endif
