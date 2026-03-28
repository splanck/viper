# MTL-06: Emissive Map Sampling

## Context
Metal shader adds `material.emissiveColor.rgb` as a uniform but doesn't sample the emissive map texture. Software backend samples it and adds as `emissive_map_color * emissive_color`.

## Implementation

### Step 1: Add emissive map texture slot
```metal
texture2d<float> emissiveTex [[texture(3)]],  // slot 3
```

### Step 2: Add hasEmissiveMap to PerMaterial
```metal
int hasEmissiveMap;
```

### Step 3: Sample emissive map in fragment
Replace the `result += material.emissiveColor.rgb` line at the end of the lighting loop:
```metal
// Before: result += material.emissiveColor.rgb;
// After:
float3 emissive = material.emissiveColor.rgb;
if (material.hasEmissiveMap) {
    emissive *= emissiveTex.sample(texSampler, in.uv).rgb;
}
result += emissive;
```

### Step 4: Bind in C code
```objc
if (cmd->emissive_map) {
    id<MTLTexture> emisTex = getCachedTexture(ctx, cmd->emissive_map);
    [ctx.encoder setFragmentTexture:emisTex atIndex:3];
    mat.hasEmissiveMap = 1;
}
```

## Depends On
- MTL-03 (texture cache)

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_metal.m` — shader slot 3, PerMaterial flag, fragment sampling, C binding

## Testing
- No emissive map → emissive color only (backward compatible)
- White emissive map → full emissive color applied
- Patterned emissive map → glowing pattern on surface (e.g., lava cracks, circuit traces)
