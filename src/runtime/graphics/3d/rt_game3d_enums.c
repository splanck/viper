//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/rt_game3d_enums.c
// Purpose: Enum-mirror accessors for the Viper.Game3D layer — each returns the
//   matching RT_GAME3D_* / input constant so frontends can bind them as
//   read-only Layers / BodyShape / SyncMode / AlphaMode / ShadingModel /
//   Quality / CollisionPhase / Keys / MouseButtons properties. Split out of
//   rt_game3d.c (which retains the world/entity/asset/simulation surface).
// Key invariants:
//   - Pure constant/forwarding accessors: stateless, no allocation, no failure.
// Ownership/Lifetime:
//   - None — every function is stateless and returns a scalar constant.
// Links: rt_game3d.h (declarations + RT_GAME3D_* constants), rt_input.h
//
//===----------------------------------------------------------------------===//

#include "rt_game3d.h"

#include "rt_input.h"

//=========================================================================
// Enum-mirror accessors — each returns the matching RT_GAME3D_* / input
// constant so frontends can bind them as read-only Layers/BodyShape/SyncMode/
// AlphaMode/ShadingModel/Quality/CollisionPhase/Keys/MouseButtons properties.
// Per-function semantics are documented on the declarations in rt_game3d.h.
//=========================================================================

int64_t rt_game3d_layers_world(void) {
    return RT_GAME3D_LAYER_WORLD;
}

int64_t rt_game3d_layers_dynamic(void) {
    return RT_GAME3D_LAYER_DYNAMIC;
}

int64_t rt_game3d_layers_player(void) {
    return RT_GAME3D_LAYER_PLAYER;
}

int64_t rt_game3d_layers_trigger(void) {
    return RT_GAME3D_LAYER_TRIGGER;
}

int64_t rt_game3d_layers_debris(void) {
    return RT_GAME3D_LAYER_DEBRIS;
}

int64_t rt_game3d_body_shape_box(void) {
    return RT_GAME3D_BODY_SHAPE_BOX;
}

int64_t rt_game3d_body_shape_sphere(void) {
    return RT_GAME3D_BODY_SHAPE_SPHERE;
}

int64_t rt_game3d_body_shape_capsule(void) {
    return RT_GAME3D_BODY_SHAPE_CAPSULE;
}

int64_t rt_game3d_sync_mode_node_from_body(void) {
    return RT_GAME3D_SYNC_NODE_FROM_BODY;
}

int64_t rt_game3d_sync_mode_body_from_node(void) {
    return RT_GAME3D_SYNC_BODY_FROM_NODE;
}

int64_t rt_game3d_sync_mode_node_from_anim_root_motion(void) {
    return RT_GAME3D_SYNC_NODE_FROM_ANIM_ROOT_MOTION;
}

int64_t rt_game3d_sync_mode_two_way_kinematic(void) {
    return RT_GAME3D_SYNC_TWO_WAY_KINEMATIC;
}

int64_t rt_game3d_alpha_mode_opaque(void) {
    return RT_GAME3D_ALPHA_OPAQUE;
}

int64_t rt_game3d_alpha_mode_mask(void) {
    return RT_GAME3D_ALPHA_MASK;
}

int64_t rt_game3d_alpha_mode_blend(void) {
    return RT_GAME3D_ALPHA_BLEND;
}

int64_t rt_game3d_shading_model_phong(void) {
    return RT_GAME3D_SHADING_PHONG;
}

int64_t rt_game3d_shading_model_toon(void) {
    return RT_GAME3D_SHADING_TOON;
}

int64_t rt_game3d_shading_model_pbr(void) {
    return RT_GAME3D_SHADING_PBR;
}

int64_t rt_game3d_shading_model_fresnel(void) {
    return RT_GAME3D_SHADING_FRESNEL;
}

int64_t rt_game3d_shading_model_emissive(void) {
    return RT_GAME3D_SHADING_EMISSIVE;
}

int64_t rt_game3d_shading_model_unlit(void) {
    return RT_GAME3D_SHADING_UNLIT;
}

int64_t rt_game3d_quality_performance(void) {
    return RT_GAME3D_QUALITY_PERFORMANCE;
}

int64_t rt_game3d_quality_balanced(void) {
    return RT_GAME3D_QUALITY_BALANCED;
}

int64_t rt_game3d_quality_cinematic(void) {
    return RT_GAME3D_QUALITY_CINEMATIC;
}

int64_t rt_game3d_collision_enter(void) {
    return RT_GAME3D_COLLISION_ENTER;
}

int64_t rt_game3d_collision_stay(void) {
    return RT_GAME3D_COLLISION_STAY;
}

int64_t rt_game3d_collision_exit(void) {
    return RT_GAME3D_COLLISION_EXIT;
}

int64_t rt_game3d_collision_any(void) {
    return RT_GAME3D_COLLISION_ANY;
}

int64_t rt_game3d_key_w(void) {
    return rt_keyboard_key_w();
}

int64_t rt_game3d_key_a(void) {
    return rt_keyboard_key_a();
}

int64_t rt_game3d_key_s(void) {
    return rt_keyboard_key_s();
}

int64_t rt_game3d_key_d(void) {
    return rt_keyboard_key_d();
}

int64_t rt_game3d_key_space(void) {
    return rt_keyboard_key_space();
}

int64_t rt_game3d_key_escape(void) {
    return rt_keyboard_key_escape();
}

int64_t rt_game3d_key_shift(void) {
    return rt_keyboard_key_shift();
}

int64_t rt_game3d_key_ctrl(void) {
    return rt_keyboard_key_ctrl();
}

int64_t rt_game3d_key_up(void) {
    return rt_keyboard_key_up();
}

int64_t rt_game3d_key_down(void) {
    return rt_keyboard_key_down();
}

int64_t rt_game3d_key_left(void) {
    return rt_keyboard_key_left();
}

int64_t rt_game3d_key_right(void) {
    return rt_keyboard_key_right();
}

int64_t rt_game3d_mouse_left(void) {
    return rt_mouse_button_left();
}

int64_t rt_game3d_mouse_right(void) {
    return rt_mouse_button_right();
}

int64_t rt_game3d_mouse_middle(void) {
    return rt_mouse_button_middle();
}

int64_t rt_game3d_mouse_x1(void) {
    return rt_mouse_button_x1();
}

int64_t rt_game3d_mouse_x2(void) {
    return rt_mouse_button_x2();
}
