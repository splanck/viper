//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/anim/rt_animcontroller3d.c
// Purpose: High-level skeletal animation controller backing
//          `Viper.Graphics3D.AnimController3D`. Provides named states,
//          transitions, events, root-motion extraction, and simple
//          masked overlay layers on top of the underlying AnimPlayer3D.
//
// Key invariants:
//   - Up to RT_ANIM_CONTROLLER3D_MAX_LAYERS overlay layers; layer 0 is the base.
//   - Every public entry validates handles via *_checked helpers.
//   - State / transition tables grow on demand via realloc.
//   - The event queue is a fixed-size ring buffer; oldest events drop on overflow.
//
// Ownership/Lifetime:
//   - Controller objects are heap-allocated and GC-managed.
//   - Skeleton and Animation references are retained on assignment, released
//     on slot replacement and during finalize.
//
// Links: src/runtime/graphics/3d/anim/rt_animcontroller3d.h (public API),
//        src/runtime/graphics/3d/anim/rt_skeleton3d.h (underlying AnimPlayer3D),
//        docs/viperlib/graphics/animation.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_animcontroller3d.h"

#include "rt_blendtree3d.h"
#include "rt_graphics3d_ids.h"
#include "rt_iksolver3d.h"
#include "rt_mat4.h"
#include "rt_object.h"
#include "rt_quat.h"
#include "rt_skeleton3d.h"
#include "rt_skeleton3d_internal.h"
#include "rt_string.h"
#include "rt_trap.h"
#include "rt_vec3.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RT_ANIM_CONTROLLER3D_MAX_LAYERS 4
#define RT_ANIM_CONTROLLER3D_EVENT_NAME_MAX 64
#define RT_ANIM_CONTROLLER3D_STATE_NAME_MAX 64
#define RT_ANIM_CONTROLLER3D_EVENT_QUEUE_MAX 64
#define RT_ANIM_CONTROLLER3D_LOD_DISTANCE_MAX 1000000.0
#define RT_ANIM_CONTROLLER3D_LOD_RATE_MAX 240.0
#define RT_ANIM_CONTROLLER3D_ROOT_MOTION_CYCLE_MAX 1024

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
    int8_t additive;
    float weight;
    int32_t mask_root_bone;
    int32_t mask_bone_count_seen;
    uint8_t mask_bits[VGFX3D_MAX_BONES];
} anim_controller3d_layer_t;

typedef struct {
    void *vptr;
    rt_skeleton3d *skeleton;

    anim_controller3d_state_t *states;
    int32_t state_count;
    int32_t state_capacity;
    uint64_t *state_name_hashes;
    int32_t *state_name_indices;
    int32_t state_name_index_capacity;
    int8_t state_name_index_dirty;

    anim_controller3d_transition_t *transitions;
    int32_t transition_count;
    int32_t transition_capacity;

    anim_controller3d_event_t *events;
    int32_t event_count;
    int32_t event_capacity;

    anim_controller3d_layer_t layers[RT_ANIM_CONTROLLER3D_MAX_LAYERS];
    void *blend_tree;
    void *ik_solver;

    float *final_palette;
    float *final_globals;
    float *prev_final_palette;
    int8_t has_prev_final_palette;
    double root_motion_delta[3];
    double root_motion_rotation[4];
    double animation_lod_distance;
    double animation_lod_rate_hz;
    double animation_lod_accum;
    int32_t animation_lod_max_bones; /* >0 freezes bones at/after this index to bind-local (LOD) */
    int32_t root_motion_bone;

    char event_queue[RT_ANIM_CONTROLLER3D_EVENT_QUEUE_MAX][RT_ANIM_CONTROLLER3D_EVENT_NAME_MAX];
    int32_t event_queue_head;
    int32_t event_queue_count;
} rt_anim_controller3d;

/// @brief Validate that @p obj is a live AnimController3D handle, returning NULL otherwise.
static rt_anim_controller3d *anim_controller3d_checked(void *obj) {
    return (rt_anim_controller3d *)rt_g3d_checked_or_null(obj, RT_G3D_ANIMCONTROLLER3D_CLASS_ID);
}

/// @brief Validate that @p obj is a live Skeleton3D handle, returning NULL otherwise.
static rt_skeleton3d *skeleton3d_checked(void *obj) {
    return (rt_skeleton3d *)rt_g3d_checked_or_null(obj, RT_G3D_SKELETON3D_CLASS_ID);
}

/// @brief Validate that @p obj is a live Animation3D handle, returning NULL otherwise.
static rt_animation3d *animation3d_checked(void *obj) {
    return (rt_animation3d *)rt_g3d_checked_or_null(obj, RT_G3D_ANIMATION3D_CLASS_ID);
}

/// @brief Narrow a double to float, returning `fallback` for non-finite input and
///   saturating to ±FLT_MAX on overflow (keeps NaN/Inf out of the float pipeline).
static float controller_clamp_to_float(double value, float fallback) {
    if (!isfinite(value))
        return fallback;
    if (value > (double)FLT_MAX)
        return FLT_MAX;
    if (value < -(double)FLT_MAX)
        return -FLT_MAX;
    return (float)value;
}

/// @brief Narrow a double to a non-negative float; non-finite or ≤0 yields 0, overflow
///   saturates to FLT_MAX. For durations/speeds that must stay ≥ 0.
static float controller_clamp_nonnegative_float(double value) {
    if (!isfinite(value) || value <= 0.0)
        return 0.0f;
    if (value > (double)FLT_MAX)
        return FLT_MAX;
    return (float)value;
}

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

/// @brief Release a retained controller-owned object only when its class still matches.
static void controller_release_class_ref(void **slot, int64_t class_id) {
    if (!slot || !*slot)
        return;
    if (!rt_g3d_has_class(*slot, class_id)) {
        *slot = NULL;
        return;
    }
    controller_release_ref(slot);
}

/// @brief Return a C string only for a valid runtime string handle.
static const char *controller_string_cstr_or_null(rt_string name) {
    if (!name || !rt_string_is_handle(name))
        return NULL;
    return rt_string_cstr(name);
}

/// @brief Copy an `rt_string` into a fixed-size character buffer, NUL-terminating.
/// @details Truncates if the source exceeds `cap - 1` bytes; produces an
///          empty string when `name` is NULL or the buffer is zero-sized.
///          Used to snapshot state and event names into the fixed-size
///          arrays in `anim_controller3d_state_t` / `_event_t`.
static void controller_copy_name(char *dst, size_t cap, rt_string name) {
    const char *src = controller_string_cstr_or_null(name);
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
    if (!buffer || !capacity || need <= 0 || elem_size == 0)
        return 0;
    if (*capacity < 0)
        return 0;
    if (*capacity >= need)
        return 1;
    if (*capacity > INT32_MAX / 2)
        new_capacity = need;
    else
        new_capacity = *capacity > 0 ? (*capacity * 2) : 4;
    if (new_capacity < need)
        new_capacity = need;
    if ((size_t)new_capacity > SIZE_MAX / elem_size)
        return 0;
    grown = realloc(*buffer, (size_t)new_capacity * elem_size);
    if (!grown)
        return 0;
    *buffer = grown;
    *capacity = new_capacity;
    return 1;
}

/// @brief Clamp a (count, capacity) pair to a safe element count: 0 when the array or sizes
///   are invalid, otherwise min(count, capacity).
static int32_t controller_clamped_array_count(const void *items,
                                              int32_t count,
                                              int32_t capacity) {
    if (!items || count <= 0 || capacity <= 0)
        return 0;
    if (count > capacity)
        return capacity;
    return count;
}

/// @brief Number of animation states safe to read directly (live count clamped to capacity).
static int32_t controller_safe_state_count(const rt_anim_controller3d *controller) {
    return controller ? controller_clamped_array_count(controller->states,
                                                      controller->state_count,
                                                      controller->state_capacity)
                      : 0;
}

/// @brief Number of transitions safe to read directly (live count clamped to capacity).
static int32_t controller_safe_transition_count(const rt_anim_controller3d *controller) {
    return controller ? controller_clamped_array_count(controller->transitions,
                                                      controller->transition_count,
                                                      controller->transition_capacity)
                      : 0;
}

/// @brief Number of events safe to read directly (live count clamped to capacity).
static int32_t controller_safe_event_count(const rt_anim_controller3d *controller) {
    return controller ? controller_clamped_array_count(controller->events,
                                                      controller->event_count,
                                                      controller->event_capacity)
                      : 0;
}

/// @brief Normalize the state-table count/capacity invariants: reset both when the array is
///   absent, clamp a negative capacity, and re-clamp the count (marks the name index dirty).
static void controller_repair_state_table(rt_anim_controller3d *controller) {
    if (!controller)
        return;
    if (!controller->states) {
        controller->state_count = 0;
        controller->state_capacity = 0;
        controller->state_name_index_dirty = 1;
        return;
    }
    if (controller->state_capacity < 0)
        controller->state_capacity = 0;
    controller->state_count = controller_safe_state_count(controller);
}

/// @brief Normalize the transition-table count/capacity invariants (defensive clamp).
static void controller_repair_transition_table(rt_anim_controller3d *controller) {
    if (!controller)
        return;
    if (!controller->transitions) {
        controller->transition_count = 0;
        controller->transition_capacity = 0;
        return;
    }
    if (controller->transition_capacity < 0)
        controller->transition_capacity = 0;
    controller->transition_count = controller_safe_transition_count(controller);
}

/// @brief Normalize the event-table count/capacity invariants (defensive clamp).
static void controller_repair_event_table(rt_anim_controller3d *controller) {
    if (!controller)
        return;
    if (!controller->events) {
        controller->event_count = 0;
        controller->event_capacity = 0;
        return;
    }
    if (controller->event_capacity < 0)
        controller->event_capacity = 0;
    controller->event_count = controller_safe_event_count(controller);
}

/// @brief Clamp every animation layer's current/previous state indices, transition timers,
///   and blend weight into valid ranges (layer 0 is forced to full weight).
static void controller_repair_layer_state_indices(rt_anim_controller3d *controller) {
    int32_t state_count;
    if (!controller)
        return;
    state_count = controller_safe_state_count(controller);
    for (int32_t layer_index = 0; layer_index < RT_ANIM_CONTROLLER3D_MAX_LAYERS; layer_index++) {
        anim_controller3d_layer_t *layer = &controller->layers[layer_index];
        if (layer->current_state < 0 || layer->current_state >= state_count) {
            layer->current_state = -1;
            layer->previous_state = -1;
            layer->transitioning = 0;
            layer->transition_time = 0.0f;
            layer->transition_duration = 0.0f;
        } else if (layer->previous_state < 0 || layer->previous_state >= state_count) {
            layer->previous_state = -1;
        }
        if (!isfinite((double)layer->transition_time) || layer->transition_time < 0.0f)
            layer->transition_time = 0.0f;
        if (!isfinite((double)layer->transition_duration) ||
            layer->transition_duration < 0.0f)
            layer->transition_duration = 0.0f;
        if (layer->transition_duration <= 0.0f) {
            layer->transitioning = 0;
            layer->transition_time = 0.0f;
        } else if (layer->transition_time >= layer->transition_duration) {
            layer->transitioning = 0;
            layer->transition_time = layer->transition_duration;
        }
        if (!isfinite((double)layer->weight) || layer->weight < 0.0f)
            layer->weight = layer_index == 0 ? 1.0f : 0.0f;
        else if (layer->weight > 1.0f)
            layer->weight = 1.0f;
        if (layer_index == 0)
            layer->weight = 1.0f;
    }
}

/// @brief True if @p value is a positive power of two.
static int controller_is_power_of_two_i32(int32_t value) {
    return value > 0 && (value & (value - 1)) == 0;
}

/// @brief Number of skeleton bones safe to read (delegates to skeleton3d_safe_bone_count).
static int32_t controller_safe_bone_count(const rt_skeleton3d *skeleton) {
    return skeleton3d_safe_bone_count(skeleton);
}

/// @brief Clamp the event ring-buffer's head/tail/size indices back into range after any
///   potential corruption, keeping the queue traversable.
static void controller_sanitize_event_queue(rt_anim_controller3d *controller) {
    if (!controller)
        return;
    if (controller->event_queue_head < 0 ||
        controller->event_queue_head >= RT_ANIM_CONTROLLER3D_EVENT_QUEUE_MAX)
        controller->event_queue_head = 0;
    if (controller->event_queue_count < 0)
        controller->event_queue_count = 0;
    if (controller->event_queue_count > RT_ANIM_CONTROLLER3D_EVENT_QUEUE_MAX)
        controller->event_queue_count = RT_ANIM_CONTROLLER3D_EVENT_QUEUE_MAX;
}

/// @brief FNV-1a 64-bit hash of a state name, used as the key in the open-addressing
///   name index. Returns 0 for a NULL name; otherwise a non-zero hash (a hash that would
///   be 0 is remapped to 1 so 0 stays reserved as the empty-slot sentinel).
static uint64_t controller_hash_name_cstr(const char *name) {
    uint64_t hash = UINT64_C(1469598103934665603);
    if (!name)
        return 0u;
    while (*name) {
        hash ^= (unsigned char)*name++;
        hash *= UINT64_C(1099511628211);
    }
    return hash ? hash : 1u;
}

/// @brief Rebuild the open-addressing (linear-probe) name index that maps state-name
///   hashes to state-array slots, growing to a power-of-two capacity sized to keep the
///   load factor below 50%, and clear the dirty flag.
/// @return 1 on success, 0 if `controller` is NULL or an index allocation fails.
static int controller_rebuild_state_name_index(rt_anim_controller3d *controller) {
    int32_t capacity = 16;
    int32_t state_count;
    uint64_t *hashes;
    int32_t *indices;
    if (!controller)
        return 0;
    controller_repair_state_table(controller);
    state_count = controller->state_count;
    while (capacity < state_count * 2) {
        if (capacity > INT32_MAX / 2)
            return 0;
        capacity *= 2;
    }
    if (capacity != controller->state_name_index_capacity) {
        hashes = (uint64_t *)calloc((size_t)capacity, sizeof(*hashes));
        indices = (int32_t *)malloc((size_t)capacity * sizeof(*indices));
        if (!hashes || !indices) {
            free(hashes);
            free(indices);
            return 0;
        }
        free(controller->state_name_hashes);
        free(controller->state_name_indices);
        controller->state_name_hashes = hashes;
        controller->state_name_indices = indices;
        controller->state_name_index_capacity = capacity;
    } else {
        memset(controller->state_name_hashes,
               0,
               (size_t)capacity * sizeof(*controller->state_name_hashes));
    }
    for (int32_t i = 0; i < capacity; ++i)
        controller->state_name_indices[i] = -1;
    for (int32_t i = 0; i < state_count; ++i) {
        uint64_t hash = controller_hash_name_cstr(controller->states[i].name);
        int32_t mask = capacity - 1;
        for (int32_t probe = 0; probe < capacity; ++probe) {
            int32_t slot = (int32_t)((hash + (uint64_t)probe) & (uint64_t)mask);
            if (controller->state_name_hashes[slot] == 0u) {
                controller->state_name_hashes[slot] = hash;
                controller->state_name_indices[slot] = i;
                break;
            }
        }
    }
    controller->state_name_index_dirty = 0;
    return 1;
}

/// @brief Find a state by name, using an open-addressed name index for larger controllers.
/// @return State index, or -1 if `name` is NULL/empty or no match.
static int32_t controller_find_state(rt_anim_controller3d *controller, rt_string name) {
    char target_buf[RT_ANIM_CONTROLLER3D_STATE_NAME_MAX];
    const char *target = target_buf;
    int32_t state_count;
    if (!controller || !name)
        return -1;
    controller_repair_state_table(controller);
    state_count = controller->state_count;
    controller_copy_name(target_buf, sizeof(target_buf), name);
    if (target_buf[0] == '\0')
        return -1;
    if (state_count >= 16) {
        uint64_t hash = controller_hash_name_cstr(target);
        if ((controller->state_name_index_dirty || controller->state_name_index_capacity <= 0 ||
             !controller_is_power_of_two_i32(controller->state_name_index_capacity) ||
             !controller->state_name_hashes || !controller->state_name_indices) &&
            !controller_rebuild_state_name_index(controller)) {
            goto linear_scan;
        }
        if (controller->state_name_hashes && controller->state_name_indices) {
            int32_t mask = controller->state_name_index_capacity - 1;
            for (int32_t probe = 0; probe < controller->state_name_index_capacity; ++probe) {
                int32_t slot = (int32_t)((hash + (uint64_t)probe) & (uint64_t)mask);
                int32_t index;
                if (controller->state_name_hashes[slot] == 0u)
                    return -1;
                index = controller->state_name_indices[slot];
                if (controller->state_name_hashes[slot] == hash && index >= 0 &&
                    index < state_count &&
                    strcmp(controller->states[index].name, target) == 0)
                    return index;
            }
            return -1;
        }
    }
linear_scan:
    for (int32_t i = 0; i < state_count; i++) {
        if (strcmp(controller->states[i].name, target) == 0)
            return i;
    }
    return -1;
}

/// @brief Linear-scan the transition table for an exact `(from, to)` pair.
/// @return Transition index, or -1 if no match is registered.
static int32_t controller_find_transition(const rt_anim_controller3d *controller,
                                          int32_t from_state,
                                          int32_t to_state) {
    if (!controller)
        return -1;
    for (int32_t i = 0, count = controller_safe_transition_count(controller); i < count; i++) {
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
    controller_sanitize_event_queue(controller);
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
    int32_t event_count;
    if (!controller || state_index < 0 || state_index >= controller_safe_state_count(controller))
        return;
    event_count = controller_safe_event_count(controller);
    for (int32_t i = 0; i < event_count; i++) {
        if (controller->events[i].state_index == state_index &&
            controller->events[i].time_seconds <= 1e-6f) {
            controller_enqueue_event(controller, controller->events[i].name);
        }
    }
}

/// @brief Fire state events crossed by the last playback step.
/// @details Three cases:
///          - **Zero-duration animation** (no real timeline): any event with
///            `time_seconds <= curr_time` fires. Treats the animation as a
///            single-frame impulse.
///          - **Non-looping**: half-open forward/reverse windows fire only
///            events crossed by the clamped step.
///          - **Looping**: forward and reverse wrap split the window at the
///            loop boundary. A step that spans at least one full duration
///            fires each registered event once.
///          Lower-bound exclusivity prevents an event exactly at `prev_time`
///          from firing twice on consecutive ticks.
static void controller_process_events(rt_anim_controller3d *controller,
                                      int32_t state_index,
                                      double prev_time,
                                      double curr_time,
                                      double duration,
                                      int8_t looping,
                                      double elapsed_time) {
    int32_t event_count;
    if (!controller || state_index < 0 || state_index >= controller_safe_state_count(controller))
        return;
    if (duration <= 0.0) {
        return;
    }
    event_count = controller_safe_event_count(controller);
    for (int32_t i = 0; i < event_count; i++) {
        const anim_controller3d_event_t *event = &controller->events[i];
        double t;
        if (event->state_index != state_index)
            continue;
        t = (double)event->time_seconds;
        if (!looping) {
            if (elapsed_time >= 0.0) {
                if (t > prev_time && t <= curr_time)
                    controller_enqueue_event(controller, event->name);
            } else if (t < prev_time && t >= curr_time) {
                controller_enqueue_event(controller, event->name);
            }
            continue;
        }
        if (isfinite(elapsed_time) && fabs(elapsed_time) >= duration) {
            controller_enqueue_event(controller, event->name);
            continue;
        }
        if (elapsed_time < 0.0) {
            if (curr_time <= prev_time) {
                if (t < prev_time && t >= curr_time)
                    controller_enqueue_event(controller, event->name);
            } else {
                if (t < prev_time || t >= curr_time)
                    controller_enqueue_event(controller, event->name);
            }
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
    if (bone_count < 0)
        bone_count = 0;
    if (bone_count > VGFX3D_MAX_BONES)
        bone_count = VGFX3D_MAX_BONES;
    memset(layer->mask_bits, 0, sizeof(layer->mask_bits));
    for (int32_t bone = 0; bone < bone_count; bone++)
        layer->mask_bits[bone] = 1;
    layer->mask_bone_count_seen = bone_count;
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
    bone_count = controller_safe_bone_count(controller->skeleton);
    if (layer_index == 0 || layer->mask_root_bone < 0 || layer->mask_root_bone >= bone_count) {
        controller_set_all_mask_bits(layer, bone_count);
        return;
    }
    memset(layer->mask_bits, 0, sizeof(layer->mask_bits));
    for (int32_t bone = 0; bone < bone_count; bone++) {
        int32_t current = bone;
        int32_t depth = 0;
        while (current >= 0 && current < bone_count && depth++ < bone_count) {
            if (current == layer->mask_root_bone) {
                layer->mask_bits[bone] = 1;
                break;
            }
            current = controller->skeleton->bones[current].parent_index;
        }
    }
    layer->mask_bone_count_seen = bone_count;
}

/// @brief Push a state's per-state speed and looping settings into a player.
/// @details Always enables the player's loop-override so the controller's
///          per-state looping decision wins over whatever the underlying
///          Animation3D defaults to.
static void controller_apply_state_settings(rt_anim_player3d *player,
                                            const anim_controller3d_state_t *state) {
    if (!player || !state)
        return;
    player->loop_override_enabled = 1;
    player->loop_override_value = state->looping ? 1 : 0;
    rt_anim_player3d_set_speed(player, state->speed);
}

static void controller_compute_final_palette(rt_anim_controller3d *controller);

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
    blend_seconds = (double)controller_clamp_nonnegative_float(blend_seconds);
    if (state_index < 0 || state_index >= controller_safe_state_count(controller))
        return 0;
    layer = &controller->layers[layer_index];
    state = &controller->states[state_index];
    if (!layer->player || !state->animation)
        return 0;

    use_crossfade = blend_seconds > 0.0 && layer->current_state >= 0 &&
                    layer->current_state != state_index &&
                    rt_anim_player3d_is_playing(layer->player);

    layer->previous_state = layer->current_state;
    layer->current_state = state_index;
    if (use_crossfade) {
        rt_anim_player3d_crossfade(layer->player, state->animation, blend_seconds);
        controller_apply_state_settings(layer->player, state);
        layer->transitioning = 1;
        layer->transition_time = 0.0f;
        layer->transition_duration = controller_clamp_nonnegative_float(blend_seconds);
    } else {
        controller_apply_state_settings(layer->player, state);
        rt_anim_player3d_play(layer->player, state->animation);
        layer->transitioning = 0;
        layer->transition_time = 0.0f;
        layer->transition_duration = 0.0f;
    }
    rt_anim_player3d_update(layer->player, 0.0);
    controller_compute_final_palette(controller);
    controller_fire_entry_events(controller, state_index);
    return 1;
}

/// @brief Multiply two row-major 4x4 float matrices.
static void controller_mat4f_mul(const float *a, const float *b, float *out) {
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            out[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c] +
                             a[r * 4 + 2] * b[2 * 4 + c] + a[r * 4 + 3] * b[3 * 4 + c];
        }
    }
}

/// @brief Reset final globals to bind pose and final palette to skinning identity.
static void controller_bind_pose_palette(rt_anim_controller3d *controller) {
    int32_t bone_count;
    float bind_locals[VGFX3D_MAX_BONES * 16];
    if (!controller || !controller->final_palette || !controller->final_globals ||
        !controller->skeleton)
        return;
    bone_count = controller_safe_bone_count(controller->skeleton);
    if (bone_count <= 0)
        return;
    for (int32_t bone = 0; bone < bone_count; bone++)
        memcpy(&bind_locals[bone * 16],
               controller->skeleton->bones[bone].bind_pose_local,
               16 * sizeof(float));
    skeleton3d_compute_globals_from_locals(
        controller->skeleton, bind_locals, controller->final_globals, bone_count);
    for (int32_t bone = 0; bone < bone_count; bone++) {
        controller_mat4f_mul(&controller->final_globals[bone * 16],
                             controller->skeleton->bones[bone].inverse_bind,
                             &controller->final_palette[bone * 16]);
    }
}

static void controller_quat_from_matrix_rows(double m00,
                                             double m01,
                                             double m02,
                                             double m10,
                                             double m11,
                                             double m12,
                                             double m20,
                                             double m21,
                                             double m22,
                                             double *out);

/// @brief Spherical-linear interpolate two unit quaternions (float lanes).
/// @details Flips `b` when the dot product is negative so the shorter arc is
///          taken, then falls back to a plain normalized lerp for nearly
///          parallel inputs (dot > 0.9995) where the `sin(theta)` divisor would
///          approach zero. The result is renormalized, with an identity-quaternion
///          guard for the degenerate zero-length case.
static void controller_quat_slerp_float(const float *a, const float *b, float t, float *out) {
    float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    float nb[4] = {b[0], b[1], b[2], b[3]};
    if (!isfinite(t) || t <= 0.0f)
        t = 0.0f;
    else if (t >= 1.0f)
        t = 1.0f;
    if (dot < 0.0f) {
        dot = -dot;
        nb[0] = -nb[0];
        nb[1] = -nb[1];
        nb[2] = -nb[2];
        nb[3] = -nb[3];
    }
    if (!isfinite(dot)) {
        out[0] = out[1] = out[2] = 0.0f;
        out[3] = 1.0f;
        return;
    }
    if (dot > 1.0f)
        dot = 1.0f;
    if (dot < -1.0f)
        dot = -1.0f;
    if (dot > 0.9995f) {
        for (int32_t i = 0; i < 4; i++)
            out[i] = a[i] + t * (nb[i] - a[i]);
    } else {
        float theta = acosf(dot);
        float sin_theta = sinf(theta);
        float wa = sinf((1.0f - t) * theta) / sin_theta;
        float wb = sinf(t * theta) / sin_theta;
        for (int32_t i = 0; i < 4; i++)
            out[i] = wa * a[i] + wb * nb[i];
    }
    float len = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
    if (!isfinite(len) || len <= 1e-8f) {
        out[0] = out[1] = out[2] = 0.0f;
        out[3] = 1.0f;
        return;
    }
    for (int32_t i = 0; i < 4; i++)
        out[i] /= len;
}

/// @brief Quaternion conjugate (negated vector part) — the inverse for a unit quaternion.
static void controller_quat_conjugate_float(const float *q, float *out) {
    if (!q || !out)
        return;
    out[0] = -q[0];
    out[1] = -q[1];
    out[2] = -q[2];
    out[3] = q[3];
}

/// @brief Hamilton product out = a * b (apply b then a), normalized; identity on degenerate result.
static void controller_quat_mul_float(const float *a, const float *b, float *out) {
    float w;
    float x;
    float y;
    float z;
    float len;
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
    len = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
    if (!isfinite(len) || len <= 1e-8f) {
        out[0] = out[1] = out[2] = 0.0f;
        out[3] = 1.0f;
        return;
    }
    for (int32_t i = 0; i < 4; i++)
        out[i] /= len;
}

/// @brief Compose a row-major 4x4 TRS matrix from translation, rotation
///        (quaternion) and scale (all float lanes).
/// @details Standard quaternion-to-basis expansion with each basis column
///          pre-multiplied by its scale component and the translation written
///          into the 4th column.
static void controller_build_trs_float(const float *pos,
                                       const float *quat,
                                       const float *scl,
                                       float *out) {
    float x = quat[0], y = quat[1], z = quat[2], w = quat[3];
    float x2 = x + x, y2 = y + y, z2 = z + z;
    float xx = x * x2, xy = x * y2, xz = x * z2;
    float yy = y * y2, yz = y * z2, zz = z * z2;
    float wx = w * x2, wy = w * y2, wz = w * z2;
    out[0] = (1.0f - (yy + zz)) * scl[0];
    out[1] = (xy - wz) * scl[1];
    out[2] = (xz + wy) * scl[2];
    out[3] = pos[0];
    out[4] = (xy + wz) * scl[0];
    out[5] = (1.0f - (xx + zz)) * scl[1];
    out[6] = (yz - wx) * scl[2];
    out[7] = pos[1];
    out[8] = (xz - wy) * scl[0];
    out[9] = (yz + wx) * scl[1];
    out[10] = (1.0f - (xx + yy)) * scl[2];
    out[11] = pos[2];
    out[12] = out[13] = out[14] = 0.0f;
    out[15] = 1.0f;
}

/// @brief Decompose a row-major 4x4 matrix into translation, rotation
///        (quaternion) and scale (all float lanes).
/// @details Scale per axis is the length of each basis column, guarded to 1.0
///          when near-zero or non-finite so the subsequent divide that
///          orthonormalizes the basis before quaternion extraction stays finite.
static void controller_decompose_trs_float(const float *m,
                                           float *out_pos,
                                           float *out_rot,
                                           float *out_scl) {
    double rot[4];
    double sx = sqrt((double)m[0] * (double)m[0] + (double)m[4] * (double)m[4] +
                     (double)m[8] * (double)m[8]);
    double sy = sqrt((double)m[1] * (double)m[1] + (double)m[5] * (double)m[5] +
                     (double)m[9] * (double)m[9]);
    double sz = sqrt((double)m[2] * (double)m[2] + (double)m[6] * (double)m[6] +
                     (double)m[10] * (double)m[10]);
    out_pos[0] = isfinite(m[3]) ? m[3] : 0.0f;
    out_pos[1] = isfinite(m[7]) ? m[7] : 0.0f;
    out_pos[2] = isfinite(m[11]) ? m[11] : 0.0f;
    out_scl[0] = isfinite(sx) && sx > 1e-6 && sx <= (double)FLT_MAX ? (float)sx : 1.0f;
    out_scl[1] = isfinite(sy) && sy > 1e-6 && sy <= (double)FLT_MAX ? (float)sy : 1.0f;
    out_scl[2] = isfinite(sz) && sz > 1e-6 && sz <= (double)FLT_MAX ? (float)sz : 1.0f;
    controller_quat_from_matrix_rows((double)m[0] / out_scl[0],
                                     (double)m[1] / out_scl[1],
                                     (double)m[2] / out_scl[2],
                                     (double)m[4] / out_scl[0],
                                     (double)m[5] / out_scl[1],
                                     (double)m[6] / out_scl[2],
                                     (double)m[8] / out_scl[0],
                                     (double)m[9] / out_scl[1],
                                     (double)m[10] / out_scl[2],
                                     rot);
    out_rot[0] = (float)rot[0];
    out_rot[1] = (float)rot[1];
    out_rot[2] = (float)rot[2];
    out_rot[3] = (float)rot[3];
}

/// @brief Find the animation channel targeting @p bone_index, or NULL if the clip has
///   no channel for that bone.
static const vgfx3d_anim_channel_t *controller_find_animation_channel(const rt_animation3d *anim,
                                                                      int32_t bone_index) {
    int32_t channel_count;
    if (!anim || bone_index < 0)
        return NULL;
    channel_count = animation3d_safe_channel_count(anim);
    for (int32_t i = 0; i < channel_count; i++) {
        if (anim->channels[i].bone_index == bone_index)
            return &anim->channels[i];
    }
    return NULL;
}

/// @brief Return @p value when finite, else @p fallback (float scalar sanitizer).
static float controller_finite_float_or(float value, float fallback) {
    return isfinite(value) ? value : fallback;
}

/// @brief Normalize a float quaternion in place, falling back to @p fallback (then to identity)
///   when its length is non-finite or ~zero.
static void controller_normalize_quat_float_or(float *q, const float *fallback) {
    float len;
    if (!q)
        return;
    len = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (!isfinite(len) || len <= 1e-8f) {
        if (fallback) {
            memcpy(q, fallback, 4 * sizeof(float));
            len = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
        }
        if (!fallback || !isfinite(len) || len <= 1e-8f) {
            q[0] = q[1] = q[2] = 0.0f;
            q[3] = 1.0f;
            return;
        }
    }
    for (int32_t i = 0; i < 4; i++)
        q[i] /= len;
}

/// @brief Sanitize a TRS sample in place: finite-guard each position/scale lane (using the
///   supplied fallbacks) and re-normalize the rotation quaternion.
static void controller_sanitize_trs_sample(float *pos,
                                           float *rot,
                                           float *scl,
                                           const float *fallback_pos,
                                           const float *fallback_rot,
                                           const float *fallback_scl) {
    if (!pos || !rot || !scl)
        return;
    for (int32_t i = 0; i < 3; i++) {
        pos[i] = controller_finite_float_or(pos[i], fallback_pos ? fallback_pos[i] : 0.0f);
        scl[i] = controller_finite_float_or(scl[i], fallback_scl ? fallback_scl[i] : 1.0f);
        if (fabsf(scl[i]) <= 1e-8f)
            scl[i] = fallback_scl && isfinite(fallback_scl[i]) && fabsf(fallback_scl[i]) > 1e-8f
                         ? fallback_scl[i]
                         : 1.0f;
    }
    for (int32_t i = 0; i < 4; i++)
        rot[i] = controller_finite_float_or(rot[i], fallback_rot ? fallback_rot[i] : (i == 3 ? 1.0f : 0.0f));
    controller_normalize_quat_float_or(rot, fallback_rot);
}

/// @brief Resolve a keyframe's translation/rotation/scale, substituting the bind-pose
///   fallback for any channel component the keyframe's presence masks leave unset
///   (rotation is all-or-nothing: only used when all four quaternion lanes are present).
static void controller_keyframe_effective_trs(const vgfx3d_keyframe_t *key,
                                              const float *fallback_pos,
                                              const float *fallback_rot,
                                              const float *fallback_scl,
                                              float *out_pos,
                                              float *out_rot,
                                              float *out_scl) {
    if (!key)
        return;
    for (int32_t i = 0; i < 3; i++) {
        uint8_t bit = (uint8_t)(1u << i);
        out_pos[i] = (key->position_mask & bit) ? key->position[i] : fallback_pos[i];
        out_scl[i] = (key->scale_mask & bit) ? key->scale_xyz[i] : fallback_scl[i];
    }
    if ((key->rotation_mask & 0x0Fu) == 0x0Fu)
        memcpy(out_rot, key->rotation, 4 * sizeof(float));
    else
        memcpy(out_rot, fallback_rot, 4 * sizeof(float));
    controller_sanitize_trs_sample(out_pos, out_rot, out_scl, fallback_pos, fallback_rot, fallback_scl);
}

/// @brief Sample a channel's TRS at @p time, interpolating between the surrounding
///   keyframes (linear for position/scale, SLERP for rotation). Empty channels return
///   the fallback bind-pose; single-keyframe channels return that key directly.
static void controller_sample_channel_trs(const vgfx3d_anim_channel_t *channel,
                                          float time,
                                          const float *fallback_pos,
                                          const float *fallback_rot,
                                          const float *fallback_scl,
                                          float *out_pos,
                                          float *out_rot,
                                          float *out_scl) {
    int32_t keyframe_count;
    if (!isfinite(time))
        time = 0.0f;
    keyframe_count = animation3d_safe_keyframe_count(channel);
    if (!channel || keyframe_count <= 0 || !channel->keyframes) {
        memcpy(out_pos, fallback_pos, 3 * sizeof(float));
        memcpy(out_rot, fallback_rot, 4 * sizeof(float));
        memcpy(out_scl, fallback_scl, 3 * sizeof(float));
        return;
    }
    if (keyframe_count == 1) {
        controller_keyframe_effective_trs(&channel->keyframes[0],
                                          fallback_pos,
                                          fallback_rot,
                                          fallback_scl,
                                          out_pos,
                                          out_rot,
                                          out_scl);
        return;
    }

    if (time <= channel->keyframes[0].time) {
        controller_keyframe_effective_trs(&channel->keyframes[0],
                                          fallback_pos,
                                          fallback_rot,
                                          fallback_scl,
                                          out_pos,
                                          out_rot,
                                          out_scl);
        return;
    }
    if (time >= channel->keyframes[keyframe_count - 1].time) {
        controller_keyframe_effective_trs(&channel->keyframes[keyframe_count - 1],
                                          fallback_pos,
                                          fallback_rot,
                                          fallback_scl,
                                          out_pos,
                                          out_rot,
                                          out_scl);
        return;
    }

    int32_t k0 = 0;
    int32_t k1 = keyframe_count - 1;
    while (k1 - k0 > 1) {
        int32_t mid = k0 + (k1 - k0) / 2;
        if (channel->keyframes[mid].time <= time)
            k0 = mid;
        else
            k1 = mid;
    }
    float t0 = channel->keyframes[k0].time;
    float t1 = channel->keyframes[k1].time;
    float alpha = (t1 > t0) ? (time - t0) / (t1 - t0) : 0.0f;
    if (!isfinite(alpha) || alpha < 0.0f)
        alpha = 0.0f;
    if (alpha > 1.0f)
        alpha = 1.0f;

    float pos0[3], rot0[4], scl0[3];
    float pos1[3], rot1[4], scl1[3];
    controller_keyframe_effective_trs(
        &channel->keyframes[k0], fallback_pos, fallback_rot, fallback_scl, pos0, rot0, scl0);
    controller_keyframe_effective_trs(
        &channel->keyframes[k1], fallback_pos, fallback_rot, fallback_scl, pos1, rot1, scl1);
    for (int32_t i = 0; i < 3; i++) {
        out_pos[i] = pos0[i] + (pos1[i] - pos0[i]) * alpha;
        out_scl[i] = scl0[i] + (scl1[i] - scl0[i]) * alpha;
    }
    controller_quat_slerp_float(rot0, rot1, alpha, out_rot);
    controller_sanitize_trs_sample(out_pos, out_rot, out_scl, fallback_pos, fallback_rot, fallback_scl);
}

/// @brief Compute one bone's local transform matrix for @p animation at @p time: sample
///   the bone's channel (falling back to its bind pose) and recompose into @p out_local.
static void controller_sample_animation_local_matrix(const rt_skeleton3d *skeleton,
                                                     const rt_animation3d *animation,
                                                     int32_t bone_index,
                                                     float time,
                                                     float *out_local) {
    float fallback_pos[3], fallback_rot[4], fallback_scl[3];
    float pos[3], rot[4], scl[3];
    const float *bind_local = skeleton->bones[bone_index].bind_pose_local;
    controller_decompose_trs_float(bind_local, fallback_pos, fallback_rot, fallback_scl);
    controller_sample_channel_trs(controller_find_animation_channel(animation, bone_index),
                                  time,
                                  fallback_pos,
                                  fallback_rot,
                                  fallback_scl,
                                  pos,
                                  rot,
                                  scl);
    controller_build_trs_float(pos, rot, scl, out_local);
}

/// @brief Compute the world-space matrix of @p bone_index for the given state at @p time
///   by sampling every bone's local transform and walking the parent chain; returns 0
///   on invalid controller/state/bone, else 1 with the result in @p out_global.
static int controller_sample_state_global_matrix(const rt_anim_controller3d *controller,
                                                 int32_t state_index,
                                                 float time,
                                                 int32_t bone_index,
                                                 float *out_global) {
    float globals[VGFX3D_MAX_BONES * 16];
    float locals[VGFX3D_MAX_BONES * 16];
    int32_t state_count;
    int32_t bone_count;
    rt_animation3d *animation;
    if (!controller || !controller->skeleton || !out_global)
        return 0;
    state_count = controller_safe_state_count(controller);
    bone_count = controller_safe_bone_count(controller->skeleton);
    if (state_index < 0 || state_index >= state_count)
        return 0;
    if (bone_index < 0 || bone_index >= bone_count)
        return 0;
    animation = controller->states[state_index].animation;
    if (!animation)
        return 0;
    for (int32_t bone = 0; bone < bone_count; bone++) {
        controller_sample_animation_local_matrix(
            controller->skeleton, animation, bone, time, &locals[bone * 16]);
    }
    skeleton3d_compute_globals_from_locals(controller->skeleton, locals, globals, bone_count);
    memcpy(out_global, &globals[bone_index * 16], 16 * sizeof(float));
    return 1;
}

/// @brief Composite the base layer + weighted overlay layers into the final bone palette.
/// @details Two-pass blend:
///          1. Base layer (layer 0) is copied wholesale into `final_palette`.
///             If the base has no animation, identity matrices are used so
///             the skeleton renders in its rest pose.
///          2. For each overlay layer (1..MAX-1) with weight > 0, blend the
///             overlay's palette into `final_palette` at the layer's weight,
///             but only on bones whose `mask_bits` flag is set. The blend
///             decomposes each matrix to TRS, lerps translation/scale, and
///             slerps rotation before recomposition.
///          Weight is clamped to `[0, 1]`. Layers with empty masks contribute
///          nothing.
static void controller_compute_final_palette(rt_anim_controller3d *controller) {
    int32_t bone_count;
    int32_t state_count;
    float final_locals[VGFX3D_MAX_BONES * 16];
    if (!controller || !controller->final_palette || !controller->final_globals ||
        !controller->skeleton)
        return;
    controller_repair_state_table(controller);
    controller_repair_layer_state_indices(controller);
    state_count = controller->state_count;
    bone_count = controller_safe_bone_count(controller->skeleton);
    if (bone_count <= 0)
        return;

    for (int32_t layer_index = 0; layer_index < RT_ANIM_CONTROLLER3D_MAX_LAYERS;
         layer_index++) {
        anim_controller3d_layer_t *layer = &controller->layers[layer_index];
        if (layer_index == 0 || layer->mask_root_bone < 0 ||
            layer->mask_root_bone >= bone_count) {
            if (layer->mask_bone_count_seen != bone_count || layer_index == 0)
                controller_set_all_mask_bits(layer, bone_count);
        } else {
            controller_rebuild_layer_mask(controller, layer_index);
        }
    }

    if (controller->blend_tree) {
        void *blend = rt_blend_tree3d_get_blend(controller->blend_tree);
        int32_t blend_bone_count = 0;
        const float *blend_locals =
            rt_anim_blend3d_get_local_transform_data(blend, &blend_bone_count);
        if (blend_locals && blend_bone_count >= bone_count) {
            memcpy(final_locals, blend_locals, (size_t)bone_count * 16 * sizeof(float));
        } else {
            for (int32_t bone = 0; bone < bone_count; bone++) {
                memcpy(&final_locals[bone * 16],
                       controller->skeleton->bones[bone].bind_pose_local,
                       16 * sizeof(float));
            }
        }
    } else if (controller->layers[0].current_state >= 0 &&
               controller->layers[0].current_state < state_count && controller->layers[0].player &&
               controller->layers[0].player->local_transforms) {
        memcpy(final_locals,
               controller->layers[0].player->local_transforms,
               (size_t)bone_count * 16 * sizeof(float));
    } else {
        for (int32_t bone = 0; bone < bone_count; bone++) {
            memcpy(&final_locals[bone * 16],
                   controller->skeleton->bones[bone].bind_pose_local,
                   16 * sizeof(float));
        }
    }

    skeleton3d_compute_globals_from_locals(
        controller->skeleton, final_locals, controller->final_globals, bone_count);

    for (int32_t layer_index = 1; layer_index < RT_ANIM_CONTROLLER3D_MAX_LAYERS; layer_index++) {
        anim_controller3d_layer_t *layer = &controller->layers[layer_index];
        float weight = layer->weight;
        if (!layer->player || !layer->player->local_transforms || layer->current_state < 0 ||
            layer->current_state >= state_count || weight <= 1e-6f)
            continue;
        if (weight > 1.0f)
            weight = 1.0f;
        for (int32_t bone = 0; bone < bone_count; bone++) {
            float *dst;
            const float *src;
            if (!layer->mask_bits[bone])
                continue;
            dst = &final_locals[bone * 16];
            src = &layer->player->local_transforms[bone * 16];
            {
                float dst_pos[3], dst_rot[4], dst_scl[3];
                float src_pos[3], src_rot[4], src_scl[3];
                float blend_pos[3], blend_rot[4], blend_scl[3];
                controller_decompose_trs_float(dst, dst_pos, dst_rot, dst_scl);
                controller_decompose_trs_float(src, src_pos, src_rot, src_scl);
                if (layer->additive) {
                    const float *bind = controller->skeleton->bones[bone].bind_pose_local;
                    float bind_pos[3], bind_rot[4], bind_scl[3];
                    float inv_bind_rot[4], delta_rot[4], weighted_delta[4];
                    const float identity_rot[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                    controller_decompose_trs_float(bind, bind_pos, bind_rot, bind_scl);
                    for (int32_t i = 0; i < 3; i++) {
                        blend_pos[i] = dst_pos[i] + (src_pos[i] - bind_pos[i]) * weight;
                        blend_scl[i] = dst_scl[i] + (src_scl[i] - bind_scl[i]) * weight;
                        if (!isfinite(blend_scl[i]) || fabsf(blend_scl[i]) <= 1e-6f)
                            blend_scl[i] = 1.0f;
                    }
                    controller_quat_conjugate_float(bind_rot, inv_bind_rot);
                    controller_quat_mul_float(src_rot, inv_bind_rot, delta_rot);
                    controller_quat_slerp_float(identity_rot, delta_rot, weight, weighted_delta);
                    controller_quat_mul_float(weighted_delta, dst_rot, blend_rot);
                } else {
                    for (int32_t i = 0; i < 3; i++) {
                        blend_pos[i] = dst_pos[i] + (src_pos[i] - dst_pos[i]) * weight;
                        blend_scl[i] = dst_scl[i] + (src_scl[i] - dst_scl[i]) * weight;
                    }
                    controller_quat_slerp_float(dst_rot, src_rot, weight, blend_rot);
                }
                controller_sanitize_trs_sample(
                    blend_pos, blend_rot, blend_scl, dst_pos, dst_rot, dst_scl);
                controller_build_trs_float(blend_pos, blend_rot, blend_scl, dst);
            }
        }
        skeleton3d_compute_globals_from_locals(
            controller->skeleton, final_locals, controller->final_globals, bone_count);
    }

    if (controller->ik_solver) {
        (void)rt_ik_solver3d_apply_to_pose(
            controller->ik_solver, final_locals, controller->final_globals, bone_count);
    }

    /* Bone-count animation LOD: for distant characters, freeze bones at/after the LOD index to
     * their bind-pose local transform. They still follow their (animated) ancestors through the
     * hierarchy, but add no local animation of their own, so distal detail (fingers, toes) costs
     * nothing to pose. The shared hierarchy builder keeps frozen bones hanging off their last
     * animated ancestors even when an imported skeleton is not stored parent-before-child. */
    if (controller->animation_lod_max_bones > 0 &&
        controller->animation_lod_max_bones < bone_count) {
        for (int32_t bone = controller->animation_lod_max_bones; bone < bone_count; bone++) {
            const float *bind = controller->skeleton->bones[bone].bind_pose_local;
            memcpy(&final_locals[bone * 16], bind, 16 * sizeof(float));
        }
        skeleton3d_compute_globals_from_locals(
            controller->skeleton, final_locals, controller->final_globals, bone_count);
    }

    for (int32_t bone = 0; bone < bone_count; bone++) {
        controller_mat4f_mul(&controller->final_globals[bone * 16],
                             controller->skeleton->bones[bone].inverse_bind,
                             &controller->final_palette[bone * 16]);
    }
}

/// @brief Read the translation column from the controller's final global pose.
/// @details Used by root-motion delta accumulation. Out-pointers are zeroed on failure so
///          callers can safely subtract before/after samples.
static void controller_get_final_translation(
    const rt_anim_controller3d *controller, int32_t bone_index, double *x, double *y, double *z) {
    const float *m;
    if (!x || !y || !z) {
        return;
    }
    *x = 0.0;
    *y = 0.0;
    *z = 0.0;
    if (!controller || !controller->final_globals || !controller->skeleton)
        return;
    if (bone_index < 0 || bone_index >= controller_safe_bone_count(controller->skeleton))
        return;
    m = &controller->final_globals[bone_index * 16];
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
    if (!isfinite(len_sq) || len_sq < 1e-20) {
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
    if (!isfinite(m00) || !isfinite(m01) || !isfinite(m02) || !isfinite(m10) || !isfinite(m11) ||
        !isfinite(m12) || !isfinite(m20) || !isfinite(m21) || !isfinite(m22)) {
        controller_quat_identity(out);
        return;
    }
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

/// @brief Extract the rotation quaternion from the controller's final bone matrix.
/// @details The final global pose stores TRS-composed matrices, so the rotation
///          must be unscaled before being converted to a quaternion.
///          Computes per-axis scale from the columns' Euclidean norms,
///          divides each column out by its scale to recover an orthonormal
///          rotation matrix, then converts via Shepperd's method. Falls
///          back to identity when any axis scale is degenerate (< 1e-8).
static void controller_get_final_rotation(const rt_anim_controller3d *controller,
                                          int32_t bone_index,
                                          double *out_quat) {
    const float *m;
    double sx;
    double sy;
    double sz;
    double inv_sx;
    double inv_sy;
    double inv_sz;

    controller_quat_identity(out_quat);
    if (!controller || !controller->final_globals || !controller->skeleton)
        return;
    if (bone_index < 0 || bone_index >= controller_safe_bone_count(controller->skeleton))
        return;
    m = &controller->final_globals[bone_index * 16];
    sx = sqrt((double)m[0] * (double)m[0] + (double)m[4] * (double)m[4] +
              (double)m[8] * (double)m[8]);
    sy = sqrt((double)m[1] * (double)m[1] + (double)m[5] * (double)m[5] +
              (double)m[9] * (double)m[9]);
    sz = sqrt((double)m[2] * (double)m[2] + (double)m[6] * (double)m[6] +
              (double)m[10] * (double)m[10]);
    if (!isfinite(sx) || !isfinite(sy) || !isfinite(sz) || sx < 1e-8 || sy < 1e-8 ||
        sz < 1e-8)
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

/// @brief Return @p value when finite, else @p fallback (double scalar sanitizer).
static double controller_finite_double_or(double value, double fallback) {
    return isfinite(value) ? value : fallback;
}

/// @brief Finite-guard each lane of the controller's accumulated root-motion delta, zeroing
///   any non-finite component.
static void controller_sanitize_root_motion_delta(rt_anim_controller3d *controller) {
    if (!controller)
        return;
    for (int32_t i = 0; i < 3; i++)
        controller->root_motion_delta[i] = controller_finite_double_or(controller->root_motion_delta[i], 0.0);
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
    controller_release_class_ref((void **)&controller->skeleton, RT_G3D_SKELETON3D_CLASS_ID);
    for (int32_t i = 0, count = controller_safe_state_count(controller); i < count; i++)
        controller_release_class_ref((void **)&controller->states[i].animation,
                                     RT_G3D_ANIMATION3D_CLASS_ID);
    free(controller->states);
    free(controller->state_name_hashes);
    free(controller->state_name_indices);
    free(controller->transitions);
    free(controller->events);
    controller->states = NULL;
    controller->state_name_hashes = NULL;
    controller->state_name_indices = NULL;
    controller->transitions = NULL;
    controller->events = NULL;
    controller->state_count = 0;
    controller->state_capacity = 0;
    controller->state_name_index_capacity = 0;
    controller->transition_count = 0;
    controller->transition_capacity = 0;
    controller->event_count = 0;
    controller->event_capacity = 0;
    for (int32_t i = 0; i < RT_ANIM_CONTROLLER3D_MAX_LAYERS; i++)
        controller_release_class_ref((void **)&controller->layers[i].player,
                                     RT_G3D_ANIMPLAYER3D_CLASS_ID);
    controller_release_class_ref(&controller->blend_tree, RT_G3D_BLENDTREE3D_CLASS_ID);
    controller_release_class_ref(&controller->ik_solver, RT_G3D_IKSOLVER3D_CLASS_ID);
    free(controller->final_palette);
    controller->final_palette = NULL;
    free(controller->final_globals);
    controller->final_globals = NULL;
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
    rt_skeleton3d *skel = skeleton3d_checked(skeleton);
    int32_t bone_count;
    if (!skel) {
        rt_trap("AnimController3D.New: skeleton must be a Skeleton3D");
        return NULL;
    }
    bone_count = controller_safe_bone_count(skel);
    controller = (rt_anim_controller3d *)rt_obj_new_i64(RT_G3D_ANIMCONTROLLER3D_CLASS_ID,
                                                        (int64_t)sizeof(rt_anim_controller3d));
    if (!controller) {
        rt_trap("AnimController3D.New: allocation failed");
        return NULL;
    }
    memset(controller, 0, sizeof(*controller));
    rt_obj_set_finalizer(controller, controller_finalize);
    controller->skeleton = skel;
    rt_obj_retain_maybe(controller->skeleton);
    controller->root_motion_bone = -1;
    controller_quat_identity(controller->root_motion_rotation);

    {
        size_t palette_size = (size_t)VGFX3D_MAX_BONES * 16 * sizeof(float);
        controller->final_palette = (float *)calloc(1, palette_size);
        controller->final_globals = (float *)calloc(1, palette_size);
        controller->prev_final_palette = (float *)calloc(1, palette_size);
    }
    if (!controller->final_palette || !controller->final_globals ||
        !controller->prev_final_palette) {
        rt_trap("AnimController3D.New: palette allocation failed");
        if (rt_obj_release_check0(controller))
            rt_obj_free(controller);
        return NULL;
    }
    for (int32_t layer_index = 0; layer_index < RT_ANIM_CONTROLLER3D_MAX_LAYERS; layer_index++) {
        anim_controller3d_layer_t *layer = &controller->layers[layer_index];
        layer->player = (rt_anim_player3d *)rt_anim_player3d_new(skeleton);
        if (!layer->player) {
            rt_trap("AnimController3D.New: layer allocation failed");
            if (rt_obj_release_check0(controller))
                rt_obj_free(controller);
            return NULL;
        }
        layer->current_state = -1;
        layer->previous_state = -1;
        layer->weight = layer_index == 0 ? 1.0f : 0.0f;
        layer->mask_root_bone = -1;
        controller_set_all_mask_bits(layer, bone_count);
    }
    controller_bind_pose_palette(controller);
    return controller;
}

/// @brief Register a named animation state. `name` is the lookup key (used by `_play`,
/// `_crossfade`, etc); `animation` is an Animation3D handle. If a state with `name` already
/// exists, returns its existing index without creating a duplicate. Returns -1 on failure.
int64_t rt_anim_controller3d_add_state(void *obj, rt_string name, void *animation) {
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    rt_animation3d *anim = animation3d_checked(animation);
    rt_skeleton3d *skel;
    anim_controller3d_state_t *state;
    char canonical_name[RT_ANIM_CONTROLLER3D_STATE_NAME_MAX];
    int32_t existing;
    if (!controller || !anim)
        return -1;
    skel = skeleton3d_checked(controller->skeleton);
    if (!animation3d_channels_fit_skeleton(anim, skel))
        return -1;
    controller_repair_state_table(controller);
    controller_copy_name(canonical_name, sizeof(canonical_name), name);
    if (canonical_name[0] == '\0')
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
    memcpy(state->name, canonical_name, sizeof(state->name));
    state->animation = anim;
    rt_obj_retain_maybe(state->animation);
    state->speed = 1.0f;
    state->looping = rt_animation3d_get_looping(anim) ? 1 : 0;
    controller->state_count++;
    controller->state_name_index_dirty = 1;
    return controller->state_count - 1;
}

/// @brief Register a directional transition with a default blend time. When `_play` is later
/// called with `to_state` while in `from_state`, this blend time is used automatically. If the
/// transition already exists, its blend time is updated. Returns 1 on success.
int8_t rt_anim_controller3d_add_transition(void *obj,
                                           rt_string from_state,
                                           rt_string to_state,
                                           double blend_seconds) {
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    int32_t from_index;
    int32_t to_index;
    int32_t existing;
    anim_controller3d_transition_t *transition;
    if (!controller)
        return 0;
    controller_repair_state_table(controller);
    controller_repair_transition_table(controller);
    from_index = controller_find_state(controller, from_state);
    to_index = controller_find_state(controller, to_state);
    if (from_index < 0 || to_index < 0)
        return 0;
    existing = controller_find_transition(controller, from_index, to_index);
    if (existing >= 0) {
        controller->transitions[existing].blend_seconds =
            controller_clamp_nonnegative_float(blend_seconds);
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
    transition->blend_seconds = controller_clamp_nonnegative_float(blend_seconds);
    return 1;
}

/// @brief Play a state on the base layer. If a transition was registered between the current
/// state and `state_name`, its blend time is used; otherwise the switch is instantaneous.
/// Returns 1 if the state exists, 0 if not.
int8_t rt_anim_controller3d_play(void *obj, rt_string state_name) {
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    int32_t state_index;
    double blend_seconds = 0.0;
    if (!controller)
        return 0;
    controller_repair_state_table(controller);
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
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    int32_t state_index;
    if (!controller)
        return 0;
    controller_repair_state_table(controller);
    state_index = controller_find_state(controller, state_name);
    if (state_index < 0)
        return 0;
    return controller_set_layer_state(controller, 0, state_index, blend_seconds);
}

/// @brief Stop playback on every layer (base + overlays). Does not clear the current_state
/// indices — calling `_play` again resumes from the same animation. Resets transition timers.
void rt_anim_controller3d_stop(void *obj) {
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
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
    controller->has_prev_final_palette = 0;
    controller_compute_final_palette(controller);
}

/// @brief Per-frame tick for every layer. Advances each player's time by `delta_time`, fires
/// time-stamped events crossing the previous→current playback window, advances transition
/// blends, then composites the layered final bone palette. Also accumulates root-motion bone
/// translation deltas for `_consume_root_motion`. No-op for negative dt or missing skeleton.
void rt_anim_controller3d_update(void *obj, double delta_time) {
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
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
    double base_prev_time = 0.0;
    double base_elapsed_time = 0.0;
    double base_duration = 0.0;
    int32_t base_state = -1;
    int8_t base_looping = 0;
    int32_t state_count;
    int32_t bone_count;
    if (!controller || !controller->skeleton || !isfinite(delta_time) || delta_time < 0.0)
        return;
    controller_repair_state_table(controller);
    controller_repair_transition_table(controller);
    controller_repair_event_table(controller);
    controller_repair_layer_state_indices(controller);
    state_count = controller->state_count;
    bone_count = controller_safe_bone_count(controller->skeleton);

    if (!isfinite(controller->animation_lod_accum) || controller->animation_lod_accum < 0.0)
        controller->animation_lod_accum = 0.0;
    if (isfinite(controller->animation_lod_rate_hz) && controller->animation_lod_rate_hz > 0.0 &&
        delta_time > 0.0) {
        double interval = 1.0 / controller->animation_lod_rate_hz;
        if (!isfinite(interval) || interval <= 0.0) {
            controller->animation_lod_rate_hz = 0.0;
            controller->animation_lod_accum = 0.0;
        } else {
            controller->animation_lod_accum += delta_time;
            if (!isfinite(controller->animation_lod_accum) ||
                controller->animation_lod_accum < 0.0) {
                controller->animation_lod_accum = 0.0;
                return;
            }
            if (controller->animation_lod_accum + 1e-12 < interval)
                return;
            double steps = floor(controller->animation_lod_accum / interval);
            double consumed = steps * interval;
            if (!isfinite(consumed) || consumed <= 0.0 ||
                consumed > controller->animation_lod_accum)
                consumed = controller->animation_lod_accum;
            delta_time = consumed;
            controller->animation_lod_accum -= consumed;
            if (!isfinite(controller->animation_lod_accum) ||
                controller->animation_lod_accum < 1e-12)
                controller->animation_lod_accum = 0.0;
        }
    }

    if (controller->final_palette && controller->prev_final_palette && controller->skeleton &&
        bone_count > 0) {
        memcpy(controller->prev_final_palette,
               controller->final_palette,
               (size_t)bone_count * 16 * sizeof(float));
        controller->has_prev_final_palette = 1;
    }

    controller_get_final_translation(
        controller, controller->root_motion_bone, &before_x, &before_y, &before_z);
    controller_get_final_rotation(controller, controller->root_motion_bone, before_rot);

    for (int32_t layer_index = 0; layer_index < RT_ANIM_CONTROLLER3D_MAX_LAYERS; layer_index++) {
        anim_controller3d_layer_t *layer = &controller->layers[layer_index];
        double prev_time;
        double curr_time;
        double elapsed_time;
        int32_t state_index = layer->current_state;
        if (!layer->player || state_index < 0 || state_index >= state_count)
            continue;
        prev_time = rt_anim_player3d_get_time(layer->player);
        elapsed_time = delta_time * rt_anim_player3d_get_speed(layer->player);
        rt_anim_player3d_update(layer->player, delta_time);
        curr_time = rt_anim_player3d_get_time(layer->player);
        if (layer_index == 0) {
            base_state = state_index;
            base_prev_time = prev_time;
            base_elapsed_time = elapsed_time;
            base_duration = rt_animation3d_get_duration(controller->states[state_index].animation);
            base_looping = controller->states[state_index].looping;
        }
        controller_process_events(
            controller,
            state_index,
            prev_time,
            curr_time,
            rt_animation3d_get_duration(controller->states[state_index].animation),
            controller->states[state_index].looping,
            elapsed_time);
        if (layer->transitioning) {
            layer->transition_time += controller_clamp_to_float(delta_time, 0.0f);
            if (layer->transition_time >= layer->transition_duration) {
                layer->transitioning = 0;
                layer->transition_time = layer->transition_duration;
            }
        }
    }

    if (controller->blend_tree)
        rt_blend_tree3d_update(controller->blend_tree, delta_time);
    controller_compute_final_palette(controller);
    controller_get_final_translation(
        controller, controller->root_motion_bone, &after_x, &after_y, &after_z);
    controller_get_final_rotation(controller, controller->root_motion_bone, after_rot);
    if (controller->root_motion_bone >= 0) {
        double dx = after_x - before_x;
        double dy = after_y - before_y;
        double dz = after_z - before_z;
        int64_t cycle_count = 0;
        int forward_cycles = base_elapsed_time >= 0.0 ? 1 : 0;
        controller_quat_conjugate(before_rot, inv_before_rot);
        controller_quat_mul(after_rot, inv_before_rot, delta_rot);

        if (base_looping && base_state >= 0 && base_duration > 0.0 && isfinite(base_elapsed_time)) {
            double raw_time = base_prev_time + base_elapsed_time;
            double cycle_ratio = 0.0;
            if (base_elapsed_time >= 0.0 && raw_time >= base_duration)
                cycle_ratio = floor(raw_time / base_duration);
            else if (base_elapsed_time < 0.0 && raw_time < 0.0)
                cycle_ratio = ceil((-raw_time) / base_duration);
            if (isfinite(cycle_ratio) && cycle_ratio > 0.0) {
                if (cycle_ratio > (double)RT_ANIM_CONTROLLER3D_ROOT_MOTION_CYCLE_MAX)
                    cycle_count = RT_ANIM_CONTROLLER3D_ROOT_MOTION_CYCLE_MAX;
                else
                    cycle_count = (int64_t)cycle_ratio;
            }
        }

        if (cycle_count > 0) {
            float start_global[16];
            float end_global[16];
            if (controller_sample_state_global_matrix(
                    controller, base_state, 0.0f, controller->root_motion_bone, start_global) &&
                controller_sample_state_global_matrix(controller,
                                                      base_state,
                                                      (float)base_duration,
                                                      controller->root_motion_bone,
                                                      end_global)) {
                double cycle_dx = (double)end_global[3] - (double)start_global[3];
                double cycle_dy = (double)end_global[7] - (double)start_global[7];
                double cycle_dz = (double)end_global[11] - (double)start_global[11];
                double sign = forward_cycles ? 1.0 : -1.0;
                double add_x = sign * (double)cycle_count * cycle_dx;
                double add_y = sign * (double)cycle_count * cycle_dy;
                double add_z = sign * (double)cycle_count * cycle_dz;
                if (isfinite(add_x) && isfinite(add_y) && isfinite(add_z)) {
                    dx += add_x;
                    dy += add_y;
                    dz += add_z;
                }

                float start_pos[3], start_rot_f[4], start_scl[3];
                float end_pos[3], end_rot_f[4], end_scl[3];
                double start_rot[4], end_rot[4], inv_start_rot[4], cycle_rot[4];
                controller_decompose_trs_float(start_global, start_pos, start_rot_f, start_scl);
                controller_decompose_trs_float(end_global, end_pos, end_rot_f, end_scl);
                for (int32_t i = 0; i < 4; i++) {
                    start_rot[i] = start_rot_f[i];
                    end_rot[i] = end_rot_f[i];
                }
                controller_quat_conjugate(start_rot, inv_start_rot);
                controller_quat_mul(end_rot, inv_start_rot, cycle_rot);
                if (!forward_cycles)
                    controller_quat_conjugate(cycle_rot, cycle_rot);
                int32_t rotation_cycles = cycle_count > 1024 ? 1024 : (int32_t)cycle_count;
                for (int32_t i = 0; i < rotation_cycles; i++)
                    controller_quat_mul(cycle_rot, delta_rot, delta_rot);
            }
        }

        if (isfinite(dx) && isfinite(dy) && isfinite(dz)) {
            controller->root_motion_delta[0] += dx;
            controller->root_motion_delta[1] += dy;
            controller->root_motion_delta[2] += dz;
            controller_sanitize_root_motion_delta(controller);
        }
        controller_quat_mul(controller->root_motion_rotation, delta_rot, accumulated_rot);
        memcpy(controller->root_motion_rotation, accumulated_rot, sizeof(accumulated_rot));
    }
}

/// @brief Name of the state currently playing on the base layer (empty string if none/missing).
rt_string rt_anim_controller3d_get_current_state(void *obj) {
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    int32_t state_index;
    if (!controller)
        return rt_const_cstr("");
    state_index = controller->layers[0].current_state;
    if (state_index < 0 || state_index >= controller_safe_state_count(controller))
        return rt_const_cstr("");
    return rt_const_cstr(controller->states[state_index].name);
}

/// @brief Name of the state on the base layer immediately before the current one. Useful during
/// transitions to know what the controller is fading *from*. Empty if no prior state.
rt_string rt_anim_controller3d_get_previous_state(void *obj) {
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    int32_t state_index;
    if (!controller)
        return rt_const_cstr("");
    state_index = controller->layers[0].previous_state;
    if (state_index < 0 || state_index >= controller_safe_state_count(controller))
        return rt_const_cstr("");
    return rt_const_cstr(controller->states[state_index].name);
}

/// @brief Returns 1 if the base layer is mid-blend between two states.
int8_t rt_anim_controller3d_get_is_transitioning(void *obj) {
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    return controller ? controller->layers[0].transitioning : 0;
}

/// @brief Number of states currently registered with the controller.
int64_t rt_anim_controller3d_get_state_count(void *obj) {
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    return controller ? controller_safe_state_count(controller) : 0;
}

/// @brief Current base-layer playback time in seconds.
double rt_anim_controller3d_get_state_time(void *obj) {
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    anim_controller3d_layer_t *layer;
    if (!controller)
        return 0.0;
    layer = &controller->layers[0];
    if (!layer->player || layer->current_state < 0 ||
        layer->current_state >= controller_safe_state_count(controller))
        return 0.0;
    return rt_anim_player3d_get_time(layer->player);
}

/// @brief True if the named state is the active, playing base-layer state.
int8_t rt_anim_controller3d_is_state_playing(void *obj, rt_string state_name) {
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    anim_controller3d_layer_t *layer;
    int32_t state_index;
    if (!controller)
        return 0;
    state_index = controller_find_state(controller, state_name);
    if (state_index < 0)
        return 0;
    layer = &controller->layers[0];
    return layer->current_state == state_index && layer->player &&
                   rt_anim_player3d_is_playing(layer->player)
               ? 1
               : 0;
}

/// @brief Override the playback speed multiplier for a state. Applied immediately to any
/// layer currently playing that state. Negative speeds are accepted (reverse playback).
void rt_anim_controller3d_set_state_speed(void *obj, rt_string state_name, double speed) {
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    int32_t state_index;
    if (!controller)
        return;
    state_index = controller_find_state(controller, state_name);
    if (state_index < 0)
        return;
    if (!isfinite(speed))
        speed = 1.0;
    controller->states[state_index].speed = controller_clamp_to_float(speed, 1.0f);
    for (int32_t layer_index = 0; layer_index < RT_ANIM_CONTROLLER3D_MAX_LAYERS; layer_index++) {
        if (controller->layers[layer_index].current_state == state_index)
            rt_anim_player3d_set_speed(controller->layers[layer_index].player,
                                       controller->states[state_index].speed);
    }
}

/// @brief Override whether a state loops (1) or plays once (0). Applied immediately to any
/// layer currently playing that state via the player's loop_override hook.
void rt_anim_controller3d_set_state_looping(void *obj, rt_string state_name, int8_t loop) {
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
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

/// @brief Configure deterministic animation update-rate LOD.
/// @details Positive `distance` and `rate_hz` enable throttling; elapsed time is accumulated
/// and applied in batches at `rate_hz`. Non-positive or non-finite inputs disable the throttle.
void rt_anim_controller3d_set_animation_lod(void *obj, double distance, double rate_hz) {
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    if (!controller)
        return;
    if (!isfinite(distance) || distance <= 0.0 || !isfinite(rate_hz) || rate_hz <= 0.0) {
        controller->animation_lod_distance = 0.0;
        controller->animation_lod_rate_hz = 0.0;
        controller->animation_lod_accum = 0.0;
        return;
    }
    if (distance > RT_ANIM_CONTROLLER3D_LOD_DISTANCE_MAX)
        distance = RT_ANIM_CONTROLLER3D_LOD_DISTANCE_MAX;
    if (rate_hz > RT_ANIM_CONTROLLER3D_LOD_RATE_MAX)
        rate_hz = RT_ANIM_CONTROLLER3D_LOD_RATE_MAX;
    controller->animation_lod_distance = distance;
    controller->animation_lod_rate_hz = rate_hz;
    controller->animation_lod_accum = 0.0;
}

/// @brief Configure bone-count animation LOD: bones at/after @p max_bones are frozen to their
/// bind-pose local transform (they still follow animated ancestors but add no local animation).
/// @details A non-positive @p max_bones disables it. Callers enable this for distant characters so
/// distal detail (fingers, toes — high bone indices in the parents-first ordering) costs nothing.
void rt_anim_controller3d_set_bone_lod(void *obj, int64_t max_bones) {
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    if (!controller)
        return;
    if (max_bones <= 0)
        controller->animation_lod_max_bones = 0;
    else if (max_bones > INT32_MAX)
        controller->animation_lod_max_bones = INT32_MAX;
    else
        controller->animation_lod_max_bones = (int32_t)max_bones;
}

/// @brief Use a BlendTree3D as the base pose source. Passing NULL clears it.
int8_t rt_anim_controller3d_set_blend_tree(void *obj, void *blend_tree) {
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    if (!controller)
        return 0;
    if (!blend_tree) {
        controller_release_class_ref(&controller->blend_tree, RT_G3D_BLENDTREE3D_CLASS_ID);
        controller_compute_final_palette(controller);
        return 1;
    }
    if (!rt_g3d_has_class(blend_tree, RT_G3D_BLENDTREE3D_CLASS_ID))
        return 0;
    {
        void *blend = rt_blend_tree3d_get_blend(blend_tree);
        void *blend_skeleton = rt_anim_blend3d_get_skeleton(blend);
        int32_t blend_bone_count = 0;
        const float *locals = rt_anim_blend3d_get_local_transform_data(blend, &blend_bone_count);
        if (!locals || !controller->skeleton || blend_skeleton != controller->skeleton ||
            blend_bone_count != controller_safe_bone_count(controller->skeleton))
            return 0;
    }
    if (controller->blend_tree != blend_tree) {
        rt_obj_retain_maybe(blend_tree);
        controller_release_class_ref(&controller->blend_tree, RT_G3D_BLENDTREE3D_CLASS_ID);
        controller->blend_tree = blend_tree;
    }
    rt_blend_tree3d_update(controller->blend_tree, 0.0);
    controller_compute_final_palette(controller);
    return 1;
}

/// @brief Attach an IKSolver3D to the final controller pose; NULL clears it.
int8_t rt_anim_controller3d_set_ik_solver(void *obj, void *ik_solver) {
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    void *solver_skeleton;
    if (!controller)
        return 0;
    if (!ik_solver) {
        controller_release_class_ref(&controller->ik_solver, RT_G3D_IKSOLVER3D_CLASS_ID);
        controller_compute_final_palette(controller);
        return 1;
    }
    if (!rt_g3d_has_class(ik_solver, RT_G3D_IKSOLVER3D_CLASS_ID))
        return 0;
    solver_skeleton = rt_ik_solver3d_get_skeleton(ik_solver);
    if (!solver_skeleton || solver_skeleton != controller->skeleton)
        return 0;
    if (controller->ik_solver != ik_solver) {
        rt_obj_retain_maybe(ik_solver);
        controller_release_class_ref(&controller->ik_solver, RT_G3D_IKSOLVER3D_CLASS_ID);
        controller->ik_solver = ik_solver;
    }
    controller_compute_final_palette(controller);
    return 1;
}

/// @brief Register a time-stamped event on a state. When `_update` advances playback past
/// `time_seconds`, `event_name` is enqueued for `_poll_event`. Useful for footstep sounds,
/// hit-frame triggers, attack windows. Multiple events per state are allowed.
void rt_anim_controller3d_add_event(void *obj,
                                    rt_string state_name,
                                    double time_seconds,
                                    rt_string event_name) {
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    anim_controller3d_event_t *event;
    int32_t state_index;
    const char *event_cstr;
    if (!controller)
        return;
    controller_repair_state_table(controller);
    controller_repair_event_table(controller);
    event_cstr = controller_string_cstr_or_null(event_name);
    if (!event_cstr || event_cstr[0] == '\0')
        return;
    state_index = controller_find_state(controller, state_name);
    if (state_index < 0)
        return;
    if (!isfinite(time_seconds))
        return;
    if (time_seconds < 0.0)
        time_seconds = 0.0;
    {
        double duration = rt_animation3d_get_duration(controller->states[state_index].animation);
        if (duration > 0.0 && time_seconds > duration)
            time_seconds = duration;
    }
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
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    const char *name;
    if (!controller)
        return rt_const_cstr("");
    controller_sanitize_event_queue(controller);
    if (controller->event_queue_count == 0)
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
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    if (!controller || !controller->skeleton)
        return;
    if (bone_index == -1) {
        controller->root_motion_bone = -1;
        controller->root_motion_delta[0] = 0.0;
        controller->root_motion_delta[1] = 0.0;
        controller->root_motion_delta[2] = 0.0;
        controller_quat_identity(controller->root_motion_rotation);
        return;
    }
    if (bone_index < 0 || bone_index >= controller_safe_bone_count(controller->skeleton))
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
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    if (!controller)
        return rt_vec3_new(0.0, 0.0, 0.0);
    controller_sanitize_root_motion_delta(controller);
    return rt_vec3_new(controller->root_motion_delta[0],
                       controller->root_motion_delta[1],
                       controller->root_motion_delta[2]);
}

/// @brief Atomically read and reset the accumulated root-motion delta. Returns the Vec3 the
/// character should be moved by this frame to follow the animation's hip translation.
void *rt_anim_controller3d_consume_root_motion(void *obj) {
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    void *delta;
    if (!controller)
        return rt_vec3_new(0.0, 0.0, 0.0);
    controller_sanitize_root_motion_delta(controller);
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
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    void *rotation;
    if (!controller)
        return rt_quat_new(0.0, 0.0, 0.0, 1.0);
    controller_quat_normalize(controller->root_motion_rotation);
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
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    if (!controller || layer_index < 0 || layer_index >= RT_ANIM_CONTROLLER3D_MAX_LAYERS)
        return;
    if (layer_index == 0) {
        controller->layers[0].weight = 1.0f;
        return;
    }
    if (!isfinite(weight))
        weight = 0.0;
    if (weight < 0.0)
        weight = 0.0;
    if (weight > 1.0)
        weight = 1.0;
    controller->layers[layer_index].weight = (float)weight;
    controller_compute_final_palette(controller);
}

/// @brief Set which bones an overlay layer affects. The layer composites only on `root_bone`
/// and its descendants in the skeleton hierarchy (typical use: arms-only upper-body overlay).
/// Layer 0 always covers the full skeleton.
void rt_anim_controller3d_set_layer_mask(void *obj, int64_t layer_index, int64_t root_bone) {
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    int32_t bone_count;
    if (!controller || layer_index < 0 || layer_index >= RT_ANIM_CONTROLLER3D_MAX_LAYERS)
        return;
    bone_count = controller_safe_bone_count(controller->skeleton);
    if (layer_index == 0) {
        anim_controller3d_layer_t *layer = &controller->layers[0];
        if (layer->mask_root_bone != -1 ||
            layer->mask_bone_count_seen != bone_count)
            controller_set_all_mask_bits(layer, bone_count);
        layer->mask_root_bone = -1;
        return;
    }
    if (root_bone < -1 || root_bone > INT32_MAX)
        root_bone = -1;
    if (controller->layers[layer_index].mask_root_bone == (int32_t)root_bone &&
        controller->layers[layer_index].mask_bone_count_seen == bone_count)
        return;
    controller->layers[layer_index].mask_root_bone = (int32_t)root_bone;
    controller_rebuild_layer_mask(controller, (int32_t)layer_index);
}

/// @brief Play a state on a specific overlay layer (layer_index ≥ 1). No transition blend.
/// Returns 1 on success, 0 if `layer_index` is invalid or `state_name` doesn't exist.
int8_t rt_anim_controller3d_play_layer(void *obj, int64_t layer_index, rt_string state_name) {
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    int32_t state_index;
    if (!controller || layer_index < 0 || layer_index >= RT_ANIM_CONTROLLER3D_MAX_LAYERS)
        return 0;
    state_index = controller_find_state(controller, state_name);
    if (state_index < 0)
        return 0;
    controller->layers[layer_index].additive = 0;
    return controller_set_layer_state(controller, (int32_t)layer_index, state_index, 0.0);
}

/// @brief Play a state on a layer as a true additive delta from the bind pose.
/// @details Unlike `PlayLayer`, which replaces toward the overlay pose, this composes
/// `(overlay - bind_pose) * layer_weight` onto the current base pose for masked bones.
int8_t rt_anim_controller3d_play_layer_additive(void *obj,
                                                int64_t layer_index,
                                                rt_string state_name) {
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    int32_t state_index;
    if (!controller || layer_index <= 0 || layer_index >= RT_ANIM_CONTROLLER3D_MAX_LAYERS)
        return 0;
    state_index = controller_find_state(controller, state_name);
    if (state_index < 0)
        return 0;
    controller->layers[layer_index].additive = 1;
    return controller_set_layer_state(controller, (int32_t)layer_index, state_index, 0.0);
}

/// @brief Cross-fade an overlay layer to `state_name` over `blend_seconds`. Mirrors `_crossfade`
/// but targets a specific layer, leaving the base unaffected.
int8_t rt_anim_controller3d_crossfade_layer(void *obj,
                                            int64_t layer_index,
                                            rt_string state_name,
                                            double blend_seconds) {
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    int32_t state_index;
    if (!controller || layer_index < 0 || layer_index >= RT_ANIM_CONTROLLER3D_MAX_LAYERS)
        return 0;
    state_index = controller_find_state(controller, state_name);
    if (state_index < 0)
        return 0;
    controller->layers[layer_index].additive = 0;
    return controller_set_layer_state(controller, (int32_t)layer_index, state_index, blend_seconds);
}

/// @brief Cross-fade an overlay layer as a true additive bind-pose delta.
int8_t rt_anim_controller3d_crossfade_layer_additive(void *obj,
                                                     int64_t layer_index,
                                                     rt_string state_name,
                                                     double blend_seconds) {
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    int32_t state_index;
    if (!controller || layer_index <= 0 || layer_index >= RT_ANIM_CONTROLLER3D_MAX_LAYERS)
        return 0;
    state_index = controller_find_state(controller, state_name);
    if (state_index < 0)
        return 0;
    controller->layers[layer_index].additive = 1;
    return controller_set_layer_state(controller, (int32_t)layer_index, state_index, blend_seconds);
}

/// @brief Stop playback on a specific overlay layer. Layer 0 (base) keeps its current_state but
/// pauses; overlay layers (≥ 1) are also cleared so they no longer composite onto the base.
void rt_anim_controller3d_stop_layer(void *obj, int64_t layer_index) {
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    anim_controller3d_layer_t *layer;
    if (!controller || layer_index < 0 || layer_index >= RT_ANIM_CONTROLLER3D_MAX_LAYERS)
        return;
    layer = &controller->layers[layer_index];
    if (layer->player)
        rt_anim_player3d_stop(layer->player);
    if (layer_index != 0)
        layer->current_state = -1;
    layer->additive = 0;
    layer->transitioning = 0;
    layer->transition_time = 0.0f;
    layer->transition_duration = 0.0f;
    controller_compute_final_palette(controller);
}

/// @brief Snapshot the current final-palette matrix for a single bone as a Mat4.
/// @details Returns a freshly allocated Mat4 — caller owns the resulting
///          object. Returns NULL on missing controller / palette / out-of-
///          range bone. The matrix reflects the composited result of all
///          layers as of the most recent `_update`.
void *rt_anim_controller3d_get_bone_matrix(void *obj, int64_t bone_index) {
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    const float *m;
    if (!controller || !controller->skeleton || !controller->final_globals)
        return NULL;
    if (bone_index < 0 || bone_index >= controller_safe_bone_count(controller->skeleton))
        return NULL;
    m = &controller->final_globals[bone_index * 16];
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
void *rt_anim_controller3d_get_skeleton(void *obj) {
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    return controller ? controller->skeleton : NULL;
}

/// @brief Direct pointer to the final-palette float buffer (no copy).
/// @details Used by the renderer's GPU-upload path which needs to upload
///          the entire palette in one call. The caller does not own the
///          buffer — it remains valid until the next `_update` overwrites
///          it or the controller is finalised. `bone_count` is filled with
///          the palette length in bones (not floats).
/// @return Pointer to `bone_count * 16` floats, or NULL on missing data.
const float *rt_anim_controller3d_get_final_palette_data(void *obj, int32_t *bone_count) {
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    if (bone_count)
        *bone_count = 0;
    if (!controller || !controller->skeleton || !controller->final_palette)
        return NULL;
    if (bone_count)
        *bone_count = controller_safe_bone_count(controller->skeleton);
    return controller->final_palette;
}

/// @brief Direct pointer to the *previous* frame's final-palette float buffer.
/// @details Used by motion-vector / temporal-AA renderers that need both
///          this frame's and last frame's bone palettes to compute per-
///          pixel velocity. Returns NULL until at least one `_update` has
///          completed (the prev-palette is populated by snapshotting
///          before the per-layer player update).
const float *rt_anim_controller3d_get_previous_palette_data(void *obj, int32_t *bone_count) {
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    if (bone_count)
        *bone_count = 0;
    if (!controller || !controller->skeleton || !controller->prev_final_palette ||
        !controller->has_prev_final_palette)
        return NULL;
    if (bone_count)
        *bone_count = controller_safe_bone_count(controller->skeleton);
    return controller->prev_final_palette;
}

#endif
