# Plan: Terrain Texture Splatting — COMPLETE

## What Was Added (2026-03-28)

### New Terrain3D API
- `SetSplatMap(pixels)` — RGBA Pixels where each channel controls blend weight for 4 layers
- `SetLayerTexture(layer, pixels)` — Set texture for layer 0-3
- `SetLayerScale(layer, scale)` — UV tiling scale per layer (default 1.0)

### Implementation: Baked Splat Texture
When a splat map is set and chunks need rebuilding, the terrain bakes a blended diffuse texture:
1. For each texel in the splat map, sample RGBA weights (normalized)
2. For each layer with weight > 0, sample the layer texture at `UV * layerScale`
3. Blend: `color = sum(layer_color * weight)`
4. Write to a new Pixels object, set as the material's diffuse texture

This "bake" approach works across all 4 backends without shader changes. Quality is limited to splat map resolution (per-texel, not per-pixel), but is practical for the current rendering pipeline.

### Also Fixed
- Normal computation in `get_normal_at` was already correct for non-square scaling (uses `t->scale[0]` for Y component normalization)

### Files Changed
- `src/runtime/graphics/rt_terrain3d.c/h` — Splat map storage, layer textures/scales, bake function
- `src/il/runtime/runtime.def` — 3 RT_FUNC entries + 3 RT_METHOD entries on Terrain3D class

### Tests Added
5 new tests in `test_rt_canvas3d.cpp`:
- `test_terrain_create`: Basic construction
- `test_terrain_set_splat_map`: Accepts Pixels
- `test_terrain_set_layer_texture`: 4 layers + out-of-range safety
- `test_terrain_set_layer_scale`: UV scale + out-of-range safety
- `test_terrain_null_safety`: All splat functions with NULL

Total: 72/72 canvas3d, 1358/1358 full suite.
