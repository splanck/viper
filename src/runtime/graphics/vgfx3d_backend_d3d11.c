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
//   - Depth buffer DXGI_FORMAT_D32_FLOAT
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

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")

//=============================================================================
// HLSL shader source (compiled at runtime)
//=============================================================================

static const char *hlsl_shader_source =
    "struct Light {\n"
    "    int type; float _p0, _p1, _p2;\n"
    "    float4 direction;\n"
    "    float4 position;\n"
    "    float4 color;\n"
    "    float intensity;\n"
    "    float attenuation;\n"
    "    float _p3[2];\n"
    "};\n"
    "\n"
    "cbuffer PerObject : register(b0) {\n"
    "    row_major float4x4 modelMatrix;\n"
    "    row_major float4x4 viewProjection;\n"
    "    row_major float4x4 normalMatrix;\n"
    "};\n"
    "\n"
    "cbuffer PerScene : register(b1) {\n"
    "    float4 cameraPosition;\n"
    "    float4 ambientColor;\n"
    "    int lightCount;\n"
    "    int _scenePad[3];\n"
    "};\n"
    "\n"
    "cbuffer PerMaterial : register(b2) {\n"
    "    float4 diffuseColor;\n"
    "    float4 specularColor;\n" /* .w = shininess */
    "    float4 emissiveColor;\n"
    "    float alpha;\n"
    "    int hasTexture;\n"
    "    int unlit;\n"
    "    int _matPad;\n"
    "};\n"
    "\n"
    "cbuffer PerLights : register(b3) {\n"
    "    Light lights[8];\n"
    "};\n"
    "\n"
    "struct VS_INPUT {\n"
    "    float3 pos    : POSITION;\n"
    "    float3 normal : NORMAL;\n"
    "    float2 uv     : TEXCOORD0;\n"
    "    float4 color  : COLOR;\n"
    "    float3 tangent: TANGENT;\n"
    "    uint4  boneIdx: BLENDINDICES;\n"
    "    float4 boneWt : BLENDWEIGHT;\n"
    "};\n"
    "\n"
    "struct PS_INPUT {\n"
    "    float4 pos      : SV_POSITION;\n"
    "    float3 worldPos : TEXCOORD0;\n"
    "    float3 normal   : TEXCOORD1;\n"
    "    float2 uv       : TEXCOORD2;\n"
    "    float4 color    : COLOR;\n"
    "};\n"
    "\n"
    "PS_INPUT VSMain(VS_INPUT input) {\n"
    "    PS_INPUT output;\n"
    "    float4 wp = mul(float4(input.pos, 1.0), modelMatrix);\n"
    "    output.pos = mul(wp, viewProjection);\n"
    "    /* D3D11 depth range [0,1] — remap from OpenGL NDC [-1,1] */\n"
    "    output.pos.z = output.pos.z * 0.5 + output.pos.w * 0.5;\n"
    "    output.worldPos = wp.xyz;\n"
    "    output.normal = mul(float4(input.normal, 0.0), normalMatrix).xyz;\n"
    "    output.uv = input.uv;\n"
    "    output.color = input.color;\n"
    "    return output;\n"
    "}\n"
    "\n"
    "float4 PSMain(PS_INPUT input) : SV_Target {\n"
    "    if (unlit) return float4(diffuseColor.rgb, alpha);\n"
    "    float3 N = normalize(input.normal);\n"
    "    float3 V = normalize(cameraPosition.xyz - input.worldPos);\n"
    "    float3 result = ambientColor.rgb * diffuseColor.rgb;\n"
    "    for (int i = 0; i < lightCount; i++) {\n"
    "        float3 L; float atten = 1.0;\n"
    "        if (lights[i].type == 0) {\n"
    "            L = normalize(-lights[i].direction.xyz);\n"
    "        } else if (lights[i].type == 1) {\n"
    "            float3 tl = lights[i].position.xyz - input.worldPos;\n"
    "            float d = length(tl); L = tl / max(d, 0.0001);\n"
    "            atten = 1.0 / (1.0 + lights[i].attenuation * d * d);\n"
    "        } else {\n"
    "            result += lights[i].color.rgb * lights[i].intensity * diffuseColor.rgb;\n"
    "            continue;\n"
    "        }\n"
    "        float NdotL = max(dot(N, L), 0.0);\n"
    "        result += lights[i].color.rgb * lights[i].intensity * NdotL * diffuseColor.rgb * "
    "atten;\n"
    "        if (NdotL > 0.0 && specularColor.w > 0.0) {\n"
    "            float3 H = normalize(L + V);\n"
    "            float spec = pow(max(dot(N, H), 0.0), specularColor.w);\n"
    "            result += lights[i].color.rgb * lights[i].intensity * spec * specularColor.rgb * "
    "atten;\n"
    "        }\n"
    "    }\n"
    "    result += emissiveColor.rgb;\n"
    "    return float4(result, alpha);\n"
    "}\n";

//=============================================================================
// D3D11 context
//=============================================================================

typedef struct {
    ID3D11Device *device;
    ID3D11DeviceContext *ctx;
    IDXGISwapChain *swapChain;
    ID3D11RenderTargetView *rtv;
    ID3D11DepthStencilView *dsv;
    ID3D11DepthStencilState *dss;
    ID3D11DepthStencilState *dssNoWrite; /* depth test ON, write OFF (transparent) */
    ID3D11BlendState *blendState;
    ID3D11RasterizerState *rsState;
    ID3D11VertexShader *vs;
    ID3D11PixelShader *ps;
    ID3D11InputLayout *inputLayout;
    ID3D11Buffer *cbPerObject;
    ID3D11Buffer *cbPerScene;
    ID3D11Buffer *cbPerMaterial;
    ID3D11Buffer *cbPerLights;
    int32_t width, height;
    float vp[16];
    float cam_pos[3];
    float clearR, clearG, clearB;
} d3d11_context_t;

//=============================================================================
// Uniform buffer structs (match HLSL cbuffers)
//=============================================================================

typedef struct {
    float m[16];
    float vp[16];
    float nm[16];
} d3d_per_object_t;

typedef struct {
    float cp[4];
    float ac[4];
    int32_t lc;
    int32_t _p[3];
} d3d_per_scene_t;

typedef struct {
    int32_t type;
    float _p0, _p1, _p2;
    float dir[4];
    float pos[4];
    float col[4];
    float intensity, attenuation;
    float _p3[2];
} d3d_light_t;

typedef struct {
    float dc[4];
    float sc[4];
    float ec[4];
    float alpha;
    int32_t ht;
    int32_t unlit;
    int32_t _p;
} d3d_per_material_t;

//=============================================================================
// Matrix helper
//=============================================================================

static void mat4f_mul_d3d(const float *a, const float *b, float *out) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            out[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c] +
                             a[r * 4 + 2] * b[2 * 4 + c] + a[r * 4 + 3] * b[3 * 4 + c];
}

//=============================================================================
// Backend vtable implementation
//=============================================================================

/* Forward declaration — used by d3d11_create_ctx error paths before the
   full definition appears below. */
static void d3d11_destroy_ctx(void *ctx_ptr);

static void *d3d11_create_ctx(vgfx_window_t win, int32_t w, int32_t h) {
    HWND hwnd = (HWND)vgfx_get_native_view(win);
    if (!hwnd)
        return NULL;

    d3d11_context_t *ctx = (d3d11_context_t *)calloc(1, sizeof(d3d11_context_t));
    if (!ctx)
        return NULL;
    ctx->width = w;
    ctx->height = h;

    DXGI_SWAP_CHAIN_DESC scd;
    memset(&scd, 0, sizeof(scd));
    scd.BufferCount = 1;
    scd.BufferDesc.Width = (UINT)w;
    scd.BufferDesc.Height = (UINT)h;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;

    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(NULL,
                                               D3D_DRIVER_TYPE_HARDWARE,
                                               NULL,
                                               0,
                                               &featureLevel,
                                               1,
                                               D3D11_SDK_VERSION,
                                               &scd,
                                               &ctx->swapChain,
                                               &ctx->device,
                                               NULL,
                                               &ctx->ctx);
    if (FAILED(hr)) {
        free(ctx);
        return NULL;
    }

    /* Back buffer RTV */
    ID3D11Texture2D *backBuf;
    IDXGISwapChain_GetBuffer(ctx->swapChain, 0, &IID_ID3D11Texture2D, (void **)&backBuf);
    ID3D11Device_CreateRenderTargetView(ctx->device, (ID3D11Resource *)backBuf, NULL, &ctx->rtv);
    ID3D11Texture2D_Release(backBuf);

    /* Depth-stencil buffer */
    D3D11_TEXTURE2D_DESC depthDesc;
    memset(&depthDesc, 0, sizeof(depthDesc));
    depthDesc.Width = (UINT)w;
    depthDesc.Height = (UINT)h;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ID3D11Texture2D *depthTex;
    ID3D11Device_CreateTexture2D(ctx->device, &depthDesc, NULL, &depthTex);
    ID3D11Device_CreateDepthStencilView(ctx->device, (ID3D11Resource *)depthTex, NULL, &ctx->dsv);
    ID3D11Texture2D_Release(depthTex);

    /* Depth-stencil state */
    D3D11_DEPTH_STENCIL_DESC dssDesc;
    memset(&dssDesc, 0, sizeof(dssDesc));
    dssDesc.DepthEnable = TRUE;
    dssDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dssDesc.DepthFunc = D3D11_COMPARISON_LESS;
    ID3D11Device_CreateDepthStencilState(ctx->device, &dssDesc, &ctx->dss);

    /* Depth-stencil state for transparent draws (test ON, write OFF) */
    dssDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    ID3D11Device_CreateDepthStencilState(ctx->device, &dssDesc, &ctx->dssNoWrite);

    /* Blend state (src*srcAlpha + dst*(1-srcAlpha)) */
    D3D11_BLEND_DESC blendDesc;
    memset(&blendDesc, 0, sizeof(blendDesc));
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ID3D11Device_CreateBlendState(ctx->device, &blendDesc, &ctx->blendState);

    /* Rasterizer state */
    D3D11_RASTERIZER_DESC rsDesc;
    memset(&rsDesc, 0, sizeof(rsDesc));
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_BACK;
    /* D3D11 tests winding after viewport transform (Y-flip from NDC).
     * Our OpenGL-convention projection + D3D viewport Y-flip means CCW in clip
     * space appears CW in screen space. FrontCounterClockwise = FALSE (default)
     * correctly treats our CCW geometry as front-facing after the flip. */
    rsDesc.FrontCounterClockwise = FALSE;
    rsDesc.DepthClipEnable = TRUE;
    ID3D11Device_CreateRasterizerState(ctx->device, &rsDesc, &ctx->rsState);

    /* Compile shaders */
    ID3DBlob *vsBlob = NULL, *psBlob = NULL, *errBlob = NULL;
    hr = D3DCompile(hlsl_shader_source,
                    strlen(hlsl_shader_source),
                    "shader",
                    NULL,
                    NULL,
                    "VSMain",
                    "vs_5_0",
                    0,
                    0,
                    &vsBlob,
                    &errBlob);
    if (FAILED(hr)) {
        if (errBlob)
            ID3D10Blob_Release(errBlob);
        d3d11_destroy_ctx(ctx);
        return NULL;
    }

    hr = D3DCompile(hlsl_shader_source,
                    strlen(hlsl_shader_source),
                    "shader",
                    NULL,
                    NULL,
                    "PSMain",
                    "ps_5_0",
                    0,
                    0,
                    &psBlob,
                    &errBlob);
    if (FAILED(hr)) {
        ID3D10Blob_Release(vsBlob);
        if (errBlob)
            ID3D10Blob_Release(errBlob);
        d3d11_destroy_ctx(ctx);
        return NULL;
    }

    ID3D11Device_CreateVertexShader(ctx->device,
                                    ID3D10Blob_GetBufferPointer(vsBlob),
                                    ID3D10Blob_GetBufferSize(vsBlob),
                                    NULL,
                                    &ctx->vs);
    ID3D11Device_CreatePixelShader(ctx->device,
                                   ID3D10Blob_GetBufferPointer(psBlob),
                                   ID3D10Blob_GetBufferSize(psBlob),
                                   NULL,
                                   &ctx->ps);

    /* Input layout (matches 80-byte vgfx3d_vertex_t) */
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 60, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    ID3D11Device_CreateInputLayout(ctx->device,
                                   layout,
                                   7,
                                   ID3D10Blob_GetBufferPointer(vsBlob),
                                   ID3D10Blob_GetBufferSize(vsBlob),
                                   &ctx->inputLayout);

    ID3D10Blob_Release(vsBlob);
    ID3D10Blob_Release(psBlob);

    /* Constant buffers */
    D3D11_BUFFER_DESC cbDesc;
    memset(&cbDesc, 0, sizeof(cbDesc));
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    cbDesc.ByteWidth = sizeof(d3d_per_object_t);
    ID3D11Device_CreateBuffer(ctx->device, &cbDesc, NULL, &ctx->cbPerObject);
    cbDesc.ByteWidth = sizeof(d3d_per_scene_t);
    ID3D11Device_CreateBuffer(ctx->device, &cbDesc, NULL, &ctx->cbPerScene);
    cbDesc.ByteWidth = sizeof(d3d_per_material_t);
    ID3D11Device_CreateBuffer(ctx->device, &cbDesc, NULL, &ctx->cbPerMaterial);
    cbDesc.ByteWidth = sizeof(d3d_light_t) * 8;
    ID3D11Device_CreateBuffer(ctx->device, &cbDesc, NULL, &ctx->cbPerLights);

    return ctx;
}

static void d3d11_destroy_ctx(void *ctx_ptr) {
    if (!ctx_ptr)
        return;
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    if (ctx->cbPerLights)
        ID3D11Buffer_Release(ctx->cbPerLights);
    if (ctx->cbPerMaterial)
        ID3D11Buffer_Release(ctx->cbPerMaterial);
    if (ctx->cbPerScene)
        ID3D11Buffer_Release(ctx->cbPerScene);
    if (ctx->cbPerObject)
        ID3D11Buffer_Release(ctx->cbPerObject);
    if (ctx->inputLayout)
        ID3D11InputLayout_Release(ctx->inputLayout);
    if (ctx->ps)
        ID3D11PixelShader_Release(ctx->ps);
    if (ctx->vs)
        ID3D11VertexShader_Release(ctx->vs);
    if (ctx->rsState)
        ID3D11RasterizerState_Release(ctx->rsState);
    if (ctx->blendState)
        ID3D11BlendState_Release(ctx->blendState);
    if (ctx->dssNoWrite)
        ID3D11DepthStencilState_Release(ctx->dssNoWrite);
    if (ctx->dss)
        ID3D11DepthStencilState_Release(ctx->dss);
    if (ctx->dsv)
        ID3D11DepthStencilView_Release(ctx->dsv);
    if (ctx->rtv)
        ID3D11RenderTargetView_Release(ctx->rtv);
    if (ctx->swapChain)
        IDXGISwapChain_Release(ctx->swapChain);
    if (ctx->ctx)
        ID3D11DeviceContext_Release(ctx->ctx);
    if (ctx->device)
        ID3D11Device_Release(ctx->device);
    free(ctx);
}

static void d3d11_clear(void *ctx_ptr, vgfx_window_t win, float r, float g, float b) {
    (void)win;
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    ctx->clearR = r;
    ctx->clearG = g;
    ctx->clearB = b;
}

static void d3d11_begin_frame(void *ctx_ptr, const vgfx3d_camera_params_t *cam) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    mat4f_mul_d3d(cam->projection, cam->view, ctx->vp);
    memcpy(ctx->cam_pos, cam->position, sizeof(float) * 3);

    float clearColor[4] = {ctx->clearR, ctx->clearG, ctx->clearB, 1.0f};
    ID3D11DeviceContext_ClearRenderTargetView(ctx->ctx, ctx->rtv, clearColor);
    ID3D11DeviceContext_ClearDepthStencilView(ctx->ctx, ctx->dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
    ID3D11DeviceContext_OMSetRenderTargets(ctx->ctx, 1, &ctx->rtv, ctx->dsv);
    ID3D11DeviceContext_OMSetDepthStencilState(ctx->ctx, ctx->dss, 0);
    ID3D11DeviceContext_RSSetState(ctx->ctx, ctx->rsState);

    D3D11_VIEWPORT vp = {0, 0, (FLOAT)ctx->width, (FLOAT)ctx->height, 0.0f, 1.0f};
    ID3D11DeviceContext_RSSetViewports(ctx->ctx, 1, &vp);

    ID3D11DeviceContext_IASetInputLayout(ctx->ctx, ctx->inputLayout);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx->ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(ctx->ctx, ctx->vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx->ctx, ctx->ps, NULL, 0);

    /* Enable alpha blending */
    float blendFactor[4] = {0, 0, 0, 0};
    ID3D11DeviceContext_OMSetBlendState(ctx->ctx, ctx->blendState, blendFactor, 0xFFFFFFFF);
}

static void d3d11_submit_draw(void *ctx_ptr,
                              vgfx_window_t win,
                              const vgfx3d_draw_cmd_t *cmd,
                              const vgfx3d_light_params_t *lights,
                              int32_t light_count,
                              const float *ambient,
                              int8_t wireframe,
                              int8_t backface_cull) {
    (void)win;
    (void)wireframe;
    (void)backface_cull;
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;

    /* Toggle depth-stencil state for transparent draws */
    if (cmd->alpha < 1.0f)
        ID3D11DeviceContext_OMSetDepthStencilState(ctx->ctx, ctx->dssNoWrite, 0);
    else
        ID3D11DeviceContext_OMSetDepthStencilState(ctx->ctx, ctx->dss, 0);

    /* Create vertex + index buffers */
    D3D11_BUFFER_DESC bd;
    D3D11_SUBRESOURCE_DATA sd;
    memset(&bd, 0, sizeof(bd));
    memset(&sd, 0, sizeof(sd));

    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = cmd->vertex_count * sizeof(vgfx3d_vertex_t);
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    sd.pSysMem = cmd->vertices;
    ID3D11Buffer *vb;
    ID3D11Device_CreateBuffer(ctx->device, &bd, &sd, &vb);

    bd.ByteWidth = cmd->index_count * sizeof(uint32_t);
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    sd.pSysMem = cmd->indices;
    ID3D11Buffer *ib;
    ID3D11Device_CreateBuffer(ctx->device, &bd, &sd, &ib);

    UINT stride = sizeof(vgfx3d_vertex_t), offset = 0;
    ID3D11DeviceContext_IASetVertexBuffers(ctx->ctx, 0, 1, &vb, &stride, &offset);
    ID3D11DeviceContext_IASetIndexBuffer(ctx->ctx, ib, DXGI_FORMAT_R32_UINT, 0);

    /* Update per-object constant buffer */
    D3D11_MAPPED_SUBRESOURCE mapped;
    ID3D11DeviceContext_Map(
        ctx->ctx, (ID3D11Resource *)ctx->cbPerObject, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    d3d_per_object_t *obj = (d3d_per_object_t *)mapped.pData;
    memcpy(obj->m, cmd->model_matrix, sizeof(float) * 16);
    memcpy(obj->vp, ctx->vp, sizeof(float) * 16);
    memcpy(obj->nm, cmd->model_matrix, sizeof(float) * 16); /* Normal matrix = model */
    ID3D11DeviceContext_Unmap(ctx->ctx, (ID3D11Resource *)ctx->cbPerObject, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx->ctx, 0, 1, &ctx->cbPerObject);

    /* Per-scene */
    ID3D11DeviceContext_Map(
        ctx->ctx, (ID3D11Resource *)ctx->cbPerScene, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    d3d_per_scene_t *scene = (d3d_per_scene_t *)mapped.pData;
    memset(scene, 0, sizeof(*scene));
    memcpy(scene->cp, ctx->cam_pos, sizeof(float) * 3);
    scene->ac[0] = ambient[0];
    scene->ac[1] = ambient[1];
    scene->ac[2] = ambient[2];
    scene->lc = light_count;
    ID3D11DeviceContext_Unmap(ctx->ctx, (ID3D11Resource *)ctx->cbPerScene, 0);
    ID3D11DeviceContext_PSSetConstantBuffers(ctx->ctx, 1, 1, &ctx->cbPerScene);

    /* Per-material */
    ID3D11DeviceContext_Map(
        ctx->ctx, (ID3D11Resource *)ctx->cbPerMaterial, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    d3d_per_material_t *mat = (d3d_per_material_t *)mapped.pData;
    memset(mat, 0, sizeof(*mat));
    memcpy(mat->dc, cmd->diffuse_color, sizeof(float) * 4);
    mat->sc[0] = cmd->specular[0];
    mat->sc[1] = cmd->specular[1];
    mat->sc[2] = cmd->specular[2];
    mat->sc[3] = cmd->shininess; /* specularColor.w = shininess in shader */
    mat->ec[0] = cmd->emissive_color[0];
    mat->ec[1] = cmd->emissive_color[1];
    mat->ec[2] = cmd->emissive_color[2];
    mat->alpha = cmd->alpha;
    mat->ht = cmd->texture ? 1 : 0;
    mat->unlit = cmd->unlit;
    ID3D11DeviceContext_Unmap(ctx->ctx, (ID3D11Resource *)ctx->cbPerMaterial, 0);
    ID3D11DeviceContext_PSSetConstantBuffers(ctx->ctx, 2, 1, &ctx->cbPerMaterial);

    /* Per-lights (cbuffer PerLights : register(b3)) */
    ID3D11DeviceContext_Map(
        ctx->ctx, (ID3D11Resource *)ctx->cbPerLights, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    d3d_light_t *dl = (d3d_light_t *)mapped.pData;
    memset(dl, 0, sizeof(d3d_light_t) * 8);
    for (int32_t i = 0; i < light_count && i < 8; i++) {
        dl[i].type = lights[i].type;
        dl[i].dir[0] = lights[i].direction[0];
        dl[i].dir[1] = lights[i].direction[1];
        dl[i].dir[2] = lights[i].direction[2];
        dl[i].pos[0] = lights[i].position[0];
        dl[i].pos[1] = lights[i].position[1];
        dl[i].pos[2] = lights[i].position[2];
        dl[i].col[0] = lights[i].color[0];
        dl[i].col[1] = lights[i].color[1];
        dl[i].col[2] = lights[i].color[2];
        dl[i].intensity = lights[i].intensity;
        dl[i].attenuation = lights[i].attenuation;
    }
    ID3D11DeviceContext_Unmap(ctx->ctx, (ID3D11Resource *)ctx->cbPerLights, 0);
    ID3D11DeviceContext_PSSetConstantBuffers(ctx->ctx, 3, 1, &ctx->cbPerLights);

    ID3D11DeviceContext_DrawIndexed(ctx->ctx, cmd->index_count, 0, 0);

    ID3D11Buffer_Release(vb);
    ID3D11Buffer_Release(ib);
}

static void d3d11_end_frame(void *ctx_ptr) {
    (void)ctx_ptr;
    /* GPU work is committed implicitly by D3D11 command list.
     * Presentation moved to d3d11_present() so only the LAST
     * Begin/End pair's content is shown per frame. */
}

static void d3d11_present(void *ctx_ptr) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    IDXGISwapChain_Present(ctx->swapChain, 1, 0); /* VSync on */
}

static void d3d11_set_render_target(void *ctx_ptr, vgfx3d_rendertarget_t *rt) {
    (void)ctx_ptr;
    (void)rt;
}

const vgfx3d_backend_t vgfx3d_d3d11_backend = {
    .name = "d3d11",
    .create_ctx = d3d11_create_ctx,
    .destroy_ctx = d3d11_destroy_ctx,
    .clear = d3d11_clear,
    .begin_frame = d3d11_begin_frame,
    .submit_draw = d3d11_submit_draw,
    .end_frame = d3d11_end_frame,
    .set_render_target = d3d11_set_render_target,
    .present = d3d11_present,
};

#endif /* _WIN32 && VIPER_ENABLE_GRAPHICS */
