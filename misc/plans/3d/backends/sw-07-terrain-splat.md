# SW-07: Per-Pixel Terrain Splatting in Software Rasterizer

## Context
Plan 15 covers the full per-pixel splatting across all backends. This plan details the software backend specifically. The current baked approach pre-blends textures on CPU into one Pixels object — we're replacing it with per-pixel splat sampling during rasterization.

This integration belongs in [`src/runtime/graphics/rt_terrain3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_terrain3d.c), not in generic Canvas3D code. Also, do not remove the baked terrain fallback until the intended GPU backends consume the same shared splat payload.

## Current State
- Terrain draws via `rt_canvas3d_draw_mesh()` per chunk with a single material
- `bake_splat_texture()` in `rt_terrain3d.c` pre-computes blended texture
- Software rasterizer samples the single baked diffuse texture

## Implementation

### Step 1: Extend Draw Command
Add to `vgfx3d_draw_cmd_t` in `vgfx3d_backend.h`:
```c
const void *splat_map;          // RGBA weight texture (NULL = not terrain)
const void *splat_layers[4];    // Layer textures
float splat_layer_scales[4];    // UV tiling per layer
int8_t has_splat;               // Flag
```

### Step 2: Populate Splat Fields in the Terrain Draw Path
In [`src/runtime/graphics/rt_terrain3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_terrain3d.c), extend `rt_canvas3d_draw_terrain()` so the terrain path can populate the shared splat fields before submission:
```c
// For each chunk:
deferred_draw_t *dd = allocate_deferred_draw(c);
// ... fill standard fields from chunk mesh + material ...
dd->cmd.has_splat = (t->splat_map != NULL) ? 1 : 0;
dd->cmd.splat_map = t->splat_map;
for (int i = 0; i < 4; i++) {
    dd->cmd.splat_layers[i] = t->layer_textures[i];
    dd->cmd.splat_layer_scales[i] = (float)t->layer_scales[i];
}
```

### Step 3: Per-Pixel Splat Sampling in Software Rasterizer
In `raster_triangle()`, after diffuse texture sampling (line ~461), add:
```c
if (cmd->has_splat && cmd->splat_map) {
    sw_pixels_view splat_view;
    if (setup_pixels_view(cmd->splat_map, &splat_view)) {
        float sr, sg, sb, sa;
        sample_texture(&splat_view, u, vc, &sr, &sg, &sb, &sa);
        // RGBA channels = weights for layers 0-3
        float w[4] = {sr, sg, sb, sa};
        float wsum = w[0] + w[1] + w[2] + w[3];
        if (wsum > 0.001f) {
            for (int i = 0; i < 4; i++) w[i] /= wsum;
        } else {
            w[0] = 1.0f; w[1] = w[2] = w[3] = 0.0f;
        }

        // Blend 4 layer textures
        float blr = 0, blg = 0, blb = 0;
        for (int L = 0; L < 4; L++) {
            if (w[L] < 0.001f || !cmd->splat_layers[L]) continue;
            sw_pixels_view layer_view;
            if (!setup_pixels_view(cmd->splat_layers[L], &layer_view)) continue;
            float lu = u * cmd->splat_layer_scales[L];
            float lv = vc * cmd->splat_layer_scales[L];
            float lr, lg2, lb, la;
            sample_texture(&layer_view, lu, lv, &lr, &lg2, &lb, &la);
            blr += lr * w[L];
            blg += lg2 * w[L];
            blb += lb * w[L];
        }
        // Replace diffuse texture color with splat result
        fr = blr * cmd->diffuse_color[0];
        fg = blg * cmd->diffuse_color[1];
        fb_c = blb * cmd->diffuse_color[2];
    }
}
```

### Step 4: Retire the baked path only after compatibility is preserved
Do this in phases:
1. Add the shared splat payload to `vgfx3d_draw_cmd_t`
2. Land the software backend consumer
3. Keep `bake_splat_texture()` as the fallback until the targeted GPU backends also consume the new payload
4. Only then remove or demote the baked path

### Step 5: Other Backends Fallback
Metal/OpenGL/D3D11: when `has_splat` is set but the backend doesn't support it, the existing material diffuse texture (if any) is used as fallback. Terrain with no splat map renders normally.

## Files Modified
- `src/runtime/graphics/vgfx3d_backend.h` — splat fields in draw command
- `src/runtime/graphics/vgfx3d_backend_sw.c` — per-pixel splat sampling
- `src/runtime/graphics/rt_terrain3d.c` — terrain-specific draw path with splat fields, staged fallback removal

## Testing
- Terrain with no splat map → renders with material texture as before
- Terrain with splat map + 4 layers → visible blending at weight boundaries
- Layer scale > 1 → texture tiling visible within each splat region
- Layer scale = 1 → no tiling, texture stretched across full terrain
