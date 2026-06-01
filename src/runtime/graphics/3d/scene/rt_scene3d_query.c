//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/scene/rt_scene3d_query.c
// Purpose: Scene3D spatial queries — AABB / sphere overlap and node raycast.
//   Each uses the BVH spatial index when available and falls back to an
//   iterative subtree walk otherwise. Split out of rt_scene3d.c; shares private
//   structs/helpers via rt_scene3d_internal.h.
// Links: rt_scene3d_internal.h, rt_seq.h, rt_vec3.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_canvas3d_internal.h"
#include "rt_object.h"
#include "rt_scene3d_internal.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_trap.h"
#include "rt_vec3.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>

static scene3d_spatial_candidate_list_t scene3d_query_borrow_candidates(rt_scene3d *scene) {
    scene3d_spatial_candidate_list_t candidates = {0};
    if (!scene)
        return candidates;
    candidates.items = scene->query_candidates;
    candidates.capacity = scene->query_candidate_capacity;
    scene->query_candidates = NULL;
    scene->query_candidate_capacity = 0;
    return candidates;
}

static void scene3d_query_return_candidates(rt_scene3d *scene,
                                            scene3d_spatial_candidate_list_t *candidates) {
    if (!candidates)
        return;
    candidates->count = 0;
    if (scene) {
        free(scene->query_candidates);
        scene->query_candidates = candidates->items;
        scene->query_candidate_capacity = candidates->capacity;
    } else {
        free(candidates->items);
    }
    candidates->items = NULL;
    candidates->capacity = 0;
}

/// @brief `Scene3D.QueryAABB`: indexed when available, flat-walk fallback otherwise.
void *rt_scene3d_query_aabb(void *obj, void *min_obj, void *max_obj) {
    rt_scene3d *s = scene3d_checked(obj);
    void *result;
    rt_scene_node3d **stack = NULL;
    size_t count = 0;
    size_t capacity = 0;
    double a[3];
    double b[3];
    double query_min[3];
    double query_max[3];
    if (!s || !s->root)
        return rt_seq_new_owned();
    if (!scene3d_read_vec3d(min_obj, a, "Scene3D.QueryAABB: min must be Vec3") ||
        !scene3d_read_vec3d(max_obj, b, "Scene3D.QueryAABB: max must be Vec3"))
        return rt_seq_new_owned();
    result = rt_seq_new_owned();
    if (!result)
        return NULL;
    for (int i = 0; i < 3; ++i) {
        query_min[i] = fmin(a[i], b[i]);
        query_max[i] = fmax(a[i], b[i]);
    }
    if (s->use_spatial_index) {
        scene3d_spatial_candidate_list_t candidates = scene3d_query_borrow_candidates(s);
        if (scene3d_spatial_collect_aabb(s, query_min, query_max, &candidates, 0)) {
            for (int32_t i = 0; i < candidates.count; ++i) {
                rt_scene_node3d *current = candidates.items[i]->node;
                double world_min[3];
                double world_max[3];
                if (scene3d_node_world_mesh_aabb(current, world_min, world_max) &&
                    scene3d_aabb_intersects_aabb(world_min, world_max, query_min, query_max))
                    rt_seq_push(result, current);
            }
            scene3d_query_return_candidates(s, &candidates);
            return result;
        }
        scene3d_query_return_candidates(s, &candidates);
    }
    if (!scene_node_stack_push(&stack, &count, &capacity, s->root)) {
        rt_trap("Scene3D.QueryAABB: traversal stack allocation failed");
        return result;
    }
    while (count > 0) {
        rt_scene_node3d *current = stack[--count];
        double world_min[3];
        double world_max[3];
        if (!current->visible)
            continue;
        if (scene3d_node_world_mesh_aabb(current, world_min, world_max) &&
            scene3d_aabb_intersects_aabb(world_min, world_max, query_min, query_max))
            rt_seq_push(result, current);
        for (int32_t i = current->child_count - 1; i >= 0; --i) {
            if (!scene_node_stack_push(&stack, &count, &capacity, current->children[i])) {
                rt_trap("Scene3D.QueryAABB: traversal stack allocation failed");
                free(stack);
                return result;
            }
        }
    }
    free(stack);
    return result;
}

/// @brief `Scene3D.QuerySphere`: indexed when available, flat-walk fallback otherwise.
void *rt_scene3d_query_sphere(void *obj, void *center_obj, double radius) {
    rt_scene3d *s = scene3d_checked(obj);
    void *result;
    rt_scene_node3d **stack = NULL;
    size_t count = 0;
    size_t capacity = 0;
    double center[3];
    double r;
    if (!s || !s->root)
        return rt_seq_new_owned();
    if (!scene3d_read_vec3d(center_obj, center, "Scene3D.QuerySphere: center must be Vec3"))
        return rt_seq_new_owned();
    result = rt_seq_new_owned();
    if (!result)
        return NULL;
    if (!isfinite(radius) || radius < 0.0)
        radius = 0.0;
    r = radius;
    if (s->use_spatial_index) {
        scene3d_spatial_candidate_list_t candidates = scene3d_query_borrow_candidates(s);
        double query_min[3] = {center[0] - r, center[1] - r, center[2] - r};
        double query_max[3] = {center[0] + r, center[1] + r, center[2] + r};
        if (scene3d_spatial_collect_aabb(s, query_min, query_max, &candidates, 0)) {
            for (int32_t i = 0; i < candidates.count; ++i) {
                rt_scene_node3d *current = candidates.items[i]->node;
                double world_min[3];
                double world_max[3];
                if (scene3d_node_world_mesh_aabb(current, world_min, world_max) &&
                    scene3d_aabb_intersects_sphere(world_min, world_max, center, r))
                    rt_seq_push(result, current);
            }
            scene3d_query_return_candidates(s, &candidates);
            return result;
        }
        scene3d_query_return_candidates(s, &candidates);
    }
    if (!scene_node_stack_push(&stack, &count, &capacity, s->root)) {
        rt_trap("Scene3D.QuerySphere: traversal stack allocation failed");
        return result;
    }
    while (count > 0) {
        rt_scene_node3d *current = stack[--count];
        double world_min[3];
        double world_max[3];
        if (!current->visible)
            continue;
        if (scene3d_node_world_mesh_aabb(current, world_min, world_max) &&
            scene3d_aabb_intersects_sphere(world_min, world_max, center, r))
            rt_seq_push(result, current);
        for (int32_t i = current->child_count - 1; i >= 0; --i) {
            if (!scene_node_stack_push(&stack, &count, &capacity, current->children[i])) {
                rt_trap("Scene3D.QuerySphere: traversal stack allocation failed");
                free(stack);
                return result;
            }
        }
    }
    free(stack);
    return result;
}

/// @brief Return the closest visible mesh node whose world AABB intersects the ray.
void *rt_scene3d_raycast_nodes(void *obj,
                               void *origin_obj,
                               void *direction_obj,
                               double max_distance) {
    rt_scene3d *s = scene3d_checked(obj);
    rt_scene_node3d **stack = NULL;
    size_t count = 0;
    size_t capacity = 0;
    double origin[3];
    double direction[3];
    double dir_len;
    double best_t;
    rt_scene_node3d *best = NULL;
    if (!s || !s->root)
        return NULL;
    if (!scene3d_read_vec3d(origin_obj, origin, "Scene3D.RaycastNodes: origin must be Vec3") ||
        !scene3d_read_vec3d(
            direction_obj, direction, "Scene3D.RaycastNodes: direction must be Vec3"))
        return NULL;
    dir_len = sqrt(direction[0] * direction[0] + direction[1] * direction[1] +
                   direction[2] * direction[2]);
    if (!isfinite(dir_len) || dir_len <= 1e-12)
        return NULL;
    direction[0] /= dir_len;
    direction[1] /= dir_len;
    direction[2] /= dir_len;
    if (!isfinite(max_distance))
        max_distance = DBL_MAX;
    if (max_distance < 0.0)
        return NULL;
    best_t = max_distance;
    if (s->use_spatial_index) {
        scene3d_spatial_candidate_list_t candidates = scene3d_query_borrow_candidates(s);
        double query_min[3];
        double query_max[3];
        int ok;
        if (scene3d_ray_sweep_bounds(origin, direction, max_distance, query_min, query_max))
            ok = scene3d_spatial_collect_aabb(s, query_min, query_max, &candidates, 0);
        else
            ok = scene3d_spatial_collect_all(s, &candidates);
        if (ok) {
            for (int32_t i = 0; i < candidates.count; ++i) {
                rt_scene_node3d *current = candidates.items[i]->node;
                double world_min[3];
                double world_max[3];
                double t;
                if (scene3d_node_world_mesh_aabb(current, world_min, world_max) &&
                    scene3d_ray_intersects_aabb(
                        origin, direction, world_min, world_max, best_t, &t)) {
                    if (t <= best_t) {
                        best_t = t;
                        best = current;
                    }
                }
            }
            scene3d_query_return_candidates(s, &candidates);
            return best;
        }
        scene3d_query_return_candidates(s, &candidates);
    }
    if (!scene_node_stack_push(&stack, &count, &capacity, s->root)) {
        rt_trap("Scene3D.RaycastNodes: traversal stack allocation failed");
        return NULL;
    }
    while (count > 0) {
        rt_scene_node3d *current = stack[--count];
        double world_min[3];
        double world_max[3];
        double t;
        if (!current->visible)
            continue;
        if (scene3d_node_world_mesh_aabb(current, world_min, world_max) &&
            scene3d_ray_intersects_aabb(origin, direction, world_min, world_max, best_t, &t)) {
            if (t <= best_t) {
                best_t = t;
                best = current;
            }
        }
        for (int32_t i = current->child_count - 1; i >= 0; --i) {
            if (!scene_node_stack_push(&stack, &count, &capacity, current->children[i])) {
                rt_trap("Scene3D.RaycastNodes: traversal stack allocation failed");
                free(stack);
                return best;
            }
        }
    }
    free(stack);
    return best;
}

#endif /* VIPER_ENABLE_GRAPHICS */
