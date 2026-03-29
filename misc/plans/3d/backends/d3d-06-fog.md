# D3D-06: Linear Distance Fog

## Current State

Fog parameters already arrive in `begin_frame()`, but the D3D11 backend discards them.

## Gap In The Earlier Plan

The earlier note added fog only to the lit path, but the shader's unlit early return would still bypass fog. The plan needs to define that behavior explicitly.

Recommended behavior for scene consistency:

- apply fog to both lit and unlit outputs

## Implementation

Add to `d3d11_context_t`:

```c
int8_t fog_enabled;
float fog_near, fog_far;
float fog_color[3];
```

Store them in `d3d11_begin_frame()`.

Extend `PerScene` with:

- `float4 fogColor`
- `float fogNear`
- `float fogFar`

Then restructure the pixel shader so it computes `finalRgb` first and applies fog once at the end.

Suggested pattern:

```hlsl
float3 finalRgb = ...;
if (fogColor.a > 0.5) {
    float dist = length(input.worldPos - cameraPosition.xyz);
    float fogRange = max(fogFar - fogNear, 0.001);
    float fogFactor = saturate((dist - fogNear) / fogRange);
    finalRgb = lerp(finalRgb, fogColor.rgb, fogFactor);
}
return float4(finalRgb, finalAlpha);
```

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)

## Done When

- distant geometry blends toward the configured fog color
- unlit materials do not bypass fog
