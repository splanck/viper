# D3D-11: GPU Post-Processing Pipeline

## Context
Same gap as Metal (MTL-11). Post-processing only works on software backend. D3D11 renders directly to swap chain back buffer with no post-process pass.

As with Metal, the backend should not inspect private `rt_postfx3d.c` internals directly. Export a compact backend-facing snapshot/helper API from [`src/runtime/graphics/rt_postfx3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_postfx3d.c) and [`src/runtime/graphics/rt_postfx3d.h`](/Users/stephen/git/viper/src/runtime/graphics/rt_postfx3d.h) first.

## Implementation

### Step 1: Offscreen render target
In `d3d11_begin_frame()`, when post-processing is enabled, create offscreen texture and render to it instead of the swap chain RTV:
```c
if (ctx->postfx_enabled) {
    // Create offscreen BGRA8 texture + RTV (same size as back buffer)
    // Render pass targets offscreen instead of swap chain
    ID3D11DeviceContext_OMSetRenderTargets(ctx->context, 1, &ctx->postfx_rtv, ctx->dsv);
}
```

### Step 2: Fullscreen quad shader
Add a second HLSL shader pair:
```hlsl
struct FS_VS_OUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

FS_VS_OUT FullscreenVS(uint vid : SV_VertexID) {
    float2 positions[4] = {float2(-1,-1), float2(1,-1), float2(-1,1), float2(1,1)};
    float2 uvs[4] = {float2(0,1), float2(1,1), float2(0,0), float2(1,0)};
    FS_VS_OUT out;
    out.pos = float4(positions[vid], 0, 1);
    out.uv = uvs[vid];
    return out;
}

cbuffer PostFXParams : register(b0) {
    int bloomEnabled;
    float bloomThreshold, bloomStrength;
    int tonemapMode;
    float tonemapExposure;
    int colorGradeEnabled;
    float4 colorMult;
    int vignetteEnabled;
    float vignetteIntensity, vignetteRadius;
    int fxaaEnabled;
};

Texture2D sceneColor : register(t0);
SamplerState sceneSampler : register(s0);

float4 PostFXPS(FS_VS_OUT input) : SV_Target {
    float4 color = sceneColor.Sample(sceneSampler, input.uv);

    // Bloom (simplified bright-pass boost)
    if (bloomEnabled) {
        float brightness = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
        if (brightness > bloomThreshold) {
            color.rgb += (color.rgb - bloomThreshold) * bloomStrength;
        }
    }

    // Tonemap
    if (tonemapMode == 1) { // Reinhard
        color.rgb = color.rgb / (color.rgb + 1.0);
    } else if (tonemapMode == 2) { // ACES
        float3 x = color.rgb * tonemapExposure;
        color.rgb = (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14);
    }

    // Color grading
    if (colorGradeEnabled) color.rgb *= colorMult.rgb;

    // Vignette
    if (vignetteEnabled) {
        float2 center = input.uv - 0.5;
        float dist = length(center);
        float vig = 1.0 - smoothstep(vignetteRadius, vignetteRadius + 0.2, dist);
        color.rgb *= lerp(1.0, vig, vignetteIntensity);
    }

    return color;
}
```

### Step 3: Compile post-process shaders in create_ctx
Compile `FullscreenVS` and `PostFXPS` alongside the main shaders. Create a second pipeline state (input layout not needed — SV_VertexID only).

### Step 4: Post-process pass in end_frame
```c
if (ctx->postfx_enabled && ctx->postfx_offscreen_srv) {
    // Switch render target to swap chain back buffer
    ID3D11DeviceContext_OMSetRenderTargets(ctx->context, 1, &ctx->rtv, NULL);
    // Bind offscreen as shader resource
    ID3D11DeviceContext_PSSetShaderResources(ctx->context, 0, 1, &ctx->postfx_offscreen_srv);
    ID3D11DeviceContext_PSSetSamplers(ctx->context, 0, 1, &ctx->sampler);
    // Set postfx cbuffer
    // ... map/unmap PostFXParams ...
    // Set postfx shaders
    ID3D11DeviceContext_VSSetShader(ctx->context, ctx->postfx_vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx->context, ctx->postfx_ps, NULL, 0);
    // Draw fullscreen quad (4 vertices, triangle strip, no VBO — SV_VertexID)
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx->context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D11DeviceContext_Draw(ctx->context, 4, 0);
    // Restore main shaders for next frame
}
```

### Step 5: Wire PostFX3D params
Same as MTL-11 — export a backend-facing PostFX snapshot from `rt_postfx3d.c` and translate that into the D3D11 `PostFXParams` cbuffer.

## Scope Note
v1 should explicitly target bloom, tone mapping, FXAA, color grade, and vignette. SSAO, DOF, and motion blur require extra depth/history inputs and should be deferred unless this plan grows to include those resources.

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_d3d11.c` — offscreen texture, postfx shaders, fullscreen quad draw, end_frame post-pass
- `src/runtime/graphics/rt_postfx3d.c` — export backend-facing PostFX snapshot/helper
- `src/runtime/graphics/rt_postfx3d.h` — declare backend-facing PostFX snapshot/helper

## Testing
- Same tests as MTL-11
