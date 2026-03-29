# D3D-16: Per-Pixel Terrain Splatting

## Depends On

- D3D-01
- D3D-03
- shared terrain payload already present in Canvas3D / Terrain3D

## Current State

The shared terrain path already populates:

- `cmd->has_splat`
- `cmd->splat_map`
- `cmd->splat_layers[4]`
- `cmd->splat_layer_scales[4]`

The D3D11 backend simply does not consume those fields yet.

## HLSL Changes

Add:

```hlsl
Texture2D splatTex    : register(t5);
Texture2D splatLayer0 : register(t6);
Texture2D splatLayer1 : register(t7);
Texture2D splatLayer2 : register(t8);
Texture2D splatLayer3 : register(t9);
```

Extend the material cbuffer with:

- `hasSplat`
- `float4 splatScales`

Blend logic:

1. sample the RGBA splat weights
2. normalize them if their sum is non-zero
3. sample up to four layer textures with the configured UV scales
4. blend the layers
5. multiply by `diffuseColor.rgb`

## Base-Color Precedence

This must be explicit:

- when `hasSplat` is enabled, splat blending replaces the regular diffuse-texture `baseColor`
- if all splat weights are effectively zero, fall back to the existing diffuse path

That avoids black output from degenerate splat maps.

## C-Side Binding

Bind:

- `t5`: splat map
- `t6`-`t9`: four layer textures

When splatting is disabled:

- set `hasSplat = 0`
- clear SRV slots `t5`-`t9` to avoid stale state

## Runtime Note

Keep the baked terrain fallback in [`rt_terrain3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_terrain3d.c) until the D3D11 splat path is validated. The GPU path should replace the baked fallback only after correctness is confirmed.

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)

## Done When

- terrain layers blend per pixel from the splat map
- degenerate splat weights still produce a sensible fallback color
