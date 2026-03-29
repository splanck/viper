# OGL-16: Per-Pixel Terrain Splatting

## Depends On

- OGL-03
- OGL-04
- shared terrain payload already present in Canvas3D / Terrain3D

## Current State

The shared terrain path already populates:

- `cmd->has_splat`
- `cmd->splat_map`
- `cmd->splat_layers[4]`
- `cmd->splat_layer_scales[4]`

The OpenGL backend simply does not consume those fields yet.

## GLSL Changes

Add:

```glsl
uniform sampler2D uSplatTex;
uniform sampler2D uSplatLayer0;
uniform sampler2D uSplatLayer1;
uniform sampler2D uSplatLayer2;
uniform sampler2D uSplatLayer3;
uniform int uHasSplat;
uniform vec4 uSplatScales;
```

Blend logic:

1. sample the RGBA splat weights
2. normalize them if their sum is non-zero
3. sample up to four layer textures with the configured UV scales
4. blend the layers
5. multiply by `uDiffuseColor.rgb`

## Base-Color Precedence

This must be explicit:

- when `uHasSplat != 0`, splat blending replaces the regular diffuse-texture `baseColor`
- if all splat weights are effectively zero, fall back to the existing diffuse path

That matches the intended terrain behavior and avoids black output from degenerate splat maps.

## C-Side Binding

Bind:

- unit `5`: splat map
- units `6`-`9`: four layer textures

Upload `uSplatScales` from `cmd->splat_layer_scales`.

When splatting is disabled:

- set `uHasSplat = 0`
- clear bound textures on units `5`-`9` to avoid stale state
- restore active texture to `GL_TEXTURE0`

## Runtime Note

Keep the baked terrain fallback in [`rt_terrain3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_terrain3d.c) until the OpenGL splat path is validated. The GPU path should replace the baked fallback only after correctness is confirmed.

## Files

- [`src/runtime/graphics/vgfx3d_backend_opengl.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_opengl.c)

## Done When

- Terrain layers blend per pixel from the splat map
- Degenerate splat weights still produce a sensible fallback color
