//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/vgfx3d_backend_d3d11.c
// Purpose: Direct3D 11 GPU backend for Viper.Graphics3D (Windows).
//
// Key invariants:
//   - Requires Windows 7+ with D3D11 feature level 11_0
//   - Falls back to software if D3D11 unavailable
//   - HLSL shaders compiled at runtime via D3DCompile
//   - row_major float4x4 in HLSL (matches Viper row-major convention)
//
// Links: vgfx3d_backend.h, plans/3d/03-d3d11-backend.md
//
//===----------------------------------------------------------------------===//

#if defined(_WIN32) && defined(VIPER_ENABLE_GRAPHICS)

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <d3d11.h>
#include <d3dcompiler.h>
#include <windows.h>

#include "vgfx.h"
#include "vgfx3d_backend.h"
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

static const float k_identity4x4[16] = {
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
};

static const char *d3d11_shader_source =
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
    "    float morphWeights[32];\n"
    "    float prevMorphWeights[32];\n"
    "};\n"
    "\n"
    "cbuffer PerScene : register(b1) {\n"
    "    row_major float4x4 viewProjection;\n"
    "    row_major float4x4 prevViewProjection;\n"
    "    row_major float4x4 shadowVP;\n"
    "    float4 cameraPosition;\n"
    "    float4 ambientColor;\n"
    "    float4 fogColor;\n"
    "    float fogNear;\n"
    "    float fogFar;\n"
    "    float shadowBias;\n"
    "    int lightCount;\n"
    "    int shadowEnabled;\n"
    "    float _scenePad0;\n"
    "    float _scenePad1;\n"
    "    float _scenePad2;\n"
    "};\n"
    "\n"
    "cbuffer PerMaterial : register(b2) {\n"
    "    float4 diffuseColor;\n"
    "    float4 specularColor;\n"
    "    float4 emissiveColor;\n"
    "    float alpha;\n"
    "    float reflectivity;\n"
    "    int hasTexture;\n"
    "    int hasNormalMap;\n"
    "    int hasSpecularMap;\n"
    "    int hasEmissiveMap;\n"
    "    int unlit;\n"
    "    int hasEnvMap;\n"
    "    int hasSplat;\n"
    "    int shadingModel;\n"
    "    float customParams[8];\n"
    "    float4 splatScales;\n"
    "};\n"
    "\n"
    "cbuffer PerLights : register(b3) {\n"
    "    Light lights[8];\n"
    "};\n"
    "\n"
    "cbuffer BonesCurrent : register(b4) {\n"
    "    row_major float4x4 bonePalette[128];\n"
    "};\n"
    "\n"
    "cbuffer BonesPrevious : register(b5) {\n"
    "    row_major float4x4 prevBonePalette[128];\n"
    "};\n"
    "\n"
    "Buffer<float> morphDeltas : register(t0);\n"
    "Buffer<float> morphNormalDeltas : register(t1);\n"
    "\n"
    "Texture2D diffuseTex : register(t0);\n"
    "Texture2D normalTex : register(t1);\n"
    "Texture2D specularTex : register(t2);\n"
    "Texture2D emissiveTex : register(t3);\n"
    "Texture2D<float> shadowTex : register(t4);\n"
    "Texture2D splatTex : register(t5);\n"
    "Texture2D splatLayer0 : register(t6);\n"
    "Texture2D splatLayer1 : register(t7);\n"
    "Texture2D splatLayer2 : register(t8);\n"
    "Texture2D splatLayer3 : register(t9);\n"
    "TextureCube envTex : register(t10);\n"
    "SamplerState texSampler : register(s0);\n"
    "SamplerComparisonState shadowSampler : register(s1);\n"
    "SamplerState envSampler : register(s2);\n"
    "\n"
    "struct VS_INPUT {\n"
    "    float3 pos : POSITION;\n"
    "    float3 normal : NORMAL;\n"
    "    float2 uv : TEXCOORD0;\n"
    "    float4 color : COLOR0;\n"
    "    float3 tangent : TANGENT;\n"
    "    uint4 boneIdx : BLENDINDICES;\n"
    "    float4 boneWt : BLENDWEIGHT;\n"
    "};\n"
    "\n"
    "struct VS_INPUT_INSTANCED {\n"
    "    float3 pos : POSITION;\n"
    "    float3 normal : NORMAL;\n"
    "    float2 uv : TEXCOORD0;\n"
    "    float4 color : COLOR0;\n"
    "    float3 tangent : TANGENT;\n"
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
    "    float3 tangent : TEXCOORD3;\n"
    "    float4 color : COLOR0;\n"
    "    float4 currClip : TEXCOORD4;\n"
    "    float4 prevClip : TEXCOORD5;\n"
    "    float hasObjectHistory : TEXCOORD6;\n"
    "};\n"
    "\n"
    "struct PS_OUTPUT {\n"
    "    float4 color : SV_Target0;\n"
    "    float4 motion : SV_Target1;\n"
    "};\n"
    "\n"
    "float3 applyMorphPosition(float3 pos, uint vid, int usePrevWeights) {\n"
    "    if (morphShapeCount <= 0 || vertexCount <= 0)\n"
    "        return pos;\n"
    "    for (int s = 0; s < morphShapeCount; s++) {\n"
    "        float w = usePrevWeights != 0 ? prevMorphWeights[s] : morphWeights[s];\n"
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
    "        float w = usePrevWeights != 0 ? prevMorphWeights[s] : morphWeights[s];\n"
    "        if (abs(w) > 0.0001) {\n"
    "            int base = (s * vertexCount + int(vid)) * 3;\n"
    "            nrm.x += morphNormalDeltas[base + 0] * w;\n"
    "            nrm.y += morphNormalDeltas[base + 1] * w;\n"
    "            nrm.z += morphNormalDeltas[base + 2] * w;\n"
    "        }\n"
    "    }\n"
    "    return normalize(nrm);\n"
    "}\n"
    "\n"
    "float4 skinPosition(float4 pos, uint4 boneIdx, float4 boneWt, int usePrevPalette) {\n"
    "    if ((usePrevPalette != 0 && hasPrevSkinning == 0) || (usePrevPalette == 0 && hasSkinning == 0))\n"
    "        return pos;\n"
    "    float4 skinned = float4(0.0, 0.0, 0.0, 0.0);\n"
    "    for (int i = 0; i < 4; i++) {\n"
    "        float w = boneWt[i];\n"
    "        if (w <= 0.0001)\n"
    "            continue;\n"
    "        uint idx = min(boneIdx[i], 127u);\n"
    "        row_major float4x4 bm = usePrevPalette != 0 ? prevBonePalette[idx] : bonePalette[idx];\n"
    "        skinned += mul(pos, bm) * w;\n"
    "    }\n"
    "    return skinned;\n"
    "}\n"
    "\n"
    "float3 skinVector(float3 vec, uint4 boneIdx, float4 boneWt) {\n"
    "    if (hasSkinning == 0)\n"
    "        return vec;\n"
    "    float3 skinned = float3(0.0, 0.0, 0.0);\n"
    "    for (int i = 0; i < 4; i++) {\n"
    "        float w = boneWt[i];\n"
    "        if (w <= 0.0001)\n"
    "            continue;\n"
    "        uint idx = min(boneIdx[i], 127u);\n"
    "        skinned += mul(float4(vec, 0.0), bonePalette[idx]).xyz * w;\n"
    "    }\n"
    "    return skinned;\n"
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
    "                     float3 tan,\n"
    "                     float2 uv,\n"
    "                     float4 color,\n"
    "                     row_major float4x4 currentModel,\n"
    "                     row_major float4x4 currentNormal,\n"
    "                     row_major float4x4 prevModel,\n"
    "                     float hasHistory) {\n"
    "    PS_INPUT output;\n"
    "    float4 wp = mul(float4(pos, 1.0), currentModel);\n"
    "    float4 prevWp = mul(float4(prevPos, 1.0), prevModel);\n"
    "    float4 currClip = mul(wp, viewProjection);\n"
    "    float4 prevClip = mul(prevWp, prevViewProjection);\n"
    "    output.pos = currClip;\n"
    "    output.pos.z = output.pos.z * 0.5 + output.pos.w * 0.5;\n"
    "    output.worldPos = wp.xyz;\n"
    "    output.normal = mul(float4(nrm, 0.0), currentNormal).xyz;\n"
    "    output.tangent = mul(float4(tan, 0.0), currentModel).xyz;\n"
    "    output.uv = uv;\n"
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
    "    float3 tan = input.tangent;\n"
    "    float4 skinnedPos = skinPosition(float4(pos, 1.0), input.boneIdx, input.boneWt, 0);\n"
    "    float4 prevSkinnedPos = skinPosition(float4(prevPos, 1.0), input.boneIdx, input.boneWt, 1);\n"
    "    float3 skinnedNormal = normalize(skinVector(nrm, input.boneIdx, input.boneWt));\n"
    "    float3 skinnedTangent = normalize(skinVector(tan, input.boneIdx, input.boneWt));\n"
    "    if (hasSkinning == 0) {\n"
    "        skinnedPos = float4(pos, 1.0);\n"
    "        skinnedNormal = normalize(nrm);\n"
    "        skinnedTangent = normalize(tan);\n"
    "    }\n"
    "    if (hasPrevSkinning == 0)\n"
    "        prevSkinnedPos = float4(prevPos, 1.0);\n"
    "    return buildOutput(skinnedPos.xyz,\n"
    "                       prevSkinnedPos.xyz,\n"
    "                       skinnedNormal,\n"
    "                       skinnedTangent,\n"
    "                       input.uv,\n"
    "                       input.color,\n"
    "                       modelMatrix,\n"
    "                       normalMatrix,\n"
    "                       hasPrevModelMatrix != 0 ? prevModelMatrix : modelMatrix,\n"
    "                       (hasPrevModelMatrix != 0 || hasPrevSkinning != 0 || hasPrevMorphWeights != 0) ? 1.0 : 0.0);\n"
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
    "    float3 tan = input.tangent;\n"
    "    float4 skinnedPos = skinPosition(float4(pos, 1.0), input.boneIdx, input.boneWt, 0);\n"
    "    float4 prevSkinnedPos = skinPosition(float4(prevPos, 1.0), input.boneIdx, input.boneWt, 1);\n"
    "    float3 skinnedNormal = normalize(skinVector(nrm, input.boneIdx, input.boneWt));\n"
    "    float3 skinnedTangent = normalize(skinVector(tan, input.boneIdx, input.boneWt));\n"
    "    if (hasSkinning == 0) {\n"
    "        skinnedPos = float4(pos, 1.0);\n"
    "        skinnedNormal = normalize(nrm);\n"
    "        skinnedTangent = normalize(tan);\n"
    "    }\n"
    "    if (hasPrevSkinning == 0)\n"
    "        prevSkinnedPos = float4(prevPos, 1.0);\n"
    "    return buildOutput(skinnedPos.xyz,\n"
    "                       prevSkinnedPos.xyz,\n"
    "                       skinnedNormal,\n"
    "                       skinnedTangent,\n"
    "                       input.uv,\n"
    "                       input.color,\n"
    "                       instModel,\n"
    "                       instNormal,\n"
    "                       hasPrevInstanceMatrices != 0 ? prevModel : instModel,\n"
    "                       (hasPrevInstanceMatrices != 0 || hasPrevSkinning != 0 || hasPrevMorphWeights != 0) ? 1.0 : 0.0);\n"
    "}\n"
    "\n"
    "float sampleShadow(float3 worldPos) {\n"
    "    if (shadowEnabled == 0)\n"
    "        return 1.0;\n"
    "    float4 lc = mul(float4(worldPos, 1.0), shadowVP);\n"
    "    float invW = 1.0 / max(lc.w, 0.0001);\n"
    "    float3 ndc = lc.xyz * invW;\n"
    "    float2 uv = ndc.xy * 0.5 + 0.5;\n"
    "    float depth = ndc.z * 0.5 + 0.5;\n"
    "    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || depth > 1.0)\n"
    "        return 1.0;\n"
    "    return shadowTex.SampleCmpLevelZero(shadowSampler, uv, depth - shadowBias);\n"
    "}\n"
    "\n"
    "PS_OUTPUT PSMain(PS_INPUT input) {\n"
    "    PS_OUTPUT output;\n"
    "    float3 baseColor = diffuseColor.rgb * input.color.rgb;\n"
    "    float texAlpha = 1.0;\n"
    "    float finalAlpha = alpha * input.color.a;\n"
    "    if (hasTexture != 0) {\n"
    "        float4 texSample = diffuseTex.Sample(texSampler, input.uv);\n"
    "        baseColor *= texSample.rgb;\n"
    "        texAlpha = texSample.a;\n"
    "    }\n"
    "    if (hasSplat != 0) {\n"
    "        float4 sp = splatTex.Sample(texSampler, input.uv);\n"
    "        float sum = sp.r + sp.g + sp.b + sp.a;\n"
    "        if (sum > 0.0001) {\n"
    "            sp /= sum;\n"
    "            float3 splatColor = splatLayer0.Sample(texSampler, input.uv * splatScales.x).rgb * sp.r +\n"
    "                                splatLayer1.Sample(texSampler, input.uv * splatScales.y).rgb * sp.g +\n"
    "                                splatLayer2.Sample(texSampler, input.uv * splatScales.z).rgb * sp.b +\n"
    "                                splatLayer3.Sample(texSampler, input.uv * splatScales.w).rgb * sp.a;\n"
    "            baseColor = splatColor * diffuseColor.rgb * input.color.rgb;\n"
    "        }\n"
    "    }\n"
    "    finalAlpha *= texAlpha;\n"
    "    float3 N = normalize(input.normal);\n"
    "    if (hasNormalMap != 0) {\n"
    "        float3 mapN = normalTex.Sample(texSampler, input.uv).xyz * 2.0 - 1.0;\n"
    "        float3 T = normalize(input.tangent - N * dot(input.tangent, N));\n"
    "        if (dot(T, T) > 0.0001) {\n"
    "            float3 B = normalize(cross(N, T));\n"
    "            N = normalize(mapN.x * T + mapN.y * B + mapN.z * N);\n"
    "        }\n"
    "    }\n"
    "    float3 emissive = emissiveColor.rgb;\n"
    "    if (hasEmissiveMap != 0)\n"
    "        emissive *= emissiveTex.Sample(texSampler, input.uv).rgb;\n"
    "    float3 result = ambientColor.rgb * baseColor;\n"
    "    if (unlit != 0) {\n"
    "        result = baseColor + emissive;\n"
    "    } else {\n"
    "        float3 V = normalize(cameraPosition.xyz - input.worldPos);\n"
    "        float3 specColor = specularColor.rgb;\n"
    "        if (hasSpecularMap != 0)\n"
    "            specColor *= specularTex.Sample(texSampler, input.uv).rgb;\n"
    "        for (int i = 0; i < lightCount; i++) {\n"
    "            float3 L = float3(0.0, 0.0, 0.0);\n"
    "            float atten = 1.0;\n"
    "            if (lights[i].type == 0) {\n"
    "                L = normalize(-lights[i].direction.xyz);\n"
    "                atten *= lerp(0.15, 1.0, sampleShadow(input.worldPos));\n"
    "            } else if (lights[i].type == 1) {\n"
    "                float3 toLight = lights[i].position.xyz - input.worldPos;\n"
    "                float d = length(toLight);\n"
    "                L = toLight / max(d, 0.0001);\n"
    "                atten = 1.0 / (1.0 + lights[i].attenuation * d * d);\n"
    "            } else if (lights[i].type == 2) {\n"
    "                result += lights[i].color.rgb * lights[i].intensity * baseColor;\n"
    "                continue;\n"
    "            } else if (lights[i].type == 3) {\n"
    "                float3 toLight = lights[i].position.xyz - input.worldPos;\n"
    "                float d = length(toLight);\n"
    "                L = toLight / max(d, 0.0001);\n"
    "                float cone = smoothstep(lights[i].outer_cos,\n"
    "                                        lights[i].inner_cos,\n"
    "                                        dot(normalize(-lights[i].direction.xyz), L));\n"
    "                atten = cone / (1.0 + lights[i].attenuation * d * d);\n"
    "            } else {\n"
    "                continue;\n"
    "            }\n"
    "            float NdotL = max(dot(N, L), 0.0);\n"
    "            if (shadingModel == 1) {\n"
    "                float bands = max(customParams[0], 2.0);\n"
    "                NdotL = floor(NdotL * bands) / max(bands - 1.0, 1.0);\n"
    "            }\n"
    "            result += lights[i].color.rgb * lights[i].intensity * NdotL * baseColor * atten;\n"
    "            if (NdotL > 0.0 && specularColor.w > 0.0) {\n"
    "                float3 H = normalize(L + V);\n"
    "                float spec = pow(max(dot(N, H), 0.0), specularColor.w);\n"
    "                if (shadingModel == 1)\n"
    "                    spec = spec >= max(customParams[1], 0.5) ? 1.0 : 0.0;\n"
    "                result += lights[i].color.rgb * lights[i].intensity * spec * specColor * atten;\n"
    "            }\n"
        "        }\n"
        "        result += emissive;\n"
        "        if (hasEnvMap != 0) {\n"
    "            float3 V = normalize(cameraPosition.xyz - input.worldPos);\n"
    "            float3 R = reflect(-V, normalize(N));\n"
        "            float3 envColor = envTex.Sample(envSampler, R).rgb;\n"
        "            result = lerp(result, envColor, saturate(reflectivity));\n"
        "        }\n"
    "    }\n"
    "    if (shadingModel == 4) {\n"
    "        float3 V = normalize(cameraPosition.xyz - input.worldPos);\n"
    "        float ndv = saturate(dot(N, V));\n"
    "        float power = max(customParams[0], 1.0);\n"
        "        float bias = customParams[1];\n"
    "        finalAlpha *= saturate(pow(1.0 - ndv, power) + bias);\n"
    "    } else if (shadingModel == 5) {\n"
    "        float strength = max(customParams[0], 1.0);\n"
    "        result += emissive * (strength - 1.0);\n"
    "    }\n"
    "    if (fogColor.a > 0.5) {\n"
    "        float dist = length(cameraPosition.xyz - input.worldPos);\n"
    "        float fogFactor = saturate((dist - fogNear) / max(fogFar - fogNear, 0.001));\n"
    "        result = lerp(result, fogColor.rgb, fogFactor);\n"
    "    }\n"
    "    output.color = float4(result, finalAlpha);\n"
    "    float2 currNdc = input.currClip.xy / max(input.currClip.w, 0.0001);\n"
    "    float2 prevNdc = input.prevClip.xy / max(input.prevClip.w, 0.0001);\n"
    "    float2 velocity = (currNdc - prevNdc) * 0.5;\n"
    "    output.motion = float4(saturate(velocity * 0.5 + 0.5), input.hasObjectHistory, 1.0);\n"
    "    return output;\n"
    "}\n"
    "\n"
    "struct SHADOW_OUT {\n"
    "    float4 pos : SV_POSITION;\n"
    "};\n"
    "\n"
    "SHADOW_OUT VSShadow(VS_INPUT input, uint vid : SV_VertexID) {\n"
    "    SHADOW_OUT output;\n"
    "    float3 pos = applyMorphPosition(input.pos, vid, 0);\n"
    "    float4 skinnedPos = skinPosition(float4(pos, 1.0), input.boneIdx, input.boneWt, 0);\n"
    "    if (hasSkinning == 0)\n"
    "        skinnedPos = float4(pos, 1.0);\n"
    "    float4 wp = mul(skinnedPos, modelMatrix);\n"
    "    float4 clip = mul(wp, viewProjection);\n"
    "    clip.z = clip.z * 0.5 + clip.w * 0.5;\n"
    "    output.pos = clip;\n"
    "    return output;\n"
    "}\n";

static const char *d3d11_skybox_shader_source =
    "cbuffer Skybox : register(b0) {\n"
    "    row_major float4x4 viewRotation;\n"
    "    row_major float4x4 projection;\n"
    "};\n"
    "TextureCube skyboxTex : register(t0);\n"
    "SamplerState skyboxSampler : register(s0);\n"
    "struct VS_INPUT { float3 pos : POSITION; };\n"
    "struct VS_OUTPUT {\n"
    "    float4 pos : SV_POSITION;\n"
    "    float3 dir : TEXCOORD0;\n"
    "};\n"
    "VS_OUTPUT VSSkybox(VS_INPUT input) {\n"
    "    VS_OUTPUT output;\n"
    "    float4 viewPos = mul(float4(input.pos, 1.0), viewRotation);\n"
    "    float4 clip = mul(viewPos, projection);\n"
    "    clip.z = clip.w;\n"
    "    output.pos = clip;\n"
    "    output.dir = input.pos;\n"
    "    return output;\n"
    "}\n"
    "float4 PSSkybox(VS_OUTPUT input) : SV_Target {\n"
    "    return skyboxTex.Sample(skyboxSampler, normalize(input.dir));\n"
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
    "SamplerState postSampler : register(s0);\n"
    "struct VS_OUTPUT {\n"
    "    float4 pos : SV_POSITION;\n"
    "    float2 uv : TEXCOORD0;\n"
    "};\n"
    "VS_OUTPUT VSPostFX(uint vid : SV_VertexID) {\n"
    "    float2 pos = vid == 0 ? float2(-1.0, -1.0) : (vid == 1 ? float2(-1.0, 3.0) : float2(3.0, -1.0));\n"
    "    VS_OUTPUT output;\n"
    "    output.pos = float4(pos, 0.0, 1.0);\n"
    "    output.uv = float2((pos.x + 1.0) * 0.5, 1.0 - (pos.y + 1.0) * 0.5);\n"
    "    return output;\n"
    "}\n"
    "float luminance(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }\n"
    "float depthAt(float2 uv) { return depthTex.Sample(postSampler, uv).r; }\n"
    "float3 sceneAt(float2 uv) { return sceneTex.Sample(postSampler, uv).rgb; }\n"
    "float3 reconstructWorld(float2 uv, float depth) {\n"
    "    float4 clip = float4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);\n"
    "    float4 world = mul(clip, invViewProjection);\n"
    "    return world.xyz / max(world.w, 0.0001);\n"
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
    "    float4 prevClip = mul(float4(worldPos, 1.0), prevViewProjection);\n"
    "    float2 prevNdc = prevClip.xy / max(prevClip.w, 0.0001);\n"
    "    float2 currNdc = uv * 2.0 - 1.0;\n"
    "    return (currNdc - prevNdc) * 0.5;\n"
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
    "    float blur = saturate(abs(dist - dofFocusDistance) / max(dofAperture, 0.001)) * dofMaxBlur;\n"
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
    "    float3 bloom = float3(0.0, 0.0, 0.0);\n"
    "    float2 taps[4] = { float2(invResolution.x, 0.0), float2(-invResolution.x, 0.0),\n"
    "                       float2(0.0, invResolution.y), float2(0.0, -invResolution.y) };\n"
    "    for (int i = 0; i < 4; i++) {\n"
    "        float3 sampleColor = sceneAt(uv + taps[i]);\n"
    "        float l = luminance(sampleColor);\n"
    "        if (l > bloomThreshold)\n"
    "            bloom += sampleColor * (l - bloomThreshold);\n"
    "    }\n"
    "    return color + bloom * (bloomIntensity * 0.25);\n"
    "}\n"
    "float3 tonemap(float3 color) {\n"
    "    color *= tonemapExposure;\n"
    "    if (tonemapMode == 1)\n"
    "        return color / (1.0 + color);\n"
    "    if (tonemapMode == 2) {\n"
    "        float a = 2.51;\n"
    "        float b = 0.03;\n"
    "        float c = 2.43;\n"
    "        float d = 0.59;\n"
    "        float e = 0.14;\n"
    "        return saturate((color * (a * color + b)) / (color * (c * color + d) + e));\n"
    "    }\n"
    "    return color;\n"
    "}\n"
    "float3 applyColorGrade(float3 color) {\n"
    "    if (colorGradeEnabled == 0)\n"
    "        return color;\n"
    "    color = (color - 0.5) * cgContrast + 0.5;\n"
    "    color += cgBrightness;\n"
    "    float l = luminance(color);\n"
    "    return lerp(float3(l, l, l), color, cgSaturation);\n"
    "}\n"
    "float3 applyVignette(float2 uv, float3 color) {\n"
    "    if (vignetteEnabled == 0)\n"
    "        return color;\n"
    "    float2 centered = uv * 2.0 - 1.0;\n"
    "    float dist = length(centered);\n"
    "    float vig = saturate((vignetteRadius - dist) / max(vignetteSoftness, 0.0001));\n"
    "    vig = vig * vig * (3.0 - 2.0 * vig);\n"
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
    "    color *= computeSSAO(input.uv, depth);\n"
    "    color = applyMotionBlur(input.uv, depth, color);\n"
    "    color = applyDOF(input.uv, depth, color);\n"
    "    color = applyBloom(input.uv, color);\n"
    "    color = tonemap(color);\n"
    "    color = applyColorGrade(color);\n"
    "    color = applyVignette(input.uv, color);\n"
    "    color = applyFXAA(input.uv, color);\n"
    "    return float4(saturate(color), 1.0);\n"
    "}\n";

typedef struct {
    const void *pixels_ptr;
    uint64_t generation;
    ID3D11Texture2D *tex;
    ID3D11ShaderResourceView *srv;
} d3d_tex_cache_entry_t;

typedef struct {
    const void *cubemap_ptr;
    uint64_t generation;
    ID3D11Texture2D *tex;
    ID3D11ShaderResourceView *srv;
} d3d_cubemap_cache_entry_t;

typedef struct {
    ID3D11Texture2D *tex;
    ID3D11ShaderResourceView *srv;
    int temporary;
} d3d_temp_srv_t;

typedef struct {
    d3d_temp_srv_t textures[9];
    d3d_temp_srv_t cubemap;
    int has_texture;
    int has_normal_map;
    int has_specular_map;
    int has_emissive_map;
    int has_env_map;
    int has_splat;
} d3d_draw_resources_t;

typedef struct {
    float model[16];
    float prev_model[16];
    float normal[16];
    int32_t has_prev_model_matrix;
    int32_t has_skinning;
    int32_t has_prev_skinning;
    int32_t has_morph_normal_deltas;
    int32_t morph_shape_count;
    int32_t vertex_count;
    int32_t has_prev_morph_weights;
    int32_t has_prev_instance_matrices;
    float morph_weights[32];
    float prev_morph_weights[32];
} d3d_per_object_t;

typedef struct {
    float vp[16];
    float prev_vp[16];
    float shadow_vp[16];
    float camera_pos[4];
    float ambient[4];
    float fog_color[4];
    float fog_near;
    float fog_far;
    float shadow_bias;
    int32_t light_count;
    int32_t shadow_enabled;
    float _pad0;
    float _pad1;
    float _pad2;
} d3d_per_scene_t;

typedef struct {
    float diffuse[4];
    float specular[4];
    float emissive[4];
    float alpha;
    float reflectivity;
    int32_t has_texture;
    int32_t has_normal_map;
    int32_t has_specular_map;
    int32_t has_emissive_map;
    int32_t unlit;
    int32_t has_env_map;
    int32_t has_splat;
    int32_t shading_model;
    float custom_params[8];
    float splat_scales[4];
} d3d_per_material_t;

typedef struct {
    int32_t type;
    float _pad0;
    float _pad1;
    float _pad2;
    float direction[4];
    float position[4];
    float color[4];
    float intensity;
    float attenuation;
    float inner_cos;
    float outer_cos;
} d3d_light_t;

typedef struct {
    float view_rotation[16];
    float projection[16];
} d3d_skybox_cb_t;

typedef struct {
    float inv_vp[16];
    float prev_vp[16];
    float camera_pos[4];
    float inv_resolution[2];
    int32_t bloom_enabled;
    float bloom_threshold;
    float bloom_intensity;
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

typedef struct {
    float model[16];
    float normal[16];
    float prev_model[16];
} d3d_instance_data_t;

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

typedef struct {
    ID3D11Device *device;
    ID3D11DeviceContext *ctx;
    IDXGISwapChain *swap_chain;
    ID3D11RenderTargetView *rtv;
    ID3D11Texture2D *depth_tex;
    ID3D11DepthStencilView *dsv;

    ID3D11BlendState *blend_state;
    ID3D11DepthStencilState *depth_state;
    ID3D11DepthStencilState *depth_state_no_write;
    ID3D11DepthStencilState *depth_state_readonly_lequal;
    ID3D11RasterizerState *rs_solid_cull;
    ID3D11RasterizerState *rs_solid_no_cull;
    ID3D11RasterizerState *rs_wire_cull;
    ID3D11RasterizerState *rs_wire_no_cull;
    ID3D11SamplerState *linear_wrap_sampler;
    ID3D11SamplerState *linear_clamp_sampler;
    ID3D11SamplerState *shadow_cmp_sampler;

    ID3D11VertexShader *vs_main;
    ID3D11VertexShader *vs_instanced;
    ID3D11PixelShader *ps_main;
    ID3D11VertexShader *vs_shadow;
    ID3D11VertexShader *vs_skybox;
    ID3D11PixelShader *ps_skybox;
    ID3D11VertexShader *vs_postfx;
    ID3D11PixelShader *ps_postfx;

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

    ID3D11Buffer *morph_buffer;
    ID3D11ShaderResourceView *morph_srv;
    size_t morph_buffer_size;
    ID3D11Buffer *morph_normal_buffer;
    ID3D11ShaderResourceView *morph_normal_srv;
    size_t morph_normal_buffer_size;

    ID3D11Texture2D *scene_color_tex;
    ID3D11RenderTargetView *scene_color_rtv;
    ID3D11ShaderResourceView *scene_color_srv;
    ID3D11Texture2D *scene_motion_tex;
    ID3D11RenderTargetView *scene_motion_rtv;
    ID3D11ShaderResourceView *scene_motion_srv;
    ID3D11Texture2D *scene_depth_tex;
    ID3D11DepthStencilView *scene_dsv;
    ID3D11ShaderResourceView *scene_depth_srv;
    int32_t scene_width;
    int32_t scene_height;

    ID3D11Texture2D *rtt_color_tex;
    ID3D11RenderTargetView *rtt_rtv;
    ID3D11Texture2D *rtt_depth_tex;
    ID3D11DepthStencilView *rtt_dsv;
    ID3D11Texture2D *rtt_staging;
    int32_t rtt_width;
    int32_t rtt_height;
    int8_t rtt_active;
    vgfx3d_rendertarget_t *rtt_target;

    ID3D11Texture2D *shadow_depth_tex;
    ID3D11DepthStencilView *shadow_dsv;
    ID3D11ShaderResourceView *shadow_srv;
    int32_t shadow_width;
    int32_t shadow_height;
    int8_t shadow_active;
    float shadow_vp[16];
    float shadow_bias;

    d3d_tex_cache_entry_t tex_cache[64];
    int32_t tex_cache_count;
    d3d_cubemap_cache_entry_t cubemap_cache[16];
    int32_t cubemap_cache_count;
    d3d11_mesh_cache_entry_t mesh_cache[D3D11_MESH_CACHE_CAPACITY];
    uint64_t frame_serial;

    ID3D11RenderTargetView *current_rtvs[2];
    UINT current_rtv_count;
    ID3D11DepthStencilView *current_dsv;
    int32_t current_width;
    int32_t current_height;

    int32_t width;
    int32_t height;
    float view[16];
    float projection[16];
    float vp[16];
    float prev_vp[16];
    float inv_vp[16];
    int8_t prev_vp_valid;
    float cam_pos[3];
    float clear_r;
    float clear_g;
    float clear_b;
    int8_t fog_enabled;
    float fog_near;
    float fog_far;
    float fog_color[3];
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

static void d3d11_destroy_ctx(void *ctx_ptr);
static void d3d11_log_hresult(const char *msg, HRESULT hr);
static void d3d11_log_shader_error(const char *stage, ID3DBlob *err_blob);

static void mat4f_mul_d3d(const float *a, const float *b, float *out) {
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            out[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c] +
                             a[r * 4 + 2] * b[2 * 4 + c] + a[r * 4 + 3] * b[3 * 4 + c];
        }
    }
}

static void d3d11_log_hresult(const char *msg, HRESULT hr) {
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "[vgfx3d_d3d11] %s failed (hr=0x%08lx)\n", msg,
             (unsigned long)hr);
    OutputDebugStringA(buffer);
    fputs(buffer, stderr);
}

static void d3d11_log_shader_error(const char *stage, ID3DBlob *err_blob) {
    if (!err_blob)
        return;
    {
        const char *text = (const char *)ID3D10Blob_GetBufferPointer(err_blob);
        char buffer[1024];
        snprintf(buffer, sizeof(buffer), "[vgfx3d_d3d11] %s compile failed: %s\n", stage,
                 text ? text : "(no compiler output)");
        OutputDebugStringA(buffer);
        fputs(buffer, stderr);
    }
}

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

static HRESULT d3d11_create_rasterizer_state(d3d11_context_t *ctx,
                                             D3D11_FILL_MODE fill_mode,
                                             D3D11_CULL_MODE cull_mode,
                                             ID3D11RasterizerState **out_state) {
    D3D11_RASTERIZER_DESC desc;
    memset(&desc, 0, sizeof(desc));
    desc.FillMode = fill_mode;
    desc.CullMode = cull_mode;
    desc.FrontCounterClockwise = FALSE;
    desc.DepthClipEnable = TRUE;
    return ID3D11Device_CreateRasterizerState(ctx->device, &desc, out_state);
}

static ID3D11RasterizerState *d3d11_choose_rasterizer(d3d11_context_t *ctx,
                                                      int8_t wireframe,
                                                      int8_t backface_cull) {
    if (!ctx)
        return NULL;
    if (wireframe)
        return backface_cull ? ctx->rs_wire_cull : ctx->rs_wire_no_cull;
    return backface_cull ? ctx->rs_solid_cull : ctx->rs_solid_no_cull;
}

static UINT d3d11_constant_buffer_byte_width(size_t size) {
    return (UINT)((size + 15u) & ~15u);
}

static HRESULT d3d11_update_constant_buffer(d3d11_context_t *ctx,
                                            ID3D11Buffer *buffer,
                                            const void *data,
                                            size_t size) {
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr;

    if (!ctx || !buffer || !data || size == 0)
        return E_INVALIDARG;
    hr = ID3D11DeviceContext_Map(
        ctx->ctx, (ID3D11Resource *)buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr))
        return hr;
    memcpy(mapped.pData, data, size);
    ID3D11DeviceContext_Unmap(ctx->ctx, (ID3D11Resource *)buffer, 0);
    return S_OK;
}

static HRESULT d3d11_ensure_dynamic_buffer(d3d11_context_t *ctx,
                                           ID3D11Buffer **buffer,
                                           size_t *capacity,
                                           UINT bind_flags,
                                           size_t needed,
                                           size_t initial_size) {
    D3D11_BUFFER_DESC desc;
    size_t new_capacity;
    HRESULT hr;

    if (!ctx || !buffer || !capacity)
        return E_INVALIDARG;
    if (needed == 0)
        needed = 4;
    if (*buffer && *capacity >= needed)
        return S_OK;

    new_capacity = *capacity > 0 ? *capacity : initial_size;
    while (new_capacity < needed)
        new_capacity *= 2;
    if (new_capacity > UINT_MAX)
        return E_OUTOFMEMORY;

    SAFE_RELEASE(*buffer);
    memset(&desc, 0, sizeof(desc));
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.ByteWidth = (UINT)new_capacity;
    desc.BindFlags = bind_flags;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = ID3D11Device_CreateBuffer(ctx->device, &desc, NULL, buffer);
    if (SUCCEEDED(hr))
        *capacity = new_capacity;
    return hr;
}

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
    memcpy(mapped.pData, data, bytes);
    ID3D11DeviceContext_Unmap(ctx->ctx, (ID3D11Resource *)*buffer, 0);
    return 1;
}

static void d3d11_release_mesh_cache_entry(d3d11_mesh_cache_entry_t *entry) {
    if (!entry)
        return;
    SAFE_RELEASE(entry->vb);
    SAFE_RELEASE(entry->ib);
    memset(entry, 0, sizeof(*entry));
}

static void d3d11_release_mesh_cache(d3d11_context_t *ctx) {
    if (!ctx)
        return;
    for (int32_t i = 0; i < D3D11_MESH_CACHE_CAPACITY; i++)
        d3d11_release_mesh_cache_entry(&ctx->mesh_cache[i]);
}

static HRESULT d3d11_create_static_buffer(d3d11_context_t *ctx,
                                          UINT bind_flags,
                                          const void *data,
                                          size_t bytes,
                                          ID3D11Buffer **out_buffer) {
    D3D11_BUFFER_DESC desc;
    D3D11_SUBRESOURCE_DATA init;

    if (!ctx || !data || bytes == 0 || !out_buffer || bytes > UINT_MAX)
        return E_INVALIDARG;
    memset(&desc, 0, sizeof(desc));
    memset(&init, 0, sizeof(init));
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.ByteWidth = (UINT)bytes;
    desc.BindFlags = bind_flags;
    init.pSysMem = data;
    return ID3D11Device_CreateBuffer(ctx->device, &desc, &init, out_buffer);
}

static int d3d11_acquire_mesh_buffers(d3d11_context_t *ctx,
                                      const vgfx3d_draw_cmd_t *cmd,
                                      ID3D11Buffer **out_vb,
                                      ID3D11Buffer **out_ib) {
    size_t vertex_bytes;
    size_t index_bytes;

    if (!ctx || !cmd || !out_vb || !out_ib || !cmd->vertices || !cmd->indices ||
        cmd->vertex_count == 0 || cmd->index_count == 0)
        return 0;

    vertex_bytes = (size_t)cmd->vertex_count * sizeof(vgfx3d_vertex_t);
    index_bytes = (size_t)cmd->index_count * sizeof(uint32_t);
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

static HRESULT d3d11_ensure_float_srv_buffer(d3d11_context_t *ctx,
                                             ID3D11Buffer **buffer,
                                             ID3D11ShaderResourceView **srv,
                                             size_t *capacity,
                                             size_t element_count) {
    D3D11_BUFFER_DESC desc;
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
    size_t bytes;
    HRESULT hr;

    if (!ctx || !buffer || !srv || !capacity)
        return E_INVALIDARG;
    if (element_count == 0)
        return S_OK;
    if (*buffer && *capacity >= element_count)
        return S_OK;

    bytes = element_count * sizeof(float);
    if (bytes > UINT_MAX)
        return E_OUTOFMEMORY;

    SAFE_RELEASE(*srv);
    SAFE_RELEASE(*buffer);

    memset(&desc, 0, sizeof(desc));
    desc.ByteWidth = (UINT)bytes;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    hr = ID3D11Device_CreateBuffer(ctx->device, &desc, NULL, buffer);
    if (FAILED(hr))
        return hr;

    memset(&srv_desc, 0, sizeof(srv_desc));
    srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srv_desc.Buffer.FirstElement = 0;
    srv_desc.Buffer.NumElements = (UINT)element_count;
    hr = ID3D11Device_CreateShaderResourceView(
        ctx->device, (ID3D11Resource *)*buffer, &srv_desc, srv);
    if (FAILED(hr)) {
        SAFE_RELEASE(*buffer);
        SAFE_RELEASE(*srv);
        return hr;
    }
    *capacity = element_count;
    return S_OK;
}

static HRESULT d3d11_update_float_srv_buffer(d3d11_context_t *ctx,
                                             ID3D11Buffer **buffer,
                                             ID3D11ShaderResourceView **srv,
                                             size_t *capacity,
                                             const float *data,
                                             size_t element_count) {
    HRESULT hr;

    if (!ctx || !data || element_count == 0)
        return E_INVALIDARG;
    hr = d3d11_ensure_float_srv_buffer(ctx, buffer, srv, capacity, element_count);
    if (FAILED(hr))
        return hr;
    ID3D11DeviceContext_UpdateSubresource(ctx->ctx, (ID3D11Resource *)*buffer, 0, NULL, data, 0, 0);
    return S_OK;
}

static void d3d11_release_texture_cache(d3d11_context_t *ctx) {
    if (!ctx)
        return;
    for (int32_t i = 0; i < ctx->tex_cache_count; i++) {
        SAFE_RELEASE(ctx->tex_cache[i].srv);
        SAFE_RELEASE(ctx->tex_cache[i].tex);
        ctx->tex_cache[i].pixels_ptr = NULL;
    }
    ctx->tex_cache_count = 0;
}

static void d3d11_release_cubemap_cache(d3d11_context_t *ctx) {
    if (!ctx)
        return;
    for (int32_t i = 0; i < ctx->cubemap_cache_count; i++) {
        SAFE_RELEASE(ctx->cubemap_cache[i].srv);
        SAFE_RELEASE(ctx->cubemap_cache[i].tex);
        ctx->cubemap_cache[i].cubemap_ptr = NULL;
    }
    ctx->cubemap_cache_count = 0;
}

static void d3d11_release_temp_srv(d3d_temp_srv_t *entry) {
    if (!entry || !entry->temporary)
        return;
    SAFE_RELEASE(entry->srv);
    SAFE_RELEASE(entry->tex);
    entry->temporary = 0;
}

static HRESULT d3d11_create_texture_srv(d3d11_context_t *ctx,
                                        const void *pixels,
                                        ID3D11Texture2D **out_tex,
                                        ID3D11ShaderResourceView **out_srv) {
    int32_t w = 0;
    int32_t h = 0;
    uint8_t *rgba = NULL;
    D3D11_TEXTURE2D_DESC desc;
    D3D11_SUBRESOURCE_DATA init;
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
    HRESULT hr;

    if (!ctx || !pixels || !out_tex || !out_srv)
        return E_INVALIDARG;
    if (vgfx3d_unpack_pixels_rgba(pixels, &w, &h, &rgba) != 0)
        return E_FAIL;

    memset(&desc, 0, sizeof(desc));
    desc.Width = (UINT)w;
    desc.Height = (UINT)h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    memset(&init, 0, sizeof(init));
    init.pSysMem = rgba;
    init.SysMemPitch = (UINT)(w * 4);

    hr = ID3D11Device_CreateTexture2D(ctx->device, &desc, &init, out_tex);
    if (SUCCEEDED(hr)) {
        memset(&srv_desc, 0, sizeof(srv_desc));
        srv_desc.Format = desc.Format;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels = 1;
        hr = ID3D11Device_CreateShaderResourceView(
            ctx->device, (ID3D11Resource *)*out_tex, &srv_desc, out_srv);
    }
    free(rgba);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateTexture2D/ShaderResourceView(texture)", hr);
        SAFE_RELEASE(*out_srv);
        SAFE_RELEASE(*out_tex);
    }
    return hr;
}

static ID3D11ShaderResourceView *d3d11_get_or_create_srv(d3d11_context_t *ctx,
                                                         const void *pixels,
                                                         d3d_temp_srv_t *out_temp) {
    ID3D11Texture2D *tex = NULL;
    ID3D11ShaderResourceView *srv = NULL;
    uint64_t generation;

    if (out_temp)
        memset(out_temp, 0, sizeof(*out_temp));
    if (!ctx || !pixels)
        return NULL;
    generation = vgfx3d_get_pixels_generation(pixels);

    for (int32_t i = 0; i < ctx->tex_cache_count; i++) {
        if (ctx->tex_cache[i].pixels_ptr == pixels &&
            ctx->tex_cache[i].generation == generation)
            return ctx->tex_cache[i].srv;
    }

    for (int32_t i = 0; i < ctx->tex_cache_count; i++) {
        if (ctx->tex_cache[i].pixels_ptr == pixels) {
            SAFE_RELEASE(ctx->tex_cache[i].srv);
            SAFE_RELEASE(ctx->tex_cache[i].tex);
            if (FAILED(d3d11_create_texture_srv(ctx, pixels, &ctx->tex_cache[i].tex, &ctx->tex_cache[i].srv)))
                return NULL;
            ctx->tex_cache[i].generation = generation;
            return ctx->tex_cache[i].srv;
        }
    }

    if (FAILED(d3d11_create_texture_srv(ctx, pixels, &tex, &srv)))
        return NULL;

    if (ctx->tex_cache_count < (int32_t)(sizeof(ctx->tex_cache) / sizeof(ctx->tex_cache[0]))) {
        ctx->tex_cache[ctx->tex_cache_count].pixels_ptr = pixels;
        ctx->tex_cache[ctx->tex_cache_count].generation = generation;
        ctx->tex_cache[ctx->tex_cache_count].tex = tex;
        ctx->tex_cache[ctx->tex_cache_count].srv = srv;
        ctx->tex_cache_count++;
        return srv;
    }

    if (out_temp) {
        out_temp->tex = tex;
        out_temp->srv = srv;
        out_temp->temporary = 1;
    }
    return srv;
}

static HRESULT d3d11_create_cubemap_srv(d3d11_context_t *ctx,
                                        const rt_cubemap3d *cubemap,
                                        ID3D11Texture2D **out_tex,
                                        ID3D11ShaderResourceView **out_srv) {
    uint8_t *faces[6];
    int32_t face_size = 0;
    D3D11_TEXTURE2D_DESC desc;
    D3D11_SUBRESOURCE_DATA init[6];
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
    HRESULT hr;

    if (!ctx || !cubemap || !out_tex || !out_srv)
        return E_INVALIDARG;
    if (vgfx3d_unpack_cubemap_faces_rgba(cubemap, &face_size, faces) != 0)
        return E_FAIL;

    memset(&desc, 0, sizeof(desc));
    desc.Width = (UINT)face_size;
    desc.Height = (UINT)face_size;
    desc.MipLevels = 1;
    desc.ArraySize = 6;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

    memset(init, 0, sizeof(init));
    for (int i = 0; i < 6; i++) {
        init[i].pSysMem = faces[i];
        init[i].SysMemPitch = (UINT)(face_size * 4);
    }

    hr = ID3D11Device_CreateTexture2D(ctx->device, &desc, init, out_tex);
    if (SUCCEEDED(hr)) {
        memset(&srv_desc, 0, sizeof(srv_desc));
        srv_desc.Format = desc.Format;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        srv_desc.TextureCube.MipLevels = 1;
        hr = ID3D11Device_CreateShaderResourceView(
            ctx->device, (ID3D11Resource *)*out_tex, &srv_desc, out_srv);
    }
    for (int i = 0; i < 6; i++)
        free(faces[i]);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateTexture2D/ShaderResourceView(cubemap)", hr);
        SAFE_RELEASE(*out_tex);
        SAFE_RELEASE(*out_srv);
    }
    return hr;
}

static ID3D11ShaderResourceView *d3d11_get_or_create_cubemap_srv(d3d11_context_t *ctx,
                                                                 const rt_cubemap3d *cubemap,
                                                                 d3d_temp_srv_t *out_temp) {
    ID3D11Texture2D *tex = NULL;
    ID3D11ShaderResourceView *srv = NULL;
    uint64_t generation;

    if (out_temp)
        memset(out_temp, 0, sizeof(*out_temp));
    if (!ctx || !cubemap)
        return NULL;
    generation = vgfx3d_get_cubemap_generation(cubemap);

    for (int32_t i = 0; i < ctx->cubemap_cache_count; i++) {
        if (ctx->cubemap_cache[i].cubemap_ptr == cubemap &&
            ctx->cubemap_cache[i].generation == generation)
            return ctx->cubemap_cache[i].srv;
    }

    for (int32_t i = 0; i < ctx->cubemap_cache_count; i++) {
        if (ctx->cubemap_cache[i].cubemap_ptr == cubemap) {
            SAFE_RELEASE(ctx->cubemap_cache[i].srv);
            SAFE_RELEASE(ctx->cubemap_cache[i].tex);
            if (FAILED(d3d11_create_cubemap_srv(ctx,
                                                cubemap,
                                                &ctx->cubemap_cache[i].tex,
                                                &ctx->cubemap_cache[i].srv)))
                return NULL;
            ctx->cubemap_cache[i].generation = generation;
            return ctx->cubemap_cache[i].srv;
        }
    }

    if (FAILED(d3d11_create_cubemap_srv(ctx, cubemap, &tex, &srv)))
        return NULL;

    if (ctx->cubemap_cache_count <
        (int32_t)(sizeof(ctx->cubemap_cache) / sizeof(ctx->cubemap_cache[0]))) {
        ctx->cubemap_cache[ctx->cubemap_cache_count].cubemap_ptr = cubemap;
        ctx->cubemap_cache[ctx->cubemap_cache_count].generation = generation;
        ctx->cubemap_cache[ctx->cubemap_cache_count].tex = tex;
        ctx->cubemap_cache[ctx->cubemap_cache_count].srv = srv;
        ctx->cubemap_cache_count++;
        return srv;
    }

    if (out_temp) {
        out_temp->tex = tex;
        out_temp->srv = srv;
        out_temp->temporary = 1;
    }
    return srv;
}

static HRESULT d3d11_create_color_target(d3d11_context_t *ctx,
                                         int32_t width,
                                         int32_t height,
                                         ID3D11Texture2D **out_tex,
                                         ID3D11RenderTargetView **out_rtv,
                                         ID3D11ShaderResourceView **out_srv) {
    D3D11_TEXTURE2D_DESC desc;
    D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
    HRESULT hr;

    if (!ctx || width <= 0 || height <= 0 || !out_tex || !out_rtv)
        return E_INVALIDARG;

    memset(&desc, 0, sizeof(desc));
    desc.Width = (UINT)width;
    desc.Height = (UINT)height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
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

    if (!ctx || width <= 0 || height <= 0 || !out_tex || !out_dsv)
        return E_INVALIDARG;

    memset(&desc, 0, sizeof(desc));
    desc.Width = (UINT)width;
    desc.Height = (UINT)height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = shader_readable ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL |
                     (shader_readable && out_srv ? D3D11_BIND_SHADER_RESOURCE : 0);

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

static HRESULT d3d11_create_staging_texture(d3d11_context_t *ctx,
                                            int32_t width,
                                            int32_t height,
                                            ID3D11Texture2D **out_tex) {
    D3D11_TEXTURE2D_DESC desc;

    if (!ctx || width <= 0 || height <= 0 || !out_tex)
        return E_INVALIDARG;

    memset(&desc, 0, sizeof(desc));
    desc.Width = (UINT)width;
    desc.Height = (UINT)height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    return ID3D11Device_CreateTexture2D(ctx->device, &desc, NULL, out_tex);
}

static void d3d11_destroy_scene_targets(d3d11_context_t *ctx) {
    if (!ctx)
        return;
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
}

static HRESULT d3d11_ensure_scene_targets(d3d11_context_t *ctx, int32_t width, int32_t height) {
    HRESULT hr;

    if (!ctx || width <= 0 || height <= 0)
        return E_INVALIDARG;
    if (ctx->scene_color_rtv && ctx->scene_width == width && ctx->scene_height == height)
        return S_OK;

    d3d11_destroy_scene_targets(ctx);

    hr = d3d11_create_color_target(
        ctx, width, height, &ctx->scene_color_tex, &ctx->scene_color_rtv, &ctx->scene_color_srv);
    if (FAILED(hr))
        return hr;
    hr = d3d11_create_color_target(
        ctx, width, height, &ctx->scene_motion_tex, &ctx->scene_motion_rtv, &ctx->scene_motion_srv);
    if (FAILED(hr)) {
        d3d11_destroy_scene_targets(ctx);
        return hr;
    }
    hr = d3d11_create_depth_target(ctx,
                                   width,
                                   height,
                                   1,
                                   &ctx->scene_depth_tex,
                                   &ctx->scene_dsv,
                                   &ctx->scene_depth_srv);
    if (FAILED(hr)) {
        d3d11_destroy_scene_targets(ctx);
        return hr;
    }

    ctx->scene_width = width;
    ctx->scene_height = height;
    return S_OK;
}

static void d3d11_destroy_rtt_targets(d3d11_context_t *ctx) {
    if (!ctx)
        return;
    SAFE_RELEASE(ctx->rtt_staging);
    SAFE_RELEASE(ctx->rtt_dsv);
    SAFE_RELEASE(ctx->rtt_depth_tex);
    SAFE_RELEASE(ctx->rtt_rtv);
    SAFE_RELEASE(ctx->rtt_color_tex);
    ctx->rtt_width = 0;
    ctx->rtt_height = 0;
    ctx->rtt_active = 0;
    ctx->rtt_target = NULL;
}

static HRESULT d3d11_ensure_rtt_targets(d3d11_context_t *ctx, vgfx3d_rendertarget_t *rt) {
    HRESULT hr;

    if (!ctx || !rt)
        return E_INVALIDARG;
    if (ctx->rtt_color_tex && ctx->rtt_width == rt->width && ctx->rtt_height == rt->height) {
        ctx->rtt_active = 1;
        ctx->rtt_target = rt;
        return S_OK;
    }

    d3d11_destroy_rtt_targets(ctx);

    hr = d3d11_create_color_target(ctx, rt->width, rt->height, &ctx->rtt_color_tex, &ctx->rtt_rtv, NULL);
    if (FAILED(hr))
        return hr;
    hr = d3d11_create_depth_target(
        ctx, rt->width, rt->height, 0, &ctx->rtt_depth_tex, &ctx->rtt_dsv, NULL);
    if (FAILED(hr)) {
        d3d11_destroy_rtt_targets(ctx);
        return hr;
    }
    hr = d3d11_create_staging_texture(ctx, rt->width, rt->height, &ctx->rtt_staging);
    if (FAILED(hr)) {
        d3d11_destroy_rtt_targets(ctx);
        return hr;
    }

    ctx->rtt_width = rt->width;
    ctx->rtt_height = rt->height;
    ctx->rtt_active = 1;
    ctx->rtt_target = rt;
    return S_OK;
}

static void d3d11_destroy_shadow_targets(d3d11_context_t *ctx) {
    if (!ctx)
        return;
    SAFE_RELEASE(ctx->shadow_srv);
    SAFE_RELEASE(ctx->shadow_dsv);
    SAFE_RELEASE(ctx->shadow_depth_tex);
    ctx->shadow_width = 0;
    ctx->shadow_height = 0;
}

static HRESULT d3d11_ensure_shadow_targets(d3d11_context_t *ctx, int32_t width, int32_t height) {
    HRESULT hr;

    if (!ctx || width <= 0 || height <= 0)
        return E_INVALIDARG;
    if (ctx->shadow_dsv && ctx->shadow_width == width && ctx->shadow_height == height)
        return S_OK;

    d3d11_destroy_shadow_targets(ctx);
    hr = d3d11_create_depth_target(ctx,
                                   width,
                                   height,
                                   1,
                                   &ctx->shadow_depth_tex,
                                   &ctx->shadow_dsv,
                                   &ctx->shadow_srv);
    if (FAILED(hr))
        return hr;
    ctx->shadow_width = width;
    ctx->shadow_height = height;
    return S_OK;
}

static void d3d11_bind_render_targets(d3d11_context_t *ctx) {
    D3D11_VIEWPORT viewport;

    if (!ctx)
        return;
    ID3D11DeviceContext_OMSetRenderTargets(ctx->ctx, ctx->current_rtv_count, ctx->current_rtvs, ctx->current_dsv);

    memset(&viewport, 0, sizeof(viewport));
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = (FLOAT)ctx->current_width;
    viewport.Height = (FLOAT)ctx->current_height;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    ID3D11DeviceContext_RSSetViewports(ctx->ctx, 1, &viewport);
}

static void d3d11_select_scene_targets(d3d11_context_t *ctx) {
    if (!ctx)
        return;
    if (ctx->rtt_active && ctx->rtt_rtv && ctx->rtt_dsv) {
        ctx->current_rtvs[0] = ctx->rtt_rtv;
        ctx->current_rtvs[1] = NULL;
        ctx->current_rtv_count = 1;
        ctx->current_dsv = ctx->rtt_dsv;
        ctx->current_width = ctx->rtt_width;
        ctx->current_height = ctx->rtt_height;
    } else if (ctx->scene_color_rtv && ctx->scene_motion_rtv && ctx->scene_dsv) {
        ctx->current_rtvs[0] = ctx->scene_color_rtv;
        ctx->current_rtvs[1] = ctx->scene_motion_rtv;
        ctx->current_rtv_count = 2;
        ctx->current_dsv = ctx->scene_dsv;
        ctx->current_width = ctx->scene_width;
        ctx->current_height = ctx->scene_height;
    } else {
        ctx->current_rtvs[0] = ctx->rtv;
        ctx->current_rtvs[1] = NULL;
        ctx->current_rtv_count = 1;
        ctx->current_dsv = ctx->dsv;
        ctx->current_width = ctx->width;
        ctx->current_height = ctx->height;
    }
    d3d11_bind_render_targets(ctx);
}

static void d3d11_bind_swapchain_target(d3d11_context_t *ctx) {
    D3D11_VIEWPORT viewport;

    if (!ctx)
        return;
    ID3D11DeviceContext_OMSetRenderTargets(ctx->ctx, 1, &ctx->rtv, NULL);
    memset(&viewport, 0, sizeof(viewport));
    viewport.Width = (FLOAT)ctx->width;
    viewport.Height = (FLOAT)ctx->height;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    ID3D11DeviceContext_RSSetViewports(ctx->ctx, 1, &viewport);
}

static void d3d11_clear_current_targets(d3d11_context_t *ctx,
                                        int8_t load_existing_color,
                                        int8_t load_existing_depth) {
    float clear_color[4];
    float motion_clear[4] = {0.5f, 0.5f, 0.0f, 1.0f};

    if (!ctx)
        return;

    clear_color[0] = ctx->clear_r;
    clear_color[1] = ctx->clear_g;
    clear_color[2] = ctx->clear_b;
    clear_color[3] = 1.0f;

    if (!load_existing_color && ctx->current_rtv_count > 0 && ctx->current_rtvs[0])
        ID3D11DeviceContext_ClearRenderTargetView(ctx->ctx, ctx->current_rtvs[0], clear_color);
    if (!load_existing_color && ctx->current_rtv_count > 1 && ctx->current_rtvs[1])
        ID3D11DeviceContext_ClearRenderTargetView(ctx->ctx, ctx->current_rtvs[1], motion_clear);
    if (!load_existing_depth && ctx->current_dsv)
        ID3D11DeviceContext_ClearDepthStencilView(ctx->ctx, ctx->current_dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
}

static void d3d11_bind_common_state(d3d11_context_t *ctx) {
    float blend_factor[4] = {0, 0, 0, 0};

    if (!ctx)
        return;
    ID3D11DeviceContext_OMSetBlendState(ctx->ctx, ctx->blend_state, blend_factor, 0xFFFFFFFF);
    if (ctx->linear_wrap_sampler)
        ID3D11DeviceContext_PSSetSamplers(ctx->ctx, 0, 1, &ctx->linear_wrap_sampler);
    if (ctx->shadow_cmp_sampler)
        ID3D11DeviceContext_PSSetSamplers(ctx->ctx, 1, 1, &ctx->shadow_cmp_sampler);
    if (ctx->linear_clamp_sampler)
        ID3D11DeviceContext_PSSetSamplers(ctx->ctx, 2, 1, &ctx->linear_clamp_sampler);
}

static void d3d11_prepare_object_data(const vgfx3d_draw_cmd_t *cmd,
                                      d3d_per_object_t *object_data) {
    int morph_count = 0;

    memset(object_data, 0, sizeof(*object_data));
    memcpy(object_data->model, cmd->model_matrix, sizeof(object_data->model));
    memcpy(object_data->prev_model,
           cmd->has_prev_model_matrix ? cmd->prev_model_matrix : cmd->model_matrix,
           sizeof(object_data->prev_model));
    vgfx3d_compute_normal_matrix4(cmd->model_matrix, object_data->normal);

    object_data->has_prev_model_matrix = cmd->has_prev_model_matrix ? 1 : 0;
    object_data->has_skinning = (cmd->bone_palette && cmd->bone_count > 0 && cmd->bone_count <= 128) ? 1 : 0;
    object_data->has_prev_skinning =
        (object_data->has_skinning && cmd->prev_bone_palette != NULL) ? 1 : 0;
    object_data->has_prev_instance_matrices = cmd->has_prev_instance_matrices ? 1 : 0;

    if (cmd->morph_deltas && cmd->morph_weights && cmd->morph_shape_count > 0) {
        morph_count = cmd->morph_shape_count;
        if (morph_count > 32)
            morph_count = 32;
    }
    object_data->morph_shape_count = morph_count;
    object_data->vertex_count = morph_count > 0 ? (int32_t)cmd->vertex_count : 0;
    object_data->has_prev_morph_weights = (morph_count > 0 && cmd->prev_morph_weights != NULL) ? 1 : 0;
    object_data->has_morph_normal_deltas =
        (morph_count > 0 && cmd->morph_normal_deltas != NULL) ? 1 : 0;
    if (morph_count > 0) {
        memcpy(object_data->morph_weights, cmd->morph_weights, (size_t)morph_count * sizeof(float));
        if (cmd->prev_morph_weights)
            memcpy(object_data->prev_morph_weights,
                   cmd->prev_morph_weights,
                   (size_t)morph_count * sizeof(float));
        else
            memcpy(object_data->prev_morph_weights,
                   cmd->morph_weights,
                   (size_t)morph_count * sizeof(float));
    }
}

static void d3d11_prepare_scene_data(d3d11_context_t *ctx,
                                     const vgfx3d_light_params_t *lights,
                                     int32_t light_count,
                                     const float *ambient,
                                     d3d_per_scene_t *scene_data) {
    memset(scene_data, 0, sizeof(*scene_data));
    memcpy(scene_data->vp, ctx->vp, sizeof(scene_data->vp));
    memcpy(scene_data->prev_vp, ctx->prev_vp_valid ? ctx->prev_vp : ctx->vp, sizeof(scene_data->prev_vp));
    memcpy(scene_data->shadow_vp, ctx->shadow_vp, sizeof(scene_data->shadow_vp));
    memcpy(scene_data->camera_pos, ctx->cam_pos, sizeof(float) * 3);
    scene_data->camera_pos[3] = 1.0f;
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
    scene_data->light_count = lights ? (light_count < 0 ? 0 : (light_count > 8 ? 8 : light_count)) : 0;
    scene_data->shadow_enabled = (ctx->shadow_active && ctx->shadow_srv) ? 1 : 0;
}

static void d3d11_prepare_material_data(const vgfx3d_draw_cmd_t *cmd,
                                        int has_texture,
                                        int has_normal_map,
                                        int has_specular_map,
                                        int has_emissive_map,
                                        int has_env_map,
                                        int has_splat,
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
    material_data->alpha = cmd->alpha;
    material_data->reflectivity = cmd->reflectivity;
    material_data->has_texture = has_texture;
    material_data->has_normal_map = has_normal_map;
    material_data->has_specular_map = has_specular_map;
    material_data->has_emissive_map = has_emissive_map;
    material_data->unlit = cmd->unlit;
    material_data->has_env_map = has_env_map;
    material_data->has_splat = has_splat;
    material_data->shading_model = cmd->shading_model;
    memcpy(material_data->custom_params, cmd->custom_params, sizeof(material_data->custom_params));
    memcpy(material_data->splat_scales, cmd->splat_layer_scales, sizeof(material_data->splat_scales));
}

static void d3d11_prepare_light_data(const vgfx3d_light_params_t *lights,
                                     int32_t light_count,
                                     d3d_light_t *light_data) {
    memset(light_data, 0, sizeof(d3d_light_t) * 8);
    for (int32_t i = 0; i < light_count && i < 8; i++) {
        light_data[i].type = lights[i].type;
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
    }
}

static int d3d11_prepare_anim_resources(d3d11_context_t *ctx,
                                        const vgfx3d_draw_cmd_t *cmd,
                                        d3d_per_object_t *object_data) {
    static const float k_zero_bones[128 * 16] = {0};
    HRESULT hr;
    size_t morph_count;

    if (!ctx || !cmd || !object_data)
        return 0;

    hr = d3d11_update_constant_buffer(ctx,
                                      ctx->cb_bones,
                                      object_data->has_skinning ? cmd->bone_palette : k_zero_bones,
                                      sizeof(k_zero_bones));
    if (FAILED(hr))
        d3d11_log_hresult("Map(cbBones)", hr);
    hr = d3d11_update_constant_buffer(
        ctx,
        ctx->cb_prev_bones,
        object_data->has_prev_skinning ? cmd->prev_bone_palette :
                                         (object_data->has_skinning ? cmd->bone_palette : k_zero_bones),
        sizeof(k_zero_bones));
    if (FAILED(hr))
        d3d11_log_hresult("Map(cbPrevBones)", hr);

    if (object_data->morph_shape_count <= 0 || !cmd->morph_deltas || !cmd->morph_weights) {
        object_data->morph_shape_count = 0;
        object_data->vertex_count = 0;
        object_data->has_prev_morph_weights = 0;
        object_data->has_morph_normal_deltas = 0;
        return 0;
    }

    morph_count = (size_t)object_data->morph_shape_count * (size_t)cmd->vertex_count * 3u;
    hr = d3d11_update_float_srv_buffer(
        ctx, &ctx->morph_buffer, &ctx->morph_srv, &ctx->morph_buffer_size, cmd->morph_deltas, morph_count);
    if (FAILED(hr)) {
        d3d11_log_hresult("Create/UpdateBuffer(morphDeltas)", hr);
        object_data->morph_shape_count = 0;
        object_data->vertex_count = 0;
        object_data->has_prev_morph_weights = 0;
        object_data->has_morph_normal_deltas = 0;
        return 0;
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
            object_data->has_morph_normal_deltas = 0;
        }
    }

    return 1;
}

static void d3d11_bind_main_pipeline(d3d11_context_t *ctx,
                                     const vgfx3d_draw_cmd_t *cmd,
                                     int8_t wireframe,
                                     int8_t backface_cull,
                                     int instanced) {
    ID3D11RasterizerState *rasterizer;
    ID3D11ShaderResourceView *vs_srvs[2];

    rasterizer = d3d11_choose_rasterizer(ctx, wireframe, backface_cull);
    if (rasterizer)
        ID3D11DeviceContext_RSSetState(ctx->ctx, rasterizer);
    ID3D11DeviceContext_OMSetDepthStencilState(
        ctx->ctx, (cmd->alpha < 1.0f) ? ctx->depth_state_no_write : ctx->depth_state, 0);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx->ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_IASetInputLayout(
        ctx->ctx, instanced ? ctx->input_layout_instanced : ctx->input_layout);
    ID3D11DeviceContext_VSSetShader(ctx->ctx, instanced ? ctx->vs_instanced : ctx->vs_main, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx->ctx, ctx->ps_main, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx->ctx, 0, 1, &ctx->cb_per_object);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx->ctx, 1, 1, &ctx->cb_per_scene);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx->ctx, 4, 1, &ctx->cb_bones);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx->ctx, 5, 1, &ctx->cb_prev_bones);
    ID3D11DeviceContext_PSSetConstantBuffers(ctx->ctx, 1, 1, &ctx->cb_per_scene);
    ID3D11DeviceContext_PSSetConstantBuffers(ctx->ctx, 2, 1, &ctx->cb_per_material);
    ID3D11DeviceContext_PSSetConstantBuffers(ctx->ctx, 3, 1, &ctx->cb_per_lights);
    d3d11_bind_common_state(ctx);

    vs_srvs[0] = ctx->morph_srv;
    vs_srvs[1] = ctx->morph_normal_srv;
    ID3D11DeviceContext_VSSetShaderResources(ctx->ctx, 0, 2, vs_srvs);
}

static void d3d11_unbind_draw_resources(d3d11_context_t *ctx) {
    ID3D11ShaderResourceView *null_vs[2] = {NULL, NULL};
    ID3D11ShaderResourceView *null_ps[11] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
    if (!ctx)
        return;
    ID3D11DeviceContext_VSSetShaderResources(ctx->ctx, 0, 2, null_vs);
    ID3D11DeviceContext_PSSetShaderResources(ctx->ctx, 0, 11, null_ps);
}

static void d3d11_prepare_skybox_data(d3d11_context_t *ctx, d3d_skybox_cb_t *skybox_data) {
    memcpy(skybox_data->view_rotation, ctx->view, sizeof(skybox_data->view_rotation));
    memcpy(skybox_data->projection, ctx->projection, sizeof(skybox_data->projection));
    skybox_data->view_rotation[3] = 0.0f;
    skybox_data->view_rotation[7] = 0.0f;
    skybox_data->view_rotation[11] = 0.0f;
}

static void d3d11_prepare_postfx_data(d3d11_context_t *ctx,
                                      const vgfx3d_postfx_snapshot_t *snapshot,
                                      d3d_postfx_cb_t *postfx_data) {
    memset(postfx_data, 0, sizeof(*postfx_data));
    memcpy(postfx_data->inv_vp, ctx->inv_vp, sizeof(postfx_data->inv_vp));
    memcpy(postfx_data->prev_vp, ctx->prev_vp_valid ? ctx->prev_vp : ctx->vp, sizeof(postfx_data->prev_vp));
    memcpy(postfx_data->camera_pos, ctx->cam_pos, sizeof(float) * 3);
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

static int d3d11_bind_draw_resources(d3d11_context_t *ctx,
                                     const vgfx3d_draw_cmd_t *cmd,
                                     d3d_draw_resources_t *resources) {
    ID3D11ShaderResourceView *srvs[11];

    if (!ctx || !cmd || !resources)
        return 0;

    memset(resources, 0, sizeof(*resources));
    srvs[0] = d3d11_get_or_create_srv(ctx, cmd->texture, &resources->textures[0]);
    srvs[1] = d3d11_get_or_create_srv(ctx, cmd->normal_map, &resources->textures[1]);
    srvs[2] = d3d11_get_or_create_srv(ctx, cmd->specular_map, &resources->textures[2]);
    srvs[3] = d3d11_get_or_create_srv(ctx, cmd->emissive_map, &resources->textures[3]);
    srvs[4] = (ctx->shadow_active && ctx->shadow_srv) ? ctx->shadow_srv : NULL;
    srvs[5] = d3d11_get_or_create_srv(ctx, cmd->splat_map, &resources->textures[4]);
    srvs[6] = d3d11_get_or_create_srv(ctx, cmd->splat_layers[0], &resources->textures[5]);
    srvs[7] = d3d11_get_or_create_srv(ctx, cmd->splat_layers[1], &resources->textures[6]);
    srvs[8] = d3d11_get_or_create_srv(ctx, cmd->splat_layers[2], &resources->textures[7]);
    srvs[9] = d3d11_get_or_create_srv(ctx, cmd->splat_layers[3], &resources->textures[8]);
    srvs[10] = (cmd->reflectivity > 0.0001f)
                   ? d3d11_get_or_create_cubemap_srv(
                         ctx, (const rt_cubemap3d *)cmd->env_map, &resources->cubemap)
                   : NULL;

    resources->has_texture = srvs[0] != NULL;
    resources->has_normal_map = srvs[1] != NULL;
    resources->has_specular_map = srvs[2] != NULL;
    resources->has_emissive_map = srvs[3] != NULL;
    resources->has_env_map = srvs[10] != NULL;
    resources->has_splat = cmd->has_splat && srvs[5] != NULL;
    if (!resources->has_splat) {
        srvs[5] = NULL;
        srvs[6] = NULL;
        srvs[7] = NULL;
        srvs[8] = NULL;
        srvs[9] = NULL;
    }

    ID3D11DeviceContext_PSSetShaderResources(ctx->ctx, 0, 11, srvs);
    return resources->has_splat;
}

static void d3d11_release_temporary_resources(d3d_draw_resources_t *resources) {
    if (!resources)
        return;
    for (int i = 0; i < 9; i++)
        d3d11_release_temp_srv(&resources->textures[i]);
    d3d11_release_temp_srv(&resources->cubemap);
}

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
    d3d_light_t light_data[8];
    d3d_draw_resources_t draw_resources;
    int has_splat;
    HRESULT hr;
    UINT stride = sizeof(vgfx3d_vertex_t);
    UINT offset = 0;
    ID3D11Buffer *mesh_vb = NULL;
    ID3D11Buffer *mesh_ib = NULL;

    (void)win;
    if (!ctx || !cmd || !cmd->vertices || !cmd->indices || cmd->vertex_count == 0 || cmd->index_count == 0)
        return;

    d3d11_prepare_object_data(cmd, &object_data);
    d3d11_prepare_scene_data(ctx, lights, light_count, ambient, &scene_data);
    d3d11_prepare_light_data(lights, scene_data.light_count, light_data);
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
                                &material_data);
    hr = d3d11_update_constant_buffer(ctx, ctx->cb_per_material, &material_data, sizeof(material_data));
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
    d3d_light_t light_data[8];
    d3d_instance_data_t *instance_data = NULL;
    d3d_draw_resources_t draw_resources;
    int has_splat;
    HRESULT hr;
    UINT strides[2];
    UINT offsets[2] = {0, 0};
    ID3D11Buffer *vertex_buffers[2];
    ID3D11Buffer *mesh_vb = NULL;
    ID3D11Buffer *mesh_ib = NULL;

    (void)win;
    if (!ctx || !cmd || !instance_matrices || instance_count <= 0 || !cmd->vertices || !cmd->indices)
        return;

    instance_data = (d3d_instance_data_t *)malloc((size_t)instance_count * sizeof(d3d_instance_data_t));
    if (!instance_data)
        return;
    for (int32_t i = 0; i < instance_count; i++) {
        memcpy(instance_data[i].model, &instance_matrices[(size_t)i * 16u], sizeof(instance_data[i].model));
        vgfx3d_compute_normal_matrix4(&instance_matrices[(size_t)i * 16u], instance_data[i].normal);
        if (cmd->has_prev_instance_matrices && cmd->prev_instance_matrices)
            memcpy(instance_data[i].prev_model,
                   &cmd->prev_instance_matrices[(size_t)i * 16u],
                   sizeof(instance_data[i].prev_model));
        else
            memcpy(instance_data[i].prev_model, instance_data[i].model, sizeof(instance_data[i].prev_model));
    }

    d3d11_prepare_object_data(cmd, &object_data);
    object_data.has_prev_model_matrix = 0;
    object_data.has_prev_instance_matrices = cmd->has_prev_instance_matrices ? 1 : 0;
    d3d11_prepare_scene_data(ctx, lights, light_count, ambient, &scene_data);
    d3d11_prepare_light_data(lights, scene_data.light_count, light_data);
    d3d11_prepare_anim_resources(ctx, cmd, &object_data);

    hr = d3d11_update_constant_buffer(ctx, ctx->cb_per_object, &object_data, sizeof(object_data));
    if (FAILED(hr)) {
        d3d11_log_hresult("Map(cbPerObject)", hr);
        free(instance_data);
        return;
    }
    hr = d3d11_update_constant_buffer(ctx, ctx->cb_per_scene, &scene_data, sizeof(scene_data));
    if (FAILED(hr)) {
        d3d11_log_hresult("Map(cbPerScene)", hr);
        free(instance_data);
        return;
    }
    hr = d3d11_update_constant_buffer(ctx, ctx->cb_per_lights, light_data, sizeof(light_data));
    if (FAILED(hr)) {
        d3d11_log_hresult("Map(cbPerLights)", hr);
        free(instance_data);
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
                                &material_data);
    hr = d3d11_update_constant_buffer(ctx, ctx->cb_per_material, &material_data, sizeof(material_data));
    if (FAILED(hr)) {
        d3d11_log_hresult("Map(cbPerMaterial)", hr);
        d3d11_unbind_draw_resources(ctx);
        d3d11_release_temporary_resources(&draw_resources);
        free(instance_data);
        return;
    }

    if (!d3d11_acquire_mesh_buffers(ctx, cmd, &mesh_vb, &mesh_ib) ||
        !d3d11_upload_dynamic_buffer(ctx,
                                     &ctx->instance_buffer,
                                     &ctx->instance_buffer_size,
                                     D3D11_BIND_VERTEX_BUFFER,
                                     instance_data,
                                     (size_t)instance_count * sizeof(d3d_instance_data_t),
                                     D3D11_INITIAL_INSTANCE_BUFFER_SIZE)) {
        d3d11_unbind_draw_resources(ctx);
        d3d11_release_temporary_resources(&draw_resources);
        free(instance_data);
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
    free(instance_data);
}

static void *d3d11_create_ctx(vgfx_window_t win, int32_t width, int32_t height) {
    d3d11_context_t *ctx = NULL;
    HWND hwnd = (HWND)vgfx_get_native_view(win);
    DXGI_SWAP_CHAIN_DESC swap_desc;
    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
    D3D11_DEPTH_STENCIL_DESC depth_desc;
    D3D11_BLEND_DESC blend_desc;
    D3D11_SAMPLER_DESC sampler_desc;
    ID3DBlob *vs_blob = NULL;
    ID3DBlob *vs_instanced_blob = NULL;
    ID3DBlob *ps_blob = NULL;
    ID3DBlob *vs_shadow_blob = NULL;
    ID3DBlob *vs_skybox_blob = NULL;
    ID3DBlob *ps_skybox_blob = NULL;
    ID3DBlob *vs_postfx_blob = NULL;
    ID3DBlob *ps_postfx_blob = NULL;
    ID3D11Texture2D *back_buffer = NULL;
    D3D11_INPUT_ELEMENT_DESC layout[7];
    D3D11_INPUT_ELEMENT_DESC instanced_layout[19];
    D3D11_INPUT_ELEMENT_DESC skybox_layout[1];
    D3D11_BUFFER_DESC cb_desc;
    D3D11_BUFFER_DESC skybox_desc;
    static const float skybox_vertices[] = {
        -1.0f, -1.0f, -1.0f,  -1.0f, 1.0f,  -1.0f, 1.0f,  1.0f,  -1.0f,
        -1.0f, -1.0f, -1.0f,  1.0f,  1.0f,  -1.0f, 1.0f,  -1.0f, -1.0f,
        -1.0f, -1.0f, 1.0f,   1.0f,  1.0f,  1.0f,  -1.0f, 1.0f,  1.0f,
        -1.0f, -1.0f, 1.0f,   1.0f,  -1.0f, 1.0f,  1.0f,  1.0f,  1.0f,
        -1.0f, 1.0f,  -1.0f,  -1.0f, 1.0f,  1.0f,  1.0f,  1.0f,  1.0f,
        -1.0f, 1.0f,  -1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  -1.0f,
        -1.0f, -1.0f, -1.0f,  1.0f,  -1.0f, 1.0f,  -1.0f, -1.0f, 1.0f,
        -1.0f, -1.0f, -1.0f,  1.0f,  -1.0f, -1.0f, 1.0f,  -1.0f, 1.0f,
        -1.0f, -1.0f, -1.0f,  -1.0f, -1.0f, 1.0f,  -1.0f, 1.0f,  1.0f,
        -1.0f, -1.0f, -1.0f,  -1.0f, 1.0f,  1.0f,  -1.0f, 1.0f,  -1.0f,
        1.0f,  -1.0f, -1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  -1.0f, 1.0f,
        1.0f,  -1.0f, -1.0f,  1.0f,  1.0f,  -1.0f, 1.0f,  1.0f,  1.0f,
    };
    D3D11_SUBRESOURCE_DATA skybox_init;
    HRESULT hr;

    if (!hwnd)
        return NULL;

    ctx = (d3d11_context_t *)calloc(1, sizeof(d3d11_context_t));
    if (!ctx)
        return NULL;
    ctx->width = width;
    ctx->height = height;
    memcpy(ctx->prev_vp, k_identity4x4, sizeof(ctx->prev_vp));
    memcpy(ctx->inv_vp, k_identity4x4, sizeof(ctx->inv_vp));

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
    hr = ID3D11Device_CreateRenderTargetView(ctx->device, (ID3D11Resource *)back_buffer, NULL, &ctx->rtv);
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
    depth_desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    hr = ID3D11Device_CreateDepthStencilState(ctx->device, &depth_desc, &ctx->depth_state_readonly_lequal);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateDepthStencilState(skybox)", hr);
        goto fail;
    }

    memset(&blend_desc, 0, sizeof(blend_desc));
    blend_desc.RenderTarget[0].BlendEnable = TRUE;
    blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    blend_desc.RenderTarget[1] = blend_desc.RenderTarget[0];
    hr = ID3D11Device_CreateBlendState(ctx->device, &blend_desc, &ctx->blend_state);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateBlendState(alpha)", hr);
        goto fail;
    }

    hr = d3d11_create_rasterizer_state(ctx, D3D11_FILL_SOLID, D3D11_CULL_BACK, &ctx->rs_solid_cull);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateRasterizerState(solid+cull)", hr);
        goto fail;
    }
    hr = d3d11_create_rasterizer_state(ctx, D3D11_FILL_SOLID, D3D11_CULL_NONE, &ctx->rs_solid_no_cull);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateRasterizerState(solid+nocull)", hr);
        goto fail;
    }
    hr = d3d11_create_rasterizer_state(ctx, D3D11_FILL_WIREFRAME, D3D11_CULL_BACK, &ctx->rs_wire_cull);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateRasterizerState(wire+cull)", hr);
        goto fail;
    }
    hr = d3d11_create_rasterizer_state(ctx, D3D11_FILL_WIREFRAME, D3D11_CULL_NONE, &ctx->rs_wire_no_cull);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateRasterizerState(wire+nocull)", hr);
        goto fail;
    }

    memset(&sampler_desc, 0, sizeof(sampler_desc));
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
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
    memset(&sampler_desc, 0, sizeof(sampler_desc));
    sampler_desc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
    sampler_desc.BorderColor[0] = 1.0f;
    sampler_desc.BorderColor[1] = 1.0f;
    sampler_desc.BorderColor[2] = 1.0f;
    sampler_desc.BorderColor[3] = 1.0f;
    sampler_desc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
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

    layout[0].SemanticName = "POSITION";
    layout[0].SemanticIndex = 0;
    layout[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    layout[0].InputSlot = 0;
    layout[0].AlignedByteOffset = 0;
    layout[0].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
    layout[0].InstanceDataStepRate = 0;
    layout[1] = layout[0];
    layout[1].SemanticName = "NORMAL";
    layout[1].AlignedByteOffset = 12;
    layout[2] = layout[0];
    layout[2].SemanticName = "TEXCOORD";
    layout[2].Format = DXGI_FORMAT_R32G32_FLOAT;
    layout[2].AlignedByteOffset = 24;
    layout[3] = layout[0];
    layout[3].SemanticName = "COLOR";
    layout[3].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    layout[3].AlignedByteOffset = 32;
    layout[4] = layout[0];
    layout[4].SemanticName = "TANGENT";
    layout[4].AlignedByteOffset = 48;
    layout[5] = layout[0];
    layout[5].SemanticName = "BLENDINDICES";
    layout[5].Format = DXGI_FORMAT_R8G8B8A8_UINT;
    layout[5].AlignedByteOffset = 60;
    layout[6] = layout[0];
    layout[6].SemanticName = "BLENDWEIGHT";
    layout[6].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    layout[6].AlignedByteOffset = 64;
    hr = ID3D11Device_CreateInputLayout(ctx->device,
                                        layout,
                                        7,
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
        int elem = 7;
        for (int row = 0; row < 4; row++, elem++) {
            instanced_layout[elem].SemanticName = "TEXCOORD";
            instanced_layout[elem].SemanticIndex = 4u + (UINT)row;
            instanced_layout[elem].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            instanced_layout[elem].InputSlot = 1;
            instanced_layout[elem].AlignedByteOffset = (UINT)(offsetof(d3d_instance_data_t, model) + row * 16);
            instanced_layout[elem].InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
            instanced_layout[elem].InstanceDataStepRate = 1;
        }
        for (int row = 0; row < 4; row++, elem++) {
            instanced_layout[elem].SemanticName = "TEXCOORD";
            instanced_layout[elem].SemanticIndex = 8u + (UINT)row;
            instanced_layout[elem].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            instanced_layout[elem].InputSlot = 1;
            instanced_layout[elem].AlignedByteOffset = (UINT)(offsetof(d3d_instance_data_t, normal) + row * 16);
            instanced_layout[elem].InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
            instanced_layout[elem].InstanceDataStepRate = 1;
        }
        for (int row = 0; row < 4; row++, elem++) {
            instanced_layout[elem].SemanticName = "TEXCOORD";
            instanced_layout[elem].SemanticIndex = 12u + (UINT)row;
            instanced_layout[elem].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            instanced_layout[elem].InputSlot = 1;
            instanced_layout[elem].AlignedByteOffset = (UINT)(offsetof(d3d_instance_data_t, prev_model) + row * 16);
            instanced_layout[elem].InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
            instanced_layout[elem].InstanceDataStepRate = 1;
        }
    }
    hr = ID3D11Device_CreateInputLayout(ctx->device,
                                        instanced_layout,
                                        19,
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

    memset(&cb_desc, 0, sizeof(cb_desc));
    cb_desc.Usage = D3D11_USAGE_DYNAMIC;
    cb_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cb_desc.ByteWidth = d3d11_constant_buffer_byte_width(sizeof(d3d_per_object_t));
    hr = ID3D11Device_CreateBuffer(ctx->device, &cb_desc, NULL, &ctx->cb_per_object);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateBuffer(cbPerObject)", hr);
        goto fail;
    }
    cb_desc.ByteWidth = d3d11_constant_buffer_byte_width(sizeof(d3d_per_scene_t));
    hr = ID3D11Device_CreateBuffer(ctx->device, &cb_desc, NULL, &ctx->cb_per_scene);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateBuffer(cbPerScene)", hr);
        goto fail;
    }
    cb_desc.ByteWidth = d3d11_constant_buffer_byte_width(sizeof(d3d_per_material_t));
    hr = ID3D11Device_CreateBuffer(ctx->device, &cb_desc, NULL, &ctx->cb_per_material);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateBuffer(cbPerMaterial)", hr);
        goto fail;
    }
    cb_desc.ByteWidth = d3d11_constant_buffer_byte_width(sizeof(d3d_light_t) * 8u);
    hr = ID3D11Device_CreateBuffer(ctx->device, &cb_desc, NULL, &ctx->cb_per_lights);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateBuffer(cbPerLights)", hr);
        goto fail;
    }
    cb_desc.ByteWidth = d3d11_constant_buffer_byte_width(sizeof(float) * 128u * 16u);
    hr = ID3D11Device_CreateBuffer(ctx->device, &cb_desc, NULL, &ctx->cb_bones);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateBuffer(cbBones)", hr);
        goto fail;
    }
    hr = ID3D11Device_CreateBuffer(ctx->device, &cb_desc, NULL, &ctx->cb_prev_bones);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateBuffer(cbPrevBones)", hr);
        goto fail;
    }
    cb_desc.ByteWidth = d3d11_constant_buffer_byte_width(sizeof(d3d_skybox_cb_t));
    hr = ID3D11Device_CreateBuffer(ctx->device, &cb_desc, NULL, &ctx->cb_skybox);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateBuffer(cbSkybox)", hr);
        goto fail;
    }
    cb_desc.ByteWidth = d3d11_constant_buffer_byte_width(sizeof(d3d_postfx_cb_t));
    hr = ID3D11Device_CreateBuffer(ctx->device, &cb_desc, NULL, &ctx->cb_postfx);
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
    SAFE_RELEASE(vs_skybox_blob);
    SAFE_RELEASE(ps_skybox_blob);
    SAFE_RELEASE(vs_postfx_blob);
    SAFE_RELEASE(ps_postfx_blob);
    return ctx;

fail:
    SAFE_RELEASE(vs_blob);
    SAFE_RELEASE(vs_instanced_blob);
    SAFE_RELEASE(ps_blob);
    SAFE_RELEASE(vs_shadow_blob);
    SAFE_RELEASE(vs_skybox_blob);
    SAFE_RELEASE(ps_skybox_blob);
    SAFE_RELEASE(vs_postfx_blob);
    SAFE_RELEASE(ps_postfx_blob);
    SAFE_RELEASE(back_buffer);
    d3d11_destroy_ctx(ctx);
    return NULL;
}

static void d3d11_destroy_ctx(void *ctx_ptr) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    if (!ctx)
        return;

    d3d11_release_texture_cache(ctx);
    d3d11_release_cubemap_cache(ctx);
    d3d11_release_mesh_cache(ctx);
    d3d11_destroy_shadow_targets(ctx);
    d3d11_destroy_rtt_targets(ctx);
    d3d11_destroy_scene_targets(ctx);

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
    SAFE_RELEASE(ctx->vs_postfx);
    SAFE_RELEASE(ctx->ps_skybox);
    SAFE_RELEASE(ctx->vs_skybox);
    SAFE_RELEASE(ctx->vs_shadow);
    SAFE_RELEASE(ctx->ps_main);
    SAFE_RELEASE(ctx->vs_instanced);
    SAFE_RELEASE(ctx->vs_main);
    SAFE_RELEASE(ctx->shadow_cmp_sampler);
    SAFE_RELEASE(ctx->linear_clamp_sampler);
    SAFE_RELEASE(ctx->linear_wrap_sampler);
    SAFE_RELEASE(ctx->rs_wire_no_cull);
    SAFE_RELEASE(ctx->rs_wire_cull);
    SAFE_RELEASE(ctx->rs_solid_no_cull);
    SAFE_RELEASE(ctx->rs_solid_cull);
    SAFE_RELEASE(ctx->depth_state_readonly_lequal);
    SAFE_RELEASE(ctx->depth_state_no_write);
    SAFE_RELEASE(ctx->depth_state);
    SAFE_RELEASE(ctx->blend_state);
    SAFE_RELEASE(ctx->dsv);
    SAFE_RELEASE(ctx->depth_tex);
    SAFE_RELEASE(ctx->rtv);
    SAFE_RELEASE(ctx->swap_chain);
    SAFE_RELEASE(ctx->ctx);
    SAFE_RELEASE(ctx->device);
    free(ctx);
}

static void d3d11_clear(void *ctx_ptr, vgfx_window_t win, float r, float g, float b) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    (void)win;
    if (!ctx)
        return;
    ctx->clear_r = r;
    ctx->clear_g = g;
    ctx->clear_b = b;
}

static void d3d11_begin_frame(void *ctx_ptr, const vgfx3d_camera_params_t *cam) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    HRESULT hr = S_OK;

    if (!ctx || !cam)
        return;

    ctx->frame_serial++;
    ctx->shadow_active = 0;

    if (ctx->prev_vp_valid)
        memcpy(ctx->prev_vp, ctx->vp, sizeof(ctx->prev_vp));
    memcpy(ctx->view, cam->view, sizeof(ctx->view));
    memcpy(ctx->projection, cam->projection, sizeof(ctx->projection));
    mat4f_mul_d3d(cam->projection, cam->view, ctx->vp);
    if (vgfx3d_invert_matrix4(ctx->vp, ctx->inv_vp) != 0)
        memcpy(ctx->inv_vp, k_identity4x4, sizeof(ctx->inv_vp));
    if (!ctx->prev_vp_valid) {
        memcpy(ctx->prev_vp, ctx->vp, sizeof(ctx->prev_vp));
        ctx->prev_vp_valid = 1;
    }
    memcpy(ctx->cam_pos, cam->position, sizeof(float) * 3);
    ctx->fog_enabled = cam->fog_enabled;
    ctx->fog_near = cam->fog_near;
    ctx->fog_far = cam->fog_far;
    memcpy(ctx->fog_color, cam->fog_color, sizeof(ctx->fog_color));

    if (!ctx->rtt_active) {
        hr = d3d11_ensure_scene_targets(ctx, ctx->width, ctx->height);
        if (FAILED(hr))
            d3d11_log_hresult("CreateSceneTargets", hr);
    }
    d3d11_select_scene_targets(ctx);
    d3d11_clear_current_targets(ctx, cam->load_existing_color, cam->load_existing_depth);
    d3d11_bind_common_state(ctx);
}

static void d3d11_end_frame(void *ctx_ptr) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr;
    int32_t row_bytes;

    if (!ctx)
        return;
    if (!ctx->rtt_active || !ctx->rtt_target || !ctx->rtt_color_tex || !ctx->rtt_staging)
        return;

    ID3D11DeviceContext_CopyResource(
        ctx->ctx, (ID3D11Resource *)ctx->rtt_staging, (ID3D11Resource *)ctx->rtt_color_tex);
    hr = ID3D11DeviceContext_Map(
        ctx->ctx, (ID3D11Resource *)ctx->rtt_staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        d3d11_log_hresult("Map(rttStaging)", hr);
        return;
    }

    row_bytes = ctx->rtt_target->stride;
    for (int32_t y = 0; y < ctx->rtt_height; y++) {
        memcpy(&ctx->rtt_target->color_buf[(size_t)y * (size_t)row_bytes],
               (const uint8_t *)mapped.pData + (size_t)y * mapped.RowPitch,
               (size_t)row_bytes);
    }
    ID3D11DeviceContext_Unmap(ctx->ctx, (ID3D11Resource *)ctx->rtt_staging, 0);
}

static void d3d11_resize(void *ctx_ptr, int32_t w, int32_t h) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    ID3D11Texture2D *back_buffer = NULL;
    HRESULT hr;

    if (!ctx || w <= 0 || h <= 0)
        return;
    if (ctx->width == w && ctx->height == h)
        return;

    ID3D11DeviceContext_OMSetRenderTargets(ctx->ctx, 0, NULL, NULL);
    d3d11_destroy_scene_targets(ctx);
    SAFE_RELEASE(ctx->rtv);
    SAFE_RELEASE(ctx->dsv);
    SAFE_RELEASE(ctx->depth_tex);

    hr = IDXGISwapChain_ResizeBuffers(ctx->swap_chain, 0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        d3d11_log_hresult("IDXGISwapChain::ResizeBuffers", hr);
        return;
    }

    hr = IDXGISwapChain_GetBuffer(ctx->swap_chain, 0, &IID_ID3D11Texture2D, (void **)&back_buffer);
    if (FAILED(hr)) {
        d3d11_log_hresult("IDXGISwapChain::GetBuffer(resize)", hr);
        return;
    }
    hr = ID3D11Device_CreateRenderTargetView(ctx->device, (ID3D11Resource *)back_buffer, NULL, &ctx->rtv);
    SAFE_RELEASE(back_buffer);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateRenderTargetView(backbuffer resize)", hr);
        return;
    }
    hr = d3d11_create_depth_target(ctx, w, h, 0, &ctx->depth_tex, &ctx->dsv, NULL);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateTexture2D/DepthStencilView(main resize)", hr);
        SAFE_RELEASE(ctx->rtv);
        return;
    }

    ctx->width = w;
    ctx->height = h;
}

static int d3d11_readback_rgba(void *ctx_ptr, uint8_t *dst_rgba, int32_t w, int32_t h, int32_t stride) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    ID3D11Texture2D *staging = NULL;
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr;
    int32_t copy_w, copy_h;

    if (!ctx || !dst_rgba || w <= 0 || h <= 0 || stride < w * 4)
        return 0;
    if (!ctx->scene_color_tex || ctx->scene_width <= 0 || ctx->scene_height <= 0)
        return 0;

    hr = d3d11_create_staging_texture(ctx, ctx->scene_width, ctx->scene_height, &staging);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateTexture2D(sceneStaging)", hr);
        return 0;
    }

    memset(dst_rgba, 0, (size_t)stride * (size_t)h);
    ID3D11DeviceContext_CopyResource(ctx->ctx, (ID3D11Resource *)staging, (ID3D11Resource *)ctx->scene_color_tex);
    hr = ID3D11DeviceContext_Map(ctx->ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        d3d11_log_hresult("Map(sceneStaging)", hr);
        SAFE_RELEASE(staging);
        return 0;
    }

    copy_w = ctx->scene_width < w ? ctx->scene_width : w;
    copy_h = ctx->scene_height < h ? ctx->scene_height : h;
    for (int32_t y = 0; y < copy_h; y++) {
        memcpy(dst_rgba + (size_t)y * (size_t)stride,
               (const uint8_t *)mapped.pData + (size_t)y * mapped.RowPitch,
               (size_t)copy_w * 4u);
    }
    ID3D11DeviceContext_Unmap(ctx->ctx, (ID3D11Resource *)staging, 0);
    SAFE_RELEASE(staging);
    return 1;
}

static void d3d11_present_internal(d3d11_context_t *ctx, const vgfx3d_postfx_snapshot_t *snapshot) {
    d3d_postfx_cb_t postfx_data;
    ID3D11ShaderResourceView *srvs[3];
    HRESULT hr;

    if (!ctx || ctx->rtt_active)
        return;
    if (!ctx->scene_color_srv) {
        IDXGISwapChain_Present(ctx->swap_chain, 1, 0);
        return;
    }

    d3d11_bind_swapchain_target(ctx);
    d3d11_prepare_postfx_data(ctx, snapshot, &postfx_data);
    hr = d3d11_update_constant_buffer(ctx, ctx->cb_postfx, &postfx_data, sizeof(postfx_data));
    if (FAILED(hr)) {
        d3d11_log_hresult("Map(cbPostFX)", hr);
        IDXGISwapChain_Present(ctx->swap_chain, 1, 0);
        return;
    }

    srvs[0] = ctx->scene_color_srv;
    srvs[1] = ctx->scene_depth_srv;
    srvs[2] = ctx->scene_motion_srv;
    ID3D11DeviceContext_IASetInputLayout(ctx->ctx, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx->ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(ctx->ctx, ctx->vs_postfx, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx->ctx, ctx->ps_postfx, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx->ctx, 0, 1, &ctx->cb_postfx);
    ID3D11DeviceContext_PSSetConstantBuffers(ctx->ctx, 0, 1, &ctx->cb_postfx);
    if (ctx->linear_clamp_sampler)
        ID3D11DeviceContext_PSSetSamplers(ctx->ctx, 0, 1, &ctx->linear_clamp_sampler);
    ID3D11DeviceContext_PSSetShaderResources(ctx->ctx, 0, 3, srvs);
    ID3D11DeviceContext_RSSetState(ctx->ctx, ctx->rs_solid_no_cull);
    ID3D11DeviceContext_OMSetDepthStencilState(ctx->ctx, NULL, 0);
    ID3D11DeviceContext_Draw(ctx->ctx, 3, 0);
    {
        ID3D11ShaderResourceView *null_srvs[3] = {NULL, NULL, NULL};
        ID3D11DeviceContext_PSSetShaderResources(ctx->ctx, 0, 3, null_srvs);
    }
    IDXGISwapChain_Present(ctx->swap_chain, 1, 0);
}

static void d3d11_present(void *ctx_ptr) {
    d3d11_present_internal((d3d11_context_t *)ctx_ptr, NULL);
}

static void d3d11_present_postfx(void *ctx_ptr, const vgfx3d_postfx_snapshot_t *postfx) {
    d3d11_present_internal((d3d11_context_t *)ctx_ptr, postfx);
}

static void d3d11_set_render_target(void *ctx_ptr, vgfx3d_rendertarget_t *rt) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    HRESULT hr;

    if (!ctx)
        return;
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

static void d3d11_shadow_begin(void *ctx_ptr, float *depth_buf, int32_t width, int32_t height, const float *light_vp) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    D3D11_VIEWPORT viewport;
    HRESULT hr;

    (void)depth_buf;
    if (!ctx || !light_vp)
        return;
    hr = d3d11_ensure_shadow_targets(ctx, width, height);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateShadowTargets", hr);
        return;
    }

    memcpy(ctx->shadow_vp, light_vp, sizeof(ctx->shadow_vp));
    ID3D11DeviceContext_OMSetRenderTargets(ctx->ctx, 0, NULL, ctx->shadow_dsv);
    ID3D11DeviceContext_ClearDepthStencilView(ctx->ctx, ctx->shadow_dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
    memset(&viewport, 0, sizeof(viewport));
    viewport.Width = (FLOAT)width;
    viewport.Height = (FLOAT)height;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    ID3D11DeviceContext_RSSetViewports(ctx->ctx, 1, &viewport);
    ID3D11DeviceContext_RSSetState(ctx->ctx, ctx->rs_solid_cull);
    ID3D11DeviceContext_OMSetDepthStencilState(ctx->ctx, ctx->depth_state, 0);
    ID3D11DeviceContext_IASetInputLayout(ctx->ctx, ctx->input_layout);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx->ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(ctx->ctx, ctx->vs_shadow, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx->ctx, NULL, NULL, 0);
}

static void d3d11_shadow_draw(void *ctx_ptr, const vgfx3d_draw_cmd_t *cmd) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    d3d_per_object_t object_data;
    d3d_per_scene_t scene_data;
    HRESULT hr;
    UINT stride = sizeof(vgfx3d_vertex_t);
    UINT offset = 0;
    ID3D11Buffer *mesh_vb = NULL;
    ID3D11Buffer *mesh_ib = NULL;

    if (!ctx || !cmd || !cmd->vertices || !cmd->indices || cmd->vertex_count == 0 || cmd->index_count == 0)
        return;

    d3d11_prepare_object_data(cmd, &object_data);
    memset(&scene_data, 0, sizeof(scene_data));
    memcpy(scene_data.vp, ctx->shadow_vp, sizeof(scene_data.vp));
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

    if (!d3d11_acquire_mesh_buffers(ctx, cmd, &mesh_vb, &mesh_ib))
        return;

    ID3D11DeviceContext_IASetVertexBuffers(ctx->ctx, 0, 1, &mesh_vb, &stride, &offset);
    ID3D11DeviceContext_IASetIndexBuffer(ctx->ctx, mesh_ib, DXGI_FORMAT_R32_UINT, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx->ctx, 0, 1, &ctx->cb_per_object);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx->ctx, 1, 1, &ctx->cb_per_scene);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx->ctx, 4, 1, &ctx->cb_bones);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx->ctx, 5, 1, &ctx->cb_prev_bones);
    {
        ID3D11ShaderResourceView *vs_srvs[2] = {ctx->morph_srv, NULL};
        ID3D11DeviceContext_VSSetShaderResources(ctx->ctx, 0, 2, vs_srvs);
    }
    ID3D11DeviceContext_DrawIndexed(ctx->ctx, cmd->index_count, 0, 0);
    {
        ID3D11ShaderResourceView *null_vs[2] = {NULL, NULL};
        ID3D11DeviceContext_VSSetShaderResources(ctx->ctx, 0, 2, null_vs);
    }
}

static void d3d11_shadow_end(void *ctx_ptr, float bias) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    if (!ctx)
        return;
    ctx->shadow_active = 1;
    ctx->shadow_bias = bias;
    d3d11_select_scene_targets(ctx);
}

static void d3d11_draw_skybox(void *ctx_ptr, const void *cubemap_ptr) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    const rt_cubemap3d *cubemap = (const rt_cubemap3d *)cubemap_ptr;
    d3d_skybox_cb_t skybox_data;
    d3d_temp_srv_t cubemap_resource;
    ID3D11ShaderResourceView *srv;
    HRESULT hr;
    UINT stride = sizeof(float) * 3;
    UINT offset = 0;

    if (!ctx || !cubemap || !ctx->vs_skybox || !ctx->ps_skybox || !ctx->skybox_vb)
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
    ID3D11DeviceContext_VSSetShader(ctx->ctx, ctx->vs_skybox, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx->ctx, ctx->ps_skybox, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx->ctx, 0, 1, &ctx->cb_skybox);
    if (ctx->linear_clamp_sampler)
        ID3D11DeviceContext_PSSetSamplers(ctx->ctx, 0, 1, &ctx->linear_clamp_sampler);
    ID3D11DeviceContext_PSSetShaderResources(ctx->ctx, 0, 1, &srv);
    ID3D11DeviceContext_RSSetState(ctx->ctx, ctx->rs_solid_no_cull);
    ID3D11DeviceContext_OMSetDepthStencilState(ctx->ctx, ctx->depth_state_readonly_lequal, 0);
    ID3D11DeviceContext_Draw(ctx->ctx, 36, 0);
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
};

#endif /* _WIN32 && VIPER_ENABLE_GRAPHICS */
