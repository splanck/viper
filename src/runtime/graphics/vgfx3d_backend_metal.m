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
#include "vgfx.h"
#include "vgfx3d_backend.h"
#include "vgfx3d_backend_metal_shared.h"
#include "vgfx3d_backend_utils.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    /* MTL-12: Shadow light view-projection (stored from shadow_begin) */
    float _shadowLightVP[16];
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
@property(nonatomic) uint64_t frameSerial;
@property(nonatomic, strong) id<MTLSamplerState> sharedSampler;
@property(nonatomic, strong) id<MTLSamplerState> cubeSampler;
@property(nonatomic, strong) id<MTLTexture> defaultCubemap;
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
@property(nonatomic) int8_t gpuPostfxSnapshotValid;
@property(nonatomic) vgfx3d_postfx_snapshot_t gpuPostfxSnapshot;
@property(nonatomic) vgfx3d_metal_frame_history_t frameHistory;
@property(nonatomic) int8_t postfxEncodedThisFrame;
@end

@implementation VGFXMetalContext
@end

@interface VGFXMetalTextureCacheEntry : NSObject
@property(nonatomic, strong) id<MTLTexture> texture;
@property(nonatomic) uint64_t generation;
@property(nonatomic) uint64_t lastUsedFrame;
@end

@implementation VGFXMetalTextureCacheEntry
@end

@interface VGFXMetalCubemapCacheEntry : NSObject
@property(nonatomic, strong) id<MTLTexture> texture;
@property(nonatomic) uint64_t generation;
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
@property(nonatomic) uint64_t lastUsedFrame;
@end

@implementation VGFXMetalRenderTargetCacheEntry
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
     "    float4 tangent  [[attribute(4)]];\n"
     "    uchar4 boneIdx  [[attribute(5)]];\n"
     "    float4 boneWt   [[attribute(6)]];\n"
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
     "    float4 fogParams;\n"
     "    int4 counts;\n"
     "    float4 cameraForward;\n"
     "    float4x4 prevViewProjection;\n"
     "    float4x4 shadowVP;\n"
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
     "};\n"
     "\n"
     "struct ShadowOut {\n"
     "    float4 position [[position]];\n"
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
     "float4 skinPosition(float4 pos,\n"
     "                    VertexIn in,\n"
     "                    constant float4x4 *palette,\n"
     "                    int enabled) {\n"
     "    if (enabled == 0)\n"
     "        return pos;\n"
     "    float4 skinned = float4(0.0);\n"
     "    for (int i = 0; i < 4; i++) {\n"
     "        float bw = in.boneWt[i];\n"
     "        if (bw <= 0.0001)\n"
     "            continue;\n"
     "        uint idx = min((uint)in.boneIdx[i], 127u);\n"
     "        skinned += (palette[idx] * pos) * bw;\n"
     "    }\n"
     "    return skinned;\n"
     "}\n"
     "\n"
     "float3 skinVector(float3 vec,\n"
     "                  VertexIn in,\n"
     "                  constant float4x4 *palette,\n"
     "                  int enabled) {\n"
     "    if (enabled == 0)\n"
     "        return vec;\n"
     "    float3 skinned = float3(0.0);\n"
     "    for (int i = 0; i < 4; i++) {\n"
     "        float bw = in.boneWt[i];\n"
     "        if (bw <= 0.0001)\n"
     "            continue;\n"
     "        uint idx = min((uint)in.boneIdx[i], 127u);\n"
     "        skinned += (palette[idx] * float4(vec, 0.0)).xyz * bw;\n"
     "    }\n"
     "    return skinned;\n"
     "}\n"
     "\n"
     "VertexOut buildVertex(float3 currPos,\n"
     "                      float3 prevPos,\n"
     "                      float3 currNormal,\n"
     "                      float4 currTangent,\n"
     "                      float2 uv,\n"
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
     "    out.position = currClip;\n"
     "    out.position.z = out.position.z * 0.5 + out.position.w * 0.5;\n"
     "    out.worldPos = worldPos.xyz;\n"
     "    out.normal = (normalMatrix * float4(currNormal, 0.0)).xyz;\n"
     "    out.tangent = float4((modelMatrix * float4(currTangent.xyz, 0.0)).xyz, currTangent.w);\n"
     "    out.uv = uv;\n"
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
     "                       normalize(skinnedNormal),\n"
     "                       float4(normalize(skinnedTangent), currTangent.w),\n"
     "                       in.uv,\n"
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
     "                       normalize(skinnedNormal),\n"
     "                       float4(normalize(skinnedTangent), currTangent.w),\n"
     "                       in.uv,\n"
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
     "    out.position = obj.viewProjection * (obj.modelMatrix * skinnedPos);\n"
     "    out.position.z = out.position.z * 0.5 + out.position.w * 0.5;\n"
     "    return out;\n"
     "}\n"
     "\n"
     "float4 motion_output(VertexOut in) {\n"
     "    float2 currNdc = in.currClip.xy / max(in.currClip.w, 0.0001);\n"
     "    float2 prevNdc = in.prevClip.xy / max(in.prevClip.w, 0.0001);\n"
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
     "float3 env_sample(texturecube<float> envTex,\n"
     "                  sampler envSampler,\n"
     "                  float3 dir,\n"
     "                  float roughness,\n"
     "                  float maxLod) {\n"
     "    float lod = clamp(roughness, 0.0, 1.0) * max(maxLod, 0.0);\n"
     "    return envTex.sample(envSampler, normalize(dir), level(lod)).rgb;\n"
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
     "    depth2d<float> shadowMap [[texture(4)]],\n"
     "    texture2d<float> splatTex [[texture(5)]],\n"
     "    texture2d<float> splatLayer0 [[texture(6)]],\n"
     "    texture2d<float> splatLayer1 [[texture(7)]],\n"
     "    texture2d<float> splatLayer2 [[texture(8)]],\n"
     "    texture2d<float> splatLayer3 [[texture(9)]],\n"
     "    texturecube<float> envTex [[texture(10)]],\n"
     "    texture2d<float> metallicRoughnessTex [[texture(11)]],\n"
     "    texture2d<float> aoTex [[texture(12)]],\n"
     "    sampler texSampler [[sampler(0)]],\n"
     "    sampler shadowSampler [[sampler(1)]],\n"
     "    sampler envSampler [[sampler(2)]]\n"
     ") {\n"
     "    MainOut out;\n"
     "    float3 baseColor = material.diffuseColor.rgb;\n"
     "    float texAlpha = 1.0;\n"
     "    float materialAlpha = material.diffuseColor.a * material.scalars.x;\n"
     "    if (material.flags0.x != 0) {\n"
     "        float4 texSample = diffuseTex.sample(texSampler, in.uv);\n"
     "        baseColor *= texSample.rgb;\n"
     "        texAlpha = texSample.a;\n"
     "    }\n"
     "    if (material.flags1.z != 0) {\n"
     "        float4 sp = splatTex.sample(texSampler, in.uv);\n"
     "        float wsum = sp.r + sp.g + sp.b + sp.a;\n"
     "        if (wsum > 0.001) sp /= wsum;\n"
     "        float3 blended = float3(0);\n"
     "        if (sp.r > 0.001) blended += splatLayer0.sample(texSampler, in.uv * "
     "material.splatScales.x).rgb * sp.r;\n"
     "        if (sp.g > 0.001) blended += splatLayer1.sample(texSampler, in.uv * "
     "material.splatScales.y).rgb * sp.g;\n"
     "        if (sp.b > 0.001) blended += splatLayer2.sample(texSampler, in.uv * "
     "material.splatScales.z).rgb * sp.b;\n"
     "        if (sp.a > 0.001) blended += splatLayer3.sample(texSampler, in.uv * "
     "material.splatScales.w).rgb * sp.a;\n"
     "        baseColor = blended * material.diffuseColor.rgb;\n"
     "    }\n"
     "    float3 N = normalize(in.normal);\n"
     "    float3 cameraToWorld = scene.cameraPosition.xyz - in.worldPos;\n"
     "    float3 V = normalize(scene.cameraPosition.w > 0.5 ? -scene.cameraForward.xyz : "
     "cameraToWorld);\n"
     "    float viewDistance = scene.cameraPosition.w > 0.5\n"
     "        ? abs(dot(in.worldPos - scene.cameraPosition.xyz, scene.cameraForward.xyz))\n"
     "        : length(cameraToWorld);\n"
     "    if (material.flags0.z != 0) {\n"
     "        float3 T = normalize(in.tangent.xyz);\n"
     "        T = normalize(T - N * dot(T, N));\n"
     "        float lenT = length(T);\n"
     "        if (lenT > 0.001) {\n"
     "            float3 B = cross(N, T) * (in.tangent.w < 0.0 ? -1.0 : 1.0);\n"
     "            float3 mapN = normalTex.sample(texSampler, in.uv).rgb * 2.0 - 1.0;\n"
     "            mapN.xy *= material.pbrScalars1.x;\n"
     "            N = normalize(T * mapN.x + B * mapN.y + N * mapN.z);\n"
     "        }\n"
     "    }\n"
     "    float3 emissive = material.emissiveColor.rgb * material.pbrScalars0.w;\n"
     "    if (material.flags1.x != 0) {\n"
     "        emissive *= emissiveTex.sample(texSampler, in.uv).rgb;\n"
     "    }\n"
     "    float4 metallicRoughnessSample = float4(1.0);\n"
     "    float envRoughness = clamp(material.pbrScalars0.y, 0.0, 1.0);\n"
     "    if (material.pbrFlags.z != 0) {\n"
     "        metallicRoughnessSample = metallicRoughnessTex.sample(texSampler, in.uv);\n"
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
     "            float4 aoSample = aoTex.sample(texSampler, in.uv);\n"
     "            ao = clamp(ao * aoSample.r, 0.0, 1.0);\n"
     "        }\n"
     "        result = scene.ambientColor.rgb * baseColor * ao;\n"
     "        for (int i = 0; i < scene.counts.x; i++) {\n"
     "            float3 L; float atten = 1.0;\n"
     "            if (lights[i].type == 0) {\n"
     "                L = normalize(-lights[i].direction.xyz);\n"
     "            } else if (lights[i].type == 1) {\n"
     "                float3 tl = lights[i].position.xyz - in.worldPos;\n"
     "                float d = length(tl); L = tl / max(d, 0.0001);\n"
     "                atten = 1.0 / (1.0 + lights[i].attenuation * d * d);\n"
     "            } else if (lights[i].type == 3) {\n"
     "                float3 tl = lights[i].position.xyz - in.worldPos;\n"
     "                float d = length(tl); L = tl / max(d, 0.0001);\n"
     "                atten = 1.0 / (1.0 + lights[i].attenuation * d * d);\n"
     "                float spotDot = dot(-L, normalize(lights[i].direction.xyz));\n"
     "                if (spotDot < lights[i].outer_cos) {\n"
     "                    atten = 0.0;\n"
     "                } else if (spotDot < lights[i].inner_cos) {\n"
     "                    float t = (spotDot - lights[i].outer_cos) / "
     "(lights[i].inner_cos - lights[i].outer_cos);\n"
     "                    atten *= t * t * (3.0 - 2.0 * t);\n"
     "                }\n"
     "            } else {\n"
     "                result += lights[i].color.rgb * lights[i].intensity * baseColor * ao;\n"
     "                continue;\n"
     "            }\n"
     "            float NdotL = max(dot(N, L), 0.0);\n"
     "            if (scene.counts.y != 0 && lights[i].type == 0) {\n"
     "                float4 lc = scene.shadowVP * float4(in.worldPos, 1.0);\n"
     "                float3 suv = lc.xyz / lc.w;\n"
     "                suv.xy = suv.xy * 0.5 + 0.5;\n"
     "                suv.y = 1.0 - suv.y;\n"
     "                if (suv.x >= 0.0 && suv.x <= 1.0 && suv.y >= 0.0 && suv.y <= 1.0) {\n"
     "                    float shadow = shadowMap.sample_compare(shadowSampler, suv.xy, suv.z - "
     "scene.fogParams.z);\n"
     "                    atten *= mix(0.15, 1.0, shadow);\n"
     "                }\n"
     "            }\n"
     "            if (NdotL <= 0.0)\n"
     "                continue;\n"
     "            float3 H = normalize(L + V);\n"
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
     "            specColor *= specularTex.sample(texSampler, in.uv).rgb;\n"
     "        }\n"
     "        for (int i = 0; i < scene.counts.x; i++) {\n"
     "            float3 L; float atten = 1.0;\n"
     "            if (lights[i].type == 0) {\n"
     "                L = normalize(-lights[i].direction.xyz);\n"
     "            } else if (lights[i].type == 1) {\n"
     "                float3 tl = lights[i].position.xyz - in.worldPos;\n"
     "                float d = length(tl); L = tl / max(d, 0.0001);\n"
     "                atten = 1.0 / (1.0 + lights[i].attenuation * d * d);\n"
     "            } else if (lights[i].type == 3) {\n"
     "                float3 tl = lights[i].position.xyz - in.worldPos;\n"
     "                float d = length(tl); L = tl / max(d, 0.0001);\n"
     "                atten = 1.0 / (1.0 + lights[i].attenuation * d * d);\n"
     "                float spotDot = dot(-L, normalize(lights[i].direction.xyz));\n"
     "                if (spotDot < lights[i].outer_cos) {\n"
     "                    atten = 0.0;\n"
     "                } else if (spotDot < lights[i].inner_cos) {\n"
     "                    float t = (spotDot - lights[i].outer_cos) / "
     "(lights[i].inner_cos - lights[i].outer_cos);\n"
     "                    atten *= t * t * (3.0 - 2.0 * t);\n"
     "                }\n"
     "            } else {\n"
     "                result += lights[i].color.rgb * lights[i].intensity * baseColor;\n"
     "                continue;\n"
     "            }\n"
     "            float NdotL = max(dot(N, L), 0.0);\n"
     "            if (scene.counts.y != 0 && lights[i].type == 0) {\n"
     "                float4 lc = scene.shadowVP * float4(in.worldPos, 1.0);\n"
     "                float3 suv = lc.xyz / lc.w;\n"
     "                suv.xy = suv.xy * 0.5 + 0.5;\n"
     "                suv.y = 1.0 - suv.y;\n"
     "                if (suv.x >= 0.0 && suv.x <= 1.0 && suv.y >= 0.0 && suv.y <= 1.0) {\n"
     "                    float shadow = shadowMap.sample_compare(shadowSampler, suv.xy, suv.z - "
     "scene.fogParams.z);\n"
     "                    atten *= mix(0.15, 1.0, shadow);\n"
     "                }\n"
     "            }\n"
     "            result += lights[i].color.rgb * lights[i].intensity * NdotL * "
     "baseColor * atten;\n"
     "            if (NdotL > 0.0 && material.specularColor.w > 0.0) {\n"
     "                float3 H = normalize(L + V);\n"
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
    float shadow_vp[16];
} mtl_per_scene_t;

typedef struct {
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

static void transpose4x4(const float *src, float *dst) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            dst[c * 4 + r] = src[r * 4 + c];
}

static void mat4f_mul(const float *a, const float *b, float *out) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            out[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c] +
                             a[r * 4 + 2] * b[2 * 4 + c] + a[r * 4 + 3] * b[3 * 4 + c];
}

static void mat4f_identity(float *out) {
    if (!out)
        return;
    memset(out, 0, sizeof(float) * 16);
    out[0] = 1.0f;
    out[5] = 1.0f;
    out[10] = 1.0f;
    out[15] = 1.0f;
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

static int metal_copy_texture_to_rgba(
    id<MTLTexture> tex, uint8_t *dst_rgba, int32_t w, int32_t h, int32_t stride);
static void metal_update_layer_size(VGFXMetalContext *ctx);
static int metal_capture_current_drawable_to_display_texture(VGFXMetalContext *ctx);
static id<MTLTexture> metal_encode_postfx_if_needed(
    VGFXMetalContext *ctx, const vgfx3d_postfx_snapshot_t *postfx);

static float metal_cubemap_max_lod(const rt_cubemap3d *cubemap) {
    int32_t mip_count;

    if (!cubemap || cubemap->face_size <= 1)
        return 0.0f;
    mip_count =
        vgfx3d_metal_compute_mip_count((int32_t)cubemap->face_size, (int32_t)cubemap->face_size);
    return mip_count > 1 ? (float)(mip_count - 1) : 0.0f;
}

static MTLPixelFormat
metal_color_pixel_format(vgfx3d_metal_color_format_t format) {
    return format == VGFX3D_METAL_COLOR_FORMAT_HDR16F ? MTLPixelFormatRGBA16Float
                                                      : MTLPixelFormatBGRA8Unorm;
}

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

static VGFXMetalRenderTargetCacheEntry *
metal_lookup_render_target_entry(VGFXMetalContext *ctx, vgfx3d_rendertarget_t *rt) {
    if (!ctx || !ctx.renderTargetCache || !rt)
        return nil;
    return ctx.renderTargetCache[[NSValue valueWithPointer:rt]];
}

static VGFXMetalRenderTargetCacheEntry *
metal_ensure_render_target_entry(VGFXMetalContext *ctx, vgfx3d_rendertarget_t *rt) {
    NSValue *key;
    VGFXMetalRenderTargetCacheEntry *entry;
    MTLTextureDescriptor *color_desc;
    MTLTextureDescriptor *depth_desc;

    if (!ctx || !ctx.renderTargetCache || !rt || rt->width <= 0 || rt->height <= 0)
        return nil;

    key = [NSValue valueWithPointer:rt];
    entry = ctx.renderTargetCache[key];
    if (entry && entry.width == rt->width && entry.height == rt->height && entry.colorTexture &&
        entry.motionTexture && entry.depthTexture) {
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
    entry.lastUsedFrame = ctx.frameSerial;

    color_desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:(NSUInteger)rt->width
                                                          height:(NSUInteger)rt->height
                                                       mipmapped:NO];
    color_desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    color_desc.storageMode = MTLStorageModeShared;
    entry.colorTexture = [ctx.device newTextureWithDescriptor:color_desc];

    color_desc.usage = MTLTextureUsageRenderTarget;
    color_desc.storageMode = MTLStorageModePrivate;
    entry.motionTexture = [ctx.device newTextureWithDescriptor:color_desc];

    depth_desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                           width:(NSUInteger)rt->width
                                                          height:(NSUInteger)rt->height
                                                       mipmapped:NO];
    depth_desc.usage = MTLTextureUsageRenderTarget;
    depth_desc.storageMode = MTLStorageModePrivate;
    entry.depthTexture = [ctx.device newTextureWithDescriptor:depth_desc];
    ctx.renderTargetCache[key] = entry;
    return entry;
}

static int metal_sync_render_target_color(void *userdata, vgfx3d_rendertarget_t *target) {
    VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)userdata;
    VGFXMetalRenderTargetCacheEntry *entry;

    if (!ctx || !target || !target->color_buf)
        return 0;
    entry = metal_lookup_render_target_entry(ctx, target);
    if (!entry || !entry.colorTexture)
        return 0;
    if (entry.pendingCommandBuffer) {
        [entry.pendingCommandBuffer waitUntilCompleted];
        entry.pendingCommandBuffer = nil;
    }
    entry.lastUsedFrame = ctx.frameSerial;
    return metal_copy_texture_to_rgba(
        entry.colorTexture, target->color_buf, target->width, target->height, target->stride);
}

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

static void metal_recreate_main_targets(VGFXMetalContext *ctx, int32_t w, int32_t h) {
    if (!ctx || w <= 0 || h <= 0)
        return;

    ctx.depthTexture =
        metal_new_depth_texture(ctx, w, h, MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);

    if (ctx.gpuPostfxEnabled) {
        ctx.offscreenColor = metal_new_color_texture(
            ctx,
            w,
            h,
            metal_color_pixel_format(VGFX3D_METAL_COLOR_FORMAT_HDR16F),
            MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead,
            MTLStorageModePrivate,
            NO);
        ctx.offscreenMotion = metal_new_color_texture(ctx,
                                                      w,
                                                      h,
                                                      MTLPixelFormatBGRA8Unorm,
                                                      MTLTextureUsageRenderTarget |
                                                          MTLTextureUsageShaderRead,
                                                      MTLStorageModePrivate,
                                                      NO);
        ctx.overlayColorTexture = metal_new_color_texture(
            ctx,
            w,
            h,
            metal_color_pixel_format(VGFX3D_METAL_COLOR_FORMAT_UNORM8),
            MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead,
            MTLStorageModePrivate,
            NO);
        ctx.overlayMotionTexture = metal_new_color_texture(ctx,
                                                           w,
                                                           h,
                                                           MTLPixelFormatBGRA8Unorm,
                                                           MTLTextureUsageRenderTarget |
                                                               MTLTextureUsageShaderRead,
                                                           MTLStorageModePrivate,
                                                           NO);
        ctx.overlayDepthTexture =
            metal_new_depth_texture(ctx, w, h, MTLTextureUsageRenderTarget);
        if (ctx.postfxPipeline) {
            ctx.postfxColorTexture = metal_new_color_texture(
                ctx,
                w,
                h,
                metal_color_pixel_format(VGFX3D_METAL_COLOR_FORMAT_UNORM8),
                MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead,
                MTLStorageModeShared,
                NO);
        } else {
            ctx.postfxColorTexture = nil;
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
    }

    ctx.displayTexture = nil;
    ctx.postfxEncodedThisFrame = 0;
}

static int metal_upload_rgba_to_bgra_texture(id<MTLTexture> tex,
                                             const uint8_t *rgba,
                                             int32_t w,
                                             int32_t h) {
    uint8_t *bgra;
    size_t pixel_count;
    if (!tex || !rgba || w <= 0 || h <= 0)
        return 0;
    pixel_count = (size_t)w * (size_t)h;
    bgra = (uint8_t *)malloc(pixel_count * 4u);
    if (!bgra)
        return 0;
    for (size_t i = 0; i < pixel_count; i++) {
        bgra[i * 4 + 0] = rgba[i * 4 + 2];
        bgra[i * 4 + 1] = rgba[i * 4 + 1];
        bgra[i * 4 + 2] = rgba[i * 4 + 0];
        bgra[i * 4 + 3] = rgba[i * 4 + 3];
    }
    [tex replaceRegion:MTLRegionMake2D(0, 0, (NSUInteger)w, (NSUInteger)h)
           mipmapLevel:0
             withBytes:bgra
           bytesPerRow:(NSUInteger)(w * 4)];
    free(bgra);
    return 1;
}

static int metal_copy_texture_to_rgba(
    id<MTLTexture> tex, uint8_t *dst_rgba, int32_t w, int32_t h, int32_t stride) {
    int32_t copy_w;
    int32_t copy_h;
    if (!tex || !dst_rgba || w <= 0 || h <= 0 || stride < w * 4)
        return 0;

    memset(dst_rgba, 0, (size_t)stride * (size_t)h);
    copy_w = (int32_t)tex.width < w ? (int32_t)tex.width : w;
    copy_h = (int32_t)tex.height < h ? (int32_t)tex.height : h;
    if (copy_w <= 0 || copy_h <= 0)
        return 0;

    [tex getBytes:dst_rgba
        bytesPerRow:(NSUInteger)stride
         fromRegion:MTLRegionMake2D(0, 0, (NSUInteger)copy_w, (NSUInteger)copy_h)
        mipmapLevel:0];

    for (int32_t y = 0; y < copy_h; y++) {
        uint8_t *row = dst_rgba + (size_t)y * (size_t)stride;
        for (int32_t x = 0; x < copy_w; x++) {
            uint8_t *px = row + (size_t)x * 4u;
            uint8_t tmp = px[0];
            px[0] = px[2];
            px[2] = tmp;
        }
    }
    return 1;
}

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

static void metal_finish_encoding(VGFXMetalContext *ctx) {
    if (!ctx)
        return;
    if (ctx.encoder) {
        [ctx.encoder endEncoding];
        ctx.encoder = nil;
    }
}

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
    }
}

static void metal_present_texture_to_framebuffer(VGFXMetalContext *ctx, id<MTLTexture> texture) {
    vgfx_framebuffer_t fb;
    if (!ctx || !texture || !ctx.vgfxWin)
        return;
    if (!vgfx_get_framebuffer(ctx.vgfxWin, &fb))
        return;
    (void)metal_copy_texture_to_rgba(texture, fb.pixels, fb.width, fb.height, fb.stride);
    ctx.displayTexture = texture;
}

static BOOL metal_present_texture(VGFXMetalContext *ctx, id<MTLTexture> texture) {
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
    [ctx.cmdBuf presentDrawable:drawable];
    ctx.displayTexture = texture;
    return YES;
}

static id<MTLBuffer> metal_new_shared_buffer(VGFXMetalContext *ctx,
                                             const void *bytes,
                                             size_t length) {
    if (!ctx || !bytes || length == 0)
        return nil;
    return [ctx.device newBufferWithBytes:bytes length:length options:MTLResourceStorageModeShared];
}

static id<MTLBuffer> metal_new_shared_buffer_with_length(VGFXMetalContext *ctx, size_t length) {
    if (!ctx || !ctx.device || length == 0)
        return nil;
    return [ctx.device newBufferWithLength:length options:MTLResourceStorageModeShared];
}

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

    if (cmd->geometry_key && cmd->geometry_revision != 0) {
        NSValue *key = [NSValue valueWithPointer:cmd->geometry_key];
        VGFXMetalGeometryCacheEntry *entry = ctx.geometryCache[key];
        if (!entry || entry.revision != cmd->geometry_revision ||
            entry.vertexCount != cmd->vertex_count || entry.indexCount != cmd->index_count ||
            !entry.vertexBuffer || !entry.indexBuffer) {
            metal_cache_evict_if_needed(ctx);
            entry = [[VGFXMetalGeometryCacheEntry alloc] init];
            entry.vertexBuffer = metal_new_shared_buffer(
                ctx, cmd->vertices, (size_t)cmd->vertex_count * sizeof(vgfx3d_vertex_t));
            entry.indexBuffer = metal_new_shared_buffer(
                ctx, cmd->indices, (size_t)cmd->index_count * sizeof(uint32_t));
            entry.revision = cmd->geometry_revision;
            entry.vertexCount = cmd->vertex_count;
            entry.indexCount = cmd->index_count;
            ctx.geometryCache[key] = entry;
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

//=============================================================================
// Vertex descriptor (84-byte vgfx3d_vertex_t)
//=============================================================================

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
    d.attributes[3].format = MTLVertexFormatFloat4;
    d.attributes[3].offset = 32;
    d.attributes[3].bufferIndex = 0;
    d.attributes[4].format = MTLVertexFormatFloat4;
    d.attributes[4].offset = 48;
    d.attributes[4].bufferIndex = 0;
    d.attributes[5].format = MTLVertexFormatUChar4;
    d.attributes[5].offset = 64;
    d.attributes[5].bufferIndex = 0;
    d.attributes[6].format = MTLVertexFormatFloat4;
    d.attributes[6].offset = 68;
    d.attributes[6].bufferIndex = 0;
    d.layouts[0].stride = 84;
    d.layouts[0].stepRate = 1;
    d.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
    return d;
}

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

static id<MTLRenderPipelineState>
metal_create_pipeline_state(id<MTLDevice> device,
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

static id<MTLRenderPipelineState>
metal_create_skybox_pipeline_state(id<MTLDevice> device,
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

static id<MTLRenderPipelineState>
metal_select_pipeline_state(VGFXMetalContext *ctx, const vgfx3d_draw_cmd_t *cmd, BOOL instanced) {
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

//=============================================================================
// Backend vtable
//=============================================================================

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
        ctx.currentTargetKind = VGFX3D_METAL_TARGET_SWAPCHAIN;

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

        ctx.commandQueue = [device newCommandQueue];
        if (!ctx.commandQueue) {
            NSLog(@ "[Metal] newCommandQueue returned nil");
            return NULL;
        }
        mat4f_identity(ctx->_view);
        mat4f_identity(ctx->_projection);
        mat4f_identity(ctx->_vp);
        mat4f_identity(ctx->_prevVP);
        mat4f_identity(ctx->_invVP);
        ctx->_prevVPValid = NO;

        NSError *error = nil;
        ctx.library = [device newLibraryWithSource:metal_shader_source options:nil error:&error];
        if (!ctx.library) {
            NSLog(@ "[Metal] Shader error: %@", error);
            return NULL;
        }
        /* Shaders compiled successfully */

        id<MTLFunction> vf = [ctx.library newFunctionWithName:@ "vertex_main"];
        id<MTLFunction> vfInstanced = [ctx.library newFunctionWithName:@ "vertex_main_instanced"];
        id<MTLFunction> vfShadow = [ctx.library newFunctionWithName:@ "vertex_shadow"];
        id<MTLFunction> ff = [ctx.library newFunctionWithName:@ "fragment_main"];
        if (!vf || !vfInstanced || !vfShadow || !ff) {
            NSLog(@ "[Metal] required shader entrypoints missing (vf=%@ inst=%@ shadow=%@ ff=%@)",
                  vf,
                  vfInstanced,
                  vfShadow,
                  ff);
            return NULL;
        }

        ctx.pipelineState = metal_create_pipeline_state(device,
                                                        vf,
                                                        ff,
                                                        create_vertex_descriptor(),
                                                        MTLPixelFormatRGBA16Float,
                                                        VGFX3D_METAL_BLEND_OPAQUE,
                                                        NO,
                                                        &error);
        ctx.pipelineStateAlpha = metal_create_pipeline_state(device,
                                                             vf,
                                                             ff,
                                                             create_vertex_descriptor(),
                                                             MTLPixelFormatRGBA16Float,
                                                             VGFX3D_METAL_BLEND_ALPHA,
                                                             YES,
                                                             &error);
        ctx.pipelineStateAdditive = metal_create_pipeline_state(device,
                                                                vf,
                                                                ff,
                                                                create_vertex_descriptor(),
                                                                MTLPixelFormatRGBA16Float,
                                                                VGFX3D_METAL_BLEND_ADDITIVE,
                                                                YES,
                                                                &error);
        ctx.pipelineStateColorOnly = metal_create_pipeline_state(device,
                                                                 vf,
                                                                 ff,
                                                                 create_vertex_descriptor(),
                                                                 MTLPixelFormatBGRA8Unorm,
                                                                 VGFX3D_METAL_BLEND_OPAQUE,
                                                                 NO,
                                                                 &error);
        ctx.pipelineStateColorOnlyAlpha =
            metal_create_pipeline_state(device,
                                        vf,
                                        ff,
                                        create_vertex_descriptor(),
                                        MTLPixelFormatBGRA8Unorm,
                                        VGFX3D_METAL_BLEND_ALPHA,
                                        YES,
                                        &error);
        ctx.pipelineStateColorOnlyAdditive =
            metal_create_pipeline_state(device,
                                        vf,
                                        ff,
                                        create_vertex_descriptor(),
                                        MTLPixelFormatBGRA8Unorm,
                                        VGFX3D_METAL_BLEND_ADDITIVE,
                                        YES,
                                        &error);
        ctx.instancedPipelineState = metal_create_pipeline_state(device,
                                                                 vfInstanced,
                                                                 ff,
                                                                 create_vertex_descriptor(),
                                                                 MTLPixelFormatRGBA16Float,
                                                                 VGFX3D_METAL_BLEND_OPAQUE,
                                                                 NO,
                                                                 &error);
        ctx.instancedPipelineStateAlpha =
            metal_create_pipeline_state(device,
                                        vfInstanced,
                                        ff,
                                        create_vertex_descriptor(),
                                        MTLPixelFormatRGBA16Float,
                                        VGFX3D_METAL_BLEND_ALPHA,
                                        YES,
                                        &error);
        ctx.instancedPipelineStateAdditive =
            metal_create_pipeline_state(device,
                                        vfInstanced,
                                        ff,
                                        create_vertex_descriptor(),
                                        MTLPixelFormatRGBA16Float,
                                        VGFX3D_METAL_BLEND_ADDITIVE,
                                        YES,
                                        &error);
        ctx.instancedPipelineStateColorOnly =
            metal_create_pipeline_state(device,
                                        vfInstanced,
                                        ff,
                                        create_vertex_descriptor(),
                                        MTLPixelFormatBGRA8Unorm,
                                        VGFX3D_METAL_BLEND_OPAQUE,
                                                                 NO,
                                                                 &error);
        ctx.instancedPipelineStateColorOnlyAlpha =
            metal_create_pipeline_state(device,
                                        vfInstanced,
                                        ff,
                                        create_vertex_descriptor(),
                                        MTLPixelFormatBGRA8Unorm,
                                        VGFX3D_METAL_BLEND_ALPHA,
                                        YES,
                                        &error);
        ctx.instancedPipelineStateColorOnlyAdditive =
            metal_create_pipeline_state(device,
                                        vfInstanced,
                                        ff,
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
            return NULL;
        }

        MTLDepthStencilDescriptor *dd = [[MTLDepthStencilDescriptor alloc] init];
        dd.depthCompareFunction = MTLCompareFunctionLess;
        dd.depthWriteEnabled = YES;
        ctx.depthState = [device newDepthStencilStateWithDescriptor:dd];
        /* Depth state for transparent draws: test ON, write OFF */
        dd.depthWriteEnabled = NO;
        ctx.depthStateNoWrite = [device newDepthStencilStateWithDescriptor:dd];
        dd.depthCompareFunction = MTLCompareFunctionLessEqual;
        ctx.skyboxDepthState = [device newDepthStencilStateWithDescriptor:dd];

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

        /* MTL-03: Shared sampler (linear filter, repeat wrap) used for all textures */
        {
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

        /* MTL-12: Shadow pipeline (depth-only, no fragment shader) */
        {
            MTLRenderPipelineDescriptor *spd = [[MTLRenderPipelineDescriptor alloc] init];
            spd.vertexFunction = vfShadow;
            spd.fragmentFunction = nil;
            spd.vertexDescriptor = create_vertex_descriptor();
            spd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
            spd.colorAttachments[0].pixelFormat = MTLPixelFormatInvalid;
            NSError *shadowErr = nil;
            ctx.shadowPipeline = [device newRenderPipelineStateWithDescriptor:spd error:&shadowErr];
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
                        "    float2 prevUv = prevClip.xy / max(prevClip.w, 0.0001) * 0.5 + 0.5;\n"
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
                              "        float br = dot(color, float3(0.2126,0.7152,0.0722));\n"
                              "        if (br > p.bloomThreshold)\n"
                              "            color += (color - p.bloomThreshold) * p.bloomStrength;\n"
                              "    }\n"
                              "    if (p.tonemapMode == 1) {\n"
                              "        color *= p.tonemapExposure;\n"
                              "        color = color / (color + 1.0);\n"
                              "    }\n"
                              "    if (p.tonemapMode == 2) {\n"
                              "        float3 x = color * p.tonemapExposure;\n"
                              "        color = (x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14);\n"
                              "    }\n"
                              "    if (p.colorGradeEnabled) {\n"
                              "        color = (color - 0.5) * p.cgContrast + 0.5;\n"
                              "        color += p.cgBright;\n"
                              "        float luma = dot(color, float3(0.299,0.587,0.114));\n"
                              "        color = mix(float3(luma), color, p.cgSat);\n"
                              "    }\n"
	                              "    if (p.vignetteEnabled) {\n"
	                              "        float2 ctr = in.uv - 0.5;\n"
	                              "        float d = length(ctr);\n"
	                              "        float vig = 1.0 - smoothstep(p.vigRadius, p.vigRadius + "
	                              "p.vigSoftness, d);\n"
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
                 "        worldDir = normalize(p.cameraForward.xyz);\n"
                 "    } else {\n"
                 "        float4 clip = float4(in.ndc, 1.0, 1.0);\n"
                 "        float4 view = p.inverseProjection * clip;\n"
                 "        float3 viewDir = normalize(view.xyz / max(fabs(view.w), 0.0001));\n"
                 "        worldDir = normalize((p.inverseViewRotation * float4(viewDir, "
                 "0.0)).xyz);\n"
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

static void metal_destroy_ctx(void *ctx_ptr) {
    if (!ctx_ptr)
        return;
    @autoreleasepool {
        VGFXMetalContext *ctx = (__bridge_transfer VGFXMetalContext *)ctx_ptr;
        [ctx.metalLayer removeFromSuperlayer];
        ctx.metalLayer = nil;
        (void)ctx; /* ARC releases all properties */
    }
}

static void metal_clear(void *ctx_ptr, vgfx_window_t win, float r, float g, float b) {
    (void)win;
    VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
    ctx.clearR = r;
    ctx.clearG = g;
    ctx.clearB = b;
}

static void metal_begin_frame(void *ctx_ptr, const vgfx3d_camera_params_t *cam) {
    @autoreleasepool {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
        BOOL new_command_buffer = NO;
        float inv_vp[16];
        vgfx3d_metal_frame_history_t history;
        int8_t load_existing_color;
        int8_t is_overlay_pass;
        if (!ctx)
            return;

        ctx.frameSerial++;
        float vp[16];
        mat4f_mul(cam->projection, cam->view, vp);
        if (vgfx3d_invert_matrix4(vp, inv_vp) != 0)
            mat4f_identity(inv_vp);

        ctx.currentTargetKind = vgfx3d_metal_choose_target_kind(
            ctx.rttActive ? 1 : 0, ctx.gpuPostfxEnabled, cam->load_existing_color);
        is_overlay_pass = ctx.currentTargetKind == VGFX3D_METAL_TARGET_OVERLAY ? 1 : 0;
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
            new_command_buffer = YES;
        } else if (!ctx.frameBuffers)
            ctx.frameBuffers = [NSMutableArray arrayWithCapacity:32];
        if (new_command_buffer || !is_overlay_pass)
            ctx.shadowActive = NO;

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

/// MTL-03: Retrieve or create a cached MTLTexture from a Pixels object pointer.
/// Cache uses the stable Pixels cache key so allocator-reused addresses do not
/// alias a fresh image to an older GPU upload. Conversion is RGBA→BGRA.
static id<MTLTexture> metal_get_cached_texture(VGFXMetalContext *ctx, const void *pixels_ptr) {
    int32_t tw = 0;
    int32_t th = 0;
    uint8_t *rgba = NULL;
    int32_t mip_count;
    uint64_t cache_key;
    NSValue *key = [NSValue valueWithPointer:pixels_ptr];
    VGFXMetalTextureCacheEntry *cached = ctx.textureCache[key];
    cache_key = vgfx3d_get_pixels_cache_key(pixels_ptr);
    if (cached && cached.texture && cached.generation == cache_key) {
        cached.lastUsedFrame = ctx.frameSerial;
        return cached.texture;
    }
    if (vgfx3d_unpack_pixels_rgba(pixels_ptr, &tw, &th, &rgba) != 0 || !rgba)
        return nil;

    mip_count = vgfx3d_metal_compute_mip_count(tw, th);
    if (!cached || !cached.texture || (int32_t)cached.texture.width != tw ||
        (int32_t)cached.texture.height != th) {
        MTLTextureDescriptor *texDesc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                               width:(NSUInteger)tw
                                                              height:(NSUInteger)th
                                                           mipmapped:(mip_count > 1)];
        texDesc.usage = MTLTextureUsageShaderRead;
        texDesc.storageMode = MTLStorageModeShared;
        if (!cached)
            cached = [[VGFXMetalTextureCacheEntry alloc] init];
        cached.texture = [ctx.device newTextureWithDescriptor:texDesc];
    }
    if (!metal_upload_rgba_to_bgra_texture(cached.texture, rgba, tw, th)) {
        free(rgba);
        return nil;
    }
    free(rgba);
    cached.generation = cache_key;
    cached.lastUsedFrame = ctx.frameSerial;
    metal_generate_mipmaps(ctx, cached.texture);
    ctx.textureCache[key] = cached;
    return cached.texture;
}

static id<MTLTexture> metal_get_cached_cubemap(VGFXMetalContext *ctx, const rt_cubemap3d *cubemap) {
    int32_t face_size = 0;
    uint8_t *faces[6];
    int32_t mip_count;
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
    if (vgfx3d_unpack_cubemap_faces_rgba(cubemap, &face_size, faces) != 0)
        return nil;

    mip_count = vgfx3d_metal_compute_mip_count(face_size, face_size);
    if (!cached || !cached.texture || (int32_t)cached.texture.width != face_size) {
        MTLTextureDescriptor *cubeDesc =
            [MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                  size:(NSUInteger)face_size
                                                             mipmapped:(mip_count > 1)];
        cubeDesc.usage = MTLTextureUsageShaderRead;
        cubeDesc.storageMode = MTLStorageModeShared;
        if (!cached)
            cached = [[VGFXMetalCubemapCacheEntry alloc] init];
        cached.texture = [ctx.device newTextureWithDescriptor:cubeDesc];
    }

    for (NSUInteger face = 0; face < 6; face++) {
        size_t pixel_count = (size_t)face_size * (size_t)face_size;
        uint8_t *bgra = (uint8_t *)malloc(pixel_count * 4u);
        if (!bgra) {
            for (int cleanup = 0; cleanup < 6; cleanup++)
                free(faces[cleanup]);
            return nil;
        }
        for (size_t i = 0; i < pixel_count; i++) {
            bgra[i * 4 + 0] = faces[face][i * 4 + 2];
            bgra[i * 4 + 1] = faces[face][i * 4 + 1];
            bgra[i * 4 + 2] = faces[face][i * 4 + 0];
            bgra[i * 4 + 3] = faces[face][i * 4 + 3];
        }
        [cached.texture
            replaceRegion:MTLRegionMake2D(0, 0, (NSUInteger)face_size, (NSUInteger)face_size)
              mipmapLevel:0
                    slice:face
                withBytes:bgra
              bytesPerRow:(NSUInteger)(face_size * 4)
            bytesPerImage:(NSUInteger)(face_size * face_size * 4)];
        free(bgra);
    }

    for (int cleanup = 0; cleanup < 6; cleanup++)
        free(faces[cleanup]);

    cached.generation = generation;
    cached.lastUsedFrame = ctx.frameSerial;
    metal_generate_mipmaps(ctx, cached.texture);
    ctx.cubemapCache[key] = cached;
    return cached.texture;
}

static VGFXMetalMorphCacheEntry *
metal_get_cached_morph_entry(VGFXMetalContext *ctx, const vgfx3d_draw_cmd_t *cmd) {
    NSValue *key;
    VGFXMetalMorphCacheEntry *entry;
    size_t bytes;

    if (!ctx || !ctx.morphCache || !cmd || !cmd->morph_key || cmd->morph_revision == 0 ||
        !cmd->morph_deltas || !cmd->morph_weights || cmd->morph_shape_count <= 0 ||
        cmd->vertex_count == 0) {
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

    bytes = (size_t)cmd->morph_shape_count * cmd->vertex_count * 3u * sizeof(float);
    entry = entry ? entry : [[VGFXMetalMorphCacheEntry alloc] init];
    entry.deltaBuffer = metal_new_shared_buffer(ctx, cmd->morph_deltas, bytes);
    entry.normalBuffer = cmd->morph_normal_deltas
                             ? metal_new_shared_buffer(ctx, cmd->morph_normal_deltas, bytes)
                             : nil;
    entry.key = cmd->morph_key;
    entry.revision = cmd->morph_revision;
    entry.shapeCount = cmd->morph_shape_count;
    entry.vertexCount = cmd->vertex_count;
    entry.hasNormalDeltas = cmd->morph_normal_deltas ? 1 : 0;
    entry.lastUsedFrame = ctx.frameSerial;
    ctx.morphCache[key] = entry;
    return entry;
}

static void metal_bind_bone_palettes(VGFXMetalContext *ctx,
                                     const vgfx3d_draw_cmd_t *cmd,
                                     int has_skinning,
                                     int has_prev_skinning) {
    float packed_palette[VGFX3D_METAL_MAX_BONES * 16];
    size_t palette_bytes = sizeof(packed_palette);

    if (!ctx || !cmd || !has_skinning)
        return;

    vgfx3d_metal_pack_bone_palette(packed_palette, cmd->bone_palette, cmd->bone_count);
    id<MTLBuffer> bone_buf = metal_new_shared_buffer(ctx, packed_palette, palette_bytes);
    [ctx.encoder setVertexBuffer:bone_buf offset:0 atIndex:3];
    if (ctx.frameBuffers && bone_buf)
        [ctx.frameBuffers addObject:bone_buf];
    if (has_prev_skinning) {
        id<MTLBuffer> prev_bone_buf;
        vgfx3d_metal_pack_bone_palette(packed_palette, cmd->prev_bone_palette, cmd->bone_count);
        prev_bone_buf = metal_new_shared_buffer(ctx, packed_palette, palette_bytes);
        [ctx.encoder setVertexBuffer:prev_bone_buf offset:0 atIndex:7];
        if (ctx.frameBuffers && prev_bone_buf)
            [ctx.frameBuffers addObject:prev_bone_buf];
    }
}

static void metal_bind_morph_payload(VGFXMetalContext *ctx, const vgfx3d_draw_cmd_t *cmd) {
    VGFXMetalMorphCacheEntry *cached_entry;
    id<MTLBuffer> delta_buf = nil;
    id<MTLBuffer> normal_buf = nil;
    size_t delta_bytes;
    size_t weight_bytes;

    if (!ctx || !cmd || !cmd->morph_deltas || !cmd->morph_weights || cmd->morph_shape_count <= 0)
        return;

    cached_entry = metal_get_cached_morph_entry(ctx, cmd);
    if (cached_entry) {
        delta_buf = cached_entry.deltaBuffer;
        normal_buf = cached_entry.normalBuffer;
    } else {
        delta_bytes = (size_t)cmd->morph_shape_count * cmd->vertex_count * 3u * sizeof(float);
        delta_buf = metal_new_shared_buffer(ctx, cmd->morph_deltas, delta_bytes);
        normal_buf = cmd->morph_normal_deltas
                         ? metal_new_shared_buffer(ctx, cmd->morph_normal_deltas, delta_bytes)
                         : nil;
        if (ctx.frameBuffers && delta_buf)
            [ctx.frameBuffers addObject:delta_buf];
        if (ctx.frameBuffers && normal_buf)
            [ctx.frameBuffers addObject:normal_buf];
    }

    weight_bytes = (size_t)cmd->morph_shape_count * sizeof(float);
    if (delta_buf)
        [ctx.encoder setVertexBuffer:delta_buf offset:0 atIndex:4];
    if (normal_buf)
        [ctx.encoder setVertexBuffer:normal_buf offset:0 atIndex:9];
    [ctx.encoder setVertexBytes:cmd->morph_weights length:weight_bytes atIndex:5];
    if (cmd->prev_morph_weights)
        [ctx.encoder setVertexBytes:cmd->prev_morph_weights length:weight_bytes atIndex:8];
}

static void metal_ensure_instance_storage(VGFXMetalContext *ctx, int32_t instance_count) {
    int32_t needed_capacity;
    int32_t next_capacity;
    NSUInteger byte_count;

    if (!ctx || instance_count <= 0)
        return;

    needed_capacity = instance_count;
    next_capacity =
        vgfx3d_metal_next_capacity((int32_t)ctx.instanceCapacity, needed_capacity, 64);
    if ((NSUInteger)next_capacity > ctx.instanceCapacity || !ctx.instanceScratch) {
        byte_count = (NSUInteger)next_capacity * sizeof(vgfx3d_metal_instance_data_t);
        ctx.instanceScratch = [NSMutableData dataWithLength:byte_count];
        ctx.instanceCapacity = (NSUInteger)next_capacity;
    }
    if (!ctx.instanceBuf || ctx.instanceBuf.length < ctx.instanceScratch.length) {
        ctx.instanceBuf = metal_new_shared_buffer_with_length(ctx, ctx.instanceScratch.length);
    }
}

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
        if (!ctx || !ctx.encoder || !ctx.inFrame)
            return;

        blend_mode = vgfx3d_metal_choose_blend_mode(cmd);
        [ctx.encoder setRenderPipelineState:metal_select_pipeline_state(ctx, cmd, NO)];
        [ctx.encoder setCullMode:backface_cull ? MTLCullModeBack : MTLCullModeNone];

        /* MTL-08: Wireframe mode via fill mode toggle */
        [ctx.encoder
            setTriangleFillMode:wireframe ? MTLTriangleFillModeLines : MTLTriangleFillModeFill];

        /* Switch depth state: no Z-write for transparent draws */
        if (blend_mode != VGFX3D_METAL_BLEND_OPAQUE)
            [ctx.encoder setDepthStencilState:ctx.depthStateNoWrite];
        else
            [ctx.encoder setDepthStencilState:ctx.depthState];

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
        transpose4x4(ctx->_vp, vp_t);
        memcpy(obj.vp, vp_t, sizeof(float) * 16);
        vgfx3d_compute_normal_matrix4(cmd->model_matrix, normal_m);
        transpose4x4(normal_m, obj.nm);
        int capped_bone_count = cmd->bone_count > 128 ? 128 : cmd->bone_count;
        int has_skinning = (cmd->bone_palette && capped_bone_count > 0) ? 1 : 0;
        int has_prev_skinning = (has_skinning && cmd->prev_bone_palette) ? 1 : 0;
        obj.flags0[0] = has_skinning;
        obj.flags0[1] = has_prev_skinning;
        obj.flags0[2] = cmd->morph_shape_count;
        obj.flags0[3] = (int32_t)cmd->vertex_count;
        obj.flags1[0] = cmd->has_prev_model_matrix ? 1 : 0;
        obj.flags1[1] =
            (cmd->morph_deltas && cmd->morph_shape_count > 0 && cmd->prev_morph_weights) ? 1 : 0;
        obj.flags1[2] = 0;
        obj.flags1[3] =
            (cmd->morph_deltas && cmd->morph_shape_count > 0 && cmd->morph_normal_deltas) ? 1 : 0;
        [ctx.encoder setVertexBytes:&obj length:sizeof(obj) atIndex:1];

        /* MTL-09: Bind bone palette if skinning active */
        metal_bind_bone_palettes(ctx, cmd, has_skinning, has_prev_skinning);

        /* MTL-10: Bind morph deltas/weights if morph active */
        metal_bind_morph_payload(ctx, cmd);

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
        scene.fog_params[2] = ctx.shadowBias;
        scene.counts[0] = light_count < 0 ? 0 : (light_count > 8 ? 8 : light_count);
        /* MTL-12: Shadow mapping */
        scene.counts[1] = ctx.shadowActive ? 1 : 0;
        memcpy(scene.camera_forward, ctx->_camForward, sizeof(float) * 3);
        transpose4x4(ctx.frameHistory.draw_prev_vp, scene.prev_vp);
        if (ctx.shadowActive)
            transpose4x4(ctx->_shadowLightVP, scene.shadow_vp);
        [ctx.encoder setVertexBytes:&scene length:sizeof(scene) atIndex:2];
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
        mat.flags0[0] = cmd->texture ? 1 : 0;
        mat.flags0[1] = cmd->unlit;
        mat.flags0[2] = cmd->normal_map ? 1 : 0;
        mat.flags0[3] = cmd->specular_map ? 1 : 0;
        mat.flags1[0] = cmd->emissive_map ? 1 : 0;
        mat.flags1[1] = (cmd->env_map && cmd->reflectivity > 0.0001f) ? 1 : 0;
        mat.flags1[2] = cmd->has_splat;
        mat.pbrFlags[0] = cmd->workflow;
        mat.pbrFlags[1] = cmd->alpha_mode;
        mat.pbrFlags[2] = cmd->metallic_roughness_map ? 1 : 0;
        mat.pbrFlags[3] = cmd->ao_map ? 1 : 0;
        if (cmd->has_splat) {
            for (int si = 0; si < 4; si++)
                mat.splatScales[si] = cmd->splat_layer_scales[si];
        }
        mat.shadingModel = cmd->shading_model;
        memcpy(mat.customParams, cmd->custom_params, sizeof(float) * 8);
        [ctx.encoder setFragmentBytes:&mat length:sizeof(mat) atIndex:1];

        /* Bind default textures to all shader slots. */
        for (int slot = 0; slot < 10; slot++)
            [ctx.encoder setFragmentTexture:ctx.defaultTexture atIndex:slot];
        [ctx.encoder setFragmentTexture:ctx.defaultTexture atIndex:11];
        [ctx.encoder setFragmentTexture:ctx.defaultTexture atIndex:12];
        [ctx.encoder setFragmentTexture:ctx.defaultCubemap atIndex:10];
        /* MTL-12: Bind shadow depth texture to slot 4 */
        if (ctx.shadowActive && ctx.shadowDepthTexture)
            [ctx.encoder setFragmentTexture:ctx.shadowDepthTexture atIndex:4];
        [ctx.encoder setFragmentSamplerState:ctx.sharedSampler atIndex:0];
        if (ctx.shadowSampler)
            [ctx.encoder setFragmentSamplerState:ctx.shadowSampler atIndex:1];
        if (ctx.cubeSampler)
            [ctx.encoder setFragmentSamplerState:ctx.cubeSampler atIndex:2];

        /* MTL-03: Bind cached textures for each material map slot */
        if (cmd->texture) {
            id<MTLTexture> tex = metal_get_cached_texture(ctx, cmd->texture);
            if (tex)
                [ctx.encoder setFragmentTexture:tex atIndex:0];
        }
        if (cmd->normal_map) {
            id<MTLTexture> tex = metal_get_cached_texture(ctx, cmd->normal_map);
            if (tex)
                [ctx.encoder setFragmentTexture:tex atIndex:1];
        }
        if (cmd->specular_map) {
            id<MTLTexture> tex = metal_get_cached_texture(ctx, cmd->specular_map);
            if (tex)
                [ctx.encoder setFragmentTexture:tex atIndex:2];
        }
        if (cmd->emissive_map) {
            id<MTLTexture> tex = metal_get_cached_texture(ctx, cmd->emissive_map);
            if (tex)
                [ctx.encoder setFragmentTexture:tex atIndex:3];
        }
        if (cmd->metallic_roughness_map) {
            id<MTLTexture> tex = metal_get_cached_texture(ctx, cmd->metallic_roughness_map);
            if (tex)
                [ctx.encoder setFragmentTexture:tex atIndex:11];
        }
        if (cmd->ao_map) {
            id<MTLTexture> tex = metal_get_cached_texture(ctx, cmd->ao_map);
            if (tex)
                [ctx.encoder setFragmentTexture:tex atIndex:12];
        }
        /* MTL-14: Terrain splat textures (slots 5-9) */
        if (cmd->has_splat && cmd->splat_map) {
            id<MTLTexture> sp = metal_get_cached_texture(ctx, cmd->splat_map);
            if (sp)
                [ctx.encoder setFragmentTexture:sp atIndex:5];
            for (int si = 0; si < 4; si++) {
                if (cmd->splat_layers[si]) {
                    id<MTLTexture> lt = metal_get_cached_texture(ctx, cmd->splat_layers[si]);
                    if (lt)
                        [ctx.encoder setFragmentTexture:lt atIndex:6 + si];
                }
            }
        }
        if (cmd->env_map && cmd->reflectivity > 0.0001f) {
            id<MTLTexture> envTex =
                metal_get_cached_cubemap(ctx, (const rt_cubemap3d *)cmd->env_map);
            if (envTex)
                [ctx.encoder setFragmentTexture:envTex atIndex:10];
        }

        /* Lights — always set buffer 2, even if empty (prevents validation warnings) */
        {
            mtl_light_t ml[8];
            memset(ml, 0, sizeof(ml));
            for (int32_t i = 0; i < light_count && i < 8; i++) {
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
            int32_t buf_count = light_count > 0 ? (light_count > 8 ? 8 : light_count) : 1;
            [ctx.encoder setFragmentBytes:ml length:sizeof(mtl_light_t) * buf_count atIndex:2];
        }

        [ctx.encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                indexCount:cmd->index_count
                                 indexType:MTLIndexTypeUInt32
                               indexBuffer:ib
                         indexBufferOffset:0];
    }
}

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
        }

        ctx.encoder = nil;
        ctx.inFrame = NO;
    }
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

        final_texture = metal_active_readback_texture(ctx);
        if (!final_texture)
            final_texture = ctx.offscreenColor;
        if (metal_present_texture(ctx, final_texture))
            metal_commit_pending(ctx, NO);
        else {
            metal_commit_pending(ctx, YES);
            metal_present_texture_to_framebuffer(ctx, final_texture);
        }
    }
}

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

static int metal_readback_rgba(
    void *ctx_ptr, uint8_t *dst_rgba, int32_t w, int32_t h, int32_t stride) {
    if (!ctx_ptr)
        return 0;
    @autoreleasepool {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
        id<MTLTexture> texture = nil;
        vgfx3d_postfx_snapshot_t snapshot_copy;
        const vgfx3d_postfx_snapshot_t *snapshot = NULL;
        vgfx3d_metal_readback_kind_t readback_kind;
        if (!ctx || !dst_rgba || w <= 0 || h <= 0 || stride < w * 4)
            return 0;

        readback_kind = vgfx3d_metal_choose_readback_kind(
            (ctx.gpuPostfxEnabled && ctx.gpuPostfxSnapshotValid) ? 1 : 0);
        if (!ctx.rttActive && ctx.cmdBuf) {
            if (readback_kind == VGFX3D_METAL_READBACK_POSTFX_COMPOSITE) {
                if (ctx.gpuPostfxSnapshotValid) {
                    snapshot_copy = ctx.gpuPostfxSnapshot;
                    snapshot = &snapshot_copy;
                }
                texture = metal_encode_postfx_if_needed(ctx, snapshot);
            } else if (ctx.currentTargetKind == VGFX3D_METAL_TARGET_SWAPCHAIN &&
                       !metal_capture_current_drawable_to_display_texture(ctx)) {
                return 0;
            }
            metal_commit_pending(ctx, YES);
        }
        texture = metal_active_readback_texture(ctx);
        return metal_copy_texture_to_rgba(texture, dst_rgba, w, h, stride);
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
            ctx.rttActive = NO;
            ctx.rttColorTexture = nil;
            ctx.rttMotionTexture = nil;
            ctx.rttDepthTexture = nil;
            ctx.rttTarget = NULL;
            ctx.displayTexture = nil;
            ctx.postfxEncodedThisFrame = 0;
            return;
        }

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
        ctx.rttActive = YES;
    }
}

//=============================================================================
// MTL-12: Shadow mapping
//=============================================================================

static void metal_shadow_begin(
    void *ctx_ptr, float *depth_buf, int32_t w, int32_t h, const float *light_vp) {
    @autoreleasepool {
        (void)depth_buf; /* GPU shadows use MTLTexture, not CPU buffer */
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
        if (!ctx || !ctx.shadowPipeline)
            return;

        /* Create/recreate shadow depth texture if size changed */
        if (!ctx.shadowDepthTexture || (int32_t)ctx.shadowDepthTexture.width != w ||
            (int32_t)ctx.shadowDepthTexture.height != h) {
            MTLTextureDescriptor *desc =
                [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
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
        if (ctx.encoder) {
            [ctx.encoder endEncoding];
            ctx.encoder = nil;
        }

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

static void metal_shadow_draw(void *ctx_ptr, const vgfx3d_draw_cmd_t *cmd) {
    @autoreleasepool {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
        id<MTLBuffer> vb;
        id<MTLBuffer> ib;
        int has_skinning;
        if (!ctx || !ctx.encoder)
            return;

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
        transpose4x4(ctx->_shadowLightVP, lvp_t);
        memcpy(obj.vp, lvp_t, sizeof(float) * 16);
        memcpy(obj.nm, obj.m, sizeof(float) * 16);
        has_skinning = (cmd->bone_palette && cmd->bone_count > 0) ? 1 : 0;
        obj.flags0[0] = has_skinning;
        obj.flags0[2] = cmd->morph_shape_count;
        obj.flags0[3] = (int32_t)cmd->vertex_count;
        [ctx.encoder setVertexBytes:&obj length:sizeof(obj) atIndex:1];
        metal_bind_bone_palettes(ctx, cmd, has_skinning, 0);
        metal_bind_morph_payload(ctx, cmd);

        [ctx.encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                indexCount:cmd->index_count
                                 indexType:MTLIndexTypeUInt32
                               indexBuffer:ib
                         indexBufferOffset:0];
    }
}

static void metal_shadow_end(void *ctx_ptr, float bias) {
    @autoreleasepool {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
        if (!ctx || !ctx.encoder)
            return;
        [ctx.encoder endEncoding];
        ctx.encoder = nil;
        ctx.shadowActive = YES;
        ctx.shadowBias = bias;
        metal_begin_scene_encoder(ctx, YES, YES);
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
                                        int8_t backface_cull) {
    @autoreleasepool {
        (void)win;
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
        vgfx3d_metal_instance_data_t *instance_data;
        id<MTLBuffer> vb;
        id<MTLBuffer> ib;
        vgfx3d_metal_blend_mode_t blend_mode;
        if (!ctx || !ctx.encoder || !ctx.inFrame || instance_count <= 0)
            return;

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
        if (blend_mode != VGFX3D_METAL_BLEND_OPAQUE)
            [ctx.encoder setDepthStencilState:ctx.depthStateNoWrite];
        else
            [ctx.encoder setDepthStencilState:ctx.depthState];

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
        int capped_bone_count = cmd->bone_count > 128 ? 128 : cmd->bone_count;
        int has_skinning = (cmd->bone_palette && capped_bone_count > 0) ? 1 : 0;
        int has_prev_skinning = (has_skinning && cmd->prev_bone_palette) ? 1 : 0;
        mat4f_identity(obj.m);
        mat4f_identity(obj.prev_m);
        transpose4x4(ctx->_vp, vp_t);
        memcpy(obj.vp, vp_t, sizeof(float) * 16);
        mat4f_identity(obj.nm);
        obj.flags0[0] = has_skinning;
        obj.flags0[1] = has_prev_skinning;
        obj.flags0[2] = cmd->morph_shape_count;
        obj.flags0[3] = (int32_t)cmd->vertex_count;
        obj.flags1[0] = 0;
        obj.flags1[1] =
            (cmd->morph_deltas && cmd->morph_shape_count > 0 && cmd->prev_morph_weights) ? 1 : 0;
        obj.flags1[2] = cmd->has_prev_instance_matrices ? 1 : 0;
        obj.flags1[3] =
            (cmd->morph_deltas && cmd->morph_shape_count > 0 && cmd->morph_normal_deltas) ? 1 : 0;
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
        scene.fog_params[2] = ctx.shadowBias;
        scene.counts[0] = light_count < 0 ? 0 : (light_count > 8 ? 8 : light_count);
        scene.counts[1] = ctx.shadowActive ? 1 : 0;
        memcpy(scene.camera_forward, ctx->_camForward, sizeof(float) * 3);
        transpose4x4(ctx.frameHistory.draw_prev_vp, scene.prev_vp);
        if (ctx.shadowActive)
            transpose4x4(ctx->_shadowLightVP, scene.shadow_vp);
        [ctx.encoder setVertexBytes:&scene length:sizeof(scene) atIndex:2];
        [ctx.encoder setFragmentBytes:&scene length:sizeof(scene) atIndex:0];

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
        mat.flags0[0] = cmd->texture ? 1 : 0;
        mat.flags0[1] = cmd->unlit;
        mat.flags0[2] = cmd->normal_map ? 1 : 0;
        mat.flags0[3] = cmd->specular_map ? 1 : 0;
        mat.flags1[0] = cmd->emissive_map ? 1 : 0;
        mat.flags1[1] = (cmd->env_map && cmd->reflectivity > 0.0001f) ? 1 : 0;
        mat.flags1[2] = cmd->has_splat;
        mat.pbrFlags[0] = cmd->workflow;
        mat.pbrFlags[1] = cmd->alpha_mode;
        mat.pbrFlags[2] = cmd->metallic_roughness_map ? 1 : 0;
        mat.pbrFlags[3] = cmd->ao_map ? 1 : 0;
        if (cmd->has_splat) {
            for (int si = 0; si < 4; si++)
                mat.splatScales[si] = cmd->splat_layer_scales[si];
        }
        mat.shadingModel = cmd->shading_model;
        memcpy(mat.customParams, cmd->custom_params, sizeof(float) * 8);
        [ctx.encoder setFragmentBytes:&mat length:sizeof(mat) atIndex:1];

        metal_bind_bone_palettes(ctx, cmd, has_skinning, has_prev_skinning);
        metal_bind_morph_payload(ctx, cmd);

        /* Default textures + sampler */
        for (int slot = 0; slot < 10; slot++)
            [ctx.encoder setFragmentTexture:ctx.defaultTexture atIndex:slot];
        [ctx.encoder setFragmentTexture:ctx.defaultTexture atIndex:11];
        [ctx.encoder setFragmentTexture:ctx.defaultTexture atIndex:12];
        [ctx.encoder setFragmentTexture:ctx.defaultCubemap atIndex:10];
        if (ctx.shadowActive && ctx.shadowDepthTexture)
            [ctx.encoder setFragmentTexture:ctx.shadowDepthTexture atIndex:4];
        [ctx.encoder setFragmentSamplerState:ctx.sharedSampler atIndex:0];
        if (ctx.shadowSampler)
            [ctx.encoder setFragmentSamplerState:ctx.shadowSampler atIndex:1];
        if (ctx.cubeSampler)
            [ctx.encoder setFragmentSamplerState:ctx.cubeSampler atIndex:2];
        if (cmd->texture) {
            id<MTLTexture> tex = metal_get_cached_texture(ctx, cmd->texture);
            if (tex)
                [ctx.encoder setFragmentTexture:tex atIndex:0];
        }
        if (cmd->normal_map) {
            id<MTLTexture> tex = metal_get_cached_texture(ctx, cmd->normal_map);
            if (tex)
                [ctx.encoder setFragmentTexture:tex atIndex:1];
        }
        if (cmd->specular_map) {
            id<MTLTexture> tex = metal_get_cached_texture(ctx, cmd->specular_map);
            if (tex)
                [ctx.encoder setFragmentTexture:tex atIndex:2];
        }
        if (cmd->emissive_map) {
            id<MTLTexture> tex = metal_get_cached_texture(ctx, cmd->emissive_map);
            if (tex)
                [ctx.encoder setFragmentTexture:tex atIndex:3];
        }
        if (cmd->metallic_roughness_map) {
            id<MTLTexture> tex = metal_get_cached_texture(ctx, cmd->metallic_roughness_map);
            if (tex)
                [ctx.encoder setFragmentTexture:tex atIndex:11];
        }
        if (cmd->ao_map) {
            id<MTLTexture> tex = metal_get_cached_texture(ctx, cmd->ao_map);
            if (tex)
                [ctx.encoder setFragmentTexture:tex atIndex:12];
        }
        if (cmd->has_splat && cmd->splat_map) {
            id<MTLTexture> sp = metal_get_cached_texture(ctx, cmd->splat_map);
            if (sp)
                [ctx.encoder setFragmentTexture:sp atIndex:5];
            for (int si = 0; si < 4; si++) {
                if (cmd->splat_layers[si]) {
                    id<MTLTexture> lt = metal_get_cached_texture(ctx, cmd->splat_layers[si]);
                    if (lt)
                        [ctx.encoder setFragmentTexture:lt atIndex:6 + si];
                }
            }
        }
        if (cmd->env_map && cmd->reflectivity > 0.0001f) {
            id<MTLTexture> envTex =
                metal_get_cached_cubemap(ctx, (const rt_cubemap3d *)cmd->env_map);
            if (envTex)
                [ctx.encoder setFragmentTexture:envTex atIndex:10];
        }

        /* Lights */
        {
            mtl_light_t ml[8];
            memset(ml, 0, sizeof(ml));
            for (int32_t i = 0; i < light_count && i < 8; i++) {
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
            int32_t bc = light_count > 0 ? (light_count > 8 ? 8 : light_count) : 1;
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
    int32_t tonemapMode;
    float tonemapExposure;
    int32_t fxaaEnabled;
    int32_t colorGradeEnabled;
    float cgBright, cgContrast, cgSat;
    int32_t vignetteEnabled;
    float vigRadius, vigSoftness;
    /* Extended effects (parity with D3D11) */
    int32_t dofEnabled;
    float dofFocusDist, dofAperture, dofMaxBlur;
    int32_t ssaoEnabled;
    float ssaoRadius, ssaoIntensity;
    int32_t ssaoSamples;
    int32_t motionBlurEnabled;
    float motionBlurIntensity;
    int32_t motionBlurSamples;
    int32_t overlayEnabled;
} mtl_postfx_params_t;

static int metal_capture_current_drawable_to_display_texture(VGFXMetalContext *ctx) {
    NSUInteger copy_w;
    NSUInteger copy_h;
    id<MTLBlitCommandEncoder> blit;

    if (!ctx || !ctx.cmdBuf || !ctx.drawable)
        return 0;
    if (!ctx.displayTexture || ctx.displayTexture == ctx.postfxColorTexture ||
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

static id<MTLTexture> metal_encode_postfx_if_needed(
    VGFXMetalContext *ctx, const vgfx3d_postfx_snapshot_t *postfx) {
    MTLRenderPassDescriptor *rp;
    id<MTLRenderCommandEncoder> pfxEncoder;
    mtl_postfx_params_t params;

    if (!ctx)
        return nil;
    if (ctx.postfxEncodedThisFrame && ctx.postfxColorTexture) {
        ctx.displayTexture = ctx.postfxColorTexture;
        return ctx.postfxColorTexture;
    }
    if (!ctx.cmdBuf || !ctx.postfxPipeline || !ctx.postfxColorTexture || !ctx.offscreenColor ||
        !ctx.offscreenMotion || !ctx.depthTexture || !postfx) {
        return nil;
    }

    metal_finish_encoding(ctx);

    rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = ctx.postfxColorTexture;
    rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;

    pfxEncoder = [ctx.cmdBuf renderCommandEncoderWithDescriptor:rp];
    if (!pfxEncoder)
        return nil;

    [pfxEncoder setRenderPipelineState:ctx.postfxPipeline];
    [pfxEncoder setFragmentTexture:ctx.offscreenColor atIndex:0];
    [pfxEncoder setFragmentTexture:ctx.depthTexture atIndex:1];
    [pfxEncoder setFragmentTexture:ctx.offscreenMotion atIndex:2];
    if (ctx.frameHistory.overlay_used_this_frame && ctx.overlayColorTexture)
        [pfxEncoder setFragmentTexture:ctx.overlayColorTexture atIndex:3];
    [pfxEncoder
        setFragmentSamplerState:(ctx.sharedSampler ? ctx.sharedSampler : ctx.defaultSampler)
                        atIndex:0];

    memset(&params, 0, sizeof(params));
    transpose4x4(ctx.frameHistory.scene_inv_vp, params.invViewProjection);
    transpose4x4(ctx.frameHistory.scene_prev_vp, params.prevViewProjection);
    memcpy(params.cameraPosition, ctx.frameHistory.scene_cam_pos, sizeof(float) * 3);
    params.cameraPosition[3] = 1.0f;
    params.invResolution[0] = ctx.width > 0 ? 1.0f / (float)ctx.width : 0.0f;
    params.invResolution[1] = ctx.height > 0 ? 1.0f / (float)ctx.height : 0.0f;
    params.bloomEnabled = postfx->bloom_enabled ? 1 : 0;
    params.bloomThreshold = postfx->bloom_threshold;
    params.bloomStrength = postfx->bloom_intensity;
    params.tonemapMode = (int32_t)postfx->tonemap_mode;
    params.tonemapExposure = postfx->tonemap_exposure;
    params.fxaaEnabled = postfx->fxaa_enabled ? 1 : 0;
    params.colorGradeEnabled = postfx->color_grade_enabled ? 1 : 0;
    params.cgBright = postfx->cg_brightness;
    params.cgContrast = postfx->cg_contrast;
    params.cgSat = postfx->cg_saturation;
    params.vignetteEnabled = postfx->vignette_enabled ? 1 : 0;
    params.vigRadius = postfx->vignette_radius;
    params.vigSoftness = postfx->vignette_softness;
    params.dofEnabled = postfx->dof_enabled ? 1 : 0;
    params.dofFocusDist = postfx->dof_focus_distance;
    params.dofAperture = postfx->dof_aperture;
    params.dofMaxBlur = postfx->dof_max_blur;
    params.ssaoEnabled = postfx->ssao_enabled ? 1 : 0;
    params.ssaoRadius = postfx->ssao_radius;
    params.ssaoIntensity = postfx->ssao_intensity;
    params.ssaoSamples = postfx->ssao_samples;
    params.motionBlurEnabled = postfx->motion_blur_enabled ? 1 : 0;
    params.motionBlurIntensity = postfx->motion_blur_intensity;
    params.motionBlurSamples = postfx->motion_blur_samples;
    params.overlayEnabled = ctx.frameHistory.overlay_used_this_frame ? 1 : 0;
    [pfxEncoder setFragmentBytes:&params length:sizeof(params) atIndex:0];

    [pfxEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    [pfxEncoder endEncoding];
    ctx.postfxEncodedThisFrame = 1;
    ctx.displayTexture = ctx.postfxColorTexture;
    return ctx.postfxColorTexture;
}

/// @brief Metal PostFX presentation: render fullscreen quad with PostFX shader.
/// Runs the fullscreen post-processing pass into an offscreen texture, then
/// presents that texture through the CAMetalLayer drawable when available.
static void metal_present_postfx(void *backend_ctx, const vgfx3d_postfx_snapshot_t *postfx) {
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

        if (metal_present_texture(ctx, final_texture))
            metal_commit_pending(ctx, NO);
        else {
            metal_commit_pending(ctx, YES);
            metal_present_texture_to_framebuffer(ctx, final_texture);
        }
    }
}

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
        default_pipeline = ctx.currentTargetKind == VGFX3D_METAL_TARGET_SCENE ? ctx.pipelineState
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
        ctx.displayTexture = nil;
        if (ctx.cmdBuf)
            metal_commit_pending(ctx, YES);
        if (!ctx.rttActive && ctx.width > 0 && ctx.height > 0)
            metal_recreate_main_targets(ctx, ctx.width, ctx.height);
    }
}

static void metal_set_gpu_postfx_snapshot(void *ctx_ptr, const vgfx3d_postfx_snapshot_t *postfx) {
    @autoreleasepool {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
        vgfx3d_postfx_snapshot_t empty_snapshot;
        if (!ctx)
            return;
        if (!postfx) {
            memset(&empty_snapshot, 0, sizeof(empty_snapshot));
            ctx.gpuPostfxSnapshot = empty_snapshot;
            ctx.gpuPostfxSnapshotValid = 0;
            return;
        }
        ctx.gpuPostfxSnapshot = *postfx;
        ctx.gpuPostfxSnapshotValid = 1;
    }
}

/* Hide the Metal layer during software RTT or 2D mode */
static void metal_hide_gpu_layer(void *backend_ctx) {
    if (!backend_ctx)
        return;
    @autoreleasepool {
        VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)backend_ctx;
        if (ctx.metalLayer)
            ctx.metalLayer.hidden = YES;
    }
}

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
    .set_gpu_postfx_enabled = metal_set_gpu_postfx_enabled,
    .set_gpu_postfx_snapshot = metal_set_gpu_postfx_snapshot,
    .show_gpu_layer = metal_show_gpu_layer,
    .hide_gpu_layer = metal_hide_gpu_layer,
};

#endif /* __APPLE__ && VIPER_ENABLE_GRAPHICS */
