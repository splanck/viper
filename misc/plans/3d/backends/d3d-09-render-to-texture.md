# D3D-09: Render-to-Texture

## Context
`d3d11_set_render_target()` at lines 613-616 is completely empty — both parameters void-cast. Metal has a full RTT implementation. D3D11 needs one for offscreen rendering, post-processing prerequisites, and RenderTarget3D.AsPixels().

Binding and unbinding already flow through [`src/runtime/graphics/rt_rendertarget3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_rendertarget3d.c). The D3D11 work here is backend resource management, not new public API.

## Implementation

### Step 1: Add RTT state to context
```c
// Add to d3d11_context_t:
ID3D11Texture2D *rtt_color_tex;
ID3D11RenderTargetView *rtt_rtv;
ID3D11Texture2D *rtt_depth_tex;
ID3D11DepthStencilView *rtt_dsv;
ID3D11Texture2D *rtt_staging;  // For CPU readback
int32_t rtt_width, rtt_height;
int8_t rtt_active;
vgfx3d_rendertarget_t *rtt_target; // Canvas3D target for readback
```

### Step 2: Implement set_render_target
```c
static void d3d11_set_render_target(void *ctx_ptr, vgfx3d_rendertarget_t *rt) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    if (rt) {
        // Release old RTT resources before creating new ones
        // (prevents resource leaks on repeated bind calls with different targets)
        if (ctx->rtt_color_tex) ID3D11Texture2D_Release(ctx->rtt_color_tex);
        if (ctx->rtt_rtv) ID3D11RenderTargetView_Release(ctx->rtt_rtv);
        if (ctx->rtt_depth_tex) ID3D11Texture2D_Release(ctx->rtt_depth_tex);
        if (ctx->rtt_dsv) ID3D11DepthStencilView_Release(ctx->rtt_dsv);
        if (ctx->rtt_staging) ID3D11Texture2D_Release(ctx->rtt_staging);

        // RTT color texture (BGRA8, render target + shader resource)
        D3D11_TEXTURE2D_DESC desc = {0};
        desc.Width = rt->width;
        desc.Height = rt->height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        ID3D11Device_CreateTexture2D(ctx->device, &desc, NULL, &ctx->rtt_color_tex);
        ID3D11Device_CreateRenderTargetView(ctx->device, (ID3D11Resource *)ctx->rtt_color_tex,
                                            NULL, &ctx->rtt_rtv);

        // Depth texture for RTT
        desc.Format = DXGI_FORMAT_D32_FLOAT;
        desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        ID3D11Device_CreateTexture2D(ctx->device, &desc, NULL, &ctx->rtt_depth_tex);
        ID3D11Device_CreateDepthStencilView(ctx->device, (ID3D11Resource *)ctx->rtt_depth_tex,
                                            NULL, &ctx->rtt_dsv);

        // Staging texture for CPU readback
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ID3D11Device_CreateTexture2D(ctx->device, &desc, NULL, &ctx->rtt_staging);

        ctx->rtt_width = rt->width;
        ctx->rtt_height = rt->height;
        ctx->rtt_active = 1;
        ctx->rtt_target = rt;
    } else {
        // Unbind RTT — release resources, restore screen RTV
        if (ctx->rtt_color_tex) ID3D11Texture2D_Release(ctx->rtt_color_tex);
        if (ctx->rtt_rtv) ID3D11RenderTargetView_Release(ctx->rtt_rtv);
        if (ctx->rtt_depth_tex) ID3D11Texture2D_Release(ctx->rtt_depth_tex);
        if (ctx->rtt_dsv) ID3D11DepthStencilView_Release(ctx->rtt_dsv);
        if (ctx->rtt_staging) ID3D11Texture2D_Release(ctx->rtt_staging);
        ctx->rtt_color_tex = NULL;
        ctx->rtt_rtv = NULL;
        ctx->rtt_depth_tex = NULL;
        ctx->rtt_dsv = NULL;
        ctx->rtt_staging = NULL;
        ctx->rtt_active = 0;
        ctx->rtt_target = NULL;
    }
}
```

### Step 3: Use RTT render target in begin_frame
```c
if (ctx->rtt_active && ctx->rtt_rtv) {
    ID3D11DeviceContext_OMSetRenderTargets(ctx->context, 1, &ctx->rtt_rtv, ctx->rtt_dsv);
    // Set viewport to RTT dimensions
    D3D11_VIEWPORT vp = {0, 0, (float)ctx->rtt_width, (float)ctx->rtt_height, 0, 1};
    ID3D11DeviceContext_RSSetViewports(ctx->context, 1, &vp);
} else {
    // Normal screen render target
    ID3D11DeviceContext_OMSetRenderTargets(ctx->context, 1, &ctx->rtv, ctx->dsv);
}
```

### Step 4: Readback in end_frame
```c
if (ctx->rtt_active && ctx->rtt_target && ctx->rtt_staging) {
    // Copy RTT texture → staging texture (GPU→CPU transfer)
    ID3D11DeviceContext_CopyResource(ctx->context,
        (ID3D11Resource *)ctx->rtt_staging, (ID3D11Resource *)ctx->rtt_color_tex);

    // Map staging texture for CPU read
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = ID3D11DeviceContext_Map(ctx->context,
        (ID3D11Resource *)ctx->rtt_staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (SUCCEEDED(hr)) {
        // Copy BGRA → RGBA into rt->color_buf
        uint8_t *dst = ctx->rtt_target->color_buf;
        uint8_t *src = (uint8_t *)mapped.pData;
        for (int y = 0; y < ctx->rtt_height; y++) {
            for (int x = 0; x < ctx->rtt_width; x++) {
                int si = y * mapped.RowPitch + x * 4;
                int di = (y * ctx->rtt_width + x) * 4;
                dst[di + 0] = src[si + 2]; // R
                dst[di + 1] = src[si + 1]; // G
                dst[di + 2] = src[si + 0]; // B
                dst[di + 3] = src[si + 3]; // A
            }
        }
        ID3D11DeviceContext_Unmap(ctx->context, (ID3D11Resource *)ctx->rtt_staging, 0);
    }
}
```

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_d3d11.c` — RTT context fields, set_render_target, begin_frame RTV selection, end_frame readback, destroy cleanup
- `src/runtime/graphics/rt_rendertarget3d.c` — no API change expected; only verify it remains the single binding path

## Testing
- Render scene to 256x256 RTT → AsPixels returns correct image
- Switch between RTT and screen rendering → both produce correct output
- Depth testing works in RTT (objects behind others are occluded)
