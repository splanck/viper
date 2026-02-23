//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_scene.h
// Purpose: Scene graph for hierarchical sprite management with local and world transforms computed by composing ancestor transforms.
//
// Key invariants:
//   - Each scene node has at most one parent; the root has no parent.
//   - World transform is derived by composing all ancestor local transforms.
//   - Adding a child transfers logical ownership to the parent node.
//   - Removing or destroying a node also destroys all its descendants.
//
// Ownership/Lifetime:
//   - Scene and SceneNode objects are runtime-managed.
//   - The scene owns the node tree; callers should not free individual nodes directly.
//
// Links: src/runtime/graphics/rt_scene.c (implementation), src/runtime/graphics/rt_camera.h, src/runtime/graphics/rt_graphics.h
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
    /// @return New SceneNode object with default transform (position 0,0;
    ///         scale 100%; rotation 0; visible).
    void *rt_scene_node_new(void);

    /// @brief Create a scene node with a sprite.
    /// @param sprite Sprite to attach to this node.
    /// @return New SceneNode object with sprite attached.
    void *rt_scene_node_from_sprite(void *sprite);

    //=========================================================================
    // Scene Node Properties
    //=========================================================================

    /// @brief Get node local X position (relative to parent).
    /// @param node The SceneNode object.
    /// @return The node's local X position in pixels.
    int64_t rt_scene_node_get_x(void *node);

    /// @brief Set node local X position (relative to parent).
    /// @param node The SceneNode object.
    /// @param x The new local X position in pixels.
    void rt_scene_node_set_x(void *node, int64_t x);

    /// @brief Get node local Y position (relative to parent).
    /// @param node The SceneNode object.
    /// @return The node's local Y position in pixels.
    int64_t rt_scene_node_get_y(void *node);

    /// @brief Set node local Y position (relative to parent).
    /// @param node The SceneNode object.
    /// @param y The new local Y position in pixels.
    void rt_scene_node_set_y(void *node, int64_t y);

    /// @brief Get node world X position (absolute).
    /// @param node The SceneNode object.
    /// @return The node's computed world X position, accounting for all
    ///         ancestor translations.
    int64_t rt_scene_node_get_world_x(void *node);

    /// @brief Get node world Y position (absolute).
    /// @param node The SceneNode object.
    /// @return The node's computed world Y position, accounting for all
    ///         ancestor translations.
    int64_t rt_scene_node_get_world_y(void *node);

    /// @brief Get node local scale X (100 = 100%).
    /// @param node The SceneNode object.
    /// @return The node's local X scale factor (100 = unscaled).
    int64_t rt_scene_node_get_scale_x(void *node);

    /// @brief Set node local scale X (100 = 100%).
    /// @param node The SceneNode object.
    /// @param scale The new local X scale factor (100 = unscaled).
    void rt_scene_node_set_scale_x(void *node, int64_t scale);

    /// @brief Get node local scale Y (100 = 100%).
    /// @param node The SceneNode object.
    /// @return The node's local Y scale factor (100 = unscaled).
    int64_t rt_scene_node_get_scale_y(void *node);

    /// @brief Set node local scale Y (100 = 100%).
    /// @param node The SceneNode object.
    /// @param scale The new local Y scale factor (100 = unscaled).
    void rt_scene_node_set_scale_y(void *node, int64_t scale);

    /// @brief Get node world scale X (combined with ancestors).
    /// @param node The SceneNode object.
    /// @return The node's computed world X scale, combining all ancestor
    ///         scale factors.
    int64_t rt_scene_node_get_world_scale_x(void *node);

    /// @brief Get node world scale Y (combined with ancestors).
    /// @param node The SceneNode object.
    /// @return The node's computed world Y scale, combining all ancestor
    ///         scale factors.
    int64_t rt_scene_node_get_world_scale_y(void *node);

    /// @brief Get node local rotation in degrees.
    /// @param node The SceneNode object.
    /// @return The node's local rotation angle in degrees.
    int64_t rt_scene_node_get_rotation(void *node);

    /// @brief Set node local rotation in degrees.
    /// @param node The SceneNode object.
    /// @param degrees The new local rotation angle in degrees.
    void rt_scene_node_set_rotation(void *node, int64_t degrees);

    /// @brief Get node world rotation (combined with ancestors).
    /// @param node The SceneNode object.
    /// @return The node's computed world rotation in degrees, combining
    ///         all ancestor rotations.
    int64_t rt_scene_node_get_world_rotation(void *node);

    /// @brief Get node visibility.
    /// @param node The SceneNode object.
    /// @return 1 if the node is visible, 0 if hidden.
    int8_t rt_scene_node_get_visible(void *node);

    /// @brief Set node visibility (affects children too).
    /// @param node The SceneNode object.
    /// @param visible 1 to make visible, 0 to hide (hides all descendants).
    void rt_scene_node_set_visible(void *node, int8_t visible);

    /// @brief Get node depth (Z-order for sorting).
    /// @param node The SceneNode object.
    /// @return The node's depth value used for draw-order sorting.
    int64_t rt_scene_node_get_depth(void *node);

    /// @brief Set node depth (higher values drawn later/on top).
    /// @param node The SceneNode object.
    /// @param depth The new depth value; higher values are drawn on top
    ///              of lower values during scene rendering.
    void rt_scene_node_set_depth(void *node, int64_t depth);

    /// @brief Get node name/tag for identification.
    /// @param node The SceneNode object.
    /// @return The node's name string, or an empty string if unnamed.
    rt_string rt_scene_node_get_name(void *node);

    /// @brief Set node name/tag for identification.
    /// @param node The SceneNode object.
    /// @param name The name string to assign to this node.
    void rt_scene_node_set_name(void *node, rt_string name);

    /// @brief Get the sprite attached to this node.
    /// @param node The SceneNode object.
    /// @return The attached sprite object, or NULL if no sprite is attached.
    void *rt_scene_node_get_sprite(void *node);

    /// @brief Attach a sprite to this node.
    /// @param node The SceneNode object.
    /// @param sprite The sprite to attach, or NULL to detach the current sprite.
    void rt_scene_node_set_sprite(void *node, void *sprite);

    //=========================================================================
    // Scene Node Hierarchy
    //=========================================================================

    /// @brief Add a child node.
    /// @param node Parent node.
    /// @param child Child node to add. The child is detached from any previous
    ///              parent before being added.
    void rt_scene_node_add_child(void *node, void *child);

    /// @brief Remove a child node.
    /// @param node Parent node.
    /// @param child Child node to remove. The child's parent becomes NULL.
    void rt_scene_node_remove_child(void *node, void *child);

    /// @brief Get the number of children.
    /// @param node The SceneNode object.
    /// @return The number of direct child nodes.
    int64_t rt_scene_node_child_count(void *node);

    /// @brief Get a child by index.
    /// @param node The parent SceneNode.
    /// @param index Zero-based index of the child (0 to child_count-1).
    /// @return The child SceneNode at @p index, or NULL if out of range.
    void *rt_scene_node_get_child(void *node, int64_t index);

    /// @brief Get the parent node.
    /// @param node The SceneNode object.
    /// @return The parent SceneNode, or NULL if this node has no parent
    ///         (i.e., it is a root node).
    void *rt_scene_node_get_parent(void *node);

    /// @brief Find a descendant node by name.
    /// @param node Starting node to search from (searches this node's subtree).
    /// @param name Name to search for.
    /// @return First matching node, or NULL if not found.
    void *rt_scene_node_find(void *node, rt_string name);

    /// @brief Remove this node from its parent.
    /// @param node The SceneNode to detach. After detaching, the node's
    ///             parent becomes NULL.
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
    /// @param node The SceneNode to update. Recursively updates all
    ///             descendant nodes.
    void rt_scene_node_update(void *node);

    /// @brief Move the node by delta amounts.
    /// @param node The SceneNode to move.
    /// @param dx Horizontal displacement in pixels to add to the current
    ///           X position.
    /// @param dy Vertical displacement in pixels to add to the current
    ///           Y position.
    void rt_scene_node_move(void *node, int64_t dx, int64_t dy);

    /// @brief Set both position components at once.
    /// @param node The SceneNode object.
    /// @param x The new local X position in pixels.
    /// @param y The new local Y position in pixels.
    void rt_scene_node_set_position(void *node, int64_t x, int64_t y);

    /// @brief Set both scale components at once.
    /// @param node The SceneNode object.
    /// @param scale The uniform scale factor to apply to both X and Y
    ///              (100 = unscaled).
    void rt_scene_node_set_scale(void *node, int64_t scale);

    //=========================================================================
    // Scene (Root Container)
    //=========================================================================

    /// @brief Create a new scene (root container for nodes).
    /// @return A new Scene object with an empty root node.
    void *rt_scene_new(void);

    /// @brief Get the root node of a scene.
    /// @param scene The Scene object.
    /// @return The root SceneNode that serves as the top of the scene
    ///         hierarchy.
    void *rt_scene_get_root(void *scene);

    /// @brief Add a node to the scene root.
    /// @param scene The Scene object.
    /// @param node The SceneNode to add as a child of the scene root.
    void rt_scene_add(void *scene, void *node);

    /// @brief Remove a node from the scene.
    /// @param scene The Scene object.
    /// @param node The SceneNode to remove from the scene root.
    void rt_scene_remove(void *scene, void *node);

    /// @brief Find a node in the scene by name.
    /// @param scene The Scene object.
    /// @param name The name to search for in the entire scene hierarchy.
    /// @return The first matching SceneNode, or NULL if not found.
    void *rt_scene_find(void *scene, rt_string name);

    /// @brief Draw all nodes in the scene (depth-sorted).
    /// @param scene The Scene object.
    /// @param canvas The canvas to draw all visible nodes onto, sorted
    ///               by depth.
    void rt_scene_draw(void *scene, void *canvas);

    /// @brief Draw scene with camera transform (depth-sorted).
    /// @param scene The Scene object.
    /// @param canvas The canvas to draw onto.
    /// @param camera The camera providing the world-to-screen transform.
    void rt_scene_draw_with_camera(void *scene, void *canvas, void *camera);

    /// @brief Update all nodes in the scene.
    /// @param scene The Scene object. Recursively updates all nodes in
    ///              the hierarchy.
    void rt_scene_update(void *scene);

    /// @brief Get number of nodes in the scene.
    /// @param scene The Scene object.
    /// @return The total number of nodes in the scene hierarchy (excluding
    ///         the root node).
    int64_t rt_scene_node_count(void *scene);

    /// @brief Clear all nodes from the scene.
    /// @param scene The Scene object. Removes all child nodes from the
    ///              scene root.
    void rt_scene_clear(void *scene);

#ifdef __cplusplus
}
#endif
