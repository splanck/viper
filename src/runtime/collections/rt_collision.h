//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_collision.h
// Purpose: AABB and circle collision detection helpers for game physics, providing overlap testing, depth queries, and distance calculations for both object handles and stateless free functions.
//
// Key invariants:
//   - Width and height values should be non-negative for meaningful results.
//   - Overlap depth functions return 0 when no overlap exists.
//   - Static free functions are pure with no side effects or allocation.
//   - rt_collision_rect handles support mutable position and size updates.
//
// Ownership/Lifetime:
//   - rt_collision_rect handles are caller-owned; destroy with rt_collision_rect_destroy.
//   - Static helper functions require no allocation and have no ownership semantics.
//
// Links: src/runtime/collections/rt_collision.c (implementation), src/runtime/graphics/rt_camera.h
//
//===----------------------------------------------------------------------===//
#ifndef VIPER_RT_COLLISION_H
#define VIPER_RT_COLLISION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// Opaque handle to a CollisionRect instance.
    typedef struct rt_collision_rect_impl *rt_collision_rect;

    /// @brief Allocates and initializes a new axis-aligned collision rectangle.
    /// @param x X coordinate of the left edge in world units.
    /// @param y Y coordinate of the top edge in world units.
    /// @param width Horizontal extent of the rectangle. Must be >= 0.
    /// @param height Vertical extent of the rectangle. Must be >= 0.
    /// @return A new CollisionRect handle. The caller must free it with
    ///   rt_collision_rect_destroy().
    rt_collision_rect rt_collision_rect_new(double x, double y, double width, double height);

    /// @brief Destroys a CollisionRect and releases its memory.
    /// @param rect The rectangle to destroy. Passing NULL is a no-op.
    void rt_collision_rect_destroy(rt_collision_rect rect);

    /// @brief Retrieves the X position (left edge) of the rectangle.
    /// @param rect The collision rectangle to query.
    /// @return The X coordinate of the left edge in world units.
    double rt_collision_rect_x(rt_collision_rect rect);

    /// @brief Retrieves the Y position (top edge) of the rectangle.
    /// @param rect The collision rectangle to query.
    /// @return The Y coordinate of the top edge in world units.
    double rt_collision_rect_y(rt_collision_rect rect);

    /// @brief Retrieves the width of the rectangle.
    /// @param rect The collision rectangle to query.
    /// @return The horizontal extent in world units.
    double rt_collision_rect_width(rt_collision_rect rect);

    /// @brief Retrieves the height of the rectangle.
    /// @param rect The collision rectangle to query.
    /// @return The vertical extent in world units.
    double rt_collision_rect_height(rt_collision_rect rect);

    /// @brief Computes the right edge coordinate (x + width).
    /// @param rect The collision rectangle to query.
    /// @return The X coordinate of the right edge in world units.
    double rt_collision_rect_right(rt_collision_rect rect);

    /// @brief Computes the bottom edge coordinate (y + height).
    /// @param rect The collision rectangle to query.
    /// @return The Y coordinate of the bottom edge in world units.
    double rt_collision_rect_bottom(rt_collision_rect rect);

    /// @brief Computes the horizontal center of the rectangle.
    /// @param rect The collision rectangle to query.
    /// @return The X coordinate of the center point (x + width / 2).
    double rt_collision_rect_center_x(rt_collision_rect rect);

    /// @brief Computes the vertical center of the rectangle.
    /// @param rect The collision rectangle to query.
    /// @return The Y coordinate of the center point (y + height / 2).
    double rt_collision_rect_center_y(rt_collision_rect rect);

    /// @brief Moves the rectangle to a new top-left position without changing
    ///   its size.
    /// @param rect The collision rectangle to modify.
    /// @param x New X coordinate for the left edge.
    /// @param y New Y coordinate for the top edge.
    void rt_collision_rect_set_position(rt_collision_rect rect, double x, double y);

    /// @brief Resizes the rectangle without changing its position.
    /// @param rect The collision rectangle to modify.
    /// @param width New horizontal extent. Must be >= 0.
    /// @param height New vertical extent. Must be >= 0.
    void rt_collision_rect_set_size(rt_collision_rect rect, double width, double height);

    /// @brief Sets both position and size of the rectangle in one call.
    /// @param rect The collision rectangle to modify.
    /// @param x X coordinate of the left edge.
    /// @param y Y coordinate of the top edge.
    /// @param width Horizontal extent. Must be >= 0.
    /// @param height Vertical extent. Must be >= 0.
    void rt_collision_rect_set(
        rt_collision_rect rect, double x, double y, double width, double height);

    /// @brief Repositions the rectangle so that its center is at the given point.
    /// @param rect The collision rectangle to modify.
    /// @param cx Desired center X coordinate.
    /// @param cy Desired center Y coordinate.
    void rt_collision_rect_set_center(rt_collision_rect rect, double cx, double cy);

    /// @brief Translates the rectangle by a displacement vector.
    /// @param rect The collision rectangle to modify.
    /// @param dx Horizontal displacement (positive = rightward).
    /// @param dy Vertical displacement (positive = downward).
    void rt_collision_rect_move(rt_collision_rect rect, double dx, double dy);

    /// @brief Tests whether a point lies inside the rectangle (inclusive edges).
    /// @param rect The collision rectangle to test against.
    /// @param px X coordinate of the test point.
    /// @param py Y coordinate of the test point.
    /// @return 1 if the point is inside or on the edge, 0 otherwise.
    int8_t rt_collision_rect_contains_point(rt_collision_rect rect, double px, double py);

    /// @brief Tests whether this rectangle overlaps with another CollisionRect.
    /// @param rect The first collision rectangle.
    /// @param other The second collision rectangle to test against.
    /// @return 1 if the two rectangles overlap (share any area), 0 otherwise.
    int8_t rt_collision_rect_overlaps(rt_collision_rect rect, rt_collision_rect other);

    /// @brief Tests whether this rectangle overlaps with a rectangle given as
    ///   raw coordinates.
    /// @param rect The collision rectangle handle to test against.
    /// @param ox X coordinate of the other rectangle's left edge.
    /// @param oy Y coordinate of the other rectangle's top edge.
    /// @param ow Width of the other rectangle.
    /// @param oh Height of the other rectangle.
    /// @return 1 if the rectangles overlap, 0 otherwise.
    int8_t rt_collision_rect_overlaps_rect(
        rt_collision_rect rect, double ox, double oy, double ow, double oh);

    /// @brief Computes the penetration depth on the X axis between two
    ///   overlapping rectangles.
    /// @param rect The first collision rectangle.
    /// @param other The second collision rectangle.
    /// @return The overlap depth on the X axis. Returns 0 if there is no overlap.
    ///   A positive value indicates the minimum horizontal distance needed to
    ///   separate the two rectangles.
    double rt_collision_rect_overlap_x(rt_collision_rect rect, rt_collision_rect other);

    /// @brief Computes the penetration depth on the Y axis between two
    ///   overlapping rectangles.
    /// @param rect The first collision rectangle.
    /// @param other The second collision rectangle.
    /// @return The overlap depth on the Y axis. Returns 0 if there is no overlap.
    ///   A positive value indicates the minimum vertical distance needed to
    ///   separate the two rectangles.
    double rt_collision_rect_overlap_y(rt_collision_rect rect, rt_collision_rect other);

    /// @brief Expands (or shrinks) the rectangle uniformly on all four sides.
    /// @param rect The collision rectangle to modify.
    /// @param margin Amount to add to each side. A positive value grows the
    ///   rectangle; a negative value shrinks it.
    void rt_collision_rect_expand(rt_collision_rect rect, double margin);

    /// @brief Tests whether this rectangle fully contains another rectangle.
    /// @param rect The outer collision rectangle.
    /// @param other The inner collision rectangle to test.
    /// @return 1 if every point of @p other lies within @p rect, 0 otherwise.
    int8_t rt_collision_rect_contains_rect(rt_collision_rect rect, rt_collision_rect other);

    //=============================================================================
    // Static collision helpers (no instance needed)
    //=============================================================================

    /// @brief Tests whether two axis-aligned rectangles overlap, using raw
    ///   coordinates.
    /// @param x1 Left edge of the first rectangle.
    /// @param y1 Top edge of the first rectangle.
    /// @param w1 Width of the first rectangle.
    /// @param h1 Height of the first rectangle.
    /// @param x2 Left edge of the second rectangle.
    /// @param y2 Top edge of the second rectangle.
    /// @param w2 Width of the second rectangle.
    /// @param h2 Height of the second rectangle.
    /// @return 1 if the rectangles share any area, 0 otherwise.
    int8_t rt_collision_rects_overlap(
        double x1, double y1, double w1, double h1, double x2, double y2, double w2, double h2);

    /// @brief Tests whether a point lies inside an axis-aligned rectangle.
    /// @param px X coordinate of the test point.
    /// @param py Y coordinate of the test point.
    /// @param rx Left edge of the rectangle.
    /// @param ry Top edge of the rectangle.
    /// @param rw Width of the rectangle.
    /// @param rh Height of the rectangle.
    /// @return 1 if the point is inside or on the edge, 0 otherwise.
    int8_t rt_collision_point_in_rect(
        double px, double py, double rx, double ry, double rw, double rh);

    /// @brief Tests whether two circles overlap.
    /// @param x1 X coordinate of the first circle's center.
    /// @param y1 Y coordinate of the first circle's center.
    /// @param r1 Radius of the first circle. Must be >= 0.
    /// @param x2 X coordinate of the second circle's center.
    /// @param y2 Y coordinate of the second circle's center.
    /// @param r2 Radius of the second circle. Must be >= 0.
    /// @return 1 if the circles overlap (distance between centers <= r1 + r2),
    ///   0 otherwise.
    int8_t rt_collision_circles_overlap(
        double x1, double y1, double r1, double x2, double y2, double r2);

    /// @brief Tests whether a point lies inside a circle.
    /// @param px X coordinate of the test point.
    /// @param py Y coordinate of the test point.
    /// @param cx X coordinate of the circle's center.
    /// @param cy Y coordinate of the circle's center.
    /// @param r Radius of the circle. Must be >= 0.
    /// @return 1 if the distance from the point to the center is <= r,
    ///   0 otherwise.
    int8_t rt_collision_point_in_circle(double px, double py, double cx, double cy, double r);

    /// @brief Tests whether a circle overlaps an axis-aligned rectangle.
    /// @param cx X coordinate of the circle's center.
    /// @param cy Y coordinate of the circle's center.
    /// @param r Radius of the circle. Must be >= 0.
    /// @param rx Left edge of the rectangle.
    /// @param ry Top edge of the rectangle.
    /// @param rw Width of the rectangle.
    /// @param rh Height of the rectangle.
    /// @return 1 if the circle and rectangle share any area, 0 otherwise.
    int8_t rt_collision_circle_rect(
        double cx, double cy, double r, double rx, double ry, double rw, double rh);

    /// @brief Computes the Euclidean distance between two points.
    /// @param x1 X coordinate of the first point.
    /// @param y1 Y coordinate of the first point.
    /// @param x2 X coordinate of the second point.
    /// @param y2 Y coordinate of the second point.
    /// @return The distance, always >= 0. Uses sqrt internally.
    double rt_collision_distance(double x1, double y1, double x2, double y2);

    /// @brief Computes the squared Euclidean distance between two points.
    ///
    /// Faster than rt_collision_distance() since it avoids the square root.
    /// Useful for distance comparisons where the actual magnitude is not needed.
    /// @param x1 X coordinate of the first point.
    /// @param y1 Y coordinate of the first point.
    /// @param x2 X coordinate of the second point.
    /// @param y2 Y coordinate of the second point.
    /// @return The squared distance, always >= 0.
    double rt_collision_distance_squared(double x1, double y1, double x2, double y2);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_COLLISION_H
