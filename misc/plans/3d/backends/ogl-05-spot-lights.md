# OGL-05: Spot Light Cone Attenuation

## Context
Same as MTL-02 and D3D-04. Spot lights (type 3) fall to ambient. Light uniform arrays lack cone fields.

## Implementation

### Step 1: Add cone uniforms to GLSL
```glsl
uniform float uLightInnerCos[8];
uniform float uLightOuterCos[8];
```

### Step 2: Add spot branch in fragment shader
Replace `else` clause:
```glsl
} else if (uLightType[i] == 3) {
    vec3 tl = uLightPos[i] - vWorldPos;
    float d = length(tl); L = tl / max(d, 0.0001);
    atten = 1.0 / (1.0 + uLightAtten[i] * d * d);
    float spotDot = dot(-L, normalize(uLightDir[i]));
    if (spotDot < uLightOuterCos[i]) {
        atten = 0.0;
    } else if (spotDot < uLightInnerCos[i]) {
        float t = (spotDot - uLightOuterCos[i]) /
                  (uLightInnerCos[i] - uLightOuterCos[i]);
        atten *= t * t * (3.0 - 2.0 * t);
    }
} else {
    result += uLightColor[i] * uLightIntensity[i] * baseColor;
    continue;
}
```

### Step 3: Get uniform locations + upload in C
```c
ctx->uLightInnerCos[i] = gl.GetUniformLocation(ctx->program, name);
// In submit_draw:
gl.Uniform1f(ctx->uLightInnerCos[i], lights[i].inner_cos);
gl.Uniform1f(ctx->uLightOuterCos[i], lights[i].outer_cos);
```

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_opengl.c` — GLSL cone uniforms, fragment spot branch, C uniform upload

## Testing
- Same tests as MTL-02 and D3D-04
