//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/backend/vgfx3d_backend_sw_shadow.inc  (#included from
// vgfx3d_backend_sw.c; not a standalone translation unit) Purpose: Software backend wireframe lines
// and shadow-map depth rasterization
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
static float shadow_advance_edge_value(float value, float delta, int steps) {
    for (int i = 0; i < steps; i++)
        value += delta;
    return value;
}

static void shadow_raster_tri_scissored(float *depth,
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
                                        const sw_pixels_view *alpha_tex,
                                        int32_t clip_min_x,
                                        int32_t clip_min_y,
                                        int32_t clip_max_x,
                                        int32_t clip_max_y) {
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
    min_x = min_x > clip_min_x ? min_x : clip_min_x;
    max_x = max_x < clip_max_x ? max_x : clip_max_x;
    min_y = min_y > clip_min_y ? min_y : clip_min_y;
    max_y = max_y < clip_max_y ? max_y : clip_max_y;
    if (min_x > max_x || min_y > max_y)
        return;

    float e12_dx = sx[2] - sx[1], e12_dy = sy[2] - sy[1];
    float e20_dx = sx[0] - sx[2], e20_dy = sy[0] - sy[2];
    float e01_dx = sx[1] - sx[0], e01_dy = sy[1] - sy[0];
    int full_min_x = (int)fmaxf(floorf(fminf(fminf(sx[0], sx[1]), sx[2])), 0.0f);
    int full_min_y = (int)fmaxf(floorf(fminf(fminf(sy[0], sy[1]), sy[2])), 0.0f);
    float px0 = (float)full_min_x + 0.5f, py0 = (float)full_min_y + 0.5f;
    float row_w0 = e12_dx * (py0 - sy[1]) - e12_dy * (px0 - sx[1]);
    float row_w1 = e20_dx * (py0 - sy[2]) - e20_dy * (px0 - sx[2]);
    float row_w2 = e01_dx * (py0 - sy[0]) - e01_dy * (px0 - sx[0]);
    const float edge_epsilon = 0.0f;

    row_w0 = shadow_advance_edge_value(row_w0, e12_dx, min_y - full_min_y);
    row_w1 = shadow_advance_edge_value(row_w1, e20_dx, min_y - full_min_y);
    row_w2 = shadow_advance_edge_value(row_w2, e01_dx, min_y - full_min_y);
    for (int y = min_y; y <= max_y; y++) {
        float w0 = shadow_advance_edge_value(row_w0, -e12_dy, min_x - full_min_x);
        float w1 = shadow_advance_edge_value(row_w1, -e20_dy, min_x - full_min_x);
        float w2 = shadow_advance_edge_value(row_w2, -e01_dy, min_x - full_min_x);
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
    shadow_raster_tri_scissored(
        depth, sw, sh, sx, sy, sz, su, sv, su1, sv1, sa, cmd, alpha_tex, 0, 0, sw - 1, sh - 1);
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

typedef struct {
    float sx[3], sy[3], sz[3];
    float u[3], v[3], u1[3], v1[3], alpha[3];
} sw_shadow_screen_tri_t;

typedef struct {
    sw_shadow_screen_tri_t *items;
    uint32_t count;
    uint32_t capacity;
} sw_shadow_tri_list_t;

typedef struct {
    uint32_t *indices;
    uint32_t count;
    uint32_t capacity;
} sw_shadow_tile_bin_t;

typedef struct {
    sw_shadow_tile_bin_t *bins;
    int32_t *active_indices;
    int32_t cols;
    int32_t rows;
    int32_t count;
    int32_t active_count;
    int32_t active_capacity;
} sw_shadow_tile_grid_t;

typedef struct {
    const sw_shadow_tile_grid_t *grid;
    const sw_shadow_tri_list_t *tris;
    int32_t *next_tile;
    float *depth;
    int32_t sw;
    int32_t sh;
    const vgfx3d_draw_cmd_t *cmd;
    const sw_pixels_view *alpha_tex;
} sw_shadow_raster_task_t;

typedef struct {
    const vgfx3d_draw_cmd_t *cmd;
    uint32_t tri_begin;
    uint32_t tri_end;
    const float *lmvp;
    float half_w;
    float half_h;
    sw_shadow_tri_list_t out;
    int32_t *failed;
} sw_shadow_collect_task_t;

static void sw_shadow_tri_list_free(sw_shadow_tri_list_t *list) {
    if (!list)
        return;
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static int sw_shadow_tri_list_reserve(sw_shadow_tri_list_t *list, uint32_t needed) {
    uint32_t new_capacity;
    sw_shadow_screen_tri_t *new_items;
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
    if ((size_t)new_capacity > SIZE_MAX / sizeof(sw_shadow_screen_tri_t))
        return 0;
    new_items =
        (sw_shadow_screen_tri_t *)realloc(list->items, (size_t)new_capacity * sizeof(*new_items));
    if (!new_items)
        return 0;
    list->items = new_items;
    list->capacity = new_capacity;
    return 1;
}

static int sw_shadow_tri_list_push(sw_shadow_tri_list_t *list, const sw_shadow_screen_tri_t *tri) {
    if (!list || !tri)
        return 0;
    if (list->count == UINT32_MAX)
        return 0;
    if (!sw_shadow_tri_list_reserve(list, list->count + 1u))
        return 0;
    list->items[list->count++] = *tri;
    return 1;
}

static void sw_shadow_tile_grid_free(sw_shadow_tile_grid_t *grid) {
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

static int sw_shadow_tile_grid_init(sw_shadow_tile_grid_t *grid, int32_t width, int32_t height) {
    int32_t cols;
    int32_t rows;
    if (!grid || width <= 0 || height <= 0)
        return 0;
    memset(grid, 0, sizeof(*grid));
    cols = ((width - 1) / SW_TILE_SIZE) + 1;
    rows = ((height - 1) / SW_TILE_SIZE) + 1;
    if (cols <= 0 || rows <= 0 || cols > INT32_MAX / rows)
        return 0;
    grid->cols = cols;
    grid->rows = rows;
    grid->count = cols * rows;
    grid->bins = (sw_shadow_tile_bin_t *)calloc((size_t)grid->count, sizeof(*grid->bins));
    return grid->bins ? 1 : 0;
}

static int sw_shadow_tile_grid_mark_active(sw_shadow_tile_grid_t *grid, int32_t tile_index) {
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

static int sw_shadow_tile_bin_append(sw_shadow_tile_bin_t *bin, uint32_t tri_index) {
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

static int sw_shadow_tri_bounds(const sw_shadow_screen_tri_t *tri,
                                int32_t sw,
                                int32_t sh,
                                int32_t *out_min_x,
                                int32_t *out_min_y,
                                int32_t *out_max_x,
                                int32_t *out_max_y) {
    float area;
    int32_t min_x, min_y, max_x, max_y;
    if (!tri || sw <= 0 || sh <= 0 || !out_min_x || !out_min_y || !out_max_x || !out_max_y)
        return 0;
    area = (tri->sx[1] - tri->sx[0]) * (tri->sy[2] - tri->sy[0]) -
           (tri->sx[2] - tri->sx[0]) * (tri->sy[1] - tri->sy[0]);
    if (fabsf(area) < 1e-12f)
        return 0;
    min_x = (int32_t)fmaxf(floorf(fminf(fminf(tri->sx[0], tri->sx[1]), tri->sx[2])), 0.0f);
    max_x =
        (int32_t)fminf(ceilf(fmaxf(fmaxf(tri->sx[0], tri->sx[1]), tri->sx[2])), (float)(sw - 1));
    min_y = (int32_t)fmaxf(floorf(fminf(fminf(tri->sy[0], tri->sy[1]), tri->sy[2])), 0.0f);
    max_y =
        (int32_t)fminf(ceilf(fmaxf(fmaxf(tri->sy[0], tri->sy[1]), tri->sy[2])), (float)(sh - 1));
    if (min_x > max_x || min_y > max_y)
        return 0;
    *out_min_x = min_x;
    *out_min_y = min_y;
    *out_max_x = max_x;
    *out_max_y = max_y;
    return 1;
}

static int sw_shadow_bin_triangles(sw_shadow_tile_grid_t *grid,
                                   const sw_shadow_tri_list_t *tris,
                                   int32_t sw,
                                   int32_t sh) {
    if (!grid || !grid->bins || !tris)
        return 0;
    for (uint32_t tri_index = 0; tri_index < tris->count; tri_index++) {
        int32_t min_x, min_y, max_x, max_y;
        if (!sw_shadow_tri_bounds(&tris->items[tri_index], sw, sh, &min_x, &min_y, &max_x, &max_y))
            continue;
        int32_t tx0 = min_x / SW_TILE_SIZE;
        int32_t tx1 = max_x / SW_TILE_SIZE;
        int32_t ty0 = min_y / SW_TILE_SIZE;
        int32_t ty1 = max_y / SW_TILE_SIZE;
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
                sw_shadow_tile_bin_t *bin = &grid->bins[ty * grid->cols + tx];
                uint32_t prior_count = bin->count;
                if (!sw_shadow_tile_bin_append(bin, tri_index))
                    return 0;
                if (prior_count == 0 &&
                    !sw_shadow_tile_grid_mark_active(grid, ty * grid->cols + tx))
                    return 0;
            }
        }
    }
    return 1;
}

static int sw_shadow_collect_triangles_range(const vgfx3d_draw_cmd_t *cmd,
                                             uint32_t tri_begin,
                                             uint32_t tri_end,
                                             const float *lmvp,
                                             float half_w,
                                             float half_h,
                                             sw_shadow_tri_list_t *out) {
    if (!cmd || !lmvp || !out)
        return 0;
    for (uint32_t tri_index = tri_begin; tri_index < tri_end; tri_index++) {
        uint32_t i = tri_index * 3u;
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
            sw_shadow_screen_tri_t out_tri;
            int ok = 1;
            memset(&out_tri, 0, sizeof(out_tri));
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
                out_tri.sx[vi] = (ndc_x + 1.0f) * half_w;
                out_tri.sy[vi] = (1.0f - ndc_y) * half_h;
                out_tri.sz[vi] = ndc_z * 0.5f + 0.5f;
                if (out_tri.sz[vi] < 0.0f || out_tri.sz[vi] > 1.0f) {
                    ok = 0;
                    break;
                }
                out_tri.u[vi] = fan[vi]->uv[0];
                out_tri.v[vi] = fan[vi]->uv[1];
                out_tri.u1[vi] = fan[vi]->uv1[0];
                out_tri.v1[vi] = fan[vi]->uv1[1];
                out_tri.alpha[vi] = fan[vi]->color_alpha;
            }
            if (ok && !sw_shadow_tri_list_push(out, &out_tri))
                return 0;
        }
    }
    return 1;
}

static void sw_shadow_collect_task_run(void *arg) {
    sw_shadow_collect_task_t *task = (sw_shadow_collect_task_t *)arg;
    if (!task)
        return;
    if (!sw_shadow_collect_triangles_range(task->cmd,
                                           task->tri_begin,
                                           task->tri_end,
                                           task->lmvp,
                                           task->half_w,
                                           task->half_h,
                                           &task->out)) {
        __atomic_store_n(task->failed, 1, __ATOMIC_RELAXED);
    }
}

static int sw_shadow_collect_triangles_parallel(sw_context_t *ctx,
                                                const vgfx3d_draw_cmd_t *cmd,
                                                uint32_t index_count,
                                                const float *lmvp,
                                                float half_w,
                                                float half_h,
                                                sw_shadow_tri_list_t *out) {
    sw_shadow_collect_task_t tasks[SW_MAX_TASKS];
    uint32_t tri_count;
    int32_t task_count;
    int32_t submitted = 0;
    int32_t failed = 0;
    if (!ctx || !ctx->worker_pool || ctx->worker_count <= 1 || !cmd || !lmvp || !out)
        return 0;
    tri_count = index_count / 3u;
    if (tri_count == 0)
        return 1;
    task_count = (int32_t)(ctx->worker_count + 1 < (int64_t)tri_count ? ctx->worker_count + 1
                                                                      : (int64_t)tri_count);
    memset(tasks, 0, sizeof(tasks));
    for (int32_t i = 0; i < task_count; i++) {
        tasks[i].cmd = cmd;
        tasks[i].tri_begin = (uint32_t)(((uint64_t)tri_count * (uint64_t)i) / (uint64_t)task_count);
        tasks[i].tri_end =
            (uint32_t)(((uint64_t)tri_count * (uint64_t)(i + 1)) / (uint64_t)task_count);
        tasks[i].lmvp = lmvp;
        tasks[i].half_w = half_w;
        tasks[i].half_h = half_h;
        tasks[i].failed = &failed;
    }
    for (int32_t i = 1; i < task_count; i++) {
        if (!rt_threadpool_submit_fn(ctx->worker_pool, sw_shadow_collect_task_run, &tasks[i]))
            break;
        submitted++;
    }
    for (int32_t i = submitted + 1; i < task_count; i++)
        sw_shadow_collect_task_run(&tasks[i]);
    sw_shadow_collect_task_run(&tasks[0]);
    if (submitted > 0)
        rt_threadpool_wait(ctx->worker_pool);
    if (__atomic_load_n(&failed, __ATOMIC_RELAXED)) {
        for (int32_t i = 0; i < task_count; i++)
            sw_shadow_tri_list_free(&tasks[i].out);
        return 0;
    }

    uint32_t total = 0;
    for (int32_t i = 0; i < task_count; i++) {
        if (UINT32_MAX - total < tasks[i].out.count) {
            for (int32_t j = 0; j < task_count; j++)
                sw_shadow_tri_list_free(&tasks[j].out);
            return 0;
        }
        total += tasks[i].out.count;
    }
    if (!sw_shadow_tri_list_reserve(out, total)) {
        for (int32_t i = 0; i < task_count; i++)
            sw_shadow_tri_list_free(&tasks[i].out);
        return 0;
    }
    for (int32_t i = 0; i < task_count; i++) {
        if (tasks[i].out.count > 0) {
            memcpy(&out->items[out->count],
                   tasks[i].out.items,
                   (size_t)tasks[i].out.count * sizeof(sw_shadow_screen_tri_t));
            out->count += tasks[i].out.count;
        }
        sw_shadow_tri_list_free(&tasks[i].out);
    }
    return 1;
}

static void sw_shadow_raster_task_run(void *arg) {
    sw_shadow_raster_task_t *task = (sw_shadow_raster_task_t *)arg;
    if (!task || !task->grid || !task->tris || !task->next_tile)
        return;
    for (;;) {
        int32_t tile_index = __atomic_fetch_add(task->next_tile, 1, __ATOMIC_RELAXED);
        if (tile_index >= task->grid->active_count)
            break;
        tile_index = task->grid->active_indices[tile_index];
        int32_t tx = tile_index % task->grid->cols;
        int32_t ty = tile_index / task->grid->cols;
        int32_t min_x = tx * SW_TILE_SIZE;
        int32_t min_y = ty * SW_TILE_SIZE;
        int32_t max_x = min_x + SW_TILE_SIZE - 1;
        int32_t max_y = min_y + SW_TILE_SIZE - 1;
        const sw_shadow_tile_bin_t *bin = &task->grid->bins[tile_index];
        if (max_x >= task->sw)
            max_x = task->sw - 1;
        if (max_y >= task->sh)
            max_y = task->sh - 1;
        for (uint32_t i = 0; i < bin->count; i++) {
            const sw_shadow_screen_tri_t *tri = &task->tris->items[bin->indices[i]];
            float sx[3], sy[3], sz[3], u[3], v[3], u1[3], v1[3], alpha[3];
            memcpy(sx, tri->sx, sizeof(sx));
            memcpy(sy, tri->sy, sizeof(sy));
            memcpy(sz, tri->sz, sizeof(sz));
            memcpy(u, tri->u, sizeof(u));
            memcpy(v, tri->v, sizeof(v));
            memcpy(u1, tri->u1, sizeof(u1));
            memcpy(v1, tri->v1, sizeof(v1));
            memcpy(alpha, tri->alpha, sizeof(alpha));
            shadow_raster_tri_scissored(task->depth,
                                        task->sw,
                                        task->sh,
                                        sx,
                                        sy,
                                        sz,
                                        u,
                                        v,
                                        u1,
                                        v1,
                                        alpha,
                                        task->cmd,
                                        task->alpha_tex,
                                        min_x,
                                        min_y,
                                        max_x,
                                        max_y);
        }
    }
}

static int sw_shadow_raster_tiles_parallel(sw_context_t *ctx,
                                           const sw_shadow_tile_grid_t *grid,
                                           const sw_shadow_tri_list_t *tris,
                                           float *depth,
                                           int32_t sw,
                                           int32_t sh,
                                           const vgfx3d_draw_cmd_t *cmd,
                                           const sw_pixels_view *alpha_tex) {
    sw_shadow_raster_task_t tasks[SW_MAX_TASKS];
    int32_t next_tile = 0;
    int32_t task_count;
    if (!ctx || !ctx->worker_pool || !grid || grid->active_count <= 0 || !tris || !depth)
        return 0;
    task_count = (int32_t)(ctx->worker_count + 1 < (int64_t)grid->active_count
                               ? ctx->worker_count + 1
                               : (int64_t)grid->active_count);
    for (int32_t i = 0; i < task_count; i++) {
        tasks[i].grid = grid;
        tasks[i].tris = tris;
        tasks[i].next_tile = &next_tile;
        tasks[i].depth = depth;
        tasks[i].sw = sw;
        tasks[i].sh = sh;
        tasks[i].cmd = cmd;
        tasks[i].alpha_tex = alpha_tex;
    }
    for (int32_t i = 1; i < task_count; i++) {
        if (!rt_threadpool_submit_fn(ctx->worker_pool, sw_shadow_raster_task_run, &tasks[i]))
            break;
    }
    sw_shadow_raster_task_run(&tasks[0]);
    rt_threadpool_wait(ctx->worker_pool);
    return 1;
}

static int sw_shadow_draw_parallel(void *ctx_ptr, const vgfx3d_draw_cmd_t *cmd) {
    sw_context_t *ctx = (sw_context_t *)ctx_ptr;
    int32_t slot;
    sw_pixels_view alpha_view;
    sw_pixels_view *alpha_tex = NULL;
    uint32_t index_count;
    float lmvp[16];
    float half_w, half_h;
    sw_shadow_tri_list_t tris = {0};
    sw_shadow_tile_grid_t grid = {0};
    int ok = 0;

    if (!ctx || !ctx->worker_pool || ctx->worker_count <= 1)
        return 0;
    if (rt_threadpool_current_worker_pool() == ctx->worker_pool)
        return 0;
    if (!cmd || !cmd->vertices || !cmd->indices || cmd->vertex_count == 0 || cmd->index_count == 0)
        return 0;
    index_count = vgfx3d_draw_cmd_validated_index_count(cmd);
    if (index_count < SW_PARALLEL_MIN_INDICES)
        return 0;
    slot = ctx->shadow_pass_slot;
    if (slot < 0 || slot >= VGFX3D_MAX_SHADOW_LIGHTS || !ctx->shadow_depth[slot])
        return 0;
    if ((cmd->additive_blend || vgfx3d_draw_cmd_uses_alpha_blend(cmd)) &&
        cmd->shadow_mode != RT_MATERIAL3D_SHADOW_MODE_CAST)
        return 0;
    if (cmd->alpha_mode == RT_MATERIAL3D_ALPHA_MODE_MASK && cmd->texture) {
        const sw_pixels_view *pv = (const sw_pixels_view *)cmd->texture;
        alpha_view = *pv;
        if (alpha_view.width > 0 && alpha_view.height > 0 && alpha_view.data)
            alpha_tex = &alpha_view;
    }
    if (cmd->alpha_mode == RT_MATERIAL3D_ALPHA_MODE_MASK && cmd->texture_asset && !alpha_tex)
        return 0;

    mat4f_mul(ctx->shadow_vp[slot], cmd->model_matrix, lmvp);
    half_w = (float)ctx->shadow_w[slot] * 0.5f;
    half_h = (float)ctx->shadow_h[slot] * 0.5f;

    if (!sw_shadow_collect_triangles_parallel(ctx, cmd, index_count, lmvp, half_w, half_h, &tris))
        goto cleanup;
    if (tris.count == 0) {
        ok = 1;
        goto cleanup;
    }
    if (!sw_shadow_tile_grid_init(&grid, ctx->shadow_w[slot], ctx->shadow_h[slot]))
        goto cleanup;
    if (!sw_shadow_bin_triangles(&grid, &tris, ctx->shadow_w[slot], ctx->shadow_h[slot]))
        goto cleanup;
    if (grid.active_count == 0) {
        ok = 1;
        goto cleanup;
    }
    if (!sw_shadow_raster_tiles_parallel(ctx,
                                         &grid,
                                         &tris,
                                         ctx->shadow_depth[slot],
                                         ctx->shadow_w[slot],
                                         ctx->shadow_h[slot],
                                         cmd,
                                         alpha_tex))
        goto cleanup;
    ok = 1;

cleanup:
    sw_shadow_tile_grid_free(&grid);
    sw_shadow_tri_list_free(&tris);
    return ok;
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
    sw_fill_depth_buffer(depth_buf, pixel_count, FLT_MAX);
}

/// @brief Backend `shadow_draw` op — rasterize one mesh into the shadow depth buffer.
///
/// Same vertex transform path as `submit_draw` (model matrix + light VP)
/// but feeds each triangle to `shadow_raster_tri` instead of the full
/// rasterizer. Triangles are clipped in homogeneous light clip-space before
/// perspective divide so near-plane crossings cannot poison the shadow map.
static void sw_shadow_draw_serial(void *ctx_ptr, const vgfx3d_draw_cmd_t *cmd) {
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
         * conservative; casting the full mesh would create backend-specific solid shadow flicker.
         */
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

static void sw_shadow_draw(void *ctx_ptr, const vgfx3d_draw_cmd_t *cmd) {
    if (sw_shadow_draw_parallel(ctx_ptr, cmd))
        return;
    sw_shadow_draw_serial(ctx_ptr, cmd);
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
