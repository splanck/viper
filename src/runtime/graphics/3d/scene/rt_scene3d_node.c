//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/scene/rt_scene3d_node.c
// Purpose: SceneNode3D lifecycle + accessors (transform, mesh/material/light/body,
//   hierarchy, animator binding, world-space queries). Split out of rt_scene3d.c;
//   shares private structs/helpers via rt_scene3d_internal.h.
// Links: rt_scene3d_internal.h, rt_scene3d.h
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

/*==========================================================================
 * SceneNode3D — lifecycle
 *=========================================================================*/

/// @brief GC finalizer for a SceneNode — release mesh/material/animator/body refs and the children
/// array.
static void rt_scene_node3d_finalize(void *obj) {
    rt_scene_node3d *node = (rt_scene_node3d *)obj;
    if (!node)
        return;

    for (int32_t i = 0; i < node->child_count; i++) {
        if (node->children[i])
            node->children[i]->parent = NULL;
        scene3d_release_ref((void **)&node->children[i]);
    }
    free(node->children);
    node->children = NULL;
    node->child_count = 0;
    node->child_capacity = 0;
    for (int32_t i = 0; i < node->lod_count; i++)
        scene3d_release_ref(&node->lod_levels[i].mesh);
    free(node->lod_levels);
    node->lod_levels = NULL;
    node->lod_count = 0;
    node->lod_capacity = 0;
    scene3d_release_ref(&node->mesh);
    scene3d_release_ref(&node->material);
    scene3d_release_ref(&node->light);
    scene3d_release_ref(&node->bound_body);
    scene3d_release_ref(&node->bound_animator);
    if (node->bound_node_animator)
        ((rt_node_animator3d *)node->bound_node_animator)->root = NULL;
    scene3d_release_ref(&node->bound_node_animator);
    scene3d_release_ref(&node->impostor_pixels);
    scene3d_release_ref(&node->impostor_mesh);
    scene3d_release_ref(&node->impostor_material);
    scene3d_release_ref((void **)&node->name);
}

// ===========================================================================
// SceneNode public API
//
// A SceneNode is one transformable element in the scene graph: it
// carries a TRS (position / rotation / scale), an optional mesh +
// material to draw, an optional rigid body to drive physics from,
// an optional animator, and a list of child nodes. Each accessor
// is null-safe; setters skip if `obj` is NULL, getters return zero
// / identity / NULL.
// ===========================================================================

/// @brief Create an empty SceneNode at the origin (identity rotation, scale 1).
void *rt_scene_node3d_new(void) {
    rt_scene_node3d *node = (rt_scene_node3d *)rt_obj_new_i64(RT_G3D_SCENENODE3D_CLASS_ID,
                                                              (int64_t)sizeof(rt_scene_node3d));
    if (!node) {
        rt_trap("SceneNode3D.New: memory allocation failed");
        return NULL;
    }
    memset(node, 0, sizeof(*node));
    node->vptr = NULL;
    node->position[0] = node->position[1] = node->position[2] = 0.0;
    node->rotation[0] = node->rotation[1] = node->rotation[2] = 0.0;
    node->rotation[3] = 1.0; /* identity quaternion (0,0,0,1) */
    node->scale_xyz[0] = node->scale_xyz[1] = node->scale_xyz[2] = 1.0;

    /* Identity world matrix */
    memset(node->world_matrix, 0, sizeof(double) * 16);
    node->world_matrix[0] = node->world_matrix[5] = 1.0;
    node->world_matrix[10] = node->world_matrix[15] = 1.0;
    node->world_dirty = 1;
    node->world_revision = 1;
    node->parent_world_revision_seen = 0;

    node->parent = NULL;
    node->owner_scene = NULL;
    node->children = NULL;
    node->child_count = 0;
    node->child_capacity = 0;

    node->mesh = NULL;
    node->material = NULL;
    node->light = NULL;
    node->bound_body = NULL;
    node->bound_animator = NULL;
    node->bound_node_animator = NULL;
    node->sync_mode = RT_SCENE_NODE3D_SYNC_NODE_FROM_BODY;
    node->visible = 1;
    node->name = NULL;

    memset(node->aabb_min, 0, sizeof(float) * 3);
    memset(node->aabb_max, 0, sizeof(float) * 3);
    node->bsphere_radius = 0.0f;

    node->lod_levels = NULL;
    node->lod_count = 0;
    node->lod_capacity = 0;
    node->auto_lod_enabled = 0;
    node->auto_lod_screen_error_px = 8.0;
    node->has_impostor = 0;
    node->impostor_distance = 0.0;
    node->impostor_pixels = NULL;
    node->impostor_mesh = NULL;
    node->impostor_material = NULL;

    rt_obj_set_finalizer(node, rt_scene_node3d_finalize);
    return node;
}

/*==========================================================================
 * SceneNode3D — transform
 *=========================================================================*/

/// @brief Set the local-space position component of the node's TRS.
void rt_scene_node3d_set_position(void *obj, double x, double y, double z) {
    rt_scene_node3d *n = scene_node3d_checked(obj);
    if (!n)
        return;
    n->position[0] = scene3d_clamp_abs_or(x, 0.0);
    n->position[1] = scene3d_clamp_abs_or(y, 0.0);
    n->position[2] = scene3d_clamp_abs_or(z, 0.0);
    mark_dirty(n);
}

/// @brief Read the local position as a Vec3 (origin if `obj` is NULL).
void *rt_scene_node3d_get_position(void *obj) {
    rt_scene_node3d *n = scene_node3d_checked(obj);
    if (!n)
        return rt_vec3_new(0, 0, 0);
    return rt_vec3_new(n->position[0], n->position[1], n->position[2]);
}

/// @brief Replace the local rotation with the given Quat (re-normalised on store).
void rt_scene_node3d_set_rotation(void *obj, void *quat) {
    rt_scene_node3d *n = scene_node3d_checked(obj);
    if (!n || !rt_g3d_is_quat(quat))
        return;
    n->rotation[0] = rt_quat_x(quat);
    n->rotation[1] = rt_quat_y(quat);
    n->rotation[2] = rt_quat_z(quat);
    n->rotation[3] = rt_quat_w(quat);
    scene3d_quat_normalize_local(n->rotation);
    mark_dirty(n);
}

/// @brief Read the local rotation as a Quat (identity if `obj` is NULL).
void *rt_scene_node3d_get_rotation(void *obj) {
    rt_scene_node3d *n = scene_node3d_checked(obj);
    if (!n)
        return rt_quat_new(0, 0, 0, 1);
    return rt_quat_new(n->rotation[0], n->rotation[1], n->rotation[2], n->rotation[3]);
}

/// @brief Set the per-axis scale (uniform or non-uniform).
void rt_scene_node3d_set_scale(void *obj, double x, double y, double z) {
    rt_scene_node3d *n = scene_node3d_checked(obj);
    if (!n)
        return;
    n->scale_xyz[0] = scene3d_scale_or_unit(x);
    n->scale_xyz[1] = scene3d_scale_or_unit(y);
    n->scale_xyz[2] = scene3d_scale_or_unit(z);
    mark_dirty(n);
}

/// @brief Read the local scale as a Vec3 (1,1,1 if `obj` is NULL).
void *rt_scene_node3d_get_scale(void *obj) {
    rt_scene_node3d *n = scene_node3d_checked(obj);
    if (!n)
        return rt_vec3_new(1, 1, 1);
    return rt_vec3_new(n->scale_xyz[0], n->scale_xyz[1], n->scale_xyz[2]);
}

/// @brief Compose this node's local TRS with all ancestors and return the world matrix as a Mat4.
void *rt_scene_node3d_get_world_matrix(void *obj) {
    rt_scene_node3d *n = scene_node3d_checked(obj);
    if (!n)
        return NULL;
    recompute_world_matrix(n);
    const double *m = n->world_matrix;
    return rt_mat4_new(m[0],
                       m[1],
                       m[2],
                       m[3],
                       m[4],
                       m[5],
                       m[6],
                       m[7],
                       m[8],
                       m[9],
                       m[10],
                       m[11],
                       m[12],
                       m[13],
                       m[14],
                       m[15]);
}

/// @brief Read the world-space translation as a Vec3.
void *rt_scene_node3d_get_world_position(void *obj) {
    rt_scene_node3d *n = scene_node3d_checked(obj);
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    scene_node_get_world_position(n, &x, &y, &z);
    return rt_vec3_new(x, y, z);
}

/// @brief Read a node's world-space position into @p x / @p y / @p z (resolving its world
/// transform).
/// @return 1 on success, 0 (no writes) for an invalid handle.
int8_t rt_scene_node3d_get_world_position_components(void *obj, double *x, double *y, double *z) {
    rt_scene_node3d *n = scene_node3d_checked(obj);
    if (!n || !x || !y || !z)
        return 0;
    scene_node_get_world_position(n, x, y, z);
    return 1;
}

/// @brief Read the world-space orientation as a Quat.
void *rt_scene_node3d_get_world_rotation(void *obj) {
    rt_scene_node3d *n = scene_node3d_checked(obj);
    double q[4];
    scene_node_get_world_rotation(n, q);
    return rt_quat_new(q[0], q[1], q[2], q[3]);
}

/// @brief Read world-space scale magnitudes from the composed matrix basis vectors.
void *rt_scene_node3d_get_world_scale(void *obj) {
    rt_scene_node3d *n = scene_node3d_checked(obj);
    const double *m;
    double sx;
    double sy;
    double sz;
    if (!n)
        return rt_vec3_new(1.0, 1.0, 1.0);
    recompute_world_matrix(n);
    m = n->world_matrix;
    sx = sqrt(m[0] * m[0] + m[4] * m[4] + m[8] * m[8]);
    sy = sqrt(m[1] * m[1] + m[5] * m[5] + m[9] * m[9]);
    sz = sqrt(m[2] * m[2] + m[6] * m[6] + m[10] * m[10]);
    return rt_vec3_new(sx, sy, sz);
}

/*==========================================================================
 * SceneNode3D — hierarchy
 *=========================================================================*/

/// @brief Reparent `child` under `obj`. Detaches from previous parent first; rejects cycles.
int8_t rt_scene_node3d_try_add_child(void *obj, void *child_obj) {
    rt_scene_node3d *parent = scene_node3d_checked(obj);
    rt_scene_node3d *child = scene_node3d_checked(child_obj);
    if (!parent || !child || parent == child)
        return 0;
    if (child->parent == parent)
        return 1;

    /* Reject cycle formation: parent may not already be inside child's subtree. */
    if (node_contains(child, parent))
        return 0;

    /* Grow children array if needed */
    if (parent->child_count >= parent->child_capacity) {
        int32_t new_cap;
        if (parent->child_capacity < 0 || parent->child_capacity > INT32_MAX / 2) {
            rt_trap("SceneNode3D.AddChild: too many children");
            return 0;
        }
        new_cap = parent->child_capacity == 0 ? NODE_INIT_CHILDREN : parent->child_capacity * 2;
        if ((size_t)new_cap > SIZE_MAX / sizeof(rt_scene_node3d *)) {
            rt_trap("SceneNode3D.AddChild: too many children");
            return 0;
        }
        rt_scene_node3d **nc = (rt_scene_node3d **)realloc(
            parent->children, (size_t)new_cap * sizeof(rt_scene_node3d *));
        if (!nc) {
            rt_trap("SceneNode3D.AddChild: allocation failed");
            return 0;
        }
        parent->children = nc;
        parent->child_capacity = new_cap;
    }

    rt_obj_retain_maybe(child);
    /* Detach from previous parent if any. The temporary retain above becomes
       the new parent's ownership after the old parent releases its reference. */
    if (child->parent)
        rt_scene_node3d_remove_child(child->parent, child);

    parent->children[parent->child_count++] = child;
    child->parent = parent;
    if (parent->owner_scene) {
        scene_node_assign_owner_recursive(child, parent->owner_scene);
        scene3d_mark_spatial_dirty(parent->owner_scene);
    }
    mark_dirty(child);
    return 1;
}

/// @brief Attach @p child_obj under @p obj, retaining it and propagating owner/dirty state.
/// @details Updates the child's parent link and owning scene, and marks the spatial index topology
///          dirty so the BVH rebuilds. Ignores cycles and invalid handles.
void rt_scene_node3d_add_child(void *obj, void *child_obj) {
    (void)rt_scene_node3d_try_add_child(obj, child_obj);
}

/// @brief Detach `child` from `obj`. Decrements the GC refcount. No-op if not actually a child.
void rt_scene_node3d_remove_child(void *obj, void *child_obj) {
    rt_scene_node3d *parent = scene_node3d_checked(obj);
    rt_scene_node3d *child = scene_node3d_checked(child_obj);
    rt_scene3d *owner = parent ? parent->owner_scene : NULL;
    if (!parent || !child)
        return;

    for (int32_t i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == child) {
            /* Shift remaining children down */
            for (int32_t j = i; j < parent->child_count - 1; j++)
                parent->children[j] = parent->children[j + 1];
            parent->child_count--;
            parent->children[parent->child_count] = NULL;
            child->parent = NULL;
            if (owner) {
                scene_node_clear_owner_recursive(child, owner);
                scene3d_mark_spatial_dirty(owner);
            }
            mark_dirty(child);
            if (rt_obj_release_check0(child))
                rt_obj_free(child);
            return;
        }
    }
}

/// @brief Number of immediate (non-recursive) children attached to this node.
int64_t rt_scene_node3d_child_count(void *obj) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    return node ? node->child_count : 0;
}

/// @brief Return the `index`-th child handle (NULL on out-of-range or NULL `obj`).
void *rt_scene_node3d_get_child(void *obj, int64_t index) {
    rt_scene_node3d *n = scene_node3d_checked(obj);
    if (!n)
        return NULL;
    if (index < 0 || index >= n->child_count)
        return NULL;
    return n->children[index];
}

/// @brief Parent node handle (NULL for root or detached nodes).
void *rt_scene_node3d_get_parent(void *obj) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    return node ? node->parent : NULL;
}

/// @brief Recursive depth-first search of the subtree for a node with the given name.
void *rt_scene_node3d_find(void *obj, rt_string name) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    if (!node || !name)
        return NULL;
    const char *s = rt_string_cstr(name);
    if (!s)
        return NULL;
    return find_by_name(node, s);
}

/*==========================================================================
 * SceneNode3D — renderable / visibility / name
 *=========================================================================*/

/// @brief Bind a mesh to this node (replaces previous; null clears).
/// The mesh is referenced (not copied) so multiple nodes can share it.
void rt_scene_node3d_set_mesh(void *obj, void *mesh) {
    rt_scene_node3d *n = scene_node3d_checked(obj);
    if (!n)
        return;
    if (mesh && !rt_g3d_has_class(mesh, RT_G3D_MESH3D_CLASS_ID))
        return;
    if (n->mesh == mesh) {
        if (mesh)
            scene_mesh_bounds((rt_mesh3d *)mesh, n->aabb_min, n->aabb_max, &n->bsphere_radius);
        scene3d_mark_spatial_dirty(n->owner_scene);
        return;
    }
    rt_obj_retain_maybe(mesh);
    scene3d_release_ref(&n->mesh);
    n->mesh = mesh;

    /* Compute object-space AABB from mesh vertices */
    if (mesh) {
        scene_mesh_bounds((rt_mesh3d *)mesh, n->aabb_min, n->aabb_max, &n->bsphere_radius);
    } else {
        n->aabb_min[0] = n->aabb_min[1] = n->aabb_min[2] = 0.0f;
        n->aabb_max[0] = n->aabb_max[1] = n->aabb_max[2] = 0.0f;
        n->bsphere_radius = 0.0f;
    }
    scene3d_mark_spatial_dirty(n->owner_scene);
}

/// @brief Currently bound mesh handle (NULL if none).
void *rt_scene_node3d_get_mesh(void *obj) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    return node ? node->mesh : NULL;
}

/// @brief Bind a material to this node (replaces previous; null clears).
void rt_scene_node3d_set_material(void *obj, void *material) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    if (!node)
        return;
    if (material && !rt_g3d_has_class(material, RT_G3D_MATERIAL3D_CLASS_ID))
        return;
    if (node->material == material)
        return;
    rt_obj_retain_maybe(material);
    scene3d_release_ref(&node->material);
    node->material = material;
}

/// @brief Currently bound material handle (NULL if none).
void *rt_scene_node3d_get_material(void *obj) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    return node ? node->material : NULL;
}

/// @brief Attach a Light3D to this node; Scene3D.Draw transforms it by the node world pose.
void rt_scene_node3d_set_light(void *obj, void *light) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    if (!node)
        return;
    if (light && !rt_g3d_has_class(light, RT_G3D_LIGHT3D_CLASS_ID))
        return;
    if (node->light == light)
        return;
    rt_obj_retain_maybe(light);
    scene3d_release_ref(&node->light);
    node->light = light;
}

/// @brief Currently attached Light3D handle (NULL if this node has no imported/local light).
void *rt_scene_node3d_get_light(void *obj) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    return node ? node->light : NULL;
}

/// @brief Toggle whether this node participates in rendering.
void rt_scene_node3d_set_visible(void *obj, int8_t visible) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    if (node) {
        node->visible = visible ? 1 : 0;
        scene3d_mark_spatial_dirty(node->owner_scene);
    }
}

/// @brief Read the visibility flag (0 or 1; 0 if `obj` is NULL).
int8_t rt_scene_node3d_get_visible(void *obj) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    return node ? node->visible : 0;
}

/// @brief Set the node's identifier name (used by `rt_scene_node3d_find`).
void rt_scene_node3d_set_name(void *obj, rt_string name) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    if (!node)
        return;
    if (!name)
        name = rt_const_cstr("");
    if (node->name == name)
        return;
    rt_obj_retain_maybe(name);
    scene3d_release_ref((void **)&node->name);
    node->name = name;
}

/// @brief Read the node's name (empty string if unset or `obj` is NULL).
rt_string rt_scene_node3d_get_name(void *obj) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    if (node && node->name)
        return node->name;
    return rt_const_cstr("");
}

/// @brief Local-space minimum corner of this node subtree's AABB (origin if empty).
void *rt_scene_node3d_get_aabb_min(void *obj) {
    double identity[16];
    float bounds_min[3];
    float bounds_max[3];
    int has_bounds;
    rt_scene_node3d *n = scene_node3d_checked(obj);
    if (!n)
        return rt_vec3_new(0, 0, 0);
    scene_bounds_reset(bounds_min, bounds_max);
    mat4d_identity(identity);
    has_bounds = scene_node_collect_subtree_bounds(n, identity, bounds_min, bounds_max);
    if (!has_bounds) {
        bounds_min[0] = bounds_min[1] = bounds_min[2] = 0.0f;
        bounds_max[0] = bounds_max[1] = bounds_max[2] = 0.0f;
    }
    return rt_vec3_new(bounds_min[0], bounds_min[1], bounds_min[2]);
}

/// @brief Local-space maximum corner of this node subtree's AABB.
void *rt_scene_node3d_get_aabb_max(void *obj) {
    double identity[16];
    float bounds_min[3];
    float bounds_max[3];
    int has_bounds;
    rt_scene_node3d *n = scene_node3d_checked(obj);
    if (!n)
        return rt_vec3_new(0, 0, 0);
    scene_bounds_reset(bounds_min, bounds_max);
    mat4d_identity(identity);
    has_bounds = scene_node_collect_subtree_bounds(n, identity, bounds_min, bounds_max);
    if (!has_bounds) {
        bounds_min[0] = bounds_min[1] = bounds_min[2] = 0.0f;
        bounds_max[0] = bounds_max[1] = bounds_max[2] = 0.0f;
    }
    return rt_vec3_new(bounds_max[0], bounds_max[1], bounds_max[2]);
}

/// @brief Link a physics rigid body to this node so transforms stay in sync (see `set_sync_mode`).
void rt_scene_node3d_bind_body(void *obj, void *body) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    if (!node)
        return;
    if (body && !rt_g3d_has_class(body, RT_G3D_BODY3D_CLASS_ID))
        return;
    if (node->bound_body == body)
        return;
    rt_obj_retain_maybe(body);
    scene3d_release_ref(&node->bound_body);
    node->bound_body = body;
}

/// @brief Detach any bound rigid body. Subsequent `sync` calls on this node become no-ops.
void rt_scene_node3d_clear_body_binding(void *obj) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    if (!node)
        return;
    scene3d_release_ref(&node->bound_body);
}

/// @brief Currently bound rigid body handle (NULL if none).
void *rt_scene_node3d_get_body(void *obj) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    return node ? node->bound_body : NULL;
}

/// @brief Choose how this node and its bound body stay in sync each frame.
///
/// Modes: `NODE_FROM_BODY` (default — rigid-body sim drives the node),
/// `BODY_FROM_NODE` (kinematic — node animates the body),
/// `NODE_FROM_ANIMATOR_ROOT_MOTION` (root-motion driven), or
/// `TWO_WAY_KINEMATIC` (sync both directions per frame).
void rt_scene_node3d_set_sync_mode(void *obj, int64_t sync_mode) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    if (!node)
        return;
    switch (sync_mode) {
        case RT_SCENE_NODE3D_SYNC_NODE_FROM_BODY:
        case RT_SCENE_NODE3D_SYNC_BODY_FROM_NODE:
        case RT_SCENE_NODE3D_SYNC_NODE_FROM_ANIMATOR_ROOT_MOTION:
        case RT_SCENE_NODE3D_SYNC_TWO_WAY_KINEMATIC:
            node->sync_mode = (int32_t)sync_mode;
            break;
        default:
            node->sync_mode = RT_SCENE_NODE3D_SYNC_NODE_FROM_BODY;
            break;
    }
}

/// @brief Current node/body sync mode (`NODE_FROM_BODY` if `obj` is NULL).
int64_t rt_scene_node3d_get_sync_mode(void *obj) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    return node ? node->sync_mode : RT_SCENE_NODE3D_SYNC_NODE_FROM_BODY;
}

/// @brief Bind an animation controller to drive this node's transform / skeleton.
void rt_scene_node3d_bind_animator(void *obj, void *controller) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    if (!node)
        return;
    if (controller && !rt_g3d_has_class(controller, RT_G3D_ANIMCONTROLLER3D_CLASS_ID))
        return;
    if (node->bound_animator == controller)
        return;
    rt_obj_retain_maybe(controller);
    scene3d_release_ref(&node->bound_animator);
    node->bound_animator = controller;
}

/// @brief Bind a NodeAnimator3D to this scene node so its clip channels are applied
///        each frame during `rt_scene3d_update`.
/// @details Retains the new animator and releases the old one. Crucially, the old
///   animator's `root` pointer is cleared to NULL before release so it cannot hold
///   a dangling reference to this node after the swap. The new animator's `root` is
///   set to this node immediately so `node_animator_update` can navigate the subtree
///   on the very next update tick. Passing NULL detaches the current animator and is
///   equivalent to calling `rt_scene_node3d_clear_node_animator_binding`.
/// @param obj      Scene node to drive; no-op if NULL.
/// @param animator NodeAnimator3D handle, or NULL to detach.
void rt_scene_node3d_bind_node_animator(void *obj, void *animator) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    rt_node_animator3d *node_animator;
    if (!node)
        return;
    if (animator && !rt_g3d_has_class(animator, RT_G3D_NODEANIMATOR3D_CLASS_ID))
        return;
    if (node->bound_node_animator == animator)
        return;
    rt_obj_retain_maybe(animator);
    node_animator = (rt_node_animator3d *)animator;
    if (node_animator && node_animator->root && node_animator->root != node) {
        rt_scene_node3d *old_root = node_animator->root;
        if (old_root->bound_node_animator == animator)
            scene3d_release_ref(&old_root->bound_node_animator);
        else
            node_animator->root = NULL;
    }
    if (node->bound_node_animator)
        ((rt_node_animator3d *)node->bound_node_animator)->root = NULL;
    scene3d_release_ref(&node->bound_node_animator);
    node->bound_node_animator = animator;
    if (node_animator)
        node_animator->root = node;
}

/// @brief Detach any bound animator. Subsequent frames stop applying its motion.
void rt_scene_node3d_clear_animator_binding(void *obj) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    if (!node)
        return;
    scene3d_release_ref(&node->bound_animator);
}

/// @brief Currently bound animation controller handle (NULL if none).
void *rt_scene_node3d_get_animator(void *obj) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    return node ? node->bound_animator : NULL;
}

#endif /* VIPER_ENABLE_GRAPHICS */
