//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
///
/// @file rt_graphics_media_stubs.c
/// @brief Graphics-disabled spatial audio, video player, and video widget
/// entry points.
///
/// @details This split source keeps media-related unavailable-backend symbols
/// together while preserving the original no-resource-allocation stub behavior.
///
// File: src/runtime/graphics/common/rt_graphics_media_stubs.c
// Purpose: Graphics-disabled spatial audio, video player, and video widget entry points.
//
// Key invariants:
//   - Compiled only for graphics-disabled runtime builds.
//   - Stateful graphics APIs fail with the shared InvalidOperation trap.
//   - Backend-independent query helpers keep their documented fallback values.
//
// Ownership/Lifetime:
//   - Stub entry points allocate no graphics resources and retain no handles.
//
// Links: src/runtime/graphics/common/rt_graphics_stubs_internal.h
//
//===----------------------------------------------------------------------===//

#include "rt_graphics_stubs_internal.h"

/* SpatialAudio3D and object-backed spatial-audio stubs */

/// @brief Stub for `SpatialAudio3D.SetListener` — set the active listener and
///        its forward orientation. Convenience wrapper around the
///        SoundListener3D class for callers that don't need full per-
///        listener state.
///
/// Silent no-op stub.
///
/// @param p Vec3 listener position (ignored).
/// @param f Vec3 listener forward direction (ignored).
void rt_sound3d_set_listener(void *p, void *f) {
    (void)p;
    (void)f;
}

void rt_sound3d_listener_state_identity(rt_sound3d_listener_state *state) {
    if (!state)
        return;
    state->position[0] = 0.0;
    state->position[1] = 0.0;
    state->position[2] = 0.0;
    state->forward[0] = 0.0;
    state->forward[1] = 0.0;
    state->forward[2] = -1.0;
    state->right[0] = 1.0;
    state->right[1] = 0.0;
    state->right[2] = 0.0;
    state->up[0] = 0.0;
    state->up[1] = 1.0;
    state->up[2] = 0.0;
    state->velocity[0] = 0.0;
    state->velocity[1] = 0.0;
    state->velocity[2] = 0.0;
    state->valid = 1;
}

void rt_sound3d_listener_state_set(rt_sound3d_listener_state *state,
                                   const double *position,
                                   const double *forward,
                                   const double *velocity) {
    rt_sound3d_listener_state_set_pose(state, position, forward, NULL, velocity);
}

void rt_sound3d_listener_state_set_pose(rt_sound3d_listener_state *state,
                                        const double *position,
                                        const double *forward,
                                        const double *up,
                                        const double *velocity) {
    if (!state)
        return;
    rt_sound3d_listener_state_identity(state);
    if (position) {
        state->position[0] = position[0];
        state->position[1] = position[1];
        state->position[2] = position[2];
    }
    if (forward) {
        state->forward[0] = forward[0];
        state->forward[1] = forward[1];
        state->forward[2] = forward[2];
    }
    if (up) {
        state->up[0] = up[0];
        state->up[1] = up[1];
        state->up[2] = up[2];
    }
    if (velocity) {
        state->velocity[0] = velocity[0];
        state->velocity[1] = velocity[1];
        state->velocity[2] = velocity[2];
    }
}

void rt_sound3d_get_effective_listener_state(rt_sound3d_listener_state *out_state) {
    rt_sound3d_listener_state_identity(out_state);
}

void rt_sound3d_set_active_listener_state(const rt_sound3d_listener_state *state) {
    (void)state;
}

void rt_sound3d_clear_active_listener_state(void) {}

void rt_sound3d_compute_voice_params_ex(const rt_sound3d_listener_state *listener,
                                        const double *source_position,
                                        const double *source_velocity,
                                        double ref_distance,
                                        double max_distance,
                                        int64_t base_volume,
                                        int64_t *out_volume,
                                        int64_t *out_pan,
                                        double *out_doppler) {
    (void)listener;
    (void)source_position;
    (void)source_velocity;
    (void)ref_distance;
    (void)max_distance;
    if (out_volume)
        *out_volume = base_volume;
    if (out_pan)
        *out_pan = 0;
    if (out_doppler)
        *out_doppler = 1.0;
}

void rt_sound3d_compute_voice_params(const rt_sound3d_listener_state *listener,
                                     const double *source_position,
                                     double max_distance,
                                     int64_t base_volume,
                                     int64_t *out_volume,
                                     int64_t *out_pan) {
    rt_sound3d_compute_voice_params_ex(
        listener, source_position, NULL, 0.0, max_distance, base_volume, out_volume, out_pan, NULL);
}

void rt_sound3d_register_voice_ex(int64_t v, double rd, double md, int64_t bv) {
    (void)v;
    (void)rd;
    (void)md;
    (void)bv;
}

void rt_sound3d_register_voice(int64_t v, double md, int64_t bv) {
    rt_sound3d_register_voice_ex(v, 0.0, md, bv);
}

/// @brief Stub for `SpatialAudio3D.PlayAt` — would normally play sound `s` at
///        world-space position `p` with maximum-distance attenuation
///        cutoff `d` and base volume `v`. Returns the assigned voice id,
///        or `0` on failure.
///
/// Silent stub returning `0`.
///
/// @param s Sound handle (ignored).
/// @param p Vec3 spawn position (ignored).
/// @param d Maximum audible distance in world units (ignored).
/// @param v Volume 0..100 (ignored).
///
/// @return `0`.
int64_t rt_sound3d_play_at(void *s, void *p, double d, int64_t v) {
    (void)s;
    (void)p;
    (void)d;
    (void)v;
    return 0;
}

/// @brief Stub for `SpatialAudio3D.UpdateVoice` — update the world-space
///        position and max-distance of an already-playing voice (use to
///        track moving emitters that aren't bound to a SceneNode3D).
///
/// Silent no-op stub.
///
/// @param v  Voice id from `PlayAt` (ignored).
/// @param p  Vec3 new position (ignored).
/// @param md New max audible distance (ignored).
void rt_sound3d_update_voice(int64_t v, void *p, double md) {
    (void)v;
    (void)p;
    (void)md;
}

void rt_sound3d_update_voice_ex(int64_t v, void *p, const double *sv, double rd, double md) {
    (void)v;
    (void)p;
    (void)sv;
    (void)rd;
    (void)md;
}

/// @brief Stub for `SpatialAudio3D.SyncBindings` — batch-update spatial
///        positions for every SoundListener3D and SoundSource3D bound to
///        a SceneNode3D or Camera3D. Should be called once per frame
///        before any draw calls so audio reflects the same world state.
///
/// Silent no-op stub.
///
/// @param dt Delta time in seconds (ignored).
void rt_sound3d_sync_bindings(double dt) {
    (void)dt;
}

/// @brief Stub for `SoundListener3D.New` — would normally create a
///        spatial-audio listener (the "ear" the mixer applies pan and
///        distance attenuation against). Typically one per scene.
///
/// Silent stub returning NULL.
///
/// @return `NULL`.
void *rt_soundlistener3d_new(void) {
    return NULL;
}

/// @brief Stub for `SoundListener3D.Position` — get the listener's
///        current world-space position as a Vec3.
///
/// Silent stub returning NULL.
///
/// @param l SoundListener3D handle (ignored).
///
/// @return `NULL`.
void *rt_soundlistener3d_get_position(void *l) {
    (void)l;
    return NULL;
}

/// @brief Stub for `SoundListener3D.SetPosition` — set the listener's
///        world-space position from a Vec3 handle.
///
/// Silent no-op stub.
///
/// @param l SoundListener3D handle (ignored).
/// @param p Vec3 position handle (ignored).
void rt_soundlistener3d_set_position(void *l, void *p) {
    (void)l;
    (void)p;
}

/// @brief Stub for `SoundListener3D.SetPositionXYZ` — set the listener's
///        world-space position from raw doubles. Convenience overload.
///
/// Silent no-op stub.
///
/// @param l SoundListener3D handle (ignored).
/// @param x World-space x (ignored).
/// @param y World-space y (ignored).
/// @param z World-space z (ignored).
void rt_soundlistener3d_set_position_vec(void *l, double x, double y, double z) {
    (void)l;
    (void)x;
    (void)y;
    (void)z;
}

/// @brief Stub for `SoundListener3D.Forward` — get the listener's
///        forward-facing direction vector (used for stereo panning math).
///
/// Silent stub returning NULL.
///
/// @param l SoundListener3D handle (ignored).
///
/// @return `NULL`.
void *rt_soundlistener3d_get_forward(void *l) {
    (void)l;
    return NULL;
}

/// @brief Stub for `SoundListener3D.SetForward` — set the listener's
///        forward direction (must be normalized).
///
/// Silent no-op stub.
///
/// @param l SoundListener3D handle (ignored).
/// @param f Vec3 forward direction (ignored).
void rt_soundlistener3d_set_forward(void *l, void *f) {
    (void)l;
    (void)f;
}

void *rt_soundlistener3d_get_up(void *l) {
    (void)l;
    return NULL;
}

void rt_soundlistener3d_set_up(void *l, void *u) {
    (void)l;
    (void)u;
}

/// @brief Stub for `SoundListener3D.Velocity` — get the listener's
///        velocity vector (used by the mixer for Doppler-effect math).
///
/// Silent stub returning NULL.
///
/// @param l SoundListener3D handle (ignored).
///
/// @return `NULL`.
void *rt_soundlistener3d_get_velocity(void *l) {
    (void)l;
    return NULL;
}

/// @brief Stub for `SoundListener3D.SetVelocity` — set the listener's
///        velocity vector for Doppler computation.
///
/// Silent no-op stub.
///
/// @param l SoundListener3D handle (ignored).
/// @param v Vec3 velocity handle (ignored).
void rt_soundlistener3d_set_velocity(void *l, void *v) {
    (void)l;
    (void)v;
}

/// @brief Stub for `SoundListener3D.IsActive` — true when this listener
///        is the currently-selected listener for the audio mixer
///        (multiple listeners may exist; only one is active at a time).
///
/// Silent stub returning `0`.
///
/// @param l SoundListener3D handle (ignored).
///
/// @return `0`.
int8_t rt_soundlistener3d_get_is_active(void *l) {
    (void)l;
    return 0;
}

/// @brief Stub for `SoundListener3D.SetActive` — designate this
///        listener as the active one (deactivates any previously-active
///        listener).
///
/// Silent no-op stub.
///
/// @param l SoundListener3D handle (ignored).
/// @param a Non-zero to make this listener active (ignored).
void rt_soundlistener3d_set_is_active(void *l, int8_t a) {
    (void)l;
    (void)a;
}

/// @brief Stub for `SoundListener3D.BindNode` — attach a SceneNode3D so
///        the listener's position/orientation follow the node each frame.
///        Use `BindCamera` for the typical first-person setup.
///
/// Silent no-op stub.
///
/// @param l SoundListener3D handle (ignored).
/// @param n SceneNode3D handle, or NULL to detach (ignored).
void rt_soundlistener3d_bind_node(void *l, void *n) {
    (void)l;
    (void)n;
}

/// @brief Stub for `SoundListener3D.ClearNodeBinding` — detach the
///        SceneNode3D currently driving the listener's spatial position.
///
/// Silent no-op stub.
///
/// @param l SoundListener3D handle (ignored).
void rt_soundlistener3d_clear_node_binding(void *l) {
    (void)l;
}

/// @brief Stub for `SoundListener3D.BindCamera` — attach a Camera3D so
///        the listener's position and forward vector follow the camera
///        each frame (typical first-person audio setup).
///
/// Silent no-op stub.
///
/// @param l SoundListener3D handle (ignored).
/// @param c Camera3D handle, or NULL to detach (ignored).
void rt_soundlistener3d_bind_camera(void *l, void *c) {
    (void)l;
    (void)c;
}

/// @brief Stub for `SoundListener3D.ClearCameraBinding` — detach the
///        Camera3D currently driving the listener.
///
/// Silent no-op stub.
///
/// @param l SoundListener3D handle (ignored).
void rt_soundlistener3d_clear_camera_binding(void *l) {
    (void)l;
}

/// @brief Stub for `SoundSource3D.New` — would normally create a 3D
///        positional emitter for the given Sound resource.
///
/// Silent stub returning NULL.
///
/// @param s Sound handle (ignored).
///
/// @return `NULL`.
void *rt_soundsource3d_new(void *s) {
    (void)s;
    return NULL;
}

/// @brief Stub for `SoundSource3D.Position` — get the source's current
///        world-space position as a Vec3.
///
/// Silent stub returning NULL.
///
/// @param s SoundSource3D handle (ignored).
///
/// @return `NULL`.
void *rt_soundsource3d_get_position(void *s) {
    (void)s;
    return NULL;
}

/// @brief Stub for `SoundSource3D.SetPosition` — set the source's
///        world-space position from a Vec3 handle.
///
/// Silent no-op stub.
///
/// @param s SoundSource3D handle (ignored).
/// @param p Vec3 position handle (ignored).
void rt_soundsource3d_set_position(void *s, void *p) {
    (void)s;
    (void)p;
}

/// @brief Stub for `SoundSource3D.SetPositionXYZ` — set the source's
///        world-space position from raw doubles. Convenience overload.
///
/// Silent no-op stub.
///
/// @param s SoundSource3D handle (ignored).
/// @param x World-space x (ignored).
/// @param y World-space y (ignored).
/// @param z World-space z (ignored).
void rt_soundsource3d_set_position_vec(void *s, double x, double y, double z) {
    (void)s;
    (void)x;
    (void)y;
    (void)z;
}

/// @brief Stub for `SoundSource3D.Velocity` — get the source's current
///        velocity vector. Used by the mixer for Doppler-effect calculations.
///
/// Silent stub returning NULL.
///
/// @param s SoundSource3D handle (ignored).
///
/// @return `NULL`.
void *rt_soundsource3d_get_velocity(void *s) {
    (void)s;
    return NULL;
}

/// @brief Stub for `SoundSource3D.SetVelocity` — set the source's
///        velocity vector for Doppler computation.
///
/// Silent no-op stub.
///
/// @param s SoundSource3D handle (ignored).
/// @param v Vec3 velocity handle (ignored).
void rt_soundsource3d_set_velocity(void *s, void *v) {
    (void)s;
    (void)v;
}

double rt_soundsource3d_get_doppler_factor(void *s) {
    (void)s;
    return 1.0;
}

/// @brief Stub for `SoundSource3D.MaxDistance` — get the cutoff distance
///        beyond which the source is silent (inverse-distance attenuation
///        upper bound).
///
/// Silent stub returning `0.0`.
///
/// @param s SoundSource3D handle (ignored).
///
/// @return `0.0`.
double rt_soundsource3d_get_max_distance(void *s) {
    (void)s;
    return 0.0;
}

/// @brief Stub for `SoundSource3D.SetMaxDistance` — set the cutoff
///        distance for attenuation.
///
/// Silent no-op stub.
///
/// @param s SoundSource3D handle (ignored).
/// @param d Max distance in world units (ignored).
void rt_soundsource3d_set_max_distance(void *s, double d) {
    (void)s;
    (void)d;
}

/// @brief Stub for `SoundSource3D.RefDistance` — get the full-volume radius.
double rt_soundsource3d_get_ref_distance(void *s) {
    (void)s;
    return 0.0;
}

/// @brief Stub for `SoundSource3D.SetRefDistance` — set the full-volume radius.
void rt_soundsource3d_set_ref_distance(void *s, double d) {
    (void)s;
    (void)d;
}

/// @brief Stub for `SoundSource3D.Volume` — get the per-source gain
///        multiplier (0..100, applied after distance attenuation).
///
/// Silent stub returning `0`.
///
/// @param s SoundSource3D handle (ignored).
///
/// @return `0`.
int64_t rt_soundsource3d_get_volume(void *s) {
    (void)s;
    return 0;
}

/// @brief Stub for `SoundSource3D.SetVolume` — set the per-source gain
///        multiplier.
///
/// Silent no-op stub.
///
/// @param s SoundSource3D handle (ignored).
/// @param v Volume 0..100 (ignored).
void rt_soundsource3d_set_volume(void *s, int64_t v) {
    (void)s;
    (void)v;
}

/// @brief Stub for `SoundSource3D.Looping` — get the looping flag.
///
/// Silent stub returning `0`.
///
/// @param s SoundSource3D handle (ignored).
///
/// @return `0`.
int8_t rt_soundsource3d_get_looping(void *s) {
    (void)s;
    return 0;
}

/// @brief Stub for `SoundSource3D.SetLooping` — when enabled, the
///        underlying voice loops at the end of the sound buffer.
///
/// Silent no-op stub.
///
/// @param s SoundSource3D handle (ignored).
/// @param l Non-zero to enable looping (ignored).
void rt_soundsource3d_set_looping(void *s, int8_t l) {
    (void)s;
    (void)l;
}

/// @brief Stub for `SoundSource3D.IsPlaying` — true while the active
///        voice for this source is producing audio.
///
/// Silent stub returning `0`.
///
/// @param s SoundSource3D handle (ignored).
///
/// @return `0`.
int8_t rt_soundsource3d_get_is_playing(void *s) {
    (void)s;
    return 0;
}

/// @brief Stub for `SoundSource3D.VoiceId` — get the underlying mixer
///        voice handle (`0` if not playing). Useful for debugging.
///
/// Silent stub returning `0`.
///
/// @param s SoundSource3D handle (ignored).
///
/// @return `0`.
int64_t rt_soundsource3d_get_voice_id(void *s) {
    (void)s;
    return 0;
}

/// @brief Stub for `SoundSource3D.Play` — start playback of the bound
///        Sound at the source's current position. Returns the assigned
///        voice id, or `0` on failure.
///
/// Silent stub returning `0`.
///
/// @param s SoundSource3D handle (ignored).
///
/// @return `0`.
int64_t rt_soundsource3d_play(void *s) {
    (void)s;
    return 0;
}

/// @brief Stub for `SoundSource3D.Stop` — halt the source's current
///        voice immediately. Safe no-op when not playing.
///
/// Silent no-op stub.
///
/// @param s SoundSource3D handle (ignored).
void rt_soundsource3d_stop(void *s) {
    (void)s;
}

/// @brief Stub for `SoundSource3D.BindNode` — attach a SceneNode3D so
///        the source's position follows the node each frame
///        (`SpatialAudio3D.SyncBindings(dt)`).
///
/// Silent no-op stub.
///
/// @param s SoundSource3D handle (ignored).
/// @param n SceneNode3D handle, or NULL to detach (ignored).
void rt_soundsource3d_bind_node(void *s, void *n) {
    (void)s;
    (void)n;
}

/// @brief Stub for `SoundSource3D.ClearNodeBinding` — detach the
///        SceneNode3D currently driving the source's position.
///
/// Silent no-op stub.
///
/// @param s SoundSource3D handle (ignored).
void rt_soundsource3d_clear_node_binding(void *s) {
    (void)s;
}

/* VideoWidget stubs */

/// @brief Stub for `VideoWidget.New` — would normally create a GUI
///        Image widget bound to a VideoPlayer that decodes the file at
///        `path`. The widget's pixels are refreshed each `Update` call
///        with the latest decoded video frame.
///
/// Silent stub returning NULL.
///
/// @param p    Parent widget handle (ignored).
/// @param path Filesystem path to the video file (ignored).
///
/// @return `NULL`.
void *rt_videowidget_new(void *p, rt_string path) {
    (void)p;
    (void)path;
    return NULL;
}

/// @brief Stub for `VideoWidget.Destroy` — would normally destroy the
///        widget subtree and release the owned VideoPlayer immediately.
///
/// Silent no-op stub.
///
/// @param v VideoWidget handle (ignored).
void rt_videowidget_destroy(void *v) {
    (void)v;
}

/// @brief Stub for `VideoWidget.Play` — start or resume video playback
///        (delegates to the embedded VideoPlayer).
///
/// Silent no-op stub.
///
/// @param v VideoWidget handle (ignored).
void rt_videowidget_play(void *v) {
    (void)v;
}

/// @brief Stub for `VideoWidget.Pause` — pause playback. The widget
///        continues to display the current frame.
///
/// Silent no-op stub.
///
/// @param v VideoWidget handle (ignored).
void rt_videowidget_pause(void *v) {
    (void)v;
}

/// @brief Stub for `VideoWidget.Stop` — stop playback and rewind to
///        frame 0. The widget displays the first frame (or a placeholder
///        if no frame decoded yet).
///
/// Silent no-op stub.
///
/// @param v VideoWidget handle (ignored).
void rt_videowidget_stop(void *v) {
    (void)v;
}

/// @brief Stub for `VideoWidget.Update` — advance video playback by
///        `dt` seconds and refresh the widget's image. Should be called
///        once per GUI frame (typically from the app's frame callback).
///
/// Silent no-op stub.
///
/// @param v  VideoWidget handle (ignored).
/// @param dt Delta time in seconds (ignored).
void rt_videowidget_update(void *v, double dt) {
    (void)v;
    (void)dt;
}

/// @brief Stub for `VideoWidget.SetShowControls` — toggle the overlay
///        playback controls (play/pause button, scrubber bar) inside
///        the widget bounds.
///
/// Silent no-op stub.
///
/// @param v VideoWidget handle (ignored).
/// @param s Non-zero to show controls (ignored).
void rt_videowidget_set_show_controls(void *v, int8_t s) {
    (void)v;
    (void)s;
}

int64_t rt_videowidget_get_show_controls(void *v) {
    (void)v;
    return 0;
}

/// @brief Stub for `VideoWidget.SetLoop` — when enabled, the widget
///        seeks back to frame 0 on EOF and continues playback (no
///        explicit `Play` call needed).
///
/// Silent no-op stub.
///
/// @param v VideoWidget handle (ignored).
/// @param l Non-zero to enable looping (ignored).
void rt_videowidget_set_loop(void *v, int8_t l) {
    (void)v;
    (void)l;
}

int64_t rt_videowidget_get_loop(void *v) {
    (void)v;
    return 0;
}

/// @brief Stub for `VideoWidget.SetVolume` — set the audio track's
///        playback volume, 0..1. Delegates to the embedded VideoPlayer.
///
/// Silent no-op stub.
///
/// @param v   VideoWidget handle (ignored).
/// @param vol Volume, 0..1 (ignored).
void rt_videowidget_set_volume(void *v, double vol) {
    (void)v;
    (void)vol;
}

/// @brief Stub for `VideoWidget.IsPlaying` — true while the widget's
///        embedded VideoPlayer is actively decoding frames.
///
/// Silent stub returning `0`.
///
/// @param v VideoWidget handle (ignored).
///
/// @return `0`.
int64_t rt_videowidget_get_is_playing(void *v) {
    (void)v;
    return 0;
}

/// @brief Stub for `VideoWidget.Position` — get the current playback
///        position in seconds.
///
/// Silent stub returning `0.0`.
///
/// @param v VideoWidget handle (ignored).
///
/// @return `0.0`.
double rt_videowidget_get_position(void *v) {
    (void)v;
    return 0.0;
}

/// @brief Stub for `VideoWidget.Duration` — get the total length of the
///        loaded video in seconds.
///
/// Silent stub returning `0.0`.
///
/// @param v VideoWidget handle (ignored).
///
/// @return `0.0`.
double rt_videowidget_get_duration(void *v) {
    (void)v;
    return 0.0;
}

void *rt_videowidget_get_root(void *v) {
    (void)v;
    return NULL;
}

void rt_videowidget_set_visible(void *v, int64_t visible) {
    (void)v;
    (void)visible;
}

void rt_videowidget_set_enabled(void *v, int64_t enabled) {
    (void)v;
    (void)enabled;
}

void rt_videowidget_set_size(void *v, int64_t width, int64_t height) {
    (void)v;
    (void)width;
    (void)height;
}

void rt_videowidget_set_preferred_size(void *v, double width, double height) {
    (void)v;
    (void)width;
    (void)height;
}

void rt_videowidget_set_max_size(void *v, double width, double height) {
    (void)v;
    (void)width;
    (void)height;
}

void rt_videowidget_set_flex(void *v, double flex) {
    (void)v;
    (void)flex;
}

void rt_videowidget_set_margin(void *v, int64_t margin) {
    (void)v;
    (void)margin;
}

void rt_videowidget_set_position(void *v, int64_t x, int64_t y) {
    (void)v;
    (void)x;
    (void)y;
}

void rt_videowidget_add_child(void *v, void *child) {
    (void)v;
    (void)child;
}

/* VideoPlayer stubs */

/// @brief Stub for `VideoPlayer.Open` — would normally parse the video
///        file at `p` (.avi via MJPEG decoder, or .ogv via Theora
///        infrastructure) and return a player ready to `Play`.
///
/// Silent stub returning NULL.
///
/// @param p Filesystem path to the video file (ignored).
///
/// @return `NULL`.
void *rt_videoplayer_open(rt_string p) {
    (void)p;
    return NULL;
}

/// @brief Stub for `VideoPlayer.Play` — start or resume playback.
///        From a stopped state, restarts at frame 0; from a paused state,
///        resumes from the current position.
///
/// Silent no-op stub.
///
/// @param v VideoPlayer handle (ignored).
void rt_videoplayer_play(void *v) {
    (void)v;
}

/// @brief Stub for `VideoPlayer.Pause` — pause playback. Frames stop
///        decoding but the audio cursor and `Position` are preserved.
///        Resume with `Play`.
///
/// Silent no-op stub.
///
/// @param v VideoPlayer handle (ignored).
void rt_videoplayer_pause(void *v) {
    (void)v;
}

/// @brief Stub for `VideoPlayer.Stop` — stop playback and rewind to
///        frame 0. Drops any decoded frames in the queue.
///
/// Silent no-op stub.
///
/// @param v VideoPlayer handle (ignored).
void rt_videoplayer_stop(void *v) {
    (void)v;
}

/// @brief Stub for `VideoPlayer.Seek` — jump to time position `s` in
///        seconds. Audio resyncs immediately; video resyncs to the next
///        keyframe (with a brief catch-up decode).
///
/// Silent no-op stub.
///
/// @param v VideoPlayer handle (ignored).
/// @param s Target time in seconds (ignored).
void rt_videoplayer_seek(void *v, double s) {
    (void)v;
    (void)s;
}

/// @brief Stub for `VideoPlayer.Update` — advance video playback by `dt`
///        seconds: decode pending video frames, advance audio cursor,
///        maintain A/V sync.
///
/// Silent no-op stub.
///
/// @param v  VideoPlayer handle (ignored).
/// @param dt Delta time in seconds (ignored).
void rt_videoplayer_update(void *v, double dt) {
    (void)v;
    (void)dt;
}

/// @brief Stub for `VideoPlayer.SetVolume` — set the audio track's
///        playback volume, 0..1.
///
/// Silent no-op stub.
///
/// @param v   VideoPlayer handle (ignored).
/// @param vol Volume, 0..1 (ignored).
void rt_videoplayer_set_volume(void *v, double vol) {
    (void)v;
    (void)vol;
}

/// @brief Stub for `VideoPlayer.Width` — get the video's frame width in
///        pixels (parsed from the stream header at `Open` time).
///
/// Silent stub returning `0`.
///
/// @param v VideoPlayer handle (ignored).
///
/// @return `0`.
int64_t rt_videoplayer_get_width(void *v) {
    (void)v;
    return 0;
}

/// @brief Stub for `VideoPlayer.Height` — get the video's frame height
///        in pixels.
///
/// Silent stub returning `0`.
///
/// @param v VideoPlayer handle (ignored).
///
/// @return `0`.
int64_t rt_videoplayer_get_height(void *v) {
    (void)v;
    return 0;
}

/// @brief Stub for `VideoPlayer.Duration` — get the total length of the
///        video in seconds.
///
/// Silent stub returning `0.0`.
///
/// @param v VideoPlayer handle (ignored).
///
/// @return `0.0`.
double rt_videoplayer_get_duration(void *v) {
    (void)v;
    return 0.0;
}

/// @brief Stub for `VideoPlayer.Position` — get the current playback
///        position in seconds.
///
/// Silent stub returning `0.0`.
///
/// @param v VideoPlayer handle (ignored).
///
/// @return `0.0`.
double rt_videoplayer_get_position(void *v) {
    (void)v;
    return 0.0;
}

/// @brief Stub for `VideoPlayer.IsPlaying` — true while playback is
///        actively advancing (not paused, not stopped, not at EOF).
///
/// Silent stub returning `0`.
///
/// @param v VideoPlayer handle (ignored).
///
/// @return `0`.
int64_t rt_videoplayer_get_is_playing(void *v) {
    (void)v;
    return 0;
}

/// @brief Stub for `VideoPlayer.Frame` — get the most recently decoded
///        video frame as a Pixels surface (RGBA, frame size).
///
/// Silent stub returning NULL.
///
/// @param v VideoPlayer handle (ignored).
///
/// @return `NULL`.
void *rt_videoplayer_get_frame(void *v) {
    (void)v;
    return NULL;
}
