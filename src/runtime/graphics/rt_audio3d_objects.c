//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_audio3d_objects.c
// Purpose: Object-backed 3D audio listener/source APIs layered on top of the
//   low-level Audio3D helpers and the existing 2D voice runtime.
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_audio3d.h"
#include "rt_audiolistener3d.h"
#include "rt_audiosource3d.h"
#include "rt_canvas3d.h"
#include "rt_mat4.h"
#include "rt_scene3d.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
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

typedef struct rt_audiolistener3d {
    void *vptr;
    rt_audio3d_listener_state state;
    void *bound_node;
    void *bound_camera;
    double last_sync_position[3];
    int8_t has_last_sync_position;
    int8_t is_active;
    struct rt_audiolistener3d *prev;
    struct rt_audiolistener3d *next;
} rt_audiolistener3d;

typedef struct rt_audiosource3d {
    void *vptr;
    void *sound;
    void *bound_node;
    double position[3];
    double velocity[3];
    double last_sync_position[3];
    int8_t has_last_sync_position;
    double max_distance;
    int64_t volume;
    int64_t voice_id;
    int8_t looping;
    struct rt_audiosource3d *prev;
    struct rt_audiosource3d *next;
} rt_audiosource3d;

static rt_audiolistener3d *s_listener_head = NULL;
static rt_audiosource3d *s_source_head = NULL;
static rt_audiolistener3d *s_active_listener_obj = NULL;

static void audio3d_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

static void audio3d_copy3(double *dst, const double *src) {
    if (!dst)
        return;
    if (!src) {
        dst[0] = 0.0;
        dst[1] = 0.0;
        dst[2] = 0.0;
        return;
    }
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
}

static void audio3d_vec_from_obj(void *vec, double *out_xyz) {
    if (!out_xyz)
        return;
    if (!vec) {
        out_xyz[0] = 0.0;
        out_xyz[1] = 0.0;
        out_xyz[2] = 0.0;
        return;
    }
    out_xyz[0] = rt_vec3_x(vec);
    out_xyz[1] = rt_vec3_y(vec);
    out_xyz[2] = rt_vec3_z(vec);
}

/// @brief Compute velocity from position delta and update the last-position cache.
/// @details Skips velocity computation on the first call (no prior position to
///          differentiate against) or when `dt < 1e-8` (avoids divide-by-near-zero
///          producing huge spurious velocities). The last-position cache is
///          always updated so the *next* call has a baseline. Velocity is
///          intended to drive Doppler effects in the audio core.
static void audio3d_update_velocity(double *velocity,
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

static int64_t audio3d_clamp_volume(int64_t volume) {
    if (volume < 0)
        return 0;
    if (volume > 100)
        return 100;
    return volume;
}

static void audio3d_listener_list_add(rt_audiolistener3d *listener) {
    if (!listener)
        return;
    listener->prev = NULL;
    listener->next = s_listener_head;
    if (s_listener_head)
        s_listener_head->prev = listener;
    s_listener_head = listener;
}

static void audio3d_listener_list_remove(rt_audiolistener3d *listener) {
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

static void audio3d_source_list_add(rt_audiosource3d *source) {
    if (!source)
        return;
    source->prev = NULL;
    source->next = s_source_head;
    if (s_source_head)
        s_source_head->prev = source;
    s_source_head = source;
}

static void audio3d_source_list_remove(rt_audiosource3d *source) {
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
static void audio3d_get_node_world_position(void *node, double *out_position) {
    void *world_matrix;
    void *world_position;
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
        audio3d_vec_from_obj(rt_scene_node3d_get_position(node), out_position);
        return;
    }
    world_position = rt_mat4_transform_point(world_matrix, rt_vec3_new(0.0, 0.0, 0.0));
    audio3d_vec_from_obj(world_position, out_position);
}

/// @brief Resolve a SceneNode3D's world-space forward direction.
/// @details Computes forward as the world-transformed `(0, 0, -1)` minus the
///          world-transformed origin — the difference cancels the
///          translation component, leaving only the rotated direction.
///          Result is normalised; degenerate transforms fall back to the
///          identity forward `(0, 0, -1)`. Two `rt_vec3_new` allocations
///          per call are accepted overhead since this is per-tick, not
///          per-frame-per-pixel.
static void audio3d_get_node_world_forward(void *node, double *out_forward) {
    void *world_matrix;
    void *origin;
    void *ahead;
    double origin_xyz[3];
    double ahead_xyz[3];
    double len;
    if (!out_forward)
        return;
    if (!node) {
        out_forward[0] = 0.0;
        out_forward[1] = 0.0;
        out_forward[2] = -1.0;
        return;
    }
    world_matrix = rt_scene_node3d_get_world_matrix(node);
    if (!world_matrix) {
        out_forward[0] = 0.0;
        out_forward[1] = 0.0;
        out_forward[2] = -1.0;
        return;
    }
    origin = rt_mat4_transform_point(world_matrix, rt_vec3_new(0.0, 0.0, 0.0));
    ahead = rt_mat4_transform_point(world_matrix, rt_vec3_new(0.0, 0.0, -1.0));
    audio3d_vec_from_obj(origin, origin_xyz);
    audio3d_vec_from_obj(ahead, ahead_xyz);
    out_forward[0] = ahead_xyz[0] - origin_xyz[0];
    out_forward[1] = ahead_xyz[1] - origin_xyz[1];
    out_forward[2] = ahead_xyz[2] - origin_xyz[2];
    len = sqrt(out_forward[0] * out_forward[0] + out_forward[1] * out_forward[1] +
               out_forward[2] * out_forward[2]);
    if (len <= 1e-8) {
        out_forward[0] = 0.0;
        out_forward[1] = 0.0;
        out_forward[2] = -1.0;
        return;
    }
    out_forward[0] /= len;
    out_forward[1] /= len;
    out_forward[2] /= len;
}

static void audio3d_listener_push_active_state(rt_audiolistener3d *listener) {
    if (listener && listener->is_active)
        rt_audio3d_set_active_listener_state(&listener->state);
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
static void audio3d_listener_sync_binding(rt_audiolistener3d *listener, double dt) {
    double position[3];
    double forward[3];
    if (!listener)
        return;

    if (listener->bound_camera) {
        audio3d_vec_from_obj(rt_camera3d_get_position(listener->bound_camera), position);
        audio3d_vec_from_obj(rt_camera3d_get_forward(listener->bound_camera), forward);
        audio3d_update_velocity(listener->state.velocity,
                                listener->last_sync_position,
                                &listener->has_last_sync_position,
                                position,
                                dt);
        rt_audio3d_listener_state_set(&listener->state, position, forward, listener->state.velocity);
        audio3d_listener_push_active_state(listener);
        return;
    }

    if (listener->bound_node) {
        audio3d_get_node_world_position(listener->bound_node, position);
        audio3d_get_node_world_forward(listener->bound_node, forward);
        audio3d_update_velocity(listener->state.velocity,
                                listener->last_sync_position,
                                &listener->has_last_sync_position,
                                position,
                                dt);
        rt_audio3d_listener_state_set(&listener->state, position, forward, listener->state.velocity);
        audio3d_listener_push_active_state(listener);
    }
}

static void audio3d_refresh_active_listener(void) {
    if (s_active_listener_obj)
        audio3d_listener_sync_binding(s_active_listener_obj, 0.0);
}

static int8_t audio3d_source_refresh_play_state(rt_audiosource3d *source) {
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
///          delegates to `rt_audio3d_compute_voice_params` for the actual
///          attenuation + pan math, then pushes the results to the voice via
///          `rt_voice_set_volume` / `rt_voice_set_pan`. Called from every
///          source-mutating setter so changes take effect immediately rather
///          than at the next sync tick.
static void audio3d_source_apply_spatial(rt_audiosource3d *source) {
    rt_audio3d_listener_state listener;
    int64_t spatial_volume = 0;
    int64_t spatial_pan = 0;
    if (!source || !audio3d_source_refresh_play_state(source))
        return;
    audio3d_refresh_active_listener();
    rt_audio3d_get_effective_listener_state(&listener);
    rt_audio3d_compute_voice_params(
        &listener, source->position, source->max_distance, audio3d_clamp_volume(source->volume), &spatial_volume, &spatial_pan);
    rt_voice_set_volume(source->voice_id, spatial_volume);
    rt_voice_set_pan(source->voice_id, spatial_pan);
}

/// @brief Re-sync a source's position from its bound scene node.
/// @details No-op when the source isn't bound to a node (free-floating
///          source whose position is set manually). After updating
///          position + velocity, applies spatial mixing so the next
///          mixer tick uses the new values.
static void audio3d_source_sync_binding(rt_audiosource3d *source, double dt) {
    double position[3];
    if (!source || !source->bound_node)
        return;
    audio3d_get_node_world_position(source->bound_node, position);
    audio3d_update_velocity(
        source->velocity, source->last_sync_position, &source->has_last_sync_position, position, dt);
    audio3d_copy3(source->position, position);
    audio3d_source_apply_spatial(source);
}

static void audio3d_listener_finalize(void *obj) {
    rt_audiolistener3d *listener = (rt_audiolistener3d *)obj;
    if (!listener)
        return;
    if (s_active_listener_obj == listener) {
        s_active_listener_obj = NULL;
        rt_audio3d_clear_active_listener_state();
    }
    audio3d_listener_list_remove(listener);
    audio3d_release_ref(&listener->bound_node);
    audio3d_release_ref(&listener->bound_camera);
}

static void audio3d_source_finalize(void *obj) {
    rt_audiosource3d *source = (rt_audiosource3d *)obj;
    if (!source)
        return;
    if (source->voice_id > 0)
        rt_voice_stop(source->voice_id);
    source->voice_id = 0;
    audio3d_source_list_remove(source);
    audio3d_release_ref(&source->sound);
    audio3d_release_ref(&source->bound_node);
}

/// @brief Per-frame tick: walk every live AudioListener3D and AudioSource3D, and re-resolve
/// each one's bound scene-node or camera into world position+forward, computing velocity from
/// the position delta over `dt`. Called by the game loop so spatial audio tracks moving entities
/// without per-source manual updates.
void rt_audio3d_sync_bindings(double dt) {
    rt_audiolistener3d *listener = s_listener_head;
    rt_audiosource3d *source = s_source_head;
    while (listener) {
        audio3d_listener_sync_binding(listener, dt);
        listener = listener->next;
    }
    while (source) {
        audio3d_source_sync_binding(source, dt);
        source = source->next;
    }
}

/// @brief Create a 3D audio listener (the "ears" of the scene). Initializes to identity
/// (origin, forward = -Z, zero velocity). The first listener constructed becomes the active
/// one automatically; subsequent ones must be activated with `_set_is_active`.
void *rt_audiolistener3d_new(void) {
    rt_audiolistener3d *listener =
        (rt_audiolistener3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_audiolistener3d));
    if (!listener)
        return NULL;
    memset(listener, 0, sizeof(*listener));
    rt_audio3d_listener_state_identity(&listener->state);
    audio3d_listener_list_add(listener);
    rt_obj_set_finalizer(listener, audio3d_listener_finalize);
    if (!s_active_listener_obj)
        rt_audiolistener3d_set_is_active(listener, 1);
    return listener;
}

/// @brief Read the listener's world-space position. If the listener is bound to a node/camera,
/// re-syncs the binding first to ensure the returned value reflects the current transform.
void *rt_audiolistener3d_get_position(void *obj) {
    rt_audiolistener3d *listener = (rt_audiolistener3d *)obj;
    if (!listener)
        return NULL;
    audio3d_listener_sync_binding(listener, 0.0);
    return rt_vec3_new(
        listener->state.position[0], listener->state.position[1], listener->state.position[2]);
}

/// @brief Manually set the listener world position. Resets the velocity tracker so the next
/// sync starts fresh (no spurious large-velocity blip from a teleport).
void rt_audiolistener3d_set_position(void *obj, void *position) {
    rt_audiolistener3d *listener = (rt_audiolistener3d *)obj;
    double pos[3];
    if (!listener)
        return;
    audio3d_vec_from_obj(position, pos);
    audio3d_copy3(listener->state.position, pos);
    audio3d_copy3(listener->last_sync_position, pos);
    listener->has_last_sync_position = 1;
    audio3d_listener_push_active_state(listener);
}

/// @brief Convenience overload of `_set_position` taking three doubles instead of a Vec3.
void rt_audiolistener3d_set_position_vec(void *obj, double x, double y, double z) {
    rt_audiolistener3d_set_position(obj, rt_vec3_new(x, y, z));
}

/// @brief Read the listener's world-space forward (look-at) vector. Re-syncs binding first
/// so the result tracks attached nodes/cameras.
void *rt_audiolistener3d_get_forward(void *obj) {
    rt_audiolistener3d *listener = (rt_audiolistener3d *)obj;
    if (!listener)
        return NULL;
    audio3d_listener_sync_binding(listener, 0.0);
    return rt_vec3_new(
        listener->state.forward[0], listener->state.forward[1], listener->state.forward[2]);
}

/// @brief Set the listener's forward vector explicitly (for left/right pan calculations).
/// The vector is normalized inside the audio core; magnitude is irrelevant.
void rt_audiolistener3d_set_forward(void *obj, void *forward) {
    rt_audiolistener3d *listener = (rt_audiolistener3d *)obj;
    double fwd[3];
    if (!listener)
        return;
    audio3d_vec_from_obj(forward, fwd);
    rt_audio3d_listener_state_set(
        &listener->state, listener->state.position, fwd, listener->state.velocity);
    audio3d_listener_push_active_state(listener);
}

/// @brief Read the listener's velocity (used for Doppler effects). Auto-synced from binding.
void *rt_audiolistener3d_get_velocity(void *obj) {
    rt_audiolistener3d *listener = (rt_audiolistener3d *)obj;
    if (!listener)
        return NULL;
    audio3d_listener_sync_binding(listener, 0.0);
    return rt_vec3_new(
        listener->state.velocity[0], listener->state.velocity[1], listener->state.velocity[2]);
}

/// @brief Override the listener's velocity. Useful for non-physical movements (e.g., camera
/// scripted shake) where the position-delta-based auto-velocity would lie about real motion.
void rt_audiolistener3d_set_velocity(void *obj, void *velocity) {
    rt_audiolistener3d *listener = (rt_audiolistener3d *)obj;
    if (!listener)
        return;
    audio3d_vec_from_obj(velocity, listener->state.velocity);
    audio3d_listener_push_active_state(listener);
}

/// @brief Return 1 if this listener is the currently-active one (i.e., the one feeding the
/// audio core's pan/volume calculations). Only one listener can be active at a time.
int8_t rt_audiolistener3d_get_is_active(void *obj) {
    return obj ? ((rt_audiolistener3d *)obj)->is_active : 0;
}

/// @brief Make this listener the active one (deactivating any previously-active listener).
/// Setting `active=0` deactivates this listener and clears the audio core's listener state,
/// which makes spatial sources fall back to centered/full-volume output.
void rt_audiolistener3d_set_is_active(void *obj, int8_t active) {
    rt_audiolistener3d *listener = (rt_audiolistener3d *)obj;
    if (!listener)
        return;

    if (active) {
        if (s_active_listener_obj && s_active_listener_obj != listener)
            s_active_listener_obj->is_active = 0;
        s_active_listener_obj = listener;
        listener->is_active = 1;
        audio3d_listener_sync_binding(listener, 0.0);
        audio3d_listener_push_active_state(listener);
        return;
    }

    listener->is_active = 0;
    if (s_active_listener_obj == listener) {
        s_active_listener_obj = NULL;
        rt_audio3d_clear_active_listener_state();
    }
}

/// @brief Bind the listener to a SceneNode3D — its position and forward will track the node's
/// world transform every `_sync_bindings` tick. Replaces any prior node/camera binding.
void rt_audiolistener3d_bind_node(void *obj, void *node) {
    rt_audiolistener3d *listener = (rt_audiolistener3d *)obj;
    if (!listener)
        return;
    if (node)
        rt_obj_retain_maybe(node);
    audio3d_release_ref(&listener->bound_node);
    listener->bound_node = node;
    audio3d_release_ref(&listener->bound_camera);
    listener->has_last_sync_position = 0;
    audio3d_listener_sync_binding(listener, 0.0);
}

/// @brief Detach the listener from any bound scene node. Subsequent position/forward stay at
/// the most recent values until manually changed.
void rt_audiolistener3d_clear_node_binding(void *obj) {
    if (!obj)
        return;
    audio3d_release_ref(&((rt_audiolistener3d *)obj)->bound_node);
}

/// @brief Bind the listener to a Camera3D — preferred over node binding for FPS-style audio
/// since the camera's forward already encodes head orientation. Replaces any prior binding.
void rt_audiolistener3d_bind_camera(void *obj, void *camera) {
    rt_audiolistener3d *listener = (rt_audiolistener3d *)obj;
    if (!listener)
        return;
    if (camera)
        rt_obj_retain_maybe(camera);
    audio3d_release_ref(&listener->bound_camera);
    listener->bound_camera = camera;
    audio3d_release_ref(&listener->bound_node);
    listener->has_last_sync_position = 0;
    audio3d_listener_sync_binding(listener, 0.0);
}

/// @brief Detach the listener from any bound camera. Position/forward freeze at last sync.
void rt_audiolistener3d_clear_camera_binding(void *obj) {
    if (!obj)
        return;
    audio3d_release_ref(&((rt_audiolistener3d *)obj)->bound_camera);
}

/// @brief Create a 3D-positioned audio source playing `sound`. Defaults: max distance 50 world
/// units, volume 100/100, non-looping, position at origin. Spatial volume/pan are computed
/// per-frame from the active listener once playback starts via `_play`.
void *rt_audiosource3d_new(void *sound) {
    rt_audiosource3d *source =
        (rt_audiosource3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_audiosource3d));
    if (!source)
        return NULL;
    memset(source, 0, sizeof(*source));
    if (sound)
        rt_obj_retain_maybe(sound);
    source->sound = sound;
    source->max_distance = 50.0;
    source->volume = 100;
    source->looping = 0;
    audio3d_source_list_add(source);
    rt_obj_set_finalizer(source, audio3d_source_finalize);
    return source;
}

/// @brief Read the source's world-space position. Re-syncs binding so the result reflects
/// the bound node's current world transform.
void *rt_audiosource3d_get_position(void *obj) {
    rt_audiosource3d *source = (rt_audiosource3d *)obj;
    if (!source)
        return NULL;
    audio3d_source_sync_binding(source, 0.0);
    return rt_vec3_new(source->position[0], source->position[1], source->position[2]);
}

/// @brief Manually set the source's world position. Resets the velocity tracker (no jump) and
/// re-applies spatial volume/pan immediately so playback continues at the new location.
void rt_audiosource3d_set_position(void *obj, void *position) {
    rt_audiosource3d *source = (rt_audiosource3d *)obj;
    if (!source)
        return;
    audio3d_vec_from_obj(position, source->position);
    audio3d_copy3(source->last_sync_position, source->position);
    source->has_last_sync_position = 1;
    audio3d_source_apply_spatial(source);
}

/// @brief Convenience overload of `_set_position` taking three doubles instead of a Vec3.
void rt_audiosource3d_set_position_vec(void *obj, double x, double y, double z) {
    rt_audiosource3d_set_position(obj, rt_vec3_new(x, y, z));
}

/// @brief Read the source's velocity (Doppler input). Re-syncs binding before returning.
void *rt_audiosource3d_get_velocity(void *obj) {
    rt_audiosource3d *source = (rt_audiosource3d *)obj;
    if (!source)
        return NULL;
    audio3d_source_sync_binding(source, 0.0);
    return rt_vec3_new(source->velocity[0], source->velocity[1], source->velocity[2]);
}

/// @brief Override the source's velocity. Skips the auto-derived position-delta velocity for
/// the next frame; useful for scripted or non-Newtonian motion.
void rt_audiosource3d_set_velocity(void *obj, void *velocity) {
    rt_audiosource3d *source = (rt_audiosource3d *)obj;
    if (!source)
        return;
    audio3d_vec_from_obj(velocity, source->velocity);
}

/// @brief Maximum audible distance in world units. Beyond this the source contributes 0 volume.
double rt_audiosource3d_get_max_distance(void *obj) {
    return obj ? ((rt_audiosource3d *)obj)->max_distance : 0.0;
}

/// @brief Set audible-falloff distance (clamped to ≥ 0). Larger = louder for further-away
/// sources. Spatial volume/pan are recomputed immediately so playback adapts to the new range.
void rt_audiosource3d_set_max_distance(void *obj, double max_distance) {
    rt_audiosource3d *source = (rt_audiosource3d *)obj;
    if (!source)
        return;
    source->max_distance = max_distance > 0.0 ? max_distance : 0.0;
    audio3d_source_apply_spatial(source);
}

/// @brief Read the source's nominal volume (0..100). Spatial attenuation is applied separately.
int64_t rt_audiosource3d_get_volume(void *obj) {
    return obj ? ((rt_audiosource3d *)obj)->volume : 0;
}

/// @brief Set the source's nominal volume (clamped to 0..100). Re-applies spatial mixing
/// immediately so an active voice picks up the change next tick.
void rt_audiosource3d_set_volume(void *obj, int64_t volume) {
    rt_audiosource3d *source = (rt_audiosource3d *)obj;
    if (!source)
        return;
    source->volume = audio3d_clamp_volume(volume);
    audio3d_source_apply_spatial(source);
}

/// @brief Returns 1 if the source plays in a loop (vs. fire-and-forget one-shot).
int8_t rt_audiosource3d_get_looping(void *obj) {
    return obj ? ((rt_audiosource3d *)obj)->looping : 0;
}

/// @brief Toggle looping mode. Takes effect on the next `_play` call (does not affect a voice
/// already in flight; stop and replay to apply mid-stream).
void rt_audiosource3d_set_looping(void *obj, int8_t looping) {
    if (!obj)
        return;
    ((rt_audiosource3d *)obj)->looping = looping ? 1 : 0;
}

/// @brief Returns 1 if the underlying voice is still active. Auto-reaps stale voice IDs whose
/// playback has finished (so subsequent calls return 0 cleanly).
int8_t rt_audiosource3d_get_is_playing(void *obj) {
    return obj ? audio3d_source_refresh_play_state((rt_audiosource3d *)obj) : 0;
}

/// @brief Return the underlying voice ID (for low-level voice control). Returns 0 if the
/// source isn't playing or the voice has finished. Always re-checks live state first.
int64_t rt_audiosource3d_get_voice_id(void *obj) {
    rt_audiosource3d *source = (rt_audiosource3d *)obj;
    if (!source)
        return 0;
    audio3d_source_refresh_play_state(source);
    return source->voice_id;
}

/// @brief Start playback of the bound sound. Computes initial spatial volume/pan against the
/// active listener, then plays via `rt_sound_play_loop` (looping) or `rt_sound_play_ex` (one-
/// shot). Stops any prior voice this source owned. Returns the voice ID, or 0 on failure.
int64_t rt_audiosource3d_play(void *obj) {
    rt_audiosource3d *source = (rt_audiosource3d *)obj;
    rt_audio3d_listener_state listener;
    int64_t spatial_volume = 0;
    int64_t spatial_pan = 0;
    if (!source || !source->sound)
        return 0;

    audio3d_source_sync_binding(source, 0.0);
    audio3d_refresh_active_listener();
    rt_audio3d_get_effective_listener_state(&listener);
    rt_audio3d_compute_voice_params(
        &listener, source->position, source->max_distance, audio3d_clamp_volume(source->volume), &spatial_volume, &spatial_pan);

    if (source->voice_id > 0)
        rt_voice_stop(source->voice_id);
    source->voice_id = source->looping ? rt_sound_play_loop(source->sound, spatial_volume, spatial_pan)
                                       : rt_sound_play_ex(source->sound, spatial_volume, spatial_pan);
    if (source->voice_id <= 0)
        source->voice_id = 0;
    return source->voice_id;
}

/// @brief Stop the active voice (if any) and clear the source's voice ID. No-op if not playing.
void rt_audiosource3d_stop(void *obj) {
    rt_audiosource3d *source = (rt_audiosource3d *)obj;
    if (!source)
        return;
    if (source->voice_id > 0)
        rt_voice_stop(source->voice_id);
    source->voice_id = 0;
}

/// @brief Bind the source to a SceneNode3D — the source position will track the node's world
/// transform every `_sync_bindings` tick. Replaces any prior binding and immediately syncs.
void rt_audiosource3d_bind_node(void *obj, void *node) {
    rt_audiosource3d *source = (rt_audiosource3d *)obj;
    if (!source)
        return;
    if (node)
        rt_obj_retain_maybe(node);
    audio3d_release_ref(&source->bound_node);
    source->bound_node = node;
    source->has_last_sync_position = 0;
    audio3d_source_sync_binding(source, 0.0);
}

/// @brief Detach the source from any bound node. Position freezes at last sync.
void rt_audiosource3d_clear_node_binding(void *obj) {
    if (!obj)
        return;
    audio3d_release_ref(&((rt_audiosource3d *)obj)->bound_node);
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
