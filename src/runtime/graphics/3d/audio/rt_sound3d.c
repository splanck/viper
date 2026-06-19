//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/audio/rt_sound3d.c
// Purpose: Spatial audio backing `Viper.Sound.SpatialAudio3D` — computes distance
//   attenuation and stereo pan from 3D positions, then delegates to the
//   existing 2D audio API for actual sample playback.
//
// Key invariants:
//   - Listener position/orientation stored as static globals (single listener).
//   - The active SoundListener3D wins; if none is bound the fallback listener
//     (modifiable via rt_sound3d_set_listener) is used.
//   - Attenuation: linear falloff from 0 to max_distance.
//   - Pan: dot product of source direction with listener's right vector.
//   - Right vector is derived from forward and up, so caller-provided roll is
//     reflected in stereo balance.
//
// Ownership/Lifetime:
//   - Listener-state structs are caller-owned; this file keeps process-global
//     copies of the active and fallback listeners.
//   - Per-voice max-distance entries live in a fixed-size 64-slot table that
//     overwrites the oldest entry when full.
//
// Links: src/runtime/graphics/3d/audio/rt_sound3d.h (public API),
//        src/runtime/audio/rt_audio.h (underlying 2D playback),
//        src/runtime/graphics/math/rt_vec3.h (Vec3 handle accessors)
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_sound3d.h"

#include "rt_game3d_diagnostics.h"
#include "rt_platform.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);
extern int64_t rt_sound_play_ex(void *sound, int64_t volume, int64_t pan);
extern void rt_voice_set_volume(int64_t voice, int64_t volume);
extern void rt_voice_set_pan(int64_t voice, int64_t pan);
extern int64_t rt_voice_is_playing(int64_t voice);

static rt_sound3d_listener_state s_fallback_listener = {
    {0.0, 0.0, 0.0},
    {0.0, 0.0, -1.0},
    {1.0, 0.0, 0.0},
    {0.0, 1.0, 0.0},
    {0.0, 0.0, 0.0},
    1,
};
static rt_sound3d_listener_state s_active_listener = {
    {0.0, 0.0, 0.0},
    {0.0, 0.0, -1.0},
    {1.0, 0.0, 0.0},
    {0.0, 1.0, 0.0},
    {0.0, 0.0, 0.0},
    0,
};
static int8_t s_has_active_listener = 0;

/* Per-voice max_distance tracking — avoids global state pollution
 * when multiple sounds have different falloff ranges. */
#define MAX_3D_VOICES 64
#define SOUND3D_COORD_ABS_MAX 1000000000000.0
#define SOUND3D_VELOCITY_ABS_MAX 1000000.0
#define SOUND3D_DISTANCE_MAX 1000000000.0

static struct {
    int64_t voice_id;
    double ref_distance;
    double max_distance;
    int64_t base_volume;
} s_voice_dist[MAX_3D_VOICES];

static int32_t s_voice_dist_count = 0;
static int32_t s_voice_dist_next = 0;
static int8_t s_voice_dist_test_force_all_playing = 0;

void rt_sound3d_test_set_all_voices_playing(int8_t enabled) {
    RT_ASSERT_MAIN_THREAD();
    s_voice_dist_test_force_all_playing = enabled ? 1 : 0;
}

/// @brief Clamp @p value into the inclusive `[lo, hi]` range.
static int64_t clamp_i64(int64_t value, int64_t lo, int64_t hi) {
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return value;
}

/// @brief Return @p value when finite, else @p fallback.
/// @details Defensive guard for spatial audio math against NaN/inf inputs.
static double finite_or(double value, double fallback) {
    return isfinite(value) ? value : fallback;
}

/// @brief Clamp a finite scalar to +/- @p max_abs, substituting @p fallback for NaN/Inf.
static double clamp_abs_or(double value, double fallback, double max_abs) {
    value = finite_or(value, fallback);
    if (value < -max_abs)
        return -max_abs;
    if (value > max_abs)
        return max_abs;
    return value;
}

/// @brief Clamp an attenuation distance while preserving invalid-value fallback behavior.
static double sound3d_distance_or(double value, double fallback) {
    value = finite_or(value, fallback);
    if (value < 0.0)
        value = fallback;
    if (value > SOUND3D_DISTANCE_MAX)
        return SOUND3D_DISTANCE_MAX;
    return value;
}

/// @brief Clamp one velocity component before Doppler math.
static double sound3d_velocity_or(double value) {
    return clamp_abs_or(value, 0.0, SOUND3D_VELOCITY_ABS_MAX);
}

/// @brief Copy a 3-component vector with null-safe zero fill.
/// @details `dst == NULL` is a silent no-op so callers can pass through
///   optional out-params without null-checking at every site. `src == NULL`
///   writes zero rather than leaving `dst` untouched — spatial audio math
///   treats missing positions as the origin, not undefined.
static void sound3d_copy3(double *dst, const double *src) {
    if (!dst)
        return;
    if (!src) {
        dst[0] = 0.0;
        dst[1] = 0.0;
        dst[2] = 0.0;
        return;
    }
    dst[0] = clamp_abs_or(src[0], 0.0, SOUND3D_COORD_ABS_MAX);
    dst[1] = clamp_abs_or(src[1], 0.0, SOUND3D_COORD_ABS_MAX);
    dst[2] = clamp_abs_or(src[2], 0.0, SOUND3D_COORD_ABS_MAX);
}

/// @brief Decode an `rt_vec3` object handle into three raw doubles.
/// @details The runtime stores Vec3 as a GC-managed object accessed through
///   `rt_vec3_{x,y,z}`; this helper converts it into the plain array that
///   the spatial-audio math works in. Null object collapses to the origin,
///   matching `sound3d_copy3`'s null-fill convention.
static void sound3d_vec_from_obj(void *vec, double *out_xyz) {
    if (!out_xyz)
        return;
    if (!vec) {
        out_xyz[0] = 0.0;
        out_xyz[1] = 0.0;
        out_xyz[2] = 0.0;
        return;
    }
    out_xyz[0] = clamp_abs_or(rt_vec3_x(vec), 0.0, SOUND3D_COORD_ABS_MAX);
    out_xyz[1] = clamp_abs_or(rt_vec3_y(vec), 0.0, SOUND3D_COORD_ABS_MAX);
    out_xyz[2] = clamp_abs_or(rt_vec3_z(vec), 0.0, SOUND3D_COORD_ABS_MAX);
}

/// @brief Cross product out = a x b for 3-vectors.
static void sound3d_cross3(const double *a, const double *b, double *out) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

/// @brief Normalize a 3-vector in place (sanitizing non-finite components first).
/// @return 1 on success, 0 if the vector is degenerate (length <= 1e-8), leaving it unchanged.
static int sound3d_normalize3(double *v) {
    double len;
    if (!v)
        return 0;
    v[0] = finite_or(v[0], 0.0);
    v[1] = finite_or(v[1], 0.0);
    v[2] = finite_or(v[2], 0.0);
    len = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (!isfinite(len) || len <= 1e-8)
        return 0;
    v[0] /= len;
    v[1] /= len;
    v[2] /= len;
    return 1;
}

/// @brief Normalise the listener basis from forward/up.
/// @details Falls back to forward = `(0, 0, -1)` and up = `(0, 1, 0)` when
///   inputs are missing or degenerate, then orthonormalizes the basis. Right
///   is `forward x up`, so a caller-provided roll affects stereo panning.
static void sound3d_set_basis(rt_sound3d_listener_state *state,
                              const double *forward,
                              const double *up) {
    double fwd[3];
    double upv[3];
    double right[3];
    if (!state)
        return;

    fwd[0] = forward ? finite_or(forward[0], 0.0) : 0.0;
    fwd[1] = forward ? finite_or(forward[1], 0.0) : 0.0;
    fwd[2] = forward ? finite_or(forward[2], -1.0) : -1.0;
    if (!sound3d_normalize3(fwd)) {
        fwd[0] = 0.0;
        fwd[1] = 0.0;
        fwd[2] = -1.0;
    }

    upv[0] = up ? finite_or(up[0], 0.0) : 0.0;
    upv[1] = up ? finite_or(up[1], 1.0) : 1.0;
    upv[2] = up ? finite_or(up[2], 0.0) : 0.0;
    if (!sound3d_normalize3(upv)) {
        upv[0] = 0.0;
        upv[1] = 1.0;
        upv[2] = 0.0;
    }

    sound3d_cross3(fwd, upv, right);
    if (!sound3d_normalize3(right)) {
        upv[0] = fabs(fwd[1]) < 0.95 ? 0.0 : 1.0;
        upv[1] = fabs(fwd[1]) < 0.95 ? 1.0 : 0.0;
        upv[2] = 0.0;
        sound3d_cross3(fwd, upv, right);
        if (!sound3d_normalize3(right)) {
            right[0] = 1.0;
            right[1] = 0.0;
            right[2] = 0.0;
        }
    }

    sound3d_cross3(right, fwd, upv);
    if (!sound3d_normalize3(upv)) {
        upv[0] = 0.0;
        upv[1] = 1.0;
        upv[2] = 0.0;
    }

    state->forward[0] = fwd[0];
    state->forward[1] = fwd[1];
    state->forward[2] = fwd[2];
    state->right[0] = right[0];
    state->right[1] = right[1];
    state->right[2] = right[2];
    state->up[0] = upv[0];
    state->up[1] = upv[1];
    state->up[2] = upv[2];
}

/// @brief Reset a listener-state struct to the canonical identity orientation.
/// Origin position, zero velocity, forward = -Z, right = +X, up = +Y. Marks the state as valid.
void rt_sound3d_listener_state_identity(rt_sound3d_listener_state *state) {
    if (!state)
        return;
    state->position[0] = 0.0;
    state->position[1] = 0.0;
    state->position[2] = 0.0;
    state->velocity[0] = 0.0;
    state->velocity[1] = 0.0;
    state->velocity[2] = 0.0;
    sound3d_set_basis(state, NULL, NULL);
    state->valid = 1;
}

/// @brief Populate a listener-state struct from explicit position/forward/velocity arrays.
/// Forward is normalized; up defaults to +Y and the full basis is orthonormalized.
/// Pass NULL for any component to default it to zero (or -Z forward).
void rt_sound3d_listener_state_set(rt_sound3d_listener_state *state,
                                   const double *position,
                                   const double *forward,
                                   const double *velocity) {
    rt_sound3d_listener_state_set_pose(state, position, forward, NULL, velocity);
}

/// @brief Populate a listener-state struct from explicit position/forward/up/velocity arrays.
void rt_sound3d_listener_state_set_pose(rt_sound3d_listener_state *state,
                                        const double *position,
                                        const double *forward,
                                        const double *up,
                                        const double *velocity) {
    if (!state)
        return;
    sound3d_copy3(state->position, position);
    sound3d_copy3(state->velocity, velocity);
    sound3d_set_basis(state, forward, up);
    state->valid = 1;
}

/// @brief Read the listener state currently driving spatial audio.
/// Returns the active SoundListener3D's state if one is bound; otherwise the
/// fallback listener configured via `rt_sound3d_set_listener`.
void rt_sound3d_get_effective_listener_state(rt_sound3d_listener_state *out_state) {
    if (!out_state)
        return;
    if (s_has_active_listener && s_active_listener.valid) {
        *out_state = s_active_listener;
        return;
    }
    *out_state = s_fallback_listener;
}

/// @brief Promote a listener-state snapshot to the active spatial-audio listener.
/// Called by SoundListener3D.Activate. Passing NULL or invalid state clears.
void rt_sound3d_set_active_listener_state(const rt_sound3d_listener_state *state) {
    RT_ASSERT_MAIN_THREAD();
    if (!state || !state->valid) {
        rt_sound3d_clear_active_listener_state();
        return;
    }
    s_active_listener = *state;
    s_has_active_listener = 1;
}

/// @brief Detach any active SoundListener3D and revert to the fallback listener.
void rt_sound3d_clear_active_listener_state(void) {
    RT_ASSERT_MAIN_THREAD();
    rt_sound3d_listener_state_identity(&s_active_listener);
    s_active_listener.valid = 0;
    s_has_active_listener = 0;
}

/// @brief Record per-voice 3D parameters (max distance + base volume) for later updates.
/// @details `rt_sound3d_update_voice` needs to recompute attenuation against
///          the same `max_distance` the voice was launched with, but the
///          underlying 2D audio API doesn't carry per-voice 3D state.
///          This table holds it. New voices append until the table fills,
///          then the oldest entry (slot 0) is overwritten — sufficient for
///          typical workloads (≤ 64 simultaneously moving 3D sounds) and
///          much cheaper than a heap-managed map.
void rt_sound3d_register_voice_ex(int64_t voice,
                                  double ref_dist,
                                  double max_dist,
                                  int64_t base_volume) {
    RT_ASSERT_MAIN_THREAD();
    if (voice <= 0)
        return;
    ref_dist = sound3d_distance_or(ref_dist, 0.0);
    max_dist = sound3d_distance_or(max_dist, 0.0);
    if (ref_dist > 0.0 && max_dist > 0.0 && max_dist < ref_dist)
        max_dist = ref_dist;
    base_volume = clamp_i64(base_volume, 0, 100);

    /* Update existing entry */
    for (int32_t i = 0; i < s_voice_dist_count; i++) {
        if (s_voice_dist[i].voice_id == voice) {
            s_voice_dist[i].ref_distance = ref_dist;
            s_voice_dist[i].max_distance = max_dist;
            s_voice_dist[i].base_volume = base_volume;
            return;
        }
    }
    /* Add new entry (overwrite oldest if full) */
    if (s_voice_dist_count < MAX_3D_VOICES) {
        s_voice_dist[s_voice_dist_count].voice_id = voice;
        s_voice_dist[s_voice_dist_count].ref_distance = ref_dist;
        s_voice_dist[s_voice_dist_count].max_distance = max_dist;
        s_voice_dist[s_voice_dist_count].base_volume = base_volume;
        s_voice_dist_count++;
    } else {
        /* Table full: first reclaim a slot whose voice has already finished
         * (one-shot ended or mixer-culled) so we never silently evict a
         * still-playing source's falloff params. Only if every tracked voice is
         * still live do we fall back to round-robin eviction. */
        int32_t slot = -1;
        for (int32_t i = 0; i < MAX_3D_VOICES; i++) {
            if (s_voice_dist[i].voice_id <= 0 || (!s_voice_dist_test_force_all_playing &&
                                                  !rt_voice_is_playing(s_voice_dist[i].voice_id))) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            rt_game3d_diag_record_audio_voice_evicted();
            slot = s_voice_dist_next;
            if (slot < 0 || slot >= MAX_3D_VOICES)
                slot = 0;
            s_voice_dist_next = (slot + 1) % MAX_3D_VOICES;
        }
        s_voice_dist[slot].voice_id = voice;
        s_voice_dist[slot].ref_distance = ref_dist;
        s_voice_dist[slot].max_distance = max_dist;
        s_voice_dist[slot].base_volume = base_volume;
    }
}

/// @brief Register a voice's 3D attenuation params with a default reference distance of 0.
/// @details Convenience wrapper over rt_sound3d_register_voice_ex.
void rt_sound3d_register_voice(int64_t voice, double max_dist, int64_t base_volume) {
    rt_sound3d_register_voice_ex(voice, 0.0, max_dist, base_volume);
}

/// @brief Return the number of active entries in the fixed 3D voice metadata table.
/// @return Tracked voice-entry count in [0, MAX_3D_VOICES].
int64_t rt_sound3d_tracked_voice_count(void) {
    RT_ASSERT_MAIN_THREAD();
    return s_voice_dist_count >= 0 ? s_voice_dist_count : 0;
}

/// @brief Return the capacity of the fixed 3D voice metadata table.
/// @return Maximum simultaneous tracked 3D voice entries.
int64_t rt_sound3d_tracked_voice_capacity(void) {
    return MAX_3D_VOICES;
}

/// @brief Look up a voice's recorded 3D params in a single table scan.
/// @details On a hit, writes the recorded reference distance, max distance, and
///   base volume into whichever out-params are non-NULL and returns 1. On a miss
///   the out-params are left untouched (so the caller's pre-seeded defaults —
///   ref 0.0, max 50.0, base 100 — stand). Replaces three separate per-field
///   scans of the voice table with one pass.
static int lookup_voice_params(int64_t voice,
                               double *out_ref_distance,
                               double *out_max_distance,
                               int64_t *out_base_volume) {
    for (int32_t i = 0; i < s_voice_dist_count; i++) {
        if (s_voice_dist[i].voice_id == voice) {
            if (out_ref_distance)
                *out_ref_distance = s_voice_dist[i].ref_distance;
            if (out_max_distance)
                *out_max_distance = s_voice_dist[i].max_distance;
            if (out_base_volume)
                *out_base_volume = s_voice_dist[i].base_volume;
            return 1;
        }
    }
    return 0;
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
void rt_sound3d_compute_voice_params_ex(const rt_sound3d_listener_state *listener,
                                        const double *source_position,
                                        const double *source_velocity,
                                        double ref_dist,
                                        double max_dist,
                                        int64_t base_vol,
                                        int64_t *out_vol,
                                        int64_t *out_pan,
                                        double *out_doppler) {
    double sx;
    double sy;
    double sz;
    double dx;
    double dy;
    double dz;
    double dist;
    double atten;
    const rt_sound3d_listener_state *effective = listener ? listener : &s_fallback_listener;

    if (out_doppler)
        *out_doppler = 1.0;
    if (!source_position || !out_vol || !out_pan)
        return;

    sx = clamp_abs_or(source_position[0], 0.0, SOUND3D_COORD_ABS_MAX);
    sy = clamp_abs_or(source_position[1], 0.0, SOUND3D_COORD_ABS_MAX);
    sz = clamp_abs_or(source_position[2], 0.0, SOUND3D_COORD_ABS_MAX);
    dx = sx - clamp_abs_or(effective->position[0], 0.0, SOUND3D_COORD_ABS_MAX);
    dy = sy - clamp_abs_or(effective->position[1], 0.0, SOUND3D_COORD_ABS_MAX);
    dz = sz - clamp_abs_or(effective->position[2], 0.0, SOUND3D_COORD_ABS_MAX);
    if (!isfinite(dx) || !isfinite(dy) || !isfinite(dz)) {
        *out_vol = 0;
        *out_pan = 0;
        return;
    }
    dist = hypot(hypot(dx, dy), dz);
    if (!isfinite(dist)) {
        *out_vol = 0;
        *out_pan = 0;
        return;
    }

    ref_dist = sound3d_distance_or(ref_dist, 0.0);
    max_dist = sound3d_distance_or(max_dist, 0.0);
    if (ref_dist > 0.0 && max_dist > 0.0 && max_dist < ref_dist)
        max_dist = ref_dist;
    base_vol = clamp_i64(base_vol, 0, 100);
    if (max_dist <= 0.0) {
        atten = 1.0;
    } else if (ref_dist > 0.0) {
        if (dist <= ref_dist) {
            atten = 1.0;
        } else if (max_dist <= ref_dist) {
            atten = 0.0;
        } else {
            atten = 1.0 - ((dist - ref_dist) / (max_dist - ref_dist));
        }
    } else {
        atten = 1.0 - (dist / max_dist);
    }
    if (!isfinite(atten) || atten < 0.0)
        atten = 0.0;
    if (atten > 1.0)
        atten = 1.0;
    /* Round rather than truncate: the product is non-negative, so +0.5 gives the
     * nearest integer and avoids a consistent downward bias in attenuated volume. */
    *out_vol = clamp_i64((int64_t)((double)base_vol * atten + 0.5), 0, 100);

    if (dist > 1e-8) {
        double inv_dist = 1.0 / dist;
        double ndx = dx * inv_dist;
        double ndy = dy * inv_dist;
        double ndz = dz * inv_dist;
        /* Full 3D dot of the unit source direction with the listener's right
         * axis. Including the y/right[1] term means a rolled (tilted) listener
         * basis is reflected in stereo balance — as the file's invariant
         * promises — and elevated sources are not wrongly pulled toward center
         * by projecting onto x/z while still dividing by the 3D distance. */
        double dot_right =
            ndx * effective->right[0] + ndy * effective->right[1] + ndz * effective->right[2];
        *out_pan = isfinite(dot_right) ? clamp_i64((int64_t)(dot_right * 100.0), -100, 100) : 0;
    } else {
        *out_pan = 0;
    }

    if (out_doppler && dist > 1e-8) {
        const double speed_of_sound = 343.0;
        double ndx = dx / dist;
        double ndy = dy / dist;
        double ndz = dz / dist;
        double src_vx = source_velocity ? sound3d_velocity_or(source_velocity[0]) : 0.0;
        double src_vy = source_velocity ? sound3d_velocity_or(source_velocity[1]) : 0.0;
        double src_vz = source_velocity ? sound3d_velocity_or(source_velocity[2]) : 0.0;
        double listener_along = sound3d_velocity_or(effective->velocity[0]) * ndx +
                                sound3d_velocity_or(effective->velocity[1]) * ndy +
                                sound3d_velocity_or(effective->velocity[2]) * ndz;
        double source_along = src_vx * ndx + src_vy * ndy + src_vz * ndz;
        double denom = speed_of_sound + source_along;
        double factor = fabs(denom) > 1e-6 ? (speed_of_sound + listener_along) / denom : 1.0;
        if (!isfinite(factor))
            factor = 1.0;
        if (factor < 0.5)
            factor = 0.5;
        if (factor > 2.0)
            factor = 2.0;
        *out_doppler = factor;
    }
}

/// @brief Compute a spatialized voice's volume, stereo pan, and Doppler factor for one listener.
/// @details Combines distance attenuation (out to @p max_dist), left/right panning from the
///          source's bearing in the listener frame, and a relative-velocity Doppler shift.
void rt_sound3d_compute_voice_params(const rt_sound3d_listener_state *listener,
                                     const double *source_position,
                                     double max_dist,
                                     int64_t base_vol,
                                     int64_t *out_vol,
                                     int64_t *out_pan) {
    rt_sound3d_compute_voice_params_ex(
        listener, source_position, NULL, 0.0, max_dist, base_vol, out_vol, out_pan, NULL);
}

/// @brief Set the 3D audio listener position and orientation.
/// @details Updates the low-level compatibility listener used when no active
///          SoundListener3D object is driving the spatial-audio state.
/// @param position Vec3 world-space position of the listener (typically the camera).
/// @param forward  Vec3 forward direction the listener is facing.
void rt_sound3d_set_listener(void *position, void *forward) {
    RT_ASSERT_MAIN_THREAD();
    double pos[3];
    double fwd[3];
    sound3d_vec_from_obj(position, pos);
    sound3d_vec_from_obj(forward, fwd);
    rt_sound3d_listener_state_set(&s_fallback_listener, pos, fwd, NULL);
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
int64_t rt_sound3d_play_at(void *sound, void *position, double max_distance, int64_t volume) {
    rt_sound3d_listener_state listener;
    double source_pos[3];
    if (!sound || !position)
        return 0;

    volume = clamp_i64(volume, 0, 100);
    sound3d_vec_from_obj(position, source_pos);
    rt_sound3d_get_effective_listener_state(&listener);
    int64_t vol = 0;
    int64_t pan = 0;
    rt_sound3d_compute_voice_params(&listener, source_pos, max_distance, volume, &vol, &pan);
    int64_t voice = rt_sound_play_ex(sound, vol, pan);
    if (voice > 0)
        rt_sound3d_register_voice(voice, max_distance, volume);
    return voice > 0 ? voice : 0;
}

/// @brief Update a playing voice's volume and pan based on its current 3D position.
/// @details Call each frame for moving sound sources. Recomputes attenuation
///          and stereo pan relative to the current listener position. If
///          max_distance is 0, the value stored at play time is used instead.
/// @param voice        Voice ID returned by rt_sound3d_play_at.
/// @param position     Vec3 current world-space position of the sound source.
/// @param max_distance Falloff range override (0 = use value from play_at).
void rt_sound3d_update_voice(int64_t voice, void *position, double max_distance) {
    rt_sound3d_update_voice_ex(voice, position, NULL, 0.0, max_distance);
}

/// @brief Update a playing voice's 3D position (and optionally velocity/distances) for
/// respatialization.
/// @details Extended form of rt_sound3d_update_voice: @p source_velocity drives Doppler, and
///          @p ref_distance / @p max_distance override the attenuation range (0 keeps prior
///          values).
void rt_sound3d_update_voice_ex(int64_t voice,
                                void *position,
                                const double *source_velocity,
                                double ref_distance,
                                double max_distance) {
    rt_sound3d_listener_state listener;
    double source_pos[3];
    if (!position || voice <= 0)
        return;
    /* One table scan for all three recorded params; defaults stand on a miss. */
    double rec_ref_distance = 0.0;
    double rec_max_distance = 50.0;
    int64_t base_volume = 100;
    lookup_voice_params(voice, &rec_ref_distance, &rec_max_distance, &base_volume);
    if (!isfinite(ref_distance) || ref_distance <= 0.0)
        ref_distance = rec_ref_distance;
    if (!isfinite(max_distance) || max_distance <= 0.0)
        max_distance = rec_max_distance; /* per-voice fallback */
    sound3d_vec_from_obj(position, source_pos);
    rt_sound3d_get_effective_listener_state(&listener);
    int64_t vol = 0;
    int64_t pan = 0;
    double doppler = 1.0;
    rt_sound3d_compute_voice_params_ex(&listener,
                                       source_pos,
                                       source_velocity,
                                       ref_distance,
                                       max_distance,
                                       base_volume,
                                       &vol,
                                       &pan,
                                       &doppler);
    (void)doppler;
    rt_voice_set_volume(voice, vol);
    rt_voice_set_pan(voice, pan);
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
