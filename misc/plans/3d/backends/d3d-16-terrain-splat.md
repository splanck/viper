# D3D-16: Per-Pixel Terrain Splatting

## Context
Part of Plan 15 (per-pixel splatting). D3D11 needs splat map + 4 layer texture sampling in the pixel shader.

Producer-side integration belongs in [`src/runtime/graphics/rt_terrain3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_terrain3d.c). Keep the baked terrain-texture fallback until the shared splat payload is wired and the intended backends consume it.

## Depends On
- D3D-01 (diffuse texture SRV pattern)
- D3D-03 (texture cache)
- Plan 15 Phase 1 (splat fields in draw command)

## Implementation

### Step 1: Add splat texture SRVs
When `cmd->has_splat` is set, bind additional SRVs:
```c
// t5 = splat map
// t6 = layer 0
// t7 = layer 1
// t8 = layer 2
// t9 = layer 3
if (cmd->has_splat && cmd->splat_map) {
    ID3D11ShaderResourceView *splatSRV = get_or_create_srv(ctx, cmd->splat_map);
    ID3D11DeviceContext_PSSetShaderResources(ctx->context, 5, 1, &splatSRV);
    for (int i = 0; i < 4; i++) {
        if (cmd->splat_layers[i]) {
            ID3D11ShaderResourceView *layerSRV = get_or_create_srv(ctx, cmd->splat_layers[i]);
            ID3D11DeviceContext_PSSetShaderResources(ctx->context, 6 + i, 1, &layerSRV);
        }
    }
    mat.hasSplat = 1;
    memcpy(mat.splatScales, cmd->splat_layer_scales, 4 * sizeof(float));
}
```

### Step 2: Add to PerMaterial cbuffer
```hlsl
int hasSplat;
int _splatPad[3]; // align splatScales to 16-byte boundary
float4 splatScales; // UV tiling per layer
```

Update the C struct to match (exact layout depends on which other plans have been applied — D3D-07, D3D-08 may have already added fields after `_p`):
```c
// Added fields at end of d3d_per_material_t:
int32_t has_splat;
int32_t _splat_pad[3];
float splat_scales[4]; // must be 16-byte aligned
```

### Step 3: Add HLSL declarations
```hlsl
Texture2D splatTex : register(t5);
Texture2D splatLayer0 : register(t6);
Texture2D splatLayer1 : register(t7);
Texture2D splatLayer2 : register(t8);
Texture2D splatLayer3 : register(t9);
```

### Step 4: Splat sampling in pixel shader
After computing `baseColor`, before lighting:
```hlsl
if (hasSplat) {
    float4 sp = splatTex.Sample(texSampler, input.uv);
    float4 w = sp / max(dot(sp, float4(1,1,1,1)), 0.001); // normalize
    float3 blended = float3(0, 0, 0);
    if (w.r > 0.001) blended += splatLayer0.Sample(texSampler, input.uv * splatScales.x).rgb * w.r;
    if (w.g > 0.001) blended += splatLayer1.Sample(texSampler, input.uv * splatScales.y).rgb * w.g;
    if (w.b > 0.001) blended += splatLayer2.Sample(texSampler, input.uv * splatScales.z).rgb * w.b;
    if (w.a > 0.001) blended += splatLayer3.Sample(texSampler, input.uv * splatScales.w).rgb * w.a;
    baseColor = blended * diffuseColor.rgb;
}
```

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_d3d11.c` — HLSL splat declarations + sampling, PerMaterial cbuffer, SRV binding
- `src/runtime/graphics/rt_terrain3d.c` — populate shared splat payload while preserving fallback path

## Testing
- Terrain with splat map → visible blending between layers
- No splat map → renders normally with material texture
- Layer UV scales → tiling detail visible within splat regions
