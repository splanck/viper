//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/scene/rt_scene3d_spatial.c
// Purpose: Scene3D spatial acceleration — the BVH spatial index (build/refit/
//   query), world-AABB bounds collection, and frustum/box/sphere candidate
//   gathering used by culling. Split out of rt_scene3d.c; shares private
//   structs/helpers via rt_scene3d_internal.h.
// Links: rt_scene3d_internal.h, vgfx3d_frustum.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_animcontroller3d.h"
#include "rt_box.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_json.h"
#include "rt_map.h"
#include "rt_mat4.h"
#include "rt_morphtarget3d.h"
#include "rt_object.h"
#include "rt_physics3d.h"
#include "rt_pixels_internal.h"
#include "rt_quat.h"
#include "rt_scene3d.h"
#include "rt_scene3d_internal.h"
#include "rt_seq.h"
#include "rt_skeleton3d_internal.h"
#include "rt_sound3d.h"
#include "rt_string.h"
#include "rt_trap.h"
#include "rt_vec3.h"
#include "vgfx3d_frustum.h"
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCENE3D_MORPH_BOUND_SCAN_MAX_TRIPLETS (1024u * 1024u)

/// @brief Ensure the spatial index can hold @p needed entries, growing by doubling (min 64).
/// @return 1 on success, 0 on bad args, overflow, or allocation failure.
static int scene3d_spatial_ensure_capacity(rt_scene3d_spatial_index *index, int32_t needed) {
    int32_t new_capacity;
    rt_scene3d_spatial_entry *grown;
    if (!index || needed < 0)
        return 0;
    if (needed <= index->capacity)
        return 1;
    new_capacity = index->capacity < 64 ? 64 : index->capacity;
    while (new_capacity < needed) {
        if (new_capacity > INT32_MAX / 2)
            return 0;
        new_capacity *= 2;
    }
    if ((size_t)new_capacity > SIZE_MAX / sizeof(index->entries[0]))
        return 0;
    grown = (rt_scene3d_spatial_entry *)realloc(index->entries,
                                                (size_t)new_capacity * sizeof(index->entries[0]));
    if (!grown)
        return 0;
    index->entries = grown;
    index->capacity = new_capacity;
    return 1;
}

/// @brief Ensure the index's entry-index array (BVH leaf ordering) holds @p needed slots.
static int scene3d_spatial_ensure_entry_index_capacity(rt_scene3d_spatial_index *index,
                                                       int32_t needed) {
    int32_t new_capacity;
    int32_t *grown;
    if (!index || needed < 0)
        return 0;
    if (needed <= index->entry_index_capacity)
        return 1;
    new_capacity = index->entry_index_capacity < 64 ? 64 : index->entry_index_capacity;
    while (new_capacity < needed) {
        if (new_capacity > INT32_MAX / 2)
            return 0;
        new_capacity *= 2;
    }
    if ((size_t)new_capacity > SIZE_MAX / sizeof(index->entry_indices[0]))
        return 0;
    grown = (int32_t *)realloc(index->entry_indices,
                               (size_t)new_capacity * sizeof(index->entry_indices[0]));
    if (!grown)
        return 0;
    index->entry_indices = grown;
    index->entry_index_capacity = new_capacity;
    return 1;
}

/// @brief Ensure the index's BVH node array holds @p needed nodes (doubling growth).
static int scene3d_spatial_ensure_bvh_node_capacity(rt_scene3d_spatial_index *index,
                                                    int32_t needed) {
    int32_t new_capacity;
    rt_scene3d_spatial_bvh_node *grown;
    if (!index || needed < 0)
        return 0;
    if (needed <= index->node_capacity)
        return 1;
    new_capacity = index->node_capacity < 64 ? 64 : index->node_capacity;
    while (new_capacity < needed) {
        if (new_capacity > INT32_MAX / 2)
            return 0;
        new_capacity *= 2;
    }
    if ((size_t)new_capacity > SIZE_MAX / sizeof(index->nodes[0]))
        return 0;
    grown = (rt_scene3d_spatial_bvh_node *)realloc(index->nodes,
                                                   (size_t)new_capacity * sizeof(index->nodes[0]));
    if (!grown)
        return 0;
    index->nodes = grown;
    index->node_capacity = new_capacity;
    return 1;
}

/// @brief Append a spatial entry to a query's candidate list, growing it as needed.
static int scene3d_spatial_candidate_push(scene3d_spatial_candidate_list_t *list,
                                          rt_scene3d_spatial_entry *entry) {
    int32_t new_capacity;
    rt_scene3d_spatial_entry **grown;
    if (!list || !entry)
        return 0;
    if (list->count >= list->capacity) {
        new_capacity = list->capacity < 64 ? 64 : list->capacity * 2;
        if (new_capacity <= list->capacity ||
            (size_t)new_capacity > SIZE_MAX / sizeof(list->items[0]))
            return 0;
        grown = (rt_scene3d_spatial_entry **)realloc(list->items,
                                                     (size_t)new_capacity * sizeof(list->items[0]));
        if (!grown)
            return 0;
        list->items = grown;
        list->capacity = new_capacity;
    }
    list->items[list->count++] = entry;
    return 1;
}

/// @brief Push a BVH node index onto a query's traversal stack, growing it as needed.
static int scene3d_spatial_node_stack_push(scene3d_spatial_node_stack_t *stack,
                                           int32_t node_index) {
    int32_t new_capacity;
    int32_t *grown;
    if (!stack || node_index < 0)
        return 0;
    if (stack->count >= stack->capacity) {
        new_capacity = stack->capacity < 64 ? 64 : stack->capacity * 2;
        if (new_capacity <= stack->capacity ||
            (size_t)new_capacity > SIZE_MAX / sizeof(stack->items[0]))
            return 0;
        grown = (int32_t *)realloc(stack->items, (size_t)new_capacity * sizeof(stack->items[0]));
        if (!grown)
            return 0;
        stack->items = grown;
        stack->capacity = new_capacity;
    }
    stack->items[stack->count++] = node_index;
    return 1;
}

/// @brief qsort comparator ordering candidate entries by their scene traversal order (stable draw
/// order).
static int scene3d_spatial_entry_ptr_compare_order(const void *a, const void *b) {
    const rt_scene3d_spatial_entry *ea = *(rt_scene3d_spatial_entry *const *)a;
    const rt_scene3d_spatial_entry *eb = *(rt_scene3d_spatial_entry *const *)b;
    if (ea->traversal_order < eb->traversal_order)
        return -1;
    if (ea->traversal_order > eb->traversal_order)
        return 1;
    return 0;
}

/// @brief Centroid of a spatial entry's world AABB along @p axis (used to choose BVH split planes).
static double scene3d_spatial_entry_centroid_axis(const rt_scene3d_spatial_index *index,
                                                  int32_t entry_index,
                                                  int axis) {
    const rt_scene3d_spatial_entry *entry;
    if (!index || entry_index < 0 || entry_index >= index->count || axis < 0 || axis > 2)
        return 0.0;
    entry = &index->entries[entry_index];
    return 0.5 * (entry->world_min[axis] + entry->world_max[axis]);
}

/// @brief Ordering predicate for two entry indices along @p axis (by centroid, traversal-order
/// tiebreak).
static int scene3d_spatial_entry_index_less(const rt_scene3d_spatial_index *index,
                                            int32_t a,
                                            int32_t b,
                                            int axis) {
    double ca = scene3d_spatial_entry_centroid_axis(index, a, axis);
    double cb = scene3d_spatial_entry_centroid_axis(index, b, axis);
    if (ca < cb)
        return 1;
    if (ca > cb)
        return 0;
    return index && a >= 0 && b >= 0 && a < index->count && b < index->count
               ? index->entries[a].traversal_order < index->entries[b].traversal_order
               : a < b;
}

/// @brief Quicksort a sub-range of the entry-index array by centroid along @p axis.
/// @details In-place median-of-center-pivot quicksort; orders leaves so a BVH range can be split
///          cleanly at its midpoint.
static void scene3d_spatial_sort_entry_indices(rt_scene3d_spatial_index *index,
                                               int32_t start,
                                               int32_t count,
                                               int axis) {
    int32_t left;
    int32_t right;
    int32_t pivot;
    int32_t pivot_value;
    if (!index || !index->entry_indices || count <= 1 || axis < 0 || axis > 2)
        return;
    left = start;
    right = start + count - 1;
    pivot = index->entry_indices[start + count / 2];
    while (left <= right) {
        while (scene3d_spatial_entry_index_less(index, index->entry_indices[left], pivot, axis))
            left++;
        while (scene3d_spatial_entry_index_less(index, pivot, index->entry_indices[right], axis))
            right--;
        if (left <= right) {
            int32_t tmp = index->entry_indices[left];
            index->entry_indices[left] = index->entry_indices[right];
            index->entry_indices[right] = tmp;
            left++;
            right--;
        }
    }
    pivot_value = right - start + 1;
    if (pivot_value > 1)
        scene3d_spatial_sort_entry_indices(index, start, pivot_value, axis);
    pivot_value = start + count - left;
    if (pivot_value > 1)
        scene3d_spatial_sort_entry_indices(index, left, pivot_value, axis);
}

/// @brief Expand AABB [out_min, out_max] in place to also contain AABB [in_min, in_max].
static void scene3d_spatial_bounds_include(double out_min[3],
                                           double out_max[3],
                                           const double in_min[3],
                                           const double in_max[3]) {
    scene_bounds_include_point_d(out_min, out_max, in_min);
    scene_bounds_include_point_d(out_min, out_max, in_max);
}

/// @brief Choose the BVH split axis as the one with the greatest spread of entry centroids.
/// @details Splitting along the widest centroid extent yields tighter, better-balanced child nodes.
static int scene3d_spatial_choose_split_axis(const rt_scene3d_spatial_index *index,
                                             int32_t start,
                                             int32_t count) {
    double centroid_min[3];
    double centroid_max[3];
    double spread[3];
    int axis = 0;
    scene_bounds_reset_d(centroid_min, centroid_max);
    for (int32_t i = start; i < start + count; ++i) {
        int32_t entry_index = index->entry_indices[i];
        double centroid[3] = {scene3d_spatial_entry_centroid_axis(index, entry_index, 0),
                              scene3d_spatial_entry_centroid_axis(index, entry_index, 1),
                              scene3d_spatial_entry_centroid_axis(index, entry_index, 2)};
        scene_bounds_include_point_d(centroid_min, centroid_max, centroid);
    }
    spread[0] = centroid_max[0] - centroid_min[0];
    spread[1] = centroid_max[1] - centroid_min[1];
    spread[2] = centroid_max[2] - centroid_min[2];
    if (spread[1] > spread[axis])
        axis = 1;
    if (spread[2] > spread[axis])
        axis = 2;
    return axis;
}

/// @brief Allocate and zero-initialize a new BVH node (children set to -1). Returns its index or
/// -1.
static int scene3d_spatial_alloc_bvh_node(rt_scene3d_spatial_index *index) {
    int32_t node_index;
    if (!index || !scene3d_spatial_ensure_bvh_node_capacity(index, index->node_count + 1))
        return -1;
    node_index = index->node_count++;
    memset(&index->nodes[node_index], 0, sizeof(index->nodes[node_index]));
    index->nodes[node_index].left = -1;
    index->nodes[node_index].right = -1;
    return node_index;
}

/// @brief Recursively build a BVH subtree over entry-index range [start, start+count).
/// @details Computes the node's bounds and cullable count; ranges of <= 8 entries become leaves,
///          larger ranges are sorted on the widest centroid axis and split at the midpoint into two
///          child nodes. Returns the node index, or -1 on allocation failure.
static int scene3d_spatial_build_bvh_range(rt_scene3d_spatial_index *index,
                                           int32_t start,
                                           int32_t count) {
    enum { SCENE3D_SPATIAL_LEAF_SIZE = 8 };

    int32_t node_index;
    rt_scene3d_spatial_bvh_node *node;
    if (!index || start < 0 || count <= 0 || start > INT32_MAX - count)
        return -1;
    node_index = scene3d_spatial_alloc_bvh_node(index);
    if (node_index < 0)
        return -1;
    node = &index->nodes[node_index];
    scene_bounds_reset_d(node->world_min, node->world_max);
    node->start = start;
    node->count = count;
    for (int32_t i = start; i < start + count; ++i) {
        rt_scene3d_spatial_entry *entry = &index->entries[index->entry_indices[i]];
        scene3d_spatial_bounds_include(
            node->world_min, node->world_max, entry->world_min, entry->world_max);
        if (entry->cullable)
            node->cullable_count++;
    }
    if (count <= SCENE3D_SPATIAL_LEAF_SIZE) {
        node->leaf = 1;
        return node_index;
    }
    {
        int axis = scene3d_spatial_choose_split_axis(index, start, count);
        int32_t left_count = count / 2;
        int32_t right_count = count - left_count;
        int32_t left_node;
        int32_t right_node;
        scene3d_spatial_sort_entry_indices(index, start, count, axis);
        left_node = scene3d_spatial_build_bvh_range(index, start, left_count);
        right_node = scene3d_spatial_build_bvh_range(index, start + left_count, right_count);
        if (left_node < 0 || right_node < 0)
            return -1;
        node = &index->nodes[node_index];
        node->left = left_node;
        node->right = right_node;
    }
    return node_index;
}

/// @brief Whether a mesh deforms at runtime (skeletal animator or morph targets present).
/// @details Deforming meshes need looser/refit bounds each frame, so the spatial index treats them
///          differently from static geometry during culling.
int scene3d_mesh_has_dynamic_deformation(rt_mesh3d *mesh, void *effective_animator) {
    return mesh && (effective_animator != NULL || mesh->morph_targets_ref != NULL ||
                    mesh->morph_deltas != NULL || mesh->morph_weights != NULL ||
                    mesh->morph_shape_count > 0);
}

/// @brief Validate the raw transient morph-delta span before scanning it for culling bounds.
static int scene3d_morph_delta_triplet_count(const rt_mesh3d *mesh, size_t *out_count) {
    size_t shape_count;
    size_t vertex_count;
    size_t total;
    uint32_t safe_vertex_count;
    if (out_count)
        *out_count = 0;
    safe_vertex_count = rt_mesh3d_safe_vertex_count(mesh);
    if (!mesh || !mesh->morph_deltas || mesh->morph_shape_count <= 0 || safe_vertex_count == 0)
        return 0;
    shape_count = (size_t)mesh->morph_shape_count;
    vertex_count = (size_t)safe_vertex_count;
    if (shape_count > SIZE_MAX / vertex_count)
        return 0;
    total = shape_count * vertex_count;
    if (total == 0 || total > SCENE3D_MORPH_BOUND_SCAN_MAX_TRIPLETS)
        return 0;
    if (out_count)
        *out_count = total;
    return 1;
}

/// @brief Euclidean length of a 3-component morph delta (as a double); returns 0 when any
///   lane is non-finite.
static double scene3d_morph_delta_length_or_zero(const float *delta) {
    double x;
    double y;
    double z;
    double len;
    if (!delta || !isfinite(delta[0]) || !isfinite(delta[1]) || !isfinite(delta[2]))
        return 0.0;
    x = fabs((double)delta[0]);
    y = fabs((double)delta[1]);
    z = fabs((double)delta[2]);
    len = hypot(hypot(x, y), z);
    return isfinite(len) ? len : 0.0;
}

/// @brief Return cached conservative padding for raw transient morph-delta arrays.
/// @details DrawMeshMorphed can attach a raw `morph_deltas` pointer to a mesh for the duration of
///   one draw. The source array often stays stable across frames, so caching the maximum delta
///   length avoids repeatedly scanning every shape/vertex during Scene3D culling. The cache key
///   includes the pointer, shape count, safe vertex count, and geometry revision so geometry edits
///   or a different morph payload force a rescan.
static double scene3d_cached_raw_morph_bound_pad(rt_mesh3d *mesh) {
    size_t total = 0;
    double max_len = 0.0;
    uint32_t safe_vertex_count;

    if (!mesh || !mesh->morph_deltas || mesh->morph_shape_count <= 0)
        return 0.0;
    safe_vertex_count = rt_mesh3d_safe_vertex_count(mesh);
    if (safe_vertex_count == 0)
        return 0.0;
    if (mesh->morph_bound_valid && mesh->morph_bound_deltas_source == mesh->morph_deltas &&
        mesh->morph_bound_revision == mesh->geometry_revision &&
        mesh->morph_bound_vertex_count == safe_vertex_count &&
        mesh->morph_bound_shape_count == mesh->morph_shape_count) {
        return scene3d_distance_or_zero(mesh->morph_bound_pad);
    }
    mesh->morph_bound_deltas_source = mesh->morph_deltas;
    mesh->morph_bound_revision = mesh->geometry_revision;
    mesh->morph_bound_vertex_count = safe_vertex_count;
    mesh->morph_bound_shape_count = mesh->morph_shape_count;
    mesh->morph_bound_pad = 0.0;
    mesh->morph_bound_valid = 1;

    if (!scene3d_morph_delta_triplet_count(mesh, &total) || total == 0)
        return 0.0;
    for (size_t i = 0; i < total; i++) {
        const float *d = mesh->morph_deltas + i * 3u;
        double len = scene3d_morph_delta_length_or_zero(d);
        if (len > max_len)
            max_len = len;
    }
    mesh->morph_bound_pad = scene3d_distance_or_zero(max_len);
    return mesh->morph_bound_pad;
}

/// @brief Conservative local-space padding for runtime-deformed mesh bounds.
double scene3d_mesh_dynamic_bound_pad(rt_mesh3d *mesh,
                                      void *effective_animator,
                                      double base_radius) {
    double pad = 0.0;
    if (!scene3d_mesh_has_dynamic_deformation(mesh, effective_animator))
        return 0.0;
    if (mesh && mesh->morph_targets_ref) {
        double morph_pad = rt_morphtarget3d_get_max_position_delta(mesh->morph_targets_ref);
        if (isfinite(morph_pad) && morph_pad > pad)
            pad = scene3d_distance_or_zero(morph_pad);
    }
    if (mesh && mesh->morph_deltas && mesh->morph_shape_count > 0 &&
        rt_mesh3d_safe_vertex_count(mesh) > 0) {
        double raw_morph_pad = scene3d_cached_raw_morph_bound_pad(mesh);
        if (raw_morph_pad > pad)
            pad = raw_morph_pad;
    }
    if (effective_animator || (mesh && mesh->morph_shape_count > 0 && pad <= 0.0)) {
        double fallback = scene3d_distance_or_zero(base_radius);
        if (fallback > pad)
            pad = fallback;
    }
    return pad;
}

/// @brief Expand world AABB [out_min, out_max] in place to also contain [in_min, in_max].
static void scene3d_bounds_include_world_aabb(double out_min[3],
                                              double out_max[3],
                                              const double in_min[3],
                                              const double in_max[3]) {
    if (!out_min || !out_max || !in_min || !in_max)
        return;
    scene_bounds_include_point_d(out_min, out_max, in_min);
    scene_bounds_include_point_d(out_min, out_max, in_max);
}

/// @brief Accumulate a single node's mesh world-space AABB into the running bounds.
static int scene3d_include_mesh_world_bounds(rt_scene_node3d *node,
                                             void *mesh_obj,
                                             void *effective_animator,
                                             double out_min[3],
                                             double out_max[3],
                                             double *out_radius) {
    rt_mesh3d *mesh =
        rt_g3d_has_class(mesh_obj, RT_G3D_MESH3D_CLASS_ID) ? (rt_mesh3d *)mesh_obj : NULL;
    float local_min[3];
    float local_max[3];
    double world_min[3];
    double world_max[3];
    float radius_f = 0.0f;
    double radius = 0.0;
    if (!node || !mesh || !out_min || !out_max)
        return 0;
    scene_mesh_bounds(mesh, local_min, local_max, &radius_f);
    radius = (double)radius_f;
    if (scene3d_mesh_has_dynamic_deformation(mesh, effective_animator)) {
        double pad = scene3d_mesh_dynamic_bound_pad(mesh, effective_animator, radius);
        local_min[0] = scene3d_float_or_zero((double)local_min[0] - pad);
        local_min[1] = scene3d_float_or_zero((double)local_min[1] - pad);
        local_min[2] = scene3d_float_or_zero((double)local_min[2] - pad);
        local_max[0] = scene3d_float_or_zero((double)local_max[0] + pad);
        local_max[1] = scene3d_float_or_zero((double)local_max[1] + pad);
        local_max[2] = scene3d_float_or_zero((double)local_max[2] + pad);
    }
    if (!scene3d_transform_aabb_d(local_min, local_max, node->world_matrix, world_min, world_max))
        return 0;
    scene3d_bounds_include_world_aabb(out_min, out_max, world_min, world_max);
    if (out_radius && radius > *out_radius)
        *out_radius = radius;
    return 1;
}

/// @brief Compute the union world AABB of a node's drawable geometry variants.
/// @details Includes the base mesh, all authored LOD meshes, and the impostor mesh. The result is
///   intentionally conservative so frustum/PVS culling uses one stable bound while the visible mesh
///   swaps between LODs.
int scene3d_node_world_draw_union_aabb(rt_scene_node3d *node,
                                       void *effective_animator,
                                       double world_min[3],
                                       double world_max[3],
                                       double *out_radius) {
    int has_bounds = 0;
    double radius = 0.0;
    if (!node || !world_min || !world_max)
        return 0;
    scene_bounds_reset_d(world_min, world_max);
    if (node->mesh && scene3d_include_mesh_world_bounds(
                          node, node->mesh, effective_animator, world_min, world_max, &radius))
        has_bounds = 1;
    for (int32_t i = 0, lod_count = scene3d_node_lod_count(node); i < lod_count; ++i) {
        if (node->lod_levels[i].mesh &&
            scene3d_include_mesh_world_bounds(
                node, node->lod_levels[i].mesh, effective_animator, world_min, world_max, &radius))
            has_bounds = 1;
    }
    if (node->has_impostor && node->impostor_mesh &&
        scene3d_include_mesh_world_bounds(
            node, node->impostor_mesh, effective_animator, world_min, world_max, &radius))
        has_bounds = 1;
    if (out_radius)
        *out_radius = radius;
    return has_bounds;
}

void *scene3d_effective_animator(rt_scene_node3d *node);
static int scene3d_spatial_rebuild(rt_scene3d *scene);

/// @brief Recompute a spatial entry's world AABB from its node's current transform/geometry.
/// @return 1 if the bounds changed, 0 if unchanged (lets refit skip untouched subtrees).
static int scene3d_spatial_refresh_entry_bounds(rt_scene3d_spatial_entry *entry) {
    double world_min[3];
    double world_max[3];
    double radius = 0.0;
    if (!entry || !entry->node || !entry->node->visible)
        return 0;
    recompute_world_matrix(entry->node);
    if (!scene3d_node_world_draw_union_aabb(
            entry->node, scene3d_effective_animator(entry->node), world_min, world_max, &radius))
        return 0;
    memcpy(entry->world_min, world_min, sizeof(entry->world_min));
    memcpy(entry->world_max, world_max, sizeof(entry->world_max));
    entry->cullable = radius > 0.0 ? 1 : 0;
    entry->world_revision = entry->node->world_revision;
    entry->geometry_revision = scene_node_geometry_revision_signature(entry->node);
    return 1;
}

/// @brief True if @p node or any ancestor has a dirty world transform (its cached world
///   bounds cannot be trusted until the transforms are refreshed).
static int scene3d_spatial_node_or_ancestor_dirty(const rt_scene_node3d *node) {
    const rt_scene_node3d *current = node;
    while (current) {
        if (current->world_dirty)
            return 1;
        current = current->parent;
    }
    return 0;
}

/// @brief Recompute a BVH node's bounds bottom-up from its children/leaf entries (refit, no
/// resplit).
static void scene3d_spatial_refit_bvh_node(rt_scene3d_spatial_index *index, int32_t node_index) {
    rt_scene3d_spatial_bvh_node *node;
    if (!index || node_index < 0 || node_index >= index->node_count)
        return;
    node = &index->nodes[node_index];
    scene_bounds_reset_d(node->world_min, node->world_max);
    node->cullable_count = 0;
    if (node->leaf) {
        for (int32_t i = node->start; i < node->start + node->count; ++i) {
            rt_scene3d_spatial_entry *entry = &index->entries[index->entry_indices[i]];
            scene3d_spatial_bounds_include(
                node->world_min, node->world_max, entry->world_min, entry->world_max);
            if (entry->cullable)
                node->cullable_count++;
        }
        return;
    }
    scene3d_spatial_refit_bvh_node(index, node->left);
    scene3d_spatial_refit_bvh_node(index, node->right);
    if (node->left >= 0 && node->left < index->node_count) {
        rt_scene3d_spatial_bvh_node *left = &index->nodes[node->left];
        scene3d_spatial_bounds_include(
            node->world_min, node->world_max, left->world_min, left->world_max);
        node->cullable_count += left->cullable_count;
    }
    if (node->right >= 0 && node->right < index->node_count) {
        rt_scene3d_spatial_bvh_node *right = &index->nodes[node->right];
        scene3d_spatial_bounds_include(
            node->world_min, node->world_max, right->world_min, right->world_max);
        node->cullable_count += right->cullable_count;
    }
}

/// @brief Refit the whole BVH to current geometry without changing its topology.
/// @details Refreshes each leaf entry's bounds then re-expands node bounds bottom-up — cheaper than
/// a
///          rebuild when only transforms moved. Returns 0 (signalling a rebuild is needed) if the
///          tree shape no longer fits.
static int scene3d_spatial_refit(rt_scene3d *scene) {
    rt_scene3d_spatial_index *index;
    int refreshed = 0;

    enum { SCENE3D_SPATIAL_MAX_REFITS_BEFORE_REBUILD = 32 };

    if (!scene || !scene->root)
        return 0;
    index = &scene->spatial_index;
    if (!index->valid || index->topology_dirty)
        return scene3d_spatial_rebuild(scene);
    for (int32_t i = 0; i < index->count; ++i) {
        uint32_t before = index->entries[i].world_revision;
        uint32_t geometry_before = index->entries[i].geometry_revision;
        uint32_t geometry_now;
        if (!index->entries[i].node)
            return scene3d_spatial_rebuild(scene);
        geometry_now = scene_node_geometry_revision_signature(index->entries[i].node);
        if (!scene3d_spatial_node_or_ancestor_dirty(index->entries[i].node) &&
            before == index->entries[i].node->world_revision && geometry_before == geometry_now)
            continue;
        if (!scene3d_spatial_refresh_entry_bounds(&index->entries[i]))
            return scene3d_spatial_rebuild(scene);
        if (index->entries[i].world_revision != before ||
            index->entries[i].geometry_revision != geometry_before)
            refreshed++;
    }
    if (index->root_node >= 0)
        scene3d_spatial_refit_bvh_node(index, index->root_node);
    index->dirty = 0;
    index->valid = 1;
    index->topology_dirty = 0;
    index->mesh_geometry_epoch = rt_mesh3d_global_geometry_epoch();
    if (refreshed)
        index->refit_count++;
    if (refreshed && index->refit_count >= SCENE3D_SPATIAL_MAX_REFITS_BEFORE_REBUILD)
        return scene3d_spatial_rebuild(scene);
    index->last_candidate_count = 0;
    index->last_prefiltered_count = 0;
    return 1;
}

/// @brief Resolve the animator governing a node, inheriting the nearest ancestor's bound animator.
void *scene3d_effective_animator(rt_scene_node3d *node) {
    rt_scene_node3d *current = node;
    while (current) {
        void *animator =
            rt_g3d_checked_or_null(current->bound_animator, RT_G3D_ANIMCONTROLLER3D_CLASS_ID);
        if (animator)
            return animator;
        current = current->parent;
    }
    return NULL;
}

/// @brief Add a drawable node to the spatial index as a leaf entry with its world bounds.
static int scene3d_spatial_add_entry(rt_scene3d_spatial_index *index,
                                     rt_scene_node3d *node,
                                     int32_t traversal_order,
                                     const double world_min[3],
                                     const double world_max[3],
                                     double radius) {
    rt_scene3d_spatial_entry *entry;
    if (!index || !node || !world_min || !world_max)
        return 1;
    if (!scene3d_spatial_ensure_capacity(index, index->count + 1))
        return 0;
    entry = &index->entries[index->count++];
    entry->node = node;
    memcpy(entry->world_min, world_min, sizeof(entry->world_min));
    memcpy(entry->world_max, world_max, sizeof(entry->world_max));
    entry->traversal_order = traversal_order;
    entry->cullable = radius > 0.0 ? 1 : 0;
    entry->world_revision = node->world_revision;
    entry->geometry_revision = scene_node_geometry_revision_signature(node);
    return 1;
}

/// @brief Rebuild the scene's spatial BVH from scratch by traversing the node hierarchy.
/// @details Collects every drawable node as a leaf entry (tracking its effective animator), then
///          builds the BVH over them. Called when the topology dirty flag is set.
/// @return 1 on success, 0 on allocation failure.
static int scene3d_spatial_rebuild(rt_scene3d *scene) {
    scene_index_build_stack_item_t *stack = NULL;
    size_t count = 0;
    size_t capacity = 0;
    int32_t traversal_order = 0;
    rt_scene3d_spatial_index *index;
    if (!scene || !scene->root)
        return 0;
    index = &scene->spatial_index;
    index->count = 0;
    index->node_count = 0;
    index->root_node = -1;
    index->last_candidate_count = 0;
    index->last_prefiltered_count = 0;
    if (!scene_index_build_stack_push(&stack, &count, &capacity, scene->root, NULL)) {
        rt_trap("Scene3D.SpatialIndex: traversal stack allocation failed");
        return 0;
    }
    while (count > 0) {
        scene_index_build_stack_item_t item = stack[--count];
        rt_scene_node3d *current = item.node;
        void *effective_animator;
        double world_min[3];
        double world_max[3];
        double radius = 0.0;
        int32_t order = traversal_order++;

        if (!current->visible)
            continue;

        recompute_world_matrix(current);
        effective_animator =
            rt_g3d_checked_or_null(current->bound_animator, RT_G3D_ANIMCONTROLLER3D_CLASS_ID);
        if (!effective_animator)
            effective_animator = item.inherited_animator;
        if (scene3d_node_world_draw_union_aabb(
                current, effective_animator, world_min, world_max, &radius)) {
            if (!scene3d_spatial_add_entry(index, current, order, world_min, world_max, radius)) {
                rt_trap("Scene3D.SpatialIndex: entry allocation failed");
                free(stack);
                return 0;
            }
        }
        for (int32_t i = scene3d_node_child_count(current) - 1; i >= 0; --i) {
            if (!scene_index_build_stack_push(
                    &stack, &count, &capacity, current->children[i], effective_animator)) {
                rt_trap("Scene3D.SpatialIndex: traversal stack allocation failed");
                free(stack);
                return 0;
            }
        }
    }
    free(stack);
    if (!scene3d_spatial_ensure_entry_index_capacity(index, index->count)) {
        rt_trap("Scene3D.SpatialIndex: BVH index allocation failed");
        return 0;
    }
    for (int32_t i = 0; i < index->count; ++i)
        index->entry_indices[i] = i;
    if (index->count > 0) {
        index->root_node = scene3d_spatial_build_bvh_range(index, 0, index->count);
        if (index->root_node < 0) {
            rt_trap("Scene3D.SpatialIndex: BVH node allocation failed");
            return 0;
        }
    }
    index->dirty = 0;
    index->topology_dirty = 0;
    index->valid = 1;
    index->build_count++;
    index->refit_count = 0;
    index->mesh_geometry_epoch = rt_mesh3d_global_geometry_epoch();
    return 1;
}

/// @brief Ensure the spatial index is current before a query: rebuild on topology change, else
/// refit.
/// @return 1 if a usable index is available, 0 if it could not be built.
static int scene3d_spatial_ensure(rt_scene3d *scene) {
    if (!scene || !scene->use_spatial_index)
        return 0;
    if (scene->spatial_index.valid && !scene->spatial_index.dirty &&
        scene->spatial_index.mesh_geometry_epoch == rt_mesh3d_global_geometry_epoch())
        return 1;
    if (scene->spatial_index.valid && !scene->spatial_index.dirty)
        scene->spatial_index.dirty = 1;
    if (scene->spatial_index.valid && scene->spatial_index.dirty &&
        !scene->spatial_index.topology_dirty)
        return scene3d_spatial_refit(scene);
    return scene3d_spatial_rebuild(scene);
}

/// @brief Collect spatial entries whose world bounds overlap a query AABB via BVH traversal.
/// @details Descends only into nodes whose bounds intersect the query, so a large scene costs
///          O(log n + hits) rather than scanning every node.
int scene3d_spatial_collect_aabb(rt_scene3d *scene,
                                 const double query_min[3],
                                 const double query_max[3],
                                 scene3d_spatial_candidate_list_t *out,
                                 int count_cullable_prefilter) {
    rt_scene3d_spatial_index *index;
    scene3d_spatial_node_stack_t stack = {0};
    int32_t prefiltered = 0;
    if (!scene || !query_min || !query_max || !out)
        return 0;
    if (!scene3d_spatial_ensure(scene))
        return 0;
    index = &scene->spatial_index;
    if (index->root_node >= 0 && !scene3d_spatial_node_stack_push(&stack, index->root_node)) {
        rt_trap("Scene3D.SpatialIndex: BVH traversal stack allocation failed");
        return 0;
    }
    while (stack.count > 0) {
        rt_scene3d_spatial_bvh_node *node;
        int32_t node_index = stack.items[--stack.count];
        if (node_index < 0 || node_index >= index->node_count)
            continue;
        node = &index->nodes[node_index];
        if (!scene3d_aabb_intersects_aabb(node->world_min, node->world_max, query_min, query_max)) {
            prefiltered += count_cullable_prefilter ? node->cullable_count : node->count;
            continue;
        }
        if (node->leaf) {
            for (int32_t i = node->start; i < node->start + node->count; ++i) {
                rt_scene3d_spatial_entry *entry = &index->entries[index->entry_indices[i]];
                if (!scene3d_aabb_intersects_aabb(
                        entry->world_min, entry->world_max, query_min, query_max)) {
                    if (!count_cullable_prefilter || entry->cullable)
                        prefiltered++;
                    continue;
                }
                if (!scene3d_spatial_candidate_push(out, entry)) {
                    rt_trap("Scene3D.SpatialIndex: candidate allocation failed");
                    free(stack.items);
                    return 0;
                }
            }
        } else {
            if (!scene3d_spatial_node_stack_push(&stack, node->right) ||
                !scene3d_spatial_node_stack_push(&stack, node->left)) {
                rt_trap("Scene3D.SpatialIndex: BVH traversal stack allocation failed");
                free(stack.items);
                return 0;
            }
        }
    }
    free(stack.items);
    if (out->count > 1)
        qsort(out->items,
              (size_t)out->count,
              sizeof(out->items[0]),
              scene3d_spatial_entry_ptr_compare_order);
    index->last_candidate_count = out->count;
    index->last_prefiltered_count = prefiltered;
    return 1;
}

/// @brief Collect every spatial entry (no spatial filtering) into the candidate list.
int scene3d_spatial_collect_all(rt_scene3d *scene, scene3d_spatial_candidate_list_t *out) {
    rt_scene3d_spatial_index *index;
    if (!scene || !out)
        return 0;
    if (!scene3d_spatial_ensure(scene))
        return 0;
    index = &scene->spatial_index;
    for (int32_t i = 0; i < index->count; ++i) {
        if (!scene3d_spatial_candidate_push(out, &index->entries[i])) {
            rt_trap("Scene3D.SpatialIndex: candidate allocation failed");
            return 0;
        }
    }
    if (out->count > 1)
        qsort(out->items,
              (size_t)out->count,
              sizeof(out->items[0]),
              scene3d_spatial_entry_ptr_compare_order);
    index->last_candidate_count = out->count;
    index->last_prefiltered_count = 0;
    return 1;
}

#endif /* VIPER_ENABLE_GRAPHICS */
