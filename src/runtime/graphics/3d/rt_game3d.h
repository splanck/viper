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
/// @brief Key code for the F11 function key.
int64_t rt_game3d_key_f11(void);

// --- Full keyboard coverage: remaining A-Z / 0-9 / F1-F12 / navigation /
//     modifier / punctuation / numpad keys, completing Viper.Game3D.Keys so any
//     physical key is reachable as Keys.get_<Name>(). Backing: rt_input table. ---
/// @brief Key code for the B key.
int64_t rt_game3d_key_b(void);
/// @brief Key code for the C key.
int64_t rt_game3d_key_c(void);
/// @brief Key code for the E key.
int64_t rt_game3d_key_e(void);
/// @brief Key code for the F key.
int64_t rt_game3d_key_f(void);
/// @brief Key code for the G key.
int64_t rt_game3d_key_g(void);
/// @brief Key code for the H key.
int64_t rt_game3d_key_h(void);
/// @brief Key code for the I key.
int64_t rt_game3d_key_i(void);
/// @brief Key code for the J key.
int64_t rt_game3d_key_j(void);
/// @brief Key code for the K key.
int64_t rt_game3d_key_k(void);
/// @brief Key code for the L key.
int64_t rt_game3d_key_l(void);
/// @brief Key code for the M key.
int64_t rt_game3d_key_m(void);
/// @brief Key code for the N key.
int64_t rt_game3d_key_n(void);
/// @brief Key code for the O key.
int64_t rt_game3d_key_o(void);
/// @brief Key code for the P key.
int64_t rt_game3d_key_p(void);
/// @brief Key code for the Q key.
int64_t rt_game3d_key_q(void);
/// @brief Key code for the R key.
int64_t rt_game3d_key_r(void);
/// @brief Key code for the T key.
int64_t rt_game3d_key_t(void);
/// @brief Key code for the U key.
int64_t rt_game3d_key_u(void);
/// @brief Key code for the V key.
int64_t rt_game3d_key_v(void);
/// @brief Key code for the X key.
int64_t rt_game3d_key_x(void);
/// @brief Key code for the Y key.
int64_t rt_game3d_key_y(void);
/// @brief Key code for the Z key.
int64_t rt_game3d_key_z(void);
/// @brief Key code for the 0 key (top row).
int64_t rt_game3d_key_0(void);
/// @brief Key code for the 1 key (top row).
int64_t rt_game3d_key_1(void);
/// @brief Key code for the 2 key (top row).
int64_t rt_game3d_key_2(void);
/// @brief Key code for the 3 key (top row).
int64_t rt_game3d_key_3(void);
/// @brief Key code for the 4 key (top row).
int64_t rt_game3d_key_4(void);
/// @brief Key code for the 5 key (top row).
int64_t rt_game3d_key_5(void);
/// @brief Key code for the 6 key (top row).
int64_t rt_game3d_key_6(void);
/// @brief Key code for the 7 key (top row).
int64_t rt_game3d_key_7(void);
/// @brief Key code for the 8 key (top row).
int64_t rt_game3d_key_8(void);
/// @brief Key code for the 9 key (top row).
int64_t rt_game3d_key_9(void);
/// @brief Key code for the F1 function key.
int64_t rt_game3d_key_f1(void);
/// @brief Key code for the F2 function key.
int64_t rt_game3d_key_f2(void);
/// @brief Key code for the F3 function key.
int64_t rt_game3d_key_f3(void);
/// @brief Key code for the F4 function key.
int64_t rt_game3d_key_f4(void);
/// @brief Key code for the F5 function key.
int64_t rt_game3d_key_f5(void);
/// @brief Key code for the F6 function key.
int64_t rt_game3d_key_f6(void);
/// @brief Key code for the F7 function key.
int64_t rt_game3d_key_f7(void);
/// @brief Key code for the F8 function key.
int64_t rt_game3d_key_f8(void);
/// @brief Key code for the F9 function key.
int64_t rt_game3d_key_f9(void);
/// @brief Key code for the F10 function key.
int64_t rt_game3d_key_f10(void);
/// @brief Key code for the F12 function key.
int64_t rt_game3d_key_f12(void);
/// @brief Key code for the Enter/Return key.
int64_t rt_game3d_key_enter(void);
/// @brief Key code for the Tab key.
int64_t rt_game3d_key_tab(void);
/// @brief Key code for the Backspace key.
int64_t rt_game3d_key_backspace(void);
/// @brief Key code for the Insert key.
int64_t rt_game3d_key_insert(void);
/// @brief Key code for the Delete key.
int64_t rt_game3d_key_delete(void);
/// @brief Key code for the Home key.
int64_t rt_game3d_key_home(void);
/// @brief Key code for the End key.
int64_t rt_game3d_key_end(void);
/// @brief Key code for the Page Up key.
int64_t rt_game3d_key_pageup(void);
/// @brief Key code for the Page Down key.
int64_t rt_game3d_key_pagedown(void);
/// @brief Key code for the Alt modifier (left).
int64_t rt_game3d_key_alt(void);
/// @brief Key code for the left Shift modifier.
int64_t rt_game3d_key_lshift(void);
/// @brief Key code for the right Shift modifier.
int64_t rt_game3d_key_rshift(void);
/// @brief Key code for the left Ctrl modifier.
int64_t rt_game3d_key_lctrl(void);
/// @brief Key code for the right Ctrl modifier.
int64_t rt_game3d_key_rctrl(void);
/// @brief Key code for the left Alt modifier.
int64_t rt_game3d_key_lalt(void);
/// @brief Key code for the right Alt modifier.
int64_t rt_game3d_key_ralt(void);
/// @brief Key code for the apostrophe/quote key.
int64_t rt_game3d_key_quote(void);
/// @brief Key code for the comma key.
int64_t rt_game3d_key_comma(void);
/// @brief Key code for the minus/hyphen key.
int64_t rt_game3d_key_minus(void);
/// @brief Key code for the period key.
int64_t rt_game3d_key_period(void);
/// @brief Key code for the forward-slash key.
int64_t rt_game3d_key_slash(void);
/// @brief Key code for the semicolon key.
int64_t rt_game3d_key_semicolon(void);
/// @brief Key code for the equals key.
int64_t rt_game3d_key_equals(void);
/// @brief Key code for the left-bracket key.
int64_t rt_game3d_key_lbracket(void);
/// @brief Key code for the backslash key.
int64_t rt_game3d_key_backslash(void);
/// @brief Key code for the right-bracket key.
int64_t rt_game3d_key_rbracket(void);
/// @brief Key code for the grave/backtick key.
int64_t rt_game3d_key_grave(void);
/// @brief Key code for the numpad 0.
int64_t rt_game3d_key_num0(void);
/// @brief Key code for the numpad 1.
int64_t rt_game3d_key_num1(void);
/// @brief Key code for the numpad 2.
int64_t rt_game3d_key_num2(void);
/// @brief Key code for the numpad 3.
int64_t rt_game3d_key_num3(void);
/// @brief Key code for the numpad 4.
int64_t rt_game3d_key_num4(void);
/// @brief Key code for the numpad 5.
int64_t rt_game3d_key_num5(void);
/// @brief Key code for the numpad 6.
int64_t rt_game3d_key_num6(void);
/// @brief Key code for the numpad 7.
int64_t rt_game3d_key_num7(void);
/// @brief Key code for the numpad 8.
int64_t rt_game3d_key_num8(void);
/// @brief Key code for the numpad 9.
int64_t rt_game3d_key_num9(void);
/// @brief Key code for the numpad decimal point.
int64_t rt_game3d_key_numdot(void);
/// @brief Key code for the numpad divide.
int64_t rt_game3d_key_numdiv(void);
/// @brief Key code for the numpad multiply.
int64_t rt_game3d_key_nummul(void);
/// @brief Key code for the numpad subtract.
int64_t rt_game3d_key_numsub(void);
/// @brief Key code for the numpad add.
int64_t rt_game3d_key_numadd(void);
/// @brief Key code for the numpad Enter.
int64_t rt_game3d_key_numenter(void);

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
/// @brief Enable/disable raw relative mouse-look (capture + OS raw deltas).
void rt_game3d_input_set_relative_look(void *input, int8_t enabled);
/// @brief Bind a gamepad index into MoveAxis/LookAxis (-1 unbinds).
void rt_game3d_input_bind_pad(void *input, int64_t pad);
/// @brief Currently bound gamepad index (-1 when unbound).
int64_t rt_game3d_input_get_pad_bound(void *input);
/// @brief Set the right-stick look sensitivity (degrees/frame at full tilt).
void rt_game3d_input_set_pad_look_sensitivity(void *input, double sensitivity);
/// @brief Get the right-stick look sensitivity.
double rt_game3d_input_get_pad_look_sensitivity(void *input);

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
/// @brief Fluent: assign a mesh to every drawable node in this entity's scene-node subtree.
void *rt_game3d_entity_set_mesh_recursive(void *entity, void *mesh);
/// @brief Fluent: assign the material and return the entity.
void *rt_game3d_entity_set_material(void *entity, void *material);
/// @brief Fluent: assign a material to every node in this entity's scene-node subtree.
void *rt_game3d_entity_set_material_recursive(void *entity, void *material);
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
/// @brief Fluent: attach an animator, skeletal controller, or node animator and return entity.
void *rt_game3d_entity_attach_animator(void *entity, void *animator_or_controller);
/// @brief Fluent: parent @p child under this entity and drive it from the named
///   bone of this entity's animated skeleton (world = bone pose each step).
void *rt_game3d_entity_attach_to_bone(void *entity, void *child, rt_string bone_name);
/// @brief Build (cached) and activate a ragdoll from the entity's animator skeleton.
void *rt_game3d_entity_enable_ragdoll(void *entity);
/// @brief Deactivate the entity's ragdoll with a blend back to animation.
int8_t rt_game3d_entity_disable_ragdoll(void *entity, double blend_seconds);
/// @brief Get the entity's cached Ragdoll3D (NULL before enableRagdoll).
void *rt_game3d_entity_get_ragdoll(void *entity);
/// @brief Fluent: bone attachment with a positional offset in bone space.
void *rt_game3d_entity_attach_to_bone_offset(void *entity,
                                             void *child,
                                             rt_string bone_name,
                                             double offset_x,
                                             double offset_y,
                                             double offset_z);
/// @brief Fluent: remove this entity's bone-socket binding (stays parented).
void *rt_game3d_entity_detach_from_bone(void *entity);
/// @brief Fluent: attach a Behavior3D ticked by the world each simulation step
///   (null detaches).
void *rt_game3d_entity_attach_behavior(void *entity, void *behavior);
/// @brief The entity's attached Behavior3D (NULL if none).
void *rt_game3d_entity_get_behavior(void *entity);

//=========================================================================
// Behavior3D — composable per-entity preset behaviors (Viper.Game3D.Behavior3D)
//=========================================================================

/// @brief Create an empty behavior; compose presets with the fluent Add* calls.
void *rt_game3d_behavior_new(void);
/// @brief Fluent: continuous rotation about an axis at degrees/second.
void *rt_game3d_behavior_add_spin(
    void *behavior, double axis_x, double axis_y, double axis_z, double deg_per_sec);
/// @brief Fluent: circular XZ orbit around a world-space center.
void *rt_game3d_behavior_add_orbit(void *behavior,
                                   double center_x,
                                   double center_y,
                                   double center_z,
                                   double radius,
                                   double deg_per_sec);
/// @brief Fluent: vertical sine bobbing around the height at first tick.
void *rt_game3d_behavior_add_sine_float(void *behavior, double amplitude, double speed);
/// @brief Fluent: yaw so the entity's forward (-Z) axis points at the target.
void *rt_game3d_behavior_add_face_target(void *behavior, void *target_entity);
/// @brief Fluent: move toward the target entity, stopping inside range
///   (direct XZ steer, or via a bound NavAgent3D).
void *rt_game3d_behavior_add_chase(void *behavior, void *target_entity, double speed, double range);
/// @brief Fluent: follow a Path3D at constant speed (looping or one-shot).
void *rt_game3d_behavior_add_follow_path(void *behavior, void *path, double speed, int8_t loop);
/// @brief Fluent: despawn the entity after the given seconds of simulation time.
void *rt_game3d_behavior_add_lifetime(void *behavior, double seconds);
/// @brief C-internal one-shot preset: despawn @p target_entity on next update.
///   Not script-visible; exercises mid-sweep registry compaction in tests.
void *rt_game3d_behavior_add_despawn_target_internal(void *behavior, void *target_entity);
/// @brief Fluent: route chase movement through a NavAgent3D (null clears).
void *rt_game3d_behavior_set_nav_agent(void *behavior, void *agent);
/// @brief Advance one behavior for one entity by dt seconds (world tick entry).
void rt_game3d_behavior_update(void *behavior, void *entity, double dt);
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

/// @brief Create an animator driven by an AnimController3D or NodeAnimator3D.
/// @details Imported models can expose both skeletal and node animation. Use
/// ModelTemplate.Instantiate() to receive a combined wrapper automatically, or pass a raw
/// AnimController3D/NodeAnimator3D here for manual construction.
void *rt_game3d_animator_new(void *controller);
/// @brief Get the animation controller backing this animator.
void *rt_game3d_animator_get_controller(void *animator);
/// @brief Get the node animator backing this animator, or NULL for skeletal-only wrappers.
void *rt_game3d_animator_get_node_animator(void *animator);
/// @brief Get the model-space matrix for a bone from the final composited pose
///   (freshly allocated Mat4, NULL when unavailable).
void *rt_game3d_animator_get_bone_matrix(void *animator, int64_t bone_index);
/// @brief Resolve a bone index by name via the controller's skeleton (-1 if unknown).
int64_t rt_game3d_animator_find_bone(void *animator, rt_string name);
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
/// @brief Load a skeletal Animation3D clip from a model file by index.
void *rt_game3d_assets_load_animation(rt_string path, int64_t index);
/// @brief Load a skeletal Animation3D clip from a packed model asset by index.
void *rt_game3d_assets_load_animation_asset(rt_string path, int64_t index);
/// @brief Load a NodeAnimation3D clip from a model file by index.
void *rt_game3d_assets_load_node_animation(rt_string path, int64_t index);
/// @brief Load a NodeAnimation3D clip from a packed model asset by index.
void *rt_game3d_assets_load_node_animation_asset(rt_string path, int64_t index);
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
/// @brief Number of scenes addressable from the underlying model.
int64_t rt_game3d_model_template_get_scene_count(void *model_template);
/// @brief Name of the imported scene at @p index, or empty when out of range.
rt_string rt_game3d_model_template_get_scene_name(void *model_template, int64_t index);
/// @brief Number of imported cameras in @p scene_index.
int64_t rt_game3d_model_template_get_camera_count(void *model_template, int64_t scene_index);
/// @brief Get an imported camera from @p scene_index.
void *rt_game3d_model_template_get_camera(void *model_template, int64_t scene_index, int64_t index);
/// @brief Instantiate a fresh entity from the template.
void *rt_game3d_model_template_instantiate(void *model_template);
/// @brief Instantiate a fresh entity from a specific imported scene.
void *rt_game3d_model_template_instantiate_scene_at(void *model_template, int64_t index);

//=========================================================================
// Environment / EnvHandle — environment presets and builder (Viper.Game3D.Environment3D /
// EnvHandle)
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

/// @brief Fluent: enable exponential height fog (density pools below @p height).
void *rt_game3d_env_handle_with_height_fog(void *obj,
                                           double density,
                                           double height,
                                           double falloff);

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
/// @brief Get the downward gravity acceleration magnitude in units/second².
double rt_game3d_character_controller_get_gravity(void *controller);
/// @brief Set the downward gravity acceleration magnitude in units/second².
void rt_game3d_character_controller_set_gravity(void *controller, double gravity);
/// @brief Advance the controller using input and camera orientation over `dt` seconds.
void rt_game3d_character_controller_update(void *controller, void *input, void *camera, double dt);
/// @brief Teleport the character to an absolute world position, clearing velocity.
void rt_game3d_character_controller_teleport(void *controller, double x, double y, double z);
/// @brief True if the character is currently standing on ground.
int8_t rt_game3d_character_controller_grounded(void *controller);
/// @brief Get the crouch capsule height applied by SetCrouching(true).
double rt_game3d_character_controller_get_crouch_height(void *controller);
/// @brief Set the crouch capsule height.
void rt_game3d_character_controller_set_crouch_height(void *controller, double height);
/// @brief Toggle crouch; standing back up returns false when blocked by a ceiling.
int8_t rt_game3d_character_controller_set_crouching(void *controller, int8_t crouching);
/// @brief True while the crouch state is engaged.
int8_t rt_game3d_character_controller_is_crouching(void *controller);
/// @brief Get the dynamic push impulse scale (0 = block only).
double rt_game3d_character_controller_get_push_strength(void *controller);
/// @brief Set the dynamic push impulse scale.
void rt_game3d_character_controller_set_push_strength(void *controller, double strength);
/// @brief Whether the controller rides moving kinematic platforms.
int8_t rt_game3d_character_controller_get_ride_platforms(void *controller);
/// @brief Enable/disable riding moving platforms.
void rt_game3d_character_controller_set_ride_platforms(void *controller, int8_t enabled);
/// @brief True while the character rests on a too-steep surface.
int8_t rt_game3d_character_controller_is_sliding(void *controller);
/// @brief Entity owning the body under the character's feet (NULL if unmanaged).
void *rt_game3d_character_controller_ground_entity(void *controller);
/// @brief Probe for a grabbable ledge ahead (LedgeHit3D or NULL); defaults
///   origin/forward/radius from the character pose and entity facing.
void *rt_game3d_character_controller_probe_ledge(void *controller, double max_height);
/// @brief Probe for a vaultable obstacle ahead (LedgeHit3D or NULL).
void *rt_game3d_character_controller_probe_vault(void *controller,
                                                 double max_height,
                                                 double max_thickness);

//=========================================================================
// Combat volumes and health (Viper.Game3D.Hitbox3D / Health3D / events)
//=========================================================================

/// @brief Create an entity-space combat volume (kind Hurt, team 0, channel 1).
void *rt_game3d_hitbox_new(void *entity, void *collider);
/// @brief Create a bone-attached combat volume; traps on unknown bone names.
void *rt_game3d_hitbox_new_on_bone(void *entity, rt_string bone_name, void *collider);
/// @brief Get the volume kind (HitboxKind.Hurt = 0, HitboxKind.Hit = 1).
int64_t rt_game3d_hitbox_get_kind(void *hitbox);
/// @brief Set the volume kind (Hurt or Hit).
void rt_game3d_hitbox_set_kind(void *hitbox, int64_t kind);
/// @brief Get the team id (same-team pairs are skipped unless friendly fire).
int64_t rt_game3d_hitbox_get_team(void *hitbox);
/// @brief Set the team id.
void rt_game3d_hitbox_set_team(void *hitbox, int64_t team);
/// @brief Get the channel bitmask (hit and hurt channels must overlap).
int64_t rt_game3d_hitbox_get_channel(void *hitbox);
/// @brief Set the channel bitmask.
void rt_game3d_hitbox_set_channel(void *hitbox, int64_t channel);
/// @brief Get the manual activation switch.
int8_t rt_game3d_hitbox_get_active(void *hitbox);
/// @brief Set the manual activation switch (scripted attacks).
void rt_game3d_hitbox_set_active(void *hitbox, int8_t active);
/// @brief Get the friendly-fire flag.
int8_t rt_game3d_hitbox_get_friendly_fire(void *hitbox);
/// @brief Set the friendly-fire flag (allow same-team hits from this attacker).
void rt_game3d_hitbox_set_friendly_fire(void *hitbox, int8_t enabled);
/// @brief Fluent: bind an animation activation window (state name + time range).
void *rt_game3d_hitbox_bind_window(void *hitbox, rt_string state_name, double t0, double t1);
/// @brief Fluent: set the shape offset in bone/entity space.
void *rt_game3d_hitbox_set_local_offset(void *hitbox, double x, double y, double z);
/// @brief HitboxKind.Hurt constant (0).
int64_t rt_game3d_hitbox_kind_hurt(void);
/// @brief HitboxKind.Hit constant (1).
int64_t rt_game3d_hitbox_kind_hit(void);

/// @brief Create a health component with the given maximum hit points.
void *rt_game3d_health_new(double max_hp);
/// @brief Fluent: attach a Health3D component (one per entity; reattach replaces).
void *rt_game3d_entity_attach_health(void *entity, void *health);
/// @brief Get the entity's Health3D component (NULL when none).
void *rt_game3d_entity_get_health(void *entity);
/// @brief Current hit points.
double rt_game3d_health_get_current(void *health);
/// @brief Maximum hit points.
double rt_game3d_health_get_max(void *health);
/// @brief Set maximum hit points.
void rt_game3d_health_set_max(void *health, double max_hp);
/// @brief True once hp reached 0 (until Revive).
int8_t rt_game3d_health_is_dead(void *health);
/// @brief I-frame duration granted per applied damage.
double rt_game3d_health_get_invuln_seconds(void *health);
/// @brief Set the i-frame duration granted per applied damage.
void rt_game3d_health_set_invuln_seconds(void *health, double seconds);
/// @brief True while i-frames are active.
int8_t rt_game3d_health_get_invulnerable(void *health);
/// @brief Apply damage; returns the applied amount (0 while invulnerable/dead).
double rt_game3d_health_damage(void *health, double amount, void *source_entity, int64_t tag);
/// @brief Heal (clamped to max; no effect while dead).
void rt_game3d_health_heal(void *health, double amount);
/// @brief Clear the death latch and restore hp.
void rt_game3d_health_revive(void *health, double hp);
/// @brief One-shot: true for the step after hp crossed to 0.
int8_t rt_game3d_health_just_died(void *health);
/// @brief One-shot: true for the step after damage applied.
int8_t rt_game3d_health_just_damaged(void *health);
/// @brief Most recent applied damage amount.
double rt_game3d_health_last_damage(void *health);
/// @brief Most recent caller-supplied damage tag.
int64_t rt_game3d_health_last_tag(void *health);
/// @brief Impulse knockback on the owner's dynamic body (false for kinematic/none).
int8_t rt_game3d_health_apply_knockback(void *health,
                                        void *direction,
                                        double strength,
                                        void *point);

/// @brief Get the world time multiplier (default 1.0, clamped [0, 4]).
double rt_game3d_world_get_time_scale(void *world);
/// @brief Set the world time multiplier.
void rt_game3d_world_set_time_scale(void *world, double scale);
/// @brief Get the latched pause state.
int8_t rt_game3d_world_get_paused(void *world);
/// @brief Set the latched pause state (simulation freezes; rendering continues).
void rt_game3d_world_set_paused(void *world, int8_t paused);
/// @brief One-shot hit-stop for @p seconds of real time (max-latched).
void rt_game3d_world_hit_stop(void *world, double seconds);
/// @brief Real (unscaled) clamped frame step for UI/menus.
double rt_game3d_world_get_unscaled_dt(void *world);
/// @brief Real (unscaled) elapsed seconds for UI/menus.
double rt_game3d_world_get_unscaled_elapsed(void *world);
/// @brief Live DOF focus pull through the world post-FX chain; false when the
///   chain has no DOF effect.
int8_t rt_game3d_world_set_dof_focus(void *world, double distance);

/// @brief Number of hit events buffered by the most recent step.
int64_t rt_game3d_world_hit_event_count(void *world);
/// @brief Get a buffered hit event as a boxed HitEvent3D (NULL out of range).
void *rt_game3d_world_hit_event(void *world, int64_t index);
/// @brief Clear buffered hit and damage events without stepping.
void rt_game3d_world_clear_hit_events(void *world);
/// @brief Number of damage events buffered since the last step.
int64_t rt_game3d_world_damage_event_count(void *world);
/// @brief Get a buffered damage event as a boxed DamageEvent3D.
void *rt_game3d_world_damage_event(void *world, int64_t index);
/// @brief HitEvent3D.Attacker accessor.
void *rt_game3d_hit_event_get_attacker(void *event);
/// @brief HitEvent3D.Victim accessor.
void *rt_game3d_hit_event_get_victim(void *event);
/// @brief HitEvent3D.Hitbox accessor (attacking volume).
void *rt_game3d_hit_event_get_hitbox(void *event);
/// @brief HitEvent3D.Hurtbox accessor (victim volume).
void *rt_game3d_hit_event_get_hurtbox(void *event);
/// @brief HitEvent3D.Point — witness point (fresh Vec3).
void *rt_game3d_hit_event_point(void *event);
/// @brief HitEvent3D.Normal — contact normal (fresh Vec3, +Y fallback).
void *rt_game3d_hit_event_normal(void *event);
/// @brief DamageEvent3D.Victim accessor.
void *rt_game3d_damage_event_get_victim(void *event);
/// @brief DamageEvent3D.Source accessor (NULL when absent/stale).
void *rt_game3d_damage_event_get_source(void *event);
/// @brief DamageEvent3D.Amount accessor.
double rt_game3d_damage_event_get_amount(void *event);
/// @brief DamageEvent3D.Tag accessor.
int64_t rt_game3d_damage_event_get_tag(void *event);
/// @brief DamageEvent3D.WasLethal accessor.
int8_t rt_game3d_damage_event_get_was_lethal(void *event);

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
// ThirdPersonController — collision-aware spring-arm over-the-shoulder camera
// with camera-relative character drive (Viper.Game3D.ThirdPersonController)
//=========================================================================

/// @brief Create a third-person spring-arm controller orbiting the target entity.
void *rt_game3d_thirdperson_controller_new(void *world, void *target_entity);
/// @brief Get the orbited target entity.
void *rt_game3d_thirdperson_controller_get_target(void *controller);
/// @brief Set the orbited target entity.
void rt_game3d_thirdperson_controller_set_target(void *controller, void *target_entity);
/// @brief Get the optional CharacterController3D drive slot.
void *rt_game3d_thirdperson_controller_get_character(void *controller);
/// @brief Set the CharacterController3D driven camera-relatively each Update.
void rt_game3d_thirdperson_controller_set_character(void *controller, void *character_controller);
/// @brief Get the desired (pre-collision) boom length.
double rt_game3d_thirdperson_controller_get_distance(void *controller);
/// @brief Set the desired boom length (clamped to [MinDistance, MaxDistance]).
void rt_game3d_thirdperson_controller_set_distance(void *controller, double distance);
/// @brief Get the boom pull-in floor.
double rt_game3d_thirdperson_controller_get_min_distance(void *controller);
/// @brief Set the boom pull-in floor.
void rt_game3d_thirdperson_controller_set_min_distance(void *controller, double min_distance);
/// @brief Get the boom length ceiling.
double rt_game3d_thirdperson_controller_get_max_distance(void *controller);
/// @brief Set the boom length ceiling.
void rt_game3d_thirdperson_controller_set_max_distance(void *controller, double max_distance);
/// @brief Get the local-space shoulder offset as a Vec3.
void *rt_game3d_thirdperson_controller_get_shoulder_offset(void *controller);
/// @brief Set the local-space shoulder offset (x = lateral, y = up, z = forward).
void rt_game3d_thirdperson_controller_set_shoulder_offset(void *controller, void *offset);
/// @brief Get the pivot height above the target entity origin.
double rt_game3d_thirdperson_controller_get_pivot_height(void *controller);
/// @brief Set the pivot height above the target entity origin.
void rt_game3d_thirdperson_controller_set_pivot_height(void *controller, double height);
/// @brief Get the boom-release smoothing factor.
double rt_game3d_thirdperson_controller_get_damping(void *controller);
/// @brief Set the boom-release smoothing factor (pull-in stays instant).
void rt_game3d_thirdperson_controller_set_damping(void *controller, double damping);
/// @brief Get the camera orbit yaw in degrees (yaw 0 looks down -Z, 90 down -X).
double rt_game3d_thirdperson_controller_get_yaw(void *controller);
/// @brief Set the camera orbit yaw in degrees.
void rt_game3d_thirdperson_controller_set_yaw(void *controller, double yaw);
/// @brief Get the camera orbit pitch in degrees (positive = camera above pivot).
double rt_game3d_thirdperson_controller_get_pitch(void *controller);
/// @brief Set the camera orbit pitch in degrees (clamped to the pitch range).
void rt_game3d_thirdperson_controller_set_pitch(void *controller, double pitch);
/// @brief Get the boom sweep sphere radius.
double rt_game3d_thirdperson_controller_get_collision_radius(void *controller);
/// @brief Set the boom sweep sphere radius.
void rt_game3d_thirdperson_controller_set_collision_radius(void *controller, double radius);
/// @brief Get the boom collision layer mask.
int64_t rt_game3d_thirdperson_controller_get_collision_mask(void *controller);
/// @brief Set the boom collision layer mask (exclude character/projectile layers).
void rt_game3d_thirdperson_controller_set_collision_mask(void *controller, int64_t mask);
/// @brief Get whether occluder fading is enabled.
int8_t rt_game3d_thirdperson_controller_get_occlusion_fade(void *controller);
/// @brief Enable/disable occluder fading (disable restores faded materials).
void rt_game3d_thirdperson_controller_set_occlusion_fade(void *controller, int8_t enabled);
/// @brief Get the aim-mode request flag.
int8_t rt_game3d_thirdperson_controller_get_aiming(void *controller);
/// @brief Set the aim-mode request flag (distance/FOV blend animates smoothly).
void rt_game3d_thirdperson_controller_set_aiming(void *controller, int8_t aiming);
/// @brief Get the aim-mode boom length.
double rt_game3d_thirdperson_controller_get_aim_distance(void *controller);
/// @brief Set the aim-mode boom length.
void rt_game3d_thirdperson_controller_set_aim_distance(void *controller, double distance);
/// @brief Get the aim-mode camera FOV in degrees.
double rt_game3d_thirdperson_controller_get_aim_fov(void *controller);
/// @brief Set the aim-mode camera FOV in degrees.
void rt_game3d_thirdperson_controller_set_aim_fov(void *controller, double fov);
/// @brief Get the installed TargetLock3D framing source (NULL if none).
void *rt_game3d_thirdperson_controller_get_lock_target(void *controller);
/// @brief Install a TargetLock3D framing source (NULL to clear).
void rt_game3d_thirdperson_controller_set_lock_target(void *controller, void *lock);
/// @brief Pre-physics update: look input, aim blend, character drive.
void rt_game3d_thirdperson_controller_update(void *controller, void *world, double dt);
/// @brief Post-sync late update: swept spring-arm camera, aim FOV, occluder fade.
void rt_game3d_thirdperson_controller_late_update(void *controller, void *world, double dt);

//=========================================================================
// RailCamera3D — spline camera on a Path3D (Viper.Game3D.RailCamera3D)
//=========================================================================

/// @brief Create a rail camera riding a Path3D (arclength-constant traversal).
void *rt_game3d_rail_camera_new(void *world, void *path);
/// @brief Get the requested arclength-normalized progress [0,1].
double rt_game3d_rail_camera_get_progress(void *controller);
/// @brief Set the requested progress (damped when PositionDamping > 0).
void rt_game3d_rail_camera_set_progress(void *controller, double progress);
/// @brief Get the auto-advance speed in units/sec along arclength (0 = manual).
double rt_game3d_rail_camera_get_speed(void *controller);
/// @brief Set the auto-advance speed.
void rt_game3d_rail_camera_set_speed(void *controller, double speed);
/// @brief Get the progress damping factor (0 = snap).
double rt_game3d_rail_camera_get_position_damping(void *controller);
/// @brief Set the progress damping factor.
void rt_game3d_rail_camera_set_position_damping(void *controller, double damping);
/// @brief Look at an entity's post-physics position (clears other look modes).
void rt_game3d_rail_camera_set_look_entity(void *controller, void *entity);
/// @brief Look at a fixed Vec3 point (clears other look modes).
void rt_game3d_rail_camera_set_look_point(void *controller, void *point);
/// @brief Look along a second path evaluated at the same t (clears other modes).
void rt_game3d_rail_camera_set_look_path(void *controller, void *path);
/// @brief Fluent: add an FOV key at arclength t.
void *rt_game3d_rail_camera_add_fov_key(void *controller, double t, double fov);
/// @brief Fluent: add a roll key (degrees about the view axis) at arclength t.
void *rt_game3d_rail_camera_add_roll_key(void *controller, double t, double degrees);
/// @brief Whether keys interpolate with smoothstep instead of linearly.
int8_t rt_game3d_rail_camera_get_key_ease(void *controller);
/// @brief Choose smoothstep (true) or linear (false) key interpolation.
void rt_game3d_rail_camera_set_key_ease(void *controller, int8_t smooth);
/// @brief Pre-physics update: auto-advance and damp progress.
void rt_game3d_rail_camera_update(void *controller, void *world, double dt);
/// @brief Post-sync late update: evaluate the spline + keys, write the camera.
void rt_game3d_rail_camera_late_update(void *controller, void *world, double dt);

//=========================================================================
// TargetLock3D — lock-on target acquisition, cycling, and framing source
// (Viper.Game3D.TargetLock3D)
//=========================================================================

/// @brief Create a lock-on helper scoring candidates around the owner entity.
void *rt_game3d_targetlock_new(void *world, void *owner_entity);
/// @brief Get the currently locked entity (NULL when unlocked).
void *rt_game3d_targetlock_get_target(void *lock);
/// @brief Get the acquisition radius.
double rt_game3d_targetlock_get_max_distance(void *lock);
/// @brief Set the acquisition radius.
void rt_game3d_targetlock_set_max_distance(void *lock, double distance);
/// @brief Get the half-angle acquisition cone in degrees.
double rt_game3d_targetlock_get_cone_degrees(void *lock);
/// @brief Set the half-angle acquisition cone in degrees.
void rt_game3d_targetlock_set_cone_degrees(void *lock, double degrees);
/// @brief Get the targetable layer mask.
int64_t rt_game3d_targetlock_get_candidate_mask(void *lock);
/// @brief Set the targetable layer mask.
void rt_game3d_targetlock_set_candidate_mask(void *lock, int64_t mask);
/// @brief Get whether candidates must have line of sight.
int8_t rt_game3d_targetlock_get_require_los(void *lock);
/// @brief Set whether candidates must have line of sight.
void rt_game3d_targetlock_set_require_los(void *lock, int8_t require);
/// @brief Get the current-target score multiplier.
double rt_game3d_targetlock_get_stickiness(void *lock);
/// @brief Set the current-target score multiplier.
void rt_game3d_targetlock_set_stickiness(void *lock, double stickiness);
/// @brief Get the auto-release distance.
double rt_game3d_targetlock_get_break_distance(void *lock);
/// @brief Set the auto-release distance.
void rt_game3d_targetlock_set_break_distance(void *lock, double distance);
/// @brief Get the LoS-break grace period in seconds.
double rt_game3d_targetlock_get_los_grace_seconds(void *lock);
/// @brief Set the LoS-break grace period in seconds (0 = instant release).
void rt_game3d_targetlock_set_los_grace_seconds(void *lock, double seconds);
/// @brief Acquire the best candidate in view; true when a target is locked.
int8_t rt_game3d_targetlock_acquire(void *lock);
/// @brief Release the current target without firing JustLost.
void rt_game3d_targetlock_clear(void *lock);
/// @brief Cycle to the nearest candidate left (-1) / right (+1); true on change.
int8_t rt_game3d_targetlock_cycle(void *lock, int64_t direction);
/// @brief Per-step maintenance: auto-release on death/distance/LoS-grace.
void rt_game3d_targetlock_update(void *lock, double dt);
/// @brief One-shot: true for the frame after a target was acquired.
int8_t rt_game3d_targetlock_just_acquired(void *lock);
/// @brief One-shot: true for the frame after the target was auto-released.
int8_t rt_game3d_targetlock_just_lost(void *lock);
/// @brief Rotate a planar move vector up to 12° toward the target bearing.
void *rt_game3d_targetlock_locked_move_bias(void *lock, void *move);

//=========================================================================
// Timeline3D — in-engine cutscene sequencer (Viper.Game3D.Timeline3D)
//=========================================================================

/// @brief Create an empty timeline bound to a world (start via playTimeline).
void *rt_game3d_timeline_new(void *world);
/// @brief Fluent: camera cut (pose applied at t, held until the next camera key).
void *rt_game3d_timeline_add_camera_cut(void *tl, double t, void *pos, void *look, double fov);
/// @brief Fluent: camera spline move over [t0,t1] (look = Vec3|Entity3D|Path3D|NULL).
void *rt_game3d_timeline_add_camera_move(
    void *tl, double t0, double t1, void *path, void *look_target, int64_t ease);
/// @brief Fluent: FOV ramp lerped over [t0,t1].
void *rt_game3d_timeline_add_fov_ramp(
    void *tl, double t0, double t1, double fov0, double fov1, int64_t ease);
/// @brief Fluent: fire Animator3D.crossfade on a named entity at t.
void *rt_game3d_timeline_add_anim(
    void *tl, double t, rt_string entity_name, rt_string state_name, double crossfade_seconds);
/// @brief Fluent: fire an audio clip at t (2D, or positional at a Vec3).
void *rt_game3d_timeline_add_audio(void *tl, double t, void *clip, int8_t positional, void *pos);
/// @brief Fluent: subtitle text shown over [t0,t1].
void *rt_game3d_timeline_add_subtitle(void *tl, double t0, double t1, rt_string text);
/// @brief Fluent: letterbox bars covering a height fraction over [t0,t1].
void *rt_game3d_timeline_add_letterbox(void *tl, double t0, double t1, double amount);
/// @brief Fluent: full-screen fade from alpha a0 to a1 over [t0,t1].
void *rt_game3d_timeline_add_fade(void *tl, double t0, double t1, double a0, double a1);
/// @brief Fluent: polled event marker fired when the playhead crosses t.
void *rt_game3d_timeline_add_marker(void *tl, double t, int64_t id);
/// @brief Total duration (max track end time).
double rt_game3d_timeline_get_duration(void *tl);
/// @brief Current playhead time.
double rt_game3d_timeline_get_time(void *tl);
/// @brief True while playing.
int8_t rt_game3d_timeline_get_playing(void *tl);
/// @brief True once the playhead reached the end (until re-play).
int8_t rt_game3d_timeline_get_finished(void *tl);
/// @brief Whether skip() is allowed (default true).
int8_t rt_game3d_timeline_get_skippable(void *tl);
/// @brief Gate skip().
void rt_game3d_timeline_set_skippable(void *tl, int8_t skippable);
/// @brief One-shot: true for the step after the timeline finished.
int8_t rt_game3d_timeline_just_finished(void *tl);
/// @brief Markers fired during the most recent step.
int64_t rt_game3d_timeline_events_fired_count(void *tl);
/// @brief Marker id at index within the most recent step's fired set.
int64_t rt_game3d_timeline_event_fired_id(void *tl, int64_t index);
/// @brief Currently displayed subtitle text ("" when none).
rt_string rt_game3d_timeline_active_subtitle(void *tl);
/// @brief Skip to the end (final anim states, silent audio, markers fire).
void rt_game3d_timeline_skip(void *tl);
/// @brief Stop playback and uninstall from the world (controller resumes).
void rt_game3d_timeline_stop(void *tl);
/// @brief Install and start a timeline on the world (one at a time).
void rt_game3d_world_play_timeline(void *world, void *tl);
/// @brief The world's active timeline (NULL when none).
void *rt_game3d_world_active_timeline(void *world);
/// @brief Stop and uninstall the world's active timeline.
void rt_game3d_world_stop_timeline(void *world);

//=========================================================================
// Dialogue3D — 3D conversation surface (Viper.Game3D.Dialogue3D)
//=========================================================================

/// @brief Create a conversation bound to a world (shown via Show).
void *rt_game3d_dialogue_new(void *world);
/// @brief Fluent: queue a line (text may be a localization key).
void *rt_game3d_dialogue_say(void *dialogue, rt_string speaker, rt_string text);
/// @brief Fluent: queue a voiced line (clip plays when the line starts).
void *rt_game3d_dialogue_say_voiced(void *dialogue, rt_string speaker, rt_string text, void *clip);
/// @brief Fluent: queue a blocking choice prompt (seq<str> of 1..8 options).
void *rt_game3d_dialogue_ask_choice(void *dialogue, void *options_seq);
/// @brief Show: install as the world's active conversation.
void rt_game3d_dialogue_show(void *dialogue);
/// @brief Hide: release the world slot without clearing queued lines.
void rt_game3d_dialogue_hide(void *dialogue);
/// @brief Advance (or complete the reveal first — two-stage skip).
void rt_game3d_dialogue_advance(void *dialogue);
/// @brief Complete the current line's reveal instantly.
void rt_game3d_dialogue_skip_reveal(void *dialogue);
/// @brief Move the choice highlight (clamped).
void rt_game3d_dialogue_move_choice(void *dialogue, int64_t delta);
/// @brief Confirm the highlighted choice (latches choiceMade/lastChoice).
void rt_game3d_dialogue_confirm_choice(void *dialogue);
/// @brief True while shown.
int8_t rt_game3d_dialogue_get_active(void *dialogue);
/// @brief Queued line count.
int64_t rt_game3d_dialogue_get_line_count(void *dialogue);
/// @brief True while a choice prompt blocks advance.
int8_t rt_game3d_dialogue_get_choice_pending(void *dialogue);
/// @brief One-shot: a choice was confirmed since the last query.
int8_t rt_game3d_dialogue_choice_made(void *dialogue);
/// @brief Index confirmed by the last choice prompt (-1 when none).
int64_t rt_game3d_dialogue_last_choice(void *dialogue);
/// @brief Currently revealed text of the active line.
rt_string rt_game3d_dialogue_current_text(void *dialogue);
/// @brief Anchor bubbles above this entity (NULL clears).
void rt_game3d_dialogue_set_speaker_entity(void *dialogue, void *entity);
/// @brief Anchored-bubble mode (falls back to the bottom panel off-screen).
void rt_game3d_dialogue_set_anchored(void *dialogue, int8_t anchored);
/// @brief Auto-advance lines after the reveal completes plus a hold.
void rt_game3d_dialogue_set_auto_advance(void *dialogue, int8_t enabled);
/// @brief Typewriter speed in characters/second (default 40).
void rt_game3d_dialogue_set_reveal_speed(void *dialogue, double chars_per_second);
/// @brief Bind a MessageBundle for localization-key resolution (NULL unbinds).
void rt_game3d_dialogue_set_locale(void *dialogue, void *bundle);
/// @brief Style knobs: panel alpha and speaker-name color.
void rt_game3d_dialogue_set_style(void *dialogue, double panel_alpha, int64_t name_color);

//=========================================================================
// LipSync3D — amplitude visemes + blink/gaze layer (Viper.Game3D.LipSync3D)
//=========================================================================

/// @brief Create and attach a facial component to an entity (one per entity).
void *rt_game3d_lipsync_new(void *entity);
/// @brief Entity accessor for the attached LipSync3D (NULL when none).
void *rt_game3d_entity_get_lipsync(void *entity);
/// @brief Fluent: bind the MorphTarget3D the shape bindings drive.
void *rt_game3d_lipsync_bind_morph(void *lipsync, void *morph);
/// @brief Fluent: bind a mouth shape (up to 4) with a per-shape weight scale.
void *rt_game3d_lipsync_bind_mouth_shape(void *lipsync, rt_string shape_name, double scale);
/// @brief Fluent: create the gaze LookAt solver for a named head bone.
void *rt_game3d_lipsync_bind_head_bone(void *lipsync, rt_string bone_name);
/// @brief Drive from a playing voice (enables per-voice metering).
void rt_game3d_lipsync_drive(void *lipsync, int64_t voice_id);
/// @brief Drive from an explicit level 0..1 (dialogue/tests).
void rt_game3d_lipsync_drive_level(void *lipsync, double level);
/// @brief Release the drive (mouth eases closed).
void rt_game3d_lipsync_stop(void *lipsync);
/// @brief Configure the seeded procedural blink layer.
void rt_game3d_lipsync_set_blink(
    void *lipsync, int8_t enabled, rt_string shape_name, double min_interval, double max_interval);
/// @brief Ease gaze toward an Entity3D/Vec3 target (NULL clears).
void rt_game3d_lipsync_set_gaze(void *lipsync, void *target);
/// @brief True while a voice drive is active.
int8_t rt_game3d_lipsync_get_driving(void *lipsync);
/// @brief Current smoothed envelope level.
double rt_game3d_lipsync_get_level(void *lipsync);

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
/// @brief Create a game world with a camera FOV authored in horizontal degrees.
/// @details The runtime converts @p horizontal_fov_deg to the vertical FOV used by the renderer
///   based on the initial window aspect ratio. This avoids the wide-angle/fisheye look that can
///   happen when a conventional horizontal FOV value is passed to the vertical-FOV constructor.
void *rt_game3d_world_new_with_horizontal_camera(rt_string title,
                                                 int64_t width,
                                                 int64_t height,
                                                 double horizontal_fov_deg,
                                                 double near_plane,
                                                 double far_plane);
/// @brief Create a fullscreen world at desktop resolution (no windowed flash).
void *rt_game3d_world_new_fullscreen(rt_string title);
/// @brief Create a fullscreen world with a camera FOV authored in horizontal degrees.
void *rt_game3d_world_new_fullscreen_with_horizontal_camera(rt_string title,
                                                            double horizontal_fov_deg,
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
/// @brief Get the render interpolation fraction left in the fixed-timestep accumulator.
double rt_game3d_world_get_fixed_interpolation_alpha(void *world);
/// @brief Enable/disable built-in fixed-step render interpolation of entity poses.
void rt_game3d_world_set_render_interpolation(void *world, int8_t enabled);
/// @brief Whether built-in fixed-step render interpolation is enabled.
int8_t rt_game3d_world_get_render_interpolation(void *world);
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
/// @brief Count draw submissions skipped specifically by CPU frustum culling.
int64_t rt_game3d_world_get_frustum_culled_draw_count(void *world);
/// @brief Count draw submissions skipped specifically by the CPU occlusion grid.
int64_t rt_game3d_world_get_cpu_occluded_draw_count(void *world);
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
/// @brief Spawn an invisible static heightfield collider (from a Pixels heightmap) so a
///   standalone Terrain3D becomes solid to physics/character controllers. The heightfield is
///   centered on the entity; pass the terrain center as (pos_x,pos_y,pos_z). Returns the entity.
void *rt_game3d_world_spawn_heightfield_collider(void *world,
                                                 void *heightmap,
                                                 double scale_x,
                                                 double scale_y,
                                                 double scale_z,
                                                 double pos_x,
                                                 double pos_y,
                                                 double pos_z);
/// @brief Despawn an entity, removing it from scene and physics.
void rt_game3d_world_despawn(void *world, void *entity);
/// @brief Find a scene node by name, or null if absent.
void *rt_game3d_world_find_node(void *world, rt_string name);
/// @brief Find a scene node by name as Some(SceneNode3D), or None when absent.
void *rt_game3d_world_find_node_option(void *world, rt_string name);
/// @brief Find a spawned entity by name, or null if absent.
void *rt_game3d_world_find_entity(void *world, rt_string name);
/// @brief Find a spawned entity by name as Some(Entity3D), or None when absent.
void *rt_game3d_world_find_entity_option(void *world, rt_string name);
/// @brief Install a camera controller to drive the world's camera each frame.
void rt_game3d_world_set_camera_controller(void *world, void *controller);
/// @brief Point the camera to look at the given target entity or point.
void rt_game3d_world_look_at(void *world, void *target);
/// @brief Notify the world its window was resized to the given dimensions.
void rt_game3d_world_on_resize(void *world, int64_t width, int64_t height);
/// @brief Set the global ambient light color.
void rt_game3d_world_set_ambient(void *world, double r, double g, double b);
/// @brief Enable/disable image-based lighting from the world's skybox environment.
void rt_game3d_world_set_ibl_enabled(void *world, int8_t enabled);
/// @brief True when image-based lighting is enabled for the world's canvas.
int8_t rt_game3d_world_get_ibl_enabled(void *world);
/// @brief Scale the environment lighting contribution (default 1.0).
void rt_game3d_world_set_ibl_intensity(void *world, double intensity);
/// @brief Current environment lighting intensity for the world's canvas.
double rt_game3d_world_get_ibl_intensity(void *world);
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
/// @brief Run the blocking game loop, calling `update(dt)` each frame until the window closes.
/// @details Native callers pass a C-callable function pointer. VM/BytecodeVM callers pass a script
/// function reference; the Game3D VM bridge resolves it and calls the runtime with a native
/// trampoline. The update callback signature is `(Float) -> Unit`.
void rt_game3d_world_run(void *world, void *update);
/// @brief Run the game loop with per-frame `update(dt)` and 2D `overlay()` callbacks.
/// @details Native callers pass C-callable function pointers. VM/BytecodeVM callers pass script
/// function references, which the Game3D VM bridge invokes through native trampolines. The update
/// signature is `(Float) -> Unit`; the overlay signature is `() -> Unit`.
void rt_game3d_world_run_with_overlay(void *world, void *update, void *overlay);
/// @brief Run a fixed-timestep game loop with the given step and `update(dt)` callback.
/// @details Native callers pass a C-callable function pointer. VM/BytecodeVM callers pass a script
/// function reference, which the Game3D VM bridge invokes through a native trampoline. The update
/// callback signature is `(Float) -> Unit`.
void rt_game3d_world_run_fixed(void *world, double step_sec, void *update);
/// @brief Run a fixed-timestep loop with `update(dt)` and 2D `overlay()` callbacks.
/// @details Native callers pass C-callable function pointers. VM/BytecodeVM callers pass script
/// function references, which the Game3D VM bridge invokes through native trampolines. The update
/// signature is `(Float) -> Unit`; the overlay signature is `() -> Unit`.
void rt_game3d_world_run_fixed_with_overlay(void *world,
                                            double step_sec,
                                            void *update,
                                            void *overlay);
/// @brief Run a deterministic fixed number of frames at a fixed step using `update(dt)`.
/// @details Native callers pass a C-callable function pointer. VM/BytecodeVM callers pass a script
/// function reference, which the Game3D VM bridge invokes through a native trampoline. The update
/// callback signature is `(Float) -> Unit`.
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
/// @brief Draw a 2D `overlay()` callback over the current frame.
/// @details Native callers pass a C-callable function pointer. VM/BytecodeVM callers pass a script
/// function reference, which the Game3D VM bridge invokes through a native trampoline. The overlay
/// callback signature is `() -> Unit`.
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
/// @brief Get resident bytes of a scene-cell's loaded binary sidecar payload (0 if none/unloaded).
int64_t rt_game3d_world_stream_get_cell_sidecar_bytes(void *stream, int64_t index);
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
/// @brief Get resident bytes of a terrain-tile's loaded binary sidecar payload (0 if
/// none/unloaded).
int64_t rt_game3d_world_stream_get_terrain_tile_sidecar_bytes(void *stream, int64_t index);
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

/* Game3D.Surfaces — process-global surface-tag registry (plan 20). */
/// @brief Register (or look up) a surface name; ids stable from 1; idempotent.
int64_t rt_game3d_surfaces_register(rt_string name);
/// @brief Name for a surface id, or "" when unknown.
rt_string rt_game3d_surfaces_name_of(int64_t id);
/// @brief Id for a surface name, or 0 when unregistered.
int64_t rt_game3d_surfaces_id_of(rt_string name);
/// @brief Number of registered surfaces.
int64_t rt_game3d_surfaces_count(void);

/* Footsteps — SurfaceTable3D + Footsteps3D (plan 23). */
/// @brief Create an empty per-surface footstep table (row 0 = untyped default).
void *rt_game3d_surface_table_new(void);
/// @brief Append a clip variant for a surface id (fluent; up to 8 per row).
void *rt_game3d_surface_table_add_clip(void *table, int64_t surface_id, void *clip);
/// @brief Hearing-stimulus loudness scale for a surface row (fluent; default 1).
void *rt_game3d_surface_table_set_loudness(void *table, int64_t surface_id, double loudness);
/// @brief Configured clip count for a surface row.
int64_t rt_game3d_surface_table_clip_count(void *table, int64_t surface_id);
/// @brief Bind footsteps to an entity (requires an animator for event mode).
void *rt_game3d_footsteps_new(void *entity, void *table);
/// @brief Animator event-name prefix consumed as steps (fluent; default "footstep").
void *rt_game3d_footsteps_set_event_prefix(void *steps, rt_string prefix);
/// @brief Ground raycast mask (fluent; default -1 = everything).
void *rt_game3d_footsteps_set_ground_mask(void *steps, int64_t mask);
/// @brief Playback volume scale (fluent; default 1).
void *rt_game3d_footsteps_set_volume_scale(void *steps, double scale);
/// @brief Lifetime steps fired (telemetry/tests).
int64_t rt_game3d_footsteps_get_step_count(void *steps);
/// @brief Surface id resolved by the most recent step (0 = untyped).
int64_t rt_game3d_footsteps_get_last_surface(void *steps);

/* Interaction — Interactable3D + Interactor3D (plan 21). */
void *rt_game3d_interactable_new(void *entity);
void *rt_game3d_interactable_with_prompt(void *item, rt_string prompt);
rt_string rt_game3d_interactable_get_prompt(void *item);
void *rt_game3d_interactable_with_kind(void *item, int64_t kind);
int64_t rt_game3d_interactable_get_kind(void *item);
void *rt_game3d_interactable_with_radius(void *item, double radius);
double rt_game3d_interactable_get_radius(void *item);
void rt_game3d_interactable_set_enabled(void *item, int8_t enabled);
int8_t rt_game3d_interactable_get_enabled(void *item);
void rt_game3d_interactable_set_focus_priority(void *item, double priority);
double rt_game3d_interactable_get_focus_priority(void *item);
void *rt_game3d_interactor_new(void *entity);
void rt_game3d_interactor_set_cone_degrees(void *scanner, double degrees);
double rt_game3d_interactor_get_cone_degrees(void *scanner);
void rt_game3d_interactor_set_require_los(void *scanner, int8_t required);
int8_t rt_game3d_interactor_get_require_los(void *scanner);
void rt_game3d_interactor_set_los_mask(void *scanner, int64_t mask);
int64_t rt_game3d_interactor_get_los_mask(void *scanner);
void *rt_game3d_interactor_get_focused(void *scanner);
int8_t rt_game3d_interactor_focus_changed(void *scanner);
int8_t rt_game3d_interactor_interact(void *scanner);
int64_t rt_game3d_interactor_get_interact_count(void *scanner);
void *rt_game3d_interactor_get_last_interacted(void *scanner);

/* AI — Perception3D + BehaviorTree3D (plan 22). */
void *rt_game3d_perception_new(void *entity);
void rt_game3d_perception_set_sight(void *sense,
                                    double range,
                                    double fov_degrees,
                                    double eye_height);
void rt_game3d_perception_set_hearing(void *sense, double range_at_loudness1);
void rt_game3d_perception_set_target_mask(void *sense, int64_t mask);
void rt_game3d_perception_set_los_mask(void *sense, int64_t mask);
int64_t rt_game3d_perception_seen_count(void *sense);
void *rt_game3d_perception_seen_target(void *sense, int64_t index);
void *rt_game3d_perception_last_known_position(void *sense, void *target);
int8_t rt_game3d_perception_seen_changed(void *sense);
int64_t rt_game3d_perception_heard_count(void *sense);
void *rt_game3d_perception_heard_position(void *sense, int64_t index);
int64_t rt_game3d_perception_heard_tag(void *sense, int64_t index);
void rt_game3d_world_report_sound(void *world, void *position, double loudness, int64_t tag);
void *rt_game3d_btree_new(void);
int64_t rt_game3d_btree_sequence(void *tree);
int64_t rt_game3d_btree_selector(void *tree);
int64_t rt_game3d_btree_inverter(void *tree);
int64_t rt_game3d_btree_can_see(void *tree);
int64_t rt_game3d_btree_wait(void *tree, double seconds);
int64_t rt_game3d_btree_move_to_target(void *tree, double speed, double arrive_distance);
int64_t rt_game3d_btree_move_to_last_known(void *tree, double speed, double arrive_distance);
int64_t rt_game3d_btree_custom(void *tree, int64_t id);
void rt_game3d_btree_add_child(void *tree, int64_t parent, int64_t child);
void rt_game3d_btree_set_root(void *tree, int64_t node);
void *rt_game3d_bt_instance_new(void *entity, void *tree);
void rt_game3d_bt_instance_set_target(void *instance, void *target_entity);
int64_t rt_game3d_bt_instance_pending_custom(void *instance);
void rt_game3d_bt_instance_resolve(void *instance, int8_t success);

/* Audio immersion — reverb zones, occlusion, ambient beds, dialogue (plan 24). */
void *rt_game3d_reverbzone_new(void *min, void *max);
void *rt_game3d_reverbzone_set_reverb(void *zone, double room, double damping, double wet);
void rt_game3d_reverbzone_set_priority(void *zone, int64_t priority);
int64_t rt_game3d_reverbzone_get_priority(void *zone);
void rt_game3d_audio_add_reverb_zone(void *audio, void *zone);
void rt_game3d_audio_set_reverb_blend(void *audio, double seconds);
double rt_game3d_audio_get_reverb_wet(void *audio);
void rt_game3d_audio_set_reverb_routing(void *audio, int8_t enabled);
void rt_game3d_audio_set_occlusion(void *audio, int8_t enabled, int64_t mask, double amount);
void rt_game3d_audio_set_occlusion_budget(void *audio, int64_t budget);
int64_t rt_game3d_audio_play_dialogue(void *audio, void *clip);
void *rt_game3d_ambientbed_new(void *world);
void *rt_game3d_ambientbed_add_zone(void *bed, void *min, void *max, void *clip, int64_t volume);
void *rt_game3d_ambientbed_set_default(void *bed, void *clip, int64_t volume);
void rt_game3d_ambientbed_set_crossfade(void *bed, double seconds);
double rt_game3d_ambientbed_get_crossfade(void *bed);
int64_t rt_game3d_ambientbed_get_active_zone(void *bed);

/* World persistence — entity-state deltas, cell flags, VW3DSAV1 (plan 17). */
void *rt_game3d_entity_set_persistent(void *entity, rt_string key);
rt_string rt_game3d_entity_get_persistent_key(void *entity);
void rt_game3d_entity_set_state_tag(void *entity, int64_t tag);
int64_t rt_game3d_entity_get_state_tag(void *entity);
int8_t rt_game3d_world_get_persistent_alive(void *world, rt_string key);
void *rt_game3d_world_get_persistent_position(void *world, rt_string key);
int8_t rt_game3d_world_save_state(void *world, rt_string app_name, rt_string slot);
int8_t rt_game3d_world_load_state(void *world, rt_string app_name, rt_string slot);
void rt_game3d_world_stream_set_cell_flag(void *stream,
                                          rt_string cell,
                                          rt_string key,
                                          int64_t value);
int64_t rt_game3d_world_stream_get_cell_flag(void *stream, rt_string cell, rt_string key);
int64_t rt_game3d_world_stream_loaded_event_count(void *stream);
rt_string rt_game3d_world_stream_loaded_event(void *stream, int64_t index);
void rt_game3d_world_stream_clear_loaded_events(void *stream);
/// @brief Validate a VW3DSAV1 buffer without applying it (fuzz/test surface).
int8_t rt_game3d_persistence_validate(const void *data, int64_t size);

/* Minimap3D — authored-map minimap, markers, compass, indicators (plan 28). */
void *rt_game3d_minimap_new(void *world, int64_t size_px);
void rt_game3d_minimap_set_map_image(
    void *minimap, void *pixels, double min_x, double min_z, double max_x, double max_z);
void rt_game3d_minimap_set_tracked_entity(void *minimap, void *entity);
void rt_game3d_minimap_set_viewport(void *minimap, double x, double y, double w, double h);
void rt_game3d_minimap_set_compass(void *minimap, int8_t enabled, double width_px);
int64_t rt_game3d_minimap_add_marker(void *minimap, void *entity, void *icon, int64_t color);
int64_t rt_game3d_minimap_add_marker_at(void *minimap, void *point, void *icon, int64_t color);
void rt_game3d_minimap_remove_marker(void *minimap, int64_t id);
void rt_game3d_minimap_set_marker_edge_clamp(void *minimap, int64_t id, int8_t clamp);
void rt_game3d_minimap_set_marker_scale(void *minimap, int64_t id, double scale);
void rt_game3d_minimap_set_marker_on_compass(void *minimap, int64_t id, int8_t enabled);
void rt_game3d_minimap_set_objective_indicator(void *minimap, int64_t id, int8_t enabled);
int64_t rt_game3d_minimap_get_marker_count(void *minimap);
double rt_game3d_minimap_map_x(void *minimap, double world_x, double world_z);
double rt_game3d_minimap_map_y(void *minimap, double world_x, double world_z);
void rt_game3d_minimap_draw(void *minimap);

/* Profiling depth — hitch tracer + pass/hitch constants (plan 30). */
void rt_game3d_world_set_hitch_threshold(void *world, double ms);
int64_t rt_game3d_world_hitch_count(void *world);
int64_t rt_game3d_world_hitch_frame(void *world, int64_t index);
int64_t rt_game3d_world_hitch_source(void *world, int64_t index);
double rt_game3d_world_hitch_ms(void *world, int64_t index);
void rt_game3d_world_clear_hitches(void *world);
int64_t rt_game3d_renderpass_shadow(void);
int64_t rt_game3d_renderpass_opaque(void);
int64_t rt_game3d_renderpass_transparent(void);
int64_t rt_game3d_renderpass_postfx(void);
int64_t rt_game3d_renderpass_overlay(void);
int64_t rt_game3d_renderpass_present(void);
int64_t rt_game3d_hitchsource_stream_commit(void);
int64_t rt_game3d_hitchsource_frame_total(void);

/// @brief Toggle worker-backed streaming (default on); off restores blocking inline loads.
void rt_game3d_world_stream_set_async_streaming(void *stream, int8_t enabled);

/// @brief True when worker-backed streaming is enabled for this stream.
int8_t rt_game3d_world_stream_get_async_streaming(void *stream);

/// @brief Cap staged bytes committed per update (-1 = unlimited, 0 = hold commits).
void rt_game3d_world_stream_set_commit_budget(void *stream, int64_t bytes);

/// @brief Seconds of smoothed center velocity to prefetch along (0 disables prefetch).
void rt_game3d_world_stream_set_prefetch_lookahead(void *stream, double seconds);

/// @brief Worst single staged-commit slice in wall milliseconds since mount.
double rt_game3d_world_stream_get_stream_stall_ms(void *stream);

/// @brief Cells currently staged or staging purely from velocity prefetch.
int64_t rt_game3d_world_stream_get_prefetched_cell_count(void *stream);

/// @brief Set the HLOD proxy ring radius (<= 0 restores the 4x-load-radius default).
void rt_game3d_world_stream_set_proxy_radius(void *stream, double radius);

/// @brief The cell's manifest/baked proxy path ("" when the cell has no proxy).
rt_string rt_game3d_world_stream_get_cell_proxy(void *stream, int64_t index);

/// @brief Number of cells currently holding only their HLOD proxy subtree.
int64_t rt_game3d_world_stream_get_proxy_resident_count(void *stream);

/// @brief Measured bytes of resident proxy subtrees (also included in ResidentBytes).
int64_t rt_game3d_world_stream_get_proxy_resident_bytes(void *stream);

/// @brief Bake a merged low-poly proxy .vscn for a resident cell (authoring hook).
int8_t rt_game3d_world_stream_bake_cell_proxy(void *stream, int64_t index);

/// @brief Generate yaw-strip impostors for cells holding proxies; returns count.
int64_t rt_game3d_world_stream_generate_impostors(void *stream, double distance);

#ifdef __cplusplus
}
#endif
