# MTL-04: Normal Map Sampling — ✅ DONE

## Context
Normal map pointer exists in `vgfx3d_draw_cmd_t` but Metal shader has only 1 texture slot (diffuse). Normal mapping requires passing the tangent to the fragment shader and sampling a normal map texture.

## Implementation

### Step 1: Pass tangent through VertexOut
```metal
struct VertexOut {
    float4 position [[position]];
    float3 worldPos;
    float3 normal;
    float3 tangent;  // NEW
    float2 uv;
    float4 color;
};
// In vertex_main:
out.tangent = (obj.modelMatrix * float4(in.tangent, 0.0)).xyz;
```

### Step 2: Add normal map texture slot
```metal
fragment float4 fragment_main(
    VertexOut in [[stage_in]],
    constant PerScene &scene [[buffer(0)]],
    constant PerMaterial &material [[buffer(1)]],
    constant Light *lights [[buffer(2)]],
    texture2d<float> diffuseTex [[texture(0)]],
    texture2d<float> normalTex [[texture(1)]],  // NEW
    sampler texSampler [[sampler(0)]]
) {
```

### Step 3: Add hasNormalMap flag to PerMaterial
```metal
struct PerMaterial {
    // ... existing fields ...
    int hasNormalMap;  // NEW
};
```

### Step 4: Sample normal map and perturb normal in fragment
```metal
float3 N = normalize(in.normal);
if (material.hasNormalMap) {
    float3 T = normalize(in.tangent);
    T = normalize(T - N * dot(T, N));
    float3 B = cross(N, T);
    float3 mapN = normalTex.sample(texSampler, in.uv).rgb * 2.0 - 1.0;
    N = normalize(T * mapN.x + B * mapN.y + N * mapN.z);
}
```

If `T` degenerates after orthonormalization, skip the normal-map path for that fragment.

### Step 5: Bind normal map texture in C code
In `metal_submit_draw()`, after diffuse texture binding:
```objc
if (cmd->normal_map) {
    // Same conversion + cache pattern as diffuse
    id<MTLTexture> normTex = getCachedTexture(ctx, cmd->normal_map);
    [ctx.encoder setFragmentTexture:normTex atIndex:1];
    mat.hasNormalMap = 1;
}
```

## Depends On
- MTL-03 (texture cache) — avoids creating normal map texture every draw

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_metal.m` — shader structs, vertex passthrough, fragment sampling, C-side binding

## Testing
- Flat blue normal map [0.5, 0.5, 1.0] → identical to no normal map
- Bumpy normal map → visible per-pixel lighting variation
- Normal mapped sphere → surface detail without extra geometry
