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

#define MESH_INIT_VERTS 64
#define MESH_INIT_IDXS 128

static void mesh_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

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
    return m;
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

    if (m->vertex_count >= m->vertex_capacity) {
        uint32_t new_cap;
        if (m->vertex_capacity > UINT32_MAX / 2u)
            return;
        new_cap = m->vertex_capacity * 2u;
        vgfx3d_vertex_t *nv =
            (vgfx3d_vertex_t *)realloc(m->vertices, (size_t)new_cap * sizeof(vgfx3d_vertex_t));
        if (!nv)
            return;
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

    if (v0 < 0 || v1 < 0 || v2 < 0)
        return;
    if ((uint64_t)v0 >= m->vertex_count || (uint64_t)v1 >= m->vertex_count ||
        (uint64_t)v2 >= m->vertex_count)
        return;

    if (m->index_count + 3 > m->index_capacity) {
        uint32_t new_cap;
        if (m->index_capacity > UINT32_MAX / 2u)
            return;
        new_cap = m->index_capacity * 2u;
        uint32_t *ni = (uint32_t *)realloc(m->indices, (size_t)new_cap * sizeof(uint32_t));
        if (!ni)
            return;
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

    dst->vertex_capacity = src->vertex_count > 0 ? src->vertex_count : 1;
    dst->vertices = (vgfx3d_vertex_t *)malloc(dst->vertex_capacity * sizeof(vgfx3d_vertex_t));
    dst->index_capacity = src->index_count > 0 ? src->index_count : 1;
    dst->indices = (uint32_t *)malloc(dst->index_capacity * sizeof(uint32_t));

    if (!dst->vertices || !dst->indices) {
        free(dst->vertices);
        free(dst->indices);
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
    dst->bone_count = 0;
    dst->morph_deltas = NULL;
    dst->morph_weights = NULL;
    dst->morph_shape_count = 0;
    dst->morph_normal_deltas = NULL;
    dst->prev_morph_weights = NULL;
    dst->prev_bone_palette = NULL;
    mesh_assign_ref(&dst->morph_targets_ref, src->morph_targets_ref);
    dst->geometry_revision = src->geometry_revision;
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

/// @brief Apply a 4x4 transformation matrix to all vertex positions and shading bases in-place.
void rt_mesh3d_transform(void *obj, void *mat4_obj) {
    if (!obj || !mat4_obj)
        return;
    rt_mesh3d *m = (rt_mesh3d *)obj;
    mat4_impl *xform = (mat4_impl *)mat4_obj;
    float model_matrix[16];
    float normal_matrix[16];

    for (int i = 0; i < 16; i++)
        model_matrix[i] = (float)xform->m[i];
    vgfx3d_compute_normal_matrix4(model_matrix, normal_matrix);

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
        t[3] = handedness == 0.0f ? 1.0f : handedness;
    }
    rt_mesh3d_touch_geometry(m);
    rt_mesh3d_refresh_bounds(m);
}

/* Procedural generators — NewBox, NewSphere, NewPlane, NewCylinder */
/// @brief Generate a box mesh centered at the origin with the given half-extents.
void *rt_mesh3d_new_box(double sx, double sy, double sz) {
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

    return m;
}

/// @brief Generate a UV sphere mesh with the given radius and segment count.
void *rt_mesh3d_new_sphere(double radius, int64_t segments) {
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
            rt_mesh3d_add_triangle(m, a, b, a + 1);
            rt_mesh3d_add_triangle(m, a + 1, b, b + 1);
        }
    }

    return m;
}

/// @brief Generate a flat plane mesh on the XZ plane (facing +Y) with the given size.
void *rt_mesh3d_new_plane(double sx, double sz) {
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

    return m;
}

/// @brief Generate a cylinder mesh with circular caps, centered at the origin.
void *rt_mesh3d_new_cylinder(double radius, double height, int64_t segments) {
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

    return m;
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
static int64_t obj_parse_int(const char **p) {
    if (!**p || **p == '/' || **p == ' ' || **p == '\n' || **p == '\r')
        return 0;
    int64_t sign = 1;
    if (**p == '-') {
        sign = -1;
        (*p)++;
    }
    int64_t val = 0;
    while (**p >= '0' && **p <= '9') {
        val = val * 10 + (**p - '0');
        (*p)++;
    }
    return val * sign;
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
static void obj_parse_face_vert(const char **p, int64_t *vi, int64_t *ti, int64_t *ni) {
    *vi = obj_parse_int(p);
    *ti = 0;
    *ni = 0;
    if (**p == '/') {
        (*p)++;
        *ti = obj_parse_int(p);
        if (**p == '/') {
            (*p)++;
            *ni = obj_parse_int(p);
        }
    }
}

/// @brief Parse a double-precision float from `*p` via `strtod`, advancing `*p` past it.
static double obj_parse_double(const char **p) {
    while (**p == ' ' || **p == '\t')
        (*p)++;
    char *end;
    double val = strtod(*p, &end);
    *p = end;
    return val;
}

/// @brief Calculate per-vertex tangent vectors for normal mapping (Lengyel method).
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
            /* Default tangent for degenerate UVs */
            t[0] = 1.0f;
            t[1] = 0.0f;
            t[2] = 0.0f;
            t[3] = 1.0f;
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

    void *mesh = rt_mesh3d_new();
    if (!mesh || !positions || !normals || !texcoords) {
        fclose(f);
        free(positions);
        free(normals);
        free(texcoords);
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
                cap_p *= 2;
                float *tmp = (float *)realloc(positions, (size_t)cap_p * 3 * sizeof(float));
                if (!tmp) {
                    parse_failed = 1;
                    break;
                }
                positions = tmp;
            }
            positions[cnt_p * 3 + 0] = (float)obj_parse_double(&p);
            positions[cnt_p * 3 + 1] = (float)obj_parse_double(&p);
            positions[cnt_p * 3 + 2] = (float)obj_parse_double(&p);
            cnt_p++;
        } else if (p[0] == 'v' && p[1] == 'n' && p[2] == ' ') {
            /* Vertex normal: vn x y z */
            p += 3;
            if (cnt_n >= cap_n) {
                cap_n *= 2;
                float *tmp = (float *)realloc(normals, (size_t)cap_n * 3 * sizeof(float));
                if (!tmp) {
                    parse_failed = 1;
                    break;
                }
                normals = tmp;
            }
            normals[cnt_n * 3 + 0] = (float)obj_parse_double(&p);
            normals[cnt_n * 3 + 1] = (float)obj_parse_double(&p);
            normals[cnt_n * 3 + 2] = (float)obj_parse_double(&p);
            cnt_n++;
        } else if (p[0] == 'v' && p[1] == 't' && p[2] == ' ') {
            /* Texture coordinate: vt u v */
            p += 3;
            if (cnt_t >= cap_t) {
                cap_t *= 2;
                float *tmp = (float *)realloc(texcoords, (size_t)cap_t * 2 * sizeof(float));
                if (!tmp) {
                    parse_failed = 1;
                    break;
                }
                texcoords = tmp;
            }
            texcoords[cnt_t * 2 + 0] = (float)obj_parse_double(&p);
            texcoords[cnt_t * 2 + 1] = (float)obj_parse_double(&p);
            cnt_t++;
        } else if (p[0] == 'f' && p[1] == ' ') {
            /* Face: f v1[/vt1[/vn1]] v2[/vt2[/vn2]] ... */
            p += 2;
            int64_t face_vi[16], face_ti[16], face_ni[16];
            int face_count = 0;

            while (*p && *p != '\n' && *p != '\r' && face_count < 16) {
                while (*p == ' ' || *p == '\t')
                    p++;
                if (!*p || *p == '\n' || *p == '\r')
                    break;

                obj_parse_face_vert(
                    &p, &face_vi[face_count], &face_ti[face_count], &face_ni[face_count]);
                face_count++;
            }

            if (face_count < 3)
                continue;

            /* Resolve indices and emit vertices */
            int64_t mesh_indices[16];
            for (int fi = 0; fi < face_count; fi++) {
                int64_t vi = face_vi[fi];
                int64_t ti = face_ti[fi];
                int64_t ni = face_ni[fi];

                /* Handle negative (relative) indices */
                if (vi < 0)
                    vi = cnt_p + vi + 1;
                if (ti < 0)
                    ti = cnt_t + ti + 1;
                if (ni < 0)
                    ni = cnt_n + ni + 1;

                /* OBJ indices are 1-based */
                float px = 0, py = 0, pz = 0;
                if (vi >= 1 && vi <= cnt_p) {
                    px = positions[(vi - 1) * 3 + 0];
                    py = positions[(vi - 1) * 3 + 1];
                    pz = positions[(vi - 1) * 3 + 2];
                }

                float nx = 0, ny = 0, nz = 0;
                if (ni >= 1 && ni <= cnt_n) {
                    nx = normals[(ni - 1) * 3 + 0];
                    ny = normals[(ni - 1) * 3 + 1];
                    nz = normals[(ni - 1) * 3 + 2];
                }

                float tu = 0, tv = 0;
                if (ti >= 1 && ti <= cnt_t) {
                    tu = texcoords[(ti - 1) * 2 + 0];
                    tv = texcoords[(ti - 1) * 2 + 1];
                }

                mesh_indices[fi] = (int64_t)((rt_mesh3d *)mesh)->vertex_count;
                rt_mesh3d_add_vertex(mesh, px, py, pz, nx, ny, nz, tu, tv);
            }

            /* Fan triangulation: (0,1,2), (0,2,3), (0,3,4), ... */
            for (int fi = 1; fi < face_count - 1; fi++) {
                rt_mesh3d_add_triangle(
                    mesh, mesh_indices[0], mesh_indices[fi], mesh_indices[fi + 1]);
            }
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

    if (parse_failed) {
        if (rt_obj_release_check0(mesh))
            rt_obj_free(mesh);
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
    size_t expected = 84 + (size_t)tri_count * 50;
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
