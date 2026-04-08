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

#include "rt_skeleton3d.h"
#include "rt_skeleton3d_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_obj_retain_maybe(void *obj);
extern int rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);
extern rt_string rt_const_cstr(const char *s);
extern const char *rt_string_cstr(rt_string s);
extern void *rt_vec3_new(double x, double y, double z);
extern void *rt_mat4_new(double m0,
                         double m1,
                         double m2,
                         double m3,
                         double m4,
                         double m5,
                         double m6,
                         double m7,
                         double m8,
                         double m9,
                         double m10,
                         double m11,
                         double m12,
                         double m13,
                         double m14,
                         double m15);

#include "rt_trap.h"

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
    int32_t root_motion_bone;

    char event_queue[RT_ANIM_CONTROLLER3D_EVENT_QUEUE_MAX][RT_ANIM_CONTROLLER3D_EVENT_NAME_MAX];
    int32_t event_queue_head;
    int32_t event_queue_count;
} rt_anim_controller3d;

static void controller_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

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

static void controller_set_all_mask_bits(anim_controller3d_layer_t *layer, int32_t bone_count) {
    if (!layer)
        return;
    memset(layer->mask_bits, 0, sizeof(layer->mask_bits));
    for (int32_t bone = 0; bone < bone_count && bone < VGFX3D_MAX_BONES; bone++)
        layer->mask_bits[bone] = 1;
}

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

static void controller_apply_state_settings(
    rt_anim_player3d *player, const anim_controller3d_state_t *state) {
    if (!player || !state)
        return;
    player->loop_override_enabled = 1;
    player->loop_override_value = state->looping ? 1 : 0;
    rt_anim_player3d_set_speed(player, state->speed);
}

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

void rt_anim_controller3d_update(void *obj, double delta_time) {
    rt_anim_controller3d *controller = (rt_anim_controller3d *)obj;
    double before_x;
    double before_y;
    double before_z;
    double after_x;
    double after_y;
    double after_z;
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
    controller->root_motion_delta[0] += after_x - before_x;
    controller->root_motion_delta[1] += after_y - before_y;
    controller->root_motion_delta[2] += after_z - before_z;
}

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

int8_t rt_anim_controller3d_get_is_transitioning(void *obj) {
    rt_anim_controller3d *controller = (rt_anim_controller3d *)obj;
    return controller ? controller->layers[0].transitioning : 0;
}

int64_t rt_anim_controller3d_get_state_count(void *obj) {
    rt_anim_controller3d *controller = (rt_anim_controller3d *)obj;
    return controller ? controller->state_count : 0;
}

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
}

void *rt_anim_controller3d_get_root_motion_delta(void *obj) {
    rt_anim_controller3d *controller = (rt_anim_controller3d *)obj;
    if (!controller)
        return rt_vec3_new(0.0, 0.0, 0.0);
    return rt_vec3_new(controller->root_motion_delta[0],
                       controller->root_motion_delta[1],
                       controller->root_motion_delta[2]);
}

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
