//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/audio/rt_sound3d_objects.c
// Purpose: Object-backed 3D audio listener/source APIs layered on top of the
//   low-level Sound3D helpers and the existing 2D voice runtime. Backs
//   `Viper.Graphics3D.SoundListener3D` and `Viper.Graphics3D.SoundSource3D`.
//
// Key invariants:
//   - At most one SoundListener3D may be active at a time; activation pushes
//     the listener's state into rt_sound3d.c's active-listener slot.
//   - Listeners and sources are tracked in process-global doubly-linked lists
//     so SyncBindings can walk them every frame without external state.
//   - A bound camera or scene node overrides explicit position / forward;
//     unbinding restores caller-set values.
//
// Ownership/Lifetime:
//   - Listener and source objects are heap-allocated and GC-managed.
//   - bound_node / bound_camera / sound references are retained on assignment
//     and released on unbind / finalize.
//   - Active-listener handle is a weak pointer — clearing the active listener
//     simply removes it from the active slot without touching its refcount.
//
// Links: src/runtime/graphics/3d/audio/rt_soundlistener3d.h (SoundListener3D API),
//        src/runtime/graphics/3d/audio/rt_soundsource3d.h (SoundSource3D API),
//        src/runtime/graphics/3d/audio/rt_sound3d.h (low-level spatial helpers)
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_canvas3d.h"
#include "rt_graphics3d_ids.h"
#include "rt_mat4.h"
#include "rt_scene3d.h"
#include "rt_sound3d.h"
#include "rt_soundlistener3d.h"
#include "rt_soundsource3d.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern int8_t rt_obj_is_instance(void *obj, int64_t class_id, size_t min_size);
extern void rt_obj_retain_maybe(void *obj);
extern int rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);
extern void *rt_vec3_new(double x, double y, double z);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);
extern int64_t rt_sound_play_ex(void *sound, int64_t volume, int64_t pan);
extern int64_t rt_sound_play_loop(void *sound, int64_t volume, int64_t pan);
extern void rt_voice_stop(int64_t voice_id);
extern void rt_voice_set_volume(int64_t voice_id, int64_t volume);
extern void rt_voice_set_pan(int64_t voice_id, int64_t pan);
extern int64_t rt_voice_is_playing(int64_t voice_id);

typedef struct rt_soundlistener3d {
    void *vptr;
    rt_sound3d_listener_state state;
    void *bound_node;
    void *bound_camera;
    double last_sync_position[3];
    int8_t has_last_sync_position;
    int8_t is_active;
    struct rt_soundlistener3d *prev;
    struct rt_soundlistener3d *next;
} rt_soundlistener3d;

typedef struct rt_soundsource3d {
    void *vptr;
    void *sound;
    void *bound_node;
    double position[3];
    double velocity[3];
    double doppler_factor;
    double last_sync_position[3];
    int8_t has_last_sync_position;
    double ref_distance;
    double max_distance;
    int64_t volume;
    int64_t voice_id;
    int8_t looping;
    struct rt_soundsource3d *prev;
    struct rt_soundsource3d *next;
} rt_soundsource3d;

static rt_soundlistener3d *s_listener_head = NULL;
static rt_soundsource3d *s_source_head = NULL;
static rt_soundlistener3d *s_active_listener_obj = NULL;

/// @brief Checked cast of an opaque handle to SoundListener3D; NULL on class mismatch.
static rt_soundlistener3d *sound3d_listener_checked(void *obj) {
    if (!rt_obj_is_instance(obj, RT_G3D_SOUNDLISTENER3D_CLASS_ID, sizeof(rt_soundlistener3d)))
        return NULL;
    return (rt_soundlistener3d *)obj;
}

/// @brief Checked cast of an opaque handle to SoundSource3D; NULL on class mismatch.
static rt_soundsource3d *sound3d_source_checked(void *obj) {
    if (!rt_obj_is_instance(obj, RT_G3D_SOUNDSOURCE3D_CLASS_ID, sizeof(rt_soundsource3d)))
        return NULL;
    return (rt_soundsource3d *)obj;
}

/// @brief Drop a GC-managed reference stored in `**slot` and null the slot.
/// @details Idempotent — safe to call on already-null slots. Used by the
///   listener/source finalizers and by bind-site setters that need to
///   release the previous target before installing a new one.
static void sound3d_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

/// @brief Drop one reference and free if zero. Safe on NULL.
static void sound3d_release_local(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

/// @brief Return @p value when finite, else @p fallback.
static double sound3d_finite_or(double value, double fallback) {
    return isfinite(value) ? value : fallback;
}

/// @brief Translation-unit-local copy of `rt_sound3d.c::sound3d_copy3`.
/// @details Null-source-fills-zero convention applies: missing position
///   vectors collapse to the origin rather than leaving `dst` untouched.
static void sound3d_copy3(double *dst, const double *src) {
    if (!dst)
        return;
    if (!src) {
        dst[0] = 0.0;
        dst[1] = 0.0;
        dst[2] = 0.0;
        return;
    }
    dst[0] = sound3d_finite_or(src[0], 0.0);
    dst[1] = sound3d_finite_or(src[1], 0.0);
    dst[2] = sound3d_finite_or(src[2], 0.0);
}

/// @brief Translation-unit-local copy of `rt_sound3d.c::sound3d_vec_from_obj`.
/// @details Decodes an `rt_vec3` object through the accessor API; null
///   collapses to origin. Returns 0 only when a non-null object is not a Vec3.
static int sound3d_vec_from_obj(void *vec, double *out_xyz) {
    if (!out_xyz)
        return 0;
    if (!vec) {
        out_xyz[0] = 0.0;
        out_xyz[1] = 0.0;
        out_xyz[2] = 0.0;
        return 1;
    }
    if (!rt_g3d_is_vec3(vec))
        return 0;
    out_xyz[0] = sound3d_finite_or(rt_vec3_x(vec), 0.0);
    out_xyz[1] = sound3d_finite_or(rt_vec3_y(vec), 0.0);
    out_xyz[2] = sound3d_finite_or(rt_vec3_z(vec), 0.0);
    return 1;
}

/// @brief Compute velocity from position delta and update the last-position cache.
/// @details Skips velocity computation on the first call (no prior position to
///          differentiate against) or when `dt < 1e-8` (avoids divide-by-near-zero
///          producing huge spurious velocities). The last-position cache is
///          always updated so the *next* call has a baseline. Velocity is
///          intended to drive Doppler effects in the audio core.
static void sound3d_update_velocity(double *velocity,
                                    double *last_position,
                                    int8_t *has_last_position,
                                    const double *new_position,
                                    double dt) {
    if (!velocity || !last_position || !has_last_position || !new_position)
        return;
    if (*has_last_position && dt > 1e-8) {
        velocity[0] = (new_position[0] - last_position[0]) / dt;
        velocity[1] = (new_position[1] - last_position[1]) / dt;
        velocity[2] = (new_position[2] - last_position[2]) / dt;
    }
    last_position[0] = new_position[0];
    last_position[1] = new_position[1];
    last_position[2] = new_position[2];
    *has_last_position = 1;
}

/// @brief Clamp a requested volume to the runtime's 0-100 scale.
/// @details Zia / BASIC user code may pass arbitrary values (negative
///   sentinels or "1000 = maximum" style conventions from other
///   engines). This chokepoint normalizes everything to the actual
///   mixer range so downstream code can assume valid input.
static int64_t sound3d_clamp_volume(int64_t volume) {
    if (volume < 0)
        return 0;
    if (volume > 100)
        return 100;
    return volume;
}

/// @brief Push a listener onto the head of the global listener list.
/// @details The list is an intrusive doubly-linked list (prev/next fields
///   live on the listener struct itself) used by `sync_bindings` to walk
///   every live listener once per tick. Insertion at head is O(1) and
///   order doesn't matter since every node is visited uniformly.
static void sound3d_listener_list_add(rt_soundlistener3d *listener) {
    if (!listener)
        return;
    listener->prev = NULL;
    listener->next = s_listener_head;
    if (s_listener_head)
        s_listener_head->prev = listener;
    s_listener_head = listener;
}

/// @brief Splice a listener out of the global listener list.
/// @details Handles all three cases — head node (update `s_listener_head`),
///   middle node (relink neighbors' prev/next), tail node (just clear the
///   previous node's next). Both prev/next fields are zeroed on exit so the
///   listener can be re-added later without carrying stale pointers. Called
///   by the finalizer and by deactivation paths.
static void sound3d_listener_list_remove(rt_soundlistener3d *listener) {
    if (!listener)
        return;
    if (listener->prev)
        listener->prev->next = listener->next;
    else if (s_listener_head == listener)
        s_listener_head = listener->next;
    if (listener->next)
        listener->next->prev = listener->prev;
    listener->prev = NULL;
    listener->next = NULL;
}

/// @brief Push an audio source onto the head of the global source list.
/// @details Mirrors `sound3d_listener_list_add` — intrusive doubly-linked
///   list, O(1) insertion, iteration order immaterial because every live
///   source is visited uniformly during `sync_bindings`.
static void sound3d_source_list_add(rt_soundsource3d *source) {
    if (!source)
        return;
    source->prev = NULL;
    source->next = s_source_head;
    if (s_source_head)
        s_source_head->prev = source;
    s_source_head = source;
}

/// @brief Splice an audio source out of the global source list.
/// @details Symmetric to `sound3d_listener_list_remove`; clears both prev
///   and next on exit so the source can re-enter the list cleanly.
static void sound3d_source_list_remove(rt_soundsource3d *source) {
    if (!source)
        return;
    if (source->prev)
        source->prev->next = source->next;
    else if (s_source_head == source)
        s_source_head = source->next;
    if (source->next)
        source->next->prev = source->prev;
    source->prev = NULL;
    source->next = NULL;
}

/// @brief Resolve a SceneNode3D's world-space position by applying its world matrix to the origin.
/// @details Uses the node's cached world matrix when available (the standard
///          path — the scene graph has already composed the parent chain).
///          Falls back to the node's local position when no world matrix
///          is cached, which produces the wrong answer for parented nodes
///          but at least doesn't crash.
static void sound3d_get_node_world_position(void *node, double *out_position) {
    void *world_matrix;
    void *world_position;
    void *origin;
    if (!out_position)
        return;
    if (!node) {
        out_position[0] = 0.0;
        out_position[1] = 0.0;
        out_position[2] = 0.0;
        return;
    }
    world_matrix = rt_scene_node3d_get_world_matrix(node);
    if (!world_matrix) {
        void *local_position = rt_scene_node3d_get_position(node);
        sound3d_vec_from_obj(local_position, out_position);
        sound3d_release_local(local_position);
        return;
    }
    origin = rt_vec3_new(0.0, 0.0, 0.0);
    world_position = rt_mat4_transform_point(world_matrix, origin);
    sound3d_vec_from_obj(world_position, out_position);
    sound3d_release_local(origin);
    sound3d_release_local(world_position);
    sound3d_release_local(world_matrix);
}

/// @brief Resolve a SceneNode3D's world-space direction.
/// @details Computes direction as the world-transformed local vector minus the
///          world-transformed origin — the difference cancels the
///          translation component, leaving only the rotated direction.
///          Result is normalised; degenerate transforms fall back to the
///          supplied fallback. Two `rt_vec3_new` allocations
///          per call are accepted overhead since this is per-tick, not
///          per-frame-per-pixel.
static void sound3d_get_node_world_direction(void *node,
                                             const double *local_direction,
                                             const double *fallback,
                                             double *out_direction) {
    void *world_matrix;
    void *origin_vec;
    void *direction_vec;
    void *origin;
    void *direction;
    double origin_xyz[3];
    double direction_xyz[3];
    double len;
    if (!out_direction)
        return;
    if (!node) {
        sound3d_copy3(out_direction, fallback);
        return;
    }
    world_matrix = rt_scene_node3d_get_world_matrix(node);
    if (!world_matrix) {
        sound3d_copy3(out_direction, fallback);
        return;
    }
    origin_vec = rt_vec3_new(0.0, 0.0, 0.0);
    direction_vec = rt_vec3_new(local_direction[0], local_direction[1], local_direction[2]);
    origin = rt_mat4_transform_point(world_matrix, origin_vec);
    direction = rt_mat4_transform_point(world_matrix, direction_vec);
    sound3d_vec_from_obj(origin, origin_xyz);
    sound3d_vec_from_obj(direction, direction_xyz);
    sound3d_release_local(origin_vec);
    sound3d_release_local(direction_vec);
    sound3d_release_local(origin);
    sound3d_release_local(direction);
    sound3d_release_local(world_matrix);
    out_direction[0] = direction_xyz[0] - origin_xyz[0];
    out_direction[1] = direction_xyz[1] - origin_xyz[1];
    out_direction[2] = direction_xyz[2] - origin_xyz[2];
    len = sqrt(out_direction[0] * out_direction[0] + out_direction[1] * out_direction[1] +
               out_direction[2] * out_direction[2]);
    if (len <= 1e-8) {
        sound3d_copy3(out_direction, fallback);
        return;
    }
    out_direction[0] /= len;
    out_direction[1] /= len;
    out_direction[2] /= len;
}

static void sound3d_get_node_world_forward(void *node, double *out_forward) {
    static const double local_forward[3] = {0.0, 0.0, -1.0};
    static const double fallback_forward[3] = {0.0, 0.0, -1.0};
    sound3d_get_node_world_direction(node, local_forward, fallback_forward, out_forward);
}

static void sound3d_get_node_world_up(void *node, double *out_up) {
    static const double local_up[3] = {0.0, 1.0, 0.0};
    static const double fallback_up[3] = {0.0, 1.0, 0.0};
    sound3d_get_node_world_direction(node, local_up, fallback_up, out_up);
}

/// @brief Copy a listener's pose into the audio core's active-listener slot.
/// @details Only the one listener marked `is_active` contributes to spatial
///   mixing — other listeners update their own local state without pushing
///   it to the core, so there's no cross-talk between listeners tracked in
///   parallel (e.g. for split-screen or debug views). The audio core holds
///   a copy, so the listener's state can continue to change without
///   immediately perturbing in-flight voice params until the next sync.
static void sound3d_listener_push_active_state(rt_soundlistener3d *listener) {
    if (listener && listener->is_active)
        rt_sound3d_set_active_listener_state(&listener->state);
}

/// @brief Re-sync a listener's state from its bound camera or scene node.
/// @details Camera binding takes precedence over node binding when both are
///          set. Either source provides position + forward, after which
///          velocity is derived from the position delta over `dt` and the
///          listener-state struct is updated. If the listener is the
///          currently-active one, its state is also pushed into the audio
///          core so spatial mixing immediately reflects the new pose.
///          No-op when the listener has no binding at all (free-floating
///          listener whose state is set manually).
static void sound3d_listener_sync_binding(rt_soundlistener3d *listener, double dt) {
    double position[3];
    double forward[3];
    double up[3];
    if (!listener)
        return;

    if (listener->bound_camera) {
        void *camera_position = rt_camera3d_get_position(listener->bound_camera);
        void *camera_forward = rt_camera3d_get_forward(listener->bound_camera);
        sound3d_vec_from_obj(camera_position, position);
        sound3d_vec_from_obj(camera_forward, forward);
        sound3d_copy3(up, listener->state.up);
        sound3d_release_local(camera_position);
        sound3d_release_local(camera_forward);
        sound3d_update_velocity(listener->state.velocity,
                                listener->last_sync_position,
                                &listener->has_last_sync_position,
                                position,
                                dt);
        rt_sound3d_listener_state_set_pose(
            &listener->state, position, forward, up, listener->state.velocity);
        sound3d_listener_push_active_state(listener);
        return;
    }

    if (listener->bound_node) {
        sound3d_get_node_world_position(listener->bound_node, position);
        sound3d_get_node_world_forward(listener->bound_node, forward);
        sound3d_get_node_world_up(listener->bound_node, up);
        sound3d_update_velocity(listener->state.velocity,
                                listener->last_sync_position,
                                &listener->has_last_sync_position,
                                position,
                                dt);
        rt_sound3d_listener_state_set_pose(
            &listener->state, position, forward, up, listener->state.velocity);
        sound3d_listener_push_active_state(listener);
    }
}

/// @brief Re-sync the active listener's bound pose with `dt = 0`.
/// @details Called from the source-mutation path so spatial params are
///   computed against an up-to-date listener pose without ticking the
///   velocity calculation (which would introduce a spurious jump). Zero
///   dt means the velocity-derivation step is a no-op; only position and
///   forward get refreshed.
static void sound3d_refresh_active_listener(void) {
    if (s_active_listener_obj)
        sound3d_listener_sync_binding(s_active_listener_obj, 0.0);
}

/// @brief Reap a stale voice id when the underlying audio voice has finished.
/// @details Audio voices complete asynchronously (one-shot clips reach end,
///   mixer culls under-resourced voices, etc.) and the scripting layer has
///   no callback for that. Sources lazily check liveness here and zero
///   `voice_id` so the next Play() call grabs a fresh voice instead of
///   sending commands to a now-recycled id. Returns whether the source
///   currently has a live voice, so callers can fast-skip spatial updates
///   for silent sources.
static int8_t sound3d_source_refresh_play_state(rt_soundsource3d *source) {
    if (!source || source->voice_id <= 0)
        return 0;
    if (!rt_voice_is_playing(source->voice_id)) {
        source->voice_id = 0;
        return 0;
    }
    return 1;
}

/// @brief Recompute and push spatial volume + pan to a source's underlying voice.
/// @details Skips silently when the source has no live voice (refresh-play-state
///          will reap stale voice IDs as a side effect). Refreshes the active
///          listener first so the calculation uses an up-to-date pose, then
///          delegates to `rt_sound3d_compute_voice_params_ex` for the actual
///          attenuation + pan math, then pushes the results to the voice via
///          `rt_voice_set_volume` / `rt_voice_set_pan`. Called from every
///          source-mutating setter so changes take effect immediately rather
///          than at the next sync tick.
static void sound3d_source_apply_spatial(rt_soundsource3d *source) {
    rt_sound3d_listener_state listener;
    int64_t spatial_volume = 0;
    int64_t spatial_pan = 0;
    if (!source || !sound3d_source_refresh_play_state(source))
        return;
    sound3d_refresh_active_listener();
    rt_sound3d_get_effective_listener_state(&listener);
    rt_sound3d_compute_voice_params_ex(&listener,
                                       source->position,
                                       source->velocity,
                                       source->ref_distance,
                                       source->max_distance,
                                       sound3d_clamp_volume(source->volume),
                                       &spatial_volume,
                                       &spatial_pan,
                                       &source->doppler_factor);
    rt_voice_set_volume(source->voice_id, spatial_volume);
    rt_voice_set_pan(source->voice_id, spatial_pan);
}

/// @brief Refresh the cached Doppler factor even when the source is not playing.
static void sound3d_source_refresh_doppler(rt_soundsource3d *source) {
    rt_sound3d_listener_state listener;
    int64_t ignored_volume = 0;
    int64_t ignored_pan = 0;
    if (!source)
        return;
    sound3d_refresh_active_listener();
    rt_sound3d_get_effective_listener_state(&listener);
    rt_sound3d_compute_voice_params_ex(&listener,
                                       source->position,
                                       source->velocity,
                                       source->ref_distance,
                                       source->max_distance,
                                       sound3d_clamp_volume(source->volume),
                                       &ignored_volume,
                                       &ignored_pan,
                                       &source->doppler_factor);
}

/// @brief Re-sync a source's position from its bound scene node.
/// @details No-op when the source isn't bound to a node (free-floating
///          source whose position is set manually). After updating
///          position + velocity, applies spatial mixing so the next
///          mixer tick uses the new values.
static void sound3d_source_sync_binding(rt_soundsource3d *source, double dt) {
    double position[3];
    if (!source || !source->bound_node)
        return;
    sound3d_get_node_world_position(source->bound_node, position);
    sound3d_update_velocity(source->velocity,
                            source->last_sync_position,
                            &source->has_last_sync_position,
                            position,
                            dt);
    sound3d_copy3(source->position, position);
    sound3d_source_apply_spatial(source);
}

/// @brief GC finalizer for a 3D audio listener.
/// @details Three-step teardown: (1) if this listener was the active one,
///   clear the audio core's active-listener slot so in-flight voices
///   gracefully degrade to a null-listener pose rather than dereferencing
///   the freed struct; (2) unlink from the global listener list so
///   `sync_bindings` stops visiting it; (3) drop the scene-node and camera
///   back-references. The order (active-check first) is deliberate — the
///   core must be cleared before the listener memory is eligible for reuse.
static void sound3d_listener_finalize(void *obj) {
    rt_soundlistener3d *listener = (rt_soundlistener3d *)obj;
    if (!listener)
        return;
    if (s_active_listener_obj == listener) {
        s_active_listener_obj = NULL;
        rt_sound3d_clear_active_listener_state();
    }
    sound3d_listener_list_remove(listener);
    sound3d_release_ref(&listener->bound_node);
    sound3d_release_ref(&listener->bound_camera);
}

/// @brief GC finalizer for a 3D audio source.
/// @details Stops any live voice before releasing the source so the mixer
///   doesn't keep reading from a freed sound buffer one tick after the
///   source disappears. Then unlinks from the global source list (so
///   `sync_bindings` skips it), and drops references to the sound asset
///   and bound scene node. The sound's own refcount may still keep its
///   buffer alive if other sources share it — only this one source's
///   handle goes away.
static void sound3d_source_finalize(void *obj) {
    rt_soundsource3d *source = (rt_soundsource3d *)obj;
    if (!source)
        return;
    if (source->voice_id > 0)
        rt_voice_stop(source->voice_id);
    source->voice_id = 0;
    sound3d_source_list_remove(source);
    sound3d_release_ref(&source->sound);
    sound3d_release_ref(&source->bound_node);
}

/// @brief Per-frame tick: walk every live SoundListener3D and SoundSource3D, and re-resolve
/// each one's bound scene-node or camera into world position+forward, computing velocity from
/// the position delta over `dt`. Called by the game loop so spatial audio tracks moving entities
/// without per-source manual updates.
void rt_sound3d_sync_bindings(double dt) {
    rt_soundlistener3d *listener = s_listener_head;
    rt_soundsource3d *source = s_source_head;
    while (listener) {
        sound3d_listener_sync_binding(listener, dt);
        listener = listener->next;
    }
    while (source) {
        sound3d_source_sync_binding(source, dt);
        source = source->next;
    }
}

/// @brief Create a 3D audio listener (the "ears" of the scene). Initializes to identity
/// (origin, forward = -Z, zero velocity). The first listener constructed becomes the active
/// one automatically; subsequent ones must be activated with `_set_is_active`.
void *rt_soundlistener3d_new(void) {
    rt_soundlistener3d *listener = (rt_soundlistener3d *)rt_obj_new_i64(
        RT_G3D_SOUNDLISTENER3D_CLASS_ID, (int64_t)sizeof(rt_soundlistener3d));
    if (!listener)
        return NULL;
    memset(listener, 0, sizeof(*listener));
    rt_sound3d_listener_state_identity(&listener->state);
    sound3d_listener_list_add(listener);
    rt_obj_set_finalizer(listener, sound3d_listener_finalize);
    if (!s_active_listener_obj)
        rt_soundlistener3d_set_is_active(listener, 1);
    return listener;
}

/// @brief Read the listener's world-space position. If the listener is bound to a node/camera,
/// re-syncs the binding first to ensure the returned value reflects the current transform.
void *rt_soundlistener3d_get_position(void *obj) {
    rt_soundlistener3d *listener = sound3d_listener_checked(obj);
    if (!listener)
        return NULL;
    sound3d_listener_sync_binding(listener, 0.0);
    return rt_vec3_new(
        listener->state.position[0], listener->state.position[1], listener->state.position[2]);
}

/// @brief Manually set the listener world position. Resets the velocity tracker so the next
/// sync starts fresh (no spurious large-velocity blip from a teleport).
void rt_soundlistener3d_set_position(void *obj, void *position) {
    rt_soundlistener3d *listener = sound3d_listener_checked(obj);
    double pos[3];
    if (!listener)
        return;
    if (!sound3d_vec_from_obj(position, pos))
        return;
    sound3d_copy3(listener->state.position, pos);
    sound3d_copy3(listener->last_sync_position, pos);
    listener->has_last_sync_position = 1;
    sound3d_listener_push_active_state(listener);
}

/// @brief Convenience overload of `_set_position` taking three doubles instead of a Vec3.
void rt_soundlistener3d_set_position_vec(void *obj, double x, double y, double z) {
    void *position = rt_vec3_new(x, y, z);
    rt_soundlistener3d_set_position(obj, position);
    sound3d_release_local(position);
}

/// @brief Read the listener's world-space forward (look-at) vector. Re-syncs binding first
/// so the result tracks attached nodes/cameras.
void *rt_soundlistener3d_get_forward(void *obj) {
    rt_soundlistener3d *listener = sound3d_listener_checked(obj);
    if (!listener)
        return NULL;
    sound3d_listener_sync_binding(listener, 0.0);
    return rt_vec3_new(
        listener->state.forward[0], listener->state.forward[1], listener->state.forward[2]);
}

/// @brief Set the listener's forward vector explicitly (for left/right pan calculations).
/// The vector is normalized inside the audio core; magnitude is irrelevant.
void rt_soundlistener3d_set_forward(void *obj, void *forward) {
    rt_soundlistener3d *listener = sound3d_listener_checked(obj);
    double fwd[3];
    if (!listener)
        return;
    if (!sound3d_vec_from_obj(forward, fwd))
        return;
    rt_sound3d_listener_state_set_pose(
        &listener->state, listener->state.position, fwd, listener->state.up, listener->state.velocity);
    sound3d_listener_push_active_state(listener);
}

/// @brief Read the listener's world-space up vector. Re-syncs binding first
/// so the result tracks attached nodes.
void *rt_soundlistener3d_get_up(void *obj) {
    rt_soundlistener3d *listener = sound3d_listener_checked(obj);
    if (!listener)
        return NULL;
    sound3d_listener_sync_binding(listener, 0.0);
    return rt_vec3_new(listener->state.up[0], listener->state.up[1], listener->state.up[2]);
}

/// @brief Set the listener's up vector explicitly. The basis is orthonormalized
/// against the current forward vector inside the audio core.
void rt_soundlistener3d_set_up(void *obj, void *up) {
    rt_soundlistener3d *listener = sound3d_listener_checked(obj);
    double upv[3];
    if (!listener)
        return;
    if (!sound3d_vec_from_obj(up, upv))
        return;
    rt_sound3d_listener_state_set_pose(&listener->state,
                                       listener->state.position,
                                       listener->state.forward,
                                       upv,
                                       listener->state.velocity);
    sound3d_listener_push_active_state(listener);
}

/// @brief Read the listener's velocity (used for Doppler effects). Auto-synced from binding.
void *rt_soundlistener3d_get_velocity(void *obj) {
    rt_soundlistener3d *listener = sound3d_listener_checked(obj);
    if (!listener)
        return NULL;
    sound3d_listener_sync_binding(listener, 0.0);
    return rt_vec3_new(
        listener->state.velocity[0], listener->state.velocity[1], listener->state.velocity[2]);
}

/// @brief Override the listener's velocity. Useful for non-physical movements (e.g., camera
/// scripted shake) where the position-delta-based auto-velocity would lie about real motion.
void rt_soundlistener3d_set_velocity(void *obj, void *velocity) {
    rt_soundlistener3d *listener = sound3d_listener_checked(obj);
    if (!listener)
        return;
    if (!sound3d_vec_from_obj(velocity, listener->state.velocity))
        return;
    sound3d_listener_push_active_state(listener);
}

/// @brief Return 1 if this listener is the currently-active one (i.e., the one feeding the
/// audio core's pan/volume calculations). Only one listener can be active at a time.
int8_t rt_soundlistener3d_get_is_active(void *obj) {
    rt_soundlistener3d *listener = sound3d_listener_checked(obj);
    return listener ? listener->is_active : 0;
}

/// @brief Make this listener the active one (deactivating any previously-active listener).
/// Setting `active=0` deactivates this listener and clears the audio core's listener state,
/// which makes spatial sources fall back to centered/full-volume output.
void rt_soundlistener3d_set_is_active(void *obj, int8_t active) {
    rt_soundlistener3d *listener = sound3d_listener_checked(obj);
    if (!listener)
        return;

    if (active) {
        if (s_active_listener_obj && s_active_listener_obj != listener)
            s_active_listener_obj->is_active = 0;
        s_active_listener_obj = listener;
        listener->is_active = 1;
        sound3d_listener_sync_binding(listener, 0.0);
        sound3d_listener_push_active_state(listener);
        return;
    }

    listener->is_active = 0;
    if (s_active_listener_obj == listener) {
        s_active_listener_obj = NULL;
        rt_sound3d_clear_active_listener_state();
    }
}

/// @brief Bind the listener to a SceneNode3D — its position and forward will track the node's
/// world transform every `_sync_bindings` tick. Replaces any prior node/camera binding.
void rt_soundlistener3d_bind_node(void *obj, void *node) {
    rt_soundlistener3d *listener = sound3d_listener_checked(obj);
    if (!listener)
        return;
    if (node && !rt_g3d_has_class(node, RT_G3D_SCENENODE3D_CLASS_ID))
        return;
    if (node)
        rt_obj_retain_maybe(node);
    sound3d_release_ref(&listener->bound_node);
    listener->bound_node = node;
    sound3d_release_ref(&listener->bound_camera);
    listener->has_last_sync_position = 0;
    sound3d_listener_sync_binding(listener, 0.0);
}

/// @brief Detach the listener from any bound scene node. Subsequent position/forward stay at
/// the most recent values until manually changed.
void rt_soundlistener3d_clear_node_binding(void *obj) {
    rt_soundlistener3d *listener = sound3d_listener_checked(obj);
    if (!listener)
        return;
    sound3d_release_ref(&listener->bound_node);
}

/// @brief Bind the listener to a Camera3D — preferred over node binding for FPS-style audio
/// since the camera's forward already encodes head orientation. Replaces any prior binding.
void rt_soundlistener3d_bind_camera(void *obj, void *camera) {
    rt_soundlistener3d *listener = sound3d_listener_checked(obj);
    if (!listener)
        return;
    if (camera && !rt_g3d_has_class(camera, RT_G3D_CAMERA3D_CLASS_ID))
        return;
    if (camera)
        rt_obj_retain_maybe(camera);
    sound3d_release_ref(&listener->bound_camera);
    listener->bound_camera = camera;
    sound3d_release_ref(&listener->bound_node);
    listener->has_last_sync_position = 0;
    sound3d_listener_sync_binding(listener, 0.0);
}

/// @brief Detach the listener from any bound camera. Position/forward freeze at last sync.
void rt_soundlistener3d_clear_camera_binding(void *obj) {
    rt_soundlistener3d *listener = sound3d_listener_checked(obj);
    if (!listener)
        return;
    sound3d_release_ref(&listener->bound_camera);
}

/// @brief Create a 3D-positioned audio source playing `sound`. Defaults: full-volume radius
/// 1 world unit, max distance 50 world units, volume 100/100, non-looping, position at
/// origin. Spatial volume/pan are computed per-frame from the active listener once playback
/// starts via `_play`.
void *rt_soundsource3d_new(void *sound) {
    rt_soundsource3d *source = (rt_soundsource3d *)rt_obj_new_i64(
        RT_G3D_SOUNDSOURCE3D_CLASS_ID, (int64_t)sizeof(rt_soundsource3d));
    if (!source)
        return NULL;
    memset(source, 0, sizeof(*source));
    if (sound)
        rt_obj_retain_maybe(sound);
    source->sound = sound;
    source->doppler_factor = 1.0;
    source->ref_distance = 1.0;
    source->max_distance = 50.0;
    source->volume = 100;
    source->looping = 0;
    sound3d_source_list_add(source);
    rt_obj_set_finalizer(source, sound3d_source_finalize);
    return source;
}

/// @brief Read the source's world-space position. Re-syncs binding so the result reflects
/// the bound node's current world transform.
void *rt_soundsource3d_get_position(void *obj) {
    rt_soundsource3d *source = sound3d_source_checked(obj);
    if (!source)
        return NULL;
    sound3d_source_sync_binding(source, 0.0);
    return rt_vec3_new(source->position[0], source->position[1], source->position[2]);
}

/// @brief Manually set the source's world position. Resets the velocity tracker (no jump) and
/// re-applies spatial volume/pan immediately so playback continues at the new location.
void rt_soundsource3d_set_position(void *obj, void *position) {
    rt_soundsource3d *source = sound3d_source_checked(obj);
    if (!source)
        return;
    if (!sound3d_vec_from_obj(position, source->position))
        return;
    sound3d_copy3(source->last_sync_position, source->position);
    source->has_last_sync_position = 1;
    sound3d_source_apply_spatial(source);
}

/// @brief Convenience overload of `_set_position` taking three doubles instead of a Vec3.
void rt_soundsource3d_set_position_vec(void *obj, double x, double y, double z) {
    void *position = rt_vec3_new(x, y, z);
    rt_soundsource3d_set_position(obj, position);
    sound3d_release_local(position);
}

/// @brief Read the source's velocity (Doppler input). Re-syncs binding before returning.
void *rt_soundsource3d_get_velocity(void *obj) {
    rt_soundsource3d *source = sound3d_source_checked(obj);
    if (!source)
        return NULL;
    sound3d_source_sync_binding(source, 0.0);
    return rt_vec3_new(source->velocity[0], source->velocity[1], source->velocity[2]);
}

/// @brief Override the source's velocity. Skips the auto-derived position-delta velocity for
/// the next frame; useful for scripted or non-Newtonian motion.
void rt_soundsource3d_set_velocity(void *obj, void *velocity) {
    rt_soundsource3d *source = sound3d_source_checked(obj);
    if (!source)
        return;
    if (!sound3d_vec_from_obj(velocity, source->velocity))
        return;
    sound3d_source_apply_spatial(source);
}

/// @brief Latest Doppler factor computed from listener/source velocity.
double rt_soundsource3d_get_doppler_factor(void *obj) {
    rt_soundsource3d *source = sound3d_source_checked(obj);
    if (!source)
        return 1.0;
    sound3d_source_sync_binding(source, 0.0);
    sound3d_source_refresh_doppler(source);
    return source->doppler_factor;
}

/// @brief Maximum audible distance in world units. Beyond this the source contributes 0 volume.
double rt_soundsource3d_get_max_distance(void *obj) {
    rt_soundsource3d *source = sound3d_source_checked(obj);
    return source ? source->max_distance : 0.0;
}

/// @brief Set audible-falloff distance (clamped to ≥ 0). Larger = louder for further-away
/// sources. Spatial volume/pan are recomputed immediately so playback adapts to the new range.
void rt_soundsource3d_set_max_distance(void *obj, double max_distance) {
    rt_soundsource3d *source = sound3d_source_checked(obj);
    if (!source)
        return;
    source->max_distance = (isfinite(max_distance) && max_distance > 0.0) ? max_distance : 0.0;
    if (source->ref_distance > 0.0 && source->max_distance > 0.0 &&
        source->max_distance < source->ref_distance)
        source->max_distance = source->ref_distance;
    sound3d_source_apply_spatial(source);
}

/// @brief Full-volume reference distance in world units. Falloff begins past this radius.
double rt_soundsource3d_get_ref_distance(void *obj) {
    rt_soundsource3d *source = sound3d_source_checked(obj);
    return source ? source->ref_distance : 0.0;
}

/// @brief Set the full-volume reference radius. The max distance is raised when needed so
/// the attenuation interval remains well-formed.
void rt_soundsource3d_set_ref_distance(void *obj, double ref_distance) {
    rt_soundsource3d *source = sound3d_source_checked(obj);
    if (!source)
        return;
    source->ref_distance = (isfinite(ref_distance) && ref_distance > 0.0) ? ref_distance : 1.0;
    if (source->max_distance > 0.0 && source->max_distance < source->ref_distance)
        source->max_distance = source->ref_distance;
    sound3d_source_apply_spatial(source);
}

/// @brief Read the source's nominal volume (0..100). Spatial attenuation is applied separately.
int64_t rt_soundsource3d_get_volume(void *obj) {
    rt_soundsource3d *source = sound3d_source_checked(obj);
    return source ? source->volume : 0;
}

/// @brief Set the source's nominal volume (clamped to 0..100). Re-applies spatial mixing
/// immediately so an active voice picks up the change next tick.
void rt_soundsource3d_set_volume(void *obj, int64_t volume) {
    rt_soundsource3d *source = sound3d_source_checked(obj);
    if (!source)
        return;
    source->volume = sound3d_clamp_volume(volume);
    sound3d_source_apply_spatial(source);
}

/// @brief Returns 1 if the source plays in a loop (vs. fire-and-forget one-shot).
int8_t rt_soundsource3d_get_looping(void *obj) {
    rt_soundsource3d *source = sound3d_source_checked(obj);
    return source ? source->looping : 0;
}

/// @brief Toggle looping mode. Takes effect on the next `_play` call (does not affect a voice
/// already in flight; stop and replay to apply mid-stream).
void rt_soundsource3d_set_looping(void *obj, int8_t looping) {
    rt_soundsource3d *source = sound3d_source_checked(obj);
    if (!source)
        return;
    source->looping = looping ? 1 : 0;
}

/// @brief Returns 1 if the underlying voice is still active. Auto-reaps stale voice IDs whose
/// playback has finished (so subsequent calls return 0 cleanly).
int8_t rt_soundsource3d_get_is_playing(void *obj) {
    rt_soundsource3d *source = sound3d_source_checked(obj);
    return source ? sound3d_source_refresh_play_state(source) : 0;
}

/// @brief Return the underlying voice ID (for low-level voice control). Returns 0 if the
/// source isn't playing or the voice has finished. Always re-checks live state first.
int64_t rt_soundsource3d_get_voice_id(void *obj) {
    rt_soundsource3d *source = sound3d_source_checked(obj);
    if (!source)
        return 0;
    sound3d_source_refresh_play_state(source);
    return source->voice_id;
}

/// @brief Start playback of the bound sound. Computes initial spatial volume/pan against the
/// active listener, then plays via `rt_sound_play_loop` (looping) or `rt_sound_play_ex` (one-
/// shot). Stops any prior voice this source owned. Returns the voice ID, or 0 on failure.
int64_t rt_soundsource3d_play(void *obj) {
    rt_soundsource3d *source = sound3d_source_checked(obj);
    rt_sound3d_listener_state listener;
    int64_t spatial_volume = 0;
    int64_t spatial_pan = 0;
    if (!source || !source->sound)
        return 0;

    sound3d_source_sync_binding(source, 0.0);
    sound3d_refresh_active_listener();
    rt_sound3d_get_effective_listener_state(&listener);
    rt_sound3d_compute_voice_params_ex(&listener,
                                       source->position,
                                       source->velocity,
                                       source->ref_distance,
                                       source->max_distance,
                                       sound3d_clamp_volume(source->volume),
                                       &spatial_volume,
                                       &spatial_pan,
                                       &source->doppler_factor);

    if (source->voice_id > 0)
        rt_voice_stop(source->voice_id);
    source->voice_id = source->looping
                           ? rt_sound_play_loop(source->sound, spatial_volume, spatial_pan)
                           : rt_sound_play_ex(source->sound, spatial_volume, spatial_pan);
    if (source->voice_id <= 0)
        source->voice_id = 0;
    else
        rt_sound3d_register_voice_ex(source->voice_id,
                                     source->ref_distance,
                                     source->max_distance,
                                     sound3d_clamp_volume(source->volume));
    return source->voice_id;
}

/// @brief Stop the active voice (if any) and clear the source's voice ID. No-op if not playing.
void rt_soundsource3d_stop(void *obj) {
    rt_soundsource3d *source = sound3d_source_checked(obj);
    if (!source)
        return;
    if (source->voice_id > 0)
        rt_voice_stop(source->voice_id);
    source->voice_id = 0;
}

/// @brief Bind the source to a SceneNode3D — the source position will track the node's world
/// transform every `_sync_bindings` tick. Replaces any prior binding and immediately syncs.
void rt_soundsource3d_bind_node(void *obj, void *node) {
    rt_soundsource3d *source = sound3d_source_checked(obj);
    if (!source)
        return;
    if (node && !rt_g3d_has_class(node, RT_G3D_SCENENODE3D_CLASS_ID))
        return;
    if (node)
        rt_obj_retain_maybe(node);
    sound3d_release_ref(&source->bound_node);
    source->bound_node = node;
    source->has_last_sync_position = 0;
    sound3d_source_sync_binding(source, 0.0);
}

/// @brief Detach the source from any bound node. Position freezes at last sync.
void rt_soundsource3d_clear_node_binding(void *obj) {
    rt_soundsource3d *source = sound3d_source_checked(obj);
    if (!source)
        return;
    sound3d_release_ref(&source->bound_node);
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
