# Phase 8: Render-to-Texture / Offscreen Rendering

## Goal

Enable rendering 3D scenes to offscreen targets instead of the window framebuffer. This is the foundational capability required by shadow maps, reflections, and post-processing (Phase 18).

## Dependencies

- Phase 1 complete (software renderer)
- Phase 2 complete (backend abstraction — `vgfx3d_backend_t` vtable)

## Architecture

```
Canvas3D.SetRenderTarget(target)
  → bind offscreen color + depth buffers
Canvas3D.Begin(camera) / DrawMesh / End()
  → all rendering goes to offscreen target
Canvas3D.ResetRenderTarget()
  → subsequent rendering goes to window

target.AsPixels()
  → read back offscreen color buffer as Pixels (CPU access)

// Use as texture input to another pass:
material.SetTexture(target.AsPixels())
  → renders the offscreen result onto geometry
```

## New Files

#### Library Level (`src/lib/graphics/src/`)

**`vgfx3d_rendertarget.h`** (~60 LOC)
```c
typedef struct {
    uint8_t *color_buf;    // RGBA pixels (software path)
    float   *depth_buf;    // float depth buffer
    int32_t  width, height;
    int32_t  stride;       // width * 4
    void    *gpu_handle;   // backend-specific opaque pointer
    int8_t   bound;        // currently active as render target
} vgfx3d_rendertarget_t;

vgfx3d_rendertarget_t *vgfx3d_rt_create(int32_t w, int32_t h);
void vgfx3d_rt_destroy(vgfx3d_rendertarget_t *rt);
void vgfx3d_rt_clear(vgfx3d_rendertarget_t *rt, float r, float g, float b);
void vgfx3d_rt_clear_depth(vgfx3d_rendertarget_t *rt);
```

**`vgfx3d_rendertarget.c`** (~200 LOC) — Software backend
- `vgfx3d_rt_create`: malloc color buffer (`width * height * 4`) + depth buffer (`width * height * sizeof(float)`)
- `vgfx3d_rt_clear`: memset color, fill depth with `FLT_MAX`
- `vgfx3d_rt_destroy`: free both buffers
- When bound, the software rasterizer writes to `rt->color_buf` / `rt->depth_buf` instead of the window framebuffer

**`vgfx3d_rendertarget_metal.m`** (~150 LOC)
- Create `MTLTextureDescriptor` with `.usage = [.renderTarget, .shaderRead]`
- Color texture: `MTLPixelFormatRGBA8Unorm`
- Depth texture: `MTLPixelFormatDepth32Float`
- Bind: set as `MTLRenderPassDescriptor.colorAttachments[0].texture`
- As texture input: use the color MTLTexture directly as a shader resource
- Read back: `getBytes:bytesPerRow:fromRegion:mipmapLevel:` for AsPixels

**`vgfx3d_rendertarget_d3d11.c`** (~150 LOC)
- Create `ID3D11Texture2D` with `D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE`
- Create `ID3D11RenderTargetView` for rendering into it
- Create `ID3D11ShaderResourceView` for sampling from it
- Depth: separate `ID3D11Texture2D` with `DXGI_FORMAT_D32_FLOAT`
- Bind: `OMSetRenderTargets` with RT's RTV + DSV
- Read back: `Map` staging texture, `CopyResource` from RT

**`vgfx3d_rendertarget_opengl.c`** (~150 LOC)
- Create FBO: `glGenFramebuffers`, attach color texture (`GL_COLOR_ATTACHMENT0`) and depth renderbuffer (`GL_DEPTH_ATTACHMENT`)
- Color texture: `GL_RGBA8`, `GL_TEXTURE_2D`
- Depth: `GL_DEPTH_COMPONENT32F` renderbuffer
- Bind: `glBindFramebuffer(GL_FRAMEBUFFER, fbo)`
- Unbind: `glBindFramebuffer(GL_FRAMEBUFFER, 0)` returns to default
- As texture: bind the color texture to a sampler slot
- Read back: `glReadPixels` with `GL_RGBA`

#### Runtime Level (`src/runtime/graphics/`)

**`rt_rendertarget3d.h` / `rt_rendertarget3d.c`** (~240 LOC)
```c
typedef struct {
    void *vptr;
    vgfx3d_rendertarget_t *target;
    int64_t width, height;
} rt_rendertarget3d;

// Construction
void *rt_rendertarget3d_new(int64_t width, int64_t height);

// Properties
int64_t rt_rendertarget3d_get_width(void *obj);
int64_t rt_rendertarget3d_get_height(void *obj);

// Read back as Pixels
void *rt_rendertarget3d_as_pixels(void *obj);

// Canvas3D integration
void rt_canvas3d_set_render_target(void *canvas, void *target);
void rt_canvas3d_reset_render_target(void *canvas);
```

## Backend Interface Additions

Add to `vgfx3d_backend_t` (in `vgfx3d_backend.h`):

```c
void *(*create_render_target)(vgfx3d_context_t *ctx, int32_t w, int32_t h);
void  (*destroy_render_target)(vgfx3d_context_t *ctx, void *rt);
void  (*bind_render_target)(vgfx3d_context_t *ctx, void *rt);  // NULL = default window
void *(*render_target_as_texture)(vgfx3d_context_t *ctx, void *rt);  // returns backend texture handle
void  (*read_render_target)(vgfx3d_context_t *ctx, void *rt, uint8_t *out_pixels);  // GPU → CPU
```

## GC Finalizer

```c
static void rt_rendertarget3d_finalize(void *obj) {
    rt_rendertarget3d *rt = (rt_rendertarget3d *)obj;
    if (rt->target) {
        // Backend-specific GPU cleanup happens inside vgfx3d_rt_destroy
        vgfx3d_rt_destroy(rt->target);
        rt->target = NULL;
    }
}
```

## runtime.def Entries

```c
RT_FUNC(RenderTarget3DNew,            rt_rendertarget3d_new,            "Viper.Graphics3D.RenderTarget3D.New",            "obj(i64,i64)")
RT_FUNC(RenderTarget3DGetWidth,       rt_rendertarget3d_get_width,      "Viper.Graphics3D.RenderTarget3D.get_Width",      "i64(obj)")
RT_FUNC(RenderTarget3DGetHeight,      rt_rendertarget3d_get_height,     "Viper.Graphics3D.RenderTarget3D.get_Height",     "i64(obj)")
RT_FUNC(RenderTarget3DAsPixels,       rt_rendertarget3d_as_pixels,      "Viper.Graphics3D.RenderTarget3D.AsPixels",       "obj(obj)")
RT_FUNC(Canvas3DSetRenderTarget,      rt_canvas3d_set_render_target,    "Viper.Graphics3D.Canvas3D.SetRenderTarget",      "void(obj,obj)")
RT_FUNC(Canvas3DResetRenderTarget,    rt_canvas3d_reset_render_target,  "Viper.Graphics3D.Canvas3D.ResetRenderTarget",    "void(obj)")

RT_CLASS_BEGIN("Viper.Graphics3D.RenderTarget3D", RenderTarget3D, "obj", RenderTarget3DNew)
    RT_PROP("Width", "i64", RenderTarget3DGetWidth, none)
    RT_PROP("Height", "i64", RenderTarget3DGetHeight, none)
    RT_METHOD("AsPixels", "obj()", RenderTarget3DAsPixels)
RT_CLASS_END()

// Also add to Canvas3D class:
//   RT_METHOD("SetRenderTarget", "void(obj)", Canvas3DSetRenderTarget)
//   RT_METHOD("ResetRenderTarget", "void()", Canvas3DResetRenderTarget)
```

## Stubs (VIPER_ENABLE_GRAPHICS=OFF)

```c
void *rt_rendertarget3d_new(int64_t w, int64_t h) {
    (void)w; (void)h;
    rt_trap("RenderTarget3D.New: graphics support not compiled in");
    return NULL;
}
int64_t rt_rendertarget3d_get_width(void *obj) { (void)obj; return 0; }
int64_t rt_rendertarget3d_get_height(void *obj) { (void)obj; return 0; }
void *rt_rendertarget3d_as_pixels(void *obj) { (void)obj; return NULL; }
void rt_canvas3d_set_render_target(void *c, void *t) { (void)c; (void)t; }
void rt_canvas3d_reset_render_target(void *c) { (void)c; }
```

## Usage Example (Zia)

```rust
// Render scene to offscreen target, then use as texture on a TV screen mesh
var rt = RenderTarget3D.New(256, 256)
var tvMesh = Mesh3D.NewPlane(2.0, 1.5)
var tvMat = Material3D.New()

// Render the 3D scene to the offscreen target
canvas.SetRenderTarget(rt)
canvas.Clear(0.0, 0.0, 0.0)
canvas.Begin(sceneCam)
canvas.DrawMesh(worldMesh, worldTransform, worldMat)
canvas.End()

// Switch back to window, use RT as texture on the TV
canvas.ResetRenderTarget()
tvMat.SetTexture(rt.AsPixels())
canvas.Clear(0.1, 0.1, 0.1)
canvas.Begin(mainCam)
canvas.DrawMesh(tvMesh, tvTransform, tvMat)
canvas.End()
canvas.Flip()
```

## Tests (12)

| Test | Description |
|------|-------------|
| Create/destroy | Allocate RT, verify non-null, destroy cleanly |
| Dimensions | Width/Height properties match constructor args |
| Render then read back | Render colored triangle to RT, AsPixels, verify pixel colors |
| Depth independence | Two RTs have independent depth buffers |
| Bind/unbind chain | SetRenderTarget → draw → ResetRenderTarget → draw, verify separate outputs |
| NULL reset | ResetRenderTarget returns to window framebuffer |
| RT as texture input | Render to RT, use as texture on quad in main pass, verify visible |
| Clear RT | Clear with specific color, read back, verify all pixels match |
| Multiple RTs | Create 3 RTs, render different scenes to each, verify independence |
| Zero-size rejection | RT.New(0, 0) should trap |
| Large RT | RT.New(4096, 4096) succeeds (within reason) |
| GPU read back timing | Render on GPU, read back, data is correct (not stale) |

## Build Changes

**`src/lib/graphics/CMakeLists.txt`:**
- Add `vgfx3d_rendertarget.c` to all-platform sources
- Add `vgfx3d_rendertarget_metal.m` (macOS), `vgfx3d_rendertarget_d3d11.c` (Windows), `vgfx3d_rendertarget_opengl.c` (Linux)

**`src/runtime/CMakeLists.txt`:**
- Add `rt_rendertarget3d.c` to `RT_GRAPHICS_SOURCES`
