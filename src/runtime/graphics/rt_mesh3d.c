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
//   - Vertices stored as vgfx3d_vertex_t (80 bytes, float internally)
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
#include "rt_string.h"

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
extern int rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);
extern void rt_trap(const char *msg);
extern const char *rt_string_cstr(rt_string s);

#define MESH_INIT_VERTS 64
#define MESH_INIT_IDXS 128

static void rt_mesh3d_finalize(void *obj) {
    rt_mesh3d *m = (rt_mesh3d *)obj;
    free(m->vertices);
    m->vertices = NULL;
    free(m->indices);
    m->indices = NULL;
}

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

void rt_mesh3d_clear(void *obj) {
    if (!obj)
        return;
    rt_mesh3d *m = (rt_mesh3d *)obj;
    m->vertex_count = 0;
    m->index_count = 0;
}

void rt_mesh3d_add_vertex(
    void *obj, double x, double y, double z, double nx, double ny, double nz, double u, double v) {
    if (!obj)
        return;
    rt_mesh3d *m = (rt_mesh3d *)obj;

    if (m->vertex_count >= m->vertex_capacity) {
        uint32_t new_cap = m->vertex_capacity * 2;
        vgfx3d_vertex_t *nv =
            (vgfx3d_vertex_t *)realloc(m->vertices, new_cap * sizeof(vgfx3d_vertex_t));
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
}

/// @brief Perform mesh3d add triangle operation.
/// @param obj
/// @param v0
/// @param v1
/// @param v2
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
        uint32_t new_cap = m->index_capacity * 2;
        uint32_t *ni = (uint32_t *)realloc(m->indices, new_cap * sizeof(uint32_t));
        if (!ni)
            return;
        m->indices = ni;
        m->index_capacity = new_cap;
    }

    m->indices[m->index_count++] = (uint32_t)v0;
    m->indices[m->index_count++] = (uint32_t)v1;
    m->indices[m->index_count++] = (uint32_t)v2;
}

/// @brief Perform mesh3d get vertex count operation.
/// @param obj
/// @return Result value.
int64_t rt_mesh3d_get_vertex_count(void *obj) {
    if (!obj)
        return 0;
    return (int64_t)((rt_mesh3d *)obj)->vertex_count;
}

/// @brief Perform mesh3d get triangle count operation.
/// @param obj
/// @return Result value.
int64_t rt_mesh3d_get_triangle_count(void *obj) {
    if (!obj)
        return 0;
    return (int64_t)(((rt_mesh3d *)obj)->index_count / 3);
}

/// @brief Perform mesh3d recalc normals operation.
/// @param obj
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
}

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
        dst->vertices = NULL;
        free(dst->indices);
        dst->indices = NULL;
        dst->vertex_count = 0;
        dst->index_count = 0;
        return dst;
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

    return dst;
}

/// @brief Perform mesh3d transform operation.
/// @param obj
/// @param mat4_obj
void rt_mesh3d_transform(void *obj, void *mat4_obj) {
    if (!obj || !mat4_obj)
        return;
    rt_mesh3d *m = (rt_mesh3d *)obj;
    mat4_impl *xform = (mat4_impl *)mat4_obj;

    for (uint32_t i = 0; i < m->vertex_count; i++) {
        float *p = m->vertices[i].pos;
        double x = p[0], y = p[1], z = p[2];
        p[0] = (float)(xform->m[0] * x + xform->m[1] * y + xform->m[2] * z + xform->m[3]);
        p[1] = (float)(xform->m[4] * x + xform->m[5] * y + xform->m[6] * z + xform->m[7]);
        p[2] = (float)(xform->m[8] * x + xform->m[9] * y + xform->m[10] * z + xform->m[11]);

        /* Transform normals (no translation, no inverse-transpose for now) */
        float *n = m->vertices[i].normal;
        double nx = n[0], ny = n[1], nz = n[2];
        n[0] = (float)(xform->m[0] * nx + xform->m[1] * ny + xform->m[2] * nz);
        n[1] = (float)(xform->m[4] * nx + xform->m[5] * ny + xform->m[6] * nz);
        n[2] = (float)(xform->m[8] * nx + xform->m[9] * ny + xform->m[10] * nz);
        float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len > 1e-8f) {
            n[0] /= len;
            n[1] /= len;
            n[2] /= len;
        }
    }
}

/* Procedural generators — NewBox, NewSphere, NewPlane, NewCylinder */
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
 * Does NOT support: .mtl materials, groups, smooth shading directives.
 *=========================================================================*/

/* Parse one integer from a face index string, advancing *p past it.
 * Returns 0 if no integer found (empty between slashes). */
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

static double obj_parse_double(const char **p) {
    while (**p == ' ' || **p == '\t')
        (*p)++;
    char *end;
    double val = strtod(*p, &end);
    *p = end;
    return val;
}

/// @brief Perform mesh3d calc tangents operation.
/// @param obj
void rt_mesh3d_calc_tangents(void *obj) {
    if (!obj)
        return;
    rt_mesh3d *m = (rt_mesh3d *)obj;
    if (m->vertex_count == 0 || m->index_count == 0)
        return;

    /* Zero all tangents */
    for (uint32_t i = 0; i < m->vertex_count; i++) {
        m->vertices[i].tangent[0] = 0.0f;
        m->vertices[i].tangent[1] = 0.0f;
        m->vertices[i].tangent[2] = 0.0f;
    }

    /* Accumulate tangents per triangle (Lengyel's method) */
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

        float tx = (edge1[0] * duv2[1] - edge2[0] * duv1[1]) * inv_det;
        float ty = (edge1[1] * duv2[1] - edge2[1] * duv1[1]) * inv_det;
        float tz = (edge1[2] * duv2[1] - edge2[2] * duv1[1]) * inv_det;

        m->vertices[i0].tangent[0] += tx;
        m->vertices[i0].tangent[1] += ty;
        m->vertices[i0].tangent[2] += tz;
        m->vertices[i1].tangent[0] += tx;
        m->vertices[i1].tangent[1] += ty;
        m->vertices[i1].tangent[2] += tz;
        m->vertices[i2].tangent[0] += tx;
        m->vertices[i2].tangent[1] += ty;
        m->vertices[i2].tangent[2] += tz;
    }

    /* Normalize and Gram-Schmidt orthogonalize against normal */
    for (uint32_t i = 0; i < m->vertex_count; i++) {
        float *t = m->vertices[i].tangent;
        float *n = m->vertices[i].normal;

        /* Gram-Schmidt: T = T - N * dot(N, T) */
        float dot = n[0] * t[0] + n[1] * t[1] + n[2] * t[2];
        t[0] -= n[0] * dot;
        t[1] -= n[1] * dot;
        t[2] -= n[2] * dot;

        /* Normalize */
        float len = sqrtf(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
        if (len > 1e-8f) {
            t[0] /= len;
            t[1] /= len;
            t[2] /= len;
        } else {
            /* Default tangent for degenerate UVs */
            t[0] = 1.0f;
            t[1] = 0.0f;
            t[2] = 0.0f;
        }
    }
}

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
    float *positions = (float *)malloc((size_t)cap_p * 3 * sizeof(float));
    float *normals = (float *)malloc((size_t)cap_n * 3 * sizeof(float));
    float *texcoords = (float *)malloc((size_t)cap_t * 2 * sizeof(float));

    void *mesh = rt_mesh3d_new();
    if (!mesh || !positions || !normals || !texcoords) {
        fclose(f);
        free(positions);
        free(normals);
        free(texcoords);
        return mesh;
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
                if (!tmp)
                    break;
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
                if (!tmp)
                    break;
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
                if (!tmp)
                    break;
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
        }
        /* Ignore: mtllib, usemtl, s, g, o, etc. */
    }

    fclose(f);
    free(positions);
    free(normals);
    free(texcoords);

    /* If no normals were in the file, auto-compute them */
    if (cnt_n == 0 && ((rt_mesh3d *)mesh)->vertex_count > 0)
        rt_mesh3d_recalc_normals(mesh);

    return mesh;
}

#endif /* VIPER_ENABLE_GRAPHICS */
