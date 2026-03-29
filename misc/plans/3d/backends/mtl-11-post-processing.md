# MTL-11: GPU Post-Processing Pipeline — ✅ DONE

## Context
Post-processing effects (bloom, FXAA, tonemap, vignette, color grade, SSAO, DOF) are implemented in `rt_postfx3d.c` but only applied to the software rasterizer's CPU framebuffer. Metal renders to a CAMetalDrawable and presents directly — no post-processing step.

The important integration constraint is that `rt_postfx3d.c` keeps its effect-chain layout private today. The Metal backend should not inspect that private struct directly. Export a compact backend-facing snapshot/helper API from [`src/runtime/graphics/rt_postfx3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_postfx3d.c) and [`src/runtime/graphics/rt_postfx3d.h`](/Users/stephen/git/viper/src/runtime/graphics/rt_postfx3d.h) first.

## Current CPU Path
`rt_postfx3d_apply()` reads/writes the software framebuffer pixel array:
```c
// For each effect in the chain:
if (type == POSTFX_BLOOM) apply_bloom(buf, w, h, params);
if (type == POSTFX_FXAA) apply_fxaa(buf, w, h);
// etc.
```

## Implementation: Offscreen Render → Post-Process → Present

### Step 1: Render to offscreen texture instead of drawable
The backend already has an RTT (render-to-texture) pattern at `metal_set_render_target()` (lines 781-827) that creates offscreen color + depth textures. Follow that pattern.

In `metal_begin_frame()`, when post-processing is enabled:
```objc
if (ctx.postFXEnabled) {
    // Cache offscreen texture — only recreate on size change
    if (!ctx.postfxColor || ctx.postfxColor.width != w || ctx.postfxColor.height != h) {
        MTLTextureDescriptor *desc = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
            width:w height:h mipmapped:NO];
        desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        ctx.postfxColor = [ctx.device newTextureWithDescriptor:desc];
    }
    // Render pass writes to offscreen instead of drawable
    rp.colorAttachments[0].texture = ctx.postfxColor;
}
```

### Step 2: Full-screen quad shader for post-processing
Add a second shader pair for full-screen quad rendering:
```metal
struct FullscreenVert {
    float4 position [[position]];
    float2 uv;
};

vertex FullscreenVert fullscreen_vs(uint vid [[vertex_id]]) {
    // Triangle strip covering NDC [-1,1]
    float2 positions[4] = {float2(-1,-1), float2(1,-1), float2(-1,1), float2(1,1)};
    float2 uvs[4] = {float2(0,1), float2(1,1), float2(0,0), float2(1,0)};
    FullscreenVert out;
    out.position = float4(positions[vid], 0, 1);
    out.uv = uvs[vid];
    return out;
}

fragment float4 postfx_fs(
    FullscreenVert in [[stage_in]],
    texture2d<float> sceneColor [[texture(0)]],
    sampler s [[sampler(0)]],
    constant PostFXParams &params [[buffer(0)]]
) {
    float4 color = sceneColor.sample(s, in.uv);

    // Bloom: bright-pass extract + gaussian blur (simplified single-pass)
    if (params.bloomEnabled) {
        float brightness = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
        if (brightness > params.bloomThreshold) {
            // Simplified: just boost bright pixels (proper bloom needs multi-pass blur)
            color.rgb += (color.rgb - params.bloomThreshold) * params.bloomStrength;
        }
    }

    // Tone mapping (Reinhard)
    if (params.tonemapMode == 1) {
        color.rgb = color.rgb / (color.rgb + 1.0);
    }
    // ACES filmic
    if (params.tonemapMode == 2) {
        float3 x = color.rgb;
        color.rgb = (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14);
    }

    // Color grading
    if (params.colorGradeEnabled) {
        color.rgb *= params.colorMult.rgb;
    }

    // Vignette
    if (params.vignetteEnabled) {
        float2 center = in.uv - 0.5;
        float dist = length(center);
        float vig = 1.0 - smoothstep(params.vignetteRadius, params.vignetteRadius + 0.2, dist);
        color.rgb *= mix(1.0, vig, params.vignetteIntensity);
    }

    // FXAA (simplified luma-based edge detection)
    if (params.fxaaEnabled) {
        float2 texelSize = float2(1.0 / sceneColor.get_width(), 1.0 / sceneColor.get_height());
        float3 rgbN = sceneColor.sample(s, in.uv + float2(0, -texelSize.y)).rgb;
        float3 rgbS = sceneColor.sample(s, in.uv + float2(0,  texelSize.y)).rgb;
        float3 rgbE = sceneColor.sample(s, in.uv + float2( texelSize.x, 0)).rgb;
        float3 rgbW = sceneColor.sample(s, in.uv + float2(-texelSize.x, 0)).rgb;
        float3 luma = float3(0.299, 0.587, 0.114);
        float lumaN = dot(rgbN, luma), lumaS = dot(rgbS, luma);
        float lumaE = dot(rgbE, luma), lumaW = dot(rgbW, luma);
        float lumaC = dot(color.rgb, luma);
        float lumaRange = max(max(lumaN, lumaS), max(lumaE, lumaW)) - min(min(lumaN, lumaS), min(lumaE, lumaW));
        if (lumaRange > 0.05) {
            float2 dir = float2(-(lumaN - lumaS), lumaE - lumaW);
            float dirLen = max(abs(dir.x), abs(dir.y));
            dir = clamp(dir / dirLen, -1.0, 1.0) * texelSize;
            float3 avg = 0.5 * (sceneColor.sample(s, in.uv + dir * 0.5).rgb +
                                 sceneColor.sample(s, in.uv - dir * 0.5).rgb);
            color.rgb = avg;
        }
    }

    return color;
}
```

### Step 3: PostFX parameters struct
```metal
struct PostFXParams {
    int bloomEnabled;
    float bloomThreshold, bloomStrength;
    int tonemapMode; // 0=off, 1=reinhard, 2=aces
    float tonemapExposure;
    int colorGradeEnabled;
    float4 colorMult;
    int vignetteEnabled;
    float vignetteIntensity, vignetteRadius;
    int fxaaEnabled;
};
```

### Step 4: Post-process pass in end_frame
In `metal_end_frame()`, when post-processing is enabled:
```objc
if (ctx.postFXEnabled && ctx.offscreenColor) {
    // New render pass: draw fullscreen quad to drawable with postfx shader
    MTLRenderPassDescriptor *postRP = [MTLRenderPassDescriptor new];
    postRP.colorAttachments[0].texture = ctx.drawable.texture;
    postRP.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    postRP.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLRenderCommandEncoder> postEnc = [ctx.cmdBuf renderCommandEncoderWithDescriptor:postRP];
    [postEnc setRenderPipelineState:ctx.postfxPipeline];
    [postEnc setFragmentTexture:ctx.offscreenColor atIndex:0];
    [postEnc setFragmentSamplerState:ctx.sharedSampler atIndex:0];
    [postEnc setFragmentBytes:&postfxParams length:sizeof(postfxParams) atIndex:0];
    [postEnc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    [postEnc endEncoding];
}
```

### Step 5: Wire PostFX3D to Metal backend
`Canvas3D.SetPostFX(postfx)` already stores the PostFX3D object. Do not parse the private `rt_postfx3d` struct from Metal code. Instead, add a helper in `rt_postfx3d.c` that exports a backend-friendly `PostFXParams` snapshot (or a smaller effect list) and feed that into the Metal post-process pass.

### Step 6: Compile post-process pipeline
In `metal_create_ctx()`, compile the fullscreen vertex + postfx fragment shader and create a separate MTLRenderPipelineState for post-processing.

## Scope Note
v1 should only port effects that are naturally expressible as a single post pass or a small fixed pass chain: bloom, tone mapping, FXAA, color grade, vignette. SSAO, DOF, and motion blur need extra depth/history inputs and should be explicitly left for a later phase unless this plan is expanded to include those resources.

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_metal.m` — offscreen texture, postfx shader, fullscreen quad, end_frame post-pass
- `src/runtime/graphics/rt_postfx3d.c` — export backend-facing PostFX snapshot/helper
- `src/runtime/graphics/rt_postfx3d.h` — declare backend-facing PostFX snapshot/helper

## Testing
- Bloom: bright objects glow
- FXAA: edges smooth (compare with/without)
- Vignette: screen edges darken
- Tonemap: HDR colors compressed to displayable range
- No PostFX → renders directly to drawable (no performance cost)

## Performance
One extra fullscreen quad draw per frame. For bloom with multi-pass gaussian blur, need 2-3 extra passes (downscale → blur H → blur V → composite). Simplified single-pass bloom is less accurate but free.
