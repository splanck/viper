# D3D-08: Specular + Emissive Map Sampling

## Depends On

- D3D-01
- D3D-03

## Current State

The D3D11 backend only supports uniform specular and emissive terms. Texture-driven modulation is missing.

## HLSL Changes

Add:

```hlsl
Texture2D specularTex : register(t2);
Texture2D emissiveTex : register(t3);
```

Extend the material cbuffer with:

- `hasSpecularMap`
- `hasEmissiveMap`

Specular setup:

```hlsl
float3 specColor = specularColor.rgb;
float shine = specularColor.w;
if (hasSpecularMap) {
    specColor *= specularTex.Sample(texSampler, input.uv).rgb;
}
```

Emissive setup:

```hlsl
float3 emissive = emissiveColor.rgb;
if (hasEmissiveMap) {
    emissive *= emissiveTex.Sample(texSampler, input.uv).rgb;
}
result += emissive;
```

Keep shininess uniform-driven in v1.

## C-Side Binding

- bind specular map at `t2`
- bind emissive map at `t3`
- clear those SRV slots when the maps are absent

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)

## Done When

- specular highlights can be masked or tinted per texel
- emissive maps add light independent of scene lighting
