//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_raycast2d.c
// Purpose: 2D raycasting — DDA tilemap traversal, line-segment intersection.
//
// Key invariants:
//   - DDA steps through tiles one at a time along the ray.
//   - Line-rect uses Liang-Barsky axis-aligned clipping.
//   - All positions are in pixel coordinates.
//
//===----------------------------------------------------------------------===//

#include "rt_raycast2d.h"
#include "rt_tilemap.h"

#include <math.h>
#include <stdlib.h>

//=============================================================================
// Line-Rect intersection (Liang-Barsky)
//=============================================================================

/// @brief Test whether a line segment intersects an axis-aligned rectangle (Liang-Barsky).
int8_t rt_collision_line_rect(
    double x1, double y1, double x2, double y2, double rx, double ry, double rw, double rh) {
    double dx = x2 - x1;
    double dy = y2 - y1;
    double p[4] = {-dx, dx, -dy, dy};
    double q[4] = {x1 - rx, rx + rw - x1, y1 - ry, ry + rh - y1};

    double tmin = 0.0, tmax = 1.0;
    for (int i = 0; i < 4; i++) {
        if (fabs(p[i]) < 1e-12) {
            if (q[i] < 0.0)
                return 0;
        } else {
            double t = q[i] / p[i];
            if (p[i] < 0.0) {
                if (t > tmin)
                    tmin = t;
            } else {
                if (t < tmax)
                    tmax = t;
            }
            if (tmin > tmax)
                return 0;
        }
    }
    return 1;
}

//=============================================================================
// Line-Circle intersection
//=============================================================================

/// @brief Test whether a line segment intersects a circle (quadratic formula).
int8_t rt_collision_line_circle(
    double x1, double y1, double x2, double y2, double cx, double cy, double r) {
    double dx = x2 - x1;
    double dy = y2 - y1;
    double fx = x1 - cx;
    double fy = y1 - cy;
    double a = dx * dx + dy * dy;
    double b = 2.0 * (fx * dx + fy * dy);
    double c = fx * fx + fy * fy - r * r;
    double disc = b * b - 4.0 * a * c;
    if (disc < 0.0)
        return 0;
    disc = sqrt(disc);
    double t1 = (-b - disc) / (2.0 * a);
    double t2 = (-b + disc) / (2.0 * a);
    return (t1 >= 0.0 && t1 <= 1.0) || (t2 >= 0.0 && t2 <= 1.0) || (t1 < 0.0 && t2 > 1.0);
}

//=============================================================================
// Tilemap Raycast (DDA)
//=============================================================================

/// @brief Cast a ray through a tilemap and return the first solid tile hit.
/// @details Walks from (x1,y1) to (x2,y2) in pixel steps, checking tile
///          solidity at each position. Capped at 2000 steps for safety.
/// @return 1 if a solid tile was hit (hit_x/hit_y set), 0 if clear.
int8_t rt_raycast_tilemap(
    void *tilemap, int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t *hit_x, int64_t *hit_y) {
    if (!tilemap)
        return 0;

    int64_t tw = rt_tilemap_get_tile_width(tilemap);
    int64_t th = rt_tilemap_get_tile_height(tilemap);
    if (tw <= 0 || th <= 0)
        return 0;

    // Bresenham-like stepping from (x1,y1) to (x2,y2) in tile coordinates
    int64_t dx = x2 - x1;
    int64_t dy = y2 - y1;
    int64_t steps = abs((int)(dx)) > abs((int)(dy)) ? abs((int)(dx)) : abs((int)(dy));
    if (steps == 0)
        steps = 1;
    // Step in pixels, check tile at each step
    int64_t maxSteps = steps > 2000 ? 2000 : steps; // Safety cap
    for (int64_t i = 0; i <= maxSteps; i++) {
        int64_t px = x1 + dx * i / steps;
        int64_t py = y1 + dy * i / steps;
        if (rt_tilemap_is_solid_at(tilemap, px, py)) {
            if (hit_x)
                *hit_x = px;
            if (hit_y)
                *hit_y = py;
            return 1;
        }
    }
    return 0;
}

/// @brief Check whether two points have unobstructed line of sight through a tilemap.
int8_t rt_has_line_of_sight(void *tilemap, int64_t x1, int64_t y1, int64_t x2, int64_t y2) {
    return !rt_raycast_tilemap(tilemap, x1, y1, x2, y2, NULL, NULL);
}
