# D3D-14: Shadow Mapping

## Context
No backend implements shadow mapping. D3D11 needs a depth-only render pass from the light's perspective, then a shadow comparison in the main pixel shader.

Shared constraint: [`src/runtime/graphics/rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c) owns the deferred draw queue and currently replays it only once. D3D11 cannot implement the full feature purely inside `submit_draw()`. Canvas3D needs to schedule the shadow prepass.

## Implementation

### Step 1: Shadow map depth texture
```c
// Add to d3d11_context_t:
ID3D11Texture2D *shadow_tex;
ID3D11DepthStencilView *shadow_dsv;
ID3D11ShaderResourceView *shadow_srv;
int32_t shadow_resolution;  // default 1024
float shadow_vp[16];        // light view-projection matrix
int8_t shadow_enabled;
```

Create in `d3d11_enable_shadows()`:
```c
D3D11_TEXTURE2D_DESC desc = {0};
desc.Width = desc.Height = resolution;
desc.MipLevels = 1;
desc.ArraySize = 1;
desc.Format = DXGI_FORMAT_R32_TYPELESS;
desc.SampleDesc.Count = 1;
desc.Usage = D3D11_USAGE_DEFAULT;
desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
ID3D11Device_CreateTexture2D(ctx->device, &desc, NULL, &ctx->shadow_tex);

// DSV for depth writing
D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {0};
dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
ID3D11Device_CreateDepthStencilView(ctx->device, (ID3D11Resource *)ctx->shadow_tex, &dsvDesc, &ctx->shadow_dsv);

// SRV for shadow comparison in main pass
D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {0};
srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
srvDesc.Texture2D.MipLevels = 1;
ID3D11Device_CreateShaderResourceView(ctx->device, (ID3D11Resource *)ctx->shadow_tex, &srvDesc, &ctx->shadow_srv);
```

### Step 2: Shadow depth-only shader
```hlsl
// Separate VS/PS for shadow pass (depth only, no lighting)
float4 ShadowVS(VS_INPUT input) : SV_POSITION {
    float4 wp = mul(float4(input.pos, 1.0), modelMatrix);
    return mul(wp, shadowViewProjection);
}
// No pixel shader needed — depth-only pass (or minimal PS that returns nothing)
```

Compile this as a second shader pair in `create_ctx()`.

### Step 3: Shadow pass in begin_frame / end_frame
Before the main draw pass, re-render all opaque geometry with the shadow shaders to the shadow DSV:
```c
// Set shadow render target (depth-only, no color)
ID3D11DeviceContext_OMSetRenderTargets(ctx->context, 0, NULL, ctx->shadow_dsv);
// Set shadow viewport (shadow_resolution x shadow_resolution)
// For each opaque draw command: draw with shadow VS, skip PS
```

This requires a shared scheduling change in `rt_canvas3d.c`. The simplest approach is to add explicit shadow-pass hooks or an equivalent replay path that walks the opaque deferred draws twice: once for shadow depth, once for the main pass.

### Step 4: Shadow comparison in main pixel shader
```hlsl
Texture2D shadowMap : register(t5);
SamplerComparisonState shadowSampler : register(s1);

// In PerScene cbuffer:
row_major float4x4 shadowVP;
float shadowBias;
int shadowEnabled;

// In PSMain, after computing lighting:
if (shadowEnabled) {
    float4 lightClip = mul(float4(input.worldPos, 1.0), shadowVP);
    float3 shadowUV = lightClip.xyz / lightClip.w;
    shadowUV.xy = shadowUV.xy * 0.5 + 0.5;
    shadowUV.y = 1.0 - shadowUV.y; // D3D UV flip
    if (shadowUV.x >= 0 && shadowUV.x <= 1 && shadowUV.y >= 0 && shadowUV.y <= 1) {
        float shadow = shadowMap.SampleCmpLevelZero(shadowSampler, shadowUV.xy, shadowUV.z - shadowBias);
        // shadow = 0 (in shadow) or 1 (lit)
        atten *= lerp(0.15, 1.0, shadow); // 15% ambient in shadow
    }
}
```

### Step 5: Light VP matrix computation
Build orthographic projection from the first directional light:
```c
// View: lookAt(scene_center - light_dir * dist, scene_center, up)
// Projection: ortho covering scene bounding box
```

Store in `ctx->shadow_vp[16]` and pass to PerScene cbuffer.

### Step 6: Comparison sampler state
```c
D3D11_SAMPLER_DESC cmpDesc = {0};
cmpDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
cmpDesc.AddressU = cmpDesc.AddressV = cmpDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
cmpDesc.BorderColor[0] = 1.0f; // Outside shadow map = lit
cmpDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
ID3D11Device_CreateSamplerState(ctx->device, &cmpDesc, &ctx->shadow_sampler);
```

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_d3d11.c` — shadow texture/DSV/SRV, shadow shader, shadow pass, comparison in PSMain, sampler state
- `src/runtime/graphics/rt_canvas3d.c` — shared shadow-pass scheduling before main replay

## Testing
- Directional light + ground plane + box → box casts shadow on plane
- Shadow bias test: too low = acne, too high = peter-panning
- No shadow when disabled → identical to before
