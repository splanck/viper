# OGL-10: Render-to-Texture

## Context
`gl_set_render_target()` is an empty stub. Need FBO (framebuffer object) for offscreen rendering.

Binding and unbinding already route through [`src/runtime/graphics/rt_rendertarget3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_rendertarget3d.c). The OpenGL work here is implementing the backend stub and managing FBO lifetime sensibly.

## Implementation

### Step 1: Add FBO state to context
```c
// Add to gl_context_t:
GLuint rtt_fbo;
GLuint rtt_color_tex;
GLuint rtt_depth_rbo;
int32_t rtt_width, rtt_height;
int8_t rtt_active;
vgfx3d_rendertarget_t *rtt_target;
```

### Step 2: Implement set_render_target
```c
static void gl_set_render_target(void *ctx_ptr, vgfx3d_rendertarget_t *rt) {
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    if (rt) {
        // Release old FBO resources before creating new ones (prevents leaks on re-bind)
        if (ctx->rtt_fbo) {
            gl.DeleteFramebuffers(1, &ctx->rtt_fbo);
            gl.DeleteTextures(1, &ctx->rtt_color_tex);
            gl.DeleteRenderbuffers(1, &ctx->rtt_depth_rbo);
        }
        // Create FBO
        gl.GenFramebuffers(1, &ctx->rtt_fbo);
        gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->rtt_fbo);

        // Color texture attachment
        gl.GenTextures(1, &ctx->rtt_color_tex);
        gl.BindTexture(GL_TEXTURE_2D, ctx->rtt_color_tex);
        gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rt->width, rt->height, 0,
                      GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_TEXTURE_2D, ctx->rtt_color_tex, 0);

        // Depth renderbuffer attachment
        gl.GenRenderbuffers(1, &ctx->rtt_depth_rbo);
        gl.BindRenderbuffer(GL_RENDERBUFFER, ctx->rtt_depth_rbo);
        gl.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F,
                               rt->width, rt->height);
        gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                   GL_RENDERBUFFER, ctx->rtt_depth_rbo);

        // Check completeness
        GLenum status = gl.CheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            // Log error, clean up
        }

        ctx->rtt_width = rt->width;
        ctx->rtt_height = rt->height;
        ctx->rtt_active = 1;
        ctx->rtt_target = rt;
    } else {
        // Unbind FBO → back to default framebuffer
        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        if (ctx->rtt_fbo) {
            gl.DeleteFramebuffers(1, &ctx->rtt_fbo);
            gl.DeleteTextures(1, &ctx->rtt_color_tex);
            gl.DeleteRenderbuffers(1, &ctx->rtt_depth_rbo);
        }
        ctx->rtt_fbo = 0;
        ctx->rtt_active = 0;
        ctx->rtt_target = NULL;
    }
}
```

### Step 3: Use FBO in begin_frame
```c
if (ctx->rtt_active && ctx->rtt_fbo) {
    gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->rtt_fbo);
    gl.Viewport(0, 0, ctx->rtt_width, ctx->rtt_height);
} else {
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    // normal viewport
}
```

### Step 4: Readback in end_frame
```c
if (ctx->rtt_active && ctx->rtt_target) {
    gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->rtt_fbo);
    uint8_t *dst = ctx->rtt_target->color_buf;
    gl.ReadPixels(0, 0, ctx->rtt_width, ctx->rtt_height,
                  GL_RGBA, GL_UNSIGNED_BYTE, dst);
    // Flip vertically (OpenGL origin is bottom-left)
    // ... row swap loop ...
}
```

### Step 5: Load FBO GL functions
```c
LOAD(GenFramebuffers);
LOAD(DeleteFramebuffers);
LOAD(BindFramebuffer);
LOAD(FramebufferTexture2D);
LOAD(FramebufferRenderbuffer);
LOAD(CheckFramebufferStatus);
LOAD(GenRenderbuffers);
LOAD(DeleteRenderbuffers);
LOAD(BindRenderbuffer);
LOAD(RenderbufferStorage);
LOAD(ReadPixels);
```

All core GL 3.3.

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_opengl.c` — FBO state, set_render_target, begin_frame FBO binding, end_frame readback, GL function loading
- `src/runtime/graphics/rt_rendertarget3d.c` — no API change expected; only verify it remains the single binding path

## Testing
- Render to 256x256 FBO → AsPixels returns correct image
- Switch between FBO and screen → both correct
- Depth testing in FBO → occluded objects hidden
