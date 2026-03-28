# D3D-03: Texture Caching + SRV Management

## Context
D3D-01 creates textures per draw. Need a cache to avoid re-creating ID3D11Texture2D + ID3D11ShaderResourceView every frame for the same Pixels object.

## Implementation

### Cache Structure
```c
#define D3D_TEX_CACHE_SIZE 64
typedef struct {
    const void *pixels_ptr;     // Key: Pixels object pointer
    ID3D11ShaderResourceView *srv;
    ID3D11Texture2D *tex;
} d3d_tex_cache_entry_t;

// Add to d3d11_context_t:
d3d_tex_cache_entry_t tex_cache[D3D_TEX_CACHE_SIZE];
int32_t tex_cache_count;
```

### Lookup + Insert
```c
static ID3D11ShaderResourceView *get_or_create_srv(d3d11_context_t *ctx, const void *pixels) {
    // Search cache
    for (int i = 0; i < ctx->tex_cache_count; i++) {
        if (ctx->tex_cache[i].pixels_ptr == pixels)
            return ctx->tex_cache[i].srv;
    }
    // Cache miss — create texture + SRV
    // ... RGBA→BGRA conversion, CreateTexture2D, CreateShaderResourceView ...
    if (ctx->tex_cache_count < D3D_TEX_CACHE_SIZE) {
        ctx->tex_cache[ctx->tex_cache_count].pixels_ptr = pixels;
        ctx->tex_cache[ctx->tex_cache_count].srv = srv;
        ctx->tex_cache[ctx->tex_cache_count].tex = tex;
        ctx->tex_cache_count++;
    }
    return srv;
}
```

### Per-Frame Invalidation
In `d3d11_begin_frame()`:
```c
// Release all cached textures and SRVs
for (int i = 0; i < ctx->tex_cache_count; i++) {
    ID3D11ShaderResourceView_Release(ctx->tex_cache[i].srv);
    ID3D11Texture2D_Release(ctx->tex_cache[i].tex);
}
ctx->tex_cache_count = 0;
```

This conservative invalidation policy is deliberate because `Pixels` data can mutate in place. A persistent cache should wait for explicit texture-version tracking.

### Cleanup in destroy_ctx
Release all cached textures.

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_d3d11.c` — cache struct, get_or_create_srv, begin_frame invalidation, destroy cleanup

## Testing
- 100 textured objects using same Pixels → only 1 texture created
- Different Pixels → separate cache entries
- No texture leaks (verify Release count matches Create count)
