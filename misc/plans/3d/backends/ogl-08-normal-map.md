# OGL-08: Normal Map Sampling

## Context
Same gap as MTL-04 and D3D-07. Tangent available in vertex input but not passed to fragment. No normal map sampler.

## Implementation

### Step 1: Pass tangent through to fragment
Add to VertexOut/fragment inputs:
```glsl
out vec3 vTangent;
// In vertex shader:
vTangent = (uModelMatrix * vec4(aTangent, 0.0)).xyz;
```

Fragment shader:
```glsl
in vec3 vTangent;
```

### Step 2: Add normal map sampler
```glsl
uniform sampler2D uNormalTex;
uniform int uHasNormalMap;
```

### Step 3: Sample and perturb normal
```glsl
vec3 N = normalize(vNormal);
if (uHasNormalMap != 0) {
    vec3 T = normalize(vTangent);
    T = normalize(T - N * dot(T, N));
    vec3 B = cross(N, T);
    vec3 mapN = texture(uNormalTex, vUV).rgb * 2.0 - 1.0;
    N = normalize(T * mapN.x + B * mapN.y + N * mapN.z);
}
```

If `T` degenerates after orthonormalization, skip the normal-map path for that fragment.

### Step 4: Bind normal map in C
```c
if (cmd->normal_map) {
    GLuint normTex = get_or_create_texture(ctx, cmd->normal_map);
    gl.ActiveTexture(GL_TEXTURE1);
    gl.BindTexture(GL_TEXTURE_2D, normTex);
    gl.Uniform1i(ctx->uNormalTex, 1);
    gl.Uniform1i(ctx->uHasNormalMap, 1);
    gl.ActiveTexture(GL_TEXTURE0); // restore
} else {
    gl.Uniform1i(ctx->uHasNormalMap, 0);
}
```

## Depends On
- OGL-03 (texture infrastructure)
- OGL-04 (texture cache)

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_opengl.c` — GLSL tangent varying, normal map sampler, TBN math, C binding

## Testing
- Same tests as MTL-04 and D3D-07
