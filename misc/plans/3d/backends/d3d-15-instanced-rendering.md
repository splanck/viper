# D3D-15: Instanced Rendering

## Context
InstanceBatch3D already loops `submit_draw()` directly in [`src/runtime/graphics/rt_instbatch3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_instbatch3d.c). D3D11 hardware instancing is still worth adding, but the benefit is fewer backend calls and a single indexed draw, not removing deferred-queue work.

## Implementation

### Step 1: Add a shared optional instanced hook
Add `submit_draw_instanced` to [`src/runtime/graphics/vgfx3d_backend.h`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend.h) only as an optional shared hook, then update [`src/runtime/graphics/rt_instbatch3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_instbatch3d.c) to use it when present.

### Step 2: D3D11 implementation with DrawIndexedInstanced
```c
static void d3d11_submit_draw_instanced(void *ctx_ptr, vgfx_window_t win,
    const vgfx3d_draw_cmd_t *cmd,
    const float *instance_matrices, int32_t instance_count,
    const vgfx3d_light_params_t *lights, int32_t light_count,
    const float *ambient, int8_t wireframe, int8_t backface_cull) {

    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;

    // Reuse a cached instance buffer — grow only when needed (avoid per-draw creation)
    size_t needed = instance_count * 64;
    if (!ctx->inst_buf || ctx->inst_buf_size < needed) {
        if (ctx->inst_buf) ID3D11Buffer_Release(ctx->inst_buf);
        D3D11_BUFFER_DESC instDesc = {0};
        instDesc.ByteWidth = (UINT)needed;
        instDesc.Usage = D3D11_USAGE_DYNAMIC;
        instDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        instDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ID3D11Device_CreateBuffer(ctx->device, &instDesc, NULL, &ctx->inst_buf);
        ctx->inst_buf_size = needed;
    }
    D3D11_MAPPED_SUBRESOURCE mapped;
    ID3D11DeviceContext_Map(ctx->context, (ID3D11Resource *)ctx->inst_buf,
                           0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, instance_matrices, needed);
    ID3D11DeviceContext_Unmap(ctx->context, (ID3D11Resource *)ctx->inst_buf, 0);

    // Upload mesh to dynamic VBO/IBO (same as D3D-12 pattern)
    // Then bind mesh VBO at slot 0, instance buffer at slot 1
    UINT strides[2] = {sizeof(vgfx3d_vertex_t), 64};
    UINT offsets[2] = {0, 0};
    ID3D11Buffer *bufs[2] = {ctx->dynamic_vbo, ctx->inst_buf};
    ID3D11DeviceContext_IASetVertexBuffers(ctx->context, 0, 2, bufs, strides, offsets);

    // Draw all instances in one call
    ID3D11DeviceContext_DrawIndexedInstanced(ctx->context,
        cmd->index_count, instance_count, 0, 0, 0);
}
```

### Step 3: Modify vertex shader to read instance matrix
```hlsl
struct VS_INPUT {
    // ... existing per-vertex attributes ...
    // Per-instance data (slot 1):
    row_major float4x4 instanceMatrix : INST_MATRIX;
};

PS_INPUT VSMain(VS_INPUT input) {
    // Use instanceMatrix instead of modelMatrix from cbuffer
    float4 wp = mul(float4(input.pos, 1.0), input.instanceMatrix);
    output.pos = mul(wp, viewProjection);
    // ...
}
```

### Step 4: Modify input layout
Add 4 elements for the instance matrix (each row is a float4):
```c
D3D11_INPUT_ELEMENT_DESC instElements[] = {
    // ... existing 7 per-vertex elements ...
    {"INST_MATRIX", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0,  D3D11_INPUT_PER_INSTANCE_DATA, 1},
    {"INST_MATRIX", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    {"INST_MATRIX", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    {"INST_MATRIX", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1},
};
```

Note: Need a separate input layout + shader variant for instanced draws (the non-instanced path still uses `modelMatrix` from cbuffer).

### Fallback
Backends that do not implement the shared hook should keep the current loop. That includes the software path and any partial GPU backend until the dedicated input-layout/shader variant is ready.

## Files Modified
- `src/runtime/graphics/vgfx3d_backend.h` — optional shared instanced hook
- `src/runtime/graphics/rt_instbatch3d.c` — hook dispatch
- `src/runtime/graphics/vgfx3d_backend_d3d11.c` — instanced draw (loop or hardware)

## Testing
- 100 instanced boxes → all render at correct positions
- Performance: single `DrawIndexedInstanced()` path should beat the current loop on large instance counts
