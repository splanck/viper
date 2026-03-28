# SW-04: Two-Sided Rendering in Software Rasterizer

## Context
Metal and OpenGL support two-sided rendering via cull mode toggle. The software backend always culls back-faces based on screen-space winding order. Two-sided rendering is needed for foliage, cloth, thin walls seen from both sides.

## Current State
- Line 401 in `raster_triangle()`: checks screen-space area sign for backface culling
- `backface_cull` parameter at line 395 controls whether culling is active
- When `backface_cull=0`, culling is skipped (two-sided rendering already works)

**Wait — let me verify.** If `backface_cull=0`, does the software backend draw both faces?

Actually, reading line 401 more carefully: the backface check only happens when `backface_cull` is true. When false, both faces are drawn. So **two-sided rendering already works** in the software backend via the `backface_cull` parameter.

## Status: ALREADY IMPLEMENTED

The `backface_cull` parameter at line 395 of `raster_triangle()` controls this:
- `backface_cull=1` (default): back-faces are culled
- `backface_cull=0`: both faces drawn (two-sided)

Canvas3D passes this through from the vtable's `submit_draw` call. The `Canvas3D.SetBackfaceCull(false)` API already exists.

## No Changes Needed
Update the audit matrix to mark software two-sided as ✅.
