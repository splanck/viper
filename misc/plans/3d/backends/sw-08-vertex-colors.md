# SW-08: Vertex Color Support in Software Rasterizer

## Context
The vertex format includes `color[4]` per vertex. GPU backends (Metal, OpenGL, D3D11) pass vertex color to the fragment shader. The software backend ignores vertex colors entirely — `compute_lighting()` overwrites `v->color` with `cmd->diffuse_color`.

## Current State
- `compute_lighting()` line 213: `v->color[0] = cmd->diffuse_color[0]` — overwrites vertex color
- The original vertex color from the mesh is lost after lighting
- GPU backends multiply vertex color into the final result, making per-vertex coloring work

## Implementation

### Change: Multiply vertex color with material diffuse color

In `compute_lighting()`, instead of overwriting vertex color with diffuse color, multiply them:

**Before (line 212-217):**
```c
if (cmd->unlit) {
    v->color[0] = cmd->diffuse_color[0];
    v->color[1] = cmd->diffuse_color[1];
    v->color[2] = cmd->diffuse_color[2];
    v->color[3] = cmd->diffuse_color[3];
    return;
}
```

**After:**
```c
if (cmd->unlit) {
    v->color[0] = cmd->diffuse_color[0] * v->color[0];
    v->color[1] = cmd->diffuse_color[1] * v->color[1];
    v->color[2] = cmd->diffuse_color[2] * v->color[2];
    v->color[3] = cmd->diffuse_color[3] * v->color[3];
    return;
}
```

And in the lit path (line 238-240), use vertex color * diffuse instead of just diffuse:
```c
float dr = cmd->diffuse_color[0] * v->color[0];
float dg = cmd->diffuse_color[1] * v->color[1];
float db = cmd->diffuse_color[2] * v->color[2];

float r = ambient[0] * dr;
float g = ambient[1] * dg;
float b = ambient[2] * db;
```

Then use `dr, dg, db` instead of `cmd->diffuse_color[0..2]` throughout the lighting calculation (lines 326-327 in the diffuse accumulation).

### Vertex Color Initialization
The mesh vertex `vgfx3d_vertex_t.color[4]` defaults to `{1,1,1,1}` (white) when created via `rt_mesh3d_add_vertex()`. Multiplying white vertex color with any diffuse color produces the diffuse color — backward compatible.

Only meshes with explicitly set per-vertex colors (e.g., from OBJ files with vertex colors, or programmatic construction) will see a difference.

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_sw.c` — `compute_lighting()` multiply vertex color into diffuse

## Testing
- Default mesh (white vertex colors) → renders identical to before (white * diffuse = diffuse)
- Mesh with red vertex on one corner, blue on another → smooth gradient across surface
- Unlit mesh with vertex colors → directly shows vertex color * diffuse
