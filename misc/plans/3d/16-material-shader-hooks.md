# Plan 16: Material Shader Hooks

## Problem

The 3D engine uses a fixed Blinn-Phong shading model. All materials render
identically — there's no way to get toon shading, PBR metallic/roughness,
or custom Fresnel effects from Zia code.

## Goal

Add selectable shading models per-material without exposing raw shader code
to users. Materials gain a `ShadingModel` enum and 8 custom float parameters
that feed into per-backend shader branches.

## Zero External Dependencies

All changes are pure C and modifications to existing shader strings (MSL,
HLSL, GLSL). No new libraries. The software rasterizer gets equivalent
CPU-side branches.

---

## Design

### New Zia API

```
Material3D.SetShadingModel(mat, model)    // Integer: 0-5
Material3D.SetCustomParam(mat, index, value)  // index 0-7, float value
```

Shading model enum:
- 0 = BlinnPhong (default, current behavior)
- 1 = Toon (quantized diffuse bands, hard specular cutoff)
- 2 = PBR (Cook-Torrance: custom[0]=roughness, custom[1]=metallic)
- 3 = Unlit (already exists as `SetUnlit`, kept for completeness)
- 4 = Fresnel (angle-dependent alpha, custom[0]=power, custom[1]=bias)
- 5 = Emissive (glow with custom[0]=strength)

### Data Flow

```
rt_material3d  (+ shading_model, custom_params[8])
     |
     v
vgfx3d_draw_cmd_t  (+ shading_model, custom_params[8])
     |
     v
backend shader: switch(shading_model) in fragment/pixel shader
```

---

## Implementation

### Step 1: Extend Material3D struct

**File: `src/runtime/graphics/rt_canvas3d_internal.h`**

Add to `rt_material3d`:
```c
int32_t shading_model;    // 0=BlinnPhong, 1=Toon, 2=PBR, 3=Unlit, 4=Fresnel, 5=Emissive
double custom_params[8];  // user-defined parameters per shading model
```

### Step 2: Extend draw command

**File: `src/runtime/graphics/vgfx3d_backend.h`**

Add to `vgfx3d_draw_cmd_t`:
```c
int32_t shading_model;
float custom_params[8];
```

### Step 3: Add runtime API functions

**File: `src/runtime/graphics/rt_material3d.c`**

```c
void rt_material3d_set_shading_model(void *obj, int64_t model);
void rt_material3d_set_custom_param(void *obj, int64_t index, double value);
```

### Step 4: Wire through draw dispatch

**File: `src/runtime/graphics/rt_canvas3d.c`**

In the draw command construction (where material properties are copied to
`vgfx3d_draw_cmd_t`), copy `shading_model` and `custom_params[8]`.

### Step 5: Update each backend shader

**Metal (`vgfx3d_backend_metal.m`):**
- Add `int shading_model` + `float custom_params[8]` to `mtl_per_material_t`
- Add `switch(material.shading_model)` in fragment shader after lighting accumulation
- Toon: `float bands = 4.0; diffuse = floor(diffuse * bands) / bands;`
- PBR: Cook-Torrance GGX distribution, Schlick Fresnel, Smith geometry
- Fresnel: `float f = pow(1.0 - max(dot(N,V),0), custom[0]) + custom[1]; color.a *= f;`

**D3D11 (`vgfx3d_backend_d3d11.c`):**
- Same uniform additions to cbuffer
- Same switch in pixel shader (HLSL syntax)

**OpenGL (`vgfx3d_backend_opengl.c`):**
- Add `uniform int uShadingModel; uniform float uCustomParams[8];`
- Same switch in fragment shader (GLSL syntax)

**Software (`vgfx3d_backend_sw.c`):**
- In `compute_lighting()`, add switch after Blinn-Phong accumulation
- Toon: quantize diffuse. PBR: simplified Cook-Torrance. Fresnel: angle-based alpha.

### Step 6: Register in runtime.def

**File: `src/il/runtime/runtime.def`**

```
RT_FUNC(Mat3DSetShadingModel, rt_material3d_set_shading_model, "Viper.Graphics3D.Material3D.SetShadingModel", "void(obj,i64)")
RT_FUNC(Mat3DSetCustomParam,  rt_material3d_set_custom_param,  "Viper.Graphics3D.Material3D.SetCustomParam",  "void(obj,i64,f64)")
```

Add RT_METHOD entries in the Material3D class block.

### Step 7: Stubs

**File: `src/runtime/graphics/rt_graphics_stubs.c`**

Add stub implementations for both new functions (no-op when graphics disabled).

---

## Files Modified

| File | Change |
|------|--------|
| `src/runtime/graphics/rt_canvas3d_internal.h` | Extend rt_material3d struct |
| `src/runtime/graphics/vgfx3d_backend.h` | Extend vgfx3d_draw_cmd_t |
| `src/runtime/graphics/rt_material3d.c` | Add setter functions |
| `src/runtime/graphics/rt_canvas3d.c` | Copy new fields to draw cmd |
| `src/runtime/graphics/vgfx3d_backend_metal.m` | MSL fragment shader branch |
| `src/runtime/graphics/vgfx3d_backend_d3d11.c` | HLSL pixel shader branch |
| `src/runtime/graphics/vgfx3d_backend_opengl.c` | GLSL fragment shader branch |
| `src/runtime/graphics/vgfx3d_backend_sw.c` | CPU compute_lighting branch |
| `src/runtime/graphics/rt_graphics_stubs.c` | Stub implementations |
| `src/il/runtime/runtime.def` | Register new functions |

## LOC Estimate

~500-700 LOC across all files. The PBR Cook-Torrance shader is the largest
single addition (~50 lines per backend).

## Testing

1. Build: `./scripts/build_viper.sh`
2. `ctest --test-dir build --output-on-failure`
3. Write `examples/apiaudit/graphics3d/shading_models_demo.zia`:
   - Render 6 spheres side-by-side, each with a different shading model
   - Verify visual correctness on Metal, then test OpenGL via env var override

## Risk

Keeping 4 backends in sync is the main risk. Each new shading model is
~15-20 lines of shader code per backend. A test that renders all models
and compares against the software rasterizer output would catch divergence.
