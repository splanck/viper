# D3D-09: Render-To-Texture

## Current State

`d3d11_set_render_target()` is still a stub. The shared runtime already routes render-target binding through [`rt_rendertarget3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_rendertarget3d.c), so the missing work is backend-local resource ownership plus one shared behavior clarification.

## Shared Prerequisite

Canvas3D currently draws the skybox directly into `render_target->color_buf` when a CPU render target is bound. A GPU RTT path will overwrite that buffer during readback unless this is addressed.

Before D3D-09 is considered complete, pick and implement one of these behaviors:

1. move skybox rendering for GPU RTT into the backend, or
2. skip the current CPU skybox write when a GPU backend owns the render target

The plan should not silently accept skybox loss.

## Backend State

Add to `d3d11_context_t`:

```c
ID3D11Texture2D *rtt_color_tex;
ID3D11RenderTargetView *rtt_rtv;
ID3D11Texture2D *rtt_depth_tex;
ID3D11DepthStencilView *rtt_dsv;
ID3D11Texture2D *rtt_staging;
int32_t rtt_width, rtt_height;
int8_t rtt_active;
vgfx3d_rendertarget_t *rtt_target;
```

## `set_render_target()`

Implement `d3d11_set_render_target()` so that:

- `rt != NULL`
  - releases any previous RTT resources
  - creates a color texture + RTV
  - creates a depth texture + DSV
  - creates a staging texture for CPU readback
  - stores `rtt_target`, dimensions, and `rtt_active`
- `rt == NULL`
  - releases RTT resources
  - clears RTT state

Use `DXGI_FORMAT_R8G8B8A8_UNORM` for the color texture so the staging readback can be copied row-by-row into the runtime's RGBA byte buffer without an additional channel swizzle.

## Frame Ownership

`d3d11_begin_frame()` must choose the correct render target and viewport:

- RTT active:
  - `OMSetRenderTargets(..., ctx->rtt_rtv, ctx->rtt_dsv)`
  - viewport = `rtt_width` / `rtt_height`
- RTT inactive:
  - `OMSetRenderTargets(..., ctx->rtv, ctx->dsv)`
  - viewport = window size

`d3d11_end_frame()` must:

- copy the RTT color texture into the staging texture
- map the staging texture for readback
- copy rows into `rt->color_buf`

No public API currently consumes render-target depth on the CPU side, so color readback is the required correctness path.

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)
- shared follow-up in [`src/runtime/graphics/rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c) for skybox/RTT behavior

## Done When

- `RenderTarget3D.AsPixels()` returns the rendered image
- switching between offscreen and onscreen rendering works repeatedly
- GPU RTT does not silently drop the skybox path
