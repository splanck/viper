# D3D-15: Hardware Instanced Rendering

## Depends On

- shared `submit_draw_instanced()` hook
- D3D-12 recommended

## Correction To The Earlier Plan

The shared instanced hook is already present in [`vgfx3d_backend.h`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend.h) and already used by [`rt_instbatch3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_instbatch3d.c). This plan is not about adding that hook anymore. It is about making the D3D11 backend implement it with real `DrawIndexedInstanced()` hardware instancing.

## Backend Work

Add to `d3d11_context_t`:

- persistent instance buffer
- instance buffer capacity
- instanced input layout
- instanced vertex shader variant

## Per-Instance Data

This plan must not reintroduce the normal-matrix bug for instanced draws.

Recommended v1 approach:

- build a temporary CPU-side instance stream that contains:
  - model matrix
  - normal matrix

That lets the D3D11 instanced shader read a correct per-instance normal transform without relying on shader-side matrix inversion.

Suggested instance payload:

```c
typedef struct {
    float model[16];
    float normal[16];
} d3d_instance_data_t;
```

The backend can derive `normal` internally from the provided `instance_matrices` using the same helper introduced by D3D-02, so the shared hook signature does not need to change.

## Draw Path

Implement `submit_draw_instanced()` using:

- one mesh VB
- one IB
- one instance buffer
- `DrawIndexedInstanced`

Create a separate instanced input layout with per-instance data slots for:

- four `float4` rows of the model matrix
- four `float4` rows of the normal matrix

Use `D3D11_INPUT_PER_INSTANCE_DATA` with step rate `1`.

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)

## Done When

- one draw call renders N instances
- per-instance transforms work
- instanced lighting stays correct under non-uniform scale
