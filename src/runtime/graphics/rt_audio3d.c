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
#include <stdint.h>

extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);
extern int64_t rt_sound_play_ex(void *sound, int64_t volume, int64_t pan);
extern void rt_voice_set_volume(int64_t voice, int64_t volume);
extern void rt_voice_set_pan(int64_t voice, int64_t pan);

/* Listener state (single listener) */
static double listener_pos[3] = {0, 0, 0};
static double listener_fwd[3] = {0, 0, -1};
static double listener_right[3] = {1, 0, 0};

/* Per-voice max_distance tracking — avoids global state pollution
 * when multiple sounds have different falloff ranges. */
#define MAX_3D_VOICES 64
static struct {
    int64_t voice_id;
    double max_distance;
} s_voice_dist[MAX_3D_VOICES];
static int32_t s_voice_dist_count = 0;

static void track_voice_distance(int64_t voice, double max_dist) {
    /* Update existing entry */
    for (int32_t i = 0; i < s_voice_dist_count; i++) {
        if (s_voice_dist[i].voice_id == voice) {
            s_voice_dist[i].max_distance = max_dist;
            return;
        }
    }
    /* Add new entry (overwrite oldest if full) */
    if (s_voice_dist_count < MAX_3D_VOICES) {
        s_voice_dist[s_voice_dist_count].voice_id = voice;
        s_voice_dist[s_voice_dist_count].max_distance = max_dist;
        s_voice_dist_count++;
    } else {
        /* Overwrite slot 0 (oldest) */
        s_voice_dist[0].voice_id = voice;
        s_voice_dist[0].max_distance = max_dist;
    }
}

static double lookup_voice_distance(int64_t voice) {
    for (int32_t i = 0; i < s_voice_dist_count; i++) {
        if (s_voice_dist[i].voice_id == voice)
            return s_voice_dist[i].max_distance;
    }
    return 50.0; /* default fallback */
}

/// @brief Set the 3D audio listener position and orientation.
/// @details Updates the single global listener used for all spatial audio
///          calculations. The right vector is derived from cross(forward, world_up)
///          where world_up is (0,1,0). All subsequent play_at and update_voice
///          calls use this listener state for attenuation and stereo panning.
/// @param position Vec3 world-space position of the listener (typically the camera).
/// @param forward  Vec3 forward direction the listener is facing.
void rt_audio3d_set_listener(void *position, void *forward) {
    if (!position || !forward)
        return;
    listener_pos[0] = rt_vec3_x(position);
    listener_pos[1] = rt_vec3_y(position);
    listener_pos[2] = rt_vec3_z(position);
    listener_fwd[0] = rt_vec3_x(forward);
    listener_fwd[1] = rt_vec3_y(forward);
    listener_fwd[2] = rt_vec3_z(forward);

    /* Compute right = cross(forward, world_up), normalized.
     * forward × (0,1,0) = (-fz, 0, fx) */
    double rx = -listener_fwd[2];
    double rz = listener_fwd[0];
    double rlen = sqrt(rx * rx + rz * rz);
    if (rlen > 1e-8) {
        rx /= rlen;
        rz /= rlen;
    }
    listener_right[0] = rx;
    listener_right[1] = 0.0;
    listener_right[2] = rz;
}

static void compute_3d_params(
    void *position, double max_dist, int64_t base_vol, int64_t *out_vol, int64_t *out_pan) {
    double sx = rt_vec3_x(position), sy = rt_vec3_y(position), sz = rt_vec3_z(position);
    double dx = sx - listener_pos[0], dy = sy - listener_pos[1], dz = sz - listener_pos[2];
    double dist = sqrt(dx * dx + dy * dy + dz * dz);

    /* Distance attenuation (linear) */
    double atten = (max_dist > 0.0) ? 1.0 - (dist / max_dist) : 1.0;
    if (atten < 0.0)
        atten = 0.0;
    *out_vol = (int64_t)(base_vol * atten);

    /* Pan from dot(direction_to_source, listener_right) */
    if (dist > 1e-8) {
        double ndx = dx / dist, ndz = dz / dist;
        double dot_right = ndx * listener_right[0] + ndz * listener_right[2];
        *out_pan = (int64_t)(dot_right * 100.0);
    } else {
        *out_pan = 0;
    }
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
    if (!sound || !position)
        return 0;

    int64_t vol, pan;
    compute_3d_params(position, max_distance, volume, &vol, &pan);
    int64_t voice = rt_sound_play_ex(sound, vol, pan);
    if (voice > 0)
        track_voice_distance(voice, max_distance);
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
    if (!position || voice <= 0)
        return;
    if (max_distance <= 0.0)
        max_distance = lookup_voice_distance(voice); /* per-voice fallback */
    int64_t vol, pan;
    compute_3d_params(position, max_distance, 100, &vol, &pan);
    rt_voice_set_volume(voice, vol);
    rt_voice_set_pan(voice, pan);
}

#endif /* VIPER_ENABLE_GRAPHICS */
