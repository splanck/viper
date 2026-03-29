# OGL-04: Texture Caching

## Depends On

- OGL-03

## Current State

Once OGL-03 lands, the backend can upload textures, but doing so per draw is too expensive and makes normal/specular/emissive/splat maps much more costly than necessary.

## Required Cache Behavior

Match the current Metal strategy:

- cache by raw `Pixels` object pointer identity
- invalidate the cache every frame
- destroy all cached GL textures on context teardown

This conservative invalidation is intentional because `Pixels` contents can mutate in place and there is no texture versioning yet.

## Implementation

Add to `gl_context_t`:

```c
#define OGL_TEX_CACHE_SIZE 64
typedef struct {
    const void *pixels_ptr;
    GLuint tex_id;
} ogl_tex_cache_entry_t;

ogl_tex_cache_entry_t tex_cache[OGL_TEX_CACHE_SIZE];
int32_t tex_cache_count;
```

Add a helper:

```c
static GLuint ogl_get_or_create_texture(gl_context_t *ctx,
                                        const void *pixels,
                                        int *out_temporary);
```

Behavior:

1. Return cached texture on pointer hit.
2. On miss, create via the OGL-03 upload helper.
3. If cache has room:
   - insert into cache
   - set `*out_temporary = 0`
4. If cache is full:
   - return a temporary uncached texture
   - set `*out_temporary = 1`
   - caller deletes it after the draw

## Frame Lifetime

At the top of `gl_begin_frame()`:

- delete every cached texture from the previous frame
- clear `tex_cache_count`

At `gl_destroy_ctx()`:

- delete any remaining cached textures

## Why This Plan Is Explicit About Overflow

The earlier plan did not define what happens when more than 64 unique textures are used in one frame. That needs a deterministic rule so the implementation does not silently leak or stop rendering.

## Files

- [`src/runtime/graphics/vgfx3d_backend_opengl.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_opengl.c)

## Done When

- Repeated use of the same `Pixels` object within one frame reuses one GL texture
- Cache entries are destroyed at frame boundaries and on teardown
- Cache overflow does not leak textures
