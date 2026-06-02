//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/rt_game3d_entity.c
// Purpose: Entity3D for the Viper.Game3D layer — node/mesh/material/body/animator
//   composition, parent/child hierarchy, transform, and collision layer/mask.
//   Split out of rt_game3d.c; shares private types/helpers via rt_game3d_internal.h.
// Links: rt_game3d_internal.h, rt_scene3d.h, rt_physics3d.h
//
//===----------------------------------------------------------------------===//

#include "rt_animcontroller3d.h"
#include "rt_asset.h"
#include "rt_audio.h"
#include "rt_box.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_collider3d.h"
#include "rt_decal3d.h"
#include "rt_g3d_commit_queue.h"
#include "rt_game3d.h"
#include "rt_game3d_internal.h"
#include "rt_gltf.h"
#include "rt_graphics3d_ids.h"
#include "rt_input.h"
#include "rt_json.h"
#include "rt_map.h"
#include "rt_mat4.h"
#include "rt_model3d.h"
#include "rt_navmesh3d.h"
#include "rt_object.h"
#include "rt_parallel.h"
#include "rt_particles3d.h"
#include "rt_physics3d.h"
#include "rt_pixels.h"
#include "rt_platform.h"
#include "rt_postfx3d.h"
#include "rt_quat.h"
#include "rt_scene3d.h"
#include "rt_scene3d_internal.h"
#include "rt_seq.h"
#include "rt_sound3d.h"
#include "rt_soundlistener3d.h"
#include "rt_soundsource3d.h"
#include "rt_string.h"
#include "rt_terrain3d.h"
#include "rt_textureasset3d.h"
#include "rt_threadpool.h"
#include "rt_trap.h"
#include "rt_vec2.h"
#include "rt_vec3.h"
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// @brief GC finalizer for an entity: release child entities, node/mesh/material/body/anim/name
///   references it owns, then free the child array.
static void game3d_entity_finalize(void *obj) {
    rt_game3d_entity *entity = (rt_game3d_entity *)obj;
    int32_t child_count;
    if (!entity)
        return;
    child_count = game3d_entity_child_count(entity);
    for (int32_t i = 0; i < child_count; ++i) {
        rt_game3d_entity *child = (rt_game3d_entity *)rt_g3d_checked_or_null(
            entity->children[i], RT_G3D_GAME3D_ENTITY_CLASS_ID);
        if (child && child->parent == entity)
            child->parent = NULL;
        game3d_release_typed_ref((void **)&entity->children[i],
                                 RT_G3D_GAME3D_ENTITY_CLASS_ID);
    }
    free(entity->children);
    entity->children = NULL;
    entity->child_count = 0;
    entity->child_capacity = 0;
    game3d_release_typed_ref(&entity->node, RT_G3D_SCENENODE3D_CLASS_ID);
    game3d_release_typed_ref(&entity->mesh, RT_G3D_MESH3D_CLASS_ID);
    game3d_release_typed_ref(&entity->material, RT_G3D_MATERIAL3D_CLASS_ID);
    game3d_release_typed_ref(&entity->body, RT_G3D_BODY3D_CLASS_ID);
    game3d_release_typed_ref(&entity->anim, RT_G3D_GAME3D_ANIMATOR3D_CLASS_ID);
    game3d_release_ref((void **)&entity->name);
}

/// @brief Allocate a bare entity wrapping a fresh scene node, on the DYNAMIC layer
///   colliding with everything; installs the finalizer and traps on OOM.
void *rt_game3d_entity_new(void) {
    rt_game3d_entity *entity =
        (rt_game3d_entity *)rt_obj_new_i64(RT_G3D_GAME3D_ENTITY_CLASS_ID, (int64_t)sizeof(*entity));
    if (!entity) {
        rt_trap("Game3D.Entity3D.New: allocation failed");
        return NULL;
    }
    memset(entity, 0, sizeof(*entity));
    rt_obj_set_finalizer(entity, game3d_entity_finalize);
    entity->node = rt_scene_node3d_new();
    if (!entity->node) {
        if (rt_obj_release_check0(entity))
            rt_obj_free(entity);
        rt_trap("Game3D.Entity3D.New: node allocation failed");
        return NULL;
    }
    entity->layer = RT_GAME3D_LAYER_DYNAMIC;
    entity->collision_mask_bits = ~(int64_t)0;
    entity->name = rt_const_cstr("");
    rt_obj_retain_maybe(entity->name);
    return entity;
}

/// @brief Allocate an entity and assign the given mesh and material. See header.
void *rt_game3d_entity_of(void *mesh, void *material) {
    rt_game3d_entity *entity = (rt_game3d_entity *)rt_game3d_entity_new();
    if (!entity)
        return NULL;
    rt_game3d_entity_set_mesh(entity, mesh);
    rt_game3d_entity_set_material(entity, material);
    return entity;
}

/// @brief Wrap an existing SceneNode3D hierarchy as a group entity; traps if `root`
///   is not a SceneNode3D.
void *rt_game3d_entity_from_node(void *root) {
    if (!rt_g3d_has_class(root, RT_G3D_SCENENODE3D_CLASS_ID)) {
        rt_trap("Game3D.Entity3D.FromNode: root must be a SceneNode3D");
        return NULL;
    }
    rt_game3d_entity *entity =
        (rt_game3d_entity *)rt_obj_new_i64(RT_G3D_GAME3D_ENTITY_CLASS_ID, (int64_t)sizeof(*entity));
    if (!entity) {
        rt_trap("Game3D.Entity3D.FromNode: allocation failed");
        return NULL;
    }
    memset(entity, 0, sizeof(*entity));
    rt_obj_set_finalizer(entity, game3d_entity_finalize);
    game3d_assign_typed_ref(&entity->node, root, RT_G3D_SCENENODE3D_CLASS_ID);
    entity->layer = RT_GAME3D_LAYER_DYNAMIC;
    entity->collision_mask_bits = ~(int64_t)0;
    entity->name = rt_const_cstr("");
    rt_obj_retain_maybe(entity->name);
    {
        rt_string root_name = rt_scene_node3d_get_name(root);
        const char *root_name_cstr = root_name ? rt_string_cstr(root_name) : NULL;
        if (root_name_cstr && root_name_cstr[0] != '\0')
            game3d_assign_ref((void **)&entity->name, root_name);
    }
    entity->group = 1;
    return entity;
}

/// @brief Get the entity's stable id (allows a destroyed entity; 0 if invalid).
int64_t rt_game3d_entity_get_id(void *obj) {
    rt_game3d_entity *entity =
        game3d_entity_checked_allow_destroyed(obj, "Game3D.Entity3D.get_Id: invalid entity");
    return entity ? entity->id : 0;
}

/// @brief Get the entity's scene node (NULL if invalid).
void *rt_game3d_entity_get_node(void *obj) {
    rt_game3d_entity *entity =
        game3d_entity_checked(obj, "Game3D.Entity3D.get_Node: invalid entity");
    return game3d_entity_node_ref(entity);
}

/// @brief Get the entity's mesh (NULL if none/invalid).
void *rt_game3d_entity_get_mesh(void *obj) {
    rt_game3d_entity *entity =
        game3d_entity_checked(obj, "Game3D.Entity3D.get_Mesh: invalid entity");
    return game3d_entity_mesh_ref(entity);
}

/// @brief Property setter for the mesh (delegates to the fluent setMesh).
void rt_game3d_entity_set_mesh_prop(void *obj, void *mesh) {
    (void)rt_game3d_entity_set_mesh(obj, mesh);
}

/// @brief Get the entity's material (NULL if none/invalid).
void *rt_game3d_entity_get_material(void *obj) {
    rt_game3d_entity *entity =
        game3d_entity_checked(obj, "Game3D.Entity3D.get_Material: invalid entity");
    return game3d_entity_material_ref(entity);
}

/// @brief Property setter for the material (delegates to the fluent setMaterial).
void rt_game3d_entity_set_material_prop(void *obj, void *material) {
    (void)rt_game3d_entity_set_material(obj, material);
}

/// @brief Get the entity's physics body (NULL if unattached/invalid).
void *rt_game3d_entity_get_body(void *obj) {
    rt_game3d_entity *entity =
        game3d_entity_checked(obj, "Game3D.Entity3D.get_Body: invalid entity");
    return game3d_entity_body_ref(entity);
}

/// @brief Get the entity's animator (NULL if none/invalid).
void *rt_game3d_entity_get_anim(void *obj) {
    rt_game3d_entity *entity =
        game3d_entity_checked(obj, "Game3D.Entity3D.get_Anim: invalid entity");
    return game3d_entity_anim_ref(entity);
}

/// @brief Get the entity's collision layer (0 if invalid).
int64_t rt_game3d_entity_get_layer(void *obj) {
    rt_game3d_entity *entity =
        game3d_entity_checked(obj, "Game3D.Entity3D.get_Layer: invalid entity");
    return entity ? entity->layer : 0;
}

/// @brief Property setter for the collision layer (delegates to setLayer).
void rt_game3d_entity_set_layer_prop(void *obj, int64_t layer) {
    (void)rt_game3d_entity_set_layer(obj, layer);
}

/// @brief Get a fresh LayerMask reflecting the entity's collision mask bits.
void *rt_game3d_entity_get_collision_mask(void *obj) {
    rt_game3d_entity *entity =
        game3d_entity_checked(obj, "Game3D.Entity3D.get_CollisionMask: invalid entity");
    return entity ? game3d_layermask_new_bits(entity->collision_mask_bits) : NULL;
}

/// @brief Property setter for the collision mask (delegates to setCollisionMask).
void rt_game3d_entity_set_collision_mask_prop(void *obj, void *mask) {
    (void)rt_game3d_entity_set_collision_mask(obj, mask);
}

/// @brief Get the entity's name, or "" if unset/invalid.
rt_string rt_game3d_entity_get_name(void *obj) {
    rt_game3d_entity *entity =
        game3d_entity_checked(obj, "Game3D.Entity3D.get_Name: invalid entity");
    if (!entity || !entity->name)
        return rt_const_cstr("");
    return entity->name;
}

/// @brief Property setter for the name (delegates to the fluent setName).
void rt_game3d_entity_set_name_prop(void *obj, rt_string name) {
    (void)rt_game3d_entity_set_name(obj, name);
}

/// @brief Fluent: set local position (updating the node and any attached body) and
///   return the entity.
void *rt_game3d_entity_set_position(void *obj, double x, double y, double z) {
    rt_game3d_entity *entity =
        game3d_entity_checked(obj, "Game3D.Entity3D.setPosition: invalid entity");
    void *node = game3d_entity_node_ref(entity);
    if (node)
        rt_scene_node3d_set_position(node,
                                     game3d_clamp_coord_or(x, 0.0),
                                     game3d_clamp_coord_or(y, 0.0),
                                     game3d_clamp_coord_or(z, 0.0));
    game3d_sync_body_from_entity_node(entity, 0);
    return obj;
}

/// @brief Fluent: set local position from a Vec3; traps if `position` is not a Vec3.
void *rt_game3d_entity_set_position_v(void *obj, void *position) {
    if (!rt_g3d_is_vec3(position)) {
        rt_trap("Game3D.Entity3D.setPositionV: position must be Vec3");
        return obj;
    }
    return rt_game3d_entity_set_position(
        obj, rt_vec3_x(position), rt_vec3_y(position), rt_vec3_z(position));
}

/// @brief Fluent: set a uniform scale and return the entity.
void *rt_game3d_entity_set_scale(void *obj, double scale) {
    return rt_game3d_entity_set_scale_xyz(obj, scale, scale, scale);
}

/// @brief Fluent: set a non-uniform XYZ scale on the node and return the entity.
void *rt_game3d_entity_set_scale_xyz(void *obj, double x, double y, double z) {
    rt_game3d_entity *entity =
        game3d_entity_checked(obj, "Game3D.Entity3D.setScaleXYZ: invalid entity");
    x = game3d_scale_or_unit(x);
    y = game3d_scale_or_unit(y);
    z = game3d_scale_or_unit(z);
    void *node = game3d_entity_node_ref(entity);
    if (node)
        rt_scene_node3d_set_scale(node, x, y, z);
    game3d_sync_body_from_entity_node(entity, 0);
    return obj;
}

/// @brief Fluent: set rotation from Euler angles (degrees), converting to a quaternion;
///   returns the entity.
void *rt_game3d_entity_set_rotation_euler(void *obj, double x_deg, double y_deg, double z_deg) {
    rt_game3d_entity *entity =
        game3d_entity_checked(obj, "Game3D.Entity3D.setRotationEuler: invalid entity");
    void *node = game3d_entity_node_ref(entity);
    if (node) {
        const double deg = 3.14159265358979323846 / 180.0;
        x_deg = game3d_clamp_abs_or(x_deg, 0.0, RT_GAME3D_ANGLE_DEG_ABS_MAX);
        y_deg = game3d_clamp_abs_or(y_deg, 0.0, RT_GAME3D_ANGLE_DEG_ABS_MAX);
        z_deg = game3d_clamp_abs_or(z_deg, 0.0, RT_GAME3D_ANGLE_DEG_ABS_MAX);
        void *quat = rt_quat_from_euler(x_deg * deg, y_deg * deg, z_deg * deg);
        rt_scene_node3d_set_rotation(node, quat);
        game3d_release_ref(&quat);
    }
    game3d_sync_body_from_entity_node(entity, 0);
    return obj;
}

/// @brief Fluent: assign the mesh (validated as Mesh3D), mirror it onto the node, and
///   return the entity.
void *rt_game3d_entity_set_mesh(void *obj, void *mesh) {
    rt_game3d_entity *entity =
        game3d_entity_checked(obj, "Game3D.Entity3D.setMesh: invalid entity");
    if (mesh && !rt_g3d_has_class(mesh, RT_G3D_MESH3D_CLASS_ID)) {
        rt_trap("Game3D.Entity3D.setMesh: mesh must be Mesh3D");
        return obj;
    }
    if (entity) {
        void *node = game3d_entity_node_ref(entity);
        game3d_assign_typed_ref(&entity->mesh, mesh, RT_G3D_MESH3D_CLASS_ID);
        if (node)
            rt_scene_node3d_set_mesh(node, mesh);
    }
    return obj;
}

/// @brief Fluent: assign the material (validated as Material3D), mirror it onto the
///   node, and return the entity.
void *rt_game3d_entity_set_material(void *obj, void *material) {
    rt_game3d_entity *entity =
        game3d_entity_checked(obj, "Game3D.Entity3D.setMaterial: invalid entity");
    if (material && !rt_g3d_has_class(material, RT_G3D_MATERIAL3D_CLASS_ID)) {
        rt_trap("Game3D.Entity3D.setMaterial: material must be Material3D");
        return obj;
    }
    if (entity) {
        void *node = game3d_entity_node_ref(entity);
        game3d_assign_typed_ref(&entity->material, material, RT_G3D_MATERIAL3D_CLASS_ID);
        if (node)
            rt_scene_node3d_set_material(node, material);
    }
    return obj;
}

/// @brief Assign `mesh` to every mesh-bearing node in the subtree rooted at `root`,
///   walked as an iterative depth-first traversal over an explicit heap stack (avoids
///   C-stack overflow on deep hierarchies). If no node in the subtree carries a mesh,
///   the mesh is assigned to `root` as a fallback. Traps on stack-allocation failure.
/// @return Count of nodes that received the mesh (>= 1 when `root` is non-NULL).
static int game3d_entity_set_mesh_subtree(rt_scene_node3d *root, void *mesh) {
    rt_scene_node3d **stack = NULL;
    size_t count = 0;
    size_t capacity = 0;
    int assigned = 0;
    if (!root)
        return 0;
    if (!scene_node_stack_push(&stack, &count, &capacity, root)) {
        rt_trap("Game3D.Entity3D.setMeshRecursive: traversal stack allocation failed");
        return 0;
    }
    while (count > 0) {
        rt_scene_node3d *node = stack[--count];
        int32_t child_count;
        if (node->mesh) {
            rt_scene_node3d_set_mesh(node, mesh);
            assigned++;
        }
        child_count = scene3d_node_child_count(node);
        node->child_count = child_count;
        for (int32_t i = child_count - 1; i >= 0; --i) {
            rt_scene_node3d *child = (rt_scene_node3d *)rt_g3d_checked_or_null(
                node->children[i], RT_G3D_SCENENODE3D_CLASS_ID);
            if (!child)
                continue;
            if (!scene_node_stack_push(&stack, &count, &capacity, child)) {
                free(stack);
                rt_trap("Game3D.Entity3D.setMeshRecursive: traversal stack allocation failed");
                return assigned;
            }
        }
    }
    free(stack);
    if (!assigned) {
        rt_scene_node3d_set_mesh(root, mesh);
        assigned = 1;
    }
    return assigned;
}

/// @brief Assign `material` to every node in the subtree rooted at `root`, walked as an
///   iterative depth-first traversal over an explicit heap stack (avoids C-stack overflow
///   on deep hierarchies). Traps on stack-allocation failure.
static void game3d_entity_set_material_subtree(rt_scene_node3d *root, void *material) {
    rt_scene_node3d **stack = NULL;
    size_t count = 0;
    size_t capacity = 0;
    if (!root)
        return;
    if (!scene_node_stack_push(&stack, &count, &capacity, root)) {
        rt_trap("Game3D.Entity3D.setMaterialRecursive: traversal stack allocation failed");
        return;
    }
    while (count > 0) {
        rt_scene_node3d *node = stack[--count];
        int32_t child_count;
        rt_scene_node3d_set_material(node, material);
        child_count = scene3d_node_child_count(node);
        node->child_count = child_count;
        for (int32_t i = child_count - 1; i >= 0; --i) {
            rt_scene_node3d *child = (rt_scene_node3d *)rt_g3d_checked_or_null(
                node->children[i], RT_G3D_SCENENODE3D_CLASS_ID);
            if (!child)
                continue;
            if (!scene_node_stack_push(&stack, &count, &capacity, child)) {
                free(stack);
                rt_trap("Game3D.Entity3D.setMaterialRecursive: traversal stack allocation failed");
                return;
            }
        }
    }
    free(stack);
}

/// @brief Fluent: assign the mesh (validated as Mesh3D) to the entity and propagate it
///   to every mesh-bearing node of its scene-node subtree; returns the entity.
void *rt_game3d_entity_set_mesh_recursive(void *obj, void *mesh) {
    rt_game3d_entity *entity =
        game3d_entity_checked(obj, "Game3D.Entity3D.setMeshRecursive: invalid entity");
    if (mesh && !rt_g3d_has_class(mesh, RT_G3D_MESH3D_CLASS_ID)) {
        rt_trap("Game3D.Entity3D.setMeshRecursive: mesh must be Mesh3D");
        return obj;
    }
    if (entity) {
        game3d_assign_typed_ref(&entity->mesh, mesh, RT_G3D_MESH3D_CLASS_ID);
        game3d_entity_set_mesh_subtree((rt_scene_node3d *)game3d_entity_node_ref(entity), mesh);
    }
    return obj;
}

/// @brief Fluent: assign the material (validated as Material3D) to the entity and
///   propagate it to every node of its scene-node subtree; returns the entity.
void *rt_game3d_entity_set_material_recursive(void *obj, void *material) {
    rt_game3d_entity *entity =
        game3d_entity_checked(obj, "Game3D.Entity3D.setMaterialRecursive: invalid entity");
    if (material && !rt_g3d_has_class(material, RT_G3D_MATERIAL3D_CLASS_ID)) {
        rt_trap("Game3D.Entity3D.setMaterialRecursive: material must be Material3D");
        return obj;
    }
    if (entity) {
        game3d_assign_typed_ref(&entity->material, material, RT_G3D_MATERIAL3D_CLASS_ID);
        game3d_entity_set_material_subtree(
            (rt_scene_node3d *)game3d_entity_node_ref(entity), material);
    }
    return obj;
}

/// @brief Restore a previously valid body after attachBody failed partway through.
static int game3d_entity_restore_body_binding(rt_game3d_entity *entity,
                                              rt_game3d_world *world,
                                              void *old_body) {
    void *node = game3d_entity_node_ref(entity);
    if (!entity)
        return 0;
    game3d_assign_typed_ref(&entity->body, old_body, RT_G3D_BODY3D_CLASS_ID);
    if (!old_body) {
        if (node)
            rt_scene_node3d_clear_body_binding(node);
        return 1;
    }
    rt_body3d_set_collision_layer(old_body, entity->layer);
    rt_body3d_set_collision_mask(old_body, entity->collision_mask_bits);
    if (node)
        rt_scene_node3d_bind_body(node, old_body);
    if (entity->spawned && world && world->physics) {
        int old_body_already_in_world = rt_world3d_contains_body(world->physics, old_body) ? 1 : 0;
        int restored_old_body = old_body_already_in_world;
        if (!restored_old_body)
            restored_old_body = rt_world3d_try_add(world->physics, old_body);
        if (!restored_old_body || !game3d_world_body_index_add(world, entity)) {
            if (!old_body_already_in_world)
                rt_world3d_remove(world->physics, old_body);
            if (node)
                rt_scene_node3d_clear_body_binding(node);
            game3d_assign_typed_ref(&entity->body, NULL, RT_G3D_BODY3D_CLASS_ID);
            return 0;
        }
    }
    return 1;
}

/// @brief Fluent: parent `child_obj` under this entity (retaining it and linking the
///   nodes), mark this entity a group, and return it.
void *rt_game3d_entity_add_child(void *obj, void *child_obj) {
    rt_game3d_entity *entity =
        game3d_entity_checked(obj, "Game3D.Entity3D.addChild: invalid entity");
    rt_game3d_entity *child =
        game3d_entity_checked(child_obj, "Game3D.Entity3D.addChild: child must be Entity3D");
    if (!entity || !child)
        return obj;
    if (entity == child) {
        rt_trap("Game3D.Entity3D.addChild: entity cannot be its own child");
        return obj;
    }
    if (entity->destroyed || child->destroyed) {
        rt_trap("Game3D.Entity3D.addChild: destroyed entities cannot be parented");
        return obj;
    }
    if (game3d_entity_has_ancestor(entity, child)) {
        rt_trap("Game3D.Entity3D.addChild: parenting would create a cycle");
        return obj;
    }
    if (child->parent == entity || game3d_entity_find_child_index(entity, child) >= 0)
        return obj;
    if (entity->spawned && child->spawned && child->world != entity->world) {
        rt_trap("Game3D.Entity3D.addChild: child already belongs to another world");
        return obj;
    }
    if (!entity->spawned && child->spawned) {
        rt_trap(
            "Game3D.Entity3D.addChild: spawned child cannot be parented under an unspawned entity");
        return obj;
    }
    entity->child_count = game3d_entity_child_count(entity);
    if (entity->child_count == INT32_MAX ||
        !game3d_entity_grow_children(entity, entity->child_count + 1)) {
        rt_trap("Game3D.Entity3D.addChild: allocation failed");
        return obj;
    }
    rt_obj_retain_maybe(child);
    game3d_entity_detach_from_parent(child);
    entity->children[entity->child_count++] = child;
    child->parent = entity;
    void *entity_node = game3d_entity_node_ref(entity);
    void *child_node = game3d_entity_node_ref(child);
    if (entity_node && child_node) {
        if (!rt_scene_node3d_try_add_child(entity_node, child_node)) {
            entity->children[--entity->child_count] = NULL;
            child->parent = NULL;
            game3d_release_ref((void **)&child);
            rt_trap("Game3D.Entity3D.addChild: scene-node parenting failed");
            return obj;
        }
    }
    if (entity->spawned && entity->world && !child->spawned) {
        rt_game3d_world *world = (rt_game3d_world *)entity->world;
        int64_t next_id = world->next_entity_id;
        if (!game3d_world_spawn_entity_tree(world, child, 0, &next_id)) {
            int32_t child_count;
            entity_node = game3d_entity_node_ref(entity);
            child_node = game3d_entity_node_ref(child);
            if (entity_node && child_node && rt_scene_node3d_get_parent(child_node) == entity_node)
                rt_scene_node3d_remove_child(entity_node, child_node);
            child_count = game3d_entity_child_count(entity);
            entity->child_count = child_count;
            for (int32_t i = 0; i < child_count; ++i) {
                if (entity->children[i] != child)
                    continue;
                for (int32_t j = i; j < entity->child_count - 1; ++j)
                    entity->children[j] = entity->children[j + 1];
                entity->children[--entity->child_count] = NULL;
                break;
            }
            child->parent = NULL;
            game3d_release_ref((void **)&child);
            return obj;
        }
        world->next_entity_id = next_id;
    }
    entity->group = 1;
    return obj;
}

/// @brief True if the entity is a group (explicitly flagged or has children).
int8_t rt_game3d_entity_is_group(void *obj) {
    rt_game3d_entity *entity =
        game3d_entity_checked(obj, "Game3D.Entity3D.isGroup: invalid entity");
    return entity && (entity->group || game3d_entity_child_count(entity) > 0) ? 1 : 0;
}

/// @brief Fluent: assign the display name (NULL becomes ""), mirror it onto the node,
///   and return the entity.
void *rt_game3d_entity_set_name(void *obj, rt_string name) {
    rt_game3d_entity *entity =
        game3d_entity_checked(obj, "Game3D.Entity3D.setName: invalid entity");
    if (!name)
        name = rt_const_cstr("");
    if (entity) {
        game3d_assign_ref((void **)&entity->name, name);
        void *node = game3d_entity_node_ref(entity);
        if (node)
            rt_scene_node3d_set_name(node, name);
        if (entity->spawned && entity->world)
            ((rt_game3d_world *)entity->world)->name_index_valid = 0;
    }
    return obj;
}

/// @brief Fluent: set the collision layer (must be a single bit), propagate to the
///   body if any, and return the entity.
void *rt_game3d_entity_set_layer(void *obj, int64_t layer) {
    rt_game3d_entity *entity =
        game3d_entity_checked(obj, "Game3D.Entity3D.setLayer: invalid entity");
    if (!game3d_valid_layer(layer)) {
        rt_trap("Game3D.Entity3D.setLayer: layer must be a single positive bit");
        return obj;
    }
    if (entity) {
        entity->layer = layer;
        void *body = game3d_entity_body_ref(entity);
        if (body)
            rt_body3d_set_collision_layer(body, layer);
    }
    return obj;
}

/// @brief Fluent: copy a LayerMask's bits into the entity's collision mask, propagate
///   to the body if any, and return the entity.
void *rt_game3d_entity_set_collision_mask(void *obj, void *mask_obj) {
    rt_game3d_entity *entity =
        game3d_entity_checked(obj, "Game3D.Entity3D.setCollisionMask: invalid entity");
    rt_game3d_layermask *mask =
        game3d_layermask_checked(mask_obj, "Game3D.Entity3D.setCollisionMask: invalid mask");
    if (entity && mask) {
        entity->collision_mask_bits = mask->bits;
        void *body = game3d_entity_body_ref(entity);
        if (body)
            rt_body3d_set_collision_mask(body, entity->collision_mask_bits);
    }
    return obj;
}

/// @brief Fluent: attach a physics body to the entity and return it.
/// @details Accepts either a ready Physics3DBody or a BodyDef (which is materialized
///   into a body here). Swaps out any previously attached body from the physics world,
///   adopts the def's layer/mask if present, seeds the body position from the node's
///   world position, binds node↔body with the def's sync mode, and re-adds the body to
///   the physics world when the entity is already spawned. Passing NULL clears the body
///   binding. Traps if `body_or_def` is neither a Physics3DBody nor a BodyDef.
void *rt_game3d_entity_attach_body(void *obj, void *body_or_def) {
    rt_game3d_entity *entity =
        game3d_entity_checked(obj, "Game3D.Entity3D.attachBody: invalid entity");
    void *body = body_or_def;
    void *created_body = NULL;
    rt_game3d_body_def *def =
        (rt_game3d_body_def *)rt_g3d_checked_or_null(body_or_def, RT_G3D_GAME3D_BODYDEF_CLASS_ID);
    if (def) {
        created_body = game3d_body_def_create_body(def);
        body = created_body;
    } else if (body && !rt_g3d_has_class(body, RT_G3D_BODY3D_CLASS_ID)) {
        rt_trap("Game3D.Entity3D.attachBody: expected Physics3DBody or BodyDef");
        return obj;
    }
    if (entity) {
        rt_game3d_world *world = (rt_game3d_world *)entity->world;
        void *old_body = game3d_entity_body_ref(entity);
        void *node = game3d_entity_node_ref(entity);
        int64_t old_layer = entity->layer;
        int64_t old_mask_bits = entity->collision_mask_bits;
        int body_is_old = body && body == old_body;
        rt_obj_retain_maybe(old_body);
        if (body && body != old_body && entity->spawned && world && world->physics) {
            rt_game3d_entity *owner = game3d_world_find_entity_by_body(world, body);
            if (owner && owner != entity) {
                game3d_release_ref(&old_body);
                game3d_release_ref(&created_body);
                rt_trap("Game3D.Entity3D.attachBody: body is already attached to another entity");
                return obj;
            }
        }
        if (old_body && old_body != body && entity->spawned && world && world->physics) {
            rt_world3d_remove(world->physics, old_body);
            game3d_world_body_index_remove(world, old_body);
        }
        if (def && def->has_layer)
            entity->layer = def->layer;
        if (def && def->has_mask)
            entity->collision_mask_bits = def->mask_bits;
        game3d_assign_typed_ref(&entity->body, body, RT_G3D_BODY3D_CLASS_ID);
        if (body) {
            rt_body3d_set_collision_layer(body, entity->layer);
            rt_body3d_set_collision_mask(body, entity->collision_mask_bits);
            if (node) {
                if (def)
                    rt_scene_node3d_set_sync_mode(node, def->sync_mode);
                rt_scene_node3d_bind_body(node, body);
                game3d_sync_body_from_entity_node(entity, 1);
            }
            if (entity->spawned && world && world->physics) {
                int body_in_world =
                    body_is_old && rt_world3d_contains_body(world->physics, body) ? 1 : 0;
                if (!body_in_world && !rt_world3d_try_add(world->physics, body)) {
                    if (node)
                        rt_scene_node3d_clear_body_binding(node);
                    entity->layer = old_layer;
                    entity->collision_mask_bits = old_mask_bits;
                    (void)game3d_entity_restore_body_binding(entity, world, old_body);
                    game3d_release_ref(&old_body);
                    game3d_release_ref(&created_body);
                    rt_trap("Game3D.Entity3D.attachBody: physics world add failed");
                    return obj;
                }
                if (!game3d_world_body_index_add(world, entity)) {
                    if (!body_in_world)
                        rt_world3d_remove(world->physics, body);
                    if (node)
                        rt_scene_node3d_clear_body_binding(node);
                    entity->layer = old_layer;
                    entity->collision_mask_bits = old_mask_bits;
                    (void)game3d_entity_restore_body_binding(entity, world, old_body);
                    game3d_release_ref(&old_body);
                    game3d_release_ref(&created_body);
                    rt_trap("Game3D.Entity3D.attachBody: body index allocation failed");
                    return obj;
                }
            }
        } else if (node) {
            rt_scene_node3d_clear_body_binding(node);
        }
        game3d_release_ref(&old_body);
    }
    game3d_release_ref(&created_body);
    return obj;
}

/// @brief Apply a linear impulse to the entity's body; traps if it has no body.
void rt_game3d_entity_apply_impulse(void *obj, double x, double y, double z) {
    rt_game3d_entity *entity =
        game3d_entity_checked(obj, "Game3D.Entity3D.applyImpulse: invalid entity");
    void *body = game3d_entity_body_ref(entity);
    if (!body) {
        rt_trap("Game3D.Entity3D.applyImpulse: entity has no body");
        return;
    }
    rt_body3d_apply_impulse(body,
                            game3d_clamp_abs_or(x, 0.0, RT_GAME3D_CONTROLLER_SPEED_MAX),
                            game3d_clamp_abs_or(y, 0.0, RT_GAME3D_CONTROLLER_SPEED_MAX),
                            game3d_clamp_abs_or(z, 0.0, RT_GAME3D_CONTROLLER_SPEED_MAX));
}

/// @brief Set the entity body's linear velocity; traps if it has no body.
void rt_game3d_entity_set_velocity(void *obj, double x, double y, double z) {
    rt_game3d_entity *entity =
        game3d_entity_checked(obj, "Game3D.Entity3D.setVelocity: invalid entity");
    void *body = game3d_entity_body_ref(entity);
    if (!body) {
        rt_trap("Game3D.Entity3D.setVelocity: entity has no body");
        return;
    }
    rt_body3d_set_velocity(body,
                           game3d_clamp_abs_or(x, 0.0, RT_GAME3D_CONTROLLER_SPEED_MAX),
                           game3d_clamp_abs_or(y, 0.0, RT_GAME3D_CONTROLLER_SPEED_MAX),
                           game3d_clamp_abs_or(z, 0.0, RT_GAME3D_CONTROLLER_SPEED_MAX));
}

/// @brief Get the entity's local position as a Vec3 (origin if no node).
void *rt_game3d_entity_position(void *obj) {
    rt_game3d_entity *entity =
        game3d_entity_checked(obj, "Game3D.Entity3D.position: invalid entity");
    void *node = game3d_entity_node_ref(entity);
    return node ? rt_scene_node3d_get_position(node) : rt_vec3_new(0, 0, 0);
}

/// @brief Get the entity's world-space position as a Vec3 (origin if no node).
void *rt_game3d_entity_world_position(void *obj) {
    rt_game3d_entity *entity =
        game3d_entity_checked(obj, "Game3D.Entity3D.worldPosition: invalid entity");
    void *node = game3d_entity_node_ref(entity);
    return node ? rt_scene_node3d_get_world_position(node) : rt_vec3_new(0, 0, 0);
}

/// @brief Read an entity's world-space position into out x/y/z (resolving body or node as
/// appropriate).
/// @return 1 on success, 0 if the entity has no resolvable transform.
int game3d_entity_world_position_components(rt_game3d_entity *entity, double out_pos[3]) {
    int8_t ok;
    if (out_pos) {
        out_pos[0] = 0.0;
        out_pos[1] = 0.0;
        out_pos[2] = 0.0;
    }
    void *node = game3d_entity_node_ref(entity);
    if (!node || !out_pos)
        return 0;
    ok = rt_scene_node3d_get_world_position_components(node, &out_pos[0], &out_pos[1], &out_pos[2]);
    if (!ok)
        return 0;
    out_pos[0] = game3d_clamp_coord_or(out_pos[0], 0.0);
    out_pos[1] = game3d_clamp_coord_or(out_pos[1], 0.0);
    out_pos[2] = game3d_clamp_coord_or(out_pos[2], 0.0);
    return 1;
}

/// @brief True if the entity is currently spawned into a world.
int8_t rt_game3d_entity_is_spawned(void *obj) {
    rt_game3d_entity *entity =
        game3d_entity_checked_allow_destroyed(obj, "Game3D.Entity3D.isSpawned: invalid entity");
    return entity && entity->spawned ? 1 : 0;
}

/// @brief True if the entity has been despawned/destroyed.
int8_t rt_game3d_entity_is_destroyed(void *obj) {
    rt_game3d_entity *entity =
        game3d_entity_checked_allow_destroyed(obj, "Game3D.Entity3D.isDestroyed: invalid entity");
    return entity && entity->destroyed ? 1 : 0;
}
