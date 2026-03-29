# D3D-07: Normal Map Sampling

## Depends On

- D3D-01
- D3D-03

## Current State

The D3D11 vertex format already contains tangents, but the shader ignores them and there is no normal-map SRV path.

## HLSL Changes

Pass tangent through to the pixel shader:

```hlsl
struct PS_INPUT {
    float4 pos      : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 uv       : TEXCOORD2;
    float3 tangent  : TEXCOORD3;
    float4 color    : COLOR;
};
```

Vertex shader:

```hlsl
output.tangent = mul(float4(input.tangent, 0.0), modelMatrix).xyz;
```

Add:

```hlsl
Texture2D normalTex : register(t1);
```

Extend the material cbuffer with `hasNormalMap`, then perturb the normal with a TBN basis in the pixel shader.

Degenerate tangents must skip the perturbation path cleanly.

## C-Side Binding

- bind normal-map SRV at `t1`
- set `hasNormalMap` accordingly
- clear slot `t1` when no normal map is present to avoid stale state

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)

## Done When

- tangent-space normal maps visibly perturb lighting
- degenerate tangents fall back to the geometric normal
