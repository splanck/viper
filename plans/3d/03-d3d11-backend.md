# Phase 3: Direct3D 11 GPU Backend (Windows)

## Goal

GPU-accelerated 3D rendering on Windows using Direct3D 11 (Windows SDK, ships with Windows 7+).

## Critical: NDC Depth Range Mismatch

Mat4.Perspective uses OpenGL convention (Z: [-1,1]) but D3D11 expects Z: [0,1]. Two options:
- **Option A (recommended):** Add `rt_mat4_perspective_d3d()` that outputs Z [0,1] range, called only by the D3D11 backend internally
- **Option B:** Transform Z in the vertex shader: `output.z = output.z * 0.5 + output.w * 0.5`

The HLSL vertex shader must apply the correction so user code always uses the standard Mat4.Perspective.

## Architecture

```
Canvas3D.Begin(camera)
  → Map constant buffers (MVP, lights)
Canvas3D.DrawMesh(mesh, transform, material)
  → IASetVertexBuffers, IASetIndexBuffer, VSSetConstantBuffers, PSSetConstantBuffers
  → DrawIndexed
Canvas3D.End()
  → IDXGISwapChain::Present
```

## New Files

**`src/lib/graphics/src/vgfx3d_d3d11.c`** (~900 LOC)
- Device creation: `D3D11CreateDeviceAndSwapChain` with feature level 11_0
- Swap chain attached to existing HWND from vgfx_platform_win32
- Render target view from swap chain back buffer
- Depth-stencil buffer (DXGI_FORMAT_D24_UNORM_S8_UINT)
- Vertex buffer creation: `ID3D11Buffer` with D3D11_USAGE_DEFAULT
- Index buffer creation
- Constant buffers: per-frame, per-object, per-material (D3D11_USAGE_DYNAMIC, Map/Unmap)
- Input layout matching HLSL vertex shader
- Rasterizer state (back-face culling, wireframe toggle)
- Blend state (alpha blending)
- Sampler state (linear filtering with anisotropy)
- Texture creation: ID3D11Texture2D + ShaderResourceView from Pixels RGBA
- Mipmap generation: `ID3D11DeviceContext::GenerateMips`
- MSAA: swap chain sample desc
- Resize: `IDXGISwapChain::ResizeBuffers` on WM_SIZE

**`src/lib/graphics/src/vgfx3d_d3d11_shaders.hlsl`** (~200 LOC)
```hlsl
cbuffer PerObject : register(b0) {
    float4x4 modelMatrix;
    float4x4 viewProjection;
    float4x4 normalMatrix;
};

cbuffer PerScene : register(b1) {
    float3 cameraPosition;
    float3 ambientColor;
    // Light array...
    int lightCount;
};

cbuffer PerMaterial : register(b2) {
    float4 diffuseColor;
    float3 specularColor;
    float shininess;
    bool hasTexture;
    bool unlit;
};

struct VS_INPUT {
    float3 pos    : POSITION;
    float3 normal : NORMAL;
    float2 uv     : TEXCOORD0;
    float4 color  : COLOR;
};

struct PS_INPUT {
    float4 pos      : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 uv       : TEXCOORD2;
    float4 color    : COLOR;
};

// vertex_main and fragment_main similar to Metal version
```

**Shader compilation**:
- Build-time: `fxc.exe /T vs_5_0 /E vertex_main shaders.hlsl /Fh vs_bytecode.h`
- Embed as `const BYTE g_vs_main[] = { ... };`
- Load at runtime: `ID3D11Device::CreateVertexShader(g_vs_main, sizeof(g_vs_main), ...)`

**`src/lib/graphics/src/vgfx3d_d3d11_internal.h`** (~50 LOC)
- COM interface declarations, D3D11 includes

## Platform Integration Changes

**`src/lib/graphics/src/vgfx_platform_win32.c`**:
- When Canvas3D is created, attach DXGI swap chain to existing HWND
- Existing 2D GDI path continues for regular Canvas
- Handle WM_SIZE for swap chain resize

**`src/lib/graphics/CMakeLists.txt`**:
- Add `vgfx3d_d3d11.c` to sources (Windows only)
- Link `d3d11.lib dxgi.lib d3dcompiler.lib` (all ship with Windows SDK)

## Fallback

If D3D11 unavailable (rare, pre-Windows 7 SP1):
- `D3D11CreateDeviceAndSwapChain` fails → fall back to software

