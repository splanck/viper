//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/anim/rt_animcontroller3d_api.inc  (#included from rt_animcontroller3d.c; not a standalone translation unit)
// Purpose: AnimController3D public API: states, transitions, play/crossfade, update, IK, events
//===----------------------------------------------------------------------===//
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
    free(controller->blend_locals_scratch);
    controller->blend_locals_scratch = NULL;
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
        controller->blend_locals_scratch = (float *)calloc(1, palette_size);
    }
    if (!controller->final_palette || !controller->final_globals ||
        !controller->prev_final_palette || !controller->blend_locals_scratch) {
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

/// @brief Apply the controller animation-LOD rate gate: accumulate @p delta_time and, when the
///        rate is set, either skip this frame (return 0) or consume whole steps into delta_time.
static int controller_apply_animation_lod(rt_anim_controller3d *controller, double *delta_time) {
    if (!isfinite(controller->animation_lod_accum) || controller->animation_lod_accum < 0.0)
        controller->animation_lod_accum = 0.0;
    if (isfinite(controller->animation_lod_rate_hz) && controller->animation_lod_rate_hz > 0.0 &&
        *delta_time > 0.0) {
        double interval = 1.0 / controller->animation_lod_rate_hz;
        if (!isfinite(interval) || interval <= 0.0) {
            controller->animation_lod_rate_hz = 0.0;
            controller->animation_lod_accum = 0.0;
        } else {
            controller->animation_lod_accum += *delta_time;
            if (!isfinite(controller->animation_lod_accum) ||
                controller->animation_lod_accum < 0.0) {
                controller->animation_lod_accum = 0.0;
                return 0;
            }
            if (controller->animation_lod_accum + 1e-12 < interval)
                return 0;
            double steps = floor(controller->animation_lod_accum / interval);
            double consumed = steps * interval;
            if (!isfinite(consumed) || consumed <= 0.0 ||
                consumed > controller->animation_lod_accum)
                consumed = controller->animation_lod_accum;
            *delta_time = consumed;
            controller->animation_lod_accum -= consumed;
            if (!isfinite(controller->animation_lod_accum) ||
                controller->animation_lod_accum < 1e-12)
                controller->animation_lod_accum = 0.0;
        }
    }
    return 1;
}

/// @brief Advance every animation layer player by @p delta_time, fire clip events, and step
///        active transitions; reports the base-layer (layer 0) timing for root motion below.
static void controller_advance_layers(rt_anim_controller3d *controller, double delta_time,
                                      int32_t state_count, int32_t *out_base_state,
                                      double *out_base_prev_time, double *out_base_elapsed_time,
                                      double *out_base_duration, int8_t *out_base_looping) {
    int32_t base_state = -1;
    double base_prev_time = 0.0;
    double base_elapsed_time = 0.0;
    double base_duration = 0.0;
    int8_t base_looping = 0;
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
    *out_base_state = base_state;
    *out_base_prev_time = base_prev_time;
    *out_base_elapsed_time = base_elapsed_time;
    *out_base_duration = base_duration;
    *out_base_looping = base_looping;
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

    if (!controller_apply_animation_lod(controller, &delta_time))
        return;

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

    controller_advance_layers(controller, delta_time, state_count, &base_state, &base_prev_time,
                              &base_elapsed_time, &base_duration, &base_looping);

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
    return controller && controller->layers[0].transitioning ? 1 : 0;
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
    rt_skeleton3d *skeleton = controller_skeleton_ref(controller);
    const float *m;
    if (!controller || !skeleton || !controller->final_globals)
        return NULL;
    if (bone_index < 0 || bone_index >= controller_safe_bone_count(skeleton))
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

/// @brief Extract a bone's model-space position + rotation from the final pose.
/// @details Position comes from the composited global matrix's translation
///          column; rotation is the scale-normalized quaternion via
///          `controller_get_final_rotation`. Model-space means relative to the
///          skinned model's root — callers compose with the owning node's world
///          transform for world-space sockets. Returns 1 on success, 0 when the
///          controller/skeleton/pose is missing or the bone index is invalid.
int rt_anim_controller3d_get_bone_pose(void *obj,
                                       int64_t bone_index,
                                       double *out_pos,
                                       double *out_quat) {
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    rt_skeleton3d *skeleton = controller_skeleton_ref(controller);
    const float *m;

    if (out_pos)
        out_pos[0] = out_pos[1] = out_pos[2] = 0.0;
    if (out_quat) {
        out_quat[0] = out_quat[1] = out_quat[2] = 0.0;
        out_quat[3] = 1.0;
    }
    if (!controller || !skeleton || !controller->final_globals || !out_pos || !out_quat)
        return 0;
    if (bone_index < 0 || bone_index >= controller_safe_bone_count(skeleton))
        return 0;
    m = &controller->final_globals[bone_index * 16];
    out_pos[0] = (double)m[3];
    out_pos[1] = (double)m[7];
    out_pos[2] = (double)m[11];
    controller_get_final_rotation(controller, (int32_t)bone_index, out_quat);
    return 1;
}

/// @brief Borrow the Skeleton3D handle backing this controller.
void *rt_anim_controller3d_get_skeleton(void *obj) {
    rt_anim_controller3d *controller = anim_controller3d_checked(obj);
    return controller_skeleton_ref(controller);
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
    rt_skeleton3d *skeleton = controller_skeleton_ref(controller);
    if (bone_count)
        *bone_count = 0;
    if (!controller || !skeleton || !controller->final_palette)
        return NULL;
    if (bone_count)
        *bone_count = controller_safe_bone_count(skeleton);
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
    rt_skeleton3d *skeleton = controller_skeleton_ref(controller);
    if (bone_count)
        *bone_count = 0;
    if (!controller || !skeleton || !controller->prev_final_palette ||
        !controller->has_prev_final_palette)
        return NULL;
    if (bone_count)
        *bone_count = controller_safe_bone_count(skeleton);
    return controller->prev_final_palette;
}
