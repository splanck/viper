# SW-01: Normal Map Sampling in Software Rasterizer

## Context
Normal maps exist in `vgfx3d_draw_cmd_t` (field `normal_map`) but no backend samples them. The software backend computes lighting per-vertex using the mesh's geometric normal. Normal maps would perturb the normal per-pixel for surface detail without extra geometry.

## Current State
- `compute_lighting()` at line 206 uses `v->normal[0..2]` from the mesh vertex
- Lighting is Gouraud (per-vertex), so normals are interpolated across the triangle
- The `cmd->normal_map` pointer is passed but never read

## Implementation

### Step 1: Change lighting from per-vertex to per-pixel
The current Gouraud model computes lighting per-vertex then interpolates colors. Normal mapping requires per-pixel normal lookup, which means per-pixel lighting.

**In `raster_triangle()` (line 383):**
- Currently: vertex colors are pre-computed by `compute_lighting()`, interpolated via barycentric weights (line 443)
- Change: interpolate world position and normal per-pixel, then compute lighting per-pixel

Add to `screen_vert_t`:
```c
float nx, ny, nz; // interpolated normal (for per-pixel lighting)
```

In the per-pixel loop, after barycentric interpolation of position:
```c
float nx = b0 * v0->nx + b1 * v1->nx + b2 * v2->nx;
float ny = b0 * v0->ny + b1 * v1->ny + b2 * v2->ny;
float nz = b0 * v0->nz + b1 * v1->nz + b2 * v2->nz;
// normalize
float nlen = sqrtf(nx*nx + ny*ny + nz*nz);
if (nlen > 1e-7f) { nx /= nlen; ny /= nlen; nz /= nlen; }
```

### Step 2: Sample normal map and perturb normal
After interpolating the geometric normal per-pixel:
```c
if (cmd->normal_map) {
    float tnr, tng, tnb, tna;
    sample_texture(normal_map_view, u, vc, &tnr, &tng, &tnb, &tna);
    // Normal map is in tangent space: [0,1] → [-1,1]
    float map_nx = tnr * 2.0f - 1.0f;
    float map_ny = tng * 2.0f - 1.0f;
    float map_nz = tnb * 2.0f - 1.0f;

    // Build TBN matrix from interpolated normal + tangent
    // Tangent must also be interpolated per-pixel (add to screen_vert_t)
    float tx = b0*v0->tx + b1*v1->tx + b2*v2->tx;
    float ty = b0*v0->ty + b1*v1->ty + b2*v2->ty;
    float tz = b0*v0->tz + b1*v1->tz + b2*v2->tz;
    // Bitangent = cross(normal, tangent)
    float bx = ny*tz - nz*ty;
    float by = nz*tx - nx*tz;
    float bz = nx*ty - ny*tx;

    // Transform normal map from tangent space to world space
    float pnx = tx*map_nx + bx*map_ny + nx*map_nz;
    float pny = ty*map_nx + by*map_ny + ny*map_nz;
    float pnz = tz*map_nx + bz*map_ny + nz*map_nz;
    // Renormalize
    float plen = sqrtf(pnx*pnx + pny*pny + pnz*pnz);
    if (plen > 1e-7f) { nx = pnx/plen; ny = pny/plen; nz = pnz/plen; }
}
```

### Step 3: Compute per-pixel lighting with perturbed normal
Move the lighting calculation from `compute_lighting()` into the per-pixel loop, using the (possibly perturbed) normal. This is the same Blinn-Phong math currently in `compute_lighting()` but evaluated per-pixel instead of per-vertex.

### Step 4: Add tangent to screen_vert_t and pipe_vert_t
The vertex tangent (`vgfx3d_vertex_t.tangent[3]`) must flow through the pipeline:
- Copy tangent from mesh vertex to `pipe_vert_t` during vertex processing
- Transform tangent with the same upper-3x3 basis used for normals, then orthonormalize it against the interpolated normal before building the bitangent
- Pass through to `screen_vert_t` for per-pixel interpolation

If the interpolated tangent length collapses, skip the normal-map perturbation for that pixel rather than generating NaNs.

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_sw.c` — screen_vert_t, pipe_vert_t, raster_triangle, per-pixel lighting

## Testing
- Create mesh with normal map Pixels (e.g., blue-tinted flat normal [0.5, 0.5, 1.0])
- Verify flat normal map produces same result as no normal map
- Create tilted normal map — verify lighting changes per-pixel
- Performance: normal mapping adds ~5 texture samples per pixel; verify <2x slowdown

## Dependencies
- Tangent data must be present on the mesh (`CalcTangents()` already exists)
- Normal map must be in tangent space (standard convention: RGB = XYZ, [0,1] → [-1,1])
