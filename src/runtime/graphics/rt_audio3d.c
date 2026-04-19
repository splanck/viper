//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_audio3d.c
// Purpose: Spatial audio — computes distance attenuation and stereo pan from
//   3D positions, then delegates to the existing 2D audio API.
//
// Key invariants:
//   - Listener position/forward stored as static globals (single listener).
//   - Attenuation: linear falloff from 0 to max_distance.
//   - Pan: dot product of source direction with listener's right vector.
//
// Links: rt_audio3d.h, rt_audio.h, rt_vec3.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_audio3d.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);
extern int64_t rt_sound_play_ex(void *sound, int64_t volume, int64_t pan);
extern void rt_voice_set_volume(int64_t voice, int64_t volume);
extern void rt_voice_set_pan(int64_t voice, int64_t pan);

static rt_audio3d_listener_state s_fallback_listener = {
    {0.0, 0.0, 0.0},
    {0.0, 0.0, -1.0},
    {1.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    1,
};
static rt_audio3d_listener_state s_active_listener = {
    {0.0, 0.0, 0.0},
    {0.0, 0.0, -1.0},
    {1.0, 0.0, 0.0},
    {0.0, 0.0, 0.0},
    0,
};
static int8_t s_has_active_listener = 0;

/* Per-voice max_distance tracking — avoids global state pollution
 * when multiple sounds have different falloff ranges. */
#define MAX_3D_VOICES 64

static struct {
    int64_t voice_id;
    double max_distance;
    int64_t base_volume;
} s_voice_dist[MAX_3D_VOICES];

static int32_t s_voice_dist_count = 0;

static int64_t clamp_i64(int64_t value, int64_t lo, int64_t hi) {
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return value;
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

/// @brief Normalise the listener forward vector and derive the matching right vector.
/// @details Falls back to forward = `(0, 0, -1)` when the input is degenerate
///          (zero or NULL). The right vector is computed as the XZ-plane
///          rotation of forward by -90° (`right = (-forward.z, 0, forward.x)`),
///          so even an upward-tilted forward yields a horizontal right
///          vector — appropriate for stereo panning where pitch shouldn't
///          affect left/right balance. A second degenerate fallback covers
///          the case where forward is nearly straight up/down (`right`
///          collapses to zero in XZ); right snaps to `+X` then.
static void audio3d_set_forward_and_right(rt_audio3d_listener_state *state, const double *forward) {
    double fx;
    double fy;
    double fz;
    double flen;
    double rx;
    double rz;
    double rlen;
    if (!state)
        return;

    fx = forward ? forward[0] : 0.0;
    fy = forward ? forward[1] : 0.0;
    fz = forward ? forward[2] : -1.0;
    flen = sqrt(fx * fx + fy * fy + fz * fz);
    if (flen <= 1e-8) {
        fx = 0.0;
        fy = 0.0;
        fz = -1.0;
        flen = 1.0;
    }

    state->forward[0] = fx / flen;
    state->forward[1] = fy / flen;
    state->forward[2] = fz / flen;

    rx = -state->forward[2];
    rz = state->forward[0];
    rlen = sqrt(rx * rx + rz * rz);
    if (rlen <= 1e-8) {
        rx = 1.0;
        rz = 0.0;
        rlen = 1.0;
    }
    state->right[0] = rx / rlen;
    state->right[1] = 0.0;
    state->right[2] = rz / rlen;
}

/// @brief Reset a listener-state struct to the canonical identity orientation.
/// Origin position, zero velocity, forward = -Z, right = +X. Marks the state as valid.
void rt_audio3d_listener_state_identity(rt_audio3d_listener_state *state) {
    if (!state)
        return;
    state->position[0] = 0.0;
    state->position[1] = 0.0;
    state->position[2] = 0.0;
    state->velocity[0] = 0.0;
    state->velocity[1] = 0.0;
    state->velocity[2] = 0.0;
    audio3d_set_forward_and_right(state, NULL);
    state->valid = 1;
}

/// @brief Populate a listener-state struct from explicit position/forward/velocity arrays.
/// Forward is normalized; the right vector is derived (perpendicular in the XZ plane).
/// Pass NULL for any component to default it to zero (or -Z forward).
void rt_audio3d_listener_state_set(rt_audio3d_listener_state *state,
                                   const double *position,
                                   const double *forward,
                                   const double *velocity) {
    if (!state)
        return;
    audio3d_copy3(state->position, position);
    audio3d_copy3(state->velocity, velocity);
    audio3d_set_forward_and_right(state, forward);
    state->valid = 1;
}

/// @brief Read the listener state currently driving spatial audio.
/// Returns the active AudioListener3D's state if one is bound; otherwise the
/// fallback listener configured via `rt_audio3d_set_listener`.
void rt_audio3d_get_effective_listener_state(rt_audio3d_listener_state *out_state) {
    if (!out_state)
        return;
    if (s_has_active_listener && s_active_listener.valid) {
        *out_state = s_active_listener;
        return;
    }
    *out_state = s_fallback_listener;
}

/// @brief Promote a listener-state snapshot to the active spatial-audio listener.
/// Called by AudioListener3D.Activate. Passing NULL or invalid state clears.
void rt_audio3d_set_active_listener_state(const rt_audio3d_listener_state *state) {
    if (!state || !state->valid) {
        rt_audio3d_clear_active_listener_state();
        return;
    }
    s_active_listener = *state;
    s_has_active_listener = 1;
}

/// @brief Detach any active AudioListener3D and revert to the fallback listener.
void rt_audio3d_clear_active_listener_state(void) {
    rt_audio3d_listener_state_identity(&s_active_listener);
    s_active_listener.valid = 0;
    s_has_active_listener = 0;
}

/// @brief Record per-voice 3D parameters (max distance + base volume) for later updates.
/// @details `rt_audio3d_update_voice` needs to recompute attenuation against
///          the same `max_distance` the voice was launched with, but the
///          underlying 2D audio API doesn't carry per-voice 3D state.
///          This table holds it. New voices append until the table fills,
///          then the oldest entry (slot 0) is overwritten — sufficient for
///          typical workloads (≤ 64 simultaneously moving 3D sounds) and
///          much cheaper than a heap-managed map.
static void track_voice_params(int64_t voice, double max_dist, int64_t base_volume) {
    /* Update existing entry */
    for (int32_t i = 0; i < s_voice_dist_count; i++) {
        if (s_voice_dist[i].voice_id == voice) {
            s_voice_dist[i].max_distance = max_dist;
            s_voice_dist[i].base_volume = base_volume;
            return;
        }
    }
    /* Add new entry (overwrite oldest if full) */
    if (s_voice_dist_count < MAX_3D_VOICES) {
        s_voice_dist[s_voice_dist_count].voice_id = voice;
        s_voice_dist[s_voice_dist_count].max_distance = max_dist;
        s_voice_dist[s_voice_dist_count].base_volume = base_volume;
        s_voice_dist_count++;
    } else {
        /* Overwrite slot 0 (oldest) */
        s_voice_dist[0].voice_id = voice;
        s_voice_dist[0].max_distance = max_dist;
        s_voice_dist[0].base_volume = base_volume;
    }
}

/// @brief Look up the recorded `max_distance` for a voice, or 50.0 if unknown.
static double lookup_voice_distance(int64_t voice) {
    for (int32_t i = 0; i < s_voice_dist_count; i++) {
        if (s_voice_dist[i].voice_id == voice)
            return s_voice_dist[i].max_distance;
    }
    return 50.0; /* default fallback */
}

/// @brief Look up the recorded base volume for a voice, or 100 (max) if unknown.
static int64_t lookup_voice_base_volume(int64_t voice) {
    for (int32_t i = 0; i < s_voice_dist_count; i++) {
        if (s_voice_dist[i].voice_id == voice)
            return s_voice_dist[i].base_volume;
    }
    return 100;
}

/// @brief Compute distance-attenuated volume and stereo pan for a 3D source.
/// @details Linear distance falloff: vol = base * (1 - dist/max_dist), clamped
/// to [0,100]. Pan = dot(source_direction, listener.right) * 100, clamped to
/// [-100, 100]. Sources at the listener position receive centered pan.
/// @param listener Active listener state (NULL → fallback).
/// @param source_position World-space xyz of the source.
/// @param max_dist Falloff radius; sources beyond this become silent.
/// @param base_vol Pre-attenuation volume (0–100).
/// @param out_vol Receives the attenuated volume.
/// @param out_pan Receives the stereo pan (-100 left, 0 center, 100 right).
void rt_audio3d_compute_voice_params(const rt_audio3d_listener_state *listener,
                                     const double *source_position,
                                     double max_dist,
                                     int64_t base_vol,
                                     int64_t *out_vol,
                                     int64_t *out_pan) {
    double sx;
    double sy;
    double sz;
    double dx;
    double dy;
    double dz;
    double dist;
    const rt_audio3d_listener_state *effective = listener ? listener : &s_fallback_listener;

    if (!source_position || !out_vol || !out_pan)
        return;

    sx = source_position[0];
    sy = source_position[1];
    sz = source_position[2];
    dx = sx - effective->position[0];
    dy = sy - effective->position[1];
    dz = sz - effective->position[2];
    dist = sqrt(dx * dx + dy * dy + dz * dz);

    double atten = (max_dist > 0.0) ? 1.0 - (dist / max_dist) : 1.0;
    if (atten < 0.0)
        atten = 0.0;
    *out_vol = clamp_i64((int64_t)(base_vol * atten), 0, 100);

    if (dist > 1e-8) {
        double ndx = dx / dist;
        double ndz = dz / dist;
        double dot_right = ndx * effective->right[0] + ndz * effective->right[2];
        *out_pan = clamp_i64((int64_t)(dot_right * 100.0), -100, 100);
    } else {
        *out_pan = 0;
    }
}

/// @brief Set the 3D audio listener position and orientation.
/// @details Updates the low-level compatibility listener used when no active
///          AudioListener3D object is driving the spatial-audio state.
/// @param position Vec3 world-space position of the listener (typically the camera).
/// @param forward  Vec3 forward direction the listener is facing.
void rt_audio3d_set_listener(void *position, void *forward) {
    double pos[3];
    double fwd[3];
    if (!position || !forward)
        return;
    audio3d_vec_from_obj(position, pos);
    audio3d_vec_from_obj(forward, fwd);
    rt_audio3d_listener_state_set(&s_fallback_listener, pos, fwd, NULL);
}

/// @brief Play a sound at a 3D position with distance-based attenuation and stereo pan.
/// @details Computes volume attenuation (linear falloff to max_distance) and
///          stereo pan (dot product with listener's right vector), then delegates
///          to the 2D audio play API. The voice's max_distance is tracked so
///          subsequent update_voice calls can re-attenuate as the source moves.
/// @param sound        Sound asset handle.
/// @param position     Vec3 world-space position of the sound source.
/// @param max_distance Distance at which the sound becomes inaudible (0 = infinite range).
/// @param volume       Base volume before attenuation [0–100].
/// @return Voice ID for subsequent updates, or 0 on failure.
int64_t rt_audio3d_play_at(void *sound, void *position, double max_distance, int64_t volume) {
    rt_audio3d_listener_state listener;
    double source_pos[3];
    if (!sound || !position)
        return 0;

    volume = clamp_i64(volume, 0, 100);
    audio3d_vec_from_obj(position, source_pos);
    rt_audio3d_get_effective_listener_state(&listener);
    int64_t vol = 0;
    int64_t pan = 0;
    rt_audio3d_compute_voice_params(&listener, source_pos, max_distance, volume, &vol, &pan);
    int64_t voice = rt_sound_play_ex(sound, vol, pan);
    if (voice > 0)
        track_voice_params(voice, max_distance, volume);
    return voice;
}

/// @brief Update a playing voice's volume and pan based on its current 3D position.
/// @details Call each frame for moving sound sources. Recomputes attenuation
///          and stereo pan relative to the current listener position. If
///          max_distance is 0, the value stored at play time is used instead.
/// @param voice        Voice ID returned by rt_audio3d_play_at.
/// @param position     Vec3 current world-space position of the sound source.
/// @param max_distance Falloff range override (0 = use value from play_at).
void rt_audio3d_update_voice(int64_t voice, void *position, double max_distance) {
    rt_audio3d_listener_state listener;
    double source_pos[3];
    if (!position || voice <= 0)
        return;
    if (max_distance <= 0.0)
        max_distance = lookup_voice_distance(voice); /* per-voice fallback */
    int64_t base_volume = lookup_voice_base_volume(voice);
    audio3d_vec_from_obj(position, source_pos);
    rt_audio3d_get_effective_listener_state(&listener);
    int64_t vol = 0;
    int64_t pan = 0;
    rt_audio3d_compute_voice_params(&listener, source_pos, max_distance, base_volume, &vol, &pan);
    rt_voice_set_volume(voice, vol);
    rt_voice_set_pan(voice, pan);
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
