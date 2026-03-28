# SW-02: Specular Map Sampling in Software Rasterizer

## Context
`vgfx3d_draw_cmd_t` has `specular_map` field but no backend samples it. Currently specular color and shininess are uniform across the entire surface. A specular map modulates these per-texel.

## Current State
- `compute_lighting()` specular section (search for `ndh` or `spec = powf`): uses `cmd->specular[0..2]` and `cmd->shininess`
- `cmd->specular_map` is passed but never read
- Line numbers shift frequently — search by variable/function name

## Implementation

**Depends on SW-01** (per-pixel lighting must be in place first — specular map sampling is per-pixel by nature).

Before the rasterization loop, convert `cmd->specular_map` to `sw_pixels_view` (same pattern as diffuse/emissive texture conversion already in the submit_draw function).

After computing the per-pixel specular term in the per-pixel lighting loop:
```c
float spec_r = cmd->specular[0];
float spec_g = cmd->specular[1];
float spec_b = cmd->specular[2];
float shine = cmd->shininess;

if (cmd->specular_map && specular_map_view.data) {
    float smr, smg, smb, sma;
    sample_texture(&specular_map_view, u, vc, &smr, &smg, &smb, &sma);
    // Modulate specular color by map RGB
    spec_r *= smr;
    spec_g *= smg;
    spec_b *= smb;
}
```

Then use `spec_r, spec_g, spec_b, shine` in the Blinn-Phong calculation instead of the uniform values.

Leave `shine` uniform in v1 unless the engine adopts a documented gloss-map convention shared across backends.

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_sw.c` — per-pixel specular map lookup in lighting loop

## Testing
- White specular map → same as no map
- Black specular map → no specular highlights at all
- Checkerboard specular map → alternating shiny/matte regions
