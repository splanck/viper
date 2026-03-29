# D3D-01: Diffuse Texture Sampling + Vertex Color Modulation

## Current State

The D3D11 pixel shader does not sample a texture at all, and it also ignores `input.color`. That means:

- textured meshes render as flat material color
- per-vertex color is silently dropped on the GPU path

This plan fixes both problems because they belong to the same `baseColor` setup.

## HLSL Changes

Add:

```hlsl
Texture2D diffuseTex : register(t0);
SamplerState texSampler : register(s0);
```

Restructure the pixel shader so `baseColor` and final alpha are built once:

```hlsl
float3 baseColor = diffuseColor.rgb * input.color.rgb;
float texAlpha = 1.0;
float finalAlpha = alpha * input.color.a;
if (hasTexture) {
    float4 texSample = diffuseTex.Sample(texSampler, input.uv);
    baseColor *= texSample.rgb;
    texAlpha = texSample.a;
}
finalAlpha *= texAlpha;
```

Then:

- use `baseColor` in both lit and unlit paths
- return `finalAlpha` instead of plain `alpha`

This layout is intentionally compatible with D3D-07, D3D-08, and D3D-16.

## Sampler State

Create one shared sampler in `create_ctx()`:

- filter: `D3D11_FILTER_MIN_MAG_MIP_LINEAR`
- wrap: `D3D11_TEXTURE_ADDRESS_WRAP`
- `MaxLOD = D3D11_FLOAT32_MAX`

Bind it at slot `s0` during draws.

## Texture Upload Path

Add a helper that converts a `Pixels` object into a `Texture2D + ShaderResourceView` pair:

1. read `w`, `h`, and packed `0xRRGGBBAA` pixels from the runtime object
2. unpack into a sequential RGBA byte buffer
3. create a `DXGI_FORMAT_R8G8B8A8_UNORM` texture
4. create an SRV for that texture

Until D3D-03 lands, textures created for a draw may be temporary and released after the draw.

## Draw Path Rules

- bind diffuse texture SRV at `t0`
- when no texture is present:
  - set `hasTexture = 0`
  - bind a null SRV at `t0` to avoid stale state

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)

## Done When

- textured meshes sample correctly
- untextured meshes still render correctly
- vertex color visibly modulates the material color
