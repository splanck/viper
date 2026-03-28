# D3D-01: Diffuse Texture Sampling

## Context
The D3D11 pixel shader has NO texture sampling at all. The `hasTexture` flag (line 75) is set in the cbuffer but the shader never reads a texture. Every textured mesh renders as solid color on Windows GPU.

## Implementation

### Step 1: Add texture + sampler declarations to HLSL
After the cbuffer declarations, before VS_INPUT:
```hlsl
Texture2D diffuseTex : register(t0);
SamplerState texSampler : register(s0);
```

### Step 2: Sample in pixel shader
Replace line 116 and add texture sampling in the lit path:
```hlsl
float4 PSMain(PS_INPUT input) : SV_Target {
    float3 baseColor = diffuseColor.rgb;
    float texAlpha = 1.0;
    if (hasTexture) {
        float4 texSample = diffuseTex.Sample(texSampler, input.uv);
        baseColor *= texSample.rgb;
        texAlpha = texSample.a;
    }
    if (unlit) return float4(baseColor, alpha * texAlpha);
    // ... use baseColor instead of diffuseColor.rgb throughout ...
}
```

### Step 3: Create sampler state in create_ctx
```c
D3D11_SAMPLER_DESC sampDesc = {0};
sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
HRESULT hr = ID3D11Device_CreateSamplerState(ctx->device, &sampDesc, &ctx->sampler);
```

Store `ctx->sampler` and bind in submit_draw:
```c
ID3D11DeviceContext_PSSetSamplers(ctx->context, 0, 1, &ctx->sampler);
```

### Step 4: Create SRV from Pixels in submit_draw
When `cmd->texture` is non-NULL:
```c
// Convert Pixels RGBA → BGRA (D3D11 expects BGRA)
// Create D3D11_TEXTURE2D_DESC
// CreateTexture2D → CreateShaderResourceView
// PSSetShaderResources(0, 1, &srv)
```

Full texture creation:
```c
D3D11_TEXTURE2D_DESC texDesc = {0};
texDesc.Width = pw;
texDesc.Height = ph;
texDesc.MipLevels = 1;
texDesc.ArraySize = 1;
texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
texDesc.SampleDesc.Count = 1;
texDesc.Usage = D3D11_USAGE_DEFAULT;
texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

D3D11_SUBRESOURCE_DATA initData = {0};
initData.pSysMem = bgra_buffer;
initData.SysMemPitch = pw * 4;

ID3D11Texture2D *tex;
ID3D11Device_CreateTexture2D(ctx->device, &texDesc, &initData, &tex);

ID3D11ShaderResourceView *srv;
ID3D11Device_CreateShaderResourceView(ctx->device, (ID3D11Resource *)tex, NULL, &srv);
ID3D11DeviceContext_PSSetShaderResources(ctx->context, 0, 1, &srv);

// Release tex and srv after draw (or cache — see D3D-03)
ID3D11Texture2D_Release(tex);
ID3D11ShaderResourceView_Release(srv);
```

### Step 5: RGBA → BGRA conversion
Same pattern as Metal backend — allocate temp buffer, swap R and B channels, upload, free.

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_d3d11.c` — HLSL shader source, sampler creation, SRV creation + binding, pixel format conversion

## Testing
- Textured box → texture visible (was solid color)
- Untextured box → still renders with diffuse color
- Textured + lit → texture modulated by lighting
- Textured + unlit → texture * diffuseColor only
