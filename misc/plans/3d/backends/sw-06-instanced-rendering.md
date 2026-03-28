# SW-06: Instanced Rendering in Software Rasterizer

## Context
`InstanceBatch3D` already exists in the API, and the current path in [`src/runtime/graphics/rt_instbatch3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_instbatch3d.c) loops `submit_draw()` directly. That is already the correct software fallback. The main value of this plan is making sure the software path stays correct if a shared hardware-instancing hook is added for GPU backends later.

## Current State
- `rt_canvas3d_draw_instanced()` does not use Canvas3D's deferred queue today
- It builds a `vgfx3d_draw_cmd_t` per instance and calls `backend->submit_draw()` immediately
- For the software backend, this is effectively what "instancing" means: repeated CPU transform + raster work with shared mesh data

## Implementation

### Keep the current software path as the fallback
The software rasterizer is CPU-bound. Each instance still needs:
1. Vertex transformation by a different model matrix
2. Separate lighting computation
3. Independent triangle rasterization

Those steps are inherently per-instance. There is no meaningful backend-specific optimization here unless a shared instanced backend hook is introduced for Metal/D3D/OpenGL and software needs to implement the compatibility version of that API.

### If a shared instanced hook lands
If GPU backends gain a new optional `submit_draw_instanced()` entry, the software implementation should stay deliberately simple:

1. Add the shared hook to [`src/runtime/graphics/vgfx3d_backend.h`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend.h) only if at least one GPU backend actually uses it.
2. Update [`src/runtime/graphics/rt_instbatch3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_instbatch3d.c) to prefer that hook when present.
3. Implement the software version as a thin loop that reuses the existing `sw_submit_draw()` path.
4. Keep the current immediate per-instance loop as the fallback for backends that do not implement the hook.

Do not add software-only complexity such as transformed-vertex caches until profiling proves the current path is the bottleneck.

## Files Modified
- `src/runtime/graphics/rt_instbatch3d.c` — optional shared instanced-dispatch path
- `src/runtime/graphics/vgfx3d_backend.h` — optional backend hook, only if GPU backends need it
- `src/runtime/graphics/vgfx3d_backend_sw.c` — trivial compatibility implementation if the shared hook is added

## Testing
- 100 boxes via InstanceBatch3D → all render correctly at different positions
- Software fallback path should remain visually identical to today
- If the shared hook is added, software performance should be roughly unchanged and correctness must not regress
