//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/backend/vgfx3d_backend_d3d11.c
// Purpose: Direct3D 11 GPU backend for Viper.Graphics3D (Windows).
//
// Key invariants:
//   - Requires Windows 7+ with D3D11 feature level 11_0
//   - Falls back to software if D3D11 unavailable
//   - HLSL shaders compiled at runtime via D3DCompile
//   - row_major float4x4 in HLSL (matches Viper row-major convention)
//   - Constant buffers packed via vgfx3d_backend_d3d11_shared float4 alignment.
//
// Ownership/Lifetime:
//   - D3D11 device, swapchain, RTVs, DSVs, samplers, blend states, depth
//     states, shaders, and per-mesh GPU caches are owned by the backend
//     context and released in destroy_ctx.
//
// Links: vgfx3d_backend.h, vgfx3d_backend_d3d11_shared.h, plans/3d/03-d3d11-backend.md
//
//===----------------------------------------------------------------------===//

#if defined(_WIN32) && defined(VIPER_ENABLE_GRAPHICS)

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <d3d11.h>
#include <d3dcompiler.h>
#include <windows.h>

#include "rt_textureasset3d.h"
#include "vgfx.h"
#include "vgfx3d_backend.h"
#include "vgfx3d_backend_d3d11_shared.h"
#include "vgfx3d_backend_utils.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")

#define VGFX3D_STR_IMPL(x) #x
#define VGFX3D_STR(x) VGFX3D_STR_IMPL(x)

#ifndef D3D11CalcSubresource
#define D3D11CalcSubresource(MipSlice, ArraySlice, MipLevels)                                      \
    ((MipSlice) + ((ArraySlice) * (MipLevels)))
#endif

static const float k_identity4x4[16] = {
    1.0f,
    0.0f,
    0.0f,
    0.0f,
    0.0f,
    1.0f,
    0.0f,
    0.0f,
    0.0f,
    0.0f,
    1.0f,
    0.0f,
    0.0f,
    0.0f,
    0.0f,
    1.0f,
};

static const char
    *d3d11_shader_source =
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
        "cbuffer PerObject : register(b0) {\n"
        "    row_major float4x4 modelMatrix;\n"
        "    row_major float4x4 prevModelMatrix;\n"
        "    row_major float4x4 normalMatrix;\n"
        "    int hasPrevModelMatrix;\n"
        "    int hasSkinning;\n"
        "    int hasPrevSkinning;\n"
        "    int hasMorphNormalDeltas;\n"
        "    int morphShapeCount;\n"
        "    int vertexCount;\n"
        "    int hasPrevMorphWeights;\n"
        "    int hasPrevInstanceMatrices;\n"
        "    float4 morphWeightsPacked[8];\n"
        "    float4 prevMorphWeightsPacked[8];\n"
        "};\n"
        "\n"
        "cbuffer PerScene : register(b1) {\n"
        "    row_major float4x4 viewProjection;\n"
        "    row_major float4x4 prevViewProjection;\n"
        "    row_major float4x4 shadowVP[" VGFX3D_STR(
            VGFX3D_MAX_SHADOW_LIGHTS) "];\n"
                                      "    float4 cameraPosition;\n"
                                      "    float4 ambientColor;\n"
                                      "    float4 fogColor;\n"
                                      "    float fogNear;\n"
                                      "    float fogFar;\n"
                                      "    float shadowBias;\n"
                                      "    int lightCount;\n"
                                      "    int shadowCount;\n"
                                      "    float3 cameraForward;\n"
                                      "};\n"
                                      "\n"
                                      "cbuffer PerMaterial : register(b2) {\n"
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
                                      "    float3 _materialPad0;\n"
                                      "    float4 customParamsPacked[2];\n"
                                      "    int4 textureUvSets0;\n"
                                      "    int4 textureUvSets1;\n"
                                      "    float4 textureUvTransform0[" VGFX3D_STR(RT_MATERIAL3D_TEXTURE_SLOT_COUNT) "];\n"
                                                                                                                     "    float4 textureUvTransform1[" VGFX3D_STR(RT_MATERIAL3D_TEXTURE_SLOT_COUNT) "];\n"
                                                                                                                                                                                                    "};\n"
                                                                                                                                                                                                    "\n"
                                                                                                                                                                                                    "cbuffer PerLights : register(b3) {\n"
                                                                                                                                                                                                    "    Light lights[" VGFX3D_STR(VGFX3D_MAX_LIGHTS) "];\n"
                                                                                                                                                                                                                                                      "};\n"
                                                                                                                                                                                                                                                      "\n"
                                                                                                                                                                                                                                                      "cbuffer BonesCurrent : register(b4) {\n"
                                                                                                                                                                                                                                                      "    row_major float4x4 bonePalette[256];\n"
                                                                                                                                                                                                                                                      "};\n"
                                                                                                                                                                                                                                                      "\n"
                                                                                                                                                                                                                                                      "cbuffer BonesPrevious : register(b5) {\n"
                                                                                                                                                                                                                                                      "    row_major float4x4 prevBonePalette[256];\n"
                                                                                                                                                                                                                                                      "};\n"
                                                                                                                                                                                                                                                      "\n"
                                                                                                                                                                                                                                                      "Buffer<float> morphDeltas : register(t0);\n"
                                                                                                                                                                                                                                                      "Buffer<float> morphNormalDeltas : register(t1);\n"
                                                                                                                                                                                                                                                      "\n"
                                                                                                                                                                                                                                                      "Texture2D diffuseTex : register(t0);\n"
                                                                                                                                                                                                                                                      "Texture2D normalTex : register(t1);\n"
                                                                                                                                                                                                                                                      "Texture2D specularTex : register(t2);\n"
                                                                                                                                                                                                                                                      "Texture2D emissiveTex : register(t3);\n"
                                                                                                                                                                                                                                                      "Texture2D<float> shadowTex0 : register(t4);\n"
                                                                                                                                                                                                                                                      "Texture2D<float> shadowTex1 : register(t5);\n"
                                                                                                                                                                                                                                                      "Texture2D<float> shadowTex2 : register(t6);\n"
                                                                                                                                                                                                                                                      "Texture2D<float> shadowTex3 : register(t7);\n"
                                                                                                                                                                                                                                                      "Texture2D splatTex : register(t8);\n"
                                                                                                                                                                                                                                                      "Texture2D splatLayer0 : register(t9);\n"
                                                                                                                                                                                                                                                      "Texture2D splatLayer1 : register(t10);\n"
                                                                                                                                                                                                                                                      "Texture2D splatLayer2 : register(t11);\n"
                                                                                                                                                                                                                                                      "Texture2D splatLayer3 : register(t12);\n"
                                                                                                                                                                                                                                                      "TextureCube envTex : register(t13);\n"
                                                                                                                                                                                                                                                      "Texture2D metallicRoughnessTex : register(t14);\n"
                                                                                                                                                                                                                                                      "Texture2D aoTex : register(t15);\n"
                                                                                                                                                                                                                                                      "SamplerState diffuseSampler : register(s0);\n"
                                                                                                                                                                                                                                                      "SamplerComparisonState shadowSampler : register(s1);\n"
                                                                                                                                                                                                                                                      "SamplerState envSampler : register(s2);\n"
                                                                                                                                                                                                                                                      "SamplerState normalSampler : register(s3);\n"
                                                                                                                                                                                                                                                      "SamplerState specularSampler : register(s4);\n"
                                                                                                                                                                                                                                                      "SamplerState emissiveSampler : register(s5);\n"
                                                                                                                                                                                                                                                      "SamplerState metallicRoughnessSampler : register(s6);\n"
                                                                                                                                                                                                                                                      "SamplerState aoSampler : register(s7);\n"
                                                                                                                                                                                                                                                      "\n"
                                                                                                                                                                                                                                                      "struct VS_INPUT {\n"
                                                                                                                                                                                                                                                      "    float3 pos : POSITION;\n"
                                                                                                                                                                                                                                                      "    float3 normal : NORMAL;\n"
                                                                                                                                                                                                                                                      "    float2 uv : TEXCOORD0;\n"
                                                                                                                                                                                                                                                      "    float2 uv1 : TEXCOORD1;\n"
                                                                                                                                                                                                                                                      "    float4 color : COLOR0;\n"
                                                                                                                                                                                                                                                      "    float4 tangent : TANGENT;\n"
                                                                                                                                                                                                                                                      "    uint4 boneIdx : BLENDINDICES;\n"
                                                                                                                                                                                                                                                      "    float4 boneWt : BLENDWEIGHT;\n"
                                                                                                                                                                                                                                                      "};\n"
                                                                                                                                                                                                                                                      "\n"
                                                                                                                                                                                                                                                      "struct VS_INPUT_INSTANCED {\n"
                                                                                                                                                                                                                                                      "    float3 pos : POSITION;\n"
                                                                                                                                                                                                                                                      "    float3 normal : NORMAL;\n"
                                                                                                                                                                                                                                                      "    float2 uv : TEXCOORD0;\n"
                                                                                                                                                                                                                                                      "    float2 uv1 : TEXCOORD1;\n"
                                                                                                                                                                                                                                                      "    float4 color : COLOR0;\n"
                                                                                                                                                                                                                                                      "    float4 tangent : TANGENT;\n"
                                                                                                                                                                                                                                                      "    uint4 boneIdx : BLENDINDICES;\n"
                                                                                                                                                                                                                                                      "    float4 boneWt : BLENDWEIGHT;\n"
                                                                                                                                                                                                                                                      "    float4 instanceModel0 : TEXCOORD4;\n"
                                                                                                                                                                                                                                                      "    float4 instanceModel1 : TEXCOORD5;\n"
                                                                                                                                                                                                                                                      "    float4 instanceModel2 : TEXCOORD6;\n"
                                                                                                                                                                                                                                                      "    float4 instanceModel3 : TEXCOORD7;\n"
                                                                                                                                                                                                                                                      "    float4 instanceNormal0 : TEXCOORD8;\n"
                                                                                                                                                                                                                                                      "    float4 instanceNormal1 : TEXCOORD9;\n"
                                                                                                                                                                                                                                                      "    float4 instanceNormal2 : TEXCOORD10;\n"
                                                                                                                                                                                                                                                      "    float4 instanceNormal3 : TEXCOORD11;\n"
                                                                                                                                                                                                                                                      "    float4 prevModel0 : TEXCOORD12;\n"
                                                                                                                                                                                                                                                      "    float4 prevModel1 : TEXCOORD13;\n"
                                                                                                                                                                                                                                                      "    float4 prevModel2 : TEXCOORD14;\n"
                                                                                                                                                                                                                                                      "    float4 prevModel3 : TEXCOORD15;\n"
                                                                                                                                                                                                                                                      "};\n"
                                                                                                                                                                                                                                                      "\n"
                                                                                                                                                                                                                                                      "struct PS_INPUT {\n"
                                                                                                                                                                                                                                                      "    float4 pos : SV_POSITION;\n"
                                                                                                                                                                                                                                                      "    float3 worldPos : TEXCOORD0;\n"
                                                                                                                                                                                                                                                      "    float3 normal : TEXCOORD1;\n"
                                                                                                                                                                                                                                                      "    float2 uv : TEXCOORD2;\n"
                                                                                                                                                                                                                                                      "    float4 tangent : TEXCOORD3;\n"
                                                                                                                                                                                                                                                      "    float4 color : COLOR0;\n"
                                                                                                                                                                                                                                                      "    float4 currClip : TEXCOORD4;\n"
                                                                                                                                                                                                                                                      "    float4 prevClip : TEXCOORD5;\n"
                                                                                                                                                                                                                                                      "    float hasObjectHistory : TEXCOORD6;\n"
                                                                                                                                                                                                                                                      "    float2 uv1 : TEXCOORD7;\n"
                                                                                                                                                                                                                                                      "};\n"
                                                                                                                                                                                                                                                      "\n"
                                                                                                                                                                                                                                                      "struct PS_OUTPUT {\n"
                                                                                                                                                                                                                                                      "    float4 color : SV_Target0;\n"
                                                                                                                                                                                                                                                      "    float4 motion : SV_Target1;\n"
                                                                                                                                                                                                                                                      "};\n"
                                                                                                                                                                                                                                                      "\n"
                                                                                                                                                                                                                                                      "float readPackedScalar8(float4 packed[8], int idx) {\n"
                                                                                                                                                                                                                                                      "    idx = clamp(idx, 0, 31);\n"
                                                                                                                                                                                                                                                      "    int vecIdx = idx >> 2;\n"
                                                                                                                                                                                                                                                      "    int lane = idx & 3;\n"
                                                                                                                                                                                                                                                      "    float4 v = packed[vecIdx];\n"
                                                                                                                                                                                                                                                      "    return lane == 0 ? v.x : (lane == 1 ? v.y : (lane == 2 ? v.z : v.w));\n"
                                                                                                                                                                                                                                                      "}\n"
                                                                                                                                                                                                                                                      "float readPackedScalar2(float4 packed[2], int idx) {\n"
                                                                                                                                                                                                                                                      "    idx = clamp(idx, 0, 7);\n"
                                                                                                                                                                                                                                                      "    int vecIdx = idx >> 2;\n"
                                                                                                                                                                                                                                                      "    int lane = idx & 3;\n"
                                                                                                                                                                                                                                                      "    float4 v = packed[vecIdx];\n"
                                                                                                                                                                                                                                                      "    return lane == 0 ? v.x : (lane == 1 ? v.y : (lane == 2 ? v.z : v.w));\n"
                                                                                                                                                                                                                                                      "}\n"
                                                                                                                                                                                                                                                      "float morphWeightAt(int idx, int usePrevWeights) {\n"
                                                                                                                                                                                                                                                      "    return usePrevWeights != 0 ? readPackedScalar8(prevMorphWeightsPacked, idx)\n"
                                                                                                                                                                                                                                                      "                               : readPackedScalar8(morphWeightsPacked, idx);\n"
                                                                                                                                                                                                                                                      "}\n"
                                                                                                                                                                                                                                                      "float customParamAt(int idx) {\n"
                                                                                                                                                                                                                                                      "    return readPackedScalar2(customParamsPacked, idx);\n"
                                                                                                                                                                                                                                                      "}\n"
                                                                                                                                                                                                                                                      "int readInt4Lane(int4 v, int lane) {\n"
                                                                                                                                                                                                                                                      "    lane = clamp(lane, 0, 3);\n"
                                                                                                                                                                                                                                                      "    return lane == 0 ? v.x : (lane == 1 ? v.y : (lane == 2 ? v.z : v.w));\n"
                                                                                                                                                                                                                                                      "}\n"
                                                                                                                                                                                                                                                      "int textureSlotIndex(int slot) {\n"
                                                                                                                                                                                                                                                      "    return min(max(slot, 0), " VGFX3D_STR(
                                                                                                                                                                                                                                                          RT_MATERIAL3D_TEXTURE_SLOT_COUNT) " - 1);\n"
                                                                                                                                                                                                                                                                                            "}\n"
                                                                                                                                                                                                                                                                                            "int textureUvSetAt(int slot) {\n"
                                                                                                                                                                                                                                                                                            "    int safeSlot = textureSlotIndex(slot);\n"
                                                                                                                                                                                                                                                                                            "    return safeSlot < 4 ? readInt4Lane(textureUvSets0, safeSlot)\n"
                                                                                                                                                                                                                                                                                            "                        : readInt4Lane(textureUvSets1, safeSlot - 4);\n"
                                                                                                                                                                                                                                                                                            "}\n"
                                                                                                                                                                                                                                                                                            "float2 materialUv(PS_INPUT input, int slot) {\n"
                                                                                                                                                                                                                                                                                            "    int safeSlot = textureSlotIndex(slot);\n"
                                                                                                                                                                                                                                                                                            "    float2 uv = textureUvSetAt(safeSlot) != 0 ? input.uv1 : input.uv;\n"
                                                                                                                                                                                                                                                                                            "    float4 m = textureUvTransform0[safeSlot];\n"
                                                                                                                                                                                                                                                                                            "    float4 t = textureUvTransform1[safeSlot];\n"
                                                                                                                                                                                                                                                                                            "    return float2(uv.x * m.x + uv.y * m.y + t.x,\n"
                                                                                                                                                                                                                                                                                            "                  uv.x * m.z + uv.y * m.w + t.y);\n"
                                                                                                                                                                                                                                                                                            "}\n"
                                                                                                                                                                                                                                                                                            "float3 safeNormalize3(float3 v, float3 fallback) {\n"
                                                                                                                                                                                                                                                                                            "    float len2 = dot(v, v);\n"
                                                                                                                                                                                                                                                                                            "    return len2 > 1e-12 ? v * rsqrt(len2) : fallback;\n"
                                                                                                                                                                                                                                                                                            "}\n"
                                                                                                                                                                                                                                                                                            "float2 ndcToTextureUv(float2 ndc) {\n"
                                                                                                                                                                                                                                                                                            "    return float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);\n"
                                                                                                                                                                                                                                                                                            "}\n"
                                                                                                                                                                                                                                                                                            "float2 ndcDeltaToUvDelta(float2 delta) {\n"
                                                                                                                                                                                                                                                                                            "    return float2(delta.x * 0.5, -delta.y * 0.5);\n"
                                                                                                                                                                                                                                                                                            "}\n"
                                                                                                                                                                                                                                                                                            "\n"
                                                                                                                                                                                                                                                                                            "float3 applyMorphPosition(float3 pos, uint vid, int usePrevWeights) {\n"
                                                                                                                                                                                                                                                                                            "    if (morphShapeCount <= 0 || vertexCount <= 0)\n"
                                                                                                                                                                                                                                                                                            "        return pos;\n"
                                                                                                                                                                                                                                                                                            "    for (int s = 0; s < morphShapeCount; s++) {\n"
                                                                                                                                                                                                                                                                                            "        float w = morphWeightAt(s, usePrevWeights);\n"
                                                                                                                                                                                                                                                                                            "        if (abs(w) > 0.0001) {\n"
                                                                                                                                                                                                                                                                                            "            int base = (s * vertexCount + int(vid)) * 3;\n"
                                                                                                                                                                                                                                                                                            "            pos.x += morphDeltas[base + 0] * w;\n"
                                                                                                                                                                                                                                                                                            "            pos.y += morphDeltas[base + 1] * w;\n"
                                                                                                                                                                                                                                                                                            "            pos.z += morphDeltas[base + 2] * w;\n"
                                                                                                                                                                                                                                                                                            "        }\n"
                                                                                                                                                                                                                                                                                            "    }\n"
                                                                                                                                                                                                                                                                                            "    return pos;\n"
                                                                                                                                                                                                                                                                                            "}\n"
                                                                                                                                                                                                                                                                                            "\n"
                                                                                                                                                                                                                                                                                            "float3 applyMorphNormal(float3 nrm, uint vid, int usePrevWeights) {\n"
                                                                                                                                                                                                                                                                                            "    if (morphShapeCount <= 0 || vertexCount <= 0 || hasMorphNormalDeltas == 0)\n"
                                                                                                                                                                                                                                                                                            "        return nrm;\n"
                                                                                                                                                                                                                                                                                            "    for (int s = 0; s < morphShapeCount; s++) {\n"
                                                                                                                                                                                                                                                                                            "        float w = morphWeightAt(s, usePrevWeights);\n"
                                                                                                                                                                                                                                                                                            "        if (abs(w) > 0.0001) {\n"
                                                                                                                                                                                                                                                                                            "            int base = (s * vertexCount + int(vid)) * 3;\n"
                                                                                                                                                                                                                                                                                            "            nrm.x += morphNormalDeltas[base + 0] * w;\n"
                                                                                                                                                                                                                                                                                            "            nrm.y += morphNormalDeltas[base + 1] * w;\n"
                                                                                                                                                                                                                                                                                            "            nrm.z += morphNormalDeltas[base + 2] * w;\n"
                                                                                                                                                                                                                                                                                            "        }\n"
                                                                                                                                                                                                                                                                                            "    }\n"
                                                                                                                                                                                                                                                                                            "    return safeNormalize3(nrm, float3(0.0, 1.0, 0.0));\n"
                                                                                                                                                                                                                                                                                            "}\n"
                                                                                                                                                                                                                                                                                            "\n"
                                                                                                                                                                                                                                                                                            "float4 skinPosition(float4 pos, uint4 boneIdx, float4 boneWt, int usePrevPalette) {\n"
                                                                                                                                                                                                                                                                                            "    if ((usePrevPalette != 0 && hasPrevSkinning == 0) || (usePrevPalette == 0 && hasSkinning "
                                                                                                                                                                                                                                                                                            "== 0))\n"
                                                                                                                                                                                                                                                                                            "        return pos;\n"
                                                                                                                                                                                                                                                                                            "    float4 skinned = float4(0.0, 0.0, 0.0, 0.0);\n"
                                                                                                                                                                                                                                                                                            "    float totalWeight = 0.0;\n"
                                                                                                                                                                                                                                                                                            "    for (int i = 0; i < 4; i++) {\n"
                                                                                                                                                                                                                                                                                            "        float w = max(boneWt[i], 0.0);\n"
                                                                                                                                                                                                                                                                                            "        if (w <= 0.0001)\n"
                                                                                                                                                                                                                                                                                            "            continue;\n"
                                                                                                                                                                                                                                                                                            "        uint idx = min(boneIdx[i], 255u);\n"
                                                                                                                                                                                                                                                                                            "        row_major float4x4 bm = usePrevPalette != 0 ? prevBonePalette[idx] : "
                                                                                                                                                                                                                                                                                            "bonePalette[idx];\n"
                                                                                                                                                                                                                                                                                            "        skinned += mul(bm, pos) * w;\n"
                                                                                                                                                                                                                                                                                            "        totalWeight += w;\n"
                                                                                                                                                                                                                                                                                            "    }\n"
                                                                                                                                                                                                                                                                                            "    return totalWeight > 0.0001 ? skinned / totalWeight : pos;\n"
                                                                                                                                                                                                                                                                                            "}\n"
                                                                                                                                                                                                                                                                                            "\n"
                                                                                                                                                                                                                                                                                            "float3 skinVector(float3 vec, uint4 boneIdx, float4 boneWt) {\n"
                                                                                                                                                                                                                                                                                            "    if (hasSkinning == 0)\n"
                                                                                                                                                                                                                                                                                            "        return vec;\n"
                                                                                                                                                                                                                                                                                            "    float3 skinned = float3(0.0, 0.0, 0.0);\n"
                                                                                                                                                                                                                                                                                            "    float totalWeight = 0.0;\n"
                                                                                                                                                                                                                                                                                            "    for (int i = 0; i < 4; i++) {\n"
                                                                                                                                                                                                                                                                                            "        float w = max(boneWt[i], 0.0);\n"
                                                                                                                                                                                                                                                                                            "        if (w <= 0.0001)\n"
                                                                                                                                                                                                                                                                                            "            continue;\n"
                                                                                                                                                                                                                                                                                            "        uint idx = min(boneIdx[i], 255u);\n"
                                                                                                                                                                                                                                                                                            "        skinned += mul(bonePalette[idx], float4(vec, 0.0)).xyz * w;\n"
                                                                                                                                                                                                                                                                                            "        totalWeight += w;\n"
                                                                                                                                                                                                                                                                                            "    }\n"
                                                                                                                                                                                                                                                                                            "    return totalWeight > 0.0001 ? skinned / totalWeight : vec;\n"
                                                                                                                                                                                                                                                                                            "}\n"
                                                                                                                                                                                                                                                                                            "\n"
                                                                                                                                                                                                                                                                                            "float4x4 makeMatrixRows(float4 r0, float4 r1, float4 r2, float4 r3) {\n"
                                                                                                                                                                                                                                                                                            "    return float4x4(r0.x, r0.y, r0.z, r0.w,\n"
                                                                                                                                                                                                                                                                                            "                    r1.x, r1.y, r1.z, r1.w,\n"
                                                                                                                                                                                                                                                                                            "                    r2.x, r2.y, r2.z, r2.w,\n"
                                                                                                                                                                                                                                                                                            "                    r3.x, r3.y, r3.z, r3.w);\n"
                                                                                                                                                                                                                                                                                            "}\n"
                                                                                                                                                                                                                                                                                            "\n"
                                                                                                                                                                                                                                                                                            "PS_INPUT buildOutput(float3 pos,\n"
                                                                                                                                                                                                                                                                                            "                     float3 prevPos,\n"
                                                                                                                                                                                                                                                                                            "                     float3 nrm,\n"
                                                                                                                                                                                                                                                                                            "                     float4 tan,\n"
                                                                                                                                                                                                                                                                                            "                     float2 uv,\n"
                                                                                                                                                                                                                                                                                            "                     float2 uv1,\n"
                                                                                                                                                                                                                                                                                            "                     float4 color,\n"
                                                                                                                                                                                                                                                                                            "                     row_major float4x4 currentModel,\n"
                                                                                                                                                                                                                                                                                            "                     row_major float4x4 currentNormal,\n"
                                                                                                                                                                                                                                                                                            "                     row_major float4x4 prevModel,\n"
                                                                                                                                                                                                                                                                                            "                     float hasHistory) {\n"
                                                                                                                                                                                                                                                                                            "    PS_INPUT output;\n"
                                                                                                                                                                                                                                                                                            "    float4 wp = mul(currentModel, float4(pos, 1.0));\n"
                                                                                                                                                                                                                                                                                            "    float4 prevWp = mul(prevModel, float4(prevPos, 1.0));\n"
                                                                                                                                                                                                                                                                                            "    float4 currClip = mul(viewProjection, wp);\n"
                                                                                                                                                                                                                                                                                            "    float4 prevClip = mul(prevViewProjection, prevWp);\n"
                                                                                                                                                                                                                                                                                            "    output.pos = currClip;\n"
                                                                                                                                                                                                                                                                                            "    output.pos.z = output.pos.z * 0.5 + output.pos.w * 0.5;\n"
                                                                                                                                                                                                                                                                                            "    output.worldPos = wp.xyz;\n"
                                                                                                                                                                                                                                                                                            "    output.normal = mul(currentNormal, float4(nrm, 0.0)).xyz;\n"
                                                                                                                                                                                                                                                                                            "    output.tangent = float4(mul(currentModel, float4(tan.xyz, 0.0)).xyz, tan.w);\n"
                                                                                                                                                                                                                                                                                            "    output.uv = uv;\n"
                                                                                                                                                                                                                                                                                            "    output.uv1 = uv1;\n"
                                                                                                                                                                                                                                                                                            "    output.color = color;\n"
                                                                                                                                                                                                                                                                                            "    output.currClip = currClip;\n"
                                                                                                                                                                                                                                                                                            "    output.prevClip = prevClip;\n"
                                                                                                                                                                                                                                                                                            "    output.hasObjectHistory = hasHistory;\n"
                                                                                                                                                                                                                                                                                            "    return output;\n"
                                                                                                                                                                                                                                                                                            "}\n"
                                                                                                                                                                                                                                                                                            "\n"
                                                                                                                                                                                                                                                                                            "PS_INPUT VSMain(VS_INPUT input, uint vid : SV_VertexID) {\n"
                                                                                                                                                                                                                                                                                            "    float3 pos = applyMorphPosition(input.pos, vid, 0);\n"
                                                                                                                                                                                                                                                                                            "    float3 prevPos = applyMorphPosition(input.pos, vid, hasPrevMorphWeights);\n"
                                                                                                                                                                                                                                                                                            "    float3 nrm = applyMorphNormal(input.normal, vid, 0);\n"
                                                                                                                                                                                                                                                                                            "    float4 tan = input.tangent;\n"
                                                                                                                                                                                                                                                                                            "    float4 skinnedPos = skinPosition(float4(pos, 1.0), input.boneIdx, input.boneWt, 0);\n"
                                                                                                                                                                                                                                                                                            "    float4 prevSkinnedPos = skinPosition(float4(prevPos, 1.0), input.boneIdx, input.boneWt, "
                                                                                                                                                                                                                                                                                            "1);\n"
                                                                                                                                                                                                                                                                                            "    float3 skinnedNormal = safeNormalize3(skinVector(nrm, input.boneIdx, input.boneWt), "
                                                                                                                                                                                                                                                                                            "float3(0.0, 1.0, 0.0));\n"
                                                                                                                                                                                                                                                                                            "    float3 skinnedTangent = safeNormalize3(skinVector(tan.xyz, input.boneIdx, input.boneWt), "
                                                                                                                                                                                                                                                                                            "float3(1.0, 0.0, 0.0));\n"
                                                                                                                                                                                                                                                                                            "    if (hasSkinning == 0) {\n"
                                                                                                                                                                                                                                                                                            "        skinnedPos = float4(pos, 1.0);\n"
                                                                                                                                                                                                                                                                                            "        skinnedNormal = safeNormalize3(nrm, float3(0.0, 1.0, 0.0));\n"
                                                                                                                                                                                                                                                                                            "        skinnedTangent = safeNormalize3(tan.xyz, float3(1.0, 0.0, 0.0));\n"
                                                                                                                                                                                                                                                                                            "    }\n"
                                                                                                                                                                                                                                                                                            "    if (hasPrevSkinning == 0)\n"
                                                                                                                                                                                                                                                                                            "        prevSkinnedPos = float4(prevPos, 1.0);\n"
                                                                                                                                                                                                                                                                                            "    return buildOutput(skinnedPos.xyz,\n"
                                                                                                                                                                                                                                                                                            "                       prevSkinnedPos.xyz,\n"
                                                                                                                                                                                                                                                                                            "                       skinnedNormal,\n"
                                                                                                                                                                                                                                                                                            "                       float4(skinnedTangent, tan.w),\n"
                                                                                                                                                                                                                                                                                            "                       input.uv,\n"
                                                                                                                                                                                                                                                                                            "                       input.uv1,\n"
                                                                                                                                                                                                                                                                                            "                       input.color,\n"
                                                                                                                                                                                                                                                                                            "                       modelMatrix,\n"
                                                                                                                                                                                                                                                                                            "                       normalMatrix,\n"
                                                                                                                                                                                                                                                                                            "                       hasPrevModelMatrix != 0 ? prevModelMatrix : modelMatrix,\n"
                                                                                                                                                                                                                                                                                            "                       (hasPrevModelMatrix != 0 || hasPrevSkinning != 0 || "
                                                                                                                                                                                                                                                                                            "hasPrevMorphWeights != 0) ? 1.0 : 0.0);\n"
                                                                                                                                                                                                                                                                                            "}\n"
                                                                                                                                                                                                                                                                                            "\n"
                                                                                                                                                                                                                                                                                            "PS_INPUT VSMainInstanced(VS_INPUT_INSTANCED input, uint vid : SV_VertexID) {\n"
                                                                                                                                                                                                                                                                                            "    float4x4 instModel = makeMatrixRows(input.instanceModel0,\n"
                                                                                                                                                                                                                                                                                            "                                        input.instanceModel1,\n"
                                                                                                                                                                                                                                                                                            "                                        input.instanceModel2,\n"
                                                                                                                                                                                                                                                                                            "                                        input.instanceModel3);\n"
                                                                                                                                                                                                                                                                                            "    float4x4 instNormal = makeMatrixRows(input.instanceNormal0,\n"
                                                                                                                                                                                                                                                                                            "                                         input.instanceNormal1,\n"
                                                                                                                                                                                                                                                                                            "                                         input.instanceNormal2,\n"
                                                                                                                                                                                                                                                                                            "                                         input.instanceNormal3);\n"
                                                                                                                                                                                                                                                                                            "    float4x4 prevModel = makeMatrixRows(input.prevModel0,\n"
                                                                                                                                                                                                                                                                                            "                                        input.prevModel1,\n"
                                                                                                                                                                                                                                                                                            "                                        input.prevModel2,\n"
                                                                                                                                                                                                                                                                                            "                                        input.prevModel3);\n"
                                                                                                                                                                                                                                                                                            "    float3 pos = applyMorphPosition(input.pos, vid, 0);\n"
                                                                                                                                                                                                                                                                                            "    float3 prevPos = applyMorphPosition(input.pos, vid, hasPrevMorphWeights);\n"
                                                                                                                                                                                                                                                                                            "    float3 nrm = applyMorphNormal(input.normal, vid, 0);\n"
                                                                                                                                                                                                                                                                                            "    float4 tan = input.tangent;\n"
                                                                                                                                                                                                                                                                                            "    float4 skinnedPos = skinPosition(float4(pos, 1.0), input.boneIdx, input.boneWt, 0);\n"
                                                                                                                                                                                                                                                                                            "    float4 prevSkinnedPos = skinPosition(float4(prevPos, 1.0), input.boneIdx, input.boneWt, "
                                                                                                                                                                                                                                                                                            "1);\n"
                                                                                                                                                                                                                                                                                            "    float3 skinnedNormal = safeNormalize3(skinVector(nrm, input.boneIdx, input.boneWt), "
                                                                                                                                                                                                                                                                                            "float3(0.0, 1.0, 0.0));\n"
                                                                                                                                                                                                                                                                                            "    float3 skinnedTangent = safeNormalize3(skinVector(tan.xyz, input.boneIdx, input.boneWt), "
                                                                                                                                                                                                                                                                                            "float3(1.0, 0.0, 0.0));\n"
                                                                                                                                                                                                                                                                                            "    if (hasSkinning == 0) {\n"
                                                                                                                                                                                                                                                                                            "        skinnedPos = float4(pos, 1.0);\n"
                                                                                                                                                                                                                                                                                            "        skinnedNormal = safeNormalize3(nrm, float3(0.0, 1.0, 0.0));\n"
                                                                                                                                                                                                                                                                                            "        skinnedTangent = safeNormalize3(tan.xyz, float3(1.0, 0.0, 0.0));\n"
                                                                                                                                                                                                                                                                                            "    }\n"
                                                                                                                                                                                                                                                                                            "    if (hasPrevSkinning == 0)\n"
                                                                                                                                                                                                                                                                                            "        prevSkinnedPos = float4(prevPos, 1.0);\n"
                                                                                                                                                                                                                                                                                            "    return buildOutput(skinnedPos.xyz,\n"
                                                                                                                                                                                                                                                                                            "                       prevSkinnedPos.xyz,\n"
                                                                                                                                                                                                                                                                                            "                       skinnedNormal,\n"
                                                                                                                                                                                                                                                                                            "                       float4(skinnedTangent, tan.w),\n"
                                                                                                                                                                                                                                                                                            "                       input.uv,\n"
                                                                                                                                                                                                                                                                                            "                       input.uv1,\n"
                                                                                                                                                                                                                                                                                            "                       input.color,\n"
                                                                                                                                                                                                                                                                                            "                       instModel,\n"
                                                                                                                                                                                                                                                                                            "                       instNormal,\n"
                                                                                                                                                                                                                                                                                            "                       hasPrevInstanceMatrices != 0 ? prevModel : instModel,\n"
                                                                                                                                                                                                                                                                                            "                       (hasPrevInstanceMatrices != 0 || hasPrevSkinning != 0 || "
                                                                                                                                                                                                                                                                                            "hasPrevMorphWeights != 0) ? 1.0 : 0.0);\n"
                                                                                                                                                                                                                                                                                            "}\n"
                                                                                                                                                                                                                                                                                            "\n"
                                                                                                                                                                                                                                                                                            "int resolveShadowCascade(Light light, float3 worldPos) {\n"
                                                                                                                                                                                                                                                                                            "    int shadowIndex = light.shadowIndex;\n"
                                                                                                                                                                                                                                                                                            "    if (shadowIndex < 0 || shadowIndex >= shadowCount)\n"
                                                                                                                                                                                                                                                                                            "        return -1;\n"
                                                                                                                                                                                                                                                                                            "    int cascadeCount = clamp(light.shadowCascadeCount, 1, " VGFX3D_STR(VGFX3D_MAX_SHADOW_LIGHTS) ");\n"
                                                                                                                                                                                                                                                                                            "    cascadeCount = min(cascadeCount, shadowCount - shadowIndex);\n"
                                                                                                                                                                                                                                                                                            "    if (cascadeCount <= 1)\n"
                                                                                                                                                                                                                                                                                            "        return shadowIndex;\n"
                                                                                                                                                                                                                                                                                            "    float viewDepth = dot(worldPos - cameraPosition.xyz, cameraForward.xyz);\n"
                                                                                                                                                                                                                                                                                            "    if (viewDepth <= light.shadowCascadeSplits.x || cascadeCount == 1)\n"
                                                                                                                                                                                                                                                                                            "        return shadowIndex;\n"
                                                                                                                                                                                                                                                                                            "    if (viewDepth <= light.shadowCascadeSplits.y || cascadeCount == 2)\n"
                                                                                                                                                                                                                                                                                            "        return shadowIndex + 1;\n"
                                                                                                                                                                                                                                                                                            "    if (viewDepth <= light.shadowCascadeSplits.z || cascadeCount == 3)\n"
                                                                                                                                                                                                                                                                                            "        return shadowIndex + 2;\n"
                                                                                                                                                                                                                                                                                            "    return shadowIndex + 3;\n"
                                                                                                                                                                                                                                                                                            "}\n"
                                                                                                                                                                                                                                                                                            "\n"
                                                                                                                                                                                                                                                                                            "float sampleShadowAt(int shadowIndex, float2 uv, float depth) {\n"
                                                                                                                                                                                                                                                                                            "    if (shadowIndex == 0)\n"
                                                                                                                                                                                                                                                                                            "        return shadowTex0.SampleCmpLevelZero(shadowSampler, uv, depth - shadowBias);\n"
                                                                                                                                                                                                                                                                                            "    if (shadowIndex == 1)\n"
                                                                                                                                                                                                                                                                                            "        return shadowTex1.SampleCmpLevelZero(shadowSampler, uv, depth - shadowBias);\n"
                                                                                                                                                                                                                                                                                            "    if (shadowIndex == 2)\n"
                                                                                                                                                                                                                                                                                            "        return shadowTex2.SampleCmpLevelZero(shadowSampler, uv, depth - shadowBias);\n"
                                                                                                                                                                                                                                                                                            "    return shadowTex3.SampleCmpLevelZero(shadowSampler, uv, depth - shadowBias);\n"
                                                                                                                                                                                                                                                                                            "}\n"
                                                                                                                                                                                                                                                                                            "\n"
                                                                                                                                                                                                                                                                                            "float sampleShadow(Light light, float3 worldPos) {\n"
                                                                                                                                                                                                                                                                                            "    int shadowIndex = resolveShadowCascade(light, worldPos);\n"
                                                                                                                                                                                                                                                                                            "    if (shadowIndex < 0 || shadowIndex >= shadowCount)\n"
                                                                                                                                                                                                                                                                                            "        return 1.0;\n"
                                                                                                                                                                                                                                                                                            "    row_major float4x4 shadowMatrix = shadowVP[shadowIndex];\n"
                                                                                                                                                                                                                                                                                            "    float4 lc = mul(shadowMatrix, float4(worldPos, 1.0));\n"
                                                                                                                                                                                                                                                                                            "    if (lc.w <= 0.0001)\n"
                                                                                                                                                                                                                                                                                            "        return 1.0;\n"
                                                                                                                                                                                                                                                                                            "    float invW = 1.0 / max(lc.w, 0.0001);\n"
                                                                                                                                                                                                                                                                                            "    float3 ndc = lc.xyz * invW;\n"
                                                                                                                                                                                                                                                                                            "    float2 uv = ndcToTextureUv(ndc.xy);\n"
                                                                                                                                                                                                                                                                                            "    float depth = ndc.z * 0.5 + 0.5;\n"
                                                                                                                                                                                                                                                                                            "    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || depth < 0.0 || depth > 1.0)\n"
                                                                                                                                                                                                                                                                                            "        return 1.0;\n"
                                                                                                                                                                                                                                                                                            "    float shadow = sampleShadowAt(shadowIndex, uv, depth);\n"
                                                                                                                                                                                                                                                                                            "    return shadow == shadow ? saturate(shadow) : 1.0;\n"
                                                                                                                                                                                                                                                                                            "}\n"
                                                                                                                                                                                                                                                                                            "\n"
                                                                                                                                                                                                                                                                                            "float distributionGGX(float NdotH, float roughness) {\n"
                                                                                                                                                                                                                                                                                            "    float a = roughness * roughness;\n"
                                                                                                                                                                                                                                                                                            "    float a2 = a * a;\n"
                                                                                                                                                                                                                                                                                            "    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;\n"
                                                                                                                                                                                                                                                                                            "    return a2 / (3.14159265 * denom * denom + 1e-6);\n"
                                                                                                                                                                                                                                                                                            "}\n"
                                                                                                                                                                                                                                                                                            "\n"
                                                                                                                                                                                                                                                                                            "float geometrySchlickGGX(float NdotV, float roughness) {\n"
                                                                                                                                                                                                                                                                                            "    float r = roughness + 1.0;\n"
                                                                                                                                                                                                                                                                                            "    float k = (r * r) / 8.0;\n"
                                                                                                                                                                                                                                                                                            "    return NdotV / (NdotV * (1.0 - k) + k + 1e-6);\n"
                                                                                                                                                                                                                                                                                            "}\n"
                                                                                                                                                                                                                                                                                            "\n"
                                                                                                                                                                                                                                                                                            "float geometrySmith(float NdotV, float NdotL, float roughness) {\n"
                                                                                                                                                                                                                                                                                            "    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);\n"
                                                                                                                                                                                                                                                                                            "}\n"
                                                                                                                                                                                                                                                                                            "\n"
                                                                                                                                                                                                                                                                                            "float3 fresnelSchlick(float cosTheta, float3 F0) {\n"
                                                                                                                                                                                                                                                                                            "    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);\n"
                                                                                                                                                                                                                                                                                            "}\n"
                                                                                                                                                                                                                                                                                            "\n"
                                                                                                                                                                                                                                                                                            "float3 srgbToLinear(float3 c) {\n"
                                                                                                                                                                                                                                                                                            "    float3 low = c / 12.92;\n"
                                                                                                                                                                                                                                                                                            "    float3 high = pow((c + 0.055) / 1.055, float3(2.4, 2.4, 2.4));\n"
                                                                                                                                                                                                                                                                                            "    return lerp(low, high, step(float3(0.04045, 0.04045, 0.04045), c));\n"
                                                                                                                                                                                                                                                                                            "}\n"
                                                                                                                                                                                                                                                                                            "\n"
                                                                                                                                                                                                                                                                                            "PS_OUTPUT PSMain(PS_INPUT input) {\n"
                                                                                                                                                                                                                                                                                            "    PS_OUTPUT output;\n"
                                                                                                                                                                                                                                                                                            "    float3 baseColor = diffuseColor.rgb * input.color.rgb;\n"
                                                                                                                                                                                                                                                                                            "    float texAlpha = 1.0;\n"
                                                                                                                                                                                                                                                                                            "    float materialAlpha = diffuseColor.a * scalars.x * input.color.a;\n"
                                                                                                                                                                                                                                                                                            "    if (flags0.x != 0) {\n"
                                                                                                                                                                                                                                                                                            "        float4 texSample = diffuseTex.Sample(diffuseSampler, materialUv(input, 0));\n"
                                                                                                                                                                                                                                                                                            "        if (pbrFlags.x != 0) texSample.rgb = srgbToLinear(texSample.rgb);\n"
                                                                                                                                                                                                                                                                                            "        baseColor *= texSample.rgb;\n"
                                                                                                                                                                                                                                                                                            "        texAlpha = texSample.a;\n"
                                                                                                                                                                                                                                                                                            "    }\n"
                                                                                                                                                                                                                                                                                            "    if (flags1.z != 0) {\n"
                                                                                                                                                                                                                                                                                            "        float4 sp = splatTex.Sample(diffuseSampler, input.uv);\n"
                                                                                                                                                                                                                                                                                            "        float sum = sp.r + sp.g + sp.b + sp.a;\n"
                                                                                                                                                                                                                                                                                            "        if (sum > 0.0001) {\n"
                                                                                                                                                                                                                                                                                            "            sp /= sum;\n"
                                                                                                                                                                                                                                                                                            "            float3 splatColor = splatLayer0.Sample(diffuseSampler, input.uv * splatScales.x).rgb "
                                                                                                                                                                                                                                                                                            "* sp.r +\n"
                                                                                                                                                                                                                                                                                            "                                splatLayer1.Sample(diffuseSampler, input.uv * splatScales.y).rgb "
                                                                                                                                                                                                                                                                                            "* sp.g +\n"
                                                                                                                                                                                                                                                                                            "                                splatLayer2.Sample(diffuseSampler, input.uv * splatScales.z).rgb "
                                                                                                                                                                                                                                                                                            "* sp.b +\n"
                                                                                                                                                                                                                                                                                            "                                splatLayer3.Sample(diffuseSampler, input.uv * splatScales.w).rgb "
                                                                                                                                                                                                                                                                                            "* sp.a;\n"
                                                                                                                                                                                                                                                                                            "            baseColor = splatColor * diffuseColor.rgb * input.color.rgb;\n"
                                                                                                                                                                                                                                                                                            "        }\n"
                                                                                                                                                                                                                                                                                            "    }\n"
                                                                                                                                                                                                                                                                                            "    float3 N = safeNormalize3(input.normal, float3(0.0, 1.0, 0.0));\n"
                                                                                                                                                                                                                                                                                            "    if (flags0.z != 0) {\n"
                                                                                                                                                                                                                                                                                            "        float3 mapN = normalTex.Sample(normalSampler, materialUv(input, 1)).xyz * 2.0 - 1.0;\n"
                                                                                                                                                                                                                                                                                            "        mapN.xy *= pbrScalars1.x;\n"
                                                                                                                                                                                                                                                                                            "        float3 T = safeNormalize3(input.tangent.xyz - N * dot(input.tangent.xyz, N), "
                                                                                                                                                                                                                                                                                            "float3(1.0, 0.0, 0.0));\n"
                                                                                                                                                                                                                                                                                            "        if (dot(T, T) > 0.0001) {\n"
                                                                                                                                                                                                                                                                                            "            float3 B = safeNormalize3(cross(N, T), float3(0.0, 0.0, 1.0)) * "
                                                                                                                                                                                                                                                                                            "(input.tangent.w < 0.0 ? -1.0 : 1.0);\n"
                                                                                                                                                                                                                                                                                            "            N = safeNormalize3(mapN.x * T + mapN.y * B + mapN.z * N, N);\n"
                                                                                                                                                                                                                                                                                            "        }\n"
                                                                                                                                                                                                                                                                                            "    }\n"
                                                                                                                                                                                                                                                                                            "    float3 safeCameraForward = safeNormalize3(cameraForward, float3(0.0, 0.0, -1.0));\n"
                                                                                                                                                                                                                                                                                            "    float3 cameraToWorld = cameraPosition.xyz - input.worldPos;\n"
                                                                                                                                                                                                                                                                                            "    float3 V = cameraPosition.w > 0.5 ? -safeCameraForward : "
                                                                                                                                                                                                                                                                                            "safeNormalize3(cameraToWorld, -safeCameraForward);\n"
                                                                                                                                                                                                                                                                                            "    float viewDistance = cameraPosition.w > 0.5\n"
                                                                                                                                                                                                                                                                                            "        ? abs(dot(input.worldPos - cameraPosition.xyz, safeCameraForward))\n"
                                                                                                                                                                                                                                                                                            "        : length(cameraToWorld);\n"
                                                                                                                                                                                                                                                                                            "    float3 emissive = emissiveColor.rgb * pbrScalars0.w;\n"
                                                                                                                                                                                                                                                                                            "    if (flags1.x != 0) {\n"
                                                                                                                                                                                                                                                                                            "        float3 emissiveSample = emissiveTex.Sample(emissiveSampler, materialUv(input, 3)).rgb;\n"
                                                                                                                                                                                                                                                                                            "        if (pbrFlags.x != 0) emissiveSample = srgbToLinear(emissiveSample);\n"
                                                                                                                                                                                                                                                                                            "        emissive *= emissiveSample;\n"
                                                                                                                                                                                                                                                                                            "    }\n"
                                                                                                                                                                                                                                                                                            "    float finalAlpha = materialAlpha * texAlpha;\n"
                                                                                                                                                                                                                                                                                            "    if (pbrFlags.y == 1) {\n"
                                                                                                                                                                                                                                                                                            "        if (finalAlpha < pbrScalars1.y)\n"
                                                                                                                                                                                                                                                                                            "            discard;\n"
                                                                                                                                                                                                                                                                                            "        finalAlpha = 1.0;\n"
                                                                                                                                                                                                                                                                                            "    } else if (pbrFlags.y == 0) {\n"
                                                                                                                                                                                                                                                                                            "        finalAlpha = 1.0;\n"
                                                                                                                                                                                                                                                                                            "    }\n"
                                                                                                                                                                                                                                                                                            "    float envRoughness = saturate(pbrScalars0.y);\n"
                                                                                                                                                                                                                                                                                            "    float3 result = ambientColor.rgb * baseColor;\n"
                                                                                                                                                                                                                                                                                            "    if (flags0.y != 0) {\n"
                                                                                                                                                                                                                                                                                            "        result = baseColor;\n"
                                                                                                                                                                                                                                                                                            "    } else {\n"
                                                                                                                                                                                                                                                                                            "        if (pbrFlags.x != 0) {\n"
                                                                                                                                                                                                                                                                                            "            float metallic = saturate(pbrScalars0.x);\n"
                                                                                                                                                                                                                                                                                            "            float roughness = clamp(pbrScalars0.y, 0.045, 1.0);\n"
                                                                                                                                                                                                                                                                                            "            float ao = saturate(pbrScalars0.z);\n"
                                                                                                                                                                                                                                                                                            "            if (pbrFlags.z != 0) {\n"
                                                                                                                                                                                                                                                                                            "                float4 mr = metallicRoughnessTex.Sample(metallicRoughnessSampler, materialUv(input, 4));\n"
                                                                                                                                                                                                                                                                                            "                roughness = clamp(roughness * mr.g, 0.045, 1.0);\n"
                                                                                                                                                                                                                                                                                            "                metallic = saturate(metallic * mr.b);\n"
                                                                                                                                                                                                                                                                                            "                envRoughness = roughness;\n"
                                                                                                                                                                                                                                                                                            "            }\n"
                                                                                                                                                                                                                                                                                            "            if (pbrFlags.w != 0) {\n"
                                                                                                                                                                                                                                                                                            "                float4 aoSample = aoTex.Sample(aoSampler, materialUv(input, 5));\n"
                                                                                                                                                                                                                                                                                            "                ao = saturate(ao * aoSample.r);\n"
                                                                                                                                                                                                                                                                                            "            }\n"
                                                                                                                                                                                                                                                                                            "            result = ambientColor.rgb * baseColor * ao;\n"
                                                                                                                                                                                                                                                                                            "            for (int i = 0; i < lightCount; i++) {\n"
                                                                                                                                                                                                                                                                                            "                float3 L = float3(0.0, 0.0, 0.0);\n"
                                                                                                                                                                                                                                                                                            "                float atten = 1.0;\n"
                                                                                                                                                                                                                                                                                            "                if (lights[i].type == 0) {\n"
                                                                                                                                                                                                                                                                                            "                    L = safeNormalize3(-lights[i].direction.xyz, float3(0.0, -1.0, 0.0));\n"
                                                                                                                                                                                                                                                                                            "                    atten *= lerp(0.15, 1.0, sampleShadow(lights[i], input.worldPos));\n"
                                                                                                                                                                                                                                                                                            "                } else if (lights[i].type == 1) {\n"
                                                                                                                                                                                                                                                                                            "                    float3 toLight = lights[i].position.xyz - input.worldPos;\n"
                                                                                                                                                                                                                                                                                            "                    float d = length(toLight);\n"
                                                                                                                                                                                                                                                                                            "                    L = toLight / max(d, 0.0001);\n"
                                                                                                                                                                                                                                                                                            "                    atten = 1.0 / (1.0 + lights[i].attenuation * d * d);\n"
                                                                                                                                                                                                                                                                                            "                } else if (lights[i].type == 2) {\n"
                                                                                                                                                                                                                                                                                            "                    result += lights[i].color.rgb * lights[i].intensity * baseColor * ao;\n"
                                                                                                                                                                                                                                                                                            "                    continue;\n"
                                                                                                                                                                                                                                                                                            "                } else if (lights[i].type == 3) {\n"
                                                                                                                                                                                                                                                                                            "                    float3 toLight = lights[i].position.xyz - input.worldPos;\n"
                                                                                                                                                                                                                                                                                            "                    float d = length(toLight);\n"
                                                                                                                                                                                                                                                                                            "                    L = toLight / max(d, 0.0001);\n"
                                                                                                                                                                                                                                                                                            "                    atten = 1.0 / (1.0 + lights[i].attenuation * d * d);\n"
                                                                                                                                                                                                                                                                                            "                    float spotDot = dot(-L, safeNormalize3(lights[i].direction.xyz, "
                                                                                                                                                                                                                                                                                            "float3(0.0, -1.0, 0.0)));\n"
                                                                                                                                                                                                                                                                                            "                    if (spotDot < lights[i].outer_cos) {\n"
                                                                                                                                                                                                                                                                                            "                        atten = 0.0;\n"
                                                                                                                                                                                                                                                                                            "                    } else if (spotDot < lights[i].inner_cos) {\n"
                                                                                                                                                                                                                                                                                            "                        float coneRange = lights[i].inner_cos - lights[i].outer_cos;\n"
                                                                                                                                                                                                                                                                                            "                        float t = (coneRange > 0.0001) ?\n"
                                                                                                                                                                                                                                                                                            "                                  saturate((spotDot - lights[i].outer_cos) / coneRange) : 0.0;\n"
                                                                                                                                                                                                                                                                                            "                        atten *= t * t * (3.0 - 2.0 * t);\n"
                                                                                                                                                                                                                                                                                            "                    }\n"
                                                                                                                                                                                                                                                                                            "                } else {\n"
                                                                                                                                                                                                                                                                                            "                    continue;\n"
                                                                                                                                                                                                                                                                                            "                }\n"
                                                                                                                                                                                                                                                                                            "                float NdotL = max(dot(N, L), 0.0);\n"
                                                                                                                                                                                                                                                                                            "                if (NdotL <= 0.0)\n"
                                                                                                                                                                                                                                                                                            "                    continue;\n"
                                                                                                                                                                                                                                                                                            "                float3 H = safeNormalize3(L + V, N);\n"
                                                                                                                                                                                                                                                                                            "                float NdotV = max(dot(N, V), 0.001);\n"
                                                                                                                                                                                                                                                                                            "                float NdotH = max(dot(N, H), 0.0);\n"
                                                                                                                                                                                                                                                                                            "                float VdotH = max(dot(V, H), 0.0);\n"
                                                                                                                                                                                                                                                                                            "                float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);\n"
                                                                                                                                                                                                                                                                                            "                float3 F = fresnelSchlick(VdotH, F0);\n"
                                                                                                                                                                                                                                                                                            "                float D = distributionGGX(NdotH, roughness);\n"
                                                                                                                                                                                                                                                                                            "                float G = geometrySmith(NdotV, NdotL, roughness);\n"
                                                                                                                                                                                                                                                                                            "                float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.0001);\n"
                                                                                                                                                                                                                                                                                            "                float3 kS = F;\n"
                                                                                                                                                                                                                                                                                            "                float3 kD = (1.0 - kS) * (1.0 - metallic);\n"
                                                                                                                                                                                                                                                                                            "                float3 diffuse = kD * baseColor / 3.14159265;\n"
                                                                                                                                                                                                                                                                                            "                float3 radiance = lights[i].color.rgb * lights[i].intensity * atten;\n"
                                                                                                                                                                                                                                                                                            "                result += (diffuse + specular) * radiance * NdotL;\n"
                                                                                                                                                                                                                                                                                            "            }\n"
                                                                                                                                                                                                                                                                                            "        } else {\n"
                                                                                                                                                                                                                                                                                            "            float3 specColor = specularColor.rgb;\n"
                                                                                                                                                                                                                                                                                            "            if (flags0.w != 0)\n"
                                                                                                                                                                                                                                                                                            "                specColor *= specularTex.Sample(specularSampler, materialUv(input, 2)).rgb;\n"
                                                                                                                                                                                                                                                                                            "            for (int i = 0; i < lightCount; i++) {\n"
                                                                                                                                                                                                                                                                                            "                float3 L = float3(0.0, 0.0, 0.0);\n"
                                                                                                                                                                                                                                                                                            "                float atten = 1.0;\n"
                                                                                                                                                                                                                                                                                            "                if (lights[i].type == 0) {\n"
                                                                                                                                                                                                                                                                                            "                    L = safeNormalize3(-lights[i].direction.xyz, float3(0.0, -1.0, 0.0));\n"
                                                                                                                                                                                                                                                                                            "                    atten *= lerp(0.15, 1.0, sampleShadow(lights[i], input.worldPos));\n"
                                                                                                                                                                                                                                                                                            "                } else if (lights[i].type == 1) {\n"
                                                                                                                                                                                                                                                                                            "                    float3 toLight = lights[i].position.xyz - input.worldPos;\n"
                                                                                                                                                                                                                                                                                            "                    float d = length(toLight);\n"
                                                                                                                                                                                                                                                                                            "                    L = toLight / max(d, 0.0001);\n"
                                                                                                                                                                                                                                                                                            "                    atten = 1.0 / (1.0 + lights[i].attenuation * d * d);\n"
                                                                                                                                                                                                                                                                                            "                } else if (lights[i].type == 2) {\n"
                                                                                                                                                                                                                                                                                            "                    result += lights[i].color.rgb * lights[i].intensity * baseColor;\n"
                                                                                                                                                                                                                                                                                            "                    continue;\n"
                                                                                                                                                                                                                                                                                            "                } else if (lights[i].type == 3) {\n"
                                                                                                                                                                                                                                                                                            "                    float3 toLight = lights[i].position.xyz - input.worldPos;\n"
                                                                                                                                                                                                                                                                                            "                    float d = length(toLight);\n"
                                                                                                                                                                                                                                                                                            "                    L = toLight / max(d, 0.0001);\n"
                                                                                                                                                                                                                                                                                            "                    float spotDot = dot(safeNormalize3(-lights[i].direction.xyz, "
                                                                                                                                                                                                                                                                                            "float3(0.0, -1.0, 0.0)), L);\n"
                                                                                                                                                                                                                                                                                            "                    float coneRange = lights[i].inner_cos - lights[i].outer_cos;\n"
                                                                                                                                                                                                                                                                                            "                    float cone = (coneRange > 0.0001) ?\n"
                                                                                                                                                                                                                                                                                            "                        saturate((spotDot - lights[i].outer_cos) / coneRange) :\n"
                                                                                                                                                                                                                                                                                            "                        (spotDot >= lights[i].inner_cos ? 1.0 : 0.0);\n"
                                                                                                                                                                                                                                                                                            "                    cone = cone * cone * (3.0 - 2.0 * cone);\n"
                                                                                                                                                                                                                                                                                            "                    atten = cone / (1.0 + lights[i].attenuation * d * d);\n"
                                                                                                                                                                                                                                                                                            "                } else {\n"
                                                                                                                                                                                                                                                                                            "                    continue;\n"
                                                                                                                                                                                                                                                                                            "                }\n"
                                                                                                                                                                                                                                                                                            "                float NdotL = max(dot(N, L), 0.0);\n"
                                                                                                                                                                                                                                                                                            "                if (shadingModel == 1) {\n"
                                                                                                                                                                                                                                                                                            "                    float bands = max(customParamAt(0), 2.0);\n"
                                                                                                                                                                                                                                                                                            "                    NdotL = floor(NdotL * bands) / max(bands - 1.0, 1.0);\n"
                                                                                                                                                                                                                                                                                            "                }\n"
                                                                                                                                                                                                                                                                                            "                result += lights[i].color.rgb * lights[i].intensity * NdotL * baseColor * "
                                                                                                                                                                                                                                                                                            "atten;\n"
                                                                                                                                                                                                                                                                                            "                if (NdotL > 0.0 && specularColor.w > 0.0) {\n"
                                                                                                                                                                                                                                                                                            "                    float3 H = safeNormalize3(L + V, N);\n"
                                                                                                                                                                                                                                                                                            "                    float spec = pow(max(dot(N, H), 0.0), specularColor.w);\n"
                                                                                                                                                                                                                                                                                            "                    if (shadingModel == 1)\n"
                                                                                                                                                                                                                                                                                            "                        spec = spec >= max(customParamAt(1), 0.5) ? 1.0 : 0.0;\n"
                                                                                                                                                                                                                                                                                            "                    result += lights[i].color.rgb * lights[i].intensity * spec * specColor * "
                                                                                                                                                                                                                                                                                            "atten;\n"
                                                                                                                                                                                                                                                                                            "                }\n"
                                                                                                                                                                                                                                                                                            "            }\n"
                                                                                                                                                                                                                                                                                            "        }\n"
                                                                                                                                                                                                                                                                                            "    }\n"
                                                                                                                                                                                                                                                                                            "    result += emissive;\n"
                                                                                                                                                                                                                                                                                            "    if (flags1.y != 0) {\n"
                                                                                                                                                                                                                                                                                            "        float3 R = reflect(-V, safeNormalize3(N, float3(0.0, 1.0, 0.0)));\n"
                                                                                                                                                                                                                                                                                            "        float envLod = saturate(envRoughness) * max(scalars.z, 0.0);\n"
                                                                                                                                                                                                                                                                                            "        float3 envColor = envTex.SampleLevel(envSampler, R, envLod).rgb;\n"
                                                                                                                                                                                                                                                                                            "        result = lerp(result, envColor, saturate(scalars.y));\n"
                                                                                                                                                                                                                                                                                            "    }\n"
                                                                                                                                                                                                                                                                                            "    if (shadingModel == 1 && pbrFlags.x != 0) {\n"
                                                                                                                                                                                                                                                                                            "        float bands = max(customParamAt(0), 2.0);\n"
                                                                                                                                                                                                                                                                                            "        result = floor(result * bands) / bands;\n"
                                                                                                                                                                                                                                                                                            "    } else if (shadingModel == 4) {\n"
                                                                                                                                                                                                                                                                                            "        float ndv = saturate(dot(N, V));\n"
                                                                                                                                                                                                                                                                                            "        float power = max(customParamAt(0), 1.0);\n"
                                                                                                                                                                                                                                                                                            "        float bias = customParamAt(1);\n"
                                                                                                                                                                                                                                                                                            "        finalAlpha *= saturate(pow(1.0 - ndv, power) + bias);\n"
                                                                                                                                                                                                                                                                                            "    } else if (shadingModel == 5) {\n"
                                                                                                                                                                                                                                                                                            "        float strength = max(customParamAt(0), 1.0);\n"
                                                                                                                                                                                                                                                                                            "        result += emissive * (strength - 1.0);\n"
                                                                                                                                                                                                                                                                                            "    }\n"
                                                                                                                                                                                                                                                                                            "    if (any(isnan(result)) || any(isinf(result)))\n"
                                                                                                                                                                                                                                                                                            "        result = ambientColor.rgb * baseColor + emissive;\n"
                                                                                                                                                                                                                                                                                            "    if (fogColor.a > 0.5) {\n"
                                                                                                                                                                                                                                                                                            "        float fogFactor = saturate((viewDistance - fogNear) / max(fogFar - fogNear, 0.001));\n"
                                                                                                                                                                                                                                                                                            "        result = lerp(result, fogColor.rgb, fogFactor);\n"
                                                                                                                                                                                                                                                                                            "    }\n"
                                                                                                                                                                                                                                                                                            "    output.color = float4(result, finalAlpha);\n"
                                                                                                                                                                                                                                                                                            "    float2 currNdc = input.currClip.xy / max(input.currClip.w, 0.0001);\n"
                                                                                                                                                                                                                                                                                            "    float2 prevNdc = input.prevClip.xy / max(input.prevClip.w, 0.0001);\n"
                                                                                                                                                                                                                                                                                            "    float2 velocity = ndcDeltaToUvDelta(currNdc - prevNdc);\n"
                                                                                                                                                                                                                                                                                            "    output.motion = float4(saturate(velocity * 0.5 + 0.5), input.hasObjectHistory, 1.0);\n"
                                                                                                                                                                                                                                                                                            "    return output;\n"
                                                                                                                                                                                                                                                                                            "}\n"
                                                                                                                                                                                                                                                                                            "\n"
                                                                                                                                                                                                                                                                                            "struct SHADOW_OUT {\n"
                                                                                                                                                                                                                                                                                            "    float4 pos : SV_POSITION;\n"
                                                                                                                                                                                                                                                                                            "    float2 uv : TEXCOORD0;\n"
                                                                                                                                                                                                                                                                                            "    float2 uv1 : TEXCOORD1;\n"
                                                                                                                                                                                                                                                                                            "    float4 color : COLOR0;\n"
                                                                                                                                                                                                                                                                                            "};\n"
                                                                                                                                                                                                                                                                                            "\n"
                                                                                                                                                                                                                                                                                            "SHADOW_OUT VSShadow(VS_INPUT input, uint vid : SV_VertexID) {\n"
                                                                                                                                                                                                                                                                                            "    SHADOW_OUT output;\n"
                                                                                                                                                                                                                                                                                            "    float3 pos = applyMorphPosition(input.pos, vid, 0);\n"
                                                                                                                                                                                                                                                                                            "    float4 skinnedPos = skinPosition(float4(pos, 1.0), input.boneIdx, input.boneWt, 0);\n"
                                                                                                                                                                                                                                                                                            "    if (hasSkinning == 0)\n"
                                                                                                                                                                                                                                                                                            "        skinnedPos = float4(pos, 1.0);\n"
                                                                                                                                                                                                                                                                                            "    float4 wp = mul(modelMatrix, skinnedPos);\n"
                                                                                                                                                                                                                                                                                            "    float4 clip = mul(viewProjection, wp);\n"
                                                                                                                                                                                                                                                                                            "    clip.z = clip.z * 0.5 + clip.w * 0.5;\n"
                                                                                                                                                                                                                                                                                            "    output.pos = clip;\n"
                                                                                                                                                                                                                                                                                            "    output.uv = input.uv;\n"
                                                                                                                                                                                                                                                                                            "    output.uv1 = input.uv1;\n"
                                                                                                                                                                                                                                                                                            "    output.color = input.color;\n"
                                                                                                                                                                                                                                                                                            "    return output;\n"
                                                                                                                                                                                                                                                                                            "}\n"
                                                                                                                                                                                                                                                                                            "\n"
                                                                                                                                                                                                                                                                                            "float2 shadowMaterialUv(SHADOW_OUT input) {\n"
                                                                                                                                                                                                                                                                                            "    int safeSlot = textureSlotIndex(0);\n"
                                                                                                                                                                                                                                                                                            "    float2 uv = textureUvSetAt(safeSlot) != 0 ? input.uv1 : input.uv;\n"
                                                                                                                                                                                                                                                                                            "    float4 m = textureUvTransform0[safeSlot];\n"
                                                                                                                                                                                                                                                                                            "    float4 t = textureUvTransform1[safeSlot];\n"
                                                                                                                                                                                                                                                                                            "    return float2(uv.x * m.x + uv.y * m.y + t.x,\n"
                                                                                                                                                                                                                                                                                            "                  uv.x * m.z + uv.y * m.w + t.y);\n"
                                                                                                                                                                                                                                                                                            "}\n"
                                                                                                                                                                                                                                                                                            "\n"
                                                                                                                                                                                                                                                                                            "void PSShadow(SHADOW_OUT input) {\n"
                                                                                                                                                                                                                                                                                            "    float alpha = diffuseColor.a * scalars.x * input.color.a;\n"
                                                                                                                                                                                                                                                                                            "    if (flags0.x != 0)\n"
                                                                                                                                                                                                                                                                                            "        alpha *= diffuseTex.Sample(diffuseSampler, shadowMaterialUv(input)).a;\n"
                                                                                                                                                                                                                                                                                            "    if (pbrFlags.y == 1 && alpha < pbrScalars1.y)\n"
                                                                                                                                                                                                                                                                                            "        discard;\n"
                                                                                                                                                                                                                                                                                            "}\n";

static const char *d3d11_skybox_shader_source =
    "cbuffer Skybox : register(b0) {\n"
    "    row_major float4x4 inverseProjection;\n"
    "    row_major float4x4 inverseViewRotation;\n"
    "    float4 cameraForward;\n"
    "};\n"
    "TextureCube skyboxTex : register(t0);\n"
    "SamplerState skyboxSampler : register(s0);\n"
    "struct VS_INPUT { float3 pos : POSITION; };\n"
    "struct VS_OUTPUT {\n"
    "    float4 pos : SV_POSITION;\n"
    "    float2 ndc : TEXCOORD0;\n"
    "};\n"
    "VS_OUTPUT VSSkybox(VS_INPUT input) {\n"
    "    VS_OUTPUT output;\n"
    "    output.pos = float4(input.pos.xy, 1.0, 1.0);\n"
    "    output.ndc = input.pos.xy;\n"
    "    return output;\n"
    "}\n"
    "float3 safeNormalize3(float3 v, float3 fallback) {\n"
    "    float len2 = dot(v, v);\n"
    "    return len2 > 1e-12 ? v * rsqrt(len2) : fallback;\n"
    "}\n"
    "float4 PSSkybox(VS_OUTPUT input) : SV_Target {\n"
    "    float3 worldDir;\n"
    "    if (cameraForward.w > 0.5) {\n"
    "        worldDir = safeNormalize3(cameraForward.xyz, float3(0.0, 0.0, -1.0));\n"
    "    } else {\n"
    "        float4 view = mul(inverseProjection, float4(input.ndc, 1.0, 1.0));\n"
    "        float3 viewDir = safeNormalize3(view.xyz / max(abs(view.w), 0.0001), "
    "float3(0.0, 0.0, 1.0));\n"
    "        worldDir = safeNormalize3(mul(inverseViewRotation, float4(viewDir, 0.0)).xyz, "
    "float3(0.0, 0.0, -1.0));\n"
    "    }\n"
    "    return skyboxTex.SampleLevel(skyboxSampler, worldDir, 0.0);\n"
    "}\n";

static const char *d3d11_postfx_shader_source =
    "cbuffer PostFX : register(b0) {\n"
    "    row_major float4x4 invViewProjection;\n"
    "    row_major float4x4 prevViewProjection;\n"
    "    float4 cameraPosition;\n"
    "    float2 invResolution;\n"
    "    int bloomEnabled;\n"
    "    float bloomThreshold;\n"
    "    float bloomIntensity;\n"
    "    int bloomPasses;\n"
    "    int tonemapMode;\n"
    "    float tonemapExposure;\n"
    "    int fxaaEnabled;\n"
    "    int colorGradeEnabled;\n"
    "    float cgBrightness;\n"
    "    float cgContrast;\n"
    "    float cgSaturation;\n"
    "    int vignetteEnabled;\n"
    "    float vignetteRadius;\n"
    "    float vignetteSoftness;\n"
    "    int ssaoEnabled;\n"
    "    float ssaoRadius;\n"
    "    float ssaoIntensity;\n"
    "    int ssaoSamples;\n"
    "    int dofEnabled;\n"
    "    float dofFocusDistance;\n"
    "    float dofAperture;\n"
    "    float dofMaxBlur;\n"
    "    int motionBlurEnabled;\n"
    "    float motionBlurIntensity;\n"
    "    int motionBlurSamples;\n"
    "    float _postPad0;\n"
    "    float _postPad1;\n"
    "};\n"
    "Texture2D sceneTex : register(t0);\n"
    "Texture2D depthTex : register(t1);\n"
    "Texture2D motionTex : register(t2);\n"
    "Texture2D overlayTex : register(t3);\n"
    "SamplerState postSampler : register(s0);\n"
    "struct VS_OUTPUT {\n"
    "    float4 pos : SV_POSITION;\n"
    "    float2 uv : TEXCOORD0;\n"
    "};\n"
    "VS_OUTPUT VSPostFX(uint vid : SV_VertexID) {\n"
    "    float2 pos = vid == 0 ? float2(-1.0, -1.0) : (vid == 1 ? float2(-1.0, 3.0) : float2(3.0, "
    "-1.0));\n"
    "    VS_OUTPUT output;\n"
    "    output.pos = float4(pos, 0.0, 1.0);\n"
    "    output.uv = float2((pos.x + 1.0) * 0.5, 1.0 - (pos.y + 1.0) * 0.5);\n"
    "    return output;\n"
    "}\n"
    "float luminance(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }\n"
    "float depthAt(float2 uv) { return depthTex.Sample(postSampler, uv).r; }\n"
    "float3 sceneAt(float2 uv) { return sceneTex.Sample(postSampler, uv).rgb; }\n"
    "float2 uvToNdc(float2 uv) { return float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0); }\n"
    "float2 ndcDeltaToUvDelta(float2 delta) { return float2(delta.x * 0.5, -delta.y * 0.5); }\n"
    "float3 reconstructWorld(float2 uv, float depth) {\n"
    "    float4 clip = float4(uvToNdc(uv), depth * 2.0 - 1.0, 1.0);\n"
    "    float4 world = mul(invViewProjection, clip);\n"
    "    return world.xyz / max(abs(world.w), 0.0001);\n"
    "}\n"
    "float computeSSAO(float2 uv, float depth) {\n"
    "    if (ssaoEnabled == 0)\n"
    "        return 1.0;\n"
    "    int taps = clamp(ssaoSamples, 2, 12);\n"
    "    float occlusion = 0.0;\n"
    "    for (int i = 0; i < taps; i++) {\n"
    "        float ang = 6.2831853 * (float)i / (float)taps;\n"
    "        float2 dir = float2(cos(ang), sin(ang));\n"
    "        float2 suv = uv + dir * invResolution * max(ssaoRadius, 0.5);\n"
    "        float sampleDepth = depthAt(suv);\n"
    "        occlusion += sampleDepth + ssaoRadius * 0.002 < depth ? 1.0 : 0.0;\n"
    "    }\n"
    "    return saturate(1.0 - (occlusion / max((float)taps, 1.0)) * ssaoIntensity);\n"
    "}\n"
    "float2 cameraVelocity(float2 uv, float depth) {\n"
    "    float3 worldPos = reconstructWorld(uv, depth);\n"
    "    float4 prevClip = mul(prevViewProjection, float4(worldPos, 1.0));\n"
    "    float2 prevNdc = prevClip.xy / max(prevClip.w, 0.0001);\n"
    "    float2 currNdc = uvToNdc(uv);\n"
    "    return ndcDeltaToUvDelta(currNdc - prevNdc);\n"
    "}\n"
    "float3 applyMotionBlur(float2 uv, float depth, float3 color) {\n"
    "    if (motionBlurEnabled == 0)\n"
    "        return color;\n"
    "    float4 motion = motionTex.Sample(postSampler, uv);\n"
    "    float2 velocity = motion.xy * 2.0 - 1.0;\n"
    "    if (motion.z < 0.5)\n"
    "        velocity = cameraVelocity(uv, depth);\n"
    "    velocity *= motionBlurIntensity;\n"
    "    if (length(velocity) < 0.0005)\n"
    "        return color;\n"
    "    int taps = clamp(motionBlurSamples, 2, 8);\n"
    "    float3 accum = color;\n"
    "    for (int i = 1; i < taps; i++) {\n"
    "        float t = ((float)i / (float)(taps - 1)) - 0.5;\n"
    "        accum += sceneAt(uv + velocity * t);\n"
    "    }\n"
    "    return accum / (float)taps;\n"
    "}\n"
    "float3 applyDOF(float2 uv, float depth, float3 color) {\n"
    "    if (dofEnabled == 0)\n"
    "        return color;\n"
    "    float3 worldPos = reconstructWorld(uv, depth);\n"
    "    float dist = length(worldPos - cameraPosition.xyz);\n"
    "    float blur = saturate(abs(dist - dofFocusDistance) / max(dofAperture, 0.001)) * "
    "dofMaxBlur;\n"
    "    if (blur < 0.001)\n"
    "        return color;\n"
    "    float2 radius = invResolution * blur * 8.0;\n"
    "    float3 accum = color;\n"
    "    accum += sceneAt(uv + float2(radius.x, 0.0));\n"
    "    accum += sceneAt(uv + float2(-radius.x, 0.0));\n"
    "    accum += sceneAt(uv + float2(0.0, radius.y));\n"
    "    accum += sceneAt(uv + float2(0.0, -radius.y));\n"
    "    return accum * 0.2;\n"
    "}\n"
    "float3 applyBloom(float2 uv, float3 color) {\n"
    "    if (bloomEnabled == 0)\n"
    "        return color;\n"
    "    float2 bloomStep = invResolution * (float)clamp(bloomPasses, 0, 32);\n"
    "    float3 bloom = max(color - float3(bloomThreshold, bloomThreshold, bloomThreshold), "
    "float3(0.0, 0.0, 0.0));\n"
    "    bloom += max(sceneAt(uv + float2(bloomStep.x, 0.0)) - float3(bloomThreshold, "
    "bloomThreshold, bloomThreshold), float3(0.0, 0.0, 0.0));\n"
    "    bloom += max(sceneAt(uv + float2(-bloomStep.x, 0.0)) - float3(bloomThreshold, "
    "bloomThreshold, bloomThreshold), float3(0.0, 0.0, 0.0));\n"
    "    bloom += max(sceneAt(uv + float2(0.0, bloomStep.y)) - float3(bloomThreshold, "
    "bloomThreshold, bloomThreshold), float3(0.0, 0.0, 0.0));\n"
    "    bloom += max(sceneAt(uv + float2(0.0, -bloomStep.y)) - float3(bloomThreshold, "
    "bloomThreshold, bloomThreshold), float3(0.0, 0.0, 0.0));\n"
    "    return color + bloom * (bloomIntensity / 5.0);\n"
    "}\n"
    "float3 tonemap(float3 color) {\n"
    "    if (tonemapMode != 1 && tonemapMode != 2)\n"
    "        return color;\n"
    "    color *= tonemapExposure;\n"
    "    if (tonemapMode == 1)\n"
    "        return pow(saturate(color / (1.0 + color)), 1.0 / 2.2);\n"
    "    if (tonemapMode == 2) {\n"
    "        float a = 2.51;\n"
    "        float b = 0.03;\n"
    "        float c = 2.43;\n"
    "        float d = 0.59;\n"
    "        float e = 0.14;\n"
    "        color = saturate((color * (a * color + b)) / (color * (c * color + d) + e));\n"
    "        return pow(color, 1.0 / 2.2);\n"
    "    }\n"
    "    return color;\n"
    "}\n"
    "float3 applyColorGrade(float3 color) {\n"
    "    if (colorGradeEnabled == 0)\n"
    "        return color;\n"
    "    color = (color - 0.5) * cgContrast + 0.5;\n"
    "    color += cgBrightness;\n"
    "    float l = luminance(color);\n"
    "    return saturate(lerp(float3(l, l, l), color, cgSaturation));\n"
    "}\n"
    "float3 applyVignette(float2 uv, float3 color) {\n"
    "    if (vignetteEnabled == 0)\n"
    "        return color;\n"
    "    float2 centered = uv - 0.5;\n"
    "    float dist = length(centered) * 1.41421356;\n"
    "    float vig = 1.0;\n"
    "    if (dist > vignetteRadius)\n"
    "        vig = 1.0 - saturate((dist - vignetteRadius) / max(vignetteSoftness, 0.000001));\n"
    "    return color * vig;\n"
    "}\n"
    "float3 applyFXAA(float2 uv, float3 color) {\n"
    "    if (fxaaEnabled == 0)\n"
    "        return color;\n"
    "    float3 n = sceneAt(uv + float2(0.0, -invResolution.y));\n"
    "    float3 s = sceneAt(uv + float2(0.0, invResolution.y));\n"
    "    float3 e = sceneAt(uv + float2(invResolution.x, 0.0));\n"
    "    float3 w = sceneAt(uv + float2(-invResolution.x, 0.0));\n"
    "    float edge = abs(luminance(n) - luminance(s)) + abs(luminance(e) - luminance(w));\n"
    "    if (edge < 0.05)\n"
    "        return color;\n"
    "    return (color + n + s + e + w) * 0.2;\n"
    "}\n"
    "float4 PSPostFX(VS_OUTPUT input) : SV_Target {\n"
    "    float depth = depthAt(input.uv);\n"
    "    float3 color = sceneAt(input.uv);\n"
    "    color = applyMotionBlur(input.uv, depth, color);\n"
    "    color = applyDOF(input.uv, depth, color);\n"
    "    color *= computeSSAO(input.uv, depth);\n"
    "    color = applyFXAA(input.uv, color);\n"
    "    color = applyBloom(input.uv, color);\n"
    "    color = tonemap(color);\n"
    "    color = applyColorGrade(color);\n"
    "    color = applyVignette(input.uv, color);\n"
    "    return float4(saturate(color), 1.0);\n"
    "}\n"
    "float4 PSOverlayComposite(VS_OUTPUT input) : SV_Target {\n"
    "    return overlayTex.Sample(postSampler, input.uv);\n"
    "}\n";

typedef struct {
    const void *pixels_ptr;
    void *texture_asset;
    uint64_t generation;
    uint64_t pending_generation;
    ID3D11Texture2D *tex;
    ID3D11ShaderResourceView *srv;
    int32_t width;
    int32_t height;
    int32_t upload_next_row;
    int32_t native_format;
    int64_t native_next_mip;
    int64_t native_mip_count;
    int8_t upload_in_progress;
    uint64_t last_used_frame;
} d3d_tex_cache_entry_t;

typedef struct {
    const void *cubemap_ptr;
    uint64_t generation;
    uint64_t pending_generation;
    ID3D11Texture2D *tex;
    ID3D11ShaderResourceView *srv;
    int32_t face_size;
    int32_t upload_face;
    int32_t upload_next_row;
    int8_t upload_in_progress;
    uint64_t last_used_frame;
} d3d_cubemap_cache_entry_t;

typedef struct {
    ID3D11Texture2D *tex;
    ID3D11ShaderResourceView *srv;
    int temporary;
} d3d_temp_srv_t;

typedef struct {
    d3d_temp_srv_t textures[11];
    d3d_temp_srv_t cubemap;
    int has_texture;
    int has_normal_map;
    int has_specular_map;
    int has_emissive_map;
    int has_env_map;
    int has_splat;
    int has_metallic_roughness_map;
    int has_ao_map;
} d3d_draw_resources_t;

typedef vgfx3d_d3d11_per_object_t d3d_per_object_t;

typedef struct {
    float vp[16];
    float prev_vp[16];
    float shadow_vp[VGFX3D_MAX_SHADOW_LIGHTS][16];
    float camera_pos[4];
    float ambient[4];
    float fog_color[4];
    float fog_near;
    float fog_far;
    float shadow_bias;
    int32_t light_count;
    int32_t shadow_count;
    float camera_forward[3];
} d3d_per_scene_t;

typedef vgfx3d_d3d11_per_material_t d3d_per_material_t;

typedef struct {
    int32_t type;
    int32_t shadow_index;
    int32_t shadow_cascade_count;
    float _pad0;
    float direction[4];
    float position[4];
    float color[4];
    float intensity;
    float attenuation;
    float inner_cos;
    float outer_cos;
    float shadow_cascade_splits[4];
} d3d_light_t;

typedef struct {
    float inverse_projection[16];
    float inverse_view_rotation[16];
    float camera_forward[4];
} d3d_skybox_cb_t;

typedef struct {
    float inv_vp[16];
    float prev_vp[16];
    float camera_pos[4];
    float inv_resolution[2];
    int32_t bloom_enabled;
    float bloom_threshold;
    float bloom_intensity;
    int32_t bloom_passes;
    int32_t tonemap_mode;
    float tonemap_exposure;
    int32_t fxaa_enabled;
    int32_t color_grade_enabled;
    float cg_brightness;
    float cg_contrast;
    float cg_saturation;
    int32_t vignette_enabled;
    float vignette_radius;
    float vignette_softness;
    int32_t ssao_enabled;
    float ssao_radius;
    float ssao_intensity;
    int32_t ssao_samples;
    int32_t dof_enabled;
    float dof_focus_distance;
    float dof_aperture;
    float dof_max_blur;
    int32_t motion_blur_enabled;
    float motion_blur_intensity;
    int32_t motion_blur_samples;
    float _pad0;
    float _pad1;
} d3d_postfx_cb_t;

typedef vgfx3d_d3d11_instance_data_t d3d_instance_data_t;

#define D3D11_MESH_CACHE_CAPACITY 128

typedef struct {
    const void *key;
    uint32_t revision;
    uint32_t vertex_count;
    uint32_t index_count;
    ID3D11Buffer *vb;
    ID3D11Buffer *ib;
    uint64_t last_used_frame;
} d3d11_mesh_cache_entry_t;

#define D3D11_MORPH_CACHE_CAPACITY 32
#define D3D11_TEXTURE_CACHE_MAX_RESIDENT 512
#define D3D11_TEXTURE_CACHE_PRUNE_AGE 600u
#define D3D11_CUBEMAP_CACHE_MAX_RESIDENT 64
#define D3D11_CUBEMAP_CACHE_PRUNE_AGE 240u

typedef struct {
    const void *key;
    uint64_t generation;
    uint32_t vertex_count;
    uint32_t shape_count;
    int has_normal_deltas;
    ID3D11Buffer *buffer;
    ID3D11ShaderResourceView *srv;
    size_t element_count;
    ID3D11Buffer *normal_buffer;
    ID3D11ShaderResourceView *normal_srv;
    size_t normal_element_count;
    uint64_t last_used_frame;
} d3d11_morph_cache_entry_t;

typedef struct {
    ID3D11Device *device;
    ID3D11DeviceContext *ctx;
    IDXGISwapChain *swap_chain;
    ID3D11RenderTargetView *rtv;
    ID3D11Texture2D *depth_tex;
    ID3D11DepthStencilView *dsv;

    ID3D11BlendState *blend_state_opaque;
    ID3D11BlendState *blend_state_alpha;
    ID3D11BlendState *blend_state_additive;
    ID3D11BlendState *blend_state_premultiplied_alpha;
    ID3D11DepthStencilState *depth_state;
    ID3D11DepthStencilState *depth_state_no_write;
    ID3D11DepthStencilState *depth_state_disabled;
    ID3D11DepthStencilState *depth_state_readonly_lequal;
    ID3D11RasterizerState *rs_solid_cull;
    ID3D11RasterizerState *rs_solid_no_cull;
    ID3D11RasterizerState *rs_wire_cull;
    ID3D11RasterizerState *rs_wire_no_cull;
    ID3D11SamplerState *linear_wrap_sampler;
    ID3D11SamplerState *linear_clamp_sampler;
    ID3D11SamplerState *shadow_cmp_sampler;
    ID3D11SamplerState *material_samplers[3][3][2];

    ID3D11VertexShader *vs_main;
    ID3D11VertexShader *vs_instanced;
    ID3D11PixelShader *ps_main;
    ID3D11VertexShader *vs_shadow;
    ID3D11PixelShader *ps_shadow;
    ID3D11VertexShader *vs_skybox;
    ID3D11PixelShader *ps_skybox;
    ID3D11VertexShader *vs_postfx;
    ID3D11PixelShader *ps_postfx;
    ID3D11PixelShader *ps_overlay_composite;

    ID3D11InputLayout *input_layout;
    ID3D11InputLayout *input_layout_instanced;
    ID3D11InputLayout *input_layout_skybox;

    ID3D11Buffer *cb_per_object;
    ID3D11Buffer *cb_per_scene;
    ID3D11Buffer *cb_per_material;
    ID3D11Buffer *cb_per_lights;
    ID3D11Buffer *cb_bones;
    ID3D11Buffer *cb_prev_bones;
    ID3D11Buffer *cb_skybox;
    ID3D11Buffer *cb_postfx;

    ID3D11Buffer *dynamic_vb;
    ID3D11Buffer *dynamic_ib;
    ID3D11Buffer *instance_buffer;
    ID3D11Buffer *skybox_vb;
    size_t dynamic_vb_size;
    size_t dynamic_ib_size;
    size_t instance_buffer_size;
    d3d_instance_data_t *instance_upload_data;
    size_t instance_upload_capacity;

    ID3D11Buffer *morph_buffer;
    ID3D11ShaderResourceView *morph_srv;
    size_t morph_buffer_size;
    ID3D11Buffer *morph_normal_buffer;
    ID3D11ShaderResourceView *morph_normal_srv;
    size_t morph_normal_buffer_size;
    ID3D11ShaderResourceView *current_morph_srv;
    ID3D11ShaderResourceView *current_morph_normal_srv;
    d3d11_morph_cache_entry_t morph_cache[D3D11_MORPH_CACHE_CAPACITY];

    ID3D11Texture2D *scene_color_tex;
    ID3D11RenderTargetView *scene_color_rtv;
    ID3D11ShaderResourceView *scene_color_srv;
    ID3D11Texture2D *scene_motion_tex;
    ID3D11RenderTargetView *scene_motion_rtv;
    ID3D11ShaderResourceView *scene_motion_srv;
    ID3D11Texture2D *scene_depth_tex;
    ID3D11DepthStencilView *scene_dsv;
    ID3D11ShaderResourceView *scene_depth_srv;
    ID3D11Texture2D *overlay_color_tex;
    ID3D11RenderTargetView *overlay_color_rtv;
    ID3D11ShaderResourceView *overlay_color_srv;
    ID3D11Texture2D *postfx_color_tex;
    ID3D11RenderTargetView *postfx_color_rtv;
    ID3D11ShaderResourceView *postfx_color_srv;
    ID3D11Texture2D *postfx_scratch_tex;
    ID3D11RenderTargetView *postfx_scratch_rtv;
    ID3D11ShaderResourceView *postfx_scratch_srv;
    ID3D11Texture2D *presented_color_tex;
    int32_t scene_width;
    int32_t scene_height;
    int32_t overlay_width;
    int32_t overlay_height;
    int32_t postfx_width;
    int32_t postfx_height;
    int32_t postfx_scratch_width;
    int32_t postfx_scratch_height;
    int32_t presented_width;
    int32_t presented_height;

    ID3D11Texture2D *rtt_color_tex;
    ID3D11RenderTargetView *rtt_rtv;
    ID3D11Texture2D *rtt_depth_tex;
    ID3D11DepthStencilView *rtt_dsv;
    ID3D11Texture2D *rtt_staging;
    int32_t rtt_width;
    int32_t rtt_height;
    int32_t rtt_color_format;
    int8_t rtt_active;
    vgfx3d_rendertarget_t *rtt_target;

    ID3D11Texture2D *shadow_depth_tex[VGFX3D_MAX_SHADOW_LIGHTS];
    ID3D11DepthStencilView *shadow_dsv[VGFX3D_MAX_SHADOW_LIGHTS];
    ID3D11ShaderResourceView *shadow_srv[VGFX3D_MAX_SHADOW_LIGHTS];
    int32_t shadow_width[VGFX3D_MAX_SHADOW_LIGHTS];
    int32_t shadow_height[VGFX3D_MAX_SHADOW_LIGHTS];
    int32_t shadow_pass_slot;
    int32_t shadow_count;
    float shadow_vp[VGFX3D_MAX_SHADOW_LIGHTS][16];
    float shadow_bias;

    d3d_tex_cache_entry_t *tex_cache;
    int32_t tex_cache_count;
    int32_t tex_cache_capacity;
    d3d_cubemap_cache_entry_t *cubemap_cache;
    int32_t cubemap_cache_count;
    int32_t cubemap_cache_capacity;
    d3d11_mesh_cache_entry_t mesh_cache[D3D11_MESH_CACHE_CAPACITY];
    uint64_t frame_serial;
    uint64_t texture_upload_bytes;
    uint64_t texture_upload_budget_bytes;

    ID3D11RenderTargetView *current_rtvs[2];
    UINT current_rtv_count;
    ID3D11DepthStencilView *current_dsv;
    vgfx3d_d3d11_target_kind_t current_target_kind;
    vgfx3d_d3d11_target_kind_t active_target_kind;
    int32_t current_width;
    int32_t current_height;

    int32_t width;
    int32_t height;
    float view[16];
    float projection[16];
    float vp[16];
    float inv_vp[16];
    float draw_prev_vp[16];
    float scene_vp[16];
    float scene_prev_vp[16];
    float scene_inv_vp[16];
    int8_t scene_history_valid;
    float cam_pos[3];
    float cam_forward[3];
    int8_t cam_is_ortho;
    float scene_cam_pos[3];
    float clear_r;
    float clear_g;
    float clear_b;
    int8_t fog_enabled;
    float fog_near;
    float fog_far;
    float fog_color[3];
    int8_t gpu_postfx_enabled;
    int8_t gpu_postfx_chain_valid;
    int8_t current_pass_is_overlay;
    int8_t current_load_existing_color;
    int8_t overlay_used_this_frame;
    int8_t scene_composited_to_swapchain;
    int8_t presented_color_valid;
    vgfx3d_postfx_chain_t gpu_postfx_chain;
} d3d11_context_t;

#define SAFE_RELEASE(x)                                                                            \
    do {                                                                                           \
        if (x) {                                                                                   \
            IUnknown_Release((IUnknown *)(x));                                                     \
            (x) = NULL;                                                                            \
        }                                                                                          \
    } while (0)

#define D3D11_INITIAL_DYNAMIC_VB_SIZE (4u * 1024u * 1024u)
#define D3D11_INITIAL_DYNAMIC_IB_SIZE (1u * 1024u * 1024u)
#define D3D11_INITIAL_INSTANCE_BUFFER_SIZE (256u * 1024u)
#define D3D11_MAX_CONSTANT_BUFFER_BYTES (64u * 1024u)

static void d3d11_destroy_ctx(void *ctx_ptr);
static void d3d11_log_hresult(const char *msg, HRESULT hr);
static void d3d11_log_shader_error(const char *stage, ID3DBlob *err_blob);
static void d3d11_present_swapchain(d3d11_context_t *ctx);
static int d3d11_snapshot_backbuffer_for_readback(d3d11_context_t *ctx);
static void d3d11_bind_render_targets(d3d11_context_t *ctx);
static void d3d11_unbind_draw_resources(d3d11_context_t *ctx);
static void d3d11_release_swapchain_main_targets(d3d11_context_t *ctx);
static HRESULT d3d11_recreate_swapchain_main_targets(d3d11_context_t *ctx,
                                                     int32_t width,
                                                     int32_t height,
                                                     const char *log_context);

/// @brief Multiply two row-major 4×4 matrices: `out = a * b`.
///
/// Naive triple loop — fine for the once-per-draw matrix combos this
/// backend computes. Row-major order matches our HLSL shader convention.
static void mat4f_mul_d3d(const float *a, const float *b, float *out) {
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            out[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c] +
                             a[r * 4 + 2] * b[2 * 4 + c] + a[r * 4 + 3] * b[3 * 4 + c];
        }
    }
}

/// @brief Log a D3D11 API failure to both the debugger output and stderr.
/// @details Formats `msg` and the raw HRESULT value as a hex string into a
///   256-byte stack buffer, then writes it via `OutputDebugStringA` (visible
///   in Visual Studio's Output pane / DebugView) and `fputs(stderr)` so CI
///   logs capture it. Intentionally does not assert or abort — most D3D11
///   failures at steady state are recoverable (e.g., device-removed events
///   are handled by the calling layer).
/// @param msg  Human-readable label for the API call that failed (e.g.,
///   `"Map(cbPostFX)"`), used as the leading text in the log entry.
/// @param hr   The HRESULT returned by the failing call, printed as 0x%08lx.
static void d3d11_log_hresult(const char *msg, HRESULT hr) {
    char buffer[256];
    snprintf(
        buffer, sizeof(buffer), "[vgfx3d_d3d11] %s failed (hr=0x%08lx)\n", msg, (unsigned long)hr);
    OutputDebugStringA(buffer);
    fputs(buffer, stderr);
}

/// @brief Print HLSL compiler diagnostics extracted from an `ID3DBlob`.
///
/// Called when `D3DCompile` returns failure; the err_blob carries the
/// human-readable line-and-column message that's actually useful for
/// debugging shader edits.
static void d3d11_log_shader_error(const char *stage, ID3DBlob *err_blob) {
    if (!err_blob)
        return;
    {
        const char *text = (const char *)ID3D10Blob_GetBufferPointer(err_blob);
        char buffer[1024];
        snprintf(buffer,
                 sizeof(buffer),
                 "[vgfx3d_d3d11] %s compile failed: %s\n",
                 stage,
                 text ? text : "(no compiler output)");
        OutputDebugStringA(buffer);
        fputs(buffer, stderr);
    }
}

/// @brief Present the back buffer with vsync (sync interval = 1).
///
/// Logs the HRESULT on failure but doesn't surface the error — present
/// failures during window resize / device removal are expected and the
/// next frame typically recovers.
static void d3d11_present_swapchain(d3d11_context_t *ctx) {
    HRESULT hr;

    if (!ctx || !ctx->swap_chain)
        return;
    (void)d3d11_snapshot_backbuffer_for_readback(ctx);
    hr = IDXGISwapChain_Present(ctx->swap_chain, 1, 0);
    if (FAILED(hr))
        d3d11_log_hresult("IDXGISwapChain::Present", hr);
}

/// @brief Runtime-compile an HLSL shader entry point to bytecode.
///
/// Wraps `D3DCompile` with strict-mode enabled and our standard error
/// reporting. The bytecode blob is returned via `*out_blob`; caller
/// owns the release. Used for both vertex and pixel shader stages
/// (the `target` parameter selects via "vs_5_0" / "ps_5_0" etc).
///
/// @param source   Null-terminated HLSL source.
/// @param entry    Entry-point function name.
/// @param target   Target profile string.
/// @param out_blob Out: compiled bytecode blob (caller releases).
/// @return S_OK on success, HRESULT failure code otherwise.
static HRESULT d3d11_compile_shader(const char *source,
                                    const char *entry,
                                    const char *target,
                                    ID3DBlob **out_blob) {
    ID3DBlob *blob = NULL;
    ID3DBlob *err_blob = NULL;
    HRESULT hr;

    if (!source || !entry || !target || !out_blob)
        return E_INVALIDARG;

    *out_blob = NULL;
    hr = D3DCompile(source,
                    strlen(source),
                    "vgfx3d_d3d11",
                    NULL,
                    NULL,
                    entry,
                    target,
                    D3DCOMPILE_ENABLE_STRICTNESS,
                    0,
                    &blob,
                    &err_blob);
    if (FAILED(hr)) {
        d3d11_log_shader_error(entry, err_blob);
        SAFE_RELEASE(err_blob);
        SAFE_RELEASE(blob);
        return hr;
    }
    SAFE_RELEASE(err_blob);
    *out_blob = blob;
    return S_OK;
}

/// @brief Create a rasterizer state with the given fill + cull modes.
///
/// Used during context init to build the four rasterizer-state combos
/// (solid+cull, solid+nocull, wire+cull, wire+nocull). Front-face is
/// counter-clockwise to match the runtime mesh/OpenGL convention, and depth clipping is on.
static HRESULT d3d11_create_rasterizer_state(d3d11_context_t *ctx,
                                             D3D11_FILL_MODE fill_mode,
                                             D3D11_CULL_MODE cull_mode,
                                             ID3D11RasterizerState **out_state) {
    D3D11_RASTERIZER_DESC desc;

    if (out_state)
        *out_state = NULL;
    if (!ctx || !ctx->device || !out_state)
        return E_INVALIDARG;
    memset(&desc, 0, sizeof(desc));
    desc.FillMode = fill_mode;
    desc.CullMode = cull_mode;
    desc.FrontCounterClockwise = TRUE;
    desc.DepthClipEnable = TRUE;
    return ID3D11Device_CreateRasterizerState(ctx->device, &desc, out_state);
}

/// @brief Pick the right pre-built rasterizer state for the given draw flags.
///
/// Two boolean dimensions × four pre-built states. Cheaper than a
/// per-draw `Create*State` call on the device.
static ID3D11RasterizerState *d3d11_choose_rasterizer(d3d11_context_t *ctx,
                                                      int8_t wireframe,
                                                      int8_t backface_cull) {
    if (!ctx)
        return NULL;
    if (wireframe)
        return backface_cull ? ctx->rs_wire_cull : ctx->rs_wire_no_cull;
    return backface_cull ? ctx->rs_solid_cull : ctx->rs_solid_no_cull;
}

/// @brief Overflow-checked size_t multiplication used by backend byte calculations.
/// @details Clears @p out before validation so a failed computation never leaves
///   a stale byte count in the caller. Used for CPU-side allocations and D3D11
///   ByteWidth / pitch calculations before narrowing to UINT.
static int d3d11_checked_mul_size(size_t a, size_t b, size_t *out) {
    if (out)
        *out = 0;
    if (!out)
        return 0;
    if (a != 0 && b > SIZE_MAX / a)
        return 0;
    *out = a * b;
    return 1;
}

/// @brief Compute a valid D3D11 constant-buffer ByteWidth.
/// @details D3D11 constant buffers must be 16-byte aligned and cannot exceed
///   4096 float4 registers (64 KiB). Returning 0 lets creation fail before a
///   wrapped or oversized ByteWidth reaches `CreateBuffer`.
static int d3d11_compute_constant_buffer_byte_width(size_t size, UINT *out_width) {
    size_t aligned_size;

    if (out_width)
        *out_width = 0;
    if (!out_width || size == 0 || size > D3D11_MAX_CONSTANT_BUFFER_BYTES || size > SIZE_MAX - 15u)
        return 0;
    aligned_size = (size + 15u) & ~(size_t)15u;
    if (aligned_size == 0 || aligned_size > D3D11_MAX_CONSTANT_BUFFER_BYTES ||
        aligned_size > UINT_MAX)
        return 0;
    *out_width = (UINT)aligned_size;
    return 1;
}

/// @brief Create a dynamic D3D11 constant buffer for one CPU-side cbuffer struct.
/// @details Centralizes the size validation and alignment policy so every cbuffer
///   created by the backend obeys the same 16-byte and 64 KiB limits.
static HRESULT d3d11_create_constant_buffer(d3d11_context_t *ctx,
                                            size_t size,
                                            ID3D11Buffer **out_buffer) {
    D3D11_BUFFER_DESC desc;

    if (out_buffer)
        *out_buffer = NULL;
    if (!ctx || !ctx->device || !out_buffer)
        return E_INVALIDARG;
    memset(&desc, 0, sizeof(desc));
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (!d3d11_compute_constant_buffer_byte_width(size, &desc.ByteWidth))
        return E_OUTOFMEMORY;
    return ID3D11Device_CreateBuffer(ctx->device, &desc, NULL, out_buffer);
}

/// @brief Initialize common non-comparison sampler fields to D3D11-valid defaults.
/// @details `D3D11_SAMPLER_DESC` is not fully valid when left zeroed: the debug
///   layer can reject `ComparisonFunc == 0` or `MaxAnisotropy == 0` even when the
///   filter is non-comparison. Callers then override filter/address modes.
static void d3d11_init_sampler_desc_defaults(D3D11_SAMPLER_DESC *desc) {
    if (!desc)
        return;
    memset(desc, 0, sizeof(*desc));
    desc->MaxAnisotropy = 1;
    desc->ComparisonFunc = D3D11_COMPARISON_NEVER;
    desc->MaxLOD = D3D11_FLOAT32_MAX;
}

/// @brief Clear the cached description of whatever RTV/DSV set the backend thinks is bound.
/// @details This updates only the CPU mirror in `d3d11_context_t`; callers that need
///   the D3D11 immediate context unbound must call `d3d11_unbind_output_targets` too.
static void d3d11_clear_current_target_bindings(d3d11_context_t *ctx) {
    if (!ctx)
        return;
    ctx->current_rtvs[0] = NULL;
    ctx->current_rtvs[1] = NULL;
    ctx->current_rtv_count = 0;
    ctx->current_dsv = NULL;
    ctx->current_width = 0;
    ctx->current_height = 0;
    ctx->current_target_kind = VGFX3D_D3D11_TARGET_SWAPCHAIN;
}

/// @brief Unbind all output-merger render targets and depth-stencil views.
/// @details Required before releasing backbuffer/offscreen targets and before
///   copying from textures that may currently be bound as RTVs.
static void d3d11_unbind_output_targets(d3d11_context_t *ctx) {
    if (!ctx || !ctx->ctx)
        return;
    ID3D11DeviceContext_OMSetRenderTargets(ctx->ctx, 0, NULL, NULL);
}

/// @brief Clear the pixel-shader SRV slots used by post-FX and overlay passes.
/// @details Slots 0..3 are scene color, depth, motion, and overlay. Clearing them
///   prevents read/write hazards before those same textures are rebound as RTVs.
static void d3d11_unbind_postfx_resources(d3d11_context_t *ctx) {
    ID3D11ShaderResourceView *null_srvs[4] = {NULL, NULL, NULL, NULL};

    if (!ctx || !ctx->ctx)
        return;
    ID3D11DeviceContext_PSSetShaderResources(ctx->ctx, 0, 4, null_srvs);
}

/// @brief Clear the pixel-shader SRV slots used by shadow-map sampling.
/// @details Shadow maps are rebound as depth outputs during shadow passes; D3D11
///   requires their shader-resource views to be detached first.
static void d3d11_unbind_shadow_resources(d3d11_context_t *ctx) {
    ID3D11ShaderResourceView *null_shadow_srvs[VGFX3D_MAX_SHADOW_LIGHTS] = {NULL};

    if (!ctx || !ctx->ctx)
        return;
    ID3D11DeviceContext_PSSetShaderResources(
        ctx->ctx, 4, VGFX3D_MAX_SHADOW_LIGHTS, null_shadow_srvs);
}

/// @brief Rebind the CPU-tracked current output targets after a temporary unbind.
/// @details Readback and RTT sync paths unbind outputs for `CopyResource`; this
///   helper restores the render target set only when one was known to be active.
static void d3d11_restore_current_target_bindings(d3d11_context_t *ctx) {
    if (!ctx || !ctx->ctx)
        return;
    if (ctx->current_rtv_count > 0 || ctx->current_dsv)
        d3d11_bind_render_targets(ctx);
}

/// @brief Map our color-format class to a DXGI texture format.
///
/// HDR16F → R16G16B16A16_FLOAT (linear, 8 bits/channel for HDR);
/// otherwise R8G8B8A8_UNORM (sRGB-style 8-bit).
static DXGI_FORMAT d3d11_color_format_to_dxgi(vgfx3d_d3d11_color_format_t format_class) {
    return format_class == VGFX3D_D3D11_COLOR_FORMAT_HDR16F ? DXGI_FORMAT_R16G16B16A16_FLOAT
                                                            : DXGI_FORMAT_R8G8B8A8_UNORM;
}

/// @brief Reset scene-history and overlay-pass state after target lifetime changes.
/// @details Resizes, post-FX disable, and target destruction invalidate the prior
///   view-projection/depth/motion history. Resetting to identity avoids using stale
///   matrices for motion blur, SSAO reconstruction, or overlay load decisions.
static void d3d11_reset_temporal_scene_state(d3d11_context_t *ctx) {
    if (!ctx)
        return;
    memcpy(ctx->draw_prev_vp, k_identity4x4, sizeof(ctx->draw_prev_vp));
    memcpy(ctx->scene_vp, k_identity4x4, sizeof(ctx->scene_vp));
    memcpy(ctx->scene_prev_vp, k_identity4x4, sizeof(ctx->scene_prev_vp));
    memcpy(ctx->scene_inv_vp, k_identity4x4, sizeof(ctx->scene_inv_vp));
    memset(ctx->scene_cam_pos, 0, sizeof(ctx->scene_cam_pos));
    ctx->scene_history_valid = 0;
    ctx->current_pass_is_overlay = 0;
    ctx->current_load_existing_color = 0;
    ctx->overlay_used_this_frame = 0;
    ctx->scene_composited_to_swapchain = 0;
    ctx->presented_color_valid = 0;
}

/// @brief Return whether every scene color/motion/depth resource is complete.
/// @details Target selection needs more than RTV/DSV pointers: post-FX presentation
///   and readback also require the SRVs and owning textures to exist.
static int d3d11_has_scene_targets(const d3d11_context_t *ctx) {
    return ctx && ctx->scene_color_tex && ctx->scene_color_rtv && ctx->scene_color_srv &&
           ctx->scene_motion_tex && ctx->scene_motion_rtv && ctx->scene_motion_srv &&
           ctx->scene_depth_tex && ctx->scene_dsv && ctx->scene_depth_srv;
}

/// @brief Return whether the separate overlay color target is complete.
static int d3d11_has_overlay_target(const d3d11_context_t *ctx) {
    return ctx && ctx->overlay_color_tex && ctx->overlay_color_rtv && ctx->overlay_color_srv;
}

/// @brief Return whether all render-to-texture resources are complete.
/// @details RTT needs color/depth outputs plus staging readback storage; a partial
///   set must fall back before the backend binds stale or NULL resources.
static int d3d11_has_rtt_targets(const d3d11_context_t *ctx) {
    return ctx && ctx->rtt_color_tex && ctx->rtt_rtv && ctx->rtt_depth_tex && ctx->rtt_dsv &&
           ctx->rtt_staging;
}

/// @brief Recompute pass flags after target allocation or fallback decisions.
/// @details The active target kind is downgraded to a complete target set first,
///   then overlay/load-existing flags are derived from the resolved target so the
///   clear path never preserves stale contents from a failed allocation.
static void d3d11_refresh_pass_flags(d3d11_context_t *ctx, const vgfx3d_camera_params_t *cam) {
    if (!ctx || !cam)
        return;
    ctx->active_target_kind = vgfx3d_d3d11_resolve_available_target(ctx->active_target_kind,
                                                                    d3d11_has_scene_targets(ctx),
                                                                    d3d11_has_overlay_target(ctx),
                                                                    d3d11_has_rtt_targets(ctx));
    ctx->current_pass_is_overlay =
        vgfx3d_d3d11_should_treat_begin_frame_as_overlay(ctx->active_target_kind,
                                                         cam->load_existing_color)
            ? 1
            : 0;
    ctx->current_load_existing_color = vgfx3d_d3d11_should_load_existing_color(
        ctx->active_target_kind, cam->load_existing_color, ctx->overlay_used_this_frame);
    if (ctx->active_target_kind == VGFX3D_D3D11_TARGET_SCENE && cam->load_existing_color)
        ctx->current_load_existing_color = 1;
}

/// @brief Map-write-unmap a constant buffer with `data`.
///
/// `D3D11_MAP_WRITE_DISCARD` so the driver can hand back a fresh GPU
/// allocation if the previous one is in flight, avoiding a CPU stall.
/// Used per-draw for the per-object/per-scene/per-material cbuffers.
static HRESULT d3d11_update_constant_buffer(d3d11_context_t *ctx,
                                            ID3D11Buffer *buffer,
                                            const void *data,
                                            size_t size) {
    D3D11_BUFFER_DESC desc;
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr;

    if (!ctx || !ctx->ctx || !buffer || !data || size == 0)
        return E_INVALIDARG;
    ID3D11Buffer_GetDesc(buffer, &desc);
    if (size > desc.ByteWidth)
        return E_INVALIDARG;
    hr = ID3D11DeviceContext_Map(
        ctx->ctx, (ID3D11Resource *)buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr))
        return hr;
    if (!mapped.pData) {
        ID3D11DeviceContext_Unmap(ctx->ctx, (ID3D11Resource *)buffer, 0);
        return E_POINTER;
    }
    memcpy(mapped.pData, data, size);
    ID3D11DeviceContext_Unmap(ctx->ctx, (ID3D11Resource *)buffer, 0);
    return S_OK;
}

/// @brief Geometric-growth pattern for dynamic VB/IB resize.
///
/// If the current buffer can hold `needed` bytes, no-op. Otherwise
/// create a new dynamic buffer at the
/// next power-of-two ≥ needed. Caller passes `initial_size` for the
/// first allocation. The old buffer is kept alive until replacement
/// succeeds. Traps via `E_OUTOFMEMORY` on size overflow.
static HRESULT d3d11_ensure_dynamic_buffer(d3d11_context_t *ctx,
                                           ID3D11Buffer **buffer,
                                           size_t *capacity,
                                           UINT bind_flags,
                                           size_t needed,
                                           size_t initial_size) {
    D3D11_BUFFER_DESC desc;
    size_t new_capacity;
    ID3D11Buffer *new_buffer = NULL;
    HRESULT hr;

    if (!ctx || !buffer || !capacity)
        return E_INVALIDARG;
    if (needed == 0)
        needed = 4;
    if (*buffer && *capacity >= needed)
        return S_OK;

    new_capacity = *capacity > 0 ? *capacity : initial_size;
    if (new_capacity == 0)
        new_capacity = 4;
    while (new_capacity < needed) {
        if (new_capacity > SIZE_MAX / 2)
            return E_OUTOFMEMORY;
        new_capacity *= 2;
    }
    if (new_capacity > UINT_MAX)
        return E_OUTOFMEMORY;

    memset(&desc, 0, sizeof(desc));
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.ByteWidth = (UINT)new_capacity;
    desc.BindFlags = bind_flags;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = ID3D11Device_CreateBuffer(ctx->device, &desc, NULL, &new_buffer);
    if (SUCCEEDED(hr)) {
        SAFE_RELEASE(*buffer);
        *buffer = new_buffer;
        *capacity = new_capacity;
    }
    return hr;
}

/// @brief Resize-if-needed plus map-write-unmap for a dynamic buffer.
///
/// One-stop helper used by the per-draw vertex / index buffer upload
/// path. Returns 0 on any failure (logged to the HRESULT logger).
static int d3d11_upload_dynamic_buffer(d3d11_context_t *ctx,
                                       ID3D11Buffer **buffer,
                                       size_t *capacity,
                                       UINT bind_flags,
                                       const void *data,
                                       size_t bytes,
                                       size_t initial_size) {
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr;

    if (!ctx || !buffer || !capacity || !data || bytes == 0)
        return 0;
    hr = d3d11_ensure_dynamic_buffer(ctx, buffer, capacity, bind_flags, bytes, initial_size);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateBuffer(dynamic)", hr);
        return 0;
    }
    hr = ID3D11DeviceContext_Map(
        ctx->ctx, (ID3D11Resource *)*buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {
        d3d11_log_hresult("Map(dynamic)", hr);
        return 0;
    }
    if (!mapped.pData) {
        ID3D11DeviceContext_Unmap(ctx->ctx, (ID3D11Resource *)*buffer, 0);
        d3d11_log_hresult("Map(dynamic)", E_POINTER);
        return 0;
    }
    memcpy(mapped.pData, data, bytes);
    ID3D11DeviceContext_Unmap(ctx->ctx, (ID3D11Resource *)*buffer, 0);
    return 1;
}

/// @brief Grow the CPU-side instance-data scratch buffer to fit `instance_count`.
///
/// Doubling growth starting at 32 entries. Used by the instanced-draw
/// path to assemble per-instance matrices before uploading.
static int d3d11_ensure_instance_upload_capacity(d3d11_context_t *ctx, int32_t instance_count) {
    size_t new_capacity;
    d3d_instance_data_t *new_data;

    if (!ctx || instance_count <= 0)
        return 0;
    if (ctx->instance_upload_capacity >= (size_t)instance_count)
        return 1;

    new_capacity = ctx->instance_upload_capacity > 0 ? ctx->instance_upload_capacity : 32u;
    while (new_capacity < (size_t)instance_count) {
        if (new_capacity > SIZE_MAX / 2u)
            return 0;
        new_capacity *= 2u;
    }
    if (new_capacity > SIZE_MAX / sizeof(*new_data))
        return 0;
    new_data =
        (d3d_instance_data_t *)realloc(ctx->instance_upload_data, new_capacity * sizeof(*new_data));
    if (!new_data)
        return 0;
    ctx->instance_upload_data = new_data;
    ctx->instance_upload_capacity = new_capacity;
    return 1;
}

// Mesh cache — keyed by `(geometry_key, geometry_revision)`. Static
// meshes are uploaded once and reused; dynamic meshes go through the
// dynamic VB/IB instead. Bounded to D3D11_MESH_CACHE_CAPACITY entries
// with LRU-style eviction.

/// @brief Release the GPU buffers in a single mesh-cache slot.
static void d3d11_release_mesh_cache_entry(d3d11_mesh_cache_entry_t *entry) {
    if (!entry)
        return;
    SAFE_RELEASE(entry->vb);
    SAFE_RELEASE(entry->ib);
    memset(entry, 0, sizeof(*entry));
}

/// @brief Release every mesh-cache slot — called during context teardown.
static void d3d11_release_mesh_cache(d3d11_context_t *ctx) {
    if (!ctx)
        return;
    for (int32_t i = 0; i < D3D11_MESH_CACHE_CAPACITY; i++)
        d3d11_release_mesh_cache_entry(&ctx->mesh_cache[i]);
}

/// @brief Create an immutable GPU buffer initialized from `data`.
///
/// `D3D11_USAGE_IMMUTABLE` enables driver-side optimizations (no
/// possibility of CPU writes), preferred for static mesh VB/IB. The
/// buffer can never be mapped after creation.
static HRESULT d3d11_create_static_buffer(d3d11_context_t *ctx,
                                          UINT bind_flags,
                                          const void *data,
                                          size_t bytes,
                                          ID3D11Buffer **out_buffer) {
    D3D11_BUFFER_DESC desc;
    D3D11_SUBRESOURCE_DATA init;

    if (out_buffer)
        *out_buffer = NULL;
    if (!ctx || !ctx->device || !data || bytes == 0 || !out_buffer || bytes > UINT_MAX)
        return E_INVALIDARG;
    memset(&desc, 0, sizeof(desc));
    memset(&init, 0, sizeof(init));
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.ByteWidth = (UINT)bytes;
    desc.BindFlags = bind_flags;
    init.pSysMem = data;
    return ID3D11Device_CreateBuffer(ctx->device, &desc, &init, out_buffer);
}

/// @brief Get VB+IB for a draw command, using the cache for static meshes.
///
/// Decision tree:
///   - No `geometry_key` (dynamic mesh): upload to `dynamic_vb/ib`.
///   - Has key + matching cache slot: return cached buffers (cache hit).
///   - Has key + no matching slot: upload to oldest cache slot
///     (LRU-eviction) using immutable buffers and stash for next time.
/// Caller must NOT release the returned buffers — they're owned by
/// either the cache or the context-level dynamic buffers.
static int d3d11_acquire_mesh_buffers(d3d11_context_t *ctx,
                                      const vgfx3d_draw_cmd_t *cmd,
                                      ID3D11Buffer **out_vb,
                                      ID3D11Buffer **out_ib) {
    size_t vertex_bytes;
    size_t index_bytes;

    if (!ctx || !cmd || !out_vb || !out_ib || !cmd->vertices || !cmd->indices ||
        cmd->vertex_count == 0 || cmd->index_count == 0)
        return 0;

    if (!d3d11_checked_mul_size(
            (size_t)cmd->vertex_count, sizeof(vgfx3d_vertex_t), &vertex_bytes) ||
        !d3d11_checked_mul_size((size_t)cmd->index_count, sizeof(uint32_t), &index_bytes))
        return 0;
    if (!cmd->geometry_key || cmd->geometry_revision == 0) {
        if (!d3d11_upload_dynamic_buffer(ctx,
                                         &ctx->dynamic_vb,
                                         &ctx->dynamic_vb_size,
                                         D3D11_BIND_VERTEX_BUFFER,
                                         cmd->vertices,
                                         vertex_bytes,
                                         D3D11_INITIAL_DYNAMIC_VB_SIZE) ||
            !d3d11_upload_dynamic_buffer(ctx,
                                         &ctx->dynamic_ib,
                                         &ctx->dynamic_ib_size,
                                         D3D11_BIND_INDEX_BUFFER,
                                         cmd->indices,
                                         index_bytes,
                                         D3D11_INITIAL_DYNAMIC_IB_SIZE))
            return 0;
        *out_vb = ctx->dynamic_vb;
        *out_ib = ctx->dynamic_ib;
        return 1;
    }

    {
        d3d11_mesh_cache_entry_t *slot = NULL;
        d3d11_mesh_cache_entry_t *oldest = NULL;
        HRESULT hr;

        for (int32_t i = 0; i < D3D11_MESH_CACHE_CAPACITY; i++) {
            d3d11_mesh_cache_entry_t *entry = &ctx->mesh_cache[i];
            if (entry->key == cmd->geometry_key) {
                slot = entry;
                break;
            }
            if (!entry->key && !slot)
                slot = entry;
            if (!oldest || entry->last_used_frame < oldest->last_used_frame)
                oldest = entry;
        }
        if (!slot)
            slot = oldest;
        if (!slot)
            return 0;

        if (slot->key != cmd->geometry_key || slot->revision != cmd->geometry_revision ||
            slot->vertex_count != cmd->vertex_count || slot->index_count != cmd->index_count ||
            !slot->vb || !slot->ib) {
            d3d11_release_mesh_cache_entry(slot);
            hr = d3d11_create_static_buffer(
                ctx, D3D11_BIND_VERTEX_BUFFER, cmd->vertices, vertex_bytes, &slot->vb);
            if (FAILED(hr)) {
                d3d11_log_hresult("CreateBuffer(static vertex)", hr);
                d3d11_release_mesh_cache_entry(slot);
                return 0;
            }
            hr = d3d11_create_static_buffer(
                ctx, D3D11_BIND_INDEX_BUFFER, cmd->indices, index_bytes, &slot->ib);
            if (FAILED(hr)) {
                d3d11_log_hresult("CreateBuffer(static index)", hr);
                d3d11_release_mesh_cache_entry(slot);
                return 0;
            }
            slot->key = cmd->geometry_key;
            slot->revision = cmd->geometry_revision;
            slot->vertex_count = cmd->vertex_count;
            slot->index_count = cmd->index_count;
        }

        slot->last_used_frame = ctx->frame_serial;
        *out_vb = slot->vb;
        *out_ib = slot->ib;
        return 1;
    }
}

/// @brief Resize a Buffer + SRV pair to hold `element_count` floats.
///
/// Creates a replacement buffer/SRV at `element_count` elements and swaps it
/// in only after both objects exist. Used for the morph-delta SRV buffers
/// (one for positions, one for normals).
static HRESULT d3d11_ensure_float_srv_buffer(d3d11_context_t *ctx,
                                             ID3D11Buffer **buffer,
                                             ID3D11ShaderResourceView **srv,
                                             size_t *capacity,
                                             size_t element_count) {
    D3D11_BUFFER_DESC desc;
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
    ID3D11Buffer *new_buffer = NULL;
    ID3D11ShaderResourceView *new_srv = NULL;
    size_t bytes;
    HRESULT hr;

    if (!ctx || !buffer || !srv || !capacity)
        return E_INVALIDARG;
    if (element_count == 0)
        return S_OK;
    if (*buffer && *srv && *capacity >= element_count)
        return S_OK;

    if (!d3d11_checked_mul_size(element_count, sizeof(float), &bytes))
        return E_OUTOFMEMORY;
    if (bytes > UINT_MAX)
        return E_OUTOFMEMORY;
    if (element_count > UINT_MAX)
        return E_OUTOFMEMORY;

    memset(&desc, 0, sizeof(desc));
    desc.ByteWidth = (UINT)bytes;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    hr = ID3D11Device_CreateBuffer(ctx->device, &desc, NULL, &new_buffer);
    if (FAILED(hr))
        return hr;

    memset(&srv_desc, 0, sizeof(srv_desc));
    srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srv_desc.Buffer.FirstElement = 0;
    srv_desc.Buffer.NumElements = (UINT)element_count;
    hr = ID3D11Device_CreateShaderResourceView(
        ctx->device, (ID3D11Resource *)new_buffer, &srv_desc, &new_srv);
    if (FAILED(hr)) {
        SAFE_RELEASE(new_buffer);
        return hr;
    }
    d3d11_unbind_draw_resources(ctx);
    SAFE_RELEASE(*srv);
    SAFE_RELEASE(*buffer);
    *buffer = new_buffer;
    *srv = new_srv;
    *capacity = element_count;
    return S_OK;
}

/// @brief Resize-if-needed plus upload for a float SRV buffer.
///
/// Uses `UpdateSubresource` (default-usage buffer) rather than map.
static HRESULT d3d11_update_float_srv_buffer(d3d11_context_t *ctx,
                                             ID3D11Buffer **buffer,
                                             ID3D11ShaderResourceView **srv,
                                             size_t *capacity,
                                             const float *data,
                                             size_t element_count) {
    D3D11_BOX box;
    size_t upload_bytes;
    HRESULT hr;

    if (!ctx || !data || element_count == 0)
        return E_INVALIDARG;
    hr = d3d11_ensure_float_srv_buffer(ctx, buffer, srv, capacity, element_count);
    if (FAILED(hr))
        return hr;
    if (!buffer || !srv || !*buffer || !*srv || !capacity)
        return E_POINTER;
    if (!vgfx3d_d3d11_compute_float_srv_update_bytes(element_count, *capacity, &upload_bytes) ||
        upload_bytes > UINT_MAX)
        return E_INVALIDARG;
    memset(&box, 0, sizeof(box));
    box.right = (UINT)upload_bytes;
    box.bottom = 1;
    box.back = 1;
    d3d11_unbind_draw_resources(ctx);
    ID3D11DeviceContext_UpdateSubresource(ctx->ctx, (ID3D11Resource *)*buffer, 0, &box, data, 0, 0);
    return S_OK;
}

// Texture and cubemap caches — keyed by stable host identity + generation so
// re-uploaded textures (Pixels.Set) get fresh GPU resources without
// aliasing allocator-reused addresses to stale uploads.

/// @brief Release every entry in the 2D-texture cache (teardown path).
static void d3d11_release_texture_cache(d3d11_context_t *ctx) {
    if (!ctx)
        return;
    for (int32_t i = 0; i < ctx->tex_cache_count; i++) {
        SAFE_RELEASE(ctx->tex_cache[i].srv);
        SAFE_RELEASE(ctx->tex_cache[i].tex);
        ctx->tex_cache[i].pixels_ptr = NULL;
        ctx->tex_cache[i].texture_asset = NULL;
    }
    ctx->tex_cache_count = 0;
    free(ctx->tex_cache);
    ctx->tex_cache = NULL;
    ctx->tex_cache_capacity = 0;
}

/// @brief Evict aged 2D texture-cache entries while keeping a resident floor.
static void d3d11_prune_texture_cache(d3d11_context_t *ctx) {
    int32_t write_index = 0;
    int32_t total_count;

    if (!ctx || !ctx->tex_cache)
        return;
    total_count = ctx->tex_cache_count;
    for (int32_t i = 0; i < total_count; i++) {
        uint64_t age = ctx->frame_serial > ctx->tex_cache[i].last_used_frame
                           ? (ctx->frame_serial - ctx->tex_cache[i].last_used_frame)
                           : 0u;
        if (vgfx3d_d3d11_should_prune_cache_entry(total_count,
                                                  write_index,
                                                  i,
                                                  age,
                                                  D3D11_TEXTURE_CACHE_MAX_RESIDENT,
                                                  D3D11_TEXTURE_CACHE_PRUNE_AGE)) {
            SAFE_RELEASE(ctx->tex_cache[i].srv);
            SAFE_RELEASE(ctx->tex_cache[i].tex);
            memset(&ctx->tex_cache[i], 0, sizeof(ctx->tex_cache[i]));
            continue;
        }
        if (write_index != i) {
            ctx->tex_cache[write_index] = ctx->tex_cache[i];
            memset(&ctx->tex_cache[i], 0, sizeof(ctx->tex_cache[i]));
        }
        write_index++;
    }
    ctx->tex_cache_count = write_index;
}

/// @brief Release every entry in the cubemap cache (teardown path).
static void d3d11_release_cubemap_cache(d3d11_context_t *ctx) {
    if (!ctx)
        return;
    for (int32_t i = 0; i < ctx->cubemap_cache_count; i++) {
        SAFE_RELEASE(ctx->cubemap_cache[i].srv);
        SAFE_RELEASE(ctx->cubemap_cache[i].tex);
        ctx->cubemap_cache[i].cubemap_ptr = NULL;
    }
    ctx->cubemap_cache_count = 0;
    free(ctx->cubemap_cache);
    ctx->cubemap_cache = NULL;
    ctx->cubemap_cache_capacity = 0;
}

/// @brief Evict aged cubemap-cache entries while keeping a minimum resident set.
/// @details Runs a compacting sweep: entries whose `last_used_frame` is more than
///   `D3D11_CUBEMAP_CACHE_PRUNE_AGE` frames behind the current `frame_serial` are
///   released, and the rest are packed to the front of the array. The
///   `count - write_index > MAX_RESIDENT` guard means we never shrink below the
///   resident floor even if everything is aged — a scene that hasn't drawn in 10
///   seconds still keeps its working set warm for the next frame.
static void d3d11_prune_cubemap_cache(d3d11_context_t *ctx) {
    int32_t write_index = 0;
    int32_t total_count;

    if (!ctx || !ctx->cubemap_cache)
        return;
    total_count = ctx->cubemap_cache_count;
    for (int32_t i = 0; i < total_count; i++) {
        uint64_t age = ctx->frame_serial > ctx->cubemap_cache[i].last_used_frame
                           ? (ctx->frame_serial - ctx->cubemap_cache[i].last_used_frame)
                           : 0u;
        if (vgfx3d_d3d11_should_prune_cache_entry(total_count,
                                                  write_index,
                                                  i,
                                                  age,
                                                  D3D11_CUBEMAP_CACHE_MAX_RESIDENT,
                                                  D3D11_CUBEMAP_CACHE_PRUNE_AGE)) {
            SAFE_RELEASE(ctx->cubemap_cache[i].srv);
            SAFE_RELEASE(ctx->cubemap_cache[i].tex);
            memset(&ctx->cubemap_cache[i], 0, sizeof(ctx->cubemap_cache[i]));
            continue;
        }
        if (write_index != i) {
            ctx->cubemap_cache[write_index] = ctx->cubemap_cache[i];
            memset(&ctx->cubemap_cache[i], 0, sizeof(ctx->cubemap_cache[i]));
        }
        write_index++;
    }
    ctx->cubemap_cache_count = write_index;
}

/// @brief Grow the texture cache table to hold `needed` entries.
///
/// Geometric growth via the shared `vgfx3d_d3d11_next_capacity` helper.
static int d3d11_ensure_tex_cache_capacity(d3d11_context_t *ctx, int32_t needed) {
    int32_t new_capacity;
    d3d_tex_cache_entry_t *new_entries;

    if (!ctx || needed <= 0)
        return 0;
    if (ctx->tex_cache_capacity >= needed)
        return 1;
    new_capacity = vgfx3d_d3d11_next_capacity(ctx->tex_cache_capacity, needed, 64);
    if (new_capacity <= ctx->tex_cache_capacity ||
        (size_t)new_capacity > SIZE_MAX / sizeof(*new_entries))
        return 0;
    new_entries = (d3d_tex_cache_entry_t *)realloc(ctx->tex_cache,
                                                   (size_t)new_capacity * sizeof(*new_entries));
    if (!new_entries)
        return 0;
    memset(new_entries + ctx->tex_cache_capacity,
           0,
           (size_t)(new_capacity - ctx->tex_cache_capacity) * sizeof(*new_entries));
    ctx->tex_cache = new_entries;
    ctx->tex_cache_capacity = new_capacity;
    return 1;
}

/// @brief Grow the cubemap cache table to hold `needed` entries.
static int d3d11_ensure_cubemap_cache_capacity(d3d11_context_t *ctx, int32_t needed) {
    int32_t new_capacity;
    d3d_cubemap_cache_entry_t *new_entries;

    if (!ctx || needed <= 0)
        return 0;
    if (ctx->cubemap_cache_capacity >= needed)
        return 1;
    new_capacity = vgfx3d_d3d11_next_capacity(ctx->cubemap_cache_capacity, needed, 16);
    if (new_capacity <= ctx->cubemap_cache_capacity ||
        (size_t)new_capacity > SIZE_MAX / sizeof(*new_entries))
        return 0;
    new_entries = (d3d_cubemap_cache_entry_t *)realloc(ctx->cubemap_cache,
                                                       (size_t)new_capacity * sizeof(*new_entries));
    if (!new_entries)
        return 0;
    memset(new_entries + ctx->cubemap_cache_capacity,
           0,
           (size_t)(new_capacity - ctx->cubemap_cache_capacity) * sizeof(*new_entries));
    ctx->cubemap_cache = new_entries;
    ctx->cubemap_cache_capacity = new_capacity;
    return 1;
}

/// @brief Maximum LOD index (float) for a cubemap's full mip chain.
/// @details Returned as a float because D3D11's `SampleLevel` / `SampleBias` expect
///   a float LOD. A 1×1 face has only one mip and thus max-LOD 0. Used to clamp sampler
///   LOD ranges and compute prefiltered environment-map roughness lookups.
static float d3d11_cubemap_max_lod(const rt_cubemap3d *cubemap) {
    int32_t mip_count;

    if (!cubemap || cubemap->face_size <= 1)
        return 0.0f;
    mip_count =
        vgfx3d_d3d11_compute_mip_count((int32_t)cubemap->face_size, (int32_t)cubemap->face_size);
    return mip_count > 1 ? (float)(mip_count - 1) : 0.0f;
}

/// @brief Release the GPU resources in one morph-target cache slot.
///
/// Each slot holds two `(buffer, srv)` pairs — one for position deltas
/// and one for normal deltas. Both are released together.
static void d3d11_release_morph_cache_entry(d3d11_morph_cache_entry_t *entry) {
    if (!entry)
        return;
    SAFE_RELEASE(entry->normal_srv);
    SAFE_RELEASE(entry->normal_buffer);
    SAFE_RELEASE(entry->srv);
    SAFE_RELEASE(entry->buffer);
    memset(entry, 0, sizeof(*entry));
}

/// @brief Release every morph-cache slot — called during context teardown.
static void d3d11_release_morph_cache(d3d11_context_t *ctx) {
    if (!ctx)
        return;
    for (int32_t i = 0; i < D3D11_MORPH_CACHE_CAPACITY; i++)
        d3d11_release_morph_cache_entry(&ctx->morph_cache[i]);
}

/// @brief Release a temporary (non-cached) SRV after the draw consumes it.
///
/// Used when the texture cache is full; we still create the resource
/// for the current draw but don't try to keep it around. Marking
/// `temporary` lets the per-draw cleanup distinguish it from cached
/// resources (which must NOT be released here).
static void d3d11_release_temp_srv(d3d_temp_srv_t *entry) {
    if (!entry || !entry->temporary)
        return;
    SAFE_RELEASE(entry->srv);
    SAFE_RELEASE(entry->tex);
    entry->temporary = 0;
}

/// @brief Add @p bytes to the context's running per-frame texture-upload total (saturating).
static void d3d11_record_texture_upload_bytes(d3d11_context_t *ctx, uint64_t bytes) {
    if (!ctx || bytes == 0)
        return;
    if (ctx->texture_upload_bytes > UINT64_MAX - bytes) {
        ctx->texture_upload_bytes = UINT64_MAX;
        return;
    }
    ctx->texture_upload_bytes += bytes;
}

/// @brief Bytes still to upload for a cached texture entry (native or RGBA streaming path).
static uint64_t d3d11_texture_pending_bytes(const d3d_tex_cache_entry_t *entry) {
    if (!entry)
        return 0;
    if (entry->texture_asset)
        return vgfx3d_textureasset_pending_native_bytes(
            entry->texture_asset, entry->native_next_mip, entry->upload_in_progress);
    return vgfx3d_pending_rgba_upload_bytes(
        entry->width, entry->height, entry->upload_next_row, entry->upload_in_progress);
}

/// @brief Bytes still to upload for a cached cubemap entry across its remaining faces/rows.
static uint64_t d3d11_cubemap_pending_bytes(const d3d_cubemap_cache_entry_t *entry) {
    if (!entry)
        return 0;
    return vgfx3d_pending_cubemap_rgba_upload_bytes(
        entry->face_size, entry->upload_face, entry->upload_next_row, entry->upload_in_progress);
}

/// @brief Total bytes still pending across all in-progress texture/cubemap uploads (saturating).
static uint64_t d3d11_get_texture_upload_pending_bytes(void *ctx_ptr) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    uint64_t total = 0;

    if (!ctx)
        return 0;
    for (int32_t i = 0; i < ctx->tex_cache_count; i++) {
        uint64_t bytes = d3d11_texture_pending_bytes(&ctx->tex_cache[i]);
        if (total > UINT64_MAX - bytes)
            return UINT64_MAX;
        total += bytes;
    }
    for (int32_t i = 0; i < ctx->cubemap_cache_count; i++) {
        uint64_t bytes = d3d11_cubemap_pending_bytes(&ctx->cubemap_cache[i]);
        if (total > UINT64_MAX - bytes)
            return UINT64_MAX;
        total += bytes;
    }
    return total;
}

/// @brief Set the per-frame byte budget that paces streaming texture uploads on this context.
static void d3d11_set_texture_upload_budget(void *ctx_ptr, uint64_t bytes) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    if (ctx)
        ctx->texture_upload_budget_bytes = bytes;
}

/// @brief Whether the device can use @p format as a sampleable 2D texture (CheckFormatSupport query).
static int d3d11_format_supports_texture_sampling(d3d11_context_t *ctx, DXGI_FORMAT format) {
    UINT support = 0;
    if (!ctx || !ctx->device)
        return 0;
    if (FAILED(ID3D11Device_CheckFormatSupport(ctx->device, format, &support)))
        return 0;
    return ((support & D3D11_FORMAT_SUPPORT_TEXTURE2D) != 0 &&
            (support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) != 0);
}

/// @brief Report which native compressed texture formats this device can upload (BC7 only on D3D11).
static int64_t d3d11_get_native_texture_caps(void *ctx_ptr) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    int64_t caps = 0;
    if (d3d11_format_supports_texture_sampling(ctx, DXGI_FORMAT_BC7_UNORM))
        caps |= RT_CANVAS3D_BACKEND_CAP_BC7;
    return caps;
}

/// @brief Map a native texture format id to its DXGI format (only BC7 is supported; else UNKNOWN).
static DXGI_FORMAT d3d11_native_texture_format(int32_t format_id) {
    if (format_id == RT_TEXTUREASSET3D_NATIVE_FORMAT_BC7)
        return DXGI_FORMAT_BC7_UNORM;
    return DXGI_FORMAT_UNKNOWN;
}

/// @brief Bytes per block-row of a native compressed mip (block columns × block byte size).
/// @details D3D11 texture updates need the source row pitch; this computes it overflow-safely.
static uint64_t d3d11_native_texture_row_bytes(const vgfx3d_native_texture_mip_t *mip) {
    uint64_t cols;
    if (!mip || mip->width <= 0 || mip->block_width <= 0 || mip->block_bytes <= 0)
        return 0;
    cols = ((uint64_t)(uint32_t)mip->width + (uint64_t)(uint32_t)mip->block_width - 1u) /
           (uint64_t)(uint32_t)mip->block_width;
    if (cols > UINT64_MAX / (uint64_t)(uint32_t)mip->block_bytes)
        return 0;
    return cols * (uint64_t)(uint32_t)mip->block_bytes;
}

/// @brief Create an RGBA8 2D texture and its shader-resource view at the given size.
/// @return S_OK with @p out_tex / @p out_srv set, or a failure HRESULT.
static HRESULT d3d11_allocate_texture_srv(d3d11_context_t *ctx,
                                          int32_t w,
                                          int32_t h,
                                          ID3D11Texture2D **out_tex,
                                          ID3D11ShaderResourceView **out_srv) {
    D3D11_TEXTURE2D_DESC desc;
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
    int32_t mip_count;
    HRESULT hr;

    if (out_tex)
        *out_tex = NULL;
    if (out_srv)
        *out_srv = NULL;
    if (!ctx || !out_tex || !out_srv)
        return E_INVALIDARG;
    if (!vgfx3d_d3d11_is_valid_texture2d_extent(w, h) || w > (int32_t)(UINT_MAX / 4u)) {
        return E_INVALIDARG;
    }

    memset(&desc, 0, sizeof(desc));
    desc.Width = (UINT)w;
    desc.Height = (UINT)h;
    mip_count = vgfx3d_d3d11_compute_mip_count(w, h);
    desc.MipLevels = (UINT)mip_count;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

    hr = ID3D11Device_CreateTexture2D(ctx->device, &desc, NULL, out_tex);
    if (SUCCEEDED(hr)) {
        memset(&srv_desc, 0, sizeof(srv_desc));
        srv_desc.Format = desc.Format;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels = desc.MipLevels;
        hr = ID3D11Device_CreateShaderResourceView(
            ctx->device, (ID3D11Resource *)*out_tex, &srv_desc, out_srv);
    }
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateTexture2D/ShaderResourceView(texture)", hr);
        SAFE_RELEASE(*out_srv);
        SAFE_RELEASE(*out_tex);
    }
    return hr;
}

/// @brief Create a native compressed (BC7) 2D texture and its shader-resource view for a mip chain.
/// @return S_OK with the texture/SRV set, or a failure HRESULT.
static HRESULT d3d11_allocate_native_texture_srv(d3d11_context_t *ctx,
                                                 const vgfx3d_native_texture_mip_t *first_mip,
                                                 int64_t mip_count,
                                                 ID3D11Texture2D **out_tex,
                                                 ID3D11ShaderResourceView **out_srv) {
    D3D11_TEXTURE2D_DESC desc;
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
    DXGI_FORMAT format;
    HRESULT hr;

    if (out_tex)
        *out_tex = NULL;
    if (out_srv)
        *out_srv = NULL;
    if (!ctx || !out_tex || !out_srv || !first_mip || mip_count <= 0 || mip_count > UINT_MAX)
        return E_INVALIDARG;
    format = d3d11_native_texture_format(first_mip->format_id);
    if (format == DXGI_FORMAT_UNKNOWN || !vgfx3d_d3d11_is_valid_texture2d_extent(first_mip->width, first_mip->height))
        return E_INVALIDARG;

    memset(&desc, 0, sizeof(desc));
    desc.Width = (UINT)first_mip->width;
    desc.Height = (UINT)first_mip->height;
    desc.MipLevels = (UINT)mip_count;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    hr = ID3D11Device_CreateTexture2D(ctx->device, &desc, NULL, out_tex);
    if (SUCCEEDED(hr)) {
        memset(&srv_desc, 0, sizeof(srv_desc));
        srv_desc.Format = desc.Format;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels = desc.MipLevels;
        hr = ID3D11Device_CreateShaderResourceView(
            ctx->device, (ID3D11Resource *)*out_tex, &srv_desc, out_srv);
    }
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateTexture2D/ShaderResourceView(native texture)", hr);
        SAFE_RELEASE(*out_srv);
        SAFE_RELEASE(*out_tex);
    }
    return hr;
}

/// @brief Upload more of an in-progress native compressed texture, bounded by the upload budget.
/// @return 1 if the upload finished this call, 0 if more mips remain.
static int d3d11_continue_native_texture_upload(d3d11_context_t *ctx,
                                                d3d_tex_cache_entry_t *entry) {
    if (!ctx || !entry || !entry->texture_asset || !entry->upload_in_progress || !entry->tex ||
        !entry->srv)
        return 0;
    while (entry->native_next_mip < entry->native_mip_count) {
        vgfx3d_native_texture_mip_t mip;
        uint64_t row_bytes;
        uint64_t block_rows;

        if (ctx->texture_upload_budget_bytes != UINT64_MAX &&
            ctx->texture_upload_bytes >= ctx->texture_upload_budget_bytes)
            return 0;
        if (!vgfx3d_textureasset_get_native_resident_mip(
                entry->texture_asset, entry->native_next_mip, &mip))
            return 0;
        row_bytes = d3d11_native_texture_row_bytes(&mip);
        block_rows = ((uint64_t)(uint32_t)mip.height + (uint64_t)(uint32_t)mip.block_height - 1u) /
                     (uint64_t)(uint32_t)mip.block_height;
        if (row_bytes == 0 || row_bytes > UINT_MAX || block_rows > UINT_MAX ||
            mip.bytes > UINT_MAX)
            return 0;
        d3d11_unbind_draw_resources(ctx);
        ID3D11DeviceContext_UpdateSubresource(ctx->ctx,
                                              (ID3D11Resource *)entry->tex,
                                              (UINT)entry->native_next_mip,
                                              NULL,
                                              mip.data,
                                              (UINT)row_bytes,
                                              (UINT)mip.bytes);
        d3d11_record_texture_upload_bytes(ctx, mip.bytes);
        entry->native_next_mip++;
        entry->last_used_frame = ctx->frame_serial;
    }
    entry->generation = entry->pending_generation;
    entry->pending_generation = 0;
    entry->upload_in_progress = 0;
    return 1;
}

/// @brief Begin uploading a native compressed TextureAsset3D: create the texture and seed the cursor.
static int d3d11_start_native_texture_upload(d3d11_context_t *ctx,
                                             d3d_tex_cache_entry_t *entry,
                                             void *asset,
                                             uint64_t cache_key) {
    vgfx3d_native_texture_mip_t first_mip;
    int64_t mip_count;

    if (!ctx || !entry || !asset ||
        !vgfx3d_textureasset_native_supported(asset, d3d11_get_native_texture_caps(ctx)) ||
        !vgfx3d_textureasset_get_native_resident_mip(asset, 0, &first_mip))
        return 0;
    mip_count = rt_textureasset3d_get_resident_mip_count(asset);
    if (mip_count <= 0)
        return 0;
    SAFE_RELEASE(entry->srv);
    SAFE_RELEASE(entry->tex);
    if (FAILED(d3d11_allocate_native_texture_srv(ctx, &first_mip, mip_count, &entry->tex, &entry->srv)))
        return 0;

    entry->pixels_ptr = NULL;
    entry->texture_asset = asset;
    entry->pending_generation = cache_key;
    entry->generation = 0;
    entry->width = first_mip.width;
    entry->height = first_mip.height;
    entry->upload_next_row = 0;
    entry->native_format = first_mip.format_id;
    entry->native_next_mip = 0;
    entry->native_mip_count = mip_count;
    entry->upload_in_progress = 1;
    entry->last_used_frame = ctx->frame_serial;
    d3d11_continue_native_texture_upload(ctx, entry);
    return 1;
}

/// @brief Upload more rows of an in-progress RGBA texture, bounded by the upload budget.
/// @return 1 if the upload finished this call, 0 if more rows remain.
static int d3d11_continue_texture_upload(
    d3d11_context_t *ctx, d3d_tex_cache_entry_t *entry, const void *pixels) {
    int32_t rows;
    int32_t slice_w = 0;
    int32_t slice_rows = 0;
    uint8_t *rgba = NULL;
    D3D11_BOX box;
    uint64_t bytes;

    if (!ctx || !entry || !pixels || !entry->upload_in_progress || !entry->tex || !entry->srv)
        return 0;
    rows = vgfx3d_upload_rows_for_budget(entry->width,
                                         entry->height,
                                         entry->upload_next_row,
                                         ctx->texture_upload_budget_bytes,
                                         ctx->texture_upload_bytes);
    if (rows <= 0)
        return 0;
    if (vgfx3d_unpack_pixels_rgba_rows(
            pixels, entry->upload_next_row, rows, 0, &slice_w, &slice_rows, &rgba) != 0 ||
        !rgba || slice_w != entry->width || slice_rows <= 0) {
        free(rgba);
        return 0;
    }

    memset(&box, 0, sizeof(box));
    box.left = 0;
    box.right = (UINT)slice_w;
    box.top = (UINT)entry->upload_next_row;
    box.bottom = (UINT)(entry->upload_next_row + slice_rows);
    box.front = 0;
    box.back = 1;
    d3d11_unbind_draw_resources(ctx);
    ID3D11DeviceContext_UpdateSubresource(
        ctx->ctx, (ID3D11Resource *)entry->tex, 0, &box, rgba, (UINT)(slice_w * 4), 0);
    bytes = (uint64_t)(uint32_t)slice_w * (uint64_t)(uint32_t)slice_rows * 4u;
    d3d11_record_texture_upload_bytes(ctx, bytes);
    free(rgba);

    entry->upload_next_row += slice_rows;
    entry->last_used_frame = ctx->frame_serial;
    if (entry->upload_next_row >= entry->height) {
        ID3D11DeviceContext_GenerateMips(ctx->ctx, entry->srv);
        entry->generation = entry->pending_generation;
        entry->pending_generation = 0;
        entry->upload_in_progress = 0;
        return 1;
    }
    return 0;
}

/// @brief Begin uploading an RGBA Pixels texture: allocate the texture/SRV and seed the row cursor.
static int d3d11_start_texture_upload(
    d3d11_context_t *ctx, d3d_tex_cache_entry_t *entry, const void *pixels, uint64_t cache_key) {
    int32_t w = 0;
    int32_t h = 0;

    if (!ctx || !entry || !pixels || !vgfx3d_get_pixels_extent(pixels, &w, &h))
        return 0;
    SAFE_RELEASE(entry->srv);
    SAFE_RELEASE(entry->tex);
    if (FAILED(d3d11_allocate_texture_srv(ctx, w, h, &entry->tex, &entry->srv)))
        return 0;

    entry->pixels_ptr = pixels;
    entry->texture_asset = NULL;
    entry->pending_generation = cache_key;
    entry->generation = 0;
    entry->width = w;
    entry->height = h;
    entry->upload_next_row = 0;
    entry->native_format = RT_TEXTUREASSET3D_NATIVE_FORMAT_NONE;
    entry->native_next_mip = 0;
    entry->native_mip_count = 0;
    entry->upload_in_progress = 1;
    entry->last_used_frame = ctx->frame_serial;
    d3d11_continue_texture_upload(ctx, entry, pixels);
    return 1;
}

/// @brief Cache lookup for textures, with auto-create and budgeted row uploads.
///
/// Three-way decision:
///   1. Hit (pixels_ptr + cache-key match) → return cached SRV.
///   2. Pixels match but stale generation → restart upload in that slot.
///   3. Miss → create new, append to cache, and return only when upload completes.
static ID3D11ShaderResourceView *d3d11_get_or_create_srv(d3d11_context_t *ctx,
                                                         const void *pixels,
                                                         d3d_temp_srv_t *out_temp) {
    uint64_t cache_key;

    if (out_temp)
        memset(out_temp, 0, sizeof(*out_temp));
    if (!ctx || !pixels)
        return NULL;
    cache_key = vgfx3d_get_pixels_cache_key(pixels);

    for (int32_t i = 0; i < ctx->tex_cache_count; i++) {
        if (ctx->tex_cache[i].pixels_ptr == pixels && ctx->tex_cache[i].generation == cache_key &&
            ctx->tex_cache[i].tex && ctx->tex_cache[i].srv) {
            ctx->tex_cache[i].last_used_frame = ctx->frame_serial;
            return ctx->tex_cache[i].srv;
        }
        if (ctx->tex_cache[i].pixels_ptr == pixels &&
            ctx->tex_cache[i].pending_generation == cache_key &&
            ctx->tex_cache[i].upload_in_progress) {
            return d3d11_continue_texture_upload(ctx, &ctx->tex_cache[i], pixels)
                       ? ctx->tex_cache[i].srv
                       : NULL;
        }
    }

    for (int32_t i = 0; i < ctx->tex_cache_count; i++) {
        if (ctx->tex_cache[i].pixels_ptr == pixels) {
            if (!d3d11_start_texture_upload(ctx, &ctx->tex_cache[i], pixels, cache_key))
                return NULL;
            return ctx->tex_cache[i].upload_in_progress ? NULL : ctx->tex_cache[i].srv;
        }
    }

    for (int32_t i = 0; i < ctx->tex_cache_count; i++) {
        if (!ctx->tex_cache[i].pixels_ptr && !ctx->tex_cache[i].texture_asset) {
            if (!d3d11_start_texture_upload(ctx, &ctx->tex_cache[i], pixels, cache_key))
                return NULL;
            return ctx->tex_cache[i].upload_in_progress ? NULL : ctx->tex_cache[i].srv;
        }
    }
    if (d3d11_ensure_tex_cache_capacity(ctx, ctx->tex_cache_count + 1)) {
        d3d_tex_cache_entry_t *entry = &ctx->tex_cache[ctx->tex_cache_count++];
        memset(entry, 0, sizeof(*entry));
        if (!d3d11_start_texture_upload(ctx, entry, pixels, cache_key)) {
            ctx->tex_cache_count--;
            return NULL;
        }
        return entry->upload_in_progress ? NULL : entry->srv;
    }
    (void)out_temp;
    return NULL;
}

/// @brief Get the SRV for a native TextureAsset3D, creating/streaming it on a cache miss.
/// @details Keyed by the asset's native cache key so residency changes invalidate the entry.
/// @return The shader-resource view to bind, or NULL if it cannot be created.
static ID3D11ShaderResourceView *d3d11_get_or_create_native_srv(d3d11_context_t *ctx,
                                                                void *asset) {
    uint64_t cache_key;

    if (!ctx || !asset || !vgfx3d_textureasset_native_supported(
                             asset, d3d11_get_native_texture_caps(ctx)))
        return NULL;
    cache_key = rt_textureasset3d_get_native_cache_key(asset);
    if (cache_key == 0)
        return NULL;

    for (int32_t i = 0; i < ctx->tex_cache_count; i++) {
        if (ctx->tex_cache[i].texture_asset == asset &&
            ctx->tex_cache[i].generation == cache_key && ctx->tex_cache[i].tex &&
            ctx->tex_cache[i].srv) {
            ctx->tex_cache[i].last_used_frame = ctx->frame_serial;
            return ctx->tex_cache[i].srv;
        }
        if (ctx->tex_cache[i].texture_asset == asset &&
            ctx->tex_cache[i].pending_generation == cache_key &&
            ctx->tex_cache[i].upload_in_progress) {
            return d3d11_continue_native_texture_upload(ctx, &ctx->tex_cache[i])
                       ? ctx->tex_cache[i].srv
                       : NULL;
        }
    }

    for (int32_t i = 0; i < ctx->tex_cache_count; i++) {
        if (ctx->tex_cache[i].texture_asset == asset) {
            if (!d3d11_start_native_texture_upload(ctx, &ctx->tex_cache[i], asset, cache_key))
                return NULL;
            return ctx->tex_cache[i].upload_in_progress ? NULL : ctx->tex_cache[i].srv;
        }
    }

    for (int32_t i = 0; i < ctx->tex_cache_count; i++) {
        if (!ctx->tex_cache[i].pixels_ptr && !ctx->tex_cache[i].texture_asset) {
            if (!d3d11_start_native_texture_upload(ctx, &ctx->tex_cache[i], asset, cache_key))
                return NULL;
            return ctx->tex_cache[i].upload_in_progress ? NULL : ctx->tex_cache[i].srv;
        }
    }
    if (d3d11_ensure_tex_cache_capacity(ctx, ctx->tex_cache_count + 1)) {
        d3d_tex_cache_entry_t *entry = &ctx->tex_cache[ctx->tex_cache_count++];
        memset(entry, 0, sizeof(*entry));
        if (!d3d11_start_native_texture_upload(ctx, entry, asset, cache_key)) {
            ctx->tex_cache_count--;
            return NULL;
        }
        return entry->upload_in_progress ? NULL : entry->srv;
    }
    return NULL;
}

/// @brief Resolve a material's texture to an SRV, preferring native blocks then RGBA Pixels.
/// @return The shader-resource view to bind, or NULL if neither source is uploadable.
static ID3D11ShaderResourceView *d3d11_get_or_create_material_srv(d3d11_context_t *ctx,
                                                                  void *asset,
                                                                  const void *pixels,
                                                                  d3d_temp_srv_t *out_temp) {
    ID3D11ShaderResourceView *srv = d3d11_get_or_create_native_srv(ctx, asset);
    if (srv)
        return srv;
    return d3d11_get_or_create_srv(ctx, pixels, out_temp);
}

/// @brief Create a cubemap texture (6 faces) and its cube shader-resource view at the given size.
/// @return S_OK with the texture/SRV set, or a failure HRESULT.
static HRESULT d3d11_allocate_cubemap_srv(d3d11_context_t *ctx,
                                          int32_t face_size,
                                          ID3D11Texture2D **out_tex,
                                          ID3D11ShaderResourceView **out_srv) {
    D3D11_TEXTURE2D_DESC desc;
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
    int32_t mip_count;
    HRESULT hr;

    if (out_tex)
        *out_tex = NULL;
    if (out_srv)
        *out_srv = NULL;
    if (!ctx || !out_tex || !out_srv)
        return E_INVALIDARG;
    if (!vgfx3d_d3d11_is_valid_cubemap_extent(face_size) || face_size > (int32_t)(UINT_MAX / 4u))
        return E_INVALIDARG;

    memset(&desc, 0, sizeof(desc));
    desc.Width = (UINT)face_size;
    desc.Height = (UINT)face_size;
    mip_count = vgfx3d_d3d11_compute_mip_count(face_size, face_size);
    desc.MipLevels = (UINT)mip_count;
    desc.ArraySize = 6;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE | D3D11_RESOURCE_MISC_GENERATE_MIPS;

    hr = ID3D11Device_CreateTexture2D(ctx->device, &desc, NULL, out_tex);
    if (SUCCEEDED(hr)) {
        memset(&srv_desc, 0, sizeof(srv_desc));
        srv_desc.Format = desc.Format;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        srv_desc.TextureCube.MipLevels = desc.MipLevels;
        hr = ID3D11Device_CreateShaderResourceView(
            ctx->device, (ID3D11Resource *)*out_tex, &srv_desc, out_srv);
    }
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateTexture2D/ShaderResourceView(cubemap)", hr);
        SAFE_RELEASE(*out_srv);
        SAFE_RELEASE(*out_tex);
    }
    return hr;
}

/// @brief Upload more of an in-progress cubemap (face by face, row band by row band) within budget.
/// @return 1 if all six faces finished this call, 0 if more remains.
static int d3d11_continue_cubemap_upload(
    d3d11_context_t *ctx, d3d_cubemap_cache_entry_t *entry, const rt_cubemap3d *cubemap) {
    int32_t mip_count;

    if (!ctx || !entry || !cubemap || !entry->upload_in_progress || !entry->tex || !entry->srv ||
        entry->face_size <= 0)
        return 0;

    mip_count = vgfx3d_d3d11_compute_mip_count(entry->face_size, entry->face_size);
    while (entry->upload_face < 6) {
        int32_t rows = vgfx3d_upload_rows_for_budget(entry->face_size,
                                                     entry->face_size,
                                                     entry->upload_next_row,
                                                     ctx->texture_upload_budget_bytes,
                                                     ctx->texture_upload_bytes);
        int32_t slice_size = 0;
        int32_t slice_rows = 0;
        uint8_t *rgba = NULL;
        D3D11_BOX box;
        uint64_t bytes;

        if (rows <= 0)
            return 0;
        if (vgfx3d_unpack_cubemap_rgba_rows(cubemap,
                                            entry->upload_face,
                                            entry->upload_next_row,
                                            rows,
                                            0,
                                            &slice_size,
                                            &slice_rows,
                                            &rgba) != 0 ||
            !rgba || slice_size != entry->face_size || slice_rows <= 0) {
            free(rgba);
            return 0;
        }

        memset(&box, 0, sizeof(box));
        box.left = 0;
        box.right = (UINT)slice_size;
        box.top = (UINT)entry->upload_next_row;
        box.bottom = (UINT)(entry->upload_next_row + slice_rows);
        box.front = 0;
        box.back = 1;
        d3d11_unbind_draw_resources(ctx);
        ID3D11DeviceContext_UpdateSubresource(
            ctx->ctx,
            (ID3D11Resource *)entry->tex,
            D3D11CalcSubresource(0, (UINT)entry->upload_face, (UINT)mip_count),
            &box,
            rgba,
            (UINT)(slice_size * 4),
            0);
        bytes = (uint64_t)(uint32_t)slice_size * (uint64_t)(uint32_t)slice_rows * 4u;
        d3d11_record_texture_upload_bytes(ctx, bytes);
        free(rgba);

        entry->upload_next_row += slice_rows;
        entry->last_used_frame = ctx->frame_serial;
        if (entry->upload_next_row < entry->face_size)
            return 0;
        entry->upload_face++;
        entry->upload_next_row = 0;
    }

    ID3D11DeviceContext_GenerateMips(ctx->ctx, entry->srv);
    entry->generation = entry->pending_generation;
    entry->pending_generation = 0;
    entry->upload_in_progress = 0;
    return 1;
}

/// @brief Begin uploading a cubemap: create the cube texture/SRV and seed the face/row cursor.
static int d3d11_start_cubemap_upload(d3d11_context_t *ctx,
                                      d3d_cubemap_cache_entry_t *entry,
                                      const rt_cubemap3d *cubemap,
                                      uint64_t generation) {
    int32_t face_size = 0;

    if (!ctx || !entry || !cubemap || !vgfx3d_get_cubemap_face_size(cubemap, &face_size))
        return 0;
    SAFE_RELEASE(entry->srv);
    SAFE_RELEASE(entry->tex);
    if (FAILED(d3d11_allocate_cubemap_srv(ctx, face_size, &entry->tex, &entry->srv)))
        return 0;

    entry->cubemap_ptr = cubemap;
    entry->pending_generation = generation;
    entry->generation = 0;
    entry->face_size = face_size;
    entry->upload_face = 0;
    entry->upload_next_row = 0;
    entry->upload_in_progress = 1;
    entry->last_used_frame = ctx->frame_serial;
    d3d11_continue_cubemap_upload(ctx, entry, cubemap);
    return 1;
}

/// @brief Cubemap-cache lookup analogous to `d3d11_get_or_create_srv`.
///
/// Same hit/stale/miss flow as the 2D texture cache; returns NULL while a
/// cubemap is still uploading under the current per-frame budget.
static ID3D11ShaderResourceView *d3d11_get_or_create_cubemap_srv(d3d11_context_t *ctx,
                                                                 const rt_cubemap3d *cubemap,
                                                                 d3d_temp_srv_t *out_temp) {
    uint64_t generation;

    if (out_temp)
        memset(out_temp, 0, sizeof(*out_temp));
    if (!ctx || !cubemap)
        return NULL;
    generation = vgfx3d_get_cubemap_generation(cubemap);

    for (int32_t i = 0; i < ctx->cubemap_cache_count; i++) {
        if (ctx->cubemap_cache[i].cubemap_ptr == cubemap &&
            ctx->cubemap_cache[i].generation == generation && ctx->cubemap_cache[i].tex &&
            ctx->cubemap_cache[i].srv) {
            ctx->cubemap_cache[i].last_used_frame = ctx->frame_serial;
            return ctx->cubemap_cache[i].srv;
        }
        if (ctx->cubemap_cache[i].cubemap_ptr == cubemap &&
            ctx->cubemap_cache[i].pending_generation == generation &&
            ctx->cubemap_cache[i].upload_in_progress) {
            ctx->cubemap_cache[i].last_used_frame = ctx->frame_serial;
            return d3d11_continue_cubemap_upload(ctx, &ctx->cubemap_cache[i], cubemap)
                       ? ctx->cubemap_cache[i].srv
                       : NULL;
        }
    }

    for (int32_t i = 0; i < ctx->cubemap_cache_count; i++) {
        if (ctx->cubemap_cache[i].cubemap_ptr == cubemap) {
            if (!d3d11_start_cubemap_upload(ctx, &ctx->cubemap_cache[i], cubemap, generation))
                return NULL;
            return ctx->cubemap_cache[i].upload_in_progress ? NULL : ctx->cubemap_cache[i].srv;
        }
    }

    for (int32_t i = 0; i < ctx->cubemap_cache_count; i++) {
        if (!ctx->cubemap_cache[i].cubemap_ptr) {
            if (!d3d11_start_cubemap_upload(ctx, &ctx->cubemap_cache[i], cubemap, generation))
                return NULL;
            return ctx->cubemap_cache[i].upload_in_progress ? NULL : ctx->cubemap_cache[i].srv;
        }
    }
    if (d3d11_ensure_cubemap_cache_capacity(ctx, ctx->cubemap_cache_count + 1)) {
        d3d_cubemap_cache_entry_t *entry = &ctx->cubemap_cache[ctx->cubemap_cache_count++];
        memset(entry, 0, sizeof(*entry));
        if (!d3d11_start_cubemap_upload(ctx, entry, cubemap, generation)) {
            ctx->cubemap_cache_count--;
            return NULL;
        }
        return entry->upload_in_progress ? NULL : entry->srv;
    }

    (void)out_temp;
    return NULL;
}

/// @brief Allocate a render-target color texture (with optional shader-readable SRV).
///
/// Defaults to single-sample, MipLevels=1. Format chosen by the
/// caller-supplied `format_class`. If `out_srv` is non-NULL, also adds
/// `D3D11_BIND_SHADER_RESOURCE` so post-FX passes can sample the
/// rendered scene.
static HRESULT d3d11_create_color_target(d3d11_context_t *ctx,
                                         int32_t width,
                                         int32_t height,
                                         vgfx3d_d3d11_color_format_t format_class,
                                         ID3D11Texture2D **out_tex,
                                         ID3D11RenderTargetView **out_rtv,
                                         ID3D11ShaderResourceView **out_srv) {
    D3D11_TEXTURE2D_DESC desc;
    D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
    HRESULT hr;

    if (out_tex)
        *out_tex = NULL;
    if (out_rtv)
        *out_rtv = NULL;
    if (out_srv)
        *out_srv = NULL;
    if (!ctx || !vgfx3d_d3d11_is_valid_texture2d_extent(width, height) || !out_tex || !out_rtv)
        return E_INVALIDARG;

    memset(&desc, 0, sizeof(desc));
    desc.Width = (UINT)width;
    desc.Height = (UINT)height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = d3d11_color_format_to_dxgi(format_class);
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | (out_srv ? D3D11_BIND_SHADER_RESOURCE : 0);

    hr = ID3D11Device_CreateTexture2D(ctx->device, &desc, NULL, out_tex);
    if (FAILED(hr))
        return hr;

    memset(&rtv_desc, 0, sizeof(rtv_desc));
    rtv_desc.Format = desc.Format;
    rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    hr = ID3D11Device_CreateRenderTargetView(
        ctx->device, (ID3D11Resource *)*out_tex, &rtv_desc, out_rtv);
    if (FAILED(hr)) {
        SAFE_RELEASE(*out_tex);
        return hr;
    }

    if (out_srv) {
        memset(&srv_desc, 0, sizeof(srv_desc));
        srv_desc.Format = desc.Format;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels = 1;
        hr = ID3D11Device_CreateShaderResourceView(
            ctx->device, (ID3D11Resource *)*out_tex, &srv_desc, out_srv);
        if (FAILED(hr)) {
            SAFE_RELEASE(*out_rtv);
            SAFE_RELEASE(*out_tex);
            return hr;
        }
    }

    return S_OK;
}

/// @brief Allocate a depth-stencil texture (with optional shader-readable SRV).
///
/// Uses TYPELESS storage when `shader_readable` is set so the texture
/// can be aliased as both DSV (D32_FLOAT) and SRV (R32_FLOAT) — this
/// pattern lets shadow maps and SSAO read the depth buffer.
static HRESULT d3d11_create_depth_target(d3d11_context_t *ctx,
                                         int32_t width,
                                         int32_t height,
                                         int shader_readable,
                                         ID3D11Texture2D **out_tex,
                                         ID3D11DepthStencilView **out_dsv,
                                         ID3D11ShaderResourceView **out_srv) {
    D3D11_TEXTURE2D_DESC desc;
    D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc;
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
    HRESULT hr;

    if (out_tex)
        *out_tex = NULL;
    if (out_dsv)
        *out_dsv = NULL;
    if (out_srv)
        *out_srv = NULL;
    if (!ctx || !vgfx3d_d3d11_is_valid_texture2d_extent(width, height) || !out_tex || !out_dsv)
        return E_INVALIDARG;

    memset(&desc, 0, sizeof(desc));
    desc.Width = (UINT)width;
    desc.Height = (UINT)height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = shader_readable ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags =
        D3D11_BIND_DEPTH_STENCIL | (shader_readable && out_srv ? D3D11_BIND_SHADER_RESOURCE : 0);

    hr = ID3D11Device_CreateTexture2D(ctx->device, &desc, NULL, out_tex);
    if (FAILED(hr))
        return hr;

    memset(&dsv_desc, 0, sizeof(dsv_desc));
    dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
    dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    hr = ID3D11Device_CreateDepthStencilView(
        ctx->device, (ID3D11Resource *)*out_tex, &dsv_desc, out_dsv);
    if (FAILED(hr)) {
        SAFE_RELEASE(*out_tex);
        return hr;
    }

    if (shader_readable && out_srv) {
        memset(&srv_desc, 0, sizeof(srv_desc));
        srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels = 1;
        hr = ID3D11Device_CreateShaderResourceView(
            ctx->device, (ID3D11Resource *)*out_tex, &srv_desc, out_srv);
        if (FAILED(hr)) {
            SAFE_RELEASE(*out_dsv);
            SAFE_RELEASE(*out_tex);
            return hr;
        }
    }

    return S_OK;
}

/// @brief Allocate a CPU-readable staging texture for `Map(READ)`-based readback.
///
/// Used by `d3d11_readback_*` to copy GPU textures to system memory
/// for screenshot capture, golden-frame tests, etc.
static HRESULT d3d11_create_staging_texture(d3d11_context_t *ctx,
                                            int32_t width,
                                            int32_t height,
                                            DXGI_FORMAT format,
                                            ID3D11Texture2D **out_tex) {
    D3D11_TEXTURE2D_DESC desc;

    if (out_tex)
        *out_tex = NULL;
    if (!ctx || !vgfx3d_d3d11_is_valid_texture2d_extent(width, height) || !out_tex)
        return E_INVALIDARG;

    memset(&desc, 0, sizeof(desc));
    desc.Width = (UINT)width;
    desc.Height = (UINT)height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    return ID3D11Device_CreateTexture2D(ctx->device, &desc, NULL, out_tex);
}

/// @brief Allocate/reuse the default-usage texture that snapshots the final swapchain image.
static HRESULT d3d11_ensure_presented_snapshot_texture(d3d11_context_t *ctx,
                                                       int32_t width,
                                                       int32_t height) {
    D3D11_TEXTURE2D_DESC desc;
    HRESULT hr;

    if (!ctx || !vgfx3d_d3d11_is_valid_texture2d_extent(width, height))
        return E_INVALIDARG;
    if (ctx->presented_color_tex && ctx->presented_width == width &&
        ctx->presented_height == height)
        return S_OK;

    SAFE_RELEASE(ctx->presented_color_tex);
    ctx->presented_width = 0;
    ctx->presented_height = 0;
    ctx->presented_color_valid = 0;

    memset(&desc, 0, sizeof(desc));
    desc.Width = (UINT)width;
    desc.Height = (UINT)height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    hr = ID3D11Device_CreateTexture2D(ctx->device, &desc, NULL, &ctx->presented_color_tex);
    if (SUCCEEDED(hr)) {
        ctx->presented_width = width;
        ctx->presented_height = height;
    }
    return hr;
}

/// @brief Capture the current swapchain backbuffer before Present invalidates it.
static int d3d11_snapshot_backbuffer_for_readback(d3d11_context_t *ctx) {
    ID3D11Texture2D *back_buffer = NULL;
    HRESULT hr;

    if (!ctx || !ctx->swap_chain || !ctx->ctx)
        return 0;
    ctx->presented_color_valid = 0;
    hr = d3d11_ensure_presented_snapshot_texture(ctx, ctx->width, ctx->height);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateTexture2D(presentedSnapshot)", hr);
        return 0;
    }
    hr = IDXGISwapChain_GetBuffer(ctx->swap_chain, 0, &IID_ID3D11Texture2D, (void **)&back_buffer);
    if (FAILED(hr)) {
        d3d11_log_hresult("IDXGISwapChain::GetBuffer(presentedSnapshot)", hr);
        return 0;
    }
    d3d11_unbind_draw_resources(ctx);
    d3d11_unbind_output_targets(ctx);
    ID3D11DeviceContext_CopyResource(ctx->ctx,
                                     (ID3D11Resource *)ctx->presented_color_tex,
                                     (ID3D11Resource *)back_buffer);
    SAFE_RELEASE(back_buffer);
    d3d11_restore_current_target_bindings(ctx);
    ctx->presented_color_valid = 1;
    return 1;
}

/// @brief Release every offscreen render-target the scene path uses.
///
/// Tears down: scene color RTV/SRV, motion-vector RTV/SRV, depth
/// DSV/SRV, and the overlay color target. Called on resize and
/// during context destruction.
static void d3d11_destroy_scene_targets(d3d11_context_t *ctx) {
    if (!ctx)
        return;
    d3d11_unbind_postfx_resources(ctx);
    d3d11_unbind_shadow_resources(ctx);
    d3d11_unbind_output_targets(ctx);
    if (ctx->current_target_kind == VGFX3D_D3D11_TARGET_SCENE ||
        ctx->current_target_kind == VGFX3D_D3D11_TARGET_OVERLAY) {
        d3d11_clear_current_target_bindings(ctx);
    }
    SAFE_RELEASE(ctx->postfx_scratch_srv);
    SAFE_RELEASE(ctx->postfx_scratch_rtv);
    SAFE_RELEASE(ctx->postfx_scratch_tex);
    SAFE_RELEASE(ctx->postfx_color_srv);
    SAFE_RELEASE(ctx->postfx_color_rtv);
    SAFE_RELEASE(ctx->postfx_color_tex);
    SAFE_RELEASE(ctx->presented_color_tex);
    SAFE_RELEASE(ctx->overlay_color_srv);
    SAFE_RELEASE(ctx->overlay_color_rtv);
    SAFE_RELEASE(ctx->overlay_color_tex);
    SAFE_RELEASE(ctx->scene_depth_srv);
    SAFE_RELEASE(ctx->scene_dsv);
    SAFE_RELEASE(ctx->scene_depth_tex);
    SAFE_RELEASE(ctx->scene_motion_srv);
    SAFE_RELEASE(ctx->scene_motion_rtv);
    SAFE_RELEASE(ctx->scene_motion_tex);
    SAFE_RELEASE(ctx->scene_color_srv);
    SAFE_RELEASE(ctx->scene_color_rtv);
    SAFE_RELEASE(ctx->scene_color_tex);
    ctx->scene_width = 0;
    ctx->scene_height = 0;
    ctx->overlay_width = 0;
    ctx->overlay_height = 0;
    ctx->postfx_width = 0;
    ctx->postfx_height = 0;
    ctx->postfx_scratch_width = 0;
    ctx->postfx_scratch_height = 0;
    ctx->presented_width = 0;
    ctx->presented_height = 0;
    ctx->presented_color_valid = 0;
    d3d11_reset_temporal_scene_state(ctx);
}

/// @brief Allocate scene render targets at the requested size, idempotent on size match.
///
/// Builds three targets: scene color (HDR-capable), motion vectors
/// (LDR), and depth (R32). On any failure the partially-built set is
/// torn back down so the next call can retry from a clean state.
static HRESULT d3d11_ensure_scene_targets(d3d11_context_t *ctx, int32_t width, int32_t height) {
    HRESULT hr;

    if (!ctx || width <= 0 || height <= 0)
        return E_INVALIDARG;
    if (ctx->scene_color_tex && ctx->scene_color_rtv && ctx->scene_color_srv &&
        ctx->scene_motion_tex && ctx->scene_motion_rtv && ctx->scene_motion_srv &&
        ctx->scene_depth_tex && ctx->scene_dsv && ctx->scene_depth_srv &&
        ctx->scene_width == width && ctx->scene_height == height)
        return S_OK;

    d3d11_destroy_scene_targets(ctx);

    hr = d3d11_create_color_target(ctx,
                                   width,
                                   height,
                                   vgfx3d_d3d11_choose_color_format(VGFX3D_D3D11_TARGET_SCENE),
                                   &ctx->scene_color_tex,
                                   &ctx->scene_color_rtv,
                                   &ctx->scene_color_srv);
    if (FAILED(hr))
        return hr;
    hr = d3d11_create_color_target(ctx,
                                   width,
                                   height,
                                   vgfx3d_d3d11_choose_color_format(VGFX3D_D3D11_TARGET_SWAPCHAIN),
                                   &ctx->scene_motion_tex,
                                   &ctx->scene_motion_rtv,
                                   &ctx->scene_motion_srv);
    if (FAILED(hr)) {
        d3d11_destroy_scene_targets(ctx);
        return hr;
    }
    hr = d3d11_create_depth_target(
        ctx, width, height, 1, &ctx->scene_depth_tex, &ctx->scene_dsv, &ctx->scene_depth_srv);
    if (FAILED(hr)) {
        d3d11_destroy_scene_targets(ctx);
        return hr;
    }

    ctx->scene_width = width;
    ctx->scene_height = height;
    return S_OK;
}

/// @brief Allocate the overlay render target (HUD layer above 3D scene).
///
/// Single color target — overlay doesn't need its own depth (it's
/// drawn 2D on top of the composited scene).
static HRESULT d3d11_ensure_overlay_target(d3d11_context_t *ctx, int32_t width, int32_t height) {
    HRESULT hr;

    if (!ctx || width <= 0 || height <= 0)
        return E_INVALIDARG;
    if (ctx->overlay_color_tex && ctx->overlay_color_rtv && ctx->overlay_color_srv &&
        ctx->overlay_width == width && ctx->overlay_height == height)
        return S_OK;
    if (ctx->overlay_color_tex || ctx->overlay_color_rtv || ctx->overlay_color_srv) {
        d3d11_unbind_postfx_resources(ctx);
        if (ctx->current_target_kind == VGFX3D_D3D11_TARGET_OVERLAY) {
            d3d11_unbind_output_targets(ctx);
            d3d11_clear_current_target_bindings(ctx);
        }
        SAFE_RELEASE(ctx->overlay_color_srv);
        SAFE_RELEASE(ctx->overlay_color_rtv);
        SAFE_RELEASE(ctx->overlay_color_tex);
        ctx->overlay_width = 0;
        ctx->overlay_height = 0;
        ctx->overlay_used_this_frame = 0;
    }
    hr = d3d11_create_color_target(ctx,
                                   width,
                                   height,
                                   vgfx3d_d3d11_choose_color_format(VGFX3D_D3D11_TARGET_OVERLAY),
                                   &ctx->overlay_color_tex,
                                   &ctx->overlay_color_rtv,
                                   &ctx->overlay_color_srv);
    if (FAILED(hr)) {
        SAFE_RELEASE(ctx->overlay_color_srv);
        SAFE_RELEASE(ctx->overlay_color_rtv);
        SAFE_RELEASE(ctx->overlay_color_tex);
    } else {
        ctx->overlay_width = width;
        ctx->overlay_height = height;
    }
    return hr;
}

/// @brief Ensure the post-processing render target is allocated at the requested dimensions.
/// @details Skips reallocation when an existing target already matches @p width × @p height.
///          On a size change (or first call) releases the old color SRV/RTV/texture, then
///          calls d3d11_create_color_target to allocate a new scene-format texture. Cleans up
///          all three D3D11 objects and returns the failure HRESULT if allocation fails.
/// @return S_OK on success; E_INVALIDARG for a null context or non-positive dimensions;
///         the D3D11 HRESULT from d3d11_create_color_target on allocation failure.
static HRESULT d3d11_ensure_postfx_target(d3d11_context_t *ctx, int32_t width, int32_t height) {
    HRESULT hr;

    if (!ctx || width <= 0 || height <= 0)
        return E_INVALIDARG;
    if (ctx->postfx_color_tex && ctx->postfx_color_rtv && ctx->postfx_color_srv &&
        ctx->postfx_width == width && ctx->postfx_height == height)
        return S_OK;

    d3d11_unbind_postfx_resources(ctx);
    d3d11_unbind_output_targets(ctx);
    SAFE_RELEASE(ctx->postfx_color_srv);
    SAFE_RELEASE(ctx->postfx_color_rtv);
    SAFE_RELEASE(ctx->postfx_color_tex);
    ctx->postfx_width = 0;
    ctx->postfx_height = 0;
    hr = d3d11_create_color_target(ctx,
                                   width,
                                   height,
                                   vgfx3d_d3d11_choose_color_format(VGFX3D_D3D11_TARGET_SCENE),
                                   &ctx->postfx_color_tex,
                                   &ctx->postfx_color_rtv,
                                   &ctx->postfx_color_srv);
    if (FAILED(hr)) {
        SAFE_RELEASE(ctx->postfx_color_srv);
        SAFE_RELEASE(ctx->postfx_color_rtv);
        SAFE_RELEASE(ctx->postfx_color_tex);
        return hr;
    }
    ctx->postfx_width = width;
    ctx->postfx_height = height;
    return S_OK;
}

/// @brief Ensure the secondary post-FX scratch target used for non-destructive ping-pong.
static HRESULT d3d11_ensure_postfx_scratch_target(d3d11_context_t *ctx,
                                                  int32_t width,
                                                  int32_t height) {
    HRESULT hr;

    if (!ctx || width <= 0 || height <= 0)
        return E_INVALIDARG;
    if (ctx->postfx_scratch_tex && ctx->postfx_scratch_rtv && ctx->postfx_scratch_srv &&
        ctx->postfx_scratch_width == width && ctx->postfx_scratch_height == height)
        return S_OK;

    d3d11_unbind_postfx_resources(ctx);
    d3d11_unbind_output_targets(ctx);
    SAFE_RELEASE(ctx->postfx_scratch_srv);
    SAFE_RELEASE(ctx->postfx_scratch_rtv);
    SAFE_RELEASE(ctx->postfx_scratch_tex);
    ctx->postfx_scratch_width = 0;
    ctx->postfx_scratch_height = 0;
    hr = d3d11_create_color_target(ctx,
                                   width,
                                   height,
                                   vgfx3d_d3d11_choose_color_format(VGFX3D_D3D11_TARGET_SCENE),
                                   &ctx->postfx_scratch_tex,
                                   &ctx->postfx_scratch_rtv,
                                   &ctx->postfx_scratch_srv);
    if (FAILED(hr)) {
        SAFE_RELEASE(ctx->postfx_scratch_srv);
        SAFE_RELEASE(ctx->postfx_scratch_rtv);
        SAFE_RELEASE(ctx->postfx_scratch_tex);
        return hr;
    }
    ctx->postfx_scratch_width = width;
    ctx->postfx_scratch_height = height;
    return S_OK;
}

/// @brief Copy the GPU-side RTT color texture down to the CPU-side `rt_pixels` payload.
/// @details When a Canvas3D rendertarget is read back via `RenderTarget3D.Pixels()`, the
///   D3D11 backend copies the bound `rtt_color_tex` through a staging texture (CPU-readable),
///   maps it, and writes the pixels into `target->color_pixels` using `rt_pixels` layout.
///   Returns 0 when preconditions fail so the caller can fall back to the last cached copy.
static int d3d11_sync_render_target_color(void *ctx_ptr, vgfx3d_rendertarget_t *target) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr;
    int restore_target = 0;
    int mapped_ok = 0;
    int ok = 0;

    if (!ctx || !target || !ctx->rtt_color_tex || !ctx->rtt_staging || ctx->rtt_target != target ||
        target->width <= 0 || target->height <= 0) {
        return 0;
    }
    if (!vgfx3d_rendertarget_ensure_color(target))
        return 0;
    if (vgfx3d_rendertarget_is_hdr(target) && !vgfx3d_rendertarget_ensure_hdr_color(target))
        return 0;

    restore_target = ctx->current_target_kind == VGFX3D_D3D11_TARGET_RTT ? 1 : 0;
    if (restore_target)
        d3d11_unbind_output_targets(ctx);

    ID3D11DeviceContext_CopyResource(
        ctx->ctx, (ID3D11Resource *)ctx->rtt_staging, (ID3D11Resource *)ctx->rtt_color_tex);
    hr = ID3D11DeviceContext_Map(
        ctx->ctx, (ID3D11Resource *)ctx->rtt_staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        d3d11_log_hresult("Map(rttStaging)", hr);
        goto cleanup;
    }
    if (!mapped.pData)
        goto cleanup;
    mapped_ok = 1;

    if (vgfx3d_rendertarget_is_hdr(target)) {
        size_t hdr_row_bytes;
        size_t rgba_row_bytes;
        if (mapped.RowPitch > (UINT)INT32_MAX)
            goto cleanup;
        if (!vgfx3d_d3d11_compute_row_bytes(target->width, 8, &hdr_row_bytes) ||
            !vgfx3d_d3d11_compute_row_bytes(target->width, 4, &rgba_row_bytes) ||
            mapped.RowPitch < hdr_row_bytes || (size_t)target->stride < rgba_row_bytes)
            goto cleanup;
        vgfx3d_copy_linear_rgba16f_to_rgba8(target->color_buf,
                                            target->stride,
                                            target->width,
                                            target->height,
                                            (const uint16_t *)mapped.pData,
                                            (int32_t)mapped.RowPitch);
        vgfx3d_copy_linear_rgba16f_to_rgba32f(target->hdr_color_buf,
                                              target->width * 4,
                                              target->width,
                                              target->height,
                                              (const uint16_t *)mapped.pData,
                                              (int32_t)mapped.RowPitch);
        target->hdr_color_valid = 1;
        ok = 1;
    } else {
        size_t row_bytes;
        if (!vgfx3d_d3d11_compute_row_bytes(target->width, 4, &row_bytes) ||
            (size_t)target->stride < row_bytes || mapped.RowPitch < row_bytes)
            goto cleanup;
        for (int32_t y = 0; y < target->height; y++) {
            memcpy(&target->color_buf[(size_t)y * (size_t)target->stride],
                   (const uint8_t *)mapped.pData + (size_t)y * mapped.RowPitch,
                   (size_t)row_bytes);
        }
        target->hdr_color_valid = 0;
        ok = 1;
    }

cleanup:
    if (mapped_ok)
        ID3D11DeviceContext_Unmap(ctx->ctx, (ID3D11Resource *)ctx->rtt_staging, 0);
    if (restore_target)
        d3d11_restore_current_target_bindings(ctx);
    if (ok)
        target->color_dirty = 0;
    return ok;
}

/// @brief Release every offscreen render-to-texture (RTT) resource.
///
/// `rtt_target` is the user-visible RT object — we set it to NULL so
/// the next bind cycle doesn't think the targets are still live.
static void d3d11_destroy_rtt_targets(d3d11_context_t *ctx) {
    if (!ctx)
        return;
    if (ctx->rtt_target) {
        if (ctx->rtt_target->color_dirty)
            d3d11_sync_render_target_color(ctx, ctx->rtt_target);
        vgfx3d_rendertarget_clear_sync(ctx->rtt_target);
    }
    if (ctx->current_target_kind == VGFX3D_D3D11_TARGET_RTT) {
        d3d11_unbind_output_targets(ctx);
        d3d11_clear_current_target_bindings(ctx);
    }
    SAFE_RELEASE(ctx->rtt_staging);
    SAFE_RELEASE(ctx->rtt_dsv);
    SAFE_RELEASE(ctx->rtt_depth_tex);
    SAFE_RELEASE(ctx->rtt_rtv);
    SAFE_RELEASE(ctx->rtt_color_tex);
    ctx->rtt_width = 0;
    ctx->rtt_height = 0;
    ctx->rtt_color_format = (int32_t)VGFX3D_RENDERTARGET_COLOR_FORMAT_UNORM8;
    ctx->rtt_active = 0;
    ctx->rtt_target = NULL;
    if (ctx->active_target_kind == VGFX3D_D3D11_TARGET_RTT)
        ctx->active_target_kind = VGFX3D_D3D11_TARGET_SWAPCHAIN;
    ctx->current_pass_is_overlay = 0;
    ctx->current_load_existing_color = 0;
    ctx->overlay_used_this_frame = 0;
}

/// @brief Allocate RTT color + depth + staging textures sized to `rt`.
///
/// Sets `rtt_active` so subsequent bind calls route to the RTT instead
/// of the swapchain. The staging texture is for CPU readback.
static HRESULT d3d11_ensure_rtt_targets(d3d11_context_t *ctx, vgfx3d_rendertarget_t *rt) {
    HRESULT hr;
    vgfx3d_d3d11_color_format_t color_format;
    DXGI_FORMAT staging_format;

    if (!ctx || !rt || rt->width <= 0 || rt->height <= 0)
        return E_INVALIDARG;
    if (ctx->rtt_color_tex && ctx->rtt_rtv && ctx->rtt_depth_tex && ctx->rtt_dsv &&
        ctx->rtt_staging && ctx->rtt_width == rt->width && ctx->rtt_height == rt->height &&
        ctx->rtt_color_format == rt->color_format) {
        if (ctx->rtt_target && ctx->rtt_target != rt) {
            if (ctx->rtt_target->color_dirty)
                d3d11_sync_render_target_color(ctx, ctx->rtt_target);
            vgfx3d_rendertarget_clear_sync(ctx->rtt_target);
        }
        ctx->rtt_active = 1;
        ctx->rtt_target = rt;
        ctx->current_pass_is_overlay = 0;
        ctx->current_load_existing_color = 0;
        ctx->overlay_used_this_frame = 0;
        rt->color_dirty = 0;
        rt->sync_color = d3d11_sync_render_target_color;
        rt->sync_color_userdata = ctx;
        return S_OK;
    }

    d3d11_destroy_rtt_targets(ctx);
    color_format = vgfx3d_rendertarget_is_hdr(rt) ? VGFX3D_D3D11_COLOR_FORMAT_HDR16F
                                                  : VGFX3D_D3D11_COLOR_FORMAT_UNORM8;
    staging_format = vgfx3d_rendertarget_is_hdr(rt) ? DXGI_FORMAT_R16G16B16A16_FLOAT
                                                    : DXGI_FORMAT_R8G8B8A8_UNORM;

    hr = d3d11_create_color_target(
        ctx, rt->width, rt->height, color_format, &ctx->rtt_color_tex, &ctx->rtt_rtv, NULL);
    if (FAILED(hr))
        return hr;
    hr = d3d11_create_depth_target(
        ctx, rt->width, rt->height, 0, &ctx->rtt_depth_tex, &ctx->rtt_dsv, NULL);
    if (FAILED(hr)) {
        d3d11_destroy_rtt_targets(ctx);
        return hr;
    }
    hr =
        d3d11_create_staging_texture(ctx, rt->width, rt->height, staging_format, &ctx->rtt_staging);
    if (FAILED(hr)) {
        d3d11_destroy_rtt_targets(ctx);
        return hr;
    }

    ctx->rtt_width = rt->width;
    ctx->rtt_height = rt->height;
    ctx->rtt_color_format = rt->color_format;
    ctx->rtt_active = 1;
    ctx->rtt_target = rt;
    ctx->current_pass_is_overlay = 0;
    ctx->current_load_existing_color = 0;
    ctx->overlay_used_this_frame = 0;
    rt->color_dirty = 0;
    rt->sync_color = d3d11_sync_render_target_color;
    rt->sync_color_userdata = ctx;
    return S_OK;
}

/// @brief Release shadow-map depth target resources.
static void d3d11_destroy_shadow_targets(d3d11_context_t *ctx) {
    if (!ctx)
        return;
    d3d11_unbind_shadow_resources(ctx);
    if (ctx->shadow_pass_slot >= 0) {
        d3d11_unbind_output_targets(ctx);
        ctx->shadow_pass_slot = -1;
    }
    for (int32_t slot = 0; slot < VGFX3D_MAX_SHADOW_LIGHTS; slot++) {
        SAFE_RELEASE(ctx->shadow_srv[slot]);
        SAFE_RELEASE(ctx->shadow_dsv[slot]);
        SAFE_RELEASE(ctx->shadow_depth_tex[slot]);
        ctx->shadow_width[slot] = 0;
        ctx->shadow_height[slot] = 0;
    }
    ctx->shadow_pass_slot = -1;
    ctx->shadow_count = 0;
}

/// @brief Release one shadow-map slot and detach it from any active bindings.
/// @details Clears all shadow SRVs first because D3D11 forbids releasing/rebinding
///   a depth texture while a shader-resource view of the same texture is still
///   visible to the pixel shader. If the slot is currently being rendered, the
///   output-merger is also detached and the active shadow-pass marker is cleared.
static void d3d11_release_shadow_slot(d3d11_context_t *ctx, int32_t slot) {
    if (!ctx || slot < 0 || slot >= VGFX3D_MAX_SHADOW_LIGHTS)
        return;
    d3d11_unbind_shadow_resources(ctx);
    if (ctx->shadow_pass_slot == slot) {
        d3d11_unbind_output_targets(ctx);
        ctx->shadow_pass_slot = -1;
    }
    SAFE_RELEASE(ctx->shadow_srv[slot]);
    SAFE_RELEASE(ctx->shadow_dsv[slot]);
    SAFE_RELEASE(ctx->shadow_depth_tex[slot]);
    ctx->shadow_width[slot] = 0;
    ctx->shadow_height[slot] = 0;
}

/// @brief Recompute the highest contiguous shadow slot advertised to shaders.
/// @details The shader receives `shadowCount` and validates each light's
///   `shadowIndex` against it. Recomputing after allocation failures prevents a
///   stale count from making an empty slot sample a NULL SRV.
static void d3d11_recompute_shadow_count(d3d11_context_t *ctx) {
    int complete[VGFX3D_MAX_SHADOW_LIGHTS] = {0};

    if (!ctx)
        return;
    for (int32_t slot = 0; slot < VGFX3D_MAX_SHADOW_LIGHTS; slot++) {
        if (ctx->shadow_srv[slot] && ctx->shadow_dsv[slot] && ctx->shadow_depth_tex[slot])
            complete[slot] = 1;
    }
    ctx->shadow_count = vgfx3d_d3d11_compute_shadow_count(VGFX3D_MAX_SHADOW_LIGHTS, complete);
}

/// @brief Allocate the shadow-map depth target at the requested size.
///
/// Always shader-readable so the main pass can sample shadows.
static HRESULT d3d11_ensure_shadow_targets(d3d11_context_t *ctx,
                                           int32_t slot,
                                           int32_t width,
                                           int32_t height) {
    HRESULT hr;

    if (!ctx || slot < 0 || slot >= VGFX3D_MAX_SHADOW_LIGHTS || width <= 0 || height <= 0)
        return E_INVALIDARG;
    if (ctx->shadow_depth_tex[slot] && ctx->shadow_dsv[slot] && ctx->shadow_srv[slot] &&
        ctx->shadow_width[slot] == width && ctx->shadow_height[slot] == height)
        return S_OK;

    d3d11_release_shadow_slot(ctx, slot);
    hr = d3d11_create_depth_target(ctx,
                                   width,
                                   height,
                                   1,
                                   &ctx->shadow_depth_tex[slot],
                                   &ctx->shadow_dsv[slot],
                                   &ctx->shadow_srv[slot]);
    if (FAILED(hr))
        return hr;
    ctx->shadow_width[slot] = width;
    ctx->shadow_height[slot] = height;
    return S_OK;
}

/// @brief Push the currently-selected render target(s) and viewport into the pipeline.
///
/// Combines `OMSetRenderTargets` (color RTVs + depth DSV) with a
/// matching `RSSetViewports`. Called whenever the active target
/// changes.
static void d3d11_bind_render_targets(d3d11_context_t *ctx) {
    D3D11_VIEWPORT viewport;

    if (!ctx || !ctx->ctx)
        return;
    ID3D11DeviceContext_OMSetRenderTargets(
        ctx->ctx, ctx->current_rtv_count, ctx->current_rtvs, ctx->current_dsv);
    if (ctx->current_width <= 0 || ctx->current_height <= 0)
        return;

    memset(&viewport, 0, sizeof(viewport));
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = (FLOAT)ctx->current_width;
    viewport.Height = (FLOAT)ctx->current_height;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    ID3D11DeviceContext_RSSetViewports(ctx->ctx, 1, &viewport);
}

/// @brief Translate the active target kind into concrete RTV/DSV pointers.
///
/// Decision tree:
///   - RTT mode → user-bound color + depth.
///   - SCENE mode → scene color + motion + depth (MRT, 2 color targets).
///   - OVERLAY mode → overlay color (with fallback to scene/swapchain).
///   - default (SWAPCHAIN) → backbuffer + main depth.
/// Then pushes everything into the pipeline via `bind_render_targets`.
static void d3d11_select_current_targets(d3d11_context_t *ctx) {
    if (!ctx)
        return;
    if (ctx->active_target_kind == VGFX3D_D3D11_TARGET_RTT && d3d11_has_rtt_targets(ctx)) {
        ctx->current_rtvs[0] = ctx->rtt_rtv;
        ctx->current_rtvs[1] = NULL;
        ctx->current_rtv_count = 1;
        ctx->current_dsv = ctx->rtt_dsv;
        ctx->current_width = ctx->rtt_width;
        ctx->current_height = ctx->rtt_height;
        ctx->current_target_kind = VGFX3D_D3D11_TARGET_RTT;
    } else if (ctx->active_target_kind == VGFX3D_D3D11_TARGET_SCENE &&
               d3d11_has_scene_targets(ctx)) {
        ctx->current_rtvs[0] = ctx->scene_color_rtv;
        ctx->current_rtvs[1] = ctx->scene_motion_rtv;
        ctx->current_rtv_count = 2;
        ctx->current_dsv = ctx->scene_dsv;
        ctx->current_width = ctx->scene_width;
        ctx->current_height = ctx->scene_height;
        ctx->current_target_kind = VGFX3D_D3D11_TARGET_SCENE;
    } else if (ctx->active_target_kind == VGFX3D_D3D11_TARGET_OVERLAY) {
        ctx->current_rtvs[1] = NULL;
        ctx->current_rtv_count = 1;
        ctx->current_dsv = NULL;
        if (d3d11_has_overlay_target(ctx)) {
            ctx->current_rtvs[0] = ctx->overlay_color_rtv;
            ctx->current_width = ctx->overlay_width;
            ctx->current_height = ctx->overlay_height;
            ctx->current_target_kind = VGFX3D_D3D11_TARGET_OVERLAY;
        } else if (d3d11_has_scene_targets(ctx)) {
            ctx->current_rtvs[0] = ctx->scene_color_rtv;
            ctx->current_width = ctx->scene_width;
            ctx->current_height = ctx->scene_height;
            ctx->current_target_kind = VGFX3D_D3D11_TARGET_SCENE;
        } else {
            ctx->current_rtvs[0] = ctx->rtv;
            ctx->current_dsv = ctx->dsv;
            ctx->current_rtv_count = ctx->rtv ? 1u : 0u;
            ctx->current_width = ctx->rtv ? ctx->width : 0;
            ctx->current_height = ctx->rtv ? ctx->height : 0;
            ctx->current_target_kind = VGFX3D_D3D11_TARGET_SWAPCHAIN;
        }
    } else {
        ctx->current_rtvs[0] = ctx->rtv;
        ctx->current_rtvs[1] = NULL;
        ctx->current_rtv_count = ctx->rtv ? 1u : 0u;
        ctx->current_dsv = ctx->dsv;
        ctx->current_width = ctx->rtv ? ctx->width : 0;
        ctx->current_height = ctx->rtv ? ctx->height : 0;
        ctx->current_target_kind = VGFX3D_D3D11_TARGET_SWAPCHAIN;
    }
    d3d11_bind_render_targets(ctx);
}

/// @brief Force-bind the swapchain backbuffer (skip the kind dispatch).
///
/// Used during present / postfx composition where we always want to
/// write to the swapchain regardless of the active target kind.
static void d3d11_bind_swapchain_target(d3d11_context_t *ctx) {
    if (!ctx)
        return;
    ctx->current_rtvs[0] = ctx->rtv;
    ctx->current_rtvs[1] = NULL;
    ctx->current_rtv_count = ctx->rtv ? 1u : 0u;
    ctx->current_dsv = NULL;
    ctx->current_width = ctx->rtv ? ctx->width : 0;
    ctx->current_height = ctx->rtv ? ctx->height : 0;
    ctx->current_target_kind = VGFX3D_D3D11_TARGET_SWAPCHAIN;
    d3d11_bind_render_targets(ctx);
}

/// @brief Clear color + (optionally) depth on the currently-bound targets.
///
/// Overlay targets always clear to transparent black. Scene MRT also
/// clears the motion-vector target to (0.5, 0.5, 0, 1) — the encoded
/// "no motion" sentinel. The `load_existing_*` flags let callers
/// preserve the contents (used by additive overlay passes).
static void d3d11_clear_current_targets(d3d11_context_t *ctx,
                                        int8_t load_existing_color,
                                        int8_t load_existing_depth) {
    float clear_color[4];
    static const float overlay_clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float motion_clear[4] = {0.5f, 0.5f, 0.0f, 1.0f};

    if (!ctx)
        return;

    if (ctx->current_target_kind == VGFX3D_D3D11_TARGET_OVERLAY) {
        memcpy(clear_color, overlay_clear, sizeof(clear_color));
    } else {
        clear_color[0] = ctx->clear_r;
        clear_color[1] = ctx->clear_g;
        clear_color[2] = ctx->clear_b;
        clear_color[3] = 1.0f;
    }

    if (ctx->current_target_kind == VGFX3D_D3D11_TARGET_OVERLAY && !load_existing_color &&
        ctx->current_rtv_count > 0 && ctx->current_rtvs[0]) {
        ID3D11DeviceContext_ClearRenderTargetView(ctx->ctx, ctx->current_rtvs[0], clear_color);
    } else if (!load_existing_color && ctx->current_rtv_count > 0 && ctx->current_rtvs[0]) {
        ID3D11DeviceContext_ClearRenderTargetView(ctx->ctx, ctx->current_rtvs[0], clear_color);
    }
    if (!load_existing_color && ctx->current_target_kind == VGFX3D_D3D11_TARGET_SCENE &&
        ctx->current_rtv_count > 1 && ctx->current_rtvs[1])
        ID3D11DeviceContext_ClearRenderTargetView(ctx->ctx, ctx->current_rtvs[1], motion_clear);
    if (!load_existing_depth && ctx->current_dsv)
        ID3D11DeviceContext_ClearDepthStencilView(
            ctx->ctx, ctx->current_dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
}

/// @brief Bind the three sampler states the pixel shader expects.
///
/// Slot 0: linear/wrap (diffuse/normal/specular textures).
/// Slot 1: comparison sampler for shadow PCF.
/// Slot 2: linear/clamp (skybox + post-processing).
static int d3d11_sampler_wrap_index(int32_t mode) {
    if (mode == RT_MATERIAL3D_TEXTURE_WRAP_CLAMP_TO_EDGE)
        return 1;
    if (mode == RT_MATERIAL3D_TEXTURE_WRAP_MIRRORED_REPEAT)
        return 2;
    return 0;
}

/// @brief Map a Viper wrap-mode index (returned by d3d11_wrap_mode_index) to the
///        corresponding D3D11_TEXTURE_ADDRESS_MODE enum value.
/// @details index 1 → CLAMP, index 2 → MIRROR, any other value → WRAP (default repeat).
static D3D11_TEXTURE_ADDRESS_MODE d3d11_sampler_address_mode(int index) {
    if (index == 1)
        return D3D11_TEXTURE_ADDRESS_CLAMP;
    if (index == 2)
        return D3D11_TEXTURE_ADDRESS_MIRROR;
    return D3D11_TEXTURE_ADDRESS_WRAP;
}

/// @brief Return a cached or newly-created D3D11 sampler state for the given draw command and
/// texture slot.
/// @details Resolves wrap_s, wrap_t, and filter from the per-slot or primary material parameters,
///          maps them to cache indices via `d3d11_sampler_wrap_index`, and returns the cached entry
///          in `ctx->material_samplers[wrap_s][wrap_t][filter]` when available. Otherwise creates
///          a new `ID3D11SamplerState` and caches it. Falls back to `ctx->linear_wrap_sampler` on
///          creation failure or null cmd.
static ID3D11SamplerState *d3d11_get_material_sampler(d3d11_context_t *ctx,
                                                      const vgfx3d_draw_cmd_t *cmd,
                                                      int32_t slot) {
    int wrap_s;
    int wrap_t;
    int filter;
    D3D11_SAMPLER_DESC desc;
    HRESULT hr;
    if (!ctx)
        return NULL;
    if (!cmd)
        return ctx->linear_wrap_sampler;
    if (slot >= 0 && slot < RT_MATERIAL3D_TEXTURE_SLOT_COUNT) {
        wrap_s = d3d11_sampler_wrap_index(cmd->texture_slot_wrap_s[slot]);
        wrap_t = d3d11_sampler_wrap_index(cmd->texture_slot_wrap_t[slot]);
        filter = cmd->texture_slot_filter[slot] == RT_MATERIAL3D_TEXTURE_FILTER_NEAREST ? 1 : 0;
    } else {
        wrap_s = d3d11_sampler_wrap_index(cmd->texture_wrap_s);
        wrap_t = d3d11_sampler_wrap_index(cmd->texture_wrap_t);
        filter = cmd->texture_filter == RT_MATERIAL3D_TEXTURE_FILTER_NEAREST ? 1 : 0;
    }
    if (ctx->material_samplers[wrap_s][wrap_t][filter])
        return ctx->material_samplers[wrap_s][wrap_t][filter];
    d3d11_init_sampler_desc_defaults(&desc);
    desc.Filter = filter ? D3D11_FILTER_MIN_MAG_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    desc.AddressU = d3d11_sampler_address_mode(wrap_s);
    desc.AddressV = d3d11_sampler_address_mode(wrap_t);
    desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    hr = ID3D11Device_CreateSamplerState(
        ctx->device, &desc, &ctx->material_samplers[wrap_s][wrap_t][filter]);
    if (FAILED(hr))
        return ctx->linear_wrap_sampler;
    return ctx->material_samplers[wrap_s][wrap_t][filter];
}

/// @brief Bind all material sampler states for a draw command to the pixel shader slots.
/// @details Material textures occupy PS sampler slots 0–7: slot 0 = base color,
///   slot 1 = shadow comparison sampler, slot 2 = linear-clamp (for screen-space
///   textures), slots 3–7 = normal, specular, emissive, metallic-roughness, AO.
///   Each per-material sampler is created lazily (or retrieved from a cache) by
///   `d3d11_get_material_sampler`, so NULL entries are skipped rather than bound.
static void d3d11_bind_common_state(d3d11_context_t *ctx, const vgfx3d_draw_cmd_t *cmd) {
    ID3D11SamplerState *material_samplers[8] = {NULL};
    if (!ctx)
        return;
    material_samplers[0] =
        d3d11_get_material_sampler(ctx, cmd, RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR);
    material_samplers[3] = d3d11_get_material_sampler(ctx, cmd, RT_MATERIAL3D_TEXTURE_SLOT_NORMAL);
    material_samplers[4] =
        d3d11_get_material_sampler(ctx, cmd, RT_MATERIAL3D_TEXTURE_SLOT_SPECULAR);
    material_samplers[5] =
        d3d11_get_material_sampler(ctx, cmd, RT_MATERIAL3D_TEXTURE_SLOT_EMISSIVE);
    material_samplers[6] =
        d3d11_get_material_sampler(ctx, cmd, RT_MATERIAL3D_TEXTURE_SLOT_METALLIC_ROUGHNESS);
    material_samplers[7] = d3d11_get_material_sampler(ctx, cmd, RT_MATERIAL3D_TEXTURE_SLOT_AO);
    if (material_samplers[0])
        ID3D11DeviceContext_PSSetSamplers(ctx->ctx, 0, 1, &material_samplers[0]);
    if (ctx->shadow_cmp_sampler)
        ID3D11DeviceContext_PSSetSamplers(ctx->ctx, 1, 1, &ctx->shadow_cmp_sampler);
    if (ctx->linear_clamp_sampler)
        ID3D11DeviceContext_PSSetSamplers(ctx->ctx, 2, 1, &ctx->linear_clamp_sampler);
    if (material_samplers[3])
        ID3D11DeviceContext_PSSetSamplers(ctx->ctx, 3, 1, &material_samplers[3]);
    if (material_samplers[4])
        ID3D11DeviceContext_PSSetSamplers(ctx->ctx, 4, 1, &material_samplers[4]);
    if (material_samplers[5])
        ID3D11DeviceContext_PSSetSamplers(ctx->ctx, 5, 1, &material_samplers[5]);
    if (material_samplers[6])
        ID3D11DeviceContext_PSSetSamplers(ctx->ctx, 6, 1, &material_samplers[6]);
    if (material_samplers[7])
        ID3D11DeviceContext_PSSetSamplers(ctx->ctx, 7, 1, &material_samplers[7]);
}

/// @brief Pack draw-command per-object state into the cbuffer struct.
///
/// Computes the normal matrix, sets up skinning / morph / instance
/// flags, and packs morph weights into float4 lanes (HLSL doesn't have
/// scalar arrays in cbuffers, so we manually pack 4 weights per vec4).
static void d3d11_prepare_object_data(const vgfx3d_draw_cmd_t *cmd, d3d_per_object_t *object_data) {
    int morph_count = 0;

    memset(object_data, 0, sizeof(*object_data));
    memcpy(object_data->model, cmd->model_matrix, sizeof(object_data->model));
    memcpy(object_data->prev_model,
           cmd->has_prev_model_matrix ? cmd->prev_model_matrix : cmd->model_matrix,
           sizeof(object_data->prev_model));
    vgfx3d_compute_normal_matrix4(cmd->model_matrix, object_data->normal);

    object_data->has_prev_model_matrix = cmd->has_prev_model_matrix ? 1 : 0;
    object_data->has_skinning =
        vgfx3d_d3d11_should_enable_skinning(cmd->bone_palette, cmd->bone_count) ? 1 : 0;
    object_data->has_prev_skinning =
        (object_data->has_skinning && cmd->prev_bone_palette != NULL) ? 1 : 0;
    object_data->has_prev_instance_matrices = cmd->has_prev_instance_matrices ? 1 : 0;

    if (cmd->morph_deltas && cmd->morph_weights && cmd->morph_shape_count > 0)
        morph_count =
            vgfx3d_d3d11_clamp_morph_shape_count(cmd->vertex_count, cmd->morph_shape_count);
    object_data->morph_shape_count = morph_count;
    object_data->vertex_count = morph_count > 0 ? (int32_t)cmd->vertex_count : 0;
    object_data->has_prev_morph_weights =
        (morph_count > 0 && cmd->prev_morph_weights != NULL) ? 1 : 0;
    object_data->has_morph_normal_deltas =
        (morph_count > 0 && cmd->morph_normal_deltas != NULL) ? 1 : 0;
    if (morph_count > 0) {
        vgfx3d_d3d11_pack_scalar_array4(object_data->morph_weights_packed,
                                        VGFX3D_D3D11_PACKED_MORPH_WEIGHT_VECS,
                                        cmd->morph_weights,
                                        morph_count);
        vgfx3d_d3d11_pack_scalar_array4(object_data->prev_morph_weights_packed,
                                        VGFX3D_D3D11_PACKED_MORPH_WEIGHT_VECS,
                                        cmd->prev_morph_weights ? cmd->prev_morph_weights
                                                                : cmd->morph_weights,
                                        morph_count);
    }
}

/// @brief Pack per-scene state (camera/lights/shadows/fog) into cbuffer struct.
///
/// `lights` parameter is unused at this layer — light data goes into
/// the dedicated PerLights cbuffer via `prepare_light_data`. This
/// struct holds the everything-else scene constants.
static void d3d11_prepare_scene_data(d3d11_context_t *ctx,
                                     const vgfx3d_light_params_t *lights,
                                     int32_t light_count,
                                     const float *ambient,
                                     d3d_per_scene_t *scene_data) {
    memset(scene_data, 0, sizeof(*scene_data));
    memcpy(scene_data->vp, ctx->vp, sizeof(scene_data->vp));
    memcpy(scene_data->prev_vp, ctx->draw_prev_vp, sizeof(scene_data->prev_vp));
    memcpy(scene_data->shadow_vp, ctx->shadow_vp, sizeof(scene_data->shadow_vp));
    memcpy(scene_data->camera_pos, ctx->cam_pos, sizeof(float) * 3);
    scene_data->camera_pos[3] = ctx->cam_is_ortho ? 1.0f : 0.0f;
    memcpy(scene_data->camera_forward, ctx->cam_forward, sizeof(scene_data->camera_forward));
    if (ambient) {
        scene_data->ambient[0] = ambient[0];
        scene_data->ambient[1] = ambient[1];
        scene_data->ambient[2] = ambient[2];
    }
    scene_data->ambient[3] = 1.0f;
    scene_data->fog_color[0] = ctx->fog_color[0];
    scene_data->fog_color[1] = ctx->fog_color[1];
    scene_data->fog_color[2] = ctx->fog_color[2];
    scene_data->fog_color[3] = ctx->fog_enabled ? 1.0f : 0.0f;
    scene_data->fog_near = ctx->fog_near;
    scene_data->fog_far = ctx->fog_far;
    scene_data->shadow_bias = ctx->shadow_bias;
    scene_data->light_count =
        lights ? (light_count < 0
                      ? 0
                      : (light_count > VGFX3D_MAX_LIGHTS ? VGFX3D_MAX_LIGHTS : light_count))
               : 0;
    scene_data->shadow_count = vgfx3d_d3d11_clamp_shadow_count(ctx->shadow_count);
}

/// @brief Pack material colors, scalars, and texture-availability flags into the cbuffer.
///
/// Bundles every per-material constant the pixel shader needs:
/// PBR scalars (metallic, roughness, AO, emissive intensity), normal-
/// scale, alpha-cutoff, custom params, and the boolean flags telling
/// the shader which texture slots are populated.
static void d3d11_prepare_material_data(const vgfx3d_draw_cmd_t *cmd,
                                        int has_texture,
                                        int has_normal_map,
                                        int has_specular_map,
                                        int has_emissive_map,
                                        int has_env_map,
                                        int has_splat,
                                        int has_metallic_roughness_map,
                                        int has_ao_map,
                                        d3d_per_material_t *material_data) {
    memset(material_data, 0, sizeof(*material_data));
    memcpy(material_data->diffuse, cmd->diffuse_color, sizeof(material_data->diffuse));
    material_data->specular[0] = cmd->specular[0];
    material_data->specular[1] = cmd->specular[1];
    material_data->specular[2] = cmd->specular[2];
    material_data->specular[3] = cmd->shininess;
    material_data->emissive[0] = cmd->emissive_color[0];
    material_data->emissive[1] = cmd->emissive_color[1];
    material_data->emissive[2] = cmd->emissive_color[2];
    material_data->scalars[0] = cmd->alpha;
    material_data->scalars[1] = cmd->reflectivity;
    material_data->scalars[2] =
        cmd->env_map ? d3d11_cubemap_max_lod((const rt_cubemap3d *)cmd->env_map) : 0.0f;
    material_data->pbr_scalars0[0] = cmd->metallic;
    material_data->pbr_scalars0[1] = cmd->roughness;
    material_data->pbr_scalars0[2] = cmd->ao;
    material_data->pbr_scalars0[3] = cmd->emissive_intensity;
    material_data->pbr_scalars1[0] = cmd->normal_scale;
    material_data->pbr_scalars1[1] = cmd->alpha_cutoff;
    material_data->flags0[0] = has_texture;
    material_data->flags0[1] = cmd->unlit;
    material_data->flags0[2] = has_normal_map;
    material_data->flags0[3] = has_specular_map;
    material_data->flags1[0] = has_emissive_map;
    material_data->flags1[1] = has_env_map;
    material_data->flags1[2] = has_splat;
    material_data->pbr_flags[0] = cmd->workflow;
    material_data->pbr_flags[1] = cmd->alpha_mode;
    material_data->pbr_flags[2] = has_metallic_roughness_map;
    material_data->pbr_flags[3] = has_ao_map;
    material_data->shading_model = cmd->shading_model;
    vgfx3d_d3d11_pack_scalar_array4(material_data->custom_params_packed,
                                    VGFX3D_D3D11_PACKED_CUSTOM_PARAM_VECS,
                                    cmd->custom_params,
                                    8);
    for (int32_t slot = 0; slot < RT_MATERIAL3D_TEXTURE_SLOT_COUNT; slot++) {
        if (slot < 4)
            material_data->texture_uv_sets0[slot] = cmd->texture_slot_uv_set[slot];
        else
            material_data->texture_uv_sets1[slot - 4] = cmd->texture_slot_uv_set[slot];
        material_data->texture_uv_transform0[slot][0] = cmd->texture_slot_uv_transform[slot][0];
        material_data->texture_uv_transform0[slot][1] = cmd->texture_slot_uv_transform[slot][1];
        material_data->texture_uv_transform0[slot][2] = cmd->texture_slot_uv_transform[slot][2];
        material_data->texture_uv_transform0[slot][3] = cmd->texture_slot_uv_transform[slot][3];
        material_data->texture_uv_transform1[slot][0] = cmd->texture_slot_uv_transform[slot][4];
        material_data->texture_uv_transform1[slot][1] = cmd->texture_slot_uv_transform[slot][5];
    }
    memcpy(
        material_data->splat_scales, cmd->splat_layer_scales, sizeof(material_data->splat_scales));
}

/// @brief Copy up to `VGFX3D_MAX_LIGHTS` lights from the scene into the cbuffer Light array.
///
/// Each light contributes type, direction, position, color, intensity,
/// attenuation, and spot-cone parameters. Anything beyond `VGFX3D_MAX_LIGHTS`
/// is dropped silently (the shader's array is fixed-size). Shadow indices are
/// sanitized against the contiguous advertised shadow-slot range so a stale or
/// sparse light cannot sample an unbound shadow SRV.
static void d3d11_prepare_light_data(const vgfx3d_light_params_t *lights,
                                     int32_t light_count,
                                     int32_t advertised_shadow_count,
                                     d3d_light_t *light_data) {
    if (!light_data)
        return;
    memset(light_data, 0, sizeof(d3d_light_t) * VGFX3D_MAX_LIGHTS);
    if (!lights || light_count <= 0)
        return;
    for (int32_t i = 0; i < light_count && i < VGFX3D_MAX_LIGHTS; i++) {
        light_data[i].type = lights[i].type;
        light_data[i].shadow_index =
            vgfx3d_d3d11_sanitize_shadow_index(lights[i].shadow_index, advertised_shadow_count);
        light_data[i].shadow_cascade_count =
            light_data[i].shadow_index >= 0 ? lights[i].shadow_cascade_count : 1;
        light_data[i].direction[0] = lights[i].direction[0];
        light_data[i].direction[1] = lights[i].direction[1];
        light_data[i].direction[2] = lights[i].direction[2];
        light_data[i].position[0] = lights[i].position[0];
        light_data[i].position[1] = lights[i].position[1];
        light_data[i].position[2] = lights[i].position[2];
        light_data[i].color[0] = lights[i].color[0];
        light_data[i].color[1] = lights[i].color[1];
        light_data[i].color[2] = lights[i].color[2];
        light_data[i].intensity = lights[i].intensity;
        light_data[i].attenuation = lights[i].attenuation;
        light_data[i].inner_cos = lights[i].inner_cos;
        light_data[i].outer_cos = lights[i].outer_cos;
        memcpy(light_data[i].shadow_cascade_splits,
               lights[i].shadow_cascade_splits,
               sizeof(light_data[i].shadow_cascade_splits));
    }
}

/// @brief Upload bone palettes and morph deltas, with caching.
///
/// Bones are cbuffer arrays (current + previous-frame for motion
/// blur). Morph deltas live in SRV buffers and are cached by
/// `(morph_key, morph_revision)` so static morph targets aren't
/// re-uploaded each frame. Returns 1 if the SRVs are bound, 0 if
/// the draw should fall back to no-morph.
static int d3d11_prepare_anim_resources(d3d11_context_t *ctx,
                                        const vgfx3d_draw_cmd_t *cmd,
                                        d3d_per_object_t *object_data) {
    float bone_palette[VGFX3D_D3D11_BONE_PALETTE_FLOATS];
    float prev_bone_palette[VGFX3D_D3D11_BONE_PALETTE_FLOATS];
    HRESULT hr;
    size_t morph_count;
    int current_bone_upload_ok = 1;
    int prev_bone_upload_ok = 1;
    int morph_upload_ok = 1;
    int morph_normal_upload_ok = 1;

    if (!ctx || !cmd || !object_data)
        return 0;

    ctx->current_morph_srv = NULL;
    ctx->current_morph_normal_srv = NULL;

    vgfx3d_d3d11_pack_bone_palette(
        bone_palette, object_data->has_skinning ? cmd->bone_palette : NULL, cmd->bone_count);
    vgfx3d_d3d11_pack_bone_palette(prev_bone_palette,
                                   object_data->has_prev_skinning
                                       ? cmd->prev_bone_palette
                                       : (object_data->has_skinning ? cmd->bone_palette : NULL),
                                   cmd->bone_count);

    hr = d3d11_update_constant_buffer(ctx, ctx->cb_bones, bone_palette, sizeof(bone_palette));
    if (FAILED(hr))
        d3d11_log_hresult("Map(cbBones)", hr);
    current_bone_upload_ok = SUCCEEDED(hr);
    hr = d3d11_update_constant_buffer(
        ctx, ctx->cb_prev_bones, prev_bone_palette, sizeof(prev_bone_palette));
    if (FAILED(hr))
        d3d11_log_hresult("Map(cbPrevBones)", hr);
    prev_bone_upload_ok = SUCCEEDED(hr);
    vgfx3d_d3d11_resolve_bone_upload_status(
        object_data, current_bone_upload_ok, prev_bone_upload_ok);

    if (object_data->morph_shape_count <= 0 || !cmd->morph_deltas || !cmd->morph_weights) {
        object_data->morph_shape_count = 0;
        object_data->vertex_count = 0;
        object_data->has_prev_morph_weights = 0;
        object_data->has_morph_normal_deltas = 0;
        return 0;
    }

    if (!d3d11_checked_mul_size(
            (size_t)object_data->morph_shape_count, (size_t)cmd->vertex_count, &morph_count) ||
        !d3d11_checked_mul_size(morph_count, 3u, &morph_count)) {
        object_data->morph_shape_count = 0;
        object_data->vertex_count = 0;
        object_data->has_prev_morph_weights = 0;
        object_data->has_morph_normal_deltas = 0;
        return 0;
    }
    morph_upload_ok = 0;
    morph_normal_upload_ok = object_data->has_morph_normal_deltas ? 0 : 1;
    if (cmd->morph_key && cmd->morph_revision != 0) {
        d3d11_morph_cache_entry_t *slot = NULL;
        d3d11_morph_cache_entry_t *oldest = NULL;

        for (int32_t i = 0; i < D3D11_MORPH_CACHE_CAPACITY; i++) {
            d3d11_morph_cache_entry_t *entry = &ctx->morph_cache[i];
            if (entry->key == cmd->morph_key) {
                slot = entry;
                break;
            }
            if (!entry->key && !slot)
                slot = entry;
            if (!oldest || entry->last_used_frame < oldest->last_used_frame)
                oldest = entry;
        }
        if (!slot)
            slot = oldest;
        if (slot) {
            int cache_reusable =
                slot->buffer && slot->srv &&
                (!object_data->has_morph_normal_deltas ||
                 (slot->normal_buffer && slot->normal_srv)) &&
                vgfx3d_d3d11_should_reuse_morph_cache(slot->key,
                                                      slot->generation,
                                                      (int32_t)slot->shape_count,
                                                      slot->vertex_count,
                                                      (int8_t)slot->has_normal_deltas,
                                                      cmd);
            if (!cache_reusable) {
                d3d11_unbind_draw_resources(ctx);
                d3d11_release_morph_cache_entry(slot);
                hr = d3d11_update_float_srv_buffer(ctx,
                                                   &slot->buffer,
                                                   &slot->srv,
                                                   &slot->element_count,
                                                   cmd->morph_deltas,
                                                   morph_count);
                if (FAILED(hr)) {
                    d3d11_log_hresult("Create/UpdateBuffer(morphDeltas cache)", hr);
                } else {
                    slot->key = cmd->morph_key;
                    slot->generation = cmd->morph_revision;
                    slot->vertex_count = (uint32_t)cmd->vertex_count;
                    slot->shape_count = (uint32_t)object_data->morph_shape_count;
                    slot->has_normal_deltas = object_data->has_morph_normal_deltas ? 1 : 0;
                    morph_upload_ok = 1;
                    if (object_data->has_morph_normal_deltas) {
                        hr = d3d11_update_float_srv_buffer(ctx,
                                                           &slot->normal_buffer,
                                                           &slot->normal_srv,
                                                           &slot->normal_element_count,
                                                           cmd->morph_normal_deltas,
                                                           morph_count);
                        if (FAILED(hr)) {
                            d3d11_log_hresult("Create/UpdateBuffer(morphNormalDeltas cache)", hr);
                        } else {
                            morph_normal_upload_ok = 1;
                        }
                    }
                }
            } else {
                morph_upload_ok = 1;
                morph_normal_upload_ok =
                    object_data->has_morph_normal_deltas ? (slot->normal_srv != NULL) : 1;
            }
            if (morph_upload_ok) {
                slot->last_used_frame = ctx->frame_serial;
                ctx->current_morph_srv = slot->srv;
                ctx->current_morph_normal_srv = morph_normal_upload_ok ? slot->normal_srv : NULL;
            }
        }
    }

    if (!ctx->current_morph_srv) {
        hr = d3d11_update_float_srv_buffer(ctx,
                                           &ctx->morph_buffer,
                                           &ctx->morph_srv,
                                           &ctx->morph_buffer_size,
                                           cmd->morph_deltas,
                                           morph_count);
        if (FAILED(hr)) {
            d3d11_log_hresult("Create/UpdateBuffer(morphDeltas)", hr);
        } else {
            morph_upload_ok = 1;
            ctx->current_morph_srv = ctx->morph_srv;
        }
        if (object_data->has_morph_normal_deltas) {
            hr = d3d11_update_float_srv_buffer(ctx,
                                               &ctx->morph_normal_buffer,
                                               &ctx->morph_normal_srv,
                                               &ctx->morph_normal_buffer_size,
                                               cmd->morph_normal_deltas,
                                               morph_count);
            if (FAILED(hr)) {
                d3d11_log_hresult("Create/UpdateBuffer(morphNormalDeltas)", hr);
            } else {
                morph_normal_upload_ok = 1;
                ctx->current_morph_normal_srv = ctx->morph_normal_srv;
            }
        }
    }

    vgfx3d_d3d11_resolve_morph_upload_status(object_data, morph_upload_ok, morph_normal_upload_ok);
    if (object_data->morph_shape_count <= 0) {
        ctx->current_morph_srv = NULL;
        ctx->current_morph_normal_srv = NULL;
        return 0;
    }
    if (!object_data->has_morph_normal_deltas)
        ctx->current_morph_normal_srv = NULL;
    return 1;
}

/// @brief Bind the main vertex+pixel shader pipeline state for a draw.
///
/// Selects rasterizer (wireframe/cull combo), depth-stencil state
/// (alpha blending = no depth write), blend mode, primitive topology,
/// the right input layout (instanced vs. non), main shaders, all four
/// cbuffers, sampler states, and morph SRVs.
static void d3d11_bind_main_pipeline(d3d11_context_t *ctx,
                                     const vgfx3d_draw_cmd_t *cmd,
                                     int8_t wireframe,
                                     int8_t backface_cull,
                                     int instanced) {
    ID3D11RasterizerState *rasterizer;
    ID3D11ShaderResourceView *vs_srvs[2];
    float blend_factor[4] = {0, 0, 0, 0};
    vgfx3d_d3d11_blend_mode_t blend_mode = vgfx3d_d3d11_choose_blend_mode(cmd);
    UINT draw_rtv_count = ctx->current_rtv_count;

    rasterizer = d3d11_choose_rasterizer(ctx, wireframe, backface_cull);
    if (rasterizer)
        ID3D11DeviceContext_RSSetState(ctx->ctx, rasterizer);
    if (ctx->current_target_kind == VGFX3D_D3D11_TARGET_SCENE &&
        vgfx3d_d3d11_choose_motion_attachment_mode(ctx->current_target_kind, cmd) ==
            VGFX3D_D3D11_MOTION_ATTACHMENTS_COLOR_ONLY)
        draw_rtv_count = 1;
    ID3D11DeviceContext_OMSetRenderTargets(
        ctx->ctx, draw_rtv_count, ctx->current_rtvs, ctx->current_dsv);
    ID3D11DeviceContext_OMSetDepthStencilState(ctx->ctx,
                                               cmd->disable_depth_test
                                                   ? ctx->depth_state_disabled
                                                   : (blend_mode == VGFX3D_D3D11_BLEND_OPAQUE
                                                          ? ctx->depth_state
                                                          : ctx->depth_state_no_write),
                                               0);
    ID3D11DeviceContext_OMSetBlendState(ctx->ctx,
                                        blend_mode == VGFX3D_D3D11_BLEND_ALPHA
                                            ? ctx->blend_state_alpha
                                            : (blend_mode == VGFX3D_D3D11_BLEND_ADDITIVE
                                                   ? ctx->blend_state_additive
                                                   : ctx->blend_state_opaque),
                                        blend_factor,
                                        0xFFFFFFFF);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx->ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_IASetInputLayout(
        ctx->ctx, instanced ? ctx->input_layout_instanced : ctx->input_layout);
    ID3D11DeviceContext_VSSetShader(
        ctx->ctx, instanced ? ctx->vs_instanced : ctx->vs_main, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx->ctx, ctx->ps_main, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx->ctx, 0, 1, &ctx->cb_per_object);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx->ctx, 1, 1, &ctx->cb_per_scene);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx->ctx, 4, 1, &ctx->cb_bones);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx->ctx, 5, 1, &ctx->cb_prev_bones);
    ID3D11DeviceContext_PSSetConstantBuffers(ctx->ctx, 1, 1, &ctx->cb_per_scene);
    ID3D11DeviceContext_PSSetConstantBuffers(ctx->ctx, 2, 1, &ctx->cb_per_material);
    ID3D11DeviceContext_PSSetConstantBuffers(ctx->ctx, 3, 1, &ctx->cb_per_lights);
    d3d11_bind_common_state(ctx, cmd);

    vs_srvs[0] = ctx->current_morph_srv;
    vs_srvs[1] = ctx->current_morph_normal_srv;
    ID3D11DeviceContext_VSSetShaderResources(ctx->ctx, 0, 2, vs_srvs);
}

/// @brief Clear all VS / PS texture bindings (the "untether" call).
///
/// Necessary before binding the same resource as an RTV next frame —
/// D3D11 forbids a texture being bound as both SRV and RTV simultaneously.
static void d3d11_unbind_draw_resources(d3d11_context_t *ctx) {
    ID3D11ShaderResourceView *null_vs[2] = {NULL, NULL};
    ID3D11ShaderResourceView *null_ps[16] = {NULL};
    if (!ctx || !ctx->ctx)
        return;
    ID3D11DeviceContext_VSSetShaderResources(ctx->ctx, 0, 2, null_vs);
    ID3D11DeviceContext_PSSetShaderResources(ctx->ctx, 0, 16, null_ps);
}

/// @brief Pack inverse-projection + inverse-view-rotation data for the skybox shader.
static void d3d11_prepare_skybox_data(d3d11_context_t *ctx, d3d_skybox_cb_t *skybox_data) {
    float view_rotation[16];

    memcpy(view_rotation, ctx->view, sizeof(view_rotation));
    view_rotation[3] = 0.0f;
    view_rotation[7] = 0.0f;
    view_rotation[11] = 0.0f;
    view_rotation[12] = 0.0f;
    view_rotation[13] = 0.0f;
    view_rotation[14] = 0.0f;
    view_rotation[15] = 1.0f;
    if (vgfx3d_invert_matrix4(ctx->projection, skybox_data->inverse_projection) != 0)
        memcpy(skybox_data->inverse_projection,
               k_identity4x4,
               sizeof(skybox_data->inverse_projection));
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            skybox_data->inverse_view_rotation[r * 4 + c] = view_rotation[c * 4 + r];
    memcpy(skybox_data->camera_forward, ctx->cam_forward, sizeof(float) * 3);
    skybox_data->camera_forward[3] = ctx->cam_is_ortho ? 1.0f : 0.0f;
}

/// @brief Pack the post-FX cbuffer with snapshot data + camera + resolution.
///
/// Bundles every post-process toggle and parameter the shaders need:
/// bloom thresholds, tonemap mode/exposure, FXAA, color grading,
/// vignette, SSAO, depth-of-field, motion blur. NULL snapshot means
/// "post-FX disabled" (most fields stay at their zero defaults).
static void d3d11_prepare_postfx_data(d3d11_context_t *ctx,
                                      const vgfx3d_postfx_snapshot_t *snapshot,
                                      d3d_postfx_cb_t *postfx_data) {
    memset(postfx_data, 0, sizeof(*postfx_data));
    memcpy(postfx_data->inv_vp, ctx->scene_inv_vp, sizeof(postfx_data->inv_vp));
    memcpy(postfx_data->prev_vp, ctx->scene_prev_vp, sizeof(postfx_data->prev_vp));
    memcpy(postfx_data->camera_pos, ctx->scene_cam_pos, sizeof(float) * 3);
    postfx_data->camera_pos[3] = 1.0f;
    postfx_data->inv_resolution[0] = ctx->scene_width > 0 ? 1.0f / (float)ctx->scene_width : 0.0f;
    postfx_data->inv_resolution[1] = ctx->scene_height > 0 ? 1.0f / (float)ctx->scene_height : 0.0f;
    postfx_data->tonemap_exposure = 1.0f;
    postfx_data->cg_contrast = 1.0f;
    postfx_data->cg_saturation = 1.0f;
    postfx_data->vignette_radius = 1.0f;
    if (!snapshot)
        return;
    postfx_data->bloom_enabled = snapshot->bloom_enabled ? 1 : 0;
    postfx_data->bloom_threshold = snapshot->bloom_threshold;
    postfx_data->bloom_intensity = snapshot->bloom_intensity;
    postfx_data->bloom_passes = snapshot->bloom_passes;
    postfx_data->tonemap_mode = snapshot->tonemap_mode;
    postfx_data->tonemap_exposure = snapshot->tonemap_exposure;
    postfx_data->fxaa_enabled = snapshot->fxaa_enabled ? 1 : 0;
    postfx_data->color_grade_enabled = snapshot->color_grade_enabled ? 1 : 0;
    postfx_data->cg_brightness = snapshot->cg_brightness;
    postfx_data->cg_contrast = snapshot->cg_contrast;
    postfx_data->cg_saturation = snapshot->cg_saturation;
    postfx_data->vignette_enabled = snapshot->vignette_enabled ? 1 : 0;
    postfx_data->vignette_radius = snapshot->vignette_radius;
    postfx_data->vignette_softness = snapshot->vignette_softness;
    postfx_data->ssao_enabled = snapshot->ssao_enabled ? 1 : 0;
    postfx_data->ssao_radius = snapshot->ssao_radius;
    postfx_data->ssao_intensity = snapshot->ssao_intensity;
    postfx_data->ssao_samples = snapshot->ssao_samples;
    postfx_data->dof_enabled = snapshot->dof_enabled ? 1 : 0;
    postfx_data->dof_focus_distance = snapshot->dof_focus_distance;
    postfx_data->dof_aperture = snapshot->dof_aperture;
    postfx_data->dof_max_blur = snapshot->dof_max_blur;
    postfx_data->motion_blur_enabled = snapshot->motion_blur_enabled ? 1 : 0;
    postfx_data->motion_blur_intensity = snapshot->motion_blur_intensity;
    postfx_data->motion_blur_samples = snapshot->motion_blur_samples;
}

/// @brief Bind every texture / cubemap SRV the shader expects (PS slots 0..15).
///
/// Slots: 0=diffuse, 1=normal, 2=specular, 3=emissive, 4-7=shadows,
/// 8=splat-control, 9-12=splat layers, 13=env cube, 14=metallic-rough,
/// 15=AO. Unbound slots get NULL so the shader's `has*` flags can
/// gate sampling. Returns whether splat sampling was actually enabled.
static int d3d11_bind_draw_resources(d3d11_context_t *ctx,
                                     const vgfx3d_draw_cmd_t *cmd,
                                     d3d_draw_resources_t *resources) {
    ID3D11ShaderResourceView *srvs[16];

    if (!ctx || !cmd || !resources)
        return 0;

    memset(resources, 0, sizeof(*resources));
    srvs[0] = d3d11_get_or_create_material_srv(
        ctx, cmd->texture_asset, cmd->texture, &resources->textures[0]);
    srvs[1] = d3d11_get_or_create_material_srv(
        ctx, cmd->normal_map_asset, cmd->normal_map, &resources->textures[1]);
    srvs[2] = d3d11_get_or_create_material_srv(
        ctx, cmd->specular_map_asset, cmd->specular_map, &resources->textures[2]);
    srvs[3] = d3d11_get_or_create_material_srv(
        ctx, cmd->emissive_map_asset, cmd->emissive_map, &resources->textures[3]);
    for (int32_t slot = 0; slot < VGFX3D_MAX_SHADOW_LIGHTS; slot++)
        srvs[4 + slot] = ctx->shadow_count > slot ? ctx->shadow_srv[slot] : NULL;
    srvs[8] = cmd->has_splat ? d3d11_get_or_create_srv(ctx, cmd->splat_map, &resources->textures[4])
                             : NULL;
    srvs[9] = cmd->has_splat
                  ? d3d11_get_or_create_srv(ctx, cmd->splat_layers[0], &resources->textures[5])
                  : NULL;
    srvs[10] = cmd->has_splat
                  ? d3d11_get_or_create_srv(ctx, cmd->splat_layers[1], &resources->textures[6])
                  : NULL;
    srvs[11] = cmd->has_splat
                  ? d3d11_get_or_create_srv(ctx, cmd->splat_layers[2], &resources->textures[7])
                  : NULL;
    srvs[12] = cmd->has_splat
                   ? d3d11_get_or_create_srv(ctx, cmd->splat_layers[3], &resources->textures[8])
                   : NULL;
    srvs[13] = (cmd->env_map && cmd->reflectivity > 0.0001f)
                   ? d3d11_get_or_create_cubemap_srv(
                         ctx, (const rt_cubemap3d *)cmd->env_map, &resources->cubemap)
                   : NULL;
    srvs[14] = d3d11_get_or_create_material_srv(ctx,
                                                cmd->metallic_roughness_map_asset,
                                                cmd->metallic_roughness_map,
                                                &resources->textures[9]);
    srvs[15] =
        d3d11_get_or_create_material_srv(ctx, cmd->ao_map_asset, cmd->ao_map, &resources->textures[10]);

    resources->has_texture = srvs[0] != NULL;
    resources->has_normal_map = srvs[1] != NULL;
    resources->has_specular_map = srvs[2] != NULL;
    resources->has_emissive_map = srvs[3] != NULL;
    resources->has_env_map = srvs[13] != NULL;
    resources->has_metallic_roughness_map = srvs[14] != NULL;
    resources->has_ao_map = srvs[15] != NULL;
    resources->has_splat = vgfx3d_d3d11_has_complete_splat(cmd->has_splat,
                                                           srvs[8] != NULL,
                                                           srvs[9] != NULL,
                                                           srvs[10] != NULL,
                                                           srvs[11] != NULL,
                                                           srvs[12] != NULL);
    if (!resources->has_splat) {
        srvs[8] = NULL;
        srvs[9] = NULL;
        srvs[10] = NULL;
        srvs[11] = NULL;
        srvs[12] = NULL;
    }

    ID3D11DeviceContext_PSSetShaderResources(ctx->ctx, 0, 16, srvs);
    return resources->has_splat;
}

/// @brief Release any temp SRVs the per-draw bind path created.
///
/// Called at end of each draw to clean up resources that didn't fit in
/// the texture/cubemap caches. Cached resources are no-ops here.
static void d3d11_release_temporary_resources(d3d_draw_resources_t *resources) {
    if (!resources)
        return;
    for (int i = 0; i < 11; i++)
        d3d11_release_temp_srv(&resources->textures[i]);
    d3d11_release_temp_srv(&resources->cubemap);
}

/// @brief End-to-end draw for a single non-instanced mesh command.
///
/// Composes everything: pack object/scene/light/material data into
/// cbuffers, prep skinning + morph buffers, bind textures + samplers,
/// bind pipeline state, get VB/IB (cached or dynamic), set them on
/// the IA, issue `DrawIndexed`, then release any temp resources.
static void d3d11_submit_draw(void *ctx_ptr,
                              vgfx_window_t win,
                              const vgfx3d_draw_cmd_t *cmd,
                              const vgfx3d_light_params_t *lights,
                              int32_t light_count,
                              const float *ambient,
                              int8_t wireframe,
                              int8_t backface_cull) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    d3d_per_object_t object_data;
    d3d_per_scene_t scene_data;
    d3d_per_material_t material_data;
    d3d_light_t light_data[VGFX3D_MAX_LIGHTS];
    d3d_draw_resources_t draw_resources;
    int has_splat;
    HRESULT hr;
    UINT stride = sizeof(vgfx3d_vertex_t);
    UINT offset = 0;
    ID3D11Buffer *mesh_vb = NULL;
    ID3D11Buffer *mesh_ib = NULL;

    (void)win;
    if (!ctx || !cmd || !cmd->vertices || !cmd->indices || cmd->vertex_count == 0 ||
        cmd->index_count == 0)
        return;

    d3d11_prepare_object_data(cmd, &object_data);
    d3d11_prepare_scene_data(ctx, lights, light_count, ambient, &scene_data);
    d3d11_prepare_light_data(lights, scene_data.light_count, scene_data.shadow_count, light_data);
    d3d11_prepare_anim_resources(ctx, cmd, &object_data);

    hr = d3d11_update_constant_buffer(ctx, ctx->cb_per_object, &object_data, sizeof(object_data));
    if (FAILED(hr)) {
        d3d11_log_hresult("Map(cbPerObject)", hr);
        return;
    }
    hr = d3d11_update_constant_buffer(ctx, ctx->cb_per_scene, &scene_data, sizeof(scene_data));
    if (FAILED(hr)) {
        d3d11_log_hresult("Map(cbPerScene)", hr);
        return;
    }
    hr = d3d11_update_constant_buffer(ctx, ctx->cb_per_lights, light_data, sizeof(light_data));
    if (FAILED(hr)) {
        d3d11_log_hresult("Map(cbPerLights)", hr);
        return;
    }

    has_splat = d3d11_bind_draw_resources(ctx, cmd, &draw_resources);
    d3d11_prepare_material_data(cmd,
                                draw_resources.has_texture,
                                draw_resources.has_normal_map,
                                draw_resources.has_specular_map,
                                draw_resources.has_emissive_map,
                                draw_resources.has_env_map,
                                has_splat,
                                draw_resources.has_metallic_roughness_map,
                                draw_resources.has_ao_map,
                                &material_data);
    hr = d3d11_update_constant_buffer(
        ctx, ctx->cb_per_material, &material_data, sizeof(material_data));
    if (FAILED(hr)) {
        d3d11_log_hresult("Map(cbPerMaterial)", hr);
        d3d11_unbind_draw_resources(ctx);
        d3d11_release_temporary_resources(&draw_resources);
        return;
    }

    if (!d3d11_acquire_mesh_buffers(ctx, cmd, &mesh_vb, &mesh_ib)) {
        d3d11_unbind_draw_resources(ctx);
        d3d11_release_temporary_resources(&draw_resources);
        return;
    }

    ID3D11DeviceContext_IASetVertexBuffers(ctx->ctx, 0, 1, &mesh_vb, &stride, &offset);
    ID3D11DeviceContext_IASetIndexBuffer(ctx->ctx, mesh_ib, DXGI_FORMAT_R32_UINT, 0);
    d3d11_bind_main_pipeline(ctx, cmd, wireframe, backface_cull, 0);
    ID3D11DeviceContext_DrawIndexed(ctx->ctx, cmd->index_count, 0, 0);

    d3d11_unbind_draw_resources(ctx);
    d3d11_release_temporary_resources(&draw_resources);
}

/// @brief Instanced version of `d3d11_submit_draw` (one DrawIndexedInstanced).
///
/// Same flow as the non-instanced path plus: pack per-instance matrix
/// data into a CPU scratch buffer, upload to the dynamic instance
/// vertex buffer, and bind that buffer to slot 1 alongside the mesh
/// VB at slot 0. The instanced input layout reads instance attributes
/// from slot 1 with `D3D11_INPUT_PER_INSTANCE_DATA`.
static void d3d11_submit_draw_instanced(void *ctx_ptr,
                                        vgfx_window_t win,
                                        const vgfx3d_draw_cmd_t *cmd,
                                        const float *instance_matrices,
                                        int32_t instance_count,
                                        const vgfx3d_light_params_t *lights,
                                        int32_t light_count,
                                        const float *ambient,
                                        int8_t wireframe,
                                        int8_t backface_cull) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    d3d_per_object_t object_data;
    d3d_per_scene_t scene_data;
    d3d_per_material_t material_data;
    d3d_light_t light_data[VGFX3D_MAX_LIGHTS];
    d3d_draw_resources_t draw_resources;
    int has_splat;
    HRESULT hr;
    size_t instance_upload_bytes;
    UINT strides[2];
    UINT offsets[2] = {0, 0};
    ID3D11Buffer *vertex_buffers[2];
    ID3D11Buffer *mesh_vb = NULL;
    ID3D11Buffer *mesh_ib = NULL;

    (void)win;
    if (!ctx || !cmd || !instance_matrices || instance_count <= 0 || !cmd->vertices ||
        !cmd->indices)
        return;

    if (!d3d11_ensure_instance_upload_capacity(ctx, instance_count))
        return;
    vgfx3d_d3d11_fill_instance_data(ctx->instance_upload_data,
                                    instance_count,
                                    instance_matrices,
                                    cmd->prev_instance_matrices,
                                    cmd->has_prev_instance_matrices);

    d3d11_prepare_object_data(cmd, &object_data);
    object_data.has_prev_model_matrix = 0;
    object_data.has_prev_instance_matrices = cmd->has_prev_instance_matrices ? 1 : 0;
    d3d11_prepare_scene_data(ctx, lights, light_count, ambient, &scene_data);
    d3d11_prepare_light_data(lights, scene_data.light_count, scene_data.shadow_count, light_data);
    d3d11_prepare_anim_resources(ctx, cmd, &object_data);

    hr = d3d11_update_constant_buffer(ctx, ctx->cb_per_object, &object_data, sizeof(object_data));
    if (FAILED(hr)) {
        d3d11_log_hresult("Map(cbPerObject)", hr);
        return;
    }
    hr = d3d11_update_constant_buffer(ctx, ctx->cb_per_scene, &scene_data, sizeof(scene_data));
    if (FAILED(hr)) {
        d3d11_log_hresult("Map(cbPerScene)", hr);
        return;
    }
    hr = d3d11_update_constant_buffer(ctx, ctx->cb_per_lights, light_data, sizeof(light_data));
    if (FAILED(hr)) {
        d3d11_log_hresult("Map(cbPerLights)", hr);
        return;
    }

    has_splat = d3d11_bind_draw_resources(ctx, cmd, &draw_resources);
    d3d11_prepare_material_data(cmd,
                                draw_resources.has_texture,
                                draw_resources.has_normal_map,
                                draw_resources.has_specular_map,
                                draw_resources.has_emissive_map,
                                draw_resources.has_env_map,
                                has_splat,
                                draw_resources.has_metallic_roughness_map,
                                draw_resources.has_ao_map,
                                &material_data);
    hr = d3d11_update_constant_buffer(
        ctx, ctx->cb_per_material, &material_data, sizeof(material_data));
    if (FAILED(hr)) {
        d3d11_log_hresult("Map(cbPerMaterial)", hr);
        d3d11_unbind_draw_resources(ctx);
        d3d11_release_temporary_resources(&draw_resources);
        return;
    }

    if (!vgfx3d_d3d11_compute_instance_upload_bytes(
            instance_count, sizeof(d3d_instance_data_t), &instance_upload_bytes) ||
        !d3d11_acquire_mesh_buffers(ctx, cmd, &mesh_vb, &mesh_ib) ||
        !d3d11_upload_dynamic_buffer(ctx,
                                     &ctx->instance_buffer,
                                     &ctx->instance_buffer_size,
                                     D3D11_BIND_VERTEX_BUFFER,
                                     ctx->instance_upload_data,
                                     instance_upload_bytes,
                                     D3D11_INITIAL_INSTANCE_BUFFER_SIZE)) {
        d3d11_unbind_draw_resources(ctx);
        d3d11_release_temporary_resources(&draw_resources);
        return;
    }

    strides[0] = sizeof(vgfx3d_vertex_t);
    strides[1] = sizeof(d3d_instance_data_t);
    vertex_buffers[0] = mesh_vb;
    vertex_buffers[1] = ctx->instance_buffer;
    ID3D11DeviceContext_IASetVertexBuffers(ctx->ctx, 0, 2, vertex_buffers, strides, offsets);
    ID3D11DeviceContext_IASetIndexBuffer(ctx->ctx, mesh_ib, DXGI_FORMAT_R32_UINT, 0);
    d3d11_bind_main_pipeline(ctx, cmd, wireframe, backface_cull, 1);
    ID3D11DeviceContext_DrawIndexedInstanced(ctx->ctx, cmd->index_count, instance_count, 0, 0, 0);

    d3d11_unbind_draw_resources(ctx);
    d3d11_release_temporary_resources(&draw_resources);
}

/// @brief Allocate and initialize the entire D3D11 backend context.
///
/// The big one — creates the device + swap chain, compiles every
/// shader (main VS/PS, instanced VS, shadow VS, skybox VS/PS, postfx
/// VS/PS, overlay PS), creates input layouts, depth/blend/sampler
/// states, all four rasterizer-state combos, all cbuffers, and the
/// skybox cube vertex buffer. Returns NULL on any device-create
/// failure (the host falls back to the software backend in that case).
static void *d3d11_create_ctx(vgfx_window_t win, int32_t width, int32_t height) {
    d3d11_context_t *ctx = NULL;
    HWND hwnd = (HWND)vgfx_get_native_view(win);
    vgfx_framebuffer_t fb;
    DXGI_SWAP_CHAIN_DESC swap_desc;
    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
    D3D11_DEPTH_STENCIL_DESC depth_desc;
    D3D11_BLEND_DESC blend_desc;
    D3D11_SAMPLER_DESC sampler_desc;
    ID3DBlob *vs_blob = NULL;
    ID3DBlob *vs_instanced_blob = NULL;
    ID3DBlob *ps_blob = NULL;
    ID3DBlob *vs_shadow_blob = NULL;
    ID3DBlob *ps_shadow_blob = NULL;
    ID3DBlob *vs_skybox_blob = NULL;
    ID3DBlob *ps_skybox_blob = NULL;
    ID3DBlob *vs_postfx_blob = NULL;
    ID3DBlob *ps_postfx_blob = NULL;
    ID3DBlob *ps_overlay_blob = NULL;
    ID3D11Texture2D *back_buffer = NULL;
    D3D11_INPUT_ELEMENT_DESC layout[8];
    D3D11_INPUT_ELEMENT_DESC instanced_layout[20];
    D3D11_INPUT_ELEMENT_DESC skybox_layout[1];
    D3D11_BUFFER_DESC skybox_desc;
    static const float skybox_vertices[] = {
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
    D3D11_SUBRESOURCE_DATA skybox_init;
    HRESULT hr;

    if (!hwnd)
        return NULL;
    if (vgfx_get_framebuffer(win, &fb) && fb.width > 0 && fb.height > 0) {
        width = fb.width;
        height = fb.height;
    }
    if (width <= 0)
        width = 1;
    if (height <= 0)
        height = 1;
    if (!vgfx3d_d3d11_is_valid_texture2d_extent(width, height))
        return NULL;

    ctx = (d3d11_context_t *)calloc(1, sizeof(d3d11_context_t));
    if (!ctx)
        return NULL;
    ctx->width = width;
    ctx->height = height;
    memcpy(ctx->view, k_identity4x4, sizeof(ctx->view));
    memcpy(ctx->projection, k_identity4x4, sizeof(ctx->projection));
    memcpy(ctx->vp, k_identity4x4, sizeof(ctx->vp));
    memcpy(ctx->inv_vp, k_identity4x4, sizeof(ctx->inv_vp));
    memcpy(ctx->draw_prev_vp, k_identity4x4, sizeof(ctx->draw_prev_vp));
    memcpy(ctx->scene_vp, k_identity4x4, sizeof(ctx->scene_vp));
    memcpy(ctx->scene_prev_vp, k_identity4x4, sizeof(ctx->scene_prev_vp));
    memcpy(ctx->scene_inv_vp, k_identity4x4, sizeof(ctx->scene_inv_vp));
    ctx->current_target_kind = VGFX3D_D3D11_TARGET_SWAPCHAIN;
    ctx->active_target_kind = VGFX3D_D3D11_TARGET_SWAPCHAIN;
    ctx->texture_upload_budget_bytes = UINT64_MAX;

    memset(&swap_desc, 0, sizeof(swap_desc));
    swap_desc.BufferCount = 1;
    swap_desc.BufferDesc.Width = (UINT)width;
    swap_desc.BufferDesc.Height = (UINT)height;
    swap_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap_desc.BufferDesc.RefreshRate.Numerator = 60;
    swap_desc.BufferDesc.RefreshRate.Denominator = 1;
    swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_desc.OutputWindow = hwnd;
    swap_desc.SampleDesc.Count = 1;
    swap_desc.Windowed = TRUE;

    hr = D3D11CreateDeviceAndSwapChain(NULL,
                                       D3D_DRIVER_TYPE_HARDWARE,
                                       NULL,
                                       0,
                                       &feature_level,
                                       1,
                                       D3D11_SDK_VERSION,
                                       &swap_desc,
                                       &ctx->swap_chain,
                                       &ctx->device,
                                       NULL,
                                       &ctx->ctx);
    if (FAILED(hr)) {
        d3d11_log_hresult("D3D11CreateDeviceAndSwapChain", hr);
        free(ctx);
        return NULL;
    }

    hr = IDXGISwapChain_GetBuffer(ctx->swap_chain, 0, &IID_ID3D11Texture2D, (void **)&back_buffer);
    if (FAILED(hr)) {
        d3d11_log_hresult("IDXGISwapChain::GetBuffer", hr);
        goto fail;
    }
    hr = ID3D11Device_CreateRenderTargetView(
        ctx->device, (ID3D11Resource *)back_buffer, NULL, &ctx->rtv);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateRenderTargetView(backbuffer)", hr);
        goto fail;
    }
    SAFE_RELEASE(back_buffer);

    hr = d3d11_create_depth_target(ctx, width, height, 0, &ctx->depth_tex, &ctx->dsv, NULL);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateTexture2D/DepthStencilView(main)", hr);
        goto fail;
    }

    memset(&depth_desc, 0, sizeof(depth_desc));
    depth_desc.DepthEnable = TRUE;
    depth_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depth_desc.DepthFunc = D3D11_COMPARISON_LESS;
    hr = ID3D11Device_CreateDepthStencilState(ctx->device, &depth_desc, &ctx->depth_state);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateDepthStencilState(opaque)", hr);
        goto fail;
    }
    depth_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    hr = ID3D11Device_CreateDepthStencilState(ctx->device, &depth_desc, &ctx->depth_state_no_write);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateDepthStencilState(transparent)", hr);
        goto fail;
    }
    depth_desc.DepthEnable = FALSE;
    depth_desc.DepthFunc = D3D11_COMPARISON_ALWAYS;
    hr = ID3D11Device_CreateDepthStencilState(ctx->device, &depth_desc, &ctx->depth_state_disabled);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateDepthStencilState(disabled)", hr);
        goto fail;
    }
    depth_desc.DepthEnable = TRUE;
    depth_desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    hr = ID3D11Device_CreateDepthStencilState(
        ctx->device, &depth_desc, &ctx->depth_state_readonly_lequal);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateDepthStencilState(skybox)", hr);
        goto fail;
    }

    memset(&blend_desc, 0, sizeof(blend_desc));
    blend_desc.IndependentBlendEnable = TRUE;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    blend_desc.RenderTarget[1].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = ID3D11Device_CreateBlendState(ctx->device, &blend_desc, &ctx->blend_state_opaque);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateBlendState(opaque)", hr);
        goto fail;
    }
    memset(&blend_desc, 0, sizeof(blend_desc));
    blend_desc.IndependentBlendEnable = TRUE;
    blend_desc.RenderTarget[0].BlendEnable = TRUE;
    blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    blend_desc.RenderTarget[1].BlendEnable = FALSE;
    blend_desc.RenderTarget[1].RenderTargetWriteMask = 0;
    hr = ID3D11Device_CreateBlendState(ctx->device, &blend_desc, &ctx->blend_state_alpha);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateBlendState(alpha)", hr);
        goto fail;
    }
    memset(&blend_desc, 0, sizeof(blend_desc));
    blend_desc.IndependentBlendEnable = TRUE;
    blend_desc.RenderTarget[0].BlendEnable = TRUE;
    blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    blend_desc.RenderTarget[1].BlendEnable = FALSE;
    blend_desc.RenderTarget[1].RenderTargetWriteMask = 0;
    hr = ID3D11Device_CreateBlendState(ctx->device, &blend_desc, &ctx->blend_state_additive);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateBlendState(additive)", hr);
        goto fail;
    }
    memset(&blend_desc, 0, sizeof(blend_desc));
    blend_desc.IndependentBlendEnable = TRUE;
    blend_desc.RenderTarget[0].BlendEnable = TRUE;
    blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    blend_desc.RenderTarget[1].BlendEnable = FALSE;
    blend_desc.RenderTarget[1].RenderTargetWriteMask = 0;
    hr = ID3D11Device_CreateBlendState(
        ctx->device, &blend_desc, &ctx->blend_state_premultiplied_alpha);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateBlendState(premultiplied alpha)", hr);
        goto fail;
    }

    hr = d3d11_create_rasterizer_state(ctx, D3D11_FILL_SOLID, D3D11_CULL_BACK, &ctx->rs_solid_cull);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateRasterizerState(solid+cull)", hr);
        goto fail;
    }
    hr = d3d11_create_rasterizer_state(
        ctx, D3D11_FILL_SOLID, D3D11_CULL_NONE, &ctx->rs_solid_no_cull);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateRasterizerState(solid+nocull)", hr);
        goto fail;
    }
    hr = d3d11_create_rasterizer_state(
        ctx, D3D11_FILL_WIREFRAME, D3D11_CULL_BACK, &ctx->rs_wire_cull);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateRasterizerState(wire+cull)", hr);
        goto fail;
    }
    hr = d3d11_create_rasterizer_state(
        ctx, D3D11_FILL_WIREFRAME, D3D11_CULL_NONE, &ctx->rs_wire_no_cull);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateRasterizerState(wire+nocull)", hr);
        goto fail;
    }

    d3d11_init_sampler_desc_defaults(&sampler_desc);
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    hr = ID3D11Device_CreateSamplerState(ctx->device, &sampler_desc, &ctx->linear_wrap_sampler);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateSamplerState(linearWrap)", hr);
        goto fail;
    }
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    hr = ID3D11Device_CreateSamplerState(ctx->device, &sampler_desc, &ctx->linear_clamp_sampler);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateSamplerState(linearClamp)", hr);
        goto fail;
    }
    d3d11_init_sampler_desc_defaults(&sampler_desc);
    sampler_desc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    sampler_desc.BorderColor[0] = 1.0f;
    sampler_desc.BorderColor[1] = 1.0f;
    sampler_desc.BorderColor[2] = 1.0f;
    sampler_desc.BorderColor[3] = 1.0f;
    sampler_desc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    hr = ID3D11Device_CreateSamplerState(ctx->device, &sampler_desc, &ctx->shadow_cmp_sampler);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateSamplerState(shadowCmp)", hr);
        goto fail;
    }

    hr = d3d11_compile_shader(d3d11_shader_source, "VSMain", "vs_5_0", &vs_blob);
    if (FAILED(hr))
        goto fail;
    hr = d3d11_compile_shader(d3d11_shader_source, "VSMainInstanced", "vs_5_0", &vs_instanced_blob);
    if (FAILED(hr))
        goto fail;
    hr = d3d11_compile_shader(d3d11_shader_source, "PSMain", "ps_5_0", &ps_blob);
    if (FAILED(hr))
        goto fail;
    hr = d3d11_compile_shader(d3d11_shader_source, "VSShadow", "vs_5_0", &vs_shadow_blob);
    if (FAILED(hr))
        goto fail;
    hr = d3d11_compile_shader(d3d11_shader_source, "PSShadow", "ps_5_0", &ps_shadow_blob);
    if (FAILED(hr))
        goto fail;
    hr = d3d11_compile_shader(d3d11_skybox_shader_source, "VSSkybox", "vs_5_0", &vs_skybox_blob);
    if (FAILED(hr))
        goto fail;
    hr = d3d11_compile_shader(d3d11_skybox_shader_source, "PSSkybox", "ps_5_0", &ps_skybox_blob);
    if (FAILED(hr))
        goto fail;
    hr = d3d11_compile_shader(d3d11_postfx_shader_source, "VSPostFX", "vs_5_0", &vs_postfx_blob);
    if (FAILED(hr))
        goto fail;
    hr = d3d11_compile_shader(d3d11_postfx_shader_source, "PSPostFX", "ps_5_0", &ps_postfx_blob);
    if (FAILED(hr))
        goto fail;
    hr = d3d11_compile_shader(
        d3d11_postfx_shader_source, "PSOverlayComposite", "ps_5_0", &ps_overlay_blob);
    if (FAILED(hr))
        goto fail;

    hr = ID3D11Device_CreateVertexShader(ctx->device,
                                         ID3D10Blob_GetBufferPointer(vs_blob),
                                         ID3D10Blob_GetBufferSize(vs_blob),
                                         NULL,
                                         &ctx->vs_main);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateVertexShader(main)", hr);
        goto fail;
    }
    hr = ID3D11Device_CreateVertexShader(ctx->device,
                                         ID3D10Blob_GetBufferPointer(vs_instanced_blob),
                                         ID3D10Blob_GetBufferSize(vs_instanced_blob),
                                         NULL,
                                         &ctx->vs_instanced);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateVertexShader(instanced)", hr);
        goto fail;
    }
    hr = ID3D11Device_CreatePixelShader(ctx->device,
                                        ID3D10Blob_GetBufferPointer(ps_blob),
                                        ID3D10Blob_GetBufferSize(ps_blob),
                                        NULL,
                                        &ctx->ps_main);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreatePixelShader(main)", hr);
        goto fail;
    }
    hr = ID3D11Device_CreateVertexShader(ctx->device,
                                         ID3D10Blob_GetBufferPointer(vs_shadow_blob),
                                         ID3D10Blob_GetBufferSize(vs_shadow_blob),
                                         NULL,
                                         &ctx->vs_shadow);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateVertexShader(shadow)", hr);
        goto fail;
    }
    hr = ID3D11Device_CreatePixelShader(ctx->device,
                                        ID3D10Blob_GetBufferPointer(ps_shadow_blob),
                                        ID3D10Blob_GetBufferSize(ps_shadow_blob),
                                        NULL,
                                        &ctx->ps_shadow);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreatePixelShader(shadow)", hr);
        goto fail;
    }
    hr = ID3D11Device_CreateVertexShader(ctx->device,
                                         ID3D10Blob_GetBufferPointer(vs_skybox_blob),
                                         ID3D10Blob_GetBufferSize(vs_skybox_blob),
                                         NULL,
                                         &ctx->vs_skybox);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateVertexShader(skybox)", hr);
        goto fail;
    }
    hr = ID3D11Device_CreatePixelShader(ctx->device,
                                        ID3D10Blob_GetBufferPointer(ps_skybox_blob),
                                        ID3D10Blob_GetBufferSize(ps_skybox_blob),
                                        NULL,
                                        &ctx->ps_skybox);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreatePixelShader(skybox)", hr);
        goto fail;
    }
    hr = ID3D11Device_CreateVertexShader(ctx->device,
                                         ID3D10Blob_GetBufferPointer(vs_postfx_blob),
                                         ID3D10Blob_GetBufferSize(vs_postfx_blob),
                                         NULL,
                                         &ctx->vs_postfx);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateVertexShader(postfx)", hr);
        goto fail;
    }
    hr = ID3D11Device_CreatePixelShader(ctx->device,
                                        ID3D10Blob_GetBufferPointer(ps_postfx_blob),
                                        ID3D10Blob_GetBufferSize(ps_postfx_blob),
                                        NULL,
                                        &ctx->ps_postfx);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreatePixelShader(postfx)", hr);
        goto fail;
    }
    hr = ID3D11Device_CreatePixelShader(ctx->device,
                                        ID3D10Blob_GetBufferPointer(ps_overlay_blob),
                                        ID3D10Blob_GetBufferSize(ps_overlay_blob),
                                        NULL,
                                        &ctx->ps_overlay_composite);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreatePixelShader(overlayComposite)", hr);
        goto fail;
    }

    layout[0].SemanticName = "POSITION";
    layout[0].SemanticIndex = 0;
    layout[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    layout[0].InputSlot = 0;
    layout[0].AlignedByteOffset = (UINT)offsetof(vgfx3d_vertex_t, pos);
    layout[0].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
    layout[0].InstanceDataStepRate = 0;
    layout[1] = layout[0];
    layout[1].SemanticName = "NORMAL";
    layout[1].AlignedByteOffset = (UINT)offsetof(vgfx3d_vertex_t, normal);
    layout[2] = layout[0];
    layout[2].SemanticName = "TEXCOORD";
    layout[2].Format = DXGI_FORMAT_R32G32_FLOAT;
    layout[2].AlignedByteOffset = (UINT)offsetof(vgfx3d_vertex_t, uv);
    layout[3] = layout[0];
    layout[3].SemanticName = "TEXCOORD";
    layout[3].SemanticIndex = 1;
    layout[3].Format = DXGI_FORMAT_R32G32_FLOAT;
    layout[3].AlignedByteOffset = (UINT)offsetof(vgfx3d_vertex_t, uv1);
    layout[4] = layout[0];
    layout[4].SemanticName = "COLOR";
    layout[4].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    layout[4].AlignedByteOffset = (UINT)offsetof(vgfx3d_vertex_t, color);
    layout[5] = layout[0];
    layout[5].SemanticName = "TANGENT";
    layout[5].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    layout[5].AlignedByteOffset = (UINT)offsetof(vgfx3d_vertex_t, tangent);
    layout[6] = layout[0];
    layout[6].SemanticName = "BLENDINDICES";
    layout[6].Format = DXGI_FORMAT_R8G8B8A8_UINT;
    layout[6].AlignedByteOffset = (UINT)offsetof(vgfx3d_vertex_t, bone_indices);
    layout[7] = layout[0];
    layout[7].SemanticName = "BLENDWEIGHT";
    layout[7].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    layout[7].AlignedByteOffset = (UINT)offsetof(vgfx3d_vertex_t, bone_weights);
    hr = ID3D11Device_CreateInputLayout(ctx->device,
                                        layout,
                                        8,
                                        ID3D10Blob_GetBufferPointer(vs_blob),
                                        ID3D10Blob_GetBufferSize(vs_blob),
                                        &ctx->input_layout);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateInputLayout(main)", hr);
        goto fail;
    }

    memcpy(instanced_layout, layout, sizeof(layout));
    instanced_layout[0].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
    {
        int elem = 8;
        for (int row = 0; row < 4; row++, elem++) {
            instanced_layout[elem].SemanticName = "TEXCOORD";
            instanced_layout[elem].SemanticIndex = 4u + (UINT)row;
            instanced_layout[elem].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            instanced_layout[elem].InputSlot = 1;
            instanced_layout[elem].AlignedByteOffset =
                (UINT)(offsetof(d3d_instance_data_t, model) + row * 16);
            instanced_layout[elem].InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
            instanced_layout[elem].InstanceDataStepRate = 1;
        }
        for (int row = 0; row < 4; row++, elem++) {
            instanced_layout[elem].SemanticName = "TEXCOORD";
            instanced_layout[elem].SemanticIndex = 8u + (UINT)row;
            instanced_layout[elem].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            instanced_layout[elem].InputSlot = 1;
            instanced_layout[elem].AlignedByteOffset =
                (UINT)(offsetof(d3d_instance_data_t, normal) + row * 16);
            instanced_layout[elem].InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
            instanced_layout[elem].InstanceDataStepRate = 1;
        }
        for (int row = 0; row < 4; row++, elem++) {
            instanced_layout[elem].SemanticName = "TEXCOORD";
            instanced_layout[elem].SemanticIndex = 12u + (UINT)row;
            instanced_layout[elem].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            instanced_layout[elem].InputSlot = 1;
            instanced_layout[elem].AlignedByteOffset =
                (UINT)(offsetof(d3d_instance_data_t, prev_model) + row * 16);
            instanced_layout[elem].InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
            instanced_layout[elem].InstanceDataStepRate = 1;
        }
    }
    hr = ID3D11Device_CreateInputLayout(ctx->device,
                                        instanced_layout,
                                        20,
                                        ID3D10Blob_GetBufferPointer(vs_instanced_blob),
                                        ID3D10Blob_GetBufferSize(vs_instanced_blob),
                                        &ctx->input_layout_instanced);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateInputLayout(instanced)", hr);
        goto fail;
    }

    skybox_layout[0].SemanticName = "POSITION";
    skybox_layout[0].SemanticIndex = 0;
    skybox_layout[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    skybox_layout[0].InputSlot = 0;
    skybox_layout[0].AlignedByteOffset = 0;
    skybox_layout[0].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
    skybox_layout[0].InstanceDataStepRate = 0;
    hr = ID3D11Device_CreateInputLayout(ctx->device,
                                        skybox_layout,
                                        1,
                                        ID3D10Blob_GetBufferPointer(vs_skybox_blob),
                                        ID3D10Blob_GetBufferSize(vs_skybox_blob),
                                        &ctx->input_layout_skybox);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateInputLayout(skybox)", hr);
        goto fail;
    }

    hr = d3d11_create_constant_buffer(ctx, sizeof(d3d_per_object_t), &ctx->cb_per_object);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateBuffer(cbPerObject)", hr);
        goto fail;
    }
    hr = d3d11_create_constant_buffer(ctx, sizeof(d3d_per_scene_t), &ctx->cb_per_scene);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateBuffer(cbPerScene)", hr);
        goto fail;
    }
    hr = d3d11_create_constant_buffer(ctx, sizeof(d3d_per_material_t), &ctx->cb_per_material);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateBuffer(cbPerMaterial)", hr);
        goto fail;
    }
    hr = d3d11_create_constant_buffer(
        ctx, sizeof(d3d_light_t) * (uint32_t)VGFX3D_MAX_LIGHTS, &ctx->cb_per_lights);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateBuffer(cbPerLights)", hr);
        goto fail;
    }
    hr = d3d11_create_constant_buffer(ctx, VGFX3D_D3D11_BONE_PALETTE_BYTES, &ctx->cb_bones);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateBuffer(cbBones)", hr);
        goto fail;
    }
    hr = d3d11_create_constant_buffer(ctx, VGFX3D_D3D11_BONE_PALETTE_BYTES, &ctx->cb_prev_bones);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateBuffer(cbPrevBones)", hr);
        goto fail;
    }
    hr = d3d11_create_constant_buffer(ctx, sizeof(d3d_skybox_cb_t), &ctx->cb_skybox);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateBuffer(cbSkybox)", hr);
        goto fail;
    }
    hr = d3d11_create_constant_buffer(ctx, sizeof(d3d_postfx_cb_t), &ctx->cb_postfx);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateBuffer(cbPostFX)", hr);
        goto fail;
    }

    hr = d3d11_ensure_dynamic_buffer(ctx,
                                     &ctx->dynamic_vb,
                                     &ctx->dynamic_vb_size,
                                     D3D11_BIND_VERTEX_BUFFER,
                                     D3D11_INITIAL_DYNAMIC_VB_SIZE,
                                     D3D11_INITIAL_DYNAMIC_VB_SIZE);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateBuffer(dynamicVB)", hr);
        goto fail;
    }
    hr = d3d11_ensure_dynamic_buffer(ctx,
                                     &ctx->dynamic_ib,
                                     &ctx->dynamic_ib_size,
                                     D3D11_BIND_INDEX_BUFFER,
                                     D3D11_INITIAL_DYNAMIC_IB_SIZE,
                                     D3D11_INITIAL_DYNAMIC_IB_SIZE);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateBuffer(dynamicIB)", hr);
        goto fail;
    }
    hr = d3d11_ensure_dynamic_buffer(ctx,
                                     &ctx->instance_buffer,
                                     &ctx->instance_buffer_size,
                                     D3D11_BIND_VERTEX_BUFFER,
                                     D3D11_INITIAL_INSTANCE_BUFFER_SIZE,
                                     D3D11_INITIAL_INSTANCE_BUFFER_SIZE);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateBuffer(instanceBuffer)", hr);
        goto fail;
    }

    memset(&skybox_desc, 0, sizeof(skybox_desc));
    skybox_desc.Usage = D3D11_USAGE_IMMUTABLE;
    skybox_desc.ByteWidth = sizeof(skybox_vertices);
    skybox_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    memset(&skybox_init, 0, sizeof(skybox_init));
    skybox_init.pSysMem = skybox_vertices;
    hr = ID3D11Device_CreateBuffer(ctx->device, &skybox_desc, &skybox_init, &ctx->skybox_vb);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateBuffer(skyboxVB)", hr);
        goto fail;
    }

    SAFE_RELEASE(vs_blob);
    SAFE_RELEASE(vs_instanced_blob);
    SAFE_RELEASE(ps_blob);
    SAFE_RELEASE(vs_shadow_blob);
    SAFE_RELEASE(ps_shadow_blob);
    SAFE_RELEASE(vs_skybox_blob);
    SAFE_RELEASE(ps_skybox_blob);
    SAFE_RELEASE(vs_postfx_blob);
    SAFE_RELEASE(ps_postfx_blob);
    SAFE_RELEASE(ps_overlay_blob);
    return ctx;

fail:
    SAFE_RELEASE(vs_blob);
    SAFE_RELEASE(vs_instanced_blob);
    SAFE_RELEASE(ps_blob);
    SAFE_RELEASE(vs_shadow_blob);
    SAFE_RELEASE(ps_shadow_blob);
    SAFE_RELEASE(vs_skybox_blob);
    SAFE_RELEASE(ps_skybox_blob);
    SAFE_RELEASE(vs_postfx_blob);
    SAFE_RELEASE(ps_postfx_blob);
    SAFE_RELEASE(ps_overlay_blob);
    SAFE_RELEASE(back_buffer);
    d3d11_destroy_ctx(ctx);
    return NULL;
}

/// @brief Tear down the D3D11 backend context — releases everything created in `_create_ctx`.
///
/// Strict reverse-order release: caches first (so they don't reference
/// the device after it's gone), then framebuffer + offscreen targets,
/// then dynamic buffers, cbuffers, input layouts, shaders, sampler
/// states, rasterizer states, depth-stencil states, blend states,
/// finally swap chain + device. Frees the wrapper struct last.
static void d3d11_destroy_ctx(void *ctx_ptr) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    if (!ctx)
        return;

    if (ctx->ctx) {
        d3d11_unbind_draw_resources(ctx);
        d3d11_unbind_shadow_resources(ctx);
        d3d11_unbind_output_targets(ctx);
        ID3D11DeviceContext_ClearState(ctx->ctx);
        ID3D11DeviceContext_Flush(ctx->ctx);
    }

    d3d11_release_texture_cache(ctx);
    d3d11_release_cubemap_cache(ctx);
    d3d11_release_mesh_cache(ctx);
    d3d11_release_morph_cache(ctx);
    d3d11_destroy_shadow_targets(ctx);
    d3d11_destroy_rtt_targets(ctx);
    d3d11_destroy_scene_targets(ctx);
    vgfx3d_postfx_chain_free(&ctx->gpu_postfx_chain);

    SAFE_RELEASE(ctx->morph_normal_srv);
    SAFE_RELEASE(ctx->morph_normal_buffer);
    SAFE_RELEASE(ctx->morph_srv);
    SAFE_RELEASE(ctx->morph_buffer);
    SAFE_RELEASE(ctx->skybox_vb);
    SAFE_RELEASE(ctx->instance_buffer);
    SAFE_RELEASE(ctx->dynamic_ib);
    SAFE_RELEASE(ctx->dynamic_vb);
    SAFE_RELEASE(ctx->cb_postfx);
    SAFE_RELEASE(ctx->cb_skybox);
    SAFE_RELEASE(ctx->cb_prev_bones);
    SAFE_RELEASE(ctx->cb_bones);
    SAFE_RELEASE(ctx->cb_per_lights);
    SAFE_RELEASE(ctx->cb_per_material);
    SAFE_RELEASE(ctx->cb_per_scene);
    SAFE_RELEASE(ctx->cb_per_object);
    SAFE_RELEASE(ctx->input_layout_skybox);
    SAFE_RELEASE(ctx->input_layout_instanced);
    SAFE_RELEASE(ctx->input_layout);
    SAFE_RELEASE(ctx->ps_postfx);
    SAFE_RELEASE(ctx->ps_overlay_composite);
    SAFE_RELEASE(ctx->vs_postfx);
    SAFE_RELEASE(ctx->ps_skybox);
    SAFE_RELEASE(ctx->vs_skybox);
    SAFE_RELEASE(ctx->ps_shadow);
    SAFE_RELEASE(ctx->vs_shadow);
    SAFE_RELEASE(ctx->ps_main);
    SAFE_RELEASE(ctx->vs_instanced);
    SAFE_RELEASE(ctx->vs_main);
    for (int ws = 0; ws < 3; ws++) {
        for (int wt = 0; wt < 3; wt++) {
            for (int filter = 0; filter < 2; filter++)
                SAFE_RELEASE(ctx->material_samplers[ws][wt][filter]);
        }
    }
    SAFE_RELEASE(ctx->shadow_cmp_sampler);
    SAFE_RELEASE(ctx->linear_clamp_sampler);
    SAFE_RELEASE(ctx->linear_wrap_sampler);
    SAFE_RELEASE(ctx->rs_wire_no_cull);
    SAFE_RELEASE(ctx->rs_wire_cull);
    SAFE_RELEASE(ctx->rs_solid_no_cull);
    SAFE_RELEASE(ctx->rs_solid_cull);
    SAFE_RELEASE(ctx->depth_state_readonly_lequal);
    SAFE_RELEASE(ctx->depth_state_disabled);
    SAFE_RELEASE(ctx->depth_state_no_write);
    SAFE_RELEASE(ctx->depth_state);
    SAFE_RELEASE(ctx->blend_state_premultiplied_alpha);
    SAFE_RELEASE(ctx->blend_state_additive);
    SAFE_RELEASE(ctx->blend_state_alpha);
    SAFE_RELEASE(ctx->blend_state_opaque);
    SAFE_RELEASE(ctx->dsv);
    SAFE_RELEASE(ctx->depth_tex);
    SAFE_RELEASE(ctx->rtv);
    SAFE_RELEASE(ctx->swap_chain);
    SAFE_RELEASE(ctx->ctx);
    SAFE_RELEASE(ctx->device);
    free(ctx->instance_upload_data);
    free(ctx);
}

/// @brief Backend `clear` op — stores the clear color for the next BeginFrame.
///
/// We don't actually clear the targets here; that happens in
/// `BeginFrame`'s `clear_current_targets` call so we know which
/// target to clear (depending on RTT/scene/swapchain selection).
static void d3d11_clear(void *ctx_ptr, vgfx_window_t win, float r, float g, float b) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    (void)win;
    if (!ctx)
        return;
    ctx->clear_r = r;
    ctx->clear_g = g;
    ctx->clear_b = b;
}

/// @brief Backend `begin_frame` — set up matrices, target selection, and clear.
///
/// Increments the frame serial (used by mesh-cache LRU), recomputes
/// view-projection (and its inverse), captures fog/camera state,
/// chooses the active target kind based on RTT/postfx/overlay flags,
/// updates the frame-history (prev VP for motion blur), and calls
/// `select_current_targets` + `clear_current_targets` to set the
/// pipeline up for the upcoming draws.
static void d3d11_begin_frame(void *ctx_ptr, const vgfx3d_camera_params_t *cam) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    HRESULT hr = S_OK;

    if (!ctx || !cam)
        return;

    ctx->frame_serial++;
    ctx->presented_color_valid = 0;
    ctx->shadow_pass_slot = -1;

    memcpy(ctx->view, cam->view, sizeof(ctx->view));
    memcpy(ctx->projection, cam->projection, sizeof(ctx->projection));
    mat4f_mul_d3d(cam->projection, cam->view, ctx->vp);
    if (vgfx3d_invert_matrix4(ctx->vp, ctx->inv_vp) != 0)
        memcpy(ctx->inv_vp, k_identity4x4, sizeof(ctx->inv_vp));
    memcpy(ctx->cam_pos, cam->position, sizeof(float) * 3);
    memcpy(ctx->cam_forward, cam->forward, sizeof(float) * 3);
    ctx->cam_is_ortho = cam->is_ortho ? 1 : 0;
    ctx->fog_enabled = cam->fog_enabled;
    ctx->fog_near = cam->fog_near;
    ctx->fog_far = cam->fog_far;
    memcpy(ctx->fog_color, cam->fog_color, sizeof(ctx->fog_color));
    if (vgfx3d_d3d11_should_reset_composited_swapchain_for_frame(ctx->rtt_active,
                                                                 cam->load_existing_color))
        ctx->scene_composited_to_swapchain = 0;
    ctx->active_target_kind = vgfx3d_d3d11_choose_target_kind(
        ctx->rtt_active, ctx->gpu_postfx_enabled, cam->load_existing_color);
    if (!ctx->rtt_active && cam->load_existing_color && ctx->scene_composited_to_swapchain)
        ctx->active_target_kind = VGFX3D_D3D11_TARGET_SWAPCHAIN;

    if ((!ctx->rtv || !ctx->depth_tex || !ctx->dsv) &&
        vgfx3d_d3d11_is_valid_texture2d_extent(ctx->width, ctx->height)) {
        d3d11_release_swapchain_main_targets(ctx);
        hr = d3d11_recreate_swapchain_main_targets(ctx,
                                                   ctx->width,
                                                   ctx->height,
                                                   "Recreate missing swapchain targets");
        if (FAILED(hr))
            d3d11_log_hresult("Recreate missing swapchain targets", hr);
    }

    if (!ctx->rtt_active && ctx->active_target_kind != VGFX3D_D3D11_TARGET_SWAPCHAIN) {
        hr = d3d11_ensure_scene_targets(ctx, ctx->width, ctx->height);
        if (FAILED(hr)) {
            d3d11_log_hresult("CreateSceneTargets", hr);
            ctx->active_target_kind = VGFX3D_D3D11_TARGET_SWAPCHAIN;
        }
    }
    if (ctx->active_target_kind == VGFX3D_D3D11_TARGET_OVERLAY) {
        hr = d3d11_ensure_overlay_target(ctx, ctx->width, ctx->height);
        if (FAILED(hr))
            d3d11_log_hresult("CreateOverlayTarget", hr);
    }
    d3d11_refresh_pass_flags(ctx, cam);

    if (!ctx->current_pass_is_overlay) {
        ctx->texture_upload_bytes = 0;
        ctx->shadow_count = 0;
        ctx->overlay_used_this_frame = 0;
    }

    {
        vgfx3d_d3d11_frame_history_t history;

        memcpy(history.scene_vp, ctx->scene_vp, sizeof(history.scene_vp));
        memcpy(history.scene_prev_vp, ctx->scene_prev_vp, sizeof(history.scene_prev_vp));
        memcpy(history.scene_inv_vp, ctx->scene_inv_vp, sizeof(history.scene_inv_vp));
        memcpy(history.draw_prev_vp, ctx->draw_prev_vp, sizeof(history.draw_prev_vp));
        memcpy(history.scene_cam_pos, ctx->scene_cam_pos, sizeof(history.scene_cam_pos));
        history.scene_history_valid = ctx->scene_history_valid;
        history.overlay_used_this_frame = ctx->overlay_used_this_frame;
        vgfx3d_d3d11_update_frame_history(
            &history,
            ctx->vp,
            ctx->inv_vp,
            cam->position,
            ctx->current_pass_is_overlay,
            (int8_t)vgfx3d_d3d11_uses_separate_overlay_target(ctx->active_target_kind,
                                                              d3d11_has_overlay_target(ctx)));
        memcpy(ctx->scene_vp, history.scene_vp, sizeof(ctx->scene_vp));
        memcpy(ctx->scene_prev_vp, history.scene_prev_vp, sizeof(ctx->scene_prev_vp));
        memcpy(ctx->scene_inv_vp, history.scene_inv_vp, sizeof(ctx->scene_inv_vp));
        memcpy(ctx->draw_prev_vp, history.draw_prev_vp, sizeof(ctx->draw_prev_vp));
        memcpy(ctx->scene_cam_pos, history.scene_cam_pos, sizeof(ctx->scene_cam_pos));
        ctx->scene_history_valid = history.scene_history_valid;
        ctx->overlay_used_this_frame = history.overlay_used_this_frame;
    }

    d3d11_select_current_targets(ctx);
    d3d11_clear_current_targets(ctx, ctx->current_load_existing_color, cam->load_existing_depth);
    d3d11_bind_common_state(ctx, NULL);
    if ((ctx->frame_serial & 31u) == 0u) {
        d3d11_prune_texture_cache(ctx);
        d3d11_prune_cubemap_cache(ctx);
    }
}

/// @brief Backend `end_frame` — mark RTT pixels dirty for lazy host readback.
///
/// CPU readback is deferred until a caller explicitly asks for pixels.
/// This keeps render-to-texture frames on the GPU fast instead of forcing
/// a staging copy at the end of every RTT frame.
static void d3d11_end_frame(void *ctx_ptr) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;

    if (!ctx)
        return;
    if (!vgfx3d_d3d11_should_mark_rtt_dirty(ctx->rtt_active,
                                            ctx->rtt_target != NULL,
                                            ctx->rtt_color_tex != NULL,
                                            ctx->rtt_rtv != NULL,
                                            ctx->rtt_depth_tex != NULL,
                                            ctx->rtt_dsv != NULL,
                                            ctx->rtt_staging != NULL))
        return;
    ctx->rtt_target->sync_color = d3d11_sync_render_target_color;
    ctx->rtt_target->sync_color_userdata = ctx;
    ctx->rtt_target->color_dirty = 1;
    ctx->rtt_target->hdr_color_valid = 0;
}

/// @brief Read the total bytes uploaded to D3D11 textures so far this frame (diagnostics counter).
static uint64_t d3d11_get_texture_upload_bytes(void *ctx_ptr) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    return ctx ? ctx->texture_upload_bytes : 0;
}

/// @brief Release swapchain-owned color/depth targets and clear stale CPU binding mirrors.
static void d3d11_release_swapchain_main_targets(d3d11_context_t *ctx) {
    if (!ctx)
        return;
    d3d11_unbind_output_targets(ctx);
    if (ctx->current_target_kind == VGFX3D_D3D11_TARGET_SWAPCHAIN)
        d3d11_clear_current_target_bindings(ctx);
    SAFE_RELEASE(ctx->dsv);
    SAFE_RELEASE(ctx->depth_tex);
    SAFE_RELEASE(ctx->rtv);
}

/// @brief Rebuild the swapchain-derived main render targets (back-buffer RTV, scene/depth
///   textures) at @p width × @p height after a resize or device reset; @p log_context
///   tags any failure log. Returns the first failing HRESULT, or S_OK on success.
static HRESULT d3d11_recreate_swapchain_main_targets(d3d11_context_t *ctx,
                                                     int32_t width,
                                                     int32_t height,
                                                     const char *log_context) {
    ID3D11Texture2D *back_buffer = NULL;
    HRESULT hr;

    if (!ctx || !vgfx3d_d3d11_is_valid_texture2d_extent(width, height))
        return E_INVALIDARG;

    hr = IDXGISwapChain_GetBuffer(ctx->swap_chain, 0, &IID_ID3D11Texture2D, (void **)&back_buffer);
    if (FAILED(hr)) {
        d3d11_log_hresult(log_context ? log_context : "IDXGISwapChain::GetBuffer", hr);
        return hr;
    }
    hr = ID3D11Device_CreateRenderTargetView(
        ctx->device, (ID3D11Resource *)back_buffer, NULL, &ctx->rtv);
    SAFE_RELEASE(back_buffer);
    if (FAILED(hr)) {
        d3d11_log_hresult(log_context ? log_context : "CreateRenderTargetView(backbuffer)", hr);
        return hr;
    }
    hr = d3d11_create_depth_target(ctx, width, height, 0, &ctx->depth_tex, &ctx->dsv, NULL);
    if (FAILED(hr)) {
        d3d11_log_hresult(log_context ? log_context : "CreateTexture2D/DepthStencilView(main)", hr);
        SAFE_RELEASE(ctx->rtv);
        return hr;
    }
    return S_OK;
}

/// @brief Backend `resize` — handle window-size changes.
///
/// Unbinds targets, releases the backbuffer RTV + main depth, calls
/// `IDXGISwapChain::ResizeBuffers`, gets the new backbuffer, recreates
/// RTV + depth target. Scene-targets are also destroyed; they get
/// recreated in the next `BeginFrame` at the new size. No-op if the
/// dimensions match.
static void d3d11_resize(void *ctx_ptr, int32_t w, int32_t h) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    int32_t old_w;
    int32_t old_h;
    HRESULT hr;

    if (!ctx || !vgfx3d_d3d11_is_valid_texture2d_extent(w, h))
        return;
    if (ctx->width == w && ctx->height == h)
        return;
    old_w = ctx->width;
    old_h = ctx->height;

    d3d11_unbind_draw_resources(ctx);
    d3d11_destroy_scene_targets(ctx);
    d3d11_release_swapchain_main_targets(ctx);
    d3d11_clear_current_target_bindings(ctx);

    hr = IDXGISwapChain_ResizeBuffers(ctx->swap_chain, 0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        d3d11_log_hresult("IDXGISwapChain::ResizeBuffers", hr);
        if (SUCCEEDED(d3d11_recreate_swapchain_main_targets(
                ctx, old_w, old_h, "Recreate swapchain targets after failed resize"))) {
            d3d11_bind_swapchain_target(ctx);
        }
        return;
    }

    ctx->width = w;
    ctx->height = h;
    d3d11_reset_temporal_scene_state(ctx);
    hr =
        d3d11_recreate_swapchain_main_targets(ctx, w, h, "Recreate swapchain targets after resize");
    if (FAILED(hr))
        return;
}

/// @brief Copy a mapped texture's rows to an RGBA8 destination, format-aware.
///
/// Direct memcpy for R8G8B8A8_UNORM. For R16G16B16A16_FLOAT, decodes
/// each half to float then quantizes to 8-bit. Other formats are
/// silently skipped.
static int d3d11_copy_mapped_texture_to_rgba8(uint8_t *dst_rgba,
                                              int32_t dst_stride,
                                              int32_t copy_w,
                                              int32_t copy_h,
                                              const D3D11_MAPPED_SUBRESOURCE *mapped,
                                              DXGI_FORMAT format) {
    if (!dst_rgba || !mapped || copy_w <= 0 || copy_h <= 0)
        return 0;

    for (int32_t y = 0; y < copy_h; y++) {
        uint8_t *dst_row = dst_rgba + (size_t)y * (size_t)dst_stride;
        const uint8_t *src_row = (const uint8_t *)mapped->pData + (size_t)y * mapped->RowPitch;
        size_t min_src_row_bytes;

        if (format == DXGI_FORMAT_R8G8B8A8_UNORM) {
            if (!vgfx3d_d3d11_compute_row_bytes(copy_w, 4, &min_src_row_bytes) ||
                (size_t)dst_stride < min_src_row_bytes || mapped->RowPitch < min_src_row_bytes)
                return 0;
            memcpy(dst_row, src_row, (size_t)copy_w * 4u);
            continue;
        }
        if (format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
            size_t dst_row_bytes;
            if (!vgfx3d_d3d11_compute_row_bytes(copy_w, 8, &min_src_row_bytes) ||
                !vgfx3d_d3d11_compute_row_bytes(copy_w, 4, &dst_row_bytes) ||
                (size_t)dst_stride < dst_row_bytes || mapped->RowPitch < min_src_row_bytes)
                return 0;
            vgfx3d_copy_linear_rgba16f_to_rgba8(
                dst_row, dst_stride, copy_w, 1, (const uint16_t *)src_row, copy_w * 8);
            continue;
        }
        return 0;
    }
    return 1;
}

/// @brief Read back a Texture2D into a host RGBA8 buffer.
///
/// Allocates a transient staging texture matching the source's format
/// + size, CopyResource into it, Map(READ), copy rows. Caller's
/// destination doesn't have to match the source size — clipping is
/// applied. Returns 0 on any failure.
static int d3d11_readback_texture_rgba(d3d11_context_t *ctx,
                                       ID3D11Texture2D *source_tex,
                                       uint8_t *dst_rgba,
                                       int32_t w,
                                       int32_t h,
                                       int32_t stride) {
    ID3D11Texture2D *staging = NULL;
    D3D11_TEXTURE2D_DESC desc;
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr;
    int32_t copy_w;
    int32_t copy_h;
    size_t clear_bytes;
    int restore_target = 0;
    int mapped_ok = 0;
    int ok = 0;

    if (!ctx || !source_tex || !dst_rgba ||
        !vgfx3d_d3d11_validate_rgba8_destination(w, h, stride, &clear_bytes))
        return 0;

    ID3D11Texture2D_GetDesc(source_tex, &desc);
    if (desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM &&
        desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT) {
        return 0;
    }
    hr = d3d11_create_staging_texture(
        ctx, (int32_t)desc.Width, (int32_t)desc.Height, desc.Format, &staging);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateTexture2D(sceneStaging)", hr);
        return 0;
    }

    memset(dst_rgba, 0, clear_bytes);
    restore_target = (ctx->current_rtv_count > 0 || ctx->current_dsv) ? 1 : 0;
    d3d11_unbind_draw_resources(ctx);
    d3d11_unbind_output_targets(ctx);
    ID3D11DeviceContext_CopyResource(
        ctx->ctx, (ID3D11Resource *)staging, (ID3D11Resource *)source_tex);
    hr =
        ID3D11DeviceContext_Map(ctx->ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        d3d11_log_hresult("Map(sceneStaging)", hr);
        goto cleanup;
    }
    if (!mapped.pData)
        goto cleanup;
    mapped_ok = 1;

    copy_w = (int32_t)desc.Width < w ? (int32_t)desc.Width : w;
    copy_h = (int32_t)desc.Height < h ? (int32_t)desc.Height : h;
    ok = d3d11_copy_mapped_texture_to_rgba8(dst_rgba, stride, copy_w, copy_h, &mapped, desc.Format);

cleanup:
    if (mapped_ok)
        ID3D11DeviceContext_Unmap(ctx->ctx, (ID3D11Resource *)staging, 0);
    SAFE_RELEASE(staging);
    if (restore_target)
        d3d11_restore_current_target_bindings(ctx);
    return ok;
}

/// @brief Point the D3D11 output-merger at one postfx render target and set its viewport.
/// @details Each postfx pass (bloom extract, blur, tonemap) draws a full-screen triangle
///   into a different RTV. This helper batches the OMSetRenderTargets + RSSetViewports
///   calls so each pass is one line, not three, and so the viewport tracks the RTV size
///   automatically (missing that is a classic source of postfx-chain letterboxing bugs).
static void d3d11_bind_postfx_target(d3d11_context_t *ctx,
                                     ID3D11RenderTargetView *rtv,
                                     int32_t width,
                                     int32_t height) {
    D3D11_VIEWPORT viewport;

    if (!ctx || !rtv || width <= 0 || height <= 0)
        return;
    memset(&viewport, 0, sizeof(viewport));
    viewport.Width = (FLOAT)width;
    viewport.Height = (FLOAT)height;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    d3d11_unbind_postfx_resources(ctx);
    ID3D11DeviceContext_OMSetRenderTargets(ctx->ctx, 1, &rtv, NULL);
    ID3D11DeviceContext_RSSetViewports(ctx->ctx, 1, &viewport);
}

/// @brief Execute one post-FX pass: upload the effect's constant data and issue a
///        fullscreen-triangle draw from @p source_color_srv into @p dest_rtv.
/// @details The pass uses `vs_postfx` (a no-input-layout shader that generates a
///   clip-space fullscreen triangle from `SV_VertexID`) together with `ps_postfx`
///   which samples four PS SRV slots:
///     - slot 0: @p source_color_srv — the colour result of the previous pass
///     - slot 1: `ctx->scene_depth_srv` — linear depth for depth-aware effects
///     - slot 2: `ctx->scene_motion_srv` — screen-space motion vectors (TAA / blur)
///     - slot 3: unused (NULL)
///   The postfx constant buffer is updated from @p snapshot before the draw so
///   per-effect uniforms (exposure, bloom threshold, vignette, etc.) are current.
///   Depth testing and stencil are disabled; the opaque blend state is used because
///   the post-FX shaders output the final composited colour directly. SRVs are
///   unbound after the draw to avoid hazards on the next write to the scene targets.
/// @param ctx               Active D3D11 backend context.
/// @param source_color_srv  The colour texture from the previous pass or the scene.
/// @param dest_rtv          The render target to write this pass's output into.
/// @param width             Viewport width in pixels (must match RTV dimensions).
/// @param height            Viewport height in pixels.
/// @param snapshot          Per-effect uniform data written into the postfx cbuffer.
/// @return 1 on success, 0 if any parameter is invalid or the cbuffer update failed.
static int d3d11_draw_postfx_pass(d3d11_context_t *ctx,
                                  ID3D11ShaderResourceView *source_color_srv,
                                  ID3D11RenderTargetView *dest_rtv,
                                  int32_t width,
                                  int32_t height,
                                  const vgfx3d_postfx_snapshot_t *snapshot) {
    d3d_postfx_cb_t postfx_data;
    ID3D11ShaderResourceView *srvs[4];
    ID3D11ShaderResourceView *null_srvs[4] = {NULL, NULL, NULL, NULL};
    float blend_factor[4] = {0, 0, 0, 0};
    HRESULT hr;

    if (!ctx || !source_color_srv || !dest_rtv || width <= 0 || height <= 0)
        return 0;

    d3d11_prepare_postfx_data(ctx, snapshot, &postfx_data);
    hr = d3d11_update_constant_buffer(ctx, ctx->cb_postfx, &postfx_data, sizeof(postfx_data));
    if (FAILED(hr)) {
        d3d11_log_hresult("Map(cbPostFX)", hr);
        return 0;
    }

    d3d11_bind_postfx_target(ctx, dest_rtv, width, height);
    srvs[0] = source_color_srv;
    srvs[1] = ctx->scene_depth_srv;
    srvs[2] = ctx->scene_motion_srv;
    srvs[3] = NULL;
    ID3D11DeviceContext_IASetInputLayout(ctx->ctx, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx->ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(ctx->ctx, ctx->vs_postfx, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx->ctx, ctx->ps_postfx, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx->ctx, 0, 1, &ctx->cb_postfx);
    ID3D11DeviceContext_PSSetConstantBuffers(ctx->ctx, 0, 1, &ctx->cb_postfx);
    if (ctx->linear_clamp_sampler)
        ID3D11DeviceContext_PSSetSamplers(ctx->ctx, 0, 1, &ctx->linear_clamp_sampler);
    ID3D11DeviceContext_PSSetShaderResources(ctx->ctx, 0, 4, srvs);
    ID3D11DeviceContext_RSSetState(ctx->ctx, ctx->rs_solid_no_cull);
    ID3D11DeviceContext_OMSetDepthStencilState(ctx->ctx, NULL, 0);
    ID3D11DeviceContext_OMSetBlendState(
        ctx->ctx, ctx->blend_state_opaque, blend_factor, 0xFFFFFFFF);
    ID3D11DeviceContext_Draw(ctx->ctx, 3, 0);
    ID3D11DeviceContext_PSSetShaderResources(ctx->ctx, 0, 4, null_srvs);
    return 1;
}

/// @brief Composite the 2D GUI overlay texture on top of the 3D scene using alpha blending.
/// @details After all post-FX passes have resolved the scene colour, the 2D overlay
///   (rendered earlier into `ctx->overlay_color_srv`) is blended over @p dest_rtv
///   using `ps_overlay_composite` and a premultiplied-alpha blend state. The overlay SRV is bound
///   at PS slot 3 (matching the shader's expected binding), and the same
///   no-input-layout fullscreen-triangle technique used by `d3d11_draw_postfx_pass`
///   is re-used here for consistency. The function is a no-op (returns 1) when no
///   overlay was produced this frame (`ctx->overlay_used_this_frame == 0`) or when
///   the overlay resources or composite shader are absent, so callers can always
///   call it unconditionally without checking state themselves.
/// @param ctx        Active D3D11 backend context.
/// @param dest_rtv   Render target to composite the overlay into (final output or
///                   intermediate offscreen buffer when `force_offscreen_final` is set).
/// @param width      Viewport width in pixels.
/// @param height     Viewport height in pixels.
/// @return Always 1; kept as int for symmetry with `d3d11_draw_postfx_pass`.
static int d3d11_draw_overlay_composite(d3d11_context_t *ctx,
                                        ID3D11RenderTargetView *dest_rtv,
                                        int32_t width,
                                        int32_t height) {
    ID3D11ShaderResourceView *overlay_srv;
    ID3D11BlendState *blend_state;
    ID3D11ShaderResourceView *null_srvs[4] = {NULL, NULL, NULL, NULL};
    float blend_factor[4] = {0, 0, 0, 0};

    if (!ctx || !dest_rtv || width <= 0 || height <= 0 || !ctx->overlay_used_this_frame ||
        !ctx->overlay_color_srv || !ctx->ps_overlay_composite) {
        return 1;
    }

    overlay_srv = ctx->overlay_color_srv;
    blend_state = ctx->blend_state_premultiplied_alpha ? ctx->blend_state_premultiplied_alpha
                                                       : ctx->blend_state_alpha;
    d3d11_bind_postfx_target(ctx, dest_rtv, width, height);
    ID3D11DeviceContext_IASetInputLayout(ctx->ctx, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx->ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(ctx->ctx, ctx->vs_postfx, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx->ctx, ctx->ps_overlay_composite, NULL, 0);
    if (ctx->linear_clamp_sampler)
        ID3D11DeviceContext_PSSetSamplers(ctx->ctx, 0, 1, &ctx->linear_clamp_sampler);
    ID3D11DeviceContext_PSSetShaderResources(ctx->ctx, 3, 1, &overlay_srv);
    ID3D11DeviceContext_RSSetState(ctx->ctx, ctx->rs_solid_no_cull);
    ID3D11DeviceContext_OMSetDepthStencilState(ctx->ctx, NULL, 0);
    ID3D11DeviceContext_OMSetBlendState(ctx->ctx, blend_state, blend_factor, 0xFFFFFFFF);
    ID3D11DeviceContext_Draw(ctx->ctx, 3, 0);
    ID3D11DeviceContext_PSSetShaderResources(ctx->ctx, 0, 4, null_srvs);
    return 1;
}

/// @brief Resolve the offscreen scene color to a final target without applying user effects.
static int d3d11_resolve_scene_to_target(d3d11_context_t *ctx,
                                         ID3D11RenderTargetView *final_rtv,
                                         int32_t final_width,
                                         int32_t final_height,
                                         int force_offscreen_final,
                                         ID3D11Texture2D **out_result_tex) {
    ID3D11RenderTargetView *dest_rtv;
    int32_t width;
    int32_t height;

    if (out_result_tex)
        *out_result_tex = NULL;
    if (!ctx || !ctx->scene_color_srv || !ctx->scene_color_tex || !ctx->scene_depth_srv ||
        !ctx->scene_motion_srv)
        return 0;
    if (force_offscreen_final) {
        if (FAILED(d3d11_ensure_postfx_target(ctx, ctx->scene_width, ctx->scene_height)))
            return 0;
        dest_rtv = ctx->postfx_color_rtv;
        width = ctx->scene_width;
        height = ctx->scene_height;
    } else {
        dest_rtv = final_rtv;
        width = final_width;
        height = final_height;
    }
    if (!dest_rtv || width <= 0 || height <= 0)
        return 0;
    if (!d3d11_draw_postfx_pass(ctx, ctx->scene_color_srv, dest_rtv, width, height, NULL))
        return 0;
    if (!d3d11_draw_overlay_composite(ctx, dest_rtv, width, height))
        return 0;
    if (out_result_tex)
        *out_result_tex = force_offscreen_final ? ctx->postfx_color_tex : NULL;
    return 1;
}

/// @brief Apply a full chain of post-FX effects to the rendered scene using ping-pong buffers.
/// @details Iterates `chain->effects[0..effect_count-1]`, drawing each via
///   `d3d11_draw_postfx_pass`. Passes alternate between `scene_color_srv/rtv` and
///   `postfx_color_srv/rtv` and `postfx_scratch_srv/rtv` as intermediate destinations so
///   no pass reads and writes the same texture simultaneously and screenshot readback does
///   not overwrite the live scene color. For the final
///   pass, unless @p force_offscreen_final is set, the output goes directly into
///   @p final_rtv (usually the swapchain back buffer) at @p final_width × @p
///   final_height so no extra blit is needed.
///
///   When @p force_offscreen_final is non-zero (used by the screenshot readback path)
///   the final pass also writes to an offscreen scene texture and the result texture
///   pointer is returned via @p out_result_tex so the caller can stage-copy it.
///
///   After all post-FX passes, `d3d11_draw_overlay_composite` is called once to
///   alpha-blend the 2D GUI overlay over the result regardless of effect count.
///
///   Returns 0 early if any of the required scene resources (scene_color_srv,
///   scene_depth_srv, scene_motion_srv) are absent, or if any individual pass fails.
/// @param ctx                 Active D3D11 backend context.
/// @param chain               Effect chain descriptor (enabled flag + effects array).
/// @param final_rtv           Swapchain (or caller-supplied) render target for the last pass.
/// @param final_width         Width of @p final_rtv in pixels.
/// @param final_height        Height of @p final_rtv in pixels.
/// @param force_offscreen_final If non-zero, the last pass writes to an offscreen texture
///                             instead of @p final_rtv; the texture is returned via
///                             @p out_result_tex.
/// @param out_result_tex      Receives the offscreen result texture when
///                            @p force_offscreen_final is set; set to NULL otherwise.
/// @return 1 on success, 0 if resources are missing or any pass fails.
static int d3d11_apply_postfx_chain(d3d11_context_t *ctx,
                                    const vgfx3d_postfx_chain_t *chain,
                                    ID3D11RenderTargetView *final_rtv,
                                    int32_t final_width,
                                    int32_t final_height,
                                    int force_offscreen_final,
                                    ID3D11Texture2D **out_result_tex) {
    ID3D11ShaderResourceView *source_srv;
    ID3D11RenderTargetView *dest_rtv;
    ID3D11Texture2D *result_tex = NULL;
    int need_postfx_target;
    int need_scratch_target;
    int32_t width;
    int32_t height;

    if (out_result_tex)
        *out_result_tex = NULL;
    if (!ctx || !chain || !chain->enabled || chain->effect_count <= 0 || !chain->effects ||
        !ctx->scene_color_srv || !ctx->scene_color_rtv || !ctx->scene_color_tex ||
        !ctx->scene_depth_srv || !ctx->scene_motion_srv) {
        return 0;
    }

    need_postfx_target = force_offscreen_final || chain->effect_count > 1;
    need_scratch_target = (force_offscreen_final && chain->effect_count > 1) ||
                          (!force_offscreen_final && chain->effect_count > 2);
    if (need_postfx_target &&
        FAILED(d3d11_ensure_postfx_target(ctx, ctx->scene_width, ctx->scene_height))) {
        return 0;
    }
    if (need_scratch_target &&
        FAILED(d3d11_ensure_postfx_scratch_target(ctx, ctx->scene_width, ctx->scene_height))) {
        return 0;
    }

    source_srv = ctx->scene_color_srv;
    result_tex = ctx->scene_color_tex;
    for (int32_t i = 0; i < chain->effect_count; i++) {
        const int is_last = (i == chain->effect_count - 1) ? 1 : 0;
        if (is_last && !force_offscreen_final) {
            dest_rtv = final_rtv;
            width = final_width;
            height = final_height;
            result_tex = NULL;
        } else if (source_srv == ctx->postfx_color_srv) {
            dest_rtv = ctx->postfx_scratch_rtv;
            width = ctx->scene_width;
            height = ctx->scene_height;
            result_tex = ctx->postfx_scratch_tex;
        } else {
            dest_rtv = ctx->postfx_color_rtv;
            width = ctx->scene_width;
            height = ctx->scene_height;
            result_tex = ctx->postfx_color_tex;
        }
        if (!dest_rtv || width <= 0 || height <= 0)
            return 0;
        if (!d3d11_draw_postfx_pass(
                ctx, source_srv, dest_rtv, width, height, &chain->effects[i].snapshot))
            return 0;
        if (!is_last || force_offscreen_final) {
            source_srv = result_tex == ctx->postfx_scratch_tex ? ctx->postfx_scratch_srv
                                                               : ctx->postfx_color_srv;
        }
    }

    if (force_offscreen_final) {
        dest_rtv = result_tex == ctx->postfx_scratch_tex ? ctx->postfx_scratch_rtv
                                                         : ctx->postfx_color_rtv;
        width = ctx->scene_width;
        height = ctx->scene_height;
        if (!d3d11_draw_overlay_composite(ctx, dest_rtv, width, height))
            return 0;
        if (out_result_tex)
            *out_result_tex = result_tex;
        return result_tex != NULL ? 1 : 0;
    }

    if (!d3d11_draw_overlay_composite(ctx, final_rtv, final_width, final_height))
        return 0;
    return 1;
}

/// @brief Backend `readback_rgba` op — pick the best source texture to read.
///
/// Prefers the offscreen scene color (or the fully composited post-FX result)
/// when scene targets are active. Falls back to the swapchain backbuffer
/// otherwise. Used by screenshot capture and golden-frame e2e tests.
static int d3d11_readback_rgba(
    void *ctx_ptr, uint8_t *dst_rgba, int32_t w, int32_t h, int32_t stride) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    ID3D11Texture2D *back_buffer = NULL;
    ID3D11Texture2D *source_tex = NULL;
    vgfx3d_d3d11_readback_kind_t readback_kind;
    HRESULT hr;

    if (!ctx || !dst_rgba || !vgfx3d_d3d11_validate_rgba8_destination(w, h, stride, NULL))
        return 0;
    readback_kind = vgfx3d_d3d11_choose_readback_kind(ctx->presented_color_valid,
                                                     ctx->scene_composited_to_swapchain,
                                                     ctx->gpu_postfx_enabled,
                                                     ctx->gpu_postfx_chain_valid,
                                                     ctx->gpu_postfx_chain.enabled,
                                                     ctx->gpu_postfx_chain.effect_count,
                                                     ctx->gpu_postfx_chain.effects != NULL,
                                                     d3d11_has_scene_targets(ctx),
                                                     ctx->current_target_kind);
    if (readback_kind == VGFX3D_D3D11_READBACK_PRESENTED_SNAPSHOT) {
        source_tex = ctx->presented_color_tex;
    } else if (readback_kind == VGFX3D_D3D11_READBACK_POSTFX_COMPOSITE) {
        if (!d3d11_apply_postfx_chain(ctx, &ctx->gpu_postfx_chain, NULL, 0, 0, 1, &source_tex)) {
            return 0;
        }
    } else if (readback_kind == VGFX3D_D3D11_READBACK_SCENE_COLOR && ctx->scene_width > 0 &&
               ctx->scene_height > 0) {
        if (ctx->overlay_used_this_frame && d3d11_has_overlay_target(ctx)) {
            if (!d3d11_resolve_scene_to_target(ctx, NULL, 0, 0, 1, &source_tex))
                return 0;
        } else {
            source_tex = ctx->scene_color_tex;
        }
    } else {
        hr = IDXGISwapChain_GetBuffer(
            ctx->swap_chain, 0, &IID_ID3D11Texture2D, (void **)&back_buffer);
        if (FAILED(hr)) {
            d3d11_log_hresult("IDXGISwapChain::GetBuffer(readback)", hr);
            return 0;
        }
        source_tex = back_buffer;
    }

    {
        int ok = d3d11_readback_texture_rgba(ctx, source_tex, dst_rgba, w, h, stride);
        SAFE_RELEASE(back_buffer);
        return ok;
    }
}

/// @brief Internal present path — composite scene + post-FX + overlay onto the swapchain.
static void d3d11_present_internal(d3d11_context_t *ctx, const vgfx3d_postfx_chain_t *chain) {
    int use_postfx;

    if (!ctx || ctx->rtt_active)
        return;
    if (ctx->scene_composited_to_swapchain) {
        d3d11_present_swapchain(ctx);
        ctx->scene_composited_to_swapchain = 0;
        return;
    }
    use_postfx = (chain != NULL && chain->enabled && chain->effect_count > 0 &&
                  ctx->gpu_postfx_enabled && d3d11_has_scene_targets(ctx))
                     ? 1
                     : 0;
    if (!use_postfx) {
        if (ctx->gpu_postfx_enabled && d3d11_has_scene_targets(ctx) &&
            d3d11_resolve_scene_to_target(ctx, ctx->rtv, ctx->width, ctx->height, 0, NULL)) {
            d3d11_present_swapchain(ctx);
            ctx->scene_composited_to_swapchain = 0;
            return;
        }
        d3d11_present_swapchain(ctx);
        ctx->scene_composited_to_swapchain = 0;
        return;
    }

    if (!d3d11_apply_postfx_chain(ctx, chain, ctx->rtv, ctx->width, ctx->height, 0, NULL)) {
        if (d3d11_resolve_scene_to_target(ctx, ctx->rtv, ctx->width, ctx->height, 0, NULL)) {
            d3d11_present_swapchain(ctx);
            ctx->scene_composited_to_swapchain = 0;
            return;
        }
        d3d11_present_swapchain(ctx);
        ctx->scene_composited_to_swapchain = 0;
        return;
    }
    d3d11_present_swapchain(ctx);
    ctx->scene_composited_to_swapchain = 0;
}

/// @brief Backend `apply_postfx` — composite scene/post-FX onto the swapchain without present.
static void d3d11_apply_postfx(void *ctx_ptr, const vgfx3d_postfx_chain_t *postfx) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    int use_postfx;

    if (!ctx || ctx->rtt_active)
        return;
    ctx->presented_color_valid = 0;
    if (ctx->scene_composited_to_swapchain)
        return;
    if (!vgfx3d_d3d11_should_composite_to_swapchain(ctx->rtt_active,
                                                    ctx->gpu_postfx_enabled,
                                                    d3d11_has_scene_targets(ctx),
                                                    ctx->scene_composited_to_swapchain))
        return;
    use_postfx = (postfx != NULL && postfx->enabled && postfx->effect_count > 0 &&
                  ctx->gpu_postfx_enabled && d3d11_has_scene_targets(ctx))
                     ? 1
                     : 0;
    if (!use_postfx) {
        if (ctx->gpu_postfx_enabled && d3d11_has_scene_targets(ctx) &&
            d3d11_resolve_scene_to_target(ctx, ctx->rtv, ctx->width, ctx->height, 0, NULL))
            ctx->scene_composited_to_swapchain = 1;
        return;
    }
    if (d3d11_apply_postfx_chain(ctx, postfx, ctx->rtv, ctx->width, ctx->height, 0, NULL)) {
        ctx->scene_composited_to_swapchain = 1;
        return;
    }
    if (d3d11_resolve_scene_to_target(ctx, ctx->rtv, ctx->width, ctx->height, 0, NULL))
        ctx->scene_composited_to_swapchain = 1;
}

/// @brief Backend `present` (no post-FX) — direct swapchain present.
static void d3d11_present(void *ctx_ptr) {
    d3d11_present_internal((d3d11_context_t *)ctx_ptr, NULL);
}

/// @brief Backend `present_postfx` — present with an ordered post-FX chain applied.
static void d3d11_present_postfx(void *ctx_ptr, const vgfx3d_postfx_chain_t *postfx) {
    d3d11_present_internal((d3d11_context_t *)ctx_ptr, postfx);
}

/// @brief Backend `set_gpu_postfx_enabled` — toggle the postfx route.
///
/// When enabled, frames render to the offscreen scene target so postfx
/// can read it. When disabled, frames render straight to the swapchain.
static void d3d11_set_gpu_postfx_enabled(void *ctx_ptr, int8_t enabled) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    int8_t next_enabled = enabled ? 1 : 0;
    if (!ctx)
        return;
    if (vgfx3d_d3d11_should_reset_composited_swapchain_for_postfx_update(
            ctx->gpu_postfx_enabled, next_enabled)) {
        ctx->scene_composited_to_swapchain = 0;
        ctx->presented_color_valid = 0;
    }
    ctx->gpu_postfx_enabled = next_enabled;
    if (!ctx->gpu_postfx_enabled) {
        vgfx3d_postfx_chain_reset(&ctx->gpu_postfx_chain);
        ctx->gpu_postfx_chain_valid = 0;
        d3d11_unbind_postfx_resources(ctx);
        d3d11_reset_temporal_scene_state(ctx);
    }
}

/// @brief Backend `set_gpu_postfx_snapshot` — latch the frame's postfx chain for replay.
/// @details Called once per frame from the canvas. A null `postfx` clears the cached
///   chain (equivalent to "no postfx this frame"); otherwise the chain is deep-copied
///   into the context so later present-time passes see a stable snapshot even if the
///   caller mutates their chain afterward. A copy failure aborts-and-resets rather than
///   rendering with a partial chain.
static void d3d11_set_gpu_postfx_snapshot(void *ctx_ptr, const vgfx3d_postfx_chain_t *postfx) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    if (!ctx)
        return;
    if (!postfx) {
        vgfx3d_postfx_chain_reset(&ctx->gpu_postfx_chain);
        ctx->gpu_postfx_chain_valid = 0;
        return;
    }
    if (!vgfx3d_postfx_chain_copy(&ctx->gpu_postfx_chain, postfx)) {
        vgfx3d_postfx_chain_reset(&ctx->gpu_postfx_chain);
        ctx->gpu_postfx_chain_valid = 0;
        return;
    }
    ctx->gpu_postfx_chain_valid = 1;
}

/// @brief Backend `set_render_target` — bind / unbind RTT mode.
///
/// NULL `rt` tears down RTT and reverts to swapchain rendering.
/// Non-NULL ensures RTT targets at the requested size and activates them.
static void d3d11_set_render_target(void *ctx_ptr, vgfx3d_rendertarget_t *rt) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    HRESULT hr;

    if (!ctx)
        return;
    ctx->scene_composited_to_swapchain = 0;
    ctx->presented_color_valid = 0;
    if (!rt) {
        d3d11_destroy_rtt_targets(ctx);
        return;
    }

    hr = d3d11_ensure_rtt_targets(ctx, rt);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateRTTTargets", hr);
        d3d11_destroy_rtt_targets(ctx);
    }
}

/// @brief Backend `shadow_begin` — switch the pipeline to shadow-pass mode.
///
/// Allocates the shadow depth target if needed, captures the light's
/// view-projection matrix (used by main pass for shadow sampling),
/// unbinds the shadow SRV (so we can write to it), binds the shadow
/// DSV with no color RTV (depth-only pass), clears depth, sets viewport,
/// and binds the shadow vertex shader with no pixel shader.
static void d3d11_shadow_begin(void *ctx_ptr,
                               int32_t slot,
                               float *depth_buf,
                               int32_t width,
                               int32_t height,
                               const float *light_vp) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    D3D11_VIEWPORT viewport;
    HRESULT hr;

    (void)depth_buf;
    if (!ctx || !light_vp || width <= 0 || height <= 0 || slot < 0 ||
        slot >= VGFX3D_MAX_SHADOW_LIGHTS)
        return;
    ctx->shadow_pass_slot = -1;
    hr = d3d11_ensure_shadow_targets(ctx, slot, width, height);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateShadowTargets", hr);
        d3d11_release_shadow_slot(ctx, slot);
        d3d11_recompute_shadow_count(ctx);
        return;
    }

    ctx->shadow_pass_slot = slot;
    memcpy(ctx->shadow_vp[slot], light_vp, sizeof(ctx->shadow_vp[slot]));
    d3d11_unbind_shadow_resources(ctx);
    ID3D11DeviceContext_OMSetRenderTargets(ctx->ctx, 0, NULL, ctx->shadow_dsv[slot]);
    ID3D11DeviceContext_ClearDepthStencilView(
        ctx->ctx, ctx->shadow_dsv[slot], D3D11_CLEAR_DEPTH, 1.0f, 0);
    memset(&viewport, 0, sizeof(viewport));
    viewport.Width = (FLOAT)width;
    viewport.Height = (FLOAT)height;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    ID3D11DeviceContext_RSSetViewports(ctx->ctx, 1, &viewport);
    ID3D11DeviceContext_RSSetState(ctx->ctx, ctx->rs_solid_no_cull);
    ID3D11DeviceContext_OMSetDepthStencilState(ctx->ctx, ctx->depth_state, 0);
    ID3D11DeviceContext_IASetInputLayout(ctx->ctx, ctx->input_layout);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx->ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(ctx->ctx, ctx->vs_shadow, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx->ctx, NULL, NULL, 0);
}

/// @brief Backend `shadow_draw` — emit a single mesh into the shadow depth buffer.
///
/// Slimmer than `submit_draw`: only object/scene cbuffers + bone
/// palettes + morph SRV (if any) — no material, no lights, no
/// pixel shader. Uses the light-space VP captured by `shadow_begin`.
static void d3d11_shadow_draw(void *ctx_ptr, const vgfx3d_draw_cmd_t *cmd) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    d3d_per_object_t object_data;
    d3d_per_scene_t scene_data;
    d3d_per_material_t material_data;
    d3d_temp_srv_t shadow_diffuse = {0};
    ID3D11ShaderResourceView *shadow_diffuse_srv = NULL;
    ID3D11ShaderResourceView *null_shadow_ps_srv[1] = {NULL};
    ID3D11SamplerState *shadow_diffuse_sampler = NULL;
    HRESULT hr;
    UINT stride = sizeof(vgfx3d_vertex_t);
    UINT offset = 0;
    ID3D11Buffer *mesh_vb = NULL;
    ID3D11Buffer *mesh_ib = NULL;
    int alpha_masked_shadow = cmd && cmd->alpha_mode == RT_MATERIAL3D_ALPHA_MODE_MASK;

    if (!ctx || !cmd || ctx->shadow_pass_slot < 0 ||
        ctx->shadow_pass_slot >= VGFX3D_MAX_SHADOW_LIGHTS || !cmd->vertices || !cmd->indices ||
        cmd->vertex_count == 0 || cmd->index_count == 0)
        return;

    d3d11_prepare_object_data(cmd, &object_data);
    memset(&scene_data, 0, sizeof(scene_data));
    memcpy(scene_data.vp, ctx->shadow_vp[ctx->shadow_pass_slot], sizeof(scene_data.vp));
    d3d11_prepare_anim_resources(ctx, cmd, &object_data);

    hr = d3d11_update_constant_buffer(ctx, ctx->cb_per_object, &object_data, sizeof(object_data));
    if (FAILED(hr)) {
        d3d11_log_hresult("Map(cbPerObject shadow)", hr);
        return;
    }
    hr = d3d11_update_constant_buffer(ctx, ctx->cb_per_scene, &scene_data, sizeof(scene_data));
    if (FAILED(hr)) {
        d3d11_log_hresult("Map(cbPerScene shadow)", hr);
        return;
    }
    if (alpha_masked_shadow) {
        shadow_diffuse_srv =
            d3d11_get_or_create_material_srv(ctx, cmd->texture_asset, cmd->texture, &shadow_diffuse);
        d3d11_prepare_material_data(
            cmd, shadow_diffuse_srv != NULL, 0, 0, 0, 0, 0, 0, 0, &material_data);
        hr = d3d11_update_constant_buffer(
            ctx, ctx->cb_per_material, &material_data, sizeof(material_data));
        if (FAILED(hr)) {
            d3d11_log_hresult("Map(cbPerMaterial shadow)", hr);
            d3d11_release_temp_srv(&shadow_diffuse);
            return;
        }
    }

    if (!d3d11_acquire_mesh_buffers(ctx, cmd, &mesh_vb, &mesh_ib)) {
        d3d11_release_temp_srv(&shadow_diffuse);
        return;
    }

    ID3D11DeviceContext_IASetVertexBuffers(ctx->ctx, 0, 1, &mesh_vb, &stride, &offset);
    ID3D11DeviceContext_IASetIndexBuffer(ctx->ctx, mesh_ib, DXGI_FORMAT_R32_UINT, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx->ctx, 0, 1, &ctx->cb_per_object);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx->ctx, 1, 1, &ctx->cb_per_scene);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx->ctx, 4, 1, &ctx->cb_bones);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx->ctx, 5, 1, &ctx->cb_prev_bones);
    if (alpha_masked_shadow && ctx->ps_shadow) {
        shadow_diffuse_sampler =
            d3d11_get_material_sampler(ctx, cmd, RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR);
        ID3D11DeviceContext_PSSetShader(ctx->ctx, ctx->ps_shadow, NULL, 0);
        ID3D11DeviceContext_PSSetConstantBuffers(ctx->ctx, 2, 1, &ctx->cb_per_material);
        ID3D11DeviceContext_PSSetShaderResources(ctx->ctx, 0, 1, &shadow_diffuse_srv);
        if (shadow_diffuse_sampler)
            ID3D11DeviceContext_PSSetSamplers(ctx->ctx, 0, 1, &shadow_diffuse_sampler);
    } else {
        ID3D11DeviceContext_PSSetShader(ctx->ctx, NULL, NULL, 0);
    }
    {
        ID3D11ShaderResourceView *vs_srvs[2] = {ctx->current_morph_srv, NULL};
        ID3D11DeviceContext_VSSetShaderResources(ctx->ctx, 0, 2, vs_srvs);
    }
    ID3D11DeviceContext_DrawIndexed(ctx->ctx, cmd->index_count, 0, 0);
    ID3D11DeviceContext_PSSetShaderResources(ctx->ctx, 0, 1, null_shadow_ps_srv);
    d3d11_release_temp_srv(&shadow_diffuse);
    {
        ID3D11ShaderResourceView *null_vs[2] = {NULL, NULL};
        ID3D11DeviceContext_VSSetShaderResources(ctx->ctx, 0, 2, null_vs);
    }
}

/// @brief Backend `shadow_end` — exit shadow-pass mode and rebind the main targets.
///
/// Marks `shadow_active = 1` so the main pass's PerScene cbuffer
/// reports `shadowEnabled` to the shader. Stores the bias the shader
/// uses to bias depth comparisons. Re-selects the active scene/RTT
/// targets so subsequent draws hit the right buffers.
static void d3d11_shadow_end(void *ctx_ptr, int32_t slot, float bias) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    if (!ctx || slot < 0 || slot >= VGFX3D_MAX_SHADOW_LIGHTS)
        return;
    if (ctx->shadow_pass_slot != slot)
        return;
    d3d11_recompute_shadow_count(ctx);
    ctx->shadow_bias = bias;
    ctx->shadow_pass_slot = -1;
    d3d11_select_current_targets(ctx);
}

/// @brief Backend `draw_skybox` — full-screen triangle draw with the cubemap shader.
///
/// Sets up skybox VS/PS, the fullscreen-triangle VB built at context creation,
/// the inverse-projection/inverse-view-rotation constants, and a depth state
/// that draws "behind" everything else (`<=` compare with no depth writes).
/// The cubemap is bound from the cubemap cache or as a temp resource.
static void d3d11_draw_skybox(void *ctx_ptr, const void *cubemap_ptr) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    const rt_cubemap3d *cubemap = (const rt_cubemap3d *)cubemap_ptr;
    d3d_skybox_cb_t skybox_data;
    d3d_temp_srv_t cubemap_resource;
    ID3D11ShaderResourceView *srv;
    HRESULT hr;
    UINT stride = sizeof(float) * 3;
    UINT offset = 0;
    D3D11_VIEWPORT viewport;
    float blend_factor[4] = {0, 0, 0, 0};

    if (!ctx || !cubemap || !ctx->vs_skybox || !ctx->ps_skybox || !ctx->skybox_vb ||
        ctx->current_rtv_count == 0 || !ctx->current_rtvs[0] || ctx->current_width <= 0 ||
        ctx->current_height <= 0)
        return;

    memset(&cubemap_resource, 0, sizeof(cubemap_resource));
    srv = d3d11_get_or_create_cubemap_srv(ctx, cubemap, &cubemap_resource);
    if (!srv)
        return;

    d3d11_prepare_skybox_data(ctx, &skybox_data);
    hr = d3d11_update_constant_buffer(ctx, ctx->cb_skybox, &skybox_data, sizeof(skybox_data));
    if (FAILED(hr)) {
        d3d11_log_hresult("Map(cbSkybox)", hr);
        d3d11_release_temp_srv(&cubemap_resource);
        return;
    }

    ID3D11DeviceContext_IASetInputLayout(ctx->ctx, ctx->input_layout_skybox);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx->ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_IASetVertexBuffers(ctx->ctx, 0, 1, &ctx->skybox_vb, &stride, &offset);
    memset(&viewport, 0, sizeof(viewport));
    viewport.Width = (FLOAT)ctx->current_width;
    viewport.Height = (FLOAT)ctx->current_height;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    ID3D11DeviceContext_OMSetRenderTargets(ctx->ctx, 1, ctx->current_rtvs, ctx->current_dsv);
    ID3D11DeviceContext_RSSetViewports(ctx->ctx, 1, &viewport);
    ID3D11DeviceContext_VSSetShader(ctx->ctx, ctx->vs_skybox, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx->ctx, ctx->ps_skybox, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx->ctx, 0, 1, &ctx->cb_skybox);
    if (ctx->linear_clamp_sampler)
        ID3D11DeviceContext_PSSetSamplers(ctx->ctx, 0, 1, &ctx->linear_clamp_sampler);
    ID3D11DeviceContext_PSSetShaderResources(ctx->ctx, 0, 1, &srv);
    ID3D11DeviceContext_RSSetState(ctx->ctx, ctx->rs_solid_no_cull);
    ID3D11DeviceContext_OMSetDepthStencilState(ctx->ctx, ctx->depth_state_readonly_lequal, 0);
    ID3D11DeviceContext_OMSetBlendState(
        ctx->ctx, ctx->blend_state_opaque, blend_factor, 0xFFFFFFFF);
    ID3D11DeviceContext_Draw(ctx->ctx, 3, 0);
    {
        ID3D11ShaderResourceView *null_srv = NULL;
        ID3D11DeviceContext_PSSetShaderResources(ctx->ctx, 0, 1, &null_srv);
    }
    d3d11_release_temp_srv(&cubemap_resource);
}

const vgfx3d_backend_t vgfx3d_d3d11_backend = {
    .name = "d3d11",
    .create_ctx = d3d11_create_ctx,
    .destroy_ctx = d3d11_destroy_ctx,
    .clear = d3d11_clear,
    .resize = d3d11_resize,
    .begin_frame = d3d11_begin_frame,
    .submit_draw = d3d11_submit_draw,
    .end_frame = d3d11_end_frame,
    .set_render_target = d3d11_set_render_target,
    .shadow_begin = d3d11_shadow_begin,
    .shadow_draw = d3d11_shadow_draw,
    .shadow_end = d3d11_shadow_end,
    .draw_skybox = d3d11_draw_skybox,
    .submit_draw_instanced = d3d11_submit_draw_instanced,
    .present = d3d11_present,
    .readback_rgba = d3d11_readback_rgba,
    .present_postfx = d3d11_present_postfx,
    .apply_postfx = d3d11_apply_postfx,
    .set_gpu_postfx_enabled = d3d11_set_gpu_postfx_enabled,
    .set_gpu_postfx_snapshot = d3d11_set_gpu_postfx_snapshot,
    .set_texture_upload_budget = d3d11_set_texture_upload_budget,
    .get_texture_upload_pending_bytes = d3d11_get_texture_upload_pending_bytes,
    .get_texture_upload_bytes = d3d11_get_texture_upload_bytes,
    .get_native_texture_caps = d3d11_get_native_texture_caps,
};

#endif /* _WIN32 && VIPER_ENABLE_GRAPHICS */
