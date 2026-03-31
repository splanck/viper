# Fix 108: Shadow Mapping Slope-Dependent Bias

## Severity: P1 — High

## Problem

The software renderer's shadow mapping (`vgfx3d_backend_sw.c:795`) uses a single uniform
bias for all surfaces. Steep surfaces get shadow acne; flat surfaces get peter-panning.

## Prerequisites

**Verify normal and light direction availability at shadow test point.** The shadow test
happens during pixel rasterization. At this point:
- **Surface normal:** Available from the interpolated vertex normal (already computed for
  lighting at the same pixel)
- **Light direction:** Available from the light struct stored in the fog/shadow context

If either is NOT interpolated to the shadow test site, it must be threaded through the
rasterizer's per-pixel state. This is the key prerequisite to verify before implementing.

### Fallback if normals aren't available at shadow site:

Use a fixed slope-scale factor based on the shadow map depth gradient instead:
```c
float dx = dzdx; // depth derivative in X
float dy = dzdy; // depth derivative in Y
float slope = sqrtf(dx*dx + dy*dy);
float adjusted_bias = shadow_bias * (1.0f + slope);
```

This doesn't require the surface normal.

## Fix (if normals available)

```c
// At shadow test point:
float n_dot_l = fabsf(normal[0]*light_dir[0] + normal[1]*light_dir[1] + normal[2]*light_dir[2]);
float slope_factor = 1.0f - n_dot_l;  // 0 for face-on, 1 for edge-on
float adjusted_bias = shadow_bias + shadow_bias * slope_factor * 2.0f;
if (sd > sz_map + adjusted_bias) {
    // In shadow
}
```

### GPU backends (Metal/D3D11/OpenGL)

Same formula in the fragment shader:
```glsl
float slopeFactor = 1.0 - abs(dot(normal, lightDir));
float adjustedBias = shadowBias + shadowBias * slopeFactor * 2.0;
```

## Files to Modify

| File | Change |
|------|--------|
| `src/runtime/graphics/vgfx3d_backend_sw.c` | Slope-scaled bias (~5 LOC) |
| `src/runtime/graphics/vgfx3d_backend_metal.m` | MSL shader update (~3 LOC) |
| `src/runtime/graphics/vgfx3d_backend_d3d11.c` | HLSL shader update (~3 LOC) |
| `src/runtime/graphics/vgfx3d_backend_opengl.c` | GLSL shader update (~3 LOC) |

## Documentation Update

Update `docs/graphics3d-guide.md` shadow mapping section:
- Note that shadow bias is now slope-dependent
- Users can still set the base bias via `Canvas3D.SetShadowBias()`

## Test

- Visual: render scene with steep walls and flat floor under directional light
- Verify no self-shadowing on walls (no acne)
- Verify shadows still contact flat surfaces (no peter-panning)
- Existing 3D tests pass (regression)
