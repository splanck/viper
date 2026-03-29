# D3D-03: Texture Cache + SRV Management

## Depends On

- D3D-01

## Current State

Once D3D-01 lands, the backend can create textures, but doing that per draw is too expensive and would make later normal/specular/emissive/splat paths unnecessarily costly.

## Required Cache Behavior

Match the current Metal strategy:

- cache by raw `Pixels` pointer identity
- invalidate the cache every frame
- release all cached resources on context teardown

This stays conservative because `Pixels` data can mutate in place and there is no version tracking yet.

## Implementation

Add to `d3d11_context_t`:

```c
#define D3D_TEX_CACHE_SIZE 64
typedef struct {
    const void *pixels_ptr;
    ID3D11Texture2D *tex;
    ID3D11ShaderResourceView *srv;
} d3d_tex_cache_entry_t;

d3d_tex_cache_entry_t tex_cache[D3D_TEX_CACHE_SIZE];
int32_t tex_cache_count;
```

Add a helper:

```c
static ID3D11ShaderResourceView *d3d_get_or_create_srv(d3d11_context_t *ctx,
                                                       const void *pixels,
                                                       int *out_temporary);
```

Behavior:

1. return cached SRV on pointer hit
2. on miss, create texture + SRV via the D3D-01 upload helper
3. if cache has room:
   - store it
   - `*out_temporary = 0`
4. if cache is full:
   - return an uncached temporary SRV
   - `*out_temporary = 1`
   - caller releases it after the draw

## Frame Lifetime

At the top of `d3d11_begin_frame()`:

- release every cached `tex` and `srv`
- clear `tex_cache_count`

At `d3d11_destroy_ctx()`:

- release any remaining cached entries

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)

## Done When

- repeated use of the same `Pixels` object within one frame reuses one SRV
- cache entries are released at frame boundaries and teardown
- cache overflow does not leak resources
