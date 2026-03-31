# Plan 19: Vegetation3D — Instanced Grass & Foliage

## Problem

No vegetation rendering system exists. Sprite3D does camera-facing billboards
but is drawn individually (one draw call per sprite). InstanceBatch3D supports
GPU instancing but has no wind animation, density maps, or LOD culling.

## Goal

New `Vegetation3D` runtime class that combines InstanceBatch3D's batching
with cross-billboard geometry, density map population, wind animation,
and distance-based LOD.

## Zero External Dependencies

Uses existing InstanceBatch3D instancing path, Terrain3D height queries,
Pixels for density maps, and standard C math for wind sine waves.

---

## Design

### Zia API

```
Vegetation3D.New(bladeTexture)
Vegetation3D.SetDensityMap(veg, pixels)
Vegetation3D.SetWindParams(veg, speed, strength, turbulence)
Vegetation3D.SetLODDistances(veg, nearDist, farDist)
Vegetation3D.SetBladeSize(veg, width, height, sizeVariation)
Vegetation3D.Populate(veg, terrain, count)
Vegetation3D.Update(veg, dt, cameraX, cameraY, cameraZ)
Canvas3D.DrawVegetation(canvas, veg)
```

### Internal Architecture

```
Vegetation3D
  |-- blade_mesh: Mesh3D (cross-billboard: 2 perpendicular quads)
  |-- blade_material: Material3D (textured, alpha-tested)
  |-- positions[]: float[3] per blade (world-space base position)
  |-- base_transforms[]: float[16] per blade (position + random Y rotation + scale)
  |-- wind_transforms[]: float[16] per blade (base + wind offset, updated each frame)
  |-- visible_transforms[]: float[16] (LOD-culled subset for current frame)
  |-- visible_count: int32_t
  |-- density_map: Pixels (brightness = spawn probability)
  |-- wind_speed, wind_strength, wind_turbulence: double
  |-- lod_near, lod_far: double
  |-- blade_width, blade_height, size_variation: double
  |-- time: double
  |-- total_count: int32_t
```

---

## Implementation

### Step 1: Blade Mesh (Cross-Billboard)

Two perpendicular quads forming an X shape when viewed from above.
Each quad: 4 vertices, 2 triangles. Total: 8 vertices, 4 triangles.

```c
static void build_blade_mesh(void *mesh, double w, double h) {
    double hw = w * 0.5;
    // Quad 1: aligned with X axis
    // Bottom-left, bottom-right, top-right, top-left
    rt_mesh3d_add_vertex(mesh, -hw, 0, 0,  0,0,1,  0,1);  // bottom-left
    rt_mesh3d_add_vertex(mesh,  hw, 0, 0,  0,0,1,  1,1);  // bottom-right
    rt_mesh3d_add_vertex(mesh,  hw, h, 0,  0,0,1,  1,0);  // top-right
    rt_mesh3d_add_vertex(mesh, -hw, h, 0,  0,0,1,  0,0);  // top-left
    rt_mesh3d_add_triangle(mesh, 0, 1, 2);
    rt_mesh3d_add_triangle(mesh, 0, 2, 3);

    // Quad 2: rotated 90 degrees around Y
    rt_mesh3d_add_vertex(mesh, 0, 0, -hw,  1,0,0,  0,1);
    rt_mesh3d_add_vertex(mesh, 0, 0,  hw,  1,0,0,  1,1);
    rt_mesh3d_add_vertex(mesh, 0, h,  hw,  1,0,0,  1,0);
    rt_mesh3d_add_vertex(mesh, 0, h, -hw,  1,0,0,  0,0);
    rt_mesh3d_add_triangle(mesh, 4, 5, 6);
    rt_mesh3d_add_triangle(mesh, 4, 6, 7);
}
```

Material: textured with alpha, unlit (grass doesn't need specular).
Backface culling disabled (visible from both sides).

### Step 2: Populate

Given a Terrain3D and count, scatter blade positions:

```c
void rt_vegetation3d_populate(void *veg, void *terrain, int64_t count) {
    rt_vegetation3d *v = (rt_vegetation3d *)veg;
    rt_terrain3d *t = (rt_terrain3d *)terrain;

    // Simple LCG random for deterministic placement
    uint32_t rng = 12345;

    for (int64_t i = 0; i < count; i++) {
        // Random position within terrain bounds
        rng = rng * 1103515245 + 12345;
        double fx = (double)(rng & 0xFFFF) / 65535.0;
        rng = rng * 1103515245 + 12345;
        double fz = (double)(rng & 0xFFFF) / 65535.0;

        double wx = fx * t->width * t->scale[0];
        double wz = fz * t->depth * t->scale[2];

        // Density map check (if set)
        if (v->density_map) {
            // Sample density at normalized UV
            uint32_t dp = sample_density(v->density_map, fx, fz);
            rng = rng * 1103515245 + 12345;
            if ((rng & 0xFF) > (dp >> 24))  // R channel = density
                continue;
        }

        double wy = rt_terrain3d_get_height_at(terrain, wx, wz);

        // Random Y rotation + scale variation
        rng = rng * 1103515245 + 12345;
        double angle = ((double)(rng & 0xFFFF) / 65535.0) * 6.283185;
        rng = rng * 1103515245 + 12345;
        double scale = 1.0 + (((double)(rng & 0xFFFF) / 65535.0) - 0.5)
                        * 2.0 * v->size_variation;

        // Build transform: translate(wx, wy, wz) * rotateY(angle) * scale
        store_blade_transform(v, wx, wy, wz, angle, scale);
    }
}
```

### Step 3: Wind Animation

Per-frame update. Wind offsets the top vertices of each blade using a
sine wave based on position and time:

```c
void rt_vegetation3d_update(void *veg, double dt,
                             double camX, double camY, double camZ) {
    rt_vegetation3d *v = (rt_vegetation3d *)veg;
    v->time += dt;

    v->visible_count = 0;

    for (int32_t i = 0; i < v->total_count; i++) {
        float *base = &v->base_transforms[i * 16];
        float bx = base[3], by = base[7], bz = base[11]; // translation column

        // LOD: compute distance to camera
        double dx = bx - camX, dy = by - camY, dz = bz - camZ;
        double dist = sqrt(dx*dx + dy*dy + dz*dz);

        if (dist > v->lod_far)
            continue; // cull

        // Wind displacement (applied as shear in model matrix)
        double phase = v->wind_turbulence * (bx * 0.1 + bz * 0.07)
                       + v->time * v->wind_speed;
        double wind_x = sin(phase) * v->wind_strength;
        double wind_z = cos(phase * 0.7) * v->wind_strength * 0.5;

        // Copy base transform, add wind shear to top vertices
        float *dst = &v->wind_transforms[v->visible_count * 16];
        memcpy(dst, base, 16 * sizeof(float));

        // Shear: model[0][1] += wind_x, model[2][1] += wind_z
        // This tilts the Y-axis columns, bending tops of blades
        dst[1] += (float)wind_x;   // column 0, row 1: X shear
        dst[9] += (float)wind_z;   // column 2, row 1: Z shear

        v->visible_count++;
    }
}
```

### Step 4: Rendering

Use the existing `submit_draw_instanced` backend vtable call:

```c
void rt_canvas3d_draw_vegetation(void *canvas, void *veg) {
    rt_vegetation3d *v = (rt_vegetation3d *)veg;
    if (v->visible_count == 0) return;

    // Disable backface culling for grass
    rt_canvas3d_set_backface_cull(canvas, 0);

    // Use instanced draw path (same as InstanceBatch3D)
    // Build draw command from blade mesh + material
    // Submit with wind_transforms array and visible_count
    // ... (follows rt_canvas3d_draw_instanced pattern)

    rt_canvas3d_set_backface_cull(canvas, 1);
}
```

### Step 5: LOD Thinning (Between near and far)

Between `lod_near` and `lod_far`, progressively thin blade count:

```c
// In the LOD section of update():
if (dist > v->lod_near) {
    // Thin: draw every Nth blade based on distance
    double t = (dist - v->lod_near) / (v->lod_far - v->lod_near);
    int skip = 1 + (int)(t * 4);  // skip more at distance
    if ((i % skip) != 0)
        continue;
}
```

---

## Files

| File | Change |
|------|--------|
| `src/runtime/graphics/rt_vegetation3d.c` | **New file** (~400 LOC) |
| `src/runtime/graphics/rt_vegetation3d.h` | **New file** (~30 LOC) |
| `src/runtime/graphics/rt_canvas3d.c` | Add `rt_canvas3d_draw_vegetation` dispatch |
| `src/runtime/graphics/rt_graphics_stubs.c` | Stub implementations for all new functions |
| `src/runtime/CMakeLists.txt` | Add `graphics/rt_vegetation3d.c` to source list |
| `src/il/runtime/runtime.def` | Register Vegetation3D class + methods |

## LOC Estimate

~500 LOC new C code + ~30 LOC header + ~50 LOC runtime.def + ~20 LOC stubs.

## Testing

1. Build: `./scripts/build_viper.sh`
2. `ctest --test-dir build --output-on-failure`
3. Write `examples/apiaudit/graphics3d/vegetation_demo.zia`:
   - Create terrain with procedural heightmap
   - Create grass texture (green blade on transparent background)
   - Populate 5000 blades with density map
   - Verify wind animation + LOD thinning at distance
4. Verify on Metal (GPU instanced path) and software fallback (loop path)

## Considerations

- **Memory**: 5000 blades * 16 floats * 3 arrays (base + wind + visible) = ~960KB.
  For 50000 blades, ~9.6MB — still reasonable.
- **Draw calls**: GPU path = 1 draw call for all visible blades.
  Software path = N individual draws (slower but functional).
- **Alpha testing**: Grass blade textures need alpha channel. The existing
  alpha-blend path in all backends handles this correctly.
- **No per-instance color**: All blades share one material. Color variation
  would require vertex color support in instanced draws (future work).
