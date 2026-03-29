# OGL-10: Render-To-Texture

## Current State

`gl_set_render_target()` is still a stub. The shared runtime already routes render-target binding through [`rt_rendertarget3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_rendertarget3d.c), so the missing work is backend-local FBO ownership plus one shared behavior clarification.

## Shared Prerequisite

Canvas3D currently draws the skybox directly into `render_target->color_buf` when a CPU render target is bound. A GPU render-to-texture path will overwrite that buffer during readback unless this is addressed.

Before OGL-10 is considered complete, pick and implement one of these behaviors:

1. move skybox rendering for GPU RTT into the backend, or
2. skip the current CPU skybox write when a GPU backend owns the render target

The plan should not silently accept skybox loss.

## Backend State

Add to `gl_context_t`:

```c
GLuint rtt_fbo;
GLuint rtt_color_tex;
GLuint rtt_depth_rbo;
int32_t rtt_width, rtt_height;
int8_t rtt_active;
vgfx3d_rendertarget_t *rtt_target;
```

## `set_render_target()`

Implement `gl_set_render_target()` so that:

- `rt != NULL`
  - destroys any previous RTT resources
  - creates a framebuffer
  - creates a color texture attachment (`GL_RGBA8`)
  - creates a depth renderbuffer attachment (`GL_DEPTH_COMPONENT32F`)
  - validates `GL_FRAMEBUFFER_COMPLETE`
  - stores `rtt_target`, `rtt_width`, `rtt_height`, `rtt_active`
- `rt == NULL`
  - binds the default framebuffer
  - destroys RTT resources
  - clears RTT state

## Frame Ownership

`gl_begin_frame()` must bind the correct framebuffer and viewport:

- RTT active:
  - bind `ctx->rtt_fbo`
  - set viewport to `rtt_width`/`rtt_height`
- RTT inactive:
  - bind framebuffer `0`
  - set viewport to window size

`gl_end_frame()` must:

- read the RTT color texture back into `rt->color_buf` with `glReadPixels`
- vertically flip the readback because OpenGL's origin is bottom-left
- avoid swapping the window backbuffer; presentation still belongs to `present()`

No public API currently consumes render-target depth on the CPU side, so color readback is the required correctness path.

## Loader And Constant Additions

Load:

- `GenFramebuffers`
- `DeleteFramebuffers`
- `BindFramebuffer`
- `CheckFramebufferStatus`
- `FramebufferTexture2D`
- `GenRenderbuffers`
- `DeleteRenderbuffers`
- `BindRenderbuffer`
- `RenderbufferStorage`
- `FramebufferRenderbuffer`
- `ReadPixels`

Add constants for:

- `GL_FRAMEBUFFER`
- `GL_RENDERBUFFER`
- `GL_COLOR_ATTACHMENT0`
- `GL_DEPTH_ATTACHMENT`
- `GL_FRAMEBUFFER_COMPLETE`

## Files

- [`src/runtime/graphics/vgfx3d_backend_opengl.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_opengl.c)
- shared follow-up in [`src/runtime/graphics/rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c) for skybox/RTT behavior

## Done When

- Rendering to `RenderTarget3D` produces a correct `AsPixels()` result
- Switching between offscreen and onscreen rendering works repeatedly
- GPU RTT does not silently drop the skybox path
