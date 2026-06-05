//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/backend/vgfx3d_backend_sw_shadow.inc  (#included from vgfx3d_backend_sw.c; not a standalone translation unit)
// Purpose: Software backend wireframe lines and shadow-map depth rasterization
//===----------------------------------------------------------------------===//
/*==========================================================================
 * Wireframe — Bresenham line
 *=========================================================================*/

/// @brief Bresenham-style line rasterizer used for wireframe mode.
///
/// Walks the longer of dx or dy, stepping the shorter axis via an
/// error-accumulator. No anti-aliasing — we trade quality for the
/// software backend's per-pixel speed.
static void draw_line(uint8_t *pixels,
                      int32_t fb_w,
                      int32_t fb_h,
                      int32_t stride,
                      int x0,
                      int y0,
                      int x1,
                      int y1,
                      uint8_t r,
                      uint8_t g,
                      uint8_t b) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (x0 >= 0 && x0 < fb_w && y0 >= 0 && y0 < fb_h) {
            uint8_t *dst = &pixels[y0 * stride + x0 * 4];
            dst[0] = r;
            dst[1] = g;
            dst[2] = b;
            dst[3] = 0xFF;
        }
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/*==========================================================================
 * Shadow map — depth-only rasterization
 *=========================================================================*/

/// @brief Rasterize a single triangle into a depth buffer (no color, no lighting).
/// @brief Depth-only triangle rasterizer for the shadow map pass.
///
/// Slimmer than `raster_triangle` — only writes Z (no color, lighting,
/// or texturing). Same edge-function approach.
static void shadow_raster_tri(float *depth,
                              int32_t sw,
                              int32_t sh,
                              float *sx,
                              float *sy,
                              float *sz,
                              const float *su,
                              const float *sv,
                              const float *su1,
                              const float *sv1,
                              const float *sa,
                              const vgfx3d_draw_cmd_t *cmd,
                              const sw_pixels_view *alpha_tex) {
    float u[3] = {su ? su[0] : 0.0f, su ? su[1] : 0.0f, su ? su[2] : 0.0f};
    float vcoord[3] = {sv ? sv[0] : 0.0f, sv ? sv[1] : 0.0f, sv ? sv[2] : 0.0f};
    float u1[3] = {su1 ? su1[0] : 0.0f, su1 ? su1[1] : 0.0f, su1 ? su1[2] : 0.0f};
    float v1coord[3] = {sv1 ? sv1[0] : 0.0f, sv1 ? sv1[1] : 0.0f, sv1 ? sv1[2] : 0.0f};
    float alpha_v[3] = {sa ? sa[0] : 1.0f, sa ? sa[1] : 1.0f, sa ? sa[2] : 1.0f};
    /* Screen-space area (winding check) */
    float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
    if (area < 0.0f) {
        /* Swap v1/v2 to ensure positive area */
        float t;
        t = sx[1];
        sx[1] = sx[2];
        sx[2] = t;
        t = sy[1];
        sy[1] = sy[2];
        sy[2] = t;
        t = sz[1];
        sz[1] = sz[2];
        sz[2] = t;
        t = u[1];
        u[1] = u[2];
        u[2] = t;
        t = vcoord[1];
        vcoord[1] = vcoord[2];
        vcoord[2] = t;
        t = u1[1];
        u1[1] = u1[2];
        u1[2] = t;
        t = v1coord[1];
        v1coord[1] = v1coord[2];
        v1coord[2] = t;
        t = alpha_v[1];
        alpha_v[1] = alpha_v[2];
        alpha_v[2] = t;
        area = -area;
    }
    if (area < 1e-12f)
        return;

    float inv_area = 1.0f / area;
    int min_x = (int)fmaxf(floorf(fminf(fminf(sx[0], sx[1]), sx[2])), 0.0f);
    int max_x = (int)fminf(ceilf(fmaxf(fmaxf(sx[0], sx[1]), sx[2])), (float)(sw - 1));
    int min_y = (int)fmaxf(floorf(fminf(fminf(sy[0], sy[1]), sy[2])), 0.0f);
    int max_y = (int)fminf(ceilf(fmaxf(fmaxf(sy[0], sy[1]), sy[2])), (float)(sh - 1));
    if (min_x > max_x || min_y > max_y)
        return;

    float e12_dx = sx[2] - sx[1], e12_dy = sy[2] - sy[1];
    float e20_dx = sx[0] - sx[2], e20_dy = sy[0] - sy[2];
    float e01_dx = sx[1] - sx[0], e01_dy = sy[1] - sy[0];
    float px0 = (float)min_x + 0.5f, py0 = (float)min_y + 0.5f;
    float row_w0 = e12_dx * (py0 - sy[1]) - e12_dy * (px0 - sx[1]);
    float row_w1 = e20_dx * (py0 - sy[2]) - e20_dy * (px0 - sx[2]);
    float row_w2 = e01_dx * (py0 - sy[0]) - e01_dy * (px0 - sx[0]);
    const float edge_epsilon = 0.0f;

    for (int y = min_y; y <= max_y; y++) {
        float w0 = row_w0, w1 = row_w1, w2 = row_w2;
        for (int x = min_x; x <= max_x; x++) {
            if (w0 >= edge_epsilon && w1 >= edge_epsilon && w2 >= edge_epsilon) {
                float b0 = w0 * inv_area, b1 = w1 * inv_area, b2 = w2 * inv_area;
                float z = b0 * sz[0] + b1 * sz[1] + b2 * sz[2];
                int idx = y * sw + x;
                if (cmd && cmd->alpha_mode == RT_MATERIAL3D_ALPHA_MODE_MASK) {
                    float alpha = cmd->diffuse_color[3] * cmd->alpha *
                                  (b0 * alpha_v[0] + b1 * alpha_v[1] + b2 * alpha_v[2]);
                    if (alpha_tex) {
                        float tr, tg, tb, ta;
                        int use_uv1 =
                            cmd->texture_slot_uv_set[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR] > 0;
                        const float *m =
                            cmd->texture_slot_uv_transform[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR];
                        float base_u = use_uv1 ? (b0 * u1[0] + b1 * u1[1] + b2 * u1[2])
                                               : (b0 * u[0] + b1 * u[1] + b2 * u[2]);
                        float base_v = use_uv1
                                           ? (b0 * v1coord[0] + b1 * v1coord[1] + b2 * v1coord[2])
                                           : (b0 * vcoord[0] + b1 * vcoord[1] + b2 * vcoord[2]);
                        float tex_u = base_u * m[0] + base_v * m[1] + m[4];
                        float tex_v = base_u * m[2] + base_v * m[3] + m[5];
                        sample_texture_slot_ex(alpha_tex,
                                               cmd,
                                               RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR,
                                               tex_u,
                                               tex_v,
                                               &tr,
                                               &tg,
                                               &tb,
                                               &ta);
                        alpha *= ta;
                    }
                    if (alpha < cmd->alpha_cutoff)
                        goto next_shadow_pixel;
                }
                if (z < depth[idx])
                    depth[idx] = z;
            }
        next_shadow_pixel:
            w0 -= e12_dy;
            w1 -= e20_dy;
            w2 -= e01_dy;
        }
        row_w0 += e12_dx;
        row_w1 += e20_dx;
        row_w2 += e01_dx;
    }
}

typedef struct {
    float clip[4];
    float uv[2];
    float uv1[2];
    float color_alpha;
} shadow_clip_vertex_t;

/// @brief Signed distance from a clip-space vertex to the @p plane-th frustum plane.
/// @details Plane indexing (0..5) is left, right, bottom, top, near, far. Positive
///   results mean the vertex is on the inside half-space of that plane. Used by the
///   shadow-pass Sutherland-Hodgman polygon clipper to test which side each input
///   vertex falls on.
static float shadow_clip_distance(const shadow_clip_vertex_t *v, int plane) {
    switch (plane) {
        case 0:
            return v->clip[0] + v->clip[3]; /* left */
        case 1:
            return v->clip[3] - v->clip[0]; /* right */
        case 2:
            return v->clip[1] + v->clip[3]; /* bottom */
        case 3:
            return v->clip[3] - v->clip[1]; /* top */
        case 4:
            return v->clip[2] + v->clip[3]; /* near */
        default:
            return v->clip[3] - v->clip[2]; /* far */
    }
}

/// @brief Linearly interpolate two clip-space vertices at parameter @p t.
/// @details Used by the shadow polygon clipper to construct the new vertex generated
///   when an edge crosses a clip plane. The interpolation runs on the homogeneous
///   clip-space coordinates so the resulting position lies on the original edge.
static shadow_clip_vertex_t shadow_clip_lerp(const shadow_clip_vertex_t *a,
                                             const shadow_clip_vertex_t *b,
                                             float t) {
    shadow_clip_vertex_t out;
    for (int i = 0; i < 4; i++)
        out.clip[i] = a->clip[i] + (b->clip[i] - a->clip[i]) * t;
    out.uv[0] = a->uv[0] + (b->uv[0] - a->uv[0]) * t;
    out.uv[1] = a->uv[1] + (b->uv[1] - a->uv[1]) * t;
    out.uv1[0] = a->uv1[0] + (b->uv1[0] - a->uv1[0]) * t;
    out.uv1[1] = a->uv1[1] + (b->uv1[1] - a->uv1[1]) * t;
    out.color_alpha = a->color_alpha + (b->color_alpha - a->color_alpha) * t;
    return out;
}

/// @brief Sutherland-Hodgman clip a polygon in 4D clip space against the 6 frustum planes.
/// @details Walks each plane in turn; for every input edge, emits the entry intersection
///   when one endpoint is outside, and emits the inside endpoint unchanged. The output
///   polygon replaces the input via a `tmp[]` swap buffer (max 16 vertices). Returns the
///   final clipped polygon vertex count, with the result written back to @p poly. Used
///   by the shadow-pass shadow-map rasterizer to clip triangles against the shadow frustum.
static int shadow_clip_polygon(shadow_clip_vertex_t *poly, int count) {
    shadow_clip_vertex_t tmp[16];
    for (int plane = 0; plane < 6; plane++) {
        int out_count = 0;
        if (count <= 0)
            return 0;
        shadow_clip_vertex_t prev = poly[count - 1];
        float prev_d = shadow_clip_distance(&prev, plane);
        int prev_inside = prev_d >= 0.0f;
        for (int i = 0; i < count; i++) {
            shadow_clip_vertex_t cur = poly[i];
            float cur_d = shadow_clip_distance(&cur, plane);
            int cur_inside = cur_d >= 0.0f;
            if (cur_inside != prev_inside) {
                float denom = prev_d - cur_d;
                float t = fabsf(denom) > 1e-8f ? prev_d / denom : 0.0f;
                if (out_count < (int)(sizeof(tmp) / sizeof(tmp[0])))
                    tmp[out_count++] = shadow_clip_lerp(&prev, &cur, t);
            }
            if (cur_inside && out_count < (int)(sizeof(tmp) / sizeof(tmp[0])))
                tmp[out_count++] = cur;
            prev = cur;
            prev_d = cur_d;
            prev_inside = cur_inside;
        }
        if (out_count <= 0)
            return 0;
        memcpy(poly, tmp, (size_t)out_count * sizeof(*poly));
        count = out_count;
    }
    return count;
}

/// @brief Backend `shadow_begin` op — allocate / clear the shadow depth buffer.
///
/// Caller-provided depth buffer (so the canvas owns the storage and
/// can resize without consulting the backend). Captures the light VP
/// matrix that `shadow_draw` will use.
static void sw_shadow_begin(
    void *ctx_ptr, int32_t slot, float *depth_buf, int32_t w, int32_t h, const float *light_vp) {
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    size_t pixel_count;
    if (!ctx)
        return;
    ctx->shadow_pass_slot = -1;
    if (slot < 0 || slot >= VGFX3D_MAX_SHADOW_LIGHTS)
        return;
    ctx->shadow_complete[slot] = 0;
    if (!depth_buf || !light_vp || w <= 0 || h <= 0) {
        sw_recompute_shadow_count(ctx);
        return;
    }
    if ((size_t)w > SIZE_MAX / (size_t)h) {
        sw_recompute_shadow_count(ctx);
        return;
    }
    pixel_count = (size_t)w * (size_t)h;
    if (pixel_count > (size_t)INT32_MAX) {
        sw_recompute_shadow_count(ctx);
        return;
    }
    ctx->shadow_pass_slot = (int8_t)slot;
    ctx->shadow_depth[slot] = depth_buf;
    ctx->shadow_w[slot] = w;
    ctx->shadow_h[slot] = h;
    memcpy(ctx->shadow_vp[slot], light_vp, 16 * sizeof(float));
    /* Clear shadow depth to FLT_MAX */
    for (size_t i = 0; i < pixel_count; i++)
        depth_buf[i] = FLT_MAX;
}

/// @brief Backend `shadow_draw` op — rasterize one mesh into the shadow depth buffer.
///
/// Same vertex transform path as `submit_draw` (model matrix + light VP)
/// but feeds each triangle to `shadow_raster_tri` instead of the full
/// rasterizer. Triangles are clipped in homogeneous light clip-space before
/// perspective divide so near-plane crossings cannot poison the shadow map.
static void sw_shadow_draw(void *ctx_ptr, const vgfx3d_draw_cmd_t *cmd) {
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    int32_t slot;
    sw_pixels_view alpha_view;
    sw_pixels_view *alpha_tex = NULL;
    uint32_t index_count;
    if (!ctx || !cmd || !cmd->vertices || !cmd->indices || cmd->vertex_count == 0 ||
        cmd->index_count == 0)
        return;
    index_count = vgfx3d_draw_cmd_validated_index_count(cmd);
    if (index_count == 0)
        return;
    slot = ctx->shadow_pass_slot;
    if (slot < 0 || slot >= VGFX3D_MAX_SHADOW_LIGHTS || !ctx->shadow_depth[slot])
        return;
    /* Skip transparent objects unless Canvas3D explicitly converted them to shadow cutouts. */
    if ((cmd->additive_blend || vgfx3d_draw_cmd_uses_alpha_blend(cmd)) &&
        cmd->shadow_mode != RT_MATERIAL3D_SHADOW_MODE_CAST)
        return;
    if (cmd->alpha_mode == RT_MATERIAL3D_ALPHA_MODE_MASK && cmd->texture) {
        const sw_pixels_view *pv = (const sw_pixels_view *)cmd->texture;
        alpha_view = *pv;
        if (alpha_view.width > 0 && alpha_view.height > 0 && alpha_view.data)
            alpha_tex = &alpha_view;
    }
    if (cmd->alpha_mode == RT_MATERIAL3D_ALPHA_MODE_MASK && cmd->texture_asset && !alpha_tex) {
        /* Native-only cutout textures cannot be sampled by the software shadow pass. Skipping is
         * conservative; casting the full mesh would create backend-specific solid shadow flicker. */
        return;
    }

    /* Build light MVP = shadow_vp * model */
    float lmvp[16];
    mat4f_mul(ctx->shadow_vp[slot], cmd->model_matrix, lmvp);

    float half_w = (float)ctx->shadow_w[slot] * 0.5f;
    float half_h = (float)ctx->shadow_h[slot] * 0.5f;

    for (uint32_t i = 0; i + 2 < index_count; i += 3) {
        uint32_t i0 = cmd->indices[i], i1 = cmd->indices[i + 1], i2 = cmd->indices[i + 2];
        if (i0 >= cmd->vertex_count || i1 >= cmd->vertex_count || i2 >= cmd->vertex_count)
            continue;

        shadow_clip_vertex_t clipped[16];
        for (int vi = 0; vi < 3; vi++) {
            const uint32_t idx[3] = {i0, i1, i2};
            const vgfx3d_vertex_t *v = &cmd->vertices[idx[vi]];
            float pos4[4] = {v->pos[0], v->pos[1], v->pos[2], 1.0f};
            mat4f_transform4(lmvp, pos4, clipped[vi].clip);
            clipped[vi].uv[0] = v->uv[0];
            clipped[vi].uv[1] = v->uv[1];
            clipped[vi].uv1[0] = v->uv1[0];
            clipped[vi].uv1[1] = v->uv1[1];
            clipped[vi].color_alpha = v->color[3];
        }
        int clipped_count = shadow_clip_polygon(clipped, 3);
        if (clipped_count < 3)
            continue;

        for (int tri = 1; tri + 1 < clipped_count; tri++) {
            const shadow_clip_vertex_t *fan[3] = {&clipped[0], &clipped[tri], &clipped[tri + 1]};
            float screen_x[3], screen_y[3], screen_z[3];
            float tex_u[3], tex_v[3], tex_u1[3], tex_v1[3], vert_alpha[3];
            int ok = 1;
            for (int vi = 0; vi < 3; vi++) {
                float w = fan[vi]->clip[3];
                if (w <= 1e-7f || !isfinite(w)) {
                    ok = 0;
                    break;
                }
                float iw = 1.0f / w;
                float ndc_x = fan[vi]->clip[0] * iw;
                float ndc_y = fan[vi]->clip[1] * iw;
                float ndc_z = fan[vi]->clip[2] * iw;
                if (!isfinite(ndc_x) || !isfinite(ndc_y) || !isfinite(ndc_z)) {
                    ok = 0;
                    break;
                }
                screen_x[vi] = (ndc_x + 1.0f) * half_w;
                screen_y[vi] = (1.0f - ndc_y) * half_h;
                screen_z[vi] = ndc_z * 0.5f + 0.5f;
                if (screen_z[vi] < 0.0f || screen_z[vi] > 1.0f) {
                    ok = 0;
                    break;
                }
                tex_u[vi] = fan[vi]->uv[0];
                tex_v[vi] = fan[vi]->uv[1];
                tex_u1[vi] = fan[vi]->uv1[0];
                tex_v1[vi] = fan[vi]->uv1[1];
                vert_alpha[vi] = fan[vi]->color_alpha;
            }
            if (!ok)
                continue;
            shadow_raster_tri(ctx->shadow_depth[slot],
                              ctx->shadow_w[slot],
                              ctx->shadow_h[slot],
                              screen_x,
                              screen_y,
                              screen_z,
                              tex_u,
                              tex_v,
                              tex_u1,
                              tex_v1,
                              vert_alpha,
                              cmd,
                              alpha_tex);
        }
    }
}

/// @brief Backend `shadow_end` op — mark shadow map ready and store the bias.
static void sw_shadow_end(void *ctx_ptr, int32_t slot, float bias) {
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    if (!ctx || slot < 0 || slot >= VGFX3D_MAX_SHADOW_LIGHTS)
        return;
    if (ctx->shadow_pass_slot != slot)
        return;
    ctx->shadow_bias = bias;
    ctx->shadow_complete[slot] =
        (ctx->shadow_depth[slot] && ctx->shadow_w[slot] > 0 && ctx->shadow_h[slot] > 0) ? 1 : 0;
    ctx->shadow_pass_slot = -1;
    sw_recompute_shadow_count(ctx);
}
