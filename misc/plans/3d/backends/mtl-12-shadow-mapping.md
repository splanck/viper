# MTL-12: Shadow Mapping

## Context
Same as D3D-14 and SW-05. No backend has shadows. Metal needs a depth-only render pass from light perspective + shadow comparison in fragment shader.

Shared constraint: the deferred draw queue lives in [`src/runtime/graphics/rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c). Metal cannot implement the whole feature only inside `submit_draw()`. Canvas3D must schedule a shadow pass before the normal opaque replay.

## Implementation
Same conceptual approach as D3D-14, adapted to Metal API:

### Shadow depth texture
```objc
MTLTextureDescriptor *desc = [MTLTextureDescriptor
    texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
    width:resolution height:resolution mipmapped:NO];
desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
ctx.shadowDepthTexture = [ctx.device newTextureWithDescriptor:desc];
```

### Shadow render pass
Separate render pass descriptor with only depth attachment:
```objc
MTLRenderPassDescriptor *shadowRP = [MTLRenderPassDescriptor new];
shadowRP.depthAttachment.texture = ctx.shadowDepthTexture;
shadowRP.depthAttachment.loadAction = MTLLoadActionClear;
shadowRP.depthAttachment.clearDepth = 1.0;
shadowRP.depthAttachment.storeAction = MTLStoreActionStore;
```

### Shadow pipeline state and comparison sampler
The shadow pass needs its own pipeline and sampler:
```objc
// Shadow-only pipeline (depth write, no color attachment, no fragment shader)
MTLRenderPipelineDescriptor *shadowPD = [[MTLRenderPipelineDescriptor alloc] init];
shadowPD.vertexFunction = ctx.shadowVertexFunc; // same vertex transform, no fragment
shadowPD.fragmentFunction = nil; // depth-only
shadowPD.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
shadowPD.colorAttachments[0].pixelFormat = MTLPixelFormatInvalid; // no color
ctx.shadowPipeline = [ctx.device newRenderPipelineStateWithDescriptor:shadowPD error:&err];

// Comparison sampler for shadow lookup
MTLSamplerDescriptor *cmpDesc = [[MTLSamplerDescriptor alloc] init];
cmpDesc.compareFunction = MTLCompareFunctionLessEqual;
cmpDesc.minFilter = MTLSamplerMinMagFilterLinear; // PCF-like 2x2 filtering
cmpDesc.magFilter = MTLSamplerMinMagFilterLinear;
ctx.shadowSampler = [ctx.device newSamplerStateWithDescriptor:cmpDesc];
```

Both should be created once in `metal_create_ctx()` and cached.

### Shadow comparison in fragment shader
```metal
depth2d<float> shadowMap [[texture(4)]],
sampler shadowSampler [[sampler(1)]]
// ...
if (scene.shadowEnabled) {
    float4 lightClip = scene.shadowVP * float4(in.worldPos, 1.0);
    float3 shadowUV = lightClip.xyz / lightClip.w;
    shadowUV.xy = shadowUV.xy * 0.5 + 0.5;
    shadowUV.y = 1.0 - shadowUV.y;
    float shadow = shadowMap.sample_compare(shadowSampler, shadowUV.xy, shadowUV.z - scene.shadowBias);
    atten *= mix(0.15, 1.0, shadow);
}
```

### PerScene cbuffer additions
```metal
float4x4 shadowVP;
float shadowBias;
int shadowEnabled;
```

## Runtime Integration
- Reuse the existing shadow enable/resolution/bias state already stored on `rt_canvas3d`
- Restrict v1 to one directional-light shadow map
- Add the shared pass scheduling in `rt_canvas3d.c`; keep the Metal-specific render pass and comparison logic in `vgfx3d_backend_metal.m`

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_metal.m` — shadow texture, shadow pass, fragment comparison, PerScene additions
- `src/runtime/graphics/rt_canvas3d.c` — shared shadow-pass scheduling before the main replay

## Testing
- Same tests as D3D-14 and SW-05
