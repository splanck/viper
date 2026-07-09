//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/backend/vgfx3d_backend_sw_vtable.inc  (#included from
// vgfx3d_backend_sw.c; not a standalone translation unit) Purpose: Software backend vtable
// implementation (clear, frame, submit, present)
//===----------------------------------------------------------------------===//
/*==========================================================================
 * Backend vtable implementation
 *=========================================================================*/

/// @brief Backend `create_ctx` op — allocate the software-renderer state struct.
///
/// Trivial compared to hardware backends — no shaders, no FBOs, no
/// device. Just a `sw_context_t` with a deferred-allocated zbuffer.
static void *sw_create_ctx(vgfx_window_t win, int32_t w, int32_t h) {
    sw_context_t *ctx = (sw_context_t *)calloc(1, sizeof(sw_context_t));
    if (!ctx)
        return NULL;

    /* Use physical framebuffer dimensions (HiDPI-aware), not logical dimensions.
     * The rasterizer writes to fb.pixels which is at physical resolution. */
    vgfx_framebuffer_t fb;
    if (win && vgfx_get_framebuffer(win, &fb)) {
        ctx->width = fb.width;
        ctx->height = fb.height;
    } else {
        ctx->width = w;
        ctx->height = h;
    }
    ctx->win = win;
    ctx->render_scale = 1.0f;

    if (!sw_ensure_zbuf_capacity(ctx, ctx->width, ctx->height)) {
        free(ctx);
        return NULL;
    }
    sw_init_worker_pool(ctx);
    return ctx;
}

/// @brief Backend `destroy_ctx` op — free zbuffer + struct.
static void sw_destroy_ctx(void *ctx_ptr) {
    if (!ctx_ptr)
        return;
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    sw_release_worker_pool(ctx);
    free(ctx->zbuf);
    free(ctx->opaque_zbuf);
    free(ctx->vertex_scratch);
    free(ctx->scaled_color);
    free(ctx);
}

/// @brief True when window-backed frames should rasterize at reduced resolution.
static int sw_render_scale_active(const sw_context_t *ctx) {
    return ctx && !ctx->render_target && isfinite(ctx->render_scale) &&
           ctx->render_scale < 0.999f;
}

/// @brief Compute the scaled rasterization dimensions for a framebuffer size.
static void sw_scaled_dims(const sw_context_t *ctx, int32_t fb_w, int32_t fb_h,
                           int32_t *out_w, int32_t *out_h) {
    int32_t w = (int32_t)((float)fb_w * ctx->render_scale);
    int32_t h = (int32_t)((float)fb_h * ctx->render_scale);
    *out_w = w > 0 ? w : 1;
    *out_h = h > 0 ? h : 1;
}

/// @brief Ensure the internal scaled color buffer holds at least w*h pixels.
static int sw_ensure_scaled_color(sw_context_t *ctx, int32_t w, int32_t h) {
    size_t pixel_count;
    if (!ctx || w <= 0 || h <= 0)
        return 0;
    if ((size_t)w > SIZE_MAX / (size_t)h)
        return 0;
    pixel_count = (size_t)w * (size_t)h;
    if (pixel_count > SIZE_MAX / sizeof(uint32_t))
        return 0;
    if (ctx->scaled_color && ctx->scaled_color_capacity >= pixel_count) {
        ctx->scaled_w = w;
        ctx->scaled_h = h;
        return 1;
    }
    uint32_t *grown = (uint32_t *)realloc(ctx->scaled_color, pixel_count * sizeof(uint32_t));
    if (!grown)
        return 0;
    ctx->scaled_color = grown;
    ctx->scaled_color_capacity = pixel_count;
    ctx->scaled_w = w;
    ctx->scaled_h = h;
    return 1;
}

/// @brief Nearest-neighbor upscale of the internal scaled buffer into the framebuffer.
/// @details Row-index mapping precomputes nothing; the per-row source index uses a
///   fixed-point step to avoid per-pixel divides. Covers every framebuffer pixel, so
///   no pre-clear of the framebuffer is required.
static void sw_upscale_to_framebuffer(const sw_context_t *ctx, const vgfx_framebuffer_t *fb) {
    if (!ctx || !fb || !ctx->scaled_color || ctx->scaled_w <= 0 || ctx->scaled_h <= 0 ||
        fb->width <= 0 || fb->height <= 0 || !fb->pixels)
        return;
    uint64_t x_step = ((uint64_t)ctx->scaled_w << 16) / (uint64_t)fb->width;
    uint64_t y_step = ((uint64_t)ctx->scaled_h << 16) / (uint64_t)fb->height;
    uint64_t sy_fx = 0;
    for (int32_t y = 0; y < fb->height; y++, sy_fx += y_step) {
        int32_t sy = (int32_t)(sy_fx >> 16);
        if (sy >= ctx->scaled_h)
            sy = ctx->scaled_h - 1;
        const uint32_t *src_row = &ctx->scaled_color[(size_t)sy * (size_t)ctx->scaled_w];
        uint32_t *dst_row = (uint32_t *)(fb->pixels + (size_t)y * (size_t)fb->stride);
        uint64_t sx_fx = 0;
        if (((uintptr_t)dst_row & 3u) == 0u) {
            for (int32_t x = 0; x < fb->width; x++, sx_fx += x_step) {
                int32_t sx = (int32_t)(sx_fx >> 16);
                if (sx >= ctx->scaled_w)
                    sx = ctx->scaled_w - 1;
                dst_row[x] = src_row[sx];
            }
        } else {
            uint8_t *dst_bytes = (uint8_t *)dst_row;
            for (int32_t x = 0; x < fb->width; x++, sx_fx += x_step) {
                int32_t sx = (int32_t)(sx_fx >> 16);
                if (sx >= ctx->scaled_w)
                    sx = ctx->scaled_w - 1;
                memcpy(&dst_bytes[(size_t)x * 4u], &src_row[sx], 4u);
            }
        }
    }
}

/// @brief Fill an RGBA byte framebuffer with a solid opaque color.
/// @details Writes the first row pixel-by-pixel, then copies that row into
///   subsequent rows. This preserves the software framebuffer byte layout and
///   handles padded strides while avoiding per-pixel channel stores for every
///   row during clears.
/// @param pixels First byte of the target framebuffer.
/// @param width Number of pixels per row.
/// @param height Number of rows to fill.
/// @param stride Byte distance between two adjacent rows.
/// @param cr Red channel byte.
/// @param cg Green channel byte.
/// @param cb Blue channel byte.
static void sw_fill_rgba_rows(uint8_t *pixels,
                              int32_t width,
                              int32_t height,
                              int32_t stride,
                              uint8_t cr,
                              uint8_t cg,
                              uint8_t cb) {
    if (!pixels || width <= 0 || height <= 0 || width > INT32_MAX / 4 || stride < width * 4)
        return;
    uint8_t *row0 = pixels;
    for (int32_t x = 0; x < width; x++) {
        uint8_t *px = &row0[x * 4];
        px[0] = cr;
        px[1] = cg;
        px[2] = cb;
        px[3] = 0xFF;
    }
    size_t row_bytes = (size_t)width * 4u;
    for (int32_t y = 1; y < height; y++)
        memcpy(pixels + (size_t)y * (size_t)stride, row0, row_bytes);
}

/// @brief Backend `clear` op: clear the framebuffer to a solid color and reset Z.
/// @details Fills every color pixel with `(r, g, b, 1)` and sets every depth
///   value to FLT_MAX so the next draw's less-than depth tests succeed.
static void sw_clear(void *ctx_ptr, vgfx_window_t win, float r, float g, float b) {
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    if (!ctx)
        return;

    uint8_t cr = (uint8_t)(clamp01f(r) * 255.0f);
    uint8_t cg = (uint8_t)(clamp01f(g) * 255.0f);
    uint8_t cb = (uint8_t)(clamp01f(b) * 255.0f);

    if (ctx->render_target) {
        vgfx3d_rendertarget_t *rt = ctx->render_target;
        if (!vgfx3d_rendertarget_ensure_color(rt) || !vgfx3d_rendertarget_ensure_depth(rt))
            return;
        sw_fill_rgba_rows(rt->color_buf, rt->width, rt->height, rt->stride, cr, cg, cb);
        size_t total = 0;
        if (sw_pixel_count_checked(rt->width, rt->height, &total))
            sw_fill_depth_buffer(rt->depth_buf, total, FLT_MAX);
        rt->hdr_color_valid = 0;
        vgfx3d_rendertarget_clear_sync(rt);
    } else {
        vgfx_framebuffer_t fb;
        if (vgfx_get_framebuffer(win, &fb)) {
            if (sw_render_scale_active(ctx)) {
                int32_t sw_w;
                int32_t sw_h;
                sw_scaled_dims(ctx, fb.width, fb.height, &sw_w, &sw_h);
                if (!sw_ensure_scaled_color(ctx, sw_w, sw_h) ||
                    !sw_ensure_zbuf_capacity(ctx, sw_w, sw_h))
                    return;
                sw_fill_rgba_rows((uint8_t *)ctx->scaled_color,
                                  sw_w,
                                  sw_h,
                                  sw_w * 4,
                                  cr,
                                  cg,
                                  cb);
                ctx->scaled_frame_active = 1;
            } else {
                if (!sw_ensure_zbuf_capacity(ctx, fb.width, fb.height))
                    return;
                sw_fill_rgba_rows(fb.pixels, fb.width, fb.height, fb.stride, cr, cg, cb);
            }
        }
        size_t total = 0;
        if (sw_pixel_count_checked(ctx->width, ctx->height, &total))
            sw_fill_depth_buffer(ctx->zbuf, total, FLT_MAX);
    }
}

/// @brief Backend `begin_frame` op — capture camera + compute view-projection.
///
/// Multiplies projection × view, captures fog state and camera
/// position. Doesn't touch any pixels — drawing happens in `submit_draw`.
static void sw_begin_frame(void *ctx_ptr, const vgfx3d_camera_params_t *cam) {
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    if (!ctx || !cam)
        return;
    ctx->depth_probe_count = 0;
    /* VP = projection * view */
    mat4f_mul(cam->projection, cam->view, ctx->vp);
    ctx->cam_pos[0] = cam->position[0];
    ctx->cam_pos[1] = cam->position[1];
    ctx->cam_pos[2] = cam->position[2];
    ctx->cam_forward[0] = cam->forward[0];
    ctx->cam_forward[1] = cam->forward[1];
    ctx->cam_forward[2] = cam->forward[2];
    ctx->cam_is_ortho = cam->is_ortho ? 1 : 0;
    ctx->fog_enabled = cam->fog_enabled;
    ctx->fog_near = cam->fog_near;
    ctx->fog_far = cam->fog_far;
    ctx->fog_color[0] = cam->fog_color[0];
    ctx->fog_color[1] = cam->fog_color[1];
    ctx->fog_color[2] = cam->fog_color[2];
    ctx->height_fog_enabled = cam->height_fog_enabled;
    ctx->height_fog_base = cam->height_fog_base;
    ctx->height_fog_falloff = cam->height_fog_falloff;
    ctx->height_fog_density = cam->height_fog_density;
    ctx->height_fog_blend = cam->height_fog_blend;
    ctx->ibl_enabled = cam->ibl_enabled;
    ctx->ibl_intensity = cam->ibl_intensity;
    memcpy(ctx->ibl_sh, cam->ibl_sh, sizeof(ctx->ibl_sh));
    ctx->shadow_strength = cam->shadow_strength;
    ctx->shadow_slope_bias = cam->shadow_slope_bias;
    ctx->shadow_quality = cam->shadow_quality;
    ctx->cam_znear = cam->znear;
    ctx->cam_zfar = cam->zfar;
    ctx->opaque_zbuf_valid = 0; /* Plan 10: snapshot is per-frame */
    ctx->debug_draw_count = 0;
    ctx->debug_tri_count = 0;
    if (!cam->load_existing_depth) {
        if (ctx->render_target) {
            vgfx3d_rendertarget_t *rt = ctx->render_target;
            if (vgfx3d_rendertarget_ensure_depth(rt)) {
                size_t total = 0;
                if (sw_pixel_count_checked(rt->width, rt->height, &total))
                    sw_fill_depth_buffer(rt->depth_buf, total, FLT_MAX);
            }
        } else {
            size_t total = 0;
            if (sw_pixel_count_checked(ctx->width, ctx->height, &total))
                sw_fill_depth_buffer(ctx->zbuf, total, FLT_MAX);
        }
    }
    /* Reset shadow state — rebuilt if shadows are enabled this frame */
    ctx->shadow_pass_slot = -1;
    ctx->shadow_count = 0;
    memset(ctx->shadow_complete, 0, sizeof(ctx->shadow_complete));
}

/// @brief Backend `resolve_opaque_targets` op — snapshot the active target's
///   depth buffer at the opaque->transparent seam so blend draws (soft
///   particles) can fade against the opaque scene. A plain memcpy: the SW
///   depth buffer is CPU memory already.
static void sw_resolve_opaque_targets(void *ctx_ptr) {
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    const float *src = NULL;
    int32_t w = 0;
    int32_t h = 0;
    size_t total = 0;

    if (!ctx)
        return;
    ctx->opaque_zbuf_valid = 0;
    if (ctx->render_target) {
        vgfx3d_rendertarget_t *rt = ctx->render_target;
        src = rt->depth_buf;
        w = rt->width;
        h = rt->height;
    } else {
        src = ctx->zbuf;
        w = ctx->width;
        h = ctx->height;
    }
    if (!src || !sw_pixel_count_checked(w, h, &total) || total == 0)
        return;
    if (ctx->opaque_zbuf_capacity < total) {
        float *grown = (float *)realloc(ctx->opaque_zbuf, total * sizeof(float));
        if (!grown)
            return; /* no snapshot: blend draws keep hard edges this frame */
        ctx->opaque_zbuf = grown;
        ctx->opaque_zbuf_capacity = total;
    }
    memcpy(ctx->opaque_zbuf, src, total * sizeof(float));
    ctx->opaque_zbuf_w = w;
    ctx->opaque_zbuf_h = h;
    ctx->opaque_zbuf_valid = 1;
}

/// @brief A draw call's resolved material texture views plus the (possibly NULL)
///   pointers handed to the rasterizer. The pointers alias the views stored in
///   this struct, so it must outlive the rasterization loop that reads them.
typedef struct {
    sw_pixels_view tex_view, emissive_view, normal_view;
    sw_pixels_view specular_view, metallic_roughness_view, ao_view;
    sw_pixels_view *tex_ptr;
    sw_pixels_view *emissive_ptr;
    sw_pixels_view *normal_ptr;
    sw_pixels_view *specular_ptr;
    sw_pixels_view *metallic_roughness_ptr;
    sw_pixels_view *ao_ptr;
} sw_material_views;

/// @brief Copy each present material map (base color, emissive, normal, specular,
///   metallic-roughness, AO) into @p mv, setting the matching pointer non-NULL
///   only when the view has non-empty dimensions and backing data.
static void sw_resolve_material_views(const vgfx3d_draw_cmd_t *cmd, sw_material_views *mv) {
    mv->tex_ptr = NULL;
    mv->emissive_ptr = NULL;
    mv->normal_ptr = NULL;
    mv->specular_ptr = NULL;
    mv->metallic_roughness_ptr = NULL;
    mv->ao_ptr = NULL;
    if (cmd->texture) {
        const sw_pixels_view *pv = (const sw_pixels_view *)cmd->texture;
        mv->tex_view = *pv;
        if (mv->tex_view.width > 0 && mv->tex_view.height > 0 && mv->tex_view.data)
            mv->tex_ptr = &mv->tex_view;
    }
    if (cmd->emissive_map) {
        const sw_pixels_view *pv = (const sw_pixels_view *)cmd->emissive_map;
        mv->emissive_view = *pv;
        if (mv->emissive_view.width > 0 && mv->emissive_view.height > 0 && mv->emissive_view.data)
            mv->emissive_ptr = &mv->emissive_view;
    }
    if (cmd->normal_map) {
        const sw_pixels_view *pv = (const sw_pixels_view *)cmd->normal_map;
        mv->normal_view = *pv;
        if (mv->normal_view.width > 0 && mv->normal_view.height > 0 && mv->normal_view.data)
            mv->normal_ptr = &mv->normal_view;
    }
    if (cmd->specular_map) {
        const sw_pixels_view *pv = (const sw_pixels_view *)cmd->specular_map;
        mv->specular_view = *pv;
        if (mv->specular_view.width > 0 && mv->specular_view.height > 0 && mv->specular_view.data)
            mv->specular_ptr = &mv->specular_view;
    }
    if (cmd->metallic_roughness_map) {
        const sw_pixels_view *pv = (const sw_pixels_view *)cmd->metallic_roughness_map;
        mv->metallic_roughness_view = *pv;
        if (mv->metallic_roughness_view.width > 0 && mv->metallic_roughness_view.height > 0 &&
            mv->metallic_roughness_view.data)
            mv->metallic_roughness_ptr = &mv->metallic_roughness_view;
    }
    if (cmd->ao_map) {
        const sw_pixels_view *pv = (const sw_pixels_view *)cmd->ao_map;
        mv->ao_view = *pv;
        if (mv->ao_view.width > 0 && mv->ao_view.height > 0 && mv->ao_view.data)
            mv->ao_ptr = &mv->ao_view;
    }
}

/// @brief Backend `submit_draw` op — render one indexed mesh in software.
///
/// End-to-end pipeline:
///   1. Resolve framebuffer + ensure zbuffer is sized.
///   2. Compute world × view × projection matrix.
///   3. Set up texture views from material maps.
///   4. For each triangle: transform vertices to clip space, light
///      (Gouraud), clip against frustum, project to screen, rasterize.
/// Either runs the wireframe path (3 line draws per triangle) or the
/// full `raster_triangle` shader.
/// @brief Select the render output buffers — either the bound render target or
///   the window framebuffer — ensuring their color/depth allocations. Returns 1
///   with the out-params filled, or 0 if a required buffer could not be ensured.
static int sw_resolve_output_target(sw_context_t *ctx,
                                    vgfx_window_t win,
                                    uint8_t **out_pixels,
                                    float **out_zbuf,
                                    int32_t *out_w,
                                    int32_t *out_h,
                                    int32_t *out_stride) {
    if (ctx->render_target) {
        vgfx3d_rendertarget_t *rt = ctx->render_target;
        if (!vgfx3d_rendertarget_ensure_color(rt) || !vgfx3d_rendertarget_ensure_depth(rt))
            return 0;
        *out_pixels = rt->color_buf;
        *out_zbuf = rt->depth_buf;
        *out_w = rt->width;
        *out_h = rt->height;
        *out_stride = rt->stride;
        rt->hdr_color_valid = 0;
        vgfx3d_rendertarget_clear_sync(rt);
    } else {
        vgfx_framebuffer_t fb;
        if (!vgfx_get_framebuffer(win, &fb))
            return 0;
        if (sw_render_scale_active(ctx)) {
            int32_t sw_w;
            int32_t sw_h;
            sw_scaled_dims(ctx, fb.width, fb.height, &sw_w, &sw_h);
            if (!sw_ensure_scaled_color(ctx, sw_w, sw_h) ||
                !sw_ensure_zbuf_capacity(ctx, sw_w, sw_h))
                return 0;
            ctx->scaled_frame_active = 1;
            *out_pixels = (uint8_t *)ctx->scaled_color;
            *out_zbuf = ctx->zbuf;
            *out_w = sw_w;
            *out_h = sw_h;
            *out_stride = sw_w * 4;
            return 1;
        }
        if (!sw_ensure_zbuf_capacity(ctx, fb.width, fb.height))
            return 0;
        *out_pixels = fb.pixels;
        *out_zbuf = ctx->zbuf;
        *out_w = fb.width;
        *out_h = fb.height;
        *out_stride = fb.stride;
    }
    return 1;
}

/// @brief Transform every mesh vertex into the pipeline-vertex scratch buffer:
///   world position, world normal + tangent (via the normal matrix), clip-space
///   position (via MVP), UVs, and color. Computes per-vertex Gouraud lighting for
///   legacy unlit/non-normal-mapped materials; otherwise stores raw albedo so the
///   fragment stage can light per-pixel.
static void sw_transform_vertices_range(sw_context_t *ctx,
                                        const vgfx3d_draw_cmd_t *cmd,
                                        pipe_vert_t *pv,
                                        uint32_t begin,
                                        uint32_t end,
                                        const float *mvp,
                                        const float *normal_matrix,
                                        const vgfx3d_light_params_t *lights,
                                        int32_t light_count,
                                        const float *ambient) {
    for (uint32_t i = begin; i < end; i++) {
        const vgfx3d_vertex_t *src = &cmd->vertices[i];
        pipe_vert_t *dst = &pv[i];
        float pos4[4] = {src->pos[0], src->pos[1], src->pos[2], 1.0f};

        /* World-space */
        float world4[4];
        mat4f_transform4(cmd->model_matrix, pos4, world4);
        dst->world[0] = world4[0];
        dst->world[1] = world4[1];
        dst->world[2] = world4[2];

        float nrm4[4] = {src->normal[0], src->normal[1], src->normal[2], 0.0f};
        float wnrm4[4];
        mat4f_transform4(normal_matrix, nrm4, wnrm4);
        dst->normal[0] = wnrm4[0];
        dst->normal[1] = wnrm4[1];
        dst->normal[2] = wnrm4[2];

        /* Transform tangent to world space (for TBN construction with normal maps) */
        float tan4[4] = {src->tangent[0], src->tangent[1], src->tangent[2], 0.0f};
        float wtan4[4];
        mat4f_transform4(normal_matrix, tan4, wtan4);
        dst->tangent[0] = wtan4[0];
        dst->tangent[1] = wtan4[1];
        dst->tangent[2] = wtan4[2];
        dst->tangent[3] = src->tangent[3];

        /* Clip-space */
        float clip[4];
        mat4f_transform4(mvp, pos4, clip);
        dst->clip[0] = clip[0];
        dst->clip[1] = clip[1];
        dst->clip[2] = clip[2];
        dst->clip[3] = clip[3];

        dst->uv[0] = src->uv[0];
        dst->uv[1] = src->uv[1];
        dst->uv1[0] = src->uv1[0];
        dst->uv1[1] = src->uv1[1];

        /* Vertex color (defaults to white {1,1,1,1} if not set) */
        dst->color[0] = src->color[0];
        dst->color[1] = src->color[1];
        dst->color[2] = src->color[2];
        dst->color[3] = src->color[3];

        /* Per-vertex lighting (Gouraud) — skipped when normal/PBR work is done per-pixel. */
        if (!cmd->unlit && (cmd->workflow == RT_MATERIAL3D_WORKFLOW_PBR || cmd->normal_map)) {
            /* Store raw albedo: vertex_color * diffuse (lighting computed per-pixel) */
            dst->color[0] = cmd->diffuse_color[0] * dst->color[0];
            dst->color[1] = cmd->diffuse_color[1] * dst->color[1];
            dst->color[2] = cmd->diffuse_color[2] * dst->color[2];
            dst->color[3] = dst->color[3] * cmd->diffuse_color[3] * cmd->alpha;
        } else {
            compute_lighting(dst, ctx, cmd, lights, light_count, ambient);
        }
    }
}

static void sw_transform_vertices(sw_context_t *ctx,
                                  const vgfx3d_draw_cmd_t *cmd,
                                  pipe_vert_t *pv,
                                  uint32_t vc,
                                  const float *mvp,
                                  const float *normal_matrix,
                                  const vgfx3d_light_params_t *lights,
                                  int32_t light_count,
                                  const float *ambient) {
    sw_transform_vertices_range(
        ctx, cmd, pv, 0, vc, mvp, normal_matrix, lights, light_count, ambient);
}

/// @brief Emit verbose per-draw diagnostics (command params, MVP rows, first few
///   transformed vertices) to stderr for the first 8 draws when SW debug is on.
static void sw_debug_log_draw(const vgfx3d_draw_cmd_t *cmd,
                              sw_context_t *ctx,
                              const float *mvp,
                              const pipe_vert_t *pv,
                              uint32_t vc,
                              const float *ambient,
                              int32_t light_count) {
    if (!ctx || !sw_debug_enabled() || ctx->debug_draw_count >= 8)
        return;
    uint32_t limit = vc < 4 ? vc : 4;
    fprintf(stderr,
            "[sw3d] draw %d vc=%u ic=%u ambient=(%.3f %.3f %.3f) lights=%d alpha=%.3f "
            "diffuse=(%.3f %.3f %.3f %.3f)\n",
            ctx->debug_draw_count,
            cmd->vertex_count,
            cmd->index_count,
            ambient ? ambient[0] : 0.0f,
            ambient ? ambient[1] : 0.0f,
            ambient ? ambient[2] : 0.0f,
            light_count,
            cmd->alpha,
            cmd->diffuse_color[0],
            cmd->diffuse_color[1],
            cmd->diffuse_color[2],
            cmd->diffuse_color[3]);
    fprintf(stderr,
            "[sw3d] mvp rows=(%.3f %.3f %.3f %.3f) (%.3f %.3f %.3f %.3f) (%.3f %.3f %.3f "
            "%.3f) (%.3f %.3f %.3f %.3f)\n",
            mvp[0],
            mvp[1],
            mvp[2],
            mvp[3],
            mvp[4],
            mvp[5],
            mvp[6],
            mvp[7],
            mvp[8],
            mvp[9],
            mvp[10],
            mvp[11],
            mvp[12],
            mvp[13],
            mvp[14],
            mvp[15]);
    for (uint32_t i = 0; i < limit; i++) {
        fprintf(stderr,
                "[sw3d] v%u pos=(%.3f %.3f %.3f) clip=(%.3f %.3f %.3f %.3f) color=(%.3f "
                "%.3f %.3f %.3f)\n",
                i,
                cmd->vertices[i].pos[0],
                cmd->vertices[i].pos[1],
                cmd->vertices[i].pos[2],
                pv[i].clip[0],
                pv[i].clip[1],
                pv[i].clip[2],
                pv[i].clip[3],
                pv[i].color[0],
                pv[i].color[1],
                pv[i].color[2],
                pv[i].color[3]);
    }
    ctx->debug_draw_count++;
}

/// @brief Perspective-divide one clipped pipeline vertex into a screen-space
///   vertex (viewport-mapped position + perspective-correct attribute carriers).
///   Returns 0 (skip the triangle) when the clip-space w is degenerate.
static int sw_project_clip_to_screen(const pipe_vert_t *p,
                                     float half_w,
                                     float half_h,
                                     screen_vert_t *sv) {
    if (!p || !sv || !isfinite(p->clip[0]) || !isfinite(p->clip[1]) || !isfinite(p->clip[2]) ||
        !isfinite(p->clip[3]) || p->clip[3] <= 1e-7f)
        return 0;
    if (p->clip[2] < -p->clip[3] - 1e-5f || p->clip[2] > p->clip[3] + 1e-5f)
        return 0;
    float iw = 1.0f / p->clip[3];
    sv->sx = (p->clip[0] * iw + 1.0f) * half_w;
    sv->sy = (1.0f - p->clip[1] * iw) * half_h;
    sv->sz = p->clip[2] * iw;
    sv->r = p->color[0];
    sv->g = p->color[1];
    sv->b = p->color[2];
    sv->a = p->color[3];
    sv->inv_w = iw;
    sv->u_over_w = p->uv[0] * iw;
    sv->v_over_w = p->uv[1] * iw;
    sv->u1_over_w = p->uv1[0] * iw;
    sv->v1_over_w = p->uv1[1] * iw;
    sv->wx = p->world[0];
    sv->wy = p->world[1];
    sv->wz = p->world[2];
    sv->nx = p->normal[0];
    sv->ny = p->normal[1];
    sv->nz = p->normal[2];
    sv->tx = p->tangent[0];
    sv->ty = p->tangent[1];
    sv->tz = p->tangent[2];
    sv->tw = p->tangent[3];
    return 1;
}

/// @brief Draw the three edges of a screen-space triangle as wireframe lines,
///   colored from the first vertex's interpolated color.
static void sw_draw_wireframe_tri(
    uint8_t *pixels, int32_t fb_w, int32_t fb_h, int32_t stride, const screen_vert_t *sv) {
    uint8_t wr = (uint8_t)(clamp01f(sv[0].r) * 255.0f);
    uint8_t wg = (uint8_t)(clamp01f(sv[0].g) * 255.0f);
    uint8_t wb = (uint8_t)(clamp01f(sv[0].b) * 255.0f);
    draw_line(pixels,
              fb_w,
              fb_h,
              stride,
              (int)sv[0].sx,
              (int)sv[0].sy,
              (int)sv[1].sx,
              (int)sv[1].sy,
              wr,
              wg,
              wb);
    draw_line(pixels,
              fb_w,
              fb_h,
              stride,
              (int)sv[1].sx,
              (int)sv[1].sy,
              (int)sv[2].sx,
              (int)sv[2].sy,
              wr,
              wg,
              wb);
    draw_line(pixels,
              fb_w,
              fb_h,
              stride,
              (int)sv[2].sx,
              (int)sv[2].sy,
              (int)sv[0].sx,
              (int)sv[0].sy,
              wr,
              wg,
              wb);
}

typedef struct {
    screen_vert_t sv[3];
} sw_screen_tri_t;

typedef struct {
    sw_screen_tri_t *items;
    uint32_t count;
    uint32_t capacity;
} sw_screen_tri_list_t;

typedef struct {
    uint32_t *indices;
    uint32_t count;
    uint32_t capacity;
} sw_tile_bin_t;

typedef struct {
    sw_tile_bin_t *bins;
    int32_t *active_indices;
    int32_t cols;
    int32_t rows;
    int32_t count;
    int32_t active_count;
    int32_t active_capacity;
} sw_tile_grid_t;

typedef struct {
    sw_context_t *ctx;
    const vgfx3d_draw_cmd_t *cmd;
    pipe_vert_t *pv;
    uint32_t begin;
    uint32_t end;
    const float *mvp;
    const float *normal_matrix;
    const vgfx3d_light_params_t *lights;
    int32_t light_count;
    const float *ambient;
} sw_transform_task_t;

typedef struct {
    const vgfx3d_draw_cmd_t *cmd;
    const pipe_vert_t *pv;
    uint32_t vertex_count;
    uint32_t tri_begin;
    uint32_t tri_end;
    float half_w;
    float half_h;
    sw_screen_tri_list_t out;
    int32_t *failed;
} sw_clip_task_t;

typedef struct {
    const sw_tile_grid_t *grid;
    const sw_screen_tri_list_t *tris;
    int32_t *next_tile;
    uint8_t *pixels;
    float *zbuf;
    int32_t fb_w;
    int32_t fb_h;
    int32_t stride;
    const sw_pixels_view *tex;
    const sw_pixels_view *emissive_tex;
    const float *emissive_color;
    const sw_pixels_view *normal_map;
    const sw_pixels_view *specular_map;
    const sw_pixels_view *metallic_roughness_map;
    const sw_pixels_view *ao_map;
    const vgfx3d_draw_cmd_t *cmd;
    const vgfx3d_light_params_t *lights;
    int32_t light_count;
    const float *ambient;
    int8_t backface_cull;
    sw_context_t *fog_ctx;
} sw_raster_task_t;

static void sw_screen_tri_list_free(sw_screen_tri_list_t *list) {
    if (!list)
        return;
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static int sw_screen_tri_list_reserve(sw_screen_tri_list_t *list, uint32_t needed) {
    uint32_t new_capacity;
    sw_screen_tri_t *new_items;
    if (!list)
        return 0;
    if (needed <= list->capacity)
        return 1;
    new_capacity = list->capacity ? list->capacity : 64u;
    while (new_capacity < needed) {
        if (new_capacity > UINT32_MAX / 2u) {
            new_capacity = needed;
            break;
        }
        new_capacity *= 2u;
    }
    if ((size_t)new_capacity > SIZE_MAX / sizeof(sw_screen_tri_t))
        return 0;
    new_items = (sw_screen_tri_t *)realloc(list->items, (size_t)new_capacity * sizeof(*new_items));
    if (!new_items)
        return 0;
    list->items = new_items;
    list->capacity = new_capacity;
    return 1;
}

static int sw_screen_tri_list_push(sw_screen_tri_list_t *list, const screen_vert_t sv[3]) {
    if (!list || !sv)
        return 0;
    if (list->count == UINT32_MAX)
        return 0;
    if (!sw_screen_tri_list_reserve(list, list->count + 1u))
        return 0;
    memcpy(list->items[list->count].sv, sv, 3u * sizeof(screen_vert_t));
    list->count++;
    return 1;
}

static void sw_tile_grid_free(sw_tile_grid_t *grid) {
    if (!grid)
        return;
    if (grid->bins) {
        for (int32_t i = 0; i < grid->count; i++)
            free(grid->bins[i].indices);
    }
    free(grid->active_indices);
    free(grid->bins);
    grid->bins = NULL;
    grid->active_indices = NULL;
    grid->cols = 0;
    grid->rows = 0;
    grid->count = 0;
    grid->active_count = 0;
    grid->active_capacity = 0;
}

static int sw_tile_grid_init(sw_tile_grid_t *grid, int32_t width, int32_t height) {
    int32_t cols;
    int32_t rows;
    if (!grid || width <= 0 || height <= 0)
        return 0;
    memset(grid, 0, sizeof(*grid));
    cols = ((width - 1) / SW_TILE_SIZE) + 1;
    rows = ((height - 1) / SW_TILE_SIZE) + 1;
    if (cols <= 0 || rows <= 0 || cols > INT32_MAX / rows)
        return 0;
    grid->count = cols * rows;
    grid->cols = cols;
    grid->rows = rows;
    grid->bins = (sw_tile_bin_t *)calloc((size_t)grid->count, sizeof(*grid->bins));
    return grid->bins ? 1 : 0;
}

static int sw_tile_grid_mark_active(sw_tile_grid_t *grid, int32_t tile_index) {
    int32_t new_capacity;
    int32_t *new_indices;
    if (!grid || tile_index < 0 || tile_index >= grid->count)
        return 0;
    if (grid->active_count < grid->active_capacity) {
        grid->active_indices[grid->active_count++] = tile_index;
        return 1;
    }
    new_capacity = grid->active_capacity ? grid->active_capacity * 2 : 32;
    if (new_capacity <= grid->active_capacity)
        new_capacity = grid->active_count + 1;
    if ((size_t)new_capacity > SIZE_MAX / sizeof(int32_t))
        return 0;
    new_indices =
        (int32_t *)realloc(grid->active_indices, (size_t)new_capacity * sizeof(*new_indices));
    if (!new_indices)
        return 0;
    grid->active_indices = new_indices;
    grid->active_capacity = new_capacity;
    grid->active_indices[grid->active_count++] = tile_index;
    return 1;
}

static int sw_tile_bin_append(sw_tile_bin_t *bin, uint32_t tri_index) {
    uint32_t new_capacity;
    uint32_t *new_indices;
    if (!bin)
        return 0;
    if (bin->count == UINT32_MAX)
        return 0;
    if (bin->count < bin->capacity) {
        bin->indices[bin->count++] = tri_index;
        return 1;
    }
    new_capacity = bin->capacity ? bin->capacity * 2u : 16u;
    if (new_capacity <= bin->capacity)
        new_capacity = bin->count + 1u;
    if ((size_t)new_capacity > SIZE_MAX / sizeof(uint32_t))
        return 0;
    new_indices = (uint32_t *)realloc(bin->indices, (size_t)new_capacity * sizeof(*new_indices));
    if (!new_indices)
        return 0;
    bin->indices = new_indices;
    bin->capacity = new_capacity;
    bin->indices[bin->count++] = tri_index;
    return 1;
}

static void sw_transform_task_run(void *arg) {
    sw_transform_task_t *task = (sw_transform_task_t *)arg;
    if (!task)
        return;
    sw_transform_vertices_range(task->ctx,
                                task->cmd,
                                task->pv,
                                task->begin,
                                task->end,
                                task->mvp,
                                task->normal_matrix,
                                task->lights,
                                task->light_count,
                                task->ambient);
}

static int sw_parallel_transform_vertices(sw_context_t *ctx,
                                          const vgfx3d_draw_cmd_t *cmd,
                                          pipe_vert_t *pv,
                                          uint32_t vc,
                                          const float *mvp,
                                          const float *normal_matrix,
                                          const vgfx3d_light_params_t *lights,
                                          int32_t light_count,
                                          const float *ambient) {
    sw_transform_task_t tasks[SW_MAX_TASKS];
    int32_t task_count;
    int32_t submitted = 0;
    if (!ctx || !ctx->worker_pool || ctx->worker_count <= 1 || vc < 2)
        return 0;
    task_count =
        (int32_t)(ctx->worker_count + 1 < (int64_t)vc ? ctx->worker_count + 1 : (int64_t)vc);
    for (int32_t i = 0; i < task_count; i++) {
        uint32_t begin = (uint32_t)(((uint64_t)vc * (uint64_t)i) / (uint64_t)task_count);
        uint32_t end = (uint32_t)(((uint64_t)vc * (uint64_t)(i + 1)) / (uint64_t)task_count);
        tasks[i].ctx = ctx;
        tasks[i].cmd = cmd;
        tasks[i].pv = pv;
        tasks[i].begin = begin;
        tasks[i].end = end;
        tasks[i].mvp = mvp;
        tasks[i].normal_matrix = normal_matrix;
        tasks[i].lights = lights;
        tasks[i].light_count = light_count;
        tasks[i].ambient = ambient;
    }
    for (int32_t i = 1; i < task_count; i++) {
        if (!rt_threadpool_submit_fn(ctx->worker_pool, sw_transform_task_run, &tasks[i]))
            break;
        submitted++;
    }
    for (int32_t i = submitted + 1; i < task_count; i++)
        sw_transform_task_run(&tasks[i]);
    sw_transform_task_run(&tasks[0]);
    if (submitted > 0)
        rt_threadpool_wait(ctx->worker_pool);
    return 1;
}

static int sw_collect_screen_triangles_range(const vgfx3d_draw_cmd_t *cmd,
                                             const pipe_vert_t *pv,
                                             uint32_t vc,
                                             uint32_t tri_begin,
                                             uint32_t tri_end,
                                             float half_w,
                                             float half_h,
                                             sw_screen_tri_list_t *out) {
    pipe_vert_t clipped[MAX_CLIP_VERTS];
    for (uint32_t tri_index = tri_begin; tri_index < tri_end; tri_index++) {
        uint32_t i = tri_index * 3u;
        uint32_t i0 = cmd->indices[i], i1 = cmd->indices[i + 1], i2 = cmd->indices[i + 2];
        if (i0 >= vc || i1 >= vc || i2 >= vc)
            continue;

        pipe_vert_t tri[3] = {pv[i0], pv[i1], pv[i2]};
        int clip_count = clip_triangle(tri, clipped);
        if (clip_count < 3)
            continue;

        for (int t = 1; t < clip_count - 1; t++) {
            screen_vert_t sv[3];
            const pipe_vert_t *fan[3] = {&clipped[0], &clipped[t], &clipped[t + 1]};
            int ok = 1;
            for (int vi = 0; vi < 3; vi++) {
                if (!sw_project_clip_to_screen(fan[vi], half_w, half_h, &sv[vi])) {
                    ok = 0;
                    break;
                }
            }
            if (ok && !sw_screen_tri_list_push(out, sv))
                return 0;
        }
    }
    return 1;
}

static void sw_clip_task_run(void *arg) {
    sw_clip_task_t *task = (sw_clip_task_t *)arg;
    if (!task)
        return;
    if (!sw_collect_screen_triangles_range(task->cmd,
                                           task->pv,
                                           task->vertex_count,
                                           task->tri_begin,
                                           task->tri_end,
                                           task->half_w,
                                           task->half_h,
                                           &task->out)) {
        rt_atomic_store_i32(task->failed, 1, __ATOMIC_RELAXED);
    }
}

static int sw_collect_screen_triangles_parallel(sw_context_t *ctx,
                                                const vgfx3d_draw_cmd_t *cmd,
                                                const pipe_vert_t *pv,
                                                uint32_t vc,
                                                uint32_t index_count,
                                                float half_w,
                                                float half_h,
                                                sw_screen_tri_list_t *out) {
    sw_clip_task_t tasks[SW_MAX_TASKS];
    uint32_t tri_count;
    int32_t task_count;
    int32_t submitted = 0;
    int32_t failed = 0;
    if (!ctx || !ctx->worker_pool || ctx->worker_count <= 1 || !out)
        return 0;
    tri_count = index_count / 3u;
    if (tri_count == 0)
        return 1;
    task_count = (int32_t)(ctx->worker_count + 1 < (int64_t)tri_count ? ctx->worker_count + 1
                                                                      : (int64_t)tri_count);
    memset(tasks, 0, sizeof(tasks));
    for (int32_t i = 0; i < task_count; i++) {
        uint32_t begin = (uint32_t)(((uint64_t)tri_count * (uint64_t)i) / (uint64_t)task_count);
        uint32_t end = (uint32_t)(((uint64_t)tri_count * (uint64_t)(i + 1)) / (uint64_t)task_count);
        tasks[i].cmd = cmd;
        tasks[i].pv = pv;
        tasks[i].vertex_count = vc;
        tasks[i].tri_begin = begin;
        tasks[i].tri_end = end;
        tasks[i].half_w = half_w;
        tasks[i].half_h = half_h;
        tasks[i].failed = &failed;
    }
    for (int32_t i = 1; i < task_count; i++) {
        if (!rt_threadpool_submit_fn(ctx->worker_pool, sw_clip_task_run, &tasks[i]))
            break;
        submitted++;
    }
    for (int32_t i = submitted + 1; i < task_count; i++)
        sw_clip_task_run(&tasks[i]);
    sw_clip_task_run(&tasks[0]);
    if (submitted > 0)
        rt_threadpool_wait(ctx->worker_pool);
    if (rt_atomic_load_i32(&failed, __ATOMIC_RELAXED)) {
        for (int32_t i = 0; i < task_count; i++)
            sw_screen_tri_list_free(&tasks[i].out);
        return 0;
    }

    uint32_t total = 0;
    for (int32_t i = 0; i < task_count; i++) {
        if (UINT32_MAX - total < tasks[i].out.count) {
            for (int32_t j = 0; j < task_count; j++)
                sw_screen_tri_list_free(&tasks[j].out);
            return 0;
        }
        total += tasks[i].out.count;
    }
    if (!sw_screen_tri_list_reserve(out, total)) {
        for (int32_t i = 0; i < task_count; i++)
            sw_screen_tri_list_free(&tasks[i].out);
        return 0;
    }
    for (int32_t i = 0; i < task_count; i++) {
        if (tasks[i].out.count > 0) {
            memcpy(&out->items[out->count],
                   tasks[i].out.items,
                   (size_t)tasks[i].out.count * sizeof(sw_screen_tri_t));
            out->count += tasks[i].out.count;
        }
        sw_screen_tri_list_free(&tasks[i].out);
    }
    return 1;
}

static int sw_bin_screen_triangles(sw_tile_grid_t *grid,
                                   const sw_screen_tri_list_t *tris,
                                   int32_t fb_w,
                                   int32_t fb_h,
                                   int8_t backface_cull) {
    if (!grid || !grid->bins || !tris)
        return 0;
    for (uint32_t tri_index = 0; tri_index < tris->count; tri_index++) {
        const sw_screen_tri_t *tri = &tris->items[tri_index];
        sw_raster_geom geom;
        if (!sw_raster_prepare(
                &tri->sv[0], &tri->sv[1], &tri->sv[2], fb_w, fb_h, backface_cull, 0, NULL, &geom))
            continue;
        int32_t tx0 = geom.min_x / SW_TILE_SIZE;
        int32_t tx1 = geom.max_x / SW_TILE_SIZE;
        int32_t ty0 = geom.min_y / SW_TILE_SIZE;
        int32_t ty1 = geom.max_y / SW_TILE_SIZE;
        if (tx0 < 0)
            tx0 = 0;
        if (ty0 < 0)
            ty0 = 0;
        if (tx1 >= grid->cols)
            tx1 = grid->cols - 1;
        if (ty1 >= grid->rows)
            ty1 = grid->rows - 1;
        for (int32_t ty = ty0; ty <= ty1; ty++) {
            for (int32_t tx = tx0; tx <= tx1; tx++) {
                sw_tile_bin_t *bin = &grid->bins[ty * grid->cols + tx];
                uint32_t prior_count = bin->count;
                if (!sw_tile_bin_append(bin, tri_index))
                    return 0;
                if (prior_count == 0 && !sw_tile_grid_mark_active(grid, ty * grid->cols + tx))
                    return 0;
            }
        }
    }
    return 1;
}

static void sw_raster_task_run(void *arg) {
    sw_raster_task_t *task = (sw_raster_task_t *)arg;
    if (!task || !task->grid || !task->tris || !task->next_tile)
        return;
    for (;;) {
        int32_t tile_index = rt_atomic_fetch_add_i32(task->next_tile, 1, __ATOMIC_RELAXED);
        if (tile_index >= task->grid->active_count)
            break;
        tile_index = task->grid->active_indices[tile_index];
        int32_t tx = tile_index % task->grid->cols;
        int32_t ty = tile_index / task->grid->cols;
        int32_t min_x = tx * SW_TILE_SIZE;
        int32_t min_y = ty * SW_TILE_SIZE;
        int32_t max_x = min_x + SW_TILE_SIZE - 1;
        int32_t max_y = min_y + SW_TILE_SIZE - 1;
        const sw_tile_bin_t *bin = &task->grid->bins[tile_index];
        if (max_x >= task->fb_w)
            max_x = task->fb_w - 1;
        if (max_y >= task->fb_h)
            max_y = task->fb_h - 1;
        for (uint32_t i = 0; i < bin->count; i++) {
            const sw_screen_tri_t *tri = &task->tris->items[bin->indices[i]];
            raster_triangle_scissored(task->pixels,
                                      task->zbuf,
                                      task->fb_w,
                                      task->fb_h,
                                      task->stride,
                                      &tri->sv[0],
                                      &tri->sv[1],
                                      &tri->sv[2],
                                      task->tex,
                                      task->emissive_tex,
                                      task->emissive_color,
                                      task->normal_map,
                                      task->specular_map,
                                      task->metallic_roughness_map,
                                      task->ao_map,
                                      task->cmd,
                                      task->lights,
                                      task->light_count,
                                      task->ambient,
                                      task->backface_cull,
                                      task->fog_ctx,
                                      min_x,
                                      min_y,
                                      max_x,
                                      max_y);
        }
    }
}

static int sw_raster_tiles_parallel(sw_context_t *ctx,
                                    const sw_tile_grid_t *grid,
                                    const sw_screen_tri_list_t *tris,
                                    uint8_t *pixels,
                                    float *zbuf,
                                    int32_t fb_w,
                                    int32_t fb_h,
                                    int32_t stride,
                                    const sw_material_views *mv,
                                    const vgfx3d_draw_cmd_t *cmd,
                                    const vgfx3d_light_params_t *lights,
                                    int32_t light_count,
                                    const float *ambient,
                                    int8_t backface_cull) {
    sw_raster_task_t tasks[SW_MAX_TASKS];
    int32_t next_tile = 0;
    int32_t task_count;
    int32_t submitted = 0;
    if (!ctx || !ctx->worker_pool || !grid || grid->active_count <= 0 || !tris || !mv)
        return 0;
    task_count = (int32_t)(ctx->worker_count + 1 < (int64_t)grid->active_count
                               ? ctx->worker_count + 1
                               : (int64_t)grid->active_count);
    for (int32_t i = 0; i < task_count; i++) {
        tasks[i].grid = grid;
        tasks[i].tris = tris;
        tasks[i].next_tile = &next_tile;
        tasks[i].pixels = pixels;
        tasks[i].zbuf = zbuf;
        tasks[i].fb_w = fb_w;
        tasks[i].fb_h = fb_h;
        tasks[i].stride = stride;
        tasks[i].tex = mv->tex_ptr;
        tasks[i].emissive_tex = mv->emissive_ptr;
        tasks[i].emissive_color = cmd->emissive_color;
        tasks[i].normal_map = mv->normal_ptr;
        tasks[i].specular_map = mv->specular_ptr;
        tasks[i].metallic_roughness_map = mv->metallic_roughness_ptr;
        tasks[i].ao_map = mv->ao_ptr;
        tasks[i].cmd = cmd;
        tasks[i].lights = lights;
        tasks[i].light_count = light_count;
        tasks[i].ambient = ambient;
        tasks[i].backface_cull = backface_cull;
        tasks[i].fog_ctx = ctx;
    }
    for (int32_t i = 1; i < task_count; i++) {
        if (!rt_threadpool_submit_fn(ctx->worker_pool, sw_raster_task_run, &tasks[i]))
            break;
        submitted++;
    }
    for (int32_t i = submitted + 1; i < task_count; i++)
        sw_raster_task_run(&tasks[i]);
    sw_raster_task_run(&tasks[0]);
    if (submitted > 0)
        rt_threadpool_wait(ctx->worker_pool);
    return 1;
}

static int sw_submit_draw_parallel(void *ctx_ptr,
                                   vgfx_window_t win,
                                   const vgfx3d_draw_cmd_t *cmd,
                                   const vgfx3d_light_params_t *lights,
                                   int32_t light_count,
                                   const float *ambient,
                                   int8_t wireframe,
                                   int8_t backface_cull) {
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    static const float zero_ambient[3] = {0.0f, 0.0f, 0.0f};
    uint32_t index_count;
    uint8_t *out_pixels;
    float *out_zbuf;
    int32_t out_w, out_h, out_stride;
    float mvp[16];
    float normal_matrix[16];
    sw_material_views mv;
    sw_screen_tri_list_t tris = {0};
    sw_tile_grid_t grid = {0};
    int ok = 0;

    if (!ctx || !ctx->worker_pool || ctx->worker_count <= 1 || wireframe || sw_debug_enabled())
        return 0;
    if (rt_threadpool_current_worker_pool() == ctx->worker_pool)
        return 0;
    if (!cmd || !cmd->vertices || !cmd->indices || cmd->vertex_count == 0 || cmd->index_count == 0)
        return 0;
    index_count = vgfx3d_draw_cmd_validated_index_count(cmd);
    if (index_count == 0)
        return 0;
    if (index_count < SW_PARALLEL_MIN_INDICES)
        return 0;
    if (!ambient)
        ambient = zero_ambient;
    if (!lights || light_count < 0)
        light_count = 0;
    if (!sw_resolve_output_target(ctx, win, &out_pixels, &out_zbuf, &out_w, &out_h, &out_stride))
        return 0;

    mat4f_mul(ctx->vp, cmd->model_matrix, mvp);
    vgfx3d_compute_normal_matrix4(cmd->model_matrix, normal_matrix);
    sw_resolve_material_views(cmd, &mv);

    uint32_t vc = cmd->vertex_count;
    pipe_vert_t *pv = sw_acquire_pipe_vertices(ctx, vc);
    if (!pv)
        return 0;
    if (!sw_parallel_transform_vertices(
            ctx, cmd, pv, vc, mvp, normal_matrix, lights, light_count, ambient))
        return 0;
    if (!sw_collect_screen_triangles_parallel(
            ctx, cmd, pv, vc, index_count, (float)out_w * 0.5f, (float)out_h * 0.5f, &tris))
        goto cleanup;
    if (tris.count == 0) {
        ok = 1;
        goto cleanup;
    }
    if (!sw_tile_grid_init(&grid, out_w, out_h))
        goto cleanup;
    if (!sw_bin_screen_triangles(&grid, &tris, out_w, out_h, backface_cull))
        goto cleanup;
    if (grid.active_count == 0) {
        ok = 1;
        goto cleanup;
    }
    if (!sw_raster_tiles_parallel(ctx,
                                  &grid,
                                  &tris,
                                  out_pixels,
                                  out_zbuf,
                                  out_w,
                                  out_h,
                                  out_stride,
                                  &mv,
                                  cmd,
                                  lights,
                                  light_count,
                                  ambient,
                                  backface_cull))
        goto cleanup;
    ok = 1;

cleanup:
    sw_tile_grid_free(&grid);
    sw_screen_tri_list_free(&tris);
    return ok;
}

/// @brief Serial software draw setup shared by single-draw and instanced paths.
/// @details Resolving the output framebuffer and material texture views is invariant across
///   instances in a batch. Keeping those views in one prepared struct lets the instanced
///   software path avoid revalidating the same render target and material maps for every
///   instance while preserving the existing per-instance transform, clip, and raster code.
typedef struct {
    uint8_t *pixels;
    float *zbuf;
    int32_t width;
    int32_t height;
    int32_t stride;
    float half_width;
    float half_height;
    sw_material_views material_views;
} sw_serial_draw_setup_t;

/// @brief Prepare invariant framebuffer and material state for a serial software draw.
/// @param ctx Software backend context.
/// @param win Window handle used when no render target is bound.
/// @param cmd Draw command whose material maps will be resolved.
/// @param setup Receives output target pointers, dimensions, and material views.
/// @return 1 on success, 0 when the output target cannot be resolved.
static int sw_prepare_serial_draw_setup(sw_context_t *ctx,
                                        vgfx_window_t win,
                                        const vgfx3d_draw_cmd_t *cmd,
                                        sw_serial_draw_setup_t *setup) {
    if (!ctx || !cmd || !setup)
        return 0;
    memset(setup, 0, sizeof(*setup));
    if (!sw_resolve_output_target(
            ctx, win, &setup->pixels, &setup->zbuf, &setup->width, &setup->height, &setup->stride))
        return 0;
    setup->half_width = (float)setup->width * 0.5f;
    setup->half_height = (float)setup->height * 0.5f;
    sw_resolve_material_views(cmd, &setup->material_views);
    return 1;
}

/// @brief Execute the serial software rasterizer using already-prepared invariant state.
/// @details The caller supplies output buffers and material views that are shared across one
///   or more commands. The command's model matrix may vary between calls, so this helper still
///   recomputes MVP/normal matrices and transforms vertices for each instance.
/// @param ctx Software backend context.
/// @param cmd Draw command to render.
/// @param setup Prepared framebuffer and material view state.
/// @param lights Active light array, or NULL for no direct lights.
/// @param light_count Number of valid entries in @p lights.
/// @param ambient Ambient RGB triplet.
/// @param wireframe Non-zero to draw triangle edges only.
/// @param backface_cull Non-zero to cull back-facing triangles.
static void sw_submit_draw_serial_prepared(sw_context_t *ctx,
                                           const vgfx3d_draw_cmd_t *cmd,
                                           const sw_serial_draw_setup_t *setup,
                                           const vgfx3d_light_params_t *lights,
                                           int32_t light_count,
                                           const float *ambient,
                                           int8_t wireframe,
                                           int8_t backface_cull) {
    uint32_t index_count;
    float mvp[16];
    float normal_matrix[16];
    uint32_t vc;
    pipe_vert_t *pv;
    pipe_vert_t clipped[MAX_CLIP_VERTS];

    if (!ctx || !cmd || !setup || !setup->pixels || !setup->zbuf || !cmd->vertices ||
        !cmd->indices || cmd->vertex_count == 0 || cmd->index_count == 0)
        return;
    index_count = vgfx3d_draw_cmd_validated_index_count(cmd);
    if (index_count == 0)
        return;

    mat4f_mul(ctx->vp, cmd->model_matrix, mvp);
    vgfx3d_compute_normal_matrix4(cmd->model_matrix, normal_matrix);

    vc = cmd->vertex_count;
    pv = sw_acquire_pipe_vertices(ctx, vc);
    if (!pv)
        return;
    sw_transform_vertices(ctx, cmd, pv, vc, mvp, normal_matrix, lights, light_count, ambient);
    sw_debug_log_draw(cmd, ctx, mvp, pv, vc, ambient, light_count);

    for (uint32_t i = 0; i + 2 < index_count; i += 3) {
        uint32_t i0 = cmd->indices[i], i1 = cmd->indices[i + 1], i2 = cmd->indices[i + 2];
        if (i0 >= vc || i1 >= vc || i2 >= vc)
            continue;

        pipe_vert_t tri[3] = {pv[i0], pv[i1], pv[i2]};
        int clip_count = clip_triangle(tri, clipped);
        if (clip_count < 3)
            continue;

        for (int t = 1; t < clip_count - 1; t++) {
            screen_vert_t sv[3];
            const pipe_vert_t *fan[3] = {&clipped[0], &clipped[t], &clipped[t + 1]};
            int ok = 1;
            for (int vi = 0; vi < 3; vi++) {
                if (!sw_project_clip_to_screen(
                        fan[vi], setup->half_width, setup->half_height, &sv[vi])) {
                    ok = 0;
                    break;
                }
            }
            if (!ok)
                continue;

            if (wireframe) {
                sw_draw_wireframe_tri(
                    setup->pixels, setup->width, setup->height, setup->stride, sv);
            } else {
                raster_triangle(setup->pixels,
                                setup->zbuf,
                                setup->width,
                                setup->height,
                                setup->stride,
                                &sv[0],
                                &sv[1],
                                &sv[2],
                                setup->material_views.tex_ptr,
                                setup->material_views.emissive_ptr,
                                cmd->emissive_color,
                                setup->material_views.normal_ptr,
                                setup->material_views.specular_ptr,
                                setup->material_views.metallic_roughness_ptr,
                                setup->material_views.ao_ptr,
                                cmd,
                                lights,
                                light_count,
                                ambient,
                                backface_cull,
                                ctx);
            }
        }
    }
}

/// @brief Backend `submit_draw` op for the software rasterizer. Resolves output
///   buffers, transforms vertices, then clips and rasterizes (or wireframes) each
///   triangle of the mesh.
static void sw_submit_draw_serial(void *ctx_ptr,
                                  vgfx_window_t win,
                                  const vgfx3d_draw_cmd_t *cmd,
                                  const vgfx3d_light_params_t *lights,
                                  int32_t light_count,
                                  const float *ambient,
                                  int8_t wireframe,
                                  int8_t backface_cull) {
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    static const float zero_ambient[3] = {0.0f, 0.0f, 0.0f};
    uint32_t index_count;
    sw_serial_draw_setup_t setup;
    if (!ctx || !cmd || !cmd->vertices || !cmd->indices || cmd->vertex_count == 0 ||
        cmd->index_count == 0)
        return;
    index_count = vgfx3d_draw_cmd_validated_index_count(cmd);
    if (index_count == 0)
        return;
    if (!ambient)
        ambient = zero_ambient;
    if (!lights || light_count < 0)
        light_count = 0;

    if (!sw_prepare_serial_draw_setup(ctx, win, cmd, &setup))
        return;
    sw_submit_draw_serial_prepared(
        ctx, cmd, &setup, lights, light_count, ambient, wireframe, backface_cull);
}

static void sw_submit_draw(void *ctx_ptr,
                           vgfx_window_t win,
                           const vgfx3d_draw_cmd_t *cmd,
                           const vgfx3d_light_params_t *lights,
                           int32_t light_count,
                           const float *ambient,
                           int8_t wireframe,
                           int8_t backface_cull) {
    if (sw_submit_draw_parallel(
            ctx_ptr, win, cmd, lights, light_count, ambient, wireframe, backface_cull))
        return;
    sw_submit_draw_serial(
        ctx_ptr, win, cmd, lights, light_count, ambient, wireframe, backface_cull);
}

/// @brief Backend `submit_draw_instanced` op — rasterize one shared mesh N times.
/// @details Iterates instances in the backend inner loop with a stack-local
///   command whose model matrix is swapped per instance. Removes the deferred
///   per-instance queue overhead (and its fallback cap) for opaque instanced
///   batches on the software path; material/texture views and the shared
///   geometry stay identical across instances.
static void sw_submit_draw_instanced(void *ctx_ptr,
                                     vgfx_window_t win,
                                     const vgfx3d_draw_cmd_t *cmd,
                                     const float *instance_matrices,
                                     int32_t instance_count,
                                     const vgfx3d_light_params_t *lights,
                                     int32_t light_count,
                                     const float *ambient,
                                     int8_t wireframe,
                                     int8_t backface_cull) {
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    static const float zero_ambient[3] = {0.0f, 0.0f, 0.0f};
    sw_serial_draw_setup_t setup;
    if (!cmd || !instance_matrices || instance_count <= 0)
        return;
    if (!ctx)
        return;
    if (!ambient)
        ambient = zero_ambient;
    if (!lights || light_count < 0)
        light_count = 0;

    /* Keep the existing parallel path for very large meshes, where worker
     * throughput matters more than per-instance setup overhead. Small instanced
     * meshes are the common particle/foliage case and benefit from hoisting the
     * invariant serial draw setup once for the whole batch. */
    if (ctx->worker_pool && ctx->worker_count > 1 &&
        vgfx3d_draw_cmd_validated_index_count(cmd) >= SW_PARALLEL_MIN_INDICES) {
        vgfx3d_draw_cmd_t inst = *cmd;
        for (int32_t i = 0; i < instance_count; i++) {
            memcpy(
                inst.model_matrix, &instance_matrices[(size_t)i * 16u], sizeof(inst.model_matrix));
            sw_submit_draw(
                ctx_ptr, win, &inst, lights, light_count, ambient, wireframe, backface_cull);
        }
        return;
    }
    if (!sw_prepare_serial_draw_setup(ctx, win, cmd, &setup))
        return;
    vgfx3d_draw_cmd_t inst = *cmd;
    for (int32_t i = 0; i < instance_count; i++) {
        memcpy(inst.model_matrix, &instance_matrices[(size_t)i * 16u], sizeof(inst.model_matrix));
        sw_submit_draw_serial_prepared(
            ctx, &inst, &setup, lights, light_count, ambient, wireframe, backface_cull);
    }
}

/// @brief Backend `end_frame` op — upscale scaled frames; otherwise a no-op
///   (frames write directly to fb).
static void sw_end_frame(void *ctx_ptr) {
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    if (!ctx || !ctx->scaled_frame_active)
        return;
    ctx->scaled_frame_active = 0;
    if (ctx->render_target)
        return;
    vgfx_framebuffer_t fb;
    if (!vgfx_get_framebuffer(ctx->win, &fb))
        return;
    sw_upscale_to_framebuffer(ctx, &fb);
}

/// @brief Backend hook: set the window-backed rasterization scale ([0.25, 1]).
static int8_t sw_set_render_scale(void *ctx_ptr, float scale) {
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    if (!ctx)
        return 0;
    if (!isfinite(scale) || scale > 1.0f)
        scale = 1.0f;
    if (scale < 0.25f)
        scale = 0.25f;
    ctx->render_scale = scale;
    return 1;
}

/// @brief Backend `resize` op — re-allocate the depth buffer to the new size.
static void sw_resize(void *ctx_ptr, int32_t w, int32_t h) {
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    if (!ctx || ctx->render_target)
        return;
    sw_ensure_zbuf_capacity(ctx, w, h);
}

/// @brief Backend `set_render_target` op — bind / unbind a host-memory RTT.
///
/// Software backend RTT is just "render directly to the user-supplied
/// host buffer instead of the swapchain". No GPU resources to manage.
/// NULL `rt` reverts to swapchain rendering.
static void sw_set_render_target(void *ctx_ptr, vgfx3d_rendertarget_t *rt) {
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    if (!ctx)
        return;
    if (rt) {
        if (!vgfx3d_rendertarget_ensure_color(rt) || !vgfx3d_rendertarget_ensure_depth(rt)) {
            ctx->render_target = NULL;
            return;
        }
    }
    ctx->render_target = rt;
}
