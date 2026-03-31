# Fix 104: Metal Instanced Skinning — Missing Bone Palette Binding

## Severity: P0 — Critical

## Problem

The Metal backend's `metal_submit_draw_instanced()` does NOT bind the bone palette buffer.
When instanced meshes have skeletal animation, the vertex shader reads from buffer index 3
(bones) but nothing is bound there, producing garbage vertex positions.

D3D11 and OpenGL both properly bind bone palettes in their instanced draw paths.

## Prerequisites

None — the non-instanced Metal draw path already binds bones at index 3 (line ~963).
This fix mirrors that pattern into the instanced path.

**Dependency on Fix 103:** The bone count cap from Fix 103 should be applied here too.
If implementing both, apply the cap in both paths.

## Fix

In `metal_submit_draw_instanced()` (~line 1497), after per-instance matrix binding:

```objc
if (cmd->bone_palette && cmd->bone_count > 0) {
    int bc = cmd->bone_count > 128 ? 128 : cmd->bone_count;
    size_t bsz = (size_t)bc * 16 * sizeof(float);
    id<MTLBuffer> boneBuf = [ctx.device newBufferWithBytes:cmd->bone_palette
                                                    length:bsz
                                                   options:MTLResourceStorageModeShared];
    [ctx.encoder setVertexBuffer:boneBuf offset:0 atIndex:3];
    if (ctx.frameBuffers)
        [ctx.frameBuffers addObject:boneBuf];
}
```

The `frameBuffers` array keeps the buffer alive for the frame (ARC will release it at
frame end when the array is cleared).

## Files to Modify

| File | Change |
|------|--------|
| `src/runtime/graphics/vgfx3d_backend_metal.m` | Add bone palette binding in instanced path (~8 LOC) |

## Documentation Update

None — internal fix. Instanced skinning is already documented as supported.

## Test

- Create instanced batch with skinned mesh (e.g., crowd of animated characters)
- Verify instances render with correct skeletal pose on macOS
- Metal validation layer produces no buffer binding errors
- Existing 3D tests pass (regression)
