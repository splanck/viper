//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_animcontroller3d.c
// Purpose: High-level skeletal animation controller with named states,
//   transitions, events, root motion, and simple masked overlay layers.
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_animcontroller3d.h"

#include "rt_mat4.h"
#include "rt_object.h"
#include "rt_quat.h"
#include "rt_skeleton3d.h"
#include "rt_skeleton3d_internal.h"
#include "rt_string.h"
#include "rt_trap.h"
#include "rt_vec3.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RT_ANIM_CONTROLLER3D_MAX_LAYERS 4
#define RT_ANIM_CONTROLLER3D_EVENT_NAME_MAX 64
#define RT_ANIM_CONTROLLER3D_STATE_NAME_MAX 64
#define RT_ANIM_CONTROLLER3D_EVENT_QUEUE_MAX 64

typedef struct {
    char name[RT_ANIM_CONTROLLER3D_STATE_NAME_MAX];
    rt_animation3d *animation;
    float speed;
    int8_t looping;
} anim_controller3d_state_t;

typedef struct {
    int32_t from_state;
    int32_t to_state;
    float blend_seconds;
} anim_controller3d_transition_t;

typedef struct {
    int32_t state_index;
    float time_seconds;
    char name[RT_ANIM_CONTROLLER3D_EVENT_NAME_MAX];
} anim_controller3d_event_t;

typedef struct {
    rt_anim_player3d *player;
    int32_t current_state;
    int32_t previous_state;
    float transition_time;
    float transition_duration;
    int8_t transitioning;
    float weight;
    int32_t mask_root_bone;
    uint8_t mask_bits[VGFX3D_MAX_BONES];
} anim_controller3d_layer_t;

typedef struct {
    void *vptr;
    rt_skeleton3d *skeleton;

    anim_controller3d_state_t *states;
    int32_t state_count;
    int32_t state_capacity;

    anim_controller3d_transition_t *transitions;
    int32_t transition_count;
    int32_t transition_capacity;

    anim_controller3d_event_t *events;
    int32_t event_count;
    int32_t event_capacity;

    anim_controller3d_layer_t layers[RT_ANIM_CONTROLLER3D_MAX_LAYERS];

    float *final_palette;
    float *prev_final_palette;
    int8_t has_prev_final_palette;
    double root_motion_delta[3];
    double root_motion_rotation[4];
    int32_t root_motion_bone;

    char event_queue[RT_ANIM_CONTROLLER3D_EVENT_QUEUE_MAX][RT_ANIM_CONTROLLER3D_EVENT_NAME_MAX];
    int32_t event_queue_head;
    int32_t event_queue_count;
} rt_anim_controller3d;

/// @brief Drop one reference from a slot, free if it was the last, then NULL out the slot.
/// @details Used by the finalizer and slot-replacement paths so the same
///          retain-then-release discipline is shared across every animated-
///          object pointer the controller owns. NULL slot or NULL contents
///          are no-ops.
static void controller_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

/// @brief Copy an `rt_string` into a fixed-size character buffer, NUL-terminating.
/// @details Truncates if the source exceeds `cap - 1` bytes; produces an
///          empty string when `name` is NULL or the buffer is zero-sized.
///          Used to snapshot state and event names into the fixed-size
///          arrays in `anim_controller3d_state_t` / `_event_t`.
static void controller_copy_name(char *dst, size_t cap, rt_string name) {
    const char *src = name ? rt_string_cstr(name) : NULL;
    size_t len = src ? strlen(src) : 0;
    if (!dst || cap == 0) {
        return;
    }
    if (len >= cap)
        len = cap - 1;
    if (len > 0)
        memcpy(dst, src, len);
    dst[len] = '\0';
}

/// @brief Geometric-grow a heap buffer to hold at least `need` elements.
/// @details Doubles capacity (or jumps to `need`, whichever is larger) and
///          `realloc`s in place. Used by the states / transitions / events
///          arrays — capacity starts at 0, jumps to 4, then doubles.
/// @return 1 on success (buffer + capacity updated); 0 on allocation failure
///         (buffer + capacity left untouched).
static int controller_grow_array(void **buffer, int32_t *capacity, int32_t need, size_t elem_size) {
    int32_t new_capacity;
    void *grown;
    if (*capacity >= need)
        return 1;
    new_capacity = *capacity > 0 ? (*capacity * 2) : 4;
    if (new_capacity < need)
        new_capacity = need;
    grown = realloc(*buffer, (size_t)new_capacity * elem_size);
    if (!grown)
        return 0;
    *buffer = grown;
    *capacity = new_capacity;
    return 1;
}

/// @brief Linear-scan the state table by name.
/// @return State index, or -1 if `name` is NULL/empty or no match.
static int32_t controller_find_state(const rt_anim_controller3d *controller, rt_string name) {
    const char *target = name ? rt_string_cstr(name) : NULL;
    if (!controller || !target)
        return -1;
    for (int32_t i = 0; i < controller->state_count; i++) {
        if (strcmp(controller->states[i].name, target) == 0)
            return i;
    }
    return -1;
}

/// @brief Linear-scan the transition table for an exact `(from, to)` pair.
/// @return Transition index, or -1 if no match is registered.
static int32_t controller_find_transition(
    const rt_anim_controller3d *controller, int32_t from_state, int32_t to_state) {
    if (!controller)
        return -1;
    for (int32_t i = 0; i < controller->transition_count; i++) {
        if (controller->transitions[i].from_state == from_state &&
            controller->transitions[i].to_state == to_state)
            return i;
    }
    return -1;
}

/// @brief Push an event name onto the event-queue ring buffer.
/// @details The queue is bounded at `RT_ANIM_CONTROLLER3D_EVENT_QUEUE_MAX`.
///          When full, the oldest entry is dropped (head advances) before
///          the new one is appended — the rationale is "lose the stalest
///          event, never silently drop the freshest one." NULL or empty
///          event names are silently ignored. Names longer than the
///          fixed-size slot are truncated (NUL-terminated within `slot`).
static void controller_enqueue_event(rt_anim_controller3d *controller, const char *event_name) {
    int32_t slot;
    size_t len;
    if (!controller || !event_name || event_name[0] == '\0')
        return;
    if (controller->event_queue_count == RT_ANIM_CONTROLLER3D_EVENT_QUEUE_MAX) {
        controller->event_queue_head =
            (controller->event_queue_head + 1) % RT_ANIM_CONTROLLER3D_EVENT_QUEUE_MAX;
        controller->event_queue_count--;
    }
    slot = (controller->event_queue_head + controller->event_queue_count) %
           RT_ANIM_CONTROLLER3D_EVENT_QUEUE_MAX;
    len = strlen(event_name);
    if (len >= RT_ANIM_CONTROLLER3D_EVENT_NAME_MAX)
        len = RT_ANIM_CONTROLLER3D_EVENT_NAME_MAX - 1;
    memcpy(controller->event_queue[slot], event_name, len);
    controller->event_queue[slot][len] = '\0';
    controller->event_queue_count++;
}

/// @brief Enqueue every event registered at time ≈ 0 for a state, on entry to that state.
/// @details Some events are pinned to the start of an animation (e.g., a
///          weapon-pull anim that should fire its sound on frame 0). These
///          would otherwise be missed by the prev_time→curr_time window
///          tracking in `controller_process_events` because the player
///          hasn't ticked yet. Threshold is 1µs.
static void controller_fire_entry_events(rt_anim_controller3d *controller, int32_t state_index) {
    if (!controller || state_index < 0 || state_index >= controller->state_count)
        return;
    for (int32_t i = 0; i < controller->event_count; i++) {
        if (controller->events[i].state_index == state_index &&
            controller->events[i].time_seconds <= 1e-6f) {
            controller_enqueue_event(controller, controller->events[i].name);
        }
    }
}

/// @brief Fire any state events whose timestamp falls in the (prev_time, curr_time] window.
/// @details Three cases:
///          - **Zero-duration animation** (no real timeline): any event with
///            `time_seconds <= curr_time` fires. Treats the animation as a
///            single-frame impulse.
///          - **Non-looping**: standard half-open window — events fire when
///            `prev_time < t <= curr_time`.
///          - **Looping**: when `curr_time < prev_time` the playback wrapped
///            around the loop boundary, so the window is the union of
///            `(prev_time, duration]` and `[0, curr_time]`. Events are
///            still fired at most once per crossing.
///          Half-open semantics on the lower bound prevent an event at
///          exactly `prev_time` from firing twice on consecutive ticks.
static void controller_process_events(rt_anim_controller3d *controller,
                                      int32_t state_index,
                                      double prev_time,
                                      double curr_time,
                                      double duration,
                                      int8_t looping) {
    if (!controller || state_index < 0 || state_index >= controller->state_count)
        return;
    if (duration <= 0.0) {
        for (int32_t i = 0; i < controller->event_count; i++) {
            if (controller->events[i].state_index == state_index &&
                controller->events[i].time_seconds <= curr_time) {
                controller_enqueue_event(controller, controller->events[i].name);
            }
        }
        return;
    }
    for (int32_t i = 0; i < controller->event_count; i++) {
        const anim_controller3d_event_t *event = &controller->events[i];
        double t;
        if (event->state_index != state_index)
            continue;
        t = (double)event->time_seconds;
        if (!looping) {
            if (t > prev_time && t <= curr_time)
                controller_enqueue_event(controller, event->name);
            continue;
        }
        if (curr_time >= prev_time) {
            if (t > prev_time && t <= curr_time)
                controller_enqueue_event(controller, event->name);
        } else {
            if (t > prev_time || t <= curr_time)
                controller_enqueue_event(controller, event->name);
        }
    }
}

/// @brief Mark every bone (up to `bone_count`) as included in this layer's mask.
/// @details Used for the base layer (layer 0) which always covers the full
///          skeleton, and as the reset state when an overlay layer's
///          `mask_root_bone` is invalid or out of range.
static void controller_set_all_mask_bits(anim_controller3d_layer_t *layer, int32_t bone_count) {
    if (!layer)
        return;
    memset(layer->mask_bits, 0, sizeof(layer->mask_bits));
    for (int32_t bone = 0; bone < bone_count && bone < VGFX3D_MAX_BONES; bone++)
        layer->mask_bits[bone] = 1;
}

/// @brief Recompute a layer's mask bits from its `mask_root_bone` setting.
/// @details For layer 0 or invalid root, sets all bits (covers the whole
///          skeleton). Otherwise, walks each bone's parent chain and marks
///          the bone as masked-in if `mask_root_bone` is an ancestor.
///          This is the standard "this layer animates an arm subtree only"
///          construction — the upper-body layer of a character that
///          composites a wave / aim / reload pose over a running base.
static void controller_rebuild_layer_mask(rt_anim_controller3d *controller, int32_t layer_index) {
    anim_controller3d_layer_t *layer;
    int32_t bone_count;
    if (!controller || layer_index < 0 || layer_index >= RT_ANIM_CONTROLLER3D_MAX_LAYERS)
        return;
    layer = &controller->layers[layer_index];
    bone_count = controller->skeleton ? controller->skeleton->bone_count : 0;
    if (layer_index == 0 || layer->mask_root_bone < 0 || layer->mask_root_bone >= bone_count) {
        controller_set_all_mask_bits(layer, bone_count);
        return;
    }
    memset(layer->mask_bits, 0, sizeof(layer->mask_bits));
    for (int32_t bone = 0; bone < bone_count && bone < VGFX3D_MAX_BONES; bone++) {
        int32_t current = bone;
        while (current >= 0) {
            if (current == layer->mask_root_bone) {
                layer->mask_bits[bone] = 1;
                break;
            }
            current = controller->skeleton->bones[current].parent_index;
        }
    }
}

/// @brief Push a state's per-state speed and looping settings into a player.
/// @details Always enables the player's loop-override so the controller's
///          per-state looping decision wins over whatever the underlying
///          Animation3D defaults to.
static void controller_apply_state_settings(
    rt_anim_player3d *player, const anim_controller3d_state_t *state) {
    if (!player || !state)
        return;
    player->loop_override_enabled = 1;
    player->loop_override_value = state->looping ? 1 : 0;
    rt_anim_player3d_set_speed(player, state->speed);
}

/// @brief Switch a layer to a new state, optionally crossfading from the current state.
/// @details Crossfade is used when `blend_seconds > 0`, the layer already has
///          a different active state, and the existing player is mid-play.
///          Otherwise the switch is instantaneous (a hard cut). After
///          switching, the player is ticked by 0 seconds to push initial
///          pose data into the bone palette so the next render sees the
///          new state without a one-frame lag, and time-zero entry events
///          for the new state are fired.
/// @return 1 on success, 0 on bad layer index, missing state, or missing
///         underlying player/animation.
static int8_t controller_set_layer_state(rt_anim_controller3d *controller,
                                         int32_t layer_index,
                                         int32_t state_index,
                                         double blend_seconds) {
    anim_controller3d_layer_t *layer;
    const anim_controller3d_state_t *state;
    int use_crossfade;
    if (!controller || layer_index < 0 || layer_index >= RT_ANIM_CONTROLLER3D_MAX_LAYERS)
        return 0;
    if (state_index < 0 || state_index >= controller->state_count)
        return 0;
    layer = &controller->layers[layer_index];
    state = &controller->states[state_index];
    if (!layer->player || !state->animation)
        return 0;

    use_crossfade = blend_seconds > 0.0 && layer->current_state >= 0 &&
                    layer->current_state != state_index && rt_anim_player3d_is_playing(layer->player);

    layer->previous_state = layer->current_state;
    layer->current_state = state_index;
    controller_apply_state_settings(layer->player, state);
    if (use_crossfade) {
        rt_anim_player3d_crossfade(layer->player, state->animation, blend_seconds);
        layer->transitioning = 1;
        layer->transition_time = 0.0f;
        layer->transition_duration = (float)blend_seconds;
    } else {
        rt_anim_player3d_play(layer->player, state->animation);
        layer->transitioning = 0;
        layer->transition_time = 0.0f;
        layer->transition_duration = 0.0f;
    }
    rt_anim_player3d_update(layer->player, 0.0);
    controller_fire_entry_events(controller, state_index);
    return 1;
}

/// @brief Reset the final bone palette to identity matrices for every bone.
/// @details Used as the seed when no base-layer animation is playing or as
///          a clean slate before compositing overlay layers. Each 16-float
///          matrix block is zeroed then the diagonal is set to 1.0.
static void controller_identity_palette(rt_anim_controller3d *controller) {
    int32_t bone_count;
    if (!controller || !controller->final_palette || !controller->skeleton)
        return;
    bone_count = controller->skeleton->bone_count;
    for (int32_t bone = 0; bone < bone_count; bone++) {
        float *m = &controller->final_palette[bone * 16];
        memset(m, 0, 16 * sizeof(float));
        m[0] = m[5] = m[10] = m[15] = 1.0f;
    }
}

/// @brief Composite the base layer + weighted overlay layers into the final bone palette.
/// @details Two-pass blend:
///          1. Base layer (layer 0) is copied wholesale into `final_palette`.
///             If the base has no animation, identity matrices are used so
///             the skeleton renders in its rest pose.
///          2. For each overlay layer (1..MAX-1) with weight > 0, blend the
///             overlay's palette into `final_palette` at the layer's weight,
///             but only on bones whose `mask_bits` flag is set. The blend
///             is per-element linear interpolation: `dst += (src - dst) * w`.
///          Weight is clamped to `[0, 1]`. Layers with empty masks contribute
///          nothing.
static void controller_compute_final_palette(rt_anim_controller3d *controller) {
    int32_t bone_count;
    if (!controller || !controller->final_palette || !controller->skeleton)
        return;
    bone_count = controller->skeleton->bone_count;
    if (bone_count <= 0)
        return;

    if (controller->layers[0].player && controller->layers[0].player->bone_palette) {
        memcpy(controller->final_palette,
               controller->layers[0].player->bone_palette,
               (size_t)bone_count * 16 * sizeof(float));
    } else {
        controller_identity_palette(controller);
    }

    for (int32_t layer_index = 1; layer_index < RT_ANIM_CONTROLLER3D_MAX_LAYERS; layer_index++) {
        anim_controller3d_layer_t *layer = &controller->layers[layer_index];
        float weight = layer->weight;
        if (!layer->player || !layer->player->bone_palette || layer->current_state < 0 ||
            weight <= 1e-6f)
            continue;
        if (weight > 1.0f)
            weight = 1.0f;
        for (int32_t bone = 0; bone < bone_count; bone++) {
            float *dst;
            const float *src;
            if (!layer->mask_bits[bone])
                continue;
            dst = &controller->final_palette[bone * 16];
            src = &layer->player->bone_palette[bone * 16];
            for (int32_t i = 0; i < 16; i++)
                dst[i] += (src[i] - dst[i]) * weight;
        }
    }
}

/// @brief Read the translation column from a layer's bone matrix at `bone_index`.
/// @details Reads `(m[3], m[7], m[11])` which is the translation row in the
///          column-major 4x4 matrix used by the player's bone palette.
///          Used by root-motion delta accumulation. Out-pointers are zeroed
///          on any failure path so callers don't have to NULL-check.
static void controller_get_layer_translation(const anim_controller3d_layer_t *layer,
                                             int32_t bone_index,
                                             double *x,
                                             double *y,
                                             double *z) {
    const float *m;
    if (!x || !y || !z) {
        return;
    }
    *x = 0.0;
    *y = 0.0;
    *z = 0.0;
    if (!layer || !layer->player || !layer->player->bone_palette || !layer->player->skeleton)
        return;
    if (bone_index < 0 || bone_index >= layer->player->skeleton->bone_count)
        return;
    m = &layer->player->bone_palette[bone_index * 16];
    *x = (double)m[3];
    *y = (double)m[7];
    *z = (double)m[11];
}

/// @brief Set a quaternion to the identity rotation `(0, 0, 0, 1)` (xyzw).
static void controller_quat_identity(double *out) {
    if (!out)
        return;
    out[0] = 0.0;
    out[1] = 0.0;
    out[2] = 0.0;
    out[3] = 1.0;
}

/// @brief Normalize a quaternion in place to unit length.
/// @details Falls back to identity if the quaternion is degenerate (squared
///          length below 1e-20) to avoid `1/0` from the inverse-length
///          division. Critical because subsequent quaternion multiplications
///          assume unit operands.
static void controller_quat_normalize(double *q) {
    double len_sq;
    double inv_len;
    if (!q)
        return;
    len_sq = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    if (len_sq < 1e-20) {
        controller_quat_identity(q);
        return;
    }
    inv_len = 1.0 / sqrt(len_sq);
    q[0] *= inv_len;
    q[1] *= inv_len;
    q[2] *= inv_len;
    q[3] *= inv_len;
}

/// @brief Convert a 3×3 rotation matrix (passed as nine row elements) to a quaternion.
/// @details Implementation of Shepperd's method (1978): pick the branch that
///          maximises numerical stability based on which of `trace`,
///          `m00`, `m11`, or `m22` is largest. This avoids the catastrophic
///          cancellation that the naive `sqrt(1+trace)/2` formulation
///          produces near 180° rotations. Output quaternion is normalised
///          before return.
static void controller_quat_from_matrix_rows(double m00,
                                             double m01,
                                             double m02,
                                             double m10,
                                             double m11,
                                             double m12,
                                             double m20,
                                             double m21,
                                             double m22,
                                             double *out) {
    double trace;
    if (!out)
        return;
    trace = m00 + m11 + m22;
    if (trace > 0.0) {
        double s = sqrt(trace + 1.0) * 2.0;
        out[3] = 0.25 * s;
        out[0] = (m21 - m12) / s;
        out[1] = (m02 - m20) / s;
        out[2] = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        double s = sqrt(1.0 + m00 - m11 - m22) * 2.0;
        out[3] = (m21 - m12) / s;
        out[0] = 0.25 * s;
        out[1] = (m01 + m10) / s;
        out[2] = (m02 + m20) / s;
    } else if (m11 > m22) {
        double s = sqrt(1.0 + m11 - m00 - m22) * 2.0;
        out[3] = (m02 - m20) / s;
        out[0] = (m01 + m10) / s;
        out[1] = 0.25 * s;
        out[2] = (m12 + m21) / s;
    } else {
        double s = sqrt(1.0 + m22 - m00 - m11) * 2.0;
        out[3] = (m10 - m01) / s;
        out[0] = (m02 + m20) / s;
        out[1] = (m12 + m21) / s;
        out[2] = 0.25 * s;
    }
    controller_quat_normalize(out);
}

/// @brief Extract the rotation quaternion from a layer's bone matrix at `bone_index`.
/// @details The bone palette stores TRS-composed matrices, so the rotation
///          must be unscaled before being converted to a quaternion.
///          Computes per-axis scale from the columns' Euclidean norms,
///          divides each column out by its scale to recover an orthonormal
///          rotation matrix, then converts via Shepperd's method. Falls
///          back to identity when any axis scale is degenerate (< 1e-8).
static void controller_get_layer_rotation(
    const anim_controller3d_layer_t *layer, int32_t bone_index, double *out_quat) {
    const float *m;
    double sx;
    double sy;
    double sz;
    double inv_sx;
    double inv_sy;
    double inv_sz;

    controller_quat_identity(out_quat);
    if (!layer || !layer->player || !layer->player->bone_palette || !layer->player->skeleton)
        return;
    if (bone_index < 0 || bone_index >= layer->player->skeleton->bone_count)
        return;
    m = &layer->player->bone_palette[bone_index * 16];
    sx = sqrt((double)m[0] * (double)m[0] + (double)m[4] * (double)m[4] + (double)m[8] * (double)m[8]);
    sy = sqrt((double)m[1] * (double)m[1] + (double)m[5] * (double)m[5] + (double)m[9] * (double)m[9]);
    sz = sqrt((double)m[2] * (double)m[2] + (double)m[6] * (double)m[6] + (double)m[10] * (double)m[10]);
    if (sx < 1e-8 || sy < 1e-8 || sz < 1e-8)
        return;
    inv_sx = 1.0 / sx;
    inv_sy = 1.0 / sy;
    inv_sz = 1.0 / sz;
    controller_quat_from_matrix_rows((double)m[0] * inv_sx,
                                     (double)m[1] * inv_sy,
                                     (double)m[2] * inv_sz,
                                     (double)m[4] * inv_sx,
                                     (double)m[5] * inv_sy,
                                     (double)m[6] * inv_sz,
                                     (double)m[8] * inv_sx,
                                     (double)m[9] * inv_sy,
                                     (double)m[10] * inv_sz,
                                     out_quat);
}

/// @brief Compute the conjugate of a quaternion (`(-x, -y, -z, w)`).
/// @details For a unit quaternion, the conjugate equals the inverse. Used
///          here to invert the "before" rotation when computing the
///          per-frame rotation delta.
static void controller_quat_conjugate(const double *q, double *out) {
    if (!q || !out)
        return;
    out[0] = -q[0];
    out[1] = -q[1];
    out[2] = -q[2];
    out[3] = q[3];
}

/// @brief Multiply two quaternions: `out = a * b` (apply `b` then `a`).
/// @details Standard Hamilton product. Output is normalised before return
///          to absorb the floating-point drift that accumulates as
///          quaternion multiplications are chained over many frames of
///          root-motion accumulation.
static void controller_quat_mul(const double *a, const double *b, double *out) {
    double x;
    double y;
    double z;
    double w;
    if (!a || !b || !out)
        return;
    w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    out[0] = x;
    out[1] = y;
    out[2] = z;
    out[3] = w;
    controller_quat_normalize(out);
}

/// @brief GC finalizer: release every owned reference and free the heap arrays.
/// @details Releases the skeleton, every state's animation, all four layer
///          players, and frees the states / transitions / events / palette
///          buffers. Slot pointers are NULL'd via `controller_release_ref`
///          so a re-entrant call (shouldn't happen but defensive) is safe.
static void controller_finalize(void *obj) {
    rt_anim_controller3d *controller = (rt_anim_controller3d *)obj;
    if (!controller)
        return;
    controller_release_ref((void **)&controller->skeleton);
    for (int32_t i = 0; i < controller->state_count; i++)
        controller_release_ref((void **)&controller->states[i].animation);
    free(controller->states);
    free(controller->transitions);
    free(controller->events);
    controller->states = NULL;
    controller->transitions = NULL;
    controller->events = NULL;
    for (int32_t i = 0; i < RT_ANIM_CONTROLLER3D_MAX_LAYERS; i++)
        controller_release_ref((void **)&controller->layers[i].player);
    free(controller->final_palette);
    controller->final_palette = NULL;
    free(controller->prev_final_palette);
    controller->prev_final_palette = NULL;
}

/// @brief Construct a new AnimController3D bound to a skeleton.
/// @details Allocates the controller object with a finalizer, retains the
///          skeleton, allocates the current and previous bone-palette
///          buffers, and constructs `RT_ANIM_CONTROLLER3D_MAX_LAYERS`
///          (currently 4) underlying anim players — one per layer. Layer 0
///          is the base layer (weight 1.0); higher layers start at weight
///          0.0 with full-skeleton masks. The palette is initialised to
///          identity so the skeleton renders in rest pose until a state
///          is played. Traps on null skeleton or any allocation failure
///          (failed palette / layer allocations clean up partial state via
///          `controller_finalize` before returning NULL).
void *rt_anim_controller3d_new(void *skeleton) {
    rt_anim_controller3d *controller;
    rt_skeleton3d *skel = (rt_skeleton3d *)skeleton;
    if (!skeleton) {
        rt_trap("AnimController3D.New: null skeleton");
        return NULL;
    }
    controller =
        (rt_anim_controller3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_anim_controller3d));
    if (!controller) {
        rt_trap("AnimController3D.New: allocation failed");
        return NULL;
    }
    memset(controller, 0, sizeof(*controller));
    rt_obj_set_finalizer(controller, controller_finalize);
    controller->skeleton = skel;
    rt_obj_retain_maybe(controller->skeleton);
    controller->root_motion_bone = 0;
    controller_quat_identity(controller->root_motion_rotation);

    if (skel->bone_count > 0) {
        size_t palette_size = (size_t)skel->bone_count * 16 * sizeof(float);
        controller->final_palette = (float *)calloc(1, palette_size);
        controller->prev_final_palette = (float *)calloc(1, palette_size);
        if (!controller->final_palette || !controller->prev_final_palette) {
            rt_trap("AnimController3D.New: palette allocation failed");
            controller_finalize(controller);
            return NULL;
        }
    }
    for (int32_t layer_index = 0; layer_index < RT_ANIM_CONTROLLER3D_MAX_LAYERS; layer_index++) {
        anim_controller3d_layer_t *layer = &controller->layers[layer_index];
        layer->player = (rt_anim_player3d *)rt_anim_player3d_new(skeleton);
        if (!layer->player) {
            rt_trap("AnimController3D.New: layer allocation failed");
            controller_finalize(controller);
            return NULL;
        }
        layer->current_state = -1;
        layer->previous_state = -1;
        layer->weight = layer_index == 0 ? 1.0f : 0.0f;
        layer->mask_root_bone = -1;
        controller_set_all_mask_bits(layer, skel->bone_count);
    }
    controller_identity_palette(controller);
    return controller;
}

/// @brief Register a named animation state. `name` is the lookup key (used by `_play`,
/// `_crossfade`, etc); `animation` is an Animation3D handle. If a state with `name` already
/// exists, returns its existing index without creating a duplicate. Returns -1 on failure.
int64_t rt_anim_controller3d_add_state(void *obj, rt_string name, void *animation) {
    rt_anim_controller3d *controller = (rt_anim_controller3d *)obj;
    anim_controller3d_state_t *state;
    int32_t existing;
    if (!controller || !animation)
        return -1;
    existing = controller_find_state(controller, name);
    if (existing >= 0)
        return existing;
    if (!controller_grow_array((void **)&controller->states,
                               &controller->state_capacity,
                               controller->state_count + 1,
                               sizeof(*controller->states))) {
        rt_trap("AnimController3D.AddState: allocation failed");
        return -1;
    }
    state = &controller->states[controller->state_count];
    memset(state, 0, sizeof(*state));
    controller_copy_name(state->name, sizeof(state->name), name);
    state->animation = (rt_animation3d *)animation;
    rt_obj_retain_maybe(state->animation);
    state->speed = 1.0f;
    state->looping = rt_animation3d_get_looping(animation) ? 1 : 0;
    return controller->state_count++;
}

/// @brief Register a directional transition with a default blend time. When `_play` is later
/// called with `to_state` while in `from_state`, this blend time is used automatically. If the
/// transition already exists, its blend time is updated. Returns 1 on success.
int8_t rt_anim_controller3d_add_transition(
    void *obj, rt_string from_state, rt_string to_state, double blend_seconds) {
    rt_anim_controller3d *controller = (rt_anim_controller3d *)obj;
    int32_t from_index;
    int32_t to_index;
    int32_t existing;
    anim_controller3d_transition_t *transition;
    if (!controller)
        return 0;
    from_index = controller_find_state(controller, from_state);
    to_index = controller_find_state(controller, to_state);
    if (from_index < 0 || to_index < 0)
        return 0;
    existing = controller_find_transition(controller, from_index, to_index);
    if (existing >= 0) {
        controller->transitions[existing].blend_seconds = (float)blend_seconds;
        return 1;
    }
    if (!controller_grow_array((void **)&controller->transitions,
                               &controller->transition_capacity,
                               controller->transition_count + 1,
                               sizeof(*controller->transitions))) {
        rt_trap("AnimController3D.AddTransition: allocation failed");
        return 0;
    }
    transition = &controller->transitions[controller->transition_count++];
    transition->from_state = from_index;
    transition->to_state = to_index;
    transition->blend_seconds = (float)blend_seconds;
    return 1;
}

/// @brief Play a state on the base layer. If a transition was registered between the current
/// state and `state_name`, its blend time is used; otherwise the switch is instantaneous.
/// Returns 1 if the state exists, 0 if not.
int8_t rt_anim_controller3d_play(void *obj, rt_string state_name) {
    rt_anim_controller3d *controller = (rt_anim_controller3d *)obj;
    int32_t state_index;
    double blend_seconds = 0.0;
    if (!controller)
        return 0;
    state_index = controller_find_state(controller, state_name);
    if (state_index < 0)
        return 0;
    if (controller->layers[0].current_state >= 0) {
        int32_t transition_index = controller_find_transition(
            controller, controller->layers[0].current_state, state_index);
        if (transition_index >= 0)
            blend_seconds = controller->transitions[transition_index].blend_seconds;
    }
    return controller_set_layer_state(controller, 0, state_index, blend_seconds);
}

/// @brief Cross-fade to `state_name` over `blend_seconds`, regardless of any registered default
/// transition. Use this for one-off blends with custom timing.
int8_t rt_anim_controller3d_crossfade(void *obj, rt_string state_name, double blend_seconds) {
    rt_anim_controller3d *controller = (rt_anim_controller3d *)obj;
    int32_t state_index;
    if (!controller)
        return 0;
    state_index = controller_find_state(controller, state_name);
    if (state_index < 0)
        return 0;
    return controller_set_layer_state(controller, 0, state_index, blend_seconds);
}

/// @brief Stop playback on every layer (base + overlays). Does not clear the current_state
/// indices — calling `_play` again resumes from the same animation. Resets transition timers.
void rt_anim_controller3d_stop(void *obj) {
    rt_anim_controller3d *controller = (rt_anim_controller3d *)obj;
    if (!controller)
        return;
    for (int32_t layer_index = 0; layer_index < RT_ANIM_CONTROLLER3D_MAX_LAYERS; layer_index++) {
        anim_controller3d_layer_t *layer = &controller->layers[layer_index];
        if (layer->player)
            rt_anim_player3d_stop(layer->player);
        layer->transitioning = 0;
        layer->transition_time = 0.0f;
        layer->transition_duration = 0.0f;
    }
}

/// @brief Per-frame tick for every layer. Advances each player's time by `delta_time`, fires
/// time-stamped events crossing the previous→current playback window, advances transition
/// blends, then composites the layered final bone palette. Also accumulates root-motion bone
/// translation deltas for `_consume_root_motion`. No-op for negative dt or missing skeleton.
void rt_anim_controller3d_update(void *obj, double delta_time) {
    rt_anim_controller3d *controller = (rt_anim_controller3d *)obj;
    double before_x;
    double before_y;
    double before_z;
    double before_rot[4];
    double after_x;
    double after_y;
    double after_z;
    double after_rot[4];
    double inv_before_rot[4];
    double delta_rot[4];
    double accumulated_rot[4];
    if (!controller || !controller->skeleton || delta_time < 0.0)
        return;

    if (controller->final_palette && controller->prev_final_palette && controller->skeleton &&
        controller->skeleton->bone_count > 0) {
        memcpy(controller->prev_final_palette,
               controller->final_palette,
               (size_t)controller->skeleton->bone_count * 16 * sizeof(float));
        controller->has_prev_final_palette = 1;
    }

    controller_get_layer_translation(
        &controller->layers[0], controller->root_motion_bone, &before_x, &before_y, &before_z);
    controller_get_layer_rotation(&controller->layers[0], controller->root_motion_bone, before_rot);

    for (int32_t layer_index = 0; layer_index < RT_ANIM_CONTROLLER3D_MAX_LAYERS; layer_index++) {
        anim_controller3d_layer_t *layer = &controller->layers[layer_index];
        double prev_time;
        double curr_time;
        int32_t state_index = layer->current_state;
        if (!layer->player || state_index < 0 || state_index >= controller->state_count)
            continue;
        prev_time = rt_anim_player3d_get_time(layer->player);
        rt_anim_player3d_update(layer->player, delta_time);
        curr_time = rt_anim_player3d_get_time(layer->player);
        controller_process_events(controller,
                                  state_index,
                                  prev_time,
                                  curr_time,
                                  rt_animation3d_get_duration(controller->states[state_index].animation),
                                  controller->states[state_index].looping);
        if (layer->transitioning) {
            layer->transition_time += (float)delta_time;
            if (layer->transition_time >= layer->transition_duration) {
                layer->transitioning = 0;
                layer->transition_time = layer->transition_duration;
            }
        }
    }

    controller_compute_final_palette(controller);
    controller_get_layer_translation(
        &controller->layers[0], controller->root_motion_bone, &after_x, &after_y, &after_z);
    controller_get_layer_rotation(&controller->layers[0], controller->root_motion_bone, after_rot);
    controller->root_motion_delta[0] += after_x - before_x;
    controller->root_motion_delta[1] += after_y - before_y;
    controller->root_motion_delta[2] += after_z - before_z;
    controller_quat_conjugate(before_rot, inv_before_rot);
    controller_quat_mul(after_rot, inv_before_rot, delta_rot);
    controller_quat_mul(controller->root_motion_rotation, delta_rot, accumulated_rot);
    memcpy(controller->root_motion_rotation, accumulated_rot, sizeof(accumulated_rot));
}

/// @brief Name of the state currently playing on the base layer (empty string if none/missing).
rt_string rt_anim_controller3d_get_current_state(void *obj) {
    rt_anim_controller3d *controller = (rt_anim_controller3d *)obj;
    int32_t state_index;
    if (!controller)
        return rt_const_cstr("");
    state_index = controller->layers[0].current_state;
    if (state_index < 0 || state_index >= controller->state_count)
        return rt_const_cstr("");
    return rt_const_cstr(controller->states[state_index].name);
}

/// @brief Name of the state on the base layer immediately before the current one. Useful during
/// transitions to know what the controller is fading *from*. Empty if no prior state.
rt_string rt_anim_controller3d_get_previous_state(void *obj) {
    rt_anim_controller3d *controller = (rt_anim_controller3d *)obj;
    int32_t state_index;
    if (!controller)
        return rt_const_cstr("");
    state_index = controller->layers[0].previous_state;
    if (state_index < 0 || state_index >= controller->state_count)
        return rt_const_cstr("");
    return rt_const_cstr(controller->states[state_index].name);
}

/// @brief Returns 1 if the base layer is mid-blend between two states.
int8_t rt_anim_controller3d_get_is_transitioning(void *obj) {
    rt_anim_controller3d *controller = (rt_anim_controller3d *)obj;
    return controller ? controller->layers[0].transitioning : 0;
}

/// @brief Number of states currently registered with the controller.
int64_t rt_anim_controller3d_get_state_count(void *obj) {
    rt_anim_controller3d *controller = (rt_anim_controller3d *)obj;
    return controller ? controller->state_count : 0;
}

/// @brief Override the playback speed multiplier for a state. Applied immediately to any
/// layer currently playing that state. Negative speeds are accepted (reverse playback).
void rt_anim_controller3d_set_state_speed(void *obj, rt_string state_name, double speed) {
    rt_anim_controller3d *controller = (rt_anim_controller3d *)obj;
    int32_t state_index;
    if (!controller)
        return;
    state_index = controller_find_state(controller, state_name);
    if (state_index < 0)
        return;
    controller->states[state_index].speed = (float)speed;
    for (int32_t layer_index = 0; layer_index < RT_ANIM_CONTROLLER3D_MAX_LAYERS; layer_index++) {
        if (controller->layers[layer_index].current_state == state_index)
            rt_anim_player3d_set_speed(controller->layers[layer_index].player, speed);
    }
}

/// @brief Override whether a state loops (1) or plays once (0). Applied immediately to any
/// layer currently playing that state via the player's loop_override hook.
void rt_anim_controller3d_set_state_looping(void *obj, rt_string state_name, int8_t loop) {
    rt_anim_controller3d *controller = (rt_anim_controller3d *)obj;
    int32_t state_index;
    if (!controller)
        return;
    state_index = controller_find_state(controller, state_name);
    if (state_index < 0)
        return;
    controller->states[state_index].looping = loop ? 1 : 0;
    for (int32_t layer_index = 0; layer_index < RT_ANIM_CONTROLLER3D_MAX_LAYERS; layer_index++) {
        anim_controller3d_layer_t *layer = &controller->layers[layer_index];
        if (layer->current_state != state_index || !layer->player)
            continue;
        layer->player->loop_override_enabled = 1;
        layer->player->loop_override_value = loop ? 1 : 0;
    }
}

/// @brief Register a time-stamped event on a state. When `_update` advances playback past
/// `time_seconds`, `event_name` is enqueued for `_poll_event`. Useful for footstep sounds,
/// hit-frame triggers, attack windows. Multiple events per state are allowed.
void rt_anim_controller3d_add_event(
    void *obj, rt_string state_name, double time_seconds, rt_string event_name) {
    rt_anim_controller3d *controller = (rt_anim_controller3d *)obj;
    anim_controller3d_event_t *event;
    int32_t state_index;
    if (!controller)
        return;
    state_index = controller_find_state(controller, state_name);
    if (state_index < 0)
        return;
    if (!controller_grow_array((void **)&controller->events,
                               &controller->event_capacity,
                               controller->event_count + 1,
                               sizeof(*controller->events))) {
        rt_trap("AnimController3D.AddEvent: allocation failed");
        return;
    }
    event = &controller->events[controller->event_count++];
    memset(event, 0, sizeof(*event));
    event->state_index = state_index;
    event->time_seconds = (float)time_seconds;
    controller_copy_name(event->name, sizeof(event->name), event_name);
}

/// @brief Pop the next pending event name from the queue (FIFO). Returns empty string when
/// the queue is exhausted. Caller typically polls in a loop after each `_update` to drain.
rt_string rt_anim_controller3d_poll_event(void *obj) {
    rt_anim_controller3d *controller = (rt_anim_controller3d *)obj;
    const char *name;
    if (!controller || controller->event_queue_count == 0)
        return rt_const_cstr("");
    name = controller->event_queue[controller->event_queue_head];
    controller->event_queue_head =
        (controller->event_queue_head + 1) % RT_ANIM_CONTROLLER3D_EVENT_QUEUE_MAX;
    controller->event_queue_count--;
    return rt_const_cstr(name);
}

/// @brief Set which bone supplies root motion (typically the hip/pelvis). Translation deltas
/// from this bone each frame are accumulated for `_consume_root_motion` so the game can move
/// the character via the animation rather than fighting with it. Resets the accumulated delta.
void rt_anim_controller3d_set_root_motion_bone(void *obj, int64_t bone_index) {
    rt_anim_controller3d *controller = (rt_anim_controller3d *)obj;
    if (!controller || !controller->skeleton)
        return;
    if (bone_index < 0 || bone_index >= controller->skeleton->bone_count)
        return;
    controller->root_motion_bone = (int32_t)bone_index;
    controller->root_motion_delta[0] = 0.0;
    controller->root_motion_delta[1] = 0.0;
    controller->root_motion_delta[2] = 0.0;
    controller_quat_identity(controller->root_motion_rotation);
}

/// @brief Read (without clearing) the accumulated root-motion translation delta as a Vec3.
/// Use `_consume_root_motion` instead when you want to move the character and reset.
void *rt_anim_controller3d_get_root_motion_delta(void *obj) {
    rt_anim_controller3d *controller = (rt_anim_controller3d *)obj;
    if (!controller)
        return rt_vec3_new(0.0, 0.0, 0.0);
    return rt_vec3_new(controller->root_motion_delta[0],
                       controller->root_motion_delta[1],
                       controller->root_motion_delta[2]);
}

/// @brief Atomically read and reset the accumulated root-motion delta. Returns the Vec3 the
/// character should be moved by this frame to follow the animation's hip translation.
void *rt_anim_controller3d_consume_root_motion(void *obj) {
    rt_anim_controller3d *controller = (rt_anim_controller3d *)obj;
    void *delta;
    if (!controller)
        return rt_vec3_new(0.0, 0.0, 0.0);
    delta = rt_vec3_new(controller->root_motion_delta[0],
                        controller->root_motion_delta[1],
                        controller->root_motion_delta[2]);
    controller->root_motion_delta[0] = 0.0;
    controller->root_motion_delta[1] = 0.0;
    controller->root_motion_delta[2] = 0.0;
    return delta;
}

/// @brief Atomically read and reset the accumulated root-motion rotation as a Quat.
/// @details Mirror of `_consume_root_motion` for rotation. Returns the
///          quaternion the character should be rotated by this frame to
///          follow the animation's hip-bone yaw / pitch / roll, then
///          resets the accumulator to identity so the next frame starts
///          fresh. Animation systems that drive character heading from
///          root motion call this each frame after `_update`.
void *rt_anim_controller3d_consume_root_motion_rotation(void *obj) {
    rt_anim_controller3d *controller = (rt_anim_controller3d *)obj;
    void *rotation;
    if (!controller)
        return rt_quat_new(0.0, 0.0, 0.0, 1.0);
    rotation = rt_quat_new(controller->root_motion_rotation[0],
                           controller->root_motion_rotation[1],
                           controller->root_motion_rotation[2],
                           controller->root_motion_rotation[3]);
    controller_quat_identity(controller->root_motion_rotation);
    return rotation;
}

/// @brief Set the blend weight of an overlay layer (clamped to [0, 1]). Layer 0 is the base
/// and is always full weight; setting it has no effect. Higher overlays compose additively
/// over the base, masked to their bone subset.
void rt_anim_controller3d_set_layer_weight(void *obj, int64_t layer_index, double weight) {
    rt_anim_controller3d *controller = (rt_anim_controller3d *)obj;
    if (!controller || layer_index < 0 || layer_index >= RT_ANIM_CONTROLLER3D_MAX_LAYERS)
        return;
    if (layer_index == 0) {
        controller->layers[0].weight = 1.0f;
        return;
    }
    if (weight < 0.0)
        weight = 0.0;
    if (weight > 1.0)
        weight = 1.0;
    controller->layers[layer_index].weight = (float)weight;
}

/// @brief Set which bones an overlay layer affects. The layer composites only on `root_bone`
/// and its descendants in the skeleton hierarchy (typical use: arms-only upper-body overlay).
/// Layer 0 always covers the full skeleton.
void rt_anim_controller3d_set_layer_mask(void *obj, int64_t layer_index, int64_t root_bone) {
    rt_anim_controller3d *controller = (rt_anim_controller3d *)obj;
    if (!controller || layer_index < 0 || layer_index >= RT_ANIM_CONTROLLER3D_MAX_LAYERS)
        return;
    if (layer_index == 0) {
        controller_set_all_mask_bits(&controller->layers[0], controller->skeleton->bone_count);
        controller->layers[0].mask_root_bone = -1;
        return;
    }
    controller->layers[layer_index].mask_root_bone = (int32_t)root_bone;
    controller_rebuild_layer_mask(controller, (int32_t)layer_index);
}

/// @brief Play a state on a specific overlay layer (layer_index ≥ 1). No transition blend.
/// Returns 1 on success, 0 if `layer_index` is invalid or `state_name` doesn't exist.
int8_t rt_anim_controller3d_play_layer(void *obj, int64_t layer_index, rt_string state_name) {
    rt_anim_controller3d *controller = (rt_anim_controller3d *)obj;
    int32_t state_index;
    if (!controller || layer_index < 0 || layer_index >= RT_ANIM_CONTROLLER3D_MAX_LAYERS)
        return 0;
    state_index = controller_find_state(controller, state_name);
    if (state_index < 0)
        return 0;
    return controller_set_layer_state(controller, (int32_t)layer_index, state_index, 0.0);
}

/// @brief Cross-fade an overlay layer to `state_name` over `blend_seconds`. Mirrors `_crossfade`
/// but targets a specific layer, leaving the base unaffected.
int8_t rt_anim_controller3d_crossfade_layer(
    void *obj, int64_t layer_index, rt_string state_name, double blend_seconds) {
    rt_anim_controller3d *controller = (rt_anim_controller3d *)obj;
    int32_t state_index;
    if (!controller || layer_index < 0 || layer_index >= RT_ANIM_CONTROLLER3D_MAX_LAYERS)
        return 0;
    state_index = controller_find_state(controller, state_name);
    if (state_index < 0)
        return 0;
    return controller_set_layer_state(controller, (int32_t)layer_index, state_index, blend_seconds);
}

/// @brief Stop playback on a specific overlay layer. Layer 0 (base) keeps its current_state but
/// pauses; overlay layers (≥ 1) are also cleared so they no longer composite onto the base.
void rt_anim_controller3d_stop_layer(void *obj, int64_t layer_index) {
    rt_anim_controller3d *controller = (rt_anim_controller3d *)obj;
    anim_controller3d_layer_t *layer;
    if (!controller || layer_index < 0 || layer_index >= RT_ANIM_CONTROLLER3D_MAX_LAYERS)
        return;
    layer = &controller->layers[layer_index];
    if (layer->player)
        rt_anim_player3d_stop(layer->player);
    if (layer_index != 0)
        layer->current_state = -1;
    layer->transitioning = 0;
    layer->transition_time = 0.0f;
    layer->transition_duration = 0.0f;
}

/// @brief Snapshot the current final-palette matrix for a single bone as a Mat4.
/// @details Returns a freshly allocated Mat4 — caller owns the resulting
///          object. Returns NULL on missing controller / palette / out-of-
///          range bone. The matrix reflects the composited result of all
///          layers as of the most recent `_update`.
void *rt_anim_controller3d_get_bone_matrix(void *obj, int64_t bone_index) {
    rt_anim_controller3d *controller = (rt_anim_controller3d *)obj;
    const float *m;
    if (!controller || !controller->skeleton || !controller->final_palette)
        return NULL;
    if (bone_index < 0 || bone_index >= controller->skeleton->bone_count)
        return NULL;
    m = &controller->final_palette[bone_index * 16];
    return rt_mat4_new(m[0],
                       m[1],
                       m[2],
                       m[3],
                       m[4],
                       m[5],
                       m[6],
                       m[7],
                       m[8],
                       m[9],
                       m[10],
                       m[11],
                       m[12],
                       m[13],
                       m[14],
                       m[15]);
}

/// @brief Direct pointer to the final-palette float buffer (no copy).
/// @details Used by the renderer's GPU-upload path which needs to upload
///          the entire palette in one call. The caller does not own the
///          buffer — it remains valid until the next `_update` overwrites
///          it or the controller is finalised. `bone_count` is filled with
///          the palette length in bones (not floats).
/// @return Pointer to `bone_count * 16` floats, or NULL on missing data.
const float *rt_anim_controller3d_get_final_palette_data(void *obj, int32_t *bone_count) {
    rt_anim_controller3d *controller = (rt_anim_controller3d *)obj;
    if (bone_count)
        *bone_count = 0;
    if (!controller || !controller->skeleton || !controller->final_palette)
        return NULL;
    if (bone_count)
        *bone_count = controller->skeleton->bone_count;
    return controller->final_palette;
}

/// @brief Direct pointer to the *previous* frame's final-palette float buffer.
/// @details Used by motion-vector / temporal-AA renderers that need both
///          this frame's and last frame's bone palettes to compute per-
///          pixel velocity. Returns NULL until at least one `_update` has
///          completed (the prev-palette is populated by snapshotting
///          before the per-layer player update).
const float *rt_anim_controller3d_get_previous_palette_data(void *obj, int32_t *bone_count) {
    rt_anim_controller3d *controller = (rt_anim_controller3d *)obj;
    if (bone_count)
        *bone_count = 0;
    if (!controller || !controller->skeleton || !controller->prev_final_palette ||
        !controller->has_prev_final_palette)
        return NULL;
    if (bone_count)
        *bone_count = controller->skeleton->bone_count;
    return controller->prev_final_palette;
}

#endif
