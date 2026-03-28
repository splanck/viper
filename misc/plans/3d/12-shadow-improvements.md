# Plan: Shadow Quality Improvements

## Overview
Shadows exist but lack quality control. Add resolution settings, soft shadows, and cascaded shadow maps for outdoor scenes.

## 1. Shadow Resolution Control

### API
```
Canvas3D.EnableShadows(lightIndex, resolution)   // resolution: 512, 1024, 2048, 4096
Canvas3D.SetShadowSoftness(softness)             // 0.0 (hard) to 1.0 (very soft)
```

### Implementation
- Shadow map render target already exists via `EnableShadows`
- Add `shadow_resolution` parameter to creation
- Backends allocate depth texture at specified resolution
- Larger = sharper shadows, more VRAM

## 2. Soft Shadows (PCF)
Percentage-closer filtering in fragment shader:
```glsl
float shadow = 0.0;
for (int x = -1; x <= 1; x++) {
    for (int y = -1; y <= 1; y++) {
        float depth = texture(shadowMap, uv + vec2(x, y) * texelSize).r;
        shadow += (fragDepth > depth + bias) ? 0.0 : 1.0;
    }
}
shadow /= 9.0;
```
3x3 kernel = decent soft edges. 5x5 for higher quality.

## 3. Cascaded Shadow Maps (CSM) for Directional Lights
Split view frustum into 3-4 cascades (near, mid, far). Each cascade gets its own shadow map at different scale:
- Cascade 0: 0-10m (highest resolution)
- Cascade 1: 10-50m
- Cascade 2: 50-200m
- Cascade 3: 200-1000m (lowest resolution)

Fragment shader selects cascade based on fragment distance from camera.

## Files Modified
- `src/runtime/graphics/rt_canvas3d.c` — Shadow setup with resolution
- All 4 backends — PCF sampling, cascade selection
- `src/il/runtime/runtime.def` — Updated signatures

## Verification
- Shadow resolution 512 vs 4096 — visible quality difference
- Soft shadows: edges should be blurred, not aliased staircase
- CSM: near objects have sharp shadows, distant objects have softer shadows, no obvious cascade boundary
