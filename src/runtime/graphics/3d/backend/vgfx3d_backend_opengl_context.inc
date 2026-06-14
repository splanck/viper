//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/backend/vgfx3d_backend_opengl_context.inc  (#included from vgfx3d_backend_opengl.c; not a standalone translation unit)
// Purpose: OpenGL context creation, shader/program/object init, and teardown
//===----------------------------------------------------------------------===//
typedef struct gl_shader_set_t {
    GLuint vs;
    GLuint fs;
    GLuint shadow_vs;
    GLuint shadow_fs;
    GLuint postfx_vs;
    GLuint postfx_fs;
    GLuint skybox_vs;
    GLuint skybox_fs;
} gl_shader_set_t;

/// @brief Pick an FBConfig compatible with the target X window's visual/depth.
static GLXFBConfig gl_pick_matching_fb_config(Display *dpy,
                                             Window xwin,
                                             GLXFBConfig *configs,
                                             int fb_count) {
    GLXFBConfig chosen = configs[0];
    XWindowAttributes window_attrs;
    if (XGetWindowAttributes(dpy, xwin, &window_attrs)) {
        for (int i = 0; i < fb_count; ++i) {
            XVisualInfo *visual_info = glx.GetVisualFromFBConfig(dpy, configs[i]);
            if (!visual_info)
                continue;
            const int visual_match = visual_info->visual == window_attrs.visual;
            const int depth_match = visual_info->depth == window_attrs.depth;
            XFree(visual_info);
            if (visual_match && depth_match) {
                chosen = configs[i];
                break;
            }
        }
    }
    return chosen;
}

/// @brief Initialize all per-context matrices and default target/upload state.
static void gl_init_context_defaults(gl_context_t *ctx, int32_t w, int32_t h) {
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
    ctx->width = w;
    ctx->height = h;
    memcpy(ctx->view, kIdentity4x4, sizeof(ctx->view));
    memcpy(ctx->projection, kIdentity4x4, sizeof(ctx->projection));
    memcpy(ctx->vp, kIdentity4x4, sizeof(ctx->vp));
    memcpy(ctx->inv_vp, kIdentity4x4, sizeof(ctx->inv_vp));
    memcpy(ctx->draw_prev_vp, kIdentity4x4, sizeof(ctx->draw_prev_vp));
    memcpy(ctx->scene_vp, kIdentity4x4, sizeof(ctx->scene_vp));
    memcpy(ctx->scene_prev_vp, kIdentity4x4, sizeof(ctx->scene_prev_vp));
    memcpy(ctx->scene_inv_vp, kIdentity4x4, sizeof(ctx->scene_inv_vp));
    ctx->active_target_kind = VGFX3D_OPENGL_TARGET_SWAPCHAIN;
    ctx->texture_upload_budget_bytes = UINT64_MAX;
}

/// @brief Compile every shader used by the OpenGL backend context.
static int gl_compile_context_shaders(gl_shader_set_t *shaders) {
    memset(shaders, 0, sizeof(*shaders));
    shaders->vs =
        compile_shader_parts(GL_VERTEX_SHADER,
                             glsl_vertex_src,
                             (GLsizei)(sizeof(glsl_vertex_src) / sizeof(glsl_vertex_src[0])));
    shaders->fs =
        compile_shader_parts(GL_FRAGMENT_SHADER,
                             glsl_fragment_src,
                             (GLsizei)(sizeof(glsl_fragment_src) / sizeof(glsl_fragment_src[0])));
    shaders->shadow_vs = compile_shader(GL_VERTEX_SHADER, glsl_shadow_vertex_src);
    shaders->shadow_fs = compile_shader(GL_FRAGMENT_SHADER, glsl_shadow_fragment_src);
    shaders->postfx_vs = compile_shader(GL_VERTEX_SHADER, glsl_postfx_vertex_src);
    shaders->postfx_fs = compile_shader_parts(
        GL_FRAGMENT_SHADER,
        glsl_postfx_fragment_src,
        (GLsizei)(sizeof(glsl_postfx_fragment_src) / sizeof(glsl_postfx_fragment_src[0])));
    shaders->skybox_vs = compile_shader(GL_VERTEX_SHADER, glsl_skybox_vertex_src);
    shaders->skybox_fs = compile_shader(GL_FRAGMENT_SHADER, glsl_skybox_fragment_src);
    return shaders->vs && shaders->fs && shaders->shadow_vs && shaders->shadow_fs &&
           shaders->postfx_vs && shaders->postfx_fs && shaders->skybox_vs && shaders->skybox_fs;
}

/// @brief Delete all shaders in a temporary context shader bundle.
static void gl_delete_context_shaders(gl_shader_set_t *shaders) {
    if (shaders->vs)
        gl.DeleteShader(shaders->vs);
    if (shaders->fs)
        gl.DeleteShader(shaders->fs);
    if (shaders->shadow_vs)
        gl.DeleteShader(shaders->shadow_vs);
    if (shaders->shadow_fs)
        gl.DeleteShader(shaders->shadow_fs);
    if (shaders->postfx_vs)
        gl.DeleteShader(shaders->postfx_vs);
    if (shaders->postfx_fs)
        gl.DeleteShader(shaders->postfx_fs);
    if (shaders->skybox_vs)
        gl.DeleteShader(shaders->skybox_vs);
    if (shaders->skybox_fs)
        gl.DeleteShader(shaders->skybox_fs);
    memset(shaders, 0, sizeof(*shaders));
}

/// @brief Link shader programs from the temporary shader bundle.
static int gl_link_context_programs(gl_context_t *ctx, const gl_shader_set_t *shaders) {
    ctx->program = link_program(shaders->vs, shaders->fs);
    ctx->shadow_program = link_program(shaders->shadow_vs, shaders->shadow_fs);
    ctx->postfx_program = link_program(shaders->postfx_vs, shaders->postfx_fs);
    ctx->skybox_program = link_program(shaders->skybox_vs, shaders->skybox_fs);
    return ctx->program && ctx->shadow_program && ctx->postfx_program && ctx->skybox_program;
}

/// @brief Delete shader programs created during context setup.
static void gl_delete_context_programs(gl_context_t *ctx) {
    if (ctx->program)
        gl.DeleteProgram(ctx->program);
    if (ctx->shadow_program)
        gl.DeleteProgram(ctx->shadow_program);
    if (ctx->postfx_program)
        gl.DeleteProgram(ctx->postfx_program);
    if (ctx->skybox_program)
        gl.DeleteProgram(ctx->skybox_program);
    ctx->program = 0;
    ctx->shadow_program = 0;
    ctx->postfx_program = 0;
    ctx->skybox_program = 0;
}

/// @brief Bind default sampler-unit uniforms across main, shadow, skybox and post-FX programs.
static void gl_bind_default_sampler_uniforms(gl_context_t *ctx) {
    gl.UseProgram(ctx->program);
    gl.Uniform1i(ctx->uDiffuseTex, 0);
    gl.Uniform1i(ctx->uNormalTex, 1);
    gl.Uniform1i(ctx->uSpecularTex, 2);
    gl.Uniform1i(ctx->uEmissiveTex, 3);
    for (int32_t slot = 0; slot < VGFX3D_MAX_SHADOW_LIGHTS; slot++)
        gl.Uniform1i(ctx->uShadowTex[slot], GL_TU_SHADOW0 + slot);
    gl.Uniform1i(ctx->uSplatTex, GL_TU_SPLAT_CONTROL);
    gl.Uniform1i(ctx->uSplatLayer0, GL_TU_SPLAT_LAYER0);
    gl.Uniform1i(ctx->uSplatLayer1, GL_TU_SPLAT_LAYER0 + 1);
    gl.Uniform1i(ctx->uSplatLayer2, GL_TU_SPLAT_LAYER0 + 2);
    gl.Uniform1i(ctx->uSplatLayer3, GL_TU_SPLAT_LAYER0 + 3);
    gl.Uniform1i(ctx->uMorphDeltas, GL_TU_MORPH_DELTAS);
    gl.Uniform1i(ctx->uMorphNormalDeltas, GL_TU_MORPH_NORMAL_DELTAS);
    gl.Uniform1i(ctx->uEnvMap, GL_TU_ENV_MAP);
    gl.Uniform1i(ctx->uMetallicRoughnessTex, GL_TU_METALLIC_ROUGHNESS);
    gl.Uniform1i(ctx->uAOTex, GL_TU_AO);

    gl.UseProgram(ctx->shadow_program);
    gl.Uniform1i(ctx->shadow_uMorphDeltas, GL_TU_MORPH_DELTAS);
    gl.Uniform1i(ctx->shadow_uDiffuseTex, 0);

    gl.UseProgram(ctx->skybox_program);
    gl.Uniform1i(ctx->skybox_uSkybox, 13);

    gl.UseProgram(ctx->postfx_program);
    gl.Uniform1i(ctx->postfx_uSceneTex, 0);
    gl.Uniform1i(ctx->postfx_uSceneDepthTex, 1);
    gl.Uniform1i(ctx->postfx_uSceneMotionTex, 2);
}

/// @brief Bind uniform-block indices for current/previous bone palettes.
static void gl_bind_context_uniform_blocks(gl_context_t *ctx) {
    GLuint main_block = gl.GetUniformBlockIndex(ctx->program, "Bones");
    GLuint prev_main_block = gl.GetUniformBlockIndex(ctx->program, "PrevBones");
    GLuint shadow_block = gl.GetUniformBlockIndex(ctx->shadow_program, "Bones");
    if (main_block != GL_INVALID_INDEX)
        gl.UniformBlockBinding(ctx->program, main_block, 0);
    if (prev_main_block != GL_INVALID_INDEX)
        gl.UniformBlockBinding(ctx->program, prev_main_block, 1);
    if (shadow_block != GL_INVALID_INDEX)
        gl.UniformBlockBinding(ctx->shadow_program, shadow_block, 0);
}

/// @brief Generate VAOs, buffers, UBOs, and morph texture-buffer handles for the context.
static void gl_generate_context_objects(gl_context_t *ctx) {
    gl.GenVertexArrays(1, &ctx->vao);
    gl.GenVertexArrays(1, &ctx->fullscreen_vao);
    gl.GenVertexArrays(1, &ctx->skybox_vao);
    gl.GenBuffers(1, &ctx->fullscreen_vbo);
    gl.GenBuffers(1, &ctx->mesh_vbo);
    gl.GenBuffers(1, &ctx->mesh_ibo);
    gl.GenBuffers(1, &ctx->instance_vbo);
    gl.GenBuffers(1, &ctx->prev_instance_vbo);
    gl.GenBuffers(1, &ctx->bone_ubo);
    gl.GenBuffers(1, &ctx->prev_bone_ubo);
    gl.GenBuffers(1, &ctx->morph_buffer);
    gl.GenBuffers(1, &ctx->morph_normal_buffer);
    gl.GenBuffers(1, &ctx->skybox_vbo);
    gl.GenTextures(1, &ctx->morph_tbo);
    gl.GenTextures(1, &ctx->morph_normal_tbo);
}

/// @brief Allocate the dynamic streaming VBO/IBO, instance, morph, and bone buffers.
static void gl_init_streaming_buffers(gl_context_t *ctx) {
    gl.BindBuffer(GL_ARRAY_BUFFER, ctx->mesh_vbo);
    gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(4 * 1024 * 1024), NULL, GL_STREAM_DRAW);
    ctx->mesh_vbo_capacity = 4u * 1024u * 1024u;
    gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ctx->mesh_ibo);
    gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(1 * 1024 * 1024), NULL, GL_STREAM_DRAW);
    ctx->mesh_ibo_capacity = 1u * 1024u * 1024u;
    gl.BindBuffer(GL_ARRAY_BUFFER, ctx->instance_vbo);
    gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(64 * 1024), NULL, GL_STREAM_DRAW);
    ctx->instance_vbo_capacity = 64u * 1024u;
    gl.BindBuffer(GL_ARRAY_BUFFER, ctx->prev_instance_vbo);
    gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(64 * 1024), NULL, GL_STREAM_DRAW);
    ctx->prev_instance_vbo_capacity = 64u * 1024u;
    gl.BindBuffer(GL_TEXTURE_BUFFER, ctx->morph_buffer);
    gl.BufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)(64 * 1024), NULL, GL_STREAM_DRAW);
    ctx->morph_capacity_bytes = 64u * 1024u;
    gl.BindBuffer(GL_TEXTURE_BUFFER, ctx->morph_normal_buffer);
    gl.BufferData(GL_TEXTURE_BUFFER, (GLsizeiptr)(64 * 1024), NULL, GL_STREAM_DRAW);
    ctx->morph_normal_capacity_bytes = 64u * 1024u;

    gl.BindBuffer(GL_UNIFORM_BUFFER, ctx->bone_ubo);
    gl.BufferData(
        GL_UNIFORM_BUFFER, (GLsizeiptr)VGFX3D_OPENGL_BONE_PALETTE_BYTES, NULL, GL_DYNAMIC_DRAW);
    gl.BindBufferBase(GL_UNIFORM_BUFFER, 0, ctx->bone_ubo);
    gl.BindBuffer(GL_UNIFORM_BUFFER, ctx->prev_bone_ubo);
    gl.BufferData(
        GL_UNIFORM_BUFFER, (GLsizeiptr)VGFX3D_OPENGL_BONE_PALETTE_BYTES, NULL, GL_DYNAMIC_DRAW);
    gl.BindBufferBase(GL_UNIFORM_BUFFER, 1, ctx->prev_bone_ubo);
}

/// @brief Upload immutable fullscreen-triangle and skybox vertex buffers.
static void gl_init_static_geometry_buffers(gl_context_t *ctx) {
    gl.BindVertexArray(ctx->fullscreen_vao);
    gl.BindBuffer(GL_ARRAY_BUFFER, ctx->fullscreen_vbo);
    gl.BufferData(GL_ARRAY_BUFFER,
                  (GLsizeiptr)sizeof(gl_fullscreen_triangle),
                  gl_fullscreen_triangle,
                  GL_STATIC_DRAW);
    gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * (GLsizei)sizeof(float), (void *)0);
    gl.EnableVertexAttribArray(0);

    gl.BindVertexArray(ctx->skybox_vao);
    gl.BindBuffer(GL_ARRAY_BUFFER, ctx->skybox_vbo);
    gl.BufferData(GL_ARRAY_BUFFER,
                  (GLsizeiptr)sizeof(gl_skybox_vertices),
                  gl_skybox_vertices,
                  GL_STATIC_DRAW);
    gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * (GLsizei)sizeof(float), (void *)0);
    gl.EnableVertexAttribArray(0);
}

/// @brief Initialize fixed GL render state used before per-draw overrides.
static void gl_init_default_render_state(void) {
    gl.Enable(GL_DEPTH_TEST);
    gl.DepthFunc(GL_LESS);
    gl.Enable(GL_CULL_FACE);
    gl.CullFace(GL_BACK);
    gl.FrontFace(GL_CCW);
    gl.Disable(GL_BLEND);
    gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl.PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

/// @brief Allocate and initialize the entire OpenGL backend context.
///
/// Creates: GLX context (3.3 core profile preferred, fallback to
/// any), shader programs (main, shadow, skybox, postfx), VAOs +
/// VBOs (mesh, instance, fullscreen-triangle, skybox cube), bone
/// UBOs, morph TBOs. Returns NULL on any failure path; caller falls
/// back to the software backend.
///
/// FBConfig selection picks the first config matching the window's
/// X visual to avoid drawable-incompatibility errors at MakeCurrent.
static void *gl_create_ctx(vgfx_window_t win, int32_t w, int32_t h) {
    gl_shader_set_t shaders;
    if (load_gl() != 0)
        return NULL;

    void *native = vgfx_get_native_view(win);
    if (!native)
        return NULL;
    Window xwin = (Window)(uintptr_t)native;
    Display *dpy = (Display *)vgfx_get_native_display(win);
    if (!dpy)
        return NULL;

    int fb_attribs[] = {GLX_RENDER_TYPE,
                        GLX_RGBA_BIT,
                        GLX_DRAWABLE_TYPE,
                        GLX_WINDOW_BIT,
                        GLX_DOUBLEBUFFER,
                        1,
                        GLX_RED_SIZE,
                        8,
                        GLX_GREEN_SIZE,
                        8,
                        GLX_BLUE_SIZE,
                        8,
                        GLX_ALPHA_SIZE,
                        8,
                        GLX_DEPTH_SIZE,
                        24,
                        0};
    int fb_count = 0;
    GLXFBConfig *configs = glx.ChooseFBConfig(dpy, DefaultScreen(dpy), fb_attribs, &fb_count);
    if (!configs || fb_count == 0)
        return NULL;

    GLXFBConfig chosen = gl_pick_matching_fb_config(dpy, xwin, configs, fb_count);
    GLXContext glxCtx = NULL;
    if (glx.CreateContextAttribsARB) {
        const int ctx_attribs[] = {GLX_CONTEXT_MAJOR_VERSION_ARB,
                                   3,
                                   GLX_CONTEXT_MINOR_VERSION_ARB,
                                   3,
                                   GLX_CONTEXT_PROFILE_MASK_ARB,
                                   GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
                                   0};
        glxCtx = glx.CreateContextAttribsARB(dpy, chosen, NULL, 1, ctx_attribs);
    }
    if (!glxCtx)
        glxCtx = glx.CreateNewContext(dpy, chosen, GLX_RGBA_TYPE, NULL, 1);
    XFree(configs);
    if (!glxCtx)
        return NULL;
    glx.MakeCurrent(dpy, xwin, glxCtx);

    gl_context_t *ctx = (gl_context_t *)calloc(1, sizeof(gl_context_t));
    if (!ctx) {
        glx.DestroyContext(dpy, glxCtx);
        return NULL;
    }
    ctx->display = dpy;
    ctx->window = xwin;
    ctx->glxCtx = glxCtx;
    gl.Enable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    gl_init_context_defaults(ctx, w, h);
    gl_query_anisotropy_support(ctx);

    if (!gl_compile_context_shaders(&shaders)) {
        gl_delete_context_shaders(&shaders);
        glx.DestroyContext(dpy, glxCtx);
        free(ctx);
        return NULL;
    }

    if (!gl_link_context_programs(ctx, &shaders)) {
        gl_delete_context_programs(ctx);
        gl_delete_context_shaders(&shaders);
        glx.DestroyContext(dpy, glxCtx);
        free(ctx);
        return NULL;
    }
    gl_delete_context_shaders(&shaders);

    query_main_uniforms(ctx);
    query_shadow_uniforms(ctx);
    query_skybox_uniforms(ctx);
    query_postfx_uniforms(ctx);

    gl_bind_default_sampler_uniforms(ctx);
    gl_bind_context_uniform_blocks(ctx);
    gl_generate_context_objects(ctx);
    gl_init_streaming_buffers(ctx);
    gl_init_static_geometry_buffers(ctx);
    gl_init_default_render_state();

    return ctx;
}

/// @brief Tear down the GL backend context — releases everything from `_create_ctx`.
///
/// Order matters: caches first (GL objects depend on the context),
/// then offscreen targets, then the master VAOs/VBOs/UBOs/programs,
/// finally the GLX context itself.
static void gl_destroy_ctx(void *ctx_ptr) {
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    if (!ctx)
        return;

    if (ctx->display && ctx->window && ctx->glxCtx)
        glx.MakeCurrent(ctx->display, ctx->window, ctx->glxCtx);

    texture_cache_destroy(ctx);
    gl_destroy_mesh_cache(ctx);
    destroy_scene_targets(ctx);
    destroy_postfx_readback_target(ctx);
    destroy_rtt_targets(ctx);
    destroy_shadow_targets(ctx);
    vgfx3d_postfx_chain_free(&ctx->gpu_postfx_chain);

    for (int ws = 0; ws < 3; ws++) {
        for (int wt = 0; wt < 3; wt++) {
            for (int filter = 0; filter < 2; filter++) {
                for (int anisotropy = 0; anisotropy < VGFX3D_OPENGL_ANISOTROPY_LEVEL_COUNT;
                     anisotropy++) {
                    if (ctx->material_samplers[ws][wt][filter][anisotropy])
                        gl.DeleteSamplers(1, &ctx->material_samplers[ws][wt][filter][anisotropy]);
                }
            }
        }
    }

    if (ctx->fullscreen_vbo)
        gl.DeleteBuffers(1, &ctx->fullscreen_vbo);
    if (ctx->skybox_vbo)
        gl.DeleteBuffers(1, &ctx->skybox_vbo);
    if (ctx->morph_normal_tbo)
        gl.DeleteTextures(1, &ctx->morph_normal_tbo);
    if (ctx->morph_tbo)
        gl.DeleteTextures(1, &ctx->morph_tbo);
    if (ctx->morph_normal_buffer)
        gl.DeleteBuffers(1, &ctx->morph_normal_buffer);
    if (ctx->bone_ubo)
        gl.DeleteBuffers(1, &ctx->bone_ubo);
    if (ctx->prev_bone_ubo)
        gl.DeleteBuffers(1, &ctx->prev_bone_ubo);
    if (ctx->morph_buffer)
        gl.DeleteBuffers(1, &ctx->morph_buffer);
    if (ctx->instance_vbo)
        gl.DeleteBuffers(1, &ctx->instance_vbo);
    if (ctx->prev_instance_vbo)
        gl.DeleteBuffers(1, &ctx->prev_instance_vbo);
    if (ctx->mesh_vbo)
        gl.DeleteBuffers(1, &ctx->mesh_vbo);
    if (ctx->mesh_ibo)
        gl.DeleteBuffers(1, &ctx->mesh_ibo);
    if (ctx->skybox_vao)
        gl.DeleteVertexArrays(1, &ctx->skybox_vao);
    if (ctx->fullscreen_vao)
        gl.DeleteVertexArrays(1, &ctx->fullscreen_vao);
    if (ctx->vao)
        gl.DeleteVertexArrays(1, &ctx->vao);
    if (ctx->program)
        gl.DeleteProgram(ctx->program);
    if (ctx->shadow_program)
        gl.DeleteProgram(ctx->shadow_program);
    if (ctx->postfx_program)
        gl.DeleteProgram(ctx->postfx_program);
    if (ctx->skybox_program)
        gl.DeleteProgram(ctx->skybox_program);
    if (ctx->glxCtx && ctx->display)
        glx.DestroyContext(ctx->display, ctx->glxCtx);
    free(ctx);
}
