# Phase 3: Metal GPU Backend (macOS)

## Goal

GPU-accelerated 3D rendering on macOS using Metal (system framework, 10.11+).

## Prerequisites

- Phase 2 complete (backend abstraction provides `vgfx3d_backend_t` vtable)
- macOS 10.11+ (El Capitan) with Metal-capable GPU

Implements `vgfx3d_metal_backend` as a `vgfx3d_backend_t`, filling in all vtable function pointers.

## Architecture

```
Canvas3D.Begin(camera)
  → Encode uniforms into MTLBuffer
Canvas3D.DrawMesh(mesh, transform, material)
  → Append draw call to MTLRenderCommandEncoder
Canvas3D.End()
  → Commit MTLCommandBuffer → present via CAMetalLayer
```

## New Files

**`src/lib/graphics/src/vgfx3d_metal.m`** (~800 LOC)
- Metal device + command queue creation (lazy, on first Canvas3D.New)
- CAMetalLayer setup: attach to existing NSView from vgfx_platform_macos
- Render pipeline state objects (PSOs) for flat, Gouraud, Phong, textured variants
- Depth-stencil state (MTLDepthStencilDescriptor)
- Vertex buffer management: upload mesh data as MTLBuffer (shared storage)
- Uniform buffers: per-frame (camera MVP), per-object (model matrix), per-material (color/shininess)
- Light uniform buffer: array of 8 lights
- Texture creation from Pixels RGBA data (MTLTextureDescriptor, 2D, RGBA8Unorm)
- Mipmap generation via MTLBlitCommandEncoder
- MSAA: configurable sample count (1, 2, 4) via render pass descriptor
- Window resize: update CAMetalLayer.drawableSize
- Triple buffering: semaphore with 3 in-flight frames

**`src/lib/graphics/src/vgfx3d_metal_shaders.metal`** (~200 LOC)
```metal
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 position [[attribute(0)]];
    float3 normal   [[attribute(1)]];
    float2 uv       [[attribute(2)]];
    float4 color    [[attribute(3)]];
};

struct VertexOut {
    float4 position [[position]];
    float3 worldPos;
    float3 normal;
    float2 uv;
    float4 color;
};

struct Uniforms {
    float4x4 modelMatrix;
    float4x4 viewProjection;
    float4x4 normalMatrix;    // inverse-transpose of model
};

struct Light {
    int type;          // 0=directional, 1=point, 2=ambient
    float3 direction;
    float3 position;
    float3 color;
    float intensity;
    float attenuation;
};

struct SceneUniforms {
    float3 cameraPosition;
    float3 ambientColor;
    Light lights[8];
    int lightCount;
};

struct MaterialUniforms {
    float4 diffuseColor;
    float3 specularColor;
    float shininess;
    bool hasTexture;
    bool unlit;
};

vertex VertexOut vertex_main(VertexIn in [[stage_in]],
                             constant Uniforms &uniforms [[buffer(1)]]) {
    VertexOut out;
    float4 worldPos = uniforms.modelMatrix * float4(in.position, 1.0);
    out.position = uniforms.viewProjection * worldPos;
    out.worldPos = worldPos.xyz;
    out.normal = (uniforms.normalMatrix * float4(in.normal, 0.0)).xyz;
    out.uv = in.uv;
    out.color = in.color;
    return out;
}

fragment float4 fragment_main(VertexOut in [[stage_in]],
                              constant SceneUniforms &scene [[buffer(0)]],
                              constant MaterialUniforms &material [[buffer(1)]],
                              texture2d<float> diffuseTexture [[texture(0)]],
                              sampler texSampler [[sampler(0)]]) {
    if (material.unlit)
        return material.diffuseColor;

    float3 N = normalize(in.normal);
    float3 V = normalize(scene.cameraPosition - in.worldPos);
    float3 result = scene.ambientColor * material.diffuseColor.rgb;

    for (int i = 0; i < scene.lightCount; i++) {
        Light light = scene.lights[i];
        float3 L;
        float attenuation = 1.0;
        if (light.type == 0) { // directional
            L = normalize(-light.direction);
        } else if (light.type == 1) { // point
            float3 toLight = light.position - in.worldPos;
            float dist = length(toLight);
            L = toLight / dist;
            attenuation = 1.0 / (1.0 + light.attenuation * dist * dist);
        } else { // ambient — handled separately
            result += light.color * light.intensity * material.diffuseColor.rgb;
            continue;
        }
        float NdotL = max(dot(N, L), 0.0);
        float3 diffuse = light.color * light.intensity * NdotL * material.diffuseColor.rgb;
        // Blinn-Phong specular
        float3 H = normalize(L + V);
        float NdotH = max(dot(N, H), 0.0);
        float3 specular = light.color * light.intensity * pow(NdotH, material.shininess) * material.specularColor;
        result += (diffuse + specular) * attenuation;
    }

    float4 texColor = material.hasTexture ? diffuseTexture.sample(texSampler, in.uv) : float4(1);
    return float4(result * texColor.rgb, material.diffuseColor.a * texColor.a);
}
```

**`src/lib/graphics/src/vgfx3d_metal_internal.h`** (~50 LOC)
- Forward declarations for Metal types (id<MTLDevice>, id<MTLCommandQueue>, etc.)
- Internal context structure for Metal state

## Metal Shader Embedding

Two options (both maintain zero external deps):

**Option A: Compile at build time** (recommended)
- CMake custom command: `xcrun -sdk macosx metal -c shaders.metal -o shaders.air && xcrun metallib shaders.air -o shaders.metallib`
- Embed `.metallib` as a C byte array via `xxd -i`
- Load at runtime: `[device newLibraryWithData:]`

**Option B: Compile at runtime**
- Embed MSL source as a C string literal
- Compile at runtime: `[device newLibraryWithSource:options:error:]`
- Simpler build but slower startup (~100ms compile)

## Platform Integration Changes

**`src/lib/graphics/src/vgfx_platform_macos.m`**:
- Add `CAMetalLayer` property to VGFXView
- When Canvas3D is created, set up `layer.device = MTLCreateSystemDefaultDevice()`
- Existing 2D CGImage path continues to work for regular Canvas

**`src/lib/graphics/CMakeLists.txt`**:
- Add `vgfx3d_metal.m` to sources (macOS only)
- Link `-framework Metal -framework MetalKit -framework QuartzCore`

## Fallback

If Metal is unavailable (pre-10.11, no GPU):
- `MTLCreateSystemDefaultDevice()` returns nil
- Canvas3D falls back to software backend automatically
- Log warning: "Metal unavailable, using software 3D renderer"

