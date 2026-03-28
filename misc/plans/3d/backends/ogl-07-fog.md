# OGL-07: Linear Distance Fog

## Context
Same gap as MTL-07 and D3D-06. Fog params passed to begin_frame but OpenGL doesn't store or use them.

## Implementation

### Step 1: Store fog params in context
```c
// Add to gl_context_t:
int8_t fog_enabled;
float fog_near, fog_far;
float fog_color[3];
```

### Step 2: Store in begin_frame
```c
ctx->fog_enabled = camera->fog_enabled;
ctx->fog_near = camera->fog_near;
ctx->fog_far = camera->fog_far;
memcpy(ctx->fog_color, camera->fog_color, 3 * sizeof(float));
```

### Step 3: Add fog uniforms to GLSL
```glsl
uniform vec3 uFogColor;
uniform float uFogNear;
uniform float uFogFar;
uniform int uFogEnabled;
```

### Step 4: Apply fog in fragment shader
Before `FragColor`:
```glsl
if (uFogEnabled != 0) {
    float dist = length(vWorldPos - uCameraPos);
    float fogRange = uFogFar - uFogNear;
    float fogFactor = clamp((dist - uFogNear) / max(fogRange, 0.001), 0.0, 1.0);
    result = mix(result, uFogColor, fogFactor);
}
FragColor = vec4(result, uAlpha);
```

### Step 5: Get uniform locations + upload
```c
ctx->uFogColor = gl.GetUniformLocation(ctx->program, "uFogColor");
ctx->uFogNear = gl.GetUniformLocation(ctx->program, "uFogNear");
ctx->uFogFar = gl.GetUniformLocation(ctx->program, "uFogFar");
ctx->uFogEnabled = gl.GetUniformLocation(ctx->program, "uFogEnabled");

// In submit_draw:
gl.Uniform3f(ctx->uFogColor, ctx->fog_color[0], ctx->fog_color[1], ctx->fog_color[2]);
gl.Uniform1f(ctx->uFogNear, ctx->fog_near);
gl.Uniform1f(ctx->uFogFar, ctx->fog_far);
gl.Uniform1i(ctx->uFogEnabled, ctx->fog_enabled);
```

## Depends On
- OGL-01 (alpha fix — the `FragColor` line uses `uAlpha` which requires the OGL-01 declaration fix)

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_opengl.c` — context fields, begin_frame storage, GLSL uniforms, fragment fog, upload

## Testing
- Same tests as MTL-07 and D3D-06
