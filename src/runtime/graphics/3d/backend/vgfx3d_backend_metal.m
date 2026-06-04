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

#include "rt_postfx3d.h"
#include "rt_textureasset3d.h"
#include "vgfx.h"
#include "vgfx3d_backend.h"
#include "vgfx3d_backend_metal_shared.h"
#include "vgfx3d_backend_utils.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VGFX3D_STR_IMPL(x) #x
#define VGFX3D_STR(x) VGFX3D_STR_IMPL(x)

//=============================================================================
// Objective-C wrapper to hold Metal objects under ARC
//=============================================================================

@interface VGFXMetalContext : NSObject {
  @public
    float _view[16];
    float _projection[16];
    float _vp[16];
    float _prevVP[16];
    float _invVP[16];
    float _camPos[3];
    float _camForward[3];
    BOOL _camIsOrtho;
    BOOL _prevVPValid;
    /* MTL-07: Fog parameters (stored per-frame from begin_frame) */
    float _fogColor[3];
    float _fogNear, _fogFar;
    BOOL _fogEnabled;
    /* MTL-12: Shadow light view-projection matrices (stored from shadow_begin) */
    float _shadowLightVP[VGFX3D_MAX_SHADOW_LIGHTS][16];
    id<MTLTexture> _shadowDepthTexture[VGFX3D_MAX_SHADOW_LIGHTS];
    int32_t _shadowPassSlot;
    int32_t _shadowCount;
    int8_t _shadowComplete[VGFX3D_MAX_SHADOW_LIGHTS];
    float _shadowBias;
}
@property(nonatomic, strong) id<MTLDevice> device;
@property(nonatomic, strong) id<MTLCommandQueue> commandQueue;
@property(nonatomic, strong) id<MTLRenderPipelineState> pipelineState;
@property(nonatomic, strong) id<MTLRenderPipelineState> pipelineStateAlpha;
@property(nonatomic, strong) id<MTLRenderPipelineState> pipelineStateAdditive;
@property(nonatomic, strong) id<MTLRenderPipelineState> pipelineStateColorOnly;
@property(nonatomic, strong) id<MTLRenderPipelineState> pipelineStateColorOnlyAlpha;
@property(nonatomic, strong) id<MTLRenderPipelineState> pipelineStateColorOnlyAdditive;
@property(nonatomic, strong) id<MTLRenderPipelineState> instancedPipelineState;
@property(nonatomic, strong) id<MTLRenderPipelineState> instancedPipelineStateAlpha;
@property(nonatomic, strong) id<MTLRenderPipelineState> instancedPipelineStateAdditive;
@property(nonatomic, strong) id<MTLRenderPipelineState> instancedPipelineStateColorOnly;
@property(nonatomic, strong) id<MTLRenderPipelineState> instancedPipelineStateColorOnlyAlpha;
@property(nonatomic, strong) id<MTLRenderPipelineState> instancedPipelineStateColorOnlyAdditive;
@property(nonatomic, strong) id<MTLDepthStencilState> depthState;
@property(nonatomic, strong) id<MTLDepthStencilState> depthStateNoWrite;
@property(nonatomic, strong) id<MTLDepthStencilState> depthStateDisabled;
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
@property(nonatomic, strong) id<MTLTexture> rttMotionTexture;
@property(nonatomic, strong) id<MTLTexture> rttDepthTexture;
@property(nonatomic) BOOL rttActive;
@property(nonatomic) int32_t rttWidth, rttHeight;
@property(nonatomic, assign) vgfx3d_rendertarget_t *rttTarget; /* CPU-side RT for readback */
/* Generation-aware texture/cubemap caches keyed by runtime object identity. */
@property(nonatomic, strong) NSMutableDictionary *textureCache;
@property(nonatomic, strong) NSMutableDictionary *cubemapCache;
@property(nonatomic, strong) NSMutableDictionary *geometryCache;
@property(nonatomic, strong) NSMutableDictionary *morphCache;
@property(nonatomic, strong) NSMutableDictionary *renderTargetCache;
@property(nonatomic, strong) NSMutableDictionary *samplerCache;
@property(nonatomic) uint64_t frameSerial;
@property(nonatomic) uint64_t textureUploadBytes;
@property(nonatomic) uint64_t textureUploadBudgetBytes;
@property(nonatomic, strong) id<MTLSamplerState> sharedSampler;
@property(nonatomic, strong) id<MTLSamplerState> cubeSampler;
@property(nonatomic, strong) id<MTLTexture> defaultCubemap;
/* MTL-12: Shadow mapping state */
@property(nonatomic, strong) id<MTLRenderPipelineState> shadowPipeline;
@property(nonatomic, strong) id<MTLSamplerState> shadowSampler;
@property(nonatomic, strong) id<MTLDepthStencilState> shadowDepthState;
/* MTL-13: Instanced rendering — pooled instance buffer */
@property(nonatomic, strong) id<MTLBuffer> instanceBuf;
/* MTL-11: Post-processing state */
@property(nonatomic, strong) id<MTLTexture> postfxColorTexture;
@property(nonatomic, strong) id<MTLTexture> postfxScratchTexture;
@property(nonatomic, strong) id<MTLRenderPipelineState> postfxPipeline;
@property(nonatomic, strong) id<MTLLibrary> postfxLibrary;
@property(nonatomic, strong) id<MTLTexture> overlayColorTexture;
@property(nonatomic, strong) id<MTLTexture> overlayMotionTexture;
@property(nonatomic, strong) id<MTLTexture> overlayDepthTexture;
/* Window-backed scene/postfx rendering stays offscreen until present time. */
@property(nonatomic, strong) id<MTLTexture> offscreenColor;
@property(nonatomic, strong) id<MTLTexture> offscreenMotion;
@property(nonatomic, strong) id<MTLTexture> displayTexture;
@property(nonatomic, strong) id<MTLRenderPipelineState> skyboxPipeline;
@property(nonatomic, strong) id<MTLRenderPipelineState> skyboxColorPipeline;
@property(nonatomic, strong) id<MTLDepthStencilState> skyboxDepthState;
@property(nonatomic, strong) id<MTLBuffer> skyboxVertexBuffer;
@property(nonatomic, assign) vgfx_window_t vgfxWin; /* for framebuffer readback */
@property(nonatomic, strong) NSMutableData *instanceScratch;
@property(nonatomic) NSUInteger instanceCapacity;
@property(nonatomic) vgfx3d_metal_target_kind_t currentTargetKind;
@property(nonatomic) int8_t gpuPostfxEnabled;
@property(nonatomic) int8_t gpuPostfxChainValid;
@property(nonatomic) vgfx3d_postfx_chain_t gpuPostfxChain;
@property(nonatomic) vgfx3d_metal_frame_history_t frameHistory;
@property(nonatomic) int8_t postfxEncodedThisFrame;
@property(nonatomic) int8_t postfxCompositedToDrawable;
@end

@implementation VGFXMetalContext
@end

@interface VGFXMetalTextureCacheEntry : NSObject
@property(nonatomic, strong) id<MTLTexture> texture;
@property(nonatomic) uint64_t generation;
@property(nonatomic) uint64_t pendingGeneration;
@property(nonatomic) int32_t width;
@property(nonatomic) int32_t height;
@property(nonatomic) int32_t uploadNextRow;
@property(nonatomic) int8_t uploadInProgress;
@property(nonatomic, assign) void *textureAsset;
@property(nonatomic) int32_t nativeFormat;
@property(nonatomic) int32_t nativeNextBlockRow;
@property(nonatomic) int64_t nativeNextMip;
@property(nonatomic) int64_t nativeMipStart;
@property(nonatomic) int64_t nativeMipCount;
@property(nonatomic) uint64_t lastUsedFrame;
@end

@implementation VGFXMetalTextureCacheEntry
@end

@interface VGFXMetalCubemapCacheEntry : NSObject
@property(nonatomic, strong) id<MTLTexture> texture;
@property(nonatomic) uint64_t generation;
@property(nonatomic) uint64_t pendingGeneration;
@property(nonatomic) int32_t faceSize;
@property(nonatomic) int32_t uploadFace;
@property(nonatomic) int32_t uploadNextRow;
@property(nonatomic) int8_t uploadInProgress;
@property(nonatomic) uint64_t lastUsedFrame;
@end

@implementation VGFXMetalCubemapCacheEntry
@end

@interface VGFXMetalGeometryCacheEntry : NSObject
@property(nonatomic, strong) id<MTLBuffer> vertexBuffer;
@property(nonatomic, strong) id<MTLBuffer> indexBuffer;
@property(nonatomic) uint32_t revision;
@property(nonatomic) uint32_t vertexCount;
@property(nonatomic) uint32_t indexCount;
@property(nonatomic) uint64_t lastUsedFrame;
@end

@implementation VGFXMetalGeometryCacheEntry
@end

@interface VGFXMetalMorphCacheEntry : NSObject
@property(nonatomic, strong) id<MTLBuffer> deltaBuffer;
@property(nonatomic, strong) id<MTLBuffer> normalBuffer;
@property(nonatomic, assign) const void *key;
@property(nonatomic) uint64_t revision;
@property(nonatomic) int32_t shapeCount;
@property(nonatomic) uint32_t vertexCount;
@property(nonatomic) int8_t hasNormalDeltas;
@property(nonatomic) uint64_t lastUsedFrame;
@end

@implementation VGFXMetalMorphCacheEntry
@end

@interface VGFXMetalRenderTargetCacheEntry : NSObject
@property(nonatomic, strong) id<MTLTexture> colorTexture;
@property(nonatomic, strong) id<MTLTexture> motionTexture;
@property(nonatomic, strong) id<MTLTexture> depthTexture;
@property(nonatomic, strong) id<MTLCommandBuffer> pendingCommandBuffer;
@property(nonatomic, assign) vgfx3d_rendertarget_t *target;
@property(nonatomic) int32_t width;
@property(nonatomic) int32_t height;
@property(nonatomic) MTLPixelFormat colorPixelFormat;
@property(nonatomic) uint64_t lastUsedFrame;
@end

@implementation VGFXMetalRenderTargetCacheEntry
@end

//=============================================================================
// MSL Shader source (vertex + fragment in two halves to stay under C99 limit)
//=============================================================================

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Woverlength-strings"

static NSString *metal_shader_source = @
    "#include <metal_stdlib>\n"
                                        "using namespace metal;\n"
                                        "\n"
                                        "struct VertexIn {\n"
                                        "    float3 position [[attribute(0)]];\n"
                                        "    float3 normal   [[attribute(1)]];\n"
                                        "    float2 uv       [[attribute(2)]];\n"
                                        "    float4 color    [[attribute(3)]];\n"
                                        "    float4 tangent  [[attribute(4)]];\n"
                                        "    uchar4 boneIdx  [[attribute(5)]];\n"
                                        "    float4 boneWt   [[attribute(6)]];\n"
                                        "    float2 uv1      [[attribute(7)]];\n"
                                        "};\n"
                                        "\n"
                                        "struct InstanceData {\n"
                                        "    float4x4 modelMatrix;\n"
                                        "    float4x4 normalMatrix;\n"
                                        "    float4x4 prevModelMatrix;\n"
                                        "};\n"
                                        "\n"
                                        "struct VertexOut {\n"
                                        "    float4 position [[position]];\n"
                                        "    float3 worldPos;\n"
                                        "    float3 normal;\n"
                                        "    float4 tangent;\n"
                                        "    float2 uv;\n"
                                        "    float2 uv1;\n"
                                        "    float4 color;\n"
                                        "    float4 currClip;\n"
                                        "    float4 prevClip;\n"
                                        "    float hasObjectHistory;\n"
                                        "};\n"
                                        "\n"
                                        "struct PerObject {\n"
                                        "    float4x4 modelMatrix;\n"
                                        "    float4x4 prevModelMatrix;\n"
                                        "    float4x4 viewProjection;\n"
                                        "    float4x4 normalMatrix;\n"
                                        "    int4 flags0;\n"
                                        "    int4 flags1;\n"
                                        "};\n"
                                        "\n"
                                        "struct Light {\n"
                                        "    int type;\n"
                                        "    int shadowIndex;\n"
                                        "    int shadowCascadeCount;\n"
                                        "    float _p0;\n"
                                        "    float4 direction;\n"
                                        "    float4 position;\n"
                                        "    float4 color;\n"
                                        "    float intensity;\n"
                                        "    float attenuation;\n"
                                        "    float inner_cos;\n"
                                        "    float outer_cos;\n"
                                        "    float4 shadowCascadeSplits;\n"
                                        "};\n"
                                        "\n"
                                        "struct PerScene {\n"
                                        "    float4 cameraPosition;\n"
                                        "    float4 ambientColor;\n"
                                        "    float4 fogColor;\n"
                                        "    float4 fogParams;\n"
                                        "    int4 counts;\n"
                                        "    float4 cameraForward;\n"
                                        "    float4x4 prevViewProjection;\n"
                                        "    float4x4 shadowVP[" VGFX3D_STR(
                                            VGFX3D_MAX_SHADOW_LIGHTS) "];\n"
                                                                      "};\n"
                                                                      "\n"
                                                                      "struct PerMaterial {\n"
                                                                      "    float4 diffuseColor;\n"
                                                                      "    float4 specularColor;\n"
                                                                      "    float4 emissiveColor;\n"
                                                                      "    float4 scalars;\n"
                                                                      "    float4 pbrScalars0;\n"
                                                                      "    float4 pbrScalars1;\n"
                                                                      "    int4 flags0;\n"
                                                                      "    int4 flags1;\n"
                                                                      "    int4 pbrFlags;\n"
                                                                      "    float4 splatScales;\n"
                                                                      "    int shadingModel;\n"
                                                                      "    float customParams[8];\n"
                                                                      "    int4 textureUvSets0;\n"
                                                                      "    int4 textureUvSets1;\n"
                                                                      "    float4 "
                                                                      "textureUvTransform0"
                                                                      "[" VGFX3D_STR(
                                                                          RT_MATERIAL3D_TEXTURE_SLOT_COUNT) "];\n"
                                                                                                            "    float4 textureUvTransform1[" VGFX3D_STR(
                                                                                                                RT_MATERIAL3D_TEXTURE_SLOT_COUNT) "];\n"
                                                                                                                                                  "};\n"
                                                                                                                                                  "\n"
                                                                                                                                                  "struct ShadowOut {\n"
                                                                                                                                                  "    float4 position [[position]];\n"
                                                                                                                                                  "    float2 uv;\n"
                                                                                                                                                  "    float2 uv1;\n"
                                                                                                                                                  "    float4 color;\n"
                                                                                                                                                  "};\n"
                                                                                                                                                  "\n"
                                                                                                                                                  "struct MainOut {\n"
                                                                                                                                                  "    float4 color [[color(0)]];\n"
                                                                                                                                                  "    float4 motion [[color(1)]];\n"
                                                                                                                                                  "};\n"
                                                                                                                                                  "\n"
                                                                                                                                                  "float3 applyMorphPosition(float3 pos,\n"
                                                                                                                                                  "                         constant PerObject &obj,\n"
                                                                                                                                                  "                         constant float *morphDeltas,\n"
                                                                                                                                                  "                         constant float *weights,\n"
                                                                                                                                                  "                         uint vid) {\n"
                                                                                                                                                  "    int morphShapeCount = obj.flags0.z;\n"
                                                                                                                                                  "    int vertexCount = obj.flags0.w;\n"
                                                                                                                                                  "    if (morphShapeCount <= 0 || vertexCount <= 0)\n"
                                                                                                                                                  "        return pos;\n"
                                                                                                                                                  "    for (int s = 0; s < morphShapeCount; s++) {\n"
                                                                                                                                                  "        float w = weights[s];\n"
                                                                                                                                                  "        if (fabs(w) > 0.0001) {\n"
                                                                                                                                                  "            int off = (s * vertexCount + int(vid)) * 3;\n"
                                                                                                                                                  "            pos.x += morphDeltas[off + 0] * w;\n"
                                                                                                                                                  "            pos.y += morphDeltas[off + 1] * w;\n"
                                                                                                                                                  "            pos.z += morphDeltas[off + 2] * w;\n"
                                                                                                                                                  "        }\n"
                                                                                                                                                  "    }\n"
                                                                                                                                                  "    return pos;\n"
                                                                                                                                                  "}\n"
                                                                                                                                                  "\n"
                                                                                                                                                  "float3 applyMorphNormal(float3 nrm,\n"
                                                                                                                                                  "                       constant PerObject &obj,\n"
                                                                                                                                                  "                       constant float *morphNormalDeltas,\n"
                                                                                                                                                  "                       constant float *weights,\n"
                                                                                                                                                  "                       uint vid) {\n"
                                                                                                                                                  "    int morphShapeCount = obj.flags0.z;\n"
                                                                                                                                                  "    int vertexCount = obj.flags0.w;\n"
                                                                                                                                                  "    if (obj.flags1.w == 0 || morphShapeCount <= 0 || vertexCount <= 0)\n"
                                                                                                                                                  "        return nrm;\n"
                                                                                                                                                  "    for (int s = 0; s < morphShapeCount; s++) {\n"
                                                                                                                                                  "        float w = weights[s];\n"
                                                                                                                                                  "        if (fabs(w) > 0.0001) {\n"
                                                                                                                                                  "            int off = (s * vertexCount + int(vid)) * 3;\n"
                                                                                                                                                  "            nrm.x += morphNormalDeltas[off + 0] * w;\n"
                                                                                                                                                  "            nrm.y += morphNormalDeltas[off + 1] * w;\n"
                                                                                                                                                  "            nrm.z += morphNormalDeltas[off + 2] * w;\n"
                                                                                                                                                  "        }\n"
                                                                                                                                                  "    }\n"
                                                                                                                                                  "    return nrm;\n"
                                                                                                                                                  "}\n"
                                                                                                                                                  "\n"
                                                                                                                                                  "float3 safe_normalize3(float3 v, float3 fallback) {\n"
                                                                                                                                                  "    float len2 = dot(v, v);\n"
                                                                                                                                                  "    return (len2 > 1e-12 && len2 < 1e20) ? v * rsqrt(len2) : fallback;\n"
                                                                                                                                                  "}\n"
                                                                                                                                                  "\n"
                                                                                                                                                  "float3x3 safe_normal_matrix(float4x4 m) {\n"
                                                                                                                                                  "    float3x3 linear = float3x3(m[0].xyz, m[1].xyz, m[2].xyz);\n"
                                                                                                                                                  "    float3 c0 = linear[0];\n"
                                                                                                                                                  "    float3 c1 = linear[1];\n"
                                                                                                                                                  "    float3 c2 = linear[2];\n"
                                                                                                                                                  "    float3 cof0 = cross(c1, c2);\n"
                                                                                                                                                  "    float3 cof1 = cross(c2, c0);\n"
                                                                                                                                                  "    float3 cof2 = cross(c0, c1);\n"
                                                                                                                                                  "    float det = dot(c0, cof0);\n"
                                                                                                                                                  "    if (fabs(det) > 1e-8)\n"
                                                                                                                                                  "        return float3x3(cof0 / det, cof1 / det, cof2 / det);\n"
                                                                                                                                                  "    return float3x3(float3(1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0), float3(0.0, 0.0, 1.0));\n"
                                                                                                                                                  "}\n"
                                                                                                                                                  "\n"
                                                                                                                                                  "float4 skinPosition(float4 pos,\n"
                                                                                                                                                  "                    VertexIn in,\n"
                                                                                                                                                  "                    constant float4x4 *palette,\n"
                                                                                                                                                  "                    int enabled) {\n"
                                                                                                                                                  "    if (enabled == 0)\n"
                                                                                                                                                  "        return pos;\n"
                                                                                                                                                  "    float4 skinned = float4(0.0);\n"
                                                                                                                                                  "    float totalWeight = 0.0;\n"
                                                                                                                                                  "    for (int i = 0; i < 4; i++) {\n"
                                                                                                                                                  "        float bw = in.boneWt[i];\n"
                                                                                                                                                  "        if (bw <= 0.0001)\n"
                                                                                                                                                  "            continue;\n"
                                                                                                                                                  "        uint idx = min((uint)in.boneIdx[i], 255u);\n"
                                                                                                                                                  "        skinned += (palette[idx] * pos) * bw;\n"
                                                                                                                                                  "        totalWeight += bw;\n"
                                                                                                                                                  "    }\n"
                                                                                                                                                  "    return totalWeight > 0.0001 ? skinned / totalWeight : pos;\n"
                                                                                                                                                  "}\n"
                                                                                                                                                  "\n"
                                                                                                                                                  "float4 clipZToBackend(float4 clip) {\n"
                                                                                                                                                  "    clip.z = clip.z * 0.5 + clip.w * 0.5;\n"
                                                                                                                                                  "    return clip;\n"
                                                                                                                                                  "}\n"
                                                                                                                                                  "\n"
                                                                                                                                                  "float3 skinVector(float3 vec,\n"
                                                                                                                                                  "                  VertexIn in,\n"
                                                                                                                                                  "                  constant float4x4 *palette,\n"
                                                                                                                                                  "                  int enabled) {\n"
                                                                                                                                                  "    if (enabled == 0)\n"
                                                                                                                                                  "        return vec;\n"
                                                                                                                                                  "    float3 skinned = float3(0.0);\n"
                                                                                                                                                  "    float totalWeight = 0.0;\n"
                                                                                                                                                  "    for (int i = 0; i < 4; i++) {\n"
                                                                                                                                                  "        float bw = in.boneWt[i];\n"
                                                                                                                                                  "        if (bw <= 0.0001)\n"
                                                                                                                                                  "            continue;\n"
                                                                                                                                                  "        uint idx = min((uint)in.boneIdx[i], 255u);\n"
                                                                                                                                                  "        skinned += (safe_normal_matrix(palette[idx]) * vec) * bw;\n"
                                                                                                                                                  "        totalWeight += bw;\n"
                                                                                                                                                  "    }\n"
                                                                                                                                                  "    return totalWeight > 0.0001 ? skinned / totalWeight : vec;\n"
                                                                                                                                                  "}\n"
                                                                                                                                                  "\n"
                                                                                                                                                  "VertexOut buildVertex(float3 currPos,\n"
                                                                                                                                                  "                      float3 prevPos,\n"
                                                                                                                                                  "                      float3 currNormal,\n"
                                                                                                                                                  "                      float4 currTangent,\n"
                                                                                                                                                  "                      float2 uv,\n"
                                                                                                                                                  "                      float2 uv1,\n"
                                                                                                                                                  "                      float4 color,\n"
                                                                                                                                                  "                      float4x4 modelMatrix,\n"
                                                                                                                                                  "                      float4x4 normalMatrix,\n"
                                                                                                                                                  "                      float4x4 prevModelMatrix,\n"
                                                                                                                                                  "                      constant PerObject &obj,\n"
                                                                                                                                                  "                      constant PerScene &scene,\n"
                                                                                                                                                  "                      float hasHistory) {\n"
                                                                                                                                                  "    VertexOut out;\n"
                                                                                                                                                  "    float4 worldPos = modelMatrix * float4(currPos, 1.0);\n"
                                                                                                                                                  "    float4 prevWorldPos = prevModelMatrix * float4(prevPos, 1.0);\n"
                                                                                                                                                  "    float4 currClip = obj.viewProjection * worldPos;\n"
                                                                                                                                                  "    float4 prevClip = scene.prevViewProjection * prevWorldPos;\n"
                                                                                                                                                  "    out.position = clipZToBackend(currClip);\n"
                                                                                                                                                  "    out.worldPos = worldPos.xyz;\n"
                                                                                                                                                  "    out.normal = (normalMatrix * float4(currNormal, 0.0)).xyz;\n"
                                                                                                                                                  "    out.tangent = float4((normalMatrix * float4(currTangent.xyz, 0.0)).xyz, currTangent.w);\n"
                                                                                                                                                  "    out.uv = uv;\n"
                                                                                                                                                  "    out.uv1 = uv1;\n"
                                                                                                                                                  "    out.color = color;\n"
                                                                                                                                                  "    out.currClip = currClip;\n"
                                                                                                                                                  "    out.prevClip = prevClip;\n"
                                                                                                                                                  "    out.hasObjectHistory = hasHistory;\n"
                                                                                                                                                  "    return out;\n"
                                                                                                                                                  "}\n"
                                                                                                                                                  "\n"
                                                                                                                                                  "vertex VertexOut vertex_main(\n"
                                                                                                                                                  "    VertexIn in [[stage_in]],\n"
                                                                                                                                                  "    constant PerObject &obj [[buffer(1)]],\n"
                                                                                                                                                  "    constant PerScene &scene [[buffer(2)]],\n"
                                                                                                                                                  "    constant float4x4 *bonePalette [[buffer(3)]],\n"
                                                                                                                                                  "    constant float *morphDeltas [[buffer(4)]],\n"
                                                                                                                                                  "    constant float *morphWeights [[buffer(5)]],\n"
                                                                                                                                                  "    constant float4x4 *prevBonePalette [[buffer(7)]],\n"
                                                                                                                                                  "    constant float *prevMorphWeights [[buffer(8)]],\n"
                                                                                                                                                  "    constant float *morphNormalDeltas [[buffer(9)]],\n"
                                                                                                                                                  "    uint vid [[vertex_id]]) {\n"
                                                                                                                                                  "    float3 pos = applyMorphPosition(in.position, obj, morphDeltas, morphWeights, vid);\n"
                                                                                                                                                  "    float3 prevPos = applyMorphPosition(in.position,\n"
                                                                                                                                                  "                                       obj,\n"
                                                                                                                                                  "                                       morphDeltas,\n"
                                                                                                                                                  "                                       obj.flags1.y != 0 ? prevMorphWeights : morphWeights,\n"
                                                                                                                                                  "                                       vid);\n"
                                                                                                                                                  "    float3 currNormal = applyMorphNormal(in.normal, obj, morphNormalDeltas, morphWeights, "
                                                                                                                                                  "vid);\n"
                                                                                                                                                  "    float4 currTangent = in.tangent;\n"
                                                                                                                                                  "    float4 skinnedPos = skinPosition(float4(pos, 1.0), in, bonePalette, obj.flags0.x);\n"
                                                                                                                                                  "    float4 prevSkinnedPos = skinPosition(float4(prevPos, 1.0),\n"
                                                                                                                                                  "                                         in,\n"
                                                                                                                                                  "                                         prevBonePalette,\n"
                                                                                                                                                  "                                         obj.flags0.y);\n"
                                                                                                                                                  "    float3 skinnedNormal = skinVector(currNormal, in, bonePalette, obj.flags0.x);\n"
                                                                                                                                                  "    float3 skinnedTangent = skinVector(currTangent.xyz, in, bonePalette, obj.flags0.x);\n"
                                                                                                                                                  "    if (obj.flags0.x == 0) {\n"
                                                                                                                                                  "        skinnedPos = float4(pos, 1.0);\n"
                                                                                                                                                  "        skinnedNormal = currNormal;\n"
                                                                                                                                                  "        skinnedTangent = currTangent.xyz;\n"
                                                                                                                                                  "    }\n"
                                                                                                                                                  "    if (obj.flags0.y == 0)\n"
                                                                                                                                                  "        prevSkinnedPos = float4(prevPos, 1.0);\n"
                                                                                                                                                  "    float4x4 prevModel = obj.flags1.x != 0 ? obj.prevModelMatrix : obj.modelMatrix;\n"
                                                                                                                                                  "    float hasHistory = (obj.flags1.x != 0 || obj.flags0.y != 0 || obj.flags1.y != 0) ? 1.0 : "
                                                                                                                                                  "0.0;\n"
                                                                                                                                                  "    return buildVertex(skinnedPos.xyz,\n"
                                                                                                                                                  "                       prevSkinnedPos.xyz,\n"
                                                                                                                                                  "                       safe_normalize3(skinnedNormal, float3(0.0, 0.0, 1.0)),\n"
                                                                                                                                                  "                       float4(safe_normalize3(skinnedTangent, float3(1.0, 0.0, 0.0)), currTangent.w),\n"
                                                                                                                                                  "                       in.uv,\n"
                                                                                                                                                  "                       in.uv1,\n"
                                                                                                                                                  "                       in.color,\n"
                                                                                                                                                  "                       obj.modelMatrix,\n"
                                                                                                                                                  "                       obj.normalMatrix,\n"
                                                                                                                                                  "                       prevModel,\n"
                                                                                                                                                  "                       obj,\n"
                                                                                                                                                  "                       scene,\n"
                                                                                                                                                  "                       hasHistory);\n"
                                                                                                                                                  "}\n"
                                                                                                                                                  "\n"
                                                                                                                                                  "vertex VertexOut vertex_main_instanced(\n"
                                                                                                                                                  "    VertexIn in [[stage_in]],\n"
                                                                                                                                                  "    constant PerObject &obj [[buffer(1)]],\n"
                                                                                                                                                  "    constant PerScene &scene [[buffer(2)]],\n"
                                                                                                                                                  "    constant float4x4 *bonePalette [[buffer(3)]],\n"
                                                                                                                                                  "    constant float *morphDeltas [[buffer(4)]],\n"
                                                                                                                                                  "    constant float *morphWeights [[buffer(5)]],\n"
                                                                                                                                                  "    device const InstanceData *instances [[buffer(6)]],\n"
                                                                                                                                                  "    constant float4x4 *prevBonePalette [[buffer(7)]],\n"
                                                                                                                                                  "    constant float *prevMorphWeights [[buffer(8)]],\n"
                                                                                                                                                  "    constant float *morphNormalDeltas [[buffer(9)]],\n"
                                                                                                                                                  "    uint vid [[vertex_id]],\n"
                                                                                                                                                  "    uint iid [[instance_id]]) {\n"
                                                                                                                                                  "    InstanceData inst = instances[iid];\n"
                                                                                                                                                  "    float3 pos = applyMorphPosition(in.position, obj, morphDeltas, morphWeights, vid);\n"
                                                                                                                                                  "    float3 prevPos = applyMorphPosition(in.position,\n"
                                                                                                                                                  "                                       obj,\n"
                                                                                                                                                  "                                       morphDeltas,\n"
                                                                                                                                                  "                                       obj.flags1.y != 0 ? prevMorphWeights : morphWeights,\n"
                                                                                                                                                  "                                       vid);\n"
                                                                                                                                                  "    float3 currNormal = applyMorphNormal(in.normal, obj, morphNormalDeltas, morphWeights, "
                                                                                                                                                  "vid);\n"
                                                                                                                                                  "    float4 currTangent = in.tangent;\n"
                                                                                                                                                  "    float4 skinnedPos = skinPosition(float4(pos, 1.0), in, bonePalette, obj.flags0.x);\n"
                                                                                                                                                  "    float4 prevSkinnedPos = skinPosition(float4(prevPos, 1.0),\n"
                                                                                                                                                  "                                         in,\n"
                                                                                                                                                  "                                         prevBonePalette,\n"
                                                                                                                                                  "                                         obj.flags0.y);\n"
                                                                                                                                                  "    float3 skinnedNormal = skinVector(currNormal, in, bonePalette, obj.flags0.x);\n"
                                                                                                                                                  "    float3 skinnedTangent = skinVector(currTangent.xyz, in, bonePalette, obj.flags0.x);\n"
                                                                                                                                                  "    if (obj.flags0.x == 0) {\n"
                                                                                                                                                  "        skinnedPos = float4(pos, 1.0);\n"
                                                                                                                                                  "        skinnedNormal = currNormal;\n"
                                                                                                                                                  "        skinnedTangent = currTangent.xyz;\n"
                                                                                                                                                  "    }\n"
                                                                                                                                                  "    if (obj.flags0.y == 0)\n"
                                                                                                                                                  "        prevSkinnedPos = float4(prevPos, 1.0);\n"
                                                                                                                                                  "    float4x4 prevModel = obj.flags1.z != 0 ? inst.prevModelMatrix : inst.modelMatrix;\n"
                                                                                                                                                  "    float hasHistory = (obj.flags1.z != 0 || obj.flags0.y != 0 || obj.flags1.y != 0) ? 1.0 : "
                                                                                                                                                  "0.0;\n"
                                                                                                                                                  "    return buildVertex(skinnedPos.xyz,\n"
                                                                                                                                                  "                       prevSkinnedPos.xyz,\n"
                                                                                                                                                  "                       safe_normalize3(skinnedNormal, float3(0.0, 0.0, 1.0)),\n"
                                                                                                                                                  "                       float4(safe_normalize3(skinnedTangent, float3(1.0, 0.0, 0.0)), currTangent.w),\n"
                                                                                                                                                  "                       in.uv,\n"
                                                                                                                                                  "                       in.uv1,\n"
                                                                                                                                                  "                       in.color,\n"
                                                                                                                                                  "                       inst.modelMatrix,\n"
                                                                                                                                                  "                       inst.normalMatrix,\n"
                                                                                                                                                  "                       prevModel,\n"
                                                                                                                                                  "                       obj,\n"
                                                                                                                                                  "                       scene,\n"
                                                                                                                                                  "                       hasHistory);\n"
                                                                                                                                                  "}\n"
                                                                                                                                                  "\n"
                                                                                                                                                  "vertex ShadowOut vertex_shadow(\n"
                                                                                                                                                  "    VertexIn in [[stage_in]],\n"
                                                                                                                                                  "    constant PerObject &obj [[buffer(1)]],\n"
                                                                                                                                                  "    constant float4x4 *bonePalette [[buffer(3)]],\n"
                                                                                                                                                  "    constant float *morphDeltas [[buffer(4)]],\n"
                                                                                                                                                  "    constant float *morphWeights [[buffer(5)]],\n"
                                                                                                                                                  "    uint vid [[vertex_id]]) {\n"
                                                                                                                                                  "    ShadowOut out;\n"
                                                                                                                                                  "    float3 pos = applyMorphPosition(in.position, obj, morphDeltas, morphWeights, vid);\n"
                                                                                                                                                  "    float4 skinnedPos = skinPosition(float4(pos, 1.0), in, bonePalette, obj.flags0.x);\n"
                                                                                                                                                  "    if (obj.flags0.x == 0)\n"
                                                                                                                                                  "        skinnedPos = float4(pos, 1.0);\n"
                                                                                                                                                  "    out.position = clipZToBackend(obj.viewProjection * (obj.modelMatrix * skinnedPos));\n"
                                                                                                                                                  "    out.uv = in.uv;\n"
                                                                                                                                                  "    out.uv1 = in.uv1;\n"
                                                                                                                                                  "    out.color = in.color;\n"
                                                                                                                                                  "    return out;\n"
                                                                                                                                                  "}\n"
                                                                                                                                                  "\n"
                                                                                                                                                  "float2 shadow_material_uv(ShadowOut in, constant PerMaterial &material, int slot) {\n"
                                                                                                                                                  "    int uvSet = slot < 4 ? material.textureUvSets0[slot] : "
                                                                                                                                                  "material.textureUvSets1[slot - 4];\n"
                                                                                                                                                  "    float2 uv = uvSet != 0 ? in.uv1 : in.uv;\n"
                                                                                                                                                  "    float4 m = material.textureUvTransform0[slot];\n"
                                                                                                                                                  "    float4 t = material.textureUvTransform1[slot];\n"
                                                                                                                                                  "    return float2(uv.x * m.x + uv.y * m.y + t.x,\n"
                                                                                                                                                  "                  uv.x * m.z + uv.y * m.w + t.y);\n"
                                                                                                                                                  "}\n"
                                                                                                                                                  "\n"
                                                                                                                                                  "fragment void fragment_shadow(\n"
                                                                                                                                                  "    ShadowOut in [[stage_in]],\n"
                                                                                                                                                  "    constant PerMaterial &material [[buffer(1)]],\n"
                                                                                                                                                  "    texture2d<float> diffuseTex [[texture(0)]],\n"
                                                                                                                                                  "    sampler diffuseSampler [[sampler(0)]]) {\n"
                                                                                                                                                  "    float alpha = material.diffuseColor.a * material.scalars.x * in.color.a;\n"
                                                                                                                                                  "    if (material.flags0.x != 0)\n"
                                                                                                                                                  "        alpha *= diffuseTex.sample(diffuseSampler, shadow_material_uv(in, material, 0)).a;\n"
                                                                                                                                                  "    if (material.pbrFlags.y == 1 && alpha < material.pbrScalars1.y)\n"
                                                                                                                                                  "        discard_fragment();\n"
                                                                                                                                                  "}\n"
                                                                                                                                                  "\n"
                                                                                                                                                  "float4 motion_output(VertexOut in) {\n"
                                                                                                                                                  "    float currW = (in.currClip.w < 0.0 ? -1.0 : 1.0) * max(fabs(in.currClip.w), 0.0001);\n"
                                                                                                                                                  "    float prevW = (in.prevClip.w < 0.0 ? -1.0 : 1.0) * max(fabs(in.prevClip.w), 0.0001);\n"
                                                                                                                                                  "    float2 currNdc = in.currClip.xy / currW;\n"
                                                                                                                                                  "    float2 prevNdc = in.prevClip.xy / prevW;\n"
                                                                                                                                                  "    float2 velocity = (currNdc - prevNdc) * 0.5;\n"
                                                                                                                                                  "    return float4(clamp(velocity * 0.5 + 0.5, 0.0, 1.0), in.hasObjectHistory, 1.0);\n"
                                                                                                                                                  "}\n"
                                                                                                                                                  "\n"
                                                                                                                                                  "float distribution_ggx(float NdotH, float roughness) {\n"
                                                                                                                                                  "    float a = roughness * roughness;\n"
                                                                                                                                                  "    float a2 = a * a;\n"
                                                                                                                                                  "    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;\n"
                                                                                                                                                  "    return a2 / (3.14159265 * denom * denom + 1e-6);\n"
                                                                                                                                                  "}\n"
                                                                                                                                                  "\n"
                                                                                                                                                  "float geometry_schlick_ggx(float NdotV, float roughness) {\n"
                                                                                                                                                  "    float r = roughness + 1.0;\n"
                                                                                                                                                  "    float k = (r * r) / 8.0;\n"
                                                                                                                                                  "    return NdotV / (NdotV * (1.0 - k) + k + 1e-6);\n"
                                                                                                                                                  "}\n"
                                                                                                                                                  "\n"
                                                                                                                                                  "float geometry_smith(float NdotV, float NdotL, float roughness) {\n"
                                                                                                                                                  "    return geometry_schlick_ggx(NdotV, roughness) * geometry_schlick_ggx(NdotL, roughness);\n"
                                                                                                                                                  "}\n"
                                                                                                                                                  "\n"
                                                                                                                                                  "float3 fresnel_schlick(float cosTheta, float3 F0) {\n"
                                                                                                                                                  "    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);\n"
                                                                                                                                                  "}\n"
                                                                                                                                                  "\n"
                                                                                                                                                  "float3 srgb_to_linear(float3 c) {\n"
                                                                                                                                                  "    float3 low = c / 12.92;\n"
                                                                                                                                                  "    float3 high = pow((c + float3(0.055)) / 1.055, float3(2.4));\n"
                                                                                                                                                  "    return mix(low, high, step(float3(0.04045), c));\n"
                                                                                                                                                  "}\n"
                                                                                                                                                  "\n"
                                                                                                                                                  "float3 env_sample(texturecube<float> envTex,\n"
                                                                                                                                                  "                  sampler envSampler,\n"
                                                                                                                                                  "                  float3 dir,\n"
                                                                                                                                                  "                  float roughness,\n"
                                                                                                                                                  "                  float maxLod) {\n"
                                                                                                                                                  "    float lod = clamp(roughness, 0.0, 1.0) * max(maxLod, 0.0);\n"
                                                                                                                                                  "    return envTex.sample(envSampler, safe_normalize3(dir, float3(0.0, 0.0, 1.0)), level(lod)).rgb;\n"
                                                                                                                                                  "}\n"
                                                                                                                                                  "\n"
                                                                                                                                                  "int texture_uv_set_at(constant PerMaterial &material, int slot) {\n"
                                                                                                                                                  "    return slot < 4 ? material.textureUvSets0[slot] : material.textureUvSets1[slot - 4];\n"
                                                                                                                                                  "}\n"
                                                                                                                                                  "\n"
                                                                                                                                                  "float2 material_uv(VertexOut in, constant PerMaterial &material, int slot) {\n"
                                                                                                                                                  "    float2 uv = texture_uv_set_at(material, slot) != 0 ? in.uv1 : in.uv;\n"
                                                                                                                                                  "    float4 m = material.textureUvTransform0[slot];\n"
                                                                                                                                                  "    float4 t = material.textureUvTransform1[slot];\n"
                                                                                                                                                  "    return float2(uv.x * m.x + uv.y * m.y + t.x,\n"
                                                                                                                                                  "                  uv.x * m.z + uv.y * m.w + t.y);\n"
                                                                                                                                                  "}\n"
                                                                                                                                                  "\n"
                                                                                                                                                  "int resolve_shadow_cascade(constant Light &light,\n"
                                                                                                                                                  "                           float3 worldPos,\n"
                                                                                                                                                  "                           constant PerScene &scene) {\n"
                                                                                                                                                  "    int shadowIndex = light.shadowIndex;\n"
                                                                                                                                                  "    if (shadowIndex < 0 || shadowIndex >= scene.counts.y)\n"
                                                                                                                                                  "        return -1;\n"
                                                                                                                                                  "    int cascadeCount = clamp(light.shadowCascadeCount, 1, " VGFX3D_STR(VGFX3D_MAX_SHADOW_LIGHTS) ");\n"
                                                                                                                                                                                                                                                    "    cascadeCount = min(cascadeCount, scene.counts.y - shadowIndex);\n"
                                                                                                                                                                                                                                                    "    if (cascadeCount <= 1)\n"
                                                                                                                                                                                                                                                    "        return shadowIndex;\n"
                                                                                                                                                                                                                                                    "    float viewDepth = dot(worldPos - scene.cameraPosition.xyz, scene.cameraForward.xyz);\n"
                                                                                                                                                                                                                                                    "    if (viewDepth <= light.shadowCascadeSplits.x || cascadeCount == 1)\n"
                                                                                                                                                                                                                                                    "        return shadowIndex;\n"
                                                                                                                                                                                                                                                    "    if (viewDepth <= light.shadowCascadeSplits.y || cascadeCount == 2)\n"
                                                                                                                                                                                                                                                    "        return shadowIndex + 1;\n"
                                                                                                                                                                                                                                                    "    if (viewDepth <= light.shadowCascadeSplits.z || cascadeCount == 3)\n"
                                                                                                                                                                                                                                                    "        return shadowIndex + 2;\n"
                                                                                                                                                                                                                                                    "    return shadowIndex + 3;\n"
                                                                                                                                                                                                                                                    "}\n"
                                                                                                                                                                                                                                                    "\n"
                                                                                                                                                                                                                                                    "float sample_shadow_at(int shadowIndex,\n"
                                                                                                                                                                                                                                                    "                       float2 uv,\n"
                                                                                                                                                                                                                                                    "                       float depth,\n"
                                                                                                                                                                                                                                                    "                       constant PerScene &scene,\n"
                                                                                                                                                                                                                                                    "                       depth2d<float> shadowMap0,\n"
                                                                                                                                                                                                                                                    "                       depth2d<float> shadowMap1,\n"
                                                                                                                                                                                                                                                    "                       depth2d<float> shadowMap2,\n"
                                                                                                                                                                                                                                                    "                       depth2d<float> shadowMap3,\n"
                                                                                                                                                                                                                                                    "                       sampler shadowSampler) {\n"
                                                                                                                                                                                                                                                    "    if (shadowIndex == 0)\n"
                                                                                                                                                                                                                                                    "        return shadowMap0.sample_compare(shadowSampler, uv, depth - scene.fogParams.z);\n"
                                                                                                                                                                                                                                                    "    if (shadowIndex == 1)\n"
                                                                                                                                                                                                                                                    "        return shadowMap1.sample_compare(shadowSampler, uv, depth - scene.fogParams.z);\n"
                                                                                                                                                                                                                                                    "    if (shadowIndex == 2)\n"
                                                                                                                                                                                                                                                    "        return shadowMap2.sample_compare(shadowSampler, uv, depth - scene.fogParams.z);\n"
                                                                                                                                                                                                                                                    "    return shadowMap3.sample_compare(shadowSampler, uv, depth - scene.fogParams.z);\n"
                                                                                                                                                                                                                                                    "}\n"
                                                                                                                                                                                                                                                    "\n"
                                                                                                                                                                                                                                                    "float sample_shadow(constant Light &light,\n"
                                                                                                                                                                                                                                                    "                    float3 worldPos,\n"
                                                                                                                                                                                                                                                    "                    constant PerScene &scene,\n"
                                                                                                                                                                                                                                                    "                    depth2d<float> shadowMap0,\n"
                                                                                                                                                                                                                                                    "                    depth2d<float> shadowMap1,\n"
                                                                                                                                                                                                                                                    "                    depth2d<float> shadowMap2,\n"
                                                                                                                                                                                                                                                    "                    depth2d<float> shadowMap3,\n"
                                                                                                                                                                                                                                                    "                    sampler shadowSampler) {\n"
                                                                                                                                                                                                                                                    "    int shadowIndex = resolve_shadow_cascade(light, worldPos, scene);\n"
                                                                                                                                                                                                                                                    "    if (shadowIndex < 0 || shadowIndex >= scene.counts.y)\n"
                                                                                                                                                                                                                                                    "        return 1.0;\n"
                                                                                                                                                                                                                                                    "    float4 lc = scene.shadowVP[shadowIndex] * float4(worldPos, 1.0);\n"
                                                                                                                                                                                                                                                    "    if (lc.w <= 0.0001)\n"
                                                                                                                                                                                                                                                    "        return 1.0;\n"
                                                                                                                                                                                                                                                    "    float3 suv = lc.xyz / lc.w;\n"
                                                                                                                                                                                                                                                    "    suv.xy = suv.xy * 0.5 + 0.5;\n"
                                                                                                                                                                                                                                                    "    suv.y = 1.0 - suv.y;\n"
                                                                                                                                                                                                                                                    "    if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0 || suv.z < 0.0 || suv.z > 1.0)\n"
                                                                                                                                                                                                                                                    "        return 1.0;\n"
                                                                                                                                                                                                                                                    "    return sample_shadow_at(shadowIndex, suv.xy, suv.z, scene, shadowMap0, shadowMap1, shadowMap2, shadowMap3, shadowSampler);\n"
                                                                                                                                                                                                                                                    "}\n"
                                                                                                                                                                                                                                                    "\n"
                                                                                                                                                                                                                                                    "fragment MainOut fragment_main(\n"
                                                                                                                                                                                                                                                    "    VertexOut in [[stage_in]],\n"
                                                                                                                                                                                                                                                    "    constant PerScene &scene [[buffer(0)]],\n"
                                                                                                                                                                                                                                                    "    constant PerMaterial &material [[buffer(1)]],\n"
                                                                                                                                                                                                                                                    "    constant Light *lights [[buffer(2)]],\n"
                                                                                                                                                                                                                                                    "    texture2d<float> diffuseTex [[texture(0)]],\n"
                                                                                                                                                                                                                                                    "    texture2d<float> normalTex [[texture(1)]],\n"
                                                                                                                                                                                                                                                    "    texture2d<float> specularTex [[texture(2)]],\n"
                                                                                                                                                                                                                                                    "    texture2d<float> emissiveTex [[texture(3)]],\n"
                                                                                                                                                                                                                                                    "    depth2d<float> shadowMap0 [[texture(4)]],\n"
                                                                                                                                                                                                                                                    "    depth2d<float> shadowMap1 [[texture(5)]],\n"
                                                                                                                                                                                                                                                    "    depth2d<float> shadowMap2 [[texture(6)]],\n"
                                                                                                                                                                                                                                                    "    depth2d<float> shadowMap3 [[texture(7)]],\n"
                                                                                                                                                                                                                                                    "    texture2d<float> splatTex [[texture(8)]],\n"
                                                                                                                                                                                                                                                    "    texture2d<float> splatLayer0 [[texture(9)]],\n"
                                                                                                                                                                                                                                                    "    texture2d<float> splatLayer1 [[texture(10)]],\n"
                                                                                                                                                                                                                                                    "    texture2d<float> splatLayer2 [[texture(11)]],\n"
                                                                                                                                                                                                                                                    "    texture2d<float> splatLayer3 [[texture(12)]],\n"
                                                                                                                                                                                                                                                    "    texturecube<float> envTex [[texture(13)]],\n"
                                                                                                                                                                                                                                                    "    texture2d<float> metallicRoughnessTex [[texture(14)]],\n"
                                                                                                                                                                                                                                                    "    texture2d<float> aoTex [[texture(15)]],\n"
                                                                                                                                                                                                                                                    "    sampler diffuseSampler [[sampler(0)]],\n"
                                                                                                                                                                                                                                                    "    sampler shadowSampler [[sampler(1)]],\n"
                                                                                                                                                                                                                                                    "    sampler envSampler [[sampler(2)]],\n"
                                                                                                                                                                                                                                                    "    sampler normalSampler [[sampler(3)]],\n"
                                                                                                                                                                                                                                                    "    sampler specularSampler [[sampler(4)]],\n"
                                                                                                                                                                                                                                                    "    sampler emissiveSampler [[sampler(5)]],\n"
                                                                                                                                                                                                                                                    "    sampler metallicRoughnessSampler [[sampler(6)]],\n"
                                                                                                                                                                                                                                                    "    sampler aoSampler [[sampler(7)]]\n"
                                                                                                                                                                                                                                                    ") {\n"
                                                                                                                                                                                                                                                    "    MainOut out;\n"
                                                                                                                                                                                                                                                    "    float3 baseColor = material.diffuseColor.rgb * in.color.rgb;\n"
                                                                                                                                                                                                                                                    "    float texAlpha = 1.0;\n"
                                                                                                                                                                                                                                                    "    float materialAlpha = material.diffuseColor.a * material.scalars.x * in.color.a;\n"
                                                                                                                                                                                                                                                    "    if (material.flags0.x != 0) {\n"
                                                                                                                                                                                                                                                    "        float4 texSample = diffuseTex.sample(diffuseSampler, material_uv(in, material, 0));\n"
                                                                                                                                                                                                                                                    "        if (material.pbrFlags.x != 0) texSample.rgb = srgb_to_linear(texSample.rgb);\n"
                                                                                                                                                                                                                                                    "        baseColor *= texSample.rgb;\n"
                                                                                                                                                                                                                                                    "        texAlpha = texSample.a;\n"
                                                                                                                                                                                                                                                    "    }\n"
                                                                                                                                                                                                                                                    "    if (material.flags1.z != 0) {\n"
                                                                                                                                                                                                                                                    "        float4 sp = splatTex.sample(diffuseSampler, in.uv);\n"
                                                                                                                                                                                                                                                    "        float wsum = sp.r + sp.g + sp.b + sp.a;\n"
                                                                                                                                                                                                                                                    "        if (wsum > 0.001) sp /= wsum;\n"
                                                                                                                                                                                                                                                    "        float3 blended = float3(0);\n"
                                                                                                                                                                                                                                                    "        if (sp.r > 0.001) blended += splatLayer0.sample(diffuseSampler, in.uv * "
                                                                                                                                                                                                                                                    "material.splatScales.x).rgb * sp.r;\n"
                                                                                                                                                                                                                                                    "        if (sp.g > 0.001) blended += splatLayer1.sample(diffuseSampler, in.uv * "
                                                                                                                                                                                                                                                    "material.splatScales.y).rgb * sp.g;\n"
                                                                                                                                                                                                                                                    "        if (sp.b > 0.001) blended += splatLayer2.sample(diffuseSampler, in.uv * "
                                                                                                                                                                                                                                                    "material.splatScales.z).rgb * sp.b;\n"
                                                                                                                                                                                                                                                    "        if (sp.a > 0.001) blended += splatLayer3.sample(diffuseSampler, in.uv * "
                                                                                                                                                                                                                                                    "material.splatScales.w).rgb * sp.a;\n"
                                                                                                                                                                                                                                                    "        baseColor = blended * material.diffuseColor.rgb * in.color.rgb;\n"
                                                                                                                                                                                                                                                    "    }\n"
                                                                                                                                                                                                                                                    "    float3 N = safe_normalize3(in.normal, float3(0.0, 0.0, 1.0));\n"
                                                                                                                                                                                                                                                    "    float3 cameraToWorld = scene.cameraPosition.xyz - in.worldPos;\n"
                                                                                                                                                                                                                                                    "    float3 V = safe_normalize3(scene.cameraPosition.w > 0.5 ? -scene.cameraForward.xyz : "
                                                                                                                                                                                                                                                    "cameraToWorld, float3(0.0, 0.0, 1.0));\n"
                                                                                                                                                                                                                                                    "    float viewDistance = scene.cameraPosition.w > 0.5\n"
                                                                                                                                                                                                                                                    "        ? abs(dot(in.worldPos - scene.cameraPosition.xyz, scene.cameraForward.xyz))\n"
                                                                                                                                                                                                                                                    "        : length(cameraToWorld);\n"
                                                                                                                                                                                                                                                    "    if (material.flags0.z != 0) {\n"
                                                                                                                                                                                                                                                    "        float3 T = safe_normalize3(in.tangent.xyz, float3(1.0, 0.0, 0.0));\n"
                                                                                                                                                                                                                                                    "        T = safe_normalize3(T - N * dot(T, N), float3(1.0, 0.0, 0.0));\n"
                                                                                                                                                                                                                                                    "        float lenT = length(T);\n"
                                                                                                                                                                                                                                                    "        if (lenT > 0.001) {\n"
                                                                                                                                                                                                                                                    "            float3 B = safe_normalize3(cross(N, T), float3(0.0, 1.0, 0.0)) * (in.tangent.w < 0.0 ? -1.0 : 1.0);\n"
                                                                                                                                                                                                                                                    "            float3 mapN = normalTex.sample(normalSampler, material_uv(in, material, 1)).rgb * 2.0 - 1.0;\n"
                                                                                                                                                                                                                                                    "            mapN.xy *= material.pbrScalars1.x;\n"
                                                                                                                                                                                                                                                    "            N = safe_normalize3(T * mapN.x + B * mapN.y + N * mapN.z, N);\n"
                                                                                                                                                                                                                                                    "        }\n"
                                                                                                                                                                                                                                                    "    }\n"
                                                                                                                                                                                                                                                    "    float3 emissive = material.emissiveColor.rgb * material.pbrScalars0.w;\n"
                                                                                                                                                                                                                                                    "    if (material.flags1.x != 0) {\n"
                                                                                                                                                                                                                                                    "        float3 emissiveSample = emissiveTex.sample(emissiveSampler, material_uv(in, material, 3)).rgb;\n"
                                                                                                                                                                                                                                                    "        if (material.pbrFlags.x != 0) emissiveSample = srgb_to_linear(emissiveSample);\n"
                                                                                                                                                                                                                                                    "        emissive *= emissiveSample;\n"
                                                                                                                                                                                                                                                    "    }\n"
                                                                                                                                                                                                                                                    "    float4 metallicRoughnessSample = float4(1.0);\n"
                                                                                                                                                                                                                                                    "    float envRoughness = clamp(material.pbrScalars0.y, 0.0, 1.0);\n"
                                                                                                                                                                                                                                                    "    if (material.pbrFlags.z != 0) {\n"
                                                                                                                                                                                                                                                    "        metallicRoughnessSample = metallicRoughnessTex.sample(metallicRoughnessSampler, material_uv(in, material, 4));\n"
                                                                                                                                                                                                                                                    "        envRoughness = clamp(envRoughness * metallicRoughnessSample.g, 0.045, 1.0);\n"
                                                                                                                                                                                                                                                    "    }\n"
                                                                                                                                                                                                                                                    "    float finalAlpha = materialAlpha * texAlpha;\n"
                                                                                                                                                                                                                                                    "    if (material.pbrFlags.y == 1) {\n"
                                                                                                                                                                                                                                                    "        if (finalAlpha < material.pbrScalars1.y)\n"
                                                                                                                                                                                                                                                    "            discard_fragment();\n"
                                                                                                                                                                                                                                                    "        finalAlpha = 1.0;\n"
                                                                                                                                                                                                                                                    "    } else if (material.pbrFlags.y == 0) {\n"
                                                                                                                                                                                                                                                    "        finalAlpha = 1.0;\n"
                                                                                                                                                                                                                                                    "    }\n"
                                                                                                                                                                                                                                                    "    if (material.flags0.y != 0) {\n"
                                                                                                                                                                                                                                                    "        float3 unlitColor = baseColor + emissive;\n"
                                                                                                                                                                                                                                                    "        if (material.flags1.y != 0) {\n"
                                                                                                                                                                                                                                                    "            float3 R = reflect(-V, N);\n"
                                                                                                                                                                                                                                                    "            float3 envColor = env_sample(envTex, envSampler, R, envRoughness, "
                                                                                                                                                                                                                                                    "material.scalars.z);\n"
                                                                                                                                                                                                                                                    "            unlitColor = mix(unlitColor, envColor, clamp(material.scalars.y, 0.0, 1.0));\n"
                                                                                                                                                                                                                                                    "        }\n"
                                                                                                                                                                                                                                                    "        if (scene.fogColor.a > 0.5) {\n"
                                                                                                                                                                                                                                                    "            float fogRange = scene.fogParams.y - scene.fogParams.x;\n"
                                                                                                                                                                                                                                                    "            float fogFactor = clamp((viewDistance - scene.fogParams.x) / max(fogRange, "
                                                                                                                                                                                                                                                    "0.001), 0.0, 1.0);\n"
                                                                                                                                                                                                                                                    "            unlitColor = mix(unlitColor, scene.fogColor.rgb, fogFactor);\n"
                                                                                                                                                                                                                                                    "        }\n"
                                                                                                                                                                                                                                                    "        out.color = float4(unlitColor, finalAlpha);\n"
                                                                                                                                                                                                                                                    "        out.motion = motion_output(in);\n"
                                                                                                                                                                                                                                                    "        return out;\n"
                                                                                                                                                                                                                                                    "    }\n"
                                                                                                                                                                                                                                                    "    float3 result = float3(0.0);\n"
                                                                                                                                                                                                                                                    "    if (material.pbrFlags.x != 0) {\n"
                                                                                                                                                                                                                                                    "        float metallic = clamp(material.pbrScalars0.x, 0.0, 1.0);\n"
                                                                                                                                                                                                                                                    "        float roughness = clamp(material.pbrScalars0.y, 0.045, 1.0);\n"
                                                                                                                                                                                                                                                    "        float ao = clamp(material.pbrScalars0.z, 0.0, 1.0);\n"
                                                                                                                                                                                                                                                    "        if (material.pbrFlags.z != 0) {\n"
                                                                                                                                                                                                                                                    "            roughness = clamp(roughness * metallicRoughnessSample.g, 0.045, 1.0);\n"
                                                                                                                                                                                                                                                    "            metallic = clamp(metallic * metallicRoughnessSample.b, 0.0, 1.0);\n"
                                                                                                                                                                                                                                                    "            envRoughness = roughness;\n"
                                                                                                                                                                                                                                                    "        }\n"
                                                                                                                                                                                                                                                    "        if (material.pbrFlags.w != 0) {\n"
                                                                                                                                                                                                                                                    "            float4 aoSample = aoTex.sample(aoSampler, material_uv(in, material, 5));\n"
                                                                                                                                                                                                                                                    "            ao = clamp(ao * aoSample.r, 0.0, 1.0);\n"
                                                                                                                                                                                                                                                    "        }\n"
                                                                                                                                                                                                                                                    "        result = scene.ambientColor.rgb * baseColor * ao;\n"
                                                                                                                                                                                                                                                    "        for (int i = 0; i < scene.counts.x; i++) {\n"
                                                                                                                                                                                                                                                    "            float3 L; float atten = 1.0;\n"
                                                                                                                                                                                                                                                    "            if (lights[i].type == 0) {\n"
                                                                                                                                                                                                                                                    "                L = safe_normalize3(-lights[i].direction.xyz, float3(0.0, -1.0, 0.0));\n"
                                                                                                                                                                                                                                                    "            } else if (lights[i].type == 1) {\n"
                                                                                                                                                                                                                                                    "                float3 tl = lights[i].position.xyz - in.worldPos;\n"
                                                                                                                                                                                                                                                    "                float d = length(tl); L = safe_normalize3(tl, float3(0.0));\n"
                                                                                                                                                                                                                                                    "                atten = 1.0 / (1.0 + lights[i].attenuation * d * d);\n"
                                                                                                                                                                                                                                                    "            } else if (lights[i].type == 3) {\n"
                                                                                                                                                                                                                                                    "                float3 tl = lights[i].position.xyz - in.worldPos;\n"
                                                                                                                                                                                                                                                    "                float d = length(tl); L = safe_normalize3(tl, float3(0.0));\n"
                                                                                                                                                                                                                                                    "                atten = 1.0 / (1.0 + lights[i].attenuation * d * d);\n"
                                                                                                                                                                                                                                                    "                float spotDot = dot(-L, safe_normalize3(lights[i].direction.xyz, float3(0.0, -1.0, 0.0)));\n"
                                                                                                                                                                                                                                                    "                if (spotDot < lights[i].outer_cos) {\n"
                                                                                                                                                                                                                                                    "                    atten = 0.0;\n"
                                                                                                                                                                                                                                                    "                } else if (spotDot < lights[i].inner_cos) {\n"
                                                                                                                                                                                                                                                    "                    float coneRange = lights[i].inner_cos - lights[i].outer_cos;\n"
                                                                                                                                                                                                                                                    "                    float t = coneRange > 0.0001 ? clamp((spotDot - lights[i].outer_cos) / coneRange, 0.0, 1.0) : 0.0;\n"
                                                                                                                                                                                                                                                    "                    atten *= t * t * (3.0 - 2.0 * t);\n"
                                                                                                                                                                                                                                                    "                }\n"
                                                                                                                                                                                                                                                    "            } else {\n"
                                                                                                                                                                                                                                                    "                result += lights[i].color.rgb * lights[i].intensity * baseColor * ao;\n"
                                                                                                                                                                                                                                                    "                continue;\n"
                                                                                                                                                                                                                                                    "            }\n"
                                                                                                                                                                                                                                                    "            float NdotL = max(dot(N, L), 0.0);\n"
                                                                                                                                                                                                                                                    "            if (lights[i].type == 0)\n"
                                                                                                                                                                                                                                                    "                atten *= mix(0.15, 1.0, sample_shadow(lights[i], in.worldPos, "
                                                                                                                                                                                                                                                    "scene, shadowMap0, shadowMap1, shadowMap2, shadowMap3, shadowSampler));\n"
                                                                                                                                                                                                                                                    "            if (NdotL <= 0.0)\n"
                                                                                                                                                                                                                                                    "                continue;\n"
                                                                                                                                                                                                                                                    "            float3 H = safe_normalize3(L + V, N);\n"
                                                                                                                                                                                                                                                    "            float NdotV = max(dot(N, V), 0.001);\n"
                                                                                                                                                                                                                                                    "            float NdotH = max(dot(N, H), 0.0);\n"
                                                                                                                                                                                                                                                    "            float VdotH = max(dot(V, H), 0.0);\n"
                                                                                                                                                                                                                                                    "            float3 F0 = mix(float3(0.04), baseColor, metallic);\n"
                                                                                                                                                                                                                                                    "            float3 F = fresnel_schlick(VdotH, F0);\n"
                                                                                                                                                                                                                                                    "            float D = distribution_ggx(NdotH, roughness);\n"
                                                                                                                                                                                                                                                    "            float G = geometry_smith(NdotV, NdotL, roughness);\n"
                                                                                                                                                                                                                                                    "            float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.0001);\n"
                                                                                                                                                                                                                                                    "            float3 kS = F;\n"
                                                                                                                                                                                                                                                    "            float3 kD = (1.0 - kS) * (1.0 - metallic);\n"
                                                                                                                                                                                                                                                    "            float3 diffuse = kD * baseColor / 3.14159265;\n"
                                                                                                                                                                                                                                                    "            float3 radiance = lights[i].color.rgb * lights[i].intensity * atten;\n"
                                                                                                                                                                                                                                                    "            result += (diffuse + specular) * radiance * NdotL;\n"
                                                                                                                                                                                                                                                    "        }\n"
                                                                                                                                                                                                                                                    "    } else {\n"
                                                                                                                                                                                                                                                    "        result = scene.ambientColor.rgb * baseColor;\n"
                                                                                                                                                                                                                                                    "        float3 specColor = material.specularColor.rgb;\n"
                                                                                                                                                                                                                                                    "        if (material.flags0.w != 0) {\n"
                                                                                                                                                                                                                                                    "            specColor *= specularTex.sample(specularSampler, material_uv(in, material, 2)).rgb;\n"
                                                                                                                                                                                                                                                    "        }\n"
                                                                                                                                                                                                                                                    "        for (int i = 0; i < scene.counts.x; i++) {\n"
                                                                                                                                                                                                                                                    "            float3 L; float atten = 1.0;\n"
                                                                                                                                                                                                                                                    "            if (lights[i].type == 0) {\n"
                                                                                                                                                                                                                                                    "                L = safe_normalize3(-lights[i].direction.xyz, float3(0.0, -1.0, 0.0));\n"
                                                                                                                                                                                                                                                    "            } else if (lights[i].type == 1) {\n"
                                                                                                                                                                                                                                                    "                float3 tl = lights[i].position.xyz - in.worldPos;\n"
                                                                                                                                                                                                                                                    "                float d = length(tl); L = safe_normalize3(tl, float3(0.0));\n"
                                                                                                                                                                                                                                                    "                atten = 1.0 / (1.0 + lights[i].attenuation * d * d);\n"
                                                                                                                                                                                                                                                    "            } else if (lights[i].type == 3) {\n"
                                                                                                                                                                                                                                                    "                float3 tl = lights[i].position.xyz - in.worldPos;\n"
                                                                                                                                                                                                                                                    "                float d = length(tl); L = safe_normalize3(tl, float3(0.0));\n"
                                                                                                                                                                                                                                                    "                atten = 1.0 / (1.0 + lights[i].attenuation * d * d);\n"
                                                                                                                                                                                                                                                    "                float spotDot = dot(-L, safe_normalize3(lights[i].direction.xyz, float3(0.0, -1.0, 0.0)));\n"
                                                                                                                                                                                                                                                    "                if (spotDot < lights[i].outer_cos) {\n"
                                                                                                                                                                                                                                                    "                    atten = 0.0;\n"
                                                                                                                                                                                                                                                    "                } else if (spotDot < lights[i].inner_cos) {\n"
                                                                                                                                                                                                                                                    "                    float coneRange = lights[i].inner_cos - lights[i].outer_cos;\n"
                                                                                                                                                                                                                                                    "                    float t = coneRange > 0.0001 ? clamp((spotDot - lights[i].outer_cos) / coneRange, 0.0, 1.0) : 0.0;\n"
                                                                                                                                                                                                                                                    "                    atten *= t * t * (3.0 - 2.0 * t);\n"
                                                                                                                                                                                                                                                    "                }\n"
                                                                                                                                                                                                                                                    "            } else {\n"
                                                                                                                                                                                                                                                    "                result += lights[i].color.rgb * lights[i].intensity * baseColor;\n"
                                                                                                                                                                                                                                                    "                continue;\n"
                                                                                                                                                                                                                                                    "            }\n"
                                                                                                                                                                                                                                                    "            float NdotL = max(dot(N, L), 0.0);\n"
                                                                                                                                                                                                                                                    "            if (lights[i].type == 0)\n"
                                                                                                                                                                                                                                                    "                atten *= mix(0.15, 1.0, sample_shadow(lights[i], in.worldPos, "
                                                                                                                                                                                                                                                    "scene, shadowMap0, shadowMap1, shadowMap2, shadowMap3, shadowSampler));\n"
                                                                                                                                                                                                                                                    "            result += lights[i].color.rgb * lights[i].intensity * NdotL * "
                                                                                                                                                                                                                                                    "baseColor * atten;\n"
                                                                                                                                                                                                                                                    "            if (NdotL > 0.0 && material.specularColor.w > 0.0) {\n"
                                                                                                                                                                                                                                                    "                float3 H = safe_normalize3(L + V, N);\n"
                                                                                                                                                                                                                                                    "                float spec = pow(max(dot(N, H), 0.0), material.specularColor.w);\n"
                                                                                                                                                                                                                                                    "                result += lights[i].color.rgb * lights[i].intensity * spec * "
                                                                                                                                                                                                                                                    "specColor * atten;\n"
                                                                                                                                                                                                                                                    "            }\n"
                                                                                                                                                                                                                                                    "        }\n"
                                                                                                                                                                                                                                                    "    }\n"
                                                                                                                                                                                                                                                    "    result += emissive;\n"
                                                                                                                                                                                                                                                    "    if (material.flags1.y != 0) {\n"
                                                                                                                                                                                                                                                    "        float3 R = reflect(-V, N);\n"
                                                                                                                                                                                                                                                    "        float3 envColor = env_sample(envTex, envSampler, R, envRoughness, "
                                                                                                                                                                                                                                                    "material.scalars.z);\n"
                                                                                                                                                                                                                                                    "        result = mix(result, envColor, clamp(material.scalars.y, 0.0, 1.0));\n"
                                                                                                                                                                                                                                                    "    }\n"
                                                                                                                                                                                                                                                    "    if (scene.fogColor.a > 0.5) {\n"
                                                                                                                                                                                                                                                    "        float fogRange = scene.fogParams.y - scene.fogParams.x;\n"
                                                                                                                                                                                                                                                    "        float fogFactor = clamp((viewDistance - scene.fogParams.x) / max(fogRange, 0.001), "
                                                                                                                                                                                                                                                    "0.0, 1.0);\n"
                                                                                                                                                                                                                                                    "        result = mix(result, scene.fogColor.rgb, fogFactor);\n"
                                                                                                                                                                                                                                                    "    }\n"
                                                                                                                                                                                                                                                    "    if (material.shadingModel == 1) {\n"
                                                                                                                                                                                                                                                    "        float bands = material.customParams[0] > 0.5 ? material.customParams[0] : 4.0;\n"
                                                                                                                                                                                                                                                    "        result = floor(result * bands) / bands;\n"
                                                                                                                                                                                                                                                    "    } else if (material.shadingModel == 4) {\n"
                                                                                                                                                                                                                                                    "        float ndv = max(dot(N, V), 0.0);\n"
                                                                                                                                                                                                                                                    "        float power = material.customParams[0] > 0.1 ? material.customParams[0] : 3.0;\n"
                                                                                                                                                                                                                                                    "        float bias = material.customParams[1];\n"
                                                                                                                                                                                                                                                    "        float fresnel = pow(1.0 - ndv, power) + bias;\n"
                                                                                                                                                                                                                                                    "        finalAlpha *= clamp(fresnel, 0.0, 1.0);\n"
                                                                                                                                                                                                                                                    "    } else if (material.shadingModel == 5) {\n"
                                                                                                                                                                                                                                                    "        float strength = material.customParams[0] > 0.0 ? material.customParams[0] : 2.0;\n"
                                                                                                                                                                                                                                                    "        result += emissive * (strength - 1.0);\n"
                                                                                                                                                                                                                                                    "    }\n"
                                                                                                                                                                                                                                                    "    out.color = float4(result, finalAlpha);\n"
                                                                                                                                                                                                                                                    "    out.motion = motion_output(in);\n"
                                                                                                                                                                                                                                                    "    return out;\n"
                                                                                                                                                                                                                                                    "}\n";

#pragma clang diagnostic pop

//=============================================================================
// Uniform buffer structs (must match shader)
//=============================================================================

typedef struct {
    float m[16];
    float prev_m[16];
    float vp[16];
    float nm[16];
    int32_t flags0[4];
    int32_t flags1[4];
} mtl_per_object_t;

typedef struct {
    float cp[4];
    float ac[4];
    float fc[4];
    float fog_params[4];
    int32_t counts[4];
    float camera_forward[4];
    float prev_vp[16];
    float shadow_vp[VGFX3D_MAX_SHADOW_LIGHTS][16];
} mtl_per_scene_t;

typedef struct {
    int32_t type;
    int32_t shadow_index;
    int32_t shadow_cascade_count;
    float _p0;
    float dir[4];
    float pos[4];
    float col[4];
    float intensity;
    float attenuation;
    float inner_cos;
    float outer_cos;
    float shadow_cascade_splits[4];
} mtl_light_t;

typedef struct {
    float dc[4];
    float sc[4];
    float ec[4];
    float scalars[4];
    float pbrScalars0[4];
    float pbrScalars1[4];
    int32_t flags0[4];
    int32_t flags1[4];
    int32_t pbrFlags[4];
    float splatScales[4];
    int32_t shadingModel;
    float customParams[8];
    int32_t textureUvSets0[4];
    int32_t textureUvSets1[4];
    float textureUvTransform0[RT_MATERIAL3D_TEXTURE_SLOT_COUNT][4];
    float textureUvTransform1[RT_MATERIAL3D_TEXTURE_SLOT_COUNT][4];
} mtl_per_material_t;

typedef struct {
    float model[16];
    float normal[16];
    float prev_model[16];
} mtl_instance_t;

typedef struct {
    float inverseProjection[16];
    float inverseViewRotation[16];
    float cameraForward[4];
} mtl_skybox_params_t;

//=============================================================================
// Helpers
//=============================================================================

/// @brief Transpose a row-major 4x4 float matrix: dst[c][r] = src[r][c] (@p src and @p dst must not
///   alias). Used to convert engine row-major matrices to the column-major layout Metal expects.
static void transpose4x4(const float *src, float *dst) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            dst[c * 4 + r] = src[r * 4 + c];
}

/// @brief Row-major 4x4 matrix product out = a * b (out must not alias a or b).
static void mat4f_mul(const float *a, const float *b, float *out) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            out[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c] +
                             a[r * 4 + 2] * b[2 * 4 + c] + a[r * 4 + 3] * b[3 * 4 + c];
}

/// @brief Write the 4x4 identity matrix into @p out.
static void mat4f_identity(float *out) {
    if (!out)
        return;
    memset(out, 0, sizeof(float) * 16);
    out[0] = 1.0f;
    out[5] = 1.0f;
    out[10] = 1.0f;
    out[15] = 1.0f;
}

/// @brief Recount the contiguous run of completed shadow-map slots into ctx->_shadowCount.
/// @details Stops at the first slot that is incomplete or lacks a depth texture, so only a
///          prefix of ready shadow maps is reported to the lighting pass.
static void metal_recompute_shadow_count(VGFXMetalContext *ctx) {
    int32_t count = 0;
    if (!ctx)
        return;
    for (int32_t slot = 0; slot < VGFX3D_MAX_SHADOW_LIGHTS; slot++) {
        if (!ctx->_shadowComplete[slot] || !ctx->_shadowDepthTexture[slot])
            break;
        count = slot + 1;
    }
    ctx->_shadowCount = count;
}

static const float metal_skybox_vertices[] = {
    -1.0f,
    -1.0f,
    1.0f,
    3.0f,
    -1.0f,
    1.0f,
    -1.0f,
    3.0f,
    1.0f,
};

static const uint64_t k_texture_cache_max_age = 240u;
static const uint64_t k_cubemap_cache_max_age = 240u;
static const uint64_t k_morph_cache_max_age = 240u;

static int metal_copy_texture_to_rgba(VGFXMetalContext *ctx,
                                      id<MTLTexture> tex,
                                      uint8_t *dst_rgba,
                                      int32_t w,
                                      int32_t h,
                                      int32_t stride,
                                      float *dst_hdr_rgba);
static void metal_update_layer_size(VGFXMetalContext *ctx);
static int metal_capture_current_drawable_to_display_texture(VGFXMetalContext *ctx);
static id<MTLTexture> metal_encode_postfx_if_needed(VGFXMetalContext *ctx,
                                                    const vgfx3d_postfx_chain_t *postfx);

/// @brief Maximum mip LOD index for a cubemap (0 if it has no mip chain).
static float metal_cubemap_max_lod(const rt_cubemap3d *cubemap) {
    int32_t face_size;
    int32_t mip_count;

    if (!cubemap || !vgfx3d_get_cubemap_face_size(cubemap, &face_size) || face_size <= 1)
        return 0.0f;
    mip_count = vgfx3d_metal_compute_mip_count(face_size, face_size);
    return mip_count > 1 ? (float)(mip_count - 1) : 0.0f;
}

/// @brief Map a runtime color-format enum to its Metal pixel format (HDR16F → RGBA16Float, else
/// BGRA8).
static MTLPixelFormat metal_color_pixel_format(vgfx3d_metal_color_format_t format) {
    return format == VGFX3D_METAL_COLOR_FORMAT_HDR16F ? MTLPixelFormatRGBA16Float
                                                      : MTLPixelFormatBGRA8Unorm;
}

/// @brief Create a 2D color texture with the given format/usage/storage (nil on invalid args).
static id<MTLTexture> metal_new_color_texture(VGFXMetalContext *ctx,
                                              int32_t w,
                                              int32_t h,
                                              MTLPixelFormat pixel_format,
                                              MTLTextureUsage usage,
                                              MTLStorageMode storage_mode,
                                              BOOL mipmapped) {
    MTLTextureDescriptor *td;
    if (!ctx || !ctx.device || w <= 0 || h <= 0)
        return nil;
    td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pixel_format
                                                            width:(NSUInteger)w
                                                           height:(NSUInteger)h
                                                        mipmapped:mipmapped];
    td.usage = usage;
    td.storageMode = storage_mode;
    return [ctx.device newTextureWithDescriptor:td];
}

/// @brief Create a private-storage Depth32Float depth texture (nil on invalid args).
static id<MTLTexture> metal_new_depth_texture(VGFXMetalContext *ctx,
                                              int32_t w,
                                              int32_t h,
                                              MTLTextureUsage usage) {
    MTLTextureDescriptor *td;
    if (!ctx || !ctx.device || w <= 0 || h <= 0)
        return nil;
    td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                            width:(NSUInteger)w
                                                           height:(NSUInteger)h
                                                        mipmapped:NO];
    td.usage = usage;
    td.storageMode = MTLStorageModePrivate;
    return [ctx.device newTextureWithDescriptor:td];
}

/// @brief Generate the full mip chain for a texture via a one-shot blit encoder (blocks until
/// done).
static void metal_generate_mipmaps(VGFXMetalContext *ctx, id<MTLTexture> texture) {
    id<MTLCommandBuffer> cmd_buf;
    id<MTLBlitCommandEncoder> blit;

    if (!ctx || !texture || texture.mipmapLevelCount <= 1)
        return;
    cmd_buf = [ctx.commandQueue commandBuffer];
    if (!cmd_buf)
        return;
    blit = [cmd_buf blitCommandEncoder];
    if (!blit)
        return;
    [blit generateMipmapsForTexture:texture];
    [blit endEncoding];
    [cmd_buf commit];
    [cmd_buf waitUntilCompleted];
}

/// @brief Evict texture-cache entries that are empty or older than the max-age threshold.
/// @details Aging is measured in frames against ctx.frameSerial so textures unused for ~240 frames
///          are released, bounding GPU memory.
static void metal_prune_texture_cache(VGFXMetalContext *ctx) {
    NSMutableArray *keys_to_remove;

    if (!ctx || !ctx.textureCache)
        return;
    keys_to_remove = [NSMutableArray array];
    for (NSValue *key in ctx.textureCache) {
        VGFXMetalTextureCacheEntry *entry = ctx.textureCache[key];
        if (!entry || !entry.texture ||
            vgfx3d_metal_should_prune_cache_entry(
                ctx.frameSerial, entry.lastUsedFrame, k_texture_cache_max_age)) {
            [keys_to_remove addObject:key];
        }
    }
    [ctx.textureCache removeObjectsForKeys:keys_to_remove];
}

/// @brief Evict cubemap-cache entries that are empty or older than the max-age threshold.
static void metal_prune_cubemap_cache(VGFXMetalContext *ctx) {
    NSMutableArray *keys_to_remove;

    if (!ctx || !ctx.cubemapCache)
        return;
    keys_to_remove = [NSMutableArray array];
    for (NSValue *key in ctx.cubemapCache) {
        VGFXMetalCubemapCacheEntry *entry = ctx.cubemapCache[key];
        if (!entry || !entry.texture ||
            vgfx3d_metal_should_prune_cache_entry(
                ctx.frameSerial, entry.lastUsedFrame, k_cubemap_cache_max_age)) {
            [keys_to_remove addObject:key];
        }
    }
    [ctx.cubemapCache removeObjectsForKeys:keys_to_remove];
}

/// @brief Evict morph-cache entries that are empty or older than the max-age threshold.
static void metal_prune_morph_cache(VGFXMetalContext *ctx) {
    NSMutableArray *keys_to_remove;

    if (!ctx || !ctx.morphCache)
        return;
    keys_to_remove = [NSMutableArray array];
    for (NSValue *key in ctx.morphCache) {
        VGFXMetalMorphCacheEntry *entry = ctx.morphCache[key];
        if (!entry || !entry.deltaBuffer ||
            vgfx3d_metal_should_prune_cache_entry(
                ctx.frameSerial, entry.lastUsedFrame, k_morph_cache_max_age)) {
            [keys_to_remove addObject:key];
        }
    }
    [ctx.morphCache removeObjectsForKeys:keys_to_remove];
}

/// @brief Look up the cached Metal textures for a RenderTarget3D (NULL if not cached).
static VGFXMetalRenderTargetCacheEntry *metal_lookup_render_target_entry(
    VGFXMetalContext *ctx, vgfx3d_rendertarget_t *rt) {
    if (!ctx || !ctx.renderTargetCache || !rt)
        return nil;
    return ctx.renderTargetCache[[NSValue valueWithPointer:rt]];
}

/// @brief Get or create the cached color/depth/motion textures for a render target.
/// @details Recreates the textures when the target's size or HDR/UNORM format changed; otherwise
///          returns the existing entry. NULL on invalid input or allocation failure.
static VGFXMetalRenderTargetCacheEntry *metal_ensure_render_target_entry(
    VGFXMetalContext *ctx, vgfx3d_rendertarget_t *rt) {
    NSValue *key;
    VGFXMetalRenderTargetCacheEntry *entry;
    MTLTextureDescriptor *color_desc;
    MTLTextureDescriptor *depth_desc;
    MTLPixelFormat desired_color_format;

    if (!ctx || !ctx.renderTargetCache || !rt || rt->width <= 0 || rt->height <= 0)
        return nil;

    key = [NSValue valueWithPointer:rt];
    entry = ctx.renderTargetCache[key];
    desired_color_format =
        metal_color_pixel_format(vgfx3d_rendertarget_is_hdr(rt) ? VGFX3D_METAL_COLOR_FORMAT_HDR16F
                                                                : VGFX3D_METAL_COLOR_FORMAT_UNORM8);
    if (entry && entry.width == rt->width && entry.height == rt->height && entry.colorTexture &&
        entry.motionTexture && entry.depthTexture &&
        entry.colorPixelFormat == desired_color_format) {
        entry.target = rt;
        entry.lastUsedFrame = ctx.frameSerial;
        return entry;
    }

    if (entry && entry.pendingCommandBuffer) {
        [entry.pendingCommandBuffer waitUntilCompleted];
        entry.pendingCommandBuffer = nil;
    }

    entry = [[VGFXMetalRenderTargetCacheEntry alloc] init];
    entry.target = rt;
    entry.width = rt->width;
    entry.height = rt->height;
    entry.colorPixelFormat = desired_color_format;
    entry.lastUsedFrame = ctx.frameSerial;

    color_desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:desired_color_format
                                                                    width:(NSUInteger)rt->width
                                                                   height:(NSUInteger)rt->height
                                                                mipmapped:NO];
    color_desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    color_desc.storageMode = MTLStorageModeShared;
    entry.colorTexture = [ctx.device newTextureWithDescriptor:color_desc];

    color_desc.usage = MTLTextureUsageRenderTarget;
    color_desc.storageMode = MTLStorageModePrivate;
    entry.motionTexture = [ctx.device newTextureWithDescriptor:color_desc];

    depth_desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                    width:(NSUInteger)rt->width
                                                                   height:(NSUInteger)rt->height
                                                                mipmapped:NO];
    depth_desc.usage = MTLTextureUsageRenderTarget;
    depth_desc.storageMode = MTLStorageModePrivate;
    entry.depthTexture = [ctx.device newTextureWithDescriptor:depth_desc];
    ctx.renderTargetCache[key] = entry;
    return entry;
}

/// @brief Read a render target's GPU color texture back into its CPU-side Pixels (callback form).
/// @details Invoked when the runtime needs the rendered image as Pixels (e.g.
/// RenderTarget3D.AsPixels).
static int metal_sync_render_target_color(void *userdata, vgfx3d_rendertarget_t *target) {
    VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)userdata;
    VGFXMetalRenderTargetCacheEntry *entry;

    if (!ctx || !target)
        return 0;
    if (!vgfx3d_rendertarget_ensure_color(target))
        return 0;
    if (vgfx3d_rendertarget_is_hdr(target) && !vgfx3d_rendertarget_ensure_hdr_color(target))
        return 0;
    entry = metal_lookup_render_target_entry(ctx, target);
    if (!entry || !entry.colorTexture)
        return 0;
    if (entry.pendingCommandBuffer) {
        [entry.pendingCommandBuffer waitUntilCompleted];
        entry.pendingCommandBuffer = nil;
    }
    entry.lastUsedFrame = ctx.frameSerial;
    {
        int ok = metal_copy_texture_to_rgba(
            ctx,
            entry.colorTexture,
            target->color_buf,
            target->width,
            target->height,
            target->stride,
            vgfx3d_rendertarget_is_hdr(target) ? target->hdr_color_buf : NULL);
        target->hdr_color_valid = (int8_t)(ok && vgfx3d_rendertarget_is_hdr(target));
        if (ok)
            target->color_dirty = 0;
        return ok;
    }
}

/// @brief Build the render-pass descriptor for the main scene pass (color + motion + depth
/// attachments).
/// @details Configures load/store actions and clear values for the target's color, motion-vector,
/// and
///          depth textures.
static MTLRenderPassDescriptor *metal_make_scene_pass_descriptor(VGFXMetalContext *ctx,
                                                                 BOOL loadExistingColor,
                                                                 BOOL loadExistingDepth) {
    MTLRenderPassDescriptor *rp;
    id<MTLTexture> color;
    id<MTLTexture> motion;
    id<MTLTexture> depth;
    MTLClearColor clear_color;

    if (!ctx)
        return nil;
    color = nil;
    motion = nil;
    depth = nil;
    clear_color = MTLClearColorMake(ctx.clearR, ctx.clearG, ctx.clearB, 1.0);

    switch (ctx.currentTargetKind) {
        case VGFX3D_METAL_TARGET_SCENE:
            color = ctx.offscreenColor;
            motion = ctx.offscreenMotion;
            depth = ctx.depthTexture;
            break;
        case VGFX3D_METAL_TARGET_OVERLAY:
            color = ctx.overlayColorTexture;
            motion = ctx.overlayMotionTexture;
            depth = ctx.overlayDepthTexture;
            clear_color = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
            break;
        case VGFX3D_METAL_TARGET_RTT:
            color = ctx.rttColorTexture;
            motion = ctx.rttMotionTexture;
            depth = ctx.rttDepthTexture;
            break;
        case VGFX3D_METAL_TARGET_SWAPCHAIN:
            metal_update_layer_size(ctx);
            if (!ctx.drawable && ctx.metalLayer)
                ctx.drawable = [ctx.metalLayer nextDrawable];
            color = ctx.drawable ? ctx.drawable.texture : nil;
            motion = ctx.offscreenMotion;
            depth = ctx.depthTexture;
            break;
        default:
            break;
    }

    if (!color || !motion || !depth)
        return nil;

    rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = color;
    rp.colorAttachments[0].loadAction = loadExistingColor ? MTLLoadActionLoad : MTLLoadActionClear;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.colorAttachments[0].clearColor = clear_color;

    rp.colorAttachments[1].texture = motion;
    rp.colorAttachments[1].loadAction = loadExistingColor ? MTLLoadActionLoad : MTLLoadActionClear;
    rp.colorAttachments[1].storeAction = MTLStoreActionStore;
    rp.colorAttachments[1].clearColor = MTLClearColorMake(0.5, 0.5, 0.0, 1.0);

    rp.depthAttachment.texture = depth;
    rp.depthAttachment.loadAction = loadExistingDepth ? MTLLoadActionLoad : MTLLoadActionClear;
    rp.depthAttachment.storeAction = MTLStoreActionStore;
    rp.depthAttachment.clearDepth = 1.0;
    return rp;
}

/// @brief Begin the scene render-command encoder for the frame from a pass descriptor.
/// @details Sets up viewport and shared render state so subsequent draw calls can be encoded.
static void metal_begin_scene_encoder(VGFXMetalContext *ctx,
                                      BOOL loadExistingColor,
                                      BOOL loadExistingDepth) {
    double vw, vh;
    MTLViewport viewport;
    MTLRenderPassDescriptor *rp;

    if (!ctx)
        return;
    if (!ctx.cmdBuf) {
        ctx.cmdBuf = [ctx.commandQueue commandBuffer];
        if (!ctx.cmdBuf)
            return;
    }
    rp = metal_make_scene_pass_descriptor(ctx, loadExistingColor, loadExistingDepth);
    if (!rp)
        return;
    ctx.encoder = [ctx.cmdBuf renderCommandEncoderWithDescriptor:rp];
    if (!ctx.encoder)
        return;

    [ctx.encoder setRenderPipelineState:(ctx.currentTargetKind == VGFX3D_METAL_TARGET_SCENE)
                                            ? ctx.pipelineState
                                            : ctx.pipelineStateColorOnly];
    [ctx.encoder setDepthStencilState:ctx.depthState];
    vw = (double)(ctx.rttActive ? ctx.rttWidth : ctx.width);
    vh = (double)(ctx.rttActive ? ctx.rttHeight : ctx.height);
    viewport = (MTLViewport){0, 0, vw, vh, 0.0, 1.0};
    [ctx.encoder setViewport:viewport];
    [ctx.encoder setFrontFacingWinding:MTLWindingCounterClockwise];
    [ctx.encoder setCullMode:MTLCullModeBack];
}

/// @brief Recreate the main offscreen color/motion/depth textures at size (w, h) after a resize.
static void metal_recreate_main_targets(VGFXMetalContext *ctx, int32_t w, int32_t h) {
    if (!ctx || w <= 0 || h <= 0)
        return;

    ctx.depthTexture =
        metal_new_depth_texture(ctx, w, h, MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);

    if (ctx.gpuPostfxEnabled) {
        ctx.offscreenColor =
            metal_new_color_texture(ctx,
                                    w,
                                    h,
                                    metal_color_pixel_format(VGFX3D_METAL_COLOR_FORMAT_HDR16F),
                                    MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead,
                                    MTLStorageModePrivate,
                                    NO);
        ctx.offscreenMotion =
            metal_new_color_texture(ctx,
                                    w,
                                    h,
                                    MTLPixelFormatBGRA8Unorm,
                                    MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead,
                                    MTLStorageModePrivate,
                                    NO);
        ctx.overlayColorTexture =
            metal_new_color_texture(ctx,
                                    w,
                                    h,
                                    metal_color_pixel_format(VGFX3D_METAL_COLOR_FORMAT_UNORM8),
                                    MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead,
                                    MTLStorageModePrivate,
                                    NO);
        ctx.overlayMotionTexture =
            metal_new_color_texture(ctx,
                                    w,
                                    h,
                                    MTLPixelFormatBGRA8Unorm,
                                    MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead,
                                    MTLStorageModePrivate,
                                    NO);
        ctx.overlayDepthTexture = metal_new_depth_texture(ctx, w, h, MTLTextureUsageRenderTarget);
        if (ctx.postfxPipeline) {
            ctx.postfxColorTexture =
                metal_new_color_texture(ctx,
                                        w,
                                        h,
                                        metal_color_pixel_format(VGFX3D_METAL_COLOR_FORMAT_UNORM8),
                                        MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead,
                                        MTLStorageModeShared,
                                        NO);
            ctx.postfxScratchTexture =
                metal_new_color_texture(ctx,
                                        w,
                                        h,
                                        metal_color_pixel_format(VGFX3D_METAL_COLOR_FORMAT_UNORM8),
                                        MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead,
                                        MTLStorageModePrivate,
                                        NO);
        } else {
            ctx.postfxColorTexture = nil;
            ctx.postfxScratchTexture = nil;
        }
    } else {
        ctx.offscreenColor = nil;
        ctx.offscreenMotion = metal_new_color_texture(ctx,
                                                      w,
                                                      h,
                                                      MTLPixelFormatBGRA8Unorm,
                                                      MTLTextureUsageRenderTarget,
                                                      MTLStorageModePrivate,
                                                      NO);
        ctx.overlayColorTexture = nil;
        ctx.overlayMotionTexture = nil;
        ctx.overlayDepthTexture = nil;
        ctx.postfxColorTexture = nil;
        ctx.postfxScratchTexture = nil;
    }

    ctx.displayTexture = nil;
    ctx.postfxEncodedThisFrame = 0;
    ctx.postfxCompositedToDrawable = 0;
}

/// @brief Upload a band of RGBA8 source rows into a BGRA8 Metal texture, swizzling R↔B per pixel.
/// @details Metal's default textures are BGRA-order; Pixels are RGBA, so the channels are swapped
///          during the copy. Returns 1 on success.
static int metal_upload_rgba_to_bgra_texture_rows(
    id<MTLTexture> tex, const uint8_t *rgba, int32_t w, int32_t start_y, int32_t rows) {
    uint8_t *bgra;
    size_t pixel_count;
    if (!tex || !rgba || w <= 0 || rows <= 0 || start_y < 0)
        return 0;
    pixel_count = (size_t)w * (size_t)rows;
    if ((size_t)w != 0 && pixel_count / (size_t)w != (size_t)rows)
        return 0;
    if (pixel_count > SIZE_MAX / 4u)
        return 0;
    bgra = (uint8_t *)malloc(pixel_count * 4u);
    if (!bgra)
        return 0;
    for (size_t i = 0; i < pixel_count; i++) {
        bgra[i * 4 + 0] = rgba[i * 4 + 2];
        bgra[i * 4 + 1] = rgba[i * 4 + 1];
        bgra[i * 4 + 2] = rgba[i * 4 + 0];
        bgra[i * 4 + 3] = rgba[i * 4 + 3];
    }
    [tex replaceRegion:MTLRegionMake2D(0, (NSUInteger)start_y, (NSUInteger)w, (NSUInteger)rows)
           mipmapLevel:0
             withBytes:bgra
           bytesPerRow:(NSUInteger)(w * 4)];
    free(bgra);
    return 1;
}

/// @brief Upload a band of RGBA8 rows into one BGRA8 cubemap face, swizzling R↔B per pixel.
static int metal_upload_rgba_to_bgra_cubemap_rows(id<MTLTexture> tex,
                                                  const uint8_t *rgba,
                                                  int32_t w,
                                                  int32_t face,
                                                  int32_t start_y,
                                                  int32_t rows) {
    uint8_t *bgra;
    size_t pixel_count;
    if (!tex || !rgba || w <= 0 || face < 0 || face >= 6 || rows <= 0 || start_y < 0)
        return 0;
    pixel_count = (size_t)w * (size_t)rows;
    if ((size_t)w != 0 && pixel_count / (size_t)w != (size_t)rows)
        return 0;
    if (pixel_count > SIZE_MAX / 4u)
        return 0;
    bgra = (uint8_t *)malloc(pixel_count * 4u);
    if (!bgra)
        return 0;
    for (size_t i = 0; i < pixel_count; i++) {
        bgra[i * 4 + 0] = rgba[i * 4 + 2];
        bgra[i * 4 + 1] = rgba[i * 4 + 1];
        bgra[i * 4 + 2] = rgba[i * 4 + 0];
        bgra[i * 4 + 3] = rgba[i * 4 + 3];
    }
    [tex replaceRegion:MTLRegionMake2D(0, (NSUInteger)start_y, (NSUInteger)w, (NSUInteger)rows)
           mipmapLevel:0
                 slice:(NSUInteger)face
             withBytes:bgra
           bytesPerRow:(NSUInteger)(w * 4)
         bytesPerImage:(NSUInteger)((size_t)w * (size_t)rows * 4u)];
    free(bgra);
    return 1;
}

/// @brief Round @p value up to a multiple of @p alignment (for buffer/row-pitch alignment).
static size_t metal_align_up_size(size_t value, size_t alignment) {
    size_t remainder;
    if (alignment == 0)
        return value;
    remainder = value % alignment;
    if (remainder == 0)
        return value;
    if (value > SIZE_MAX - (alignment - remainder))
        return 0;
    return value + (alignment - remainder);
}

/// @brief Read a Metal texture back into a CPU RGBA8 (or HDR float) buffer via a blit + managed
/// copy.
/// @details Handles the BGRA→RGBA swizzle for the LDR path and the float path for HDR targets;
///          @p stride is the destination row pitch in bytes. Returns 1 on success.
static int metal_copy_texture_to_rgba(VGFXMetalContext *ctx,
                                      id<MTLTexture> tex,
                                      uint8_t *dst_rgba,
                                      int32_t w,
                                      int32_t h,
                                      int32_t stride,
                                      float *dst_hdr_rgba) {
    int32_t copy_w;
    int32_t copy_h;
    MTLPixelFormat pixel_format;
    size_t dst_row_bytes;
    size_t source_row_bytes;
    size_t bytes_per_row;
    size_t total_bytes;
    size_t bytes_per_pixel;
    id<MTLBuffer> readback_buffer;
    id<MTLCommandBuffer> command_buffer;
    id<MTLBlitCommandEncoder> blit;
    const uint8_t *src;
    if (!ctx || !ctx.device || !ctx.commandQueue || !tex || !dst_rgba || w <= 0 || h <= 0 ||
        stride <= 0)
        return 0;
    if ((size_t)w > SIZE_MAX / 4u)
        return 0;
    dst_row_bytes = (size_t)w * 4u;
    if ((size_t)stride < dst_row_bytes || (size_t)h > SIZE_MAX / (size_t)stride)
        return 0;

    memset(dst_rgba, 0, (size_t)stride * (size_t)h);
    copy_w = (int32_t)tex.width < w ? (int32_t)tex.width : w;
    copy_h = (int32_t)tex.height < h ? (int32_t)tex.height : h;
    if (copy_w <= 0 || copy_h <= 0)
        return 0;
    pixel_format = tex.pixelFormat;
    if (pixel_format == MTLPixelFormatRGBA16Float) {
        bytes_per_pixel = 8u;
    } else if (pixel_format == MTLPixelFormatBGRA8Unorm) {
        bytes_per_pixel = 4u;
    } else {
        return 0;
    }

    if ((size_t)copy_w > SIZE_MAX / bytes_per_pixel)
        return 0;
    source_row_bytes = (size_t)copy_w * bytes_per_pixel;
    bytes_per_row = metal_align_up_size(source_row_bytes, 256u);
    if (bytes_per_row == 0 || (size_t)copy_h > SIZE_MAX / bytes_per_row)
        return 0;
    total_bytes = bytes_per_row * (size_t)copy_h;
    readback_buffer = [ctx.device newBufferWithLength:(NSUInteger)total_bytes
                                              options:MTLResourceStorageModeShared];
    if (!readback_buffer)
        return 0;

    command_buffer = [ctx.commandQueue commandBuffer];
    if (!command_buffer)
        return 0;
    blit = [command_buffer blitCommandEncoder];
    if (!blit)
        return 0;
    [blit copyFromTexture:tex
                     sourceSlice:0
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(0, 0, 0)
                      sourceSize:MTLSizeMake((NSUInteger)copy_w, (NSUInteger)copy_h, 1)
                        toBuffer:readback_buffer
               destinationOffset:0
          destinationBytesPerRow:(NSUInteger)bytes_per_row
        destinationBytesPerImage:(NSUInteger)total_bytes];
    [blit endEncoding];
    [command_buffer commit];
    [command_buffer waitUntilCompleted];
    if (command_buffer.status == MTLCommandBufferStatusError)
        return 0;
    src = (const uint8_t *)[readback_buffer contents];
    if (!src)
        return 0;

    if (pixel_format == MTLPixelFormatRGBA16Float) {
        if (bytes_per_row > (size_t)INT32_MAX)
            return 0;
        vgfx3d_copy_linear_rgba16f_to_rgba8(
            dst_rgba, stride, copy_w, copy_h, (const uint16_t *)src, (int32_t)bytes_per_row);
        if (dst_hdr_rgba) {
            vgfx3d_copy_linear_rgba16f_to_rgba32f(
                dst_hdr_rgba, w * 4, copy_w, copy_h, (const uint16_t *)src, (int32_t)bytes_per_row);
        }
        return 1;
    }

    for (int32_t y = 0; y < copy_h; y++) {
        uint8_t *dst_row = dst_rgba + (size_t)y * (size_t)stride;
        const uint8_t *src_row = src + (size_t)y * bytes_per_row;
        for (int32_t x = 0; x < copy_w; x++) {
            dst_row[(size_t)x * 4u + 0u] = src_row[(size_t)x * 4u + 2u];
            dst_row[(size_t)x * 4u + 1u] = src_row[(size_t)x * 4u + 1u];
            dst_row[(size_t)x * 4u + 2u] = src_row[(size_t)x * 4u + 0u];
            dst_row[(size_t)x * 4u + 3u] = src_row[(size_t)x * 4u + 3u];
        }
    }
    return 1;
}

/// @brief Sync the CAMetalLayer's drawable size and scale to the current backing view.
static void metal_update_layer_size(VGFXMetalContext *ctx) {
    if (!ctx)
        return;
    if (ctx.metalLayer) {
        NSView *view = (__bridge NSView *)vgfx_get_native_view(ctx.vgfxWin);
        if (view) {
            ctx.metalLayer.frame = view.bounds;
            ctx.metalLayer.drawableSize = CGSizeMake((CGFloat)(ctx.width > 0 ? ctx.width : 1),
                                                     (CGFloat)(ctx.height > 0 ? ctx.height : 1));
        }
    }
}

/// @brief End the active scene render-command encoder if one is open.
static void metal_finish_encoding(VGFXMetalContext *ctx) {
    if (!ctx)
        return;
    if (ctx.encoder) {
        [ctx.encoder endEncoding];
        ctx.encoder = nil;
    }
}

/// @brief Commit the pending command buffer, optionally blocking until the GPU finishes it.
static void metal_commit_pending(VGFXMetalContext *ctx, BOOL waitUntilCompleted) {
    if (!ctx)
        return;
    metal_finish_encoding(ctx);
    if (ctx.cmdBuf) {
        id<MTLCommandBuffer> cmdBuf = ctx.cmdBuf;
        NSMutableArray *retainedFrameBuffers = ctx.frameBuffers;
        id<CAMetalDrawable> retainedDrawable = ctx.drawable;
        if (!waitUntilCompleted) {
            [cmdBuf addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
              (void)buffer;
              (void)retainedFrameBuffers;
              (void)retainedDrawable;
            }];
        }
        [cmdBuf commit];
        if (waitUntilCompleted)
            [cmdBuf waitUntilCompleted];
        ctx.cmdBuf = nil;
        ctx.frameBuffers = nil;
        ctx.drawable = nil;
        ctx.postfxCompositedToDrawable = 0;
    }
}

/// @brief Detach the active offscreen render target, optionally syncing its color back to Pixels
/// first.
static void metal_detach_render_target(VGFXMetalContext *ctx, BOOL syncColor) {
    vgfx3d_rendertarget_t *target;
    BOOL hadPendingFrame;
    if (!ctx)
        return;
    target = ctx.rttTarget;
    hadPendingFrame = ctx.inFrame || ctx.encoder || ctx.cmdBuf;
    if (hadPendingFrame) {
        metal_commit_pending(ctx, syncColor);
        ctx.inFrame = NO;
        if (target && syncColor)
            target->color_dirty = 1;
    }
    if (target) {
        if (syncColor && target->color_dirty)
            (void)metal_sync_render_target_color((__bridge void *)ctx, target);
        vgfx3d_rendertarget_clear_sync(target);
    }
    ctx.rttActive = NO;
    ctx.rttColorTexture = nil;
    ctx.rttMotionTexture = nil;
    ctx.rttDepthTexture = nil;
    ctx.rttTarget = NULL;
    ctx.displayTexture = nil;
    ctx.postfxEncodedThisFrame = 0;
    ctx.postfxCompositedToDrawable = 0;
}

/// @brief Blit a finished texture into the window's CPU framebuffer (software-present fallback
/// path).
static void metal_present_texture_to_framebuffer(VGFXMetalContext *ctx, id<MTLTexture> texture) {
    vgfx_framebuffer_t fb;
    if (!ctx || !texture || !ctx.vgfxWin)
        return;
    if (!vgfx_get_framebuffer(ctx.vgfxWin, &fb))
        return;
    (void)metal_copy_texture_to_rgba(ctx, texture, fb.pixels, fb.width, fb.height, fb.stride, NULL);
    ctx.displayTexture = texture;
}

/// @brief Present a finished texture to the layer's next drawable (or framebuffer fallback).
/// @return YES if a drawable was presented, NO if it fell back / had no drawable.
static BOOL metal_present_texture(VGFXMetalContext *ctx,
                                  id<MTLTexture> texture,
                                  BOOL schedulePresent) {
    NSUInteger copy_w;
    NSUInteger copy_h;
    id<CAMetalDrawable> drawable;
    id<MTLBlitCommandEncoder> blit;

    if (!ctx || !texture || ctx.rttActive)
        return NO;
    if (!ctx.cmdBuf) {
        ctx.cmdBuf = [ctx.commandQueue commandBuffer];
        if (!ctx.cmdBuf)
            return NO;
        if (!ctx.frameBuffers)
            ctx.frameBuffers = [NSMutableArray arrayWithCapacity:4];
    }
    metal_update_layer_size(ctx);
    if (!ctx.metalLayer)
        return NO;
    drawable = [ctx.metalLayer nextDrawable];
    if (!drawable)
        return NO;

    copy_w = texture.width < drawable.texture.width ? texture.width : drawable.texture.width;
    copy_h = texture.height < drawable.texture.height ? texture.height : drawable.texture.height;
    if (copy_w == 0 || copy_h == 0)
        return NO;

    ctx.drawable = drawable;
    blit = [ctx.cmdBuf blitCommandEncoder];
    if (!blit)
        return NO;
    [blit copyFromTexture:texture
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(copy_w, copy_h, 1)
                toTexture:drawable.texture
         destinationSlice:0
         destinationLevel:0
        destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blit endEncoding];
    if (schedulePresent)
        [ctx.cmdBuf presentDrawable:drawable];
    ctx.displayTexture = texture;
    return YES;
}

/// @brief Allocate a shared-storage MTLBuffer initialized from @p bytes (CPU/GPU-visible).
static id<MTLBuffer> metal_new_shared_buffer(VGFXMetalContext *ctx,
                                             const void *bytes,
                                             size_t length) {
    if (!ctx || !bytes || length == 0)
        return nil;
    return [ctx.device newBufferWithBytes:bytes length:length options:MTLResourceStorageModeShared];
}

/// @brief Allocate an uninitialized shared-storage MTLBuffer of @p length bytes.
static id<MTLBuffer> metal_new_shared_buffer_with_length(VGFXMetalContext *ctx, size_t length) {
    if (!ctx || !ctx.device || length == 0)
        return nil;
    return [ctx.device newBufferWithLength:length options:MTLResourceStorageModeShared];
}

/// @brief Run the texture/cubemap/morph cache pruning passes once per frame.
static void metal_cache_evict_if_needed(VGFXMetalContext *ctx) {
    NSValue *oldestKey = nil;
    VGFXMetalGeometryCacheEntry *oldestEntry = nil;

    if (!ctx || !ctx.geometryCache || ctx.geometryCache.count < 128)
        return;
    for (NSValue *key in ctx.geometryCache) {
        VGFXMetalGeometryCacheEntry *entry = ctx.geometryCache[key];
        if (!oldestEntry || entry.lastUsedFrame < oldestEntry.lastUsedFrame) {
            oldestEntry = entry;
            oldestKey = key;
        }
    }
    if (oldestKey)
        [ctx.geometryCache removeObjectForKey:oldestKey];
}

/// @brief Get (growing as needed) the per-frame vertex and index buffers for a draw of given size.
static void metal_get_geometry_buffers(VGFXMetalContext *ctx,
                                       const vgfx3d_draw_cmd_t *cmd,
                                       id<MTLBuffer> *outVB,
                                       id<MTLBuffer> *outIB) {
    if (!outVB || !outIB) {
        return;
    }
    *outVB = nil;
    *outIB = nil;
    if (!ctx || !cmd || !cmd->vertices || !cmd->indices || cmd->vertex_count == 0 ||
        cmd->index_count == 0)
        return;
    if ((size_t)cmd->vertex_count > SIZE_MAX / sizeof(vgfx3d_vertex_t) ||
        (size_t)cmd->index_count > SIZE_MAX / sizeof(uint32_t))
        return;

    if (cmd->geometry_key && cmd->geometry_revision != 0) {
        NSValue *key = [NSValue valueWithPointer:cmd->geometry_key];
        VGFXMetalGeometryCacheEntry *entry = ctx.geometryCache[key];
        if (!entry || entry.revision != cmd->geometry_revision ||
            entry.vertexCount != cmd->vertex_count || entry.indexCount != cmd->index_count ||
            !entry.vertexBuffer || !entry.indexBuffer) {
            VGFXMetalGeometryCacheEntry *new_entry;
            metal_cache_evict_if_needed(ctx);
            new_entry = [[VGFXMetalGeometryCacheEntry alloc] init];
            new_entry.vertexBuffer = metal_new_shared_buffer(
                ctx, cmd->vertices, (size_t)cmd->vertex_count * sizeof(vgfx3d_vertex_t));
            new_entry.indexBuffer = metal_new_shared_buffer(
                ctx, cmd->indices, (size_t)cmd->index_count * sizeof(uint32_t));
            if (!new_entry.vertexBuffer || !new_entry.indexBuffer)
                return;
            new_entry.revision = cmd->geometry_revision;
            new_entry.vertexCount = cmd->vertex_count;
            new_entry.indexCount = cmd->index_count;
            ctx.geometryCache[key] = new_entry;
            entry = new_entry;
        }
        entry.lastUsedFrame = ctx.frameSerial;
        *outVB = entry.vertexBuffer;
        *outIB = entry.indexBuffer;
        return;
    }

    *outVB = metal_new_shared_buffer(
        ctx, cmd->vertices, (size_t)cmd->vertex_count * sizeof(vgfx3d_vertex_t));
    *outIB =
        metal_new_shared_buffer(ctx, cmd->indices, (size_t)cmd->index_count * sizeof(uint32_t));
}

/// @brief The texture that holds the current frame's final image for readback/present.
/// @details Prefers the post-FX output when post-processing ran, else the main scene color texture.
static id<MTLTexture> metal_active_readback_texture(VGFXMetalContext *ctx) {
    if (!ctx)
        return nil;
    if (ctx.rttActive && ctx.rttColorTexture)
        return ctx.rttColorTexture;
    if (ctx.displayTexture)
        return ctx.displayTexture;
    if (ctx.gpuPostfxEnabled && ctx.postfxEncodedThisFrame && ctx.postfxColorTexture)
        return ctx.postfxColorTexture;
    if (ctx.currentTargetKind == VGFX3D_METAL_TARGET_SWAPCHAIN && ctx.drawable)
        return ctx.drawable.texture;
    return ctx.offscreenColor;
}

/// @brief Reset the cached GPU post-processing chain snapshot to empty (without freeing buffers).
static void metal_reset_gpu_postfx_chain(VGFXMetalContext *ctx) {
    vgfx3d_postfx_chain_t chain;
    if (!ctx)
        return;
    chain = ctx.gpuPostfxChain;
    vgfx3d_postfx_chain_reset(&chain);
    ctx.gpuPostfxChain = chain;
    ctx.gpuPostfxChainValid = 0;
}

/// @brief Free the cached GPU post-processing chain snapshot and release its owned memory.
static void metal_free_gpu_postfx_chain(VGFXMetalContext *ctx) {
    vgfx3d_postfx_chain_t chain;
    if (!ctx)
        return;
    chain = ctx.gpuPostfxChain;
    vgfx3d_postfx_chain_free(&chain);
    ctx.gpuPostfxChain = chain;
    ctx.gpuPostfxChainValid = 0;
}

/// @brief Deep-copy a post-FX chain snapshot into the context for GPU post-processing.
/// @details Owns the copy so the chain can be applied across frames independent of the caller's
/// data.
/// @return 1 on success, 0 on allocation failure.
static int metal_copy_gpu_postfx_chain(VGFXMetalContext *ctx, const vgfx3d_postfx_chain_t *src) {
    vgfx3d_postfx_chain_t chain;
    int ok;
    if (!ctx)
        return 0;
    chain = ctx.gpuPostfxChain;
    ok = vgfx3d_postfx_chain_copy(&chain, src);
    ctx.gpuPostfxChain = chain;
    ctx.gpuPostfxChainValid = ok ? 1 : 0;
    return ok;
}

//=============================================================================
// Vertex descriptor (92-byte vgfx3d_vertex_t)
//=============================================================================

/// @brief Build the MTLVertexDescriptor describing the interleaved 92-byte vgfx3d_vertex_t layout
///   (a single per-vertex buffer at index 0) that every render pipeline consumes.
/// @return A newly-allocated descriptor owned by the caller.
static MTLVertexDescriptor *create_vertex_descriptor(void) {
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
    d.attributes[7].format = MTLVertexFormatFloat2;
    d.attributes[7].offset = 32;
    d.attributes[7].bufferIndex = 0;
    d.attributes[3].format = MTLVertexFormatFloat4;
    d.attributes[3].offset = 40;
    d.attributes[3].bufferIndex = 0;
    d.attributes[4].format = MTLVertexFormatFloat4;
    d.attributes[4].offset = 56;
    d.attributes[4].bufferIndex = 0;
    d.attributes[5].format = MTLVertexFormatUChar4;
    d.attributes[5].offset = 72;
    d.attributes[5].bufferIndex = 0;
    d.attributes[6].format = MTLVertexFormatFloat4;
    d.attributes[6].offset = 76;
    d.attributes[6].bufferIndex = 0;
    d.layouts[0].stride = 92;
    d.layouts[0].stepRate = 1;
    d.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
    return d;
}

/// @brief Configure a pipeline color attachment's blend state for a material alpha mode.
/// @details Sets the blend factors/equation for opaque, alpha-blend, or additive modes (and enables
///          or disables blending accordingly).
static void metal_configure_blend_state(MTLRenderPipelineColorAttachmentDescriptor *attachment,
                                        vgfx3d_metal_blend_mode_t blend_mode) {
    if (!attachment)
        return;
    attachment.blendingEnabled = blend_mode != VGFX3D_METAL_BLEND_OPAQUE;
    if (blend_mode == VGFX3D_METAL_BLEND_ADDITIVE) {
        attachment.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        attachment.destinationRGBBlendFactor = MTLBlendFactorOne;
        attachment.sourceAlphaBlendFactor = MTLBlendFactorOne;
        attachment.destinationAlphaBlendFactor = MTLBlendFactorOne;
        return;
    }
    attachment.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    attachment.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    attachment.sourceAlphaBlendFactor = MTLBlendFactorOne;
    attachment.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
}

/// @brief Compile a render pipeline state for a given vertex/fragment function and blend/format
/// config.
/// @return The pipeline state, or nil on compilation failure.
static id<MTLRenderPipelineState> metal_create_pipeline_state(
    id<MTLDevice> device,
    id<MTLFunction> vertex_function,
    id<MTLFunction> fragment_function,
    MTLVertexDescriptor *vertex_descriptor,
    MTLPixelFormat color0_format,
    vgfx3d_metal_blend_mode_t blend_mode,
    BOOL disable_motion_writes,
    NSError **error) {
    MTLRenderPipelineDescriptor *descriptor;

    if (!device || !vertex_function || !fragment_function)
        return nil;

    descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    descriptor.vertexFunction = vertex_function;
    descriptor.fragmentFunction = fragment_function;
    descriptor.vertexDescriptor = vertex_descriptor;
    descriptor.colorAttachments[0].pixelFormat = color0_format;
    descriptor.colorAttachments[1].pixelFormat = MTLPixelFormatBGRA8Unorm;
    descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    metal_configure_blend_state(descriptor.colorAttachments[0], blend_mode);
    descriptor.colorAttachments[1].writeMask =
        disable_motion_writes ? MTLColorWriteMaskNone : MTLColorWriteMaskAll;
    return [device newRenderPipelineStateWithDescriptor:descriptor error:error];
}

/// @brief Compile the skybox render pipeline state (full-screen triangle sampling a cubemap).
static id<MTLRenderPipelineState> metal_create_skybox_pipeline_state(
    id<MTLDevice> device,
    id<MTLFunction> vertex_function,
    id<MTLFunction> fragment_function,
    MTLPixelFormat color0_format,
    NSError **error) {
    MTLRenderPipelineDescriptor *descriptor;

    if (!device || !vertex_function || !fragment_function)
        return nil;

    descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    descriptor.vertexFunction = vertex_function;
    descriptor.fragmentFunction = fragment_function;
    descriptor.vertexDescriptor = nil;
    descriptor.colorAttachments[0].pixelFormat = color0_format;
    descriptor.colorAttachments[1].pixelFormat = MTLPixelFormatBGRA8Unorm;
    descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    descriptor.colorAttachments[1].writeMask = MTLColorWriteMaskNone;
    return [device newRenderPipelineStateWithDescriptor:descriptor error:error];
}

/// @brief Get or build the cached pipeline state matching a draw's shading model, blend, and
/// formats.
/// @details Keyed by the render-state config so identical draws reuse one compiled pipeline.
static id<MTLRenderPipelineState> metal_select_pipeline_state(VGFXMetalContext *ctx,
                                                              const vgfx3d_draw_cmd_t *cmd,
                                                              BOOL instanced) {
    vgfx3d_metal_blend_mode_t blend_mode;

    if (!ctx)
        return nil;
    blend_mode = vgfx3d_metal_choose_blend_mode(cmd);
    if (ctx.currentTargetKind == VGFX3D_METAL_TARGET_SCENE) {
        if (instanced)
            return blend_mode == VGFX3D_METAL_BLEND_ALPHA
                       ? ctx.instancedPipelineStateAlpha
                       : (blend_mode == VGFX3D_METAL_BLEND_ADDITIVE
                              ? ctx.instancedPipelineStateAdditive
                              : ctx.instancedPipelineState);
        return blend_mode == VGFX3D_METAL_BLEND_ALPHA
                   ? ctx.pipelineStateAlpha
                   : (blend_mode == VGFX3D_METAL_BLEND_ADDITIVE ? ctx.pipelineStateAdditive
                                                                : ctx.pipelineState);
    }

    if (instanced)
        return blend_mode == VGFX3D_METAL_BLEND_ALPHA
                   ? ctx.instancedPipelineStateColorOnlyAlpha
                   : (blend_mode == VGFX3D_METAL_BLEND_ADDITIVE
                          ? ctx.instancedPipelineStateColorOnlyAdditive
                          : ctx.instancedPipelineStateColorOnly);
    return blend_mode == VGFX3D_METAL_BLEND_ALPHA
               ? ctx.pipelineStateColorOnlyAlpha
               : (blend_mode == VGFX3D_METAL_BLEND_ADDITIVE ? ctx.pipelineStateColorOnlyAdditive
                                                            : ctx.pipelineStateColorOnly);
}

/// @brief Map a material texture-wrap mode to the corresponding MTLSamplerAddressMode.
static MTLSamplerAddressMode metal_material_address_mode(int32_t mode) {
    if (mode == RT_MATERIAL3D_TEXTURE_WRAP_CLAMP_TO_EDGE)
        return MTLSamplerAddressModeClampToEdge;
    if (mode == RT_MATERIAL3D_TEXTURE_WRAP_MIRRORED_REPEAT)
        return MTLSamplerAddressModeMirrorRepeat;
    return MTLSamplerAddressModeRepeat;
}

/// @brief Get or build the cached sampler state for a material's filter and wrap modes.
static id<MTLSamplerState> metal_get_material_sampler(VGFXMetalContext *ctx,
                                                      const vgfx3d_draw_cmd_t *cmd,
                                                      int32_t slot) {
    int use_slot = cmd && slot >= 0 && slot < RT_MATERIAL3D_TEXTURE_SLOT_COUNT;
    if (!ctx)
        return nil;
    if (!cmd || !ctx.samplerCache)
        return ctx.sharedSampler ? ctx.sharedSampler : ctx.defaultSampler;

    int32_t wrap_s = use_slot ? cmd->texture_slot_wrap_s[slot] : cmd->texture_wrap_s;
    int32_t wrap_t = use_slot ? cmd->texture_slot_wrap_t[slot] : cmd->texture_wrap_t;
    int32_t filter = (use_slot ? cmd->texture_slot_filter[slot] : cmd->texture_filter) ==
                             RT_MATERIAL3D_TEXTURE_FILTER_NEAREST
                         ? RT_MATERIAL3D_TEXTURE_FILTER_NEAREST
                         : RT_MATERIAL3D_TEXTURE_FILTER_LINEAR;
    NSString *key = [NSString stringWithFormat:@ "%d:%d:%d", wrap_s, wrap_t, filter];
    id<MTLSamplerState> sampler = ctx.samplerCache[key];
    if (sampler)
        return sampler;

    MTLSamplerDescriptor *sd = [[MTLSamplerDescriptor alloc] init];
    sd.minFilter = filter == RT_MATERIAL3D_TEXTURE_FILTER_NEAREST ? MTLSamplerMinMagFilterNearest
                                                                  : MTLSamplerMinMagFilterLinear;
    sd.magFilter = sd.minFilter;
    sd.mipFilter = filter == RT_MATERIAL3D_TEXTURE_FILTER_NEAREST ? MTLSamplerMipFilterNearest
                                                                  : MTLSamplerMipFilterLinear;
    sd.sAddressMode = metal_material_address_mode(wrap_s);
    sd.tAddressMode = metal_material_address_mode(wrap_t);
    sampler = [ctx.device newSamplerStateWithDescriptor:sd];
    if (sampler)
        ctx.samplerCache[key] = sampler;
    return sampler ? sampler : (ctx.sharedSampler ? ctx.sharedSampler : ctx.defaultSampler);
}

//=============================================================================
// Backend vtable
//=============================================================================

typedef struct metal_main_functions_t {
    id<MTLFunction> vertex;
    id<MTLFunction> vertexInstanced;
    id<MTLFunction> vertexShadow;
    id<MTLFunction> fragmentShadow;
    id<MTLFunction> fragment;
} metal_main_functions_t;

/// @brief Allocate the Objective-C context and initialize cache/default fields.
static VGFXMetalContext *metal_create_context_base(id<MTLDevice> device,
                                                   vgfx_window_t win,
                                                   int32_t w,
                                                   int32_t h) {
    VGFXMetalContext *ctx = [[VGFXMetalContext alloc] init];
    ctx.device = device;
    ctx.width = w;
    ctx.height = h;
    ctx.vgfxWin = win;
    ctx.textureCache = [NSMutableDictionary dictionaryWithCapacity:32];
    ctx.cubemapCache = [NSMutableDictionary dictionaryWithCapacity:8];
    ctx.geometryCache = [NSMutableDictionary dictionaryWithCapacity:32];
    ctx.morphCache = [NSMutableDictionary dictionaryWithCapacity:16];
    ctx.renderTargetCache = [NSMutableDictionary dictionaryWithCapacity:8];
    ctx.samplerCache = [NSMutableDictionary dictionaryWithCapacity:8];
    ctx.currentTargetKind = VGFX3D_METAL_TARGET_SWAPCHAIN;
    ctx.textureUploadBudgetBytes = UINT64_MAX;
    return ctx;
}

/// @brief Attach a CAMetalLayer to the native view and sync its drawable size.
static void metal_attach_layer_to_view(VGFXMetalContext *ctx, NSView *view, id<MTLDevice> device) {
    view.wantsLayer = YES;
    if (!view.layer)
        view.layer = [CALayer layer];
    ctx.metalLayer = [CAMetalLayer layer];
    ctx.metalLayer.device = device;
    ctx.metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    ctx.metalLayer.framebufferOnly = NO;
    ctx.metalLayer.hidden = YES;
    ctx.metalLayer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
    [view.layer addSublayer:ctx.metalLayer];
    metal_update_layer_size(ctx);
}

/// @brief Create the command queue and reset frame matrices/state.
static BOOL metal_create_command_queue_and_defaults(VGFXMetalContext *ctx, id<MTLDevice> device) {
    ctx.commandQueue = [device newCommandQueue];
    if (!ctx.commandQueue) {
        NSLog(@ "[Metal] newCommandQueue returned nil");
        return NO;
    }
    mat4f_identity(ctx->_view);
    mat4f_identity(ctx->_projection);
    mat4f_identity(ctx->_vp);
    mat4f_identity(ctx->_prevVP);
    mat4f_identity(ctx->_invVP);
    ctx->_prevVPValid = NO;
    return YES;
}

/// @brief Compile the main in-memory Metal shader library.
static BOOL metal_compile_main_library(VGFXMetalContext *ctx, id<MTLDevice> device) {
    NSError *error = nil;
    ctx.library = [device newLibraryWithSource:metal_shader_source options:nil error:&error];
    if (!ctx.library) {
        NSLog(@ "[Metal] Shader error: %@", error);
        return NO;
    }
    return YES;
}

/// @brief Resolve the required main/shadow shader entry points from the main library.
static BOOL metal_load_main_functions(VGFXMetalContext *ctx, metal_main_functions_t *funcs) {
    funcs->vertex = nil;
    funcs->vertexInstanced = nil;
    funcs->vertexShadow = nil;
    funcs->fragmentShadow = nil;
    funcs->fragment = nil;
    funcs->vertex = [ctx.library newFunctionWithName:@ "vertex_main"];
    funcs->vertexInstanced = [ctx.library newFunctionWithName:@ "vertex_main_instanced"];
    funcs->vertexShadow = [ctx.library newFunctionWithName:@ "vertex_shadow"];
    funcs->fragmentShadow = [ctx.library newFunctionWithName:@ "fragment_shadow"];
    funcs->fragment = [ctx.library newFunctionWithName:@ "fragment_main"];
    if (!funcs->vertex || !funcs->vertexInstanced || !funcs->vertexShadow ||
        !funcs->fragmentShadow || !funcs->fragment) {
        NSLog(@ "[Metal] required shader entrypoints missing (vf=%@ inst=%@ shadow=%@ shadowf=%@ "
               "ff=%@)",
              funcs->vertex,
              funcs->vertexInstanced,
              funcs->vertexShadow,
              funcs->fragmentShadow,
              funcs->fragment);
        return NO;
    }
    return YES;
}

/// @brief Build all main and instanced pipeline-state variants.
static BOOL metal_create_main_pipeline_states(VGFXMetalContext *ctx,
                                             id<MTLDevice> device,
                                             const metal_main_functions_t *funcs) {
    NSError *error = nil;
    ctx.pipelineState = metal_create_pipeline_state(device,
                                                    funcs->vertex,
                                                    funcs->fragment,
                                                    create_vertex_descriptor(),
                                                    MTLPixelFormatRGBA16Float,
                                                    VGFX3D_METAL_BLEND_OPAQUE,
                                                    NO,
                                                    &error);
    ctx.pipelineStateAlpha = metal_create_pipeline_state(device,
                                                         funcs->vertex,
                                                         funcs->fragment,
                                                         create_vertex_descriptor(),
                                                         MTLPixelFormatRGBA16Float,
                                                         VGFX3D_METAL_BLEND_ALPHA,
                                                         YES,
                                                         &error);
    ctx.pipelineStateAdditive = metal_create_pipeline_state(device,
                                                            funcs->vertex,
                                                            funcs->fragment,
                                                            create_vertex_descriptor(),
                                                            MTLPixelFormatRGBA16Float,
                                                            VGFX3D_METAL_BLEND_ADDITIVE,
                                                            YES,
                                                            &error);
    ctx.pipelineStateColorOnly = metal_create_pipeline_state(device,
                                                             funcs->vertex,
                                                             funcs->fragment,
                                                             create_vertex_descriptor(),
                                                             MTLPixelFormatBGRA8Unorm,
                                                             VGFX3D_METAL_BLEND_OPAQUE,
                                                             NO,
                                                             &error);
    ctx.pipelineStateColorOnlyAlpha = metal_create_pipeline_state(device,
                                                                  funcs->vertex,
                                                                  funcs->fragment,
                                                                  create_vertex_descriptor(),
                                                                  MTLPixelFormatBGRA8Unorm,
                                                                  VGFX3D_METAL_BLEND_ALPHA,
                                                                  YES,
                                                                  &error);
    ctx.pipelineStateColorOnlyAdditive =
        metal_create_pipeline_state(device,
                                    funcs->vertex,
                                    funcs->fragment,
                                    create_vertex_descriptor(),
                                    MTLPixelFormatBGRA8Unorm,
                                    VGFX3D_METAL_BLEND_ADDITIVE,
                                    YES,
                                    &error);
    ctx.instancedPipelineState = metal_create_pipeline_state(device,
                                                             funcs->vertexInstanced,
                                                             funcs->fragment,
                                                             create_vertex_descriptor(),
                                                             MTLPixelFormatRGBA16Float,
                                                             VGFX3D_METAL_BLEND_OPAQUE,
                                                             NO,
                                                             &error);
    ctx.instancedPipelineStateAlpha = metal_create_pipeline_state(device,
                                                                  funcs->vertexInstanced,
                                                                  funcs->fragment,
                                                                  create_vertex_descriptor(),
                                                                  MTLPixelFormatRGBA16Float,
                                                                  VGFX3D_METAL_BLEND_ALPHA,
                                                                  YES,
                                                                  &error);
    ctx.instancedPipelineStateAdditive =
        metal_create_pipeline_state(device,
                                    funcs->vertexInstanced,
                                    funcs->fragment,
                                    create_vertex_descriptor(),
                                    MTLPixelFormatRGBA16Float,
                                    VGFX3D_METAL_BLEND_ADDITIVE,
                                    YES,
                                    &error);
    ctx.instancedPipelineStateColorOnly =
        metal_create_pipeline_state(device,
                                    funcs->vertexInstanced,
                                    funcs->fragment,
                                    create_vertex_descriptor(),
                                    MTLPixelFormatBGRA8Unorm,
                                    VGFX3D_METAL_BLEND_OPAQUE,
                                    NO,
                                    &error);
    ctx.instancedPipelineStateColorOnlyAlpha =
        metal_create_pipeline_state(device,
                                    funcs->vertexInstanced,
                                    funcs->fragment,
                                    create_vertex_descriptor(),
                                    MTLPixelFormatBGRA8Unorm,
                                    VGFX3D_METAL_BLEND_ALPHA,
                                    YES,
                                    &error);
    ctx.instancedPipelineStateColorOnlyAdditive =
        metal_create_pipeline_state(device,
                                    funcs->vertexInstanced,
                                    funcs->fragment,
                                    create_vertex_descriptor(),
                                    MTLPixelFormatBGRA8Unorm,
                                    VGFX3D_METAL_BLEND_ADDITIVE,
                                    YES,
                                    &error);
    if (!ctx.pipelineState || !ctx.pipelineStateAlpha || !ctx.pipelineStateAdditive ||
        !ctx.pipelineStateColorOnly || !ctx.pipelineStateColorOnlyAlpha ||
        !ctx.pipelineStateColorOnlyAdditive || !ctx.instancedPipelineState ||
        !ctx.instancedPipelineStateAlpha || !ctx.instancedPipelineStateAdditive ||
        !ctx.instancedPipelineStateColorOnly || !ctx.instancedPipelineStateColorOnlyAlpha ||
        !ctx.instancedPipelineStateColorOnlyAdditive) {
        NSLog(@ "Metal pipeline error: %@", error);
        return NO;
    }
    return YES;
}

/// @brief Create depth-stencil state variants used by opaque, transparent, disabled, and skybox draws.
static void metal_create_depth_state_variants(VGFXMetalContext *ctx, id<MTLDevice> device) {
    MTLDepthStencilDescriptor *dd = [[MTLDepthStencilDescriptor alloc] init];
    dd.depthCompareFunction = MTLCompareFunctionLess;
    dd.depthWriteEnabled = YES;
    ctx.depthState = [device newDepthStencilStateWithDescriptor:dd];
    dd.depthWriteEnabled = NO;
    ctx.depthStateNoWrite = [device newDepthStencilStateWithDescriptor:dd];
    dd.depthCompareFunction = MTLCompareFunctionAlways;
    ctx.depthStateDisabled = [device newDepthStencilStateWithDescriptor:dd];
    dd.depthCompareFunction = MTLCompareFunctionLessEqual;
    ctx.skyboxDepthState = [device newDepthStencilStateWithDescriptor:dd];
}

/// @brief Create default white 2D/cubemap textures plus the default sampler.
static void metal_create_default_texture_resources(VGFXMetalContext *ctx, id<MTLDevice> device) {
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
    sd.mipFilter = MTLSamplerMipFilterLinear;
    ctx.defaultSampler = [device newSamplerStateWithDescriptor:sd];

    MTLTextureDescriptor *cubeDesc =
        [MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                              size:1
                                                         mipmapped:NO];
    cubeDesc.usage = MTLTextureUsageShaderRead;
    cubeDesc.storageMode = MTLStorageModeShared;
    ctx.defaultCubemap = [device newTextureWithDescriptor:cubeDesc];
    for (NSUInteger face = 0; face < 6; face++) {
        uint32_t cube_white = 0xFFFFFFFF;
        [ctx.defaultCubemap replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
                              mipmapLevel:0
                                    slice:face
                                withBytes:&cube_white
                              bytesPerRow:4
                            bytesPerImage:4];
    }
}

/// @brief Create shared texture and cubemap samplers.
static void metal_create_shared_sampler_resources(VGFXMetalContext *ctx, id<MTLDevice> device) {
    MTLSamplerDescriptor *sd = [[MTLSamplerDescriptor alloc] init];
    sd.minFilter = MTLSamplerMinMagFilterLinear;
    sd.magFilter = MTLSamplerMinMagFilterLinear;
    sd.mipFilter = MTLSamplerMipFilterLinear;
    sd.sAddressMode = MTLSamplerAddressModeRepeat;
    sd.tAddressMode = MTLSamplerAddressModeRepeat;
    ctx.sharedSampler = [device newSamplerStateWithDescriptor:sd];

    MTLSamplerDescriptor *cubeSd = [[MTLSamplerDescriptor alloc] init];
    cubeSd.minFilter = MTLSamplerMinMagFilterLinear;
    cubeSd.magFilter = MTLSamplerMinMagFilterLinear;
    cubeSd.mipFilter = MTLSamplerMipFilterLinear;
    cubeSd.sAddressMode = MTLSamplerAddressModeClampToEdge;
    cubeSd.tAddressMode = MTLSamplerAddressModeClampToEdge;
    cubeSd.rAddressMode = MTLSamplerAddressModeClampToEdge;
    ctx.cubeSampler = [device newSamplerStateWithDescriptor:cubeSd];
}

/// @brief Create shadow pipeline, comparison sampler, and shadow depth state.
static void metal_create_shadow_resources(VGFXMetalContext *ctx,
                                          id<MTLDevice> device,
                                          const metal_main_functions_t *funcs) {
    MTLRenderPipelineDescriptor *spd = [[MTLRenderPipelineDescriptor alloc] init];
    spd.vertexFunction = funcs->vertexShadow;
    spd.fragmentFunction = funcs->fragmentShadow;
    spd.vertexDescriptor = create_vertex_descriptor();
    spd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    spd.colorAttachments[0].pixelFormat = MTLPixelFormatInvalid;
    NSError *shadowErr = nil;
    ctx.shadowPipeline = [device newRenderPipelineStateWithDescriptor:spd error:&shadowErr];

    MTLSamplerDescriptor *csd = [[MTLSamplerDescriptor alloc] init];
    csd.compareFunction = MTLCompareFunctionLessEqual;
    csd.minFilter = MTLSamplerMinMagFilterLinear;
    csd.magFilter = MTLSamplerMinMagFilterLinear;
    ctx.shadowSampler = [device newSamplerStateWithDescriptor:csd];

    MTLDepthStencilDescriptor *sdd = [[MTLDepthStencilDescriptor alloc] init];
    sdd.depthCompareFunction = MTLCompareFunctionLess;
    sdd.depthWriteEnabled = YES;
    ctx.shadowDepthState = [device newDepthStencilStateWithDescriptor:sdd];
}

/// @brief Backend vtable entry: create the Metal rendering context for window @p win sized @p w×@p h —
///   acquires the system default MTLDevice, attaches a CAMetalLayer to the native NSView, and
///   initializes the per-context resource caches (texture/cubemap/geometry/morph/render-target/sampler).
/// @return An opaque VGFXMetalContext* handle (cast to void*), or NULL if no Metal device or native
///   view is available.
static void *metal_create_ctx(vgfx_window_t win, int32_t w, int32_t h) {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            NSLog(@ "[Metal] MTLCreateSystemDefaultDevice returned nil");
            return NULL;
        }

        NSView *view = (__bridge NSView *)vgfx_get_native_view(win);
        if (!view) {
            NSLog(@ "[Metal] vgfx_get_native_view returned nil");
            return NULL;
        }

        VGFXMetalContext *ctx = metal_create_context_base(device, win, w, h);
        metal_main_functions_t funcs;
        metal_attach_layer_to_view(ctx, view, device);
        if (!metal_create_command_queue_and_defaults(ctx, device) ||
            !metal_compile_main_library(ctx, device) || !metal_load_main_functions(ctx, &funcs) ||
            !metal_create_main_pipeline_states(ctx, device, &funcs))
            return NULL;
        metal_create_depth_state_variants(ctx, device);
        metal_create_default_texture_resources(ctx, device);
        metal_create_shared_sampler_resources(ctx, device);
        metal_create_shadow_resources(ctx, device, &funcs);

        /* MTL-11: Post-processing fullscreen quad shader + pipeline */
        {
            NSArray<NSString *> *postfxChunks = @[
                [NSString
                    stringWithUTF8String:
                        "#include <metal_stdlib>\n"
                        "using namespace metal;\n"
                        "struct FullscreenVert { float4 position [[position]]; float2 uv; };\n"
                        "struct PostFXParams {\n"
                        "    float4x4 invViewProjection;\n"
                        "    float4x4 prevViewProjection;\n"
                        "    float4 cameraPosition;\n"
                        "    float2 invResolution;\n"
                        "    int bloomEnabled;\n"
                        "    float bloomThreshold;\n"
                        "    float bloomStrength;\n"
                        "    int bloomPasses;\n"
                        "    int tonemapMode;\n"
                        "    float tonemapExposure;\n"
                        "    int fxaaEnabled;\n"
                        "    int colorGradeEnabled;\n"
                        "    float cgBright;\n"
                        "    float cgContrast;\n"
                        "    float cgSat;\n"
                        "    int vignetteEnabled;\n"
                        "    float vigRadius;\n"
                        "    float vigSoftness;\n"
                        "    int ssaoEnabled;\n"
                        "    float ssaoRadius;\n"
                        "    float ssaoIntensity;\n"
                        "    int ssaoSamples;\n"
                        "    int dofEnabled;\n"
                        "    float dofFocusDist;\n"
                        "    float dofAperture;\n"
                        "    float dofMaxBlur;\n"
                        "    int motionBlurEnabled;\n"
                        "    float motionBlurIntensity;\n"
                        "    int motionBlurSamples;\n"
                        "    int overlayEnabled;\n"
                        "};\n"
                        "vertex FullscreenVert fullscreen_vs(uint vid [[vertex_id]]) {\n"
                        "    float2 positions[4] = {float2(-1,-1), float2(1,-1), float2(-1,1), "
                        "float2(1,1)};\n"
                        "    float2 uvs[4] = {float2(0,1), float2(1,1), float2(0,0), "
                        "float2(1,0)};\n"
                        "    FullscreenVert out;\n"
                        "    out.position = float4(positions[vid], 0, 1);\n"
                        "    out.uv = uvs[vid];\n"
                        "    return out;\n"
                        "}\n"],
                [NSString
                    stringWithUTF8String:
                        "float3 sampleScene(texture2d<float> sceneTex, sampler s, float2 uv) { "
                        "return sceneTex.sample(s, uv).rgb; }\n"
                        "float sampleDepth(depth2d<float> depthTex, sampler s, float2 uv) { return "
                        "depthTex.sample(s, uv); }\n"
                        "float3 sampleMotion(texture2d<float> motionTex, sampler s, float2 uv) { "
                        "return motionTex.sample(s, uv).rgb; }\n"
                        "float3 reconstructWorld(constant PostFXParams &p, float2 uv, float depth) "
                        "{\n"
                        "    float4 clip = float4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);\n"
                        "    float4 world = p.invViewProjection * clip;\n"
                        "    return world.xyz / max(world.w, 0.0001);\n"
                        "}\n"
                        "float computeSsao(constant PostFXParams &p, depth2d<float> depthTex, "
                        "sampler s, float2 uv, float centerDepth) {\n"
                        "    float radius = max(p.ssaoRadius, 0.0001) * max(p.invResolution.x, "
                        "p.invResolution.y) * 120.0;\n"
                        "    float2 offsets[8] = {float2(1.0, 0.0), float2(-1.0, 0.0), float2(0.0, "
                        "1.0), float2(0.0, -1.0),\n"
                        "                         float2(0.707, 0.707), float2(-0.707, 0.707), "
                        "float2(0.707, -0.707), float2(-0.707, -0.707)};\n"
                        "    int count = clamp(p.ssaoSamples, 1, 8);\n"
                        "    float occ = 0.0;\n"
                        "    for (int i = 0; i < count; i++) {\n"
                        "        float sd = sampleDepth(depthTex, s, uv + offsets[i] * radius);\n"
                        "        occ += max(sd - centerDepth, 0.0);\n"
                        "    }\n"
                        "    return 1.0 - clamp((occ / float(count)) * p.ssaoIntensity * 32.0, "
                        "0.0, 1.0);\n"
                        "}\n"
                        "float2 cameraVelocity(constant PostFXParams &p, float2 uv, float3 "
                        "worldPos) {\n"
                        "    float4 prevClip = p.prevViewProjection * float4(worldPos, 1.0);\n"
                        "    float prevW = (prevClip.w < 0.0 ? -1.0 : 1.0) * max(fabs(prevClip.w), 0.0001);\n"
                        "    float2 prevUv = prevClip.xy / prevW * 0.5 + 0.5;\n"
                        "    return uv - prevUv;\n"
                        "}\n"],
                [NSString
                    stringWithUTF8String:
                        "float3 applyMotionBlur(constant PostFXParams &p,\n"
                        "                       texture2d<float> sceneTex,\n"
                        "                       texture2d<float> motionTex,\n"
                        "                       depth2d<float> depthTex,\n"
                        "                       sampler s,\n"
                        "                       float2 uv,\n"
                        "                       float3 color,\n"
                        "                       float3 worldPos) {\n"
                        "    float3 motionSample = sampleMotion(motionTex, s, uv);\n"
                        "    float2 velocity = motionSample.xy * 2.0 - 1.0;\n"
                        "    if (motionSample.z < 0.5)\n"
                        "        velocity = cameraVelocity(p, uv, worldPos);\n"
                        "    velocity *= p.motionBlurIntensity;\n"
                        "    if (length(velocity) < 0.0005)\n"
                        "        return color;\n"
                        "    int taps = clamp(p.motionBlurSamples, 2, 8);\n"
                        "    float3 acc = float3(0.0);\n"
                        "    float weight = 0.0;\n"
                        "    for (int i = 0; i < taps; i++) {\n"
                        "        float t = (float(i) / float(taps - 1)) - 0.5;\n"
                        "        acc += sampleScene(sceneTex, s, uv + velocity * t);\n"
                        "        weight += 1.0;\n"
                        "    }\n"
                        "    return acc / max(weight, 1.0);\n"
                        "}\n"
                        "float3 applyDof(constant PostFXParams &p,\n"
                        "                texture2d<float> sceneTex,\n"
                        "                sampler s,\n"
                        "                float2 uv,\n"
                        "                float3 color,\n"
                        "                float3 worldPos) {\n"
                        "    float dist = length(worldPos - p.cameraPosition.xyz);\n"
                        "    float blur = clamp(abs(dist - p.dofFocusDist) * max(p.dofAperture, "
                        "0.0) * 0.02, 0.0, p.dofMaxBlur);\n"
                        "    if (blur < 0.001)\n"
                        "        return color;\n"
                        "    float2 stepUv = p.invResolution * blur;\n"
                        "    float3 acc = color * 0.4;\n"
                        "    acc += sampleScene(sceneTex, s, uv + float2(stepUv.x, 0.0)) * 0.15;\n"
                        "    acc += sampleScene(sceneTex, s, uv - float2(stepUv.x, 0.0)) * 0.15;\n"
                        "    acc += sampleScene(sceneTex, s, uv + float2(0.0, stepUv.y)) * 0.15;\n"
                        "    acc += sampleScene(sceneTex, s, uv - float2(0.0, stepUv.y)) * 0.15;\n"
                        "    return acc;\n"
                        "}\n"],
                [NSString stringWithUTF8String:
                              "float3 applyFxaa(constant PostFXParams &p, texture2d<float> "
                              "sceneTex, sampler s, float2 uv, float3 color) {\n"
                              "    float3 lv = float3(0.299, 0.587, 0.114);\n"
                              "    float lumaM = dot(color, lv);\n"
                              "    float lumaN = dot(sampleScene(sceneTex, s, uv + float2(0.0, "
                              "-p.invResolution.y)), lv);\n"
                              "    float lumaS = dot(sampleScene(sceneTex, s, uv + float2(0.0, "
                              "p.invResolution.y)), lv);\n"
                              "    float lumaE = dot(sampleScene(sceneTex, s, uv + "
                              "float2(p.invResolution.x, 0.0)), lv);\n"
                              "    float lumaW = dot(sampleScene(sceneTex, s, uv + "
                              "float2(-p.invResolution.x, 0.0)), lv);\n"
                              "    float edge = fabs(lumaN + lumaS - 2.0 * lumaM) + fabs(lumaE + "
                              "lumaW - 2.0 * lumaM);\n"
                              "    if (edge < 0.08)\n"
                              "        return color;\n"
                              "    float3 avg = (sampleScene(sceneTex, s, uv + "
                              "float2(p.invResolution.x, 0.0)) +\n"
                              "                  sampleScene(sceneTex, s, uv + "
                              "float2(-p.invResolution.x, 0.0)) +\n"
                              "                  sampleScene(sceneTex, s, uv + float2(0.0, "
                              "p.invResolution.y)) +\n"
                              "                  sampleScene(sceneTex, s, uv + float2(0.0, "
                              "-p.invResolution.y))) * 0.25;\n"
                              "    return mix(color, avg, 0.5);\n"
                              "}\n"],
                [NSString stringWithUTF8String:
                              "fragment float4 postfx_fs(\n"
                              "    FullscreenVert in [[stage_in]],\n"
                              "    texture2d<float> sceneTex [[texture(0)]],\n"
                              "    depth2d<float> depthTex [[texture(1)]],\n"
                              "    texture2d<float> motionTex [[texture(2)]],\n"
                              "    texture2d<float> overlayTex [[texture(3)]],\n"
                              "    sampler s [[sampler(0)]],\n"
                              "    constant PostFXParams &p [[buffer(0)]]) {\n"
                              "    float3 color = sampleScene(sceneTex, s, in.uv);\n"
                              "    float depth = sampleDepth(depthTex, s, in.uv);\n"
                              "    float3 worldPos = reconstructWorld(p, in.uv, depth);\n"
                              "    if (p.motionBlurEnabled != 0) color = applyMotionBlur(p, "
                              "sceneTex, motionTex, depthTex, s, in.uv, color, worldPos);\n"
                              "    if (p.dofEnabled != 0) color = applyDof(p, sceneTex, s, in.uv, "
                              "color, worldPos);\n"
                              "    if (p.ssaoEnabled != 0) color *= computeSsao(p, depthTex, s, "
                              "in.uv, depth);\n"
                              "    if (p.fxaaEnabled != 0) color = applyFxaa(p, sceneTex, s, "
                              "in.uv, color);\n"
                              "    if (p.bloomEnabled) {\n"
                              "        float2 bloomStep = p.invResolution * "
                              "float(min(max(p.bloomPasses, 0), 32));\n"
                              "        float3 threshold = float3(p.bloomThreshold);\n"
                              "        float3 bloom = max(color - threshold, float3(0.0));\n"
                              "        bloom += max(sampleScene(sceneTex, s, in.uv + "
                              "float2(bloomStep.x, 0.0)) - threshold, float3(0.0));\n"
                              "        bloom += max(sampleScene(sceneTex, s, in.uv - "
                              "float2(bloomStep.x, 0.0)) - threshold, float3(0.0));\n"
                              "        bloom += max(sampleScene(sceneTex, s, in.uv + "
                              "float2(0.0, bloomStep.y)) - threshold, float3(0.0));\n"
                              "        bloom += max(sampleScene(sceneTex, s, in.uv - "
                              "float2(0.0, bloomStep.y)) - threshold, float3(0.0));\n"
                              "        color += bloom * (p.bloomStrength / 5.0);\n"
                              "    }\n"
                              "    if (p.tonemapMode == 1) {\n"
                              "        color *= p.tonemapExposure;\n"
                              "        color = color / (color + 1.0);\n"
                              "        color = pow(clamp(color, 0.0, 1.0), float3(1.0 / 2.2));\n"
                              "    }\n"
                              "    if (p.tonemapMode == 2) {\n"
                              "        float3 x = color * p.tonemapExposure;\n"
                              "        color = (x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14);\n"
                              "        color = pow(clamp(color, 0.0, 1.0), float3(1.0 / 2.2));\n"
                              "    }\n"
                              "    if (p.colorGradeEnabled) {\n"
                              "        color = (color - 0.5) * p.cgContrast + 0.5;\n"
                              "        color += p.cgBright;\n"
                              "        float luma = dot(color, float3(0.299,0.587,0.114));\n"
                              "        color = mix(float3(luma), color, p.cgSat);\n"
                              "        color = clamp(color, 0.0, 1.0);\n"
                              "    }\n"
                              "    if (p.vignetteEnabled) {\n"
                              "        float2 ctr = in.uv - 0.5;\n"
                              "        float d = length(ctr) * 1.41421356;\n"
                              "        float vig = 1.0;\n"
                              "        if (d > p.vigRadius) vig = 1.0 - clamp((d - "
                              "p.vigRadius) / max(p.vigSoftness, 0.000001), 0.0, 1.0);\n"
                              "        color *= vig;\n"
                              "    }\n"
                              "    if (p.overlayEnabled != 0) {\n"
                              "        float4 overlay = overlayTex.sample(s, in.uv);\n"
                              "        color = mix(color, overlay.rgb, clamp(overlay.a, 0.0, "
                              "1.0));\n"
                              "    }\n"
                              "    return float4(color, 1.0);\n"
                              "}\n"]
            ];
            NSString *postfxSrc = [postfxChunks componentsJoinedByString:@ ""];

            NSError *pfxErr = nil;
            ctx.postfxLibrary = [device newLibraryWithSource:postfxSrc options:nil error:&pfxErr];
            if (ctx.postfxLibrary) {
                id<MTLFunction> pfxVS = [ctx.postfxLibrary newFunctionWithName:@ "fullscreen_vs"];
                id<MTLFunction> pfxFS = [ctx.postfxLibrary newFunctionWithName:@ "postfx_fs"];
                if (pfxVS && pfxFS) {
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

        {
            NSString *skyboxSrc =
                @
                 "#include <metal_stdlib>\n"
                 "using namespace metal;\n"
                 "struct SkyboxOut { float4 position [[position]]; float2 ndc; };\n"
                 "struct SkyboxParams { float4x4 inverseProjection; float4x4 "
                 "inverseViewRotation; float4 cameraForward; };\n"
                 "float3 skybox_safe_normalize3(float3 v, float3 fallback) {\n"
                 "    float len2 = dot(v, v);\n"
                 "    return (len2 > 1e-12 && len2 < 1e20) ? v * rsqrt(len2) : fallback;\n"
                 "}\n"
                 "vertex SkyboxOut skybox_vs(uint vid [[vertex_id]]) {\n"
                 "    SkyboxOut out;\n"
                 "    float2 clip;\n"
                 "    if (vid == 0) clip = float2(-1.0, -1.0);\n"
                 "    else if (vid == 1) clip = float2(3.0, -1.0);\n"
                 "    else clip = float2(-1.0, 3.0);\n"
                 "    out.position = float4(clip, 1.0, 1.0);\n"
                 "    out.ndc = clip;\n"
                 "    return out;\n"
                 "}\n"
                 "fragment float4 skybox_fs(SkyboxOut in [[stage_in]], constant SkyboxParams &p "
                 "[[buffer(0)]], texturecube<float> skyboxTex [[texture(0)]], sampler s "
                 "[[sampler(0)]]) {\n"
                 "    float3 worldDir;\n"
                 "    if (p.cameraForward.w > 0.5) {\n"
                 "        worldDir = skybox_safe_normalize3(p.cameraForward.xyz, "
                 "float3(0.0, 0.0, -1.0));\n"
                 "    } else {\n"
                 "        float4 clip = float4(in.ndc, 1.0, 1.0);\n"
                 "        float4 view = p.inverseProjection * clip;\n"
                 "        float3 viewDir = skybox_safe_normalize3(view.xyz / "
                 "max(fabs(view.w), 0.0001), float3(0.0, 0.0, -1.0));\n"
                 "        worldDir = skybox_safe_normalize3((p.inverseViewRotation * "
                 "float4(viewDir, 0.0)).xyz, viewDir);\n"
                 "    }\n"
                 "    return skyboxTex.sample(s, worldDir);\n"
                 "}\n";
            NSError *skyErr = nil;
            id<MTLLibrary> skyLib = [device newLibraryWithSource:skyboxSrc
                                                         options:nil
                                                           error:&skyErr];
            if (skyLib) {
                id<MTLFunction> skyVS = [skyLib newFunctionWithName:@ "skybox_vs"];
                id<MTLFunction> skyFS = [skyLib newFunctionWithName:@ "skybox_fs"];
                if (skyVS && skyFS) {
                    ctx.skyboxPipeline = metal_create_skybox_pipeline_state(
                        device, skyVS, skyFS, MTLPixelFormatRGBA16Float, &skyErr);
                    ctx.skyboxColorPipeline = metal_create_skybox_pipeline_state(
                        device, skyVS, skyFS, MTLPixelFormatBGRA8Unorm, &skyErr);
                }
            }
            ctx.skyboxVertexBuffer = [device newBufferWithBytes:metal_skybox_vertices
                                                         length:sizeof(metal_skybox_vertices)
                                                        options:MTLResourceStorageModeShared];
        }

        metal_recreate_main_targets(ctx, w, h);

        /* Backend initialized successfully */
        return (__bridge_retained void *)ctx;
    }
}

/// @brief Destroy the Metal backend context and release all GPU resources and caches.
static void metal_destroy_ctx(void *ctx_ptr) {
    if (!ctx_ptr)
        return;
    @autoreleasepool {
        VGFXMetalContext *ctx = (__bridge_transfer VGFXMetalContext *)ctx_ptr;
        metal_detach_render_target(ctx, YES);
        metal_free_gpu_postfx_chain(ctx);
        [ctx.metalLayer removeFromSuperlayer];
        ctx.metalLayer = nil;
        (void)ctx; /* ARC releases all properties */
    }
}

/// @brief Set the clear color used when the next frame's scene pass begins.
static void metal_clear(void *ctx_ptr, vgfx_window_t win, float r, float g, float b) {
    (void)win;
    VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
    ctx.clearR = r;
    ctx.clearG = g;
    ctx.clearB = b;
}

/// @brief Begin a 3D frame: advance the frame serial, set up camera uniforms, and open the scene
/// pass.
static void metal_begin_frame(void *ctx_ptr, const vgfx3d_camera_params_t *cam) {
    @autoreleasepool {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
        BOOL new_command_buffer = NO;
        float inv_vp[16];
        vgfx3d_metal_frame_history_t history;
        int8_t load_existing_color;
        int8_t is_overlay_pass;
        if (!ctx || !cam)
            return;

        ctx.frameSerial++;
        float vp[16];
        mat4f_mul(cam->projection, cam->view, vp);
        if (vgfx3d_invert_matrix4(vp, inv_vp) != 0)
            mat4f_identity(inv_vp);

        ctx.currentTargetKind = vgfx3d_metal_choose_target_kind(
            ctx.rttActive ? 1 : 0, ctx.gpuPostfxEnabled, cam->load_existing_color);
        if (!ctx.rttActive && cam->load_existing_color && ctx.postfxCompositedToDrawable)
            ctx.currentTargetKind = VGFX3D_METAL_TARGET_SWAPCHAIN;
        is_overlay_pass = ctx.currentTargetKind == VGFX3D_METAL_TARGET_OVERLAY ? 1 : 0;
        if (!is_overlay_pass)
            ctx.textureUploadBytes = 0;
        history = ctx.frameHistory;
        load_existing_color = vgfx3d_metal_should_load_existing_color(
            ctx.currentTargetKind, cam->load_existing_color, history.overlay_used_this_frame);
        vgfx3d_metal_update_frame_history(&history,
                                          vp,
                                          inv_vp,
                                          cam->position,
                                          is_overlay_pass,
                                          ctx.currentTargetKind == VGFX3D_METAL_TARGET_OVERLAY);
        ctx.frameHistory = history;

        memcpy(ctx->_view, cam->view, sizeof(float) * 16);
        memcpy(ctx->_projection, cam->projection, sizeof(float) * 16);
        memcpy(ctx->_vp, vp, sizeof(float) * 16);
        memcpy(ctx->_prevVP, ctx.frameHistory.draw_prev_vp, sizeof(ctx->_prevVP));
        memcpy(ctx->_invVP, inv_vp, sizeof(ctx->_invVP));
        memcpy(ctx->_camPos, cam->position, sizeof(float) * 3);
        memcpy(ctx->_camForward, cam->forward, sizeof(float) * 3);
        ctx->_camIsOrtho = cam->is_ortho ? YES : NO;
        ctx->_prevVPValid = ctx.frameHistory.scene_history_valid ? YES : NO;

        /* MTL-07: Store fog parameters for submit_draw */
        ctx->_fogEnabled = cam->fog_enabled;
        ctx->_fogNear = cam->fog_near;
        ctx->_fogFar = cam->fog_far;
        ctx->_fogColor[0] = cam->fog_color[0];
        ctx->_fogColor[1] = cam->fog_color[1];
        ctx->_fogColor[2] = cam->fog_color[2];

        /* Reuse the command buffer if one is already open (multi-pass frame).
         * RTT end_frame commits and nils the cmdBuf, so on-screen passes
         * after an RTT pass will create a fresh one. */
        if (!ctx.cmdBuf) {
            ctx.cmdBuf = [ctx.commandQueue commandBuffer];
            if (!ctx.cmdBuf)
                return;
            ctx.frameBuffers = [NSMutableArray arrayWithCapacity:32];
            ctx.drawable = nil;
            ctx.displayTexture = nil;
            ctx.postfxEncodedThisFrame = 0;
            ctx.postfxCompositedToDrawable = 0;
            new_command_buffer = YES;
        } else if (!ctx.frameBuffers)
            ctx.frameBuffers = [NSMutableArray arrayWithCapacity:32];
        if (new_command_buffer || !is_overlay_pass) {
            ctx->_shadowPassSlot = -1;
            ctx->_shadowCount = 0;
            memset(ctx->_shadowComplete, 0, sizeof(ctx->_shadowComplete));
        }

        if ((ctx.frameSerial & 31u) == 0u) {
            metal_prune_texture_cache(ctx);
            metal_prune_cubemap_cache(ctx);
            metal_prune_morph_cache(ctx);
        }

        metal_begin_scene_encoder(ctx, load_existing_color, cam->load_existing_depth);
        if (!ctx.encoder)
            return;
        ctx.inFrame = YES;
    }
}

/// @brief Add @p bytes to the context's running per-frame texture-upload total (saturating).
static void metal_record_texture_upload_bytes(VGFXMetalContext *ctx, uint64_t bytes) {
    if (!ctx || bytes == 0)
        return;
    if (ctx.textureUploadBytes > UINT64_MAX - bytes) {
        ctx.textureUploadBytes = UINT64_MAX;
        return;
    }
    ctx.textureUploadBytes += bytes;
}

/// @brief Report which native compressed texture formats this Metal device can sample
/// (BC7/ASTC/ETC2).
/// @details Apple-silicon GPUs support ASTC/ETC2; Intel Macs support BC7. Returns the matching
///          RT_CANVAS3D_BACKEND_CAP_* bitmask.
static int64_t metal_get_native_texture_caps(void *ctx_ptr) {
    VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
    int64_t caps = 0;
    if (!ctx || !ctx.device)
        return 0;
#if defined(__aarch64__)
    caps |= RT_CANVAS3D_BACKEND_CAP_ASTC | RT_CANVAS3D_BACKEND_CAP_ETC2;
#else
    if ([ctx.device respondsToSelector:@selector(supportsBCTextureCompression)] &&
        ctx.device.supportsBCTextureCompression)
        caps |= RT_CANVAS3D_BACKEND_CAP_BC7;
#endif
    return caps;
}

/// @brief Map a native compressed mip's format/block size to the matching MTLPixelFormat (0 if
/// unsupported).
static MTLPixelFormat metal_native_texture_pixel_format(const vgfx3d_native_texture_mip_t *mip) {
    if (!mip)
        return MTLPixelFormatInvalid;
    if (mip->format_id == RT_TEXTUREASSET3D_NATIVE_FORMAT_BC7)
        return MTLPixelFormatBC7_RGBAUnorm;
    if (mip->format_id == RT_TEXTUREASSET3D_NATIVE_FORMAT_ETC2)
        return MTLPixelFormatEAC_RGBA8;
    if (mip->format_id == RT_TEXTUREASSET3D_NATIVE_FORMAT_ASTC) {
        if (mip->block_width == 4 && mip->block_height == 4)
            return MTLPixelFormatASTC_4x4_LDR;
        if (mip->block_width == 5 && mip->block_height == 4)
            return MTLPixelFormatASTC_5x4_LDR;
        if (mip->block_width == 5 && mip->block_height == 5)
            return MTLPixelFormatASTC_5x5_LDR;
        if (mip->block_width == 6 && mip->block_height == 5)
            return MTLPixelFormatASTC_6x5_LDR;
        if (mip->block_width == 6 && mip->block_height == 6)
            return MTLPixelFormatASTC_6x6_LDR;
        if (mip->block_width == 8 && mip->block_height == 5)
            return MTLPixelFormatASTC_8x5_LDR;
        if (mip->block_width == 8 && mip->block_height == 6)
            return MTLPixelFormatASTC_8x6_LDR;
        if (mip->block_width == 8 && mip->block_height == 8)
            return MTLPixelFormatASTC_8x8_LDR;
        if (mip->block_width == 10 && mip->block_height == 5)
            return MTLPixelFormatASTC_10x5_LDR;
        if (mip->block_width == 10 && mip->block_height == 6)
            return MTLPixelFormatASTC_10x6_LDR;
        if (mip->block_width == 10 && mip->block_height == 8)
            return MTLPixelFormatASTC_10x8_LDR;
        if (mip->block_width == 10 && mip->block_height == 10)
            return MTLPixelFormatASTC_10x10_LDR;
        if (mip->block_width == 12 && mip->block_height == 10)
            return MTLPixelFormatASTC_12x10_LDR;
        if (mip->block_width == 12 && mip->block_height == 12)
            return MTLPixelFormatASTC_12x12_LDR;
    }
    return MTLPixelFormatInvalid;
}

/// @brief Bytes per block-row of a native compressed mip (block columns × block byte size,
/// overflow-safe).
static uint64_t metal_native_texture_row_bytes(const vgfx3d_native_texture_mip_t *mip) {
    uint64_t cols;
    if (!mip || mip->width <= 0 || mip->block_width <= 0 || mip->block_bytes <= 0)
        return 0;
    cols = ((uint64_t)(uint32_t)mip->width + (uint64_t)(uint32_t)mip->block_width - 1u) /
           (uint64_t)(uint32_t)mip->block_width;
    if (cols > UINT64_MAX / (uint64_t)(uint32_t)mip->block_bytes)
        return 0;
    return cols * (uint64_t)(uint32_t)mip->block_bytes;
}

/// @brief Bytes still to upload for a cached texture entry (native or RGBA streaming path).
static uint64_t metal_texture_pending_bytes(VGFXMetalTextureCacheEntry *entry) {
    if (!entry)
        return 0;
    if (entry.textureAsset)
        return vgfx3d_textureasset_pending_native_snapshot_bytes(
            entry.textureAsset,
            entry.nativeMipStart,
            entry.nativeMipCount,
            entry.nativeNextMip,
            entry.nativeNextBlockRow,
            entry.uploadInProgress ? 1 : 0);
    return vgfx3d_pending_rgba_upload_bytes(
        entry.width, entry.height, entry.uploadNextRow, entry.uploadInProgress ? 1 : 0);
}

/// @brief Bytes still to upload for a cached cubemap entry across its remaining faces/rows.
static uint64_t metal_cubemap_pending_bytes(VGFXMetalCubemapCacheEntry *entry) {
    if (!entry)
        return 0;
    return vgfx3d_pending_cubemap_rgba_upload_bytes(
        entry.faceSize, entry.uploadFace, entry.uploadNextRow, entry.uploadInProgress ? 1 : 0);
}

/// @brief Total bytes still pending across all in-progress texture/cubemap uploads (saturating).
static uint64_t metal_get_texture_upload_pending_bytes(void *ctx_ptr) {
    VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
    uint64_t total = 0;
    if (!ctx || !ctx.textureCache)
        return 0;
    for (NSValue *key in ctx.textureCache) {
        VGFXMetalTextureCacheEntry *entry = ctx.textureCache[key];
        uint64_t bytes = metal_texture_pending_bytes(entry);
        if (total > UINT64_MAX - bytes)
            return UINT64_MAX;
        total += bytes;
    }
    for (NSValue *key in ctx.cubemapCache) {
        VGFXMetalCubemapCacheEntry *entry = ctx.cubemapCache[key];
        uint64_t bytes = metal_cubemap_pending_bytes(entry);
        if (total > UINT64_MAX - bytes)
            return UINT64_MAX;
        total += bytes;
    }
    return total;
}

/// @brief Set the per-frame byte budget that paces streaming texture uploads on this context.
static void metal_set_texture_upload_budget(void *ctx_ptr, uint64_t bytes) {
    VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
    if (ctx)
        ctx.textureUploadBudgetBytes = bytes;
}

/// @brief Upload more of an in-progress native compressed texture, bounded by the upload budget.
/// @return 1 if the upload finished this call, 0 if more mips remain.
static int metal_continue_native_texture_upload(VGFXMetalContext *ctx,
                                                VGFXMetalTextureCacheEntry *entry) {
    if (!ctx || !entry || !entry.textureAsset || !entry.uploadInProgress || !entry.texture)
        return 0;
    while (entry.nativeNextMip < entry.nativeMipCount) {
        vgfx3d_native_texture_mip_t mip;
        uint64_t row_bytes;
        uint64_t block_rows;
        int32_t rows;
        uint64_t offset;
        uint64_t upload_bytes;
        uint64_t y;
        uint64_t h;
        MTLRegion region;

        if (ctx.textureUploadBudgetBytes != UINT64_MAX &&
            ctx.textureUploadBytes >= ctx.textureUploadBudgetBytes)
            return 0;
        if (!vgfx3d_textureasset_get_native_snapshot_mip(entry.textureAsset,
                                                         entry.nativeMipStart,
                                                         entry.nativeMipCount,
                                                         entry.nativeNextMip,
                                                         &mip))
            return 0;
        row_bytes = metal_native_texture_row_bytes(&mip);
        if (row_bytes == 0 || row_bytes > (uint64_t)NSUIntegerMax ||
            mip.bytes > (uint64_t)NSUIntegerMax)
            return 0;
        block_rows =
            ((uint64_t)(uint32_t)mip.height + (uint64_t)(uint32_t)mip.block_height - 1u) /
            (uint64_t)(uint32_t)mip.block_height;
        if (block_rows == 0 || block_rows > (uint64_t)INT32_MAX)
            return 0;
        if (entry.nativeNextBlockRow < 0 ||
            (uint64_t)(uint32_t)entry.nativeNextBlockRow >= block_rows)
            entry.nativeNextBlockRow = 0;
        rows = vgfx3d_upload_block_rows_for_budget(mip.width,
                                                   mip.height,
                                                   mip.block_width,
                                                   mip.block_height,
                                                   mip.block_bytes,
                                                   entry.nativeNextBlockRow,
                                                   ctx.textureUploadBudgetBytes,
                                                   ctx.textureUploadBytes);
        if (rows <= 0)
            return 0;
        offset = (uint64_t)(uint32_t)entry.nativeNextBlockRow * row_bytes;
        upload_bytes = (uint64_t)(uint32_t)rows * row_bytes;
        y = (uint64_t)(uint32_t)entry.nativeNextBlockRow * (uint64_t)(uint32_t)mip.block_height;
        h = (uint64_t)(uint32_t)rows * (uint64_t)(uint32_t)mip.block_height;
        if (offset > mip.bytes || upload_bytes > mip.bytes - offset || y >= (uint64_t)mip.height)
            return 0;
        if (h > (uint64_t)mip.height - y)
            h = (uint64_t)mip.height - y;
        if (upload_bytes > (uint64_t)NSUIntegerMax || y > (uint64_t)NSUIntegerMax ||
            h > (uint64_t)NSUIntegerMax)
            return 0;
        region = MTLRegionMake2D(0, (NSUInteger)y, (NSUInteger)mip.width, (NSUInteger)h);
        [entry.texture replaceRegion:region
                         mipmapLevel:(NSUInteger)entry.nativeNextMip
                           withBytes:(const uint8_t *)mip.data + offset
                         bytesPerRow:(NSUInteger)row_bytes];
        metal_record_texture_upload_bytes(ctx, upload_bytes);
        entry.nativeNextBlockRow += rows;
        if ((uint64_t)(uint32_t)entry.nativeNextBlockRow >= block_rows) {
            entry.nativeNextMip++;
            entry.nativeNextBlockRow = 0;
        }
        entry.lastUsedFrame = ctx.frameSerial;
    }
    entry.generation = entry.pendingGeneration;
    entry.pendingGeneration = 0;
    entry.uploadInProgress = 0;
    return 1;
}

/// @brief Begin uploading a native compressed TextureAsset3D: create the texture and seed the
/// cursor.
static int metal_start_native_texture_upload(VGFXMetalContext *ctx,
                                             VGFXMetalTextureCacheEntry *entry,
                                             void *asset,
                                             uint64_t cache_key,
                                             int64_t mip_start,
                                             int64_t mip_count) {
    vgfx3d_native_texture_mip_t first_mip;
    MTLPixelFormat pixel_format;

    if (!ctx || !entry || !asset ||
        !vgfx3d_textureasset_native_supported(
            asset, metal_get_native_texture_caps((__bridge void *)ctx)) ||
        !vgfx3d_textureasset_get_native_snapshot_mip(asset, mip_start, mip_count, 0, &first_mip))
        return 0;
    pixel_format = metal_native_texture_pixel_format(&first_mip);
    if (pixel_format == MTLPixelFormatInvalid)
        return 0;
    if (mip_count <= 0)
        return 0;

    MTLTextureDescriptor *texDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pixel_format
                                                           width:(NSUInteger)first_mip.width
                                                          height:(NSUInteger)first_mip.height
                                                       mipmapped:(mip_count > 1)];
    texDesc.mipmapLevelCount = (NSUInteger)mip_count;
    texDesc.usage = MTLTextureUsageShaderRead;
    texDesc.storageMode = MTLStorageModeShared;
    entry.texture = [ctx.device newTextureWithDescriptor:texDesc];
    if (!entry.texture)
        return 0;

    entry.textureAsset = asset;
    entry.nativeFormat = first_mip.format_id;
    entry.nativeNextBlockRow = 0;
    entry.nativeNextMip = 0;
    entry.nativeMipStart = mip_start;
    entry.nativeMipCount = mip_count;
    entry.pendingGeneration = cache_key;
    entry.generation = 0;
    entry.width = first_mip.width;
    entry.height = first_mip.height;
    entry.uploadNextRow = 0;
    entry.uploadInProgress = 1;
    entry.lastUsedFrame = ctx.frameSerial;
    metal_continue_native_texture_upload(ctx, entry);
    return 1;
}

/// @brief Upload more rows of an in-progress RGBA texture (swizzled to BGRA), bounded by the
/// budget.
/// @return 1 if the upload finished this call, 0 if more rows remain.
static int metal_continue_texture_upload(VGFXMetalContext *ctx,
                                         VGFXMetalTextureCacheEntry *entry,
                                         const void *pixels_ptr) {
    int32_t rows;
    int32_t slice_w = 0;
    int32_t slice_rows = 0;
    uint8_t *rgba = NULL;
    uint64_t bytes;

    if (!ctx || !entry || !pixels_ptr || !entry.uploadInProgress || !entry.texture)
        return 0;
    rows = vgfx3d_upload_rows_for_budget(entry.width,
                                         entry.height,
                                         entry.uploadNextRow,
                                         ctx.textureUploadBudgetBytes,
                                         ctx.textureUploadBytes);
    if (rows <= 0)
        return 0;
    if (vgfx3d_unpack_pixels_rgba_rows(
            pixels_ptr, entry.uploadNextRow, rows, 0, &slice_w, &slice_rows, &rgba) != 0 ||
        !rgba || slice_w != entry.width || slice_rows <= 0) {
        free(rgba);
        return 0;
    }
    if (!metal_upload_rgba_to_bgra_texture_rows(
            entry.texture, rgba, slice_w, entry.uploadNextRow, slice_rows)) {
        free(rgba);
        return 0;
    }
    bytes = (uint64_t)(uint32_t)slice_w * (uint64_t)(uint32_t)slice_rows * 4u;
    metal_record_texture_upload_bytes(ctx, bytes);
    free(rgba);

    entry.uploadNextRow += slice_rows;
    entry.lastUsedFrame = ctx.frameSerial;
    if (entry.uploadNextRow >= entry.height) {
        entry.generation = entry.pendingGeneration;
        entry.pendingGeneration = 0;
        entry.uploadInProgress = 0;
        metal_generate_mipmaps(ctx, entry.texture);
        return 1;
    }
    return 0;
}

/// @brief Begin uploading an RGBA Pixels texture: allocate the BGRA texture and seed the row
/// cursor.
static int metal_start_texture_upload(VGFXMetalContext *ctx,
                                      VGFXMetalTextureCacheEntry *entry,
                                      const void *pixels_ptr,
                                      uint64_t cache_key) {
    int32_t tw = 0;
    int32_t th = 0;
    int32_t mip_count;

    if (!ctx || !entry || !pixels_ptr || !vgfx3d_get_pixels_extent(pixels_ptr, &tw, &th))
        return 0;

    mip_count = vgfx3d_metal_compute_mip_count(tw, th);
    if (!entry.texture || (int32_t)entry.texture.width != tw ||
        (int32_t)entry.texture.height != th) {
        MTLTextureDescriptor *texDesc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                               width:(NSUInteger)tw
                                                              height:(NSUInteger)th
                                                           mipmapped:(mip_count > 1)];
        texDesc.usage = MTLTextureUsageShaderRead;
        texDesc.storageMode = MTLStorageModeShared;
        entry.texture = [ctx.device newTextureWithDescriptor:texDesc];
    }
    if (!entry.texture)
        return 0;

    entry.pendingGeneration = cache_key;
    entry.generation = 0;
    entry.textureAsset = NULL;
    entry.nativeFormat = RT_TEXTUREASSET3D_NATIVE_FORMAT_NONE;
    entry.nativeNextBlockRow = 0;
    entry.nativeNextMip = 0;
    entry.nativeMipCount = 0;
    entry.width = tw;
    entry.height = th;
    entry.uploadNextRow = 0;
    entry.uploadInProgress = 1;
    entry.lastUsedFrame = ctx.frameSerial;
    metal_continue_texture_upload(ctx, entry, pixels_ptr);
    return 1;
}

/// MTL-03: Retrieve or create a cached MTLTexture from a Pixels object pointer.
/// Cache uses the stable Pixels cache key so allocator-reused addresses do not
/// alias a fresh image to an older GPU upload. Conversion is RGBA→BGRA.
static id<MTLTexture> metal_get_cached_texture(VGFXMetalContext *ctx, const void *pixels_ptr) {
    uint64_t cache_key;
    NSValue *key;
    VGFXMetalTextureCacheEntry *cached;

    if (!ctx || !pixels_ptr)
        return nil;
    key = [NSValue valueWithPointer:pixels_ptr];
    cached = ctx.textureCache[key];
    cache_key = vgfx3d_get_pixels_cache_key(pixels_ptr);
    if (cached && cached.texture && cached.generation == cache_key) {
        cached.lastUsedFrame = ctx.frameSerial;
        return cached.texture;
    }
    if (cached && cached.pendingGeneration == cache_key && cached.uploadInProgress) {
        return metal_continue_texture_upload(ctx, cached, pixels_ptr) ? cached.texture : nil;
    }
    if (!cached)
        cached = [[VGFXMetalTextureCacheEntry alloc] init];
    ctx.textureCache[key] = cached;
    if (!metal_start_texture_upload(ctx, cached, pixels_ptr, cache_key))
        return nil;
    return cached.uploadInProgress ? nil : cached.texture;
}

/// @brief Get the Metal texture for a native TextureAsset3D, creating/streaming it on a cache miss.
/// @details Keyed by the asset's native cache key; returns nil while an upload is still in
/// progress.
static id<MTLTexture> metal_get_cached_native_texture(VGFXMetalContext *ctx,
                                                      void *asset,
                                                      uint64_t cache_key,
                                                      int64_t mip_start,
                                                      int64_t mip_count) {
    NSValue *key;
    VGFXMetalTextureCacheEntry *cached;

    if (!ctx || !asset ||
        !vgfx3d_textureasset_native_supported(asset,
                                              metal_get_native_texture_caps((__bridge void *)ctx)))
        return nil;
    if (cache_key == 0)
        cache_key = rt_textureasset3d_get_native_cache_key(asset);
    if (mip_start < 0)
        mip_start = rt_textureasset3d_get_resident_mip_start(asset);
    if (mip_count <= 0)
        mip_count = rt_textureasset3d_get_resident_mip_count(asset);
    if (cache_key == 0)
        return nil;
    key = [NSValue valueWithPointer:asset];
    cached = ctx.textureCache[key];
    if (cached && cached.texture && cached.textureAsset == asset &&
        cached.generation == cache_key && cached.nativeMipStart == mip_start &&
        cached.nativeMipCount == mip_count) {
        cached.lastUsedFrame = ctx.frameSerial;
        return cached.texture;
    }
    if (cached && cached.textureAsset == asset && cached.pendingGeneration == cache_key &&
        cached.nativeMipStart == mip_start && cached.nativeMipCount == mip_count &&
        cached.uploadInProgress) {
        return metal_continue_native_texture_upload(ctx, cached) ? cached.texture : nil;
    }
    if (!cached)
        cached = [[VGFXMetalTextureCacheEntry alloc] init];
    ctx.textureCache[key] = cached;
    if (!metal_start_native_texture_upload(ctx, cached, asset, cache_key, mip_start, mip_count))
        return nil;
    return cached.uploadInProgress ? nil : cached.texture;
}

/// @brief Resolve a material's texture to an MTLTexture, preferring native blocks then RGBA Pixels.
/// @return The texture to bind, or nil if neither source is uploadable yet.
static id<MTLTexture> metal_get_material_texture(VGFXMetalContext *ctx,
                                                 void *asset,
                                                 const void *pixels_ptr,
                                                 uint64_t asset_cache_key,
                                                 int64_t mip_start,
                                                 int64_t mip_count) {
    /* Native-supported assets stay on the native upload path. Falling back to
     * RGBA while native upload is budget-paused leaves abandoned pending bytes. */
    if (asset && vgfx3d_textureasset_native_supported(
                     asset, metal_get_native_texture_caps((__bridge void *)ctx)))
        return metal_get_cached_native_texture(ctx, asset, asset_cache_key, mip_start, mip_count);
    return pixels_ptr ? metal_get_cached_texture(ctx, pixels_ptr) : nil;
}

/// @brief Upload more of an in-progress cubemap (face by face, row band by row band) within budget.
/// @return 1 if all six faces finished this call, 0 if more remains.
static int metal_continue_cubemap_upload(VGFXMetalContext *ctx,
                                         VGFXMetalCubemapCacheEntry *entry,
                                         const rt_cubemap3d *cubemap) {
    if (!ctx || !entry || !cubemap || !entry.uploadInProgress || !entry.texture ||
        entry.faceSize <= 0)
        return 0;

    while (entry.uploadFace < 6) {
        int32_t rows = vgfx3d_upload_rows_for_budget(entry.faceSize,
                                                     entry.faceSize,
                                                     entry.uploadNextRow,
                                                     ctx.textureUploadBudgetBytes,
                                                     ctx.textureUploadBytes);
        int32_t slice_size = 0;
        int32_t slice_rows = 0;
        uint8_t *rgba = NULL;
        uint64_t bytes;

        if (rows <= 0)
            return 0;
        if (vgfx3d_unpack_cubemap_rgba_rows(cubemap,
                                            entry.uploadFace,
                                            entry.uploadNextRow,
                                            rows,
                                            0,
                                            &slice_size,
                                            &slice_rows,
                                            &rgba) != 0 ||
            !rgba || slice_size != entry.faceSize || slice_rows <= 0) {
            free(rgba);
            return 0;
        }
        if (!metal_upload_rgba_to_bgra_cubemap_rows(entry.texture,
                                                    rgba,
                                                    slice_size,
                                                    entry.uploadFace,
                                                    entry.uploadNextRow,
                                                    slice_rows)) {
            free(rgba);
            return 0;
        }
        bytes = (uint64_t)(uint32_t)slice_size * (uint64_t)(uint32_t)slice_rows * 4u;
        metal_record_texture_upload_bytes(ctx, bytes);
        free(rgba);

        entry.uploadNextRow += slice_rows;
        entry.lastUsedFrame = ctx.frameSerial;
        if (entry.uploadNextRow < entry.faceSize)
            return 0;
        entry.uploadFace++;
        entry.uploadNextRow = 0;
    }

    entry.generation = entry.pendingGeneration;
    entry.pendingGeneration = 0;
    entry.uploadInProgress = 0;
    metal_generate_mipmaps(ctx, entry.texture);
    return 1;
}

/// @brief Begin uploading a cubemap: create the cube texture and seed the face/row cursor.
static int metal_start_cubemap_upload(VGFXMetalContext *ctx,
                                      VGFXMetalCubemapCacheEntry *entry,
                                      const rt_cubemap3d *cubemap,
                                      uint64_t generation) {
    int32_t face_size = 0;
    int32_t mip_count;

    if (!ctx || !entry || !cubemap || !vgfx3d_get_cubemap_face_size(cubemap, &face_size))
        return 0;

    mip_count = vgfx3d_metal_compute_mip_count(face_size, face_size);
    if (!entry.texture || (int32_t)entry.texture.width != face_size) {
        MTLTextureDescriptor *cubeDesc =
            [MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                  size:(NSUInteger)face_size
                                                             mipmapped:(mip_count > 1)];
        cubeDesc.usage = MTLTextureUsageShaderRead;
        cubeDesc.storageMode = MTLStorageModeShared;
        entry.texture = [ctx.device newTextureWithDescriptor:cubeDesc];
    }
    if (!entry.texture)
        return 0;

    entry.pendingGeneration = generation;
    entry.generation = 0;
    entry.faceSize = face_size;
    entry.uploadFace = 0;
    entry.uploadNextRow = 0;
    entry.uploadInProgress = 1;
    entry.lastUsedFrame = ctx.frameSerial;
    metal_continue_cubemap_upload(ctx, entry, cubemap);
    return 1;
}

/// @brief Get the Metal cubemap texture for a Cubemap3D, creating/streaming it on a cache miss.
/// @details Returns nil while the cubemap upload is still in progress.
static id<MTLTexture> metal_get_cached_cubemap(VGFXMetalContext *ctx, const rt_cubemap3d *cubemap) {
    uint64_t generation;
    NSValue *key;
    VGFXMetalCubemapCacheEntry *cached;

    if (!ctx || !cubemap)
        return nil;

    key = [NSValue valueWithPointer:cubemap];
    cached = ctx.cubemapCache[key];
    generation = vgfx3d_get_cubemap_generation(cubemap);
    if (cached && cached.texture && cached.generation == generation) {
        cached.lastUsedFrame = ctx.frameSerial;
        return cached.texture;
    }
    if (cached && cached.pendingGeneration == generation && cached.uploadInProgress) {
        cached.lastUsedFrame = ctx.frameSerial;
        return metal_continue_cubemap_upload(ctx, cached, cubemap) ? cached.texture : nil;
    }
    if (!cached)
        cached = [[VGFXMetalCubemapCacheEntry alloc] init];
    ctx.cubemapCache[key] = cached;
    if (!metal_start_cubemap_upload(ctx, cached, cubemap, generation))
        return nil;
    return cached.uploadInProgress ? nil : cached.texture;
}

typedef struct {
    int32_t shape_count;
    int has_prev_weights;
    int has_normal_deltas;
} metal_morph_bind_status_t;

/// @brief Compute the byte size of a morph-target GPU payload for a vertex count and channel set.
/// @return 1 with the size written, 0 on overflow or invalid inputs.
static int metal_compute_morph_payload_bytes(uint32_t vertex_count,
                                             int32_t morph_count,
                                             size_t *out_bytes) {
    size_t elements;
    size_t bytes;

    if (out_bytes)
        *out_bytes = 0;
    if (!out_bytes || vertex_count == 0 || morph_count <= 0)
        return 0;
    if ((size_t)morph_count > SIZE_MAX / (size_t)vertex_count)
        return 0;
    elements = (size_t)morph_count * (size_t)vertex_count;
    if (elements > SIZE_MAX / 3u)
        return 0;
    elements *= 3u;
    if (elements > SIZE_MAX / sizeof(float))
        return 0;
    bytes = elements * sizeof(float);
    *out_bytes = bytes;
    return 1;
}

/// @brief Get or build the cached GPU morph-delta buffer for a mesh's morph targets.
/// @details Keyed by mesh + morph revision so it rebuilds when the deltas change. NULL on failure.
static VGFXMetalMorphCacheEntry *metal_get_cached_morph_entry(VGFXMetalContext *ctx,
                                                              const vgfx3d_draw_cmd_t *cmd,
                                                              int32_t morph_count) {
    NSValue *key;
    VGFXMetalMorphCacheEntry *entry;
    size_t bytes;

    if (!ctx || !ctx.morphCache || !cmd || morph_count <= 0 || !cmd->morph_key ||
        cmd->morph_revision == 0 || !cmd->morph_deltas || !cmd->morph_weights ||
        cmd->morph_shape_count <= 0 || cmd->vertex_count == 0) {
        return nil;
    }

    key = [NSValue valueWithPointer:cmd->morph_key];
    entry = ctx.morphCache[key];
    if (entry && entry.deltaBuffer &&
        vgfx3d_metal_should_reuse_morph_cache(entry.key,
                                              entry.revision,
                                              entry.shapeCount,
                                              entry.vertexCount,
                                              entry.hasNormalDeltas,
                                              cmd)) {
        entry.lastUsedFrame = ctx.frameSerial;
        return entry;
    }

    if (!metal_compute_morph_payload_bytes(cmd->vertex_count, morph_count, &bytes))
        return nil;
    entry = entry ? entry : [[VGFXMetalMorphCacheEntry alloc] init];
    entry.deltaBuffer = metal_new_shared_buffer(ctx, cmd->morph_deltas, bytes);
    entry.normalBuffer = cmd->morph_normal_deltas
                             ? metal_new_shared_buffer(ctx, cmd->morph_normal_deltas, bytes)
                             : nil;
    if (!entry.deltaBuffer)
        return nil;
    entry.key = cmd->morph_key;
    entry.revision = cmd->morph_revision;
    entry.shapeCount = morph_count;
    entry.vertexCount = cmd->vertex_count;
    entry.hasNormalDeltas = entry.normalBuffer ? 1 : 0;
    entry.lastUsedFrame = ctx.frameSerial;
    ctx.morphCache[key] = entry;
    return entry;
}

/// @brief Bind a draw's skeletal bone matrices (the skinning palette) into the vertex stage.
/// @return 1 on success, 0 if the palette is missing or too large.
static int metal_bind_bone_palettes(VGFXMetalContext *ctx,
                                    const vgfx3d_draw_cmd_t *cmd,
                                    int has_skinning,
                                    int has_prev_skinning,
                                    int *out_prev_ok) {
    float packed_palette[VGFX3D_METAL_MAX_BONES * 16];
    size_t palette_bytes = sizeof(packed_palette);
    id<MTLBuffer> bone_buf;

    if (out_prev_ok)
        *out_prev_ok = 0;

    if (!ctx || !cmd || !has_skinning)
        return 0;

    vgfx3d_metal_pack_bone_palette(packed_palette, cmd->bone_palette, cmd->bone_count);
    bone_buf = metal_new_shared_buffer(ctx, packed_palette, palette_bytes);
    if (!bone_buf)
        return 0;
    [ctx.encoder setVertexBuffer:bone_buf offset:0 atIndex:3];
    if (ctx.frameBuffers && bone_buf)
        [ctx.frameBuffers addObject:bone_buf];
    if (has_prev_skinning) {
        id<MTLBuffer> prev_bone_buf;
        vgfx3d_metal_pack_bone_palette(packed_palette, cmd->prev_bone_palette, cmd->bone_count);
        prev_bone_buf = metal_new_shared_buffer(ctx, packed_palette, palette_bytes);
        if (prev_bone_buf) {
            [ctx.encoder setVertexBuffer:prev_bone_buf offset:0 atIndex:7];
            if (out_prev_ok)
                *out_prev_ok = 1;
        }
        if (ctx.frameBuffers && prev_bone_buf)
            [ctx.frameBuffers addObject:prev_bone_buf];
    }
    return 1;
}

/// @brief Bind a mesh's morph delta buffer and per-shape weights for a draw.
/// @return A status enum indicating whether morphing is bound, skipped, or failed.
static metal_morph_bind_status_t metal_bind_morph_payload(VGFXMetalContext *ctx,
                                                          const vgfx3d_draw_cmd_t *cmd) {
    metal_morph_bind_status_t status = {0, 0, 0};
    VGFXMetalMorphCacheEntry *cached_entry;
    id<MTLBuffer> delta_buf = nil;
    id<MTLBuffer> normal_buf = nil;
    int32_t morph_count;
    size_t delta_bytes;
    size_t weight_bytes;

    if (!ctx || !cmd || !cmd->morph_deltas || !cmd->morph_weights || cmd->morph_shape_count <= 0)
        return status;
    morph_count = vgfx3d_metal_clamp_morph_shape_count(cmd->vertex_count, cmd->morph_shape_count);
    if (morph_count <= 0)
        return status;

    cached_entry = metal_get_cached_morph_entry(ctx, cmd, morph_count);
    if (cached_entry) {
        delta_buf = cached_entry.deltaBuffer;
        normal_buf = cached_entry.normalBuffer;
    } else {
        if (!metal_compute_morph_payload_bytes(cmd->vertex_count, morph_count, &delta_bytes))
            return status;
        delta_buf = metal_new_shared_buffer(ctx, cmd->morph_deltas, delta_bytes);
        normal_buf = cmd->morph_normal_deltas
                         ? metal_new_shared_buffer(ctx, cmd->morph_normal_deltas, delta_bytes)
                         : nil;
        if (ctx.frameBuffers && delta_buf)
            [ctx.frameBuffers addObject:delta_buf];
        if (ctx.frameBuffers && normal_buf)
            [ctx.frameBuffers addObject:normal_buf];
    }
    if (!delta_buf)
        return status;

    weight_bytes = (size_t)morph_count * sizeof(float);
    [ctx.encoder setVertexBuffer:delta_buf offset:0 atIndex:4];
    if (normal_buf)
        [ctx.encoder setVertexBuffer:normal_buf offset:0 atIndex:9];
    [ctx.encoder setVertexBytes:cmd->morph_weights length:weight_bytes atIndex:5];
    if (cmd->prev_morph_weights)
        [ctx.encoder setVertexBytes:cmd->prev_morph_weights length:weight_bytes atIndex:8];
    status.shape_count = morph_count;
    status.has_prev_weights = cmd->prev_morph_weights ? 1 : 0;
    status.has_normal_deltas = normal_buf ? 1 : 0;
    return status;
}

/// @brief Ensure the per-instance transform buffer can hold @p instance_count instances (grows as
/// needed).
static void metal_ensure_instance_storage(VGFXMetalContext *ctx, int32_t instance_count) {
    int32_t needed_capacity;
    int32_t next_capacity;
    NSUInteger byte_count;

    if (!ctx || instance_count <= 0)
        return;

    needed_capacity = instance_count;
    next_capacity = vgfx3d_metal_next_capacity((int32_t)ctx.instanceCapacity, needed_capacity, 64);
    if ((NSUInteger)next_capacity > ctx.instanceCapacity || !ctx.instanceScratch) {
        byte_count = (NSUInteger)next_capacity * sizeof(vgfx3d_metal_instance_data_t);
        ctx.instanceScratch = [NSMutableData dataWithLength:byte_count];
        ctx.instanceCapacity = (NSUInteger)next_capacity;
    }
    if (!ctx.instanceBuf || ctx.instanceBuf.length < ctx.instanceScratch.length) {
        ctx.instanceBuf = metal_new_shared_buffer_with_length(ctx, ctx.instanceScratch.length);
    }
}

/// @brief Encode and submit a single 3D draw command (the backend's main draw entry point).
/// @details Selects/creates the pipeline state for the material, binds geometry, textures,
/// lighting,
///          skinning, and morph data, applies the camera-relative transform, and issues the draw
///          (instanced when an instance buffer is provided).
static void metal_submit_draw(void *ctx_ptr,
                              vgfx_window_t win,
                              const vgfx3d_draw_cmd_t *cmd,
                              const vgfx3d_light_params_t *lights,
                              int32_t light_count,
                              const float *ambient,
                              int8_t wireframe,
                              int8_t backface_cull) {
    @autoreleasepool {
        (void)win;
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
        id<MTLBuffer> vb;
        id<MTLBuffer> ib;
        vgfx3d_metal_blend_mode_t blend_mode;
        static const float zero_ambient[3] = {0.0f, 0.0f, 0.0f};
        if (!ctx || !ctx.encoder || !ctx.inFrame || !cmd || !cmd->vertices || !cmd->indices ||
            cmd->vertex_count == 0 || cmd->index_count == 0)
            return;
        if (!ambient)
            ambient = zero_ambient;
        if (!lights || light_count < 0)
            light_count = 0;

        blend_mode = vgfx3d_metal_choose_blend_mode(cmd);
        [ctx.encoder setRenderPipelineState:metal_select_pipeline_state(ctx, cmd, NO)];
        [ctx.encoder setCullMode:backface_cull ? MTLCullModeBack : MTLCullModeNone];

        /* MTL-08: Wireframe mode via fill mode toggle */
        [ctx.encoder
            setTriangleFillMode:wireframe ? MTLTriangleFillModeLines : MTLTriangleFillModeFill];

        /* Switch depth state: no Z-write for transparent draws */
        if (cmd->disable_depth_test)
            [ctx.encoder setDepthStencilState:ctx.depthStateDisabled];
        else if (blend_mode != VGFX3D_METAL_BLEND_OPAQUE)
            [ctx.encoder setDepthStencilState:ctx.depthStateNoWrite];
        else
            [ctx.encoder setDepthStencilState:ctx.depthState];
        if (fabsf(cmd->depth_bias) > 1e-8f || fabsf(cmd->slope_scaled_depth_bias) > 1e-8f)
            [ctx.encoder setDepthBias:cmd->depth_bias
                            slopeScale:cmd->slope_scaled_depth_bias
                                 clamp:0.0f];
        else
            [ctx.encoder setDepthBias:0.0f slopeScale:0.0f clamp:0.0f];

        metal_get_geometry_buffers(ctx, cmd, &vb, &ib);
        if (!vb || !ib)
            return;
        [ctx.encoder setVertexBuffer:vb offset:0 atIndex:0];

        /* Retain buffers until frame commit — prevents ARC from releasing
         * them before the GPU executes the draw commands. */
        if (ctx.frameBuffers) {
            [ctx.frameBuffers addObject:vb];
            [ctx.frameBuffers addObject:ib];
        }

        /* Per-object uniforms (includes MTL-09/10 skinning+morph flags) */
        mtl_per_object_t obj;
        memset(&obj, 0, sizeof(obj));
        transpose4x4(cmd->model_matrix, obj.m);
        transpose4x4(cmd->has_prev_model_matrix ? cmd->prev_model_matrix : cmd->model_matrix,
                     obj.prev_m);
        float vp_t[16];
        float normal_m[16];
        int prev_bone_upload_ok = 0;
        metal_morph_bind_status_t morph_status;
        transpose4x4(ctx->_vp, vp_t);
        memcpy(obj.vp, vp_t, sizeof(float) * 16);
        vgfx3d_compute_normal_matrix4(cmd->model_matrix, normal_m);
        transpose4x4(normal_m, obj.nm);
        int has_skinning = (cmd->bone_palette && cmd->bone_count > 0) ? 1 : 0;
        int has_prev_skinning = (has_skinning && cmd->prev_bone_palette) ? 1 : 0;
        has_skinning = metal_bind_bone_palettes(
            ctx, cmd, has_skinning, has_prev_skinning, &prev_bone_upload_ok);
        has_prev_skinning = (has_skinning && has_prev_skinning && prev_bone_upload_ok) ? 1 : 0;
        morph_status = metal_bind_morph_payload(ctx, cmd);
        obj.flags0[0] = has_skinning;
        obj.flags0[1] = has_prev_skinning;
        obj.flags0[2] = morph_status.shape_count;
        obj.flags0[3] = morph_status.shape_count > 0 ? (int32_t)cmd->vertex_count : 0;
        obj.flags1[0] = cmd->has_prev_model_matrix ? 1 : 0;
        obj.flags1[1] = morph_status.has_prev_weights;
        obj.flags1[2] = 0;
        obj.flags1[3] = morph_status.has_normal_deltas;
        [ctx.encoder setVertexBytes:&obj length:sizeof(obj) atIndex:1];

        /* Per-scene (includes MTL-07 fog) */
        mtl_per_scene_t scene;
        memset(&scene, 0, sizeof(scene));
        memcpy(scene.cp, ctx->_camPos, sizeof(float) * 3);
        scene.cp[3] = ctx->_camIsOrtho ? 1.0f : 0.0f;
        scene.ac[0] = ambient[0];
        scene.ac[1] = ambient[1];
        scene.ac[2] = ambient[2];
        scene.fc[0] = ctx->_fogColor[0];
        scene.fc[1] = ctx->_fogColor[1];
        scene.fc[2] = ctx->_fogColor[2];
        scene.fc[3] = ctx->_fogEnabled ? 1.0f : 0.0f;
        scene.fog_params[0] = ctx->_fogNear;
        scene.fog_params[1] = ctx->_fogFar;
        scene.fog_params[2] = ctx->_shadowBias;
        scene.counts[0] = light_count < 0
                              ? 0
                              : (light_count > VGFX3D_MAX_LIGHTS ? VGFX3D_MAX_LIGHTS : light_count);
        /* MTL-12: Shadow mapping */
        scene.counts[1] = ctx->_shadowCount;
        memcpy(scene.camera_forward, ctx->_camForward, sizeof(float) * 3);
        transpose4x4(ctx.frameHistory.draw_prev_vp, scene.prev_vp);
        for (int32_t slot = 0; slot < ctx->_shadowCount && slot < VGFX3D_MAX_SHADOW_LIGHTS; slot++)
            transpose4x4(ctx->_shadowLightVP[slot], scene.shadow_vp[slot]);
        [ctx.encoder setVertexBytes:&scene length:sizeof(scene) atIndex:2];
        [ctx.encoder setFragmentBytes:&scene length:sizeof(scene) atIndex:0];

        /* Per-material (includes MTL-04/05/06 map flags) */
        mtl_per_material_t mat;
        int has_complete_splat = vgfx3d_metal_has_complete_splat(cmd->has_splat,
                                                                 cmd->splat_map != NULL,
                                                                 cmd->splat_layers[0] != NULL,
                                                                 cmd->splat_layers[1] != NULL,
                                                                 cmd->splat_layers[2] != NULL,
                                                                 cmd->splat_layers[3] != NULL);
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
        mat.scalars[0] = cmd->alpha;
        mat.scalars[1] = cmd->reflectivity;
        mat.scalars[2] =
            cmd->env_map ? metal_cubemap_max_lod((const rt_cubemap3d *)cmd->env_map) : 0.0f;
        mat.pbrScalars0[0] = cmd->metallic;
        mat.pbrScalars0[1] = cmd->roughness;
        mat.pbrScalars0[2] = cmd->ao;
        mat.pbrScalars0[3] = cmd->emissive_intensity;
        mat.pbrScalars1[0] = cmd->normal_scale;
        mat.pbrScalars1[1] = cmd->alpha_cutoff;
        mat.flags0[0] = (cmd->texture || cmd->texture_asset) ? 1 : 0;
        mat.flags0[1] = cmd->unlit;
        mat.flags0[2] = (cmd->normal_map || cmd->normal_map_asset) ? 1 : 0;
        mat.flags0[3] = (cmd->specular_map || cmd->specular_map_asset) ? 1 : 0;
        mat.flags1[0] = (cmd->emissive_map || cmd->emissive_map_asset) ? 1 : 0;
        mat.flags1[1] = (cmd->env_map && cmd->reflectivity > 0.0001f) ? 1 : 0;
        mat.flags1[2] = has_complete_splat;
        mat.pbrFlags[0] = cmd->workflow;
        mat.pbrFlags[1] = cmd->alpha_mode;
        mat.pbrFlags[2] =
            (cmd->metallic_roughness_map || cmd->metallic_roughness_map_asset) ? 1 : 0;
        mat.pbrFlags[3] = (cmd->ao_map || cmd->ao_map_asset) ? 1 : 0;
        if (has_complete_splat) {
            for (int si = 0; si < 4; si++)
                mat.splatScales[si] = cmd->splat_layer_scales[si];
        }
        mat.shadingModel = cmd->shading_model;
        memcpy(mat.customParams, cmd->custom_params, sizeof(float) * 8);
        for (int slot = 0; slot < RT_MATERIAL3D_TEXTURE_SLOT_COUNT; slot++) {
            if (slot < 4)
                mat.textureUvSets0[slot] = cmd->texture_slot_uv_set[slot];
            else
                mat.textureUvSets1[slot - 4] = cmd->texture_slot_uv_set[slot];
            mat.textureUvTransform0[slot][0] = cmd->texture_slot_uv_transform[slot][0];
            mat.textureUvTransform0[slot][1] = cmd->texture_slot_uv_transform[slot][1];
            mat.textureUvTransform0[slot][2] = cmd->texture_slot_uv_transform[slot][2];
            mat.textureUvTransform0[slot][3] = cmd->texture_slot_uv_transform[slot][3];
            mat.textureUvTransform1[slot][0] = cmd->texture_slot_uv_transform[slot][4];
            mat.textureUvTransform1[slot][1] = cmd->texture_slot_uv_transform[slot][5];
        }
        [ctx.encoder setFragmentBytes:&mat length:sizeof(mat) atIndex:1];

        /* Bind default textures to all shader slots. */
        for (int slot = 0; slot <= 12; slot++)
            [ctx.encoder setFragmentTexture:ctx.defaultTexture atIndex:slot];
        [ctx.encoder setFragmentTexture:ctx.defaultTexture atIndex:14];
        [ctx.encoder setFragmentTexture:ctx.defaultTexture atIndex:15];
        [ctx.encoder setFragmentTexture:ctx.defaultCubemap atIndex:13];
        for (int32_t slot = 0; slot < ctx->_shadowCount && slot < VGFX3D_MAX_SHADOW_LIGHTS;
             slot++) {
            if (ctx->_shadowDepthTexture[slot])
                [ctx.encoder setFragmentTexture:ctx->_shadowDepthTexture[slot] atIndex:4 + slot];
        }
        [ctx.encoder setFragmentSamplerState:metal_get_material_sampler(
                                                 ctx, cmd, RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR)
                                     atIndex:0];
        if (ctx.shadowSampler)
            [ctx.encoder setFragmentSamplerState:ctx.shadowSampler atIndex:1];
        if (ctx.cubeSampler)
            [ctx.encoder setFragmentSamplerState:ctx.cubeSampler atIndex:2];
        [ctx.encoder setFragmentSamplerState:metal_get_material_sampler(
                                                 ctx, cmd, RT_MATERIAL3D_TEXTURE_SLOT_NORMAL)
                                     atIndex:3];
        [ctx.encoder setFragmentSamplerState:metal_get_material_sampler(
                                                 ctx, cmd, RT_MATERIAL3D_TEXTURE_SLOT_SPECULAR)
                                     atIndex:4];
        [ctx.encoder setFragmentSamplerState:metal_get_material_sampler(
                                                 ctx, cmd, RT_MATERIAL3D_TEXTURE_SLOT_EMISSIVE)
                                     atIndex:5];
        [ctx.encoder
            setFragmentSamplerState:metal_get_material_sampler(
                                        ctx, cmd, RT_MATERIAL3D_TEXTURE_SLOT_METALLIC_ROUGHNESS)
                            atIndex:6];
        [ctx.encoder setFragmentSamplerState:metal_get_material_sampler(
                                                 ctx, cmd, RT_MATERIAL3D_TEXTURE_SLOT_AO)
                                     atIndex:7];

        /* MTL-03: Bind cached textures for each material map slot */
        if (cmd->texture || cmd->texture_asset) {
            id<MTLTexture> tex =
                metal_get_material_texture(ctx,
                                           cmd->texture_asset,
                                           cmd->texture,
                                           cmd->texture_asset_cache_key
                                               [RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR],
                                           cmd->texture_asset_mip_start
                                               [RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR],
                                           cmd->texture_asset_mip_count
                                               [RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR]);
            if (tex)
                [ctx.encoder setFragmentTexture:tex atIndex:0];
        }
        if (cmd->normal_map || cmd->normal_map_asset) {
            id<MTLTexture> tex =
                metal_get_material_texture(ctx,
                                           cmd->normal_map_asset,
                                           cmd->normal_map,
                                           cmd->texture_asset_cache_key
                                               [RT_MATERIAL3D_TEXTURE_SLOT_NORMAL],
                                           cmd->texture_asset_mip_start
                                               [RT_MATERIAL3D_TEXTURE_SLOT_NORMAL],
                                           cmd->texture_asset_mip_count
                                               [RT_MATERIAL3D_TEXTURE_SLOT_NORMAL]);
            if (tex)
                [ctx.encoder setFragmentTexture:tex atIndex:1];
        }
        if (cmd->specular_map || cmd->specular_map_asset) {
            id<MTLTexture> tex =
                metal_get_material_texture(ctx,
                                           cmd->specular_map_asset,
                                           cmd->specular_map,
                                           cmd->texture_asset_cache_key
                                               [RT_MATERIAL3D_TEXTURE_SLOT_SPECULAR],
                                           cmd->texture_asset_mip_start
                                               [RT_MATERIAL3D_TEXTURE_SLOT_SPECULAR],
                                           cmd->texture_asset_mip_count
                                               [RT_MATERIAL3D_TEXTURE_SLOT_SPECULAR]);
            if (tex)
                [ctx.encoder setFragmentTexture:tex atIndex:2];
        }
        if (cmd->emissive_map || cmd->emissive_map_asset) {
            id<MTLTexture> tex =
                metal_get_material_texture(ctx,
                                           cmd->emissive_map_asset,
                                           cmd->emissive_map,
                                           cmd->texture_asset_cache_key
                                               [RT_MATERIAL3D_TEXTURE_SLOT_EMISSIVE],
                                           cmd->texture_asset_mip_start
                                               [RT_MATERIAL3D_TEXTURE_SLOT_EMISSIVE],
                                           cmd->texture_asset_mip_count
                                               [RT_MATERIAL3D_TEXTURE_SLOT_EMISSIVE]);
            if (tex)
                [ctx.encoder setFragmentTexture:tex atIndex:3];
        }
        if (cmd->metallic_roughness_map || cmd->metallic_roughness_map_asset) {
            id<MTLTexture> tex = metal_get_material_texture(
                ctx,
                cmd->metallic_roughness_map_asset,
                cmd->metallic_roughness_map,
                cmd->texture_asset_cache_key[RT_MATERIAL3D_TEXTURE_SLOT_METALLIC_ROUGHNESS],
                cmd->texture_asset_mip_start[RT_MATERIAL3D_TEXTURE_SLOT_METALLIC_ROUGHNESS],
                cmd->texture_asset_mip_count[RT_MATERIAL3D_TEXTURE_SLOT_METALLIC_ROUGHNESS]);
            if (tex)
                [ctx.encoder setFragmentTexture:tex atIndex:14];
        }
        if (cmd->ao_map || cmd->ao_map_asset) {
            id<MTLTexture> tex = metal_get_material_texture(ctx,
                                                            cmd->ao_map_asset,
                                                            cmd->ao_map,
                                                            cmd->texture_asset_cache_key
                                                                [RT_MATERIAL3D_TEXTURE_SLOT_AO],
                                                            cmd->texture_asset_mip_start
                                                                [RT_MATERIAL3D_TEXTURE_SLOT_AO],
                                                            cmd->texture_asset_mip_count
                                                                [RT_MATERIAL3D_TEXTURE_SLOT_AO]);
            if (tex)
                [ctx.encoder setFragmentTexture:tex atIndex:15];
        }
        /* MTL-14: Terrain splat textures (slots 8-12) */
        if (has_complete_splat) {
            id<MTLTexture> sp = metal_get_cached_texture(ctx, cmd->splat_map);
            if (sp)
                [ctx.encoder setFragmentTexture:sp atIndex:8];
            for (int si = 0; si < 4; si++) {
                if (cmd->splat_layers[si]) {
                    id<MTLTexture> lt = metal_get_cached_texture(ctx, cmd->splat_layers[si]);
                    if (lt)
                        [ctx.encoder setFragmentTexture:lt atIndex:9 + si];
                }
            }
        }
        if (cmd->env_map && cmd->reflectivity > 0.0001f) {
            id<MTLTexture> envTex =
                metal_get_cached_cubemap(ctx, (const rt_cubemap3d *)cmd->env_map);
            if (envTex)
                [ctx.encoder setFragmentTexture:envTex atIndex:13];
        }

        /* Lights — always set buffer 2, even if empty (prevents validation warnings) */
        {
            mtl_light_t ml[VGFX3D_MAX_LIGHTS];
            memset(ml, 0, sizeof(ml));
            for (int32_t i = 0; i < light_count && i < VGFX3D_MAX_LIGHTS; i++) {
                ml[i].type = lights[i].type;
                ml[i].shadow_index =
                    vgfx3d_metal_sanitize_shadow_index(lights[i].shadow_index, ctx->_shadowCount);
                ml[i].shadow_cascade_count =
                    ml[i].shadow_index >= 0 ? lights[i].shadow_cascade_count : 1;
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
                memcpy(ml[i].shadow_cascade_splits,
                       lights[i].shadow_cascade_splits,
                       sizeof(ml[i].shadow_cascade_splits));
            }
            int32_t buf_count =
                light_count > 0
                    ? (light_count > VGFX3D_MAX_LIGHTS ? VGFX3D_MAX_LIGHTS : light_count)
                    : 1;
            [ctx.encoder setFragmentBytes:ml length:sizeof(mtl_light_t) * buf_count atIndex:2];
        }

        [ctx.encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                indexCount:cmd->index_count
                                 indexType:MTLIndexTypeUInt32
                               indexBuffer:ib
                         indexBufferOffset:0];
    }
}

/// @brief End the frame: close the scene encoder, run GPU post-FX, present, and commit.
static void metal_end_frame(void *ctx_ptr) {
    @autoreleasepool {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
        VGFXMetalRenderTargetCacheEntry *rt_entry = nil;
        id<MTLCommandBuffer> submitted = nil;
        if (!ctx || !ctx.encoder || !ctx.inFrame)
            return;

        [ctx.encoder endEncoding];

        if (ctx.rttActive && ctx.rttTarget && ctx.rttColorTexture) {
            rt_entry = metal_lookup_render_target_entry(ctx, ctx.rttTarget);
            submitted = ctx.cmdBuf;
            if (rt_entry)
                rt_entry.pendingCommandBuffer = submitted;
            metal_commit_pending(ctx, NO);
            ctx.rttTarget->sync_color = metal_sync_render_target_color;
            ctx.rttTarget->sync_color_userdata = (__bridge void *)ctx;
            ctx.rttTarget->color_dirty = 1;
            ctx.rttTarget->hdr_color_valid = 0;
        }

        ctx.encoder = nil;
        ctx.inFrame = NO;
    }
}

/// @brief Read the total bytes uploaded to Metal textures so far this frame (diagnostics counter).
static uint64_t metal_get_texture_upload_bytes(void *ctx_ptr) {
    VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
    return ctx ? ctx.textureUploadBytes : 0;
}

/// @brief Present the most recent on-screen drawable. Called from Flip().
/// Commits the frame's command buffer (which contains all render passes)
/// and presents the drawable to the display in one atomic operation.
static void metal_present(void *backend_ctx) {
    if (!backend_ctx)
        return;
    @autoreleasepool {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)backend_ctx;
        id<MTLTexture> final_texture = nil;

        if (!ctx || ctx.rttActive)
            return;

        if (!ctx.gpuPostfxEnabled && ctx.currentTargetKind == VGFX3D_METAL_TARGET_SWAPCHAIN &&
            ctx.cmdBuf && ctx.drawable) {
            ctx.displayTexture = ctx.drawable.texture;
            [ctx.cmdBuf presentDrawable:ctx.drawable];
            metal_commit_pending(ctx, NO);
            return;
        }
        if (ctx.postfxCompositedToDrawable && ctx.cmdBuf && ctx.drawable) {
            (void)metal_capture_current_drawable_to_display_texture(ctx);
            [ctx.cmdBuf presentDrawable:ctx.drawable];
            metal_commit_pending(ctx, NO);
            ctx.postfxCompositedToDrawable = 0;
            return;
        }

        final_texture = metal_active_readback_texture(ctx);
        if (!final_texture)
            final_texture = ctx.offscreenColor;
        if (metal_present_texture(ctx, final_texture, YES))
            metal_commit_pending(ctx, NO);
        else {
            metal_commit_pending(ctx, YES);
            metal_present_texture_to_framebuffer(ctx, final_texture);
        }
    }
}

/// @brief Handle a window resize: update the layer size and recreate the main render targets.
static void metal_resize(void *ctx_ptr, int32_t w, int32_t h) {
    if (!ctx_ptr || w <= 0 || h <= 0)
        return;
    @autoreleasepool {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
        if (!ctx || (ctx.width == w && ctx.height == h))
            return;
        metal_commit_pending(ctx, YES);
        ctx.width = w;
        ctx.height = h;
        metal_update_layer_size(ctx);
        metal_recreate_main_targets(ctx, w, h);
    }
}

/// @brief Read the current frame's rendered image back into a CPU RGBA buffer (for screenshots).
/// @return 1 on success, 0 if no readable texture is available.
static int metal_readback_rgba(
    void *ctx_ptr, uint8_t *dst_rgba, int32_t w, int32_t h, int32_t stride) {
    if (!ctx_ptr)
        return 0;
    @autoreleasepool {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
        id<MTLTexture> texture = nil;
        vgfx3d_postfx_chain_t chain_copy = {0};
        const vgfx3d_postfx_chain_t *chain = NULL;
        vgfx3d_metal_readback_kind_t readback_kind;
        if (!ctx || !dst_rgba || w <= 0 || h <= 0 || stride <= 0)
            return 0;
        if ((size_t)w > SIZE_MAX / 4u || (size_t)stride < (size_t)w * 4u)
            return 0;

        readback_kind = vgfx3d_metal_choose_readback_kind(
            (ctx.gpuPostfxEnabled && ctx.gpuPostfxChainValid) ? 1 : 0);
        if (!ctx.rttActive && ctx.cmdBuf) {
            if (readback_kind == VGFX3D_METAL_READBACK_POSTFX_COMPOSITE) {
                if (ctx.gpuPostfxChainValid) {
                    vgfx3d_postfx_chain_t stored_chain = ctx.gpuPostfxChain;
                    if (vgfx3d_postfx_chain_copy(&chain_copy, &stored_chain))
                        chain = &chain_copy;
                }
                texture = metal_encode_postfx_if_needed(ctx, chain);
            } else if (ctx.currentTargetKind == VGFX3D_METAL_TARGET_SWAPCHAIN &&
                       !metal_capture_current_drawable_to_display_texture(ctx)) {
                return 0;
            }
            metal_commit_pending(ctx, YES);
        }
        texture = metal_active_readback_texture(ctx);
        {
            const int ok = metal_copy_texture_to_rgba(ctx, texture, dst_rgba, w, h, stride, NULL);
            vgfx3d_postfx_chain_free(&chain_copy);
            return ok;
        }
    }
}

//=============================================================================
// Exported backend
//=============================================================================

/// @brief Bind or unbind an offscreen render target for Metal GPU rendering.
/// When rt is non-NULL, subsequent begin_frame/submit_draw/end_frame calls render
/// to GPU textures instead of the on-screen CAMetalLayer drawable. When rt is NULL,
/// rendering reverts to the screen. RTT color stays GPU-owned until the runtime
/// explicitly asks for CPU pixels through the lazy sync_color hook.
static void metal_set_render_target(void *ctx_ptr, vgfx3d_rendertarget_t *rt) {
    @autoreleasepool {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
        VGFXMetalRenderTargetCacheEntry *entry;
        if (!ctx)
            return;

        if (!rt) {
            /* Disable RTT — revert to on-screen rendering */
            metal_detach_render_target(ctx, YES);
            return;
        }
        if (ctx.rttTarget && ctx.rttTarget != rt)
            metal_detach_render_target(ctx, YES);

        entry = metal_ensure_render_target_entry(ctx, rt);
        if (!entry)
            return;

        ctx.rttTarget = rt;
        ctx.rttWidth = rt->width;
        ctx.rttHeight = rt->height;
        ctx.rttColorTexture = entry.colorTexture;
        ctx.rttMotionTexture = entry.motionTexture;
        ctx.rttDepthTexture = entry.depthTexture;
        ctx.displayTexture = nil;
        ctx.postfxEncodedThisFrame = 0;
        ctx.postfxCompositedToDrawable = 0;
        ctx.rttActive = YES;
    }
}

//=============================================================================
// MTL-12: Shadow mapping
//=============================================================================

/// @brief Backend vtable entry: begin the shadow-map depth pass for shadow-casting light @p slot,
///   rendering depth from the light's @p light_vp view-projection into a @p w×@p h Depth32Float
///   texture (created or resized as needed).
/// @details The CPU @p depth_buf is ignored — the GPU path stores depth in an MTLTexture. No-op for
///   an out-of-range @p slot, or when the device/shadow pipeline/@p light_vp is unavailable or the
///   dimensions are non-positive.
static void metal_shadow_begin(
    void *ctx_ptr, int32_t slot, float *depth_buf, int32_t w, int32_t h, const float *light_vp) {
    @autoreleasepool {
        (void)depth_buf; /* GPU shadows use MTLTexture, not CPU buffer */
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
        if (!ctx)
            return;
        ctx->_shadowPassSlot = -1;
        if (slot < 0 || slot >= VGFX3D_MAX_SHADOW_LIGHTS)
            return;
        ctx->_shadowComplete[slot] = 0;
        metal_recompute_shadow_count(ctx);
        if (!ctx.device || !ctx.shadowPipeline || !light_vp || w <= 0 || h <= 0)
            return;

        /* Create/recreate shadow depth texture if size changed */
        if (!ctx->_shadowDepthTexture[slot] || (int32_t)ctx->_shadowDepthTexture[slot].width != w ||
            (int32_t)ctx->_shadowDepthTexture[slot].height != h) {
            MTLTextureDescriptor *desc =
                [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                   width:(NSUInteger)w
                                                                  height:(NSUInteger)h
                                                               mipmapped:NO];
            desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            desc.storageMode = MTLStorageModePrivate;
            ctx->_shadowDepthTexture[slot] = [ctx.device newTextureWithDescriptor:desc];
        }
        if (!ctx->_shadowDepthTexture[slot])
            return;

        /* Store light VP for main pass uniform */
        memcpy(ctx->_shadowLightVP[slot], light_vp, 16 * sizeof(float));

        /* Start shadow render pass */
        if (!ctx.cmdBuf)
            ctx.cmdBuf = [ctx.commandQueue commandBuffer];
        if (ctx.encoder) {
            [ctx.encoder endEncoding];
            ctx.encoder = nil;
        }

        MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.depthAttachment.texture = ctx->_shadowDepthTexture[slot];
        rp.depthAttachment.loadAction = MTLLoadActionClear;
        rp.depthAttachment.storeAction = MTLStoreActionStore;
        rp.depthAttachment.clearDepth = 1.0;

        ctx.encoder = [ctx.cmdBuf renderCommandEncoderWithDescriptor:rp];
        if (!ctx.encoder) {
            ctx->_shadowPassSlot = -1;
            return;
        }
        ctx->_shadowPassSlot = slot;
        [ctx.encoder setRenderPipelineState:ctx.shadowPipeline];
        [ctx.encoder setDepthStencilState:ctx.shadowDepthState];
        MTLViewport vp = {0, 0, (double)w, (double)h, 0.0, 1.0};
        [ctx.encoder setViewport:vp];
        [ctx.encoder setFrontFacingWinding:MTLWindingCounterClockwise];
        [ctx.encoder setCullMode:MTLCullModeBack];
        [ctx.encoder setDepthBias:0.0f slopeScale:0.0f clamp:0.0f];
    }
}

/// @brief Encode a draw into the active shadow-map depth pass (depth-only, from the light's view).
static void metal_shadow_draw(void *ctx_ptr, const vgfx3d_draw_cmd_t *cmd) {
    @autoreleasepool {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
        id<MTLBuffer> vb;
        id<MTLBuffer> ib;
        int has_skinning;
        int prev_bone_upload_ok = 0;
        metal_morph_bind_status_t morph_status;
        if (!ctx || !ctx.encoder || !cmd || !cmd->vertices || !cmd->indices ||
            cmd->vertex_count == 0 || cmd->index_count == 0 || ctx->_shadowPassSlot < 0 ||
            ctx->_shadowPassSlot >= VGFX3D_MAX_SHADOW_LIGHTS)
            return;
        [ctx.encoder setCullMode:cmd->double_sided ? MTLCullModeNone : MTLCullModeBack];
        if (fabsf(cmd->depth_bias) > 1e-8f || fabsf(cmd->slope_scaled_depth_bias) > 1e-8f)
            [ctx.encoder setDepthBias:cmd->depth_bias
                            slopeScale:cmd->slope_scaled_depth_bias
                                 clamp:0.0f];
        else
            [ctx.encoder setDepthBias:0.0f slopeScale:0.0f clamp:0.0f];

        metal_get_geometry_buffers(ctx, cmd, &vb, &ib);
        if (!vb || !ib)
            return;
        [ctx.encoder setVertexBuffer:vb offset:0 atIndex:0];
        if (ctx.frameBuffers) {
            [ctx.frameBuffers addObject:vb];
            [ctx.frameBuffers addObject:ib];
        }

        /* Per-object: transform by light VP */
        mtl_per_object_t obj;
        memset(&obj, 0, sizeof(obj));
        transpose4x4(cmd->model_matrix, obj.m);
        transpose4x4(cmd->model_matrix, obj.prev_m);
        float lvp_t[16];
        transpose4x4(ctx->_shadowLightVP[ctx->_shadowPassSlot], lvp_t);
        memcpy(obj.vp, lvp_t, sizeof(float) * 16);
        memcpy(obj.nm, obj.m, sizeof(float) * 16);
        has_skinning = (cmd->bone_palette && cmd->bone_count > 0) ? 1 : 0;
        has_skinning = metal_bind_bone_palettes(ctx, cmd, has_skinning, 0, &prev_bone_upload_ok);
        morph_status = metal_bind_morph_payload(ctx, cmd);
        obj.flags0[0] = has_skinning;
        obj.flags0[2] = morph_status.shape_count;
        obj.flags0[3] = morph_status.shape_count > 0 ? (int32_t)cmd->vertex_count : 0;
        [ctx.encoder setVertexBytes:&obj length:sizeof(obj) atIndex:1];
        {
            mtl_per_material_t mat;
            id<MTLTexture> diffuse_tex =
                metal_get_material_texture(ctx,
                                           cmd->texture_asset,
                                           cmd->texture,
                                           cmd->texture_asset_cache_key
                                               [RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR],
                                           cmd->texture_asset_mip_start
                                               [RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR],
                                           cmd->texture_asset_mip_count
                                               [RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR]);
            memset(&mat, 0, sizeof(mat));
            memcpy(mat.dc, cmd->diffuse_color, sizeof(float) * 4);
            mat.scalars[0] = cmd->alpha;
            mat.pbrScalars1[1] = cmd->alpha_cutoff;
            mat.flags0[0] = diffuse_tex ? 1 : 0;
            mat.pbrFlags[1] = cmd->alpha_mode;
            for (int tex_slot = 0; tex_slot < RT_MATERIAL3D_TEXTURE_SLOT_COUNT; tex_slot++) {
                if (tex_slot < 4)
                    mat.textureUvSets0[tex_slot] = cmd->texture_slot_uv_set[tex_slot];
                else
                    mat.textureUvSets1[tex_slot - 4] = cmd->texture_slot_uv_set[tex_slot];
                mat.textureUvTransform0[tex_slot][0] = cmd->texture_slot_uv_transform[tex_slot][0];
                mat.textureUvTransform0[tex_slot][1] = cmd->texture_slot_uv_transform[tex_slot][1];
                mat.textureUvTransform0[tex_slot][2] = cmd->texture_slot_uv_transform[tex_slot][2];
                mat.textureUvTransform0[tex_slot][3] = cmd->texture_slot_uv_transform[tex_slot][3];
                mat.textureUvTransform1[tex_slot][0] = cmd->texture_slot_uv_transform[tex_slot][4];
                mat.textureUvTransform1[tex_slot][1] = cmd->texture_slot_uv_transform[tex_slot][5];
            }
            [ctx.encoder setFragmentBytes:&mat length:sizeof(mat) atIndex:1];
            [ctx.encoder setFragmentTexture:diffuse_tex ? diffuse_tex : ctx.defaultTexture
                                    atIndex:0];
            [ctx.encoder
                setFragmentSamplerState:metal_get_material_sampler(
                                            ctx, cmd, RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR)
                                atIndex:0];
        }

        [ctx.encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                indexCount:cmd->index_count
                                 indexType:MTLIndexTypeUInt32
                               indexBuffer:ib
                         indexBufferOffset:0];
    }
}

/// @brief Finish the shadow-map pass for @p slot, storing its depth texture, light VP, and depth
/// bias.
/// @details Marks the slot complete so the lighting pass can sample it; @p bias offsets depth
///          comparisons to reduce shadow acne.
static void metal_shadow_end(void *ctx_ptr, int32_t slot, float bias) {
    @autoreleasepool {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
        if (!ctx || !ctx.encoder || slot < 0 || slot >= VGFX3D_MAX_SHADOW_LIGHTS)
            return;
        if (ctx->_shadowPassSlot != slot)
            return;
        [ctx.encoder endEncoding];
        ctx.encoder = nil;
        ctx->_shadowComplete[slot] = ctx->_shadowDepthTexture[slot] ? 1 : 0;
        ctx->_shadowPassSlot = -1;
        ctx->_shadowBias = bias;
        metal_recompute_shadow_count(ctx);
        metal_begin_scene_encoder(ctx, YES, YES);
    }
}

//=============================================================================
// MTL-13: Instanced rendering
//=============================================================================

/// @brief Backend vtable entry: draw @p instance_count copies of the mesh in @p cmd in one instanced
///   call, each positioned by a 4x4 transform from @p instance_matrices and lit by @p lights /
///   @p ambient. Honors @p wireframe and @p backface_cull.
/// @details No-op outside an active frame, on empty geometry, or with no instances. NULL @p ambient
///   defaults to black and a negative @p light_count is treated as 0.
static void metal_submit_draw_instanced(void *ctx_ptr,
                                        vgfx_window_t win,
                                        const vgfx3d_draw_cmd_t *cmd,
                                        const float *instance_matrices,
                                        int32_t instance_count,
                                        const vgfx3d_light_params_t *lights,
                                        int32_t light_count,
                                        const float *ambient,
                                        int8_t wireframe,
                                        int8_t backface_cull) {
    @autoreleasepool {
        (void)win;
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
        vgfx3d_metal_instance_data_t *instance_data;
        id<MTLBuffer> vb;
        id<MTLBuffer> ib;
        vgfx3d_metal_blend_mode_t blend_mode;
        static const float zero_ambient[3] = {0.0f, 0.0f, 0.0f};
        if (!ctx || !ctx.encoder || !ctx.inFrame || !cmd || !cmd->vertices || !cmd->indices ||
            cmd->vertex_count == 0 || cmd->index_count == 0 || !instance_matrices ||
            instance_count <= 0)
            return;
        if (!ambient)
            ambient = zero_ambient;
        if (!lights || light_count < 0)
            light_count = 0;

        metal_ensure_instance_storage(ctx, instance_count);
        if (!ctx.instanceScratch || !ctx.instanceBuf)
            return;
        instance_data = (vgfx3d_metal_instance_data_t *)ctx.instanceScratch.mutableBytes;
        if (!instance_data)
            return;
        vgfx3d_metal_fill_instance_data(instance_data,
                                        instance_count,
                                        instance_matrices,
                                        cmd->prev_instance_matrices,
                                        cmd->has_prev_instance_matrices ? 1 : 0);
        memcpy(ctx.instanceBuf.contents,
               instance_data,
               (size_t)instance_count * sizeof(vgfx3d_metal_instance_data_t));

        blend_mode = vgfx3d_metal_choose_blend_mode(cmd);
        [ctx.encoder setRenderPipelineState:metal_select_pipeline_state(ctx, cmd, YES)];
        [ctx.encoder setCullMode:backface_cull ? MTLCullModeBack : MTLCullModeNone];
        [ctx.encoder
            setTriangleFillMode:wireframe ? MTLTriangleFillModeLines : MTLTriangleFillModeFill];
        if (cmd->disable_depth_test)
            [ctx.encoder setDepthStencilState:ctx.depthStateDisabled];
        else if (blend_mode != VGFX3D_METAL_BLEND_OPAQUE)
            [ctx.encoder setDepthStencilState:ctx.depthStateNoWrite];
        else
            [ctx.encoder setDepthStencilState:ctx.depthState];
        if (fabsf(cmd->depth_bias) > 1e-8f || fabsf(cmd->slope_scaled_depth_bias) > 1e-8f)
            [ctx.encoder setDepthBias:cmd->depth_bias
                            slopeScale:cmd->slope_scaled_depth_bias
                                 clamp:0.0f];
        else
            [ctx.encoder setDepthBias:0.0f slopeScale:0.0f clamp:0.0f];

        /* Vertex/index buffers */
        metal_get_geometry_buffers(ctx, cmd, &vb, &ib);
        if (!vb || !ib || !ctx.instanceBuf)
            return;
        [ctx.encoder setVertexBuffer:vb offset:0 atIndex:0];
        [ctx.encoder setVertexBuffer:ctx.instanceBuf offset:0 atIndex:6];
        if (ctx.frameBuffers) {
            [ctx.frameBuffers addObject:vb];
            [ctx.frameBuffers addObject:ib];
        }

        /* Per-object + per-scene + per-material + lights + textures */
        mtl_per_object_t obj;
        memset(&obj, 0, sizeof(obj));
        float vp_t[16];
        int prev_bone_upload_ok = 0;
        metal_morph_bind_status_t morph_status;
        int has_skinning = (cmd->bone_palette && cmd->bone_count > 0) ? 1 : 0;
        int has_prev_skinning = (has_skinning && cmd->prev_bone_palette) ? 1 : 0;
        has_skinning = metal_bind_bone_palettes(
            ctx, cmd, has_skinning, has_prev_skinning, &prev_bone_upload_ok);
        has_prev_skinning = (has_skinning && has_prev_skinning && prev_bone_upload_ok) ? 1 : 0;
        morph_status = metal_bind_morph_payload(ctx, cmd);
        mat4f_identity(obj.m);
        mat4f_identity(obj.prev_m);
        transpose4x4(ctx->_vp, vp_t);
        memcpy(obj.vp, vp_t, sizeof(float) * 16);
        mat4f_identity(obj.nm);
        obj.flags0[0] = has_skinning;
        obj.flags0[1] = has_prev_skinning;
        obj.flags0[2] = morph_status.shape_count;
        obj.flags0[3] = morph_status.shape_count > 0 ? (int32_t)cmd->vertex_count : 0;
        obj.flags1[0] = 0;
        obj.flags1[1] = morph_status.has_prev_weights;
        obj.flags1[2] = cmd->has_prev_instance_matrices ? 1 : 0;
        obj.flags1[3] = morph_status.has_normal_deltas;
        [ctx.encoder setVertexBytes:&obj length:sizeof(obj) atIndex:1];

        mtl_per_scene_t scene;
        memset(&scene, 0, sizeof(scene));
        memcpy(scene.cp, ctx->_camPos, sizeof(float) * 3);
        scene.cp[3] = ctx->_camIsOrtho ? 1.0f : 0.0f;
        scene.ac[0] = ambient[0];
        scene.ac[1] = ambient[1];
        scene.ac[2] = ambient[2];
        scene.fc[0] = ctx->_fogColor[0];
        scene.fc[1] = ctx->_fogColor[1];
        scene.fc[2] = ctx->_fogColor[2];
        scene.fc[3] = ctx->_fogEnabled ? 1.0f : 0.0f;
        scene.fog_params[0] = ctx->_fogNear;
        scene.fog_params[1] = ctx->_fogFar;
        scene.fog_params[2] = ctx->_shadowBias;
        scene.counts[0] = light_count < 0
                              ? 0
                              : (light_count > VGFX3D_MAX_LIGHTS ? VGFX3D_MAX_LIGHTS : light_count);
        scene.counts[1] = ctx->_shadowCount;
        memcpy(scene.camera_forward, ctx->_camForward, sizeof(float) * 3);
        transpose4x4(ctx.frameHistory.draw_prev_vp, scene.prev_vp);
        for (int32_t slot = 0; slot < ctx->_shadowCount && slot < VGFX3D_MAX_SHADOW_LIGHTS; slot++)
            transpose4x4(ctx->_shadowLightVP[slot], scene.shadow_vp[slot]);
        [ctx.encoder setVertexBytes:&scene length:sizeof(scene) atIndex:2];
        [ctx.encoder setFragmentBytes:&scene length:sizeof(scene) atIndex:0];

        mtl_per_material_t mat;
        int has_complete_splat = vgfx3d_metal_has_complete_splat(cmd->has_splat,
                                                                 cmd->splat_map != NULL,
                                                                 cmd->splat_layers[0] != NULL,
                                                                 cmd->splat_layers[1] != NULL,
                                                                 cmd->splat_layers[2] != NULL,
                                                                 cmd->splat_layers[3] != NULL);
        memset(&mat, 0, sizeof(mat));
        memcpy(mat.dc, cmd->diffuse_color, sizeof(float) * 4);
        mat.sc[0] = cmd->specular[0];
        mat.sc[1] = cmd->specular[1];
        mat.sc[2] = cmd->specular[2];
        mat.sc[3] = cmd->shininess;
        mat.ec[0] = cmd->emissive_color[0];
        mat.ec[1] = cmd->emissive_color[1];
        mat.ec[2] = cmd->emissive_color[2];
        mat.scalars[0] = cmd->alpha;
        mat.scalars[1] = cmd->reflectivity;
        mat.scalars[2] =
            cmd->env_map ? metal_cubemap_max_lod((const rt_cubemap3d *)cmd->env_map) : 0.0f;
        mat.pbrScalars0[0] = cmd->metallic;
        mat.pbrScalars0[1] = cmd->roughness;
        mat.pbrScalars0[2] = cmd->ao;
        mat.pbrScalars0[3] = cmd->emissive_intensity;
        mat.pbrScalars1[0] = cmd->normal_scale;
        mat.pbrScalars1[1] = cmd->alpha_cutoff;
        mat.flags0[0] = (cmd->texture || cmd->texture_asset) ? 1 : 0;
        mat.flags0[1] = cmd->unlit;
        mat.flags0[2] = (cmd->normal_map || cmd->normal_map_asset) ? 1 : 0;
        mat.flags0[3] = (cmd->specular_map || cmd->specular_map_asset) ? 1 : 0;
        mat.flags1[0] = (cmd->emissive_map || cmd->emissive_map_asset) ? 1 : 0;
        mat.flags1[1] = (cmd->env_map && cmd->reflectivity > 0.0001f) ? 1 : 0;
        mat.flags1[2] = has_complete_splat;
        mat.pbrFlags[0] = cmd->workflow;
        mat.pbrFlags[1] = cmd->alpha_mode;
        mat.pbrFlags[2] =
            (cmd->metallic_roughness_map || cmd->metallic_roughness_map_asset) ? 1 : 0;
        mat.pbrFlags[3] = (cmd->ao_map || cmd->ao_map_asset) ? 1 : 0;
        if (has_complete_splat) {
            for (int si = 0; si < 4; si++)
                mat.splatScales[si] = cmd->splat_layer_scales[si];
        }
        mat.shadingModel = cmd->shading_model;
        memcpy(mat.customParams, cmd->custom_params, sizeof(float) * 8);
        for (int slot = 0; slot < RT_MATERIAL3D_TEXTURE_SLOT_COUNT; slot++) {
            if (slot < 4)
                mat.textureUvSets0[slot] = cmd->texture_slot_uv_set[slot];
            else
                mat.textureUvSets1[slot - 4] = cmd->texture_slot_uv_set[slot];
            mat.textureUvTransform0[slot][0] = cmd->texture_slot_uv_transform[slot][0];
            mat.textureUvTransform0[slot][1] = cmd->texture_slot_uv_transform[slot][1];
            mat.textureUvTransform0[slot][2] = cmd->texture_slot_uv_transform[slot][2];
            mat.textureUvTransform0[slot][3] = cmd->texture_slot_uv_transform[slot][3];
            mat.textureUvTransform1[slot][0] = cmd->texture_slot_uv_transform[slot][4];
            mat.textureUvTransform1[slot][1] = cmd->texture_slot_uv_transform[slot][5];
        }
        [ctx.encoder setFragmentBytes:&mat length:sizeof(mat) atIndex:1];

        /* Default textures + sampler */
        for (int slot = 0; slot <= 12; slot++)
            [ctx.encoder setFragmentTexture:ctx.defaultTexture atIndex:slot];
        [ctx.encoder setFragmentTexture:ctx.defaultTexture atIndex:14];
        [ctx.encoder setFragmentTexture:ctx.defaultTexture atIndex:15];
        [ctx.encoder setFragmentTexture:ctx.defaultCubemap atIndex:13];
        for (int32_t slot = 0; slot < ctx->_shadowCount && slot < VGFX3D_MAX_SHADOW_LIGHTS;
             slot++) {
            if (ctx->_shadowDepthTexture[slot])
                [ctx.encoder setFragmentTexture:ctx->_shadowDepthTexture[slot] atIndex:4 + slot];
        }
        [ctx.encoder setFragmentSamplerState:metal_get_material_sampler(
                                                 ctx, cmd, RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR)
                                     atIndex:0];
        if (ctx.shadowSampler)
            [ctx.encoder setFragmentSamplerState:ctx.shadowSampler atIndex:1];
        if (ctx.cubeSampler)
            [ctx.encoder setFragmentSamplerState:ctx.cubeSampler atIndex:2];
        [ctx.encoder setFragmentSamplerState:metal_get_material_sampler(
                                                 ctx, cmd, RT_MATERIAL3D_TEXTURE_SLOT_NORMAL)
                                     atIndex:3];
        [ctx.encoder setFragmentSamplerState:metal_get_material_sampler(
                                                 ctx, cmd, RT_MATERIAL3D_TEXTURE_SLOT_SPECULAR)
                                     atIndex:4];
        [ctx.encoder setFragmentSamplerState:metal_get_material_sampler(
                                                 ctx, cmd, RT_MATERIAL3D_TEXTURE_SLOT_EMISSIVE)
                                     atIndex:5];
        [ctx.encoder
            setFragmentSamplerState:metal_get_material_sampler(
                                        ctx, cmd, RT_MATERIAL3D_TEXTURE_SLOT_METALLIC_ROUGHNESS)
                            atIndex:6];
        [ctx.encoder setFragmentSamplerState:metal_get_material_sampler(
                                                 ctx, cmd, RT_MATERIAL3D_TEXTURE_SLOT_AO)
                                     atIndex:7];
        if (cmd->texture || cmd->texture_asset) {
            id<MTLTexture> tex =
                metal_get_material_texture(ctx,
                                           cmd->texture_asset,
                                           cmd->texture,
                                           cmd->texture_asset_cache_key
                                               [RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR],
                                           cmd->texture_asset_mip_start
                                               [RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR],
                                           cmd->texture_asset_mip_count
                                               [RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR]);
            if (tex)
                [ctx.encoder setFragmentTexture:tex atIndex:0];
        }
        if (cmd->normal_map || cmd->normal_map_asset) {
            id<MTLTexture> tex =
                metal_get_material_texture(ctx,
                                           cmd->normal_map_asset,
                                           cmd->normal_map,
                                           cmd->texture_asset_cache_key
                                               [RT_MATERIAL3D_TEXTURE_SLOT_NORMAL],
                                           cmd->texture_asset_mip_start
                                               [RT_MATERIAL3D_TEXTURE_SLOT_NORMAL],
                                           cmd->texture_asset_mip_count
                                               [RT_MATERIAL3D_TEXTURE_SLOT_NORMAL]);
            if (tex)
                [ctx.encoder setFragmentTexture:tex atIndex:1];
        }
        if (cmd->specular_map || cmd->specular_map_asset) {
            id<MTLTexture> tex =
                metal_get_material_texture(ctx,
                                           cmd->specular_map_asset,
                                           cmd->specular_map,
                                           cmd->texture_asset_cache_key
                                               [RT_MATERIAL3D_TEXTURE_SLOT_SPECULAR],
                                           cmd->texture_asset_mip_start
                                               [RT_MATERIAL3D_TEXTURE_SLOT_SPECULAR],
                                           cmd->texture_asset_mip_count
                                               [RT_MATERIAL3D_TEXTURE_SLOT_SPECULAR]);
            if (tex)
                [ctx.encoder setFragmentTexture:tex atIndex:2];
        }
        if (cmd->emissive_map || cmd->emissive_map_asset) {
            id<MTLTexture> tex =
                metal_get_material_texture(ctx,
                                           cmd->emissive_map_asset,
                                           cmd->emissive_map,
                                           cmd->texture_asset_cache_key
                                               [RT_MATERIAL3D_TEXTURE_SLOT_EMISSIVE],
                                           cmd->texture_asset_mip_start
                                               [RT_MATERIAL3D_TEXTURE_SLOT_EMISSIVE],
                                           cmd->texture_asset_mip_count
                                               [RT_MATERIAL3D_TEXTURE_SLOT_EMISSIVE]);
            if (tex)
                [ctx.encoder setFragmentTexture:tex atIndex:3];
        }
        if (cmd->metallic_roughness_map || cmd->metallic_roughness_map_asset) {
            id<MTLTexture> tex = metal_get_material_texture(
                ctx,
                cmd->metallic_roughness_map_asset,
                cmd->metallic_roughness_map,
                cmd->texture_asset_cache_key[RT_MATERIAL3D_TEXTURE_SLOT_METALLIC_ROUGHNESS],
                cmd->texture_asset_mip_start[RT_MATERIAL3D_TEXTURE_SLOT_METALLIC_ROUGHNESS],
                cmd->texture_asset_mip_count[RT_MATERIAL3D_TEXTURE_SLOT_METALLIC_ROUGHNESS]);
            if (tex)
                [ctx.encoder setFragmentTexture:tex atIndex:14];
        }
        if (cmd->ao_map || cmd->ao_map_asset) {
            id<MTLTexture> tex = metal_get_material_texture(ctx,
                                                            cmd->ao_map_asset,
                                                            cmd->ao_map,
                                                            cmd->texture_asset_cache_key
                                                                [RT_MATERIAL3D_TEXTURE_SLOT_AO],
                                                            cmd->texture_asset_mip_start
                                                                [RT_MATERIAL3D_TEXTURE_SLOT_AO],
                                                            cmd->texture_asset_mip_count
                                                                [RT_MATERIAL3D_TEXTURE_SLOT_AO]);
            if (tex)
                [ctx.encoder setFragmentTexture:tex atIndex:15];
        }
        if (has_complete_splat) {
            id<MTLTexture> sp = metal_get_cached_texture(ctx, cmd->splat_map);
            if (sp)
                [ctx.encoder setFragmentTexture:sp atIndex:8];
            for (int si = 0; si < 4; si++) {
                if (cmd->splat_layers[si]) {
                    id<MTLTexture> lt = metal_get_cached_texture(ctx, cmd->splat_layers[si]);
                    if (lt)
                        [ctx.encoder setFragmentTexture:lt atIndex:9 + si];
                }
            }
        }
        if (cmd->env_map && cmd->reflectivity > 0.0001f) {
            id<MTLTexture> envTex =
                metal_get_cached_cubemap(ctx, (const rt_cubemap3d *)cmd->env_map);
            if (envTex)
                [ctx.encoder setFragmentTexture:envTex atIndex:13];
        }

        /* Lights */
        {
            mtl_light_t ml[VGFX3D_MAX_LIGHTS];
            memset(ml, 0, sizeof(ml));
            for (int32_t i = 0; i < light_count && i < VGFX3D_MAX_LIGHTS; i++) {
                ml[i].type = lights[i].type;
                ml[i].shadow_index =
                    vgfx3d_metal_sanitize_shadow_index(lights[i].shadow_index, ctx->_shadowCount);
                ml[i].shadow_cascade_count =
                    ml[i].shadow_index >= 0 ? lights[i].shadow_cascade_count : 1;
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
                memcpy(ml[i].shadow_cascade_splits,
                       lights[i].shadow_cascade_splits,
                       sizeof(ml[i].shadow_cascade_splits));
            }
            int32_t bc = light_count > 0
                             ? (light_count > VGFX3D_MAX_LIGHTS ? VGFX3D_MAX_LIGHTS : light_count)
                             : 1;
            [ctx.encoder setFragmentBytes:ml length:sizeof(mtl_light_t) * bc atIndex:2];
        }

        [ctx.encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                indexCount:cmd->index_count
                                 indexType:MTLIndexTypeUInt32
                               indexBuffer:ib
                         indexBufferOffset:0
                             instanceCount:(NSUInteger)instance_count];
    }
}

//=============================================================================
// MTL-11: Post-processing
//=============================================================================

/* C-side PostFX params struct (must match MSL PostFXParams) */
typedef struct {
    float invViewProjection[16];
    float prevViewProjection[16];
    float cameraPosition[4];
    float invResolution[2];
    int32_t bloomEnabled;
    float bloomThreshold, bloomStrength;
    int32_t bloomPasses;
    int32_t tonemapMode;
    float tonemapExposure;
    int32_t fxaaEnabled;
    int32_t colorGradeEnabled;
    float cgBright, cgContrast, cgSat;
    int32_t vignetteEnabled;
    float vigRadius, vigSoftness;
    /* Extended effects (parity with D3D11) */
    int32_t ssaoEnabled;
    float ssaoRadius, ssaoIntensity;
    int32_t ssaoSamples;
    int32_t dofEnabled;
    float dofFocusDist, dofAperture, dofMaxBlur;
    int32_t motionBlurEnabled;
    float motionBlurIntensity;
    int32_t motionBlurSamples;
    int32_t overlayEnabled;
} mtl_postfx_params_t;

/// @brief Copy the current drawable's contents into the persistent display texture.
/// @details Lets later passes (and the software-present fallback) read the last presented image.
/// @return 1 on success, 0 if no drawable is available.
static int metal_capture_current_drawable_to_display_texture(VGFXMetalContext *ctx) {
    NSUInteger copy_w;
    NSUInteger copy_h;
    id<MTLBlitCommandEncoder> blit;

    if (!ctx || !ctx.cmdBuf || !ctx.drawable)
        return 0;
    if (!ctx.displayTexture || ctx.displayTexture.pixelFormat != MTLPixelFormatBGRA8Unorm ||
        (int32_t)ctx.displayTexture.width != ctx.width ||
        (int32_t)ctx.displayTexture.height != ctx.height) {
        ctx.displayTexture = metal_new_color_texture(ctx,
                                                     ctx.width,
                                                     ctx.height,
                                                     MTLPixelFormatBGRA8Unorm,
                                                     MTLTextureUsageShaderRead,
                                                     MTLStorageModeShared,
                                                     NO);
    }
    if (!ctx.displayTexture)
        return 0;

    copy_w = ctx.drawable.texture.width < ctx.displayTexture.width ? ctx.drawable.texture.width
                                                                   : ctx.displayTexture.width;
    copy_h = ctx.drawable.texture.height < ctx.displayTexture.height ? ctx.drawable.texture.height
                                                                     : ctx.displayTexture.height;
    blit = [ctx.cmdBuf blitCommandEncoder];
    if (!blit)
        return 0;
    [blit copyFromTexture:ctx.drawable.texture
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(copy_w, copy_h, 1)
                toTexture:ctx.displayTexture
         destinationSlice:0
         destinationLevel:0
        destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blit endEncoding];
    return 1;
}

/// @brief Populate the post-processing uniform parameters (exposure, tonemap, bloom, etc.) for a
/// frame.
static void metal_fill_postfx_params(VGFXMetalContext *ctx,
                                     const vgfx3d_postfx_snapshot_t *postfx,
                                     int overlay_enabled,
                                     mtl_postfx_params_t *params) {
    if (!ctx || !params)
        return;
    memset(params, 0, sizeof(*params));
    transpose4x4(ctx.frameHistory.scene_inv_vp, params->invViewProjection);
    transpose4x4(ctx.frameHistory.scene_prev_vp, params->prevViewProjection);
    memcpy(params->cameraPosition, ctx.frameHistory.scene_cam_pos, sizeof(float) * 3);
    params->cameraPosition[3] = 1.0f;
    params->invResolution[0] = ctx.width > 0 ? 1.0f / (float)ctx.width : 0.0f;
    params->invResolution[1] = ctx.height > 0 ? 1.0f / (float)ctx.height : 0.0f;
    params->overlayEnabled = overlay_enabled ? 1 : 0;
    if (!postfx)
        return;
    params->bloomEnabled = postfx->bloom_enabled ? 1 : 0;
    params->bloomThreshold = postfx->bloom_threshold;
    params->bloomStrength = postfx->bloom_intensity;
    params->bloomPasses = postfx->bloom_passes;
    params->tonemapMode = (int32_t)postfx->tonemap_mode;
    params->tonemapExposure = postfx->tonemap_exposure;
    params->fxaaEnabled = postfx->fxaa_enabled ? 1 : 0;
    params->colorGradeEnabled = postfx->color_grade_enabled ? 1 : 0;
    params->cgBright = postfx->cg_brightness;
    params->cgContrast = postfx->cg_contrast;
    params->cgSat = postfx->cg_saturation;
    params->vignetteEnabled = postfx->vignette_enabled ? 1 : 0;
    params->vigRadius = postfx->vignette_radius;
    params->vigSoftness = postfx->vignette_softness;
    params->dofEnabled = postfx->dof_enabled ? 1 : 0;
    params->dofFocusDist = postfx->dof_focus_distance;
    params->dofAperture = postfx->dof_aperture;
    params->dofMaxBlur = postfx->dof_max_blur;
    params->ssaoEnabled = postfx->ssao_enabled ? 1 : 0;
    params->ssaoRadius = postfx->ssao_radius;
    params->ssaoIntensity = postfx->ssao_intensity;
    params->ssaoSamples = postfx->ssao_samples;
    params->motionBlurEnabled = postfx->motion_blur_enabled ? 1 : 0;
    params->motionBlurIntensity = postfx->motion_blur_intensity;
    params->motionBlurSamples = postfx->motion_blur_samples;
}

/// @brief Encode one full-screen post-processing pass, sampling @p source into a new output
/// texture.
/// @return The pass output texture (nil on failure).
static id<MTLTexture> metal_encode_postfx_pass(VGFXMetalContext *ctx,
                                               id<MTLTexture> source_texture,
                                               id<MTLTexture> target_texture,
                                               const vgfx3d_postfx_snapshot_t *postfx,
                                               int overlay_enabled) {
    MTLRenderPassDescriptor *rp;
    id<MTLRenderCommandEncoder> pfxEncoder;
    mtl_postfx_params_t params;

    if (!ctx || !ctx.cmdBuf || !ctx.postfxPipeline || !source_texture || !target_texture ||
        !ctx.offscreenMotion || !ctx.depthTexture)
        return nil;

    rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = target_texture;
    rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;

    pfxEncoder = [ctx.cmdBuf renderCommandEncoderWithDescriptor:rp];
    if (!pfxEncoder)
        return nil;

    [pfxEncoder setRenderPipelineState:ctx.postfxPipeline];
    [pfxEncoder setFragmentTexture:source_texture atIndex:0];
    [pfxEncoder setFragmentTexture:ctx.depthTexture atIndex:1];
    [pfxEncoder setFragmentTexture:ctx.offscreenMotion atIndex:2];
    if (overlay_enabled && ctx.overlayColorTexture)
        [pfxEncoder setFragmentTexture:ctx.overlayColorTexture atIndex:3];
    [pfxEncoder setFragmentSamplerState:(ctx.sharedSampler ? ctx.sharedSampler : ctx.defaultSampler)
                                atIndex:0];

    metal_fill_postfx_params(ctx, postfx, overlay_enabled, &params);
    [pfxEncoder setFragmentBytes:&params length:sizeof(params) atIndex:0];

    [pfxEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    [pfxEncoder endEncoding];
    return target_texture;
}

/// @brief Run the post-FX chain on the scene color texture if post-processing is enabled.
/// @details Applies each enabled effect (tonemap, bloom, etc.) in sequence and returns the final
///          texture; returns the unmodified scene texture when post-FX is disabled or empty.
static id<MTLTexture> metal_encode_postfx_if_needed(VGFXMetalContext *ctx,
                                                    const vgfx3d_postfx_chain_t *postfx) {
    id<MTLTexture> source_texture;
    id<MTLTexture> target_texture;

    if (!ctx)
        return nil;
    if (ctx.postfxEncodedThisFrame && ctx.displayTexture)
        return ctx.displayTexture;
    if (!ctx.cmdBuf || !ctx.postfxPipeline || !ctx.postfxColorTexture ||
        !ctx.postfxScratchTexture || !ctx.offscreenColor || !ctx.offscreenMotion ||
        !ctx.depthTexture) {
        return nil;
    }

    metal_finish_encoding(ctx);

    source_texture = ctx.offscreenColor;
    target_texture = ctx.postfxColorTexture;
    if (postfx && postfx->enabled && postfx->effect_count > 0 && postfx->effects) {
        for (int32_t i = 0; i < postfx->effect_count; i++) {
            id<MTLTexture> output_texture = metal_encode_postfx_pass(
                ctx, source_texture, target_texture, &postfx->effects[i].snapshot, 0);
            if (!output_texture)
                return nil;
            source_texture = output_texture;
            target_texture = (source_texture == ctx.postfxColorTexture) ? ctx.postfxScratchTexture
                                                                        : ctx.postfxColorTexture;
        }
    } else {
        id<MTLTexture> output_texture =
            metal_encode_postfx_pass(ctx, source_texture, target_texture, NULL, 0);
        if (!output_texture)
            return nil;
        source_texture = output_texture;
        target_texture = ctx.postfxScratchTexture;
    }

    if (ctx.frameHistory.overlay_used_this_frame && ctx.overlayColorTexture) {
        id<MTLTexture> output_texture =
            metal_encode_postfx_pass(ctx, source_texture, target_texture, NULL, 1);
        if (!output_texture)
            return nil;
        source_texture = output_texture;
    }

    ctx.postfxEncodedThisFrame = 1;
    ctx.displayTexture = source_texture;
    return source_texture;
}

/// @brief Metal PostFX presentation: render fullscreen quad with PostFX shader.
/// Runs the fullscreen post-processing pass into an offscreen texture, then
/// presents that texture through the CAMetalLayer drawable when available.
static void metal_present_postfx(void *backend_ctx, const vgfx3d_postfx_chain_t *postfx) {
    if (!backend_ctx || !postfx)
        return;
    @autoreleasepool {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)backend_ctx;
        id<MTLTexture> final_texture;

        if (!ctx)
            return;
        final_texture = metal_encode_postfx_if_needed(ctx, postfx);
        if (!final_texture) {
            metal_present(backend_ctx);
            return;
        }

        if (metal_present_texture(ctx, final_texture, YES)) {
            metal_commit_pending(ctx, NO);
            ctx.postfxCompositedToDrawable = 0;
        } else {
            metal_commit_pending(ctx, YES);
            metal_present_texture_to_framebuffer(ctx, final_texture);
            ctx.postfxCompositedToDrawable = 0;
        }
    }
}

/// @brief Metal `apply_postfx` — composite post-FX to the drawable without committing.
static void metal_apply_postfx(void *backend_ctx, const vgfx3d_postfx_chain_t *postfx) {
    if (!backend_ctx || !postfx)
        return;
    @autoreleasepool {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)backend_ctx;
        id<MTLTexture> final_texture;

        if (!ctx)
            return;
        final_texture = metal_encode_postfx_if_needed(ctx, postfx);
        if (!final_texture)
            return;
        if (metal_present_texture(ctx, final_texture, NO))
            ctx.postfxCompositedToDrawable = 1;
    }
}

/// @brief Draw the skybox using a full-screen triangle sampling the bound environment cubemap.
static void metal_draw_skybox(void *ctx_ptr, const void *cubemap_ptr) {
    if (!ctx_ptr || !cubemap_ptr)
        return;
    @autoreleasepool {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
        id<MTLTexture> cubemap = metal_get_cached_cubemap(ctx, (const rt_cubemap3d *)cubemap_ptr);
        id<MTLRenderPipelineState> skybox_pipeline;
        id<MTLRenderPipelineState> default_pipeline;
        mtl_skybox_params_t params;
        float view_rot[16];
        float inv_projection[16];
        float inv_view_rot[16];

        if (!ctx || !ctx.encoder || !cubemap)
            return;
        skybox_pipeline = ctx.currentTargetKind == VGFX3D_METAL_TARGET_SCENE
                              ? ctx.skyboxPipeline
                              : ctx.skyboxColorPipeline;
        default_pipeline = ctx.currentTargetKind == VGFX3D_METAL_TARGET_SCENE
                               ? ctx.pipelineState
                               : ctx.pipelineStateColorOnly;
        if (!skybox_pipeline)
            skybox_pipeline = ctx.skyboxPipeline ? ctx.skyboxPipeline : ctx.skyboxColorPipeline;
        if (!default_pipeline)
            default_pipeline = ctx.pipelineState ? ctx.pipelineState : ctx.pipelineStateColorOnly;
        if (!skybox_pipeline || !default_pipeline)
            return;

        memcpy(view_rot, ctx->_view, sizeof(view_rot));
        view_rot[3] = 0.0f;
        view_rot[7] = 0.0f;
        view_rot[11] = 0.0f;
        view_rot[12] = 0.0f;
        view_rot[13] = 0.0f;
        view_rot[14] = 0.0f;
        view_rot[15] = 1.0f;

        memset(&params, 0, sizeof(params));
        if (vgfx3d_invert_matrix4(ctx->_projection, inv_projection) != 0)
            mat4f_identity(inv_projection);
        transpose4x4(view_rot, inv_view_rot);
        transpose4x4(inv_projection, params.inverseProjection);
        transpose4x4(inv_view_rot, params.inverseViewRotation);
        memcpy(params.cameraForward, ctx->_camForward, sizeof(float) * 3);
        params.cameraForward[3] = ctx->_camIsOrtho ? 1.0f : 0.0f;

        [ctx.encoder setCullMode:MTLCullModeNone];
        [ctx.encoder setDepthStencilState:(ctx.skyboxDepthState ? ctx.skyboxDepthState
                                                                : ctx.depthStateNoWrite)];
        [ctx.encoder setRenderPipelineState:skybox_pipeline];
        [ctx.encoder setVertexBytes:&params length:sizeof(params) atIndex:0];
        [ctx.encoder setFragmentBytes:&params length:sizeof(params) atIndex:0];
        [ctx.encoder setFragmentTexture:cubemap atIndex:0];
        [ctx.encoder setFragmentSamplerState:(ctx.cubeSampler ? ctx.cubeSampler : ctx.sharedSampler)
                                     atIndex:0];
        [ctx.encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [ctx.encoder setRenderPipelineState:default_pipeline];
        [ctx.encoder setDepthStencilState:ctx.depthState];
        [ctx.encoder setCullMode:MTLCullModeBack];
    }
}

/// @brief Enable or disable the GPU post-processing pass for subsequent frames.
static void metal_set_gpu_postfx_enabled(void *ctx_ptr, int8_t enabled) {
    @autoreleasepool {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
        int8_t desired_enabled;
        if (!ctx)
            return;
        desired_enabled = (enabled && ctx.postfxPipeline) ? 1 : 0;
        if (ctx.gpuPostfxEnabled == desired_enabled)
            return;
        ctx.gpuPostfxEnabled = desired_enabled;
        ctx.postfxEncodedThisFrame = 0;
        ctx.postfxCompositedToDrawable = 0;
        ctx.displayTexture = nil;
        if (!ctx.gpuPostfxEnabled) {
            metal_reset_gpu_postfx_chain(ctx);
        }
        if (ctx.cmdBuf)
            metal_commit_pending(ctx, YES);
        if (!ctx.rttActive && ctx.width > 0 && ctx.height > 0)
            metal_recreate_main_targets(ctx, ctx.width, ctx.height);
    }
}

/// @brief Install a post-FX chain snapshot to drive the GPU post-processing pass (deep-copied).
static void metal_set_gpu_postfx_snapshot(void *ctx_ptr, const vgfx3d_postfx_chain_t *postfx) {
    @autoreleasepool {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
        if (!ctx)
            return;
        if (!postfx) {
            metal_reset_gpu_postfx_chain(ctx);
            return;
        }
        if (!metal_copy_gpu_postfx_chain(ctx, postfx)) {
            metal_reset_gpu_postfx_chain(ctx);
            return;
        }
    }
}

/* Hide the Metal layer during software RTT or 2D mode */
/// @brief Hide the Metal layer so GPU-rendered frames stop being presented (used during
///   teardown or when switching away from the GPU backend).
static void metal_hide_gpu_layer(void *backend_ctx) {
    if (!backend_ctx)
        return;
    @autoreleasepool {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)backend_ctx;
        if (ctx.metalLayer)
            ctx.metalLayer.hidden = YES;
    }
}

/// @brief Make the Metal layer visible (attach/unhide it) so GPU-rendered frames are presented.
static void metal_show_gpu_layer(void *backend_ctx) {
    if (!backend_ctx)
        return;
    @autoreleasepool {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)backend_ctx;
        if (ctx.metalLayer) {
            metal_update_layer_size(ctx);
            ctx.metalLayer.hidden = NO;
        }
    }
}

const vgfx3d_backend_t vgfx3d_metal_backend = {
    .name = "metal",
    .create_ctx = metal_create_ctx,
    .destroy_ctx = metal_destroy_ctx,
    .clear = metal_clear,
    .resize = metal_resize,
    .begin_frame = metal_begin_frame,
    .submit_draw = metal_submit_draw,
    .end_frame = metal_end_frame,
    .set_render_target = metal_set_render_target,
    .shadow_begin = metal_shadow_begin,
    .shadow_draw = metal_shadow_draw,
    .shadow_end = metal_shadow_end,
    .draw_skybox = metal_draw_skybox,
    .submit_draw_instanced = metal_submit_draw_instanced,
    .present = metal_present,
    .readback_rgba = metal_readback_rgba,
    .present_postfx = metal_present_postfx,
    .apply_postfx = metal_apply_postfx,
    .set_gpu_postfx_enabled = metal_set_gpu_postfx_enabled,
    .set_gpu_postfx_snapshot = metal_set_gpu_postfx_snapshot,
    .show_gpu_layer = metal_show_gpu_layer,
    .hide_gpu_layer = metal_hide_gpu_layer,
    .set_texture_upload_budget = metal_set_texture_upload_budget,
    .get_texture_upload_pending_bytes = metal_get_texture_upload_pending_bytes,
    .get_texture_upload_bytes = metal_get_texture_upload_bytes,
    .get_native_texture_caps = metal_get_native_texture_caps,
};

#endif /* __APPLE__ && VIPER_ENABLE_GRAPHICS */
