# D3D-06: Linear Distance Fog

## Context
Same gap as Metal (MTL-07). Fog params passed to begin_frame but D3D11 doesn't store or use them.

## Implementation

### Step 1: Store fog params in context
```c
// Add to d3d11_context_t:
int8_t fog_enabled;
float fog_near, fog_far;
float fog_color[3];
```

In `d3d11_begin_frame()`:
```c
ctx->fog_enabled = camera->fog_enabled;
ctx->fog_near = camera->fog_near;
ctx->fog_far = camera->fog_far;
memcpy(ctx->fog_color, camera->fog_color, 3 * sizeof(float));
```

### Step 2: Add fog to PerScene cbuffer
```hlsl
cbuffer PerScene : register(b1) {
    float4 cameraPosition;
    float4 ambientColor;
    float4 fogColor;    // .rgb = color, .a = enabled
    float fogNear;
    float fogFar;
    int lightCount;
    int _scenePad;
};
```

### Step 3: Apply fog in pixel shader
Before `return`:
```hlsl
if (fogColor.a > 0.5) {
    float dist = length(input.worldPos - cameraPosition.xyz);
    float fogRange = fogFar - fogNear;
    float fogFactor = saturate((dist - fogNear) / max(fogRange, 0.001));
    result = lerp(result, fogColor.rgb, fogFactor);
}
return float4(result, alpha);
```

### Step 4: Update C struct to match
```c
typedef struct {
    float cp[4];   // cameraPosition
    float ac[4];   // ambientColor
    float fc[4];   // fogColor (.rgb = color, .a = enabled flag)
    float fog_near;
    float fog_far;
    int32_t lc;    // lightCount
    int32_t _p;
} d3d_per_scene_t; // 64 bytes, was 48
```

### Step 5: Populate in submit_draw
Copy fog params to PerScene cbuffer:
```c
scene->fc[0] = ctx->fog_color[0];
scene->fc[1] = ctx->fog_color[1];
scene->fc[2] = ctx->fog_color[2];
scene->fc[3] = ctx->fog_enabled ? 1.0f : 0.0f;
scene->fog_near = ctx->fog_near;
scene->fog_far = ctx->fog_far;
```

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_d3d11.c` — context fields, begin_frame storage, PerScene cbuffer, pixel shader fog, submit_draw copy

## Testing
- Same tests as MTL-07
