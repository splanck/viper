# MTL-10: GPU Morph Targets

## Context
Morph targets (blend shapes) are computed on CPU in `rt_morphtarget3d.c`. GPU morph targets would offload this to the vertex shader. Currently no backend supports GPU morph targets.

Producer-side integration belongs in [`src/runtime/graphics/rt_morphtarget3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_morphtarget3d.c). The backend should consume flattened morph payloads passed through `vgfx3d_draw_cmd_t`; it should not reach into private `rt_morphtarget3d` state directly.

## Current CPU Path
`rt_canvas3d_draw_mesh_morphed()` pre-computes blended vertex positions on CPU:
```c
for each vertex:
    pos = base_pos + sum(delta[shape] * weight[shape])
```
The blended mesh is then passed to the normal draw path.

## Implementation

### Step 1: Add morph target buffer to draw command
```c
// In vgfx3d_draw_cmd_t:
const float *morph_deltas;    // shape_count * vertex_count * 3 floats (position deltas)
const float *morph_weights;   // shape_count floats
int32_t morph_shape_count;
```

### Step 2: Upload morph data as Metal buffer
In `metal_submit_draw()`:
```objc
if (cmd->morph_deltas && cmd->morph_shape_count > 0) {
    [ctx.encoder setVertexBytes:cmd->morph_deltas
                         length:cmd->morph_shape_count * cmd->vertex_count * 3 * sizeof(float)
                        atIndex:4];
    [ctx.encoder setVertexBytes:cmd->morph_weights
                         length:cmd->morph_shape_count * sizeof(float)
                        atIndex:5];
    obj.morphShapeCount = cmd->morph_shape_count;
}
```

### Step 3: Apply morph in vertex shader
```metal
vertex VertexOut vertex_main(
    VertexIn in [[stage_in]],
    constant PerObject &obj [[buffer(1)]],
    constant float4x4 *bonePalette [[buffer(3)]],
    constant float *morphDeltas [[buffer(4)]],   // NEW
    constant float *morphWeights [[buffer(5)]],  // NEW
    uint vertexId [[vertex_id]]
) {
    float3 pos = in.position;
    if (obj.morphShapeCount > 0) {
        for (int s = 0; s < obj.morphShapeCount; s++) {
            float w = morphWeights[s];
            if (w > 0.001) {
                int offset = s * obj.vertexCount * 3 + vertexId * 3;
                pos.x += morphDeltas[offset + 0] * w;
                pos.y += morphDeltas[offset + 1] * w;
                pos.z += morphDeltas[offset + 2] * w;
            }
        }
    }
    // ... rest of vertex transform using pos instead of in.position ...
}
```

### Step 4: Add fields to PerObject
```metal
struct PerObject {
    // ... existing ...
    int morphShapeCount;  // NEW
    int vertexCount;      // NEW (needed for delta array indexing)
};
```

### Step 5: Canvas3D populates morph data
In [`src/runtime/graphics/rt_morphtarget3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_morphtarget3d.c), preserve the current CPU path as the fallback. When GPU morph is available, flatten the active morph deltas/weights into draw-command payloads instead of precomputing the blended mesh.

If normal deltas are meant to remain visually correct on the GPU path, the draw payload must include normal deltas as well or the plan must explicitly scope v1 to position-only morphs.

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_metal.m` — shader morph application, buffer bindings
- `src/runtime/graphics/vgfx3d_backend.h` — morph fields in draw command
- `src/runtime/graphics/rt_morphtarget3d.c` — populate morph data in draw command, preserve CPU fallback

## Testing
- Facial blend shape (smile) → smooth morph with weight 0→1
- Multiple shapes blended simultaneously → additive deltas
- Weight = 0 → no visible change (base mesh)
- Same result as CPU morph path → visual parity

## Performance Note
GPU morph targets are most beneficial for meshes with many vertices and few shapes. For meshes with many shapes (>10), the vertex buffer size grows linearly. If `shape_count * vertex_count * 12 bytes` exceeds Metal's max buffer size, fall back to CPU.
