# OGL-08: Normal Map Sampling

## Depends On

- OGL-03
- OGL-04

## Current State

The vertex format already contains tangents, but the OpenGL shader ignores them and there is no normal-map sampler path.

## GLSL Changes

Add tangent passthrough:

```glsl
out vec3 vTangent;
```

Vertex shader:

```glsl
vTangent = (uModelMatrix * vec4(aTangent, 0.0)).xyz;
```

Fragment shader additions:

```glsl
in vec3 vTangent;
uniform sampler2D uNormalTex;
uniform int uHasNormalMap;
```

Perturb the normal only when a map is present:

```glsl
vec3 N = normalize(vNormal);
if (uHasNormalMap != 0) {
    vec3 T = normalize(vTangent);
    T = normalize(T - N * dot(T, N));
    if (length(T) > 0.001) {
        vec3 B = cross(N, T);
        vec3 mapN = texture(uNormalTex, vUV).rgb * 2.0 - 1.0;
        N = normalize(T * mapN.x + B * mapN.y + N * mapN.z);
    }
}
```

## C-Side Binding

- bind normal map on texture unit `1`
- set `uNormalTex = 1`
- set `uHasNormalMap` accordingly
- restore active texture to `GL_TEXTURE0` after setup
- when no normal map is present:
  - set `uHasNormalMap = 0`
  - bind texture `0` on unit `1` to avoid stale state

## Notes

- Tangent transformation with the model matrix is acceptable here because the shader re-orthonormalizes `T` against the final normal.
- Degenerate tangents must skip the perturbation path cleanly.

## Files

- [`src/runtime/graphics/vgfx3d_backend_opengl.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_opengl.c)

## Done When

- Tangent-space normal maps visibly perturb lighting
- Degenerate tangents fall back to the geometric normal without artifacts
