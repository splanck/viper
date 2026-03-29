# OGL-07: Linear Distance Fog

## Depends On

- OGL-01

## Current State

`begin_frame()` receives fog parameters through `vgfx3d_camera_params_t`, but the OpenGL backend currently discards them.

## Gap In The Earlier Plan

The earlier note added fog only to the lit path, but the shader's early unlit return would still bypass fog. The plan must decide that behavior explicitly.

Recommended behavior for parity and scene consistency:

- apply fog to both lit and unlit outputs

## Implementation

Add to `gl_context_t`:

```c
int8_t fog_enabled;
float fog_near, fog_far;
float fog_color[3];
```

Populate them in `gl_begin_frame()` from `cam`.

Add fragment uniforms:

```glsl
uniform vec3 uFogColor;
uniform float uFogNear;
uniform float uFogFar;
uniform int uFogEnabled;
```

Restructure the fragment shader to produce a `finalRgb` first, then apply fog once:

```glsl
vec3 finalRgb = ...;
if (uFogEnabled != 0) {
    float dist = length(vWorldPos - uCameraPos);
    float fogRange = max(uFogFar - uFogNear, 0.001);
    float fogFactor = clamp((dist - uFogNear) / fogRange, 0.0, 1.0);
    finalRgb = mix(finalRgb, uFogColor, fogFactor);
}
FragColor = vec4(finalRgb, finalAlpha);
```

That structure avoids the unlit-path escape hatch and composes cleanly with later texture and emissive work.

## C-Side Upload

Fetch locations for the four fog uniforms and upload them from the cached frame state in every draw.

## Files

- [`src/runtime/graphics/vgfx3d_backend_opengl.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_opengl.c)

## Done When

- Distant geometry blends toward the configured fog color
- Unlit materials do not bypass fog
