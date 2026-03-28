# OGL-04: Texture Caching

## Context
OGL-03 creates and destroys GL textures per draw. Need a cache like MTL-03 and D3D-03.

## Implementation

### Cache structure
```c
#define OGL_TEX_CACHE_SIZE 64
typedef struct {
    const void *pixels_ptr;
    GLuint tex_id;
} ogl_tex_cache_entry_t;

// Add to gl_context_t:
ogl_tex_cache_entry_t tex_cache[OGL_TEX_CACHE_SIZE];
int32_t tex_cache_count;
```

### Lookup + insert
```c
static GLuint get_or_create_texture(gl_context_t *ctx, const void *pixels) {
    for (int i = 0; i < ctx->tex_cache_count; i++) {
        if (ctx->tex_cache[i].pixels_ptr == pixels)
            return ctx->tex_cache[i].tex_id;
    }
    // Cache miss — create texture
    GLuint tex;
    gl.GenTextures(1, &tex);
    // ... bind, upload, set params (from OGL-03) ...
    if (ctx->tex_cache_count < OGL_TEX_CACHE_SIZE) {
        ctx->tex_cache[ctx->tex_cache_count].pixels_ptr = pixels;
        ctx->tex_cache[ctx->tex_cache_count].tex_id = tex;
        ctx->tex_cache_count++;
    }
    return tex;
}
```

### Per-frame invalidation
In `gl_begin_frame()`:
```c
for (int i = 0; i < ctx->tex_cache_count; i++)
    gl.DeleteTextures(1, &ctx->tex_cache[i].tex_id);
ctx->tex_cache_count = 0;
```

This conservative invalidation policy is deliberate because `Pixels` data can mutate in place. A persistent cache needs explicit texture-version tracking first.

### Cleanup in destroy_ctx
Release all cached textures on context teardown (same loop as begin_frame invalidation).

## Depends On
- OGL-03 (diffuse texture)

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_opengl.c` — cache struct, lookup, begin_frame invalidation

## Testing
- 100 textured objects same Pixels → only 1 GL texture created
