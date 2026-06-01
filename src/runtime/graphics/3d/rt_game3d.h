//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/rt_game3d.h
// Purpose: Runtime-backed Viper.Game3D ergonomic layer over the lower-level
//   Graphics3D, Physics3D, input, audio, and post-FX primitives. Bundles a
//   window, camera, scene graph, physics world, input, audio, and effects into
//   a single World3D and exposes batteries-included entities, prefabs,
//   material/lighting/post-FX presets, and camera controllers.
//
// Key invariants:
//   - Every `void *` parameter/return is an opaque GC-managed runtime handle;
//     callers never dereference them directly.
//   - The integer-returning accessor families (layers/keys/mouse/etc.) surface
//     the RT_GAME3D_* enum constants below as callable functions so frontends
//     can bind them as read-only properties.
//   - Builder-style setters whose names lack `_prop` return their receiver to
//     allow fluent chaining; the `*_set_*_prop` variants are void property writers.
//   - Angles are in degrees, distances/positions in world units, time in seconds
//     unless a name says otherwise (e.g. `_ms`); colors are RGB channels 0.0–1.0.
//
// Ownership/Lifetime:
//   - World3D owns its canvas, camera, scene, physics, input, audio, and effects
//     sub-objects; destroying or GC-finalizing the world tears them all down.
//   - Entities, bodies, materials, meshes, and clips are GC-managed handles that
//     stay alive while referenced by the world or by frontend variables.
//   - Accessor functions return borrowed handles owned by their parent; callers
//     must not free them.
//
// Links: rt_game3d.c, rt_graphics3d_ids.h, render/rt_canvas3d.h,
//   physics/rt_physics3d.h, scene/rt_scene3d.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//=========================================================================
// Enum constants — mirrored by the integer accessor families below
//=========================================================================

/// @brief Collision/render layer bits used to build LayerMask filters.
enum {
    RT_GAME3D_LAYER_WORLD = 1,   ///< Static world geometry (ground, walls).
    RT_GAME3D_LAYER_DYNAMIC = 2, ///< Movable dynamic bodies (props, crates).
    RT_GAME3D_LAYER_PLAYER = 4,  ///< Player-controlled entities.
    RT_GAME3D_LAYER_TRIGGER = 8, ///< Non-solid trigger volumes.
    RT_GAME3D_LAYER_DEBRIS = 16, ///< Short-lived debris/particles.
};

/// @brief Rigid-body collision shape kinds selectable on a BodyDef.
enum {
    RT_GAME3D_BODY_SHAPE_BOX = 0,     ///< Axis-aligned box collider.
    RT_GAME3D_BODY_SHAPE_SPHERE = 1,  ///< Sphere collider.
    RT_GAME3D_BODY_SHAPE_CAPSULE = 2, ///< Upright capsule collider.
};

/// @brief Transform synchronization direction between a physics body and its scene node.
enum {
    RT_GAME3D_SYNC_NODE_FROM_BODY = 0,             ///< Physics drives the node (dynamic bodies).
    RT_GAME3D_SYNC_BODY_FROM_NODE = 1,             ///< Node drives the body (scripted/kinematic).
    RT_GAME3D_SYNC_NODE_FROM_ANIM_ROOT_MOTION = 2, ///< Animation root motion drives the node.
    RT_GAME3D_SYNC_TWO_WAY_KINEMATIC = 3,          ///< Bidirectional kinematic sync.
};

/// @brief Material alpha-handling mode.
enum {
    RT_GAME3D_ALPHA_OPAQUE = 0, ///< Fully opaque, depth-write on.
    RT_GAME3D_ALPHA_MASK = 1,   ///< Alpha-tested cutout (binary coverage).
    RT_GAME3D_ALPHA_BLEND = 2,  ///< Order-dependent translucent blending.
};

/// @brief Built-in surface shading models selectable per material.
enum {
    RT_GAME3D_SHADING_PHONG = 0,    ///< Classic Blinn-Phong specular.
    RT_GAME3D_SHADING_TOON = 1,     ///< Banded/cel cartoon shading.
    RT_GAME3D_SHADING_PBR = 2,      ///< Physically based metallic/roughness workflow.
    RT_GAME3D_SHADING_UNLIT = 3,    ///< Flat albedo, no lighting applied.
    RT_GAME3D_SHADING_FRESNEL = 4,  ///< Rim/Fresnel emphasis.
    RT_GAME3D_SHADING_EMISSIVE = 5, ///< Self-illuminated, unlit by scene lights.
};

/// @brief Render quality presets trading fidelity against performance.
enum {
    RT_GAME3D_QUALITY_PERFORMANCE = 0, ///< Lowest cost, effects trimmed.
    RT_GAME3D_QUALITY_BALANCED = 1,    ///< Default middle ground.
    RT_GAME3D_QUALITY_CINEMATIC = 2,   ///< Highest fidelity.
};

/// @brief Collision-event phase selector used when querying World3D events.
enum {
    RT_GAME3D_COLLISION_ENTER = 0, ///< First frame two colliders touch.
    RT_GAME3D_COLLISION_STAY = 1,  ///< Continuing contact frames.
    RT_GAME3D_COLLISION_EXIT = 2,  ///< Frame contact ends.
    RT_GAME3D_COLLISION_ANY = 3,   ///< Match any of the above phases.
};

//=========================================================================
// Layers — collision-layer bit constants (Viper.Game3D.Layers)
//=========================================================================

/// @brief Layer bit for static world geometry (RT_GAME3D_LAYER_WORLD).
int64_t rt_game3d_layers_world(void);
/// @brief Layer bit for movable dynamic bodies (RT_GAME3D_LAYER_DYNAMIC).
int64_t rt_game3d_layers_dynamic(void);
/// @brief Layer bit for player-controlled entities (RT_GAME3D_LAYER_PLAYER).
int64_t rt_game3d_layers_player(void);
/// @brief Layer bit for non-solid trigger volumes (RT_GAME3D_LAYER_TRIGGER).
int64_t rt_game3d_layers_trigger(void);
/// @brief Layer bit for short-lived debris/particles (RT_GAME3D_LAYER_DEBRIS).
int64_t rt_game3d_layers_debris(void);

//=========================================================================
// BodyShape — rigid-body shape kind constants (Viper.Game3D.BodyShape)
//=========================================================================

/// @brief Box shape kind constant (RT_GAME3D_BODY_SHAPE_BOX).
int64_t rt_game3d_body_shape_box(void);
/// @brief Sphere shape kind constant (RT_GAME3D_BODY_SHAPE_SPHERE).
int64_t rt_game3d_body_shape_sphere(void);
/// @brief Capsule shape kind constant (RT_GAME3D_BODY_SHAPE_CAPSULE).
int64_t rt_game3d_body_shape_capsule(void);

//=========================================================================
// SyncMode — body/node transform sync constants (Viper.Game3D.SyncMode)
//=========================================================================

/// @brief Sync constant: physics body drives the scene node.
int64_t rt_game3d_sync_mode_node_from_body(void);
/// @brief Sync constant: scene node drives the physics body.
int64_t rt_game3d_sync_mode_body_from_node(void);
/// @brief Sync constant: animation root motion drives the scene node.
int64_t rt_game3d_sync_mode_node_from_anim_root_motion(void);
/// @brief Sync constant: bidirectional kinematic body/node coupling.
int64_t rt_game3d_sync_mode_two_way_kinematic(void);

//=========================================================================
// AlphaMode — material alpha mode constants (Viper.Game3D.AlphaMode)
//=========================================================================

/// @brief Opaque alpha-mode constant (RT_GAME3D_ALPHA_OPAQUE).
int64_t rt_game3d_alpha_mode_opaque(void);
/// @brief Alpha-tested cutout mode constant (RT_GAME3D_ALPHA_MASK).
int64_t rt_game3d_alpha_mode_mask(void);
/// @brief Translucent blend mode constant (RT_GAME3D_ALPHA_BLEND).
int64_t rt_game3d_alpha_mode_blend(void);

//=========================================================================
// ShadingModel — surface shading model constants (Viper.Game3D.ShadingModel)
//=========================================================================

/// @brief Blinn-Phong shading-model constant.
int64_t rt_game3d_shading_model_phong(void);
/// @brief Toon/cel shading-model constant.
int64_t rt_game3d_shading_model_toon(void);
/// @brief Physically based (PBR) shading-model constant.
int64_t rt_game3d_shading_model_pbr(void);
/// @brief Fresnel/rim shading-model constant.
int64_t rt_game3d_shading_model_fresnel(void);
/// @brief Emissive (self-lit) shading-model constant.
int64_t rt_game3d_shading_model_emissive(void);
/// @brief Unlit flat-albedo shading-model constant.
int64_t rt_game3d_shading_model_unlit(void);

//=========================================================================
// Quality — render quality preset constants (Viper.Game3D.Quality)
//=========================================================================

/// @brief Performance quality preset constant.
int64_t rt_game3d_quality_performance(void);
/// @brief Balanced quality preset constant.
int64_t rt_game3d_quality_balanced(void);
/// @brief Cinematic quality preset constant.
int64_t rt_game3d_quality_cinematic(void);

//=========================================================================
// CollisionPhase — collision-event phase constants (Viper.Game3D.CollisionPhase)
//=========================================================================

/// @brief Enter-phase constant (first contact frame).
int64_t rt_game3d_collision_enter(void);
/// @brief Stay-phase constant (ongoing contact frames).
int64_t rt_game3d_collision_stay(void);
/// @brief Exit-phase constant (contact-end frame).
int64_t rt_game3d_collision_exit(void);
/// @brief Wildcard constant matching any collision phase.
int64_t rt_game3d_collision_any(void);

//=========================================================================
// Keys — keyboard key-code constants (Viper.Game3D.Keys)
//=========================================================================

/// @brief Key code for the W key.
int64_t rt_game3d_key_w(void);
/// @brief Key code for the A key.
int64_t rt_game3d_key_a(void);
/// @brief Key code for the S key.
int64_t rt_game3d_key_s(void);
/// @brief Key code for the D key.
int64_t rt_game3d_key_d(void);
/// @brief Key code for the spacebar.
int64_t rt_game3d_key_space(void);
/// @brief Key code for the Escape key.
int64_t rt_game3d_key_escape(void);
/// @brief Key code for the Shift modifier.
int64_t rt_game3d_key_shift(void);
/// @brief Key code for the Ctrl modifier.
int64_t rt_game3d_key_ctrl(void);
/// @brief Key code for the Up arrow.
int64_t rt_game3d_key_up(void);
/// @brief Key code for the Down arrow.
int64_t rt_game3d_key_down(void);
/// @brief Key code for the Left arrow.
int64_t rt_game3d_key_left(void);
/// @brief Key code for the Right arrow.
int64_t rt_game3d_key_right(void);

//=========================================================================
// MouseButtons — mouse-button code constants (Viper.Game3D.MouseButtons)
//=========================================================================

/// @brief Button code for the left mouse button.
int64_t rt_game3d_mouse_left(void);
/// @brief Button code for the right mouse button.
int64_t rt_game3d_mouse_right(void);
/// @brief Button code for the middle (wheel) mouse button.
int64_t rt_game3d_mouse_middle(void);
/// @brief Button code for the first extra (X1) mouse button.
int64_t rt_game3d_mouse_x1(void);
/// @brief Button code for the second extra (X2) mouse button.
int64_t rt_game3d_mouse_x2(void);

//=========================================================================
// LayerMask — collision-filter bitmask object (Viper.Game3D.LayerMask)
//=========================================================================

/// @brief Create an empty layer mask that matches no layers.
void *rt_game3d_layermask_none(void);
/// @brief Create a layer mask with every layer bit set.
void *rt_game3d_layermask_all(void);
/// @brief Create a layer mask containing exactly the given layer bit.
void *rt_game3d_layermask_of(int64_t layer);
/// @brief Get the raw bitfield backing a layer mask.
int64_t rt_game3d_layermask_get_bits(void *mask);
/// @brief Overwrite the raw bitfield backing a layer mask.
void rt_game3d_layermask_set_bits(void *mask, int64_t bits);
/// @brief Return a mask with the given layer added (fluent; may return a new handle).
void *rt_game3d_layermask_include(void *mask, int64_t layer);
/// @brief Test whether the mask includes the given layer bit.
int8_t rt_game3d_layermask_includes(void *mask, int64_t layer);

//=========================================================================
// BodyDef — rigid-body construction descriptor (Viper.Game3D.BodyDef)
//=========================================================================

/// @brief Build a dynamic box body definition from half-extents and mass.
void *rt_game3d_body_def_box(double half_x, double half_y, double half_z, double mass);
/// @brief Build a dynamic sphere body definition from radius and mass.
void *rt_game3d_body_def_sphere(double radius, double mass);
/// @brief Build a dynamic capsule body definition from radius, height, and mass.
void *rt_game3d_body_def_capsule(double radius, double height, double mass);
/// @brief Build a static (immovable) box body definition from half-extents.
void *rt_game3d_body_def_static_box(double half_x, double half_y, double half_z);
/// @brief Build a static ground-plane body definition spanning the given size.
void *rt_game3d_body_def_static_plane(double size);
/// @brief Get the collision shape kind (RT_GAME3D_BODY_SHAPE_*) of a body def.
int64_t rt_game3d_body_def_get_shape(void *def);
/// @brief Set the collision shape kind of a body def.
void rt_game3d_body_def_set_shape(void *def, int64_t shape);
/// @brief Get the body mass in kilograms.
double rt_game3d_body_def_get_mass(void *def);
/// @brief Set the body mass in kilograms.
void rt_game3d_body_def_set_mass(void *def, double mass);
/// @brief Get the surface friction coefficient.
double rt_game3d_body_def_get_friction(void *def);
/// @brief Set the surface friction coefficient.
void rt_game3d_body_def_set_friction(void *def, double friction);
/// @brief Get the restitution (bounciness) coefficient.
double rt_game3d_body_def_get_restitution(void *def);
/// @brief Set the restitution (bounciness) coefficient.
void rt_game3d_body_def_set_restitution(void *def, double restitution);
/// @brief True if the body is static (never integrated by the solver).
int8_t rt_game3d_body_def_get_static(void *def);
/// @brief Mark the body static (true) or dynamic (false).
void rt_game3d_body_def_set_static(void *def, int8_t is_static);
/// @brief True if the body is kinematic (script-driven, infinite mass).
int8_t rt_game3d_body_def_get_kinematic(void *def);
/// @brief Mark the body kinematic (true) or simulated (false).
void rt_game3d_body_def_set_kinematic(void *def, int8_t is_kinematic);
/// @brief True if the body is a non-solid trigger (overlap events only).
int8_t rt_game3d_body_def_get_trigger(void *def);
/// @brief Mark the body as a trigger volume (true) or solid collider (false).
void rt_game3d_body_def_set_trigger(void *def, int8_t is_trigger);
/// @brief True if continuous collision detection (CCD) is enabled.
int8_t rt_game3d_body_def_get_use_ccd(void *def);
/// @brief Enable or disable continuous collision detection for fast bodies.
void rt_game3d_body_def_set_use_ccd(void *def, int8_t use_ccd);
/// @brief Get the collision layer this body belongs to.
int64_t rt_game3d_body_def_get_layer(void *def);
/// @brief Set the collision layer this body belongs to (property setter).
void rt_game3d_body_def_set_layer_prop(void *def, int64_t layer);
/// @brief Get the collision mask selecting which layers this body collides with.
void *rt_game3d_body_def_get_mask(void *def);
/// @brief Set the collision mask of this body (property setter).
void rt_game3d_body_def_set_mask_prop(void *def, void *mask);
/// @brief Get the body/node transform sync mode (RT_GAME3D_SYNC_*).
int64_t rt_game3d_body_def_get_sync_mode(void *def);
/// @brief Set the body/node transform sync mode (property setter).
void rt_game3d_body_def_set_sync_mode_prop(void *def, int64_t sync_mode);
/// @brief Fluent setter: assign the collision layer and return the def.
void *rt_game3d_body_def_with_layer(void *def, int64_t layer);
/// @brief Fluent setter: assign the collision mask and return the def.
void *rt_game3d_body_def_with_mask(void *def, void *mask);
/// @brief Fluent setter: mark the def as a trigger and return it.
void *rt_game3d_body_def_as_trigger(void *def);
/// @brief Fluent setter: assign the sync mode and return the def.
void *rt_game3d_body_def_with_sync(void *def, int64_t sync_mode);

//=========================================================================
// Collision3DEvent — per-contact collision event record (Viper.Game3D.Collision3DEvent)
//=========================================================================

/// @brief Get the event phase (RT_GAME3D_COLLISION_*).
int64_t rt_game3d_collision_event_get_phase(void *event);
/// @brief Get the first entity involved in the collision.
void *rt_game3d_collision_event_get_a(void *event);
/// @brief Get the second entity involved in the collision.
void *rt_game3d_collision_event_get_b(void *event);
/// @brief Get the underlying low-level physics collision event.
void *rt_game3d_collision_event_get_raw(void *event);
/// @brief True if either body in the contact is a trigger volume.
int8_t rt_game3d_collision_event_get_is_trigger(void *event);
/// @brief Get the relative approach speed of the two bodies at contact.
double rt_game3d_collision_event_get_relative_speed(void *event);
/// @brief Get the normal impulse magnitude applied to resolve the contact.
double rt_game3d_collision_event_get_normal_impulse(void *event);
/// @brief Get the number of contact points carried by the wrapped raw event.
int64_t rt_game3d_collision_event_get_contact_count(void *event);
/// @brief Get the world-space contact point as a Vec3.
void *rt_game3d_collision_event_point(void *event);
/// @brief Get the world-space contact normal as a Vec3.
void *rt_game3d_collision_event_normal(void *event);
/// @brief Get indexed world-space contact point as a Vec3.
void *rt_game3d_collision_event_contact_point(void *event, int64_t index);
/// @brief Get indexed world-space contact normal as a Vec3.
void *rt_game3d_collision_event_contact_normal(void *event, int64_t index);
/// @brief Get indexed signed contact separation.
double rt_game3d_collision_event_contact_separation(void *event, int64_t index);
/// @brief Given one participating entity, return the other entity in the contact.
void *rt_game3d_collision_event_other(void *event, void *entity);

//=========================================================================
// Input3D — keyboard/mouse input state (Viper.Game3D.Input3D)
//=========================================================================

/// @brief Create a new input-state object bound to the active window.
void *rt_game3d_input_new(void);
/// @brief Get the mouse-look sensitivity multiplier.
double rt_game3d_input_get_look_sensitivity(void *input);
/// @brief Set the mouse-look sensitivity multiplier.
void rt_game3d_input_set_look_sensitivity(void *input, double sensitivity);
/// @brief Sample fresh input and roll edge (pressed/released) state forward one frame.
void rt_game3d_input_update(void *input);
/// @brief True while the given key is held down this frame.
int8_t rt_game3d_input_is_down(void *input, int64_t key);
/// @brief True only on the frame the given key transitions to down.
int8_t rt_game3d_input_pressed(void *input, int64_t key);
/// @brief True only on the frame the given key transitions to up.
int8_t rt_game3d_input_released(void *input, int64_t key);
/// @brief Get the per-frame mouse movement delta as a Vec2.
void *rt_game3d_input_mouse_delta(void *input);
/// @brief True while the given mouse button is held down this frame.
int8_t rt_game3d_input_mouse_button(void *input, int64_t button);
/// @brief True only on the frame the given mouse button transitions to down.
int8_t rt_game3d_input_mouse_pressed(void *input, int64_t button);
/// @brief Get the mouse wheel scroll delta along Y for this frame.
double rt_game3d_input_wheel_y(void *input);
/// @brief Get the WASD/arrow/space/shift movement axis as a normalized Vec3.
void *rt_game3d_input_move_axis(void *input);
/// @brief Get the mouse-look axis as a Vec2 (yaw/pitch delta scaled by sensitivity).
void *rt_game3d_input_look_axis(void *input);
/// @brief Capture and hide the OS cursor for relative mouse-look.
void rt_game3d_input_capture_mouse(void *input);
/// @brief Release the captured cursor back to the OS.
void rt_game3d_input_release_mouse(void *input);

//=========================================================================
// Entity3D — scene entity composing node, mesh, material, body, animator
//   (Viper.Game3D.Entity3D)
//=========================================================================

/// @brief Create a new empty entity (a bare scene node with no renderable).
void *rt_game3d_entity_new(void);
/// @brief Create an entity from an existing mesh and material.
void *rt_game3d_entity_of(void *mesh, void *material);
/// @brief Wrap an existing scene node hierarchy as an entity (e.g. a loaded model).
void *rt_game3d_entity_from_node(void *root);
/// @brief Get the entity's stable integer id.
int64_t rt_game3d_entity_get_id(void *entity);
/// @brief Get the entity's underlying scene node.
void *rt_game3d_entity_get_node(void *entity);
/// @brief Get the entity's mesh, or null if it has none.
void *rt_game3d_entity_get_mesh(void *entity);
/// @brief Set the entity's mesh (property setter).
void rt_game3d_entity_set_mesh_prop(void *entity, void *mesh);
/// @brief Get the entity's material, or null if it has none.
void *rt_game3d_entity_get_material(void *entity);
/// @brief Set the entity's material (property setter).
void rt_game3d_entity_set_material_prop(void *entity, void *material);
/// @brief Get the entity's attached physics body, or null if unattached.
void *rt_game3d_entity_get_body(void *entity);
/// @brief Get the entity's attached animator, or null if none.
void *rt_game3d_entity_get_anim(void *entity);
/// @brief Get the entity's collision layer.
int64_t rt_game3d_entity_get_layer(void *entity);
/// @brief Set the entity's collision layer (property setter).
void rt_game3d_entity_set_layer_prop(void *entity, int64_t layer);
/// @brief Get the entity's collision mask.
void *rt_game3d_entity_get_collision_mask(void *entity);
/// @brief Set the entity's collision mask (property setter).
void rt_game3d_entity_set_collision_mask_prop(void *entity, void *mask);
/// @brief Get the entity's display name.
rt_string rt_game3d_entity_get_name(void *entity);
/// @brief Set the entity's display name (property setter).
void rt_game3d_entity_set_name_prop(void *entity, rt_string name);
/// @brief Fluent: set local position from XYZ and return the entity.
void *rt_game3d_entity_set_position(void *entity, double x, double y, double z);
/// @brief Fluent: set local position from a Vec3 and return the entity.
void *rt_game3d_entity_set_position_v(void *entity, void *position);
/// @brief Fluent: set a uniform scale and return the entity.
void *rt_game3d_entity_set_scale(void *entity, double scale);
/// @brief Fluent: set a non-uniform XYZ scale and return the entity.
void *rt_game3d_entity_set_scale_xyz(void *entity, double x, double y, double z);
/// @brief Fluent: set rotation from Euler angles (degrees) and return the entity.
void *rt_game3d_entity_set_rotation_euler(void *entity, double x_deg, double y_deg, double z_deg);
/// @brief Fluent: assign the mesh and return the entity.
void *rt_game3d_entity_set_mesh(void *entity, void *mesh);
/// @brief Fluent: assign the material and return the entity.
void *rt_game3d_entity_set_material(void *entity, void *material);
/// @brief Fluent: parent the given child entity under this one and return this entity.
void *rt_game3d_entity_add_child(void *entity, void *child);
/// @brief True if the entity is a group (no own renderable, only children).
int8_t rt_game3d_entity_is_group(void *entity);
/// @brief Fluent: assign the display name and return the entity.
void *rt_game3d_entity_set_name(void *entity, rt_string name);
/// @brief Fluent: assign the collision layer and return the entity.
void *rt_game3d_entity_set_layer(void *entity, int64_t layer);
/// @brief Fluent: assign the collision mask and return the entity.
void *rt_game3d_entity_set_collision_mask(void *entity, void *mask);
/// @brief Fluent: attach a physics body (from a Body or BodyDef) and return the entity.
void *rt_game3d_entity_attach_body(void *entity, void *body_or_def);
/// @brief Fluent: attach an animator (from an Animator or controller) and return the entity.
void *rt_game3d_entity_attach_animator(void *entity, void *animator_or_controller);
/// @brief Apply an instantaneous linear impulse to the entity's body.
void rt_game3d_entity_apply_impulse(void *entity, double x, double y, double z);
/// @brief Set the entity body's linear velocity directly.
void rt_game3d_entity_set_velocity(void *entity, double x, double y, double z);
/// @brief Get the entity's local position as a Vec3.
void *rt_game3d_entity_position(void *entity);
/// @brief Get the entity's world-space position as a Vec3.
void *rt_game3d_entity_world_position(void *entity);
/// @brief True if the entity is currently spawned into a world.
int8_t rt_game3d_entity_is_spawned(void *entity);
/// @brief True if the entity has been despawned/destroyed.
int8_t rt_game3d_entity_is_destroyed(void *entity);

//=========================================================================
// Animator3D — animation state-machine driver (Viper.Game3D.Animator3D)
//=========================================================================

/// @brief Create an animator driven by the given animation controller.
void *rt_game3d_animator_new(void *controller);
/// @brief Get the animation controller backing this animator.
void *rt_game3d_animator_get_controller(void *animator);
/// @brief Play the named clip immediately; returns false if the clip is unknown.
int8_t rt_game3d_animator_play(void *animator, rt_string name);
/// @brief Cross-fade to the named clip over `seconds`; returns false if unknown.
int8_t rt_game3d_animator_crossfade(void *animator, rt_string name, double seconds);
/// @brief Play a named clip as a true additive overlay layer.
int8_t rt_game3d_animator_play_layer_additive(void *animator, int64_t layer, rt_string name);
/// @brief Cross-fade a named clip as a true additive overlay layer.
int8_t rt_game3d_animator_crossfade_layer_additive(void *animator,
                                                   int64_t layer,
                                                   rt_string name,
                                                   double seconds);
/// @brief Set a BlendTree3D as the wrapped controller's base pose source.
int8_t rt_game3d_animator_set_blend_tree(void *animator, void *blend_tree);
/// @brief Set an IKSolver3D as the wrapped controller's final-pose constraint.
int8_t rt_game3d_animator_set_ik_solver(void *animator, void *ik_solver);
/// @brief Set the playback speed multiplier for the named clip.
void rt_game3d_animator_set_speed(void *animator, rt_string name, double speed);
/// @brief True if the named clip is currently the active state.
int8_t rt_game3d_animator_is_playing(void *animator, rt_string name);
/// @brief Get the elapsed time of the current animation state in seconds.
double rt_game3d_animator_state_time(void *animator);
/// @brief Count animation events fired during the most recent update.
int64_t rt_game3d_animator_event_count(void *animator);
/// @brief Get the name of the i-th animation event from the most recent update.
rt_string rt_game3d_animator_event_name(void *animator, int64_t index);
/// @brief Advance the animator by `dt` seconds, sampling poses and firing events.
void rt_game3d_animator_update(void *animator, double dt);

//=========================================================================
// Sound3D — spatial audio subsystem (Viper.Game3D.Sound3D)
//=========================================================================

/// @brief Get the audio listener (ears) object.
void *rt_game3d_audio_get_listener(void *audio);
/// @brief True if the listener auto-tracks the active camera.
int8_t rt_game3d_audio_get_listener_follows_camera(void *audio);
/// @brief Get the attenuation reference distance (full-volume radius).
double rt_game3d_audio_get_ref_distance(void *audio);
/// @brief Get the attenuation maximum distance (silence radius).
double rt_game3d_audio_get_max_distance(void *audio);
/// @brief Get the master output volume (0–100).
int64_t rt_game3d_audio_get_volume(void *audio);
/// @brief Set the master output volume (0–100).
void rt_game3d_audio_set_volume(void *audio, int64_t volume);
/// @brief Count currently active 3D sound sources.
int64_t rt_game3d_audio_get_source_count(void *audio);
/// @brief Enable or disable the listener auto-following the camera.
void rt_game3d_audio_listener_follow_camera(void *audio, int8_t enabled);
/// @brief Set the listener pose explicitly from position/forward/up Vec3s.
void rt_game3d_audio_set_listener_pose(void *audio, void *position, void *forward, void *up);
/// @brief Set the distance-attenuation reference and maximum radii.
void rt_game3d_audio_set_attenuation(void *audio, double ref_distance, double max_distance);
/// @brief Load an audio clip from a filesystem path.
void *rt_game3d_audio_load(void *audio, rt_string path);
/// @brief Load an audio clip from a packed asset path.
void *rt_game3d_audio_load_asset(void *audio, rt_string asset_path);
/// @brief Play a clip as a one-shot at a fixed world position; returns the source.
void *rt_game3d_audio_play_at(void *audio, void *clip, void *position);
/// @brief Play a clip attached to an entity (follows it); returns the source.
void *rt_game3d_audio_play_attached(void *audio, void *clip, void *entity);
/// @brief Play a clip as non-spatial 2D audio; returns a source id.
int64_t rt_game3d_audio_play2d(void *audio, void *clip);
/// @brief Stop and remove all active sources.
void rt_game3d_audio_clear_sources(void *audio);

//=========================================================================
// Effects3D registry — per-world particle/decal manager (Viper.Game3D.EffectRegistry3D)
//=========================================================================

/// @brief Get the post-FX stack associated with this effect registry.
void *rt_game3d_effects_get_postfx(void *effects);
/// @brief Count total active effects (particles + decals).
int64_t rt_game3d_effects_get_count(void *effects);
/// @brief Count active particle systems.
int64_t rt_game3d_effects_get_particles_count(void *effects);
/// @brief Count active decals.
int64_t rt_game3d_effects_get_decal_count(void *effects);
/// @brief Register a particle system with an auto-expire lifetime; returns its handle.
void *rt_game3d_effects_add_particles(void *effects, void *particles, double lifetime);
/// @brief Register a decal; returns its handle.
void *rt_game3d_effects_add_decal(void *effects, void *decal);
/// @brief Advance all registered effects by `dt` seconds and retire expired ones.
void rt_game3d_effects_update(void *effects, double dt);
/// @brief Draw all registered effects through the given canvas and camera.
void rt_game3d_effects_draw(void *effects, void *canvas, void *camera);
/// @brief Remove all registered effects immediately.
void rt_game3d_effects_clear(void *effects);

//=========================================================================
// Effects3D presets — one-shot effect factories (Viper.Game3D.Effects3D)
//=========================================================================

/// @brief Spawn an explosion effect at the given world position.
void *rt_game3d_effects3d_explosion(void *world, void *position);
/// @brief Spawn a directional spark burst at the given position.
void *rt_game3d_effects3d_sparks(void *world, void *position, void *direction);
/// @brief Spawn a dust puff at the given position.
void *rt_game3d_effects3d_dust(void *world, void *position);
/// @brief Spawn a rising smoke plume at the given position.
void *rt_game3d_effects3d_smoke(void *world, void *position);
/// @brief Spawn an impact decal oriented to the given surface normal.
void *rt_game3d_effects3d_impact_decal(void *world, void *position, void *normal);

//=========================================================================
// Lighting — scene lighting presets (Viper.Game3D.Lighting)
//=========================================================================

/// @brief Apply a neutral three-point studio lighting rig to the world.
void rt_game3d_lighting_studio(void *world);
/// @brief Apply outdoor sun+sky lighting using the given sun direction.
void rt_game3d_lighting_outdoor(void *world, void *sun_dir);
/// @brief Apply a dim moonlit night lighting preset.
void rt_game3d_lighting_night(void *world);
/// @brief Apply a warm indoor lighting preset.
void rt_game3d_lighting_interior(void *world);
/// @brief Remove all preset lights from the world.
void rt_game3d_lighting_clear(void *world);

//=========================================================================
// Materials — material presets (Viper.Game3D.Materials)
//=========================================================================

/// @brief Create a matte plastic material of the given RGB color.
void *rt_game3d_materials_plastic(double r, double g, double b);
/// @brief Create a metallic material of the given RGB color.
void *rt_game3d_materials_metal(double r, double g, double b);
/// @brief Create a soft rubber material of the given RGB color.
void *rt_game3d_materials_rubber(double r, double g, double b);
/// @brief Create a translucent glass material of the given RGB color and alpha.
void *rt_game3d_materials_glass(double r, double g, double b, double alpha);
/// @brief Create an emissive (self-lit) material of the given RGB color and intensity.
void *rt_game3d_materials_emissive(double r, double g, double b, double intensity);
/// @brief Create an unlit flat-albedo material of the given RGB color.
void *rt_game3d_materials_unlit(double r, double g, double b);
/// @brief Create a material whose albedo is sampled from a Pixels texture.
void *rt_game3d_materials_from_albedo_map(void *pixels);

//=========================================================================
// PostFX — post-processing presets (Viper.Game3D.PostFX)
//=========================================================================

/// @brief Apply a cinematic post-FX chain with bloom, tone-map, and subtle vignette.
void rt_game3d_postfx_cinematic(void *world);
/// @brief Apply a crisp, minimal-grading post-FX chain to the world.
void rt_game3d_postfx_crisp(void *world);
/// @brief Disable all post-processing for the world.
void rt_game3d_postfx_none(void *world);

//=========================================================================
// Quality — quality preset application (Viper.Game3D.Quality)
//=========================================================================

/// @brief Apply a render quality preset (RT_GAME3D_QUALITY_*) to the world.
void rt_game3d_quality_apply(void *world, int64_t quality);

//=========================================================================
// Prefab — primitive mesh-entity factories (Viper.Game3D.Prefab)
//=========================================================================

/// @brief Create a cube entity of the given uniform size with a material.
void *rt_game3d_prefab_box(double size, void *material);
/// @brief Create a box entity with explicit width/height/depth and a material.
void *rt_game3d_prefab_box_xyz(double width, double height, double depth, void *material);
/// @brief Create a UV sphere entity with the given radius and segment count.
void *rt_game3d_prefab_sphere(double radius, int64_t segments, void *material);
/// @brief Create a cylinder entity with the given radius, height, and segments.
void *rt_game3d_prefab_cylinder(double radius, double height, int64_t segments, void *material);
/// @brief Create a flat plane entity of the given width and depth.
void *rt_game3d_prefab_plane(double width, double depth, void *material);
/// @brief Create a large ground-plane entity of the given size.
void *rt_game3d_prefab_ground(double size, void *material);

//=========================================================================
// Assets3D — model loading and caching (Viper.Game3D.Assets3D)
//=========================================================================

/// @brief Load a model from a filesystem path as a ready-to-spawn entity.
void *rt_game3d_assets_load_model(rt_string path);
/// @brief Load a model from a packed asset path as a ready-to-spawn entity.
void *rt_game3d_assets_load_model_asset(rt_string path);
/// @brief Load a model from a filesystem path as a reusable instancing template.
void *rt_game3d_assets_load_model_template(rt_string path);
/// @brief Load a model from a packed asset path as a reusable instancing template.
void *rt_game3d_assets_load_model_template_asset(rt_string path);
/// @brief Load a filesystem model through the AssetHandle3D contract.
void *rt_game3d_assets_load_model_async(rt_string path);
/// @brief Load a packed-asset model through the AssetHandle3D contract.
void *rt_game3d_assets_load_model_asset_async(rt_string path);
/// @brief Load a filesystem model template through the AssetHandle3D contract.
void *rt_game3d_assets_load_model_template_async(rt_string path);
/// @brief Load a packed-asset model template through the AssetHandle3D contract.
void *rt_game3d_assets_load_model_template_asset_async(rt_string path);
/// @brief Set the process-wide cached-template residency budget; negative means unlimited.
void rt_game3d_assets_set_residency_budget(int64_t bytes);
/// @brief Return estimated bytes currently resident in the shared cached-template store.
int64_t rt_game3d_assets_get_resident_bytes(void);
/// @brief Hint cache eviction: higher priority and lower distance survive pressure first.
void rt_game3d_assets_set_residency_hint(void *model_template, double priority, double distance);
/// @brief Set the per-drain async asset upload budget in decoded bytes; negative means unlimited.
void rt_game3d_assets_set_upload_budget(int64_t bytes);
/// @brief Evict the cached template backing a ready template AssetHandle3D.
void rt_game3d_assets_evict(void *asset_handle);
/// @brief Warm the filesystem template cache through the background async load path.
void rt_game3d_assets_preload(rt_string path);
/// @brief Warm the packed-asset template cache through the background async load path.
void rt_game3d_assets_preload_asset(rt_string path);
/// @brief Drop all cached loaded models, freeing their memory.
void rt_game3d_assets_clear_cache(void);

//=========================================================================
// AssetHandle3D — asset-loading status/result handle (Viper.Game3D.AssetHandle3D)
//=========================================================================

/// @brief True once the asset request has reached a terminal state.
int8_t rt_game3d_asset_handle_get_ready(void *asset_handle);
/// @brief Loading progress in the inclusive range [0, 1].
double rt_game3d_asset_handle_get_progress(void *asset_handle);
/// @brief Terminal error text, or an empty string on success / pending work.
rt_string rt_game3d_asset_handle_get_error(void *asset_handle);
/// @brief Cancel a pending request; completed requests are left unchanged.
void rt_game3d_asset_handle_cancel(void *asset_handle);
/// @brief Return the loaded entity for entity-mode requests, or NULL.
void *rt_game3d_asset_handle_get_entity(void *asset_handle);
/// @brief Return the loaded model template for template-mode requests, or NULL.
void *rt_game3d_asset_handle_get_template(void *asset_handle);

//=========================================================================
// ModelTemplate — cached model for instancing (Viper.Game3D.ModelTemplate)
//=========================================================================

/// @brief Get the underlying loaded model backing the template.
void *rt_game3d_model_template_get_model(void *model_template);
/// @brief Get the source path the template was loaded from.
rt_string rt_game3d_model_template_get_path(void *model_template);
/// @brief True if the template was loaded from a packed asset (not the filesystem).
int8_t rt_game3d_model_template_get_is_asset(void *model_template);
/// @brief Instantiate a fresh entity from the template.
void *rt_game3d_model_template_instantiate(void *model_template);

//=========================================================================
// Environment / EnvHandle — environment presets and builder (Viper.Game3D.Environment / EnvHandle)
//=========================================================================

/// @brief Apply a bright outdoor environment (sky, sun, fog) and return its handle.
void *rt_game3d_environment_outdoor(void *world);
/// @brief Apply a warm sunset environment and return its handle.
void *rt_game3d_environment_sunset(void *world);
/// @brief Apply a flat overcast environment and return its handle.
void *rt_game3d_environment_overcast(void *world);
/// @brief Apply a dark night environment and return its handle.
void *rt_game3d_environment_night(void *world);
/// @brief Fluent: add terrain of the given size/height to the environment and return the handle.
void *rt_game3d_env_handle_with_terrain(void *env, double size, double height);
/// @brief Fluent: add a water plane at the given level and return the handle.
void *rt_game3d_env_handle_with_water(void *env, double level);
/// @brief Fluent: add distance fog between near/far planes and return the handle.
void *rt_game3d_env_handle_with_fog(void *env, double near_plane, double far_plane);

//=========================================================================
// Debug3D — debug visualization toggles (Viper.Game3D.Debug3D)
//=========================================================================

/// @brief Show or hide the on-screen debug stats overlay.
void rt_game3d_debug_show_overlay(void *world, int8_t enabled);
/// @brief Draw a world-space coordinate axis gizmo of the given size at origin.
void rt_game3d_debug_draw_axes(void *world, void *origin, double size);
/// @brief Enable or disable physics collider/contact debug drawing.
void rt_game3d_debug_draw_physics(void *world, int8_t enabled);
/// @brief Enable or disable on-screen camera info readout.
void rt_game3d_debug_draw_camera_info(void *world, int8_t enabled);
/// @brief Enable or disable the backend capabilities readout overlay.
void rt_game3d_debug_draw_capabilities(void *world, int8_t enabled);

//=========================================================================
// CharacterController3D — kinematic character mover (Viper.Game3D.CharacterController3D)
//=========================================================================

/// @brief Create a capsule character controller bound to an entity in the world.
void *rt_game3d_character_controller_new(
    void *world, void *entity, double radius, double height, double mass);
/// @brief Get the underlying low-level character object.
void *rt_game3d_character_controller_get_character(void *controller);
/// @brief Get the entity driven by this controller.
void *rt_game3d_character_controller_get_entity(void *controller);
/// @brief Get the horizontal move speed in units/second.
double rt_game3d_character_controller_get_speed(void *controller);
/// @brief Set the horizontal move speed in units/second.
void rt_game3d_character_controller_set_speed(void *controller, double speed);
/// @brief Get the initial jump speed in units/second.
double rt_game3d_character_controller_get_jump_speed(void *controller);
/// @brief Set the initial jump speed in units/second.
void rt_game3d_character_controller_set_jump_speed(void *controller, double jump_speed);
/// @brief Get the downward gravity acceleration in units/second².
double rt_game3d_character_controller_get_gravity(void *controller);
/// @brief Set the downward gravity acceleration in units/second².
void rt_game3d_character_controller_set_gravity(void *controller, double gravity);
/// @brief Advance the controller using input and camera orientation over `dt` seconds.
void rt_game3d_character_controller_update(void *controller, void *input, void *camera, double dt);
/// @brief Teleport the character to an absolute world position, clearing velocity.
void rt_game3d_character_controller_teleport(void *controller, double x, double y, double z);
/// @brief True if the character is currently standing on ground.
int8_t rt_game3d_character_controller_grounded(void *controller);

//=========================================================================
// FirstPersonController — FPS camera/movement rig (Viper.Game3D.FirstPersonController)
//=========================================================================

/// @brief Create a first-person controller bound to the world's camera.
void *rt_game3d_first_person_controller_new(void *world);
/// @brief Get the character controller driving movement.
void *rt_game3d_first_person_controller_get_character(void *controller);
/// @brief Set the character controller driving movement.
void rt_game3d_first_person_controller_set_character(void *controller, void *character_controller);
/// @brief Get the move speed in units/second.
double rt_game3d_first_person_controller_get_speed(void *controller);
/// @brief Set the move speed in units/second.
void rt_game3d_first_person_controller_set_speed(void *controller, double speed);
/// @brief Get the mouse-look sensitivity multiplier.
double rt_game3d_first_person_controller_get_look_sensitivity(void *controller);
/// @brief Set the mouse-look sensitivity multiplier.
void rt_game3d_first_person_controller_set_look_sensitivity(void *controller, double sensitivity);
/// @brief Capture and hide the cursor for relative mouse-look.
void rt_game3d_first_person_controller_capture_mouse(void *controller);
/// @brief Release the captured cursor.
void rt_game3d_first_person_controller_release_mouse(void *controller);
/// @brief Update movement and look from input over `dt` seconds.
void rt_game3d_first_person_controller_update(void *controller, void *world, double dt);
/// @brief Late-update the camera pose after physics over `dt` seconds.
void rt_game3d_first_person_controller_late_update(void *controller, void *world, double dt);

//=========================================================================
// FreeFlyController — unconstrained spectator camera (Viper.Game3D.FreeFlyController)
//=========================================================================

/// @brief Create a free-fly (noclip spectator) controller for the world's camera.
void *rt_game3d_free_fly_controller_new(void *world);
/// @brief Get the fly speed in units/second.
double rt_game3d_free_fly_controller_get_speed(void *controller);
/// @brief Set the fly speed in units/second.
void rt_game3d_free_fly_controller_set_speed(void *controller, double speed);
/// @brief Get the mouse-look sensitivity multiplier.
double rt_game3d_free_fly_controller_get_look_sensitivity(void *controller);
/// @brief Set the mouse-look sensitivity multiplier.
void rt_game3d_free_fly_controller_set_look_sensitivity(void *controller, double sensitivity);
/// @brief Capture and hide the cursor for relative mouse-look.
void rt_game3d_free_fly_controller_capture_mouse(void *controller);
/// @brief Release the captured cursor.
void rt_game3d_free_fly_controller_release_mouse(void *controller);
/// @brief Update fly movement and look from input over `dt` seconds.
void rt_game3d_free_fly_controller_update(void *controller, void *world, double dt);
/// @brief Late-update the camera pose after physics over `dt` seconds.
void rt_game3d_free_fly_controller_late_update(void *controller, void *world, double dt);

//=========================================================================
// OrbitController — target-orbiting camera (Viper.Game3D.OrbitController)
//=========================================================================

/// @brief Create an orbit controller circling the given target for the world's camera.
void *rt_game3d_orbit_controller_new(void *world, void *target);
/// @brief Get the orbit target (entity or point) being circled.
void *rt_game3d_orbit_controller_get_target(void *controller);
/// @brief Set the orbit target being circled.
void rt_game3d_orbit_controller_set_target(void *controller, void *target);
/// @brief Get the orbit radius (distance from target) in world units.
double rt_game3d_orbit_controller_get_distance(void *controller);
/// @brief Set the orbit radius (distance from target) in world units.
void rt_game3d_orbit_controller_set_distance(void *controller, double distance);
/// @brief Get the horizontal orbit angle (yaw) in degrees.
double rt_game3d_orbit_controller_get_yaw(void *controller);
/// @brief Set the horizontal orbit angle (yaw) in degrees.
void rt_game3d_orbit_controller_set_yaw(void *controller, double yaw);
/// @brief Get the vertical orbit angle (pitch) in degrees.
double rt_game3d_orbit_controller_get_pitch(void *controller);
/// @brief Set the vertical orbit angle (pitch) in degrees.
void rt_game3d_orbit_controller_set_pitch(void *controller, double pitch);
/// @brief Update orbit yaw/pitch/zoom from input over `dt` seconds.
void rt_game3d_orbit_controller_update(void *controller, void *world, double dt);
/// @brief Late-update the camera pose after physics over `dt` seconds.
void rt_game3d_orbit_controller_late_update(void *controller, void *world, double dt);

//=========================================================================
// FollowController — smoothed third-person chase camera (Viper.Game3D.FollowController)
//=========================================================================

/// @brief Create a follow controller chasing the target entity at the given offset.
void *rt_game3d_follow_controller_new(void *world, void *target_entity, void *offset);
/// @brief Get the entity being followed.
void *rt_game3d_follow_controller_get_target(void *controller);
/// @brief Set the entity being followed.
void rt_game3d_follow_controller_set_target(void *controller, void *target_entity);
/// @brief Get the follow offset (camera position relative to target) as a Vec3.
void *rt_game3d_follow_controller_get_offset(void *controller);
/// @brief Set the follow offset (camera position relative to target).
void rt_game3d_follow_controller_set_offset(void *controller, void *offset);
/// @brief Get the position-smoothing damping factor.
double rt_game3d_follow_controller_get_damping(void *controller);
/// @brief Set the position-smoothing damping factor.
void rt_game3d_follow_controller_set_damping(void *controller, double damping);
/// @brief Update the smoothed follow position over `dt` seconds.
void rt_game3d_follow_controller_update(void *controller, void *world, double dt);
/// @brief Late-update the camera pose after physics over `dt` seconds.
void rt_game3d_follow_controller_late_update(void *controller, void *world, double dt);

//=========================================================================
// World3D — top-level game world bundling all subsystems (Viper.Game3D.World3D)
//=========================================================================

/// @brief Create a game world (window + default camera/scene/physics/etc.).
void *rt_game3d_world_new(rt_string title, int64_t width, int64_t height);
/// @brief Create a game world with explicit camera FOV and near/far clip planes.
void *rt_game3d_world_new_with_camera(rt_string title,
                                      int64_t width,
                                      int64_t height,
                                      double fov_deg,
                                      double near_plane,
                                      double far_plane);
/// @brief Destroy the world and all owned subsystems, closing its window.
void rt_game3d_world_destroy(void *world);
/// @brief True if the world has been destroyed.
int8_t rt_game3d_world_is_destroyed(void *world);
/// @brief Get the world's rendering canvas.
void *rt_game3d_world_get_canvas(void *world);
/// @brief Get the world's active camera.
void *rt_game3d_world_get_camera(void *world);
/// @brief Get the world's scene graph.
void *rt_game3d_world_get_scene(void *world);
/// @brief Get the world's physics simulation.
void *rt_game3d_world_get_physics(void *world);
/// @brief Get the world's input-state object.
void *rt_game3d_world_get_input(void *world);
/// @brief Get the world's audio subsystem.
void *rt_game3d_world_get_audio(void *world);
/// @brief Get the world's effects registry.
void *rt_game3d_world_get_effects(void *world);
/// @brief Get the world's owned streaming controller, creating it on first access.
void *rt_game3d_world_get_stream(void *world);
/// @brief Get the most recent frame's delta time in seconds.
double rt_game3d_world_get_dt(void *world);
/// @brief Get total elapsed wall-clock time since the world started, in seconds.
double rt_game3d_world_get_elapsed(void *world);
/// @brief Get the current frame counter.
int64_t rt_game3d_world_get_frame(void *world);
/// @brief Count fixed-timestep updates discarded by the spiral-of-death guard.
int64_t rt_game3d_world_get_dropped_fixed_steps(void *world);
/// @brief Count spawned Entity3D objects currently owned by the world.
int64_t rt_game3d_world_get_entity_count(void *world);
/// @brief Count physics bodies currently registered through spawned entities.
int64_t rt_game3d_world_get_body_count(void *world);
/// @brief Count main 3D draw submissions queued by the latest ended frame.
int64_t rt_game3d_world_get_draw_count(void *world);
/// @brief Count drawable scene nodes submitted by the latest scene draw.
int64_t rt_game3d_world_get_visible_node_count(void *world);
/// @brief Count draw submissions skipped by latest visibility culling.
int64_t rt_game3d_world_get_occluded_draw_count(void *world);
/// @brief Count bytes resident in the world-owned stream controller, if any.
int64_t rt_game3d_world_get_stream_resident_bytes(void *world);
/// @brief Get the configured worker count for internal deterministic jobs.
int64_t rt_game3d_world_get_worker_count(void *world);
/// @brief True when internal jobs are allowed to use worker threads.
int8_t rt_game3d_world_get_jobs_enabled(void *world);
/// @brief Set the internal worker count; values <= 1 keep jobs disabled.
void rt_game3d_world_set_worker_count(void *world, int64_t worker_count);
/// @brief True when camera-relative floating-origin rebasing is enabled.
int8_t rt_game3d_world_get_floating_origin(void *world);
/// @brief Enable or disable camera-relative floating-origin rebasing.
void rt_game3d_world_set_floating_origin(void *world, int8_t enabled);
/// @brief Get the accumulated world-origin offset as a Vec3.
void *rt_game3d_world_get_world_origin(void *world);
/// @brief Set the camera-distance threshold that triggers a floating-origin rebase.
void rt_game3d_world_set_origin_rebase_threshold(void *world, double meters);
/// @brief Apply a manual floating-origin rebase. Must be called between frames.
void rt_game3d_world_rebase_origin(void *world, double dx, double dy, double dz);
/// @brief Spawn an entity into the world (adds it to scene + physics); returns the entity.
void *rt_game3d_world_spawn(void *world, void *entity);
/// @brief Despawn an entity, removing it from scene and physics.
void rt_game3d_world_despawn(void *world, void *entity);
/// @brief Find a scene node by name, or null if absent.
void *rt_game3d_world_find_node(void *world, rt_string name);
/// @brief Find a spawned entity by name, or null if absent.
void *rt_game3d_world_find_entity(void *world, rt_string name);
/// @brief Install a camera controller to drive the world's camera each frame.
void rt_game3d_world_set_camera_controller(void *world, void *controller);
/// @brief Point the camera to look at the given target entity or point.
void rt_game3d_world_look_at(void *world, void *target);
/// @brief Notify the world its window was resized to the given dimensions.
void rt_game3d_world_on_resize(void *world, int64_t width, int64_t height);
/// @brief Set the global ambient light color.
void rt_game3d_world_set_ambient(void *world, double r, double g, double b);
/// @brief Bind a light into the given light slot.
void rt_game3d_world_add_light(void *world, int64_t slot, void *light);
/// @brief Clear all bound light slots.
void rt_game3d_world_clear_lights(void *world);
/// @brief Set the world's skybox from a cubemap.
void rt_game3d_world_set_skybox(void *world, void *cubemap);
/// @brief Configure distance fog: RGB color plus near/far planes.
void rt_game3d_world_set_fog(
    void *world, double r, double g, double b, double near_plane, double far_plane);
/// @brief Apply a render quality preset (RT_GAME3D_QUALITY_*) to the world.
void rt_game3d_world_set_quality(void *world, int64_t quality);
/// @brief Bake a NavMesh3D from the world's current Scene3D.
void *rt_game3d_world_bake_nav_mesh(
    void *world, double agent_radius, double agent_height, double max_slope, double cell_size);
/// @brief Bake a tiled NavMesh3D from the world's current Scene3D.
void *rt_game3d_world_bake_tiled_nav_mesh(void *world,
                                          double tile_size,
                                          double agent_radius,
                                          double agent_height,
                                          double max_slope,
                                          double cell_size);
/// @brief Count collision events recorded this frame for the given phase.
int64_t rt_game3d_world_collision_event_count(void *world, int64_t phase);
/// @brief Get the i-th collision event for the given phase.
void *rt_game3d_world_collision_event(void *world, int64_t phase, int64_t index);
/// @brief Clear the recorded collision-event buffers.
void rt_game3d_world_clear_collision_events(void *world);
/// @brief Set the physics gravity vector.
void rt_game3d_world_set_gravity(void *world, double x, double y, double z);
/// @brief Run the blocking game loop, calling `update` each frame until the window closes.
void rt_game3d_world_run(void *world, void *update);
/// @brief Run the game loop with both a per-frame `update` and a 2D `overlay` callback.
void rt_game3d_world_run_with_overlay(void *world, void *update, void *overlay);
/// @brief Run a fixed-timestep game loop with the given step in seconds.
void rt_game3d_world_run_fixed(void *world, double step_sec, void *update);
/// @brief Run a fixed-timestep loop with an additional 2D overlay callback.
void rt_game3d_world_run_fixed_with_overlay(void *world,
                                            double step_sec,
                                            void *update,
                                            void *overlay);
/// @brief Run a deterministic fixed number of frames at a fixed step (for tests/recording).
void rt_game3d_world_run_frames(void *world, int64_t frame_count, double step_sec, void *update);
/// @brief Run a fixed number of frames with no update callback (pure simulation/render).
void rt_game3d_world_run_frames_only(void *world, int64_t frame_count, double step_sec);
/// @brief Advance the world one frame manually; returns false when the window should close.
int8_t rt_game3d_world_tick(void *world);
/// @brief Step physics simulation by the given fixed step in seconds.
void rt_game3d_world_step_simulation(void *world, double step_sec);
/// @brief Begin a frame: poll input, update timing, and prepare the canvas.
void rt_game3d_world_begin_frame(void *world);
/// @brief Draw the scene graph into the current frame.
void rt_game3d_world_draw_scene(void *world);
/// @brief Draw registered effects into the current frame.
void rt_game3d_world_draw_effects(void *world);
/// @brief End the 3D scene pass for the current frame.
void rt_game3d_world_end_scene(void *world);
/// @brief Draw a 2D overlay callback over the current frame.
void rt_game3d_world_draw_overlay(void *world, void *overlay);
/// @brief Finalize and capture the rendered frame as a Pixels image; returns it.
void *rt_game3d_world_capture_final_frame(void *world);
/// @brief Present the finished frame to the window (flip buffers).
void rt_game3d_world_present(void *world);

//=========================================================================
// WorldStream3D — deterministic streaming state (Viper.Game3D.WorldStream3D)
//=========================================================================

/// @brief Create a stream controller bound to a live World3D.
void *rt_game3d_world_stream_new(void *world);
/// @brief Get resident scene-cell count.
int64_t rt_game3d_world_stream_get_resident_cell_count(void *stream);
/// @brief Get resident terrain-tile count.
int64_t rt_game3d_world_stream_get_resident_terrain_tile_count(void *stream);
/// @brief Return the nth currently resident Terrain3D tile, or NULL.
void *rt_game3d_world_stream_get_resident_terrain_tile(void *stream, int64_t index);
/// @brief Get parsed scene-cell entry count.
int64_t rt_game3d_world_stream_get_cell_count(void *stream);
/// @brief Get a parsed scene-cell name, or "" for an invalid index.
rt_string rt_game3d_world_stream_get_cell_name(void *stream, int64_t index);
/// @brief Get a parsed scene-cell center, or NULL for an invalid index.
void *rt_game3d_world_stream_get_cell_center(void *stream, int64_t index);
/// @brief Return whether a parsed scene-cell entry is currently resident.
int8_t rt_game3d_world_stream_get_cell_resident(void *stream, int64_t index);
/// @brief Get the byte estimate for a parsed scene-cell entry.
int64_t rt_game3d_world_stream_get_cell_bytes(void *stream, int64_t index);
/// @brief Get parsed scene-cell material metadata, or "" for invalid/missing.
rt_string rt_game3d_world_stream_get_cell_material(void *stream, int64_t index);
/// @brief Get parsed scene-cell optional binary sidecar path, or "" for invalid/missing.
rt_string rt_game3d_world_stream_get_cell_sidecar(void *stream, int64_t index);
/// @brief Get parsed scene-cell collision/render layer metadata, or 0 if unset/invalid.
int64_t rt_game3d_world_stream_get_cell_layer(void *stream, int64_t index);
/// @brief Get parsed scene-cell collision mask metadata, or all bits if unset.
int64_t rt_game3d_world_stream_get_cell_collision_mask(void *stream, int64_t index);
/// @brief Return whether parsed scene-cell collision is enabled.
int8_t rt_game3d_world_stream_get_cell_collision_enabled(void *stream, int64_t index);
/// @brief Get parsed scene-cell navigation area metadata, or "" for invalid/missing.
rt_string rt_game3d_world_stream_get_cell_nav_area(void *stream, int64_t index);
/// @brief Get parsed scene-cell traversal cost metadata.
double rt_game3d_world_stream_get_cell_traversal_cost(void *stream, int64_t index);
/// @brief Get parsed terrain-tile entry count.
int64_t rt_game3d_world_stream_get_terrain_tile_count(void *stream);
/// @brief Get a parsed terrain-tile name, or "" for an invalid index.
rt_string rt_game3d_world_stream_get_terrain_tile_name(void *stream, int64_t index);
/// @brief Get a parsed terrain-tile heightmap sidecar path, or "" for an invalid index/missing
/// path.
rt_string rt_game3d_world_stream_get_terrain_tile_heightmap(void *stream, int64_t index);
/// @brief Get a parsed terrain-tile center, or NULL for an invalid index.
void *rt_game3d_world_stream_get_terrain_tile_center(void *stream, int64_t index);
/// @brief Return whether a parsed terrain-tile entry is currently resident.
int8_t rt_game3d_world_stream_get_terrain_tile_resident(void *stream, int64_t index);
/// @brief Get the byte estimate for a parsed terrain-tile entry.
int64_t rt_game3d_world_stream_get_terrain_tile_bytes(void *stream, int64_t index);
/// @brief Get parsed terrain-tile material metadata, or "" for invalid/missing.
rt_string rt_game3d_world_stream_get_terrain_tile_material(void *stream, int64_t index);
/// @brief Get parsed terrain-tile optional binary sidecar path, or "" for invalid/missing.
rt_string rt_game3d_world_stream_get_terrain_tile_sidecar(void *stream, int64_t index);
/// @brief Get parsed terrain-tile collision/render layer metadata.
int64_t rt_game3d_world_stream_get_terrain_tile_layer(void *stream, int64_t index);
/// @brief Get parsed terrain-tile collision mask metadata.
int64_t rt_game3d_world_stream_get_terrain_tile_collision_mask(void *stream, int64_t index);
/// @brief Return whether parsed terrain-tile collision is enabled.
int8_t rt_game3d_world_stream_get_terrain_tile_collision_enabled(void *stream, int64_t index);
/// @brief Get parsed terrain-tile navigation area metadata, or "" for invalid/missing.
rt_string rt_game3d_world_stream_get_terrain_tile_nav_area(void *stream, int64_t index);
/// @brief Get parsed terrain-tile traversal cost metadata.
double rt_game3d_world_stream_get_terrain_tile_traversal_cost(void *stream, int64_t index);
/// @brief Get pending stream request count.
int64_t rt_game3d_world_stream_get_pending_request_count(void *stream);
/// @brief Get estimated resident stream bytes.
int64_t rt_game3d_world_stream_get_resident_bytes(void *stream);
/// @brief Set the streaming focus point.
void rt_game3d_world_stream_set_center(void *stream, void *position);
/// @brief Set load/unload radii in world units.
void rt_game3d_world_stream_set_radii(void *stream, double load_radius, double unload_radius);
/// @brief Bound resident stream bytes; negative means unlimited.
void rt_game3d_world_stream_set_residency_budget(void *stream, int64_t bytes);
/// @brief Mount a tiled terrain streaming manifest.
void rt_game3d_world_stream_mount_tiled_terrain(void *stream, rt_string manifest_path);
/// @brief Mount a scene-cell streaming manifest.
void rt_game3d_world_stream_mount_cells(void *stream, rt_string manifest_path);
/// @brief Advance stream scheduling/telemetry.
void rt_game3d_world_stream_update(void *stream, double dt);

#ifdef __cplusplus
}
#endif
