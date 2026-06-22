//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/backend/vgfx3d_backend_sw_raster.inc  (#included from
// vgfx3d_backend_sw.c; not a standalone translation unit) Purpose: Software backend edge-function
// triangle rasterizer and fragment shading
//===----------------------------------------------------------------------===//
/*==========================================================================
 * Edge-function triangle rasterizer
 *=========================================================================*/

static inline void sw_compute_view_vector(const sw_context_t *ctx,
                                          float wx,
                                          float wy,
                                          float wz,
                                          float *out_vx,
                                          float *out_vy,
                                          float *out_vz);
static inline float sw_compute_fog_distance(const sw_context_t *ctx, float wx, float wy, float wz);
static void sw_apply_environment_reflection(const vgfx3d_draw_cmd_t *cmd,
                                            const sw_pixels_view *normal_map,
                                            const sw_pixels_view *metallic_roughness_map,
                                            float b0,
                                            float b1,
                                            float b2,
                                            const screen_vert_t *v0,
                                            const screen_vert_t *v1,
                                            const screen_vert_t *v2,
                                            const sw_context_t *ctx,
                                            const float *precomputed_world_normal,
                                            float *inout_r,
                                            float *inout_g,
                                            float *inout_b);

// Inline math helpers — used heavily in the per-pixel PBR shading path,
// inlined for the obvious reason that one indirect call per pixel
// would dominate the cost.

/// @brief Clamp `x` to `[0, 1]`.
static inline float clamp01f(float x) {
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

/// @brief 3-vector dot product (six args, avoids array packing overhead).
static inline float dot3f(float ax, float ay, float az, float bx, float by, float bz) {
    return ax * bx + ay * by + az * bz;
}

/// @brief Compute `x^5` with multiplications for Schlick Fresnel terms.
/// @details This replaces `powf(x, 5)` inside the fragment lighting loop.
///          The exponent is constant, so two squarings plus one multiply avoid
///          libm overhead while preserving the same formula.
/// @param x Clamped Fresnel base, normally in the [0, 1] range.
/// @return `x` raised to the fifth power.
static inline float pow5f_fast(float x) {
    float x2 = x * x;
    return x2 * x2 * x;
}

/// @brief Raise @p x to a material-controlled exponent with common fast paths.
/// @details Many authored materials use small integer exponents. Avoiding
///          `powf` for those values removes a hot libm call from software
///          fragment shading while preserving the generic fallback.
/// @param x Non-negative lighting term to exponentiate.
/// @param power Material shininess or Fresnel exponent.
/// @return `x` raised to @p power, using exact multiplication paths for
///         common integer exponents and `powf` otherwise.
static inline float sw_material_powf(float x, float power) {
    if (power <= 0.0f)
        return 1.0f;
    if (fabsf(power - 1.0f) < 1e-6f)
        return x;
    if (fabsf(power - 2.0f) < 1e-6f)
        return x * x;
    if (fabsf(power - 3.0f) < 1e-6f)
        return x * x * x;
    if (fabsf(power - 4.0f) < 1e-6f) {
        float x2 = x * x;
        return x2 * x2;
    }
    if (fabsf(power - 5.0f) < 1e-6f)
        return pow5f_fast(x);
    return powf(x, power);
}

/// @brief In-place 3-vector normalization (no-op for zero-length vectors).
static inline void normalize3f(float *x, float *y, float *z) {
    (void)sw_normalize3(x, y, z, 0.0f, 0.0f, 0.0f);
}

/// @brief Compute the unit view vector from a world-space point to the camera.
/// @details Branches on projection mode: orthographic cameras have a
///   constant view direction (camera looks along `-cam_forward` everywhere
///   in the scene), so we return the negated forward vector regardless of
///   the surface position. Perspective cameras have a per-fragment view
///   direction that points from the fragment back to the eye position.
///   The result is renormalized because subtle floating-point error in
///   the subtraction can drift the magnitude away from 1.0 enough to
///   affect specular highlight shape.
static inline void sw_compute_view_vector(const sw_context_t *ctx,
                                          float wx,
                                          float wy,
                                          float wz,
                                          float *out_vx,
                                          float *out_vy,
                                          float *out_vz) {
    float vx;
    float vy;
    float vz;

    if (!ctx || !out_vx || !out_vy || !out_vz) {
        return;
    }
    if (ctx->cam_is_ortho) {
        vx = -ctx->cam_forward[0];
        vy = -ctx->cam_forward[1];
        vz = -ctx->cam_forward[2];
    } else {
        vx = ctx->cam_pos[0] - wx;
        vy = ctx->cam_pos[1] - wy;
        vz = ctx->cam_pos[2] - wz;
    }
    normalize3f(&vx, &vy, &vz);
    *out_vx = vx;
    *out_vy = vy;
    *out_vz = vz;
}

/// @brief Compute the effective fog distance from the camera to a world-space point.
/// @details Orthographic cameras use the *depth along forward* (projection
///   of the world→camera delta onto `cam_forward`, absolute value) so fog
///   density depends only on how far in front of the camera a fragment
///   sits, not its lateral offset — matches how fog attenuation behaves
///   in real ortho-rendered scenes. Perspective cameras use plain
///   euclidean distance to the eye, giving the familiar radial fog
///   falloff. Null context returns 0 so missed-setup paths produce no
///   fog rather than crashing.
static inline float sw_compute_fog_distance(const sw_context_t *ctx, float wx, float wy, float wz) {
    if (!ctx)
        return 0.0f;
    if (ctx->cam_is_ortho) {
        return fabsf((wx - ctx->cam_pos[0]) * ctx->cam_forward[0] +
                     (wy - ctx->cam_pos[1]) * ctx->cam_forward[1] +
                     (wz - ctx->cam_pos[2]) * ctx->cam_forward[2]);
    }
    {
        float dx = wx - ctx->cam_pos[0];
        float dy = wy - ctx->cam_pos[1];
        float dz = wz - ctx->cam_pos[2];
        return sw_length3(dx, dy, dz);
    }
}

/// @brief Blend an environment-cubemap reflection into the shaded fragment color.
/// @details Barycentric-interpolates the world position, perturbs the
///   surface normal through the optional tangent-space normal map,
///   computes the reflection vector about the view direction, samples
///   the bound environment cubemap (optionally roughness-blurred via
///   `rt_cubemap_sample_roughness` when a metallic/roughness map is
///   bound), and lerps the result into `inout_rgb` by the material's
///   reflectivity scalar. All early-out branches (no cubemap bound,
///   zero reflectivity, no valid view) leave `inout_rgb` untouched so
///   the caller can invoke this unconditionally and not pay for
///   reflection work on surfaces that don't want it.
static void sw_apply_environment_reflection(const vgfx3d_draw_cmd_t *cmd,
                                            const sw_pixels_view *normal_map,
                                            const sw_pixels_view *metallic_roughness_map,
                                            float b0,
                                            float b1,
                                            float b2,
                                            const screen_vert_t *v0,
                                            const screen_vert_t *v1,
                                            const screen_vert_t *v2,
                                            const sw_context_t *ctx,
                                            const float *precomputed_world_normal,
                                            float *inout_r,
                                            float *inout_g,
                                            float *inout_b) {
    float wx;
    float wy;
    float wz;
    float pnx;
    float pny;
    float pnz;
    float vdx;
    float vdy;
    float vdz;
    float reflectivity;
    float roughness;
    float normal_u = 0.0f;
    float normal_v = 0.0f;
    float mr_u = 0.0f;
    float mr_v = 0.0f;

    if (!cmd || !ctx || !inout_r || !inout_g || !inout_b || !cmd->env_map ||
        cmd->reflectivity <= 0.0001f)
        return;

    wx = b0 * v0->wx + b1 * v1->wx + b2 * v2->wx;
    wy = b0 * v0->wy + b1 * v1->wy + b2 * v2->wy;
    wz = b0 * v0->wz + b1 * v1->wz + b2 * v2->wz;
    pnx = b0 * v0->nx + b1 * v1->nx + b2 * v2->nx;
    pny = b0 * v0->ny + b1 * v1->ny + b2 * v2->ny;
    pnz = b0 * v0->nz + b1 * v1->nz + b2 * v2->nz;
    normalize3f(&pnx, &pny, &pnz);

    sw_interpolate_uv_for_slot(
        cmd, RT_MATERIAL3D_TEXTURE_SLOT_NORMAL, b0, b1, b2, v0, v1, v2, &normal_u, &normal_v);
    sw_interpolate_uv_for_slot(
        cmd, RT_MATERIAL3D_TEXTURE_SLOT_METALLIC_ROUGHNESS, b0, b1, b2, v0, v1, v2, &mr_u, &mr_v);

    /* When the main shading path already produced this fragment's perturbed
     * world normal, reuse it (below) and skip the redundant per-pixel normal-map
     * sample + TBN rebuild here. */
    if (!precomputed_world_normal && normal_map) {
        float ptx = b0 * v0->tx + b1 * v1->tx + b2 * v2->tx;
        float pty = b0 * v0->ty + b1 * v1->ty + b2 * v2->ty;
        float ptz = b0 * v0->tz + b1 * v1->tz + b2 * v2->tz;
        float ptw = b0 * v0->tw + b1 * v1->tw + b2 * v2->tw;
        float tlen = sqrtf(ptx * ptx + pty * pty + ptz * ptz);

        if (tlen > 1e-7f) {
            float tnr;
            float tng;
            float tnb;
            float tna;
            float map_x;
            float map_y;
            float map_z;
            float tdotn;
            float tangent_sign;
            float bbx;
            float bby;
            float bbz;

            ptx /= tlen;
            pty /= tlen;
            ptz /= tlen;
            tdotn = ptx * pnx + pty * pny + ptz * pnz;
            ptx -= tdotn * pnx;
            pty -= tdotn * pny;
            ptz -= tdotn * pnz;
            normalize3f(&ptx, &pty, &ptz);

            sample_texture_slot_ex(normal_map,
                                   cmd,
                                   RT_MATERIAL3D_TEXTURE_SLOT_NORMAL,
                                   normal_u,
                                   normal_v,
                                   &tnr,
                                   &tng,
                                   &tnb,
                                   &tna);
            map_x = (tnr * 2.0f - 1.0f) * cmd->normal_scale;
            map_y = (tng * 2.0f - 1.0f) * cmd->normal_scale;
            map_z = tnb * 2.0f - 1.0f;
            tangent_sign = ptw < 0.0f ? -1.0f : 1.0f;
            bbx = (pny * ptz - pnz * pty) * tangent_sign;
            bby = (pnz * ptx - pnx * ptz) * tangent_sign;
            bbz = (pnx * pty - pny * ptx) * tangent_sign;
            pnx = ptx * map_x + bbx * map_y + pnx * map_z;
            pny = pty * map_x + bby * map_y + pny * map_z;
            pnz = ptz * map_x + bbz * map_y + pnz * map_z;
            normalize3f(&pnx, &pny, &pnz);
        }
    }
    if (precomputed_world_normal) {
        pnx = precomputed_world_normal[0];
        pny = precomputed_world_normal[1];
        pnz = precomputed_world_normal[2];
    }

    sw_compute_view_vector(ctx, wx, wy, wz, &vdx, &vdy, &vdz);
    roughness = clamp01f(cmd->roughness);
    if (cmd->workflow == RT_MATERIAL3D_WORKFLOW_PBR && metallic_roughness_map) {
        float mrr;
        float mrg;
        float mrb;
        float mra;
        sample_texture_slot_ex(metallic_roughness_map,
                               cmd,
                               RT_MATERIAL3D_TEXTURE_SLOT_METALLIC_ROUGHNESS,
                               mr_u,
                               mr_v,
                               &mrr,
                               &mrg,
                               &mrb,
                               &mra);
        roughness = clamp01f(roughness * mrg);
        if (roughness < 0.045f)
            roughness = 0.045f;
    }
    reflectivity = clamp01f(cmd->reflectivity);
    if (reflectivity > 0.0f) {
        float dot_nv = dot3f(pnx, pny, pnz, vdx, vdy, vdz);
        float rx = 2.0f * dot_nv * pnx - vdx;
        float ry = 2.0f * dot_nv * pny - vdy;
        float rz = 2.0f * dot_nv * pnz - vdz;
        float env_r;
        float env_g;
        float env_b;

        normalize3f(&rx, &ry, &rz);
        rt_cubemap_sample_roughness(
            (const rt_cubemap3d *)cmd->env_map, rx, ry, rz, roughness, &env_r, &env_g, &env_b);
        *inout_r = *inout_r * (1.0f - reflectivity) + env_r * reflectivity;
        *inout_g = *inout_g * (1.0f - reflectivity) + env_g * reflectivity;
        *inout_b = *inout_b * (1.0f - reflectivity) + env_b * reflectivity;
    }
}

/// @brief PBR D term: GGX/Trowbridge-Reitz normal distribution.
///
/// `(α² / (π · ((N·H)² · (α² - 1) + 1)²))` with `α = roughness²`.
/// Standard formula from "Microfacet Models for Refraction" (Walter
/// et al, 2007). Used in the Cook-Torrance specular term.
static inline float pbr_distribution_ggx(float ndh, float roughness) {
    const float kPi = 3.14159265358979323846f;
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = ndh * ndh * (a2 - 1.0f) + 1.0f;
    return a2 / (kPi * denom * denom + 1e-6f);
}

/// @brief PBR G1 term: Schlick-GGX geometry function for one direction.
///
/// `N·V / (N·V · (1 - k) + k)` with k = (roughness+1)²/8 (per-direction
/// k; pairs with `pbr_geometry_smith` to combine view + light). Models
/// microfacet self-shadowing/masking.
static inline float pbr_geometry_schlick_ggx(float ndv, float roughness) {
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    return ndv / (ndv * (1.0f - k) + k + 1e-6f);
}

/// @brief PBR G term: Smith geometry — combines view and light G1 contributions.
static inline float pbr_geometry_smith(float ndv, float ndl, float roughness) {
    return pbr_geometry_schlick_ggx(ndv, roughness) * pbr_geometry_schlick_ggx(ndl, roughness);
}

/// @brief Per-triangle invariants shared by every fragment of one raster_triangle
///   call. Built once before the pixel loop; sw_shade_fragment reads it per pixel
///   so the rasterizer's inner loop stays a thin barycentric/depth-test skeleton.
typedef struct {
    uint8_t *pixels;
    float *zbuf;
    int32_t stride;
    const screen_vert_t *v0;
    const screen_vert_t *v1;
    const screen_vert_t *v2;
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
    const sw_context_t *fog_ctx;
    int depth_disabled;
    int have_splat;
    const sw_pixels_view *splat_view;
    const sw_pixels_view *layer_views;
} sw_fragment_ctx;

/// @brief Vertex-attribute UVs interpolated once per fragment for each texture
///   slot consumed by the per-pixel lighting stages.
typedef struct {
    float normal_u, normal_v;
    float specular_u, specular_v;
    float emissive_u, emissive_v;
    float mr_u, mr_v;
    float ao_u, ao_v;
} sw_slot_uvs;

/// @brief Per-fragment geometric inputs shared by the PBR and Phong lighting
///   stages: the (possibly normal-mapped) shading normal, world position, view
///   direction, and clamped N·V.
typedef struct {
    float nx, ny, nz;
    float wx, wy, wz;
    float vx, vy, vz;
    float ndv;
} sw_shade_geom;

/// @brief Resolve the pre-lighting albedo: interpolated vertex color modulated by
///   the base-color texture, the optional terrain-splat layer blend, and the
///   legacy emissive map. Writes the albedo to (*fr,*fg,*fb_c) and the per-texel
///   base-color alpha to *tex_alpha.
static void sw_shade_resolve_albedo(const sw_fragment_ctx *fc,
                                    float b0,
                                    float b1,
                                    float b2,
                                    float *fr,
                                    float *fg,
                                    float *fb_c,
                                    float *tex_alpha) {
    const screen_vert_t *v0 = fc->v0;
    const screen_vert_t *v1 = fc->v1;
    const screen_vert_t *v2 = fc->v2;
    const sw_pixels_view *tex = fc->tex;
    const sw_pixels_view *emissive_tex = fc->emissive_tex;
    const float *emissive_color = fc->emissive_color;
    const vgfx3d_draw_cmd_t *cmd = fc->cmd;
    int have_splat = fc->have_splat;
    const sw_pixels_view *layer_views = fc->layer_views;

    float cr = b0 * v0->r + b1 * v1->r + b2 * v2->r;
    float cg = b0 * v0->g + b1 * v1->g + b2 * v2->g;
    float cb = b0 * v0->b + b1 * v1->b + b2 * v2->b;
    float material_r = cr;
    float material_g = cg;
    float material_b = cb;
    float ta_out = 1.0f; /* per-texel alpha (for foliage, fences) */
    if (tex) {
        float u;
        float vc;
        sw_interpolate_uv_for_slot(
            cmd, RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR, b0, b1, b2, v0, v1, v2, &u, &vc);
        {
            float tr, tg, tb, ta;
            if (cmd && cmd->workflow == RT_MATERIAL3D_WORKFLOW_PBR)
                sample_texture_slot_srgb_ex(
                    tex, cmd, RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR, u, vc, &tr, &tg, &tb, &ta);
            else
                sample_texture_slot_ex(
                    tex, cmd, RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR, u, vc, &tr, &tg, &tb, &ta);
            cr *= tr;
            cg *= tg;
            cb *= tb;
            ta_out = ta;
        }
    }
    /* Terrain splat: replace diffuse with per-pixel layer blend. Views were
     * resolved once per triangle (splat_view / layer_views). */
    if (have_splat) {
        float iw = b0 * v0->inv_w + b1 * v1->inv_w + b2 * v2->inv_w;
        if (fabsf(iw) > 1e-7f) {
            float sp_u = (b0 * v0->u_over_w + b1 * v1->u_over_w + b2 * v2->u_over_w) / iw;
            float sp_v = (b0 * v0->v_over_w + b1 * v1->v_over_w + b2 * v2->v_over_w) / iw;
            {
                float sr, sg, sb, sa;
                sample_texture(fc->splat_view, sp_u, sp_v, &sr, &sg, &sb, &sa);
                float w[4] = {sr, sg, sb, sa};
                float wsum = w[0] + w[1] + w[2] + w[3];
                if (wsum > 0.001f) {
                    for (int wi = 0; wi < 4; wi++)
                        w[wi] /= wsum;
                } else {
                    w[0] = 1.0f;
                    w[1] = w[2] = w[3] = 0.0f;
                }
                float blr = 0, blg = 0, blb = 0;
                for (int L = 0; L < 4; L++) {
                    if (w[L] < 0.001f)
                        continue;
                    float lu = sp_u * cmd->splat_layer_scales[L];
                    float lvc = sp_v * cmd->splat_layer_scales[L];
                    float pr, pg, pb, pa;
                    sample_texture(&layer_views[L], lu, lvc, &pr, &pg, &pb, &pa);
                    blr += pr * w[L];
                    blg += pg * w[L];
                    blb += pb * w[L];
                }
                cr = blr * material_r;
                cg = blg * material_g;
                cb = blb * material_b;
            }
        }
    }

    /* Emissive map sampling (legacy path only; PBR handles it in the lighting
     * branch so emissiveIntensity can scale both the color and the map.) */
    if (emissive_tex && !(cmd && cmd->workflow == RT_MATERIAL3D_WORKFLOW_PBR)) {
        float u;
        float vc;
        float er, eg, eb, ea;
        sw_interpolate_uv_for_slot(
            cmd, RT_MATERIAL3D_TEXTURE_SLOT_EMISSIVE, b0, b1, b2, v0, v1, v2, &u, &vc);
        sample_texture_slot_srgb_ex(
            emissive_tex, cmd, RT_MATERIAL3D_TEXTURE_SLOT_EMISSIVE, u, vc, &er, &eg, &eb, &ea);
        float emissive_scale = (cmd ? cmd->emissive_intensity : 1.0f);
        cr += er * emissive_color[0] * emissive_scale;
        cg += eg * emissive_color[1] * emissive_scale;
        cb += eb * emissive_color[2] * emissive_scale;
        (void)ea;
    }

    *fr = cr;
    *fg = cg;
    *fb_c = cb;
    *tex_alpha = ta_out;
}

/// @brief Interpolate the world-space vertex normal and, when a normal map is
///   bound and the tangent is non-degenerate, perturb it through the TBN basis.
///   Writes the final shading normal to (*out_nx,*out_ny,*out_nz).
static void sw_shade_perturb_normal(const sw_fragment_ctx *fc,
                                    float b0,
                                    float b1,
                                    float b2,
                                    float normal_u,
                                    float normal_v,
                                    float *out_nx,
                                    float *out_ny,
                                    float *out_nz) {
    const screen_vert_t *v0 = fc->v0;
    const screen_vert_t *v1 = fc->v1;
    const screen_vert_t *v2 = fc->v2;
    const sw_pixels_view *normal_map = fc->normal_map;
    const vgfx3d_draw_cmd_t *cmd = fc->cmd;

    /* Interpolate world normal */
    float pnx = b0 * v0->nx + b1 * v1->nx + b2 * v2->nx;
    float pny = b0 * v0->ny + b1 * v1->ny + b2 * v2->ny;
    float pnz = b0 * v0->nz + b1 * v1->nz + b2 * v2->nz;
    normalize3f(&pnx, &pny, &pnz);

    /* Interpolate tangent + build TBN */
    float ptx = b0 * v0->tx + b1 * v1->tx + b2 * v2->tx;
    float pty = b0 * v0->ty + b1 * v1->ty + b2 * v2->ty;
    float ptz = b0 * v0->tz + b1 * v1->tz + b2 * v2->tz;
    float ptw = b0 * v0->tw + b1 * v1->tw + b2 * v2->tw;
    float tlen = sw_length3(ptx, pty, ptz);

    if (normal_map && tlen > 1e-7f) {
        ptx /= tlen;
        pty /= tlen;
        ptz /= tlen;
        /* Gram-Schmidt orthogonalize T against N */
        float tdotn = ptx * pnx + pty * pny + ptz * pnz;
        ptx -= tdotn * pnx;
        pty -= tdotn * pny;
        ptz -= tdotn * pnz;
        tlen = sw_length3(ptx, pty, ptz);
        if (tlen > 1e-7f) {
            ptx /= tlen;
            pty /= tlen;
            ptz /= tlen;
        }

        /* Sample normal map: [0,1] → [-1,1] */
        float tnr, tng, tnb, tna;
        sample_texture_slot_ex(normal_map,
                               cmd,
                               RT_MATERIAL3D_TEXTURE_SLOT_NORMAL,
                               normal_u,
                               normal_v,
                               &tnr,
                               &tng,
                               &tnb,
                               &tna);
        float map_x = (tnr * 2.0f - 1.0f) * cmd->normal_scale;
        float map_y = (tng * 2.0f - 1.0f) * cmd->normal_scale;
        float map_z = tnb * 2.0f - 1.0f;

        /* Bitangent = sign * (N × T) */
        float tangent_sign = ptw < 0.0f ? -1.0f : 1.0f;
        float bbx = (pny * ptz - pnz * pty) * tangent_sign;
        float bby = (pnz * ptx - pnx * ptz) * tangent_sign;
        float bbz = (pnx * pty - pny * ptx) * tangent_sign;

        /* TBN transform: tangent-space → world-space */
        float wn_x = ptx * map_x + bbx * map_y + pnx * map_z;
        float wn_y = pty * map_x + bby * map_y + pny * map_z;
        float wn_z = ptz * map_x + bbz * map_y + pnz * map_z;
        float wlen = sw_length3(wn_x, wn_y, wn_z);
        if (wlen > 1e-7f) {
            pnx = wn_x / wlen;
            pny = wn_y / wlen;
            pnz = wn_z / wlen;
        }
    }
    /* else: degenerate tangent → use unperturbed normal */

    *out_nx = pnx;
    *out_ny = pny;
    *out_nz = pnz;
}

/// @brief Compute the normalized light direction and distance attenuation for a
///   directional (type 0), point (type 1), or spot (type 3) light at world point
///   (wx,wy,wz). Returns 1 with (*llx,*lly,*llz,*la) filled, or 0 if the light is
///   degenerate and should be skipped. Ambient lights are handled by the caller.
static int sw_eval_light_dir(const vgfx3d_light_params_t *lt,
                             float wx,
                             float wy,
                             float wz,
                             float *llx,
                             float *lly,
                             float *llz,
                             float *la) {
    float lx, ly, lz;
    float atten = 1.0f;
    if (lt->type == 0) { /* directional */
        lx = -lt->direction[0];
        ly = -lt->direction[1];
        lz = -lt->direction[2];
        normalize3f(&lx, &ly, &lz);
    } else if (lt->type == 1) { /* point */
        lx = lt->position[0] - wx;
        ly = lt->position[1] - wy;
        lz = lt->position[2] - wz;
        float dist = sw_length3(lx, ly, lz);
        if (!isfinite(dist) || dist <= 1e-7f)
            return 0;
        lx /= dist;
        ly /= dist;
        lz /= dist;
        atten = 1.0f / (1.0f + lt->attenuation * dist * dist);
    } else { /* spot (type 3) */
        lx = lt->position[0] - wx;
        ly = lt->position[1] - wy;
        lz = lt->position[2] - wz;
        float dist = sw_length3(lx, ly, lz);
        if (!isfinite(dist) || dist <= 1e-7f)
            return 0;
        lx /= dist;
        ly /= dist;
        lz /= dist;
        atten = 1.0f / (1.0f + lt->attenuation * dist * dist);
        float sdx = -lt->direction[0];
        float sdy = -lt->direction[1];
        float sdz = -lt->direction[2];
        normalize3f(&sdx, &sdy, &sdz);
        float sd = lx * sdx + ly * sdy + lz * sdz;
        if (sd < lt->outer_cos)
            atten = 0.0f;
        else if (sd < lt->inner_cos) {
            float cone_range = lt->inner_cos - lt->outer_cos;
            if (cone_range <= 1e-6f) {
                atten = 0.0f;
            } else {
                float st = (sd - lt->outer_cos) / cone_range;
                atten *= st * st * (3.0f - 2.0f * st);
            }
        }
    }
    *llx = lx;
    *lly = ly;
    *llz = lz;
    *la = atten;
    return 1;
}

/// @brief Cook-Torrance PBR lighting for one fragment. Reads the albedo from
///   (*fr,*fg,*fb_c) and overwrites it with the lit color. Samples the
///   metallic-roughness, AO, and emissive maps; accumulates ambient, per-light
///   direct, and emissive contributions; applies the toon/emissive shading models.
static void sw_shade_lighting_pbr(const sw_fragment_ctx *fc,
                                  const sw_shade_geom *g,
                                  const sw_slot_uvs *uv,
                                  float *fr,
                                  float *fg,
                                  float *fb_c) {
    const vgfx3d_draw_cmd_t *cmd = fc->cmd;
    const sw_pixels_view *emissive_tex = fc->emissive_tex;
    const sw_pixels_view *metallic_roughness_map = fc->metallic_roughness_map;
    const sw_pixels_view *ao_map = fc->ao_map;
    const vgfx3d_light_params_t *lights = fc->lights;
    int32_t light_count = fc->light_count;
    const float *ambient = fc->ambient;
    const sw_context_t *fog_ctx = fc->fog_ctx;
    float pnx = g->nx, pny = g->ny, pnz = g->nz;
    float wx = g->wx, wy = g->wy, wz = g->wz;
    float vdx = g->vx, vdy = g->vy, vdz = g->vz;

    float base_r = *fr;
    float base_g = *fg;
    float base_b = *fb_c;
    float metallic = clamp01f(cmd->metallic);
    float roughness = clamp01f(cmd->roughness);
    float ao = clamp01f(cmd->ao);
    if (metallic_roughness_map) {
        float mrr, mrg, mrb, mra;
        sample_texture_slot_ex(metallic_roughness_map,
                               cmd,
                               RT_MATERIAL3D_TEXTURE_SLOT_METALLIC_ROUGHNESS,
                               uv->mr_u,
                               uv->mr_v,
                               &mrr,
                               &mrg,
                               &mrb,
                               &mra);
        roughness *= mrg;
        metallic *= mrb;
    }
    if (ao_map) {
        float aor, aog, aob, aoa;
        sample_texture_slot_ex(
            ao_map, cmd, RT_MATERIAL3D_TEXTURE_SLOT_AO, uv->ao_u, uv->ao_v, &aor, &aog, &aob, &aoa);
        ao *= aor;
    }
    metallic = clamp01f(metallic);
    roughness = roughness < 0.045f ? 0.045f : clamp01f(roughness);
    ao = clamp01f(ao);

    float ndv = g->ndv;
    if (ndv < 0.001f)
        ndv = 0.001f;

    float lit_r = (ambient ? ambient[0] : 0.0f) * base_r * ao;
    float lit_g = (ambient ? ambient[1] : 0.0f) * base_g * ao;
    float lit_b = (ambient ? ambient[2] : 0.0f) * base_b * ao;

    for (int32_t li = 0; li < light_count; li++) {
        const vgfx3d_light_params_t *lt = &lights[li];
        float llx, lly, llz, la = 1.0f;

        if (lt->type != 0 && lt->type != 1 && lt->type != 3) { /* ambient */
            lit_r += lt->color[0] * lt->intensity * base_r * ao;
            lit_g += lt->color[1] * lt->intensity * base_g * ao;
            lit_b += lt->color[2] * lt->intensity * base_b * ao;
            continue;
        }
        if (!sw_eval_light_dir(lt, wx, wy, wz, &llx, &lly, &llz, &la))
            continue;

        if ((lt->type == 0 || lt->type == 3) && lt->shadow_index >= 0 && la > 0.0f) {
            int32_t shadow_slot = sw_resolve_shadow_slot(fog_ctx, lt, wx, wy, wz);
            la *= sw_sample_shadow_visibility(fog_ctx, shadow_slot, wx, wy, wz);
        }

        float ndl = dot3f(pnx, pny, pnz, llx, lly, llz);
        if (ndl <= 0.0f)
            continue;

        float hx = llx + vdx;
        float hy = lly + vdy;
        float hz = llz + vdz;
        normalize3f(&hx, &hy, &hz);
        float ndh = clamp01f(dot3f(pnx, pny, pnz, hx, hy, hz));
        float vdh = clamp01f(dot3f(vdx, vdy, vdz, hx, hy, hz));

        float f0_r = 0.04f + (base_r - 0.04f) * metallic;
        float f0_g = 0.04f + (base_g - 0.04f) * metallic;
        float f0_b = 0.04f + (base_b - 0.04f) * metallic;
        float fresnel_w = pow5f_fast(1.0f - vdh);
        float f_r = f0_r + (1.0f - f0_r) * fresnel_w;
        float f_g = f0_g + (1.0f - f0_g) * fresnel_w;
        float f_b = f0_b + (1.0f - f0_b) * fresnel_w;

        float d = pbr_distribution_ggx(ndh, roughness);
        float gg = pbr_geometry_smith(ndv, ndl, roughness);
        float spec_denom = 4.0f * ndv * ndl + 1e-4f;
        float spec_r = (d * gg * f_r) / spec_denom;
        float spec_g = (d * gg * f_g) / spec_denom;
        float spec_b = (d * gg * f_b) / spec_denom;

        const float inv_pi = 0.31830988618f;
        float kd_r = (1.0f - f_r) * (1.0f - metallic);
        float kd_g = (1.0f - f_g) * (1.0f - metallic);
        float kd_b = (1.0f - f_b) * (1.0f - metallic);
        float diff_r = kd_r * base_r * inv_pi;
        float diff_g = kd_g * base_g * inv_pi;
        float diff_b = kd_b * base_b * inv_pi;

        float radiance_r = lt->color[0] * lt->intensity * la;
        float radiance_g = lt->color[1] * lt->intensity * la;
        float radiance_b = lt->color[2] * lt->intensity * la;
        lit_r += (diff_r + spec_r) * radiance_r * ndl;
        lit_g += (diff_g + spec_g) * radiance_g * ndl;
        lit_b += (diff_b + spec_b) * radiance_b * ndl;
    }

    float emissive_r = cmd->emissive_color[0] * cmd->emissive_intensity;
    float emissive_g = cmd->emissive_color[1] * cmd->emissive_intensity;
    float emissive_b = cmd->emissive_color[2] * cmd->emissive_intensity;
    if (emissive_tex) {
        float er, eg, eb, ea;
        sample_texture_slot_srgb_ex(emissive_tex,
                                    cmd,
                                    RT_MATERIAL3D_TEXTURE_SLOT_EMISSIVE,
                                    uv->emissive_u,
                                    uv->emissive_v,
                                    &er,
                                    &eg,
                                    &eb,
                                    &ea);
        emissive_r *= er;
        emissive_g *= eg;
        emissive_b *= eb;
    }
    lit_r += emissive_r;
    lit_g += emissive_g;
    lit_b += emissive_b;

    if (cmd->shading_model == 1) {
        float bands = cmd->custom_params[0] > 0.5f ? cmd->custom_params[0] : 4.0f;
        lit_r = floorf(lit_r * bands) / bands;
        lit_g = floorf(lit_g * bands) / bands;
        lit_b = floorf(lit_b * bands) / bands;
    } else if (cmd->shading_model == 5) {
        float strength = cmd->custom_params[0] > 0.0f ? cmd->custom_params[0] : 2.0f;
        lit_r += emissive_r * (strength - 1.0f);
        lit_g += emissive_g * (strength - 1.0f);
        lit_b += emissive_b * (strength - 1.0f);
    }

    *fr = lit_r;
    *fg = lit_g;
    *fb_c = lit_b;
}

/// @brief Blinn-Phong lighting for one normal-mapped legacy fragment. Reads the
///   albedo from (*fr,*fg,*fb_c) and overwrites it with the lit color. Samples the
///   specular map; accumulates ambient, per-light diffuse+specular, and emissive
///   contributions; applies the toon/emissive shading models.
static void sw_shade_lighting_phong(const sw_fragment_ctx *fc,
                                    const sw_shade_geom *g,
                                    const sw_slot_uvs *uv,
                                    float *fr,
                                    float *fg,
                                    float *fb_c) {
    const vgfx3d_draw_cmd_t *cmd = fc->cmd;
    const sw_pixels_view *specular_map = fc->specular_map;
    const vgfx3d_light_params_t *lights = fc->lights;
    int32_t light_count = fc->light_count;
    const float *ambient = fc->ambient;
    const sw_context_t *fog_ctx = fc->fog_ctx;
    float pnx = g->nx, pny = g->ny, pnz = g->nz;
    float wx = g->wx, wy = g->wy, wz = g->wz;
    float vdx = g->vx, vdy = g->vy, vdz = g->vz;

    float base_r = *fr;
    float base_g = *fg;
    float base_b = *fb_c;

    /* Ambient */
    float lit_r = (ambient ? ambient[0] : 0.0f) * base_r;
    float lit_g = (ambient ? ambient[1] : 0.0f) * base_g;
    float lit_b = (ambient ? ambient[2] : 0.0f) * base_b;

    /* Specular properties (possibly from specular map) */
    float sp_r = cmd->specular[0];
    float sp_g = cmd->specular[1];
    float sp_b = cmd->specular[2];
    if (specular_map) {
        float smr, smg, smb, sma;
        sample_texture_slot_ex(specular_map,
                               cmd,
                               RT_MATERIAL3D_TEXTURE_SLOT_SPECULAR,
                               uv->specular_u,
                               uv->specular_v,
                               &smr,
                               &smg,
                               &smb,
                               &sma);
        sp_r *= smr;
        sp_g *= smg;
        sp_b *= smb;
    }

    for (int32_t li = 0; li < light_count; li++) {
        const vgfx3d_light_params_t *lt = &lights[li];
        float llx, lly, llz, la = 1.0f;

        if (lt->type != 0 && lt->type != 1 && lt->type != 3) { /* ambient */
            lit_r += lt->color[0] * lt->intensity * base_r;
            lit_g += lt->color[1] * lt->intensity * base_g;
            lit_b += lt->color[2] * lt->intensity * base_b;
            continue;
        }
        if (!sw_eval_light_dir(lt, wx, wy, wz, &llx, &lly, &llz, &la))
            continue;

        if ((lt->type == 0 || lt->type == 3) && lt->shadow_index >= 0 && la > 0.0f) {
            int32_t shadow_slot = sw_resolve_shadow_slot(fog_ctx, lt, wx, wy, wz);
            la *= sw_sample_shadow_visibility(fog_ctx, shadow_slot, wx, wy, wz);
        }

        float ndl = pnx * llx + pny * lly + pnz * llz;
        if (ndl < 0.0f)
            ndl = 0.0f;
        float li_i = lt->intensity;
        lit_r += lt->color[0] * li_i * ndl * base_r * la;
        lit_g += lt->color[1] * li_i * ndl * base_g * la;
        lit_b += lt->color[2] * li_i * ndl * base_b * la;

        if (ndl > 0.0f && cmd->shininess > 0.0f) {
            float hx = llx + vdx, hy = lly + vdy, hz = llz + vdz;
            normalize3f(&hx, &hy, &hz);
            float ndh = pnx * hx + pny * hy + pnz * hz;
            if (ndh < 0.0f)
                ndh = 0.0f;
            float spec = sw_material_powf(ndh, cmd->shininess);
            lit_r += lt->color[0] * li_i * spec * sp_r * la;
            lit_g += lt->color[1] * li_i * spec * sp_g * la;
            lit_b += lt->color[2] * li_i * spec * sp_b * la;
        }
    }

    /* Emissive color base */
    lit_r += cmd->emissive_color[0] * cmd->emissive_intensity;
    lit_g += cmd->emissive_color[1] * cmd->emissive_intensity;
    lit_b += cmd->emissive_color[2] * cmd->emissive_intensity;

    if (cmd->shading_model == 1) {
        float bands = cmd->custom_params[0] > 0.5f ? cmd->custom_params[0] : 4.0f;
        lit_r = floorf(lit_r * bands) / bands;
        lit_g = floorf(lit_g * bands) / bands;
        lit_b = floorf(lit_b * bands) / bands;
    } else if (cmd->shading_model == 5) {
        float strength = cmd->custom_params[0] > 0.0f ? cmd->custom_params[0] : 2.0f;
        lit_r += cmd->emissive_color[0] * cmd->emissive_intensity * (strength - 1.0f);
        lit_g += cmd->emissive_color[1] * cmd->emissive_intensity * (strength - 1.0f);
        lit_b += cmd->emissive_color[2] * cmd->emissive_intensity * (strength - 1.0f);
    }

    *fr = lit_r;
    *fg = lit_g;
    *fb_c = lit_b;
}

/// @brief Blend the fragment color toward the fog color by interpolated camera
///   distance. Caller guards on fog_ctx->fog_enabled.
static void sw_shade_apply_fog(const sw_context_t *fog_ctx,
                               float b0,
                               float b1,
                               float b2,
                               const screen_vert_t *v0,
                               const screen_vert_t *v1,
                               const screen_vert_t *v2,
                               float *fr,
                               float *fg,
                               float *fb_c) {
    float wx = b0 * v0->wx + b1 * v1->wx + b2 * v2->wx;
    float wy = b0 * v0->wy + b1 * v1->wy + b2 * v2->wy;
    float wz = b0 * v0->wz + b1 * v1->wz + b2 * v2->wz;
    float dist = sw_compute_fog_distance(fog_ctx, wx, wy, wz);
    float fog_range = fog_ctx->fog_far - fog_ctx->fog_near;
    float fog_f = (fog_range > 1e-6f) ? (dist - fog_ctx->fog_near) / fog_range : 0.0f;
    fog_f = fog_f < 0.0f ? 0.0f : (fog_f > 1.0f ? 1.0f : fog_f);
    *fr = *fr * (1.0f - fog_f) + fog_ctx->fog_color[0] * fog_f;
    *fg = *fg * (1.0f - fog_f) + fog_ctx->fog_color[1] * fog_f;
    *fb_c = *fb_c * (1.0f - fog_f) + fog_ctx->fog_color[2] * fog_f;
}

/// @brief Apply alpha mode (mask/opaque/blend), the shading_model==4 fresnel-alpha
///   term, then write or blend the fragment into the framebuffer (with optional
///   depth write). Returns 1 if a pixel was written, 0 if discarded.
static int sw_blend_and_write(const sw_fragment_ctx *fc,
                              int x,
                              int y,
                              size_t idx,
                              float z,
                              float b0,
                              float b1,
                              float b2,
                              float fr,
                              float fg,
                              float fb_c,
                              float tex_alpha,
                              float pixel_ndv) {
    uint8_t *pixels = fc->pixels;
    float *zbuf = fc->zbuf;
    int32_t stride = fc->stride;
    const screen_vert_t *v0 = fc->v0;
    const screen_vert_t *v1 = fc->v1;
    const screen_vert_t *v2 = fc->v2;
    const vgfx3d_draw_cmd_t *cmd = fc->cmd;
    const sw_pixels_view *normal_map = fc->normal_map;
    int depth_disabled = fc->depth_disabled;

    /* Interpolate alpha, respecting material alpha modes. */
    float material_alpha = b0 * v0->a + b1 * v1->a + b2 * v2->a;
    float fa = material_alpha * tex_alpha;
    int discard_fragment = 0;
    if (cmd && cmd->alpha_mode == RT_MATERIAL3D_ALPHA_MODE_MASK) {
        if (fa < cmd->alpha_cutoff)
            discard_fragment = 1;
        fa = 1.0f;
    } else if (cmd && cmd->alpha_mode == RT_MATERIAL3D_ALPHA_MODE_OPAQUE) {
        fa = 1.0f;
    }
    if (cmd && (cmd->workflow == RT_MATERIAL3D_WORKFLOW_PBR || normal_map) &&
        cmd->shading_model == 4) {
        float ndv = pixel_ndv;
        float power = cmd->custom_params[0] > 0.1f ? cmd->custom_params[0] : 3.0f;
        float bias = cmd->custom_params[1];
        float fresnel = sw_material_powf(1.0f - ndv, power) + bias;
        fa *= clamp01f(fresnel);
    }

    uint8_t *dst = &pixels[y * stride + x * 4];
    if (!discard_fragment && fa >= 1.0f) {
        /* Opaque: overwrite pixel; screen-space overlays skip Z writes. */
        if (!depth_disabled)
            zbuf[idx] = z;
        dst[0] = (uint8_t)(clamp01f(fr) * 255.0f);
        dst[1] = (uint8_t)(clamp01f(fg) * 255.0f);
        dst[2] = (uint8_t)(clamp01f(fb_c) * 255.0f);
        dst[3] = 0xFF;
        return 1;
    } else if (!discard_fragment && fa > 0.0f) {
        /* Transparent: additive keeps the destination, alpha uses source-over.
         * Both skip Z writes so they don't occlude later transparent draws. */
        if (cmd && cmd->additive_blend) {
            dst[0] = (uint8_t)fminf(255.0f, clamp01f(fr) * 255.0f * fa + (float)dst[0]);
            dst[1] = (uint8_t)fminf(255.0f, clamp01f(fg) * 255.0f * fa + (float)dst[1]);
            dst[2] = (uint8_t)fminf(255.0f, clamp01f(fb_c) * 255.0f * fa + (float)dst[2]);
        } else {
            float inv_a = 1.0f - fa;
            dst[0] = (uint8_t)(clamp01f(fr) * 255.0f * fa + (float)dst[0] * inv_a);
            dst[1] = (uint8_t)(clamp01f(fg) * 255.0f * fa + (float)dst[1] * inv_a);
            dst[2] = (uint8_t)(clamp01f(fb_c) * 255.0f * fa + (float)dst[2] * inv_a);
        }
        dst[3] = 0xFF;
        return 1;
    }
    /* else: alpha <= 0 → fully invisible, skip */

    return 0;
}

/// @brief Shade and write one fragment that already passed the in-triangle and
///   depth tests. Resolves albedo (base/splat/emissive), then per-pixel normal
///   mapping and PBR or Phong lighting with shadows, environment reflection,
///   distance fog, and alpha-mode-aware blending into the framebuffer.
/// @param fc Per-triangle invariants (see sw_fragment_ctx).
/// @param x,y Pixel coordinates; idx the z-buffer index; z the interpolated depth.
/// @param b0,b1,b2 Barycentric weights of the pixel within the triangle.
/// @return 1 if a pixel was written (opaque or blended), 0 if discarded.
static int sw_shade_fragment(
    const sw_fragment_ctx *fc, int x, int y, size_t idx, float z, float b0, float b1, float b2) {
    const screen_vert_t *v0 = fc->v0;
    const screen_vert_t *v1 = fc->v1;
    const screen_vert_t *v2 = fc->v2;
    const sw_pixels_view *emissive_tex = fc->emissive_tex;
    const sw_pixels_view *normal_map = fc->normal_map;
    const sw_pixels_view *specular_map = fc->specular_map;
    const sw_pixels_view *metallic_roughness_map = fc->metallic_roughness_map;
    const sw_pixels_view *ao_map = fc->ao_map;
    const vgfx3d_draw_cmd_t *cmd = fc->cmd;
    const sw_context_t *fog_ctx = fc->fog_ctx;

    float fr, fg, fb_c, tex_alpha;
    sw_shade_resolve_albedo(fc, b0, b1, b2, &fr, &fg, &fb_c, &tex_alpha);

    /* Per-pixel lighting for PBR or normal-mapped legacy materials. */
    float pixel_ndv = 1.0f;
    /* Final shading normal; reused by the env-reflection pass below to avoid
     * recomputing the TBN a second time. */
    float shading_normal[3] = {0.0f, 0.0f, 0.0f};
    int shading_normal_valid = 0;
    if (cmd && !cmd->unlit && (cmd->workflow == RT_MATERIAL3D_WORKFLOW_PBR || normal_map)) {
        float pp_iw = b0 * v0->inv_w + b1 * v1->inv_w + b2 * v2->inv_w;
        if (fabsf(pp_iw) > 1e-7f) {
            sw_slot_uvs uv = {0};
            sw_interpolate_uv_for_slot(cmd,
                                       RT_MATERIAL3D_TEXTURE_SLOT_NORMAL,
                                       b0,
                                       b1,
                                       b2,
                                       v0,
                                       v1,
                                       v2,
                                       &uv.normal_u,
                                       &uv.normal_v);
            if (specular_map)
                sw_interpolate_uv_for_slot(cmd,
                                           RT_MATERIAL3D_TEXTURE_SLOT_SPECULAR,
                                           b0,
                                           b1,
                                           b2,
                                           v0,
                                           v1,
                                           v2,
                                           &uv.specular_u,
                                           &uv.specular_v);
            if (emissive_tex)
                sw_interpolate_uv_for_slot(cmd,
                                           RT_MATERIAL3D_TEXTURE_SLOT_EMISSIVE,
                                           b0,
                                           b1,
                                           b2,
                                           v0,
                                           v1,
                                           v2,
                                           &uv.emissive_u,
                                           &uv.emissive_v);
            if (metallic_roughness_map)
                sw_interpolate_uv_for_slot(cmd,
                                           RT_MATERIAL3D_TEXTURE_SLOT_METALLIC_ROUGHNESS,
                                           b0,
                                           b1,
                                           b2,
                                           v0,
                                           v1,
                                           v2,
                                           &uv.mr_u,
                                           &uv.mr_v);
            if (ao_map)
                sw_interpolate_uv_for_slot(
                    cmd, RT_MATERIAL3D_TEXTURE_SLOT_AO, b0, b1, b2, v0, v1, v2, &uv.ao_u, &uv.ao_v);

            sw_shade_geom geom;
            sw_shade_perturb_normal(
                fc, b0, b1, b2, uv.normal_u, uv.normal_v, &geom.nx, &geom.ny, &geom.nz);
            shading_normal[0] = geom.nx;
            shading_normal[1] = geom.ny;
            shading_normal[2] = geom.nz;
            shading_normal_valid = 1;

            geom.wx = b0 * v0->wx + b1 * v1->wx + b2 * v2->wx;
            geom.wy = b0 * v0->wy + b1 * v1->wy + b2 * v2->wy;
            geom.wz = b0 * v0->wz + b1 * v1->wz + b2 * v2->wz;
            sw_compute_view_vector(
                fog_ctx, geom.wx, geom.wy, geom.wz, &geom.vx, &geom.vy, &geom.vz);
            geom.ndv = clamp01f(dot3f(geom.nx, geom.ny, geom.nz, geom.vx, geom.vy, geom.vz));
            pixel_ndv = geom.ndv;

            if (cmd->workflow == RT_MATERIAL3D_WORKFLOW_PBR)
                sw_shade_lighting_pbr(fc, &geom, &uv, &fr, &fg, &fb_c);
            else
                sw_shade_lighting_phong(fc, &geom, &uv, &fr, &fg, &fb_c);
        }
    }

    if (cmd && cmd->env_map && cmd->reflectivity > 0.0001f) {
        sw_apply_environment_reflection(cmd,
                                        normal_map,
                                        metallic_roughness_map,
                                        b0,
                                        b1,
                                        b2,
                                        v0,
                                        v1,
                                        v2,
                                        fog_ctx,
                                        shading_normal_valid ? shading_normal : NULL,
                                        &fr,
                                        &fg,
                                        &fb_c);
    }

    /* Distance fog — interpolate world position, compute camera distance */
    if (fog_ctx && fog_ctx->fog_enabled)
        sw_shade_apply_fog(fog_ctx, b0, b1, b2, v0, v1, v2, &fr, &fg, &fb_c);

    return sw_blend_and_write(fc, x, y, idx, z, b0, b1, b2, fr, fg, fb_c, tex_alpha, pixel_ndv);
}

/// @brief Edge-function triangle rasterizer with PBR + Phong + shadow + fog support.
///
/// Centerpiece of the software backend. Steps:
///   1. Backface cull / winding flip via signed triangle area.
///   2. Compute screen-space bounding box, clip to viewport.
///   3. For each pixel: barycentric weights → in-triangle test → Z
///      compare → texture sample → lighting (Phong or PBR Cook-Torrance)
///      → shadow lookup → fog blend → alpha blend → write.
/// Honours every flag on `cmd` (unlit, alpha mode, normal map, etc.).
/// Optimization: perspective-correct attribute interpolation via
/// `u/w`, `v/w`, `1/w` vertex attributes.
/// @brief Computed rasterization setup for one screen-space triangle: the
///   (possibly winding-swapped) vertices, clipped bounding box, inverse area, and
///   half-space edge-function deltas + first-pixel values.
typedef struct {
    const screen_vert_t *v0, *v1, *v2;
    int min_x, max_x, min_y, max_y;
    float area, original_area, inv_area;
    float e12_dx, e12_dy, e20_dx, e20_dy, e01_dx, e01_dy;
    float row_w0, row_w1, row_w2;
    uint8_t edge12_top_left, edge20_top_left, edge01_top_left;
} sw_raster_geom;

/// @brief Return whether an oriented edge owns samples exactly on that edge.
/// @details Implements the standard top-left fill convention so adjacent triangles sharing an edge
///          do not both shade it or both leave a crack. The software backend uses screen-space
///          coordinates after viewport transform, so this deterministic ownership rule is based on
///          the final rasterized edge direction.
static int sw_edge_is_top_left(const screen_vert_t *a, const screen_vert_t *b) {
    float dx;
    float dy;
    if (!a || !b)
        return 0;
    dx = b->sx - a->sx;
    dy = b->sy - a->sy;
    return (dy > 0.0f || (dy == 0.0f && dx < 0.0f)) ? 1 : 0;
}

/// @brief Test one edge function value using top-left sample ownership.
static int sw_edge_accepts_sample(float value, uint8_t top_left) {
    return value > 0.0f || (top_left && value == 0.0f);
}

/// @brief Backface-cull, fix winding, reject degenerate/offscreen triangles, and
///   compute the bounding box + half-space edge functions. Returns 1 with *g
///   filled when the triangle should be rasterized, or 0 (emitting the matching
///   reject debug line when emit_debug is set) when it is rejected.
static int sw_raster_prepare(const screen_vert_t *v0,
                             const screen_vert_t *v1,
                             const screen_vert_t *v2,
                             int32_t fb_w,
                             int32_t fb_h,
                             int8_t backface_cull,
                             int emit_debug,
                             int32_t *debug_tri_count,
                             sw_raster_geom *g) {
    float area = (v1->sx - v0->sx) * (v2->sy - v0->sy) - (v2->sx - v0->sx) * (v1->sy - v0->sy);
    float original_area = area;
    float span_x = fmaxf(fmaxf(v0->sx, v1->sx), v2->sx) - fminf(fminf(v0->sx, v1->sx), v2->sx);
    float span_y = fmaxf(fmaxf(v0->sy, v1->sy), v2->sy) - fminf(fminf(v0->sy, v1->sy), v2->sy);
    float scale = fmaxf(1.0f, fmaxf(fabsf(span_x), fabsf(span_y)));
    const float area_epsilon = fmaxf(1e-12f, scale * scale * 1e-12f);

    if (fabsf(area) <= area_epsilon) {
        if (emit_debug) {
            fprintf(stderr,
                    "[sw3d] tri %d degenerate area=%.6f orig=%.6f\n",
                    debug_tri_count ? (*debug_tri_count)++ : 0,
                    fabsf(area),
                    original_area);
        }
        return 0;
    }

    /* After viewport Y-flip, CCW world-space triangles have NEGATIVE screen-space
     * area. So negative area = front face, positive area = back face.
     * Cull back faces (positive area) when backface culling is enabled. */
    if (backface_cull && area > area_epsilon) {
        if (emit_debug) {
            fprintf(stderr,
                    "[sw3d] tri %d culled area=%.3f v0=(%.2f %.2f %.3f) v1=(%.2f %.2f %.3f) "
                    "v2=(%.2f %.2f %.3f)\n",
                    debug_tri_count ? (*debug_tri_count)++ : 0,
                    original_area,
                    v0->sx,
                    v0->sy,
                    v0->sz,
                    v1->sx,
                    v1->sy,
                    v1->sz,
                    v2->sx,
                    v2->sy,
                    v2->sz);
        }
        return 0;
    }
    if (area < 0.0f) {
        const screen_vert_t *tmp = v1;
        v1 = v2;
        v2 = tmp;
        area = -area;
    }

    if (!isfinite(v0->sx) || !isfinite(v0->sy) || !isfinite(v1->sx) || !isfinite(v1->sy) ||
        !isfinite(v2->sx) || !isfinite(v2->sy)) {
        if (emit_debug) {
            fprintf(stderr,
                    "[sw3d] tri %d rejected non-finite screen coordinate\n",
                    debug_tri_count ? (*debug_tri_count)++ : 0);
        }
        return 0;
    }

    int min_x = (int)fmaxf(floorf(fminf(fminf(v0->sx, v1->sx), v2->sx)), 0.0f);
    int max_x = (int)fminf(ceilf(fmaxf(fmaxf(v0->sx, v1->sx), v2->sx)), (float)(fb_w - 1));
    int min_y = (int)fmaxf(floorf(fminf(fminf(v0->sy, v1->sy), v2->sy)), 0.0f);
    int max_y = (int)fminf(ceilf(fmaxf(fmaxf(v0->sy, v1->sy), v2->sy)), (float)(fb_h - 1));
    if (min_x > max_x || min_y > max_y) {
        if (emit_debug) {
            fprintf(stderr,
                    "[sw3d] tri %d clipped bbox=(%d..%d,%d..%d) area=%.3f orig=%.3f\n",
                    debug_tri_count ? (*debug_tri_count)++ : 0,
                    min_x,
                    max_x,
                    min_y,
                    max_y,
                    area,
                    original_area);
        }
        return 0;
    }

    /* Half-space edge functions for rasterization.
     * w0/w1/w2 are the signed distances from pixel center to each edge.
     * A pixel is inside when all three are >= 0. The edge function increments
     * by -dy/+dx per pixel step, enabling efficient scanline traversal. */
    float e12_dx = v2->sx - v1->sx, e12_dy = v2->sy - v1->sy;
    float e20_dx = v0->sx - v2->sx, e20_dy = v0->sy - v2->sy;
    float e01_dx = v1->sx - v0->sx, e01_dy = v1->sy - v0->sy;
    float px0 = (float)min_x + 0.5f, py0 = (float)min_y + 0.5f;

    g->v0 = v0;
    g->v1 = v1;
    g->v2 = v2;
    g->min_x = min_x;
    g->max_x = max_x;
    g->min_y = min_y;
    g->max_y = max_y;
    g->area = area;
    g->original_area = original_area;
    g->inv_area = 1.0f / area;
    g->e12_dx = e12_dx;
    g->e12_dy = e12_dy;
    g->e20_dx = e20_dx;
    g->e20_dy = e20_dy;
    g->e01_dx = e01_dx;
    g->e01_dy = e01_dy;
    g->row_w0 = e12_dx * (py0 - v1->sy) - e12_dy * (px0 - v1->sx);
    g->row_w1 = e20_dx * (py0 - v2->sy) - e20_dy * (px0 - v2->sx);
    g->row_w2 = e01_dx * (py0 - v0->sy) - e01_dy * (px0 - v0->sx);
    g->edge12_top_left = (uint8_t)sw_edge_is_top_left(v1, v2);
    g->edge20_top_left = (uint8_t)sw_edge_is_top_left(v2, v0);
    g->edge01_top_left = (uint8_t)sw_edge_is_top_left(v0, v1);
    return 1;
}

/// @brief Rasterize one screen-space triangle into the framebuffer with depth testing and
///   per-pixel shading, sampling the bound albedo/emissive/normal/specular/metallic-roughness/
///   AO maps. This is the software backend's inner fill loop.
static float sw_advance_edge_value(float value, float delta, int steps) {
    return value + delta * (float)steps;
}

static void raster_triangle_scissored(uint8_t *pixels,
                                      float *zbuf,
                                      int32_t fb_w,
                                      int32_t fb_h,
                                      int32_t stride,
                                      const screen_vert_t *v0,
                                      const screen_vert_t *v1,
                                      const screen_vert_t *v2,
                                      const sw_pixels_view *tex,
                                      const sw_pixels_view *emissive_tex,
                                      const float *emissive_color,
                                      const sw_pixels_view *normal_map,
                                      const sw_pixels_view *specular_map,
                                      const sw_pixels_view *metallic_roughness_map,
                                      const sw_pixels_view *ao_map,
                                      const vgfx3d_draw_cmd_t *cmd,
                                      const vgfx3d_light_params_t *lights,
                                      int32_t light_count,
                                      const float *ambient,
                                      int8_t backface_cull,
                                      sw_context_t *fog_ctx,
                                      int32_t clip_min_x,
                                      int32_t clip_min_y,
                                      int32_t clip_max_x,
                                      int32_t clip_max_y) {
    int emit_debug = sw_debug_enabled() && fog_ctx && fog_ctx->debug_tri_count < 16;
    sw_raster_geom g;
    if (!sw_raster_prepare(v0,
                           v1,
                           v2,
                           fb_w,
                           fb_h,
                           backface_cull,
                           emit_debug,
                           fog_ctx ? &fog_ctx->debug_tri_count : NULL,
                           &g))
        return;
    int min_x = g.min_x > clip_min_x ? g.min_x : clip_min_x;
    int max_x = g.max_x < clip_max_x ? g.max_x : clip_max_x;
    int min_y = g.min_y > clip_min_y ? g.min_y : clip_min_y;
    int max_y = g.max_y < clip_max_y ? g.max_y : clip_max_y;
    if (min_x > max_x || min_y > max_y)
        return;

    const int depth_disabled = cmd && cmd->disable_depth_test;
    const float depth_bias = cmd ? fmaxf(-0.05f, fminf(0.05f, cmd->depth_bias)) : 0.0f;
    float slope_depth_bias = 0.0f;
    if (cmd && fabsf(cmd->slope_scaled_depth_bias) > 1e-8f) {
        float denom = g.v0->sx * (g.v1->sy - g.v2->sy) + g.v1->sx * (g.v2->sy - g.v0->sy) +
                      g.v2->sx * (g.v0->sy - g.v1->sy);
        if (fabsf(denom) > 1e-8f) {
            float dzdx = (g.v0->sz * (g.v1->sy - g.v2->sy) + g.v1->sz * (g.v2->sy - g.v0->sy) +
                          g.v2->sz * (g.v0->sy - g.v1->sy)) /
                         denom;
            float dzdy = (g.v0->sz * (g.v2->sx - g.v1->sx) + g.v1->sz * (g.v0->sx - g.v2->sx) +
                          g.v2->sz * (g.v1->sx - g.v0->sx)) /
                         denom;
            slope_depth_bias = cmd->slope_scaled_depth_bias * fmaxf(fabsf(dzdx), fabsf(dzdy));
            slope_depth_bias = fmaxf(-0.05f, fminf(0.05f, slope_depth_bias));
        }
    }
    int inside_samples = 0;
    int depth_passes = 0;
    int pixels_written = 0;

    /* Terrain-splat views are constant across the whole triangle; resolve them
     * once here instead of re-opening the five Pixels views for every pixel. */
    sw_pixels_view splat_view;
    sw_pixels_view layer_views[4];
    int have_splat = (cmd && cmd->has_splat && cmd->splat_map)
                         ? sw_setup_complete_splat(cmd, &splat_view, layer_views)
                         : 0;

    sw_fragment_ctx fc = {.pixels = pixels,
                          .zbuf = zbuf,
                          .stride = stride,
                          .v0 = g.v0,
                          .v1 = g.v1,
                          .v2 = g.v2,
                          .tex = tex,
                          .emissive_tex = emissive_tex,
                          .emissive_color = emissive_color,
                          .normal_map = normal_map,
                          .specular_map = specular_map,
                          .metallic_roughness_map = metallic_roughness_map,
                          .ao_map = ao_map,
                          .cmd = cmd,
                          .lights = lights,
                          .light_count = light_count,
                          .ambient = ambient,
                          .fog_ctx = fog_ctx,
                          .depth_disabled = depth_disabled,
                          .have_splat = have_splat,
                          .splat_view = &splat_view,
                          .layer_views = layer_views};

    float row_w0 = sw_advance_edge_value(g.row_w0, g.e12_dx, min_y - g.min_y);
    float row_w1 = sw_advance_edge_value(g.row_w1, g.e20_dx, min_y - g.min_y);
    float row_w2 = sw_advance_edge_value(g.row_w2, g.e01_dx, min_y - g.min_y);
    for (int y = min_y; y <= max_y; y++) {
        float w0 = sw_advance_edge_value(row_w0, -g.e12_dy, min_x - g.min_x);
        float w1 = sw_advance_edge_value(row_w1, -g.e20_dy, min_x - g.min_x);
        float w2 = sw_advance_edge_value(row_w2, -g.e01_dy, min_x - g.min_x);
        for (int x = min_x; x <= max_x; x++) {
            if (sw_edge_accepts_sample(w0, g.edge12_top_left) &&
                sw_edge_accepts_sample(w1, g.edge20_top_left) &&
                sw_edge_accepts_sample(w2, g.edge01_top_left)) {
                inside_samples++;
                /* Barycentric weights from edge functions. sz is the post-perspective-divide
                 * NDC depth (clip.z * 1/w, set in the vertex stage), which is *linear in screen
                 * space* — so this screen-space barycentric interpolation is exactly the depth
                 * value GPU hardware produces, and matches the GL/D3D11 backends. (Perspective-
                 * correct interpolation — z/w then divide by interpolated 1/w — is for vertex
                 * *attributes* such as UVs, not for the depth buffer.) */
                float b0 = w0 * g.inv_area, b1 = w1 * g.inv_area, b2 = w2 * g.inv_area;
                float z = b0 * g.v0->sz + b1 * g.v1->sz + b2 * g.v2->sz;
                z += depth_bias + slope_depth_bias;
                size_t idx = (size_t)y * (size_t)fb_w + (size_t)x;
                if (depth_disabled || z < zbuf[idx]) {
                    depth_passes++;
                    pixels_written += sw_shade_fragment(&fc, x, y, idx, z, b0, b1, b2);
                }
            }
            w0 -= g.e12_dy;
            w1 -= g.e20_dy;
            w2 -= g.e01_dy;
        }
        row_w0 += g.e12_dx;
        row_w1 += g.e20_dx;
        row_w2 += g.e01_dx;
    }

    if (emit_debug) {
        fprintf(stderr,
                "[sw3d] tri %d area=%.3f orig=%.3f bbox=(%d..%d,%d..%d) inside=%d depth=%d "
                "written=%d v0=(%.2f %.2f %.3f) v1=(%.2f %.2f %.3f) v2=(%.2f %.2f %.3f)\n",
                fog_ctx ? fog_ctx->debug_tri_count++ : 0,
                g.area,
                g.original_area,
                g.min_x,
                g.max_x,
                g.min_y,
                g.max_y,
                inside_samples,
                depth_passes,
                pixels_written,
                g.v0->sx,
                g.v0->sy,
                g.v0->sz,
                g.v1->sx,
                g.v1->sy,
                g.v1->sz,
                g.v2->sx,
                g.v2->sy,
                g.v2->sz);
    }
}

static void raster_triangle(uint8_t *pixels,
                            float *zbuf,
                            int32_t fb_w,
                            int32_t fb_h,
                            int32_t stride,
                            const screen_vert_t *v0,
                            const screen_vert_t *v1,
                            const screen_vert_t *v2,
                            const sw_pixels_view *tex,
                            const sw_pixels_view *emissive_tex,
                            const float *emissive_color,
                            const sw_pixels_view *normal_map,
                            const sw_pixels_view *specular_map,
                            const sw_pixels_view *metallic_roughness_map,
                            const sw_pixels_view *ao_map,
                            const vgfx3d_draw_cmd_t *cmd,
                            const vgfx3d_light_params_t *lights,
                            int32_t light_count,
                            const float *ambient,
                            int8_t backface_cull,
                            sw_context_t *fog_ctx) {
    raster_triangle_scissored(pixels,
                              zbuf,
                              fb_w,
                              fb_h,
                              stride,
                              v0,
                              v1,
                              v2,
                              tex,
                              emissive_tex,
                              emissive_color,
                              normal_map,
                              specular_map,
                              metallic_roughness_map,
                              ao_map,
                              cmd,
                              lights,
                              light_count,
                              ambient,
                              backface_cull,
                              fog_ctx,
                              0,
                              0,
                              fb_w - 1,
                              fb_h - 1);
}
