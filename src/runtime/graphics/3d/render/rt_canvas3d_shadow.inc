//===----------------------------------------------------------------------===//
// Part of the Zanna project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/render/rt_canvas3d_shadow.inc  (#included from rt_canvas3d.c; not a
// standalone translation unit) Purpose: Canvas3D shadow-map view/projection fitting and shadow
// render-target slots
//===----------------------------------------------------------------------===//

/// @brief Build a tight orthographic shadow-map VP that bounds every opaque draw.
///
/// Algorithm:
///   1. Compute world-space AABB of all opaque draws.
///   2. Pick a light-space view position behind the AABB along the
///      light direction, looking at the AABB center.
///   3. Build the view matrix (right-handed, custom up vector).
///   4. Transform the 8 AABB corners into light space to find the
///      tight orthographic bounds.
///   5. Build the orthographic projection from those bounds.
/// Returns 0 if there are no opaque draws (nothing to shadow).
static int canvas3d_build_shadow_world_bounds(rt_canvas3d *c,
                                              const deferred_draw_t *cmds,
                                              const int32_t *shadow_indices,
                                              int32_t count,
                                              float *world_min,
                                              float *world_max) {
    int8_t has_bounds = 0;
    if (!c || !cmds || count <= 0 || !world_min || !world_max)
        return 0;
    world_min[0] = world_min[1] = world_min[2] = 0.0f;
    world_max[0] = world_max[1] = world_max[2] = 0.0f;
    for (int32_t i = 0; i < count; i++) {
        int32_t draw_index = shadow_indices ? shadow_indices[i] : i;
        if (draw_index < 0)
            continue;
        canvas3d_accumulate_deferred_world_bounds(
            c, &cmds[draw_index], world_min, world_max, &has_bounds);
    }
    return has_bounds ? 1 : 0;
}

static int canvas3d_shadow_bounds_are_valid(const float *world_min, const float *world_max) {
    if (!world_min || !world_max)
        return 0;
    for (int i = 0; i < 3; ++i) {
        if (!isfinite(world_min[i]) || !isfinite(world_max[i]) || world_min[i] > world_max[i])
            return 0;
    }
    return 1;
}

static void canvas3d_shadow_bounds_center(const float *world_min,
                                          const float *world_max,
                                          float *center) {
    center[0] = 0.5f * (world_min[0] + world_max[0]);
    center[1] = 0.5f * (world_min[1] + world_max[1]);
    center[2] = 0.5f * (world_min[2] + world_max[2]);
}

static void canvas3d_shadow_normalize_light_dir(const vgfx3d_light_params_t *dir_light,
                                                float *ldir) {
    float ll;
    ldir[0] = dir_light->direction[0];
    ldir[1] = dir_light->direction[1];
    ldir[2] = dir_light->direction[2];
    ll = sqrtf(ldir[0] * ldir[0] + ldir[1] * ldir[1] + ldir[2] * ldir[2]);
    if (isfinite(ll) && ll > 1e-7f) {
        ldir[0] /= ll;
        ldir[1] /= ll;
        ldir[2] /= ll;
    } else {
        ldir[0] = 0.0f;
        ldir[1] = -1.0f;
        ldir[2] = 0.0f;
    }
}

static int canvas3d_shadow_place_eye(const float *world_min,
                                     const float *world_max,
                                     const float *center,
                                     const float *ldir,
                                     float *eye) {
    float dx = world_max[0] - world_min[0];
    float dy = world_max[1] - world_min[1];
    float dz = world_max[2] - world_min[2];
    float radius = 0.5f * sqrtf(dx * dx + dy * dy + dz * dz);
    if (!isfinite(radius))
        return 0;
    if (radius < 1.0f)
        radius = 1.0f;
    eye[0] = center[0] - ldir[0] * (radius * 2.0f + 4.0f);
    eye[1] = center[1] - ldir[1] * (radius * 2.0f + 4.0f);
    eye[2] = center[2] - ldir[2] * (radius * 2.0f + 4.0f);
    return 1;
}

static void canvas3d_shadow_build_light_view(const float *center, const float *eye, float *view) {
    float fwd[3];
    float up[3] = {0.0f, 1.0f, 0.0f};
    float fl;
    float rx;
    float ry;
    float rz;
    float rl;
    float ux;
    float uy;
    float uz;

    fwd[0] = center[0] - eye[0];
    fwd[1] = center[1] - eye[1];
    fwd[2] = center[2] - eye[2];
    fl = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
    if (isfinite(fl) && fl > 1e-7f) {
        fwd[0] /= fl;
        fwd[1] /= fl;
        fwd[2] /= fl;
    } else {
        fwd[0] = 0.0f;
        fwd[1] = 0.0f;
        fwd[2] = -1.0f;
    }
    if (fabsf(fwd[0] * up[0] + fwd[1] * up[1] + fwd[2] * up[2]) > 0.99f) {
        up[0] = 0.0f;
        up[1] = 0.0f;
        up[2] = 1.0f;
    }

    rx = fwd[1] * up[2] - fwd[2] * up[1];
    ry = fwd[2] * up[0] - fwd[0] * up[2];
    rz = fwd[0] * up[1] - fwd[1] * up[0];
    rl = sqrtf(rx * rx + ry * ry + rz * rz);
    if (isfinite(rl) && rl > 1e-7f) {
        rx /= rl;
        ry /= rl;
        rz /= rl;
    } else {
        rx = 1.0f;
        ry = rz = 0.0f;
    }

    ux = ry * fwd[2] - rz * fwd[1];
    uy = rz * fwd[0] - rx * fwd[2];
    uz = rx * fwd[1] - ry * fwd[0];

    view[0] = rx;
    view[1] = ry;
    view[2] = rz;
    view[3] = -(rx * eye[0] + ry * eye[1] + rz * eye[2]);
    view[4] = ux;
    view[5] = uy;
    view[6] = uz;
    view[7] = -(ux * eye[0] + uy * eye[1] + uz * eye[2]);
    view[8] = fwd[0];
    view[9] = fwd[1];
    view[10] = fwd[2];
    view[11] = -(fwd[0] * eye[0] + fwd[1] * eye[1] + fwd[2] * eye[2]);
    view[12] = 0.0f;
    view[13] = 0.0f;
    view[14] = 0.0f;
    view[15] = 1.0f;
}

static void canvas3d_shadow_build_world_corners(const float *world_min,
                                                const float *world_max,
                                                float corners[8][3]) {
    corners[0][0] = world_min[0];
    corners[0][1] = world_min[1];
    corners[0][2] = world_min[2];
    corners[1][0] = world_max[0];
    corners[1][1] = world_min[1];
    corners[1][2] = world_min[2];
    corners[2][0] = world_min[0];
    corners[2][1] = world_max[1];
    corners[2][2] = world_min[2];
    corners[3][0] = world_max[0];
    corners[3][1] = world_max[1];
    corners[3][2] = world_min[2];
    corners[4][0] = world_min[0];
    corners[4][1] = world_min[1];
    corners[4][2] = world_max[2];
    corners[5][0] = world_max[0];
    corners[5][1] = world_min[1];
    corners[5][2] = world_max[2];
    corners[6][0] = world_min[0];
    corners[6][1] = world_max[1];
    corners[6][2] = world_max[2];
    corners[7][0] = world_max[0];
    corners[7][1] = world_max[1];
    corners[7][2] = world_max[2];
}

static void canvas3d_shadow_accumulate_light_bounds(const float *view,
                                                    const float corners[8][3],
                                                    float *ls_min,
                                                    float *ls_max) {
    ls_min[0] = ls_min[1] = ls_min[2] = FLT_MAX;
    ls_max[0] = ls_max[1] = ls_max[2] = -FLT_MAX;
    for (int i = 0; i < 8; i++) {
        float x = corners[i][0];
        float y = corners[i][1];
        float z = corners[i][2];
        float lx = view[0] * x + view[1] * y + view[2] * z + view[3];
        float ly = view[4] * x + view[5] * y + view[6] * z + view[7];
        float lz = view[8] * x + view[9] * y + view[10] * z + view[11];
        if (lx < ls_min[0])
            ls_min[0] = lx;
        if (ly < ls_min[1])
            ls_min[1] = ly;
        if (lz < ls_min[2])
            ls_min[2] = lz;
        if (lx > ls_max[0])
            ls_max[0] = lx;
        if (ly > ls_max[1])
            ls_max[1] = ly;
        if (lz > ls_max[2])
            ls_max[2] = lz;
    }
}

static int canvas3d_shadow_build_ortho_projection(const float *ls_min,
                                                  const float *ls_max,
                                                  float *proj) {
    float pad_x = (ls_max[0] - ls_min[0]) * 0.05f + 1.0f;
    float pad_y = (ls_max[1] - ls_min[1]) * 0.05f + 1.0f;
    float pad_z = (ls_max[2] - ls_min[2]) * 0.10f + 2.0f;
    float left = ls_min[0] - pad_x;
    float right = ls_max[0] + pad_x;
    float bottom = ls_min[1] - pad_y;
    float top = ls_max[1] + pad_y;
    float near_z = ls_min[2] - pad_z;
    float far_z = ls_max[2] + pad_z;

    if (right - left < 1e-4f || top - bottom < 1e-4f || far_z - near_z < 1e-4f)
        return 0;

    memset(proj, 0, sizeof(float) * 16);
    proj[0] = 2.0f / (right - left);
    proj[3] = -(right + left) / (right - left);
    proj[5] = 2.0f / (top - bottom);
    proj[7] = -(top + bottom) / (top - bottom);
    proj[10] = 2.0f / (far_z - near_z);
    proj[11] = -(far_z + near_z) / (far_z - near_z);
    proj[15] = 1.0f;
    return 1;
}

static float canvas3d_shadow_clamp_finite(float value, float lo, float hi, float fallback) {
    if (!isfinite(value))
        value = fallback;
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return value;
}

static float canvas3d_shadow_spot_range_from_bounds(const vgfx3d_light_params_t *light,
                                                    const float *world_min,
                                                    const float *world_max) {
    float range = 0.0f;
    float corners[8][3];
    float max_distance = 0.0f;

    if (light && isfinite(light->attenuation) && light->attenuation > 1e-8f)
        range = 1.0f / sqrtf(light->attenuation);
    if (isfinite(range) && range > 0.05f)
        return range;

    canvas3d_shadow_build_world_corners(world_min, world_max, corners);
    for (int i = 0; i < 8; i++) {
        float dx = corners[i][0] - light->position[0];
        float dy = corners[i][1] - light->position[1];
        float dz = corners[i][2] - light->position[2];
        float distance = sqrtf(dx * dx + dy * dy + dz * dz);

        if (isfinite(distance) && distance > max_distance)
            max_distance = distance;
    }
    return max_distance > 1.0f ? max_distance : 1.0f;
}

static int canvas3d_shadow_build_perspective_projection(float outer_cos,
                                                        float near_z,
                                                        float far_z,
                                                        float *proj) {
    const float k_min_half_angle = 0.01745329252f; /* 1 degree */
    const float k_max_half_angle = 1.55334303427f; /* 89 degrees */
    float clamped_cos;
    float half_angle;
    float f;

    if (!proj || !isfinite(near_z) || !isfinite(far_z) || near_z <= 0.0f || far_z <= near_z + 1e-4f)
        return 0;
    clamped_cos = canvas3d_shadow_clamp_finite(outer_cos, -0.9998477f, 0.9998477f, 0.7071068f);
    half_angle = acosf(clamped_cos);
    half_angle =
        canvas3d_shadow_clamp_finite(half_angle, k_min_half_angle, k_max_half_angle, 0.7853982f);
    f = 1.0f / tanf(half_angle);
    if (!isfinite(f) || f <= 0.0f)
        return 0;

    memset(proj, 0, sizeof(float) * 16);
    proj[0] = f;
    proj[5] = f;
    proj[10] = (far_z + near_z) / (far_z - near_z);
    proj[11] = -(2.0f * far_z * near_z) / (far_z - near_z);
    proj[14] = 1.0f;
    return 1;
}

static int canvas3d_build_spot_shadow_light_vp(const float *world_min,
                                               const float *world_max,
                                               const vgfx3d_light_params_t *spot_light,
                                               float *out_light_vp) {
    float ldir[3];
    float eye[3];
    float center[3];
    float view[16];
    float proj[16];
    float far_z;
    float near_z;

    if (!world_min || !world_max || !spot_light || !out_light_vp)
        return 0;
    canvas3d_shadow_normalize_light_dir(spot_light, ldir);
    eye[0] = spot_light->position[0];
    eye[1] = spot_light->position[1];
    eye[2] = spot_light->position[2];
    if (!isfinite(eye[0]) || !isfinite(eye[1]) || !isfinite(eye[2]))
        return 0;
    center[0] = eye[0] + ldir[0];
    center[1] = eye[1] + ldir[1];
    center[2] = eye[2] + ldir[2];
    far_z = canvas3d_shadow_spot_range_from_bounds(spot_light, world_min, world_max);
    if (!isfinite(far_z) || far_z <= 0.1f)
        far_z = 1.0f;
    near_z = fmaxf(0.02f, fminf(0.5f, far_z * 0.02f));
    if (near_z >= far_z)
        near_z = far_z * 0.1f;
    if (near_z < 0.001f)
        near_z = 0.001f;

    canvas3d_shadow_build_light_view(center, eye, view);
    if (!canvas3d_shadow_build_perspective_projection(spot_light->outer_cos, near_z, far_z, proj))
        return 0;
    canvas3d_mul_mat4(proj, view, out_light_vp);
    return 1;
}

/// @brief Build the view-projection for one face of an omnidirectional point shadow.
/// @details 92-degree perspective faces (2 degrees of guard band so PCF taps near a
///   face seam still land inside the face's tile) along +X,-X,+Y,-Y,+Z,-Z. The far
///   plane derives from the shadow-caster world bounds exactly like spot shadows.
static int canvas3d_build_point_shadow_face_vp(const float *world_min,
                                               const float *world_max,
                                               const vgfx3d_light_params_t *point_light,
                                               int32_t face,
                                               float *out_light_vp) {
    static const float face_dirs[VGFX3D_SHADOW_CUBE_FACES][3] = {
        {1.0f, 0.0f, 0.0f},
        {-1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, -1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, -1.0f},
    };
    float eye[3];
    float center[3];
    float view[16];
    float proj[16];
    float far_z;
    float near_z;
    /* cos(46 deg): a 92-degree cone, i.e. 90 degrees + guard band. */
    const float face_cos = 0.69466f;

    if (!world_min || !world_max || !point_light || !out_light_vp || face < 0 ||
        face >= VGFX3D_SHADOW_CUBE_FACES)
        return 0;
    eye[0] = point_light->position[0];
    eye[1] = point_light->position[1];
    eye[2] = point_light->position[2];
    if (!isfinite(eye[0]) || !isfinite(eye[1]) || !isfinite(eye[2]))
        return 0;
    center[0] = eye[0] + face_dirs[face][0];
    center[1] = eye[1] + face_dirs[face][1];
    center[2] = eye[2] + face_dirs[face][2];
    far_z = canvas3d_shadow_spot_range_from_bounds(point_light, world_min, world_max);
    if (!isfinite(far_z) || far_z <= 0.1f)
        far_z = 1.0f;
    near_z = fmaxf(0.02f, fminf(0.5f, far_z * 0.02f));
    if (near_z >= far_z)
        near_z = far_z * 0.1f;
    if (near_z < 0.001f)
        near_z = 0.001f;

    canvas3d_shadow_build_light_view(center, eye, view);
    if (!canvas3d_shadow_build_perspective_projection(face_cos, near_z, far_z, proj))
        return 0;
    canvas3d_mul_mat4(proj, view, out_light_vp);
    return 1;
}

/// @brief Snap light-space bounds to shadow-map texel/depth increments.
/// @details Stable cascaded shadows need the orthographic light projection to move in whole shadow
///   texels; otherwise tiny camera movements shift the projected depth map under the scene and
///   create visible shimmer/flicker. X/Y preserve span and snap only the center. Z receives a
///   small conservative pad and snaps both center and span to reduce depth remapping shimmer when
///   casters move by tiny amounts.
static void canvas3d_shadow_snap_light_bounds(float *ls_min, float *ls_max, int32_t resolution) {
    if (!ls_min || !ls_max || resolution <= 0)
        return;
    for (int axis = 0; axis < 2; axis++) {
        float span = ls_max[axis] - ls_min[axis];
        float texel;
        float center;
        float snapped_center;

        if (!isfinite(span) || span <= 1e-6f)
            continue;
        texel = span / (float)resolution;
        if (!isfinite(texel) || texel <= 1e-8f)
            continue;
        center = 0.5f * (ls_min[axis] + ls_max[axis]);
        snapped_center = floorf(center / texel + 0.5f) * texel;
        ls_min[axis] = snapped_center - span * 0.5f;
        ls_max[axis] = snapped_center + span * 0.5f;
    }
    {
        float span = ls_max[2] - ls_min[2];
        float quantum;
        float center;
        float snapped_center;
        float snapped_span;
        if (!isfinite(span) || span <= 1e-6f)
            return;
        quantum = fmaxf(span / (float)(resolution > 0 ? resolution : 1024), 0.01f);
        center = 0.5f * (ls_min[2] + ls_max[2]);
        snapped_center = floorf(center / quantum + 0.5f) * quantum;
        snapped_span = ceilf((span * 1.04f + quantum * 2.0f) / quantum) * quantum;
        if (isfinite(snapped_center) && isfinite(snapped_span) && snapped_span > span) {
            ls_min[2] = snapped_center - snapped_span * 0.5f;
            ls_max[2] = snapped_center + snapped_span * 0.5f;
        }
    }
}

/// @brief Build the light's view-projection matrix that tightly bounds a world AABB for shadow
/// mapping.
/// @details Fits an orthographic light frustum around the scene bounds so the shadow map covers the
///          visible geometry at maximum resolution.
static int canvas3d_build_shadow_light_vp(const float *world_min,
                                          const float *world_max,
                                          const vgfx3d_light_params_t *shadow_light,
                                          int32_t shadow_resolution,
                                          float *out_light_vp) {
    float center[3];
    float ldir[3];
    float eye[3];
    float view[16];
    float proj[16];
    float corners[8][3];
    float ls_min[3];
    float ls_max[3];

    if (!world_min || !world_max || !shadow_light || !out_light_vp)
        return 0;
    if (!canvas3d_shadow_bounds_are_valid(world_min, world_max))
        return 0;
    if (shadow_light->type == 3)
        return canvas3d_build_spot_shadow_light_vp(
            world_min, world_max, shadow_light, out_light_vp);
    canvas3d_shadow_bounds_center(world_min, world_max, center);
    canvas3d_shadow_normalize_light_dir(shadow_light, ldir);
    if (!canvas3d_shadow_place_eye(world_min, world_max, center, ldir, eye))
        return 0;
    canvas3d_shadow_build_light_view(center, eye, view);
    canvas3d_shadow_build_world_corners(world_min, world_max, corners);
    canvas3d_shadow_accumulate_light_bounds(view, corners, ls_min, ls_max);
    canvas3d_shadow_snap_light_bounds(ls_min, ls_max, shadow_resolution);
    if (!canvas3d_shadow_build_ortho_projection(ls_min, ls_max, proj))
        return 0;

    canvas3d_mul_mat4(proj, view, out_light_vp);
    return 1;
}

/// @brief Free every shadow-pair render target and zero the live count.
/// @details Each slot in `shadow_rts` owns its own color and depth buffers
///   (allocated by `canvas3d_ensure_shadow_targets`); this walks the array and
///   releases all three allocations per slot. Safe to call during finalization
///   or after `ensure` detects that the cached resolution no longer matches.
static void canvas3d_release_shadow_targets(rt_canvas3d *c) {
    if (!c)
        return;
    for (int32_t slot = 0; slot < VGFX3D_MAX_SHADOW_LIGHTS; slot++) {
        if (!c->shadow_rts[slot])
            continue;
        if (c->shadow_rt_owned[slot]) {
            free(c->shadow_rts[slot]->color_buf);
            free(c->shadow_rts[slot]->hdr_color_buf);
            free(c->shadow_rts[slot]->depth_buf);
            free(c->shadow_rts[slot]);
        }
        c->shadow_rts[slot] = NULL;
        c->shadow_rt_owned[slot] = 0;
    }
    c->shadow_count = 0;
}

/// @brief Resolve the effective square shadow-map resolution for one target slot.
/// @details Publicly enabled shadows store their intended resolution on the canvas. Tests and some
///   internal fixtures can preseed `shadow_rts` directly and leave that field unset; in that case
///   the active target dimensions are the only reliable source. Returning zero means the slot is
///   not usable for light-space texel snapping or rendering.
static int32_t canvas3d_shadow_slot_resolution(const rt_canvas3d *c, int32_t slot) {
    const vgfx3d_rendertarget_t *rt;

    if (!c || slot < 0 || slot >= VGFX3D_MAX_SHADOW_LIGHTS)
        return 0;
    if (c->shadow_resolution > 0)
        return c->shadow_resolution;
    rt = c->shadow_rts[slot];
    if (!rt || !rt->depth_buf || rt->width <= 0 || rt->height <= 0)
        return 0;
    return rt->width < rt->height ? rt->width : rt->height;
}

/// @brief Return non-zero if any allocated shadow slot uses a different resolution.
/// @details Canvas shadow slots are sampled as a contiguous array by the lighting pass; keeping all
///   allocated slots at one square resolution avoids per-slot scale bugs and lets a resolution
///   change release every stale depth buffer in one place.
static int canvas3d_shadow_targets_need_resize(const rt_canvas3d *c, int32_t resolution) {
    if (!c || resolution <= 0)
        return 0;
    for (int32_t slot = 0; slot < VGFX3D_MAX_SHADOW_LIGHTS; slot++) {
        const vgfx3d_rendertarget_t *rt = c->shadow_rts[slot];
        if (rt && (rt->width != resolution || rt->height != resolution))
            return 1;
    }
    return 0;
}

/// @brief Lazily allocate one CPU shadow depth target slot at the requested resolution.
/// @details Only the slots actually rendered this frame are allocated. If an existing shadow target
///   uses a stale resolution, all slots are released before allocating the requested one so the
///   contiguous shadow array remains uniform. Color buffers stay null because shadow passes write
///   depth only.
/// @return 1 when @p slot is ready for shadow rendering, 0 on invalid input or allocation failure.
static int canvas3d_ensure_shadow_target_slot(rt_canvas3d *c, int32_t slot, int32_t resolution) {
    size_t depth_bytes;
    vgfx3d_rendertarget_t *new_rt;

    if (!c || slot < 0 || slot >= VGFX3D_MAX_SHADOW_LIGHTS)
        return 0;
    if (resolution <= 0) {
        vgfx3d_rendertarget_t *rt = c->shadow_rts[slot];

        return rt && rt->depth_buf && rt->width > 0 && rt->height > 0;
    }
    if ((size_t)resolution > SIZE_MAX / (size_t)resolution)
        return 0;
    if (resolution > INT32_MAX / 4)
        return 0;
    depth_bytes = (size_t)resolution * (size_t)resolution;
    if (depth_bytes > SIZE_MAX / sizeof(float))
        return 0;
    depth_bytes *= sizeof(float);
    if (canvas3d_shadow_targets_need_resize(c, resolution))
        canvas3d_release_shadow_targets(c);
    if (c->shadow_rts[slot] && c->shadow_rts[slot]->depth_buf &&
        c->shadow_rts[slot]->width == resolution && c->shadow_rts[slot]->height == resolution)
        return 1;

    new_rt = (vgfx3d_rendertarget_t *)calloc(1, sizeof(vgfx3d_rendertarget_t));
    if (!new_rt)
        return 0;
    new_rt->width = resolution;
    new_rt->height = resolution;
    new_rt->stride = resolution * 4;
    new_rt->color_buf = NULL;
    new_rt->depth_buf = (float *)malloc(depth_bytes);
    if (!new_rt->depth_buf) {
        free(new_rt);
        return 0;
    }
    vgfx3d_rendertarget_fill_depth_max(new_rt->depth_buf, (size_t)resolution * (size_t)resolution);
    c->shadow_rts[slot] = new_rt;
    c->shadow_rt_owned[slot] = 1;
    return 1;
}

/// @brief Ensure at least the first shadow target exists, or query whether any target is usable.
/// @details The render pass allocates additional slots on demand via
///   `canvas3d_ensure_shadow_target_slot`. This compatibility wrapper keeps public shadow-enable
///   validation cheap for one-light scenes while preserving the old `resolution <= 0` query mode.
/// @return 1 on success or if any usable target exists in query mode; 0 otherwise.
static int canvas3d_ensure_shadow_targets(rt_canvas3d *c, int32_t resolution) {
    if (!c)
        return 0;
    if (resolution <= 0) {
        for (int32_t slot = 0; slot < VGFX3D_MAX_SHADOW_LIGHTS; slot++) {
            vgfx3d_rendertarget_t *rt = c->shadow_rts[slot];

            if (!rt)
                continue;
            if (rt->width > 0 && rt->height > 0 && rt->depth_buf)
                return 1;
        }
        return 0;
    }
    return canvas3d_ensure_shadow_target_slot(c, 0, resolution);
}
