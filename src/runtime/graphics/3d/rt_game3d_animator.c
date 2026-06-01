//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/rt_game3d_animator.c
// Purpose: Animator3D wrapper for the Viper.Game3D layer — drives an animation
//   controller and surfaces per-update animation events. Split out of rt_game3d.c;
//   shares private types/helpers via rt_game3d_internal.h.
// Links: rt_game3d_internal.h, rt_animcontroller3d.h
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

/// @brief Release all buffered animation-event name strings and reset the count to 0.
static void game3d_animator_clear_events(rt_game3d_animator *animator) {
    if (!animator)
        return;
    for (int32_t i = 0; i < animator->event_count; ++i)
        game3d_release_ref((void **)&animator->events[i]);
    animator->event_count = 0;
}

/// @brief Pull pending controller events into the animator's buffer until the controller
///   reports none or RT_GAME3D_ANIM_EVENT_MAX is reached.
static void game3d_animator_drain_events(rt_game3d_animator *animator) {
    if (!animator || !animator->controller)
        return;
    while (animator->event_count < RT_GAME3D_ANIM_EVENT_MAX) {
        rt_string event_name = rt_anim_controller3d_poll_event(animator->controller);
        const char *name = event_name ? rt_string_cstr(event_name) : "";
        if (!name || name[0] == '\0') {
            game3d_release_ref((void **)&event_name);
            break;
        }
        game3d_assign_ref((void **)&animator->events[animator->event_count++], event_name);
        game3d_release_ref((void **)&event_name);
    }
}

/// @brief GC finalizer for an animator: drop buffered events and release the controller.
static void game3d_animator_finalize(void *obj) {
    rt_game3d_animator *animator = (rt_game3d_animator *)obj;
    if (!animator)
        return;
    game3d_animator_clear_events(animator);
    game3d_release_ref(&animator->controller);
}

/// @brief Allocate an animator wrapping an AnimController3D; traps if `controller` is
///   the wrong class or on OOM.
void *rt_game3d_animator_new(void *controller) {
    rt_game3d_animator *animator;
    if (!rt_g3d_has_class(controller, RT_G3D_ANIMCONTROLLER3D_CLASS_ID)) {
        rt_trap("Game3D.Animator3D.New: controller must be AnimController3D");
        return NULL;
    }
    animator = (rt_game3d_animator *)rt_obj_new_i64(RT_G3D_GAME3D_ANIMATOR3D_CLASS_ID,
                                                    (int64_t)sizeof(*animator));
    if (!animator) {
        rt_trap("Game3D.Animator3D.New: allocation failed");
        return NULL;
    }
    memset(animator, 0, sizeof(*animator));
    rt_obj_set_finalizer(animator, game3d_animator_finalize);
    game3d_assign_ref(&animator->controller, controller);
    return animator;
}

/// @brief Get the AnimController3D backing the animator (NULL if invalid).
void *rt_game3d_animator_get_controller(void *obj) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.get_controller: invalid animator");
    return animator ? animator->controller : NULL;
}

/// @brief Play `name` immediately, refreshing the event buffer on success; returns 0
///   if the animator is invalid or the clip is unknown.
int8_t rt_game3d_animator_play(void *obj, rt_string name) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.play: invalid animator");
    int8_t ok;
    if (!animator || !animator->controller)
        return 0;
    game3d_animator_clear_events(animator);
    ok = rt_anim_controller3d_play(animator->controller, name);
    if (ok)
        game3d_animator_drain_events(animator);
    return ok;
}

/// @brief Cross-fade to `name` over `seconds`, refreshing the event buffer on success;
///   returns 0 if the animator is invalid or the clip is unknown.
int8_t rt_game3d_animator_crossfade(void *obj, rt_string name, double seconds) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.crossfade: invalid animator");
    int8_t ok;
    if (!animator || !animator->controller)
        return 0;
    game3d_animator_clear_events(animator);
    ok = rt_anim_controller3d_crossfade(animator->controller, name, seconds);
    if (ok)
        game3d_animator_drain_events(animator);
    return ok;
}

/// @brief Play `name` as a true additive overlay on `layer`, refreshing events on success.
int8_t rt_game3d_animator_play_layer_additive(void *obj, int64_t layer, rt_string name) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.playLayerAdditive: invalid animator");
    int8_t ok;
    if (!animator || !animator->controller)
        return 0;
    game3d_animator_clear_events(animator);
    ok = rt_anim_controller3d_play_layer_additive(animator->controller, layer, name);
    if (ok)
        game3d_animator_drain_events(animator);
    return ok;
}

/// @brief Cross-fade `name` as a true additive overlay on `layer`, refreshing events on success.
int8_t rt_game3d_animator_crossfade_layer_additive(void *obj,
                                                   int64_t layer,
                                                   rt_string name,
                                                   double seconds) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.crossfadeLayerAdditive: invalid animator");
    int8_t ok;
    if (!animator || !animator->controller)
        return 0;
    game3d_animator_clear_events(animator);
    ok = rt_anim_controller3d_crossfade_layer_additive(animator->controller, layer, name, seconds);
    if (ok)
        game3d_animator_drain_events(animator);
    return ok;
}

/// @brief Set a BlendTree3D as the wrapped controller's base pose source.
int8_t rt_game3d_animator_set_blend_tree(void *obj, void *blend_tree) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.setBlendTree: invalid animator");
    if (!animator || !animator->controller)
        return 0;
    return rt_anim_controller3d_set_blend_tree(animator->controller, blend_tree);
}

/// @brief Set an IKSolver3D as the wrapped controller's final-pose constraint.
int8_t rt_game3d_animator_set_ik_solver(void *obj, void *ik_solver) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.setIKSolver: invalid animator");
    if (!animator || !animator->controller)
        return 0;
    return rt_anim_controller3d_set_ik_solver(animator->controller, ik_solver);
}

/// @brief Set the playback speed multiplier for the named state/clip.
void rt_game3d_animator_set_speed(void *obj, rt_string name, double speed) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.setSpeed: invalid animator");
    if (animator && animator->controller)
        rt_anim_controller3d_set_state_speed(animator->controller, name, speed);
}

/// @brief True if `name` is the active state (0 if invalid).
int8_t rt_game3d_animator_is_playing(void *obj, rt_string name) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.isPlaying: invalid animator");
    return animator && animator->controller
               ? rt_anim_controller3d_is_state_playing(animator->controller, name)
               : 0;
}

/// @brief Get the current state's elapsed time in seconds (0 if invalid).
double rt_game3d_animator_state_time(void *obj) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.stateTime: invalid animator");
    return animator && animator->controller
               ? rt_anim_controller3d_get_state_time(animator->controller)
               : 0.0;
}

/// @brief Count animation events buffered from the most recent play/update.
int64_t rt_game3d_animator_event_count(void *obj) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.eventCount: invalid animator");
    return animator ? animator->event_count : 0;
}

/// @brief Get the i-th buffered event name, or "" if out of range.
rt_string rt_game3d_animator_event_name(void *obj, int64_t index) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.eventName: invalid animator");
    if (!animator || index < 0 || index >= animator->event_count)
        return rt_const_cstr("");
    return animator->events[index] ? animator->events[index] : rt_const_cstr("");
}

/// @brief Advance the animator by `dt` seconds (clamped to ≥0), sampling poses and
///   refreshing the event buffer.
void rt_game3d_animator_update(void *obj, double dt) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.update: invalid animator");
    if (!animator || !animator->controller)
        return;
    if (!isfinite(dt) || dt < 0.0)
        dt = 0.0;
    game3d_animator_clear_events(animator);
    rt_anim_controller3d_update(animator->controller, dt);
    game3d_animator_drain_events(animator);
}

/// @brief Fluent: attach an animator to the entity and return it.
/// @details Accepts either an Animator3D or an AnimController3D (wrapped into a new
///   Animator3D here), binds the controller to the entity's node, and clears the
///   binding when passed NULL. Traps on any other class.
void *rt_game3d_entity_attach_animator(void *obj, void *animator_or_controller) {
    rt_game3d_entity *entity =
        game3d_entity_checked(obj, "Game3D.Entity3D.attachAnimator: invalid entity");
    void *animator = animator_or_controller;
    void *created_animator = NULL;
    if (animator_or_controller &&
        rt_g3d_has_class(animator_or_controller, RT_G3D_ANIMCONTROLLER3D_CLASS_ID)) {
        created_animator = rt_game3d_animator_new(animator_or_controller);
        animator = created_animator;
    } else if (animator_or_controller &&
               !rt_g3d_has_class(animator_or_controller, RT_G3D_GAME3D_ANIMATOR3D_CLASS_ID)) {
        rt_trap("Game3D.Entity3D.attachAnimator: expected Animator3D or AnimController3D");
        return obj;
    }
    if (entity) {
        game3d_assign_ref(&entity->anim, animator);
        if (entity->node) {
            if (animator) {
                rt_game3d_animator *game_animator = game3d_animator_checked(
                    animator, "Game3D.Entity3D.attachAnimator: invalid animator");
                rt_scene_node3d_bind_animator(entity->node,
                                              game_animator ? game_animator->controller : NULL);
            } else {
                rt_scene_node3d_clear_animator_binding(entity->node);
            }
        }
    }
    game3d_release_ref(&created_animator);
    return obj;
}
