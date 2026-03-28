# MTL-09: GPU Skeletal Skinning

## Context
Bone indices and weights are defined in the Metal vertex input (attributes 5-6) but the vertex shader ignores them. Currently Canvas3D pre-skins vertices on CPU before passing to the backend. GPU skinning would be faster for animated characters.

## Current State
- `vgfx3d_vertex_t` has `bone_indices[4]` (`uint8_t`) and `bone_weights[4]` (`float`)
- Metal vertex descriptor maps them at attributes 5-6 (lines 277-287)
- `vertex_main` reads `in.position` directly — no bone transform applied
- CPU skinning in `vgfx3d_skinning.c` pre-transforms vertices before draw submission

The producer-side change for GPU skinning belongs in [`src/runtime/graphics/rt_skeleton3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_skeleton3d.c), not in generic Canvas3D code.

## Implementation

### Step 1: Add bone palette buffer
```metal
vertex VertexOut vertex_main(
    VertexIn in [[stage_in]],
    constant PerObject &obj [[buffer(1)]],
    constant float4x4 *bonePalette [[buffer(3)]]  // NEW: up to 128 bone matrices
) {
```

### Step 2: Apply skinning in vertex shader
```metal
VertexOut out;
float4 skinnedPos = float4(0);
float3 skinnedNormal = float3(0);

if (obj.hasSkinning) {
    for (int i = 0; i < 4; i++) {
        int boneIdx = in.boneIdx[i];
        float weight = in.boneWt[i] / 255.0; // uint8 → [0,1]
        if (weight > 0.001) {
            float4x4 bm = bonePalette[boneIdx];
            skinnedPos += bm * float4(in.position, 1.0) * weight;
            skinnedNormal += (bm * float4(in.normal, 0.0)).xyz * weight;
        }
    }
} else {
    skinnedPos = float4(in.position, 1.0);
    skinnedNormal = in.normal;
}

float4 wp = obj.modelMatrix * skinnedPos;
out.position = obj.viewProjection * wp;
```

### Step 3: Add hasSkinning flag to PerObject
```metal
struct PerObject {
    float4x4 modelMatrix;
    float4x4 viewProjection;
    float4x4 normalMatrix;
    int hasSkinning;  // NEW
    int _pad[3];
};
```

### Step 4: Upload bone palette from C side
In `metal_submit_draw()`, when the draw command has bone palette data:
```objc
if (cmd->bone_palette && cmd->bone_count > 0) {
    [ctx.encoder setVertexBytes:cmd->bone_palette
                         length:cmd->bone_count * 16 * sizeof(float)
                        atIndex:3];
    obj.hasSkinning = 1;
}
```

### Step 5: Extend draw command with bone palette
Add to `vgfx3d_draw_cmd_t`:
```c
const float *bone_palette;  // bone_count * 16 floats (4x4 matrices)
int32_t bone_count;
```

Populate these in [`src/runtime/graphics/rt_skeleton3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_skeleton3d.c) when `rt_canvas3d_draw_mesh_skinned()` builds the draw. Keep the current CPU-skinned temporary-buffer path as the fallback when the backend does not advertise GPU skinning support.

### Step 6: Skip CPU skinning when GPU skinning available
In `rt_canvas3d_draw_mesh_skinned()`, when the backend supports GPU skinning, pass the bone palette through the draw command instead of pre-transforming vertices on CPU.

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_metal.m` — shader vertex skinning, PerObject flag, buffer binding
- `src/runtime/graphics/vgfx3d_backend.h` — bone_palette/bone_count in draw command
- `src/runtime/graphics/rt_skeleton3d.c` — populate bone data in draw command for skinned meshes, preserve CPU fallback

## Testing
- Animated character with walk cycle → bones move correctly on GPU
- Same animation on CPU skinning path → visual result must match
- Static mesh (no bones) → hasSkinning=0, renders as before
- Performance: GPU skinning should be faster for meshes with >1000 vertices
