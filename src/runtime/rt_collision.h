//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_collision.h
/// @brief AABB collision detection helpers for games.
///
/// Provides axis-aligned bounding box (AABB) collision detection,
/// including overlap testing, point containment, and collision response.
///
//===----------------------------------------------------------------------===//

#ifndef VIPER_RT_COLLISION_H
#define VIPER_RT_COLLISION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Opaque handle to a CollisionRect instance.
typedef struct rt_collision_rect_impl *rt_collision_rect;

/// Creates a new CollisionRect.
/// @param x Left edge.
/// @param y Top edge.
/// @param width Width.
/// @param height Height.
/// @return A new CollisionRect instance.
rt_collision_rect rt_collision_rect_new(double x, double y, double width, double height);

/// Destroys a CollisionRect and frees its memory.
/// @param rect The rect to destroy.
void rt_collision_rect_destroy(rt_collision_rect rect);

/// Gets the X position (left edge).
/// @param rect The rect.
/// @return X coordinate.
double rt_collision_rect_x(rt_collision_rect rect);

/// Gets the Y position (top edge).
/// @param rect The rect.
/// @return Y coordinate.
double rt_collision_rect_y(rt_collision_rect rect);

/// Gets the width.
/// @param rect The rect.
/// @return Width.
double rt_collision_rect_width(rt_collision_rect rect);

/// Gets the height.
/// @param rect The rect.
/// @return Height.
double rt_collision_rect_height(rt_collision_rect rect);

/// Gets the right edge (x + width).
/// @param rect The rect.
/// @return Right edge coordinate.
double rt_collision_rect_right(rt_collision_rect rect);

/// Gets the bottom edge (y + height).
/// @param rect The rect.
/// @return Bottom edge coordinate.
double rt_collision_rect_bottom(rt_collision_rect rect);

/// Gets the center X coordinate.
/// @param rect The rect.
/// @return Center X.
double rt_collision_rect_center_x(rt_collision_rect rect);

/// Gets the center Y coordinate.
/// @param rect The rect.
/// @return Center Y.
double rt_collision_rect_center_y(rt_collision_rect rect);

/// Sets the position (top-left corner).
/// @param rect The rect.
/// @param x New X coordinate.
/// @param y New Y coordinate.
void rt_collision_rect_set_position(rt_collision_rect rect, double x, double y);

/// Sets the size.
/// @param rect The rect.
/// @param width New width.
/// @param height New height.
void rt_collision_rect_set_size(rt_collision_rect rect, double width, double height);

/// Sets position and size.
/// @param rect The rect.
/// @param x X coordinate.
/// @param y Y coordinate.
/// @param width Width.
/// @param height Height.
void rt_collision_rect_set(rt_collision_rect rect, double x, double y, double width, double height);

/// Sets position by center point.
/// @param rect The rect.
/// @param cx Center X.
/// @param cy Center Y.
void rt_collision_rect_set_center(rt_collision_rect rect, double cx, double cy);

/// Moves the rect by a delta.
/// @param rect The rect.
/// @param dx X displacement.
/// @param dy Y displacement.
void rt_collision_rect_move(rt_collision_rect rect, double dx, double dy);

/// Checks if a point is inside the rect.
/// @param rect The rect.
/// @param px Point X.
/// @param py Point Y.
/// @return 1 if inside, 0 otherwise.
int8_t rt_collision_rect_contains_point(rt_collision_rect rect, double px, double py);

/// Checks if another rect overlaps with this one.
/// @param rect This rect.
/// @param other The other rect.
/// @return 1 if overlapping, 0 otherwise.
int8_t rt_collision_rect_overlaps(rt_collision_rect rect, rt_collision_rect other);

/// Checks if another rect overlaps using raw coordinates.
/// @param rect This rect.
/// @param ox Other rect X.
/// @param oy Other rect Y.
/// @param ow Other rect width.
/// @param oh Other rect height.
/// @return 1 if overlapping, 0 otherwise.
int8_t rt_collision_rect_overlaps_rect(rt_collision_rect rect, double ox, double oy, double ow, double oh);

/// Gets the overlap amount on the X axis (0 if no overlap).
/// @param rect This rect.
/// @param other The other rect.
/// @return Overlap depth on X axis (positive = overlap).
double rt_collision_rect_overlap_x(rt_collision_rect rect, rt_collision_rect other);

/// Gets the overlap amount on the Y axis (0 if no overlap).
/// @param rect This rect.
/// @param other The other rect.
/// @return Overlap depth on Y axis (positive = overlap).
double rt_collision_rect_overlap_y(rt_collision_rect rect, rt_collision_rect other);

/// Expands the rect by a margin on all sides.
/// @param rect The rect.
/// @param margin Amount to expand (negative to shrink).
void rt_collision_rect_expand(rt_collision_rect rect, double margin);

/// Checks if this rect fully contains another rect.
/// @param rect This rect.
/// @param other The other rect.
/// @return 1 if this fully contains other, 0 otherwise.
int8_t rt_collision_rect_contains_rect(rt_collision_rect rect, rt_collision_rect other);

//=============================================================================
// Static collision helpers (no instance needed)
//=============================================================================

/// Checks if two rectangles overlap (static version).
/// @param x1, y1, w1, h1 First rectangle.
/// @param x2, y2, w2, h2 Second rectangle.
/// @return 1 if overlapping, 0 otherwise.
int8_t rt_collision_rects_overlap(double x1, double y1, double w1, double h1, double x2, double y2, double w2,
                                  double h2);

/// Checks if a point is inside a rectangle (static version).
/// @param px, py Point coordinates.
/// @param rx, ry, rw, rh Rectangle.
/// @return 1 if inside, 0 otherwise.
int8_t rt_collision_point_in_rect(double px, double py, double rx, double ry, double rw, double rh);

/// Checks if two circles overlap.
/// @param x1, y1, r1 First circle center and radius.
/// @param x2, y2, r2 Second circle center and radius.
/// @return 1 if overlapping, 0 otherwise.
int8_t rt_collision_circles_overlap(double x1, double y1, double r1, double x2, double y2, double r2);

/// Checks if a point is inside a circle.
/// @param px, py Point coordinates.
/// @param cx, cy, r Circle center and radius.
/// @return 1 if inside, 0 otherwise.
int8_t rt_collision_point_in_circle(double px, double py, double cx, double cy, double r);

/// Checks if a circle overlaps a rectangle.
/// @param cx, cy, r Circle center and radius.
/// @param rx, ry, rw, rh Rectangle.
/// @return 1 if overlapping, 0 otherwise.
int8_t rt_collision_circle_rect(double cx, double cy, double r, double rx, double ry, double rw, double rh);

/// Calculates the distance between two points.
/// @param x1, y1 First point.
/// @param x2, y2 Second point.
/// @return Distance.
double rt_collision_distance(double x1, double y1, double x2, double y2);

/// Calculates the squared distance between two points (faster than distance).
/// @param x1, y1 First point.
/// @param x2, y2 Second point.
/// @return Squared distance.
double rt_collision_distance_squared(double x1, double y1, double x2, double y2);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_COLLISION_H
