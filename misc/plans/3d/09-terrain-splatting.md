# Plan: Terrain Texture Splatting

## Overview
The current terrain supports only a single material. Real terrain needs multiple textures (grass, dirt, rock, snow) blended via a splat map.

## API
```
Terrain3D.SetSplatMap(pixels)              // RGBA image: R=layer0, G=layer1, B=layer2, A=layer3
Terrain3D.SetLayerTexture(layer, pixels)   // Set texture for splat layer (0-3)
Terrain3D.SetLayerScale(layer, scale)      // UV tiling scale per layer
```

## Implementation
**File:** `src/runtime/graphics/rt_terrain3d.c`

### Data Structure
Add to terrain struct:
```c
void *splat_map;           // Pixels RGBA
void *layer_textures[4];   // Per-layer Pixels
float layer_scales[4];     // UV tiling scales
```

### Rendering
Each backend needs a terrain-specific shader that samples 4 textures and blends by splat weights:
```glsl
vec4 splat = texture(splatMap, uv);
vec3 color = tex0.rgb * splat.r + tex1.rgb * splat.g + tex2.rgb * splat.b + tex3.rgb * splat.a;
```

For the software backend: per-pixel sample all 4 layers and lerp by splat channel values.

### Normal Computation Fix (GFX-082)
While working on terrain, also fix the non-square scaling bug:
- Current: Normal computation assumes square XZ scaling
- Fix: Scale finite-difference steps by actual terrain XZ dimensions

## Files Modified
- `src/runtime/graphics/rt_terrain3d.c/h` — Splat data + rendering
- All 4 backends — Terrain shader variant
- `src/il/runtime/runtime.def` — New RT_FUNC entries

## Verification
- Create terrain with grass (flat), dirt (slopes), rock (steep) splat map
- Textures should blend smoothly at boundaries
- Non-square terrain should have correct lighting
