# MTL-02: Spot Light Cone Attenuation

## Context
Spot lights (type 3) fall through to the `else` clause in the Metal shader, which adds light as ambient (no direction or cone). The Light struct in the shader doesn't have inner_cos/outer_cos fields.

## Current Shader (broken)
```metal
} else {
    result += lights[i].color.rgb * lights[i].intensity * material.diffuseColor.rgb;
    continue;
}
```

## Fix

### Step 1: Add cone fields to Metal Light struct
```metal
struct Light {
    int type; float _p0, _p1, _p2;
    float4 direction;
    float4 position;
    float4 color;
    float intensity;
    float attenuation;
    float inner_cos;  // NEW
    float outer_cos;  // NEW (replaces _p3[2])
};
```

### Step 2: Add spot light branch in fragment shader
Replace the `else` clause:
```metal
} else if (lights[i].type == 3) {
    // Spot light: point light + cone attenuation
    float3 tl = lights[i].position.xyz - in.worldPos;
    float d = length(tl); L = tl / max(d, 0.0001);
    atten = 1.0 / (1.0 + lights[i].attenuation * d * d);
    // Cone: dot of -L with spot direction
    float spotDot = dot(-L, normalize(lights[i].direction.xyz));
    if (spotDot < lights[i].outer_cos) {
        atten = 0.0;
    } else if (spotDot < lights[i].inner_cos) {
        float t = (spotDot - lights[i].outer_cos) /
                  (lights[i].inner_cos - lights[i].outer_cos);
        atten *= t * t * (3.0 - 2.0 * t); // smoothstep
    }
} else {
    // Ambient fallback for unknown types
    result += lights[i].color.rgb * lights[i].intensity * baseColor;
    continue;
}
```

### Step 3: Update C-side light buffer copy
In `metal_submit_draw()`, the light struct copy (line ~664) must include `inner_cos` and `outer_cos` from `vgfx3d_light_params_t`.

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_metal.m` — shader Light struct + fragment spot branch + C light copy

## Testing
- Spot light pointing down → cone visible on ground plane
- Object outside cone → not lit by spot
- Smooth falloff between inner and outer cone angles
