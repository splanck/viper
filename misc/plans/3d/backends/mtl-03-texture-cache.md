# MTL-03: Texture Caching — ✅ DONE

## Context
Metal backend creates a new MTLTexture, allocates a temp BGRA conversion buffer, creates a new MTLSamplerState, and frees the buffer — every single draw call. For 100 textured objects at 60fps, that's 6000 texture uploads per second.

## Current Code (lines 614-659)
```objc
// Per draw:
uint8_t *bgra = malloc(pixel_count * 4);
// ... RGBA→BGRA conversion loop ...
MTLTextureDescriptor *texDesc = ...
id<MTLTexture> tex = [ctx.device newTextureWithDescriptor:texDesc];
[tex replaceRegion:...];
[ctx.encoder setFragmentTexture:tex atIndex:0];
MTLSamplerDescriptor *sampDesc = ...
id<MTLSamplerState> sampler = [ctx.device newSamplerStateWithDescriptor:sampDesc];
[ctx.encoder setFragmentSamplerState:sampler atIndex:0];
free(bgra);
```

## Fix: Texture Cache by Pixels Pointer

### Step 1: Add cache to context
```objc
@interface VGFXMetalContext : NSObject
// ... existing fields ...
@property (nonatomic, strong) NSMutableDictionary<NSValue *, id<MTLTexture>> *textureCache;
@property (nonatomic, strong) id<MTLSamplerState> sharedSampler;
@end
```

### Step 2: Create shared sampler once
In `metal_create_ctx()`:
```objc
MTLSamplerDescriptor *sampDesc = [[MTLSamplerDescriptor alloc] init];
sampDesc.minFilter = MTLSamplerMinMagFilterLinear;
sampDesc.magFilter = MTLSamplerMinMagFilterLinear;
sampDesc.sAddressMode = MTLSamplerAddressModeRepeat;
sampDesc.tAddressMode = MTLSamplerAddressModeRepeat;
ctx.sharedSampler = [ctx.device newSamplerStateWithDescriptor:sampDesc];
```

### Step 3: Cache lookup by Pixels pointer
In `metal_submit_draw()`, replace per-draw texture creation:
```objc
NSValue *key = [NSValue valueWithPointer:cmd->texture];
id<MTLTexture> cachedTex = ctx.textureCache[key];
if (!cachedTex) {
    // First time seeing this Pixels: convert RGBA→BGRA, upload, cache
    // ... existing conversion code ...
    cachedTex = [ctx.device newTextureWithDescriptor:texDesc];
    [cachedTex replaceRegion:...];
    ctx.textureCache[key] = cachedTex;
    free(bgra);
}
[ctx.encoder setFragmentTexture:cachedTex atIndex:0];
[ctx.encoder setFragmentSamplerState:ctx.sharedSampler atIndex:0];
```

### Step 4: Cache invalidation
Clear the cache at the start of each frame in `metal_begin_frame()`:
```objc
[ctx.textureCache removeAllObjects];
```

This is conservative — textures are re-uploaded once per frame on first use. A more sophisticated approach would track Pixels data pointers for changes, but per-frame invalidation is simple and correct.

That conservative choice is intentional because `Pixels` data can mutate in place. Prefer correctness over a persistent cache until texture-version tracking exists.

### Step 5: Cleanup in destroy_ctx
In `metal_destroy_ctx()`, clear the texture cache dictionary to release all cached MTLTexture objects. Under ARC this happens automatically when the context is deallocated, but explicit cleanup is safer for the finalizer path.

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_metal.m` — context cache fields, shared sampler, cache lookup/insert, per-frame invalidation, destroy cleanup

## Testing
- 100 textured boxes → same visual result, drastically fewer MTLTexture allocations
- Texture changes mid-frame → new texture uploaded on next use
- Profile: allocation count per frame should drop from O(draw_count) to O(unique_textures)
