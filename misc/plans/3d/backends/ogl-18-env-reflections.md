# OGL-18: Environment Reflections From CubeMap Materials

## Depends On

- OGL-17 for cubemap upload/binding infrastructure
- OGL-08 if normal-mapped reflections are expected to use the perturbed normal

## Current State

The shared runtime already stores reflection inputs on materials:

- [`rt_material3d_set_env_map()`](/Users/stephen/git/viper/src/runtime/graphics/rt_cubemap3d.c#L198)
- [`rt_material3d_set_reflectivity()`](/Users/stephen/git/viper/src/runtime/graphics/rt_cubemap3d.c#L207)

But that data never reaches the backend:

- [`rt_material3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_material3d.c#L18) still documents `env_map` and `reflectivity` as reserved for future support
- [`vgfx3d_draw_cmd_t`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend.h) has no env-map payload fields
- [`rt_canvas3d_draw_mesh_matrix()`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c#L664) does not forward them
- the OpenGL shader has no cubemap-sampling material path

## Required Shared Prerequisites

Add material reflection payloads to the draw command:

```c
const void *env_map;   /* CubeMap3D or NULL */
float reflectivity;    /* [0,1] */
```

Then propagate them in [`rt_canvas3d_draw_mesh_matrix()`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c):

- `cmd.env_map = mat->env_map`
- `cmd.reflectivity = (float)mat->reflectivity`

Do not make the backend reach back into private `rt_material3d` state.

## Reflection Model

v1 should implement simple specular-environment mixing, not full PBR.

Fragment-stage model:

1. compute the final surface normal used for lighting
2. compute the view vector from camera to world position
3. reflect that vector around the surface normal
4. sample the cubemap
5. mix the existing material result with the cubemap sample using `reflectivity`

Recommended form:

```glsl
vec3 V = normalize(uCameraPos - vWorldPos);
vec3 R = reflect(-V, N);
vec3 envColor = texture(uEnvMap, R).rgb;
vec3 shaded = ...existing lit/unlit result...
vec3 composite = mix(shaded, envColor, clamp(uReflectivity, 0.0, 1.0));
```

Apply fog after the reflection composite so reflected surfaces still participate in scene fog.

## OpenGL Backend Work

### Shader additions

Add:

```glsl
uniform samplerCube uEnvMap;
uniform int uHasEnvMap;
uniform float uReflectivity;
```

Reflection normal source:

- if normal mapping is active, use the perturbed normal
- otherwise use the interpolated geometric normal

Reflection should work in both lit and unlit materials. `unlit` skips light accumulation; it should not automatically disable environment reflections.

### Texture binding

Bind the cubemap on a dedicated unit after the existing 2D material/splat slots.

Recommended unit:

- texture unit `10`

When reflection is inactive:

- set `uHasEnvMap = 0`
- bind texture `0` on that unit to avoid stale state

### Cache reuse

Reuse the cubemap cache introduced by OGL-17. Do not upload a second cubemap object per reflective draw.

## Files

- [`src/runtime/graphics/vgfx3d_backend.h`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend.h)
- [`src/runtime/graphics/rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c)
- [`src/runtime/graphics/rt_material3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_material3d.c)
- [`src/runtime/graphics/vgfx3d_backend_opengl.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_opengl.c)

## Done When

- reflective materials visibly sample their assigned `CubeMap3D`
- `reflectivity = 0` produces the current non-reflective result
- `reflectivity = 1` produces full cubemap reflection
- normal-mapped materials reflect using the perturbed normal
