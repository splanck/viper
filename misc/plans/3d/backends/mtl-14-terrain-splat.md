# MTL-14: Per-Pixel Terrain Splatting

## Context
Part of Plan 15. Metal needs splat map + 4 layer textures bound and sampled in the fragment shader.

Producer-side integration belongs in [`src/runtime/graphics/rt_terrain3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_terrain3d.c). Keep the baked terrain-texture fallback until the shared splat payload is wired and the intended backends consume it.

## Depends On
- MTL-03 (texture cache)
- Plan 15 Phase 1 (splat fields in draw command)

## Implementation

### Step 1: Add splat textures to fragment shader
```metal
texture2d<float> splatTex [[texture(5)]],
texture2d<float> splatLayer0 [[texture(6)]],
texture2d<float> splatLayer1 [[texture(7)]],
texture2d<float> splatLayer2 [[texture(8)]],
texture2d<float> splatLayer3 [[texture(9)]],
```

### Step 2: Add to PerMaterial
```metal
int hasSplat;
float4 splatScales;
```

### Step 3: Splat blending in fragment
```metal
if (material.hasSplat) {
    float4 sp = splatTex.sample(texSampler, in.uv);
    float wsum = sp.r + sp.g + sp.b + sp.a;
    if (wsum > 0.001) sp /= wsum;
    float3 blended = float3(0);
    if (sp.r > 0.001) blended += splatLayer0.sample(texSampler, in.uv * material.splatScales.x).rgb * sp.r;
    if (sp.g > 0.001) blended += splatLayer1.sample(texSampler, in.uv * material.splatScales.y).rgb * sp.g;
    if (sp.b > 0.001) blended += splatLayer2.sample(texSampler, in.uv * material.splatScales.z).rgb * sp.b;
    if (sp.a > 0.001) blended += splatLayer3.sample(texSampler, in.uv * material.splatScales.w).rgb * sp.a;
    baseColor = blended * material.diffuseColor.rgb;
}
```

### Step 4: Bind textures in C code
```objc
if (cmd->has_splat && cmd->splat_map) {
    id<MTLTexture> splatTex = getCachedTexture(ctx, cmd->splat_map);
    [ctx.encoder setFragmentTexture:splatTex atIndex:5];
    for (int i = 0; i < 4; i++) {
        if (cmd->splat_layers[i]) {
            id<MTLTexture> layerTex = getCachedTexture(ctx, cmd->splat_layers[i]);
            [ctx.encoder setFragmentTexture:layerTex atIndex:6 + i];
        }
    }
    mat.hasSplat = 1;
    mat.splatScales = simd_make_float4(cmd->splat_layer_scales[0], ...);
}
```

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_metal.m` — shader texture slots 5-9, PerMaterial cbuffer, fragment splat, C texture binding
- `src/runtime/graphics/rt_terrain3d.c` — populate shared splat payload while preserving fallback path

## Testing
- Same tests as D3D-16 and SW-07
