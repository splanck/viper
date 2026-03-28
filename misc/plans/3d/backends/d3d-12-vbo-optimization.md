# D3D-12: VBO/IBO Per-Draw Optimization

## Context
Lines 508-598: every draw call creates a new ID3D11Buffer for vertices and indices, uploads data, draws, then releases. For 100 objects that's 200 buffer creations + 200 releases per frame.

## Current Code
```c
// Per draw:
ID3D11Buffer *vbo, *ibo;
D3D11_BUFFER_DESC vbd = {.ByteWidth = vertex_size, .Usage = D3D11_USAGE_DEFAULT, ...};
ID3D11Device_CreateBuffer(ctx->device, &vbd, &vinitData, &vbo);
// ... same for ibo ...
ID3D11DeviceContext_DrawIndexed(...);
ID3D11Buffer_Release(vbo);
ID3D11Buffer_Release(ibo);
```

## Fix: Dynamic Buffers with Map/Discard

### Step 1: Create persistent dynamic buffers in create_ctx
```c
// Large enough for typical scenes
#define D3D_MAX_VBO_SIZE (1024 * 1024 * 4)  // 4MB vertex buffer
#define D3D_MAX_IBO_SIZE (1024 * 1024)       // 1MB index buffer

D3D11_BUFFER_DESC vbd = {0};
vbd.ByteWidth = D3D_MAX_VBO_SIZE;
vbd.Usage = D3D11_USAGE_DYNAMIC;
vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
vbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
ID3D11Device_CreateBuffer(ctx->device, &vbd, NULL, &ctx->dynamic_vbo);

D3D11_BUFFER_DESC ibd = {0};
ibd.ByteWidth = D3D_MAX_IBO_SIZE;
ibd.Usage = D3D11_USAGE_DYNAMIC;
ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
ibd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
ID3D11Device_CreateBuffer(ctx->device, &ibd, NULL, &ctx->dynamic_ibo);
```

### Step 2: Map/Discard per draw
```c
UINT vbo_size = cmd->vertex_count * sizeof(vgfx3d_vertex_t);
UINT ibo_size = cmd->index_count * sizeof(uint32_t);

if (vbo_size <= D3D_MAX_VBO_SIZE && ibo_size <= D3D_MAX_IBO_SIZE) {
    // Use dynamic buffer with Map/Discard (fastest path)
    D3D11_MAPPED_SUBRESOURCE mapped;
    ID3D11DeviceContext_Map(ctx->context, (ID3D11Resource *)ctx->dynamic_vbo,
                           0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, cmd->vertices, vbo_size);
    ID3D11DeviceContext_Unmap(ctx->context, (ID3D11Resource *)ctx->dynamic_vbo, 0);

    ID3D11DeviceContext_Map(ctx->context, (ID3D11Resource *)ctx->dynamic_ibo,
                           0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, cmd->indices, ibo_size);
    ID3D11DeviceContext_Unmap(ctx->context, (ID3D11Resource *)ctx->dynamic_ibo, 0);

    // Bind persistent buffers
    UINT stride = sizeof(vgfx3d_vertex_t), offset = 0;
    ID3D11DeviceContext_IASetVertexBuffers(ctx->context, 0, 1, &ctx->dynamic_vbo, &stride, &offset);
    ID3D11DeviceContext_IASetIndexBuffer(ctx->context, ctx->dynamic_ibo, DXGI_FORMAT_R32_UINT, 0);
} else {
    // Fallback: create temp buffer for oversized meshes (rare)
    // ... existing per-draw creation code ...
}
```

### Step 3: Clean up
Release dynamic buffers in `d3d11_destroy_ctx()`.

## Performance Impact
- Eliminates 2 * N buffer Create/Release calls per frame
- Map/Discard is the fastest D3D11 pattern for dynamic geometry — the driver can double-buffer internally
- ~4MB VBO handles meshes up to ~50K vertices (80 bytes each)

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_d3d11.c` — dynamic buffers in context, Map/Discard in submit_draw, fallback for oversized meshes

## Testing
- Same visual output as before
- No buffer leaks (Release count matches Create count)
- Profile: fewer API calls per frame
