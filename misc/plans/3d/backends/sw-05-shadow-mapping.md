# SW-05: Shadow Mapping in Software Rasterizer

## Context
No backend implements shadow mapping. The software rasterizer is the best place to start because it has full control over the rendering pipeline — no shader changes needed.

Canvas3D already owns shadow enable/disable/bias state in [`src/runtime/graphics/rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c) and [`src/runtime/graphics/rt_canvas3d_internal.h`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d_internal.h). Reuse that state instead of inventing a new public API. The missing piece is pass scheduling: `rt_canvas3d_end()` currently replays the deferred queue only once.

## Approach: Single Shadow Map from Primary Directional Light

### Phase 1: Shadow Depth Pass
Render the scene from the light's perspective into a float depth buffer.

**Data structures:**
```c
// Add to sw_context_t
float *shadow_depth;       // shadow map depth buffer (width * height)
int32_t shadow_w, shadow_h; // shadow map resolution (default 1024)
float shadow_vp[16];       // light view-projection matrix
int8_t shadow_enabled;
```

**Shadow pass (before main pass):**
1. Find the first directional light in the light array
2. Build an orthographic projection from the light's direction:
   - View matrix: `look_at(light_pos, light_pos + light_dir, up)`
   - Projection: orthographic covering the scene bounding box
3. For each mesh in the draw queue:
   - Transform vertices by light VP matrix
   - Rasterize to `shadow_depth` buffer (depth-only, no color/lighting)
   - Write minimum depth per pixel

### Phase 2: Shadow Lookup in Main Pass
During the main rasterization pass, for each pixel:
```c
if (ctx->shadow_enabled) {
    // Transform world position to light clip space
    float lx = wx * shadow_vp[0] + wy * shadow_vp[1] + wz * shadow_vp[2] + shadow_vp[3];
    float ly = wx * shadow_vp[4] + wy * shadow_vp[5] + wz * shadow_vp[6] + shadow_vp[7];
    float lz = wx * shadow_vp[8] + wy * shadow_vp[9] + wz * shadow_vp[10] + shadow_vp[11];
    float lw = wx * shadow_vp[12] + wy * shadow_vp[13] + wz * shadow_vp[14] + shadow_vp[15];

    // Perspective divide → NDC → [0,1] UV
    float su = (lx / lw) * 0.5f + 0.5f;
    float sv = (ly / lw) * 0.5f + 0.5f;
    float sd = (lz / lw) * 0.5f + 0.5f;

    // Sample shadow depth
    if (su >= 0 && su < 1 && sv >= 0 && sv < 1) {
        int sx = (int)(su * shadow_w);
        int sy = (int)(sv * shadow_h);
        float shadow_z = shadow_depth[sy * shadow_w + sx];
        float bias = 0.005f; // prevent shadow acne
        if (sd > shadow_z + bias) {
            // In shadow — reduce direct lighting contribution
            atten *= 0.15f; // ambient-only in shadow
        }
    }
}
```

### Phase 3: Canvas3D Integration
- Reuse the existing `Canvas3D.EnableShadows`, `DisableShadows`, and `SetShadowBias` state already present in `rt_canvas3d.c`
- Reuse `c->shadow_rt->depth_buf` as the CPU shadow map storage
- Restrict v1 to the first directional light; do not attempt point/spot shadow maps here

### Phase 4: Two-Pass Rendering Architecture
The shadow pass needs the draw command list BEFORE the main pass renders them. Canvas3D already buffers draws in the deferred queue. The change:
1. Update [`src/runtime/graphics/rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c) so `End()` schedules a shadow pass over opaque draws before the normal replay.
2. Keep the actual depth rasterization and lookup code in [`src/runtime/graphics/vgfx3d_backend_sw.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_sw.c).
3. Main pass reads from the populated shadow depth buffer during per-pixel lighting.

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_sw.c` — shadow context, depth pass, shadow lookup
- `src/runtime/graphics/rt_canvas3d.c` — shared shadow-pass scheduling during deferred replay

## Testing
- Place a sphere above a plane with directional light → sphere casts shadow on plane
- Move light direction → shadow moves accordingly
- Bias too low → shadow acne (self-shadowing artifacts)
- Bias too high → peter-panning (shadow detaches from object)
- Multiple objects → shadows stack correctly

## Performance
Shadow map render is a second rasterization pass over all opaque geometry, depth-only (no lighting/texturing). At 1024x1024 resolution this is fast. The per-pixel shadow lookup adds 1 matrix multiply + 1 depth comparison per pixel in the main pass.
