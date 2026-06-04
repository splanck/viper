//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/render/rt_canvas3d_snapshot.c
// Purpose: Canvas3D mesh-geometry snapshotting — copy a mesh's vertex/index
//   buffers into canvas-owned temp buffers for deferred upload (optionally
//   rebased for camera-relative frames), with a per-frame revision-keyed cache.
//   Split out of rt_canvas3d.c; shares state via rt_canvas3d_internal.h.
// Key invariants:
//   - Snapshot buffers are tracked via the transient-resource tracker so they
//     survive until end-of-frame.
//   - The cache returns an existing snapshot only when the mesh's geometry
//     revision and vertex/index counts are unchanged.
// Links: rt_canvas3d_internal.h, rt_canvas3d_tempmgr.c
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_canvas3d_internal.h"
#include "rt_heap.h"

#include <stdlib.h>
#include <string.h>

/// @brief Copy mesh vertex+index arrays into canvas-owned temp buffers.
/// @details Used when the mesh's owning heap object may be freed before the GPU
///          consumes the draw command — the snapshot lives on the canvas's temp
///          buffer list, freed at end-of-frame. Returns 1 on success, 0 on
///          allocation failure or invalid mesh state.
int canvas3d_snapshot_mesh_geometry(rt_canvas3d *c,
                                    const rt_mesh3d *mesh,
                                    vgfx3d_vertex_t **out_vertices,
                                    uint32_t **out_indices) {
    vgfx3d_vertex_t *vertices;
    uint32_t *indices;
    size_t vertex_bytes;
    size_t index_bytes;
    uint32_t vertex_count;
    uint32_t index_count;
    if (!c || !mesh || !out_vertices || !out_indices || !mesh->vertices || !mesh->indices ||
        rt_mesh3d_safe_vertex_count(mesh) == 0 || rt_mesh3d_safe_index_count(mesh) == 0)
        return 0;
    vertex_count = rt_mesh3d_safe_vertex_count(mesh);
    index_count = rt_mesh3d_safe_index_count(mesh);
    if ((size_t)vertex_count > SIZE_MAX / sizeof(*vertices) ||
        (size_t)index_count > SIZE_MAX / sizeof(*indices))
        return 0;
    vertex_bytes = (size_t)vertex_count * sizeof(*vertices);
    index_bytes = (size_t)index_count * sizeof(*indices);
    if (vertex_bytes > SIZE_MAX - index_bytes)
        return 0;
    size_t total_bytes = vertex_bytes + index_bytes;
    size_t snapshot_budget = (size_t)RT_CANVAS3D_MESH_SNAPSHOT_FRAME_BYTE_BUDGET;
    if (total_bytes > snapshot_budget || c->mesh_snapshot_bytes > snapshot_budget - total_bytes)
        return 0;
    vertices = (vgfx3d_vertex_t *)malloc(vertex_bytes);
    if (!vertices)
        return 0;
    indices = (uint32_t *)malloc(index_bytes);
    if (!indices) {
        free(vertices);
        return 0;
    }
    memcpy(vertices, mesh->vertices, vertex_bytes);
    memcpy(indices, mesh->indices, index_bytes);
    if (!canvas3d_track_temp_buffer(c, vertices)) {
        free(vertices);
        free(indices);
        return 0;
    }
    if (!canvas3d_track_temp_buffer(c, indices)) {
        canvas3d_release_tracked_temp_buffer(c, vertices);
        free(indices);
        return 0;
    }
    c->mesh_snapshot_bytes += total_bytes;
    *out_vertices = vertices;
    *out_indices = indices;
    return 1;
}

/// @brief Compute the axis-aligned bounding box of a vertex array (for culling/occlusion).
void canvas3d_compute_vertices_aabb(const vgfx3d_vertex_t *vertices,
                                    uint32_t vertex_count,
                                    float out_min[3],
                                    float out_max[3]) {
    if (!out_min || !out_max)
        return;
    if (!vertices || vertex_count == 0) {
        out_min[0] = out_min[1] = out_min[2] = 0.0f;
        out_max[0] = out_max[1] = out_max[2] = 0.0f;
        return;
    }
    vgfx3d_compute_mesh_aabb(vertices, vertex_count, sizeof(vgfx3d_vertex_t), out_min, out_max);
}

/// @brief Snapshot a mesh while subtracting @p origin from vertex positions before float upload.
/// @details This is used for identity-matrix raw/generated meshes in camera-relative frames.
///          `Mesh3D.AddVertex` preserves authored double positions in `positions64`; direct
///          importer buffers without a sidecar fall back to their existing float positions.
int canvas3d_snapshot_mesh_geometry_rebased(rt_canvas3d *c,
                                            const rt_mesh3d *mesh,
                                            const double origin[3],
                                            vgfx3d_vertex_t **out_vertices,
                                            uint32_t **out_indices) {
    uint32_t vertex_count = rt_mesh3d_safe_vertex_count(mesh);
    if (!canvas3d_snapshot_mesh_geometry(c, mesh, out_vertices, out_indices))
        return 0;
    for (uint32_t i = 0; i < vertex_count; i++) {
        double x = mesh->positions64 ? mesh->positions64[(size_t)i * 3u + 0]
                                     : (double)mesh->vertices[i].pos[0];
        double y = mesh->positions64 ? mesh->positions64[(size_t)i * 3u + 1]
                                     : (double)mesh->vertices[i].pos[1];
        double z = mesh->positions64 ? mesh->positions64[(size_t)i * 3u + 2]
                                     : (double)mesh->vertices[i].pos[2];
        x -= origin[0];
        y -= origin[1];
        z -= origin[2];
        if (!canvas3d_double_fits_float(x) || !canvas3d_double_fits_float(y) ||
            !canvas3d_double_fits_float(z)) {
            canvas3d_release_tracked_temp_buffer(c, *out_vertices);
            canvas3d_release_tracked_temp_buffer(c, *out_indices);
            *out_vertices = NULL;
            *out_indices = NULL;
            return 0;
        }
        (*out_vertices)[i].pos[0] = (float)x;
        (*out_vertices)[i].pos[1] = (float)y;
        (*out_vertices)[i].pos[2] = (float)z;
    }
    return 1;
}

/// @brief Ensure the per-frame mesh-snapshot cache can hold @p needed entries (grows as needed).
int canvas3d_reserve_mesh_snapshot_cache(rt_canvas3d *c, int32_t needed) {
    if (!c)
        return 0;
    if (needed < 0 || c->mesh_snapshot_capacity < 0)
        return 0;
    if (needed <= c->mesh_snapshot_capacity)
        return 1;
    if (c->mesh_snapshot_capacity > INT32_MAX / 2)
        return 0;
    int32_t new_cap = c->mesh_snapshot_capacity == 0 ? 8 : c->mesh_snapshot_capacity * 2;
    while (new_cap < needed) {
        if (new_cap > INT32_MAX / 2)
            return 0;
        new_cap *= 2;
    }
    if ((size_t)new_cap > SIZE_MAX / sizeof(*c->mesh_snapshots))
        return 0;
    rt_canvas3d_mesh_snapshot_entry *entries = (rt_canvas3d_mesh_snapshot_entry *)realloc(
        c->mesh_snapshots, (size_t)new_cap * sizeof(*entries));
    if (!entries)
        return 0;
    c->mesh_snapshots = entries;
    c->mesh_snapshot_capacity = new_cap;
    return 1;
}

/// @brief Snapshot a mesh's geometry for deferred upload, reusing the cache when unchanged.
/// @details Keyed by the mesh's geometry revision: an unchanged mesh returns its cached snapshot,
/// so
///          repeated draws of the same mesh in a frame don't re-copy vertex data.
int canvas3d_snapshot_mesh_geometry_cached(rt_canvas3d *c,
                                           const rt_mesh3d *mesh,
                                           void *mesh_obj,
                                           vgfx3d_vertex_t **out_vertices,
                                           uint32_t **out_indices) {
    uint32_t vertex_count;
    uint32_t index_count;
    if (!c || !mesh || !out_vertices || !out_indices)
        return 0;
    vertex_count = rt_mesh3d_safe_vertex_count(mesh);
    index_count = rt_mesh3d_safe_index_count(mesh);
    int can_cache = mesh_obj && rt_heap_is_payload(mesh_obj);
    if (can_cache) {
        for (int32_t i = 0; i < c->mesh_snapshot_count; ++i) {
            rt_canvas3d_mesh_snapshot_entry *entry = &c->mesh_snapshots[i];
            if (entry->source == mesh_obj && entry->geometry_revision == mesh->geometry_revision &&
                entry->vertex_count == vertex_count && entry->index_count == index_count) {
                *out_vertices = entry->vertices;
                *out_indices = entry->indices;
                return 1;
            }
        }
    }
    if (!canvas3d_snapshot_mesh_geometry(c, mesh, out_vertices, out_indices))
        return 0;
    if (can_cache) {
        if (c->mesh_snapshot_count >= INT32_MAX)
            return 1;
        if (!canvas3d_reserve_mesh_snapshot_cache(c, c->mesh_snapshot_count + 1))
            return 1;
        rt_canvas3d_mesh_snapshot_entry *entry = &c->mesh_snapshots[c->mesh_snapshot_count++];
        entry->source = mesh_obj;
        entry->geometry_revision = mesh->geometry_revision;
        entry->vertex_count = vertex_count;
        entry->index_count = index_count;
        entry->vertices = *out_vertices;
        entry->indices = *out_indices;
        entry->tangents_generated = 0;
    }
    return 1;
}

/// @brief Generate tangents directly into a canvas-owned mesh snapshot.
/// @details Builds a temporary Mesh3D facade over @p vertices/@p indices so the existing tangent
///   generator can run without mutating the source mesh. The vertex and index buffers remain owned
///   by the canvas temp-buffer tracker; only the transient facade lives on the stack.
static int canvas3d_generate_cached_snapshot_tangents(const rt_mesh3d *source,
                                                      vgfx3d_vertex_t *vertices,
                                                      uint32_t *indices) {
    rt_mesh3d temp;
    if (!source || !vertices || !indices)
        return 0;
    memset(&temp, 0, sizeof(temp));
    temp.vertices = vertices;
    temp.vertex_count = rt_mesh3d_safe_vertex_count(source);
    temp.vertex_capacity = temp.vertex_count;
    temp.indices = indices;
    temp.index_count = rt_mesh3d_safe_index_count(source);
    temp.index_capacity = temp.index_count;
    temp.geometry_revision = source->geometry_revision;
    rt_mesh3d_calc_tangents_impl(&temp);
    return temp.tangents_ready ? 1 : 0;
}

/// @brief Snapshot geometry and ensure generated tangents are cached for this frame.
/// @details Normal-mapped draws with missing authored tangents need a generated tangent basis, but
///   recomputing it for every repeated draw of the same mesh is wasteful. This function keys the
///   generated snapshot by source object, geometry revision, and vertex/index counts. If a matching
///   snapshot already exists, tangents are generated into it at most once and then reused.
int canvas3d_snapshot_mesh_geometry_with_tangents_cached(rt_canvas3d *c,
                                                         const rt_mesh3d *mesh,
                                                         void *mesh_obj,
                                                         vgfx3d_vertex_t **out_vertices,
                                                         uint32_t **out_indices) {
    uint32_t vertex_count;
    uint32_t index_count;
    int can_cache;

    if (!c || !mesh || !out_vertices || !out_indices)
        return 0;
    vertex_count = rt_mesh3d_safe_vertex_count(mesh);
    index_count = rt_mesh3d_safe_index_count(mesh);
    can_cache = mesh_obj && rt_heap_is_payload(mesh_obj);
    if (can_cache) {
        for (int32_t i = 0; i < c->mesh_snapshot_count; ++i) {
            rt_canvas3d_mesh_snapshot_entry *entry = &c->mesh_snapshots[i];
            if (entry->source == mesh_obj && entry->geometry_revision == mesh->geometry_revision &&
                entry->vertex_count == vertex_count && entry->index_count == index_count) {
                if (!entry->tangents_generated) {
                    if (!canvas3d_generate_cached_snapshot_tangents(
                            mesh, entry->vertices, entry->indices))
                        return 0;
                    entry->tangents_generated = 1;
                }
                *out_vertices = entry->vertices;
                *out_indices = entry->indices;
                return 1;
            }
        }
    }
    if (!canvas3d_snapshot_mesh_geometry(c, mesh, out_vertices, out_indices))
        return 0;
    if (!canvas3d_generate_cached_snapshot_tangents(mesh, *out_vertices, *out_indices)) {
        canvas3d_release_tracked_temp_buffer(c, *out_vertices);
        canvas3d_release_tracked_temp_buffer(c, *out_indices);
        *out_vertices = NULL;
        *out_indices = NULL;
        return 0;
    }
    if (can_cache) {
        if (c->mesh_snapshot_count >= INT32_MAX)
            return 1;
        if (!canvas3d_reserve_mesh_snapshot_cache(c, c->mesh_snapshot_count + 1))
            return 1;
        rt_canvas3d_mesh_snapshot_entry *entry = &c->mesh_snapshots[c->mesh_snapshot_count++];
        entry->source = mesh_obj;
        entry->geometry_revision = mesh->geometry_revision;
        entry->vertex_count = vertex_count;
        entry->index_count = index_count;
        entry->vertices = *out_vertices;
        entry->indices = *out_indices;
        entry->tangents_generated = 1;
    }
    return 1;
}

/// @brief Decide whether to snapshot a mesh's geometry into canvas-owned buffers.
/// @details Heap meshes snapshot their vertex/index buffers so a user mutation after
///          enqueue cannot change submitted deferred geometry. Draw-time deformation
///          payloads stay on the original mesh so GPU skinning/morph paths can bind
///          their palettes and weights without allocating CPU geometry snapshots.
int canvas3d_should_snapshot_geometry(const rt_mesh3d *mesh, void *mesh_obj) {
    if (!mesh || !mesh_obj)
        return 0;
    if (rt_heap_is_payload(mesh_obj))
        return 1;
    return 0;
}

#endif /* VIPER_ENABLE_GRAPHICS */
