# OGL-01: Fix `uAlpha` Undeclared Uniform

## Current State

The fragment shader uses `uAlpha` but does not declare it. The C side already looks up and uploads the uniform, so the GLSL declaration is the only missing piece.

## Required Change

Add:

```glsl
uniform float uAlpha;
```

Place it with the other fragment uniforms in [`src/runtime/graphics/vgfx3d_backend_opengl.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_opengl.c).

## Notes

- No C-side API or vtable changes are required.
- This plan should land before OGL-07 because fog uses the same final alpha path.
- Keep `uAlpha` separate from `uDiffuseColor.a`; later plans layer texture alpha and fog on top of it.

## Files

- [`src/runtime/graphics/vgfx3d_backend_opengl.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_opengl.c)

## Done When

- Transparent draws respect `cmd->alpha`
- `glGetUniformLocation(..., "uAlpha")` is no longer `-1`
