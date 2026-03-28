//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_navmesh3d.c
// Purpose: 3D navigation mesh with slope-filtered walkable triangles, adjacency
//   graph, A* pathfinding, and position snapping.
//
// Key invariants:
//   - Phase 1: copy vertices from source Mesh3D.
//   - Phase 2: filter triangles by face normal (walkable = normal.y > cos(slope)).
//   - Phase 3: build adjacency (O(n²) — triangles sharing 2 verts are neighbors).
//   - A*: binary min-heap on f = g + h, centroid-to-centroid edge cost.
//   - FindPath returns a Path3D with waypoints through centroids.
//
// Links: rt_navmesh3d.h, rt_path3d.h, rt_canvas3d.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_navmesh3d.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_path3d.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_trap(const char *msg);
extern void *rt_vec3_new(double x, double y, double z);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);
extern void *rt_path3d_new(void);
extern void rt_path3d_add_point(void *path, void *pos);
extern void rt_canvas3d_draw_line3d(void *obj, void *from, void *to, int64_t color);

typedef struct {
    float position[3];
} nav_vertex_t;

typedef struct {
    int32_t v[3];
    int32_t neighbors[3]; /* adjacent tri per edge, -1 = boundary */
    float centroid[3];
    float normal[3];
} nav_triangle_t;

typedef struct {
    void *vptr;
    nav_vertex_t *vertices;
    int32_t vertex_count;
    nav_triangle_t *triangles;
    int32_t triangle_count;
    double agent_radius;
    double agent_height;
    double max_slope; /* degrees */
} rt_navmesh3d;

static void navmesh3d_finalizer(void *obj) {
    rt_navmesh3d *nm = (rt_navmesh3d *)obj;
    free(nm->vertices);
    free(nm->triangles);
    nm->vertices = NULL;
    nm->triangles = NULL;
}

void *rt_navmesh3d_build(void *mesh_obj, double agent_radius, double agent_height) {
    if (!mesh_obj)
        return NULL;
    rt_mesh3d *m = (rt_mesh3d *)mesh_obj;
    if (m->vertex_count == 0 || m->index_count < 3)
        return NULL;

    rt_navmesh3d *nm = (rt_navmesh3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_navmesh3d));
    if (!nm) {
        rt_trap("NavMesh3D.Build: allocation failed");
        return NULL;
    }
    nm->vptr = NULL;
    nm->agent_radius = agent_radius;
    nm->agent_height = agent_height;
    nm->max_slope = 45.0;
    rt_obj_set_finalizer(nm, navmesh3d_finalizer);

    double max_slope_cos = cos(nm->max_slope * M_PI / 180.0);

    /* Phase 1: Copy vertices */
    nm->vertex_count = (int32_t)m->vertex_count;
    nm->vertices = (nav_vertex_t *)malloc((size_t)nm->vertex_count * sizeof(nav_vertex_t));
    if (!nm->vertices) {
        rt_trap("NavMesh3D.Build: vertex allocation failed");
        return NULL;
    }
    for (uint32_t i = 0; i < m->vertex_count; i++) {
        nm->vertices[i].position[0] = m->vertices[i].pos[0];
        nm->vertices[i].position[1] = m->vertices[i].pos[1];
        nm->vertices[i].position[2] = m->vertices[i].pos[2];
    }

    /* Phase 2: Filter triangles by slope */
    int32_t tri_cap = (int32_t)(m->index_count / 3);
    nm->triangles = (nav_triangle_t *)malloc((size_t)tri_cap * sizeof(nav_triangle_t));
    if (!nm->triangles) {
        rt_trap("NavMesh3D.Build: triangle allocation failed");
        return NULL;
    }
    nm->triangle_count = 0;

    for (uint32_t i = 0; i + 2 < m->index_count; i += 3) {
        uint32_t i0 = m->indices[i], i1 = m->indices[i + 1], i2 = m->indices[i + 2];
        if (i0 >= m->vertex_count || i1 >= m->vertex_count || i2 >= m->vertex_count)
            continue;

        float *p0 = nm->vertices[i0].position;
        float *p1 = nm->vertices[i1].position;
        float *p2 = nm->vertices[i2].position;

        /* Face normal via cross product */
        float e1x = p1[0] - p0[0], e1y = p1[1] - p0[1], e1z = p1[2] - p0[2];
        float e2x = p2[0] - p0[0], e2y = p2[1] - p0[1], e2z = p2[2] - p0[2];
        float nx = e1y * e2z - e1z * e2y;
        float ny = e1z * e2x - e1x * e2z;
        float nz = e1x * e2y - e1y * e2x;
        float nlen = sqrtf(nx * nx + ny * ny + nz * nz);
        if (nlen < 1e-8f)
            continue;
        nx /= nlen;
        ny /= nlen;
        nz /= nlen;

        /* Walkable if |normal.y| > cos(max_slope) — either side facing up */
        if (fabsf(ny) < (float)max_slope_cos)
            continue;
        /* Ensure normal points upward for consistent orientation */
        if (ny < 0) {
            nx = -nx;
            ny = -ny;
            nz = -nz;
        }

        nav_triangle_t *tri = &nm->triangles[nm->triangle_count++];
        tri->v[0] = (int32_t)i0;
        tri->v[1] = (int32_t)i1;
        tri->v[2] = (int32_t)i2;
        tri->normal[0] = nx;
        tri->normal[1] = ny;
        tri->normal[2] = nz;
        tri->centroid[0] = (p0[0] + p1[0] + p2[0]) / 3.0f;
        tri->centroid[1] = (p0[1] + p1[1] + p2[1]) / 3.0f;
        tri->centroid[2] = (p0[2] + p1[2] + p2[2]) / 3.0f;
        tri->neighbors[0] = tri->neighbors[1] = tri->neighbors[2] = -1;
    }

    /* Phase 3: Build adjacency via edge hash map (O(n) instead of O(n²)).
     * For each triangle, hash its 3 edges. If an edge is already in the table,
     * the two triangles sharing that edge are adjacent. */
    {
        int32_t tc = nm->triangle_count;
        int32_t map_cap = tc * 4; /* load factor ~0.75 for 3 edges per tri */
        if (map_cap < 16)
            map_cap = 16;

        /* Edge hash table: key = packed edge (min_v * MAX + max_v), val = tri index */
        typedef struct {
            int64_t key;
            int32_t tri_idx;
            int32_t edge_idx; /* which edge of the triangle (0,1,2) */
            int8_t used;
        } edge_entry_t;

        edge_entry_t *emap = (edge_entry_t *)calloc((size_t)map_cap, sizeof(edge_entry_t));
        if (!emap)
            goto skip_adjacency; /* degrade gracefully — no adjacency */

        for (int32_t i = 0; i < tc; i++) {
            for (int e = 0; e < 3; e++) {
                int32_t va = nm->triangles[i].v[e];
                int32_t vb = nm->triangles[i].v[(e + 1) % 3];
                int32_t lo = va < vb ? va : vb;
                int32_t hi = va < vb ? vb : va;
                int64_t key = (int64_t)lo * 1000000LL + hi;

                /* Open-addressing linear probe */
                uint32_t slot = (uint32_t)(key & 0x7FFFFFFF) % (uint32_t)map_cap;
                for (int probe = 0; probe < map_cap; probe++) {
                    uint32_t idx = (slot + (uint32_t)probe) % (uint32_t)map_cap;
                    if (!emap[idx].used) {
                        /* Empty slot — insert edge */
                        emap[idx].key = key;
                        emap[idx].tri_idx = i;
                        emap[idx].edge_idx = e;
                        emap[idx].used = 1;
                        break;
                    }
                    if (emap[idx].key == key) {
                        /* Matching edge — triangles are adjacent */
                        int32_t j = emap[idx].tri_idx;
                        nm->triangles[i].neighbors[e] = j;
                        nm->triangles[j].neighbors[emap[idx].edge_idx] = i;
                        break;
                    }
                }
            }
        }
        free(emap);
    }
    skip_adjacency:

    return nm;
}

/// @brief Point-in-triangle test on XZ plane (2D barycentric).
static int point_in_tri_xz(float px, float pz, const float *v0, const float *v1, const float *v2) {
    float d1x = v1[0] - v0[0], d1z = v1[2] - v0[2];
    float d2x = v2[0] - v0[0], d2z = v2[2] - v0[2];
    float dpx = px - v0[0], dpz = pz - v0[2];

    float det = d1x * d2z - d2x * d1z;
    if (fabsf(det) < 1e-8f)
        return 0;
    float inv = 1.0f / det;
    float u = (dpx * d2z - d2x * dpz) * inv;
    float v = (d1x * dpz - dpx * d1z) * inv;
    return (u >= 0.0f && v >= 0.0f && (u + v) <= 1.0f) ? 1 : 0;
}

/// @brief Find triangle containing point (projected onto XZ).
static int32_t find_tri(const rt_navmesh3d *nm, float px, float py, float pz) {
    float best_dy = FLT_MAX;
    int32_t best = -1;
    for (int32_t i = 0; i < nm->triangle_count; i++) {
        const nav_triangle_t *t = &nm->triangles[i];
        const float *v0 = nm->vertices[t->v[0]].position;
        const float *v1 = nm->vertices[t->v[1]].position;
        const float *v2 = nm->vertices[t->v[2]].position;
        if (point_in_tri_xz(px, pz, v0, v1, v2)) {
            float dy = fabsf(py - t->centroid[1]);
            if (dy < best_dy) {
                best_dy = dy;
                best = i;
            }
        }
    }
    return best;
}

/*==========================================================================
 * A* pathfinding
 *=========================================================================*/

typedef struct {
    int32_t tri;
    float f;
} heap_entry_t;

static void heap_push(heap_entry_t *heap, int32_t *size, int32_t capacity, int32_t tri, float f) {
    if (*size >= capacity)
        return;
    int32_t i = (*size)++;
    heap[i].tri = tri;
    heap[i].f = f;
    /* Sift up */
    while (i > 0) {
        int32_t p = (i - 1) / 2;
        if (heap[p].f <= heap[i].f)
            break;
        heap_entry_t tmp = heap[p];
        heap[p] = heap[i];
        heap[i] = tmp;
        i = p;
    }
}

static int32_t heap_pop(heap_entry_t *heap, int32_t *size) {
    int32_t result = heap[0].tri;
    heap[0] = heap[--(*size)];
    /* Sift down */
    int32_t i = 0;
    for (;;) {
        int32_t best = i, l = 2 * i + 1, r = 2 * i + 2;
        if (l < *size && heap[l].f < heap[best].f)
            best = l;
        if (r < *size && heap[r].f < heap[best].f)
            best = r;
        if (best == i)
            break;
        heap_entry_t tmp = heap[i];
        heap[i] = heap[best];
        heap[best] = tmp;
        i = best;
    }
    return result;
}

static float centroid_dist(const rt_navmesh3d *nm, int32_t a, int32_t b) {
    const float *ca = nm->triangles[a].centroid;
    const float *cb = nm->triangles[b].centroid;
    float dx = cb[0] - ca[0], dy = cb[1] - ca[1], dz = cb[2] - ca[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

void *rt_navmesh3d_find_path(void *obj, void *from_v, void *to_v) {
    if (!obj || !from_v || !to_v)
        return NULL;
    rt_navmesh3d *nm = (rt_navmesh3d *)obj;
    if (nm->triangle_count == 0)
        return NULL;

    float fx = (float)rt_vec3_x(from_v), fy = (float)rt_vec3_y(from_v),
          fz = (float)rt_vec3_z(from_v);
    float tx = (float)rt_vec3_x(to_v), ty = (float)rt_vec3_y(to_v), tz = (float)rt_vec3_z(to_v);

    int32_t start = find_tri(nm, fx, fy, fz);
    int32_t goal = find_tri(nm, tx, ty, tz);
    if (start < 0 || goal < 0)
        return NULL;

    if (start == goal) {
        void *path = rt_path3d_new();
        rt_path3d_add_point(path, from_v);
        rt_path3d_add_point(path, to_v);
        return path;
    }

    /* A* */
    int32_t tc = nm->triangle_count;
    float *g_cost = (float *)calloc((size_t)tc, sizeof(float));
    int32_t *parent = (int32_t *)malloc((size_t)tc * sizeof(int32_t));
    int8_t *closed = (int8_t *)calloc((size_t)tc, sizeof(int8_t));
    memset(parent, -1, (size_t)tc * sizeof(int32_t));
    for (int32_t i = 0; i < tc; i++)
        g_cost[i] = FLT_MAX;

    int32_t heap_cap = tc * 3;
    heap_entry_t *heap = (heap_entry_t *)malloc((size_t)heap_cap * sizeof(heap_entry_t));
    int32_t heap_size = 0;

    g_cost[start] = 0;
    heap_push(heap, &heap_size, heap_cap, start, centroid_dist(nm, start, goal));

    int found = 0;
    while (heap_size > 0) {
        int32_t cur = heap_pop(heap, &heap_size);
        if (cur == goal) {
            found = 1;
            break;
        }
        if (closed[cur])
            continue;
        closed[cur] = 1;

        for (int e = 0; e < 3; e++) {
            int32_t next = nm->triangles[cur].neighbors[e];
            if (next < 0 || closed[next])
                continue;

            float new_g = g_cost[cur] + centroid_dist(nm, cur, next);
            if (new_g < g_cost[next]) {
                g_cost[next] = new_g;
                parent[next] = cur;
                heap_push(heap, &heap_size, heap_cap, next, new_g + centroid_dist(nm, next, goal));
            }
        }
    }

    void *path = NULL;
    if (found) {
        /* Reconstruct: collect centroids from goal back to start */
        int32_t count = 0;
        for (int32_t c = goal; c != -1; c = parent[c])
            count++;

        int32_t *seq = (int32_t *)malloc((size_t)count * sizeof(int32_t));
        int32_t idx = count - 1;
        for (int32_t c = goal; c != -1; c = parent[c])
            seq[idx--] = c;

        path = rt_path3d_new();
        rt_path3d_add_point(path, from_v);
        for (int32_t i = 0; i < count; i++) {
            float *cen = nm->triangles[seq[i]].centroid;
            rt_path3d_add_point(path, rt_vec3_new(cen[0], cen[1], cen[2]));
        }
        rt_path3d_add_point(path, to_v);
        free(seq);
    }

    free(g_cost);
    free(parent);
    free(closed);
    free(heap);
    return path;
}

void *rt_navmesh3d_sample_position(void *obj, void *point) {
    if (!obj || !point)
        return rt_vec3_new(0, 0, 0);
    rt_navmesh3d *nm = (rt_navmesh3d *)obj;
    float px = (float)rt_vec3_x(point), py = (float)rt_vec3_y(point), pz = (float)rt_vec3_z(point);

    int32_t tri = find_tri(nm, px, py, pz);
    if (tri >= 0) {
        /* Snap Y to triangle centroid height */
        return rt_vec3_new(px, nm->triangles[tri].centroid[1], pz);
    }

    /* Find nearest centroid */
    float best_d = FLT_MAX;
    int32_t best = 0;
    for (int32_t i = 0; i < nm->triangle_count; i++) {
        float *c = nm->triangles[i].centroid;
        float dx = px - c[0], dz = pz - c[2];
        float d = dx * dx + dz * dz;
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    float *c = nm->triangles[best].centroid;
    return rt_vec3_new(c[0], c[1], c[2]);
}

int8_t rt_navmesh3d_is_walkable(void *obj, void *point) {
    if (!obj || !point)
        return 0;
    rt_navmesh3d *nm = (rt_navmesh3d *)obj;
    float px = (float)rt_vec3_x(point), py = (float)rt_vec3_y(point), pz = (float)rt_vec3_z(point);
    return find_tri(nm, px, py, pz) >= 0 ? 1 : 0;
}

int64_t rt_navmesh3d_get_triangle_count(void *obj) {
    return obj ? ((rt_navmesh3d *)obj)->triangle_count : 0;
}

void rt_navmesh3d_set_max_slope(void *obj, double degrees) {
    if (obj)
        ((rt_navmesh3d *)obj)->max_slope = degrees;
}

void rt_navmesh3d_debug_draw(void *obj, void *canvas) {
    if (!obj || !canvas)
        return;
    rt_navmesh3d *nm = (rt_navmesh3d *)obj;
    int64_t color = 0x00FF44; /* green */

    for (int32_t i = 0; i < nm->triangle_count; i++) {
        nav_triangle_t *tri = &nm->triangles[i];
        float *v0 = nm->vertices[tri->v[0]].position;
        float *v1 = nm->vertices[tri->v[1]].position;
        float *v2 = nm->vertices[tri->v[2]].position;
        /* Offset slightly above surface to avoid z-fighting */
        double off = 0.02;
        void *a = rt_vec3_new(v0[0], v0[1] + off, v0[2]);
        void *b = rt_vec3_new(v1[0], v1[1] + off, v1[2]);
        void *c = rt_vec3_new(v2[0], v2[1] + off, v2[2]);
        rt_canvas3d_draw_line3d(canvas, a, b, color);
        rt_canvas3d_draw_line3d(canvas, b, c, color);
        rt_canvas3d_draw_line3d(canvas, c, a, color);
    }
}

#endif /* VIPER_ENABLE_GRAPHICS */
