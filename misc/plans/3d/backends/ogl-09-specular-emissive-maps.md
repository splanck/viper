# OGL-09: Specular + Emissive Map Sampling

## Depends On

- OGL-03
- OGL-04

## Current State

The OpenGL backend only supports uniform specular and emissive material terms. Texture-driven modulation is missing.

## GLSL Changes

Add:

```glsl
uniform sampler2D uSpecularTex;
uniform int uHasSpecularMap;
uniform sampler2D uEmissiveTex;
uniform int uHasEmissiveMap;
```

Specular setup:

```glsl
vec3 specColor = uSpecularColor.rgb;
float shine = uSpecularColor.w;
if (uHasSpecularMap != 0) {
    specColor *= texture(uSpecularTex, vUV).rgb;
}
```

Emissive setup:

```glsl
vec3 emissive = uEmissiveColor;
if (uHasEmissiveMap != 0) {
    emissive *= texture(uEmissiveTex, vUV).rgb;
}
result += emissive;
```

Keep shininess uniform-driven in v1. This plan does not introduce a gloss map convention.

## C-Side Binding

- bind specular map on unit `2`
- bind emissive map on unit `3`
- set `uSpecularTex = 2`, `uEmissiveTex = 3`
- clear `uHasSpecularMap` / `uHasEmissiveMap` and unbind stale textures when absent
- restore active texture to `GL_TEXTURE0` before the draw

## Notes

- This plan should not change diffuse or normal-map behavior.
- Emissive texture contribution remains additive on top of lit shading and fog.

## Files

- [`src/runtime/graphics/vgfx3d_backend_opengl.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_opengl.c)

## Done When

- Specular highlights can be masked or tinted per texel
- Emissive maps add light independent of scene lighting
