# D3D-10: GPU Skeletal Skinning + Morph Targets

## Context
Same gap as Metal (MTL-09 + MTL-10). Bone indices/weights defined in VS_INPUT but unused. CPU skinning works but GPU would be faster. Combining skinning + morph since both modify the vertex shader.

The producer-side work is split today:
- Skinning data and CPU fallback live in [`src/runtime/graphics/rt_skeleton3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_skeleton3d.c)
- Morph target data and CPU fallback live in [`src/runtime/graphics/rt_morphtarget3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_morphtarget3d.c)

Keep those ownership boundaries. The backend should consume shared draw-command payloads, not reach into private runtime objects.

## Implementation

### GPU Skinning

#### Step 1: Add bone palette as structured buffer
```hlsl
StructuredBuffer<float4x4> bonePalette : register(t4);
// Add to PerObject cbuffer:
int hasSkinning;
int vertexCount;
int morphShapeCount;
int _objPad;
```

Update the C struct to match:
```c
typedef struct {
    float m[16];
    float vp[16];
    float nm[16];
    int32_t has_skinning;
    int32_t vertex_count;
    int32_t morph_shape_count;
    int32_t _obj_pad;
} d3d_per_object_t; // 208 bytes (was 192)
```

#### Step 2: Apply skinning in vertex shader
```hlsl
PS_INPUT VSMain(VS_INPUT input) {
    float4 pos = float4(input.pos, 1.0);
    float3 norm = input.normal;

    if (hasSkinning) {
        float4 skinnedPos = float4(0, 0, 0, 0);
        float3 skinnedNorm = float3(0, 0, 0);
        for (int i = 0; i < 4; i++) {
            uint boneIdx = input.boneIdx[i];
            float weight = input.boneWt[i];
            if (weight > 0.001) {
                float4x4 bm = bonePalette[boneIdx];
                skinnedPos += mul(pos, bm) * weight;
                skinnedNorm += mul(float4(norm, 0), bm).xyz * weight;
            }
        }
        pos = skinnedPos;
        norm = skinnedNorm;
    }

    PS_INPUT output;
    float4 wp = mul(pos, modelMatrix);
    // ...
    output.normal = mul(float4(norm, 0.0), normalMatrix).xyz;
    // ...
}
```

#### Step 3: Upload bone palette as SRV
```c
if (cmd->bone_palette && cmd->bone_count > 0) {
    // Create structured buffer with bone_count float4x4 matrices
    D3D11_BUFFER_DESC desc = {0};
    desc.ByteWidth = cmd->bone_count * 64; // 4x4 float = 64 bytes
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = 64;
    D3D11_SUBRESOURCE_DATA init = {.pSysMem = cmd->bone_palette};
    ID3D11Buffer *boneBuf;
    ID3D11Device_CreateBuffer(ctx->device, &desc, &init, &boneBuf);
    // Create SRV for StructuredBuffer
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {0};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.NumElements = cmd->bone_count;
    ID3D11ShaderResourceView *boneSRV;
    ID3D11Device_CreateShaderResourceView(ctx->device, (ID3D11Resource *)boneBuf, &srvDesc, &boneSRV);
    ID3D11DeviceContext_VSSetShaderResources(ctx->context, 4, 1, &boneSRV);
    obj.hasSkinning = 1;
    // Release after draw
}
```

### GPU Morph Targets

#### Step 4: Add morph delta buffer
```hlsl
StructuredBuffer<float3> morphDeltas : register(t5);
Buffer<float> morphWeights : register(t6);
```

#### Step 5: Apply morph in vertex shader (before skinning)
```hlsl
if (morphShapeCount > 0) {
    uint vid = input_vertex_id; // Need SV_VertexID
    for (int s = 0; s < morphShapeCount; s++) {
        float w = morphWeights[s];
        if (w > 0.001) {
            uint offset = s * vertexCount + vid;
            pos.xyz += morphDeltas[offset] * w;
        }
    }
}
```

Note: Need to add `uint vid : SV_VertexID` to VS_INPUT.

#### Step 6: Upload morph data as SRVs
Similar to bone palette — create structured buffer for deltas, buffer for weights, bind to t5/t6.

## Depends On
- D3D-01 (texture/SRV infrastructure)

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_d3d11.c` — HLSL vertex shader skinning + morph, PerObject flags, SRV creation + binding
- `src/runtime/graphics/vgfx3d_backend.h` — bone_palette/bone_count + morph fields in draw command (shared with MTL-09/10)
- `src/runtime/graphics/rt_skeleton3d.c` — GPU-skinning producer path + CPU fallback
- `src/runtime/graphics/rt_morphtarget3d.c` — GPU-morph producer path + CPU fallback

## Testing
- Animated character → bones move correctly
- Same animation CPU vs GPU → visual parity
- Morph blend shape (face expression) → smooth deformation
- Static mesh → hasSkinning=0, no performance cost
