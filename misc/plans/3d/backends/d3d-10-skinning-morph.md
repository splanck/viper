# D3D-10: GPU Skinning + Morph Targets

## Current State

This plan covers two different categories of work:

1. backend shader consumption of draw-command payloads
2. producer-side runtime work required to populate those payloads end to end

Those must be documented separately.

## What Already Exists

Already present in shared code:

- `vgfx3d_draw_cmd_t` has `bone_palette`, `bone_count`, `morph_deltas`, `morph_weights`, and `morph_shape_count`
- the Metal backend already consumes those fields

Still missing in shared code:

- [`rt_canvas3d_draw_mesh_skinned()`](/Users/stephen/git/viper/src/runtime/graphics/rt_skeleton3d.c#L923) still CPU-skins into a temporary vertex buffer before enqueueing
- morph payload fields are still left null by [`rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c#L722)

That means the D3D11 backend alone cannot make this feature complete.

## Phase A: Backend GPU Skinning

### HLSL

Add:

```hlsl
StructuredBuffer<float4x4> bonePalette : register(t4);
```

Extend the per-object cbuffer with:

- `hasSkinning`
- `morphShapeCount`
- `vertexCount`

When skinning is enabled:

- blend position from `input.pos`
- blend normal from `input.normal`
- use up to four influences

### Runtime Limits

Define an explicit policy for very large bone palettes:

- recommended behavior: if the palette would exceed the chosen shader/resource limit, fall back to the existing CPU-skinned path

Do not leave the overflow case unspecified.

## Phase B: Shared Producer Work For True GPU Skinning

To make the D3D path real rather than redundant:

- add a producer path in [`rt_skeleton3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_skeleton3d.c) that can enqueue the original mesh plus `bone_palette` for GPU-capable backends instead of always pre-skinning on the CPU

Until that lands, the D3D11 shader path can exist, but it will still consume already-skinned vertices on the main runtime path.

## Phase C: Backend Morph Consumption

Use a vertex-shader-readable structured buffer:

```hlsl
StructuredBuffer<float3> morphDeltas : register(t5);
```

Keep morph weights in a small constant-buffer array:

```hlsl
float morphWeights[16];
```

Add `uint vid : SV_VertexID` to the vertex-shader input and apply morphs before skinning:

- `offset = shape * vertexCount + vid`
- `pos.xyz += morphDeltas[offset] * morphWeights[shape]`

This avoids a second SRV just for weights and keeps the common small-weight-count case simple.

## Phase D: Shared Producer Work For Morphs

Add a producer path in [`rt_morphtarget3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_morphtarget3d.c) that populates:

- `cmd->morph_deltas`
- `cmd->morph_weights`
- `cmd->morph_shape_count`

The current runtime does not do this anywhere, so backend shader work alone is not enough.

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)
- [`src/runtime/graphics/rt_skeleton3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_skeleton3d.c)
- [`src/runtime/graphics/rt_morphtarget3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_morphtarget3d.c)

## Done When

- the D3D11 shader can consume bone palettes and morph payloads
- the runtime can actually supply those payloads without mandatory CPU pre-application
