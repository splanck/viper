# OGL-06: Wireframe Mode

## Current State

`wireframe` is ignored in the OpenGL draw path.

## Implementation

Add:

```c
if (wireframe)
    gl.PolygonMode(GL_FRONT_AND_BACK, GL_LINE);
else
    gl.PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
```

Required loader addition:

- `PolygonMode`

Required constants:

- `GL_FRONT_AND_BACK`
- `GL_LINE`
- `GL_FILL`

## Notes

- Apply the mode in the main draw path before `glDrawElements`.
- Keep the shadow pass depth-only and unaffected by this toggle unless an explicit shadow-wireframe mode is ever added.

## Files

- [`src/runtime/graphics/vgfx3d_backend_opengl.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_opengl.c)

## Done When

- `wireframe=true` renders lines
- `wireframe=false` renders filled triangles
