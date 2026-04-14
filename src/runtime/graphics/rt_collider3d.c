//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_collider3d.c
// Purpose: Collider3D runtime implementation for reusable 3D collision shapes.
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_collider3d.h"

#include "rt_canvas3d_internal.h"
#include "rt_pixels.h"
#include "rt_trap.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void *rt_vec3_new(double x, double y, double z);
extern void rt_obj_retain_maybe(void *obj);
extern int rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);

typedef struct {
    double position[3];
    double rotation[4];
    double scale[3];
} rt_collider3d_child;

typedef struct {
    void *vptr;
    int32_t type;
    int8_t static_only;
    double half_extents[3];
    double radius;
    double height;
    rt_mesh3d *mesh;
    float *heightfield_heights;
    int32_t heightfield_width;
    int32_t heightfield_depth;
    double heightfield_scale[3];
    double heightfield_min;
    double heightfield_max;
    void **children;
    rt_collider3d_child *child_transforms;
    int32_t child_count;
    int32_t child_capacity;
    double bounds_min[3];
    double bounds_max[3];
} rt_collider3d;

typedef struct {
    void *vptr;
    double position[3];
    double rotation[4];
    double scale[3];
    double matrix[16];
    int8_t dirty;
} rt_transform3d_view;

static void vec3_set(double *dst, double x, double y, double z) {
    dst[0] = x;
    dst[1] = y;
    dst[2] = z;
}

static void vec3_copy(double *dst, const double *src) {
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
}

static double clampd(double value, double lo, double hi) {
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return value;
}

static void quat_identity(double *q) {
    q[0] = 0.0;
    q[1] = 0.0;
    q[2] = 0.0;
    q[3] = 1.0;
}

static void quat_mul(const double *a, const double *b, double *out) {
    out[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    out[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    out[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    out[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

static void quat_conjugate(const double *q, double *out) {
    out[0] = -q[0];
    out[1] = -q[1];
    out[2] = -q[2];
    out[3] = q[3];
}

static void quat_rotate_vec3(const double *q, const double *v, double *out) {
    double qv[4] = {v[0], v[1], v[2], 0.0};
    double q_conj[4];
    double tmp[4];
    quat_conjugate(q, q_conj);
    quat_mul(q, qv, tmp);
    quat_mul(tmp, q_conj, tmp);
    out[0] = tmp[0];
    out[1] = tmp[1];
    out[2] = tmp[2];
}

static void transform_point_raw(const double *position,
                                const double *rotation,
                                const double *scale,
                                const double *local_point,
                                double *out) {
    double scaled[3] = {local_point[0] * scale[0],
                        local_point[1] * scale[1],
                        local_point[2] * scale[2]};
    double rotated[3];
    quat_rotate_vec3(rotation, scaled, rotated);
    out[0] = position[0] + rotated[0];
    out[1] = position[1] + rotated[1];
    out[2] = position[2] + rotated[2];
}

static void transform_bounds_raw(const double *bounds_min,
                                 const double *bounds_max,
                                 const double *position,
                                 const double *rotation,
                                 const double *scale,
                                 double *out_min,
                                 double *out_max) {
    double local_min[3];
    double local_max[3];
    double corner[3];
    double world[3];
    vec3_copy(local_min, bounds_min);
    vec3_copy(local_max, bounds_max);
    out_min[0] = out_min[1] = out_min[2] = DBL_MAX;
    out_max[0] = out_max[1] = out_max[2] = -DBL_MAX;
    for (int mask = 0; mask < 8; ++mask) {
        corner[0] = (mask & 1) ? local_max[0] : local_min[0];
        corner[1] = (mask & 2) ? local_max[1] : local_min[1];
        corner[2] = (mask & 4) ? local_max[2] : local_min[2];
        transform_point_raw(position, rotation, scale, corner, world);
        for (int axis = 0; axis < 3; ++axis) {
            if (world[axis] < out_min[axis])
                out_min[axis] = world[axis];
            if (world[axis] > out_max[axis])
                out_max[axis] = world[axis];
        }
    }
}

static void collider3d_recompute_bounds(rt_collider3d *collider);

static void collider3d_finalizer(void *obj) {
    rt_collider3d *collider = (rt_collider3d *)obj;
    if (!collider)
        return;
    if (collider->mesh && rt_obj_release_check0(collider->mesh))
        rt_obj_free(collider->mesh);
    collider->mesh = NULL;
    for (int32_t i = 0; i < collider->child_count; ++i) {
        if (collider->children[i] && rt_obj_release_check0(collider->children[i]))
            rt_obj_free(collider->children[i]);
        collider->children[i] = NULL;
    }
    free(collider->children);
    free(collider->child_transforms);
    free(collider->heightfield_heights);
    collider->children = NULL;
    collider->child_transforms = NULL;
    collider->heightfield_heights = NULL;
}

static rt_collider3d *collider3d_alloc(int32_t type) {
    rt_collider3d *collider = (rt_collider3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_collider3d));
    if (!collider) {
        rt_trap("Collider3D.New: allocation failed");
        return NULL;
    }
    memset(collider, 0, sizeof(*collider));
    collider->type = type;
    collider->bounds_min[0] = collider->bounds_min[1] = collider->bounds_min[2] = 0.0;
    collider->bounds_max[0] = collider->bounds_max[1] = collider->bounds_max[2] = 0.0;
    rt_obj_set_finalizer(collider, collider3d_finalizer);
    return collider;
}

static void collider3d_set_from_transform(rt_collider3d_child *dst, void *transform_obj) {
    vec3_set(dst->position, 0.0, 0.0, 0.0);
    quat_identity(dst->rotation);
    vec3_set(dst->scale, 1.0, 1.0, 1.0);
    if (!transform_obj)
        return;
    {
        rt_transform3d_view *xf = (rt_transform3d_view *)transform_obj;
        vec3_copy(dst->position, xf->position);
        vec3_copy(dst->scale, xf->scale);
        dst->rotation[0] = xf->rotation[0];
        dst->rotation[1] = xf->rotation[1];
        dst->rotation[2] = xf->rotation[2];
        dst->rotation[3] = xf->rotation[3];
    }
}

static void collider3d_recompute_bounds(rt_collider3d *collider) {
    if (!collider)
        return;

    switch (collider->type) {
    case RT_COLLIDER3D_TYPE_BOX:
        collider->bounds_min[0] = -collider->half_extents[0];
        collider->bounds_min[1] = -collider->half_extents[1];
        collider->bounds_min[2] = -collider->half_extents[2];
        collider->bounds_max[0] = collider->half_extents[0];
        collider->bounds_max[1] = collider->half_extents[1];
        collider->bounds_max[2] = collider->half_extents[2];
        break;
    case RT_COLLIDER3D_TYPE_SPHERE:
        collider->bounds_min[0] = collider->bounds_min[1] = collider->bounds_min[2] =
            -collider->radius;
        collider->bounds_max[0] = collider->bounds_max[1] = collider->bounds_max[2] =
            collider->radius;
        break;
    case RT_COLLIDER3D_TYPE_CAPSULE:
        collider->bounds_min[0] = -collider->radius;
        collider->bounds_min[1] = -collider->height * 0.5;
        collider->bounds_min[2] = -collider->radius;
        collider->bounds_max[0] = collider->radius;
        collider->bounds_max[1] = collider->height * 0.5;
        collider->bounds_max[2] = collider->radius;
        break;
    case RT_COLLIDER3D_TYPE_CONVEX_HULL:
    case RT_COLLIDER3D_TYPE_MESH:
        if (collider->mesh) {
            rt_mesh3d_refresh_bounds(collider->mesh);
            collider->bounds_min[0] = collider->mesh->aabb_min[0];
            collider->bounds_min[1] = collider->mesh->aabb_min[1];
            collider->bounds_min[2] = collider->mesh->aabb_min[2];
            collider->bounds_max[0] = collider->mesh->aabb_max[0];
            collider->bounds_max[1] = collider->mesh->aabb_max[1];
            collider->bounds_max[2] = collider->mesh->aabb_max[2];
        } else {
            vec3_set(collider->bounds_min, 0.0, 0.0, 0.0);
            vec3_set(collider->bounds_max, 0.0, 0.0, 0.0);
        }
        break;
    case RT_COLLIDER3D_TYPE_HEIGHTFIELD: {
        double half_width = 0.0;
        double half_depth = 0.0;
        if (collider->heightfield_width > 1)
            half_width = ((double)(collider->heightfield_width - 1) * collider->heightfield_scale[0]) *
                         0.5;
        if (collider->heightfield_depth > 1)
            half_depth = ((double)(collider->heightfield_depth - 1) * collider->heightfield_scale[2]) *
                         0.5;
        collider->bounds_min[0] = -half_width;
        collider->bounds_min[1] = collider->heightfield_min * collider->heightfield_scale[1];
        collider->bounds_min[2] = -half_depth;
        collider->bounds_max[0] = half_width;
        collider->bounds_max[1] = collider->heightfield_max * collider->heightfield_scale[1];
        collider->bounds_max[2] = half_depth;
        break;
    }
    case RT_COLLIDER3D_TYPE_COMPOUND:
        if (collider->child_count == 0) {
            vec3_set(collider->bounds_min, 0.0, 0.0, 0.0);
            vec3_set(collider->bounds_max, 0.0, 0.0, 0.0);
            break;
        }
        collider->bounds_min[0] = collider->bounds_min[1] = collider->bounds_min[2] = DBL_MAX;
        collider->bounds_max[0] = collider->bounds_max[1] = collider->bounds_max[2] = -DBL_MAX;
        for (int32_t i = 0; i < collider->child_count; ++i) {
            double child_min[3];
            double child_max[3];
            if (!collider->children[i])
                continue;
            rt_collider3d_get_local_bounds_raw(collider->children[i], child_min, child_max);
            transform_bounds_raw(child_min,
                                 child_max,
                                 collider->child_transforms[i].position,
                                 collider->child_transforms[i].rotation,
                                 collider->child_transforms[i].scale,
                                 child_min,
                                 child_max);
            for (int axis = 0; axis < 3; ++axis) {
                if (child_min[axis] < collider->bounds_min[axis])
                    collider->bounds_min[axis] = child_min[axis];
                if (child_max[axis] > collider->bounds_max[axis])
                    collider->bounds_max[axis] = child_max[axis];
            }
        }
        if (collider->bounds_min[0] == DBL_MAX) {
            vec3_set(collider->bounds_min, 0.0, 0.0, 0.0);
            vec3_set(collider->bounds_max, 0.0, 0.0, 0.0);
        }
        break;
    default:
        vec3_set(collider->bounds_min, 0.0, 0.0, 0.0);
        vec3_set(collider->bounds_max, 0.0, 0.0, 0.0);
        break;
    }
}

/// @brief Construct an axis-aligned box collider with half-extents (hx, hy, hz). Negative
/// values are taken as their absolute value. Box AABB is fully cached for fast queries.
void *rt_collider3d_new_box(double hx, double hy, double hz) {
    rt_collider3d *collider = collider3d_alloc(RT_COLLIDER3D_TYPE_BOX);
    if (!collider)
        return NULL;
    collider->half_extents[0] = fabs(hx);
    collider->half_extents[1] = fabs(hy);
    collider->half_extents[2] = fabs(hz);
    collider3d_recompute_bounds(collider);
    return collider;
}

/// @brief Construct a sphere collider centered on the local origin with the given `radius`.
void *rt_collider3d_new_sphere(double radius) {
    rt_collider3d *collider = collider3d_alloc(RT_COLLIDER3D_TYPE_SPHERE);
    if (!collider)
        return NULL;
    collider->radius = fabs(radius);
    collider3d_recompute_bounds(collider);
    return collider;
}

/// @brief Construct a Y-axis capsule collider — cylinder of `height` with hemispherical caps of
/// `radius`. Total bounding height is `height + 2*radius`. Common for character controllers.
void *rt_collider3d_new_capsule(double radius, double height) {
    rt_collider3d *collider = collider3d_alloc(RT_COLLIDER3D_TYPE_CAPSULE);
    if (!collider)
        return NULL;
    collider->radius = fabs(radius);
    collider->height = fabs(height);
    collider3d_recompute_bounds(collider);
    return collider;
}

static void *collider3d_new_mesh_like(void *mesh, int32_t type, int8_t static_only) {
    rt_collider3d *collider;
    if (!mesh) {
        rt_trap("Collider3D.NewMesh/NewConvexHull: mesh must be non-null");
        return NULL;
    }
    collider = collider3d_alloc(type);
    if (!collider)
        return NULL;
    collider->mesh = (rt_mesh3d *)mesh;
    collider->static_only = static_only;
    rt_obj_retain_maybe(mesh);
    collider3d_recompute_bounds(collider);
    return collider;
}

/// @brief Wrap a Mesh3D as a *convex hull* collider — the mesh's vertex set is treated as a
/// convex polytope (caller's responsibility to ensure convexity). Suitable for dynamic bodies.
void *rt_collider3d_new_convex_hull(void *mesh) {
    return collider3d_new_mesh_like(mesh, RT_COLLIDER3D_TYPE_CONVEX_HULL, 0);
}

/// @brief Wrap a Mesh3D as a *triangle-mesh* collider — uses every triangle for collision tests.
/// Marked static-only: cannot be attached to dynamic rigid bodies (use the mesh as level geometry).
void *rt_collider3d_new_mesh(void *mesh) {
    return collider3d_new_mesh_like(mesh, RT_COLLIDER3D_TYPE_MESH, 1);
}

/// @brief Build a heightfield collider from a Pixels heightmap. Heights are decoded with 16-bit
/// precision (R = high byte, G = low byte) into [0, 1] then scaled by `scale_y`. `scale_x` and
/// `scale_z` are the per-cell spacing in world units. Static-only. Traps on invalid heightmap
/// (< 2×2 or null buffer).
void *rt_collider3d_new_heightfield(
    void *heightmap, double scale_x, double scale_y, double scale_z) {
    const uint32_t *raw;
    int64_t width;
    int64_t height;
    rt_collider3d *collider;
    if (!heightmap) {
        rt_trap("Collider3D.NewHeightfield: heightmap must be non-null");
        return NULL;
    }
    width = rt_pixels_width(heightmap);
    height = rt_pixels_height(heightmap);
    raw = rt_pixels_raw_buffer(heightmap);
    if (width < 2 || height < 2 || !raw) {
        rt_trap("Collider3D.NewHeightfield: heightmap must be a valid Pixels object");
        return NULL;
    }
    collider = collider3d_alloc(RT_COLLIDER3D_TYPE_HEIGHTFIELD);
    if (!collider)
        return NULL;
    collider->static_only = 1;
    collider->heightfield_width = (int32_t)width;
    collider->heightfield_depth = (int32_t)height;
    collider->heightfield_scale[0] = fabs(scale_x);
    collider->heightfield_scale[1] = fabs(scale_y);
    collider->heightfield_scale[2] = fabs(scale_z);
    collider->heightfield_heights =
        (float *)calloc((size_t)(width * height), sizeof(float));
    if (!collider->heightfield_heights) {
        if (rt_obj_release_check0(collider))
            rt_obj_free(collider);
        rt_trap("Collider3D.NewHeightfield: allocation failed");
        return NULL;
    }
    collider->heightfield_min = DBL_MAX;
    collider->heightfield_max = -DBL_MAX;
    for (int64_t z = 0; z < height; ++z) {
        for (int64_t x = 0; x < width; ++x) {
            uint32_t pixel = raw[z * width + x];
            uint32_t hi = (pixel >> 24) & 0xFFu;
            uint32_t lo = (pixel >> 16) & 0xFFu;
            double h = (double)((hi << 8) | lo) / 65535.0;
            collider->heightfield_heights[z * width + x] = (float)h;
            if (h < collider->heightfield_min)
                collider->heightfield_min = h;
            if (h > collider->heightfield_max)
                collider->heightfield_max = h;
        }
    }
    if (collider->heightfield_min == DBL_MAX) {
        collider->heightfield_min = 0.0;
        collider->heightfield_max = 0.0;
    }
    collider3d_recompute_bounds(collider);
    return collider;
}

/// @brief Construct an empty compound collider — a container holding child colliders, each
/// with its own local transform. Use `_add_child` to populate, then attach to a rigid body.
void *rt_collider3d_new_compound(void) {
    rt_collider3d *collider = collider3d_alloc(RT_COLLIDER3D_TYPE_COMPOUND);
    if (!collider)
        return NULL;
    collider3d_recompute_bounds(collider);
    return collider;
}

/// @brief Append a child collider to a compound, transformed by `local_transform` (a Transform3D).
/// Children are retained for the compound's lifetime. Recomputes the compound's AABB to enclose
/// the new child. Traps if the parent isn't compound, child is null, or self-reference is attempted.
void rt_collider3d_add_child(void *compound_obj, void *child_obj, void *local_transform) {
    rt_collider3d *compound = (rt_collider3d *)compound_obj;
    int32_t new_capacity;
    if (!compound)
        return;
    if (compound->type != RT_COLLIDER3D_TYPE_COMPOUND) {
        rt_trap("Collider3D.AddChild: target collider is not compound");
        return;
    }
    if (!child_obj) {
        rt_trap("Collider3D.AddChild: child collider must be non-null");
        return;
    }
    if (child_obj == compound_obj) {
        rt_trap("Collider3D.AddChild: a collider cannot contain itself");
        return;
    }
    if (compound->child_count >= compound->child_capacity) {
        void **new_children;
        rt_collider3d_child *new_transforms;
        new_capacity = compound->child_capacity > 0 ? compound->child_capacity * 2 : 4;
        new_children = (void **)calloc((size_t)new_capacity, sizeof(void *));
        new_transforms =
            (rt_collider3d_child *)calloc((size_t)new_capacity, sizeof(rt_collider3d_child));
        if (!new_children || !new_transforms) {
            free(new_children);
            free(new_transforms);
            rt_trap("Collider3D.AddChild: allocation failed");
            return;
        }
        if (compound->child_count > 0) {
            memcpy(new_children, compound->children, (size_t)compound->child_count * sizeof(void *));
            memcpy(new_transforms,
                   compound->child_transforms,
                   (size_t)compound->child_count * sizeof(rt_collider3d_child));
        }
        free(compound->children);
        free(compound->child_transforms);
        compound->children = new_children;
        compound->child_transforms = new_transforms;
        compound->child_capacity = new_capacity;
    }
    rt_obj_retain_maybe(child_obj);
    compound->children[compound->child_count] = child_obj;
    collider3d_set_from_transform(&compound->child_transforms[compound->child_count], local_transform);
    compound->child_count++;
    collider3d_recompute_bounds(compound);
}

/// @brief Return the collider's discriminator (RT_COLLIDER3D_TYPE_BOX, _SPHERE, ...). -1 if NULL.
int64_t rt_collider3d_get_type(void *collider) {
    return collider ? ((rt_collider3d *)collider)->type : -1;
}

/// @brief Return the AABB minimum corner in *local* space as a fresh Vec3. Returns origin
/// for a NULL handle. Re-derives the bounds from the underlying shape data first.
void *rt_collider3d_get_local_bounds_min(void *collider) {
    rt_collider3d *shape = (rt_collider3d *)collider;
    if (!shape)
        return rt_vec3_new(0.0, 0.0, 0.0);
    collider3d_recompute_bounds(shape);
    return rt_vec3_new(shape->bounds_min[0], shape->bounds_min[1], shape->bounds_min[2]);
}

/// @brief Return the AABB maximum corner in *local* space as a fresh Vec3.
void *rt_collider3d_get_local_bounds_max(void *collider) {
    rt_collider3d *shape = (rt_collider3d *)collider;
    if (!shape)
        return rt_vec3_new(0.0, 0.0, 0.0);
    collider3d_recompute_bounds(shape);
    return rt_vec3_new(shape->bounds_max[0], shape->bounds_max[1], shape->bounds_max[2]);
}

/// @brief Internal: write the local AABB into the two raw double[3] arrays. Faster than the
/// Vec3-returning getters when the physics core needs the bounds many times per frame.
void rt_collider3d_get_local_bounds_raw(void *collider, double *min_out, double *max_out) {
    rt_collider3d *shape = (rt_collider3d *)collider;
    if (!min_out || !max_out) {
        return;
    }
    if (!shape) {
        vec3_set(min_out, 0.0, 0.0, 0.0);
        vec3_set(max_out, 0.0, 0.0, 0.0);
        return;
    }
    collider3d_recompute_bounds(shape);
    vec3_copy(min_out, shape->bounds_min);
    vec3_copy(max_out, shape->bounds_max);
}

/// @brief Internal: transform the local AABB by (position, rotation quat, scale) and write the
/// resulting world-space AABB into `min_out` / `max_out`. Defaults: zero pos, identity rotation,
/// unit scale when individual params are NULL.
void rt_collider3d_compute_world_aabb_raw(void *collider,
                                          const double *position,
                                          const double *rotation,
                                          const double *scale,
                                          double *min_out,
                                          double *max_out) {
    double local_min[3];
    double local_max[3];
    double identity_rotation[4];
    double unit_scale[3];
    if (!min_out || !max_out)
        return;
    rt_collider3d_get_local_bounds_raw(collider, local_min, local_max);
    quat_identity(identity_rotation);
    vec3_set(unit_scale, 1.0, 1.0, 1.0);
    if (!position) {
        double zero_pos[3] = {0.0, 0.0, 0.0};
        transform_bounds_raw(local_min,
                             local_max,
                             zero_pos,
                             rotation ? rotation : identity_rotation,
                             scale ? scale : unit_scale,
                             min_out,
                             max_out);
        return;
    }
    transform_bounds_raw(local_min,
                         local_max,
                         position,
                         rotation ? rotation : identity_rotation,
                         scale ? scale : unit_scale,
                         min_out,
                         max_out);
}

/// @brief Internal: 1 if the collider can only be used on static bodies (mesh, heightfield).
int8_t rt_collider3d_is_static_only_raw(void *collider) {
    return collider ? ((rt_collider3d *)collider)->static_only : 0;
}

/// @brief Internal: fill `half_extents_out[3]` with the box's half-extents. Zeros for non-box.
void rt_collider3d_get_box_half_extents_raw(void *collider, double *half_extents_out) {
    rt_collider3d *shape = (rt_collider3d *)collider;
    if (!half_extents_out) {
        return;
    }
    if (!shape || shape->type != RT_COLLIDER3D_TYPE_BOX) {
        vec3_set(half_extents_out, 0.0, 0.0, 0.0);
        return;
    }
    vec3_copy(half_extents_out, shape->half_extents);
}

/// @brief Internal: sphere/capsule radius. Returns 0 for unsupported shapes.
double rt_collider3d_get_radius_raw(void *collider) {
    rt_collider3d *shape = (rt_collider3d *)collider;
    if (!shape)
        return 0.0;
    return shape->radius;
}

/// @brief Internal: capsule cylindrical height (excludes hemispherical caps). 0 for non-capsule.
double rt_collider3d_get_height_raw(void *collider) {
    rt_collider3d *shape = (rt_collider3d *)collider;
    if (!shape)
        return 0.0;
    return shape->height;
}

/// @brief Internal: borrow the underlying Mesh3D for convex-hull / triangle-mesh colliders.
/// Returns NULL for primitive shapes. Caller must NOT release — collider retains ownership.
void *rt_collider3d_get_mesh_raw(void *collider) {
    rt_collider3d *shape = (rt_collider3d *)collider;
    if (!shape)
        return NULL;
    if (shape->type != RT_COLLIDER3D_TYPE_CONVEX_HULL && shape->type != RT_COLLIDER3D_TYPE_MESH)
        return NULL;
    return shape->mesh;
}

/// @brief Internal: number of child colliders in a compound. 0 for non-compound shapes.
int64_t rt_collider3d_get_child_count_raw(void *collider) {
    rt_collider3d *shape = (rt_collider3d *)collider;
    if (!shape || shape->type != RT_COLLIDER3D_TYPE_COMPOUND)
        return 0;
    return shape->child_count;
}

/// @brief Internal: borrow the i-th child collider from a compound. NULL if out of range.
void *rt_collider3d_get_child_raw(void *collider, int64_t index) {
    rt_collider3d *shape = (rt_collider3d *)collider;
    if (!shape || shape->type != RT_COLLIDER3D_TYPE_COMPOUND)
        return NULL;
    if (index < 0 || index >= shape->child_count)
        return NULL;
    return shape->children[index];
}

/// @brief Internal: copy the i-th compound child's local TRS into the output buffers
/// (position_out[3], rotation_out[4] quaternion, scale_out[3]). Outputs default to identity.
void rt_collider3d_get_child_transform_raw(void *compound,
                                           int64_t index,
                                           double *position_out,
                                           double *rotation_out,
                                           double *scale_out) {
    rt_collider3d *shape = (rt_collider3d *)compound;
    if (position_out)
        vec3_set(position_out, 0.0, 0.0, 0.0);
    if (rotation_out)
        quat_identity(rotation_out);
    if (scale_out)
        vec3_set(scale_out, 1.0, 1.0, 1.0);
    if (!shape || shape->type != RT_COLLIDER3D_TYPE_COMPOUND)
        return;
    if (index < 0 || index >= shape->child_count)
        return;
    if (position_out)
        vec3_copy(position_out, shape->child_transforms[index].position);
    if (rotation_out)
        memcpy(rotation_out, shape->child_transforms[index].rotation, sizeof(double) * 4);
    if (scale_out)
        vec3_copy(scale_out, shape->child_transforms[index].scale);
}

static double collider3d_heightfield_height_at(
    const rt_collider3d *collider, int32_t x, int32_t z) {
    if (!collider || !collider->heightfield_heights || collider->heightfield_width <= 0 ||
        collider->heightfield_depth <= 0)
        return 0.0;
    if (x < 0)
        x = 0;
    if (z < 0)
        z = 0;
    if (x >= collider->heightfield_width)
        x = collider->heightfield_width - 1;
    if (z >= collider->heightfield_depth)
        z = collider->heightfield_depth - 1;
    return collider->heightfield_heights[z * collider->heightfield_width + x];
}

/// @brief Internal: bilinearly sample a heightfield at local-space (local_x, local_z), writing
/// the world-Y height (scaled) and surface normal (central difference) to the out-pointers.
/// Returns 1 if the sample was inside the field, 0 otherwise (out-pointers default to safe
/// fallback values: y=0, normal=(0,1,0)).
int8_t rt_collider3d_sample_heightfield_raw(void *collider,
                                            double local_x,
                                            double local_z,
                                            double *height_out,
                                            double *normal_out) {
    rt_collider3d *shape = (rt_collider3d *)collider;
    double half_width;
    double half_depth;
    double grid_x;
    double grid_z;
    int32_t x0;
    int32_t z0;
    int32_t x1;
    int32_t z1;
    double tx;
    double tz;
    double h00;
    double h10;
    double h01;
    double h11;
    double h0;
    double h1;
    double h;
    double dx;
    double dz;
    double normal[3];
    double normal_len;
    if (height_out)
        *height_out = 0.0;
    if (normal_out) {
        normal_out[0] = 0.0;
        normal_out[1] = 1.0;
        normal_out[2] = 0.0;
    }
    if (!shape || shape->type != RT_COLLIDER3D_TYPE_HEIGHTFIELD || !shape->heightfield_heights)
        return 0;

    half_width = ((double)(shape->heightfield_width - 1) * shape->heightfield_scale[0]) * 0.5;
    half_depth = ((double)(shape->heightfield_depth - 1) * shape->heightfield_scale[2]) * 0.5;
    if (shape->heightfield_scale[0] <= 1e-12 || shape->heightfield_scale[2] <= 1e-12)
        return 0;

    grid_x = (local_x + half_width) / shape->heightfield_scale[0];
    grid_z = (local_z + half_depth) / shape->heightfield_scale[2];
    if (grid_x < 0.0 || grid_z < 0.0 ||
        grid_x > (double)(shape->heightfield_width - 1) ||
        grid_z > (double)(shape->heightfield_depth - 1))
        return 0;

    x0 = (int32_t)floor(grid_x);
    z0 = (int32_t)floor(grid_z);
    x1 = x0 + 1;
    z1 = z0 + 1;
    if (x1 >= shape->heightfield_width)
        x1 = shape->heightfield_width - 1;
    if (z1 >= shape->heightfield_depth)
        z1 = shape->heightfield_depth - 1;
    tx = clampd(grid_x - (double)x0, 0.0, 1.0);
    tz = clampd(grid_z - (double)z0, 0.0, 1.0);
    h00 = collider3d_heightfield_height_at(shape, x0, z0);
    h10 = collider3d_heightfield_height_at(shape, x1, z0);
    h01 = collider3d_heightfield_height_at(shape, x0, z1);
    h11 = collider3d_heightfield_height_at(shape, x1, z1);
    h0 = h00 + (h10 - h00) * tx;
    h1 = h01 + (h11 - h01) * tx;
    h = h0 + (h1 - h0) * tz;
    if (height_out)
        *height_out = h * shape->heightfield_scale[1];

    dx = ((h10 - h00) * (1.0 - tz) + (h11 - h01) * tz) * shape->heightfield_scale[1];
    dz = ((h01 - h00) * (1.0 - tx) + (h11 - h10) * tx) * shape->heightfield_scale[1];
    normal[0] = -dx;
    normal[1] = shape->heightfield_scale[0];
    normal[2] = -dz;
    normal_len = sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
    if (normal_len > 1e-12) {
        normal[0] /= normal_len;
        normal[1] /= normal_len;
        normal[2] /= normal_len;
    } else {
        normal[0] = 0.0;
        normal[1] = 1.0;
        normal[2] = 0.0;
    }
    if (normal_out) {
        normal_out[0] = normal[0];
        normal_out[1] = normal[1];
        normal_out[2] = normal[2];
    }
    return 1;
}

#else
typedef int rt_collider3d_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
