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
static double saved_max_dist = 50.0;

/// @brief Perform audio3d set listener operation.
/// @param position
/// @param forward
void rt_audio3d_set_listener(void *position, void *forward)
{
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
    if (rlen > 1e-8)
    {
        rx /= rlen;
        rz /= rlen;
    }
    listener_right[0] = rx;
    listener_right[1] = 0.0;
    listener_right[2] = rz;
}

static void compute_3d_params(
    void *position, double max_dist, int64_t base_vol, int64_t *out_vol, int64_t *out_pan)
{
    double sx = rt_vec3_x(position), sy = rt_vec3_y(position), sz = rt_vec3_z(position);
    double dx = sx - listener_pos[0], dy = sy - listener_pos[1], dz = sz - listener_pos[2];
    double dist = sqrt(dx * dx + dy * dy + dz * dz);

    /* Distance attenuation (linear) */
    double atten = (max_dist > 0.0) ? 1.0 - (dist / max_dist) : 1.0;
    if (atten < 0.0)
        atten = 0.0;
    *out_vol = (int64_t)(base_vol * atten);

    /* Pan from dot(direction_to_source, listener_right) */
    if (dist > 1e-8)
    {
        double ndx = dx / dist, ndz = dz / dist;
        double dot_right = ndx * listener_right[0] + ndz * listener_right[2];
        *out_pan = (int64_t)(dot_right * 100.0);
    }
    else
    {
        *out_pan = 0;
    }
}

/// @brief Perform audio3d play at operation.
/// @param sound
/// @param position
/// @param max_distance
/// @param volume
/// @return Result value.
int64_t rt_audio3d_play_at(void *sound, void *position, double max_distance, int64_t volume)
{
    if (!sound || !position)
        return 0;
    saved_max_dist = max_distance;

    int64_t vol, pan;
    compute_3d_params(position, max_distance, volume, &vol, &pan);
    return rt_sound_play_ex(sound, vol, pan);
}

/// @brief Perform audio3d update voice operation.
/// @param voice
/// @param position
/// @param max_distance
void rt_audio3d_update_voice(int64_t voice, void *position, double max_distance)
{
    if (!position || voice <= 0)
        return;
    if (max_distance <= 0.0)
        max_distance = saved_max_dist; /* fallback to last play_at value */
    int64_t vol, pan;
    compute_3d_params(position, max_distance, 100, &vol, &pan);
    rt_voice_set_volume(voice, vol);
    rt_voice_set_pan(voice, pan);
}

#endif /* VIPER_ENABLE_GRAPHICS */
