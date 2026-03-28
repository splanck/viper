# D3D-05: Wireframe Mode + Backface Culling Toggle

## Context
Both `wireframe` and `backface_cull` parameters are void-cast at line 497. D3D11 requires different rasterizer states for each combination.

## Current Code (line 497)
```c
(void)wireframe;
(void)backface_cull;
```

## Implementation

### Pre-create 4 rasterizer states in create_ctx
```c
// Solid + back-cull (default)
D3D11_RASTERIZER_DESC rs = {0};
rs.FillMode = D3D11_FILL_SOLID;
rs.CullMode = D3D11_CULL_BACK;
rs.FrontCounterClockwise = TRUE;
CreateRasterizerState(&rs, &ctx->rs_solid_cull);

// Solid + no cull (two-sided)
rs.CullMode = D3D11_CULL_NONE;
CreateRasterizerState(&rs, &ctx->rs_solid_nocull);

// Wireframe + back-cull
rs.FillMode = D3D11_FILL_WIREFRAME;
rs.CullMode = D3D11_CULL_BACK;
CreateRasterizerState(&rs, &ctx->rs_wire_cull);

// Wireframe + no cull
rs.CullMode = D3D11_CULL_NONE;
CreateRasterizerState(&rs, &ctx->rs_wire_nocull);
```

### Select per draw in submit_draw
```c
ID3D11RasterizerState *rs;
if (wireframe) {
    rs = backface_cull ? ctx->rs_wire_cull : ctx->rs_wire_nocull;
} else {
    rs = backface_cull ? ctx->rs_solid_cull : ctx->rs_solid_nocull;
}
ID3D11DeviceContext_RSSetState(ctx->context, rs);
```

### Clean up in destroy_ctx
Release all 4 rasterizer states.

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_d3d11.c` — 4 rasterizer states in context, create in create_ctx, select in submit_draw, release in destroy_ctx

## Testing
- wireframe=true → mesh renders as wireframe
- backface_cull=false → both sides of a plane visible
- Combination: wireframe + two-sided → wireframe from both directions
