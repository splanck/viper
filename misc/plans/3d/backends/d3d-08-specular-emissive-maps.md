# D3D-08: Specular + Emissive Map Sampling

## Context
Same gap as Metal (MTL-05 + MTL-06). No texture sampling for specular or emissive maps. Combining into one plan since the pattern is identical.

## Implementation

### Specular map at slot t2
```hlsl
Texture2D specularTex : register(t2);
// In PerMaterial: int hasSpecularMap;
// In pixel shader:
float3 specColor = specularColor.rgb;
float shine = specularColor.w;
if (hasSpecularMap) {
    float4 specSample = specularTex.Sample(texSampler, input.uv);
    specColor *= specSample.rgb;
}
```

Keep `shine` uniform in v1 unless the engine adopts one shared gloss-map convention for all backends.

### Emissive map at slot t3
```hlsl
Texture2D emissiveTex : register(t3);
// In PerMaterial: int hasEmissiveMap;
// In pixel shader:
float3 emissive = emissiveColor.rgb;
if (hasEmissiveMap) {
    emissive *= emissiveTex.Sample(texSampler, input.uv).rgb;
}
result += emissive;
```

### C-side binding
For each map, create SRV and bind at the appropriate slot:
```c
if (cmd->specular_map) {
    ID3D11ShaderResourceView *srv = get_or_create_srv(ctx, cmd->specular_map);
    ID3D11DeviceContext_PSSetShaderResources(ctx->context, 2, 1, &srv);
    mat.hasSpecularMap = 1;
}
if (cmd->emissive_map) {
    ID3D11ShaderResourceView *srv = get_or_create_srv(ctx, cmd->emissive_map);
    ID3D11DeviceContext_PSSetShaderResources(ctx->context, 3, 1, &srv);
    mat.hasEmissiveMap = 1;
}
```

## Depends On
- D3D-01 (texture sampling infrastructure)
- D3D-03 (texture cache)

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_d3d11.c` — HLSL t2/t3 slots, PerMaterial flags, pixel shader sampling, C binding

## Testing
- Same tests as MTL-05 and MTL-06
