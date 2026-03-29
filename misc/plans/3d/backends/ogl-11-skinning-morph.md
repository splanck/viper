# OGL-11: GPU Skinning + Morph Targets

## Current State

This plan actually covers two different kinds of work:

1. backend shader consumption of already-populated draw-command payloads
2. producer-side runtime work needed to populate those payloads end to end

Those must be documented separately.

## What Already Exists

Already present in shared code:

- `vgfx3d_draw_cmd_t` has `bone_palette`, `bone_count`, `morph_deltas`, `morph_weights`, and `morph_shape_count`
- the Metal backend already consumes those fields

Still missing in shared code:

- [`rt_canvas3d_draw_mesh_skinned()`](/Users/stephen/git/viper/src/runtime/graphics/rt_skeleton3d.c#L923) still CPU-skins into a temporary vertex buffer before enqueueing the draw
- morph payload fields are still left null by [`rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c#L722)

That means the OpenGL backend alone cannot make this feature complete.

## Phase A: Backend GPU Skinning

### GLSL

Add to the vertex shader:

```glsl
uniform mat4 uBonePalette[128];
uniform int uHasSkinning;
```

When skinning is enabled:

- blend position from `aPosition`
- blend normal from `aNormal`
- use up to 4 bone influences

Upload the palette with `glUniformMatrix4fv(..., GL_TRUE, ...)` to preserve the existing row-major convention.

### Runtime Limits

Define an explicit policy for `bone_count > 128`:

- recommended behavior: fall back to the existing CPU-skinned path

Do not leave the overflow case unspecified.

## Phase B: Shared Producer Work For True GPU Skinning

To make the OpenGL skinning path real rather than redundant:

- add a producer path in [`rt_skeleton3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_skeleton3d.c) that can enqueue the original mesh plus `bone_palette` for GPU-capable backends instead of always pre-skinning on the CPU

Until that lands, the OpenGL shader path can exist, but it will still consume already-skinned vertices on the main runtime path.

## Phase C: Backend Morph Consumption

Use a GL 3.3-compatible texture-buffer path:

- buffer target: `GL_TEXTURE_BUFFER`
- texture format: `GL_R32F`
- shader fetches morph deltas via `texelFetch`
- index by `gl_VertexID`

Required shader inputs:

```glsl
uniform samplerBuffer uMorphDeltas;
uniform float uMorphWeights[16];
uniform int uMorphShapeCount;
uniform int uVertexCount;
```

Create and delete the morph buffer/texture pair around the draw until a persistent streaming policy is justified.

## Phase D: Shared Producer Work For Morphs

Add a producer path in [`rt_morphtarget3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_morphtarget3d.c) that populates:

- `cmd->morph_deltas`
- `cmd->morph_weights`
- `cmd->morph_shape_count`

The current runtime does not do this anywhere, so backend shader work alone is not enough.

## Loader And Constant Additions

For the morph path, load:

- `TexBuffer`

Add constants:

- `GL_TEXTURE_BUFFER`
- `GL_R32F`

## Files

- [`src/runtime/graphics/vgfx3d_backend_opengl.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_opengl.c)
- [`src/runtime/graphics/rt_skeleton3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_skeleton3d.c)
- [`src/runtime/graphics/rt_morphtarget3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_morphtarget3d.c)

## Done When

- The OpenGL shader can consume bone palettes and morph payloads
- The runtime can actually supply those payloads without mandatory CPU pre-application
