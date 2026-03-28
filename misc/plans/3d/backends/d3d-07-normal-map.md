# D3D-07: Normal Map Sampling

## Context
Same gap as Metal (MTL-04). Tangent available in VS_INPUT but not passed to PS_INPUT. No normal map texture binding.

## Implementation

### Step 1: Pass tangent through PS_INPUT
```hlsl
struct PS_INPUT {
    float4 pos      : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 uv       : TEXCOORD2;
    float3 tangent  : TEXCOORD3;  // NEW
    float4 color    : COLOR;
};
// In VSMain:
output.tangent = mul(float4(input.tangent, 0.0), modelMatrix).xyz;
```

### Step 2: Add normal map SRV at slot t1
```hlsl
Texture2D normalTex : register(t1);
```

### Step 3: Add hasNormalMap to PerMaterial cbuffer
```hlsl
int hasNormalMap;
```

### Step 4: Sample and perturb normal in pixel shader
```hlsl
float3 N = normalize(input.normal);
if (hasNormalMap) {
    float3 T = normalize(input.tangent);
    T = normalize(T - N * dot(T, N));
    float3 B = cross(N, T);
    float3 mapN = normalTex.Sample(texSampler, input.uv).rgb * 2.0 - 1.0;
    N = normalize(T * mapN.x + B * mapN.y + N * mapN.z);
}
```

If `T` degenerates after orthonormalization, skip the normal-map path for that pixel.

### Step 5: Bind normal map SRV in C code
Same pattern as diffuse SRV creation (D3D-01), at slot 1.

## Depends On
- D3D-01 (diffuse texture — establishes SRV creation pattern)
- D3D-03 (texture cache — avoids per-draw creation)

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_d3d11.c` — PS_INPUT tangent, HLSL t1 slot, pixel shader TBN, C-side SRV binding

## Testing
- Same tests as MTL-04
