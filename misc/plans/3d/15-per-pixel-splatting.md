# Plan 15: Per-Pixel Terrain Splatting (replacing baked approach)

## Context

The current baked approach pre-blends splat textures on CPU into a single Pixels object. This is outdated — per-pixel shader splatting has been standard since 2005. The fix requires extending the draw command with splat texture slots and updating rendering backends.

**Note:** OpenGL and D3D11 backends don't sample textures at all — their shaders use vertex colors only. Those backends will be updated separately. This plan covers the draw command extension, software backend, and Metal backend only. OpenGL/D3D11 splat support deferred until their basic texture sampling is implemented.

## Phase 1: Extend Draw Command with Splat Texture Slots

**File:** `src/runtime/graphics/vgfx3d_backend.h`

Add to `vgfx3d_draw_cmd_t`:
```c
void *splat_map;                    // RGBA weight texture
void *splat_layers[4];              // Layer textures
float splat_layer_scales[4];        // UV tiling per layer
int8_t has_splat;                   // Flag: use splat blending
```

## Phase 2: Pass Splat Data Through Draw Path

**File:** `src/runtime/graphics/rt_canvas3d.c`

In `rt_canvas3d_draw_terrain`, instead of calling `rt_canvas3d_draw_mesh` per chunk, build the deferred draw command directly with splat fields populated from the terrain struct.

## Phase 3: Software Backend — Per-Pixel Splat Sampling

**File:** `src/runtime/graphics/vgfx3d_backend_sw.c`

In the per-pixel rasterization loop, after diffuse texture sampling:
- Sample splat map at interpolated UV → get RGBA weights
- Normalize weights
- For each layer with weight > 0: sample layer texture at `UV * layerScale`
- Blend results, replace diffuse color

## Phase 4: Metal Backend — Fragment Shader Splat

**File:** `src/runtime/graphics/vgfx3d_backend_metal.m`

- Add 5 texture parameters to MSL fragment shader (slots 5-9)
- Add hasSplat flag + layerScales to material cbuffer
- Splat blending in fragment body
- Bind textures in render encoder when has_splat is set

## Phase 5: Remove Baked Approach

**File:** `src/runtime/graphics/rt_terrain3d.c`

- Remove `bake_splat_texture()` function
- Remove bake call in `rt_canvas3d_draw_terrain`
- Pass splat_map + layer_textures + layer_scales through draw command

## Files Modified (Total: 6)

| File | Changes |
|------|---------|
| `vgfx3d_backend.h` | Add splat fields to draw command |
| `rt_canvas3d.c` | Terrain draw path populates splat fields |
| `vgfx3d_backend_sw.c` | Per-pixel splat sampling in rasterizer |
| `vgfx3d_backend_metal.m` | MSL fragment shader splat + texture binding |
| `rt_terrain3d.c` | Remove bake, pass splat through draw path |
| `rt_canvas3d_internal.h` | Extend deferred_draw_t if needed |

**Deferred:** OpenGL + D3D11 splat support (requires basic texture sampling first — separate work item).

## Verification
- Build: `cmake --build build -j`
- Tests: `ctest --test-dir build` (existing terrain tests + splat tests)
- Visual: Render terrain with grass/dirt/rock splat — smooth blending at boundaries with tiling detail
