//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/anim/rt_skeleton3d_blend.inc  (#included from rt_skeleton3d.c; not a standalone translation unit)
// Purpose: AnimBlend3D multi-state animation blending
//===----------------------------------------------------------------------===//
/*==========================================================================
 * AnimBlend3D — multi-state animation blending
 *=========================================================================*/

/// @brief GC finalizer for AnimBlend3D — releases per-state animation refs and the bone palette.
static void anim_blend3d_finalizer(void *obj) {
    rt_anim_blend3d *b = (rt_anim_blend3d *)obj;
    for (int32_t i = 0, count = anim_blend3d_state_slot_limit(b); i < count; i++) {
        animation3d_release_class_ref((void **)&b->states[i].animation,
                                      RT_G3D_ANIMATION3D_CLASS_ID);
    }
    animation3d_release_class_ref((void **)&b->skeleton, RT_G3D_SKELETON3D_CLASS_ID);
    free(b->bone_palette);
    free(b->prev_bone_palette);
    free(b->motion_palette_snapshot);
    free(b->local_transforms);
    free(b->temp_state_local);
    free(b->globals_buf);
    b->bone_palette = b->prev_bone_palette = b->motion_palette_snapshot = NULL;
    b->local_transforms = b->temp_state_local = b->globals_buf = NULL;
    b->state_count = 0;
}

static int anim_blend3d_ensure_pose_capacity(rt_anim_blend3d *b, int32_t bone_count) {
    if (!b)
        return 0;
    int32_t new_capacity = skeleton3d_pose_capacity_for_count(bone_count);
    int32_t old_capacity = b->pose_capacity > 0 ? b->pose_capacity : 0;
    if (new_capacity <= old_capacity)
        return 1;
    if (!skeleton3d_grow_pose_buffer(&b->bone_palette, old_capacity, new_capacity) ||
        !skeleton3d_grow_pose_buffer(&b->prev_bone_palette, old_capacity, new_capacity) ||
        !skeleton3d_grow_pose_buffer(&b->motion_palette_snapshot, old_capacity, new_capacity) ||
        !skeleton3d_grow_pose_buffer(&b->local_transforms, old_capacity, new_capacity) ||
        !skeleton3d_grow_pose_buffer(&b->temp_state_local, old_capacity, new_capacity) ||
        !skeleton3d_grow_pose_buffer(&b->globals_buf, old_capacity, new_capacity))
        return 0;
    b->pose_capacity = new_capacity;
    return 1;
}

/// @brief Create a multi-clip blender that mixes several animations on the same skeleton.
///
/// Useful for blendspaces (e.g. mixing walk + run by speed) and
/// additive layering. Each registered "state" carries its own
/// time, speed, and weight.
void *rt_anim_blend3d_new(void *skel_obj) {
    rt_skeleton3d *skel =
        (rt_skeleton3d *)rt_g3d_checked_or_null(skel_obj, RT_G3D_SKELETON3D_CLASS_ID);
    if (!skel)
        return NULL;
    rt_anim_blend3d *b = (rt_anim_blend3d *)rt_obj_new_i64(RT_G3D_ANIMBLEND3D_CLASS_ID,
                                                           (int64_t)sizeof(rt_anim_blend3d));
    if (!b) {
        rt_trap("AnimBlend3D.New: allocation failed");
        return NULL;
    }
    b->vptr = NULL;
    b->skeleton = skel;
    rt_obj_retain_maybe(skel);
    b->state_count = 0;
    memset(b->states, 0, sizeof(b->states));
    b->last_motion_frame = 0;
    b->has_prev_motion_palette = 0;

    int32_t bone_count = skeleton3d_safe_bone_count(skel);
    b->pose_capacity = skeleton3d_pose_capacity_for_count(bone_count);
    size_t buf_sz = (size_t)b->pose_capacity * 16u * sizeof(float);
    b->bone_palette = (float *)calloc(1, buf_sz);
    b->prev_bone_palette = (float *)calloc(1, buf_sz);
    b->motion_palette_snapshot = (float *)calloc(1, buf_sz);
    b->local_transforms = (float *)calloc(1, buf_sz);
    b->temp_state_local = (float *)calloc(1, buf_sz);
    b->globals_buf = (float *)calloc(1, buf_sz);
    if (!b->bone_palette || !b->prev_bone_palette || !b->motion_palette_snapshot ||
        !b->local_transforms || !b->temp_state_local || !b->globals_buf) {
        anim_blend3d_finalizer(b);
        if (rt_obj_release_check0(b))
            rt_obj_free(b);
        rt_trap("AnimBlend3D.New: allocation failed");
        return NULL;
    }

    skeleton3d_freeze(skel);
    if (buf_sz > 0) {
        for (int32_t i = 0; i < bone_count; i++)
            memcpy(
                &b->local_transforms[i * 16], skel->bones[i].bind_pose_local, 16 * sizeof(float));
        compute_palette_from_locals(skel, b->local_transforms, b->globals_buf, b->bone_palette);
        memcpy(b->prev_bone_palette, b->bone_palette, buf_sz);
        memcpy(b->motion_palette_snapshot, b->bone_palette, buf_sz);
    }

    rt_obj_set_finalizer(b, anim_blend3d_finalizer);
    return b;
}

/// @brief Register a new blend state by name and animation; returns its index.
int64_t rt_anim_blend3d_add_state(void *obj, rt_string name, void *anim_obj) {
    rt_anim_blend3d *b =
        (rt_anim_blend3d *)rt_g3d_checked_or_null(obj, RT_G3D_ANIMBLEND3D_CLASS_ID);
    rt_animation3d *anim =
        (rt_animation3d *)rt_g3d_checked_or_null(anim_obj, RT_G3D_ANIMATION3D_CLASS_ID);
    rt_skeleton3d *skel;
    if (!b || !anim)
        return -1;
    skel = animation3d_skeleton_slot((void **)&b->skeleton);
    if (!animation3d_channels_fit_skeleton(anim, skel))
        return -1;
    b->state_count = animblend3d_safe_state_count(b);
    if (b->state_count >= RT_ANIM_BLEND3D_MAX_STATES)
        return -1;

    anim_blend_state_t *st = &b->states[b->state_count];
    memset(st, 0, sizeof(anim_blend_state_t));
    if (name) {
        const char *cstr = rt_string_cstr(name);
        if (cstr) {
            size_t len = strlen(cstr);
            if (len > 63)
                len = 63;
            memcpy(st->name, cstr, len);
            st->name[len] = '\0';
        }
    }
    st->animation = anim;
    rt_obj_retain_maybe(anim);
    st->weight = 0.0f;
    st->anim_time = 0.0f;
    st->speed = 1.0f;
    st->looping = anim->looping ? 1 : 0;
    return b->state_count++;
}

/// @brief Set state `state`'s blend weight; the blender renormalises across all states each frame.
void rt_anim_blend3d_set_weight(void *obj, int64_t state, double weight) {
    rt_anim_blend3d *b = anim_blend3d_checked(obj);
    if (!b)
        return;
    if (state < 0 || state >= animblend3d_safe_state_count(b))
        return;
    if (!isfinite(weight))
        weight = 0.0;
    if (weight < 0.0)
        weight = 0.0;
    if (weight > 1.0)
        weight = 1.0;
    b->states[state].weight = (float)weight;
}

/// @brief Convenience: look the state up by name and call `set_weight`.
void rt_anim_blend3d_set_weight_by_name(void *obj, rt_string name, double weight) {
    rt_anim_blend3d *b = anim_blend3d_checked(obj);
    char target_buf[64];
    if (!b || !name)
        return;
    const char *target = rt_string_cstr(name);
    if (!target)
        return;
    size_t len = strlen(target);
    if (len >= sizeof(target_buf))
        len = sizeof(target_buf) - 1u;
    memcpy(target_buf, target, len);
    target_buf[len] = '\0';
    if (target_buf[0] == '\0')
        return;
    for (int32_t i = 0, count = animblend3d_safe_state_count(b); i < count; i++) {
        if (strcmp(b->states[i].name, target_buf) == 0) {
            if (!isfinite(weight))
                weight = 0.0;
            if (weight < 0.0)
                weight = 0.0;
            if (weight > 1.0)
                weight = 1.0;
            b->states[i].weight = (float)weight;
            return;
        }
    }
}

/// @brief Read a state's current blend weight (0.0 for out-of-range or NULL).
double rt_anim_blend3d_get_weight(void *obj, int64_t state) {
    rt_anim_blend3d *b = anim_blend3d_checked(obj);
    if (!b)
        return 0.0;
    if (state < 0 || state >= animblend3d_safe_state_count(b))
        return 0.0;
    if (!isfinite(b->states[state].weight) || b->states[state].weight <= 0.0f)
        return 0.0;
    if (b->states[state].weight >= 1.0f)
        return 1.0;
    return (double)b->states[state].weight;
}

/// @brief Set the per-state time-scale (independent of the others — each clip can run at its own
/// rate).
void rt_anim_blend3d_set_speed(void *obj, int64_t state, double speed) {
    rt_anim_blend3d *b = anim_blend3d_checked(obj);
    if (!b)
        return;
    if (state < 0 || state >= animblend3d_safe_state_count(b))
        return;
    if (!isfinite(speed))
        speed = 1.0;
    b->states[state].speed = skeleton3d_clamp_to_float(speed, 1.0f);
}

/// @brief Advance every contributing state by `dt` seconds and recompute the blended bone palette.
///
/// For each bone, samples per-state TRS at that state's current
/// time, weights them by the normalised state weights, and lerps
/// (positions / scales) and slerps (rotations) to produce the
/// final pose. Output palette is then ready for skinning.
void rt_anim_blend3d_update(void *obj, double dt) {
    rt_anim_blend3d *b = anim_blend3d_checked(obj);
    if (!b || dt < 0 || !isfinite(dt))
        return;
    rt_skeleton3d *skel = animation3d_skeleton_slot((void **)&b->skeleton);
    int32_t bc = skeleton3d_safe_bone_count(skel);
    int32_t state_count = anim_blend3d_state_slot_limit(b);
    if (!skel || bc == 0)
        return;
    if (!anim_blend3d_ensure_pose_capacity(b, bc))
        return;
    if (!b->local_transforms || !b->temp_state_local || !b->globals_buf || !b->bone_palette)
        return;

    /* Advance all state timers */
    for (int32_t s = 0; s < state_count; s++) {
        anim_blend_state_t *st = &b->states[s];
        rt_animation3d *anim = animation3d_clip_slot((void **)&st->animation);
        if (!anim)
            continue;
        if (!isfinite(st->speed))
            st->speed = 1.0f;
        st->anim_time += skeleton3d_clamp_to_float(dt * (double)st->speed, 0.0f);
        if (!isfinite(st->anim_time))
            st->anim_time = 0.0f;
        if (st->looping && anim->duration > 0)
            st->anim_time = animation3d_wrap_time(st->anim_time, anim->duration);
        else {
            if (st->anim_time > anim->duration)
                st->anim_time = anim->duration;
            else if (st->anim_time < 0.0f)
                st->anim_time = 0.0f;
        }
    }

    /* Start with bind pose */
    for (int32_t i = 0; i < bc; i++)
        memcpy(&b->local_transforms[i * 16], skel->bones[i].bind_pose_local, 16 * sizeof(float));

    /* Blend active states */
    float total_weight = 0.0f;
    for (int32_t s = 0; s < state_count; s++) {
        anim_blend_state_t *st = &b->states[s];
        rt_animation3d *anim = animation3d_clip_slot((void **)&st->animation);
        int32_t channel_count;
        if (!anim || !isfinite(st->weight) || st->weight < 1e-6f)
            continue;

        /* Sample this state's channels into temp */
        for (int32_t i = 0; i < bc; i++)
            memcpy(
                &b->temp_state_local[i * 16], skel->bones[i].bind_pose_local, 16 * sizeof(float));

        channel_count = animation3d_safe_channel_count(anim);
        for (int32_t c = 0; c < channel_count; c++) {
            int32_t bone = anim->channels[c].bone_index;
            if (bone < 0 || bone >= bc)
                continue;
            sample_channel_with_fallback(&anim->channels[c],
                                         st->anim_time,
                                         skel->bones[bone].bind_pose_local,
                                         &b->temp_state_local[bone * 16]);
        }

        /* Weighted TRS blend into local_transforms. */
        float w = st->weight;
        total_weight += w;
        float blend_t = skeleton3d_clamp01f((total_weight > 1e-6f) ? w / total_weight : 1.0f);

        for (int32_t bone = 0; bone < bc; bone++) {
            float from_pos[3], from_rot[4], from_scl[3];
            float to_pos[3], to_rot[4], to_scl[3];
            float blend_pos[3], blend_rot[4], blend_scl[3];
            mat4f_decompose_trs(&b->local_transforms[bone * 16], from_pos, from_rot, from_scl);
            mat4f_decompose_trs(&b->temp_state_local[bone * 16], to_pos, to_rot, to_scl);
            for (int32_t i = 0; i < 3; i++) {
                blend_pos[i] = from_pos[i] + (to_pos[i] - from_pos[i]) * blend_t;
                blend_scl[i] = from_scl[i] + (to_scl[i] - from_scl[i]) * blend_t;
            }
            quat_slerp_float(from_rot, to_rot, blend_t, blend_rot);
            animation3d_sanitize_trs(blend_pos, blend_rot, blend_scl, from_pos, from_rot, from_scl);
            build_trs_float(blend_pos, blend_rot, blend_scl, &b->local_transforms[bone * 16]);
        }
    }

    compute_palette_from_locals(skel, b->local_transforms, b->globals_buf, b->bone_palette);
}

/// @brief Number of registered states (0 for NULL).
int64_t rt_anim_blend3d_state_count(void *obj) {
    rt_anim_blend3d *b = anim_blend3d_checked(obj);
    return b ? animblend3d_safe_state_count(b) : 0;
}

/// @brief Borrow the skeleton handle this blender was constructed with.
void *rt_anim_blend3d_get_skeleton(void *obj) {
    rt_anim_blend3d *b = anim_blend3d_checked(obj);
    return b ? animation3d_skeleton_slot((void **)&b->skeleton) : NULL;
}

/// @brief Borrow the current blended local transform buffer for controller integration.
const float *rt_anim_blend3d_get_local_transform_data(void *obj, int32_t *bone_count) {
    rt_anim_blend3d *b = anim_blend3d_checked(obj);
    rt_skeleton3d *skel;
    if (bone_count)
        *bone_count = 0;
    if (!b || !b->local_transforms)
        return NULL;
    skel = animation3d_skeleton_slot((void **)&b->skeleton);
    if (!skel)
        return NULL;
    if (!anim_blend3d_ensure_pose_capacity(b, skeleton3d_safe_bone_count(skel)))
        return NULL;
    if (bone_count)
        *bone_count = skeleton3d_safe_bone_count(skel);
    return b->local_transforms;
}

/// @brief Same role as `anim_player_prepare_prev_palette` but for the blender — see that function.
static const float *anim_blend_prepare_prev_palette(rt_anim_blend3d *b, int64_t frame_serial) {
    rt_skeleton3d *skel;
    if (!b || !b->bone_palette)
        return NULL;
    skel = animation3d_skeleton_slot((void **)&b->skeleton);
    int32_t bone_count = skeleton3d_safe_bone_count(skel);
    if (!anim_blend3d_ensure_pose_capacity(b, bone_count))
        return NULL;
    size_t palette_size = (size_t)bone_count * 16 * sizeof(float);
    if (b->last_motion_frame != frame_serial) {
        if (b->prev_bone_palette && b->motion_palette_snapshot && bone_count > 0 &&
            b->last_motion_frame != 0) {
            memcpy(b->prev_bone_palette, b->motion_palette_snapshot, palette_size);
            b->has_prev_motion_palette = 1;
        } else if (!b->prev_bone_palette) {
            b->has_prev_motion_palette = 0;
        }
        if (b->motion_palette_snapshot && bone_count > 0)
            memcpy(b->motion_palette_snapshot, b->bone_palette, palette_size);
        b->last_motion_frame = frame_serial;
    }
    return b->has_prev_motion_palette ? b->prev_bone_palette : NULL;
}

/// @brief Draw a skinned mesh using a blended animation pose.
/// @details Thin convenience layer that pulls the final bone palette and the
///   matching previous-frame palette out of the `rt_anim_blend3d` wrapper
///   (built by the animation controller from one or more playing clips) and
///   forwards to `rt_canvas3d_draw_mesh_matrix_skinned_keyed`. The transform
///   object supplies both the model matrix (via its `mat4_impl`) and a
///   stable identity pointer used as the motion key for temporal effects.
void rt_canvas3d_draw_mesh_blended(
    void *canvas, void *mesh_obj, void *transform, void *material, void *blend_obj) {
    if (!canvas || !mesh_obj || !transform || !material || !blend_obj)
        return;
    if (!rt_canvas3d_checked_or_stack(canvas))
        return;
    mat4_impl *transform_mat = skeleton3d_mat4_checked(transform);
    if (!transform_mat)
        return;
    rt_anim_blend3d *b =
        (rt_anim_blend3d *)rt_g3d_checked_or_null(blend_obj, RT_G3D_ANIMBLEND3D_CLASS_ID);
    if (!b)
        b = (rt_anim_blend3d *)rt_blend_tree3d_get_blend(blend_obj);
    if (!b)
        return;
    rt_skeleton3d *skel = animation3d_skeleton_slot((void **)&b->skeleton);
    if (!skel || skeleton3d_safe_bone_count(skel) == 0)
        return;

    rt_mesh3d *mesh = (rt_mesh3d *)rt_g3d_checked_or_null(mesh_obj, RT_G3D_MESH3D_CLASS_ID);
    if (!mesh)
        return;
    rt_mesh3d_repair_geometry_counts(mesh);
    if (mesh->vertex_count == 0)
        return;

    const float *prev_palette =
        anim_blend_prepare_prev_palette(b, rt_canvas3d_get_frame_serial(canvas));
    if (!rt_canvas3d_add_temp_object(canvas, blend_obj))
        return;
    rt_canvas3d_draw_mesh_matrix_skinned_keyed(canvas,
                                               mesh,
                                               transform_mat->m,
                                               material,
                                               transform,
                                               b->bone_palette,
                                               prev_palette,
                                               skeleton3d_safe_bone_count(skel));
}
