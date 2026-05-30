//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/nav/rt_navmesh3d.c
// Purpose: 3D navigation mesh with slope-filtered walkable triangles, adjacency
//   graph, A* pathfinding, and position snapping.
//
// Key invariants:
//   - Phase 1: copy vertices from source Mesh3D.
//   - Phase 2: filter triangles by face normal (walkable = normal.y > cos(slope)).
//   - Phase 3: build adjacency via edge-keyed hash table (shared edges
//     cross-link the two triangles that own them).
//   - A*: binary min-heap on f = g + h, centroid-to-centroid edge cost.
//   - String-pulling smooths waypoints through portal midpoints; falls back
//     to centroids when portal extraction fails.
//   - FindPath returns a Path3D with waypoints through portals.
//
// Ownership/Lifetime:
//   - NavMesh3D is GC-managed; finalizer frees the vertex and triangle arrays.
//   - The source mesh is borrowed during Build only — not retained.
//
// Links: rt_navmesh3d.h, rt_path3d.h, rt_canvas3d.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_navmesh3d.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_graphics3d_ids.h"
#include "rt_mat4.h"
#include "rt_path3d.h"
#include "rt_scene3d.h"
#include "rt_scene3d_internal.h"

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
extern int rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);
#include "rt_trap.h"
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
    float from[3];
    float to[3];
    int32_t from_tri;
    int32_t to_tri;
    int8_t bidirectional;
} nav_offmesh_link_t;

typedef struct {
    float min[3];
    float max[3];
} nav_obstacle_t;

typedef struct {
    void *vptr;
    nav_vertex_t *vertices;
    int32_t vertex_count;
    nav_triangle_t *source_triangles;
    int32_t source_triangle_count;
    nav_triangle_t *triangles;
    int32_t triangle_count;
    nav_offmesh_link_t *offmesh_links;
    int32_t offmesh_link_count;
    int32_t offmesh_link_capacity;
    nav_obstacle_t *obstacles;
    int32_t obstacle_count;
    int32_t obstacle_capacity;
    double agent_radius;
    double agent_height;
    double max_slope; /* degrees */
} rt_navmesh3d;

static void navmesh3d_refresh_offmesh_links(rt_navmesh3d *nm);

/// @brief GC finalizer for NavMesh3D. Frees the baked vertex and triangle arrays
/// (heap-allocated during `Build`) and nulls the slots so a stale handle would crash
/// loudly on use rather than reading freed memory. The source mesh is borrowed only
/// during the build call, not retained, so there's no reference to release here.
static void navmesh3d_finalizer(void *obj) {
    rt_navmesh3d *nm = (rt_navmesh3d *)obj;
    free(nm->vertices);
    free(nm->source_triangles);
    free(nm->triangles);
    free(nm->offmesh_links);
    free(nm->obstacles);
    nm->vertices = NULL;
    nm->source_triangles = NULL;
    nm->triangles = NULL;
    nm->offmesh_links = NULL;
    nm->obstacles = NULL;
}

/// @brief Release a partially-constructed navmesh on allocation failure, freeing if refcount hits
/// zero.
/// @details Called in error paths where the navmesh object was allocated but bake failed,
///          so the half-built object must be dropped without going through the normal finalizer.
static void navmesh3d_free_partial(rt_navmesh3d *nm) {
    if (nm && rt_obj_release_check0(nm))
        rt_obj_free(nm);
}

/// @brief Drop one reference and free if zero. Safe on NULL.
static void navmesh3d_release_local(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

/// @brief Validate a max-slope angle (in degrees), clamping to `[0, 89.9]` on bad input.
/// @details Non-finite inputs collapse to 45° (the documented default).
static double navmesh3d_sanitize_slope(double degrees) {
    if (!isfinite(degrees))
        degrees = 45.0;
    if (degrees < 0.0)
        degrees = 0.0;
    if (degrees > 89.9)
        degrees = 89.9;
    return degrees;
}

/// @brief Whether the shared edge is wide enough for the configured agent radius.
static int navmesh3d_portal_allows_agent(const rt_navmesh3d *nm, int32_t va, int32_t vb) {
    if (!nm || !nm->vertices || va < 0 || vb < 0 || va >= nm->vertex_count || vb >= nm->vertex_count)
        return 0;
    if (!isfinite(nm->agent_radius) || nm->agent_radius <= 0.0)
        return 1;
    const float *a = nm->vertices[va].position;
    const float *b = nm->vertices[vb].position;
    double dx = (double)b[0] - (double)a[0];
    double dz = (double)b[2] - (double)a[2];
    double width = sqrt(dx * dx + dz * dz);
    return width + 1e-5 >= nm->agent_radius * 2.0 ? 1 : 0;
}

/// @brief Whether a triangle overlaps any authored obstacle AABB.
/// @details This is a conservative coarse-carving baseline: triangles whose world AABB overlaps an
/// obstacle AABB are removed from the walkable set when the navmesh is refiltered.
static int navmesh3d_triangle_blocked_by_obstacle(const rt_navmesh3d *nm,
                                                  const nav_triangle_t *tri) {
    if (!nm || !tri || !nm->obstacles || nm->obstacle_count <= 0 || !nm->vertices)
        return 0;
    const float *p0 = nm->vertices[tri->v[0]].position;
    const float *p1 = nm->vertices[tri->v[1]].position;
    const float *p2 = nm->vertices[tri->v[2]].position;
    float tri_min[3] = {
        fminf(p0[0], fminf(p1[0], p2[0])),
        fminf(p0[1], fminf(p1[1], p2[1])),
        fminf(p0[2], fminf(p1[2], p2[2])),
    };
    float tri_max[3] = {
        fmaxf(p0[0], fmaxf(p1[0], p2[0])),
        fmaxf(p0[1], fmaxf(p1[1], p2[1])),
        fmaxf(p0[2], fmaxf(p1[2], p2[2])),
    };
    for (int32_t i = 0; i < nm->obstacle_count; i++) {
        const nav_obstacle_t *ob = &nm->obstacles[i];
        if (tri_max[0] < ob->min[0] || tri_min[0] > ob->max[0] || tri_max[1] < ob->min[1] ||
            tri_min[1] > ob->max[1] || tri_max[2] < ob->min[2] || tri_min[2] > ob->max[2])
            continue;
        return 1;
    }
    return 0;
}

/// @brief Compute triangle-edge adjacency, populating each triangle's `neighbors[3]` slots.
/// @details Hash table keyed on `(min_vertex << 32) | max_vertex` matches each shared edge to
///          the two triangles that own it; first hit records the triangle, second hit cross-
///          links them. Boundary edges remain with neighbor index -1.
static int navmesh3d_build_adjacency(rt_navmesh3d *nm) {
    int32_t tc;
    int32_t map_cap;
    if (!nm)
        return 0;
    tc = nm->triangle_count;
    for (int32_t i = 0; i < tc; i++)
        nm->triangles[i].neighbors[0] = nm->triangles[i].neighbors[1] =
            nm->triangles[i].neighbors[2] = -1;
    if (tc <= 0)
        return 1;
    if (tc > INT32_MAX / 4)
        return 0;
    map_cap = tc * 4;
    if (map_cap < 16)
        map_cap = 16;

    typedef struct {
        int64_t key;
        int32_t tri_idx;
        int32_t edge_idx;
        int8_t used;
        int8_t matched;
    } edge_entry_t;

    if ((size_t)map_cap > SIZE_MAX / sizeof(edge_entry_t))
        return 0;
    edge_entry_t *emap = (edge_entry_t *)calloc((size_t)map_cap, sizeof(edge_entry_t));
    if (!emap)
        return 0;

    for (int32_t i = 0; i < tc; i++) {
        for (int e = 0; e < 3; e++) {
            int32_t va = nm->triangles[i].v[e];
            int32_t vb = nm->triangles[i].v[(e + 1) % 3];
            int32_t lo = va < vb ? va : vb;
            int32_t hi = va < vb ? vb : va;
            int64_t key = ((int64_t)(uint32_t)lo << 32) | (uint32_t)hi;
            uint32_t slot = (uint32_t)(key & 0x7FFFFFFF) % (uint32_t)map_cap;
            for (int probe = 0; probe < map_cap; probe++) {
                uint32_t idx = (slot + (uint32_t)probe) % (uint32_t)map_cap;
                if (!emap[idx].used) {
                    emap[idx].key = key;
                    emap[idx].tri_idx = i;
                    emap[idx].edge_idx = e;
                    emap[idx].used = 1;
                    break;
                }
                if (emap[idx].key == key) {
                    int32_t j = emap[idx].tri_idx;
                    int32_t je = emap[idx].edge_idx;
                    if (emap[idx].matched) {
                        free(emap);
                        return 0;
                    }
                    emap[idx].matched = 1;
                    if (nm->triangles[i].neighbors[e] >= 0 || nm->triangles[j].neighbors[je] >= 0) {
                        free(emap);
                        return 0;
                    }
                    if (navmesh3d_portal_allows_agent(nm, lo, hi)) {
                        nm->triangles[i].neighbors[e] = j;
                        nm->triangles[j].neighbors[je] = i;
                    }
                    break;
                }
            }
        }
    }
    free(emap);
    return 1;
}

/// @brief Filter source triangles by max-slope angle, keeping only walkable surfaces.
/// @details Compares each triangle's surface-normal Y component against `cos(max_slope)`;
///          triangles with normals pitched too steeply are dropped. Rebuilds adjacency
///          afterwards via `navmesh3d_build_adjacency`.
static int navmesh3d_apply_slope_filter(rt_navmesh3d *nm) {
    if (!nm || !nm->source_triangles || !nm->triangles)
        return 0;
    double max_slope_cos = cos(nm->max_slope * M_PI / 180.0);
    nm->triangle_count = 0;
    for (int32_t i = 0; i < nm->source_triangle_count; i++) {
        nav_triangle_t tri = nm->source_triangles[i];
        if (tri.normal[1] < (float)max_slope_cos)
            continue;
        if (navmesh3d_triangle_blocked_by_obstacle(nm, &tri))
            continue;
        tri.neighbors[0] = tri.neighbors[1] = tri.neighbors[2] = -1;
        nm->triangles[nm->triangle_count++] = tri;
    }
    if (!navmesh3d_build_adjacency(nm)) {
        rt_trap("NavMesh3D: adjacency build failed (non-manifold edge or allocation failure)");
        return 0;
    }
    navmesh3d_refresh_offmesh_links(nm);
    return 1;
}

/// @brief Transform a point by a row-major Mat4.
static void navmesh3d_transform_point(const double *m, const float *p, double *out) {
    out[0] = m[0] * (double)p[0] + m[1] * (double)p[1] + m[2] * (double)p[2] + m[3];
    out[1] = m[4] * (double)p[0] + m[5] * (double)p[1] + m[6] * (double)p[2] + m[7];
    out[2] = m[8] * (double)p[0] + m[9] * (double)p[1] + m[10] * (double)p[2] + m[11];
}

/// @brief Transform and normalize a normal by the row-major matrix's linear basis.
static void navmesh3d_transform_normal(const double *m, const float *n, double *out) {
    double len;
    out[0] = m[0] * (double)n[0] + m[1] * (double)n[1] + m[2] * (double)n[2];
    out[1] = m[4] * (double)n[0] + m[5] * (double)n[1] + m[6] * (double)n[2];
    out[2] = m[8] * (double)n[0] + m[9] * (double)n[1] + m[10] * (double)n[2];
    len = sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
    if (!isfinite(len) || len <= 1e-12) {
        out[0] = 0.0;
        out[1] = 1.0;
        out[2] = 0.0;
        return;
    }
    out[0] /= len;
    out[1] /= len;
    out[2] /= len;
}

/// @brief Append one SceneNode3D mesh into a temporary world-space Mesh3D.
static int navmesh3d_append_scene_node_mesh(rt_mesh3d *dst, rt_scene_node3d *node) {
    rt_mesh3d *src;
    void *world_obj;
    double world[16];
    uint32_t base_vertex;
    if (!dst || !node || !node->mesh)
        return 1;
    src = (rt_mesh3d *)rt_g3d_checked_or_null(node->mesh, RT_G3D_MESH3D_CLASS_ID);
    if (!src || src->vertex_count == 0 || src->index_count < 3)
        return 1;
    if (dst->vertex_count > UINT32_MAX - src->vertex_count)
        return 0;
    world_obj = rt_scene_node3d_get_world_matrix(node);
    if (!world_obj)
        return 0;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            world[r * 4 + c] = rt_mat4_get(world_obj, r, c);
    navmesh3d_release_local(world_obj);

    base_vertex = dst->vertex_count;
    for (uint32_t i = 0; i < src->vertex_count; i++) {
        double pos[3];
        double normal[3];
        navmesh3d_transform_point(world, src->vertices[i].pos, pos);
        navmesh3d_transform_normal(world, src->vertices[i].normal, normal);
        rt_mesh3d_add_vertex(dst,
                             pos[0],
                             pos[1],
                             pos[2],
                             normal[0],
                             normal[1],
                             normal[2],
                             (double)src->vertices[i].uv[0],
                             (double)src->vertices[i].uv[1]);
        if (dst->build_failed)
            return 0;
    }
    for (uint32_t i = 0; i + 2 < src->index_count; i += 3) {
        uint32_t i0 = src->indices[i];
        uint32_t i1 = src->indices[i + 1];
        uint32_t i2 = src->indices[i + 2];
        if (i0 >= src->vertex_count || i1 >= src->vertex_count || i2 >= src->vertex_count)
            continue;
        rt_mesh3d_add_triangle(dst,
                               (int64_t)base_vertex + (int64_t)i0,
                               (int64_t)base_vertex + (int64_t)i1,
                               (int64_t)base_vertex + (int64_t)i2);
        if (dst->build_failed)
            return 0;
    }
    return 1;
}

/// @brief Gather all Mesh3D-bearing scene nodes into a temporary world-space Mesh3D.
static void *navmesh3d_build_scene_source_mesh(void *scene_obj) {
    rt_scene3d *scene = (rt_scene3d *)rt_g3d_checked_or_null(scene_obj, RT_G3D_SCENE3D_CLASS_ID);
    rt_mesh3d *mesh;
    rt_scene_node3d **stack = NULL;
    int32_t stack_count = 0;
    int32_t stack_capacity = 64;
    int32_t appended = 0;
    if (!scene || !scene->root)
        return NULL;
    mesh = (rt_mesh3d *)rt_mesh3d_new();
    if (!mesh)
        return NULL;
    stack = (rt_scene_node3d **)malloc((size_t)stack_capacity * sizeof(*stack));
    if (!stack) {
        navmesh3d_release_local(mesh);
        return NULL;
    }
    stack[stack_count++] = scene->root;
    while (stack_count > 0) {
        rt_scene_node3d *node = stack[--stack_count];
        if (!node)
            continue;
        if (node->mesh) {
            uint32_t before = mesh->index_count;
            if (!navmesh3d_append_scene_node_mesh(mesh, node)) {
                free(stack);
                navmesh3d_release_local(mesh);
                return NULL;
            }
            if (mesh->index_count > before)
                appended = 1;
        }
        for (int32_t i = 0; i < node->child_count; i++) {
            if (stack_count >= stack_capacity) {
                int32_t next_capacity;
                rt_scene_node3d **grown;
                if (stack_capacity > INT32_MAX / 2) {
                    free(stack);
                    navmesh3d_release_local(mesh);
                    return NULL;
                }
                next_capacity = stack_capacity * 2;
                grown =
                    (rt_scene_node3d **)realloc(stack, (size_t)next_capacity * sizeof(*stack));
                if (!grown) {
                    free(stack);
                    navmesh3d_release_local(mesh);
                    return NULL;
                }
                stack = grown;
                stack_capacity = next_capacity;
            }
            stack[stack_count++] = node->children[i];
        }
    }
    free(stack);
    if (!appended || mesh->vertex_count == 0 || mesh->index_count < 3) {
        navmesh3d_release_local(mesh);
        return NULL;
    }
    return mesh;
}

/// @brief Bake a navigation mesh from a triangle mesh. Filters out triangles whose normal
/// exceeds the default 45° slope (re-configurable via `_set_max_slope`). Stores agent radius/
/// height for later edge-pull-in safety. Returns the navmesh handle, or NULL on alloc failure
/// / degenerate input mesh.
void *rt_navmesh3d_build(void *mesh_obj, double agent_radius, double agent_height) {
    if (!mesh_obj)
        return NULL;
    rt_mesh3d *m = (rt_mesh3d *)rt_g3d_checked_or_null(mesh_obj, RT_G3D_MESH3D_CLASS_ID);
    if (!m)
        return NULL;
    if (m->vertex_count == 0 || m->index_count < 3)
        return NULL;
    if (m->vertex_count > INT32_MAX || m->index_count > INT32_MAX ||
        (m->index_count / 3u) > INT32_MAX) {
        rt_trap("NavMesh3D.Build: mesh is too large");
        return NULL;
    }

    rt_navmesh3d *nm =
        (rt_navmesh3d *)rt_obj_new_i64(RT_G3D_NAVMESH3D_CLASS_ID, (int64_t)sizeof(rt_navmesh3d));
    if (!nm) {
        rt_trap("NavMesh3D.Build: allocation failed");
        return NULL;
    }
    memset(nm, 0, sizeof(*nm));
    nm->vptr = NULL;
    nm->agent_radius = (isfinite(agent_radius) && agent_radius > 0.0) ? agent_radius : 0.4;
    nm->agent_height = (isfinite(agent_height) && agent_height > 0.0) ? agent_height : 1.8;
    nm->max_slope = navmesh3d_sanitize_slope(45.0);
    rt_obj_set_finalizer(nm, navmesh3d_finalizer);

    /* Phase 1: Copy vertices */
    nm->vertex_count = (int32_t)m->vertex_count;
    nm->vertices = (nav_vertex_t *)malloc((size_t)nm->vertex_count * sizeof(nav_vertex_t));
    if (!nm->vertices) {
        navmesh3d_free_partial(nm);
        rt_trap("NavMesh3D.Build: vertex allocation failed");
        return NULL;
    }
    for (uint32_t i = 0; i < m->vertex_count; i++) {
        nm->vertices[i].position[0] = m->vertices[i].pos[0];
        nm->vertices[i].position[1] = m->vertices[i].pos[1];
        nm->vertices[i].position[2] = m->vertices[i].pos[2];
    }

    /* Phase 2: Copy non-degenerate source triangles; walkable filtering is
     * applied from this preserved set so SetMaxSlope can rebuild in-place. */
    int32_t tri_cap = (int32_t)(m->index_count / 3);
    nm->source_triangles = (nav_triangle_t *)malloc((size_t)tri_cap * sizeof(nav_triangle_t));
    nm->triangles = (nav_triangle_t *)malloc((size_t)tri_cap * sizeof(nav_triangle_t));
    if (!nm->source_triangles || !nm->triangles) {
        navmesh3d_free_partial(nm);
        rt_trap("NavMesh3D.Build: triangle allocation failed");
        return NULL;
    }
    nm->source_triangle_count = 0;
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
        float authored_ny =
            (m->vertices[i0].normal[1] + m->vertices[i1].normal[1] + m->vertices[i2].normal[1]) /
            3.0f;
        if (ny < 0.0f && authored_ny > 0.25f) {
            nx = -nx;
            ny = -ny;
            nz = -nz;
        }

        nav_triangle_t *tri = &nm->source_triangles[nm->source_triangle_count++];
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

    if (!navmesh3d_apply_slope_filter(nm)) {
        navmesh3d_free_partial(nm);
        return NULL;
    }

    return nm;
}

/// @brief Bake a navmesh from all Mesh3D-bearing nodes in a Scene3D.
/// @details This baseline flattens scene node meshes through their world transforms into a
/// temporary Mesh3D, then routes through the existing triangle Build path. @p cell_size is
/// accepted for API compatibility with the planned voxel/tiled baker but is not used yet.
void *rt_navmesh3d_bake(void *scene,
                        double agent_radius,
                        double agent_height,
                        double max_slope,
                        double cell_size) {
    (void)cell_size;
    void *source_mesh = navmesh3d_build_scene_source_mesh(scene);
    rt_navmesh3d *nm;
    if (!source_mesh)
        return NULL;
    nm = (rt_navmesh3d *)rt_navmesh3d_build(source_mesh, agent_radius, agent_height);
    navmesh3d_release_local(source_mesh);
    if (!nm)
        return NULL;
    nm->max_slope = navmesh3d_sanitize_slope(max_slope);
    if (!navmesh3d_apply_slope_filter(nm)) {
        navmesh3d_free_partial(nm);
        return NULL;
    }
    return nm;
}

/// @brief Bake through the tiled API shape.
/// @details Tile ownership is not materialized yet; this returns a full-scene navmesh so callers
/// can use the final API while `RebuildTile` remains a whole-mesh refilter baseline.
void *rt_navmesh3d_bake_tiled(void *scene,
                              double tile_size,
                              double agent_radius,
                              double agent_height,
                              double max_slope,
                              double cell_size) {
    (void)tile_size;
    return rt_navmesh3d_bake(scene, agent_radius, agent_height, max_slope, cell_size);
}

/// @brief Point-in-triangle test on XZ plane (2D barycentric).
static int point_in_tri_xz(float px, float pz, const float *v0, const float *v1, const float *v2) {
    float d1x = v1[0] - v0[0], d1z = v1[2] - v0[2];
    float d2x = v2[0] - v0[0], d2z = v2[2] - v0[2];
    float dpx = px - v0[0], dpz = pz - v0[2];
    const float eps = 1e-5f;

    float det = d1x * d2z - d2x * d1z;
    if (fabsf(det) < 1e-8f)
        return 0;
    float inv = 1.0f / det;
    float u = (dpx * d2z - d2x * dpz) * inv;
    float v = (d1x * dpz - dpx * d1z) * inv;
    return (u >= -eps && v >= -eps && (u + v) <= 1.0f + eps) ? 1 : 0;
}

/// @brief Interpolate the world-space Y coordinate at `(px, pz)` on a triangle defined by
///        @p v0 / @p v1 / @p v2. Used by navmesh height queries to project an XZ point onto
///        the walkable surface. Caller must already have confirmed `(px,pz)` lies inside.
static float triangle_y_at_xz(
    float px, float pz, const float *v0, const float *v1, const float *v2) {
    float d1x = v1[0] - v0[0], d1z = v1[2] - v0[2];
    float d2x = v2[0] - v0[0], d2z = v2[2] - v0[2];
    float dpx = px - v0[0], dpz = pz - v0[2];
    float det = d1x * d2z - d2x * d1z;
    if (fabsf(det) < 1e-8f)
        return (v0[1] + v1[1] + v2[1]) / 3.0f;
    float inv = 1.0f / det;
    float u = (dpx * d2z - d2x * dpz) * inv;
    float v = (d1x * dpz - dpx * d1z) * inv;
    return v0[1] + u * (v1[1] - v0[1]) + v * (v2[1] - v0[1]);
}

/// @brief Vertical snap tolerance for point-on-mesh tests: half the agent height,
///        floored at 0.25 so a zero/degenerate agent height still tolerates small
///        Y gaps between the query point and a triangle.
static float navmesh3d_vertical_tolerance(const rt_navmesh3d *nm) {
    double h = nm ? nm->agent_height : 0.0;
    if (!isfinite(h) || h <= 0.0)
        h = 0.0;
    return (float)fmax(h * 0.5, 0.25);
}

/// @brief Closest point to (`px`,`pz`) on segment a–b, projected onto the XZ plane.
/// @details Y is ignored (navmesh queries are horizontal). The parametric `t` is
///          clamped to `[0,1]` to stay on the segment; near-zero-length segments
///          collapse to endpoint `a`. Outputs the point and its squared XZ distance.
static void closest_point_on_segment_xz(
    float px, float pz, const float *a, const float *b, float *out_x, float *out_z, float *out_d2) {
    float abx = b[0] - a[0];
    float abz = b[2] - a[2];
    float len_sq = abx * abx + abz * abz;
    float t = 0.0f;
    if (len_sq > 1e-8f) {
        t = ((px - a[0]) * abx + (pz - a[2]) * abz) / len_sq;
        if (t < 0.0f)
            t = 0.0f;
        else if (t > 1.0f)
            t = 1.0f;
    }
    float cx = a[0] + abx * t;
    float cz = a[2] + abz * t;
    float dx = px - cx;
    float dz = pz - cz;
    *out_x = cx;
    *out_z = cz;
    *out_d2 = dx * dx + dz * dz;
}

/// @brief Closest point to (`px`,`pz`) on triangle v0/v1/v2, projected onto XZ.
/// @details Returns the query point unchanged when it lies inside the triangle;
///          otherwise returns the nearest of the three edge projections.
static void closest_point_on_tri_xz(float px,
                                    float pz,
                                    const float *v0,
                                    const float *v1,
                                    const float *v2,
                                    float *out_x,
                                    float *out_z) {
    float best_x;
    float best_z;
    float best_d2;
    float cx;
    float cz;
    float d2;
    if (point_in_tri_xz(px, pz, v0, v1, v2)) {
        *out_x = px;
        *out_z = pz;
        return;
    }
    closest_point_on_segment_xz(px, pz, v0, v1, &best_x, &best_z, &best_d2);
    closest_point_on_segment_xz(px, pz, v1, v2, &cx, &cz, &d2);
    if (d2 < best_d2) {
        best_x = cx;
        best_z = cz;
        best_d2 = d2;
    }
    closest_point_on_segment_xz(px, pz, v2, v0, &cx, &cz, &d2);
    if (d2 < best_d2) {
        best_x = cx;
        best_z = cz;
    }
    *out_x = best_x;
    *out_z = best_z;
}

/// @brief Find triangle containing point (projected onto XZ), with optional vertical tolerance.
static int32_t find_tri_with_max_dy(
    const rt_navmesh3d *nm, float px, float py, float pz, float max_dy) {
    float best_dy = FLT_MAX;
    int32_t best = -1;
    for (int32_t i = 0; i < nm->triangle_count; i++) {
        const nav_triangle_t *t = &nm->triangles[i];
        const float *v0 = nm->vertices[t->v[0]].position;
        const float *v1 = nm->vertices[t->v[1]].position;
        const float *v2 = nm->vertices[t->v[2]].position;
        if (point_in_tri_xz(px, pz, v0, v1, v2)) {
            float surface_y = triangle_y_at_xz(px, pz, v0, v1, v2);
            float dy = fabsf(py - surface_y);
            if (max_dy >= 0.0f && dy > max_dy)
                continue;
            if (dy < best_dy) {
                best_dy = dy;
                best = i;
            }
        }
    }
    return best;
}

/// @brief Find triangle containing point (projected onto XZ), ignoring vertical separation.
static int32_t find_tri(const rt_navmesh3d *nm, float px, float py, float pz) {
    return find_tri_with_max_dy(nm, px, py, pz, -1.0f);
}

/// @brief Re-resolve authored off-mesh link endpoints against the current walkable triangle set.
static void navmesh3d_refresh_offmesh_links(rt_navmesh3d *nm) {
    if (!nm || !nm->offmesh_links)
        return;
    float max_dy = navmesh3d_vertical_tolerance(nm);
    for (int32_t i = 0; i < nm->offmesh_link_count; i++) {
        nav_offmesh_link_t *link = &nm->offmesh_links[i];
        link->from_tri =
            find_tri_with_max_dy(nm, link->from[0], link->from[1], link->from[2], max_dy);
        link->to_tri = find_tri_with_max_dy(nm, link->to[0], link->to[1], link->to[2], max_dy);
    }
}

/// @brief Return the authored link connecting two triangles, if any. Sets @p reverse for to->from.
static const nav_offmesh_link_t *navmesh3d_find_offmesh_link(const rt_navmesh3d *nm,
                                                             int32_t from_tri,
                                                             int32_t to_tri,
                                                             int *reverse) {
    if (reverse)
        *reverse = 0;
    if (!nm || !nm->offmesh_links)
        return NULL;
    for (int32_t i = 0; i < nm->offmesh_link_count; i++) {
        const nav_offmesh_link_t *link = &nm->offmesh_links[i];
        if (link->from_tri == from_tri && link->to_tri == to_tri)
            return link;
        if (link->bidirectional && link->to_tri == from_tri && link->from_tri == to_tri) {
            if (reverse)
                *reverse = 1;
            return link;
        }
    }
    return NULL;
}

/*==========================================================================
 * A* pathfinding
 *=========================================================================*/

typedef struct {
    int32_t tri;
    float f;
} heap_entry_t;

/// @brief Push a `(triangle, f-score)` entry onto the A* min-heap and sift up. Silently
/// no-ops on overflow — the heap is sized to the triangle count, so overflow only
/// happens if duplicate insertion guards fail. Comparison uses `f` (g + heuristic) so
/// the heap top is always the lowest-cost open triangle.
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

/// @brief Pop the lowest-`f` triangle from the A* min-heap and sift down. Returns the
/// triangle index. Caller is responsible for ensuring `*size > 0` (assertion would be
/// nice but this is a hot path; A* main loop guards via `while (size > 0)`).
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

/// @brief Euclidean distance between two triangles' precomputed centroids. Used as the
/// A* heuristic (admissible since centroid-to-centroid is always ≤ the true polygon-
/// crossing path) and as the per-edge step cost when expanding neighbours.
static float centroid_dist(const rt_navmesh3d *nm, int32_t a, int32_t b) {
    const float *ca = nm->triangles[a].centroid;
    const float *cb = nm->triangles[b].centroid;
    float dx = cb[0] - ca[0], dy = cb[1] - ca[1], dz = cb[2] - ca[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

/// @brief Internal: compute the path from `from_v` to `to_v` and copy the waypoints into a
/// freshly malloc'd `*out_points_xyz` (interleaved x,y,z). Returns the point count, or 0 on
/// failure. Caller frees `*out_points_xyz`.
int64_t rt_navmesh3d_copy_path_points(void *obj,
                                      void *from_v,
                                      void *to_v,
                                      double **out_points_xyz) {
    double *points = NULL;
    int64_t point_count = 0;
    if (!obj || !rt_g3d_is_vec3(from_v) || !rt_g3d_is_vec3(to_v))
        return 0;
    if (out_points_xyz)
        *out_points_xyz = NULL;
    rt_navmesh3d *nm = (rt_navmesh3d *)rt_g3d_checked_or_null(obj, RT_G3D_NAVMESH3D_CLASS_ID);
    if (!nm || nm->triangle_count == 0 || !nm->triangles || !nm->vertices)
        return 0;

    double fdx = rt_vec3_x(from_v), fdy = rt_vec3_y(from_v), fdz = rt_vec3_z(from_v);
    double tdx = rt_vec3_x(to_v), tdy = rt_vec3_y(to_v), tdz = rt_vec3_z(to_v);
    float fx = (float)fdx, fy = (float)fdy, fz = (float)fdz;
    float tx = (float)tdx, ty = (float)tdy, tz = (float)tdz;
    float max_dy = navmesh3d_vertical_tolerance(nm);
    if (!isfinite(fx) || !isfinite(fy) || !isfinite(fz) || !isfinite(tx) || !isfinite(ty) ||
        !isfinite(tz))
        return 0;

    int32_t start = find_tri_with_max_dy(nm, fx, fy, fz, max_dy);
    int32_t goal = find_tri_with_max_dy(nm, tx, ty, tz, max_dy);
    if (start < 0 || goal < 0)
        return 0;

    if (start == goal) {
        points = (double *)malloc(2u * 3u * sizeof(double));
        if (!points)
            return 0;
        points[0] = fdx;
        points[1] = fdy;
        points[2] = fdz;
        points[3] = tdx;
        points[4] = tdy;
        points[5] = tdz;
        if (out_points_xyz) {
            *out_points_xyz = points;
        } else {
            free(points);
        }
        return 2;
    }

    /* A* */
    int32_t tc = nm->triangle_count;
    if (tc <= 0 || tc > INT32_MAX / 3)
        return 0;
    int64_t heap_cap64 = (int64_t)tc * 3 + (int64_t)nm->offmesh_link_count * 2 + 8;
    if (heap_cap64 <= 0 || heap_cap64 > INT32_MAX)
        return 0;
    int32_t heap_cap = (int32_t)heap_cap64;
    if ((size_t)tc > SIZE_MAX / sizeof(float) || (size_t)tc > SIZE_MAX / sizeof(int32_t) ||
        (size_t)tc > SIZE_MAX / sizeof(int8_t) ||
        (size_t)heap_cap > SIZE_MAX / sizeof(heap_entry_t))
        return 0;
    float *g_cost = (float *)calloc((size_t)tc, sizeof(float));
    int32_t *parent = (int32_t *)malloc((size_t)tc * sizeof(int32_t));
    int8_t *closed = (int8_t *)calloc((size_t)tc, sizeof(int8_t));
    heap_entry_t *heap = (heap_entry_t *)malloc((size_t)heap_cap * sizeof(heap_entry_t));
    if (!g_cost || !parent || !closed || !heap) {
        free(g_cost);
        free(parent);
        free(closed);
        free(heap);
        return 0;
    }
    memset(parent, -1, (size_t)tc * sizeof(int32_t));
    for (int32_t i = 0; i < tc; i++)
        g_cost[i] = FLT_MAX;

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

        for (int32_t li = 0; li < nm->offmesh_link_count; li++) {
            const nav_offmesh_link_t *link = &nm->offmesh_links[li];
            int32_t next = -1;
            if (link->from_tri == cur)
                next = link->to_tri;
            else if (link->bidirectional && link->to_tri == cur)
                next = link->from_tri;
            if (next < 0 || next >= tc || closed[next])
                continue;

            float new_g = g_cost[cur] + centroid_dist(nm, cur, next);
            if (new_g < g_cost[next]) {
                g_cost[next] = new_g;
                parent[next] = cur;
                heap_push(heap, &heap_size, heap_cap, next, new_g + centroid_dist(nm, next, goal));
            }
        }
    }

    if (found) {
        /* Reconstruct: collect centroids from goal back to start */
        int32_t count = 0;
        for (int32_t c = goal; c != -1; c = parent[c])
            count++;

        int32_t *seq = (int32_t *)malloc((size_t)count * sizeof(int32_t));
        if (!seq) {
            free(g_cost);
            free(parent);
            free(closed);
            free(heap);
            return 0;
        }
        int32_t idx = count - 1;
        for (int32_t c = goal; c != -1; c = parent[c])
            seq[idx--] = c;

        if (count > (INT32_MAX - 2) / 3) {
            free(seq);
            free(g_cost);
            free(parent);
            free(closed);
            free(heap);
            return 0;
        }
        int32_t max_points = count * 3 + 2;
        points = (double *)malloc((size_t)max_points * 3u * sizeof(double));
        if (!points) {
            free(seq);
            free(g_cost);
            free(parent);
            free(closed);
            free(heap);
            return 0;
        }
        points[point_count * 3 + 0] = fdx;
        points[point_count * 3 + 1] = fdy;
        points[point_count * 3 + 2] = fdz;
        point_count++;

        /* Simple string-pulling: find portals between adjacent triangles and
         * walk the funnel to produce smooth waypoints. If portal extraction
         * fails for any edge, fall back to the centroid for that segment. */
        if (count >= 2) {
            float apex[3];
            apex[0] = fx;
            apex[1] = fy;
            apex[2] = fz;

            for (int32_t i = 0; i < count - 1; i++) {
                int32_t ti = seq[i];
                int32_t tn = seq[i + 1];
                /* Find the shared edge (portal) between consecutive triangles */
                nav_triangle_t *ta = &nm->triangles[ti];
                int portal_found = 0;
                for (int e = 0; e < 3; e++) {
                    if (ta->neighbors[e] == tn) {
                        /* Shared edge vertices: v[e] and v[(e+1)%3] */
                        int32_t vi0 = ta->v[e];
                        int32_t vi1 = ta->v[(e + 1) % 3];
                        float *p0 = nm->vertices[vi0].position;
                        float *p1 = nm->vertices[vi1].position;
                        /* Use the midpoint of the portal as a waypoint.
                         * A full funnel algorithm would track left/right boundaries
                         * and only emit waypoints at funnel constrictions. For now,
                         * the midpoint produces much smoother paths than centroids. */
                        float mx = (p0[0] + p1[0]) * 0.5f;
                        float my = (p0[1] + p1[1]) * 0.5f;
                        float mz = (p0[2] + p1[2]) * 0.5f;
                        /* Only add waypoint if it's far enough from the last one
                         * (avoids redundant collinear points) */
                        float dx = mx - apex[0], dy = my - apex[1], dz = mz - apex[2];
                        if (dx * dx + dy * dy + dz * dz > 0.01f) {
                            points[point_count * 3 + 0] = mx;
                            points[point_count * 3 + 1] = my;
                            points[point_count * 3 + 2] = mz;
                            point_count++;
                            apex[0] = mx;
                            apex[1] = my;
                            apex[2] = mz;
                        }
                        portal_found = 1;
                        break;
                    }
                }
                if (!portal_found) {
                    int reverse = 0;
                    const nav_offmesh_link_t *link = navmesh3d_find_offmesh_link(nm, ti, tn, &reverse);
                    if (link) {
                        const float *a = reverse ? link->to : link->from;
                        const float *b = reverse ? link->from : link->to;
                        float dax = a[0] - apex[0];
                        float day = a[1] - apex[1];
                        float daz = a[2] - apex[2];
                        if (dax * dax + day * day + daz * daz > 0.01f) {
                            points[point_count * 3 + 0] = a[0];
                            points[point_count * 3 + 1] = a[1];
                            points[point_count * 3 + 2] = a[2];
                            point_count++;
                        }
                        points[point_count * 3 + 0] = b[0];
                        points[point_count * 3 + 1] = b[1];
                        points[point_count * 3 + 2] = b[2];
                        point_count++;
                        apex[0] = b[0];
                        apex[1] = b[1];
                        apex[2] = b[2];
                    } else {
                        /* Fallback to centroid if shared edge not found */
                        float *cen = nm->triangles[tn].centroid;
                        points[point_count * 3 + 0] = cen[0];
                        points[point_count * 3 + 1] = cen[1];
                        points[point_count * 3 + 2] = cen[2];
                        point_count++;
                        apex[0] = cen[0];
                        apex[1] = cen[1];
                        apex[2] = cen[2];
                    }
                }
            }
        } else if (count == 1) {
            float *cen = nm->triangles[seq[0]].centroid;
            points[point_count * 3 + 0] = cen[0];
            points[point_count * 3 + 1] = cen[1];
            points[point_count * 3 + 2] = cen[2];
            point_count++;
        }

        points[point_count * 3 + 0] = tdx;
        points[point_count * 3 + 1] = tdy;
        points[point_count * 3 + 2] = tdz;
        point_count++;
        free(seq);
    }

    free(g_cost);
    free(parent);
    free(closed);
    free(heap);
    if (out_points_xyz)
        *out_points_xyz = points;
    else
        free(points);
    return point_count;
}

/// @brief Compute a path from `from_v` to `to_v` (both Vec3 world-space points). Returns a
/// Seq of Vec3 waypoints, including endpoints. Empty Seq if either endpoint is off-mesh or
/// no connecting path exists.
void *rt_navmesh3d_find_path(void *obj, void *from_v, void *to_v) {
    double *points = NULL;
    int64_t point_count = rt_navmesh3d_copy_path_points(obj, from_v, to_v, &points);
    if (point_count <= 0 || !points)
        return NULL;

    void *path = rt_path3d_new();
    if (!path) {
        free(points);
        return NULL;
    }
    for (int64_t i = 0; i < point_count; i++) {
        void *point = rt_vec3_new(points[i * 3 + 0], points[i * 3 + 1], points[i * 3 + 2]);
        rt_path3d_add_point(path, point);
        navmesh3d_release_local(point);
    }
    free(points);
    return path;
}

/// @brief Snap `point` (Vec3) onto the nearest navmesh triangle and return the projected
/// world-space position. Useful for pinning agent positions to walkable surface after
/// teleports. Returns the input unchanged if no triangle is found nearby.
void *rt_navmesh3d_sample_position(void *obj, void *point) {
    if (!obj || !rt_g3d_is_vec3(point))
        return rt_vec3_new(0, 0, 0);
    rt_navmesh3d *nm = (rt_navmesh3d *)rt_g3d_checked_or_null(obj, RT_G3D_NAVMESH3D_CLASS_ID);
    if (!nm)
        return rt_vec3_new(0, 0, 0);
    double pdx = rt_vec3_x(point), pdy = rt_vec3_y(point), pdz = rt_vec3_z(point);
    if (!isfinite(pdx) || !isfinite(pdy) || !isfinite(pdz))
        return rt_vec3_new(0, 0, 0);
    float px = (float)pdx, py = (float)pdy, pz = (float)pdz;
    if (nm->triangle_count <= 0 || !nm->triangles || !nm->vertices)
        return rt_vec3_new(px, py, pz);

    int32_t tri = find_tri(nm, px, py, pz);
    if (tri >= 0) {
        nav_triangle_t *t = &nm->triangles[tri];
        const float *v0 = nm->vertices[t->v[0]].position;
        const float *v1 = nm->vertices[t->v[1]].position;
        const float *v2 = nm->vertices[t->v[2]].position;
        return rt_vec3_new(px, triangle_y_at_xz(px, pz, v0, v1, v2), pz);
    }

    /* Find nearest point on the triangle surface in XZ, not merely nearest centroid. */
    float best_d = FLT_MAX;
    float best_dy = FLT_MAX;
    float best_x = px;
    float best_y = py;
    float best_z = pz;
    for (int32_t i = 0; i < nm->triangle_count; i++) {
        nav_triangle_t *t = &nm->triangles[i];
        const float *v0 = nm->vertices[t->v[0]].position;
        const float *v1 = nm->vertices[t->v[1]].position;
        const float *v2 = nm->vertices[t->v[2]].position;
        float cx;
        float cz;
        float cy;
        float dx;
        float dz;
        float dy;
        float d;
        closest_point_on_tri_xz(px, pz, v0, v1, v2, &cx, &cz);
        cy = triangle_y_at_xz(cx, cz, v0, v1, v2);
        dx = px - cx;
        dz = pz - cz;
        dy = fabsf(py - cy);
        d = dx * dx + dz * dz;
        if (d < best_d || (fabsf(d - best_d) <= 1e-6f && dy < best_dy)) {
            best_d = d;
            best_dy = dy;
            best_x = cx;
            best_y = cy;
            best_z = cz;
        }
    }
    return rt_vec3_new(best_x, best_y, best_z);
}

/// @brief Returns 1 if `point` (Vec3) lies within (or near) any walkable triangle.
int8_t rt_navmesh3d_is_walkable(void *obj, void *point) {
    if (!obj || !rt_g3d_is_vec3(point))
        return 0;
    rt_navmesh3d *nm = (rt_navmesh3d *)rt_g3d_checked_or_null(obj, RT_G3D_NAVMESH3D_CLASS_ID);
    if (!nm || nm->triangle_count <= 0 || !nm->triangles || !nm->vertices)
        return 0;
    double pdx = rt_vec3_x(point), pdy = rt_vec3_y(point), pdz = rt_vec3_z(point);
    if (!isfinite(pdx) || !isfinite(pdy) || !isfinite(pdz))
        return 0;
    float px = (float)pdx, py = (float)pdy, pz = (float)pdz;
    return find_tri_with_max_dy(nm, px, py, pz, navmesh3d_vertical_tolerance(nm)) >= 0 ? 1 : 0;
}

/// @brief Number of walkable triangles in the baked navmesh.
int64_t rt_navmesh3d_get_triangle_count(void *obj) {
    rt_navmesh3d *nm = (rt_navmesh3d *)rt_g3d_checked_or_null(obj, RT_G3D_NAVMESH3D_CLASS_ID);
    return nm ? nm->triangle_count : 0;
}

/// @brief Add an authored off-mesh traversal link between two walkable points.
int8_t rt_navmesh3d_add_offmesh_link(void *obj, void *from, void *to, int8_t bidirectional) {
    if (!obj || !rt_g3d_is_vec3(from) || !rt_g3d_is_vec3(to))
        return 0;
    rt_navmesh3d *nm = (rt_navmesh3d *)rt_g3d_checked_or_null(obj, RT_G3D_NAVMESH3D_CLASS_ID);
    if (!nm || nm->triangle_count <= 0 || !nm->triangles || !nm->vertices)
        return 0;

    double fdx = rt_vec3_x(from), fdy = rt_vec3_y(from), fdz = rt_vec3_z(from);
    double tdx = rt_vec3_x(to), tdy = rt_vec3_y(to), tdz = rt_vec3_z(to);
    float fx = (float)fdx, fy = (float)fdy, fz = (float)fdz;
    float tx = (float)tdx, ty = (float)tdy, tz = (float)tdz;
    if (!isfinite(fx) || !isfinite(fy) || !isfinite(fz) || !isfinite(tx) || !isfinite(ty) ||
        !isfinite(tz))
        return 0;

    float max_dy = navmesh3d_vertical_tolerance(nm);
    int32_t from_tri = find_tri_with_max_dy(nm, fx, fy, fz, max_dy);
    int32_t to_tri = find_tri_with_max_dy(nm, tx, ty, tz, max_dy);
    if (from_tri < 0 || to_tri < 0)
        return 0;

    if (nm->offmesh_link_count >= nm->offmesh_link_capacity) {
        int32_t new_cap = 4;
        if (nm->offmesh_link_capacity > 0) {
            if (nm->offmesh_link_capacity > INT32_MAX / 2)
                return 0;
            new_cap = nm->offmesh_link_capacity * 2;
        }
        if ((size_t)new_cap > SIZE_MAX / sizeof(nav_offmesh_link_t))
            return 0;
        nav_offmesh_link_t *links = (nav_offmesh_link_t *)realloc(
            nm->offmesh_links, (size_t)new_cap * sizeof(nav_offmesh_link_t));
        if (!links)
            return 0;
        nm->offmesh_links = links;
        nm->offmesh_link_capacity = new_cap;
    }

    nav_offmesh_link_t *link = &nm->offmesh_links[nm->offmesh_link_count++];
    link->from[0] = fx;
    link->from[1] = fy;
    link->from[2] = fz;
    link->to[0] = tx;
    link->to[1] = ty;
    link->to[2] = tz;
    link->from_tri = from_tri;
    link->to_tri = to_tri;
    link->bidirectional = bidirectional != 0 ? 1 : 0;
    return 1;
}

static int navmesh3d_read_obstacle_bounds(void *min_v,
                                          void *max_v,
                                          float *out_min,
                                          float *out_max) {
    if (!rt_g3d_is_vec3(min_v) || !rt_g3d_is_vec3(max_v) || !out_min || !out_max)
        return 0;
    double min_d[3] = {rt_vec3_x(min_v), rt_vec3_y(min_v), rt_vec3_z(min_v)};
    double max_d[3] = {rt_vec3_x(max_v), rt_vec3_y(max_v), rt_vec3_z(max_v)};
    for (int32_t i = 0; i < 3; i++) {
        if (!isfinite(min_d[i]) || !isfinite(max_d[i]))
            return 0;
        double lo = min_d[i] < max_d[i] ? min_d[i] : max_d[i];
        double hi = min_d[i] < max_d[i] ? max_d[i] : min_d[i];
        if (lo > (double)FLT_MAX || hi < -(double)FLT_MAX)
            return 0;
        if (lo < -(double)FLT_MAX)
            lo = -(double)FLT_MAX;
        if (hi > (double)FLT_MAX)
            hi = (double)FLT_MAX;
        out_min[i] = (float)lo;
        out_max[i] = (float)hi;
    }
    return 1;
}

static int navmesh3d_can_edit_obstacles(const rt_navmesh3d *nm) {
    return nm && nm->source_triangles && nm->triangles && nm->source_triangle_count > 0;
}

/// @brief Add a coarse AABB obstacle and immediately refilter the walkable triangle set.
int8_t rt_navmesh3d_add_obstacle(void *obj, void *min_v, void *max_v) {
    if (!obj)
        return 0;
    rt_navmesh3d *nm = (rt_navmesh3d *)rt_g3d_checked_or_null(obj, RT_G3D_NAVMESH3D_CLASS_ID);
    if (!navmesh3d_can_edit_obstacles(nm))
        return 0;

    float omin[3];
    float omax[3];
    if (!navmesh3d_read_obstacle_bounds(min_v, max_v, omin, omax))
        return 0;

    if (nm->obstacle_count >= nm->obstacle_capacity) {
        int32_t new_cap = 4;
        if (nm->obstacle_capacity > 0) {
            if (nm->obstacle_capacity > INT32_MAX / 2)
                return 0;
            new_cap = nm->obstacle_capacity * 2;
        }
        if ((size_t)new_cap > SIZE_MAX / sizeof(nav_obstacle_t))
            return 0;
        nav_obstacle_t *obstacles =
            (nav_obstacle_t *)realloc(nm->obstacles, (size_t)new_cap * sizeof(nav_obstacle_t));
        if (!obstacles)
            return 0;
        nm->obstacles = obstacles;
        nm->obstacle_capacity = new_cap;
    }

    nav_obstacle_t *obstacle = &nm->obstacles[nm->obstacle_count++];
    memcpy(obstacle->min, omin, sizeof(omin));
    memcpy(obstacle->max, omax, sizeof(omax));
    if (!navmesh3d_apply_slope_filter(nm)) {
        nm->obstacle_count--;
        (void)navmesh3d_apply_slope_filter(nm);
        return 0;
    }
    return 1;
}

/// @brief Remove a coarse AABB obstacle and immediately refilter the walkable triangle set.
int8_t rt_navmesh3d_remove_obstacle(void *obj, int64_t index) {
    rt_navmesh3d *nm = (rt_navmesh3d *)rt_g3d_checked_or_null(obj, RT_G3D_NAVMESH3D_CLASS_ID);
    if (!navmesh3d_can_edit_obstacles(nm) || index < 0 || index >= nm->obstacle_count)
        return 0;
    int32_t remove_at = (int32_t)index;
    nav_obstacle_t removed = nm->obstacles[remove_at];
    for (int32_t i = remove_at; i < nm->obstacle_count - 1; ++i)
        nm->obstacles[i] = nm->obstacles[i + 1];
    nm->obstacle_count--;
    if (!navmesh3d_apply_slope_filter(nm)) {
        for (int32_t i = nm->obstacle_count; i > remove_at; --i)
            nm->obstacles[i] = nm->obstacles[i - 1];
        nm->obstacles[remove_at] = removed;
        nm->obstacle_count++;
        (void)navmesh3d_apply_slope_filter(nm);
        return 0;
    }
    return 1;
}

/// @brief Update a coarse AABB obstacle and immediately refilter the walkable triangle set.
int8_t rt_navmesh3d_update_obstacle(void *obj, int64_t index, void *min_v, void *max_v) {
    rt_navmesh3d *nm = (rt_navmesh3d *)rt_g3d_checked_or_null(obj, RT_G3D_NAVMESH3D_CLASS_ID);
    float omin[3];
    float omax[3];
    nav_obstacle_t old_obstacle;
    if (!navmesh3d_can_edit_obstacles(nm) || index < 0 || index >= nm->obstacle_count)
        return 0;
    if (!navmesh3d_read_obstacle_bounds(min_v, max_v, omin, omax))
        return 0;
    int32_t edit_at = (int32_t)index;
    old_obstacle = nm->obstacles[edit_at];
    memcpy(nm->obstacles[edit_at].min, omin, sizeof(omin));
    memcpy(nm->obstacles[edit_at].max, omax, sizeof(omax));
    if (!navmesh3d_apply_slope_filter(nm)) {
        nm->obstacles[edit_at] = old_obstacle;
        (void)navmesh3d_apply_slope_filter(nm);
        return 0;
    }
    return 1;
}

/// @brief Number of authored coarse AABB obstacles.
int64_t rt_navmesh3d_get_obstacle_count(void *obj) {
    rt_navmesh3d *nm = (rt_navmesh3d *)rt_g3d_checked_or_null(obj, RT_G3D_NAVMESH3D_CLASS_ID);
    return nm ? nm->obstacle_count : 0;
}

/// @brief Number of authored off-mesh traversal links.
int64_t rt_navmesh3d_get_offmesh_link_count(void *obj) {
    rt_navmesh3d *nm = (rt_navmesh3d *)rt_g3d_checked_or_null(obj, RT_G3D_NAVMESH3D_CLASS_ID);
    return nm ? nm->offmesh_link_count : 0;
}

/// @brief Rebuild one tile after dynamic changes.
/// @details Real tiled data ownership is future work; this conservative baseline refilters the
/// preserved source triangles and rebuilds adjacency/off-mesh endpoint resolution for the whole
/// navmesh. The tile coordinates are accepted for API stability.
int8_t rt_navmesh3d_rebuild_tile(void *obj, int64_t tile_x, int64_t tile_z) {
    (void)tile_x;
    (void)tile_z;
    rt_navmesh3d *nm = (rt_navmesh3d *)rt_g3d_checked_or_null(obj, RT_G3D_NAVMESH3D_CLASS_ID);
    if (!nm || !nm->source_triangles || !nm->triangles || nm->source_triangle_count <= 0)
        return 0;
    return navmesh3d_apply_slope_filter(nm) ? 1 : 0;
}

/// @brief Set the maximum slope angle (in degrees) considered walkable.
/// Refilters the preserved source triangles and rebuilds adjacency immediately.
void rt_navmesh3d_set_max_slope(void *obj, double degrees) {
    rt_navmesh3d *nm = (rt_navmesh3d *)rt_g3d_checked_or_null(obj, RT_G3D_NAVMESH3D_CLASS_ID);
    if (!nm)
        return;
    nm->max_slope = navmesh3d_sanitize_slope(degrees);
    (void)navmesh3d_apply_slope_filter(nm);
}

/// @brief Render the navmesh as wireframe / outlined triangles to a Canvas3D for debugging.
/// Useful for visualizing what's actually walkable vs. blocked.
void rt_navmesh3d_debug_draw(void *obj, void *canvas) {
    if (!obj || !canvas)
        return;
    rt_navmesh3d *nm = (rt_navmesh3d *)rt_g3d_checked_or_null(obj, RT_G3D_NAVMESH3D_CLASS_ID);
    if (!nm || nm->triangle_count <= 0 || !nm->vertices || !nm->triangles)
        return;
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
        navmesh3d_release_local(a);
        navmesh3d_release_local(b);
        navmesh3d_release_local(c);
    }
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
