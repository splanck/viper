# OGL-06: Wireframe Mode

## Context
`wireframe` parameter void-cast at line 642. OpenGL supports wireframe via `glPolygonMode`.

## Current Code
```c
(void)wireframe;
```

## Fix
Replace with:
```c
if (wireframe)
    gl.PolygonMode(GL_FRONT_AND_BACK, GL_LINE);
else
    gl.PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
```

Need to add `PolygonMode` to the GL function loader:
```c
LOAD(PolygonMode);
```

This is core GL 3.3 — no extension needed.

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_opengl.c` — replace void-cast with glPolygonMode, load function pointer

## Testing
- wireframe=true → wireframe edges
- wireframe=false → solid fill
