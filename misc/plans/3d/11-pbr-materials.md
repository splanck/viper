# Plan: PBR Material Model

## Overview
Replace Blinn-Phong with physically-based rendering (metallic-roughness workflow). This is the single biggest visual quality improvement and the foundation for any custom shader system.

## API
```
Material3D.NewPBR(albedo_r, albedo_g, albedo_b)
Material3D.SetMetallic(metallic)           // 0.0 (dielectric) to 1.0 (metal)
Material3D.SetRoughness(roughness)         // 0.0 (mirror) to 1.0 (rough)
Material3D.SetMetallicMap(pixels)          // Per-texel metallic
Material3D.SetRoughnessMap(pixels)         // Per-texel roughness
Material3D.SetAOMap(pixels)               // Ambient occlusion texture
Material3D.SetAlbedoMap(pixels)           // Replaces SetTexture for PBR
```

## Implementation

### Material Struct Changes
**File:** `src/runtime/graphics/rt_material3d.c`
Add fields:
```c
float metallic;     // 0.0-1.0
float roughness;    // 0.0-1.0
int8_t is_pbr;      // flag to select shader path
void *metallic_map;
void *roughness_map;
void *ao_map;
```

### Shader Implementation (All 4 Backends)
PBR lighting uses the Cook-Torrance BRDF:
```
// Fresnel (Schlick approximation)
F = F0 + (1 - F0) * pow(1 - HdotV, 5)

// Normal distribution (GGX/Trowbridge-Reitz)
D = alpha^2 / (PI * (NdotH^2 * (alpha^2 - 1) + 1)^2)

// Geometry (Smith GGX)
G = G1(NdotV) * G1(NdotL)
G1(x) = x / (x * (1 - k) + k)  where k = (roughness + 1)^2 / 8

// Final specular
specular = D * F * G / (4 * NdotV * NdotL)

// Combine with diffuse (Lambert)
color = (1 - metallic) * (albedo / PI) * NdotL + specular
```

### Backend Changes
- **Metal:** New MSL function `pbr_fragment` alongside existing `phong_fragment`
- **D3D11:** New HLSL pixel shader `PS_PBR`
- **OpenGL:** New GLSL fragment shader `pbr_frag.glsl`
- **Software:** Per-pixel PBR evaluation (expensive but correct)
- Backend vtable: add `is_pbr` flag to material params, dispatch to correct shader

### Image-Based Lighting (IBL)
For environment reflections with PBR:
- Pre-filter cubemap for roughness levels (mip chain)
- BRDF lookup texture (2D LUT)
- This is optional for v1 — start with analytical lights only

## Files Modified
- `src/runtime/graphics/rt_material3d.c/h` — New fields + API
- `vgfx3d_backend.h` — Extend material params struct
- All 4 backend files — PBR shader implementation
- `src/il/runtime/runtime.def` — New RT_FUNC entries

## Verification
- PBR sphere: metallic=1, roughness=0 should look like polished chrome
- PBR sphere: metallic=0, roughness=1 should look like matte plastic
- Side-by-side: Blinn-Phong vs PBR under same lighting — PBR should look more realistic
- Texture maps: metallic/roughness maps should produce per-texel variation
