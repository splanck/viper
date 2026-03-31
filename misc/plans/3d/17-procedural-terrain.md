# Plan 17: Procedural Terrain Generation

## Problem

Terrain3D requires a pre-made heightmap PNG. There's no built-in procedural
terrain generation, forcing users to bring external assets.

## Goal

Enable fully procedural terrain in two ways:
1. **Zia-only** (no runtime changes): PerlinNoise -> Pixels -> SetHeightmap
2. **Native fast path** (optional): direct Perlin-to-heightfield in C

## Zero External Dependencies

PerlinNoise (`rt_perlin.c`) and Pixels (`rt_pixels.c`) already exist.
No new libraries needed.

---

## Part A: Zia-Only Solution (No Runtime Changes)

Everything needed already exists:
- `PerlinNoise.New(seed)` — `runtime.def:2765`
- `PerlinNoise.Octave2D(obj, x, y, octaves, persistence)` — `runtime.def:2768`
- `Pixels.New(width, height)` — `runtime.def:1320`
- `Pixels.Set(obj, x, y, color)` — `runtime.def:1321`
- `Terrain3D.SetHeightmap(obj, pixels)` — `runtime.def:10253`

### Pixel Format

Pixels uses `0xRRGGBBAA` (R in bits [31:24], G in [23:16], B in [15:8], A in [7:0]).

The terrain heightmap reader (`rt_terrain3d.c:125-129`) extracts 16-bit height
from R (high byte) + G (low byte):
```c
uint32_t hi = (pixel >> 24) & 0xFF;  // R channel
uint32_t lo = (pixel >> 16) & 0xFF;  // G channel
float height = (float)((hi << 8) | lo) / 65535.0f;
```

### Zia Heightmap Generator

```zia
module terrain_gen;

bind Viper.Math;
bind Viper.Graphics;

func generateHeightmap(width: Integer, depth: Integer, seed: Integer,
                       scale: Float, octaves: Integer, persistence: Float) -> Pixels {
    var pn = PerlinNoise.New(seed);
    var buf = Pixels.New(width, depth);

    var z = 0;
    while (z < depth) {
        var x = 0;
        while (x < width) {
            var nx = (x + 0.0) * scale / (width + 0.0);
            var nz = (z + 0.0) * scale / (depth + 0.0);
            var h = PerlinNoise.Octave2D(pn, nx, nz, octaves, persistence);

            // Remap [-1, 1] -> [0, 65535]
            var h16 = Convert.NumToInt((h + 1.0) * 32767.5);
            if (h16 < 0) { h16 = 0; }
            if (h16 > 65535) { h16 = 65535; }

            // Pack into 0xRRGGBBAA: R=high byte, G=low byte, B=0, A=255
            var hi = h16 / 256;
            var lo = h16 % 256;
            var color = hi * 16777216 + lo * 65536 + 0 * 256 + 255;
            Pixels.Set(buf, x, z, color);

            x = x + 1;
        }
        z = z + 1;
    }
    return buf;
}
```

### Splat Map Generator (Slope-Based)

Automatically assign terrain layers by slope (flat = grass, steep = rock):

```zia
func generateSplatMap(terrain: Terrain3D, width: Integer, depth: Integer,
                      scaleX: Float, scaleZ: Float) -> Pixels {
    var buf = Pixels.New(width, depth);
    var z = 0;
    while (z < depth) {
        var x = 0;
        while (x < width) {
            var wx = (x + 0.0) * scaleX / (width + 0.0);
            var wz = (z + 0.0) * scaleZ / (depth + 0.0);
            var normal = Terrain3D.GetNormalAt(terrain, wx, wz);
            var ny = Vec3.get_Y(normal);  // 1.0 = flat, 0.0 = vertical

            // R=grass (flat), G=rock (steep), B=dirt (mid), A=snow (high)
            var grass = 0;
            var rock = 0;
            if (ny > 0.8) { grass = 255; } else { rock = 255; }
            var color = grass * 16777216 + rock * 65536 + 0 * 256 + 0;
            Pixels.Set(buf, x, z, color);

            x = x + 1;
        }
        z = z + 1;
    }
    return buf;
}
```

### Performance Note

For a 256x256 heightmap, this loop runs 65536 iterations with one `Octave2D`
call each. At ~4 octaves, that's ~262K noise evaluations. On desktop hardware
this takes <100ms in the VM. For 512x512 or larger, consider Part B.

---

## Part B: Native Fast Path (Optional)

Add a C function that bypasses the Pixels intermediate and writes directly
to the terrain's `float *heights` array.

### New Runtime API

```c
// rt_terrain3d.c
void rt_terrain3d_generate_perlin(void *terrain, void *perlin,
                                   double scale, int64_t octaves,
                                   double persistence);
```

Implementation (~40 LOC):
```c
void rt_terrain3d_generate_perlin(void *terrain, void *perlin,
                                   double scale, int64_t octaves,
                                   double persistence) {
    if (!terrain || !perlin) return;
    rt_terrain3d *t = (rt_terrain3d *)terrain;

    for (int32_t z = 0; z < t->depth; z++) {
        for (int32_t x = 0; x < t->width; x++) {
            double nx = (double)x * scale / (double)t->width;
            double nz = (double)z * scale / (double)t->depth;
            double h = rt_perlin_octave2d(perlin, nx, nz, octaves, persistence);
            t->heights[z * t->width + x] = (float)((h + 1.0) * 0.5);
        }
    }

    // Invalidate chunks
    for (int32_t i = 0; i < t->chunks_x * t->chunks_z; i++)
        t->chunk_meshes[i] = NULL;
}
```

### Zia API

```
Terrain3D.GeneratePerlin(terrain, perlinNoise, scale, octaves, persistence)
```

### Files Modified

| File | Change |
|------|--------|
| `src/runtime/graphics/rt_terrain3d.c` | Add `rt_terrain3d_generate_perlin` |
| `src/runtime/graphics/rt_terrain3d.h` | Declare new function |
| `src/runtime/graphics/rt_graphics_stubs.c` | Add stub |
| `src/il/runtime/runtime.def` | Register RT_FUNC + RT_METHOD |

### LOC Estimate

Part A: ~60 lines Zia, zero C changes.
Part B: ~50 LOC C + runtime.def entry.

## Testing

1. Part A: Write `examples/apiaudit/graphics3d/procedural_terrain_demo.zia`
   - Generate 256x256 heightmap with Perlin noise
   - Feed to Terrain3D, render with camera
   - Verify terrain has organic hills, not flat
2. Part B: Same demo but using `Terrain3D.GeneratePerlin` instead
3. `./scripts/build_viper.sh && ctest --test-dir build --output-on-failure`
