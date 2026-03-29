# D3D-13: HRESULT Error Handling

## Current State

The D3D11 backend still has several resource-creation calls that assume success and continue with null COM pointers on failure.

## Plan Goal

Establish one consistent failure-handling pattern and use it everywhere new D3D resource creation is added, not only in the current `create_ctx()` body.

## Implementation

Add a small helper macro or function:

```c
#define D3D_TRY(hr_expr, msg) do { \
    HRESULT _hr = (hr_expr);       \
    if (FAILED(_hr)) {             \
        /* log msg + _hr */        \
        d3d11_destroy_ctx(ctx);    \
        return NULL;               \
    }                              \
} while (0)
```

Use it on:

- swap-chain back-buffer acquisition
- RTV / DSV creation
- blend / depth / rasterizer state creation
- shader creation
- input-layout creation
- constant-buffer creation
- any later RTT / shadow / postfx resource creation helpers introduced by the plan set

For `D3DCompile`, keep and log the error blob text before cleanup.

For `Map()` in runtime draw paths:

- check the HRESULT and bail out of that draw cleanly instead of assuming success

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)

## Done When

- resource-creation failures return cleanly instead of cascading through null pointers
- error-blob text is surfaced for shader compile failures
