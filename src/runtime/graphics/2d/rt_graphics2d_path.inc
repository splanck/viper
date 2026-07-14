//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
///
/// @file rt_graphics2d_path.inc
/// @brief Path2D path construction, storage, bounds, and draw entry points.
///
/// @details This include fragment is consumed only by rt_graphics2d.c and
/// isolates the path-vector data structure and its public runtime surface from
/// unrelated 2D rendering subsystems.
///
// File: src/runtime/graphics/2d/rt_graphics2d_path.inc
// Purpose: Path2D dynamic path-storage and path rasterization entry points.
//
// Key invariants:
//   - Included only by src/runtime/graphics/2d/rt_graphics2d.c.
//   - Keeps the original rt_graphics2d.c declaration order and static helper
//     visibility; this is a physical source split, not a behavior split.
//   - Public runtime entry points preserve their existing names and signatures.
//
// Ownership/Lifetime:
//   - Ownership rules are unchanged from the original monolithic source file;
//     GC finalizers release retained handles and heap buffers for each class.
//
// Links: src/runtime/graphics/2d/rt_graphics2d.c,
//        src/runtime/graphics/2d/rt_graphics2d_internal.h
//
//===----------------------------------------------------------------------===//

//=============================================================================
// Path2D
//=============================================================================
// Dynamic sequence of `(x, y, move)` points. `move == 1` marks a pen-up
// break — `MoveTo` followed by `LineTo` forms a subpath. `DrawToPixels`
// walks the list and renders a line between every consecutive non-move pair.

/// @brief GC finalizer — frees the points buffer.
static void path2d_finalize(void *obj) {
    if (!rt2d_has_class(obj, RT2D_PATH_CLASS_ID))
        return;
    rt_path2d_impl *path = (rt_path2d_impl *)obj;
    free(path->points);
}

/// @brief Allocate a Path2D with the given initial point capacity.
/// @details Capacity is clamped by `rt2d_initial_capacity`. On allocation failure
///          the impl is torn down cleanly and NULL is returned.
void *rt_path2d_new(int64_t capacity) {
    rt_path2d_impl *path =
        (rt_path2d_impl *)rt_obj_new_i64(RT2D_PATH_CLASS_ID, (int64_t)sizeof(rt_path2d_impl));
    if (!path)
        return NULL;
    path->capacity = rt2d_initial_capacity(capacity);
    path->points = (rt_path2d_point *)calloc((size_t)path->capacity, sizeof(*path->points));
    if (!path->points) {
        if (rt_obj_release_check0(path))
            rt_obj_free(path);
        return NULL;
    }
    rt_obj_set_finalizer(path, path2d_finalize);
    return path;
}

/// @brief Grow the point-buffer to fit at least `needed` entries.
/// @details Standard doubling-growth with element-count and byte-count overflow
///          guards. Tail is zero-initialized so a failed-mid-append sequence
///          doesn't leave stale data in unused slots.
static int32_t path2d_reserve(rt_path2d_impl *path, int64_t needed) {
    if (!path || needed <= path->capacity)
        return 1;
    int64_t cap = path->capacity > 0 ? path->capacity : RT2D_INITIAL_CAP;
    while (cap < needed) {
        if (cap > INT64_MAX / 2)
            return 0;
        cap *= 2;
    }
    if (cap > INT64_MAX / (int64_t)sizeof(rt_path2d_point))
        return 0;
    rt_path2d_point *points =
        (rt_path2d_point *)realloc(path->points, (size_t)cap * sizeof(*points));
    if (!points)
        return 0;
    memset(points + path->capacity, 0, (size_t)(cap - path->capacity) * sizeof(*points));
    path->points = points;
    path->capacity = cap;
    return 1;
}

/// @brief Append a point to the path, growing the backing buffer as needed.
/// @param move  1 for a pen-up move (new subpath start); 0 for a pen-down line segment.
static void path2d_add(void *path, int64_t x, int64_t y, int32_t move) {
    rt_path2d_impl *impl = rt2d_has_class(path, RT2D_PATH_CLASS_ID) ? (rt_path2d_impl *)path : NULL;
    if (!impl)
        return;
    if (!path2d_reserve(impl, impl->count + 1)) {
        rt_trap("Path2D: capacity overflow");
        return;
    }
    rt_path2d_point *point = &impl->points[impl->count++];
    point->x = x;
    point->y = y;
    point->move = move;
}

/// @brief Reset the path to empty without freeing the backing buffer.
void rt_path2d_clear(void *path) {
    if (rt2d_has_class(path, RT2D_PATH_CLASS_ID))
        ((rt_path2d_impl *)path)->count = 0;
}

/// @brief Append a pen-up move point — starts a new subpath at `(x, y)`.
/// @details No line is drawn from the previous point to this one; `DrawToPixels`
///          skips across move-flagged points when iterating.
void rt_path2d_move_to(void *path, int64_t x, int64_t y) {
    path2d_add(path, x, y, 1);
}

/// @brief Append a pen-down line point — a line will be drawn from the previous
///        point to `(x, y)` during `DrawToPixels`.
void rt_path2d_line_to(void *path, int64_t x, int64_t y) {
    path2d_add(path, x, y, 0);
}

/// @brief Return the number of points (both move and line) in the path.
int64_t rt_path2d_count(void *path) {
    return rt2d_has_class(path, RT2D_PATH_CLASS_ID) ? ((rt_path2d_impl *)path)->count : 0;
}

/// @brief Return the X coordinate of the point at `index`, or 0 if out of range.
int64_t rt_path2d_get_x(void *path, int64_t index) {
    rt_path2d_impl *impl = rt2d_has_class(path, RT2D_PATH_CLASS_ID) ? (rt_path2d_impl *)path : NULL;
    if (!impl || index < 0 || index >= impl->count)
        return 0;
    return impl->points[index].x;
}

/// @brief Return the Y coordinate of the point at `index`, or 0 if out of range.
int64_t rt_path2d_get_y(void *path, int64_t index) {
    rt_path2d_impl *impl = rt2d_has_class(path, RT2D_PATH_CLASS_ID) ? (rt_path2d_impl *)path : NULL;
    if (!impl || index < 0 || index >= impl->count)
        return 0;
    return impl->points[index].y;
}

/// @brief Render the path as connected line segments into a Pixels buffer.
/// @details Walks the point list and draws a line for every non-move pair.
///          The first point is always treated as a move (no line drawn to it);
///          subsequent move points introduce pen-up breaks so you can emit
///          multiple disconnected subpaths from the same Path2D. Colors accept
///          either `0xRRGGBBAA` or `0x00RRGGBB` form via `rt2d_draw_rgb`.
void rt_path2d_draw_to_pixels(void *path, void *pixels, int64_t rgba) {
    rt_path2d_impl *impl = rt2d_has_class(path, RT2D_PATH_CLASS_ID) ? (rt_path2d_impl *)path : NULL;
    if (!impl || !pixels || impl->count < 2)
        return;
    int64_t color = rt2d_draw_rgb(rgba);
    for (int64_t i = 1; i < impl->count; i++) {
        if (impl->points[i].move)
            continue;
        rt_pixels_draw_line(pixels,
                            impl->points[i - 1].x,
                            impl->points[i - 1].y,
                            impl->points[i].x,
                            impl->points[i].y,
                            color);
    }
}
