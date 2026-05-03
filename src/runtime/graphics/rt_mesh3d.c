//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_mesh3d.c
// Purpose: Viper.Graphics3D.Mesh3D — dynamic vertex/index mesh storage
//   with programmatic construction and procedural generators.
//
// Key invariants:
//   - Vertices stored as vgfx3d_vertex_t (84 bytes, float internally)
//   - Indices are uint32_t (max 16M vertices per mesh)
//   - CCW winding is front-facing
//   - GC finalizer frees vertex/index arrays
//
// Ownership/Lifetime:
//   - rt_mesh3d is GC-managed; finalizer frees heap arrays
//
// Links: rt_canvas3d.h, rt_canvas3d_internal.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_object.h"
#include "rt_pixels.h"
#include "rt_string.h"
#include "vgfx3d_backend_utils.h"

#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
#include "rt_trap.h"
extern const char *rt_string_cstr(rt_string s);

#include <errno.h>
#include <limits.h>
#define MESH_INIT_VERTS 64
#define MESH_INIT_IDXS 128
#define MESH_MAX_SPHERE_SEGMENTS 512
#define MESH_MAX_CYLINDER_SEGMENTS 8192

/// @brief Cheap wrapper around `isfinite` so attribute-validation call sites read declaratively.
static int mesh_value_is_finite(double value) {
    return isfinite(value);
}

/// @brief Guard for procedural-generator dimensions — trap if `value` is NaN/inf or ≤ 0.
/// @details The generators (`new_box`, `new_sphere`, `new_plane`, `new_cylinder`) all
///   require strictly positive, finite extents; any other value would produce degenerate
///   geometry or runaway loops. The trap message includes `label` so the caller site
///   (e.g. "Mesh3D.NewBox: sx") is identifiable in the user-facing error.
static int mesh_validate_positive_finite(double value, const char *label) {
    char msg[128];
    if (mesh_value_is_finite(value) && value > 0.0)
        return 1;
    snprintf(msg, sizeof(msg), "%s must be finite and greater than zero", label);
    rt_trap(msg);
    return 0;
}

/// @brief Release and discard a partially-built mesh if its build_failed flag is set.
/// @details Called at the end of every procedural generator (new_box, new_sphere,
///   new_plane, new_cylinder) and after the OBJ/STL loaders.  If any intermediate
///   add_vertex / add_triangle call trapped and set build_failed, this function
///   releases the GC reference so the partially-constructed mesh is freed, and
///   returns NULL to the caller.  NULL input and a clean mesh are both forwarded
///   unchanged, so the generator can use this as a tail-call return.
static void *mesh_return_null_if_build_failed(void *mesh) {
    if (!mesh || !((rt_mesh3d *)mesh)->build_failed)
        return mesh;
    if (rt_obj_release_check0(mesh))
        rt_obj_free(mesh);
    return NULL;
}

/// @brief Latch the `build_failed` bit on a mesh so subsequent builder calls bail early.
/// @details Once any add-vertex / add-triangle step traps, the mesh is in an indeterminate
///   state. Rather than mutating in place and risking inconsistent index/vertex counts,
///   we sticky-set this flag and let the OBJ/STL loader detect it and free the partially
///   built mesh.
static void mesh_mark_build_failed(rt_mesh3d *mesh) {
    if (mesh)
        mesh->build_failed = 1;
}

/// @brief Build a stable tangent orthogonal to `normal` when UVs are degenerate.
static void mesh_default_tangent_from_normal(const float *normal, float *tangent) {
    float n[3] = {0.0f, 0.0f, 1.0f};
    if (normal && isfinite(normal[0]) && isfinite(normal[1]) && isfinite(normal[2])) {
        float len = sqrtf(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
        if (len > 1e-8f) {
            n[0] = normal[0] / len;
            n[1] = normal[1] / len;
            n[2] = normal[2] / len;
        }
    }

    float axis[3] = {fabsf(n[0]) < 0.9f ? 1.0f : 0.0f, fabsf(n[0]) < 0.9f ? 0.0f : 1.0f, 0.0f};
    float dot = axis[0] * n[0] + axis[1] * n[1] + axis[2] * n[2];
    tangent[0] = axis[0] - n[0] * dot;
    tangent[1] = axis[1] - n[1] * dot;
    tangent[2] = axis[2] - n[2] * dot;
    {
        float len =
            sqrtf(tangent[0] * tangent[0] + tangent[1] * tangent[1] + tangent[2] * tangent[2]);
        if (len <= 1e-8f) {
            tangent[0] = 1.0f;
            tangent[1] = 0.0f;
            tangent[2] = 0.0f;
        } else {
            tangent[0] /= len;
            tangent[1] /= len;
            tangent[2] /= len;
        }
    }
    tangent[3] = 1.0f;
}

/// @brief Decrement-and-free on a GC-tracked reference slot; no-op if the slot is NULL.
/// @details Paired with `mesh_assign_ref` to implement `retain-then-release` ordering on
///   the `morph_targets_ref` field. The slot is cleared to NULL after release so a
///   subsequent assignment can't accidentally double-release.
static void mesh_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

/// @brief Safely swap in a new GC-tracked reference: retain new, then release old.
/// @details The retain-first ordering matters when `value` is already held transitively
///   through `*slot` — releasing first could drop the final reference and leave `value`
///   dangling. The early-return on self-assignment skips an unnecessary retain/release
///   round trip.
static void mesh_assign_ref(void **slot, void *value) {
    if (!slot || *slot == value)
        return;
    rt_obj_retain_maybe(value);
    mesh_release_ref(slot);
    *slot = value;
}

/// @brief GC finalizer for Mesh3D — releases the owned vertex and index buffers.
static void rt_mesh3d_finalize(void *obj) {
    rt_mesh3d *m = (rt_mesh3d *)obj;
    free(m->vertices);
    m->vertices = NULL;
    free(m->indices);
    m->indices = NULL;
    mesh_release_ref(&m->morph_targets_ref);
}

/// @brief Create a new empty 3D mesh for programmatic construction.
/// @details Allocates vertex and index arrays with initial capacity. Vertices are
///          stored as vgfx3d_vertex_t (84 bytes each, float internally) and indices
///          as uint32_t. The mesh supports up to 16M vertices. Geometry is built
///          by calling add_vertex/add_triangle, or by using the procedural generators
///          (new_box, new_sphere, new_plane, new_cylinder). GC finalizer frees arrays.
/// @return Opaque mesh handle, or NULL on allocation failure.
void *rt_mesh3d_new(void) {
    rt_mesh3d *m = (rt_mesh3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_mesh3d));
    if (!m) {
        rt_trap("Mesh3D.New: memory allocation failed");
        return NULL;
    }
    m->vptr = NULL;
    m->vertices = (vgfx3d_vertex_t *)calloc(MESH_INIT_VERTS, sizeof(vgfx3d_vertex_t));
    m->vertex_count = 0;
    m->vertex_capacity = MESH_INIT_VERTS;
    m->indices = (uint32_t *)calloc(MESH_INIT_IDXS, sizeof(uint32_t));
    m->index_count = 0;
    m->index_capacity = MESH_INIT_IDXS;
    m->bone_palette = NULL;
    m->bone_count = 0;
    m->morph_deltas = NULL;
    m->morph_weights = NULL;
    m->morph_shape_count = 0;
    m->morph_targets_ref = NULL;
    m->build_failed = 0;
    m->geometry_revision = 1;
    rt_mesh3d_reset_bounds(m);
    if (!m->vertices || !m->indices) {
        free(m->vertices);
        free(m->indices);
        m->vertices = NULL;
        m->indices = NULL;
        if (rt_obj_release_check0(m))
            rt_obj_free(m);
        rt_trap("Mesh3D.New: memory allocation failed");
        return NULL;
    }
    rt_obj_set_finalizer(m, rt_mesh3d_finalize);
    return mesh_return_null_if_build_failed(m);
}

/// @brief Remove all vertices and indices from the mesh, resetting to empty.
void rt_mesh3d_clear(void *obj) {
    if (!obj)
        return;
    rt_mesh3d *m = (rt_mesh3d *)obj;
    m->vertex_count = 0;
    m->index_count = 0;
    m->bone_palette = NULL;
    m->prev_bone_palette = NULL;
    m->bone_count = 0;
    m->morph_deltas = NULL;
    m->morph_normal_deltas = NULL;
    m->morph_weights = NULL;
    m->prev_morph_weights = NULL;
    m->morph_shape_count = 0;
    m->build_failed = 0;
    mesh_release_ref(&m->morph_targets_ref);
    rt_mesh3d_touch_geometry(m);
    rt_mesh3d_reset_bounds(m);
}

/// @brief Add a vertex with position, normal, and UV texture coordinates.
/// @details The vertex array grows geometrically when full. Normals should be
///          normalized (or recalculated later with recalc_normals). UV coords
///          use the standard [0,1] range. The vertex is stored as float internally
///          (vgfx3d_vertex_t), converted from the double parameters.
void rt_mesh3d_add_vertex(
    void *obj, double x, double y, double z, double nx, double ny, double nz, double u, double v) {
    if (!obj)
        return;
    rt_mesh3d *m = (rt_mesh3d *)obj;
    if (m->build_failed)
        return;
    if (!mesh_value_is_finite(x) || !mesh_value_is_finite(y) || !mesh_value_is_finite(z) ||
        !mesh_value_is_finite(nx) || !mesh_value_is_finite(ny) || !mesh_value_is_finite(nz) ||
        !mesh_value_is_finite(u) || !mesh_value_is_finite(v)) {
        mesh_mark_build_failed(m);
        rt_trap("Mesh3D.AddVertex: vertex attributes must be finite");
        return;
    }

    if (m->vertex_count >= m->vertex_capacity) {
        uint32_t new_cap;
        if (m->vertex_capacity > UINT32_MAX / 2u) {
            mesh_mark_build_failed(m);
            rt_trap("Mesh3D.AddVertex: vertex capacity overflow");
            return;
        }
        new_cap = m->vertex_capacity * 2u;
        vgfx3d_vertex_t *nv =
            (vgfx3d_vertex_t *)realloc(m->vertices, (size_t)new_cap * sizeof(vgfx3d_vertex_t));
        if (!nv) {
            mesh_mark_build_failed(m);
            rt_trap("Mesh3D.AddVertex: memory allocation failed");
            return;
        }
        m->vertices = nv;
        m->vertex_capacity = new_cap;
    }

    vgfx3d_vertex_t *vt = &m->vertices[m->vertex_count++];
    memset(vt, 0, sizeof(vgfx3d_vertex_t));
    vt->pos[0] = (float)x;
    vt->pos[1] = (float)y;
    vt->pos[2] = (float)z;
    vt->normal[0] = (float)nx;
    vt->normal[1] = (float)ny;
    vt->normal[2] = (float)nz;
    vt->uv[0] = (float)u;
    vt->uv[1] = (float)v;
    vt->uv1[0] = (float)u;
    vt->uv1[1] = (float)v;
    vt->color[0] = 1.0f;
    vt->color[1] = 1.0f;
    vt->color[2] = 1.0f;
    vt->color[3] = 1.0f;
    vt->tangent[3] = 1.0f;
    rt_mesh3d_touch_geometry(m);
}

/// @brief Add a triangle defined by three vertex indices (CCW winding = front-facing).
void rt_mesh3d_add_triangle(void *obj, int64_t v0, int64_t v1, int64_t v2) {
    if (!obj)
        return;
    rt_mesh3d *m = (rt_mesh3d *)obj;
    if (m->build_failed)
        return;

    if (v0 < 0 || v1 < 0 || v2 < 0) {
        mesh_mark_build_failed(m);
        rt_trap("Mesh3D.AddTriangle: vertex index must be non-negative");
        return;
    }
    if ((uint64_t)v0 >= m->vertex_count || (uint64_t)v1 >= m->vertex_count ||
        (uint64_t)v2 >= m->vertex_count) {
        mesh_mark_build_failed(m);
        rt_trap("Mesh3D.AddTriangle: vertex index out of range");
        return;
    }
    if (v0 == v1 || v1 == v2 || v0 == v2) {
        mesh_mark_build_failed(m);
        rt_trap("Mesh3D.AddTriangle: degenerate triangle");
        return;
    }

    if (m->index_count + 3 > m->index_capacity) {
        uint32_t new_cap;
        if (m->index_capacity > UINT32_MAX / 2u) {
            mesh_mark_build_failed(m);
            rt_trap("Mesh3D.AddTriangle: index capacity overflow");
            return;
        }
        new_cap = m->index_capacity * 2u;
        uint32_t *ni = (uint32_t *)realloc(m->indices, (size_t)new_cap * sizeof(uint32_t));
        if (!ni) {
            mesh_mark_build_failed(m);
            rt_trap("Mesh3D.AddTriangle: memory allocation failed");
            return;
        }
        m->indices = ni;
        m->index_capacity = new_cap;
    }

    m->indices[m->index_count++] = (uint32_t)v0;
    m->indices[m->index_count++] = (uint32_t)v1;
    m->indices[m->index_count++] = (uint32_t)v2;
    rt_mesh3d_touch_geometry(m);
}

/// @brief Get the number of vertices in the mesh.
int64_t rt_mesh3d_get_vertex_count(void *obj) {
    if (!obj)
        return 0;
    return (int64_t)((rt_mesh3d *)obj)->vertex_count;
}

/// @brief Get the number of triangles in the mesh (index_count / 3).
int64_t rt_mesh3d_get_triangle_count(void *obj) {
    if (!obj)
        return 0;
    return (int64_t)(((rt_mesh3d *)obj)->index_count / 3);
}

/// @brief Recalculate smooth vertex normals by averaging face normals per-vertex.
void rt_mesh3d_recalc_normals(void *obj) {
    if (!obj)
        return;
    rt_mesh3d *m = (rt_mesh3d *)obj;

    /* Zero all normals */
    for (uint32_t i = 0; i < m->vertex_count; i++) {
        m->vertices[i].normal[0] = 0.0f;
        m->vertices[i].normal[1] = 0.0f;
        m->vertices[i].normal[2] = 0.0f;
    }

    /* Accumulate face normals */
    for (uint32_t i = 0; i + 2 < m->index_count; i += 3) {
        uint32_t i0 = m->indices[i], i1 = m->indices[i + 1], i2 = m->indices[i + 2];
        if (i0 >= m->vertex_count || i1 >= m->vertex_count || i2 >= m->vertex_count)
            continue;

        float *p0 = m->vertices[i0].pos;
        float *p1 = m->vertices[i1].pos;
        float *p2 = m->vertices[i2].pos;

        float e1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
        float e2[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};

        /* cross product */
        float nx = e1[1] * e2[2] - e1[2] * e2[1];
        float ny = e1[2] * e2[0] - e1[0] * e2[2];
        float nz = e1[0] * e2[1] - e1[1] * e2[0];

        m->vertices[i0].normal[0] += nx;
        m->vertices[i0].normal[1] += ny;
        m->vertices[i0].normal[2] += nz;
        m->vertices[i1].normal[0] += nx;
        m->vertices[i1].normal[1] += ny;
        m->vertices[i1].normal[2] += nz;
        m->vertices[i2].normal[0] += nx;
        m->vertices[i2].normal[1] += ny;
        m->vertices[i2].normal[2] += nz;
    }

    /* Normalize */
    for (uint32_t i = 0; i < m->vertex_count; i++) {
        float *n = m->vertices[i].normal;
        float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len > 1e-8f) {
            n[0] /= len;
            n[1] /= len;
            n[2] /= len;
        }
    }
    rt_mesh3d_touch_geometry(m);
}

/// @brief Create a deep copy of a mesh (independent vertex/index arrays).
void *rt_mesh3d_clone(void *obj) {
    if (!obj)
        return NULL;
    rt_mesh3d *src = (rt_mesh3d *)obj;
    rt_mesh3d *dst = (rt_mesh3d *)rt_mesh3d_new();
    if (!dst)
        return NULL;

    free(dst->vertices);
    free(dst->indices);
    dst->vertices = NULL;
    dst->indices = NULL;

    if ((size_t)src->vertex_count > SIZE_MAX / sizeof(vgfx3d_vertex_t) ||
        (size_t)src->index_count > SIZE_MAX / sizeof(uint32_t)) {
        if (rt_obj_release_check0(dst))
            rt_obj_free(dst);
        rt_trap("Mesh3D.Clone: allocation overflow");
        return NULL;
    }

    dst->vertex_capacity = src->vertex_count > 0 ? src->vertex_count : 1;
    dst->vertices = (vgfx3d_vertex_t *)malloc(dst->vertex_capacity * sizeof(vgfx3d_vertex_t));
    dst->index_capacity = src->index_count > 0 ? src->index_count : 1;
    dst->indices = (uint32_t *)malloc(dst->index_capacity * sizeof(uint32_t));

    if (!dst->vertices || !dst->indices) {
        if (rt_obj_release_check0(dst))
            rt_obj_free(dst);
        return NULL;
    }

    dst->vertex_count = src->vertex_count;
    if (src->vertex_count > 0)
        memcpy(dst->vertices, src->vertices, src->vertex_count * sizeof(vgfx3d_vertex_t));

    dst->index_count = src->index_count;
    if (src->index_count > 0)
        memcpy(dst->indices, src->indices, src->index_count * sizeof(uint32_t));
    dst->bone_palette = NULL;
    dst->prev_bone_palette = NULL;
    dst->bone_count = 0;
    dst->morph_deltas = NULL;
    dst->morph_weights = NULL;
    dst->morph_shape_count = 0;
    dst->morph_normal_deltas = NULL;
    dst->prev_morph_weights = NULL;
    mesh_assign_ref(&dst->morph_targets_ref, src->morph_targets_ref);
    dst->geometry_revision = src->geometry_revision;
    dst->build_failed = src->build_failed;
    dst->aabb_min[0] = src->aabb_min[0];
    dst->aabb_min[1] = src->aabb_min[1];
    dst->aabb_min[2] = src->aabb_min[2];
    dst->aabb_max[0] = src->aabb_max[0];
    dst->aabb_max[1] = src->aabb_max[1];
    dst->aabb_max[2] = src->aabb_max[2];
    dst->bsphere_radius = src->bsphere_radius;
    dst->bounds_dirty = src->bounds_dirty;
    rt_mesh3d_refresh_bounds(dst);

    return dst;
}

/// @brief Apply a 4x4 matrix to every vertex position, normal, and tangent in-place.
/// @details Positions are transformed by the full affine matrix. Normals and tangents
///          need the inverse-transpose of the upper-left 3x3 (the "normal matrix") so
///          non-uniform scales don't tilt them off the surface; `vgfx3d_compute_normal_matrix4`
///          computes that once and the per-vertex loop reuses it.
///
///          The matrix determinant is inspected for a handedness flip (det < 0): when
///          the transform mirrors the mesh, the tangent-space bitangent must be negated
///          to keep the TBN frame right-handed. That flip is encoded in the tangent's
///          W component (stored in `t[3]`) so the shader can reconstruct the bitangent
///          with `cross(N,T) * W`. We preserve any existing W (treating 0 as the
///          canonical +1 for legacy meshes), then multiply by the handedness sign.
///
///          Normals and tangents are renormalized after transform — shear / non-uniform
///          scale otherwise produce non-unit vectors. After the pass, geometry is
///          marked dirty and bounds are recomputed so downstream culling stays correct.
/// @param obj Target mesh (no-op when NULL).
/// @param mat4_obj Mat4 handle (no-op when NULL).
void rt_mesh3d_transform(void *obj, void *mat4_obj) {
    if (!obj || !mat4_obj)
        return;
    rt_mesh3d *m = (rt_mesh3d *)obj;
    mat4_impl *xform = (mat4_impl *)mat4_obj;
    float model_matrix[16];
    float normal_matrix[16];
    float handedness_sign = 1.0f;

    for (int i = 0; i < 16; i++)
        model_matrix[i] = (float)xform->m[i];
    vgfx3d_compute_normal_matrix4(model_matrix, normal_matrix);
    {
        float det = model_matrix[0] * (model_matrix[5] * model_matrix[10] -
                                       model_matrix[6] * model_matrix[9]) -
                    model_matrix[1] * (model_matrix[4] * model_matrix[10] -
                                       model_matrix[6] * model_matrix[8]) +
                    model_matrix[2] * (model_matrix[4] * model_matrix[9] -
                                       model_matrix[5] * model_matrix[8]);
        if (det < 0.0f)
            handedness_sign = -1.0f;
    }

    for (uint32_t i = 0; i < m->vertex_count; i++) {
        float *p = m->vertices[i].pos;
        double x = p[0], y = p[1], z = p[2];
        p[0] = (float)(xform->m[0] * x + xform->m[1] * y + xform->m[2] * z + xform->m[3]);
        p[1] = (float)(xform->m[4] * x + xform->m[5] * y + xform->m[6] * z + xform->m[7]);
        p[2] = (float)(xform->m[8] * x + xform->m[9] * y + xform->m[10] * z + xform->m[11]);

        /* Transform normals with the inverse-transpose upper 3x3. */
        float *n = m->vertices[i].normal;
        double nx = n[0], ny = n[1], nz = n[2];
        n[0] = normal_matrix[0] * (float)nx + normal_matrix[1] * (float)ny +
               normal_matrix[2] * (float)nz;
        n[1] = normal_matrix[4] * (float)nx + normal_matrix[5] * (float)ny +
               normal_matrix[6] * (float)nz;
        n[2] = normal_matrix[8] * (float)nx + normal_matrix[9] * (float)ny +
               normal_matrix[10] * (float)nz;
        float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len > 1e-8f) {
            n[0] /= len;
            n[1] /= len;
            n[2] /= len;
        }

        float *t = m->vertices[i].tangent;
        float handedness = t[3];
        float tx = t[0];
        float ty = t[1];
        float tz = t[2];
        t[0] = normal_matrix[0] * tx + normal_matrix[1] * ty + normal_matrix[2] * tz;
        t[1] = normal_matrix[4] * tx + normal_matrix[5] * ty + normal_matrix[6] * tz;
        t[2] = normal_matrix[8] * tx + normal_matrix[9] * ty + normal_matrix[10] * tz;
        len = sqrtf(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
        if (len > 1e-8f) {
            t[0] /= len;
            t[1] /= len;
            t[2] /= len;
        }
        t[3] = (handedness == 0.0f ? 1.0f : handedness) * handedness_sign;
    }
    rt_mesh3d_touch_geometry(m);
    rt_mesh3d_refresh_bounds(m);
}

/* Procedural generators — NewBox, NewSphere, NewPlane, NewCylinder */
/// @brief Generate a box mesh centered at the origin with the given half-extents.
void *rt_mesh3d_new_box(double sx, double sy, double sz) {
    if (!mesh_validate_positive_finite(sx, "Mesh3D.NewBox: sx") ||
        !mesh_validate_positive_finite(sy, "Mesh3D.NewBox: sy") ||
        !mesh_validate_positive_finite(sz, "Mesh3D.NewBox: sz"))
        return NULL;
    void *m = rt_mesh3d_new();
    if (!m)
        return NULL;

    float hx = (float)(sx * 0.5), hy = (float)(sy * 0.5), hz = (float)(sz * 0.5);

    /* 6 faces, 4 verts each = 24 verts, 12 triangles (CCW winding) */
    /* Front face (+Z) */
    rt_mesh3d_add_vertex(m, -hx, -hy, hz, 0, 0, 1, 0, 1);
    rt_mesh3d_add_vertex(m, hx, -hy, hz, 0, 0, 1, 1, 1);
    rt_mesh3d_add_vertex(m, hx, hy, hz, 0, 0, 1, 1, 0);
    rt_mesh3d_add_vertex(m, -hx, hy, hz, 0, 0, 1, 0, 0);
    rt_mesh3d_add_triangle(m, 0, 1, 2);
    rt_mesh3d_add_triangle(m, 0, 2, 3);

    /* Back face (-Z) */
    rt_mesh3d_add_vertex(m, hx, -hy, -hz, 0, 0, -1, 0, 1);
    rt_mesh3d_add_vertex(m, -hx, -hy, -hz, 0, 0, -1, 1, 1);
    rt_mesh3d_add_vertex(m, -hx, hy, -hz, 0, 0, -1, 1, 0);
    rt_mesh3d_add_vertex(m, hx, hy, -hz, 0, 0, -1, 0, 0);
    rt_mesh3d_add_triangle(m, 4, 5, 6);
    rt_mesh3d_add_triangle(m, 4, 6, 7);

    /* Right face (+X) */
    rt_mesh3d_add_vertex(m, hx, -hy, hz, 1, 0, 0, 0, 1);
    rt_mesh3d_add_vertex(m, hx, -hy, -hz, 1, 0, 0, 1, 1);
    rt_mesh3d_add_vertex(m, hx, hy, -hz, 1, 0, 0, 1, 0);
    rt_mesh3d_add_vertex(m, hx, hy, hz, 1, 0, 0, 0, 0);
    rt_mesh3d_add_triangle(m, 8, 9, 10);
    rt_mesh3d_add_triangle(m, 8, 10, 11);

    /* Left face (-X) */
    rt_mesh3d_add_vertex(m, -hx, -hy, -hz, -1, 0, 0, 0, 1);
    rt_mesh3d_add_vertex(m, -hx, -hy, hz, -1, 0, 0, 1, 1);
    rt_mesh3d_add_vertex(m, -hx, hy, hz, -1, 0, 0, 1, 0);
    rt_mesh3d_add_vertex(m, -hx, hy, -hz, -1, 0, 0, 0, 0);
    rt_mesh3d_add_triangle(m, 12, 13, 14);
    rt_mesh3d_add_triangle(m, 12, 14, 15);

    /* Top face (+Y) */
    rt_mesh3d_add_vertex(m, -hx, hy, hz, 0, 1, 0, 0, 1);
    rt_mesh3d_add_vertex(m, hx, hy, hz, 0, 1, 0, 1, 1);
    rt_mesh3d_add_vertex(m, hx, hy, -hz, 0, 1, 0, 1, 0);
    rt_mesh3d_add_vertex(m, -hx, hy, -hz, 0, 1, 0, 0, 0);
    rt_mesh3d_add_triangle(m, 16, 17, 18);
    rt_mesh3d_add_triangle(m, 16, 18, 19);

    /* Bottom face (-Y) */
    rt_mesh3d_add_vertex(m, -hx, -hy, -hz, 0, -1, 0, 0, 1);
    rt_mesh3d_add_vertex(m, hx, -hy, -hz, 0, -1, 0, 1, 1);
    rt_mesh3d_add_vertex(m, hx, -hy, hz, 0, -1, 0, 1, 0);
    rt_mesh3d_add_vertex(m, -hx, -hy, hz, 0, -1, 0, 0, 0);
    rt_mesh3d_add_triangle(m, 20, 21, 22);
    rt_mesh3d_add_triangle(m, 20, 22, 23);

    return mesh_return_null_if_build_failed(m);
}

/// @brief Generate a UV sphere mesh with the given radius and segment count.
void *rt_mesh3d_new_sphere(double radius, int64_t segments) {
    if (!mesh_validate_positive_finite(radius, "Mesh3D.NewSphere: radius"))
        return NULL;
    if (segments > MESH_MAX_SPHERE_SEGMENTS) {
        rt_trap("Mesh3D.NewSphere: segments exceeds safe limit");
        return NULL;
    }
    void *m = rt_mesh3d_new();
    if (!m)
        return NULL;
    if (segments < 4)
        segments = 4;

    int64_t rings = segments;
    int64_t slices = segments * 2;
    float r = (float)radius;

    /* Generate vertices */
    for (int64_t ring = 0; ring <= rings; ring++) {
        float phi = (float)M_PI * (float)ring / (float)rings;
        float sp = sinf(phi), cp = cosf(phi);

        for (int64_t slice = 0; slice <= slices; slice++) {
            float theta = 2.0f * (float)M_PI * (float)slice / (float)slices;
            float st = sinf(theta), ct = cosf(theta);

            float nx = sp * ct, ny = cp, nz = sp * st;
            float u = (float)slice / (float)slices;
            float v = (float)ring / (float)rings;
            rt_mesh3d_add_vertex(m, nx * r, ny * r, nz * r, nx, ny, nz, u, v);
        }
    }

    /* Generate indices (CCW) */
    for (int64_t ring = 0; ring < rings; ring++) {
        for (int64_t slice = 0; slice < slices; slice++) {
            int64_t a = ring * (slices + 1) + slice;
            int64_t b = a + slices + 1;
            if (ring == 0) {
                rt_mesh3d_add_triangle(m, a + 1, b, b + 1);
            } else if (ring == rings - 1) {
                rt_mesh3d_add_triangle(m, a, b, a + 1);
            } else {
                rt_mesh3d_add_triangle(m, a, b, a + 1);
                rt_mesh3d_add_triangle(m, a + 1, b, b + 1);
            }
        }
    }

    return mesh_return_null_if_build_failed(m);
}

/// @brief Generate a flat plane mesh on the XZ plane (facing +Y) with the given size.
void *rt_mesh3d_new_plane(double sx, double sz) {
    if (!mesh_validate_positive_finite(sx, "Mesh3D.NewPlane: sx") ||
        !mesh_validate_positive_finite(sz, "Mesh3D.NewPlane: sz"))
        return NULL;
    void *m = rt_mesh3d_new();
    if (!m)
        return NULL;

    float hx = (float)(sx * 0.5), hz = (float)(sz * 0.5);

    rt_mesh3d_add_vertex(m, -hx, 0, -hz, 0, 1, 0, 0, 0);
    rt_mesh3d_add_vertex(m, hx, 0, -hz, 0, 1, 0, 1, 0);
    rt_mesh3d_add_vertex(m, hx, 0, hz, 0, 1, 0, 1, 1);
    rt_mesh3d_add_vertex(m, -hx, 0, hz, 0, 1, 0, 0, 1);

    /* CCW winding — matches box/sphere/cylinder convention.
     * Vertex normals are set explicitly to +Y above; face winding
     * only affects backface culling, not lighting. */
    rt_mesh3d_add_triangle(m, 0, 1, 2);
    rt_mesh3d_add_triangle(m, 0, 2, 3);

    return mesh_return_null_if_build_failed(m);
}

/// @brief Generate a cylinder mesh with circular caps, centered at the origin.
void *rt_mesh3d_new_cylinder(double radius, double height, int64_t segments) {
    if (!mesh_validate_positive_finite(radius, "Mesh3D.NewCylinder: radius") ||
        !mesh_validate_positive_finite(height, "Mesh3D.NewCylinder: height"))
        return NULL;
    if (segments > MESH_MAX_CYLINDER_SEGMENTS) {
        rt_trap("Mesh3D.NewCylinder: segments exceeds safe limit");
        return NULL;
    }
    void *m = rt_mesh3d_new();
    if (!m)
        return NULL;
    if (segments < 3)
        segments = 3;

    float r = (float)radius;
    float hy = (float)(height * 0.5);

    /* Side vertices */
    for (int64_t i = 0; i <= segments; i++) {
        float theta = 2.0f * (float)M_PI * (float)i / (float)segments;
        float ct = cosf(theta), st = sinf(theta);
        float u = (float)i / (float)segments;

        rt_mesh3d_add_vertex(m, r * ct, -hy, r * st, ct, 0, st, u, 1.0);
        rt_mesh3d_add_vertex(m, r * ct, hy, r * st, ct, 0, st, u, 0.0);
    }

    /* Side triangles */
    for (int64_t i = 0; i < segments; i++) {
        int64_t b = i * 2;
        rt_mesh3d_add_triangle(m, b, b + 2, b + 1);
        rt_mesh3d_add_triangle(m, b + 1, b + 2, b + 3);
    }

    /* Top cap center */
    int64_t tc = (int64_t)((rt_mesh3d *)m)->vertex_count;
    rt_mesh3d_add_vertex(m, 0, hy, 0, 0, 1, 0, 0.5, 0.5);
    for (int64_t i = 0; i < segments; i++) {
        float theta = 2.0f * (float)M_PI * (float)i / (float)segments;
        float ct = cosf(theta), st = sinf(theta);
        rt_mesh3d_add_vertex(m, r * ct, hy, r * st, 0, 1, 0, ct * 0.5 + 0.5, st * 0.5 + 0.5);
    }
    for (int64_t i = 0; i < segments; i++) {
        int64_t next = (i + 1) % segments;
        rt_mesh3d_add_triangle(m, tc, tc + 1 + i, tc + 1 + next);
    }

    /* Bottom cap center */
    int64_t bc = (int64_t)((rt_mesh3d *)m)->vertex_count;
    rt_mesh3d_add_vertex(m, 0, -hy, 0, 0, -1, 0, 0.5, 0.5);
    for (int64_t i = 0; i < segments; i++) {
        float theta = 2.0f * (float)M_PI * (float)i / (float)segments;
        float ct = cosf(theta), st = sinf(theta);
        rt_mesh3d_add_vertex(m, r * ct, -hy, r * st, 0, -1, 0, ct * 0.5 + 0.5, st * 0.5 + 0.5);
    }
    for (int64_t i = 0; i < segments; i++) {
        int64_t next = (i + 1) % segments;
        rt_mesh3d_add_triangle(m, bc, bc + 1 + next, bc + 1 + i);
    }

    return mesh_return_null_if_build_failed(m);
}

/*==========================================================================
 * Wavefront OBJ loader (~300 LOC)
 *
 * Supports: v, vn, vt, f (v, v/vt, v/vt/vn, v//vn), # comments,
 *           negative indices, quad auto-triangulation.
 * Viper-specific limitation: geometry-only import. Authored OBJ material/group
 * splits must be pre-flattened before reaching this runtime loader.
 *=========================================================================*/

/* Parse one integer from a face index string, advancing *p past it.
 * Returns 0 if no integer found (empty between slashes). */
// ---------------------------------------------------------------------------
// Wavefront OBJ parser helpers — OBJ is a text format where each
// line begins with a tag (`v` for vertex, `vt` for tex coord, `vn`
// for normal, `f` for face). These parsers walk a `**p` cursor
// through the line, consuming whitespace and tokens.
// ---------------------------------------------------------------------------

/// @brief Parse a signed decimal integer from `*p`, advancing `*p` past it.
static int obj_parse_int(const char **p, int64_t *out) {
    const char *s = *p;
    int negative = 0;
    uint64_t val = 0;
    int saw_digit = 0;
    if (!*s || *s == '/' || *s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') {
        *out = 0;
        return 1;
    }
    if (*s == '-') {
        negative = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        uint64_t digit = (uint64_t)(*s - '0');
        if (val > ((uint64_t)INT64_MAX - digit) / 10u)
            return 0;
        val = val * 10u + digit;
        saw_digit = 1;
        s++;
    }
    if (!saw_digit)
        return 0;
    *out = negative ? -(int64_t)val : (int64_t)val;
    *p = s;
    return 1;
}

/* Parse a face vertex index: v[/vt[/vn]] or v//vn
 * Sets vi, ti, ni to 1-based indices (0 = not present) */
/// @brief Parse a face vertex spec `v[/[vt][/vn]]` — slash-separated optional indices.
///
/// The OBJ face syntax allows three forms:
///   - `v`         (vertex only — `ti = ni = 0`)
///   - `v/vt`      (vertex + tex)
///   - `v//vn`     (vertex + normal, no tex)
///   - `v/vt/vn`   (all three)
/// 1-based indices in the file (OBJ convention); negative values
/// reference from the end of the current vertex list.
static int obj_parse_face_vert(const char **p, int64_t *vi, int64_t *ti, int64_t *ni) {
    if (!obj_parse_int(p, vi))
        return 0;
    *ti = 0;
    *ni = 0;
    if (**p == '/') {
        (*p)++;
        if (!obj_parse_int(p, ti))
            return 0;
        if (**p == '/') {
            (*p)++;
            if (!obj_parse_int(p, ni))
                return 0;
        }
    }
    return 1;
}

/// @brief Parse a double-precision float from `*p` via `strtod`, advancing `*p` past it.
static int obj_parse_double(const char **p, double *out) {
    while (**p == ' ' || **p == '\t')
        (*p)++;
    const char *start = *p;
    char *end;
    errno = 0;
    double val = strtod(*p, &end);
    if (end == start || errno == ERANGE || !isfinite(val))
        return 0;
    *p = end;
    *out = val;
    return 1;
}

/// @brief Double the capacity of a float accumulator array used during OBJ parsing.
/// @details The OBJ parser pre-allocates small position/normal/UV arrays and
///   grows them geometrically via this helper when the current capacity is
///   exhausted.  The `components` parameter (2 for UV, 3 for positions/normals)
///   is folded into the byte-count calculation so both array types share one
///   growth path.  Guards against INT_MAX/2 overflow on the new-capacity
///   integer and against SIZE_MAX overflow on the byte count before calling
///   realloc.  Returns 0 on any overflow or allocation failure.
static int obj_grow_float_array(float **array, int *capacity, int components) {
    if (!array || !capacity || components <= 0)
        return 0;
    if (*capacity > INT_MAX / 2)
        return 0;
    int new_capacity = *capacity * 2;
    size_t component_bytes = (size_t)components * sizeof(float);
    if ((size_t)new_capacity > SIZE_MAX / component_bytes)
        return 0;
    float *tmp = (float *)realloc(*array, (size_t)new_capacity * component_bytes);
    if (!tmp)
        return 0;
    *array = tmp;
    *capacity = new_capacity;
    return 1;
}

typedef struct {
    int64_t vi;
    int64_t ti;
    int64_t ni;
    uint32_t mesh_index;
    uint8_t used;
} obj_vertex_cache_entry_t;

typedef struct {
    obj_vertex_cache_entry_t *entries;
    size_t capacity;
    size_t count;
} obj_vertex_cache_t;

/// @brief FNV-1a-ish mix of the `(v, vt, vn)` OBJ face triplet into a 64-bit hash.
/// @details The mixing constants are the standard FNV offset basis + prime, combined with
///   Knuth's golden-ratio constant and a Boost-style XOR-shift round to distribute low bits.
///   Collisions fall back to linear probing in the cache — any well-distributed hash works
///   here; this one was picked for speed and stability across platforms.
static uint64_t obj_hash_index_triplet(int64_t vi, int64_t ti, int64_t ni) {
    uint64_t h = 1469598103934665603ull;
    uint64_t values[3] = {(uint64_t)vi, (uint64_t)ti, (uint64_t)ni};
    for (int i = 0; i < 3; i++) {
        h ^= values[i] + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        h *= 1099511628211ull;
    }
    return h;
}

/// @brief Allocate an open-addressing hash table for the OBJ vertex-deduplication cache.
/// @details Capacity is floored to 64 and must be a power of two for the `(h + probe) & (cap-1)`
///   slot wrap used by lookup/insert — the initial call sites in `rt_mesh3d_from_obj`
///   respect this by passing 1024, and `obj_vertex_cache_grow` doubles. Returns 1 on
///   success, 0 if the underlying calloc fails.
static int obj_vertex_cache_init(obj_vertex_cache_t *cache, size_t capacity) {
    if (!cache)
        return 0;
    if (capacity < 64)
        capacity = 64;
    cache->entries = (obj_vertex_cache_entry_t *)calloc(capacity, sizeof(*cache->entries));
    cache->capacity = cache->entries ? capacity : 0;
    cache->count = 0;
    return cache->entries != NULL;
}

/// @brief Release the entry array owned by an OBJ vertex cache; safe on an already-empty cache.
static void obj_vertex_cache_free(obj_vertex_cache_t *cache) {
    if (!cache)
        return;
    free(cache->entries);
    cache->entries = NULL;
    cache->capacity = 0;
    cache->count = 0;
}

/// @brief Double the hash table capacity and rehash all live entries.
/// @details Called by `obj_vertex_cache_insert` when the load factor (count/capacity) would
///   exceed 70 %. Growing early keeps the open-addressing probe chains short so lookup
///   stays close to O(1) even when a big OBJ file reuses the same vertex triplets
///   thousands of times. Returns 0 if the new allocation fails or capacity would overflow.
static int obj_vertex_cache_grow(obj_vertex_cache_t *cache) {
    obj_vertex_cache_t grown;
    size_t new_capacity;
    if (!cache || !cache->entries)
        return 0;
    if (cache->capacity > SIZE_MAX / 2u)
        return 0;
    new_capacity = cache->capacity * 2u;
    if (!obj_vertex_cache_init(&grown, new_capacity))
        return 0;
    for (size_t i = 0; i < cache->capacity; i++) {
        obj_vertex_cache_entry_t entry = cache->entries[i];
        if (!entry.used)
            continue;
        uint64_t h = obj_hash_index_triplet(entry.vi, entry.ti, entry.ni);
        for (size_t probe = 0; probe < grown.capacity; probe++) {
            size_t slot = (size_t)((h + probe) & (uint64_t)(grown.capacity - 1u));
            if (!grown.entries[slot].used) {
                grown.entries[slot] = entry;
                grown.count++;
                break;
            }
        }
    }
    free(cache->entries);
    *cache = grown;
    return 1;
}

/// @brief Linear-probe lookup for `(vi, ti, ni)` in the OBJ vertex cache.
/// @details Returns 1 and writes the cached mesh-vertex index when found, 0 otherwise.
///   Probing stops at the first unused slot — matching the invariant maintained by
///   `obj_vertex_cache_insert` that entries are contiguous along their probe chain.
static int obj_vertex_cache_lookup(const obj_vertex_cache_t *cache,
                                   int64_t vi,
                                   int64_t ti,
                                   int64_t ni,
                                   uint32_t *out_index) {
    if (!cache || !cache->entries || cache->capacity == 0)
        return 0;
    uint64_t h = obj_hash_index_triplet(vi, ti, ni);
    for (size_t probe = 0; probe < cache->capacity; probe++) {
        size_t slot = (size_t)((h + probe) & (uint64_t)(cache->capacity - 1u));
        const obj_vertex_cache_entry_t *entry = &cache->entries[slot];
        if (!entry->used)
            return 0;
        if (entry->vi == vi && entry->ti == ti && entry->ni == ni) {
            if (out_index)
                *out_index = entry->mesh_index;
            return 1;
        }
    }
    return 0;
}

/// @brief Record that `(vi, ti, ni)` has been emitted as mesh vertex `mesh_index`.
/// @details Auto-grows when load would exceed ~70 % to keep probe chains short. Returns
///   1 on success, 0 on allocation failure in grow (which propagates upward to fail the
///   OBJ import). Duplicate keys never reach this function — callers always do a lookup
///   first via `obj_get_or_add_mesh_vertex`.
static int obj_vertex_cache_insert(obj_vertex_cache_t *cache,
                                   int64_t vi,
                                   int64_t ti,
                                   int64_t ni,
                                   uint32_t mesh_index) {
    if (!cache || !cache->entries)
        return 0;
    if ((cache->count + 1u) * 10u >= cache->capacity * 7u) {
        if (!obj_vertex_cache_grow(cache))
            return 0;
    }
    uint64_t h = obj_hash_index_triplet(vi, ti, ni);
    for (size_t probe = 0; probe < cache->capacity; probe++) {
        size_t slot = (size_t)((h + probe) & (uint64_t)(cache->capacity - 1u));
        obj_vertex_cache_entry_t *entry = &cache->entries[slot];
        if (!entry->used) {
            entry->vi = vi;
            entry->ti = ti;
            entry->ni = ni;
            entry->mesh_index = mesh_index;
            entry->used = 1;
            cache->count++;
            return 1;
        }
    }
    return 0;
}

/// @brief Turn an OBJ face index into a 1-based absolute index, or reject it.
/// @details OBJ face indices are 1-based and can be negative (counting from the end of
///   the currently-defined list). A raw value of 0 means "no index at this slot" —
///   accepted only when `allow_missing` is nonzero (used for optional UV/normal slots;
///   position is always required). Writes the resolved positive index into `*out` and
///   returns 1 on success, 0 on out-of-range or a missing-but-required zero.
static int obj_resolve_index(int64_t raw, int count, int allow_missing, int64_t *out) {
    int64_t resolved = raw;
    if (!out)
        return 0;
    if (raw == 0) {
        if (allow_missing) {
            *out = 0;
            return 1;
        }
        return 0;
    }
    if (raw < 0)
        resolved = (int64_t)count + raw + 1;
    if (resolved < 1 || resolved > count)
        return 0;
    *out = resolved;
    return 1;
}

/// @brief Resolve an OBJ face triplet to a mesh-vertex index, emitting a new vertex on miss.
/// @details This is the workhorse of vertex deduplication. First tries the cache; on a
///   hit, returns the existing mesh index (the same GPU vertex is reused across faces).
///   On a miss, reads the referenced position/normal/UV from the parallel OBJ attribute
///   arrays, appends a new mesh vertex, records the mapping in the cache, and returns
///   the new index. Missing normal/UV indices (zero) emit a zeroed attribute — caller
///   recomputes normals via `rt_mesh3d_recalc_normals` if the file had none.
/// @return 1 on success, 0 on any failure (cache full, vertex-count overflow, or
///         `rt_mesh3d_add_vertex` trapped).
static int obj_get_or_add_mesh_vertex(void *mesh,
                                      obj_vertex_cache_t *cache,
                                      const float *positions,
                                      int cnt_p,
                                      const float *normals,
                                      int cnt_n,
                                      const float *texcoords,
                                      int cnt_t,
                                      int64_t vi,
                                      int64_t ti,
                                      int64_t ni,
                                      uint32_t *out_index) {
    uint32_t cached_index;
    (void)cnt_p;
    if (obj_vertex_cache_lookup(cache, vi, ti, ni, &cached_index)) {
        if (out_index)
            *out_index = cached_index;
        return 1;
    }

    float px = positions[(vi - 1) * 3 + 0];
    float py = positions[(vi - 1) * 3 + 1];
    float pz = positions[(vi - 1) * 3 + 2];
    float nx = 0.0f, ny = 0.0f, nz = 0.0f;
    float tu = 0.0f, tv = 0.0f;

    if (ni >= 1 && ni <= cnt_n) {
        nx = normals[(ni - 1) * 3 + 0];
        ny = normals[(ni - 1) * 3 + 1];
        nz = normals[(ni - 1) * 3 + 2];
    }
    if (ti >= 1 && ti <= cnt_t) {
        tu = texcoords[(ti - 1) * 2 + 0];
        tv = texcoords[(ti - 1) * 2 + 1];
    }

    rt_mesh3d *m = (rt_mesh3d *)mesh;
    if (m->vertex_count == UINT32_MAX)
        return 0;
    cached_index = m->vertex_count;
    rt_mesh3d_add_vertex(mesh, px, py, pz, nx, ny, nz, tu, tv);
    if (m->build_failed || m->vertex_count <= cached_index)
        return 0;
    if (!obj_vertex_cache_insert(cache, vi, ti, ni, cached_index))
        return 0;
    if (out_index)
        *out_index = cached_index;
    return 1;
}

/// @brief Calculate per-vertex tangent vectors for normal mapping (Lengyel's method).
/// @details Tangent space is the local coordinate system at each vertex where the
///          U texture axis aligns with the tangent and V aligns with the bitangent.
///          Normal maps are authored in this space, so we need per-vertex T to read
///          them correctly.
///
///          Two-phase algorithm (classic Eric Lengyel, "Mathematics for 3D Game
///          Programming and Computer Graphics"):
///
///          Phase 1: For each triangle derive the tangent (`sdir`) and bitangent
///          (`tdir`) directions by solving the 2x2 system that takes (dU,dV) to
///          (dPos). `det = duv1.x*duv2.y - duv1.y*duv2.x` is the UV-triangle's
///          signed area; triangles with zero UV area contribute nothing (would
///          divide by zero). The per-vertex tangent/bitangent accumulators
///          `tan1[]`/`tan2[]` sum contributions from every incident triangle so
///          the final directions are area-weighted.
///
///          Phase 2: For each vertex, run Gram-Schmidt against the normal to make
///          the tangent strictly perpendicular to N (so N, T, B form an orthonormal
///          frame even if the summed tangent wasn't quite tangential). Then derive
///          handedness: `dot(cross(N,T), summed_bitangent) < 0` means the UV chart
///          is mirrored on this face relative to the object, and the bitangent in
///          the shader must be negated — we encode that as `tangent.w = -1`. The
///          +1/-1 W channel is how the shader reconstructs B cheaply.
///
///          Degenerate cases (zero-length tangent after orthogonalization) fall back
///          to a canonical +X tangent with W=+1 so the vertex still produces sensible
///          output in the shader.
///
///          Allocates two transient float buffers (`tan1`, `tan2`) sized to the
///          vertex count; both are freed before returning. No-op on empty meshes.
/// @param obj Mesh whose `tangent[4]` slot on every vertex gets written in place.
void rt_mesh3d_calc_tangents(void *obj) {
    if (!obj)
        return;
    rt_mesh3d *m = (rt_mesh3d *)obj;
    if (m->vertex_count == 0 || m->index_count == 0)
        return;

    float *tan1 = (float *)calloc((size_t)m->vertex_count * 3u, sizeof(float));
    float *tan2 = (float *)calloc((size_t)m->vertex_count * 3u, sizeof(float));
    if (!tan1 || !tan2) {
        free(tan1);
        free(tan2);
        return;
    }

    /* Accumulate tangents and bitangents per triangle (Lengyel's method). */
    for (uint32_t i = 0; i + 2 < m->index_count; i += 3) {
        uint32_t i0 = m->indices[i], i1 = m->indices[i + 1], i2 = m->indices[i + 2];
        if (i0 >= m->vertex_count || i1 >= m->vertex_count || i2 >= m->vertex_count)
            continue;

        float *p0 = m->vertices[i0].pos;
        float *p1 = m->vertices[i1].pos;
        float *p2 = m->vertices[i2].pos;
        float *uv0 = m->vertices[i0].uv;
        float *uv1 = m->vertices[i1].uv;
        float *uv2 = m->vertices[i2].uv;

        float edge1[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
        float edge2[3] = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
        float duv1[2] = {uv1[0] - uv0[0], uv1[1] - uv0[1]};
        float duv2[2] = {uv2[0] - uv0[0], uv2[1] - uv0[1]};

        float det = duv1[0] * duv2[1] - duv1[1] * duv2[0];
        if (fabsf(det) < 1e-8f)
            continue; /* degenerate UV */
        float inv_det = 1.0f / det;

        float sdir[3] = {(edge1[0] * duv2[1] - edge2[0] * duv1[1]) * inv_det,
                         (edge1[1] * duv2[1] - edge2[1] * duv1[1]) * inv_det,
                         (edge1[2] * duv2[1] - edge2[2] * duv1[1]) * inv_det};
        float tdir[3] = {(edge2[0] * duv1[0] - edge1[0] * duv2[0]) * inv_det,
                         (edge2[1] * duv1[0] - edge1[1] * duv2[0]) * inv_det,
                         (edge2[2] * duv1[0] - edge1[2] * duv2[0]) * inv_det};

        tan1[i0 * 3u + 0] += sdir[0];
        tan1[i0 * 3u + 1] += sdir[1];
        tan1[i0 * 3u + 2] += sdir[2];
        tan1[i1 * 3u + 0] += sdir[0];
        tan1[i1 * 3u + 1] += sdir[1];
        tan1[i1 * 3u + 2] += sdir[2];
        tan1[i2 * 3u + 0] += sdir[0];
        tan1[i2 * 3u + 1] += sdir[1];
        tan1[i2 * 3u + 2] += sdir[2];

        tan2[i0 * 3u + 0] += tdir[0];
        tan2[i0 * 3u + 1] += tdir[1];
        tan2[i0 * 3u + 2] += tdir[2];
        tan2[i1 * 3u + 0] += tdir[0];
        tan2[i1 * 3u + 1] += tdir[1];
        tan2[i1 * 3u + 2] += tdir[2];
        tan2[i2 * 3u + 0] += tdir[0];
        tan2[i2 * 3u + 1] += tdir[1];
        tan2[i2 * 3u + 2] += tdir[2];
    }

    /* Normalize, orthogonalize against the normal, and store handedness in tangent.w. */
    for (uint32_t i = 0; i < m->vertex_count; i++) {
        float *t = m->vertices[i].tangent;
        float *n = m->vertices[i].normal;
        float *tan = &tan1[i * 3u];
        float *bitan = &tan2[i * 3u];

        /* Gram-Schmidt: T = T - N * dot(N, T) */
        float dot = n[0] * tan[0] + n[1] * tan[1] + n[2] * tan[2];
        t[0] = tan[0] - n[0] * dot;
        t[1] = tan[1] - n[1] * dot;
        t[2] = tan[2] - n[2] * dot;

        /* Normalize */
        float len = sqrtf(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
        if (len > 1e-8f) {
            t[0] /= len;
            t[1] /= len;
            t[2] /= len;
            {
                float cross_nt[3] = {n[1] * t[2] - n[2] * t[1],
                                     n[2] * t[0] - n[0] * t[2],
                                     n[0] * t[1] - n[1] * t[0]};
                t[3] = (cross_nt[0] * bitan[0] + cross_nt[1] * bitan[1] +
                        cross_nt[2] * bitan[2]) < 0.0f
                           ? -1.0f
                           : 1.0f;
            }
        } else {
            mesh_default_tangent_from_normal(n, t);
        }
    }
    free(tan1);
    free(tan2);
    rt_mesh3d_touch_geometry(m);
}

//=============================================================================
// OBJ Loading (Wavefront)
//=============================================================================

/// @brief Load a mesh from a Wavefront OBJ file (positions, normals, UVs, faces).
/// @details Parses v/vn/vt/f lines from the OBJ text format. Supports
///          triangulated and quad faces (quads are split into two triangles).
///          Vertex data is de-duplicated by unique (position, normal, UV) tuples.
/// @param path File path to the .obj file (runtime string).
/// @return Mesh handle, or NULL on parse/load failure.
void *rt_mesh3d_from_obj(rt_string path) {
    if (!path) {
        rt_trap("Mesh3D.FromOBJ: path must not be null");
        return NULL;
    }
    const char *filepath = rt_string_cstr(path);
    if (!filepath)
        return NULL;

    FILE *f = fopen(filepath, "r");
    if (!f) {
        rt_trap("Mesh3D.FromOBJ: failed to open file");
        return NULL;
    }

    /* Temporary arrays for positions, normals, UVs */
    int cap_p = 256, cap_n = 256, cap_t = 256;
    int cnt_p = 0, cnt_n = 0, cnt_t = 0;
    int parse_failed = 0;
    float *positions = (float *)malloc((size_t)cap_p * 3 * sizeof(float));
    float *normals = (float *)malloc((size_t)cap_n * 3 * sizeof(float));
    float *texcoords = (float *)malloc((size_t)cap_t * 2 * sizeof(float));
    obj_vertex_cache_t vertex_cache;
    memset(&vertex_cache, 0, sizeof(vertex_cache));

    void *mesh = rt_mesh3d_new();
    if (!mesh || !positions || !normals || !texcoords ||
        !obj_vertex_cache_init(&vertex_cache, 1024)) {
        fclose(f);
        free(positions);
        free(normals);
        free(texcoords);
        obj_vertex_cache_free(&vertex_cache);
        if (mesh && rt_obj_release_check0(mesh))
            rt_obj_free(mesh);
        return NULL;
    }

    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        const char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;

        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0')
            continue;

        if (p[0] == 'v' && p[1] == ' ') {
            /* Vertex position: v x y z */
            p += 2;
            if (cnt_p >= cap_p) {
                if (!obj_grow_float_array(&positions, &cap_p, 3)) {
                    parse_failed = 1;
                    break;
                }
            }
            {
                double x, y, z;
                if (!obj_parse_double(&p, &x) || !obj_parse_double(&p, &y) ||
                    !obj_parse_double(&p, &z)) {
                    parse_failed = 1;
                    break;
                }
                positions[cnt_p * 3 + 0] = (float)x;
                positions[cnt_p * 3 + 1] = (float)y;
                positions[cnt_p * 3 + 2] = (float)z;
            }
            cnt_p++;
        } else if (p[0] == 'v' && p[1] == 'n' && p[2] == ' ') {
            /* Vertex normal: vn x y z */
            p += 3;
            if (cnt_n >= cap_n) {
                if (!obj_grow_float_array(&normals, &cap_n, 3)) {
                    parse_failed = 1;
                    break;
                }
            }
            {
                double x, y, z;
                if (!obj_parse_double(&p, &x) || !obj_parse_double(&p, &y) ||
                    !obj_parse_double(&p, &z)) {
                    parse_failed = 1;
                    break;
                }
                normals[cnt_n * 3 + 0] = (float)x;
                normals[cnt_n * 3 + 1] = (float)y;
                normals[cnt_n * 3 + 2] = (float)z;
            }
            cnt_n++;
        } else if (p[0] == 'v' && p[1] == 't' && p[2] == ' ') {
            /* Texture coordinate: vt u v */
            p += 3;
            if (cnt_t >= cap_t) {
                if (!obj_grow_float_array(&texcoords, &cap_t, 2)) {
                    parse_failed = 1;
                    break;
                }
            }
            {
                double u, v;
                if (!obj_parse_double(&p, &u) || !obj_parse_double(&p, &v)) {
                    parse_failed = 1;
                    break;
                }
                texcoords[cnt_t * 2 + 0] = (float)u;
                texcoords[cnt_t * 2 + 1] = (float)v;
            }
            cnt_t++;
        } else if (p[0] == 'f' && p[1] == ' ') {
            /* Face: f v1[/vt1[/vn1]] v2[/vt2[/vn2]] ... */
            p += 2;
            size_t face_capacity = 8;
            int64_t *face_vi = (int64_t *)malloc(face_capacity * sizeof(int64_t));
            int64_t *face_ti = (int64_t *)malloc(face_capacity * sizeof(int64_t));
            int64_t *face_ni = (int64_t *)malloc(face_capacity * sizeof(int64_t));
            uint32_t *mesh_indices = NULL;
            int face_count = 0;
            if (!face_vi || !face_ti || !face_ni) {
                free(face_vi);
                free(face_ti);
                free(face_ni);
                parse_failed = 1;
                break;
            }

            while (*p && *p != '\n' && *p != '\r') {
                while (*p == ' ' || *p == '\t')
                    p++;
                if (!*p || *p == '\n' || *p == '\r' || *p == '#')
                    break;

                if ((size_t)face_count >= face_capacity) {
                    if (face_capacity > SIZE_MAX / 2u ||
                        face_capacity * 2u > SIZE_MAX / sizeof(int64_t)) {
                        parse_failed = 1;
                        break;
                    }
                    size_t new_capacity = face_capacity * 2u;
                    int64_t *new_vi = (int64_t *)malloc(new_capacity * sizeof(int64_t));
                    int64_t *new_ti = (int64_t *)malloc(new_capacity * sizeof(int64_t));
                    int64_t *new_ni = (int64_t *)malloc(new_capacity * sizeof(int64_t));
                    if (!new_vi || !new_ti || !new_ni) {
                        free(new_vi);
                        free(new_ti);
                        free(new_ni);
                        parse_failed = 1;
                        break;
                    }
                    memcpy(new_vi, face_vi, (size_t)face_count * sizeof(int64_t));
                    memcpy(new_ti, face_ti, (size_t)face_count * sizeof(int64_t));
                    memcpy(new_ni, face_ni, (size_t)face_count * sizeof(int64_t));
                    free(face_vi);
                    free(face_ti);
                    free(face_ni);
                    face_vi = new_vi;
                    face_ti = new_ti;
                    face_ni = new_ni;
                    face_capacity = new_capacity;
                }

                const char *before = p;
                if (!obj_parse_face_vert(
                        &p, &face_vi[face_count], &face_ti[face_count], &face_ni[face_count]) ||
                    p == before || face_vi[face_count] == 0) {
                    parse_failed = 1;
                    break;
                }
                face_count++;
            }

            if (parse_failed) {
                free(face_vi);
                free(face_ti);
                free(face_ni);
                break;
            }

            if (face_count < 3) {
                free(face_vi);
                free(face_ti);
                free(face_ni);
                continue;
            }

            mesh_indices = (uint32_t *)malloc((size_t)face_count * sizeof(uint32_t));
            if (!mesh_indices) {
                free(face_vi);
                free(face_ti);
                free(face_ni);
                parse_failed = 1;
                break;
            }

            /* Resolve indices and emit or reuse vertices */
            for (int fi = 0; fi < face_count; fi++) {
                int64_t vi = face_vi[fi];
                int64_t ti = face_ti[fi];
                int64_t ni = face_ni[fi];
                if (!obj_resolve_index(vi, cnt_p, 0, &vi) ||
                    !obj_resolve_index(ti, cnt_t, 1, &ti) ||
                    !obj_resolve_index(ni, cnt_n, 1, &ni) ||
                    !obj_get_or_add_mesh_vertex(mesh,
                                                &vertex_cache,
                                                positions,
                                                cnt_p,
                                                normals,
                                                cnt_n,
                                                texcoords,
                                                cnt_t,
                                                vi,
                                                ti,
                                                ni,
                                                &mesh_indices[fi])) {
                    parse_failed = 1;
                    break;
                }
            }

            /* Fan triangulation: (0,1,2), (0,2,3), (0,3,4), ... */
            if (!parse_failed) {
                for (int fi = 1; fi < face_count - 1; fi++) {
                    rt_mesh3d_add_triangle(
                        mesh, mesh_indices[0], mesh_indices[fi], mesh_indices[fi + 1]);
                    if (((rt_mesh3d *)mesh)->build_failed) {
                        parse_failed = 1;
                        break;
                    }
                }
            }
            free(face_vi);
            free(face_ti);
            free(face_ni);
            free(mesh_indices);
            if (parse_failed)
                break;
        } else if (strncmp(p, "mtllib ", 7) == 0 || strncmp(p, "usemtl ", 7) == 0 ||
                   strncmp(p, "g ", 2) == 0 || strncmp(p, "o ", 2) == 0) {
            continue;
        }
        /* Ignore: s and other non-geometry directives. */
    }

    fclose(f);
    free(positions);
    free(normals);
    free(texcoords);
    obj_vertex_cache_free(&vertex_cache);

    if (parse_failed || ((rt_mesh3d *)mesh)->build_failed) {
        if (rt_obj_release_check0(mesh))
            rt_obj_free(mesh);
        rt_trap("Mesh3D.FromOBJ: invalid or unsupported geometry");
        return NULL;
    }

    /* If no normals were in the file, auto-compute them */
    if (cnt_n == 0 && ((rt_mesh3d *)mesh)->vertex_count > 0)
        rt_mesh3d_recalc_normals(mesh);

    return mesh;
}

//=============================================================================
// STL Loading (Binary + ASCII)
//=============================================================================

// ---------------------------------------------------------------------------
// STL parser helpers — STL is a much simpler 3D mesh format with
// two variants: binary (84-byte header + per-triangle records) and
// ASCII (text "facet normal …" blocks). Both lack texture coords
// and vertex sharing — every triangle is stored in full.
// ---------------------------------------------------------------------------

/// @brief Read a little-endian uint32 from a binary STL byte stream.
static uint32_t stl_read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/// @brief Read a little-endian IEEE-754 float from a binary STL byte stream.
static float stl_read_f32_le(const uint8_t *p) {
    float val;
    memcpy(&val, p, sizeof(float));
    return val;
}

/// @brief Decode a binary-STL byte buffer into a Mesh3D.
///
/// Layout: 80-byte header + `uint32` triangle count + 50 bytes
/// per triangle (12-byte normal + 3 × 12-byte vertices + 2-byte
/// attribute count). Vertices are not deduplicated — STL emits
/// each face's vertices in full.
static void *stl_load_binary(const uint8_t *data, size_t len) {
    if (len < 84)
        return NULL;
    uint32_t tri_count = stl_read_u32_le(data + 80);
    size_t tri_count_size = (size_t)tri_count;
    if (tri_count_size > (SIZE_MAX - 84u) / 50u)
        return NULL;
    size_t expected = 84u + tri_count_size * 50u;
    if (len < expected || tri_count == 0)
        return NULL;

    void *mesh = rt_mesh3d_new();
    if (!mesh)
        return NULL;

    for (uint32_t i = 0; i < tri_count; i++) {
        const uint8_t *tri = data + 84 + (size_t)i * 50;
        // Skip face normal (12 bytes) — we compute vertex normals later
        float v1x = stl_read_f32_le(tri + 12), v1y = stl_read_f32_le(tri + 16),
              v1z = stl_read_f32_le(tri + 20);
        float v2x = stl_read_f32_le(tri + 24), v2y = stl_read_f32_le(tri + 28),
              v2z = stl_read_f32_le(tri + 32);
        float v3x = stl_read_f32_le(tri + 36), v3y = stl_read_f32_le(tri + 40),
              v3z = stl_read_f32_le(tri + 44);

        int64_t base = (int64_t)((rt_mesh3d *)mesh)->vertex_count;
        rt_mesh3d_add_vertex(mesh, v1x, v1y, v1z, 0, 0, 0, 0, 0);
        rt_mesh3d_add_vertex(mesh, v2x, v2y, v2z, 0, 0, 0, 0, 0);
        rt_mesh3d_add_vertex(mesh, v3x, v3y, v3z, 0, 0, 0, 0, 0);
        rt_mesh3d_add_triangle(mesh, base, base + 1, base + 2);
    }

    rt_mesh3d_recalc_normals(mesh);
    return mesh;
}

/// @brief Parse a double from an ASCII STL stream, advancing the cursor past it.
static double stl_parse_double(const char **pp) {
    while (**pp == ' ' || **pp == '\t')
        (*pp)++;
    char *end;
    double val = strtod(*pp, &end);
    *pp = end;
    return val;
}

/// @brief Decode an ASCII-STL byte buffer into a Mesh3D.
///
/// Walks the text, recognising `facet normal …` blocks each
/// containing three `vertex …` lines. Same no-deduplication
/// rule as the binary loader.
static void *stl_load_ascii(const uint8_t *data, size_t len) {
    void *mesh = rt_mesh3d_new();
    if (!mesh)
        return NULL;

    const char *p = (const char *)data;
    const char *end = p + len;
    float verts[9]; // 3 vertices × 3 components
    int vert_idx = 0;

    while (p < end) {
        // Skip whitespace
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
            p++;
        if (p >= end)
            break;

        if (strncmp(p, "vertex", 6) == 0 && (p[6] == ' ' || p[6] == '\t')) {
            const char *vp = p + 6;
            verts[vert_idx * 3 + 0] = (float)stl_parse_double(&vp);
            verts[vert_idx * 3 + 1] = (float)stl_parse_double(&vp);
            verts[vert_idx * 3 + 2] = (float)stl_parse_double(&vp);
            vert_idx++;

            if (vert_idx == 3) {
                int64_t base = (int64_t)((rt_mesh3d *)mesh)->vertex_count;
                rt_mesh3d_add_vertex(mesh, verts[0], verts[1], verts[2], 0, 0, 0, 0, 0);
                rt_mesh3d_add_vertex(mesh, verts[3], verts[4], verts[5], 0, 0, 0, 0, 0);
                rt_mesh3d_add_vertex(mesh, verts[6], verts[7], verts[8], 0, 0, 0, 0, 0);
                rt_mesh3d_add_triangle(mesh, base, base + 1, base + 2);
                vert_idx = 0;
            }
        }

        // Skip to next line
        while (p < end && *p != '\n')
            p++;
        if (p < end)
            p++;
    }

    if (((rt_mesh3d *)mesh)->vertex_count == 0) {
        // No triangles found — free and return NULL
        if (rt_obj_release_check0(mesh))
            rt_obj_free(mesh);
        return NULL;
    }

    rt_mesh3d_recalc_normals(mesh);
    return mesh;
}

/// @brief Load a mesh from a binary or ASCII STL file.
/// @details STL files contain raw triangle soup (no shared vertices or UVs).
///          Each triangle has its own face normal. ASCII and binary formats are
///          auto-detected. UV coordinates are set to (0,0) for all vertices.
/// @param path File path to the .stl file (runtime string).
/// @return Mesh handle, or NULL on load failure.
void *rt_mesh3d_from_stl(rt_string path) {
    if (!path)
        return NULL;
    const char *filepath = rt_string_cstr(path);
    if (!filepath)
        return NULL;

    FILE *f = fopen(filepath, "rb");
    if (!f)
        return NULL;

    fseek(f, 0, SEEK_END);
    long file_len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_len <= 0 || file_len > 512 * 1024 * 1024) {
        fclose(f);
        return NULL;
    }

    uint8_t *data = (uint8_t *)malloc((size_t)file_len);
    if (!data) {
        fclose(f);
        return NULL;
    }
    if (fread(data, 1, (size_t)file_len, f) != (size_t)file_len) {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);

    // Auto-detect: binary STL has predictable size based on triangle count at offset 80
    void *mesh = NULL;
    if ((size_t)file_len >= 84) {
        uint32_t tri_count = stl_read_u32_le(data + 80);
        size_t expected_binary = 84 + (size_t)tri_count * 50;
        if ((size_t)file_len == expected_binary && tri_count > 0) {
            mesh = stl_load_binary(data, (size_t)file_len);
        }
    }
    if (!mesh && file_len > 5 && memcmp(data, "solid", 5) == 0) {
        mesh = stl_load_ascii(data, (size_t)file_len);
    }
    if (!mesh) {
        // Final fallback: try binary anyway (some files have non-matching header)
        mesh = stl_load_binary(data, (size_t)file_len);
    }

    free(data);
    return mesh;
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
