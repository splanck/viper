//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/render/rt_canvas3d_clusters.c
// Purpose: CPU froxel binning for clustered forward+ lighting (Plan 07).
//   Sorts the flattened light array (directional/ambient prefix, then
//   point/spot) and conservatively rasterizes each local light's bounding
//   sphere into a 16x9x24 view froxel grid so fragment shaders loop only
//   their cluster's lights.
// Key invariants:
//   - Binning is deterministic: lights bin in flattened-array order and
//     per-cluster truncation on overflow is order-stable (never UB).
//   - Conservative over-inclusion only: a light whose attenuated
//     contribution is non-negligible at a point always appears in that
//     point's cluster list (the parity guarantee); the reverse is allowed.
//   - Screen extents come from projecting the sphere's AABB corners through
//     the cached render VP; any corner behind the eye degrades that light to
//     full-screen X/Y coverage rather than risking under-inclusion.
//   - Z slices are exponential over [znear, zfar]:
//     slice = floor(DIM_Z * log(d/znear) / log(zfar/znear)).
// Ownership/Lifetime:
//   - Tables are plain POD owned by the canvas's revision-keyed ring; this
//     file allocates nothing.
// Links: vgfx3d_backend.h (table layout), rt_canvas3d_lighting.c (flatten +
//   revision stamps), misc/plans/3d_overhaul/07-clustered-lighting.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_canvas3d_clusters.h"

#include <math.h>
#include <string.h>

/// @brief Attenuated contribution threshold below which a light is ignored (1/255).
#define CLUSTER_LIGHT_EPSILON (1.0f / 255.0f)

/// @brief Compute the world-space influence radius of a point/spot light.
/// @details Solves `intensity / (1 + k*d^2) < eps` for d: beyond the returned
///          radius the light contributes less than one 8-bit step. Zero (or
///          denormal) attenuation never falls off — return a negative sentinel
///          meaning "unbounded" so the caller bins it into every cluster.
float canvas3d_cluster_light_radius(float intensity, float attenuation) {
    float ratio;
    if (!isfinite(intensity) || intensity <= 0.0f)
        return 0.0f; /* contributes nothing anywhere */
    if (!isfinite(attenuation) || attenuation <= 1e-9f)
        return -1.0f; /* unbounded: no distance falloff */
    ratio = intensity / CLUSTER_LIGHT_EPSILON - 1.0f;
    if (ratio <= 0.0f)
        return 0.0f;
    return sqrtf(ratio / attenuation);
}

/// @brief Map a view depth to its exponential Z slice (clamped to the grid).
int32_t canvas3d_cluster_z_slice(float depth, float znear, float zfar) {
    float t;
    int32_t slice;
    if (!(znear > 0.0f) || !(zfar > znear))
        return 0;
    if (!isfinite(depth) || depth <= znear)
        return 0;
    if (depth >= zfar)
        return VGFX3D_CLUSTER_DIM_Z - 1;
    t = logf(depth / znear) / logf(zfar / znear);
    slice = (int32_t)(t * (float)VGFX3D_CLUSTER_DIM_Z);
    if (slice < 0)
        slice = 0;
    if (slice >= VGFX3D_CLUSTER_DIM_Z)
        slice = VGFX3D_CLUSTER_DIM_Z - 1;
    return slice;
}

/// @brief Project one world point through a row-major VP into NDC.
/// @return 0 when the point is at/behind the eye plane (w <= epsilon).
static int cluster_project_point(const float *vp, const float p[3], float *ndc_x, float *ndc_y) {
    float x = vp[0] * p[0] + vp[1] * p[1] + vp[2] * p[2] + vp[3];
    float y = vp[4] * p[0] + vp[5] * p[1] + vp[6] * p[2] + vp[7];
    float w = vp[12] * p[0] + vp[13] * p[1] + vp[14] * p[2] + vp[15];
    if (!isfinite(w) || w <= 1e-6f)
        return 0;
    *ndc_x = x / w;
    *ndc_y = y / w;
    return isfinite(*ndc_x) && isfinite(*ndc_y);
}

/// @brief Conservative NDC bounding rect of a world sphere via its AABB corners.
/// @return 0 when any corner projects behind the eye (caller must assume full screen).
static int cluster_sphere_ndc_bounds(const float *vp,
                                     const float center[3],
                                     float radius,
                                     float *min_x,
                                     float *min_y,
                                     float *max_x,
                                     float *max_y) {
    float lo_x = 1e30f;
    float lo_y = 1e30f;
    float hi_x = -1e30f;
    float hi_y = -1e30f;
    for (int corner = 0; corner < 8; corner++) {
        float p[3];
        float nx;
        float ny;
        p[0] = center[0] + ((corner & 1) ? radius : -radius);
        p[1] = center[1] + ((corner & 2) ? radius : -radius);
        p[2] = center[2] + ((corner & 4) ? radius : -radius);
        if (!cluster_project_point(vp, p, &nx, &ny))
            return 0;
        if (nx < lo_x)
            lo_x = nx;
        if (ny < lo_y)
            lo_y = ny;
        if (nx > hi_x)
            hi_x = nx;
        if (ny > hi_y)
            hi_y = ny;
    }
    *min_x = lo_x;
    *min_y = lo_y;
    *max_x = hi_x;
    *max_y = hi_y;
    return 1;
}

/// @brief Clamp an NDC coordinate onto a cluster axis index (floor semantics).
/// @details Floor is correct for both edges of an inclusive [min, max] cluster
///          range: the min edge's cluster starts at or before it and the max
///          edge's cluster ends after it. @p toward_max only picks the clamp
///          side for non-finite input.
static int32_t cluster_axis_from_ndc(float ndc, int32_t dim, int toward_max) {
    float t = ndc * 0.5f + 0.5f;
    int32_t idx;
    if (!isfinite(t))
        return toward_max ? dim - 1 : 0;
    idx = (int32_t)floorf(t * (float)dim);
    if (idx < 0)
        idx = 0;
    if (idx >= dim)
        idx = dim - 1;
    return idx;
}

/// @brief One binned light's cluster ranges (inclusive).
typedef struct {
    int32_t x0, x1;
    int32_t y0, y1;
    int32_t z0, z1;
} cluster_range_t;

/// @brief Compute the conservative cluster ranges covered by one point/spot light.
static void cluster_range_for_light(const rt_canvas3d *c,
                                    const vgfx3d_light_params_t *light,
                                    float znear,
                                    float zfar,
                                    cluster_range_t *out) {
    float radius = canvas3d_cluster_light_radius(light->intensity, light->attenuation);
    float depth;
    float min_x;
    float min_y;
    float max_x;
    float max_y;

    out->x0 = 0;
    out->x1 = VGFX3D_CLUSTER_DIM_X - 1;
    out->y0 = 0;
    out->y1 = VGFX3D_CLUSTER_DIM_Y - 1;
    out->z0 = 0;
    out->z1 = VGFX3D_CLUSTER_DIM_Z - 1;

    if (radius == 0.0f) {
        /* Contributes nothing: empty range. */
        out->x1 = -1;
        return;
    }
    if (radius < 0.0f)
        return; /* unbounded: every cluster */
    if (radius > zfar)
        radius = zfar; /* beyond the far plane the grid is fully covered anyway */

    /* Z range from the view-depth extent of the sphere. */
    depth = (light->position[0] - c->cached_cam_pos[0]) * c->cached_cam_forward[0] +
            (light->position[1] - c->cached_cam_pos[1]) * c->cached_cam_forward[1] +
            (light->position[2] - c->cached_cam_pos[2]) * c->cached_cam_forward[2];
    if (!isfinite(depth))
        return; /* keep full coverage */
    if (depth + radius < znear || depth - radius > zfar) {
        out->x1 = -1; /* entirely outside the depth range */
        return;
    }
    out->z0 = canvas3d_cluster_z_slice(depth - radius, znear, zfar);
    out->z1 = canvas3d_cluster_z_slice(depth + radius, znear, zfar);

    /* XY range from the projected AABB; behind-the-eye corners keep full XY. */
    if (cluster_sphere_ndc_bounds(
            c->cached_vp, light->position, radius, &min_x, &min_y, &max_x, &max_y)) {
        if (max_x < -1.0f || min_x > 1.0f || max_y < -1.0f || min_y > 1.0f) {
            out->x1 = -1; /* fully off-screen */
            return;
        }
        out->x0 = cluster_axis_from_ndc(min_x, VGFX3D_CLUSTER_DIM_X, 0);
        out->x1 = cluster_axis_from_ndc(max_x, VGFX3D_CLUSTER_DIM_X, 1);
        /* NDC +Y is up but cluster rows follow screen space (row 0 at the top),
         * so the Y interval flips. */
        out->y0 = cluster_axis_from_ndc(-max_y, VGFX3D_CLUSTER_DIM_Y, 0);
        out->y1 = cluster_axis_from_ndc(-min_y, VGFX3D_CLUSTER_DIM_Y, 1);
    }
}

/// @brief Sort-order key: directional(0)/ambient(2) lights precede point(1)/spot(3).
int canvas3d_cluster_light_is_global(int32_t type) {
    return type == 0 || type == 2;
}

/// @brief Build the froxel light table for a flattened (globals-first) light array.
/// @details @p lights must already be ordered with the directional/ambient prefix
///          (see build_light_params). Uses the canvas's cached render-space camera
///          state, so call only between Begin and End of a 3D frame. Never fails;
///          overflow truncates per cluster and is counted in `overflow_count`.
void canvas3d_build_cluster_table(const rt_canvas3d *c,
                                  const vgfx3d_light_params_t *lights,
                                  int32_t light_count,
                                  uint32_t lights_revision,
                                  vgfx3d_cluster_table_t *out) {
    /* Per-cluster counts reused as write cursors during the fill pass. */
    static const int32_t plane = VGFX3D_CLUSTER_DIM_X * VGFX3D_CLUSTER_DIM_Y;
    uint16_t counts[VGFX3D_CLUSTER_COUNT];
    cluster_range_t ranges[VGFX3D_MAX_LIGHTS];
    float znear;
    float zfar;
    int32_t global_count = 0;
    int32_t total = 0;

    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    out->lights_revision = lights_revision;
    if (!c || !lights || light_count <= 0)
        return;
    if (light_count > VGFX3D_MAX_LIGHTS)
        light_count = VGFX3D_MAX_LIGHTS;

    znear = c->cached_cam_near > 1e-4f ? c->cached_cam_near : 1e-4f;
    zfar = c->cached_cam_far > znear * (1.0f + 1e-3f) ? c->cached_cam_far : znear * 1000.0f;
    out->znear = znear;
    out->zfar = zfar;

    while (global_count < light_count &&
           canvas3d_cluster_light_is_global(lights[global_count].type))
        global_count++;
    out->global_light_count = global_count;
    out->binned_light_count = light_count - global_count;

    memset(counts, 0, sizeof(counts));
    for (int32_t i = global_count; i < light_count; i++) {
        cluster_range_t *r = &ranges[i];
        cluster_range_for_light(c, &lights[i], znear, zfar, r);
        for (int32_t z = r->z0; z <= r->z1; z++)
            for (int32_t y = r->y0; y <= r->y1; y++)
                for (int32_t x = r->x0; x <= r->x1; x++)
                    counts[x + y * VGFX3D_CLUSTER_DIM_X + z * plane]++;
    }

    /* Prefix sums with a hard cap: clusters past the cap truncate their tail
     * entries deterministically (later lights in flattened order drop first). */
    {
        int32_t running = 0;
        /* E11: configurable per-cluster capacity on top of the shared pool. */
        int32_t per_cluster = c->cluster_light_budget;
        if (per_cluster < 8 || per_cluster > VGFX3D_MAX_LIGHTS)
            per_cluster = VGFX3D_MAX_LIGHTS;
        for (int32_t cidx = 0; cidx < VGFX3D_CLUSTER_COUNT; cidx++) {
            int32_t want = counts[cidx];
            int32_t room = VGFX3D_MAX_CLUSTER_LIGHT_INDICES - running;
            if (room > per_cluster)
                room = per_cluster;
            int32_t give = want < room ? want : (room > 0 ? room : 0);
            out->offsets[cidx] = (uint16_t)running;
            out->overflow_count += want - give;
            counts[cidx] = (uint16_t)give; /* becomes remaining capacity below */
            running += give;
        }
        out->offsets[VGFX3D_CLUSTER_COUNT] = (uint16_t)running;
        total = running;
    }

    /* Fill pass: same iteration order as the count pass, honoring capacity. */
    {
        uint16_t cursor[VGFX3D_CLUSTER_COUNT];
        memset(cursor, 0, sizeof(cursor));
        for (int32_t i = global_count; i < light_count; i++) {
            const cluster_range_t *r = &ranges[i];
            for (int32_t z = r->z0; z <= r->z1; z++)
                for (int32_t y = r->y0; y <= r->y1; y++)
                    for (int32_t x = r->x0; x <= r->x1; x++) {
                        int32_t cidx = x + y * VGFX3D_CLUSTER_DIM_X + z * plane;
                        if (cursor[cidx] < counts[cidx]) {
                            out->indices[out->offsets[cidx] + cursor[cidx]] = (uint16_t)i;
                            cursor[cidx]++;
                        }
                    }
        }
    }
    (void)total;
}

/// @brief Shader-mirror: compute the cluster index for a screen point + view depth.
/// @details Mirrors the fragment-shader lookup exactly (uv in [0,1], row 0 at the
///          top). Exposed for the conservativeness unit test.
int32_t canvas3d_cluster_index_for_point(float u, float v, float depth, float znear, float zfar) {
    int32_t x = (int32_t)(u * (float)VGFX3D_CLUSTER_DIM_X);
    int32_t y = (int32_t)(v * (float)VGFX3D_CLUSTER_DIM_Y);
    int32_t z = canvas3d_cluster_z_slice(depth, znear, zfar);
    if (x < 0)
        x = 0;
    if (x >= VGFX3D_CLUSTER_DIM_X)
        x = VGFX3D_CLUSTER_DIM_X - 1;
    if (y < 0)
        y = 0;
    if (y >= VGFX3D_CLUSTER_DIM_Y)
        y = VGFX3D_CLUSTER_DIM_Y - 1;
    return x + y * VGFX3D_CLUSTER_DIM_X + z * VGFX3D_CLUSTER_DIM_X * VGFX3D_CLUSTER_DIM_Y;
}

#define CANVAS3D_CLUSTER_TABLE_RING 4

/// @brief Fetch-or-build the froxel table matching a light revision.
/// @details Tables live in a small revision-keyed ring on the canvas (invalidated at
///          frame Begin because binning is camera-dependent), so typical frames build
///          at most one table and mid-frame light mutations each get their own —
///          exactly mirroring the Plan 04 constant-upload gating. Returns NULL when
///          clustering is disabled or the backend keeps the flat loop (software and
///          test backends, identified by the absence of GPU window post-FX).
const vgfx3d_cluster_table_t *canvas3d_cluster_table_for_revision(
    rt_canvas3d *c, const vgfx3d_light_params_t *lights, int32_t light_count, uint32_t revision) {
    vgfx3d_cluster_table_t *ring;
    vgfx3d_cluster_table_t *slot;

    if (!c || !c->clustered_lighting || !lights || light_count <= 0 || revision == 0)
        return NULL;
    if (!c->backend || !c->backend->present_postfx)
        return NULL; /* flat-loop backends (software / test doubles) */

    ring = (vgfx3d_cluster_table_t *)c->cluster_tables;
    if (!ring) {
        ring = (vgfx3d_cluster_table_t *)calloc(CANVAS3D_CLUSTER_TABLE_RING, sizeof(*ring));
        if (!ring)
            return NULL; /* allocation failure: fall back to the flat loop */
        c->cluster_tables = ring;
        c->cluster_table_count = CANVAS3D_CLUSTER_TABLE_RING;
        c->cluster_table_cursor = 0;
    }
    for (int32_t i = 0; i < c->cluster_table_count; i++) {
        if (ring[i].lights_revision == revision)
            return &ring[i];
    }
    slot = &ring[c->cluster_table_cursor % CANVAS3D_CLUSTER_TABLE_RING];
    c->cluster_table_cursor = (c->cluster_table_cursor + 1) % CANVAS3D_CLUSTER_TABLE_RING;
    canvas3d_build_cluster_table(c, lights, light_count, revision, slot);
    c->cluster_overflow_total += slot->overflow_count;
    return slot;
}

#else
typedef int rt_graphics_disabled_tu_guard_canvas3d_clusters;
#endif /* VIPER_ENABLE_GRAPHICS */
