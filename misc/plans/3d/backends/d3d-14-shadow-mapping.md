# D3D-14: Shadow Mapping

## Depends On

- D3D-09

## Correction To The Earlier Plan

Canvas3D shadow-pass scheduling already exists in [`rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c). This plan does not need a new replay system. The D3D11 work is implementing the backend's `shadow_begin`, `shadow_draw`, and `shadow_end` hooks and integrating the resulting depth texture into the main shader.

## Backend State

Add to `d3d11_context_t`:

- typeless shadow depth texture
- shadow DSV
- shadow SRV
- comparison sampler
- shadow vertex shader
- cached shadow light VP matrix
- shadow bias
- shadow-active flag

## Shadow Pass

### `shadow_begin()`

- ignore the CPU `depth_buf` argument, as Metal already does
- create or resize the shadow texture resources
- bind the shadow DSV with no color RTV
- set the shadow viewport
- clear depth
- bind the shadow VS and set PS to `NULL`
- store the light VP matrix

Use:

- texture format: `DXGI_FORMAT_R32_TYPELESS`
- DSV format: `DXGI_FORMAT_D32_FLOAT`
- SRV format: `DXGI_FORMAT_R32_FLOAT`

### `shadow_draw()`

Render opaque geometry depth-only into the shadow map.

Use the same vertex format as the main draw path. A fragment/pixel shader is not required for a depth-only pass.

### `shadow_end()`

- store the bias
- mark shadowing active
- restore state needed by the regular draw path:
  - main RTV/DSV or RTT RTV/DSV
  - main viewport
  - main vertex shader / pixel shader

That state restoration is required because the current Canvas3D end-of-frame flow runs the shadow pass before the opaque/transparent replay, not as a separate frame.

## Main Pass Shader Work

Add:

```hlsl
Texture2D shadowMap : register(t4);
SamplerComparisonState shadowSampler : register(s1);
```

Extend `PerScene` with:

- `shadowVP`
- `shadowBias`
- `shadowEnabled`

For directional lights, sample the shadow map with `SampleCmpLevelZero` and attenuate direct light.

Recommended comparison sampler:

- filter: comparison linear
- address mode: border
- border color: white
- comparison func: `LESS_EQUAL`

Using `t4` here is deliberate so the shadow map does not collide with the terrain-splat texture range used by D3D-16 (`t5`-`t9` in the pixel shader).

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)

## Done When

- directional lights cast shadows in the main pass
- state is correctly restored after the shadow prepass
- RTT and onscreen rendering both still work after shadowing is enabled
