//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/backend/vgfx3d_backend_opengl_material.inc  (#included from
// vgfx3d_backend_opengl.c; not a standalone translation unit) Purpose: OpenGL
// sampler/material/light uniform binding, skybox draw, and uniform-location queries
//===----------------------------------------------------------------------===//

/// @brief Activate texture unit `unit`, bind `texture`, and tell the program about it.
///
/// One-stop helper that handles the GL "select active unit, then
/// bind, then push the unit number to the sampler uniform" dance.
/// Skips the uniform write when the location is invalid (-1) — that
/// happens when the shader doesn't reference the sampler.
static void bind_texture_unit(GLint uniform_loc, int unit, GLenum target, GLuint texture) {
    gl.ActiveTexture(GL_TEXTURE0 + (GLenum)unit);
    gl.BindTexture(target, texture);
    gl.BindSampler((GLuint)unit, 0);
    if (uniform_loc >= 0)
        gl.Uniform1i(uniform_loc, unit);
}

/// @brief Map a Viper material texture-wrap constant to the corresponding OpenGL enum.
/// @details RT_MATERIAL3D_TEXTURE_WRAP_CLAMP_TO_EDGE → GL_CLAMP_TO_EDGE,
///          RT_MATERIAL3D_TEXTURE_WRAP_MIRRORED_REPEAT → GL_MIRRORED_REPEAT,
///          all other values → GL_REPEAT (the GL default).
static GLenum gl_material_wrap_mode(int32_t mode) {
    if (mode == RT_MATERIAL3D_TEXTURE_WRAP_CLAMP_TO_EDGE)
        return GL_CLAMP_TO_EDGE;
    if (mode == RT_MATERIAL3D_TEXTURE_WRAP_MIRRORED_REPEAT)
        return GL_MIRRORED_REPEAT;
    return GL_REPEAT;
}

/// @brief Map a Viper material texture-filter constant to the corresponding OpenGL enum.
/// @details RT_MATERIAL3D_TEXTURE_FILTER_NEAREST → GL_NEAREST (pixel-art / point sampling);
///          all other values → GL_LINEAR (bilinear).
static GLenum gl_material_filter_mode(int32_t mode) {
    return mode == RT_MATERIAL3D_TEXTURE_FILTER_NEAREST ? GL_NEAREST : GL_LINEAR;
}

/// @brief Map a material texture-wrap mode constant to a 3-entry sampler-cache index.
/// @details The material sampler cache is keyed by wrap_s, wrap_t, filter, and anisotropy; each
///   wrap axis has exactly 3 valid values. This function maps the abstract
///   `RT_MATERIAL3D_TEXTURE_WRAP_*` constant to the corresponding compact array index:
///     - 0 = GL_REPEAT (the default)
///     - 1 = GL_CLAMP_TO_EDGE
///     - 2 = GL_MIRRORED_REPEAT
///   Any unrecognised value falls through to 0 (repeat), matching the GL default.
/// @param mode  One of the `RT_MATERIAL3D_TEXTURE_WRAP_*` constants.
/// @return Array index in [0, 2] for use as a `material_samplers` axis subscript.
static int gl_sampler_wrap_index(int32_t mode) {
    if (mode == RT_MATERIAL3D_TEXTURE_WRAP_CLAMP_TO_EDGE)
        return 1;
    if (mode == RT_MATERIAL3D_TEXTURE_WRAP_MIRRORED_REPEAT)
        return 2;
    return 0;
}

/// @brief Resolve the anisotropy value actually usable by this GL context.
static int32_t gl_effective_sampler_anisotropy(gl_context_t *ctx,
                                               int32_t requested,
                                               int32_t filter) {
    int32_t anisotropy;
    if (!ctx || filter == RT_MATERIAL3D_TEXTURE_FILTER_NEAREST || !ctx->anisotropy_supported ||
        ctx->max_texture_anisotropy <= 1.0f)
        return 1;
    anisotropy = vgfx3d_opengl_sanitize_anisotropy(requested);
    if (anisotropy > (int32_t)ctx->max_texture_anisotropy)
        anisotropy = (int32_t)ctx->max_texture_anisotropy;
    return anisotropy < 1 ? 1 : anisotropy;
}

/// @brief Return a cached or newly-created OpenGL sampler object for the given draw command and
/// texture slot.
/// @details Resolves wrap_s, wrap_t, filter, and anisotropy from the per-slot or primary material
///          parameters, maps them to cache indices via `gl_sampler_wrap_index`, and returns the
///          cached sampler
///          when available. Otherwise generates a new GL sampler, configures WRAP_S/T and
///          MIN/MAG filters, stores it in the material sampler cache, and returns it. Returns 0 on
///          null context/cmd or GL sampler generation failure.
static GLuint gl_get_material_sampler(gl_context_t *ctx,
                                      const vgfx3d_draw_cmd_t *cmd,
                                      int32_t slot) {
    int use_slot = cmd && slot >= 0 && slot < RT_MATERIAL3D_TEXTURE_SLOT_COUNT;
    int wrap_s;
    int wrap_t;
    int filter;
    int32_t anisotropy;
    int32_t anisotropy_index;
    GLenum gl_filter;
    GLuint sampler;
    if (!ctx || !cmd)
        return 0;
    wrap_s = gl_sampler_wrap_index(use_slot ? cmd->texture_slot_wrap_s[slot] : cmd->texture_wrap_s);
    wrap_t = gl_sampler_wrap_index(use_slot ? cmd->texture_slot_wrap_t[slot] : cmd->texture_wrap_t);
    filter = (use_slot ? cmd->texture_slot_filter[slot] : cmd->texture_filter) ==
                     RT_MATERIAL3D_TEXTURE_FILTER_NEAREST
                 ? 1
                 : 0;
    anisotropy = gl_effective_sampler_anisotropy(
        ctx,
        use_slot ? cmd->texture_slot_anisotropy[slot] : cmd->texture_anisotropy,
        filter ? RT_MATERIAL3D_TEXTURE_FILTER_NEAREST : RT_MATERIAL3D_TEXTURE_FILTER_LINEAR);
    anisotropy_index = vgfx3d_opengl_sampler_anisotropy_index(anisotropy);
    sampler = ctx->material_samplers[wrap_s][wrap_t][filter][anisotropy_index];
    if (sampler)
        return sampler;
    gl.GenSamplers(1, &sampler);
    if (!sampler)
        return 0;
    gl_filter = filter ? GL_NEAREST : GL_LINEAR;
    gl.SamplerParameteri(sampler,
                         GL_TEXTURE_WRAP_S,
                         (GLint)gl_material_wrap_mode(use_slot ? cmd->texture_slot_wrap_s[slot]
                                                               : cmd->texture_wrap_s));
    gl.SamplerParameteri(sampler,
                         GL_TEXTURE_WRAP_T,
                         (GLint)gl_material_wrap_mode(use_slot ? cmd->texture_slot_wrap_t[slot]
                                                               : cmd->texture_wrap_t));
    /*
     * Generic material samplers must not require mip chains: runtime Pixels
     * textures and streaming fallbacks may expose only mip level 0. Native
     * texture uploads still configure mipmapped filtering when a full mip chain
     * is present.
     */
    gl.SamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, (GLint)(filter ? GL_NEAREST : GL_LINEAR));
    gl.SamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, (GLint)gl_filter);
    if (anisotropy > 1)
        gl.SamplerParameterf(sampler, GL_TEXTURE_MAX_ANISOTROPY_EXT, (GLfloat)anisotropy);
    if (!gl_upload_ok()) {
        gl.DeleteSamplers(1, &sampler);
        return 0;
    }
    ctx->material_samplers[wrap_s][wrap_t][filter][anisotropy_index] = sampler;
    return sampler;
}

/// @brief Bind a material texture to a unit and apply the draw command's sampler state.
/// @details Extends `bind_texture_unit` with per-material wrap and filter state. For 2D
///   textures with a valid draw command, it first attempts to retrieve a pre-built
///   `GLuint` sampler object from the `material_samplers` cache via `gl_get_material_sampler`.
///   If a cached sampler exists it is bound via `glBindSampler` (DSA-style), which overrides
///   any texture-object state. If no cached sampler is available (i.e., the sampler object
///   could not be created), the wrap and filter parameters are applied directly to the texture
///   object via `glTexParameteri` as a fallback. For non-2D targets (cubemaps etc.) the
///   call degrades to a plain `bind_texture_unit` with no sampler override.
///   @p slot selects which per-slot texture parameters to use from the draw command when it
///   is in [0, RT_MATERIAL3D_TEXTURE_SLOT_COUNT); a negative slot uses the command's global
///   wrap/filter fields instead.
/// @param ctx          Active OpenGL backend context (holds the sampler object cache).
/// @param uniform_loc  GLSL sampler uniform location; -1 is safe (write is skipped).
/// @param unit         Texture unit index (0-based); activates `GL_TEXTURE0 + unit`.
/// @param target       Texture target (e.g. `GL_TEXTURE_2D`, `GL_TEXTURE_CUBE_MAP`).
/// @param texture      GL texture name to bind; 0 effectively unbinds.
/// @param cmd          Draw command providing wrap/filter parameters; may be NULL.
/// @param slot         Per-slot index for texture parameters, or -1 for global fields.
static void bind_texture_unit_with_sampler(gl_context_t *ctx,
                                           GLint uniform_loc,
                                           int unit,
                                           GLenum target,
                                           GLuint texture,
                                           const vgfx3d_draw_cmd_t *cmd,
                                           int32_t slot) {
    bind_texture_unit(uniform_loc, unit, target, texture);
    if (texture != 0 && target == GL_TEXTURE_2D && cmd) {
        int use_slot = slot >= 0 && slot < RT_MATERIAL3D_TEXTURE_SLOT_COUNT;
        GLenum filter = gl_material_filter_mode(use_slot ? cmd->texture_slot_filter[slot]
                                                         : cmd->texture_filter);
        int32_t anisotropy = gl_effective_sampler_anisotropy(
            ctx,
            use_slot ? cmd->texture_slot_anisotropy[slot] : cmd->texture_anisotropy,
            use_slot ? cmd->texture_slot_filter[slot] : cmd->texture_filter);
        GLuint sampler = gl_get_material_sampler(ctx, cmd, slot);
        if (sampler) {
            gl.BindSampler((GLuint)unit, sampler);
            return;
        }
        gl.TexParameteri(target,
                         GL_TEXTURE_WRAP_S,
                         (GLint)gl_material_wrap_mode(use_slot ? cmd->texture_slot_wrap_s[slot]
                                                               : cmd->texture_wrap_s));
        gl.TexParameteri(target,
                         GL_TEXTURE_WRAP_T,
                         (GLint)gl_material_wrap_mode(use_slot ? cmd->texture_slot_wrap_t[slot]
                                                               : cmd->texture_wrap_t));
        gl.TexParameteri(target, GL_TEXTURE_MIN_FILTER, (GLint)(filter == GL_NEAREST ? GL_NEAREST : GL_LINEAR));
        gl.TexParameteri(target, GL_TEXTURE_MAG_FILTER, (GLint)filter);
        if (anisotropy > 1)
            gl.TexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY_EXT, (GLfloat)anisotropy);
    }
}

/// @brief Push up to `VGFX3D_MAX_LIGHTS` lights' uniforms to the program.
///
/// Uniforms are arrays-of-uniforms (one location per light index)
/// rather than a UBO — simpler to set up and small enough not to
/// matter performance-wise.
static void upload_light_uniforms(gl_context_t *ctx,
                                  const vgfx3d_light_params_t *lights,
                                  int32_t light_count) {
    if (!lights || light_count < 0)
        light_count = 0;
    if (light_count > VGFX3D_MAX_LIGHTS)
        light_count = VGFX3D_MAX_LIGHTS;
    gl.Uniform1i(ctx->uLightCount, light_count);
    for (int32_t i = 0; i < light_count; i++) {
        int32_t shadow_index = gl_sanitize_shadow_slot(ctx, lights[i].shadow_index);
        gl.Uniform1i(ctx->uLightType[i], lights[i].type);
        gl.Uniform1i(ctx->uLightShadowIndex[i], shadow_index);
        gl.Uniform1i(ctx->uLightShadowCascadeCount[i],
                     shadow_index >= 0 ? lights[i].shadow_cascade_count : 1);
        gl.Uniform1i(ctx->uLightShadowProjectionType[i],
                     shadow_index >= 0 ? lights[i].shadow_projection_type
                                       : VGFX3D_SHADOW_PROJECTION_ORTHOGRAPHIC);
        gl.Uniform4f(ctx->uLightShadowCascadeSplits[i],
                     lights[i].shadow_cascade_splits[0],
                     lights[i].shadow_cascade_splits[1],
                     lights[i].shadow_cascade_splits[2],
                     lights[i].shadow_cascade_splits[3]);
        gl.Uniform3f(ctx->uLightDir[i],
                     lights[i].direction[0],
                     lights[i].direction[1],
                     lights[i].direction[2]);
        gl.Uniform3f(
            ctx->uLightPos[i], lights[i].position[0], lights[i].position[1], lights[i].position[2]);
        gl.Uniform3f(
            ctx->uLightColor[i], lights[i].color[0], lights[i].color[1], lights[i].color[2]);
        gl.Uniform1f(ctx->uLightIntensity[i], lights[i].intensity);
        gl.Uniform1f(ctx->uLightAtten[i], lights[i].attenuation);
        gl.Uniform1f(ctx->uLightInnerCos[i], lights[i].inner_cos);
        gl.Uniform1f(ctx->uLightOuterCos[i], lights[i].outer_cos);
    }
}

/// @brief Push every per-draw uniform the main program needs.
///
/// Bundles: matrices (model + prev model + VP + prev VP + normal +
/// shadow VP), camera + ambient, all material colors / scalars / flags,
/// PBR scalars, custom params, fog, shadow toggles, light array, and
/// the morph + skinning state. Effectively the GL counterpart of the
/// D3D11 backend's per-object/per-scene/per-material cbuffer fills.
static void upload_main_uniforms(gl_context_t *ctx,
                                 const vgfx3d_draw_cmd_t *cmd,
                                 const vgfx3d_light_params_t *lights,
                                 int32_t light_count,
                                 const float *ambient,
                                 int8_t instanced) {
    float normal_matrix[16];
    static const float zero_ambient[3] = {0.0f, 0.0f, 0.0f};
    if (!ambient)
        ambient = zero_ambient;
    vgfx3d_compute_normal_matrix4(cmd->model_matrix, normal_matrix);

    gl.UniformMatrix4fv(ctx->uModelMatrix, 1, GL_TRUE, cmd->model_matrix);
    gl.UniformMatrix4fv(ctx->uPrevModelMatrix,
                        1,
                        GL_TRUE,
                        cmd->has_prev_model_matrix ? cmd->prev_model_matrix : cmd->model_matrix);
    gl.UniformMatrix4fv(ctx->uViewProjection, 1, GL_TRUE, ctx->vp);
    gl.UniformMatrix4fv(ctx->uPrevViewProjection, 1, GL_TRUE, ctx->draw_prev_vp);
    gl.UniformMatrix4fv(ctx->uNormalMatrix, 1, GL_TRUE, normal_matrix);
    gl.Uniform1i(ctx->uShadowCount, ctx->shadow_count);
    for (int32_t slot = 0; slot < VGFX3D_MAX_SHADOW_LIGHTS; slot++)
        gl.UniformMatrix4fv(ctx->uShadowVP[slot], 1, GL_TRUE, ctx->shadow_vp[slot]);

    gl.Uniform3f(ctx->uCameraPos, ctx->cam_pos[0], ctx->cam_pos[1], ctx->cam_pos[2]);
    gl.Uniform3f(
        ctx->uCameraForward, ctx->cam_forward[0], ctx->cam_forward[1], ctx->cam_forward[2]);
    gl.Uniform3f(ctx->uAmbientColor, ambient[0], ambient[1], ambient[2]);
    gl.Uniform4f(ctx->uDiffuseColor,
                 cmd->diffuse_color[0],
                 cmd->diffuse_color[1],
                 cmd->diffuse_color[2],
                 cmd->diffuse_color[3]);
    gl.Uniform4f(
        ctx->uSpecularColor, cmd->specular[0], cmd->specular[1], cmd->specular[2], cmd->shininess);
    gl.Uniform3f(ctx->uEmissiveColor,
                 cmd->emissive_color[0],
                 cmd->emissive_color[1],
                 cmd->emissive_color[2]);
    gl.Uniform4f(
        ctx->uPbrScalars0, cmd->metallic, cmd->roughness, cmd->ao, cmd->emissive_intensity);
    gl.Uniform4f(ctx->uPbrScalars1, cmd->normal_scale, cmd->alpha_cutoff, 0.0f, 0.0f);
    gl.Uniform1i(ctx->uHasEnvMap, (cmd->env_map && cmd->reflectivity > 0.0001f) ? 1 : 0);
    gl.Uniform1f(ctx->uReflectivity, cmd->reflectivity);
    gl.Uniform1f(ctx->uEnvMaxLod,
                 cmd->env_map ? gl_cubemap_max_lod((const rt_cubemap3d *)cmd->env_map) : 0.0f);
    gl.Uniform1f(ctx->uAlpha, cmd->alpha);
    gl.Uniform1i(ctx->uUnlit, cmd->unlit);
    gl.Uniform1i(ctx->uShadingModel, cmd->shading_model);
    gl.Uniform1i(ctx->uWorkflow, cmd->workflow);
    gl.Uniform1i(ctx->uAlphaMode, cmd->alpha_mode);
    gl.Uniform4i(ctx->uTextureUvSets0,
                 cmd->texture_slot_uv_set[0],
                 cmd->texture_slot_uv_set[1],
                 cmd->texture_slot_uv_set[2],
                 cmd->texture_slot_uv_set[3]);
    gl.Uniform4i(
        ctx->uTextureUvSets1, cmd->texture_slot_uv_set[4], cmd->texture_slot_uv_set[5], 0, 0);
    {
        float uv_transform0[RT_MATERIAL3D_TEXTURE_SLOT_COUNT][4];
        float uv_transform1[RT_MATERIAL3D_TEXTURE_SLOT_COUNT][4];
        for (int32_t slot = 0; slot < RT_MATERIAL3D_TEXTURE_SLOT_COUNT; slot++) {
            uv_transform0[slot][0] = cmd->texture_slot_uv_transform[slot][0];
            uv_transform0[slot][1] = cmd->texture_slot_uv_transform[slot][1];
            uv_transform0[slot][2] = cmd->texture_slot_uv_transform[slot][2];
            uv_transform0[slot][3] = cmd->texture_slot_uv_transform[slot][3];
            uv_transform1[slot][0] = cmd->texture_slot_uv_transform[slot][4];
            uv_transform1[slot][1] = cmd->texture_slot_uv_transform[slot][5];
            uv_transform1[slot][2] = 0.0f;
            uv_transform1[slot][3] = 0.0f;
        }
        gl.Uniform4fv(
            ctx->uTextureUvTransform0, RT_MATERIAL3D_TEXTURE_SLOT_COUNT, &uv_transform0[0][0]);
        gl.Uniform4fv(
            ctx->uTextureUvTransform1, RT_MATERIAL3D_TEXTURE_SLOT_COUNT, &uv_transform1[0][0]);
    }
    gl.Uniform1i(ctx->uCameraIsOrtho, ctx->cam_is_ortho ? 1 : 0);
    gl.Uniform1fv(ctx->uCustomParams, 8, cmd->custom_params);
    gl.Uniform1i(ctx->uUseInstancing, instanced ? 1 : 0);
    gl.Uniform1i(ctx->uHasPrevModelMatrix, cmd->has_prev_model_matrix ? 1 : 0);
    gl.Uniform1i(ctx->uHasPrevInstanceMatrices, cmd->has_prev_instance_matrices ? 1 : 0);
    gl.Uniform1i(ctx->uHasPrevSkinning, cmd->prev_bone_palette ? 1 : 0);
    gl.Uniform1i(ctx->uHasPrevMorphWeights, cmd->prev_morph_weights ? 1 : 0);
    gl.Uniform1i(ctx->uFogEnabled,
                 (ctx->fog_enabled && ctx->cam_is_ortho && !cmd->disable_depth_test) ? 1 : 0);
    gl.Uniform1f(ctx->uFogNear, ctx->fog_near);
    gl.Uniform1f(ctx->uFogFar, ctx->fog_far);
    gl.Uniform3f(ctx->uFogColor, ctx->fog_color[0], ctx->fog_color[1], ctx->fog_color[2]);
    gl.Uniform1f(ctx->uShadowBias, ctx->shadow_bias);

    upload_light_uniforms(ctx, lights, light_count);

    bind_morph_payload(ctx,
                       cmd,
                       ctx->uHasSkinning,
                       ctx->uMorphShapeCount,
                       ctx->uVertexCount,
                       ctx->uMorphWeights,
                       ctx->uPrevMorphWeights,
                       ctx->uMorphDeltas,
                       ctx->uHasMorphNormalDeltas,
                       ctx->uMorphNormalDeltas);
}

/// @brief Resolve every cmd texture from the cache and bind to its sampler unit.
///
/// Slot map: 0=diffuse, 1=normal, 2=specular, 3=emissive, 4-7=shadow,
/// 8=splat-control, 9-12=splat layers, 13=env cubemap, 14=metallic-
/// rough, 15=AO. Slots 16/17 are reserved for morph TBOs (set by
/// `bind_morph_payload`). Each `has*` uniform tells the shader which
/// slots are populated.
static void bind_material_textures(gl_context_t *ctx, const vgfx3d_draw_cmd_t *cmd) {
    GLuint diffuse_tex = gl_get_material_texture(
        ctx,
        cmd->texture_asset,
        cmd->texture,
        cmd->texture_asset_cache_key[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR],
        cmd->texture_asset_mip_start[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR],
        cmd->texture_asset_mip_count[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR]);
    GLuint normal_tex =
        gl_get_material_texture(ctx,
                                cmd->normal_map_asset,
                                cmd->normal_map,
                                cmd->texture_asset_cache_key[RT_MATERIAL3D_TEXTURE_SLOT_NORMAL],
                                cmd->texture_asset_mip_start[RT_MATERIAL3D_TEXTURE_SLOT_NORMAL],
                                cmd->texture_asset_mip_count[RT_MATERIAL3D_TEXTURE_SLOT_NORMAL]);
    GLuint specular_tex =
        gl_get_material_texture(ctx,
                                cmd->specular_map_asset,
                                cmd->specular_map,
                                cmd->texture_asset_cache_key[RT_MATERIAL3D_TEXTURE_SLOT_SPECULAR],
                                cmd->texture_asset_mip_start[RT_MATERIAL3D_TEXTURE_SLOT_SPECULAR],
                                cmd->texture_asset_mip_count[RT_MATERIAL3D_TEXTURE_SLOT_SPECULAR]);
    GLuint emissive_tex =
        gl_get_material_texture(ctx,
                                cmd->emissive_map_asset,
                                cmd->emissive_map,
                                cmd->texture_asset_cache_key[RT_MATERIAL3D_TEXTURE_SLOT_EMISSIVE],
                                cmd->texture_asset_mip_start[RT_MATERIAL3D_TEXTURE_SLOT_EMISSIVE],
                                cmd->texture_asset_mip_count[RT_MATERIAL3D_TEXTURE_SLOT_EMISSIVE]);
    GLuint metallic_roughness_tex = gl_get_material_texture(
        ctx,
        cmd->metallic_roughness_map_asset,
        cmd->metallic_roughness_map,
        cmd->texture_asset_cache_key[RT_MATERIAL3D_TEXTURE_SLOT_METALLIC_ROUGHNESS],
        cmd->texture_asset_mip_start[RT_MATERIAL3D_TEXTURE_SLOT_METALLIC_ROUGHNESS],
        cmd->texture_asset_mip_count[RT_MATERIAL3D_TEXTURE_SLOT_METALLIC_ROUGHNESS]);
    GLuint ao_tex =
        gl_get_material_texture(ctx,
                                cmd->ao_map_asset,
                                cmd->ao_map,
                                cmd->texture_asset_cache_key[RT_MATERIAL3D_TEXTURE_SLOT_AO],
                                cmd->texture_asset_mip_start[RT_MATERIAL3D_TEXTURE_SLOT_AO],
                                cmd->texture_asset_mip_count[RT_MATERIAL3D_TEXTURE_SLOT_AO]);
    GLuint env_tex =
        cmd->env_map ? gl_get_cached_cubemap(ctx, (const rt_cubemap3d *)cmd->env_map) : 0;
    GLuint splat_tex = cmd->splat_map ? gl_get_cached_texture(ctx, cmd->splat_map) : 0;
    GLuint splat_layer0 =
        cmd->splat_layers[0] ? gl_get_cached_texture(ctx, cmd->splat_layers[0]) : 0;
    GLuint splat_layer1 =
        cmd->splat_layers[1] ? gl_get_cached_texture(ctx, cmd->splat_layers[1]) : 0;
    GLuint splat_layer2 =
        cmd->splat_layers[2] ? gl_get_cached_texture(ctx, cmd->splat_layers[2]) : 0;
    GLuint splat_layer3 =
        cmd->splat_layers[3] ? gl_get_cached_texture(ctx, cmd->splat_layers[3]) : 0;
    int has_splat = vgfx3d_opengl_has_complete_splat(cmd->has_splat,
                                                     splat_tex != 0,
                                                     splat_layer0 != 0,
                                                     splat_layer1 != 0,
                                                     splat_layer2 != 0,
                                                     splat_layer3 != 0);

    gl.Uniform1i(ctx->uHasTexture, diffuse_tex ? 1 : 0);
    gl.Uniform1i(ctx->uHasNormalMap, normal_tex ? 1 : 0);
    gl.Uniform1i(ctx->uHasSpecularMap, specular_tex ? 1 : 0);
    gl.Uniform1i(ctx->uHasEmissiveMap, emissive_tex ? 1 : 0);
    gl.Uniform1i(ctx->uHasMetallicRoughnessMap, metallic_roughness_tex ? 1 : 0);
    gl.Uniform1i(ctx->uHasAOMap, ao_tex ? 1 : 0);
    gl.Uniform1i(ctx->uHasSplat, has_splat ? 1 : 0);

    bind_texture_unit_with_sampler(ctx,
                                   ctx->uDiffuseTex,
                                   0,
                                   GL_TEXTURE_2D,
                                   diffuse_tex,
                                   cmd,
                                   RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR);
    bind_texture_unit_with_sampler(
        ctx, ctx->uNormalTex, 1, GL_TEXTURE_2D, normal_tex, cmd, RT_MATERIAL3D_TEXTURE_SLOT_NORMAL);
    bind_texture_unit_with_sampler(ctx,
                                   ctx->uSpecularTex,
                                   2,
                                   GL_TEXTURE_2D,
                                   specular_tex,
                                   cmd,
                                   RT_MATERIAL3D_TEXTURE_SLOT_SPECULAR);
    bind_texture_unit_with_sampler(ctx,
                                   ctx->uEmissiveTex,
                                   3,
                                   GL_TEXTURE_2D,
                                   emissive_tex,
                                   cmd,
                                   RT_MATERIAL3D_TEXTURE_SLOT_EMISSIVE);
    for (int32_t slot = 0; slot < VGFX3D_MAX_SHADOW_LIGHTS; slot++) {
        bind_texture_unit(ctx->uShadowTex[slot],
                          GL_TU_SHADOW0 + slot,
                          GL_TEXTURE_2D,
                          gl_sanitize_shadow_slot(ctx, slot) >= 0 ? ctx->shadow_depth_tex[slot]
                                                                  : 0);
    }
    bind_texture_unit(ctx->uSplatTex, GL_TU_SPLAT_CONTROL, GL_TEXTURE_2D, splat_tex);
    bind_texture_unit(ctx->uSplatLayer0, GL_TU_SPLAT_LAYER0, GL_TEXTURE_2D, splat_layer0);
    bind_texture_unit(ctx->uSplatLayer1, GL_TU_SPLAT_LAYER0 + 1, GL_TEXTURE_2D, splat_layer1);
    bind_texture_unit(ctx->uSplatLayer2, GL_TU_SPLAT_LAYER0 + 2, GL_TEXTURE_2D, splat_layer2);
    bind_texture_unit(ctx->uSplatLayer3, GL_TU_SPLAT_LAYER0 + 3, GL_TEXTURE_2D, splat_layer3);
    bind_texture_unit(ctx->uEnvMap, GL_TU_ENV_MAP, GL_TEXTURE_CUBE_MAP, env_tex);
    bind_texture_unit_with_sampler(ctx,
                                   ctx->uMetallicRoughnessTex,
                                   GL_TU_METALLIC_ROUGHNESS,
                                   GL_TEXTURE_2D,
                                   metallic_roughness_tex,
                                   cmd,
                                   RT_MATERIAL3D_TEXTURE_SLOT_METALLIC_ROUGHNESS);
    bind_texture_unit_with_sampler(
        ctx, ctx->uAOTex, GL_TU_AO, GL_TEXTURE_2D, ao_tex, cmd, RT_MATERIAL3D_TEXTURE_SLOT_AO);
    if (ctx->uSplatScales >= 0) {
        gl.Uniform4f(ctx->uSplatScales,
                     cmd->splat_layer_scales[0],
                     cmd->splat_layer_scales[1],
                     cmd->splat_layer_scales[2],
                     cmd->splat_layer_scales[3]);
    }
    gl.ActiveTexture(GL_TEXTURE0);
}

/// @brief Set up animation uniforms for the depth-only shadow program.
///
/// Slimmer than the main pass — shadow depth doesn't need normals or
/// previous-frame morph weights. Reuses `bind_morph_payload` with -1
/// for the unused locations so it skips those uniform writes.
static void bind_shadow_anim(gl_context_t *ctx, const vgfx3d_draw_cmd_t *cmd) {
    bind_morph_payload(ctx,
                       cmd,
                       ctx->shadow_uHasSkinning,
                       ctx->shadow_uMorphShapeCount,
                       ctx->shadow_uVertexCount,
                       ctx->shadow_uMorphWeights,
                       -1,
                       ctx->shadow_uMorphDeltas,
                       -1,
                       -1);
}

/// @brief Composite the offscreen scene to the swapchain backbuffer.
///
/// Convenience wrapper around `gl_apply_postfx_chain` that targets the display surface, falling
/// back to an offscreen final target when the GLX default framebuffer cannot write RGB.
static int draw_scene_texture(gl_context_t *ctx, const vgfx3d_postfx_chain_t *chain) {
    int32_t w;
    int32_t h;
    if (!ctx || !ctx->scene_color_tex || ctx->width <= 0 || ctx->height <= 0)
        return 0;
    w = ctx->scene_width > 0 ? ctx->scene_width : ctx->width;
    h = ctx->scene_height > 0 ? ctx->scene_height : ctx->height;
    if (chain && chain->enabled && chain->effect_count > 0 && chain->effects &&
        !ctx->default_framebuffer_writable && gl_apply_postfx_chain_in_scene(ctx, w, h, chain)) {
        return 1;
    }
    if (chain && chain->enabled && chain->effect_count > 0 && chain->effects &&
        gl_apply_postfx_chain(
            ctx, ctx->scene_color_tex, w, h, chain, 0, ctx->default_draw_buffer, 0, NULL, NULL)) {
        return 1;
    }
    if (!ctx->postfx_program)
        return 0;
    if (!ctx->default_framebuffer_writable) {
        if (ensure_postfx_readback_target(ctx, w, h) != 0)
            return 0;
        return gl_draw_texture_to_target(
            ctx, ctx->scene_color_tex, ctx->postfx_readback_fbo, GL_COLOR_ATTACHMENT0, w, h, NULL);
    }
    return gl_draw_texture_to_target(ctx, ctx->scene_color_tex, 0, ctx->default_draw_buffer, w, h, NULL);
}

/// @brief Internal skybox draw — full-screen triangle with reconstructed view rays.
///
/// Reconstructs a world-space ray from the inverse projection and inverse
/// rotation-only view matrix so perspective and orthographic cameras share one
/// consistent skybox path.
static void gl_draw_skybox_impl(gl_context_t *ctx, const rt_cubemap3d *cubemap) {
    static const float kIdentity4x4[16] = {
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
    };
    if (!ctx || !cubemap || !ctx->skybox_program)
        return;

    GLuint tex = gl_get_cached_cubemap(ctx, cubemap);
    if (!tex)
        return;

    float view_rotation[16];
    float inverse_projection[16];
    float inverse_view_rotation[16];
    memcpy(view_rotation, ctx->view, sizeof(view_rotation));
    view_rotation[3] = 0.0f;
    view_rotation[7] = 0.0f;
    view_rotation[11] = 0.0f;
    view_rotation[12] = 0.0f;
    view_rotation[13] = 0.0f;
    view_rotation[14] = 0.0f;
    view_rotation[15] = 1.0f;
    if (mat4f_inverse_gl(ctx->projection, inverse_projection) != 0)
        memcpy(inverse_projection, kIdentity4x4, sizeof(inverse_projection));
    transpose4x4(view_rotation, inverse_view_rotation);

    if (ctx->active_target_kind == VGFX3D_OPENGL_TARGET_SCENE) {
        gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->scene_fbo);
        gl.DrawBuffer(GL_COLOR_ATTACHMENT0);
    }

    gl.DepthMask(GL_FALSE);
    gl.DepthFunc(GL_LEQUAL);
    gl.Disable(GL_CULL_FACE);
    gl.UseProgram(ctx->skybox_program);
    gl.BindVertexArray(ctx->skybox_vao);
    gl.UniformMatrix4fv(ctx->skybox_uInverseProjection, 1, GL_TRUE, inverse_projection);
    gl.UniformMatrix4fv(ctx->skybox_uInverseViewRotation, 1, GL_TRUE, inverse_view_rotation);
    gl.Uniform3f(
        ctx->skybox_uDirection, ctx->cam_forward[0], ctx->cam_forward[1], ctx->cam_forward[2]);
    gl.Uniform1i(ctx->skybox_uIsOrtho, ctx->cam_is_ortho ? 1 : 0);
    bind_texture_unit(ctx->skybox_uSkybox, GL_TU_ENV_MAP, GL_TEXTURE_CUBE_MAP, tex);
    gl.DrawArrays(GL_TRIANGLES, 0, 3);

    gl.ActiveTexture(GL_TEXTURE0);
    gl.DepthFunc(GL_LESS);
    gl.DepthMask(GL_TRUE);
    gl.Enable(GL_CULL_FACE);
    gl.UseProgram(ctx->program);
    gl.BindVertexArray(ctx->vao);
    if (ctx->active_target_kind == VGFX3D_OPENGL_TARGET_SCENE)
        bind_main_framebuffer(ctx);
}

/// @brief Resolve every uniform location in the main program.
///
/// Called once at context-create time. Uses the `U(name)` macro to
/// keep the boilerplate compact. Per-light array uniforms are queried
/// individually with `snprintf`-built names like `uLightPos[3]`.
/// Locations of -1 (uniform doesn't exist or was optimized out) are
/// fine — the per-frame `glUniform*` calls handle them gracefully.
static void query_main_uniforms(gl_context_t *ctx) {
#define U(name) ctx->name = gl.GetUniformLocation(ctx->program, #name)
    U(uModelMatrix);
    U(uPrevModelMatrix);
    U(uViewProjection);
    U(uPrevViewProjection);
    U(uNormalMatrix);
    U(uCameraPos);
    U(uCameraForward);
    U(uAmbientColor);
    U(uDiffuseColor);
    U(uSpecularColor);
    U(uEmissiveColor);
    U(uAlpha);
    U(uPbrScalars0);
    U(uPbrScalars1);
    U(uUnlit);
    U(uShadingModel);
    U(uLightCount);
    U(uHasTexture);
    U(uHasNormalMap);
    U(uHasSpecularMap);
    U(uHasEmissiveMap);
    U(uHasEnvMap);
    U(uReflectivity);
    U(uEnvMaxLod);
    U(uWorkflow);
    U(uAlphaMode);
    U(uCameraIsOrtho);
    U(uHasMetallicRoughnessMap);
    U(uHasAOMap);
    U(uCustomParams);
    U(uTextureUvSets0);
    U(uTextureUvSets1);
    ctx->uTextureUvTransform0 = gl.GetUniformLocation(ctx->program, "uTextureUvTransform0[0]");
    ctx->uTextureUvTransform1 = gl.GetUniformLocation(ctx->program, "uTextureUvTransform1[0]");
    U(uHasSplat);
    U(uFogEnabled);
    U(uFogNear);
    U(uFogFar);
    U(uFogColor);
    U(uShadowCount);
    U(uShadowBias);
    U(uUseInstancing);
    U(uHasPrevModelMatrix);
    U(uHasPrevInstanceMatrices);
    U(uHasSkinning);
    U(uHasPrevSkinning);
    U(uMorphShapeCount);
    U(uVertexCount);
    U(uHasPrevMorphWeights);
    ctx->uMorphWeights = gl.GetUniformLocation(ctx->program, "uMorphWeights[0]");
    ctx->uPrevMorphWeights = gl.GetUniformLocation(ctx->program, "uPrevMorphWeights[0]");
    U(uMorphDeltas);
    U(uMorphNormalDeltas);
    U(uHasMorphNormalDeltas);
    U(uDiffuseTex);
    U(uNormalTex);
    U(uSpecularTex);
    U(uEmissiveTex);
    U(uEnvMap);
    U(uMetallicRoughnessTex);
    U(uAOTex);
    U(uSplatTex);
    U(uSplatLayer0);
    U(uSplatLayer1);
    U(uSplatLayer2);
    U(uSplatLayer3);
    U(uSplatScales);
#undef U
    for (int i = 0; i < VGFX3D_MAX_LIGHTS; i++) {
        char name[64];
        snprintf(name, sizeof(name), "uLightType[%d]", i);
        ctx->uLightType[i] = gl.GetUniformLocation(ctx->program, name);
        snprintf(name, sizeof(name), "uLightShadowIndex[%d]", i);
        ctx->uLightShadowIndex[i] = gl.GetUniformLocation(ctx->program, name);
        snprintf(name, sizeof(name), "uLightShadowCascadeCount[%d]", i);
        ctx->uLightShadowCascadeCount[i] = gl.GetUniformLocation(ctx->program, name);
        snprintf(name, sizeof(name), "uLightShadowProjectionType[%d]", i);
        ctx->uLightShadowProjectionType[i] = gl.GetUniformLocation(ctx->program, name);
        snprintf(name, sizeof(name), "uLightShadowCascadeSplits[%d]", i);
        ctx->uLightShadowCascadeSplits[i] = gl.GetUniformLocation(ctx->program, name);
        snprintf(name, sizeof(name), "uLightDir[%d]", i);
        ctx->uLightDir[i] = gl.GetUniformLocation(ctx->program, name);
        snprintf(name, sizeof(name), "uLightPos[%d]", i);
        ctx->uLightPos[i] = gl.GetUniformLocation(ctx->program, name);
        snprintf(name, sizeof(name), "uLightColor[%d]", i);
        ctx->uLightColor[i] = gl.GetUniformLocation(ctx->program, name);
        snprintf(name, sizeof(name), "uLightIntensity[%d]", i);
        ctx->uLightIntensity[i] = gl.GetUniformLocation(ctx->program, name);
        snprintf(name, sizeof(name), "uLightAtten[%d]", i);
        ctx->uLightAtten[i] = gl.GetUniformLocation(ctx->program, name);
        snprintf(name, sizeof(name), "uLightInnerCos[%d]", i);
        ctx->uLightInnerCos[i] = gl.GetUniformLocation(ctx->program, name);
        snprintf(name, sizeof(name), "uLightOuterCos[%d]", i);
        ctx->uLightOuterCos[i] = gl.GetUniformLocation(ctx->program, name);
    }
    for (int i = 0; i < VGFX3D_MAX_SHADOW_LIGHTS; i++) {
        char name[64];
        snprintf(name, sizeof(name), "uShadowVP[%d]", i);
        ctx->uShadowVP[i] = gl.GetUniformLocation(ctx->program, name);
    }
    ctx->uShadowTex[0] = gl.GetUniformLocation(ctx->program, "uShadowTex0");
    ctx->uShadowTex[1] = gl.GetUniformLocation(ctx->program, "uShadowTex1");
    ctx->uShadowTex[2] = gl.GetUniformLocation(ctx->program, "uShadowTex2");
    ctx->uShadowTex[3] = gl.GetUniformLocation(ctx->program, "uShadowTex3");
}

/// @brief Resolve uniform locations for the depth-only shadow program.
static void query_shadow_uniforms(gl_context_t *ctx) {
    ctx->shadow_uModelMatrix = gl.GetUniformLocation(ctx->shadow_program, "uModelMatrix");
    ctx->shadow_uViewProjection = gl.GetUniformLocation(ctx->shadow_program, "uViewProjection");
    ctx->shadow_uHasSkinning = gl.GetUniformLocation(ctx->shadow_program, "uHasSkinning");
    ctx->shadow_uMorphShapeCount = gl.GetUniformLocation(ctx->shadow_program, "uMorphShapeCount");
    ctx->shadow_uVertexCount = gl.GetUniformLocation(ctx->shadow_program, "uVertexCount");
    ctx->shadow_uMorphWeights = gl.GetUniformLocation(ctx->shadow_program, "uMorphWeights[0]");
    ctx->shadow_uMorphDeltas = gl.GetUniformLocation(ctx->shadow_program, "uMorphDeltas");
    ctx->shadow_uDiffuseTex = gl.GetUniformLocation(ctx->shadow_program, "uShadowDiffuseTex");
    ctx->shadow_uHasTexture = gl.GetUniformLocation(ctx->shadow_program, "uShadowHasTexture");
    ctx->shadow_uAlphaMode = gl.GetUniformLocation(ctx->shadow_program, "uShadowAlphaMode");
    ctx->shadow_uAlphaCutoff = gl.GetUniformLocation(ctx->shadow_program, "uShadowAlphaCutoff");
    ctx->shadow_uAlpha = gl.GetUniformLocation(ctx->shadow_program, "uShadowAlpha");
    ctx->shadow_uDiffuseColor = gl.GetUniformLocation(ctx->shadow_program, "uShadowDiffuseColor");
    ctx->shadow_uTextureUvSets0 =
        gl.GetUniformLocation(ctx->shadow_program, "uShadowTextureUvSets0");
    ctx->shadow_uTextureUvSets1 =
        gl.GetUniformLocation(ctx->shadow_program, "uShadowTextureUvSets1");
    ctx->shadow_uTextureUvTransform0 =
        gl.GetUniformLocation(ctx->shadow_program, "uShadowTextureUvTransform0[0]");
    ctx->shadow_uTextureUvTransform1 =
        gl.GetUniformLocation(ctx->shadow_program, "uShadowTextureUvTransform1[0]");
}

/// @brief Resolve uniform locations for the cubemap skybox program.
static void query_skybox_uniforms(gl_context_t *ctx) {
    ctx->skybox_uInverseProjection =
        gl.GetUniformLocation(ctx->skybox_program, "uInverseProjection");
    ctx->skybox_uInverseViewRotation =
        gl.GetUniformLocation(ctx->skybox_program, "uInverseViewRotation");
    ctx->skybox_uDirection = gl.GetUniformLocation(ctx->skybox_program, "uDirection");
    ctx->skybox_uIsOrtho = gl.GetUniformLocation(ctx->skybox_program, "uIsOrtho");
    ctx->skybox_uSkybox = gl.GetUniformLocation(ctx->skybox_program, "uSkybox");
}

/// @brief Resolve every uniform in the post-FX shader (bloom, tonemap, FXAA, etc.).
static void query_postfx_uniforms(gl_context_t *ctx) {
    ctx->postfx_uSceneTex = gl.GetUniformLocation(ctx->postfx_program, "uSceneTex");
    ctx->postfx_uSceneDepthTex = gl.GetUniformLocation(ctx->postfx_program, "uSceneDepthTex");
    ctx->postfx_uSceneMotionTex = gl.GetUniformLocation(ctx->postfx_program, "uSceneMotionTex");
    ctx->postfx_uInvResolution = gl.GetUniformLocation(ctx->postfx_program, "uInvResolution");
    ctx->postfx_uBloomEnabled = gl.GetUniformLocation(ctx->postfx_program, "uBloomEnabled");
    ctx->postfx_uBloomThreshold = gl.GetUniformLocation(ctx->postfx_program, "uBloomThreshold");
    ctx->postfx_uBloomIntensity = gl.GetUniformLocation(ctx->postfx_program, "uBloomIntensity");
    ctx->postfx_uBloomPasses = gl.GetUniformLocation(ctx->postfx_program, "uBloomPasses");
    ctx->postfx_uTonemapMode = gl.GetUniformLocation(ctx->postfx_program, "uTonemapMode");
    ctx->postfx_uTonemapExposure = gl.GetUniformLocation(ctx->postfx_program, "uTonemapExposure");
    ctx->postfx_uFxaaEnabled = gl.GetUniformLocation(ctx->postfx_program, "uFxaaEnabled");
    ctx->postfx_uColorGradeEnabled =
        gl.GetUniformLocation(ctx->postfx_program, "uColorGradeEnabled");
    ctx->postfx_uCgBrightness = gl.GetUniformLocation(ctx->postfx_program, "uCgBrightness");
    ctx->postfx_uCgContrast = gl.GetUniformLocation(ctx->postfx_program, "uCgContrast");
    ctx->postfx_uCgSaturation = gl.GetUniformLocation(ctx->postfx_program, "uCgSaturation");
    ctx->postfx_uVignetteEnabled = gl.GetUniformLocation(ctx->postfx_program, "uVignetteEnabled");
    ctx->postfx_uVignetteRadius = gl.GetUniformLocation(ctx->postfx_program, "uVignetteRadius");
    ctx->postfx_uVignetteSoftness = gl.GetUniformLocation(ctx->postfx_program, "uVignetteSoftness");
    ctx->postfx_uSsaoEnabled = gl.GetUniformLocation(ctx->postfx_program, "uSsaoEnabled");
    ctx->postfx_uSsaoRadius = gl.GetUniformLocation(ctx->postfx_program, "uSsaoRadius");
    ctx->postfx_uSsaoIntensity = gl.GetUniformLocation(ctx->postfx_program, "uSsaoIntensity");
    ctx->postfx_uSsaoSamples = gl.GetUniformLocation(ctx->postfx_program, "uSsaoSamples");
    ctx->postfx_uDofEnabled = gl.GetUniformLocation(ctx->postfx_program, "uDofEnabled");
    ctx->postfx_uDofFocusDistance = gl.GetUniformLocation(ctx->postfx_program, "uDofFocusDistance");
    ctx->postfx_uDofAperture = gl.GetUniformLocation(ctx->postfx_program, "uDofAperture");
    ctx->postfx_uDofMaxBlur = gl.GetUniformLocation(ctx->postfx_program, "uDofMaxBlur");
    ctx->postfx_uMotionBlurEnabled =
        gl.GetUniformLocation(ctx->postfx_program, "uMotionBlurEnabled");
    ctx->postfx_uMotionBlurIntensity =
        gl.GetUniformLocation(ctx->postfx_program, "uMotionBlurIntensity");
    ctx->postfx_uMotionBlurSamples =
        gl.GetUniformLocation(ctx->postfx_program, "uMotionBlurSamples");
    ctx->postfx_uCameraPos = gl.GetUniformLocation(ctx->postfx_program, "uCameraPos");
    ctx->postfx_uInvViewProjection =
        gl.GetUniformLocation(ctx->postfx_program, "uInvViewProjection");
    ctx->postfx_uPrevViewProjection =
        gl.GetUniformLocation(ctx->postfx_program, "uPrevViewProjection");
}
