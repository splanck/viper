# MTL-07: Linear Distance Fog

## Context
Software backend implements fog per-pixel. Metal ignores fog parameters passed to `begin_frame`. Canvas3D passes `fog_enabled`, `fog_near`, `fog_far`, `fog_color` but Metal's `begin_frame` doesn't store them.

## Implementation

### Step 1: Store fog params in Metal context
Add to VGFXMetalContext:
```objc
@property (nonatomic) BOOL fogEnabled;
@property (nonatomic) float fogNear, fogFar;
@property (nonatomic) float fogColor[3];
```

In `metal_begin_frame()`, store from camera params:
```objc
ctx.fogEnabled = camera->fog_enabled;
ctx.fogNear = camera->fog_near;
ctx.fogFar = camera->fog_far;
ctx.fogColor[0] = camera->fog_color[0];
ctx.fogColor[1] = camera->fog_color[1];
ctx.fogColor[2] = camera->fog_color[2];
```

### Step 2: Add fog to PerScene cbuffer
```metal
struct PerScene {
    float4 cameraPosition;
    float4 ambientColor;
    float4 fogColor;      // NEW: .rgb = color, .a = enabled flag
    float fogNear;        // NEW
    float fogFar;         // NEW
    int lightCount;
    int _pad;
};
```

### Step 3: Apply fog in fragment shader
After computing final lit color, before return:
```metal
if (scene.fogColor.a > 0.5) { // fog enabled
    float dist = length(in.worldPos - scene.cameraPosition.xyz);
    float fogRange = scene.fogFar - scene.fogNear;
    float fogFactor = clamp((dist - scene.fogNear) / max(fogRange, 0.001), 0.0, 1.0);
    result = mix(result, scene.fogColor.rgb, fogFactor);
}
return float4(result, material.alpha);
```

### Step 4: Populate fog in submit_draw
In `metal_submit_draw()`, copy fog params to the PerScene buffer:
```objc
scn.fogColor = simd_make_float4(ctx.fogColor[0], ctx.fogColor[1], ctx.fogColor[2],
                                 ctx.fogEnabled ? 1.0 : 0.0);
scn.fogNear = ctx.fogNear;
scn.fogFar = ctx.fogFar;
```

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_metal.m` — context fields, begin_frame storage, PerScene struct, fragment fog, submit_draw copy

## Testing
- Enable fog with near=10, far=100, color=gray → distant objects fade to gray
- Fog disabled → no visual change
- Same parameters as software backend → matching visual result
