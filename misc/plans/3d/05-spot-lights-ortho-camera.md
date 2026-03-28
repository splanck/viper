# Plan: Spot Lights + Orthographic Camera

## Overview
Two foundational features needed by most 3D game genres.

## 1. Spot Lights

### API
```
Light3D.NewSpot(position_vec3, direction_vec3, r, g, b, range, innerAngle, outerAngle)
Light3D.SetDirection(direction_vec3)   // new method
Light3D.SetRange(range)                // new method
Light3D.SetSpotAngles(inner, outer)    // new method
```

### Implementation
**File:** `src/runtime/graphics/rt_light3d.c`
- Add `LIGHT_SPOT = 2` shape type alongside existing directional (0) and point (1)
- Store `direction[3]`, `inner_cos`, `outer_cos`, `range`
- In `vgfx3d_light_params_t`: add `type`, `direction`, `inner_cos`, `outer_cos`

**Backend changes (all 4):**
- Fragment shader: compute `spot_factor = smoothstep(outer_cos, inner_cos, dot(-light_dir, spot_dir))`
- Multiply point light attenuation by spot_factor
- Metal: Update MSL shader `lighting_fragment`
- D3D11: Update HLSL pixel shader
- OpenGL: Update GLSL fragment shader
- Software: Add cone attenuation in per-pixel lighting loop

**Canvas3D:**
- `SetLight(index, light)` already passes light params — just needs to forward the new fields

### Files Modified
- `rt_light3d.c/h` — New constructor + properties
- `vgfx3d_backend.h` — Extend `vgfx3d_light_params_t`
- `vgfx3d_backend_metal.m` — MSL shader update
- `vgfx3d_backend_d3d11.c` — HLSL shader update
- `vgfx3d_backend_opengl.c` — GLSL shader update
- `vgfx3d_backend_sw.c` — Software spot cone
- `runtime.def` — New RT_FUNC entries

## 2. Orthographic Camera

### API
```
Camera3D.NewOrtho(left, right, bottom, top, near, far)
Camera3D.SetOrthoSize(size)   // convenience: -size to +size in all axes
Camera3D.IsOrtho -> Boolean
```

### Implementation
**File:** `src/runtime/graphics/rt_camera3d.c`
- Add `is_ortho` flag to camera struct
- New constructor builds orthographic projection matrix:
```c
proj[0][0] = 2.0 / (right - left);
proj[1][1] = 2.0 / (top - bottom);
proj[2][2] = -2.0 / (far - near);
// ... standard ortho matrix
```
- `LookAt` and `Orbit` work the same — only projection changes
- `ScreenToRay` needs ortho variant (parallel rays instead of perspective)

### Files Modified
- `rt_camera3d.c/h` — New constructor + ortho flag
- `runtime.def` — New RT_FUNC entries

## Verification
- Spot light: Place spot at ceiling pointing down — should create cone on floor
- Spot falloff: Objects outside cone should be unlit
- Ortho camera: Render scene — no perspective foreshortening
- Ortho + isometric: 45-degree rotation should produce classic isometric view
