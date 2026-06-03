//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/rt_game3d_audio.c
// Purpose: Sound3D audio subsystem for the Viper.Game3D layer — listener/source
//   management, distance attenuation, and camera-follow listener. Split out of
//   rt_game3d.c; shares private types/helpers via rt_game3d_internal.h.
// Ownership/Lifetime:
//   - GC-managed Sound3D handle; finalizer releases tracked sources + listener.
// Links: rt_game3d_internal.h, rt_sound3d.h
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

/// @brief GC finalizer for the audio subsystem: release every tracked source plus the
///   camera and listener, then free the source array.
static void game3d_audio_finalize(void *obj) {
    rt_game3d_audio *audio = (rt_game3d_audio *)obj;
    if (!audio)
        return;
    game3d_audio_repair_sources(audio);
    for (int32_t i = 0; i < audio->source_count; ++i)
        game3d_release_typed_ref(&audio->sources[i], RT_G3D_SOUNDSOURCE3D_CLASS_ID);
    free(audio->sources);
    audio->sources = NULL;
    audio->source_count = 0;
    audio->source_capacity = 0;
    game3d_release_typed_ref(&audio->camera, RT_G3D_CAMERA3D_CLASS_ID);
    game3d_release_typed_ref(&audio->listener, RT_G3D_SOUNDLISTENER3D_CLASS_ID);
}

/// @brief Allocate the audio subsystem with default attenuation/volume and a new
///   listener; if `camera` is given, bind and activate the listener to follow it.
void *game3d_audio_new(void *camera) {
    rt_game3d_audio *audio =
        (rt_game3d_audio *)rt_obj_new_i64(RT_G3D_GAME3D_SOUND_CLASS_ID, (int64_t)sizeof(*audio));
    if (!audio) {
        rt_trap("Game3D.Sound3D.New: allocation failed");
        return NULL;
    }
    memset(audio, 0, sizeof(*audio));
    rt_obj_set_finalizer(audio, game3d_audio_finalize);
    audio->ref_distance = RT_GAME3D_DEFAULT_AUDIO_REF_DISTANCE;
    audio->max_distance = RT_GAME3D_DEFAULT_AUDIO_MAX_DISTANCE;
    audio->volume = RT_GAME3D_DEFAULT_AUDIO_VOLUME;
    audio->listener = rt_soundlistener3d_new();
    if (rt_g3d_has_class(camera, RT_G3D_CAMERA3D_CLASS_ID))
        game3d_assign_typed_ref(&audio->camera, camera, RT_G3D_CAMERA3D_CLASS_ID);
    if (rt_g3d_has_class(audio->listener, RT_G3D_SOUNDLISTENER3D_CLASS_ID) && audio->camera) {
        audio->listener_follow_camera = 1;
        rt_soundlistener3d_bind_camera(audio->listener, audio->camera);
        rt_soundlistener3d_set_is_active(audio->listener, 1);
    }
    return audio;
}

/// @brief Get the listener (ears) object (NULL if invalid).
void *rt_game3d_audio_get_listener(void *obj) {
    rt_game3d_audio *audio =
        game3d_audio_checked(obj, "Game3D.Sound3D.get_Listener: invalid audio");
    return audio ? rt_g3d_checked_or_null(audio->listener, RT_G3D_SOUNDLISTENER3D_CLASS_ID) : NULL;
}

/// @brief True if the listener auto-follows the camera.
int8_t rt_game3d_audio_get_listener_follows_camera(void *obj) {
    rt_game3d_audio *audio =
        game3d_audio_checked(obj, "Game3D.Sound3D.get_listenerFollowsCamera: invalid audio");
    return audio && audio->listener_follow_camera ? 1 : 0;
}

/// @brief Get the attenuation reference (full-volume) distance.
double rt_game3d_audio_get_ref_distance(void *obj) {
    rt_game3d_audio *audio =
        game3d_audio_checked(obj, "Game3D.Sound3D.get_refDistance: invalid audio");
    return audio ? game3d_positive_clamped_or(audio->ref_distance,
                                              RT_GAME3D_DEFAULT_AUDIO_REF_DISTANCE,
                                              RT_GAME3D_AUDIO_DISTANCE_MAX)
                 : 0.0;
}

/// @brief Get the attenuation maximum (silence) distance.
double rt_game3d_audio_get_max_distance(void *obj) {
    rt_game3d_audio *audio =
        game3d_audio_checked(obj, "Game3D.Sound3D.get_maxDistance: invalid audio");
    if (!audio)
        return 0.0;
    double ref_distance = game3d_positive_clamped_or(
        audio->ref_distance, RT_GAME3D_DEFAULT_AUDIO_REF_DISTANCE, RT_GAME3D_AUDIO_DISTANCE_MAX);
    double max_distance = game3d_positive_clamped_or(
        audio->max_distance, RT_GAME3D_DEFAULT_AUDIO_MAX_DISTANCE, RT_GAME3D_AUDIO_DISTANCE_MAX);
    return max_distance < ref_distance ? ref_distance : max_distance;
}

/// @brief Get the master output volume (0–100).
int64_t rt_game3d_audio_get_volume(void *obj) {
    rt_game3d_audio *audio = game3d_audio_checked(obj, "Game3D.Sound3D.get_volume: invalid audio");
    return audio ? game3d_clamp_i64(audio->volume, 0, 100) : 0;
}

/// @brief Count currently active 3D sound sources.
int64_t rt_game3d_audio_get_source_count(void *obj) {
    rt_game3d_audio *audio =
        game3d_audio_checked(obj, "Game3D.Sound3D.get_sourceCount: invalid audio");
    if (audio)
        game3d_audio_repair_sources(audio);
    return audio ? audio->source_count : 0;
}

/// @brief Enable/disable the listener following the camera; rebinds or unbinds the
///   camera accordingly and reactivates the listener.
void rt_game3d_audio_listener_follow_camera(void *obj, int8_t enabled) {
    rt_game3d_audio *audio =
        game3d_audio_checked(obj, "Game3D.Sound3D.listenerFollowCamera: invalid audio");
    if (!audio)
        return;
    void *listener = rt_g3d_checked_or_null(audio->listener, RT_G3D_SOUNDLISTENER3D_CLASS_ID);
    void *camera = rt_g3d_checked_or_null(audio->camera, RT_G3D_CAMERA3D_CLASS_ID);
    if (!listener)
        return;
    audio->listener_follow_camera = enabled ? 1 : 0;
    if (audio->listener_follow_camera && camera)
        rt_soundlistener3d_bind_camera(listener, camera);
    else
        rt_soundlistener3d_clear_camera_binding(listener);
    rt_soundlistener3d_set_is_active(listener, 1);
}

/// @brief Set the listener pose explicitly from Vec3 position/forward/up;
///   disables camera-follow. Traps on non-Vec3 args.
void rt_game3d_audio_set_listener_pose(void *obj, void *position, void *forward, void *up) {
    rt_game3d_audio *audio =
        game3d_audio_checked(obj, "Game3D.Sound3D.setListenerPose: invalid audio");
    if (!audio)
        return;
    void *listener = rt_g3d_checked_or_null(audio->listener, RT_G3D_SOUNDLISTENER3D_CLASS_ID);
    if (!listener)
        return;
    if (!rt_g3d_is_vec3(position) || !rt_g3d_is_vec3(forward) || !rt_g3d_is_vec3(up)) {
        rt_trap("Game3D.Sound3D.setListenerPose: expected Vec3 position, forward, and up");
        return;
    }
    audio->listener_follow_camera = 0;
    rt_soundlistener3d_clear_camera_binding(listener);
    rt_soundlistener3d_set_position(listener, position);
    rt_soundlistener3d_set_forward(listener, forward);
    rt_soundlistener3d_set_up(listener, up);
    rt_soundlistener3d_set_is_active(listener, 1);
}

/// @brief Set the distance-attenuation reference and max radii; non-positive/non-finite
///   values fall back to the library defaults.
void rt_game3d_audio_set_attenuation(void *obj, double ref_distance, double max_distance) {
    rt_game3d_audio *audio =
        game3d_audio_checked(obj, "Game3D.Sound3D.setAttenuation: invalid audio");
    if (!audio)
        return;
    audio->ref_distance = game3d_positive_clamped_or(
        ref_distance, RT_GAME3D_DEFAULT_AUDIO_REF_DISTANCE, RT_GAME3D_AUDIO_DISTANCE_MAX);
    audio->max_distance = game3d_positive_clamped_or(
        max_distance, RT_GAME3D_DEFAULT_AUDIO_MAX_DISTANCE, RT_GAME3D_AUDIO_DISTANCE_MAX);
    if (audio->max_distance < audio->ref_distance)
        audio->max_distance = audio->ref_distance;
    game3d_audio_prune_sources(audio);
    for (int32_t i = 0; i < audio->source_count; ++i) {
        void *source = rt_g3d_checked_or_null(audio->sources[i], RT_G3D_SOUNDSOURCE3D_CLASS_ID);
        if (!source)
            continue;
        rt_soundsource3d_set_ref_distance(source, audio->ref_distance);
        rt_soundsource3d_set_max_distance(source, audio->max_distance);
    }
}

/// @brief Set the master output volume, clamped to [0, 100].
void rt_game3d_audio_set_volume(void *obj, int64_t volume) {
    rt_game3d_audio *audio = game3d_audio_checked(obj, "Game3D.Sound3D.set_volume: invalid audio");
    if (audio)
        audio->volume = game3d_clamp_i64(volume, 0, 100);
}

/// @brief Load a sound clip from a filesystem path.
void *rt_game3d_audio_load(void *obj, rt_string path) {
    (void)game3d_audio_checked(obj, "Game3D.Sound3D.load: invalid audio");
    return rt_sound_load(path);
}

/// @brief Load a sound clip from a packed asset path.
void *rt_game3d_audio_load_asset(void *obj, rt_string asset_path) {
    (void)game3d_audio_checked(obj, "Game3D.Sound3D.loadAsset: invalid audio");
    return rt_sound_load_asset(asset_path);
}

/// @brief Play a clip as a one-shot at a fixed world position, applying the subsystem's
///   attenuation/volume; tracks and returns the new source. Traps on bad clip/position.
void *rt_game3d_audio_play_at(void *obj, void *clip, void *position) {
    rt_game3d_audio *audio = game3d_audio_checked(obj, "Game3D.Sound3D.playAt: invalid audio");
    if (!audio || !clip)
        return NULL;
    if (!rt_sound_is_handle(clip)) {
        rt_trap("Game3D.Sound3D.playAt: expected Sound clip");
        return NULL;
    }
    if (!rt_g3d_is_vec3(position)) {
        rt_trap("Game3D.Sound3D.playAt: expected Vec3 position");
        return NULL;
    }
    void *source = rt_soundsource3d_new(clip);
    if (!source)
        return NULL;
    rt_soundsource3d_set_position(source, position);
    rt_soundsource3d_set_ref_distance(source, rt_game3d_audio_get_ref_distance(audio));
    rt_soundsource3d_set_max_distance(source, rt_game3d_audio_get_max_distance(audio));
    rt_soundsource3d_set_volume(source, rt_game3d_audio_get_volume(audio));
    (void)rt_soundsource3d_play(source);
    game3d_audio_track_source(audio, source);
    return source;
}

/// @brief Play a clip attached to an entity so it tracks the entity's node (or its
///   position if nodeless); tracks and returns the source. Traps on bad clip/entity.
void *rt_game3d_audio_play_attached(void *obj, void *clip, void *entity_obj) {
    rt_game3d_audio *audio =
        game3d_audio_checked(obj, "Game3D.Sound3D.playAttached: invalid audio");
    rt_game3d_entity *entity =
        game3d_entity_checked(entity_obj, "Game3D.Sound3D.playAttached: invalid entity");
    if (!audio || !entity || !clip)
        return NULL;
    if (!rt_sound_is_handle(clip)) {
        rt_trap("Game3D.Sound3D.playAttached: expected Sound clip");
        return NULL;
    }
    void *source = rt_soundsource3d_new(clip);
    if (!source)
        return NULL;
    rt_soundsource3d_set_ref_distance(source, rt_game3d_audio_get_ref_distance(audio));
    rt_soundsource3d_set_max_distance(source, rt_game3d_audio_get_max_distance(audio));
    rt_soundsource3d_set_volume(source, rt_game3d_audio_get_volume(audio));
    void *node = game3d_entity_node_ref(entity);
    if (node)
        rt_soundsource3d_bind_node(source, node);
    else {
        void *pos = rt_game3d_entity_position(entity);
        rt_soundsource3d_set_position(source, pos);
        game3d_release_ref(&pos);
    }
    (void)rt_soundsource3d_play(source);
    game3d_audio_track_source(audio, source);
    return source;
}

/// @brief Play a clip as non-spatial 2D audio at the master volume; returns a positive
///   voice id, or 0 on failure. Traps on a bad clip.
int64_t rt_game3d_audio_play2d(void *obj, void *clip) {
    rt_game3d_audio *audio = game3d_audio_checked(obj, "Game3D.Sound3D.play2D: invalid audio");
    if (!audio || !clip)
        return 0;
    if (!rt_sound_is_handle(clip)) {
        rt_trap("Game3D.Sound3D.play2D: expected Sound clip");
        return 0;
    }
    int64_t voice = rt_sound_play_ex(clip, rt_game3d_audio_get_volume(audio), 0);
    return voice > 0 ? voice : 0;
}

/// @brief Stop and release every tracked source, resetting the active count to 0.
void rt_game3d_audio_clear_sources(void *obj) {
    rt_game3d_audio *audio =
        game3d_audio_checked(obj, "Game3D.Sound3D.clearSources: invalid audio");
    if (!audio)
        return;
    game3d_audio_repair_sources(audio);
    for (int32_t i = 0; i < audio->source_count; ++i) {
        void *source = rt_g3d_checked_or_null(audio->sources[i], RT_G3D_SOUNDSOURCE3D_CLASS_ID);
        if (source)
            rt_soundsource3d_stop(source);
        game3d_release_typed_ref(&audio->sources[i], RT_G3D_SOUNDSOURCE3D_CLASS_ID);
    }
    audio->source_count = 0;
}
