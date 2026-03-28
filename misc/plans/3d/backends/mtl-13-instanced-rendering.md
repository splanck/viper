# MTL-13: Instanced Rendering

## Context
Same gap as D3D-15. Metal supports hardware instancing via `drawIndexedPrimitives:indexCount:instanceCount:`. The current `InstanceBatch3D` path in [`src/runtime/graphics/rt_instbatch3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_instbatch3d.c) already bypasses the deferred queue, so the win here is fewer backend calls and less duplicated vertex work, not queue overhead.

## Implementation

### Phase 1: shared optional instanced hook
Add a shared optional `submit_draw_instanced()` entry to [`src/runtime/graphics/vgfx3d_backend.h`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend.h), and update [`src/runtime/graphics/rt_instbatch3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_instbatch3d.c) to use it when present. Unsupported backends keep the current loop.

### Phase 2: Metal hardware path
Metal's backend implementation should use a dedicated instance-matrix buffer and `instance_id`:
```objc
// Per-instance transforms in a buffer
id<MTLBuffer> instBuf = [ctx.device newBufferWithBytes:instance_matrices
                                                length:instance_count * 64
                                               options:MTLResourceStorageModeShared];
[ctx.encoder setVertexBuffer:instBuf offset:0 atIndex:3]; // instance buffer slot

// Vertex shader reads instance_id
// vertex VertexOut vertex_main(..., uint iid [[instance_id]])
// float4x4 instMatrix = instanceMatrices[iid];

[ctx.encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                        indexCount:cmd->index_count
                         indexType:MTLIndexTypeUInt32
                       indexBuffer:idxBuf
                 indexBufferOffset:0
                     instanceCount:instance_count];
```

### Vertex shader modification
```metal
vertex VertexOut vertex_main(
    VertexIn in [[stage_in]],
    constant PerObject &obj [[buffer(1)]],
    constant float4x4 *instanceMatrices [[buffer(3)]],
    uint iid [[instance_id]]
) {
    float4x4 model = (obj.useInstancing) ? instanceMatrices[iid] : obj.modelMatrix;
    float4 wp = model * float4(in.position, 1.0);
    // ...
}
```

Use a dedicated shader/pipeline variant for instanced draws rather than overloading the non-instanced path with too many runtime branches.

## Files Modified
- `src/runtime/graphics/vgfx3d_backend.h` — optional shared instanced hook
- `src/runtime/graphics/rt_instbatch3d.c` — dispatch through the hook when available
- `src/runtime/graphics/vgfx3d_backend_metal.m` — instance buffer binding, shader variant, instanced draw call

## Testing
- 100 instanced boxes → all render correctly
- Performance: single draw call vs 100 separate draws
