# D3D-13: HRESULT Error Checking

## Context
~10 D3D11 creation calls don't check HRESULT. If any fail silently, subsequent code operates on NULL pointers.

## Unchecked Calls (from audit)
- Line 269: `IDXGISwapChain_GetBuffer`
- Line 270: `CreateRenderTargetView`
- Line 285: `CreateTexture2D` (depth buffer)
- Line 286: `CreateDepthStencilView`
- Line 295: `CreateDepthStencilState` (opaque)
- Line 299: `CreateDepthStencilState` (transparent)
- Line 312: `CreateBlendState`
- Line 325: `CreateRasterizerState`
- Lines 366-375: `CreateVertexShader` / `CreatePixelShader`
- Lines 387-392: `CreateInputLayout`
- Lines 405-411: `CreateBuffer` (4 cbuffers)

## Fix
Wrap each call with FAILED() check. On failure, clean up already-created objects and return NULL from create_ctx:

```c
#define D3D_CHECK(hr, msg) do { \
    if (FAILED(hr)) { \
        /* Log error message */ \
        d3d11_destroy_ctx(ctx); \
        return NULL; \
    } \
} while(0)

// Example:
hr = ID3D11Device_CreateRenderTargetView(ctx->device, (ID3D11Resource *)backBuf, NULL, &ctx->rtv);
D3D_CHECK(hr, "CreateRenderTargetView failed");
```

Apply to all 10+ unchecked calls. The macro calls destroy_ctx which already handles NULL checks on all COM objects, so partial cleanup is safe.

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_d3d11.c` — add FAILED() checks to all creation calls in create_ctx

## Testing
- Normal creation path → no change in behavior
- Simulated failure (e.g., invalid format) → returns NULL instead of crashing
