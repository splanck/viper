# Plan 18: Enhanced Water3D — Textures, Waves, Reflections

## Problem

Water3D (`rt_water3d.c`, 207 LOC) is minimal:
- Fixed 32x32 grid
- Single sine wave displacement
- Color-only (no textures)
- No normal maps, reflections, or Fresnel

Meanwhile, the material system already supports normal maps, env maps,
reflectivity, and alpha — Water3D just doesn't use any of them.

## Goal

Three phases of increasing capability:
- Phase A: Wire existing material properties to Water3D
- Phase B: Multi-wave Gerstner displacement
- Phase C: Planar reflection via RenderTarget3D

## Zero External Dependencies

All math (sin, cos, dot products, matrix ops) uses standard C `<math.h>`.
Reflection uses existing `RenderTarget3D` infrastructure. No new libraries.

---

## Phase A: Texture & Normal Map Support (~100 LOC)

### Insight

The water's internal material is created via `rt_material3d_new_color()` at
`rt_water3d.c:183`. All the material property setters already exist:
- `rt_material3d_set_texture()` — `rt_material3d.c:92`
- `rt_material3d_set_normal_map()` — `rt_material3d.c:127`
- `rt_material3d_set_env_map()` — `rt_cubemap3d.c:218`
- `rt_material3d_set_reflectivity()` — `rt_cubemap3d.c:225`
- `rt_material3d_set_shininess()` — already called at line 184

We just need to store these on the water struct and apply them.

### New API

```c
void rt_water3d_set_texture(void *water, void *pixels);
void rt_water3d_set_normal_map(void *water, void *pixels);
void rt_water3d_set_env_map(void *water, void *cubemap);
void rt_water3d_set_reflectivity(void *water, double value);
```

### Struct Changes

Add to `rt_water3d` in `rt_water3d.c`:
```c
void *texture;       // Pixels for surface texture
void *normal_map;    // Pixels for wave normal map
void *env_map;       // CubeMap3D for environment reflections
double reflectivity; // [0.0-1.0]
```

### Material Wiring

In `rt_water3d_update()`, after creating/updating the material:
```c
if (w->texture)    rt_material3d_set_texture(w->material, w->texture);
if (w->normal_map) rt_material3d_set_normal_map(w->material, w->normal_map);
if (w->env_map) {
    rt_material3d_set_env_map(w->material, w->env_map);
    rt_material3d_set_reflectivity(w->material, w->reflectivity);
}
```

### Grid Resolution

Increase `WATER_GRID` from 32 to 64 for better normal map fidelity.
Configurable via new API:
```c
void rt_water3d_set_resolution(void *water, int64_t resolution);
```
Clamp to [8, 256] range.

---

## Phase B: Gerstner Wave Composition (~150 LOC)

### Current Wave Model

Single sine wave: `y = amplitude * sin(frequency * (x + z) + time * speed)`

Problem: unrealistic uniform waves, no directional variation.

### Gerstner Wave Model

Sum of N directional waves. Each wave has:
- Direction (dx, dz) — normalized wave travel direction
- Speed — phase velocity
- Amplitude — wave height
- Wavelength — distance between crests

Displacement formula per wave i:
```
phase_i = dot(dir_i, pos.xz) * (2*PI / wavelength_i) + time * speed_i
y += amplitude_i * sin(phase_i)
```

Normal from derivative:
```
dy/dx = sum_i(amplitude_i * dir_i.x * freq_i * cos(phase_i))
dy/dz = sum_i(amplitude_i * dir_i.z * freq_i * cos(phase_i))
normal = normalize(-dy/dx, 1.0, -dy/dz)
```

### New API

```c
void rt_water3d_add_wave(void *water, double dirX, double dirZ,
                          double speed, double amplitude, double wavelength);
void rt_water3d_clear_waves(void *water);
```

### Struct Changes

```c
#define WATER_MAX_WAVES 8

typedef struct {
    double dir[2];      // normalized direction
    double speed;
    double amplitude;
    double wavelength;
} water_wave_t;

// In rt_water3d:
water_wave_t waves[WATER_MAX_WAVES];
int32_t wave_count;
```

### Backward Compatibility

If `wave_count == 0`, fall back to the existing single sine wave using
`wave_speed`, `wave_amplitude`, `wave_frequency`. Existing code untouched.

---

## Phase C: Planar Reflection (~200 LOC)

### Approach

1. Before the main render pass, create a reflected camera by mirroring
   the view matrix across the water plane Y
2. Render the scene into a RenderTarget3D from the reflected viewpoint
3. Bind the reflection RT as a 2D texture on the water material
4. Apply Fresnel factor: more reflection at grazing angles

### New API

```c
void rt_water3d_set_reflection(void *water, int8_t enabled);
```

When enabled, `rt_canvas3d_draw_water` internally:
1. Allocates a half-resolution RenderTarget3D (once, cached)
2. Calls `set_render_target` to redirect rendering
3. Computes reflected camera: `reflected_eye.y = 2*waterY - eye.y`
4. Re-issues scene draw calls (via a user-provided callback or stored scene)
5. Restores render target
6. Binds reflection RT as water texture with Fresnel blend

### Fresnel Approximation (Schlick)

```c
float fresnel = pow(1.0 - max(dot(N, V), 0.0), 5.0);
float blend = mix(0.02, 1.0, fresnel);  // 2% at head-on, 100% at grazing
final_color = mix(water_color, reflection_color, blend * reflectivity);
```

### Shader Changes (All 4 Backends)

Add Fresnel computation to fragment shaders. Triggered by a `has_reflection`
flag in the draw command. Same implementation across all backends.

### Alternative: Scene Callback

Since we can't re-issue arbitrary scene draws from inside the water draw
call, the reflection pass should be explicitly called by the user:

```zia
// In game loop:
Water3D.BeginReflection(water, canvas, camera);
// Re-draw scene geometry here (terrain, meshes, etc.)
Water3D.EndReflection(water, canvas);

// Then in main pass:
Canvas3D.DrawWater(canvas, water, camera);
```

This avoids storing scene state in the water object.

---

## Files Modified

| File | Change | Phase |
|------|--------|-------|
| `src/runtime/graphics/rt_water3d.c` | All three phases | A,B,C |
| `src/runtime/graphics/rt_water3d.h` | Declare new functions | A,B,C |
| `src/runtime/graphics/rt_graphics_stubs.c` | Stub implementations | A,B,C |
| `src/il/runtime/runtime.def` | Register new RT_FUNC + RT_METHOD | A,B,C |
| `src/runtime/graphics/vgfx3d_backend_metal.m` | Fresnel shader | C |
| `src/runtime/graphics/vgfx3d_backend_d3d11.c` | Fresnel shader | C |
| `src/runtime/graphics/vgfx3d_backend_opengl.c` | Fresnel shader | C |
| `src/runtime/graphics/vgfx3d_backend_sw.c` | CPU Fresnel | C |

## LOC Estimate

Phase A: ~100 LOC (mostly wiring existing functions)
Phase B: ~150 LOC (Gerstner math + wave struct management)
Phase C: ~200 LOC (reflection RT + Fresnel shader)
Total: ~450 LOC

## Testing

1. Phase A: Render water with a texture + normal map, verify visual improvement
2. Phase B: Add 3 Gerstner waves, verify directional wave pattern
3. Phase C: Enable reflection, verify terrain/skybox reflected in water surface
4. `./scripts/build_viper.sh && ctest --test-dir build --output-on-failure`
5. Test on Metal (macOS), OpenGL fallback, and verify stubs compile without graphics
