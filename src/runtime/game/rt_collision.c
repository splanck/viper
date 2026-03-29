//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_collision.c
// Purpose: Stateless AABB and circle collision primitive library for Viper
//   games. Provides GC-managed CollisionRect and CollisionCircle value objects
//   alongside stand-alone overlap and containment test functions. Intended as
//   a lightweight helper layer on top of Physics2D for game logic that needs
//   simple hit tests without a full simulation world (e.g. trigger zones,
//   projectile hit detection, UI hover regions).
//
// Key invariants:
//   - CollisionRect is an axis-aligned bounding box (AABB) with double x, y,
//     width, height fields. x, y is the top-left corner.
//   - CollisionCircle has double cx, cy center and double radius.
//   - All overlap tests return int64: 1 = overlapping, 0 = separated.
//     Tests are strict (touching edges count as overlapping unless noted).
//   - rt_collision_rect_vs_rect: SAT test on both axes, returns 1 if any area
//     overlaps (not just touches). Two rects sharing only an edge return 0.
//   - rt_collision_circle_vs_circle: distance-squared comparison to avoid
//     sqrt; returns 1 when distance < sum_of_radii.
//   - rt_collision_point_in_rect / _circle: point containment, inclusive of
//     the boundary.
//   - Standalone functions (not methods on rect/circle objects) are also
//     provided for callers that store geometry inline rather than in objects.
//
// Ownership/Lifetime:
//   - CollisionRect and CollisionCircle objects are GC-managed (rt_obj_new_i64).
//     They hold no external resources and require no finalizer.
//
// Links: src/runtime/game/rt_collision.h (public API),
//        src/runtime/graphics/rt_physics2d.h (full physics simulation),
//        docs/viperlib/game.md (Collision section)
//
//===----------------------------------------------------------------------===//

#include "rt_collision.h"
#include "rt_object.h"

#include <math.h>
#include <stdlib.h>

/// Internal structure for CollisionRect.
struct rt_collision_rect_impl {
    double x;      ///< Left edge.
    double y;      ///< Top edge.
    double width;  ///< Width.
    double height; ///< Height.
};

/// @brief Create a new axis-aligned collision rectangle.
/// @details Allocates a GC-managed AABB with the given top-left position and dimensions.
///          Negative width/height are clamped to zero.
rt_collision_rect rt_collision_rect_new(double x, double y, double width, double height) {
    struct rt_collision_rect_impl *rect = (struct rt_collision_rect_impl *)rt_obj_new_i64(
        0, (int64_t)sizeof(struct rt_collision_rect_impl));
    if (!rect)
        return NULL;

    rect->x = x;
    rect->y = y;
    rect->width = width > 0 ? width : 0;
    rect->height = height > 0 ? height : 0;

    return rect;
}

/// @brief Release a collision rectangle, decrementing its reference count.
void rt_collision_rect_destroy(rt_collision_rect rect) {
    if (rect && rt_obj_release_check0(rect))
        rt_obj_free(rect);
}

/// @brief Return the X coordinate (left edge) of the collision rectangle.
double rt_collision_rect_x(rt_collision_rect rect) {
    return rect ? rect->x : 0.0;
}

/// @brief Return the Y coordinate (top edge) of the collision rectangle.
double rt_collision_rect_y(rt_collision_rect rect) {
    return rect ? rect->y : 0.0;
}

/// @brief Return the width of the collision rectangle.
double rt_collision_rect_width(rt_collision_rect rect) {
    return rect ? rect->width : 0.0;
}

/// @brief Return the height of the collision rectangle.
double rt_collision_rect_height(rt_collision_rect rect) {
    return rect ? rect->height : 0.0;
}

/// @brief Return the right edge (x + width) of the collision rectangle.
double rt_collision_rect_right(rt_collision_rect rect) {
    return rect ? rect->x + rect->width : 0.0;
}

/// @brief Return the bottom edge (y + height) of the collision rectangle.
double rt_collision_rect_bottom(rt_collision_rect rect) {
    return rect ? rect->y + rect->height : 0.0;
}

/// @brief Return the X coordinate of the rectangle's center point.
double rt_collision_rect_center_x(rt_collision_rect rect) {
    return rect ? rect->x + rect->width * 0.5 : 0.0;
}

/// @brief Return the Y coordinate of the rectangle's center point.
double rt_collision_rect_center_y(rt_collision_rect rect) {
    return rect ? rect->y + rect->height * 0.5 : 0.0;
}

/// @brief Move the rectangle to a new top-left position.
void rt_collision_rect_set_position(rt_collision_rect rect, double x, double y) {
    if (!rect)
        return;
    rect->x = x;
    rect->y = y;
}

/// @brief Set the width and height of the collision rectangle.
/// @details Negative dimensions are clamped to zero.
void rt_collision_rect_set_size(rt_collision_rect rect, double width, double height) {
    if (!rect)
        return;
    rect->width = width > 0 ? width : 0;
    rect->height = height > 0 ? height : 0;
}

/// @brief Set all four components (x, y, width, height) of the collision rectangle.
/// @details Negative dimensions are clamped to zero.
void rt_collision_rect_set(
    rt_collision_rect rect, double x, double y, double width, double height) {
    if (!rect)
        return;
    rect->x = x;
    rect->y = y;
    rect->width = width > 0 ? width : 0;
    rect->height = height > 0 ? height : 0;
}

/// @brief Reposition the rectangle so its center is at (cx, cy).
/// @details Computes new top-left from center minus half-dimensions.
void rt_collision_rect_set_center(rt_collision_rect rect, double cx, double cy) {
    if (!rect)
        return;
    rect->x = cx - rect->width * 0.5;
    rect->y = cy - rect->height * 0.5;
}

/// @brief Translate the rectangle by (dx, dy) relative to its current position.
void rt_collision_rect_move(rt_collision_rect rect, double dx, double dy) {
    if (!rect)
        return;
    rect->x += dx;
    rect->y += dy;
}

/// @brief Test whether a point (px, py) lies inside the collision rectangle.
/// @details Inclusive on left/top edges, exclusive on right/bottom edges.
int8_t rt_collision_rect_contains_point(rt_collision_rect rect, double px, double py) {
    if (!rect)
        return 0;
    return px >= rect->x && px < rect->x + rect->width && py >= rect->y &&
           py < rect->y + rect->height;
}

/// @brief Test whether two axis-aligned rectangles overlap.
/// @details Uses the separating-axis theorem on both X and Y axes. Returns 1
///          if the rectangles share any interior area, 0 if separated.
int8_t rt_collision_rect_overlaps(rt_collision_rect rect, rt_collision_rect other) {
    if (!rect || !other)
        return 0;
    return rt_collision_rect_overlaps_rect(rect, other->x, other->y, other->width, other->height);
}

int8_t rt_collision_rect_overlaps_rect(
    rt_collision_rect rect, double ox, double oy, double ow, double oh) {
    if (!rect)
        return 0;

    double r1_left = rect->x;
    double r1_right = rect->x + rect->width;
    double r1_top = rect->y;
    double r1_bottom = rect->y + rect->height;

    double r2_left = ox;
    double r2_right = ox + ow;
    double r2_top = oy;
    double r2_bottom = oy + oh;

    // Check for no overlap
    if (r1_right <= r2_left || r2_right <= r1_left || r1_bottom <= r2_top || r2_bottom <= r1_top) {
        return 0;
    }
    return 1;
}

/// @brief Compute the overlap distance along the X axis between two rectangles.
/// @details Returns 0.0 if the rectangles do not overlap on the X axis.
double rt_collision_rect_overlap_x(rt_collision_rect rect, rt_collision_rect other) {
    if (!rect || !other)
        return 0.0;

    double r1_left = rect->x;
    double r1_right = rect->x + rect->width;
    double r2_left = other->x;
    double r2_right = other->x + other->width;

    // Calculate overlap
    double overlap_left = r1_right - r2_left;
    double overlap_right = r2_right - r1_left;

    if (overlap_left <= 0 || overlap_right <= 0)
        return 0.0;

    // Return the smaller overlap (minimum penetration)
    return (overlap_left < overlap_right) ? overlap_left : -overlap_right;
}

/// @brief Compute the overlap distance along the Y axis between two rectangles.
/// @details Returns 0.0 if the rectangles do not overlap on the Y axis.
double rt_collision_rect_overlap_y(rt_collision_rect rect, rt_collision_rect other) {
    if (!rect || !other)
        return 0.0;

    double r1_top = rect->y;
    double r1_bottom = rect->y + rect->height;
    double r2_top = other->y;
    double r2_bottom = other->y + other->height;

    // Calculate overlap
    double overlap_top = r1_bottom - r2_top;
    double overlap_bottom = r2_bottom - r1_top;

    if (overlap_top <= 0 || overlap_bottom <= 0)
        return 0.0;

    // Return the smaller overlap (minimum penetration)
    return (overlap_top < overlap_bottom) ? overlap_top : -overlap_bottom;
}

/// @brief Create a new rectangle expanded by the given amounts on each side.
/// @details The result is larger by 2*dx horizontally and 2*dy vertically,
///          centered on the same point as the original.
void rt_collision_rect_expand(rt_collision_rect rect, double margin) {
    if (!rect)
        return;
    rect->x -= margin;
    rect->y -= margin;
    rect->width += margin * 2;
    rect->height += margin * 2;

    // Ensure non-negative size
    if (rect->width < 0)
        rect->width = 0;
    if (rect->height < 0)
        rect->height = 0;
}

/// @brief Test whether this rectangle fully contains another rectangle.
/// @details Returns 1 if the other rectangle is entirely within the bounds
///          of this one (inclusive on all edges).
int8_t rt_collision_rect_contains_rect(rt_collision_rect rect, rt_collision_rect other) {
    if (!rect || !other)
        return 0;

    return other->x >= rect->x && other->y >= rect->y &&
           other->x + other->width <= rect->x + rect->width &&
           other->y + other->height <= rect->y + rect->height;
}

//=============================================================================
// Static collision helpers
//=============================================================================

int8_t rt_collision_rects_overlap(
    double x1, double y1, double w1, double h1, double x2, double y2, double w2, double h2) {
    if (x1 + w1 <= x2 || x2 + w2 <= x1 || y1 + h1 <= y2 || y2 + h2 <= y1) {
        return 0;
    }
    return 1;
}

/// @brief Standalone function: test if point (px, py) is inside rect (rx, ry, rw, rh).
/// @details Inclusive on left/top, exclusive on right/bottom.
int8_t rt_collision_point_in_rect(
    double px, double py, double rx, double ry, double rw, double rh) {
    return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
}

int8_t rt_collision_circles_overlap(
    double x1, double y1, double r1, double x2, double y2, double r2) {
    double dx = x2 - x1;
    double dy = y2 - y1;
    double dist_sq = dx * dx + dy * dy;
    double radii = r1 + r2;
    return dist_sq < radii * radii;
}

/// @brief Standalone function: test if point (px, py) is inside a circle.
/// @details Uses distance-squared comparison to avoid sqrt.
int8_t rt_collision_point_in_circle(double px, double py, double cx, double cy, double r) {
    double dx = px - cx;
    double dy = py - cy;
    return dx * dx + dy * dy < r * r;
}

int8_t rt_collision_circle_rect(
    double cx, double cy, double r, double rx, double ry, double rw, double rh) {
    // Find closest point on rectangle to circle center
    double closest_x = cx;
    double closest_y = cy;

    if (cx < rx)
        closest_x = rx;
    else if (cx > rx + rw)
        closest_x = rx + rw;

    if (cy < ry)
        closest_y = ry;
    else if (cy > ry + rh)
        closest_y = ry + rh;

    // Check if closest point is within circle
    double dx = cx - closest_x;
    double dy = cy - closest_y;
    return dx * dx + dy * dy < r * r;
}

/// @brief Compute the Euclidean distance between two points.
double rt_collision_distance(double x1, double y1, double x2, double y2) {
    double dx = x2 - x1;
    double dy = y2 - y1;
    return sqrt(dx * dx + dy * dy);
}

/// @brief Compute the squared Euclidean distance between two points.
/// @details Avoids the sqrt call, useful for comparison-only scenarios.
double rt_collision_distance_squared(double x1, double y1, double x2, double y2) {
    double dx = x2 - x1;
    double dy = y2 - y1;
    return dx * dx + dy * dy;
}
