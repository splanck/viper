# MTL-05: Specular Map Sampling

## Context
Specular color and shininess are uniform across the surface. A specular map modulates these per-texel.

## Implementation

### Step 1: Add specular map texture slot
```metal
texture2d<float> specularTex [[texture(2)]],  // slot 2
```

### Step 2: Add hasSpecularMap to PerMaterial
```metal
int hasSpecularMap;
```

### Step 3: Sample and modulate specular in fragment
After computing the specular term:
```metal
float3 specColor = material.specularColor.rgb;
float shine = material.specularColor.w;
if (material.hasSpecularMap) {
    float4 specSample = specularTex.sample(texSampler, in.uv);
    specColor *= specSample.rgb;
}
// Use specColor and shine in Blinn-Phong
```

Keep `shine` uniform in v1 unless the engine adopts one shared gloss-map convention for all backends.

### Step 4: Bind in C code
```objc
if (cmd->specular_map) {
    id<MTLTexture> specTex = getCachedTexture(ctx, cmd->specular_map);
    [ctx.encoder setFragmentTexture:specTex atIndex:2];
    mat.hasSpecularMap = 1;
}
```

## Depends On
- MTL-03 (texture cache)

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_metal.m` — shader slot 2, PerMaterial flag, fragment sampling, C binding

## Testing
- White specular map → same as no map
- Black specular map → no specular highlights
