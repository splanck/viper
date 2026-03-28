# D3D-04: Spot Light Cone Attenuation

## Context
Identical issue to Metal (MTL-02). Spot lights (type 3) fall to `else` clause, treated as ambient. Light struct lacks cone fields.

## Fix

### Step 1: Add cone fields to HLSL Light struct
```hlsl
struct Light {
    int type; float _p0, _p1, _p2;
    float4 direction;
    float4 position;
    float4 color;
    float intensity;
    float attenuation;
    float inner_cos;
    float outer_cos;
};
```

### Step 2: Add spot branch in pixel shader
Replace `else` clause (line 128-130):
```hlsl
} else if (lights[i].type == 3) {
    float3 tl = lights[i].position.xyz - input.worldPos;
    float d = length(tl); L = tl / max(d, 0.0001);
    atten = 1.0 / (1.0 + lights[i].attenuation * d * d);
    float spotDot = dot(-L, normalize(lights[i].direction.xyz));
    if (spotDot < lights[i].outer_cos) {
        atten = 0.0;
    } else if (spotDot < lights[i].inner_cos) {
        float t = (spotDot - lights[i].outer_cos) /
                  (lights[i].inner_cos - lights[i].outer_cos);
        atten *= t * t * (3.0 - 2.0 * t);
    }
} else {
    result += lights[i].color.rgb * lights[i].intensity * baseColor;
    continue;
}
```

### Step 3: Update C-side light buffer copy
In `submit_draw()`, the light struct copy (lines 578-591) must include `inner_cos` and `outer_cos` from `vgfx3d_light_params_t`.

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_d3d11.c` — HLSL Light struct + pixel shader spot branch + C light copy

## Testing
- Same tests as MTL-02
