# Fix 114: Post-Processing Feature Parity Across Backends

## Severity: P2 — Medium

## Problem

Post-processing support varies significantly across GPU backends:

| Effect | Metal | D3D11 | OpenGL |
|--------|-------|-------|--------|
| Bloom | Dead code | Full | Full |
| FXAA | Dead code | Full | Full |
| Tonemap | Dead code | Full | Full |
| DOF | Missing | Full | Missing |
| SSAO | Missing | Full | Simplified |
| Motion Blur | Missing | Full | Full |

D3D11 has 100% coverage. Metal's shader code exists but isn't wired (see Fix 110).
OpenGL is missing DOF. Users get different visual results depending on their platform.

## Fix

### Phase 1: Wire Metal PostFX (covered by Fix 110)

Gets bloom, FXAA, tonemap, vignette, color grading working on Metal.

### Phase 2: Add DOF to Metal and OpenGL (~60 LOC per backend)

Depth-of-field shader that blurs pixels based on distance from focal plane:

```glsl
// Fragment shader DOF (same logic for MSL/HLSL/GLSL):
float depth = texture(depthTex, uv).r;
float coc = abs(depth - focalDist) * blurStrength;  // circle of confusion
coc = clamp(coc, 0.0, blurRange);
// Gaussian blur weighted by CoC radius
vec4 blurred = gaussianBlur(colorTex, uv, coc);
fragColor = mix(original, blurred, coc / blurRange);
```

### Phase 3: Add SSAO to Metal (~80 LOC)

Screen-space ambient occlusion with hemisphere sampling:

```metal
// For each pixel: sample N nearby depth values, compare with current depth
// If neighbors are closer (occluded), darken the pixel
float occlusion = 0.0;
for (int i = 0; i < NUM_SAMPLES; i++) {
    float3 samplePos = fragPos + kernel[i] * radius;
    float sampleDepth = depthAt(project(samplePos));
    occlusion += (sampleDepth < samplePos.z - bias) ? 1.0 : 0.0;
}
ao = 1.0 - (occlusion / NUM_SAMPLES);
```

### Phase 4: Add Motion Blur to Metal (~40 LOC)

Velocity-based blur using current and previous frame positions:

```metal
float2 velocity = (currClipPos.xy - prevClipPos.xy) * intensity;
float4 color = float4(0);
for (int i = 0; i < NUM_SAMPLES; i++) {
    float2 offset = velocity * (float(i) / float(NUM_SAMPLES) - 0.5);
    color += colorTex.sample(samp, uv + offset);
}
color /= float(NUM_SAMPLES);
```

## Files to Modify

| File | Change |
|------|--------|
| `src/runtime/graphics/vgfx3d_backend_metal.m` | Wire PostFX + add DOF/SSAO/MotionBlur shaders |
| `src/runtime/graphics/vgfx3d_backend_opengl.c` | Add DOF shader |

## Test

- Enable each PostFX effect individually on each platform
- Visual comparison: D3D11 vs Metal vs OpenGL should produce similar results
- Performance: verify PostFX doesn't drop below 30fps on integrated GPUs
