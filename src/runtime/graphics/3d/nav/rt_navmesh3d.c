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
    int8_t blocked;  /* 1 = carved out by a dynamic obstacle; kept in the array but skipped by every
                        query */
    int32_t area_id; /* 0 = default, >0 indexes nm->area_names[id-1] */
    float traversal_cost; /* >=1 multiplier used by A* */
} nav_triangle_t;

typedef struct {
    float from[3];
    float to[3];
    int32_t from_tri;
    int32_t to_tri;
    int8_t bidirectional;
    int64_t state_flags;
    float traversal_cost; /* >=1 multiplier over endpoint distance */
    rt_string kind;       /* optional authored traversal kind/state name */
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
    rt_string *area_names;
    int32_t area_name_count;
    int32_t area_name_capacity;
    nav_obstacle_t *obstacles;
    int32_t obstacle_count;
    int32_t obstacle_capacity;
    double last_path_cost;
    double agent_radius;
    double agent_height;
    double max_slope;              /* degrees */
    int8_t skip_portal_width_gate; /* voxel bake enforces clearance via erosion, not portal width */
    /* >0 => tiled bake: RebuildTile / tiled AddObstacle re-flag one tile's triangles in place
     * (O(tile), no adjacency/grid rebuild). 0 => non-tiled: obstacle edits whole-mesh refilter.
     * Tile (tx,tz) covers world XZ [qgrid_min + t*tile_size, qgrid_min + (t+1)*tile_size). */
    double tile_size;
    /* Uniform XZ grid index over `triangles` for fast point location (find_tri), rebuilt whenever
     * apply_slope_filter rebuilds the triangle set. Empty (qgrid_nx == 0) => linear-scan fallback.
     * CSR layout: qgrid_starts[c]..qgrid_starts[c+1] indexes qgrid_tris for cell c. */
    double qgrid_min_x;
    double qgrid_min_z;
    double qgrid_inv_cell;
    int32_t qgrid_nx;
    int32_t qgrid_nz;
    int32_t *qgrid_starts;
    int32_t *qgrid_tris;
    double voxel_min_x;
    double voxel_min_z;
    double voxel_cell_size;
    int32_t voxel_nx;
    int32_t voxel_nz;
    float *voxel_cell_height;
    uint8_t *voxel_cell_walkable;
    int32_t *voxel_corner_vertices;
} rt_navmesh3d;

static void navmesh3d_refresh_offmesh_links(rt_navmesh3d *nm);
static int point_in_tri_xz(float px, float pz, const float *v0, const float *v1, const float *v2);
static void navmesh3d_voxel_emit_tri(
    rt_navmesh3d *nm, int32_t a, int32_t b, int32_t c, int8_t blocked);
static void *navmesh3d_voxel_bake(
    rt_mesh3d *src, double agent_radius, double agent_height, double max_slope, double cell_size);

/// @brief GC finalizer for NavMesh3D. Frees the baked vertex and triangle arrays
/// (heap-allocated during `Build`) and nulls the slots so a stale handle would crash
/// loudly on use rather than reading freed memory. The source mesh is borrowed only
/// during the build call, not retained, so there's no reference to release here.
static void navmesh3d_finalizer(void *obj) {
    rt_navmesh3d *nm = (rt_navmesh3d *)obj;
    if (nm->offmesh_links) {
        for (int32_t i = 0; i < nm->offmesh_link_count; i++) {
            if (nm->offmesh_links[i].kind)
                rt_string_unref(nm->offmesh_links[i].kind);
        }
    }
    if (nm->area_names) {
        for (int32_t i = 0; i < nm->area_name_count; i++) {
            if (nm->area_names[i])
                rt_string_unref(nm->area_names[i]);
        }
    }
    free(nm->vertices);
    free(nm->source_triangles);
    free(nm->triangles);
    free(nm->offmesh_links);
    free(nm->area_names);
    free(nm->obstacles);
    free(nm->qgrid_starts);
    free(nm->qgrid_tris);
    free(nm->voxel_cell_height);
    free(nm->voxel_cell_walkable);
    free(nm->voxel_corner_vertices);
    nm->vertices = NULL;
    nm->source_triangles = NULL;
    nm->triangles = NULL;
    nm->offmesh_links = NULL;
    nm->area_names = NULL;
    nm->obstacles = NULL;
    nm->qgrid_starts = NULL;
    nm->qgrid_tris = NULL;
    nm->voxel_cell_height = NULL;
    nm->voxel_cell_walkable = NULL;
    nm->voxel_corner_vertices = NULL;
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
    if (!nm || !nm->vertices || va < 0 || vb < 0 || va >= nm->vertex_count ||
        vb >= nm->vertex_count)
        return 0;
    /* Voxel-baked navmeshes already eroded the walkable set by the agent radius, so every surviving
     * grid edge is agent-safe; gating those (cell_size-wide) portals by 2*radius would fragment the
     * mesh. The width gate only applies to the Build() passthrough path over authored triangles. */
    if (nm->skip_portal_width_gate)
        return 1;
    if (!isfinite(nm->agent_radius) || nm->agent_radius <= 0.0)
        return 1;
    const float *a = nm->vertices[va].position;
    const float *b = nm->vertices[vb].position;
    double dx = (double)b[0] - (double)a[0];
    double dz = (double)b[2] - (double)a[2];
    double width = sqrt(dx * dx + dz * dz);
    return width + 1e-5 >= nm->agent_radius * 2.0 ? 1 : 0;
}

/// @brief Clamp a traversal cost to [1.0, 1e6]; non-finite or sub-1.0 values become 1.0.
static float navmesh3d_sanitize_traversal_cost(double cost) {
    if (!isfinite(cost) || cost < 1.0)
        cost = 1.0;
    if (cost > 1000000.0)
        cost = 1000000.0;
    return (float)cost;
}

/// @brief Return true when point (x,z) lies inside an axis-aligned XZ rectangle.
static int navmesh3d_point_in_rect_xz(float x, float z, const float *min, const float *max) {
    const float eps = 1e-5f;
    return x >= min[0] - eps && x <= max[0] + eps && z >= min[2] - eps && z <= max[2] + eps;
}

/// @brief Signed orientation (twice the triangle area) of points A, B, C in the XZ plane;
///   the sign gives the turn direction and zero means collinear.
static float navmesh3d_orient_xz(float ax, float az, float bx, float bz, float cx, float cz) {
    return (bx - ax) * (cz - az) - (bz - az) * (cx - ax);
}

/// @brief True if point (px, pz) is collinear with and lies within segment A→B in the XZ
///   plane (with a small epsilon tolerance).
static int navmesh3d_on_segment_xz(float ax, float az, float bx, float bz, float px, float pz) {
    const float eps = 1e-5f;
    if (fabsf(navmesh3d_orient_xz(ax, az, bx, bz, px, pz)) > eps)
        return 0;
    return px >= fminf(ax, bx) - eps && px <= fmaxf(ax, bx) + eps && pz >= fminf(az, bz) - eps &&
           pz <= fmaxf(az, bz) + eps;
}

/// @brief Closed segment intersection in the XZ plane.
static int navmesh3d_segments_intersect_xz(
    float ax, float az, float bx, float bz, float cx, float cz, float dx, float dz) {
    float o1 = navmesh3d_orient_xz(ax, az, bx, bz, cx, cz);
    float o2 = navmesh3d_orient_xz(ax, az, bx, bz, dx, dz);
    float o3 = navmesh3d_orient_xz(cx, cz, dx, dz, ax, az);
    float o4 = navmesh3d_orient_xz(cx, cz, dx, dz, bx, bz);
    const float eps = 1e-5f;
    if (((o1 > eps && o2 < -eps) || (o1 < -eps && o2 > eps)) &&
        ((o3 > eps && o4 < -eps) || (o3 < -eps && o4 > eps)))
        return 1;
    if (fabsf(o1) <= eps && navmesh3d_on_segment_xz(ax, az, bx, bz, cx, cz))
        return 1;
    if (fabsf(o2) <= eps && navmesh3d_on_segment_xz(ax, az, bx, bz, dx, dz))
        return 1;
    if (fabsf(o3) <= eps && navmesh3d_on_segment_xz(cx, cz, dx, dz, ax, az))
        return 1;
    if (fabsf(o4) <= eps && navmesh3d_on_segment_xz(cx, cz, dx, dz, bx, bz))
        return 1;
    return 0;
}

/// @brief Whether a triangle's exact XZ footprint intersects an AABB footprint and vertical span.
static int navmesh3d_triangle_overlaps_bounds(const rt_navmesh3d *nm,
                                              const nav_triangle_t *tri,
                                              const float *min,
                                              const float *max) {
    if (!nm || !tri || !min || !max || !nm->vertices)
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
    if (tri_max[0] < min[0] || tri_min[0] > max[0] || tri_max[1] < min[1] || tri_min[1] > max[1] ||
        tri_max[2] < min[2] || tri_min[2] > max[2])
        return 0;

    if (navmesh3d_point_in_rect_xz(p0[0], p0[2], min, max) ||
        navmesh3d_point_in_rect_xz(p1[0], p1[2], min, max) ||
        navmesh3d_point_in_rect_xz(p2[0], p2[2], min, max))
        return 1;

    float r0[2] = {min[0], min[2]};
    float r1[2] = {max[0], min[2]};
    float r2[2] = {max[0], max[2]};
    float r3[2] = {min[0], max[2]};
    if (point_in_tri_xz(r0[0], r0[1], p0, p1, p2) || point_in_tri_xz(r1[0], r1[1], p0, p1, p2) ||
        point_in_tri_xz(r2[0], r2[1], p0, p1, p2) || point_in_tri_xz(r3[0], r3[1], p0, p1, p2))
        return 1;

    const float *tv[3] = {p0, p1, p2};
    const float *rv[4] = {r0, r1, r2, r3};
    for (int e = 0; e < 3; e++) {
        const float *a = tv[e];
        const float *b = tv[(e + 1) % 3];
        for (int re = 0; re < 4; re++) {
            const float *c = rv[re];
            const float *d = rv[(re + 1) % 4];
            if (navmesh3d_segments_intersect_xz(a[0], a[2], b[0], b[2], c[0], c[1], d[0], d[1]))
                return 1;
        }
    }
    return 0;
}

/// @brief Whether a triangle overlaps any authored obstacle volume.
static int navmesh3d_triangle_blocked_by_obstacle(const rt_navmesh3d *nm,
                                                  const nav_triangle_t *tri) {
    if (!nm || !tri || !nm->obstacles || nm->obstacle_count <= 0 || !nm->vertices)
        return 0;
    for (int32_t i = 0; i < nm->obstacle_count; i++) {
        const nav_obstacle_t *ob = &nm->obstacles[i];
        if (!navmesh3d_triangle_overlaps_bounds(nm, tri, ob->min, ob->max))
            continue;
        return 1;
    }
    return 0;
}

/// @brief Intern a nav-area name, returning its 1-based area id and appending it (growing the
///   name table) if new.
/// @return The 1-based id, 0 for an empty name, or -1 on invalid input or allocation failure.
static int32_t navmesh3d_ensure_area_id(rt_navmesh3d *nm, rt_string area) {
    if (!nm || !area || !rt_string_is_handle(area))
        return -1;
    const char *name = rt_string_cstr(area);
    if (!name || name[0] == '\0')
        return 0;
    for (int32_t i = 0; i < nm->area_name_count; i++) {
        const char *existing = nm->area_names[i] ? rt_string_cstr(nm->area_names[i]) : "";
        if (strcmp(existing, name) == 0)
            return i + 1;
    }
    if (nm->area_name_count >= nm->area_name_capacity) {
        int32_t new_cap = nm->area_name_capacity > 0 ? nm->area_name_capacity * 2 : 4;
        if (new_cap < 0 || (size_t)new_cap > SIZE_MAX / sizeof(rt_string))
            return -1;
        rt_string *areas =
            (rt_string *)realloc(nm->area_names, (size_t)new_cap * sizeof(rt_string));
        if (!areas)
            return -1;
        for (int32_t i = nm->area_name_capacity; i < new_cap; i++)
            areas[i] = NULL;
        nm->area_names = areas;
        nm->area_name_capacity = new_cap;
    }
    nm->area_names[nm->area_name_count] = rt_string_ref(area);
    nm->area_name_count++;
    return nm->area_name_count;
}

/// @brief Reverse-lookup the name for a 1-based area id; returns "default" for id 0 or an
///   invalid/unset id.
static rt_string navmesh3d_area_name(const rt_navmesh3d *nm, int32_t area_id) {
    if (area_id > 0 && nm && nm->area_names && area_id <= nm->area_name_count &&
        nm->area_names[area_id - 1])
        return rt_string_ref(nm->area_names[area_id - 1]);
    return rt_const_cstr("default");
}

/// @brief Sanitized traversal cost of triangle @p tri (1.0 if @p tri is out of range).
static float navmesh3d_tri_cost(const rt_navmesh3d *nm, int32_t tri) {
    if (!nm || !nm->triangles || tri < 0 || tri >= nm->triangle_count)
        return 1.0f;
    return navmesh3d_sanitize_traversal_cost((double)nm->triangles[tri].traversal_cost);
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

/// @brief Release the spatial query-grid index.
static void navmesh3d_free_query_grid(rt_navmesh3d *nm) {
    if (!nm)
        return;
    free(nm->qgrid_starts);
    free(nm->qgrid_tris);
    nm->qgrid_starts = NULL;
    nm->qgrid_tris = NULL;
    nm->qgrid_nx = 0;
    nm->qgrid_nz = 0;
}

/// @brief Compute the inclusive grid-cell range covered by triangle @p i's XZ bounding box.
static void navmesh3d_tri_cell_range(const rt_navmesh3d *nm,
                                     int32_t i,
                                     double min_x,
                                     double min_z,
                                     double inv,
                                     int32_t gnx,
                                     int32_t gnz,
                                     int *cx0,
                                     int *cx1,
                                     int *cz0,
                                     int *cz1) {
    const nav_triangle_t *t = &nm->triangles[i];
    const float *a = nm->vertices[t->v[0]].position;
    const float *b = nm->vertices[t->v[1]].position;
    const float *c = nm->vertices[t->v[2]].position;
    int x0 = (int)(((double)fminf(a[0], fminf(b[0], c[0])) - min_x) * inv);
    int x1 = (int)(((double)fmaxf(a[0], fmaxf(b[0], c[0])) - min_x) * inv);
    int z0 = (int)(((double)fminf(a[2], fminf(b[2], c[2])) - min_z) * inv);
    int z1 = (int)(((double)fmaxf(a[2], fmaxf(b[2], c[2])) - min_z) * inv);
    if (x0 < 0)
        x0 = 0;
    if (z0 < 0)
        z0 = 0;
    if (x1 >= gnx)
        x1 = gnx - 1;
    if (z1 >= gnz)
        z1 = gnz - 1;
    if (x1 < x0)
        x1 = x0;
    if (z1 < z0)
        z1 = z0;
    *cx0 = x0;
    *cx1 = x1;
    *cz0 = z0;
    *cz1 = z1;
}

/// @brief Build a uniform XZ grid over `triangles` so point location scans only the triangles whose
///   XZ bounding box overlaps the query cell. A point inside a triangle is inside that triangle's
///   AABB, so the triangle is inserted into the point's cell — scanning one cell is exact. On any
///   allocation failure the grid is left empty and find_tri falls back to a (correct) linear scan.
static void navmesh3d_build_query_grid(rt_navmesh3d *nm) {
    double min_x = DBL_MAX, min_z = DBL_MAX, max_x = -DBL_MAX, max_z = -DBL_MAX;
    double span_x, span_z, cell, inv;
    int32_t dim, gnx, gnz;
    size_t ncells, total;
    int32_t *starts, *tris, *cursor;
    navmesh3d_free_query_grid(nm);
    if (!nm || nm->triangle_count <= 0 || !nm->triangles || !nm->vertices)
        return;
    for (int32_t i = 0; i < nm->triangle_count; i++) {
        const nav_triangle_t *t = &nm->triangles[i];
        for (int k = 0; k < 3; k++) {
            const float *p = nm->vertices[t->v[k]].position;
            if (p[0] < min_x)
                min_x = p[0];
            if (p[0] > max_x)
                max_x = p[0];
            if (p[2] < min_z)
                min_z = p[2];
            if (p[2] > max_z)
                max_z = p[2];
        }
    }
    if (!(max_x >= min_x) || !(max_z >= min_z))
        return;
    span_x = max_x - min_x;
    span_z = max_z - min_z;
    dim = (int32_t)sqrt((double)nm->triangle_count); /* ~1-2 triangles per cell */
    if (dim < 1)
        dim = 1;
    if (dim > 256)
        dim = 256;
    cell = fmax(span_x, span_z) / (double)dim;
    if (!(cell > 1e-9))
        cell = 1.0;
    inv = 1.0 / cell;
    gnx = (int32_t)(span_x / cell) + 1;
    gnz = (int32_t)(span_z / cell) + 1;
    if (gnx < 1)
        gnx = 1;
    if (gnz < 1)
        gnz = 1;
    if (gnx > 512)
        gnx = 512;
    if (gnz > 512)
        gnz = 512;
    ncells = (size_t)gnx * (size_t)gnz;
    starts = (int32_t *)calloc(ncells + 1, sizeof(int32_t));
    if (!starts)
        return;
    for (int32_t i = 0; i < nm->triangle_count; i++) {
        int cx0, cx1, cz0, cz1;
        navmesh3d_tri_cell_range(nm, i, min_x, min_z, inv, gnx, gnz, &cx0, &cx1, &cz0, &cz1);
        for (int cz = cz0; cz <= cz1; cz++)
            for (int cx = cx0; cx <= cx1; cx++)
                starts[(size_t)cz * (size_t)gnx + (size_t)cx + 1]++;
    }
    for (size_t c = 0; c < ncells; c++)
        starts[c + 1] += starts[c];
    total = (size_t)starts[ncells];
    tris = (int32_t *)malloc((total ? total : 1) * sizeof(int32_t));
    cursor = (int32_t *)malloc(ncells * sizeof(int32_t));
    if (!tris || !cursor) {
        free(starts);
        free(tris);
        free(cursor);
        return;
    }
    for (size_t c = 0; c < ncells; c++)
        cursor[c] = starts[c];
    for (int32_t i = 0; i < nm->triangle_count; i++) {
        int cx0, cx1, cz0, cz1;
        navmesh3d_tri_cell_range(nm, i, min_x, min_z, inv, gnx, gnz, &cx0, &cx1, &cz0, &cz1);
        for (int cz = cz0; cz <= cz1; cz++)
            for (int cx = cx0; cx <= cx1; cx++)
                tris[cursor[(size_t)cz * (size_t)gnx + (size_t)cx]++] = i;
    }
    free(cursor);
    nm->qgrid_min_x = min_x;
    nm->qgrid_min_z = min_z;
    nm->qgrid_inv_cell = inv;
    nm->qgrid_nx = gnx;
    nm->qgrid_nz = gnz;
    nm->qgrid_starts = starts;
    nm->qgrid_tris = tris;
}

/// @brief Rebuild the walkable triangle set from the source triangles, dropping over-steep ones.
/// @details A triangle is walkable when its upward normal's angle from vertical is within
///          nm->max_slope (compared via cosines). Repopulates nm->triangles / triangle_count.
static int navmesh3d_apply_slope_filter(rt_navmesh3d *nm) {
    if (!nm || !nm->source_triangles || !nm->triangles)
        return 0;
    double max_slope_cos = cos(nm->max_slope * M_PI / 180.0);
    nm->triangle_count = 0;
    for (int32_t i = 0; i < nm->source_triangle_count; i++) {
        nav_triangle_t tri = nm->source_triangles[i];
        if (tri.normal[1] < (float)max_slope_cos)
            continue;
        tri.neighbors[0] = tri.neighbors[1] = tri.neighbors[2] = -1;
        /* Keep unwalkable/source-carved and obstacle-carved triangles in the set but flag them
         * blocked, so a per-tile rebuild can flip flags/geometry without recompacting arrays. */
        tri.blocked = (tri.blocked || navmesh3d_triangle_blocked_by_obstacle(nm, &tri)) ? 1 : 0;
        tri.traversal_cost = navmesh3d_sanitize_traversal_cost((double)tri.traversal_cost);
        nm->triangles[nm->triangle_count++] = tri;
    }
    if (!navmesh3d_build_adjacency(nm)) {
        rt_trap("NavMesh3D: adjacency build failed (non-manifold edge or allocation failure)");
        return 0;
    }
    navmesh3d_refresh_offmesh_links(nm);
    navmesh3d_build_query_grid(nm);
    return 1;
}

/// @brief Map a world-space XZ position to its tile coordinates under the tiled-bake convention.
/// @details Tile (tx,tz) spans [origin + t*tile_size, origin + (t+1)*tile_size) where the origin is
///          the query-grid minimum (the mesh's min XZ). Floor keeps negative coordinates on the
///          correct side of the origin. Returns 0 (outputs untouched) when the mesh is not tiled or
///          has no spatial grid yet.
static int navmesh3d_tile_of_point(
    const rt_navmesh3d *nm, double px, double pz, int64_t *out_tx, int64_t *out_tz) {
    if (!nm || nm->tile_size <= 0.0 || nm->qgrid_nx <= 0)
        return 0;
    double inv = 1.0 / nm->tile_size;
    if (out_tx)
        *out_tx = (int64_t)floor((px - nm->qgrid_min_x) * inv);
    if (out_tz)
        *out_tz = (int64_t)floor((pz - nm->qgrid_min_z) * inv);
    return 1;
}

/// @brief Re-evaluate obstacle blocking for every triangle whose centroid lies in tile (tx,tz).
/// @details The O(tile) heart of tile-local rebuild: flips each in-tile triangle's `blocked` flag
///          against the current obstacle list without touching adjacency, the query grid, or any
///          other tile. Triangles outside the tile keep whatever state a prior edit left them in,
///          which is exactly what lets one tile be re-carved while the rest of the mesh is stale.
///          Returns the number of triangles re-flagged.
static int32_t navmesh3d_reflag_tile(rt_navmesh3d *nm, int64_t tile_x, int64_t tile_z) {
    if (!nm || !nm->triangles || nm->tile_size <= 0.0 || nm->qgrid_nx <= 0)
        return 0;
    double tsz = nm->tile_size;
    double x0 = nm->qgrid_min_x + (double)tile_x * tsz;
    double x1 = x0 + tsz;
    double z0 = nm->qgrid_min_z + (double)tile_z * tsz;
    double z1 = z0 + tsz;
    int32_t touched = 0;
    for (int32_t i = 0; i < nm->triangle_count; i++) {
        nav_triangle_t *t = &nm->triangles[i];
        double cx = (double)t->centroid[0];
        double cz = (double)t->centroid[2];
        if (cx < x0 || cx >= x1 || cz < z0 || cz >= z1)
            continue;
        int8_t source_blocked = (nm->source_triangles && i < nm->source_triangle_count &&
                                 nm->source_triangles[i].blocked)
                                    ? 1
                                    : 0;
        t->blocked = (source_blocked || navmesh3d_triangle_blocked_by_obstacle(nm, t)) ? 1 : 0;
        touched++;
    }
    return touched;
}

/// @brief Re-evaluate dynamic obstacle blocking for all tiles overlapped by an AABB.
static void navmesh3d_reflag_obstacle_tiles(rt_navmesh3d *nm,
                                            const float *omin,
                                            const float *omax) {
    if (!nm || !omin || !omax || nm->tile_size <= 0.0 || nm->qgrid_nx <= 0)
        return;
    int64_t tx0 = 0, tz0 = 0, tx1 = 0, tz1 = 0;
    navmesh3d_tile_of_point(nm, (double)omin[0], (double)omin[2], &tx0, &tz0);
    navmesh3d_tile_of_point(nm, (double)omax[0], (double)omax[2], &tx1, &tz1);
    if (tx1 < tx0) {
        int64_t t = tx0;
        tx0 = tx1;
        tx1 = t;
    }
    if (tz1 < tz0) {
        int64_t t = tz0;
        tz0 = tz1;
        tz1 = t;
    }
    for (int64_t tz = tz0; tz <= tz1; tz++)
        for (int64_t tx = tx0; tx <= tx1; tx++)
            navmesh3d_reflag_tile(nm, tx, tz);
    navmesh3d_refresh_offmesh_links(nm);
}

/// @brief True when the navmesh retained fixed-grid voxel source data for tile rebuilds.
static int navmesh3d_has_voxel_source(const rt_navmesh3d *nm) {
    return nm && nm->voxel_cell_height && nm->voxel_cell_walkable && nm->voxel_corner_vertices &&
           nm->voxel_nx > 0 && nm->voxel_nz > 0 && nm->voxel_cell_size > 0.0 && nm->vertices &&
           nm->source_triangles && nm->triangles;
}

/// @brief Return the retained voxel cell index, or SIZE_MAX for an invalid coordinate.
static size_t navmesh3d_voxel_cell_index(const rt_navmesh3d *nm, int32_t cx, int32_t cz) {
    if (!navmesh3d_has_voxel_source(nm) || cx < 0 || cz < 0 || cx >= nm->voxel_nx ||
        cz >= nm->voxel_nz)
        return SIZE_MAX;
    return (size_t)cz * (size_t)nm->voxel_nx + (size_t)cx;
}

/// @brief Get the fixed source-triangle index pair for a retained voxel cell.
static int32_t navmesh3d_voxel_cell_tri_start(const rt_navmesh3d *nm, int32_t cx, int32_t cz) {
    size_t cell = navmesh3d_voxel_cell_index(nm, cx, cz);
    if (cell == SIZE_MAX || cell > (size_t)INT32_MAX / 2)
        return -1;
    int32_t start = (int32_t)(cell * 2u);
    if (start < 0 || start + 1 >= nm->source_triangle_count || start + 1 >= nm->triangle_count)
        return -1;
    return start;
}

/// @brief Recompute one retained voxel corner height from the current tile source cells.
static void navmesh3d_voxel_refresh_corner(rt_navmesh3d *nm, int32_t ccx, int32_t ccz) {
    if (!navmesh3d_has_voxel_source(nm) || ccx < 0 || ccz < 0 || ccx > nm->voxel_nx ||
        ccz > nm->voxel_nz)
        return;
    int32_t cnx = nm->voxel_nx + 1;
    size_t corner = (size_t)ccz * (size_t)cnx + (size_t)ccx;
    int32_t vid = nm->voxel_corner_vertices[corner];
    if (vid < 0 || vid >= nm->vertex_count)
        return;
    double sum = 0.0;
    int n = 0;
    for (int dz = -1; dz <= 0; dz++) {
        for (int dx = -1; dx <= 0; dx++) {
            int32_t sx = ccx + dx;
            int32_t sz = ccz + dz;
            size_t cell = navmesh3d_voxel_cell_index(nm, sx, sz);
            if (cell == SIZE_MAX || !nm->voxel_cell_walkable[cell])
                continue;
            sum += (double)nm->voxel_cell_height[cell];
            n++;
        }
    }
    nm->vertices[vid].position[1] = n > 0 ? (float)(sum / (double)n) : 0.0f;
}

/// @brief Recompute one retained voxel cell's two source/current triangles from fixed corners.
static void navmesh3d_voxel_refresh_cell(rt_navmesh3d *nm, int32_t cx, int32_t cz) {
    int32_t tri_start = navmesh3d_voxel_cell_tri_start(nm, cx, cz);
    if (tri_start < 0)
        return;
    int32_t cnx = nm->voxel_nx + 1;
    int32_t a = nm->voxel_corner_vertices[(size_t)cz * (size_t)cnx + (size_t)cx];
    int32_t b = nm->voxel_corner_vertices[(size_t)cz * (size_t)cnx + (size_t)(cx + 1)];
    int32_t c = nm->voxel_corner_vertices[(size_t)(cz + 1) * (size_t)cnx + (size_t)(cx + 1)];
    int32_t d = nm->voxel_corner_vertices[(size_t)(cz + 1) * (size_t)cnx + (size_t)cx];
    if (a < 0 || b < 0 || c < 0 || d < 0)
        return;
    size_t cell = navmesh3d_voxel_cell_index(nm, cx, cz);
    int8_t source_blocked = (cell == SIZE_MAX || !nm->voxel_cell_walkable[cell]) ? 1 : 0;
    int32_t old_source_count = nm->source_triangle_count;
    nav_triangle_t updated[2];
    int32_t old_area[2] = {nm->source_triangles[tri_start].area_id,
                           nm->source_triangles[tri_start + 1].area_id};
    float old_cost[2] = {nm->source_triangles[tri_start].traversal_cost,
                         nm->source_triangles[tri_start + 1].traversal_cost};
    nm->source_triangle_count = tri_start;
    navmesh3d_voxel_emit_tri(nm, a, c, b, source_blocked);
    navmesh3d_voxel_emit_tri(nm, a, d, c, source_blocked);
    updated[0] = nm->source_triangles[tri_start];
    updated[1] = nm->source_triangles[tri_start + 1];
    nm->source_triangle_count = old_source_count;
    for (int i = 0; i < 2; i++) {
        nav_triangle_t tri = updated[i];
        tri.area_id = old_area[i];
        tri.traversal_cost = navmesh3d_sanitize_traversal_cost((double)old_cost[i]);
        updated[i].area_id = tri.area_id;
        updated[i].traversal_cost = tri.traversal_cost;
        tri.neighbors[0] = nm->triangles[tri_start + i].neighbors[0];
        tri.neighbors[1] = nm->triangles[tri_start + i].neighbors[1];
        tri.neighbors[2] = nm->triangles[tri_start + i].neighbors[2];
        nm->source_triangles[tri_start + i] = updated[i];
        tri.blocked = (source_blocked || navmesh3d_triangle_blocked_by_obstacle(nm, &tri)) ? 1 : 0;
        nm->triangles[tri_start + i] = tri;
    }
}

/// @brief Compute the retained voxel-cell range whose centers lie in tile (tile_x,tile_z).
static int navmesh3d_voxel_tile_cell_range(const rt_navmesh3d *nm,
                                           int64_t tile_x,
                                           int64_t tile_z,
                                           int32_t *out_cx0,
                                           int32_t *out_cx1,
                                           int32_t *out_cz0,
                                           int32_t *out_cz1) {
    if (!navmesh3d_has_voxel_source(nm) || nm->tile_size <= 0.0)
        return 0;
    double x0 = nm->qgrid_min_x + (double)tile_x * nm->tile_size;
    double x1 = x0 + nm->tile_size;
    double z0 = nm->qgrid_min_z + (double)tile_z * nm->tile_size;
    double z1 = z0 + nm->tile_size;
    int32_t cx0 =
        (int32_t)ceil((x0 - nm->voxel_min_x - nm->voxel_cell_size * 0.5) / nm->voxel_cell_size);
    int32_t cx1 =
        (int32_t)floor((x1 - nm->voxel_min_x - nm->voxel_cell_size * 0.5) / nm->voxel_cell_size);
    int32_t cz0 =
        (int32_t)ceil((z0 - nm->voxel_min_z - nm->voxel_cell_size * 0.5) / nm->voxel_cell_size);
    int32_t cz1 =
        (int32_t)floor((z1 - nm->voxel_min_z - nm->voxel_cell_size * 0.5) / nm->voxel_cell_size);
    if (cx0 < 0)
        cx0 = 0;
    if (cz0 < 0)
        cz0 = 0;
    if (cx1 >= nm->voxel_nx)
        cx1 = nm->voxel_nx - 1;
    if (cz1 >= nm->voxel_nz)
        cz1 = nm->voxel_nz - 1;
    if (cx1 < cx0 || cz1 < cz0)
        return 0;
    if (out_cx0)
        *out_cx0 = cx0;
    if (out_cx1)
        *out_cx1 = cx1;
    if (out_cz0)
        *out_cz0 = cz0;
    if (out_cz1)
        *out_cz1 = cz1;
    return 1;
}

/// @brief Rebuild one retained voxel tile's geometry/blocked flags in place.
static int32_t navmesh3d_rebuild_voxel_tile(rt_navmesh3d *nm, int64_t tile_x, int64_t tile_z) {
    int32_t cx0, cx1, cz0, cz1;
    int32_t touched = 0;
    if (!navmesh3d_voxel_tile_cell_range(nm, tile_x, tile_z, &cx0, &cx1, &cz0, &cz1))
        return 0;
    for (int32_t cz = cz0; cz <= cz1 + 1; cz++)
        for (int32_t cx = cx0; cx <= cx1 + 1; cx++)
            navmesh3d_voxel_refresh_corner(nm, cx, cz);
    for (int32_t cz = cz0; cz <= cz1; cz++) {
        for (int32_t cx = cx0; cx <= cx1; cx++) {
            navmesh3d_voxel_refresh_cell(nm, cx, cz);
            touched++;
        }
    }
    navmesh3d_refresh_offmesh_links(nm);
    return touched;
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
                grown = (rt_scene_node3d **)realloc(stack, (size_t)next_capacity * sizeof(*stack));
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
        if (!isfinite(nlen) || nlen < 1e-8f)
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
        tri->blocked = 0;
        tri->area_id = 0;
        tri->traversal_cost = 1.0f;
    }

    if (!navmesh3d_apply_slope_filter(nm)) {
        navmesh3d_free_partial(nm);
        return NULL;
    }

    return nm;
}

/// @brief Bake a navmesh from all Mesh3D-bearing nodes in a Scene3D.
/// @details Flattens scene node meshes through their world transforms into a temporary world-space
/// Mesh3D, then runs the from-scratch voxel baker (`navmesh3d_voxel_bake`): the walkable surface is
/// rasterized into a `cell_size` heightfield grid, eroded by the agent radius, and re-emitted as a
/// shared-corner grid mesh. Unlike `Build`, the result is decoupled from the input triangle density
/// and honors the agent radius, which is what `Scene3D`/`World3D` auto-bake expects.
void *rt_navmesh3d_bake(
    void *scene, double agent_radius, double agent_height, double max_slope, double cell_size) {
    void *source_mesh = navmesh3d_build_scene_source_mesh(scene);
    rt_navmesh3d *nm;
    if (!source_mesh)
        return NULL;
    nm = (rt_navmesh3d *)navmesh3d_voxel_bake(
        (rt_mesh3d *)source_mesh, agent_radius, agent_height, max_slope, cell_size);
    navmesh3d_release_local(source_mesh);
    return nm;
}

/// @brief Bake a full-scene navmesh and tag it tiled so obstacle edits can rebuild per tile.
/// @details The triangle set is still baked over the whole scene (one voxel pass), but recording
/// `tile_size` switches `RebuildTile` and `AddObstacle` onto the O(tile) in-place re-carve path
/// instead of a whole-mesh refilter. Tile (tx,tz) covers world XZ
/// [meshMin + t*tile_size, meshMin + (t+1)*tile_size). A non-positive tile_size leaves the mesh
/// untiled (whole-mesh refilter), matching `Bake`.
void *rt_navmesh3d_bake_tiled(void *scene,
                              double tile_size,
                              double agent_radius,
                              double agent_height,
                              double max_slope,
                              double cell_size) {
    void *obj = rt_navmesh3d_bake(scene, agent_radius, agent_height, max_slope, cell_size);
    if (obj && tile_size > 0.0 && isfinite(tile_size)) {
        rt_navmesh3d *nm = (rt_navmesh3d *)rt_g3d_checked_or_null(obj, RT_G3D_NAVMESH3D_CLASS_ID);
        if (nm)
            nm->tile_size = tile_size;
    }
    return obj;
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

/// @brief Get-or-create the shared grid-corner vertex at corner `(ccx, ccz)` in the navmesh vertex
///   array, placing it at the average surface height of the up-to-four walkable cells touching the
///   corner. Sharing one vertex per corner is what makes adjacent walkable cells share edge
///   vertices, so `navmesh3d_build_adjacency` connects them. Returns the vertex index.
static int32_t navmesh3d_voxel_corner_vertex(rt_navmesh3d *nm,
                                             int32_t *corner_vid,
                                             const float *cell_h,
                                             const uint8_t *cell_walk,
                                             int32_t nx,
                                             int32_t nz,
                                             double min_x,
                                             double min_z,
                                             double cell_size,
                                             int32_t ccx,
                                             int32_t ccz) {
    int32_t cnx = nx + 1;
    size_t cidx = (size_t)ccz * (size_t)cnx + (size_t)ccx;
    if (corner_vid[cidx] >= 0)
        return corner_vid[cidx];
    double sum = 0.0;
    int n = 0;
    for (int dz = -1; dz <= 0; dz++) {
        for (int dx = -1; dx <= 0; dx++) {
            int32_t sx = ccx + dx;
            int32_t sz = ccz + dz;
            if (sx < 0 || sz < 0 || sx >= nx || sz >= nz)
                continue;
            size_t s = (size_t)sz * (size_t)nx + (size_t)sx;
            if (!cell_walk[s])
                continue;
            sum += (double)cell_h[s];
            n++;
        }
    }
    int32_t vid = nm->vertex_count++;
    nm->vertices[vid].position[0] = (float)(min_x + (double)ccx * cell_size);
    nm->vertices[vid].position[1] = (n > 0) ? (float)(sum / (double)n) : 0.0f;
    nm->vertices[vid].position[2] = (float)(min_z + (double)ccz * cell_size);
    corner_vid[cidx] = vid;
    return vid;
}

/// @brief Append one walkable-surface triangle (indices into the navmesh vertex array) to the
///   navmesh source-triangle set, computing an up-facing face normal and centroid. Winding does
///   not matter to the downstream point-in-triangle / A* queries; the normal is forced to +Y so the
///   slope filter retains the surface.
static void navmesh3d_voxel_emit_tri(
    rt_navmesh3d *nm, int32_t a, int32_t b, int32_t c, int8_t blocked) {
    const float *p0 = nm->vertices[a].position;
    const float *p1 = nm->vertices[b].position;
    const float *p2 = nm->vertices[c].position;
    float e1x = p1[0] - p0[0], e1y = p1[1] - p0[1], e1z = p1[2] - p0[2];
    float e2x = p2[0] - p0[0], e2y = p2[1] - p0[1], e2z = p2[2] - p0[2];
    float nx_ = e1y * e2z - e1z * e2y;
    float ny_ = e1z * e2x - e1x * e2z;
    float nz_ = e1x * e2y - e1y * e2x;
    float nlen = sqrtf(nx_ * nx_ + ny_ * ny_ + nz_ * nz_);
    if (nlen < 1e-8f) {
        nx_ = 0.0f;
        ny_ = 1.0f;
        nz_ = 0.0f;
        nlen = 1.0f;
    }
    nx_ /= nlen;
    ny_ /= nlen;
    nz_ /= nlen;
    if (ny_ < 0.0f) {
        nx_ = -nx_;
        ny_ = -ny_;
        nz_ = -nz_;
    }
    nav_triangle_t *tri = &nm->source_triangles[nm->source_triangle_count++];
    tri->v[0] = a;
    tri->v[1] = b;
    tri->v[2] = c;
    tri->normal[0] = nx_;
    tri->normal[1] = ny_;
    tri->normal[2] = nz_;
    tri->centroid[0] = (p0[0] + p1[0] + p2[0]) / 3.0f;
    tri->centroid[1] = (p0[1] + p1[1] + p2[1]) / 3.0f;
    tri->centroid[2] = (p0[2] + p1[2] + p2[2]) / 3.0f;
    tri->neighbors[0] = tri->neighbors[1] = tri->neighbors[2] = -1;
    tri->blocked = blocked ? 1 : 0;
    tri->area_id = 0;
    tri->traversal_cost = 1.0f;
}

/// @brief From-scratch voxel navmesh baker (Recast-style, software, zero dependencies).
/// @details Replaces the slope-filter passthrough used by `Build()` for scene auto-bake. Steps:
///   (1) compute the XZ bounds of the world-space source geometry; (2) lay an XZ grid of
///   `cell_size`, coarsening the cell size if the grid would exceed a bounded budget; (3) rasterize
///   every slope-walkable source triangle into the grid, keeping the highest walkable surface
///   height per cell; (4) erode the walkable set by the agent radius (cells within `radius` of a
///   non-walkable cell or the grid edge are dropped, so corridors narrower than the agent are
///   removed); (5) emit a shared-corner grid mesh — one vertex per touched grid corner at the
///   averaged surface height, two triangles per surviving cell — into `source_triangles`; (6) run
///   the existing slope filter, which copies into `triangles`, builds edge adjacency, and refreshes
///   off-mesh links. The result's triangle count tracks the grid resolution, not the input mesh
///   density, and corridors honor the agent radius.
/// @return a NavMesh3D handle, or NULL on degenerate input / no walkable surface / allocation
///   failure.
/// @brief Rasterize slope-walkable source triangles into the voxel grid, keeping the highest
///        walkable surface height per cell. Fills @p cell_h / @p cell_walk (caller-allocated).
static void navmesh3d_voxel_rasterize(rt_mesh3d *src,
                                      double min_x,
                                      double min_z,
                                      double cell_size,
                                      int32_t nx,
                                      int32_t nz,
                                      double walkable_cos,
                                      float *cell_h,
                                      uint8_t *cell_walk) {
    for (uint32_t t = 0; t + 2 < src->index_count; t += 3) {
        uint32_t i0 = src->indices[t], i1 = src->indices[t + 1], i2 = src->indices[t + 2];
        if (i0 >= src->vertex_count || i1 >= src->vertex_count || i2 >= src->vertex_count)
            continue;
        const float *p0 = src->vertices[i0].pos;
        const float *p1 = src->vertices[i1].pos;
        const float *p2 = src->vertices[i2].pos;
        float e1x = p1[0] - p0[0], e1y = p1[1] - p0[1], e1z = p1[2] - p0[2];
        float e2x = p2[0] - p0[0], e2y = p2[1] - p0[1], e2z = p2[2] - p0[2];
        float fnx = e1y * e2z - e1z * e2y;
        float fny = e1z * e2x - e1x * e2z;
        float fnz = e1x * e2y - e1y * e2x;
        float flen = sqrtf(fnx * fnx + fny * fny + fnz * fnz);
        if (flen < 1e-8f)
            continue;
        if (fabsf(fny / flen) < (float)walkable_cos)
            continue; /* face too steep to walk */
        float tminx = fminf(p0[0], fminf(p1[0], p2[0]));
        float tmaxx = fmaxf(p0[0], fmaxf(p1[0], p2[0]));
        float tminz = fminf(p0[2], fminf(p1[2], p2[2]));
        float tmaxz = fmaxf(p0[2], fmaxf(p1[2], p2[2]));
        int cx0 = (int)((tminx - min_x) / cell_size);
        int cx1 = (int)((tmaxx - min_x) / cell_size);
        int cz0 = (int)((tminz - min_z) / cell_size);
        int cz1 = (int)((tmaxz - min_z) / cell_size);
        if (cx0 < 0)
            cx0 = 0;
        if (cz0 < 0)
            cz0 = 0;
        if (cx1 >= nx)
            cx1 = nx - 1;
        if (cz1 >= nz)
            cz1 = nz - 1;
        for (int cz = cz0; cz <= cz1; cz++) {
            for (int cx = cx0; cx <= cx1; cx++) {
                float px = (float)(min_x + ((double)cx + 0.5) * cell_size);
                float pz = (float)(min_z + ((double)cz + 0.5) * cell_size);
                if (!point_in_tri_xz(px, pz, p0, p1, p2))
                    continue;
                float y = triangle_y_at_xz(px, pz, p0, p1, p2);
                size_t idx = (size_t)cz * (size_t)nx + (size_t)cx;
                if (!cell_walk[idx] || y > cell_h[idx]) {
                    cell_h[idx] = y;
                    cell_walk[idx] = 1;
                }
            }
        }
    }
}

/// @brief Erode the walkable set by @p erode_r cells; grid edges count as boundaries so the
///        navmesh insets by the agent radius. @return new walkable buffer (caller owns), or NULL.
static uint8_t *navmesh3d_voxel_erode(const uint8_t *cell_walk, int32_t nx, int32_t nz, int erode_r) {
    size_t cell_count = (size_t)nx * (size_t)nz;
    uint8_t *eroded = (uint8_t *)malloc(cell_count);
    if (!eroded)
        return NULL;
    memcpy(eroded, cell_walk, cell_count);
    for (int32_t cz = 0; cz < nz; cz++) {
        for (int32_t cx = 0; cx < nx; cx++) {
            size_t idx = (size_t)cz * (size_t)nx + (size_t)cx;
            if (!cell_walk[idx])
                continue;
            int blocked = 0;
            for (int dz = -erode_r; dz <= erode_r && !blocked; dz++) {
                for (int dx = -erode_r; dx <= erode_r; dx++) {
                    int sx = cx + dx, sz = cz + dz;
                    if (sx < 0 || sz < 0 || sx >= nx || sz >= nz) {
                        blocked = 1;
                        break;
                    }
                    if (!cell_walk[(size_t)sz * (size_t)nx + (size_t)sx]) {
                        blocked = 1;
                        break;
                    }
                }
            }
            if (blocked)
                eroded[idx] = 0;
        }
    }
    return eroded;
}

/// @brief Emit two triangles per grid cell into @p nm, sharing corner vertices via @p corner_vid.
static void navmesh3d_voxel_emit_grid_mesh(rt_navmesh3d *nm,
                                           int32_t *corner_vid,
                                           float *cell_h,
                                           uint8_t *cell_walk,
                                           int32_t nx,
                                           int32_t nz,
                                           double min_x,
                                           double min_z,
                                           double cell_size) {
    for (int32_t cz = 0; cz < nz; cz++) {
        for (int32_t cx = 0; cx < nx; cx++) {
            size_t cell_idx = (size_t)cz * (size_t)nx + (size_t)cx;
            int8_t blocked = cell_walk[cell_idx] ? 0 : 1;
            int32_t a = navmesh3d_voxel_corner_vertex(
                nm, corner_vid, cell_h, cell_walk, nx, nz, min_x, min_z, cell_size, cx, cz);
            int32_t b = navmesh3d_voxel_corner_vertex(
                nm, corner_vid, cell_h, cell_walk, nx, nz, min_x, min_z, cell_size, cx + 1, cz);
            int32_t c = navmesh3d_voxel_corner_vertex(
                nm, corner_vid, cell_h, cell_walk, nx, nz, min_x, min_z, cell_size, cx + 1, cz + 1);
            int32_t d = navmesh3d_voxel_corner_vertex(
                nm, corner_vid, cell_h, cell_walk, nx, nz, min_x, min_z, cell_size, cx, cz + 1);
            navmesh3d_voxel_emit_tri(nm, a, c, b, blocked);
            navmesh3d_voxel_emit_tri(nm, a, d, c, blocked);
        }
    }
}

/// @brief Number of voxel cells spanning @p span at @p cell_size (floor(span/cell)+1).
/// @return 1 with @p out_dim set, or 0 if the result is non-finite or exceeds INT32_MAX.
static int navmesh3d_voxel_dim_for_span(double span, double cell_size, int64_t *out_dim) {
    if (!out_dim || !isfinite(span) || span < 0.0 || !isfinite(cell_size) || cell_size <= 0.0)
        return 0;
    double dim = floor(span / cell_size) + 1.0;
    if (!isfinite(dim) || dim < 1.0 || dim > (double)INT32_MAX)
        return 0;
    *out_dim = (int64_t)dim;
    return 1;
}

/// @brief Bake a navmesh from arbitrary source geometry via voxelization: rasterize the
///   triangles into an XZ heightfield at @p cell_size, keep cells walkable for an agent of
///   @p agent_radius / @p agent_height within @p max_slope, and emit the walkable nav polygons.
/// @return A new navmesh object, or NULL for empty/degenerate input.
static void *navmesh3d_voxel_bake(
    rt_mesh3d *src, double agent_radius, double agent_height, double max_slope, double cell_size) {
    if (!src || src->vertex_count == 0 || src->index_count < 3)
        return NULL;

    if (!isfinite(cell_size) || cell_size <= 0.0)
        cell_size = 0.5;
    double radius = (isfinite(agent_radius) && agent_radius > 0.0) ? agent_radius : 0.4;
    double slope = navmesh3d_sanitize_slope(max_slope);
    double walkable_cos = cos(slope * M_PI / 180.0);

    /* 1. XZ bounds of the world-space source geometry. */
    double min_x = DBL_MAX, min_z = DBL_MAX, max_x = -DBL_MAX, max_z = -DBL_MAX;
    for (uint32_t i = 0; i < src->vertex_count; i++) {
        double x = (double)src->vertices[i].pos[0];
        double z = (double)src->vertices[i].pos[2];
        if (x < min_x)
            min_x = x;
        if (x > max_x)
            max_x = x;
        if (z < min_z)
            min_z = z;
        if (z > max_z)
            max_z = z;
    }
    if (!(max_x >= min_x) || !(max_z >= min_z))
        return NULL;

    /* 2. Grid dimensions; coarsen cell_size until the grid fits a bounded budget. A navmesh is
     *    coarse relative to render geometry — an oversized grid makes both the adjacency build and
     *    per-frame nav queries pathologically slow — so a multi-km world collapses into a coarse
     *    single navmesh here. Fine-grained large-world nav is the tiled baker's job (R-NAV-002). */
    const int64_t MAX_DIM = 128;
    const int64_t MAX_CELLS = 16384;
    double span_x = max_x - min_x;
    double span_z = max_z - min_z;
    for (int guard = 0; guard < 64; guard++) {
        int64_t tnx = 0;
        int64_t tnz = 0;
        if (navmesh3d_voxel_dim_for_span(span_x, cell_size, &tnx) &&
            navmesh3d_voxel_dim_for_span(span_z, cell_size, &tnz) && tnx <= MAX_DIM &&
            tnz <= MAX_DIM && tnz > 0 && tnx <= MAX_CELLS / tnz)
            break;
        cell_size *= 2.0;
        if (!isfinite(cell_size))
            return NULL;
    }
    int64_t nx64 = 0;
    int64_t nz64 = 0;
    if (!navmesh3d_voxel_dim_for_span(span_x, cell_size, &nx64) ||
        !navmesh3d_voxel_dim_for_span(span_z, cell_size, &nz64))
        return NULL;
    int32_t nx = (int32_t)nx64;
    int32_t nz = (int32_t)nz64;
    if (nx < 1)
        nx = 1;
    if (nz < 1)
        nz = 1;

    if ((size_t)nx > SIZE_MAX / (size_t)nz)
        return NULL;
    size_t cell_count = (size_t)nx * (size_t)nz;
    if (cell_count > SIZE_MAX / sizeof(float))
        return NULL;
    float *cell_h = (float *)malloc(cell_count * sizeof(float));
    uint8_t *cell_walk = (uint8_t *)calloc(cell_count, 1);
    if (!cell_h || !cell_walk) {
        free(cell_h);
        free(cell_walk);
        return NULL;
    }

    /* 3. Rasterize slope-walkable surfaces into the grid, keeping the highest surface per cell. */
    navmesh3d_voxel_rasterize(src, min_x, min_z, cell_size, nx, nz, walkable_cos, cell_h, cell_walk);

    /* 4. Erode the walkable set by the agent radius; the grid edge counts as a boundary so the
     *    navmesh is inset by the radius and corridors narrower than 2*radius drop out. */
    int erode_r = (int)ceil(radius / cell_size);
    if (erode_r > 0) {
        uint8_t *eroded = navmesh3d_voxel_erode(cell_walk, nx, nz, erode_r);
        if (!eroded) {
            free(cell_h);
            free(cell_walk);
            return NULL;
        }
        free(cell_walk);
        cell_walk = eroded;
    }

    int64_t walk_cells = 0;
    for (size_t i = 0; i < cell_count; i++)
        if (cell_walk[i])
            walk_cells++;
    if (walk_cells == 0) {
        free(cell_h);
        free(cell_walk);
        return NULL;
    }

    /* 5. Emit a shared-corner grid mesh into source_triangles. */
    int32_t cnx = nx + 1, cnz = nz + 1;
    if (cnx <= 0 || cnz <= 0 || (size_t)cnx > SIZE_MAX / (size_t)cnz) {
        free(cell_h);
        free(cell_walk);
        return NULL;
    }
    size_t corner_count = (size_t)cnx * (size_t)cnz;
    if (corner_count > SIZE_MAX / sizeof(int32_t)) {
        free(cell_h);
        free(cell_walk);
        return NULL;
    }
    int32_t *corner_vid = (int32_t *)malloc(corner_count * sizeof(int32_t));
    if (!corner_vid) {
        free(cell_h);
        free(cell_walk);
        return NULL;
    }
    for (size_t i = 0; i < corner_count; i++)
        corner_vid[i] = -1;

    rt_navmesh3d *nm =
        (rt_navmesh3d *)rt_obj_new_i64(RT_G3D_NAVMESH3D_CLASS_ID, (int64_t)sizeof(rt_navmesh3d));
    if (!nm) {
        free(cell_h);
        free(cell_walk);
        free(corner_vid);
        return NULL;
    }
    memset(nm, 0, sizeof(*nm));
    nm->agent_radius = radius;
    nm->agent_height = (isfinite(agent_height) && agent_height > 0.0) ? agent_height : 1.8;
    nm->max_slope = slope;
    nm->skip_portal_width_gate = 1; /* clearance comes from the radius erosion in step 4 */
    rt_obj_set_finalizer(nm, navmesh3d_finalizer);

    int64_t tri_cap = (int64_t)cell_count * 2;
    if (corner_count > SIZE_MAX / sizeof(nav_vertex_t) ||
        (size_t)tri_cap > SIZE_MAX / sizeof(nav_triangle_t) ||
        cell_count > SIZE_MAX / sizeof(float) || corner_count > SIZE_MAX / sizeof(int32_t)) {
        free(cell_h);
        free(cell_walk);
        free(corner_vid);
        navmesh3d_free_partial(nm);
        return NULL;
    }
    nm->vertices = (nav_vertex_t *)malloc(corner_count * sizeof(nav_vertex_t));
    nm->source_triangles = (nav_triangle_t *)malloc((size_t)tri_cap * sizeof(nav_triangle_t));
    nm->triangles = (nav_triangle_t *)malloc((size_t)tri_cap * sizeof(nav_triangle_t));
    nm->voxel_cell_height = (float *)malloc(cell_count * sizeof(float));
    nm->voxel_cell_walkable = (uint8_t *)malloc(cell_count);
    nm->voxel_corner_vertices = (int32_t *)malloc(corner_count * sizeof(int32_t));
    if (!nm->vertices || !nm->source_triangles || !nm->triangles || !nm->voxel_cell_height ||
        !nm->voxel_cell_walkable || !nm->voxel_corner_vertices) {
        free(cell_h);
        free(cell_walk);
        free(corner_vid);
        navmesh3d_free_partial(nm);
        return NULL;
    }
    memcpy(nm->voxel_cell_height, cell_h, cell_count * sizeof(float));
    memcpy(nm->voxel_cell_walkable, cell_walk, cell_count);
    nm->voxel_min_x = min_x;
    nm->voxel_min_z = min_z;
    nm->voxel_cell_size = cell_size;
    nm->voxel_nx = nx;
    nm->voxel_nz = nz;
    nm->vertex_count = 0;
    nm->source_triangle_count = 0;
    nm->triangle_count = 0;

    navmesh3d_voxel_emit_grid_mesh(nm, corner_vid, cell_h, cell_walk, nx, nz, min_x, min_z, cell_size);
    memcpy(nm->voxel_corner_vertices, corner_vid, corner_count * sizeof(int32_t));

    free(cell_h);
    free(cell_walk);
    free(corner_vid);

    /* 6. Filter (keeps all up-facing surfaces), build edge adjacency, refresh off-mesh links. */
    if (!navmesh3d_apply_slope_filter(nm)) {
        navmesh3d_free_partial(nm);
        return NULL;
    }
    return nm;
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
/// @brief Test one candidate triangle for containment of (px,pz); update the best (min |dy|) match.
static void navmesh3d_find_tri_test(const rt_navmesh3d *nm,
                                    int32_t i,
                                    float px,
                                    float py,
                                    float pz,
                                    float max_dy,
                                    float *best_dy,
                                    int32_t *best) {
    const nav_triangle_t *t = &nm->triangles[i];
    if (t->blocked)
        return; /* carved out by an obstacle — not a valid point-location target */
    const float *v0 = nm->vertices[t->v[0]].position;
    const float *v1 = nm->vertices[t->v[1]].position;
    const float *v2 = nm->vertices[t->v[2]].position;
    if (point_in_tri_xz(px, pz, v0, v1, v2)) {
        float surface_y = triangle_y_at_xz(px, pz, v0, v1, v2);
        float dy = fabsf(py - surface_y);
        if (max_dy >= 0.0f && dy > max_dy)
            return;
        if (dy < *best_dy) {
            *best_dy = dy;
            *best = i;
        }
    }
}

/// @brief Find the walkable triangle under a point (XZ projection), nearest within @p max_dy in Y.
/// @details Uses the spatial query grid when built — a point lies in its own cell's triangle list,
///          so scanning one cell is exact — else falls back to a linear scan. @p max_dy < 0 ignores
///          vertical distance. Returns the triangle index, or -1 if none.
static int32_t find_tri_with_max_dy(
    const rt_navmesh3d *nm, float px, float py, float pz, float max_dy) {
    float best_dy = FLT_MAX;
    int32_t best = -1;
    /* Spatial-grid fast path: a point inside a triangle lies inside that triangle's XZ AABB, so the
     * triangle is registered in the point's cell — scanning that one cell is exact and matches the
     * linear scan. Points outside the grid bounds cannot lie on any triangle. */
    if (nm->qgrid_nx > 0 && nm->qgrid_starts && nm->qgrid_tris) {
        int cx = (int)(((double)px - nm->qgrid_min_x) * nm->qgrid_inv_cell);
        int cz = (int)(((double)pz - nm->qgrid_min_z) * nm->qgrid_inv_cell);
        if (cx < 0 || cz < 0 || cx >= nm->qgrid_nx || cz >= nm->qgrid_nz)
            return -1;
        size_t cell = (size_t)cz * (size_t)nm->qgrid_nx + (size_t)cx;
        for (int32_t k = nm->qgrid_starts[cell]; k < nm->qgrid_starts[cell + 1]; k++)
            navmesh3d_find_tri_test(nm, nm->qgrid_tris[k], px, py, pz, max_dy, &best_dy, &best);
        return best;
    }
    for (int32_t i = 0; i < nm->triangle_count; i++)
        navmesh3d_find_tri_test(nm, i, px, py, pz, max_dy, &best_dy, &best);
    return best;
}

/// @brief Find triangle containing point (projected onto XZ), ignoring vertical separation.
static int32_t find_tri(const rt_navmesh3d *nm, float px, float py, float pz) {
    return find_tri_with_max_dy(nm, px, py, pz, -1.0f);
}

/// @brief Test hook: verify the spatial query grid returns the same triangle as a linear scan.
/// @details Samples an N×N lattice over the navmesh bounds and checks each point's grid lookup
///          matches the brute-force result, guarding the grid acceleration against drift.
/// @return 1 if every sample agrees (or there is no grid), 0 on any mismatch.
int8_t rt_navmesh3d_check_query_grid_parity(void *obj) {
    rt_navmesh3d *nm = (rt_navmesh3d *)rt_g3d_checked_or_null(obj, RT_G3D_NAVMESH3D_CLASS_ID);
    float min_x, min_z, max_x, max_z, mid_y;
    const int N = 40;
    if (!nm)
        return 0;
    if (nm->triangle_count <= 0 || nm->qgrid_nx <= 0)
        return 1; /* no grid: nothing to diverge from the linear scan */
    min_x = min_z = FLT_MAX;
    max_x = max_z = -FLT_MAX;
    {
        float min_y = FLT_MAX, max_y = -FLT_MAX;
        for (int32_t i = 0; i < nm->vertex_count; i++) {
            const float *p = nm->vertices[i].position;
            if (p[0] < min_x)
                min_x = p[0];
            if (p[0] > max_x)
                max_x = p[0];
            if (p[1] < min_y)
                min_y = p[1];
            if (p[1] > max_y)
                max_y = p[1];
            if (p[2] < min_z)
                min_z = p[2];
            if (p[2] > max_z)
                max_z = p[2];
        }
        mid_y = 0.5f * (min_y + max_y);
    }
    /* Sample a lattice spanning the bounds (slightly padded so out-of-bounds points are covered).
     */
    for (int iz = 0; iz <= N; iz++) {
        for (int ix = 0; ix <= N; ix++) {
            float fx = (float)ix / (float)N;
            float fz = (float)iz / (float)N;
            float px = min_x + (max_x - min_x) * (fx * 1.1f - 0.05f);
            float pz = min_z + (max_z - min_z) * (fz * 1.1f - 0.05f);
            float best_dy = FLT_MAX;
            int32_t grid_hit = find_tri_with_max_dy(nm, px, mid_y, pz, -1.0f);
            int32_t lin_hit = -1;
            for (int32_t i = 0; i < nm->triangle_count; i++)
                navmesh3d_find_tri_test(nm, i, px, mid_y, pz, -1.0f, &best_dy, &lin_hit);
            if (grid_hit != lin_hit)
                return 0;
        }
    }
    return 1;
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
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    return isfinite(dist) ? dist : FLT_MAX;
}

/// @brief A* edge cost between adjacent triangles @p from and @p to: their centroid distance
///   scaled by the average of their traversal costs; FLT_MAX if non-finite (impassable).
static float navmesh3d_edge_cost(const rt_navmesh3d *nm, int32_t from, int32_t to) {
    float base = centroid_dist(nm, from, to);
    if (!isfinite(base) || base >= FLT_MAX)
        return FLT_MAX;
    float cost = 0.5f * (navmesh3d_tri_cost(nm, from) + navmesh3d_tri_cost(nm, to));
    float total = base * cost;
    return isfinite(total) ? total : FLT_MAX;
}

/// @brief Traversal cost of an off-mesh link: its 3D length scaled by its sanitized traversal
///   cost; FLT_MAX if non-finite.
static float navmesh3d_offmesh_cost(const nav_offmesh_link_t *link) {
    if (!link)
        return 0.0f;
    float dx = link->to[0] - link->from[0];
    float dy = link->to[1] - link->from[1];
    float dz = link->to[2] - link->from[2];
    float base = sqrtf(dx * dx + dy * dy + dz * dz);
    if (!isfinite(base))
        return FLT_MAX;
    float total = base * navmesh3d_sanitize_traversal_cost((double)link->traversal_cost);
    return isfinite(total) ? total : FLT_MAX;
}

/// @brief Internal: compute the path from `from_v` to `to_v` and copy the waypoints into a
/// freshly malloc'd `*out_points_xyz` (interleaved x,y,z). Returns the point count, or 0 on
/// failure. Caller frees `*out_points_xyz`.
/// @brief A* search over the navmesh triangle graph (edges + off-mesh links).
/// @details Caller owns the @p g_cost / @p parent / @p closed / @p heap buffers; this fills
///          @p parent and @p g_cost. @return 1 if @p goal was reached, 0 otherwise.
static int navmesh3d_astar_search(rt_navmesh3d *nm,
                                  int32_t start,
                                  int32_t goal,
                                  float *g_cost,
                                  int32_t *parent,
                                  int8_t *closed,
                                  heap_entry_t *heap,
                                  int32_t heap_cap) {
    int32_t tc = nm->triangle_count;
    int32_t heap_size = 0;
    g_cost[start] = 0;
    heap_push(heap, &heap_size, heap_cap, start, centroid_dist(nm, start, goal));
    while (heap_size > 0) {
        int32_t cur = heap_pop(heap, &heap_size);
        if (cur == goal)
            return 1;
        if (closed[cur])
            continue;
        closed[cur] = 1;

        for (int e = 0; e < 3; e++) {
            int32_t next = nm->triangles[cur].neighbors[e];
            if (next < 0 || closed[next] || nm->triangles[next].blocked)
                continue;

            float new_g = g_cost[cur] + navmesh3d_edge_cost(nm, cur, next);
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
            if (next < 0 || next >= tc || closed[next] || nm->triangles[next].blocked)
                continue;

            float new_g = g_cost[cur] + navmesh3d_offmesh_cost(link);
            if (new_g < g_cost[next]) {
                g_cost[next] = new_g;
                parent[next] = cur;
                heap_push(heap, &heap_size, heap_cap, next, new_g + centroid_dist(nm, next, goal));
            }
        }
    }
    return 0;
}

/// @brief Emit the funnel waypoint(s) for one triangle transition @p ti -> @p tn, advancing @p apex.
/// @details Prefers the shared-edge portal midpoint; falls back to an off-mesh link's endpoints,
///          then to the destination triangle's centroid. Appends into @p points / @p point_count.
static void navmesh3d_pull_segment(rt_navmesh3d *nm,
                                   int32_t ti,
                                   int32_t tn,
                                   float apex[3],
                                   double *points,
                                   int64_t *point_count) {
    /* Find the shared edge (portal) between consecutive triangles */
    nav_triangle_t *ta = &nm->triangles[ti];
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
                points[*point_count * 3 + 0] = mx;
                points[*point_count * 3 + 1] = my;
                points[*point_count * 3 + 2] = mz;
                (*point_count)++;
                apex[0] = mx;
                apex[1] = my;
                apex[2] = mz;
            }
            return;
        }
    }
    int reverse = 0;
    const nav_offmesh_link_t *link = navmesh3d_find_offmesh_link(nm, ti, tn, &reverse);
    if (link) {
        const float *a = reverse ? link->to : link->from;
        const float *b = reverse ? link->from : link->to;
        float dax = a[0] - apex[0];
        float day = a[1] - apex[1];
        float daz = a[2] - apex[2];
        if (dax * dax + day * day + daz * daz > 0.01f) {
            points[*point_count * 3 + 0] = a[0];
            points[*point_count * 3 + 1] = a[1];
            points[*point_count * 3 + 2] = a[2];
            (*point_count)++;
        }
        points[*point_count * 3 + 0] = b[0];
        points[*point_count * 3 + 1] = b[1];
        points[*point_count * 3 + 2] = b[2];
        (*point_count)++;
        apex[0] = b[0];
        apex[1] = b[1];
        apex[2] = b[2];
    } else {
        /* Fallback to centroid if shared edge not found */
        float *cen = nm->triangles[tn].centroid;
        points[*point_count * 3 + 0] = cen[0];
        points[*point_count * 3 + 1] = cen[1];
        points[*point_count * 3 + 2] = cen[2];
        (*point_count)++;
        apex[0] = cen[0];
        apex[1] = cen[1];
        apex[2] = cen[2];
    }
}

/// @brief Reconstruct the smoothed waypoint list from an A* @p parent chain ending at @p goal.
/// @return Newly malloc'd points buffer (caller owns) with @p out_point_count entries, or NULL on
///         allocation failure (then @p out_point_count is 0).
static double *navmesh3d_reconstruct_path(rt_navmesh3d *nm,
                                          const int32_t *parent,
                                          int32_t goal,
                                          float fx,
                                          float fy,
                                          float fz,
                                          double fdx,
                                          double fdy,
                                          double fdz,
                                          double tdx,
                                          double tdy,
                                          double tdz,
                                          int64_t *out_point_count) {
    int64_t point_count = 0;
    int32_t count = 0;
    double *points;
    int32_t *seq;
    int32_t idx;
    int32_t max_points;

    *out_point_count = 0;
    for (int32_t c = goal; c != -1; c = parent[c])
        count++;

    seq = (int32_t *)malloc((size_t)count * sizeof(int32_t));
    if (!seq)
        return NULL;
    idx = count - 1;
    for (int32_t c = goal; c != -1; c = parent[c])
        seq[idx--] = c;

    if (count > (INT32_MAX - 2) / 3) {
        free(seq);
        return NULL;
    }
    max_points = count * 3 + 2;
    points = (double *)malloc((size_t)max_points * 3u * sizeof(double));
    if (!points) {
        free(seq);
        return NULL;
    }
    points[point_count * 3 + 0] = fdx;
    points[point_count * 3 + 1] = fdy;
    points[point_count * 3 + 2] = fdz;
    point_count++;

    /* Simple string-pulling: find portals between adjacent triangles and
     * walk the funnel to produce smooth waypoints. If portal extraction
     * fails for any edge, fall back to the centroid for that segment. */
    if (count >= 2) {
        float apex[3] = {fx, fy, fz};
        for (int32_t i = 0; i < count - 1; i++)
            navmesh3d_pull_segment(nm, seq[i], seq[i + 1], apex, points, &point_count);
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
    *out_point_count = point_count;
    return points;
}

/// @brief Compute a path from @p from to @p to and copy it into a freshly malloc'd flat xyz
///   array. @return Point count (the caller frees @p out_points_xyz).
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
    nm->last_path_cost = 0.0;

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
        double dx = tdx - fdx, dy = tdy - fdy, dz = tdz - fdz;
        nm->last_path_cost =
            sqrt(dx * dx + dy * dy + dz * dz) * (double)navmesh3d_tri_cost(nm, start);
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

    if (navmesh3d_astar_search(nm, start, goal, g_cost, parent, closed, heap, heap_cap)) {
        nm->last_path_cost = (double)g_cost[goal];
        points = navmesh3d_reconstruct_path(
            nm, parent, goal, fx, fy, fz, fdx, fdy, fdz, tdx, tdy, tdz, &point_count);
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
        if (t->blocked)
            continue; /* don't snap onto a carved-out triangle */
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
    if (!nm || !nm->triangles)
        return 0;
    /* Public count is the walkable (non-carved) set; blocked triangles linger in the array only
     * so per-tile rebuilds can flip them back without recompacting. */
    int64_t walkable = 0;
    for (int32_t i = 0; i < nm->triangle_count; i++)
        if (!nm->triangles[i].blocked)
            walkable++;
    return walkable;
}

/// @brief Total accumulated cost of the most recent successful path query, or 0 if none has run.
double rt_navmesh3d_get_last_path_cost(void *obj) {
    rt_navmesh3d *nm = (rt_navmesh3d *)rt_g3d_checked_or_null(obj, RT_G3D_NAVMESH3D_CLASS_ID);
    return nm ? nm->last_path_cost : 0.0;
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
    link->state_flags = 0;
    link->traversal_cost = 1.0f;
    link->kind = NULL;
    return 1;
}

/// @brief Attach kind/cost/state metadata to an authored off-mesh link by index.
int8_t rt_navmesh3d_set_offmesh_link_metadata(
    void *obj, int64_t index, rt_string kind, double traversal_cost, int64_t state_flags) {
    rt_navmesh3d *nm = (rt_navmesh3d *)rt_g3d_checked_or_null(obj, RT_G3D_NAVMESH3D_CLASS_ID);
    if (!nm || index < 0 || index >= nm->offmesh_link_count || !kind || !rt_string_is_handle(kind))
        return 0;
    nav_offmesh_link_t *link = &nm->offmesh_links[(int32_t)index];
    if (link->kind) {
        rt_string_unref(link->kind);
        link->kind = NULL;
    }
    const char *kind_bytes = rt_string_cstr(kind);
    if (kind_bytes && kind_bytes[0] != '\0')
        link->kind = rt_string_ref(kind);
    link->traversal_cost = navmesh3d_sanitize_traversal_cost(traversal_cost);
    link->state_flags = state_flags;
    return 1;
}

/// @brief Read the kind-string metadata of an authored off-mesh link by index.
rt_string rt_navmesh3d_get_offmesh_link_kind(void *obj, int64_t index) {
    rt_navmesh3d *nm = (rt_navmesh3d *)rt_g3d_checked_or_null(obj, RT_G3D_NAVMESH3D_CLASS_ID);
    if (!nm || index < 0 || index >= nm->offmesh_link_count)
        return rt_const_cstr("");
    nav_offmesh_link_t *link = &nm->offmesh_links[(int32_t)index];
    return link->kind ? rt_string_ref(link->kind) : rt_const_cstr("");
}

/// @brief Read the sanitized pathfinding traversal cost of an authored off-mesh link by index
///   (0 for an out-of-range index).
double rt_navmesh3d_get_offmesh_link_traversal_cost(void *obj, int64_t index) {
    rt_navmesh3d *nm = (rt_navmesh3d *)rt_g3d_checked_or_null(obj, RT_G3D_NAVMESH3D_CLASS_ID);
    if (!nm || index < 0 || index >= nm->offmesh_link_count)
        return 0.0;
    return (double)navmesh3d_sanitize_traversal_cost(
        (double)nm->offmesh_links[(int32_t)index].traversal_cost);
}

/// @brief Read the state-flag metadata of an authored off-mesh link by index.
int64_t rt_navmesh3d_get_offmesh_link_state(void *obj, int64_t index) {
    rt_navmesh3d *nm = (rt_navmesh3d *)rt_g3d_checked_or_null(obj, RT_G3D_NAVMESH3D_CLASS_ID);
    if (!nm || index < 0 || index >= nm->offmesh_link_count)
        return 0;
    return nm->offmesh_links[(int32_t)index].state_flags;
}

/// @brief Read and sanitize an obstacle AABB from two Vec3 handles into float min/max arrays.
/// @details Rejects non-Vec3 or non-finite inputs, orders each axis (min<=max), and clamps to the
///          float range. Returns 1 on success, 0 if the bounds are unusable.
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

/// @brief Whether obstacles can be edited: the navmesh must retain its unfiltered source triangles.
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
    /* Tiled bake: re-carve only the tiles the obstacle AABB overlaps. The triangle set, adjacency,
     * and query grid are untouched — only the in-tile `blocked` flags flip. O(overlapped tiles). */
    if (nm->tile_size > 0.0 && nm->qgrid_nx > 0) {
        navmesh3d_reflag_obstacle_tiles(nm, omin, omax);
        return 1;
    }
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
    if (nm->tile_size > 0.0 && nm->qgrid_nx > 0) {
        navmesh3d_reflag_obstacle_tiles(nm, removed.min, removed.max);
        return 1;
    }
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
    if (nm->tile_size > 0.0 && nm->qgrid_nx > 0) {
        navmesh3d_reflag_obstacle_tiles(nm, old_obstacle.min, old_obstacle.max);
        navmesh3d_reflag_obstacle_tiles(nm, omin, omax);
        return 1;
    }
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

/// @brief Assign nav-area and traversal-cost metadata to polygons overlapping an AABB volume.
int8_t rt_navmesh3d_set_area(
    void *obj, void *min_v, void *max_v, rt_string area, double traversal_cost) {
    rt_navmesh3d *nm = (rt_navmesh3d *)rt_g3d_checked_or_null(obj, RT_G3D_NAVMESH3D_CLASS_ID);
    if (!nm || !nm->source_triangles || !nm->triangles || !area || !rt_string_is_handle(area))
        return 0;
    float min[3];
    float max[3];
    if (!navmesh3d_read_obstacle_bounds(min_v, max_v, min, max))
        return 0;
    int32_t area_id = navmesh3d_ensure_area_id(nm, area);
    if (area_id < 0)
        return 0;
    float cost = navmesh3d_sanitize_traversal_cost(traversal_cost);
    int32_t touched = 0;
    for (int32_t i = 0; i < nm->source_triangle_count; i++) {
        nav_triangle_t *tri = &nm->source_triangles[i];
        if (!navmesh3d_triangle_overlaps_bounds(nm, tri, min, max))
            continue;
        tri->area_id = area_id;
        tri->traversal_cost = cost;
        touched++;
    }
    for (int32_t i = 0; i < nm->triangle_count; i++) {
        nav_triangle_t *tri = &nm->triangles[i];
        if (!navmesh3d_triangle_overlaps_bounds(nm, tri, min, max))
            continue;
        tri->area_id = area_id;
        tri->traversal_cost = cost;
    }
    return touched > 0 ? 1 : 0;
}

/// @brief Read the nav-area name metadata at a walkable position.
rt_string rt_navmesh3d_get_area(void *obj, void *point) {
    if (!obj || !rt_g3d_is_vec3(point))
        return rt_const_cstr("");
    rt_navmesh3d *nm = (rt_navmesh3d *)rt_g3d_checked_or_null(obj, RT_G3D_NAVMESH3D_CLASS_ID);
    if (!nm || !nm->triangles || !nm->vertices)
        return rt_const_cstr("");
    double pdx = rt_vec3_x(point), pdy = rt_vec3_y(point), pdz = rt_vec3_z(point);
    if (!isfinite(pdx) || !isfinite(pdy) || !isfinite(pdz))
        return rt_const_cstr("");
    int32_t tri = find_tri_with_max_dy(
        nm, (float)pdx, (float)pdy, (float)pdz, navmesh3d_vertical_tolerance(nm));
    if (tri < 0)
        return rt_const_cstr("");
    return navmesh3d_area_name(nm, nm->triangles[tri].area_id);
}

/// @brief Sample the navmesh traversal cost at world @p point — resolving the triangle directly
///   at/below it (via vertical proximity, so stacked surfaces disambiguate) and returning that
///   triangle's area-weighted cost. Returns 0 when @p point is off-mesh, non-finite, or invalid.
double rt_navmesh3d_get_traversal_cost(void *obj, void *point) {
    if (!obj || !rt_g3d_is_vec3(point))
        return 0.0;
    rt_navmesh3d *nm = (rt_navmesh3d *)rt_g3d_checked_or_null(obj, RT_G3D_NAVMESH3D_CLASS_ID);
    if (!nm || !nm->triangles || !nm->vertices)
        return 0.0;
    double pdx = rt_vec3_x(point), pdy = rt_vec3_y(point), pdz = rt_vec3_z(point);
    if (!isfinite(pdx) || !isfinite(pdy) || !isfinite(pdz))
        return 0.0;
    int32_t tri = find_tri_with_max_dy(
        nm, (float)pdx, (float)pdy, (float)pdz, navmesh3d_vertical_tolerance(nm));
    if (tri < 0)
        return 0.0;
    return (double)navmesh3d_tri_cost(nm, tri);
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
    rt_navmesh3d *nm = (rt_navmesh3d *)rt_g3d_checked_or_null(obj, RT_G3D_NAVMESH3D_CLASS_ID);
    if (!nm || !nm->triangles || nm->triangle_count <= 0)
        return 0;
    /* Tiled voxel bake: rebuild just this tile's retained voxel source in place (O(tile)) — no
     * whole-scene voxel pass, adjacency rebuild, or query-grid rebuild. */
    if (nm->tile_size > 0.0 && nm->qgrid_nx > 0) {
        if (navmesh3d_has_voxel_source(nm))
            navmesh3d_rebuild_voxel_tile(nm, tile_x, tile_z);
        else {
            navmesh3d_reflag_tile(nm, tile_x, tile_z);
            navmesh3d_refresh_offmesh_links(nm);
        }
        return 1;
    }
    /* Non-tiled mesh (Build / untiled bake): preserve the whole-mesh refilter baseline. */
    if (!nm->source_triangles || nm->source_triangle_count <= 0)
        return 0;
    (void)tile_x;
    (void)tile_z;
    return navmesh3d_apply_slope_filter(nm) ? 1 : 0;
}

/// @brief Test-only: append an obstacle WITHOUT re-flagging any triangle.
/// @details Used by unit tests to create a "stale" tiled navmesh — the obstacle exists but no tile
/// has been re-carved yet — so a subsequent `RebuildTile` can be observed to affect exactly one
/// tile. Not part of the scripting surface (classified internal in RuntimeSurfacePolicy.inc).
int8_t rt_navmesh3d_test_inject_obstacle(void *obj, void *min_v, void *max_v) {
    rt_navmesh3d *nm = (rt_navmesh3d *)rt_g3d_checked_or_null(obj, RT_G3D_NAVMESH3D_CLASS_ID);
    if (!navmesh3d_can_edit_obstacles(nm))
        return 0;
    float omin[3];
    float omax[3];
    if (!navmesh3d_read_obstacle_bounds(min_v, max_v, omin, omax))
        return 0;
    if (nm->obstacle_count >= nm->obstacle_capacity) {
        int32_t new_cap = nm->obstacle_capacity > 0 ? nm->obstacle_capacity * 2 : 4;
        if (nm->obstacle_capacity > INT32_MAX / 2 ||
            (size_t)new_cap > SIZE_MAX / sizeof(nav_obstacle_t))
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
    return 1;
}

/// @brief Test-only: map a world XZ position to its tile coords (see navmesh3d_tile_of_point).
/// Returns 0 if the mesh is not tiled. Not part of the scripting surface.
int8_t rt_navmesh3d_test_tile_of_point(
    void *obj, double px, double pz, int64_t *out_tx, int64_t *out_tz) {
    rt_navmesh3d *nm = (rt_navmesh3d *)rt_g3d_checked_or_null(obj, RT_G3D_NAVMESH3D_CLASS_ID);
    return (int8_t)(navmesh3d_tile_of_point(nm, px, pz, out_tx, out_tz) ? 1 : 0);
}

/// @brief Test-only: edit retained voxel source for a tile without touching the live mesh.
/// @details This simulates a tile-local geometry source change. A far-tile RebuildTile must leave
/// the live mesh stale; rebuilding this tile must refresh its geometry/blocking from the retained
/// source. Not part of the scripting surface.
int8_t rt_navmesh3d_test_set_tile_source(
    void *obj, int64_t tile_x, int64_t tile_z, double height, int8_t walkable) {
    rt_navmesh3d *nm = (rt_navmesh3d *)rt_g3d_checked_or_null(obj, RT_G3D_NAVMESH3D_CLASS_ID);
    int32_t cx0, cx1, cz0, cz1;
    if (!navmesh3d_voxel_tile_cell_range(nm, tile_x, tile_z, &cx0, &cx1, &cz0, &cz1) ||
        !isfinite(height))
        return 0;
    if (height > (double)FLT_MAX)
        height = (double)FLT_MAX;
    if (height < -(double)FLT_MAX)
        height = -(double)FLT_MAX;
    for (int32_t cz = cz0; cz <= cz1; cz++) {
        for (int32_t cx = cx0; cx <= cx1; cx++) {
            size_t cell = navmesh3d_voxel_cell_index(nm, cx, cz);
            if (cell == SIZE_MAX)
                continue;
            nm->voxel_cell_height[cell] = (float)height;
            nm->voxel_cell_walkable[cell] = walkable ? 1 : 0;
        }
    }
    return 1;
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
        if (tri->blocked)
            continue; /* carved out — not part of the walkable surface */
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
