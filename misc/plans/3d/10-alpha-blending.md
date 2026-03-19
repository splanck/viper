# Phase 10: Alpha Blending / Transparency

## Goal

Support transparent and semi-transparent materials with proper depth-sorted rendering. Split the draw pass into opaque (front-to-back, depth write ON) and transparent (back-to-front, depth write OFF, blend ON) sub-passes.

## Dependencies

- Phase 1 complete (software rasterizer)
- Phase 2 complete (backend abstraction)

## Design

Transparency requires two key changes:

1. **Material alpha** — a per-material opacity value [0.0 = invisible, 1.0 = fully opaque]
2. **Draw pass splitting** — opaque objects render first with depth writing enabled; transparent objects render second, sorted back-to-front by centroid distance from camera, with depth writing disabled but depth testing still active

## Modified Files

#### Runtime Level

**`rt_material3d.h` / `rt_material3d.c`** (~+50 LOC)

Add to struct:
```c
double alpha;  // opacity [0.0, 1.0], default 1.0
```

New API:
```c
void   rt_material3d_set_alpha(void *obj, double alpha);
double rt_material3d_get_alpha(void *obj);
```

#### Library Level

**`vgfx3d.c`** (~+150 LOC)

Modified `vgfx3d_end()`:
```c
void vgfx3d_end(vgfx3d_context_t *ctx) {
    // 1. Partition draw commands into opaque (alpha >= 1.0) and transparent
    // 2. Sort opaque front-to-back (optional optimization for early-Z)
    // 3. Submit opaque draws: depth write ON, blend OFF
    // 4. Sort transparent back-to-front by centroid distance from camera
    //    centroid = average of transformed triangle positions
    // 5. Submit transparent draws: depth write OFF, depth test ON, blend ON
}
```

**`vgfx3d_raster.c`** (~+50 LOC)

Per-fragment alpha blending (software path):
```c
// When alpha < 1.0:
uint8_t *dst = &fb.pixels[y * fb.stride + x * 4];
float src_a = frag_color.a;
float inv_a = 1.0f - src_a;
dst[0] = (uint8_t)(frag_color.r * 255.0f * src_a + dst[0] * inv_a);
dst[1] = (uint8_t)(frag_color.g * 255.0f * src_a + dst[1] * inv_a);
dst[2] = (uint8_t)(frag_color.b * 255.0f * src_a + dst[2] * inv_a);
dst[3] = 0xFF;  // framebuffer alpha stays opaque
// Do NOT update Z-buffer for transparent fragments

// Per-texel alpha: when a diffuse texture is bound, multiply material alpha by texel alpha:
//   float final_alpha = material_alpha * texel.a / 255.0f;
// This enables texture-driven transparency (foliage, fences, particles).

// Alpha test (binary transparency): discard fragments below threshold without sorting:
//   if (final_alpha < alpha_test_threshold) continue;  // default threshold: 0.5
// Alpha test is cheaper than full blending for hard-edged cutouts (tree leaves,
// chain-link fences) since it avoids the back-to-front sorting requirement.
// Material3D.SetAlphaTest(threshold) enables this mode.
```

**`vgfx3d_draw_cmd_t`** — add `float alpha` field (copied from material)

## Backend Interface Additions

Add to `vgfx3d_backend_t`:

```c
void (*set_blend_state)(vgfx3d_context_t *ctx, int enabled);
void (*set_depth_write)(vgfx3d_context_t *ctx, int enabled);
```

**Per-backend implementation:**

| Backend | Blend Enable | Blend Func | Depth Write |
|---------|-------------|------------|-------------|
| Metal | `colorAttachments[0].blendingEnabled = YES` | `sourceRGBBlendFactor = .sourceAlpha`, `destinationRGBBlendFactor = .oneMinusSourceAlpha` | `depthStencilDescriptor.isDepthWriteEnabled` |
| D3D11 | `D3D11_BLEND_DESC.RenderTarget[0].BlendEnable = TRUE` | `SrcBlend = D3D11_BLEND_SRC_ALPHA`, `DestBlend = D3D11_BLEND_INV_SRC_ALPHA` | `D3D11_DEPTH_STENCIL_DESC.DepthWriteMask` |
| OpenGL | `glEnable(GL_BLEND)` | `glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)` | `glDepthMask(GL_TRUE/GL_FALSE)` |

## Sorting Strategy

For transparent objects, sort by **centroid distance from camera eye**:

```c
// Compute centroid in world space (average of all vertex positions after model transform)
float cx = 0, cy = 0, cz = 0;
for (uint32_t i = 0; i < cmd->vertex_count; i++) {
    // transform vertex by model matrix, accumulate
}
cx /= cmd->vertex_count; cy /= cmd->vertex_count; cz /= cmd->vertex_count;

// Distance from camera eye
float dx = cx - cam->position[0];
float dy = cy - cam->position[1];
float dz = cz - cam->position[2];
cmd->sort_key = dx*dx + dy*dy + dz*dz;  // squared distance (no sqrt needed)
```

Sort transparent commands by `sort_key` descending (farthest first).

## runtime.def Additions

```c
RT_FUNC(Material3DSetAlpha, rt_material3d_set_alpha, "Viper.Graphics3D.Material3D.set_Alpha", "void(obj,f64)")
RT_FUNC(Material3DGetAlpha, rt_material3d_get_alpha, "Viper.Graphics3D.Material3D.get_Alpha", "f64(obj)")

// Add to Material3D class:
//   RT_PROP("Alpha", "f64", Material3DGetAlpha, Material3DSetAlpha)
```

## Stubs

```c
void   rt_material3d_set_alpha(void *obj, double alpha) { (void)obj; (void)alpha; }
double rt_material3d_get_alpha(void *obj) { (void)obj; return 1.0; }
```

## Usage Example (Zia)

```rust
var glass = Material3D.NewColor(0.8, 0.9, 1.0)
glass.Alpha = 0.3  // 30% opaque glass

var floor = Material3D.NewColor(0.5, 0.5, 0.5)
// floor.Alpha defaults to 1.0 (fully opaque)

canvas.Begin(cam)
canvas.DrawMesh(floorMesh, floorTransform, floor)      // opaque — drawn first
canvas.DrawMesh(windowMesh, windowTransform, glass)     // transparent — drawn after, blended
canvas.End()
```

## Tests (10)

| Test | Description |
|------|-------------|
| Opaque occludes transparent | Opaque in front of transparent → transparent not visible |
| Transparent blends with background | Alpha 0.5 red over blue background → purple |
| Multi-transparent depth order | 3 transparent planes at different Z → correct back-to-front compositing |
| Alpha 0.0 invisible | Fully transparent material not visible |
| Alpha 1.0 opaque | Alpha=1.0 behaves identically to default (no blending) |
| Depth test still works | Transparent behind opaque → not visible (depth test ON) |
| Opaque before transparent | Verify draw order: all opaques submitted before any transparents |
| Sort stability | Equal-distance transparents render in submission order |
| Software vs GPU parity | Same blended scene, compare outputs |
| Mixed opaque/transparent scene | Complex scene with interleaved opaque and transparent meshes |
| Per-texel alpha | Texture with alpha=0 pixels creates holes in surface |
| Alpha test threshold | AlphaTest(0.5) discards pixels below threshold without sorting |
