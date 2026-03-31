//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/vgfx3d_backend_metal.m
// Purpose: Metal GPU backend for Viper.Graphics3D.
//
// Links: vgfx3d_backend.h, plans/3d/02-metal-backend.md
//
//===----------------------------------------------------------------------===//

#if defined(__APPLE__) && defined(VIPER_ENABLE_GRAPHICS)

/* Include sys/types.h before Metal headers to define BSD types (u_int, u_char)
 * that are hidden by _POSIX_C_SOURCE but required by macOS system headers. */
#include <sys/types.h>

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "vgfx.h"
#include "vgfx3d_backend.h"
#include "rt_postfx3d.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Objective-C wrapper to hold Metal objects under ARC
//=============================================================================

@interface VGFXMetalContext : NSObject
{
  @public
    float _vp[16];
    float _camPos[3];
    /* MTL-07: Fog parameters (stored per-frame from begin_frame) */
    float _fogColor[3];
    float _fogNear, _fogFar;
    BOOL _fogEnabled;
    /* MTL-12: Shadow light view-projection (stored from shadow_begin) */
    float _shadowLightVP[16];
}
@property(nonatomic, strong) id<MTLDevice> device;
@property(nonatomic, strong) id<MTLCommandQueue> commandQueue;
@property(nonatomic, strong) id<MTLRenderPipelineState> pipelineState;
@property(nonatomic, strong) id<MTLDepthStencilState> depthState;
@property(nonatomic, strong) id<MTLDepthStencilState> depthStateNoWrite;
@property(nonatomic, strong) CAMetalLayer *metalLayer;
@property(nonatomic, strong) id<MTLTexture> depthTexture;
@property(nonatomic, strong) id<MTLLibrary> library;
@property(nonatomic, strong) id<MTLCommandBuffer> cmdBuf;
@property(nonatomic, strong) id<MTLRenderCommandEncoder> encoder;
@property(nonatomic, strong) id<CAMetalDrawable> drawable;
@property(nonatomic) int32_t width;
@property(nonatomic) int32_t height;
@property(nonatomic) float clearR, clearG, clearB;
@property(nonatomic) BOOL inFrame;
/* Per-frame Metal buffer references — prevents ARC from releasing vertex/index
 * buffers before the GPU finishes executing draw commands. */
@property(nonatomic, strong) NSMutableArray *frameBuffers;
/* Default 1x1 white texture (bound when no material texture is set) */
@property(nonatomic, strong) id<MTLTexture> defaultTexture;
@property(nonatomic, strong) id<MTLSamplerState> defaultSampler;
/* Render-to-texture state */
@property(nonatomic, strong) id<MTLTexture> rttColorTexture;
@property(nonatomic, strong) id<MTLTexture> rttDepthTexture;
@property(nonatomic) BOOL rttActive;
@property(nonatomic) int32_t rttWidth, rttHeight;
@property(nonatomic, assign) vgfx3d_rendertarget_t *rttTarget; /* CPU-side RT for readback */
/* MTL-03: Texture cache (keyed by Pixels pointer, invalidated per-frame) */
@property(nonatomic, strong) NSMutableDictionary<NSValue *, id<MTLTexture>> *textureCache;
@property(nonatomic, strong) id<MTLSamplerState> sharedSampler;
/* MTL-12: Shadow mapping state */
@property(nonatomic, strong) id<MTLTexture> shadowDepthTexture;
@property(nonatomic, strong) id<MTLRenderPipelineState> shadowPipeline;
@property(nonatomic, strong) id<MTLSamplerState> shadowSampler;
@property(nonatomic, strong) id<MTLDepthStencilState> shadowDepthState;
@property(nonatomic) BOOL shadowActive;
@property(nonatomic) float shadowBias;
/* MTL-13: Instanced rendering — pooled instance buffer */
@property(nonatomic, strong) id<MTLBuffer> instanceBuf;
/* MTL-11: Post-processing state */
@property(nonatomic, strong) id<MTLTexture> postfxColorTexture;
@property(nonatomic, strong) id<MTLRenderPipelineState> postfxPipeline;
@property(nonatomic, strong) id<MTLLibrary> postfxLibrary;
@end

@implementation VGFXMetalContext
@end

//=============================================================================
// MSL Shader source (vertex + fragment in two halves to stay under C99 limit)
//=============================================================================

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Woverlength-strings"

static NSString *metal_shader_source =
    @
    "#include <metal_stdlib>\n"
     "using namespace metal;\n"
     "\n"
     "struct VertexIn {\n"
     "    float3 position [[attribute(0)]];\n"
     "    float3 normal   [[attribute(1)]];\n"
     "    float2 uv       [[attribute(2)]];\n"
     "    float4 color    [[attribute(3)]];\n"
     "    float3 tangent  [[attribute(4)]];\n"
     "    uchar4 boneIdx  [[attribute(5)]];\n"
     "    float4 boneWt   [[attribute(6)]];\n"
     "};\n"
     "\n"
     "struct VertexOut {\n"
     "    float4 position [[position]];\n"
     "    float3 worldPos;\n"
     "    float3 normal;\n"
     "    float3 tangent;\n"
     "    float2 uv;\n"
     "    float4 color;\n"
     "};\n"
     "\n"
     "struct PerObject {\n"
     "    float4x4 modelMatrix;\n"
     "    float4x4 viewProjection;\n"
     "    float4x4 normalMatrix;\n"
     "    int hasSkinning;\n"
     "    int morphShapeCount;\n"
     "    int vertexCount;\n"
     "    int _pad;\n"
     "};\n"
     "\n"
     "struct Light {\n"
     "    int type; float _p0, _p1, _p2;\n"
     "    float4 direction;\n"
     "    float4 position;\n"
     "    float4 color;\n"
     "    float intensity;\n"
     "    float attenuation;\n"
     "    float inner_cos;\n"
     "    float outer_cos;\n"
     "};\n"
     "\n"
     "struct PerScene {\n"
     "    float4 cameraPosition;\n"
     "    float4 ambientColor;\n"
     "    float4 fogColor;\n"
     "    float fogNear;\n"
     "    float fogFar;\n"
     "    int lightCount;\n"
     "    int shadowEnabled;\n"
     "    float4x4 shadowVP;\n"
     "    float shadowBias;\n"
     "    float _scenePad[3];\n"
     "};\n"
     "\n"
     "struct PerMaterial {\n"
     "    float4 diffuseColor;\n"
     "    float4 specularColor;\n"
     "    float4 emissiveColor;\n"
     "    float alpha;\n"
     "    int hasTexture;\n"
     "    int unlit;\n"
     "    int hasNormalMap;\n"
     "    int hasSpecularMap;\n"
     "    int hasEmissiveMap;\n"
     "    int hasSplat;\n"
     "    float4 splatScales;\n"
     "};\n"
     "\n"
     "vertex VertexOut vertex_main(\n"
     "    VertexIn in [[stage_in]],\n"
     "    constant PerObject &obj [[buffer(1)]],\n"
     "    constant float4x4 *bonePalette [[buffer(3)]],\n"
     "    constant float *morphDeltas [[buffer(4)]],\n"
     "    constant float *morphWeights [[buffer(5)]],\n"
     "    uint vid [[vertex_id]]\n"
     ") {\n"
     "    float3 pos = in.position;\n"
     "    float3 nrm = in.normal;\n"
     /* --- MTL-10: Morph target application --- */
     "    if (obj.morphShapeCount > 0) {\n"
     "        for (int s = 0; s < obj.morphShapeCount; s++) {\n"
     "            float w = morphWeights[s];\n"
     "            if (w > 0.001) {\n"
     "                int off = s * obj.vertexCount * 3 + int(vid) * 3;\n"
     "                pos.x += morphDeltas[off + 0] * w;\n"
     "                pos.y += morphDeltas[off + 1] * w;\n"
     "                pos.z += morphDeltas[off + 2] * w;\n"
     "            }\n"
     "        }\n"
     "    }\n"
     /* --- MTL-09: GPU skeletal skinning --- */
     "    float4 skinnedPos;\n"
     "    float3 skinnedNrm;\n"
     "    if (obj.hasSkinning != 0) {\n"
     "        skinnedPos = float4(0);\n"
     "        skinnedNrm = float3(0);\n"
     "        for (int i = 0; i < 4; i++) {\n"
     "            int bIdx = in.boneIdx[i];\n"
     "            float bw = in.boneWt[i];\n"
     "            if (bw > 0.001) {\n"
     "                float4x4 bm = bonePalette[bIdx];\n"
     "                skinnedPos += bm * float4(pos, 1.0) * bw;\n"
     "                skinnedNrm += (bm * float4(nrm, 0.0)).xyz * bw;\n"
     "            }\n"
     "        }\n"
     "    } else {\n"
     "        skinnedPos = float4(pos, 1.0);\n"
     "        skinnedNrm = nrm;\n"
     "    }\n"
     "    VertexOut out;\n"
     "    float4 wp = obj.modelMatrix * skinnedPos;\n"
     "    out.position = obj.viewProjection * wp;\n"
     "    /* Remap Z from OpenGL NDC [-1,1] to Metal [0,1] */\n"
     "    out.position.z = out.position.z * 0.5 + out.position.w * 0.5;\n"
     "    out.worldPos = wp.xyz;\n"
     "    out.normal = (obj.normalMatrix * float4(skinnedNrm, 0.0)).xyz;\n"
     "    out.tangent = (obj.modelMatrix * float4(in.tangent, 0.0)).xyz;\n"
     "    out.uv = in.uv;\n"
     "    out.color = in.color;\n"
     "    return out;\n"
     "}\n"
     "\n"
     "fragment float4 fragment_main(\n"
     "    VertexOut in [[stage_in]],\n"
     "    constant PerScene &scene [[buffer(0)]],\n"
     "    constant PerMaterial &material [[buffer(1)]],\n"
     "    constant Light *lights [[buffer(2)]],\n"
     "    texture2d<float> diffuseTex [[texture(0)]],\n"
     "    texture2d<float> normalTex [[texture(1)]],\n"
     "    texture2d<float> specularTex [[texture(2)]],\n"
     "    texture2d<float> emissiveTex [[texture(3)]],\n"
     "    depth2d<float> shadowMap [[texture(4)]],\n"
     "    texture2d<float> splatTex [[texture(5)]],\n"
     "    texture2d<float> splatLayer0 [[texture(6)]],\n"
     "    texture2d<float> splatLayer1 [[texture(7)]],\n"
     "    texture2d<float> splatLayer2 [[texture(8)]],\n"
     "    texture2d<float> splatLayer3 [[texture(9)]],\n"
     "    sampler texSampler [[sampler(0)]],\n"
     "    sampler shadowSampler [[sampler(1)]]\n"
     ") {\n"
     /* --- MTL-01: Sample diffuse texture for both lit and unlit paths --- */
     "    float3 baseColor = material.diffuseColor.rgb;\n"
     "    float texAlpha = 1.0;\n"
     "    if (material.hasTexture) {\n"
     "        float4 texSample = diffuseTex.sample(texSampler, in.uv);\n"
     "        baseColor *= texSample.rgb;\n"
     "        texAlpha = texSample.a;\n"
     "    }\n"
     /* --- MTL-14: Terrain splat — override baseColor if active --- */
     "    if (material.hasSplat) {\n"
     "        float4 sp = splatTex.sample(texSampler, in.uv);\n"
     "        float wsum = sp.r + sp.g + sp.b + sp.a;\n"
     "        if (wsum > 0.001) sp /= wsum;\n"
     "        float3 blended = float3(0);\n"
     "        if (sp.r > 0.001) blended += splatLayer0.sample(texSampler, in.uv * material.splatScales.x).rgb * sp.r;\n"
     "        if (sp.g > 0.001) blended += splatLayer1.sample(texSampler, in.uv * material.splatScales.y).rgb * sp.g;\n"
     "        if (sp.b > 0.001) blended += splatLayer2.sample(texSampler, in.uv * material.splatScales.z).rgb * sp.b;\n"
     "        if (sp.a > 0.001) blended += splatLayer3.sample(texSampler, in.uv * material.splatScales.w).rgb * sp.a;\n"
     "        baseColor = blended * material.diffuseColor.rgb;\n"
     "    }\n"
     "    if (material.unlit) {\n"
     "        return float4(baseColor, material.alpha * texAlpha);\n"
     "    }\n"
     /* --- MTL-04: Normal map sampling with TBN --- */
     "    float3 N = normalize(in.normal);\n"
     "    if (material.hasNormalMap) {\n"
     "        float3 T = normalize(in.tangent);\n"
     "        T = normalize(T - N * dot(T, N));\n"
     "        float lenT = length(T);\n"
     "        if (lenT > 0.001) {\n"
     "            float3 B = cross(N, T);\n"
     "            float3 mapN = normalTex.sample(texSampler, in.uv).rgb * 2.0 - 1.0;\n"
     "            N = normalize(T * mapN.x + B * mapN.y + N * mapN.z);\n"
     "        }\n"
     "    }\n"
     "    float3 V = normalize(scene.cameraPosition.xyz - in.worldPos);\n"
     "    float3 result = scene.ambientColor.rgb * baseColor;\n"
     /* --- MTL-05: Specular map setup --- */
     "    float3 specColor = material.specularColor.rgb;\n"
     "    if (material.hasSpecularMap) {\n"
     "        specColor *= specularTex.sample(texSampler, in.uv).rgb;\n"
     "    }\n"
     "    for (int i = 0; i < scene.lightCount; i++) {\n"
     "        float3 L; float atten = 1.0;\n"
     "        if (lights[i].type == 0) {\n"
     "            L = normalize(-lights[i].direction.xyz);\n"
     /* --- MTL-02: Point light --- */
     "        } else if (lights[i].type == 1) {\n"
     "            float3 tl = lights[i].position.xyz - in.worldPos;\n"
     "            float d = length(tl); L = tl / max(d, 0.0001);\n"
     "            atten = 1.0 / (1.0 + lights[i].attenuation * d * d);\n"
     /* --- MTL-02: Spot light with cone attenuation --- */
     "        } else if (lights[i].type == 3) {\n"
     "            float3 tl = lights[i].position.xyz - in.worldPos;\n"
     "            float d = length(tl); L = tl / max(d, 0.0001);\n"
     "            atten = 1.0 / (1.0 + lights[i].attenuation * d * d);\n"
     "            float spotDot = dot(-L, normalize(lights[i].direction.xyz));\n"
     "            if (spotDot < lights[i].outer_cos) {\n"
     "                atten = 0.0;\n"
     "            } else if (spotDot < lights[i].inner_cos) {\n"
     "                float t = (spotDot - lights[i].outer_cos) / "
     "(lights[i].inner_cos - lights[i].outer_cos);\n"
     "                atten *= t * t * (3.0 - 2.0 * t);\n"
     "            }\n"
     "        } else {\n"
     "            result += lights[i].color.rgb * lights[i].intensity * baseColor;\n"
     "            continue;\n"
     "        }\n"
     "        float NdotL = max(dot(N, L), 0.0);\n"
     /* --- MTL-12: Shadow mapping attenuation (directional light only) --- */
     "        if (scene.shadowEnabled != 0 && lights[i].type == 0) {\n"
     "            float4 lc = scene.shadowVP * float4(in.worldPos, 1.0);\n"
     "            float3 suv = lc.xyz / lc.w;\n"
     "            suv.xy = suv.xy * 0.5 + 0.5;\n"
     "            suv.y = 1.0 - suv.y;\n"
     "            if (suv.x >= 0.0 && suv.x <= 1.0 && suv.y >= 0.0 && suv.y <= 1.0) {\n"
     "                float shadow = shadowMap.sample_compare(shadowSampler, suv.xy, suv.z - scene.shadowBias);\n"
     "                atten *= mix(0.15, 1.0, shadow);\n"
     "            }\n"
     "        }\n"
     "        result += lights[i].color.rgb * lights[i].intensity * NdotL * "
     "baseColor * atten;\n"
     "        if (NdotL > 0.0 && material.specularColor.w > 0.0) {\n"
     "            float3 H = normalize(L + V);\n"
     "            float spec = pow(max(dot(N, H), 0.0), material.specularColor.w);\n"
     "            result += lights[i].color.rgb * lights[i].intensity * spec * "
     "specColor * atten;\n"
     "        }\n"
     "    }\n"
     /* --- MTL-06: Emissive map --- */
     "    float3 emissive = material.emissiveColor.rgb;\n"
     "    if (material.hasEmissiveMap) {\n"
     "        emissive *= emissiveTex.sample(texSampler, in.uv).rgb;\n"
     "    }\n"
     "    result += emissive;\n"
     /* --- MTL-07: Linear distance fog --- */
     "    if (scene.fogColor.a > 0.5) {\n"
     "        float dist = length(in.worldPos - scene.cameraPosition.xyz);\n"
     "        float fogRange = scene.fogFar - scene.fogNear;\n"
     "        float fogFactor = clamp((dist - scene.fogNear) / max(fogRange, 0.001), "
     "0.0, 1.0);\n"
     "        result = mix(result, scene.fogColor.rgb, fogFactor);\n"
     "    }\n"
     "    return float4(result, material.alpha * texAlpha);\n"
     "}\n";

#pragma clang diagnostic pop

//=============================================================================
// Uniform buffer structs (must match shader)
//=============================================================================

typedef struct
{
    float m[16];
    float vp[16];
    float nm[16];
    int32_t hasSkinning;
    int32_t morphShapeCount;
    int32_t vertexCount;
    int32_t _obj_pad;
} mtl_per_object_t;

typedef struct
{
    float cp[4];
    float ac[4];
    float fc[4]; /* fogColor: .rgb = color, .a = enabled flag */
    float fog_near;
    float fog_far;
    int32_t lc;
    int32_t shadowEnabled;
    float shadowVP[16]; /* light view-projection (column-major for Metal) */
    float shadowBias;
    float _scenePad[3];
} mtl_per_scene_t;

typedef struct
{
    int32_t type;
    float _p0, _p1, _p2;
    float dir[4];
    float pos[4];
    float col[4];
    float intensity;
    float attenuation;
    float inner_cos;
    float outer_cos;
} mtl_light_t;

typedef struct
{
    float dc[4];
    float sc[4];
    float ec[4];
    float alpha;
    int32_t ht;
    int32_t unlit;
    int32_t hasNormalMap;
    int32_t hasSpecularMap;
    int32_t hasEmissiveMap;
    int32_t hasSplat;
    float splatScales[4];
} mtl_per_material_t;

//=============================================================================
// Helpers
//=============================================================================

static void transpose4x4(const float *src, float *dst)
{
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            dst[c * 4 + r] = src[r * 4 + c];
}

static void mat4f_mul(const float *a, const float *b, float *out)
{
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            out[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c] +
                             a[r * 4 + 2] * b[2 * 4 + c] + a[r * 4 + 3] * b[3 * 4 + c];
}

//=============================================================================
// Vertex descriptor (80-byte vgfx3d_vertex_t)
//=============================================================================

static MTLVertexDescriptor *create_vertex_descriptor(void)
{
    MTLVertexDescriptor *d = [[MTLVertexDescriptor alloc] init];
    d.attributes[0].format = MTLVertexFormatFloat3;
    d.attributes[0].offset = 0;
    d.attributes[0].bufferIndex = 0;
    d.attributes[1].format = MTLVertexFormatFloat3;
    d.attributes[1].offset = 12;
    d.attributes[1].bufferIndex = 0;
    d.attributes[2].format = MTLVertexFormatFloat2;
    d.attributes[2].offset = 24;
    d.attributes[2].bufferIndex = 0;
    d.attributes[3].format = MTLVertexFormatFloat4;
    d.attributes[3].offset = 32;
    d.attributes[3].bufferIndex = 0;
    d.attributes[4].format = MTLVertexFormatFloat3;
    d.attributes[4].offset = 48;
    d.attributes[4].bufferIndex = 0;
    d.attributes[5].format = MTLVertexFormatUChar4;
    d.attributes[5].offset = 60;
    d.attributes[5].bufferIndex = 0;
    d.attributes[6].format = MTLVertexFormatFloat4;
    d.attributes[6].offset = 64;
    d.attributes[6].bufferIndex = 0;
    d.layouts[0].stride = 80;
    d.layouts[0].stepRate = 1;
    d.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
    return d;
}

//=============================================================================
// Backend vtable
//=============================================================================

static void *metal_create_ctx(vgfx_window_t win, int32_t w, int32_t h)
{
    @autoreleasepool
    {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device)
            return NULL;

        NSView *view = (__bridge NSView *)vgfx_get_native_view(win);
        if (!view)
            return NULL;

        VGFXMetalContext *ctx = [[VGFXMetalContext alloc] init];
        ctx.device = device;
        ctx.width = w;
        ctx.height = h;

        view.wantsLayer = YES;
        CAMetalLayer *layer = [CAMetalLayer layer];
        layer.device = device;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = YES;
        layer.drawableSize = CGSizeMake((CGFloat)w, (CGFloat)h);
        layer.frame = view.bounds;
        layer.opaque = NO; /* Allow software renderer to show through when Metal isn't active */
        /* Add as sublayer on top of existing content (don't replace view.layer) */
        [view.layer addSublayer:layer];
        ctx.metalLayer = layer;

        ctx.commandQueue = [device newCommandQueue];
        if (!ctx.commandQueue)
            return NULL;

        NSError *error = nil;
        ctx.library = [device newLibraryWithSource:metal_shader_source options:nil error:&error];
        if (!ctx.library)
        {
            NSLog(@ "[Metal] Shader error: %@", error);
            return NULL;
        }
        /* Shaders compiled successfully */

        id<MTLFunction> vf = [ctx.library newFunctionWithName:@ "vertex_main"];
        id<MTLFunction> ff = [ctx.library newFunctionWithName:@ "fragment_main"];
        if (!vf || !ff)
            return NULL;

        MTLRenderPipelineDescriptor *pd = [[MTLRenderPipelineDescriptor alloc] init];
        pd.vertexFunction = vf;
        pd.fragmentFunction = ff;
        pd.vertexDescriptor = create_vertex_descriptor();
        pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        /* Enable alpha blending: src*srcAlpha + dst*(1-srcAlpha) */
        pd.colorAttachments[0].blendingEnabled = YES;
        pd.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        pd.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        pd.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        pd.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

        ctx.pipelineState = [device newRenderPipelineStateWithDescriptor:pd error:&error];
        if (!ctx.pipelineState)
        {
            NSLog(@ "Metal pipeline error: %@", error);
            return NULL;
        }

        MTLDepthStencilDescriptor *dd = [[MTLDepthStencilDescriptor alloc] init];
        dd.depthCompareFunction = MTLCompareFunctionLess;
        dd.depthWriteEnabled = YES;
        ctx.depthState = [device newDepthStencilStateWithDescriptor:dd];
        /* Depth state for transparent draws: test ON, write OFF */
        dd.depthWriteEnabled = NO;
        ctx.depthStateNoWrite = [device newDepthStencilStateWithDescriptor:dd];

        /* Depth texture */
        MTLTextureDescriptor *td =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                               width:(NSUInteger)w
                                                              height:(NSUInteger)h
                                                           mipmapped:NO];
        td.usage = MTLTextureUsageRenderTarget;
        td.storageMode = MTLStorageModePrivate;
        ctx.depthTexture = [device newTextureWithDescriptor:td];

        /* Default 1x1 white texture (bound when no material texture is set,
         * so the shader's texture2d parameter is always valid) */
        {
            MTLTextureDescriptor *dtd =
                [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                   width:1
                                                                  height:1
                                                               mipmapped:NO];
            dtd.usage = MTLTextureUsageShaderRead;
            dtd.storageMode = MTLStorageModeShared;
            ctx.defaultTexture = [device newTextureWithDescriptor:dtd];
            uint32_t white = 0xFFFFFFFF;
            [ctx.defaultTexture replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
                                  mipmapLevel:0
                                    withBytes:&white
                                  bytesPerRow:4];

            MTLSamplerDescriptor *sd = [[MTLSamplerDescriptor alloc] init];
            sd.minFilter = MTLSamplerMinMagFilterLinear;
            sd.magFilter = MTLSamplerMinMagFilterLinear;
            ctx.defaultSampler = [device newSamplerStateWithDescriptor:sd];
        }

        /* MTL-03: Shared sampler (linear filter, repeat wrap) used for all textures */
        {
            MTLSamplerDescriptor *sd = [[MTLSamplerDescriptor alloc] init];
            sd.minFilter = MTLSamplerMinMagFilterLinear;
            sd.magFilter = MTLSamplerMinMagFilterLinear;
            sd.sAddressMode = MTLSamplerAddressModeRepeat;
            sd.tAddressMode = MTLSamplerAddressModeRepeat;
            ctx.sharedSampler = [device newSamplerStateWithDescriptor:sd];
        }

        /* MTL-12: Shadow pipeline (depth-only, no fragment shader) */
        {
            MTLRenderPipelineDescriptor *spd = [[MTLRenderPipelineDescriptor alloc] init];
            spd.vertexFunction = vf;
            spd.fragmentFunction = nil;
            spd.vertexDescriptor = create_vertex_descriptor();
            spd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
            spd.colorAttachments[0].pixelFormat = MTLPixelFormatInvalid;
            NSError *shadowErr = nil;
            ctx.shadowPipeline = [device newRenderPipelineStateWithDescriptor:spd
                                                                       error:&shadowErr];
            /* Non-fatal: shadow mapping disabled if pipeline fails */

            /* Comparison sampler for shadow lookup */
            MTLSamplerDescriptor *csd = [[MTLSamplerDescriptor alloc] init];
            csd.compareFunction = MTLCompareFunctionLessEqual;
            csd.minFilter = MTLSamplerMinMagFilterLinear;
            csd.magFilter = MTLSamplerMinMagFilterLinear;
            ctx.shadowSampler = [device newSamplerStateWithDescriptor:csd];

            /* Shadow depth-stencil state (always-write, less compare) */
            MTLDepthStencilDescriptor *sdd = [[MTLDepthStencilDescriptor alloc] init];
            sdd.depthCompareFunction = MTLCompareFunctionLess;
            sdd.depthWriteEnabled = YES;
            ctx.shadowDepthState = [device newDepthStencilStateWithDescriptor:sdd];
        }

        /* MTL-11: Post-processing fullscreen quad shader + pipeline */
        {
            NSString *postfxSrc = @
                "#include <metal_stdlib>\n"
                "using namespace metal;\n"
                "struct FullscreenVert { float4 position [[position]]; float2 uv; };\n"
                "struct PostFXParams {\n"
                "    int bloomEnabled; float bloomThreshold; float bloomStrength;\n"
                "    int tonemapMode; float tonemapExposure;\n"
                "    int fxaaEnabled;\n"
                "    int colorGradeEnabled; float cgBright; float cgContrast; float cgSat;\n"
                "    int vignetteEnabled; float vigRadius; float vigSoftness;\n"
                "};\n"
                "vertex FullscreenVert fullscreen_vs(uint vid [[vertex_id]]) {\n"
                "    float2 positions[4] = {float2(-1,-1), float2(1,-1), float2(-1,1), float2(1,1)};\n"
                "    float2 uvs[4] = {float2(0,1), float2(1,1), float2(0,0), float2(1,0)};\n"
                "    FullscreenVert out;\n"
                "    out.position = float4(positions[vid], 0, 1);\n"
                "    out.uv = uvs[vid];\n"
                "    return out;\n"
                "}\n"
                "fragment float4 postfx_fs(\n"
                "    FullscreenVert in [[stage_in]],\n"
                "    texture2d<float> sceneTex [[texture(0)]],\n"
                "    sampler s [[sampler(0)]],\n"
                "    constant PostFXParams &p [[buffer(0)]]\n"
                ") {\n"
                "    float4 color = sceneTex.sample(s, in.uv);\n"
                "    if (p.bloomEnabled) {\n"
                "        float br = dot(color.rgb, float3(0.2126,0.7152,0.0722));\n"
                "        if (br > p.bloomThreshold)\n"
                "            color.rgb += (color.rgb - p.bloomThreshold) * p.bloomStrength;\n"
                "    }\n"
                "    if (p.tonemapMode == 1) color.rgb = color.rgb / (color.rgb + 1.0);\n"
                "    if (p.tonemapMode == 2) {\n"
                "        float3 x = color.rgb;\n"
                "        color.rgb = (x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14);\n"
                "    }\n"
                "    if (p.colorGradeEnabled) {\n"
                "        color.rgb = (color.rgb - 0.5) * p.cgContrast + 0.5;\n"
                "        color.rgb += p.cgBright;\n"
                "        float luma = dot(color.rgb, float3(0.299,0.587,0.114));\n"
                "        color.rgb = mix(float3(luma), color.rgb, p.cgSat);\n"
                "    }\n"
                "    if (p.vignetteEnabled) {\n"
                "        float2 ctr = in.uv - 0.5;\n"
                "        float d = length(ctr);\n"
                "        float vig = 1.0 - smoothstep(p.vigRadius, p.vigRadius + p.vigSoftness, d);\n"
                "        color.rgb *= vig;\n"
                "    }\n"
                "    if (p.fxaaEnabled) {\n"
                "        float2 ts = float2(1.0/sceneTex.get_width(), 1.0/sceneTex.get_height());\n"
                "        float3 rgbN = sceneTex.sample(s, in.uv + float2(0,-ts.y)).rgb;\n"
                "        float3 rgbS = sceneTex.sample(s, in.uv + float2(0, ts.y)).rgb;\n"
                "        float3 rgbE = sceneTex.sample(s, in.uv + float2( ts.x,0)).rgb;\n"
                "        float3 rgbW = sceneTex.sample(s, in.uv + float2(-ts.x,0)).rgb;\n"
                "        float3 lv = float3(0.299,0.587,0.114);\n"
                "        float lN=dot(rgbN,lv),lS=dot(rgbS,lv),lE=dot(rgbE,lv),lW=dot(rgbW,lv);\n"
                "        float lC = dot(color.rgb, lv);\n"
                "        float lr = max(max(lN,lS),max(lE,lW)) - min(min(lN,lS),min(lE,lW));\n"
                "        if (lr > 0.05) {\n"
                "            float2 dir = float2(-(lN-lS), lE-lW);\n"
                "            float dl = max(abs(dir.x),abs(dir.y));\n"
                "            dir = clamp(dir/dl, -1.0, 1.0) * ts;\n"
                "            color.rgb = 0.5*(sceneTex.sample(s,in.uv+dir*0.5).rgb+"
                "sceneTex.sample(s,in.uv-dir*0.5).rgb);\n"
                "        }\n"
                "    }\n"
                "    return color;\n"
                "}\n";

            NSError *pfxErr = nil;
            ctx.postfxLibrary = [device newLibraryWithSource:postfxSrc options:nil error:&pfxErr];
            if (ctx.postfxLibrary)
            {
                id<MTLFunction> pfxVS = [ctx.postfxLibrary newFunctionWithName:@ "fullscreen_vs"];
                id<MTLFunction> pfxFS = [ctx.postfxLibrary newFunctionWithName:@ "postfx_fs"];
                if (pfxVS && pfxFS)
                {
                    MTLRenderPipelineDescriptor *ppd = [[MTLRenderPipelineDescriptor alloc] init];
                    ppd.vertexFunction = pfxVS;
                    ppd.fragmentFunction = pfxFS;
                    ppd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
                    ctx.postfxPipeline = [device newRenderPipelineStateWithDescriptor:ppd
                                                                               error:&pfxErr];
                }
            }
            /* Non-fatal: postfx disabled if pipeline compilation fails */
        }

        /* Backend initialized successfully */
        return (__bridge_retained void *)ctx;
    }
}

static void metal_destroy_ctx(void *ctx_ptr)
{
    if (!ctx_ptr)
        return;
    @autoreleasepool
    {
        VGFXMetalContext *ctx = (__bridge_transfer VGFXMetalContext *)ctx_ptr;
        (void)ctx; /* ARC releases all properties */
    }
}

static void metal_clear(void *ctx_ptr, vgfx_window_t win, float r, float g, float b)
{
    (void)win;
    VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
    ctx.clearR = r;
    ctx.clearG = g;
    ctx.clearB = b;
}

static void metal_begin_frame(void *ctx_ptr, const vgfx3d_camera_params_t *cam)
{
    @autoreleasepool
    {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
        if (!ctx)
            return;

        float vp[16];
        mat4f_mul(cam->projection, cam->view, vp);
        memcpy(ctx->_vp, vp, sizeof(float) * 16);
        memcpy(ctx->_camPos, cam->position, sizeof(float) * 3);

        /* MTL-07: Store fog parameters for submit_draw */
        ctx->_fogEnabled = cam->fog_enabled;
        ctx->_fogNear = cam->fog_near;
        ctx->_fogFar = cam->fog_far;
        ctx->_fogColor[0] = cam->fog_color[0];
        ctx->_fogColor[1] = cam->fog_color[1];
        ctx->_fogColor[2] = cam->fog_color[2];

        /* Clear per-frame buffer references from previous frame */
        ctx.frameBuffers = [NSMutableArray arrayWithCapacity:32];

        /* Reset shadow state for this frame (may be re-enabled by shadow_begin) */
        ctx.shadowActive = NO;

        /* MTL-03: Invalidate texture cache each frame (Pixels data can mutate) */
        if (!ctx.textureCache)
            ctx.textureCache = [NSMutableDictionary dictionaryWithCapacity:32];
        else
            [ctx.textureCache removeAllObjects];

        /* Reuse the command buffer if one is already open (multi-pass frame).
         * RTT end_frame commits and nils the cmdBuf, so on-screen passes
         * after an RTT pass will create a fresh one. */
        if (!ctx.cmdBuf)
        {
            ctx.cmdBuf = [ctx.commandQueue commandBuffer];
            if (!ctx.cmdBuf)
                return;
        }

        MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];

        if (ctx.rttActive && ctx.rttColorTexture)
        {
            /* Offscreen RTT path: render to GPU textures, skip drawable */
            rp.colorAttachments[0].texture = ctx.rttColorTexture;
            rp.colorAttachments[0].loadAction = MTLLoadActionClear;
            rp.colorAttachments[0].storeAction = MTLStoreActionStore;
            rp.colorAttachments[0].clearColor =
                MTLClearColorMake(ctx.clearR, ctx.clearG, ctx.clearB, 1.0);
            rp.depthAttachment.texture = ctx.rttDepthTexture;
            rp.depthAttachment.loadAction = MTLLoadActionClear;
            rp.depthAttachment.storeAction = MTLStoreActionDontCare;
            rp.depthAttachment.clearDepth = 1.0;
            ctx.drawable = nil; /* no drawable for offscreen */
        }
        else
        {
            /* On-screen path: reuse existing drawable across multi-pass frames.
             * Only acquire a new one for the first on-screen pass. */
            if (!ctx.drawable)
            {
                ctx.drawable = [ctx.metalLayer nextDrawable];
                if (!ctx.drawable)
                    return;
            }

            rp.colorAttachments[0].texture = ctx.drawable.texture;
            rp.colorAttachments[0].loadAction = MTLLoadActionClear;
            rp.colorAttachments[0].storeAction = MTLStoreActionStore;
            /* Clear with alpha=0 so software framebuffer (skybox) shows through */
            rp.colorAttachments[0].clearColor =
                MTLClearColorMake(ctx.clearR, ctx.clearG, ctx.clearB, 0.0);
            rp.depthAttachment.texture = ctx.depthTexture;
            rp.depthAttachment.loadAction = MTLLoadActionClear;
            rp.depthAttachment.storeAction = MTLStoreActionDontCare;
            rp.depthAttachment.clearDepth = 1.0;
        }

        ctx.encoder = [ctx.cmdBuf renderCommandEncoderWithDescriptor:rp];
        if (!ctx.encoder)
            return;
        [ctx.encoder setRenderPipelineState:ctx.pipelineState];
        [ctx.encoder setDepthStencilState:ctx.depthState];

        /* Set viewport to match the current render target dimensions */
        {
            double vw, vh;
            if (ctx.rttActive)
            {
                vw = (double)ctx.rttWidth;
                vh = (double)ctx.rttHeight;
            }
            else
            {
                vw = (double)ctx.drawable.texture.width;
                vh = (double)ctx.drawable.texture.height;
            }
            MTLViewport viewport = {0, 0, vw, vh, 0.0, 1.0};
            [ctx.encoder setViewport:viewport];
        }
        /* The signed area formula preserves winding sense regardless of Y direction:
         * CCW in NDC (Y-up) remains CCW in screen (Y-down). Our meshes use CCW
         * winding, so MTLWindingCounterClockwise marks CCW as front-facing. */
        [ctx.encoder setFrontFacingWinding:MTLWindingCounterClockwise];
        [ctx.encoder setCullMode:MTLCullModeBack];
        ctx.inFrame = YES;
    }
}

/// MTL-03: Retrieve or create a cached MTLTexture from a Pixels object pointer.
/// Cache is keyed by the raw void* (Pixels identity). Conversion is RGBA→BGRA.
static id<MTLTexture> metal_get_cached_texture(VGFXMetalContext *ctx, const void *pixels_ptr)
{
    NSValue *key = [NSValue valueWithPointer:pixels_ptr];
    id<MTLTexture> cached = ctx.textureCache[key];
    if (cached)
        return cached;

    typedef struct
    {
        int64_t w;
        int64_t h;
        uint32_t *data;
    } px_view_t;

    const px_view_t *pv = (const px_view_t *)pixels_ptr;
    if (!pv->data || pv->w <= 0 || pv->h <= 0)
        return nil;

    int32_t tw = (int32_t)pv->w, th = (int32_t)pv->h;
    size_t pixel_count = (size_t)tw * (size_t)th;
    uint8_t *bgra = (uint8_t *)malloc(pixel_count * 4);
    if (!bgra)
        return nil;

    for (size_t i = 0; i < pixel_count; i++)
    {
        uint32_t px = pv->data[i];                      /* 0xRRGGBBAA */
        bgra[i * 4 + 0] = (uint8_t)((px >> 8) & 0xFF);  /* B */
        bgra[i * 4 + 1] = (uint8_t)((px >> 16) & 0xFF); /* G */
        bgra[i * 4 + 2] = (uint8_t)((px >> 24) & 0xFF); /* R */
        bgra[i * 4 + 3] = (uint8_t)(px & 0xFF);         /* A */
    }

    MTLTextureDescriptor *texDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:(NSUInteger)tw
                                                          height:(NSUInteger)th
                                                       mipmapped:NO];
    texDesc.usage = MTLTextureUsageShaderRead;
    texDesc.storageMode = MTLStorageModeShared;
    id<MTLTexture> tex = [ctx.device newTextureWithDescriptor:texDesc];
    [tex replaceRegion:MTLRegionMake2D(0, 0, (NSUInteger)tw, (NSUInteger)th)
           mipmapLevel:0
             withBytes:bgra
           bytesPerRow:(NSUInteger)(tw * 4)];
    free(bgra);

    if (tex)
        ctx.textureCache[key] = tex;
    return tex;
}

static void metal_submit_draw(void *ctx_ptr,
                              vgfx_window_t win,
                              const vgfx3d_draw_cmd_t *cmd,
                              const vgfx3d_light_params_t *lights,
                              int32_t light_count,
                              const float *ambient,
                              int8_t wireframe,
                              int8_t backface_cull)
{
    @autoreleasepool
    {
        (void)win;
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
        if (!ctx || !ctx.encoder || !ctx.inFrame)
            return;

        [ctx.encoder setCullMode:backface_cull ? MTLCullModeBack : MTLCullModeNone];

        /* MTL-08: Wireframe mode via fill mode toggle */
        [ctx.encoder setTriangleFillMode:wireframe ? MTLTriangleFillModeLines
                                                   : MTLTriangleFillModeFill];

        /* Switch depth state: no Z-write for transparent draws */
        if (cmd->alpha < 1.0f)
            [ctx.encoder setDepthStencilState:ctx.depthStateNoWrite];
        else
            [ctx.encoder setDepthStencilState:ctx.depthState];

        id<MTLBuffer> vb =
            [ctx.device newBufferWithBytes:cmd->vertices
                                    length:cmd->vertex_count * sizeof(vgfx3d_vertex_t)
                                   options:MTLResourceStorageModeShared];
        id<MTLBuffer> ib = [ctx.device newBufferWithBytes:cmd->indices
                                                   length:cmd->index_count * sizeof(uint32_t)
                                                  options:MTLResourceStorageModeShared];
        [ctx.encoder setVertexBuffer:vb offset:0 atIndex:0];

        /* Retain buffers until frame commit — prevents ARC from releasing
         * them before the GPU executes the draw commands. */
        if (ctx.frameBuffers)
        {
            [ctx.frameBuffers addObject:vb];
            [ctx.frameBuffers addObject:ib];
        }

        /* Per-object uniforms (includes MTL-09/10 skinning+morph flags) */
        mtl_per_object_t obj;
        memset(&obj, 0, sizeof(obj));
        transpose4x4(cmd->model_matrix, obj.m);
        float vp_t[16];
        transpose4x4(ctx->_vp, vp_t);
        memcpy(obj.vp, vp_t, sizeof(float) * 16);
        memcpy(obj.nm, obj.m, sizeof(float) * 16); /* normal matrix = model (transposed) */
        int capped_bone_count = cmd->bone_count > 128 ? 128 : cmd->bone_count;
        obj.hasSkinning = (cmd->bone_palette && capped_bone_count > 0) ? 1 : 0;
        obj.morphShapeCount = cmd->morph_shape_count;
        obj.vertexCount = (int32_t)cmd->vertex_count;
        [ctx.encoder setVertexBytes:&obj length:sizeof(obj) atIndex:1];

        /* MTL-09: Bind bone palette if skinning active */
        if (obj.hasSkinning)
        {
            size_t bsz = (size_t)capped_bone_count * 16 * sizeof(float);
            id<MTLBuffer> boneBuf = [ctx.device newBufferWithBytes:cmd->bone_palette
                                                            length:bsz
                                                           options:MTLResourceStorageModeShared];
            [ctx.encoder setVertexBuffer:boneBuf offset:0 atIndex:3];
            if (ctx.frameBuffers)
                [ctx.frameBuffers addObject:boneBuf];
        }

        /* MTL-10: Bind morph deltas/weights if morph active */
        if (cmd->morph_deltas && cmd->morph_shape_count > 0)
        {
            size_t dsz =
                (size_t)cmd->morph_shape_count * cmd->vertex_count * 3 * sizeof(float);
            id<MTLBuffer> deltaBuf = [ctx.device newBufferWithBytes:cmd->morph_deltas
                                                              length:dsz
                                                             options:MTLResourceStorageModeShared];
            [ctx.encoder setVertexBuffer:deltaBuf offset:0 atIndex:4];
            size_t wsz = (size_t)cmd->morph_shape_count * sizeof(float);
            id<MTLBuffer> wtBuf = [ctx.device newBufferWithBytes:cmd->morph_weights
                                                           length:wsz
                                                          options:MTLResourceStorageModeShared];
            [ctx.encoder setVertexBuffer:wtBuf offset:0 atIndex:5];
            if (ctx.frameBuffers)
            {
                [ctx.frameBuffers addObject:deltaBuf];
                [ctx.frameBuffers addObject:wtBuf];
            }
        }

        /* Per-scene (includes MTL-07 fog) */
        mtl_per_scene_t scene;
        memset(&scene, 0, sizeof(scene));
        memcpy(scene.cp, ctx->_camPos, sizeof(float) * 3);
        scene.ac[0] = ambient[0];
        scene.ac[1] = ambient[1];
        scene.ac[2] = ambient[2];
        scene.fc[0] = ctx->_fogColor[0];
        scene.fc[1] = ctx->_fogColor[1];
        scene.fc[2] = ctx->_fogColor[2];
        scene.fc[3] = ctx->_fogEnabled ? 1.0f : 0.0f;
        scene.fog_near = ctx->_fogNear;
        scene.fog_far = ctx->_fogFar;
        scene.lc = light_count;
        /* MTL-12: Shadow mapping */
        scene.shadowEnabled = ctx.shadowActive ? 1 : 0;
        scene.shadowBias = ctx.shadowBias;
        if (ctx.shadowActive)
            transpose4x4(ctx->_shadowLightVP, scene.shadowVP);
        [ctx.encoder setFragmentBytes:&scene length:sizeof(scene) atIndex:0];

        /* Per-material (includes MTL-04/05/06 map flags) */
        mtl_per_material_t mat;
        memset(&mat, 0, sizeof(mat));
        memcpy(mat.dc, cmd->diffuse_color, sizeof(float) * 4);
        mat.sc[0] = cmd->specular[0];
        mat.sc[1] = cmd->specular[1];
        mat.sc[2] = cmd->specular[2];
        mat.sc[3] = cmd->shininess;
        mat.ec[0] = cmd->emissive_color[0];
        mat.ec[1] = cmd->emissive_color[1];
        mat.ec[2] = cmd->emissive_color[2];
        mat.ec[3] = 0;
        mat.alpha = cmd->alpha;
        mat.ht = cmd->texture ? 1 : 0;
        mat.unlit = cmd->unlit;
        mat.hasNormalMap = cmd->normal_map ? 1 : 0;
        mat.hasSpecularMap = cmd->specular_map ? 1 : 0;
        mat.hasEmissiveMap = cmd->emissive_map ? 1 : 0;
        mat.hasSplat = cmd->has_splat;
        if (cmd->has_splat) {
            for (int si = 0; si < 4; si++)
                mat.splatScales[si] = cmd->splat_layer_scales[si];
        }
        [ctx.encoder setFragmentBytes:&mat length:sizeof(mat) atIndex:1];

        /* Bind default textures to all 10 slots (shader requires valid textures) */
        for (int slot = 0; slot < 10; slot++)
            [ctx.encoder setFragmentTexture:ctx.defaultTexture atIndex:slot];
        /* MTL-12: Bind shadow depth texture to slot 4 */
        if (ctx.shadowActive && ctx.shadowDepthTexture)
            [ctx.encoder setFragmentTexture:ctx.shadowDepthTexture atIndex:4];
        [ctx.encoder setFragmentSamplerState:ctx.sharedSampler atIndex:0];
        if (ctx.shadowSampler)
            [ctx.encoder setFragmentSamplerState:ctx.shadowSampler atIndex:1];

        /* MTL-03: Bind cached textures for each material map slot */
        if (cmd->texture)
        {
            id<MTLTexture> tex = metal_get_cached_texture(ctx, cmd->texture);
            if (tex)
                [ctx.encoder setFragmentTexture:tex atIndex:0];
        }
        if (cmd->normal_map)
        {
            id<MTLTexture> tex = metal_get_cached_texture(ctx, cmd->normal_map);
            if (tex)
                [ctx.encoder setFragmentTexture:tex atIndex:1];
        }
        if (cmd->specular_map)
        {
            id<MTLTexture> tex = metal_get_cached_texture(ctx, cmd->specular_map);
            if (tex)
                [ctx.encoder setFragmentTexture:tex atIndex:2];
        }
        if (cmd->emissive_map)
        {
            id<MTLTexture> tex = metal_get_cached_texture(ctx, cmd->emissive_map);
            if (tex)
                [ctx.encoder setFragmentTexture:tex atIndex:3];
        }
        /* MTL-14: Terrain splat textures (slots 5-9) */
        if (cmd->has_splat && cmd->splat_map)
        {
            id<MTLTexture> sp = metal_get_cached_texture(ctx, cmd->splat_map);
            if (sp)
                [ctx.encoder setFragmentTexture:sp atIndex:5];
            for (int si = 0; si < 4; si++)
            {
                if (cmd->splat_layers[si])
                {
                    id<MTLTexture> lt = metal_get_cached_texture(ctx, cmd->splat_layers[si]);
                    if (lt)
                        [ctx.encoder setFragmentTexture:lt atIndex:6 + si];
                }
            }
        }

        /* Lights — always set buffer 2, even if empty (prevents validation warnings) */
        {
            mtl_light_t ml[8];
            memset(ml, 0, sizeof(ml));
            for (int32_t i = 0; i < light_count && i < 8; i++)
            {
                ml[i].type = lights[i].type;
                ml[i].dir[0] = lights[i].direction[0];
                ml[i].dir[1] = lights[i].direction[1];
                ml[i].dir[2] = lights[i].direction[2];
                ml[i].pos[0] = lights[i].position[0];
                ml[i].pos[1] = lights[i].position[1];
                ml[i].pos[2] = lights[i].position[2];
                ml[i].col[0] = lights[i].color[0];
                ml[i].col[1] = lights[i].color[1];
                ml[i].col[2] = lights[i].color[2];
                ml[i].intensity = lights[i].intensity;
                ml[i].attenuation = lights[i].attenuation;
                ml[i].inner_cos = lights[i].inner_cos;
                ml[i].outer_cos = lights[i].outer_cos;
            }
            int32_t buf_count = light_count > 0 ? light_count : 1;
            [ctx.encoder setFragmentBytes:ml length:sizeof(mtl_light_t) * buf_count atIndex:2];
        }

        [ctx.encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                indexCount:cmd->index_count
                                 indexType:MTLIndexTypeUInt32
                               indexBuffer:ib
                         indexBufferOffset:0];
    }
}

static void metal_end_frame(void *ctx_ptr)
{
    @autoreleasepool
    {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
        if (!ctx || !ctx.encoder || !ctx.inFrame)
            return;

        [ctx.encoder endEncoding];

        if (ctx.rttActive && ctx.rttTarget && ctx.rttColorTexture)
        {
            /* RTT path: commit GPU work, wait for completion, then read back
             * the color texture to the CPU-side color_buf for AsPixels(). */
            [ctx.cmdBuf commit];
            [ctx.cmdBuf waitUntilCompleted];

            /* Read GPU texture → CPU buffer (BGRA → RGBA conversion) */
            int32_t w = ctx.rttWidth, h = ctx.rttHeight;
            uint8_t *dst = ctx.rttTarget ? ctx.rttTarget->color_buf : NULL;
            if (dst)
            {
                [ctx.rttColorTexture getBytes:dst
                                  bytesPerRow:(NSUInteger)(w * 4)
                                   fromRegion:MTLRegionMake2D(0, 0, (NSUInteger)w, (NSUInteger)h)
                                  mipmapLevel:0];
                /* BGRA → RGBA in-place */
                for (int32_t i = 0; i < w * h; i++)
                {
                    uint8_t tmp = dst[i * 4];
                    dst[i * 4] = dst[i * 4 + 2];
                    dst[i * 4 + 2] = tmp;
                }
            }
        }
        else
        {
            /* On-screen path: DON'T commit yet. The command buffer stays open
             * so subsequent Begin/End pairs add more render passes to the same
             * buffer. Commit + present happens in metal_present() at Flip time. */
        }

        ctx.encoder = nil;
        /* Keep cmdBuf alive for on-screen (nil'd only for RTT above) */
        if (ctx.rttActive)
            ctx.cmdBuf = nil;
        ctx.inFrame = NO;
    }
}

/// @brief Present the most recent on-screen drawable. Called from Flip().
/// Commits the frame's command buffer (which contains all render passes)
/// and presents the drawable to the display in one atomic operation.
static void metal_present(void *backend_ctx)
{
    if (!backend_ctx)
        return;
    @autoreleasepool
    {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)backend_ctx;
        if (ctx.cmdBuf && ctx.drawable)
        {
            /* Present + commit the single command buffer that holds all
             * on-screen render passes encoded during this frame. */
            [ctx.cmdBuf presentDrawable:ctx.drawable];
            [ctx.cmdBuf commit];
            [ctx.cmdBuf waitUntilCompleted];
            ctx.frameBuffers = nil; /* release per-frame buffers after GPU is done */
            ctx.cmdBuf = nil;
            ctx.drawable = nil;
        }
        else if (ctx.drawable)
        {
            /* Edge case: cmdBuf was already committed (e.g., RTT-only frame
             * followed by Flip without an on-screen pass). Present via a
             * fresh command buffer. */
            id<MTLCommandBuffer> cb = [ctx.commandQueue commandBuffer];
            [cb presentDrawable:ctx.drawable];
            [cb commit];
            ctx.drawable = nil;
        }
    }
}

//=============================================================================
// Exported backend
//=============================================================================

/// @brief Bind or unbind an offscreen render target for Metal GPU rendering.
/// When rt is non-NULL, subsequent begin_frame/submit_draw/end_frame calls render
/// to GPU textures instead of the on-screen CAMetalLayer drawable. When rt is NULL,
/// rendering reverts to the screen. After rendering, end_frame copies the GPU color
/// texture back to rt->color_buf for CPU readback (AsPixels).
static void metal_set_render_target(void *ctx_ptr, vgfx3d_rendertarget_t *rt)
{
    @autoreleasepool
    {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
        if (!ctx)
            return;

        if (!rt)
        {
            /* Disable RTT — revert to on-screen rendering */
            ctx.rttActive = NO;
            ctx.rttColorTexture = nil;
            ctx.rttDepthTexture = nil;
            ctx.rttTarget = NULL;
            return;
        }

        /* Enable RTT — create GPU textures for offscreen rendering */
        ctx.rttWidth = rt->width;
        ctx.rttHeight = rt->height;
        ctx.rttTarget = rt;

        /* Color texture: BGRA8Unorm, render target + shader read */
        MTLTextureDescriptor *colorDesc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                               width:(NSUInteger)rt->width
                                                              height:(NSUInteger)rt->height
                                                           mipmapped:NO];
        colorDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        colorDesc.storageMode = MTLStorageModeShared; /* CPU-readable for readback */
        ctx.rttColorTexture = [ctx.device newTextureWithDescriptor:colorDesc];

        /* Depth texture: Depth32Float, render target only */
        MTLTextureDescriptor *depthDesc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                               width:(NSUInteger)rt->width
                                                              height:(NSUInteger)rt->height
                                                           mipmapped:NO];
        depthDesc.usage = MTLTextureUsageRenderTarget;
        depthDesc.storageMode = MTLStorageModePrivate;
        ctx.rttDepthTexture = [ctx.device newTextureWithDescriptor:depthDesc];

        ctx.rttActive = YES;
    }
}

//=============================================================================
// MTL-12: Shadow mapping
//=============================================================================

static void metal_shadow_begin(void *ctx_ptr, float *depth_buf, int32_t w, int32_t h,
                               const float *light_vp)
{
    @autoreleasepool
    {
        (void)depth_buf; /* GPU shadows use MTLTexture, not CPU buffer */
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
        if (!ctx || !ctx.shadowPipeline)
            return;

        /* Create/recreate shadow depth texture if size changed */
        if (!ctx.shadowDepthTexture ||
            (int32_t)ctx.shadowDepthTexture.width != w ||
            (int32_t)ctx.shadowDepthTexture.height != h)
        {
            MTLTextureDescriptor *desc = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                             width:(NSUInteger)w
                                            height:(NSUInteger)h
                                         mipmapped:NO];
            desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            desc.storageMode = MTLStorageModePrivate;
            ctx.shadowDepthTexture = [ctx.device newTextureWithDescriptor:desc];
        }

        /* Store light VP for main pass uniform */
        memcpy(ctx->_shadowLightVP, light_vp, 16 * sizeof(float));

        /* Start shadow render pass */
        if (!ctx.cmdBuf)
            ctx.cmdBuf = [ctx.commandQueue commandBuffer];

        MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.depthAttachment.texture = ctx.shadowDepthTexture;
        rp.depthAttachment.loadAction = MTLLoadActionClear;
        rp.depthAttachment.storeAction = MTLStoreActionStore;
        rp.depthAttachment.clearDepth = 1.0;

        ctx.encoder = [ctx.cmdBuf renderCommandEncoderWithDescriptor:rp];
        if (!ctx.encoder)
            return;
        [ctx.encoder setRenderPipelineState:ctx.shadowPipeline];
        [ctx.encoder setDepthStencilState:ctx.shadowDepthState];
        MTLViewport vp = {0, 0, (double)w, (double)h, 0.0, 1.0};
        [ctx.encoder setViewport:vp];
        [ctx.encoder setFrontFacingWinding:MTLWindingCounterClockwise];
        [ctx.encoder setCullMode:MTLCullModeBack];
    }
}

static void metal_shadow_draw(void *ctx_ptr, const vgfx3d_draw_cmd_t *cmd)
{
    @autoreleasepool
    {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
        if (!ctx || !ctx.encoder)
            return;

        id<MTLBuffer> vb =
            [ctx.device newBufferWithBytes:cmd->vertices
                                    length:cmd->vertex_count * sizeof(vgfx3d_vertex_t)
                                   options:MTLResourceStorageModeShared];
        id<MTLBuffer> ib =
            [ctx.device newBufferWithBytes:cmd->indices
                                    length:cmd->index_count * sizeof(uint32_t)
                                   options:MTLResourceStorageModeShared];
        [ctx.encoder setVertexBuffer:vb offset:0 atIndex:0];
        if (ctx.frameBuffers)
        {
            [ctx.frameBuffers addObject:vb];
            [ctx.frameBuffers addObject:ib];
        }

        /* Per-object: transform by light VP */
        mtl_per_object_t obj;
        memset(&obj, 0, sizeof(obj));
        transpose4x4(cmd->model_matrix, obj.m);
        float lvp_t[16];
        transpose4x4(ctx->_shadowLightVP, lvp_t);
        memcpy(obj.vp, lvp_t, sizeof(float) * 16);
        memcpy(obj.nm, obj.m, sizeof(float) * 16);
        [ctx.encoder setVertexBytes:&obj length:sizeof(obj) atIndex:1];

        [ctx.encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                indexCount:cmd->index_count
                                 indexType:MTLIndexTypeUInt32
                               indexBuffer:ib
                         indexBufferOffset:0];
    }
}

static void metal_shadow_end(void *ctx_ptr, float bias)
{
    @autoreleasepool
    {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
        if (!ctx || !ctx.encoder)
            return;
        [ctx.encoder endEncoding];
        ctx.encoder = nil;
        ctx.shadowActive = YES;
        ctx.shadowBias = bias;
    }
}

//=============================================================================
// MTL-13: Instanced rendering
//=============================================================================

static void metal_submit_draw_instanced(void *ctx_ptr,
                                        vgfx_window_t win,
                                        const vgfx3d_draw_cmd_t *cmd,
                                        const float *instance_matrices,
                                        int32_t instance_count,
                                        const vgfx3d_light_params_t *lights,
                                        int32_t light_count,
                                        const float *ambient,
                                        int8_t wireframe,
                                        int8_t backface_cull)
{
    @autoreleasepool
    {
        (void)win;
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
        if (!ctx || !ctx.encoder || !ctx.inFrame || instance_count <= 0)
            return;

        [ctx.encoder setCullMode:backface_cull ? MTLCullModeBack : MTLCullModeNone];
        [ctx.encoder setTriangleFillMode:wireframe ? MTLTriangleFillModeLines
                                                   : MTLTriangleFillModeFill];
        if (cmd->alpha < 1.0f)
            [ctx.encoder setDepthStencilState:ctx.depthStateNoWrite];
        else
            [ctx.encoder setDepthStencilState:ctx.depthState];

        /* Vertex/index buffers */
        id<MTLBuffer> vb =
            [ctx.device newBufferWithBytes:cmd->vertices
                                    length:cmd->vertex_count * sizeof(vgfx3d_vertex_t)
                                   options:MTLResourceStorageModeShared];
        id<MTLBuffer> ib =
            [ctx.device newBufferWithBytes:cmd->indices
                                    length:cmd->index_count * sizeof(uint32_t)
                                   options:MTLResourceStorageModeShared];
        [ctx.encoder setVertexBuffer:vb offset:0 atIndex:0];
        if (ctx.frameBuffers)
        {
            [ctx.frameBuffers addObject:vb];
            [ctx.frameBuffers addObject:ib];
        }

        /* Per-scene + per-material + lights + textures — same as regular draw */
        mtl_per_scene_t scene;
        memset(&scene, 0, sizeof(scene));
        memcpy(scene.cp, ctx->_camPos, sizeof(float) * 3);
        scene.ac[0] = ambient[0];
        scene.ac[1] = ambient[1];
        scene.ac[2] = ambient[2];
        scene.fc[0] = ctx->_fogColor[0];
        scene.fc[1] = ctx->_fogColor[1];
        scene.fc[2] = ctx->_fogColor[2];
        scene.fc[3] = ctx->_fogEnabled ? 1.0f : 0.0f;
        scene.fog_near = ctx->_fogNear;
        scene.fog_far = ctx->_fogFar;
        scene.lc = light_count;
        scene.shadowEnabled = ctx.shadowActive ? 1 : 0;
        scene.shadowBias = ctx.shadowBias;
        if (ctx.shadowActive)
            transpose4x4(ctx->_shadowLightVP, scene.shadowVP);
        [ctx.encoder setFragmentBytes:&scene length:sizeof(scene) atIndex:0];

        mtl_per_material_t mat;
        memset(&mat, 0, sizeof(mat));
        memcpy(mat.dc, cmd->diffuse_color, sizeof(float) * 4);
        mat.alpha = cmd->alpha;
        mat.ht = cmd->texture ? 1 : 0;
        mat.unlit = cmd->unlit;
        [ctx.encoder setFragmentBytes:&mat length:sizeof(mat) atIndex:1];

        /* Default textures + sampler */
        for (int slot = 0; slot < 10; slot++)
            [ctx.encoder setFragmentTexture:ctx.defaultTexture atIndex:slot];
        if (ctx.shadowActive && ctx.shadowDepthTexture)
            [ctx.encoder setFragmentTexture:ctx.shadowDepthTexture atIndex:4];
        [ctx.encoder setFragmentSamplerState:ctx.sharedSampler atIndex:0];
        if (ctx.shadowSampler)
            [ctx.encoder setFragmentSamplerState:ctx.shadowSampler atIndex:1];
        if (cmd->texture)
        {
            id<MTLTexture> tex = metal_get_cached_texture(ctx, cmd->texture);
            if (tex)
                [ctx.encoder setFragmentTexture:tex atIndex:0];
        }

        /* Lights */
        {
            mtl_light_t ml[8];
            memset(ml, 0, sizeof(ml));
            for (int32_t i = 0; i < light_count && i < 8; i++)
            {
                ml[i].type = lights[i].type;
                ml[i].dir[0] = lights[i].direction[0];
                ml[i].dir[1] = lights[i].direction[1];
                ml[i].dir[2] = lights[i].direction[2];
                ml[i].pos[0] = lights[i].position[0];
                ml[i].pos[1] = lights[i].position[1];
                ml[i].pos[2] = lights[i].position[2];
                ml[i].col[0] = lights[i].color[0];
                ml[i].col[1] = lights[i].color[1];
                ml[i].col[2] = lights[i].color[2];
                ml[i].intensity = lights[i].intensity;
                ml[i].attenuation = lights[i].attenuation;
                ml[i].inner_cos = lights[i].inner_cos;
                ml[i].outer_cos = lights[i].outer_cos;
            }
            int32_t bc = light_count > 0 ? light_count : 1;
            [ctx.encoder setFragmentBytes:ml length:sizeof(mtl_light_t) * bc atIndex:2];
        }

        /* Bind bone palette for instanced skinning (Fix 104) */
        if (cmd->bone_palette && cmd->bone_count > 0) {
            int bc = cmd->bone_count > 128 ? 128 : cmd->bone_count;
            size_t bsz = (size_t)bc * 16 * sizeof(float);
            id<MTLBuffer> boneBuf = [ctx.device newBufferWithBytes:cmd->bone_palette
                                                            length:bsz
                                                           options:MTLResourceStorageModeShared];
            [ctx.encoder setVertexBuffer:boneBuf offset:0 atIndex:3];
            if (ctx.frameBuffers)
                [ctx.frameBuffers addObject:boneBuf];
        }

        /* Issue N individual draws with per-instance model matrix.
         * True instanced rendering (drawIndexedPrimitives:instanceCount:) requires
         * a separate pipeline with instance_id in the vertex shader. For now,
         * we still issue N draws but from the GPU side — avoiding the deferred
         * queue overhead that the software path incurs. */
        for (int32_t i = 0; i < instance_count; i++)
        {
            mtl_per_object_t obj;
            memset(&obj, 0, sizeof(obj));
            transpose4x4(&instance_matrices[i * 16], obj.m);
            float vp_t[16];
            transpose4x4(ctx->_vp, vp_t);
            memcpy(obj.vp, vp_t, sizeof(float) * 16);
            memcpy(obj.nm, obj.m, sizeof(float) * 16);
            [ctx.encoder setVertexBytes:&obj length:sizeof(obj) atIndex:1];
            [ctx.encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                    indexCount:cmd->index_count
                                     indexType:MTLIndexTypeUInt32
                                   indexBuffer:ib
                             indexBufferOffset:0];
        }
    }
}

//=============================================================================
// MTL-11: Post-processing
//=============================================================================

/* C-side PostFX params struct (must match MSL PostFXParams) */
typedef struct
{
    int32_t bloomEnabled;
    float bloomThreshold, bloomStrength;
    int32_t tonemapMode;
    float tonemapExposure;
    int32_t fxaaEnabled;
    int32_t colorGradeEnabled;
    float cgBright, cgContrast, cgSat;
    int32_t vignetteEnabled;
    float vigRadius, vigSoftness;
} mtl_postfx_params_t;

/* Called from rt_canvas3d_flip via metal_present when PostFX is active */

/* Called from rt_rendertarget3d.c to hide the Metal layer during software RTT */
void vgfx3d_hide_gpu_layer(void *backend_ctx)
{
    if (!backend_ctx)
        return;
    @autoreleasepool
    {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)backend_ctx;
        if (ctx.metalLayer)
            ctx.metalLayer.hidden = YES;
    }
}

void vgfx3d_show_gpu_layer(void *backend_ctx)
{
    if (!backend_ctx)
        return;
    @autoreleasepool
    {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)backend_ctx;
        if (ctx.metalLayer)
            ctx.metalLayer.hidden = NO;
    }
}

const vgfx3d_backend_t vgfx3d_metal_backend = {
    .name = "metal",
    .create_ctx = metal_create_ctx,
    .destroy_ctx = metal_destroy_ctx,
    .clear = metal_clear,
    .begin_frame = metal_begin_frame,
    .submit_draw = metal_submit_draw,
    .end_frame = metal_end_frame,
    .set_render_target = metal_set_render_target,
    .shadow_begin = metal_shadow_begin,
    .shadow_draw = metal_shadow_draw,
    .shadow_end = metal_shadow_end,
    .submit_draw_instanced = metal_submit_draw_instanced,
    .present = metal_present,
};

#endif /* __APPLE__ && VIPER_ENABLE_GRAPHICS */
