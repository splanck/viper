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

/// @brief Return the wrapped AnimController3D only when the private slot is still valid.
static void *game3d_animator_controller_ref(const rt_game3d_animator *animator) {
    return animator ? rt_g3d_checked_or_null(animator->controller,
                                             RT_G3D_ANIMCONTROLLER3D_CLASS_ID)
                    : NULL;
}

/// @brief Return the wrapped NodeAnimator3D only when the private slot is still valid.
static void *game3d_animator_node_animator_ref(const rt_game3d_animator *animator) {
    return animator ? rt_g3d_checked_or_null(animator->node_animator,
                                             RT_G3D_NODEANIMATOR3D_CLASS_ID)
                    : NULL;
}

/// @brief Clamp the private event count to the public fixed-buffer range.
static int32_t game3d_animator_event_count_clamped(const rt_game3d_animator *animator) {
    if (!animator || animator->event_count < 0)
        return 0;
    return animator->event_count > RT_GAME3D_ANIM_EVENT_MAX ? RT_GAME3D_ANIM_EVENT_MAX
                                                            : animator->event_count;
}

/// @brief Release an owned string event slot without touching wrong-class private corruption.
static void game3d_animator_release_event_slot(rt_string *slot) {
    if (!slot || !*slot)
        return;
    rt_string event_name = *slot;
    *slot = NULL;
    if (rt_string_is_handle(event_name))
        rt_string_unref(event_name);
}

/// @brief Drop non-string private event slots and compact the visible event buffer.
static int32_t game3d_animator_repair_event_buffer(rt_game3d_animator *animator) {
    if (!animator)
        return 0;
    int32_t event_count = game3d_animator_event_count_clamped(animator);
    int32_t write = 0;
    for (int32_t i = 0; i < event_count; ++i) {
        rt_string event_name = animator->events[i];
        if (!rt_string_is_handle(event_name)) {
            animator->events[i] = NULL;
            continue;
        }
        if (write != i) {
            animator->events[write] = event_name;
            animator->events[i] = NULL;
        }
        ++write;
    }
    for (int32_t i = event_count; i < RT_GAME3D_ANIM_EVENT_MAX; ++i) {
        if (animator->events[i] && !rt_string_is_handle(animator->events[i]))
            animator->events[i] = NULL;
    }
    animator->event_count = write;
    return write;
}

/// @brief Release all buffered animation-event name strings and reset the count to 0.
static void game3d_animator_clear_events(rt_game3d_animator *animator) {
    if (!animator)
        return;
    for (int32_t i = 0; i < RT_GAME3D_ANIM_EVENT_MAX; ++i)
        game3d_animator_release_event_slot(&animator->events[i]);
    animator->event_count = 0;
}

/// @brief Pull pending controller events into the animator's buffer until the controller
///   reports none or RT_GAME3D_ANIM_EVENT_MAX is reached.
static void game3d_animator_drain_events(rt_game3d_animator *animator) {
    void *controller = game3d_animator_controller_ref(animator);
    if (!controller)
        return;
    game3d_animator_repair_event_buffer(animator);
    while (animator->event_count < RT_GAME3D_ANIM_EVENT_MAX) {
        rt_string event_name = rt_anim_controller3d_poll_event(controller);
        if (event_name && !rt_string_is_handle(event_name))
            break;
        const char *name = event_name ? rt_string_cstr(event_name) : "";
        if (!name || name[0] == '\0') {
            game3d_animator_release_event_slot(&event_name);
            break;
        }
        game3d_assign_ref((void **)&animator->events[animator->event_count++], event_name);
        game3d_animator_release_event_slot(&event_name);
    }
    for (int32_t discarded = 0; discarded <= RT_GAME3D_ANIM_EVENT_MAX; ++discarded) {
        rt_string overflow_name = rt_anim_controller3d_poll_event(controller);
        if (overflow_name && !rt_string_is_handle(overflow_name))
            break;
        const char *name = overflow_name ? rt_string_cstr(overflow_name) : "";
        int had_name = name && name[0] != '\0';
        game3d_animator_release_event_slot(&overflow_name);
        if (!had_name)
            break;
    }
}

/// @brief GC finalizer for an animator: drop buffered events and release the controller.
static void game3d_animator_finalize(void *obj) {
    rt_game3d_animator *animator = (rt_game3d_animator *)obj;
    if (!animator)
        return;
    game3d_animator_clear_events(animator);
    game3d_release_typed_ref(&animator->controller, RT_G3D_ANIMCONTROLLER3D_CLASS_ID);
    game3d_release_typed_ref(&animator->node_animator, RT_G3D_NODEANIMATOR3D_CLASS_ID);
}

/// @brief Allocate an animator wrapping optional skeletal and node animation drivers.
void *rt_game3d_animator_new_from_bindings(void *controller, void *node_animator) {
    rt_game3d_animator *animator;
    if (controller && !rt_g3d_has_class(controller, RT_G3D_ANIMCONTROLLER3D_CLASS_ID)) {
        rt_trap("Game3D.Animator3D.New: controller must be AnimController3D");
        return NULL;
    }
    if (node_animator && !rt_g3d_has_class(node_animator, RT_G3D_NODEANIMATOR3D_CLASS_ID)) {
        rt_trap("Game3D.Animator3D.New: node animator must be NodeAnimator3D");
        return NULL;
    }
    if (!controller && !node_animator) {
        rt_trap("Game3D.Animator3D.New: expected AnimController3D or NodeAnimator3D");
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
    game3d_assign_typed_ref(&animator->controller, controller, RT_G3D_ANIMCONTROLLER3D_CLASS_ID);
    game3d_assign_typed_ref(&animator->node_animator,
                            node_animator,
                            RT_G3D_NODEANIMATOR3D_CLASS_ID);
    return animator;
}

/// @brief Allocate an animator wrapping either an AnimController3D or a NodeAnimator3D.
void *rt_game3d_animator_new(void *controller_or_node_animator) {
    if (rt_g3d_has_class(controller_or_node_animator, RT_G3D_ANIMCONTROLLER3D_CLASS_ID))
        return rt_game3d_animator_new_from_bindings(controller_or_node_animator, NULL);
    if (rt_g3d_has_class(controller_or_node_animator, RT_G3D_NODEANIMATOR3D_CLASS_ID))
        return rt_game3d_animator_new_from_bindings(NULL, controller_or_node_animator);
    rt_trap("Game3D.Animator3D.New: expected AnimController3D or NodeAnimator3D");
    return NULL;
}

/// @brief Get the AnimController3D backing the animator (NULL if invalid).
void *rt_game3d_animator_get_controller(void *obj) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.get_controller: invalid animator");
    return game3d_animator_controller_ref(animator);
}

/// @brief Get the NodeAnimator3D backing this wrapper, or NULL for skeletal-only animators.
void *rt_game3d_animator_get_node_animator(void *obj) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.get_nodeAnimator: invalid animator");
    return game3d_animator_node_animator_ref(animator);
}

/// @brief Whether this wrapper needs Game3D's skeletal animation update pass.
int8_t rt_game3d_animator_needs_game_update(void *obj) {
    rt_game3d_animator *animator =
        (rt_game3d_animator *)rt_g3d_checked_or_null(obj, RT_G3D_GAME3D_ANIMATOR3D_CLASS_ID);
    return game3d_animator_controller_ref(animator) ? 1 : 0;
}

/// @brief Play `name` immediately, refreshing the event buffer on success; returns 0
///   if the animator is invalid or the clip is unknown.
int8_t rt_game3d_animator_play(void *obj, rt_string name) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.play: invalid animator");
    void *controller = game3d_animator_controller_ref(animator);
    void *node_animator = game3d_animator_node_animator_ref(animator);
    int8_t ok;
    if (!controller && !node_animator) {
        game3d_animator_clear_events(animator);
        return 0;
    }
    game3d_animator_clear_events(animator);
    ok = controller ? rt_anim_controller3d_play(controller, name) : 0;
    if (node_animator)
        ok = (int8_t)(rt_node_animator3d_play(node_animator, name) || ok);
    if (ok && controller)
        game3d_animator_drain_events(animator);
    return ok;
}

/// @brief Cross-fade to `name` over `seconds`, refreshing the event buffer on success;
///   returns 0 if the animator is invalid or the clip is unknown.
int8_t rt_game3d_animator_crossfade(void *obj, rt_string name, double seconds) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.crossfade: invalid animator");
    void *controller = game3d_animator_controller_ref(animator);
    void *node_animator = game3d_animator_node_animator_ref(animator);
    int8_t ok;
    if (!controller && !node_animator) {
        game3d_animator_clear_events(animator);
        return 0;
    }
    game3d_animator_clear_events(animator);
    seconds = game3d_nonnegative_clamped_or(seconds, 0.0, RT_GAME3D_ANIM_BLEND_TIME_MAX);
    ok = controller ? rt_anim_controller3d_crossfade(controller, name, seconds) : 0;
    if (node_animator)
        ok = (int8_t)(rt_node_animator3d_play(node_animator, name) || ok);
    if (ok && controller)
        game3d_animator_drain_events(animator);
    return ok;
}

/// @brief Play `name` as a true additive overlay on `layer`, refreshing events on success.
int8_t rt_game3d_animator_play_layer_additive(void *obj, int64_t layer, rt_string name) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.playLayerAdditive: invalid animator");
    void *controller = game3d_animator_controller_ref(animator);
    int8_t ok;
    if (!controller) {
        game3d_animator_clear_events(animator);
        return 0;
    }
    game3d_animator_clear_events(animator);
    ok = rt_anim_controller3d_play_layer_additive(controller, layer, name);
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
    void *controller = game3d_animator_controller_ref(animator);
    int8_t ok;
    if (!controller) {
        game3d_animator_clear_events(animator);
        return 0;
    }
    game3d_animator_clear_events(animator);
    seconds = game3d_nonnegative_clamped_or(seconds, 0.0, RT_GAME3D_ANIM_BLEND_TIME_MAX);
    ok = rt_anim_controller3d_crossfade_layer_additive(controller, layer, name, seconds);
    if (ok)
        game3d_animator_drain_events(animator);
    return ok;
}

/// @brief Set a BlendTree3D as the wrapped controller's base pose source.
int8_t rt_game3d_animator_set_blend_tree(void *obj, void *blend_tree) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.setBlendTree: invalid animator");
    void *controller = game3d_animator_controller_ref(animator);
    if (!controller)
        return 0;
    return rt_anim_controller3d_set_blend_tree(controller, blend_tree);
}

/// @brief Set an IKSolver3D as the wrapped controller's final-pose constraint.
int8_t rt_game3d_animator_set_ik_solver(void *obj, void *ik_solver) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.setIKSolver: invalid animator");
    void *controller = game3d_animator_controller_ref(animator);
    if (!controller)
        return 0;
    return rt_anim_controller3d_set_ik_solver(controller, ik_solver);
}

/// @brief Set the playback speed multiplier for the named state/clip.
void rt_game3d_animator_set_speed(void *obj, rt_string name, double speed) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.setSpeed: invalid animator");
    void *controller = game3d_animator_controller_ref(animator);
    void *node_animator = game3d_animator_node_animator_ref(animator);
    double clamped = game3d_clamp_abs_or(speed, 1.0, RT_GAME3D_ANIM_SPEED_ABS_MAX);
    if (controller)
        rt_anim_controller3d_set_state_speed(controller,
                                             name,
                                             clamped);
    if (node_animator)
        rt_node_animator3d_set_speed(node_animator, clamped);
}

/// @brief True if `name` is the active state (0 if invalid).
int8_t rt_game3d_animator_is_playing(void *obj, rt_string name) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.isPlaying: invalid animator");
    void *controller = game3d_animator_controller_ref(animator);
    void *node_animator = game3d_animator_node_animator_ref(animator);
    if (controller && rt_anim_controller3d_is_state_playing(controller, name))
        return 1;
    if (node_animator && rt_node_animator3d_get_playing(node_animator)) {
        rt_string current = rt_node_animator3d_get_current_clip(node_animator);
        const char *current_name = current ? rt_string_cstr(current) : NULL;
        const char *target_name = name ? rt_string_cstr(name) : NULL;
        if (current_name && target_name && strcmp(current_name, target_name) == 0)
            return 1;
    }
    return 0;
}

/// @brief Get the current state's elapsed time in seconds (0 if invalid).
double rt_game3d_animator_state_time(void *obj) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.stateTime: invalid animator");
    void *controller = game3d_animator_controller_ref(animator);
    void *node_animator = game3d_animator_node_animator_ref(animator);
    if (!controller && node_animator)
        return rt_node_animator3d_get_time(node_animator);
    return controller
               ? game3d_nonnegative_clamped_or(
                     rt_anim_controller3d_get_state_time(controller),
                     0.0,
                     RT_GAME3D_ANIM_BLEND_TIME_MAX)
               : 0.0;
}

/// @brief Count animation events buffered from the most recent play/update.
int64_t rt_game3d_animator_event_count(void *obj) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.eventCount: invalid animator");
    return game3d_animator_repair_event_buffer(animator);
}

/// @brief Get the i-th buffered event name, or "" if out of range.
rt_string rt_game3d_animator_event_name(void *obj, int64_t index) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.eventName: invalid animator");
    int32_t event_count = game3d_animator_repair_event_buffer(animator);
    if (!animator || index < 0 || index >= event_count)
        return rt_const_cstr("");
    return rt_string_is_handle(animator->events[index]) ? animator->events[index]
                                                        : rt_const_cstr("");
}

/// @brief Advance the animator by `dt` seconds (clamped to ≥0), sampling poses and
///   refreshing the event buffer.
void rt_game3d_animator_update(void *obj, double dt) {
    rt_game3d_animator *animator =
        game3d_animator_checked(obj, "Game3D.Animator3D.update: invalid animator");
    void *controller = game3d_animator_controller_ref(animator);
    void *node_animator = game3d_animator_node_animator_ref(animator);
    if (!controller) {
        game3d_animator_clear_events(animator);
        if (node_animator) {
            /*
             * Bound NodeAnimator3D instances are advanced by Scene3D.SyncBindings so world
             * updates do not double-step imported object/morph/camera animations. Calling
             * update manually before binding remains a no-op because NodeAnimator3D has no root.
             */
            rt_node_animator3d_update(node_animator, dt);
        }
        return;
    }
    if (!isfinite(dt) || dt < 0.0)
        dt = 0.0;
    if (dt > RT_GAME3D_ANIM_STEP_MAX)
        dt = RT_GAME3D_ANIM_STEP_MAX;
    game3d_animator_clear_events(animator);
    rt_anim_controller3d_update(controller, dt);
    game3d_animator_drain_events(animator);
}

/// @brief Fluent: attach an animator to the entity and return it.
/// @details Accepts either an Animator3D, an AnimController3D, or a NodeAnimator3D. Raw
///   controller/node-animator handles are wrapped into a Game3D.Animator3D here. The skeletal
///   controller binding and node-animator binding are applied independently so imported models
///   can keep both Animation3D and NodeAnimation3D playback attached to one entity.
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
               rt_g3d_has_class(animator_or_controller, RT_G3D_NODEANIMATOR3D_CLASS_ID)) {
        created_animator = rt_game3d_animator_new(animator_or_controller);
        animator = created_animator;
    } else if (animator_or_controller &&
               !rt_g3d_has_class(animator_or_controller, RT_G3D_GAME3D_ANIMATOR3D_CLASS_ID)) {
        rt_trap(
            "Game3D.Entity3D.attachAnimator: expected Animator3D, AnimController3D, or "
            "NodeAnimator3D");
        return obj;
    }
    if (entity) {
        game3d_assign_typed_ref(&entity->anim, animator, RT_G3D_GAME3D_ANIMATOR3D_CLASS_ID);
        void *node = game3d_entity_node_ref(entity);
        if (node) {
            if (animator) {
                rt_game3d_animator *game_animator = game3d_animator_checked(
                    animator, "Game3D.Entity3D.attachAnimator: invalid animator");
                rt_scene_node3d_bind_animator(node,
                                              game3d_animator_controller_ref(game_animator));
                rt_scene_node3d_set_animator_scene_update(node, 0);
                rt_scene_node3d_bind_node_animator(
                    node, game3d_animator_node_animator_ref(game_animator));
            } else {
                rt_scene_node3d_clear_animator_binding(node);
            }
        }
    }
    game3d_release_ref(&created_animator);
    return obj;
}
