# OGL-19: GPU Morph Normal-Delta Parity

## Depends On

- OGL-11
- shared GPU morph producer path in [`src/runtime/graphics/rt_morphtarget3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_morphtarget3d.c)

## Current State

The current GPU morph path is position-only:

- [`rt_canvas3d_draw_mesh_morphed()`](/Users/stephen/git/viper/src/runtime/graphics/rt_morphtarget3d.c#L265) flattens only `pos_deltas` for GPU-capable backends
- the CPU fallback still applies `nrm_deltas` and renormalizes normals
- the OpenGL morph shader consumes only `cmd->morph_deltas`

That means a morphed mesh can shade differently on the OpenGL GPU path than on the CPU/software path whenever the morph target includes normal deltas.

## Required Shared Prerequisites

The draw-command payload must grow to carry optional normal deltas:

```c
const float *morph_normal_deltas; /* shape_count * vertex_count * 3 floats or NULL */
```

Transient mesh fields should mirror that payload so `DrawMeshMorphed` can hand it through without touching backend-private state.

Recommended shared changes:

- add `morph_normal_deltas` to `rt_mesh3d`
- add `morph_normal_deltas` to `vgfx3d_draw_cmd_t`
- populate it in [`rt_canvas3d_draw_mesh_morphed()`](/Users/stephen/git/viper/src/runtime/graphics/rt_morphtarget3d.c) when any shape provides `nrm_deltas`
- register the packed normal-delta buffer in `temp_buffers` exactly like the position-delta buffer
- leave the pointer `NULL` when no normal deltas exist

Packing rule:

- shape-major, vertex-minor, 3 floats per normal delta
- use zero-filled deltas for shapes that omit normal deltas so indexing stays uniform

## OpenGL Backend Work

### 1. Streaming resource

Add a second streaming path for morph normal deltas:

- `GLuint morph_normal_buffer`
- `GLuint morph_normal_tbo`
- `size_t morph_normal_capacity_bytes`

Do not reuse the position-delta TBO for both payloads unless the storage layout is explicitly redesigned; two buffers are simpler and clearer.

### 2. Shader inputs

Add:

```glsl
uniform samplerBuffer uMorphNormalDeltas;
uniform int uHasMorphNormalDeltas;
```

### 3. Vertex-stage algorithm

When morphing is active:

1. accumulate position deltas in object space
2. if `uHasMorphNormalDeltas != 0`, accumulate normal deltas in object space
3. renormalize the morphed normal
4. apply skinning to the morphed position and morphed normal if skinning is also active
5. continue with the existing model/view/projection path

Do not apply normal deltas after skinning. The CPU path treats them as object-space morph inputs.

### 4. Fallback behavior

If `morph_normal_deltas == NULL`:

- keep the current position-only morph behavior
- do not allocate or bind the normal-delta TBO

This keeps meshes without normal-delta shapes cheap.

## Shadow Pass Note

The shadow pass only needs morphed positions. It does not need the normal-delta payload unless the shadow implementation later depends on per-vertex normals, which it does not today.

## Files

- [`src/runtime/graphics/vgfx3d_backend.h`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend.h)
- [`src/runtime/graphics/rt_canvas3d_internal.h`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d_internal.h)
- [`src/runtime/graphics/rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c)
- [`src/runtime/graphics/rt_morphtarget3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_morphtarget3d.c)
- [`src/runtime/graphics/vgfx3d_backend_opengl.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_opengl.c)

## Done When

- morph targets with normal deltas shade the same on the OpenGL GPU path and the CPU fallback
- meshes without normal deltas keep the current lightweight path
- skinned + morphed meshes apply morph normal deltas before skinning
