# OGL-05: Spot Light Cone Attenuation

## Current State

The OpenGL fragment shader handles directional and point lights, but spot lights still fall through to the ambient branch.

## Shared State

No new shared runtime work is required:

- `vgfx3d_light_params_t` already carries `inner_cos` and `outer_cos`
- Canvas3D already fills those fields when building light params

## GLSL Changes

Add:

```glsl
uniform float uLightInnerCos[8];
uniform float uLightOuterCos[8];
```

Implement a `uLightType == 3` branch using:

- point-light style distance attenuation
- cone attenuation based on `inner_cos` / `outer_cos`
- smooth interpolation between the inner and outer cone

Use the same smoothstep-style Hermite blend already used by the software and Metal paths:

```glsl
float t = (spotDot - outerCos) / (innerCos - outerCos);
atten *= t * t * (3.0 - 2.0 * t);
```

## C-Side Changes

- fetch uniform locations for both arrays
- upload `inner_cos` and `outer_cos` for every active light

## Files

- [`src/runtime/graphics/vgfx3d_backend_opengl.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_opengl.c)

## Done When

- Spot lights brighten inside the inner cone
- Fade smoothly between inner and outer cone
- Contribute nothing outside the outer cone
