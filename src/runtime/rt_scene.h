//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_scene.h
// Purpose: Scene graph for hierarchical sprite management.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    //=========================================================================
    // Scene Node Creation
    //=========================================================================

    /// @brief Create a new scene node.
    /// @return New SceneNode object.
    void *rt_scene_node_new(void);

    /// @brief Create a scene node with a sprite.
    /// @param sprite Sprite to attach to this node.
    /// @return New SceneNode object with sprite attached.
    void *rt_scene_node_from_sprite(void *sprite);

    //=========================================================================
    // Scene Node Properties
    //=========================================================================

    /// @brief Get node local X position (relative to parent).
    int64_t rt_scene_node_get_x(void *node);

    /// @brief Set node local X position (relative to parent).
    void rt_scene_node_set_x(void *node, int64_t x);

    /// @brief Get node local Y position (relative to parent).
    int64_t rt_scene_node_get_y(void *node);

    /// @brief Set node local Y position (relative to parent).
    void rt_scene_node_set_y(void *node, int64_t y);

    /// @brief Get node world X position (absolute).
    int64_t rt_scene_node_get_world_x(void *node);

    /// @brief Get node world Y position (absolute).
    int64_t rt_scene_node_get_world_y(void *node);

    /// @brief Get node local scale X (100 = 100%).
    int64_t rt_scene_node_get_scale_x(void *node);

    /// @brief Set node local scale X (100 = 100%).
    void rt_scene_node_set_scale_x(void *node, int64_t scale);

    /// @brief Get node local scale Y (100 = 100%).
    int64_t rt_scene_node_get_scale_y(void *node);

    /// @brief Set node local scale Y (100 = 100%).
    void rt_scene_node_set_scale_y(void *node, int64_t scale);

    /// @brief Get node world scale X (combined with ancestors).
    int64_t rt_scene_node_get_world_scale_x(void *node);

    /// @brief Get node world scale Y (combined with ancestors).
    int64_t rt_scene_node_get_world_scale_y(void *node);

    /// @brief Get node local rotation in degrees.
    int64_t rt_scene_node_get_rotation(void *node);

    /// @brief Set node local rotation in degrees.
    void rt_scene_node_set_rotation(void *node, int64_t degrees);

    /// @brief Get node world rotation (combined with ancestors).
    int64_t rt_scene_node_get_world_rotation(void *node);

    /// @brief Get node visibility.
    int8_t rt_scene_node_get_visible(void *node);

    /// @brief Set node visibility (affects children too).
    void rt_scene_node_set_visible(void *node, int8_t visible);

    /// @brief Get node depth (Z-order for sorting).
    int64_t rt_scene_node_get_depth(void *node);

    /// @brief Set node depth (higher values drawn later/on top).
    void rt_scene_node_set_depth(void *node, int64_t depth);

    /// @brief Get node name/tag for identification.
    rt_string rt_scene_node_get_name(void *node);

    /// @brief Set node name/tag for identification.
    void rt_scene_node_set_name(void *node, rt_string name);

    /// @brief Get the sprite attached to this node.
    void *rt_scene_node_get_sprite(void *node);

    /// @brief Attach a sprite to this node.
    void rt_scene_node_set_sprite(void *node, void *sprite);

    //=========================================================================
    // Scene Node Hierarchy
    //=========================================================================

    /// @brief Add a child node.
    /// @param node Parent node.
    /// @param child Child node to add.
    void rt_scene_node_add_child(void *node, void *child);

    /// @brief Remove a child node.
    /// @param node Parent node.
    /// @param child Child node to remove.
    void rt_scene_node_remove_child(void *node, void *child);

    /// @brief Get the number of children.
    int64_t rt_scene_node_child_count(void *node);

    /// @brief Get a child by index.
    void *rt_scene_node_get_child(void *node, int64_t index);

    /// @brief Get the parent node.
    void *rt_scene_node_get_parent(void *node);

    /// @brief Find a descendant node by name.
    /// @param node Starting node to search from.
    /// @param name Name to search for.
    /// @return First matching node, or NULL if not found.
    void *rt_scene_node_find(void *node, rt_string name);

    /// @brief Remove this node from its parent.
    void rt_scene_node_detach(void *node);

    //=========================================================================
    // Scene Node Methods
    //=========================================================================

    /// @brief Draw this node and all children to a canvas.
    /// @param node Node to draw.
    /// @param canvas Canvas to draw on.
    void rt_scene_node_draw(void *node, void *canvas);

    /// @brief Draw this node and children with camera transform.
    /// @param node Node to draw.
    /// @param canvas Canvas to draw on.
    /// @param camera Camera for world-to-screen transform.
    void rt_scene_node_draw_with_camera(void *node, void *canvas, void *camera);

    /// @brief Update node and all children (for animations).
    void rt_scene_node_update(void *node);

    /// @brief Move the node by delta amounts.
    void rt_scene_node_move(void *node, int64_t dx, int64_t dy);

    /// @brief Set both position components at once.
    void rt_scene_node_set_position(void *node, int64_t x, int64_t y);

    /// @brief Set both scale components at once.
    void rt_scene_node_set_scale(void *node, int64_t scale);

    //=========================================================================
    // Scene (Root Container)
    //=========================================================================

    /// @brief Create a new scene (root container for nodes).
    void *rt_scene_new(void);

    /// @brief Get the root node of a scene.
    void *rt_scene_get_root(void *scene);

    /// @brief Add a node to the scene root.
    void rt_scene_add(void *scene, void *node);

    /// @brief Remove a node from the scene.
    void rt_scene_remove(void *scene, void *node);

    /// @brief Find a node in the scene by name.
    void *rt_scene_find(void *scene, rt_string name);

    /// @brief Draw all nodes in the scene (depth-sorted).
    void rt_scene_draw(void *scene, void *canvas);

    /// @brief Draw scene with camera transform (depth-sorted).
    void rt_scene_draw_with_camera(void *scene, void *canvas, void *camera);

    /// @brief Update all nodes in the scene.
    void rt_scene_update(void *scene);

    /// @brief Get number of nodes in the scene.
    int64_t rt_scene_node_count(void *scene);

    /// @brief Clear all nodes from the scene.
    void rt_scene_clear(void *scene);

#ifdef __cplusplus
}
#endif
