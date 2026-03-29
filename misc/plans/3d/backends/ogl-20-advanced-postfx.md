# OGL-20: Depth/History-Based GPU PostFX

## Depends On

- OGL-10
- OGL-13

## Current State

The OpenGL GPU postfx path intentionally stopped at the effects that fit a single scene-color presentation chain:

- bloom
- tone mapping
- FXAA
- color grade
- vignette

The runtime still exposes additional PostFX APIs:

- [`rt_postfx3d_add_ssao()`](/Users/stephen/git/viper/src/runtime/graphics/rt_postfx3d.h#L44)
- [`rt_postfx3d_add_dof()`](/Users/stephen/git/viper/src/runtime/graphics/rt_postfx3d.h#L45)
- [`rt_postfx3d_add_motion_blur()`](/Users/stephen/git/viper/src/runtime/graphics/rt_postfx3d.h#L46)

But the backend-facing snapshot currently drops them:

- [`vgfx3d_postfx_get_snapshot()`](/Users/stephen/git/viper/src/runtime/graphics/rt_postfx3d.c#L631) ignores SSAO, DOF, and motion blur

So the public API surface is broader than the current OpenGL GPU implementation.

## Scope

This follow-on plan covers the remaining PostFX3D features that require depth and/or history inputs:

- SSAO
- depth of field
- motion blur

Do not fold this into OGL-13. The resource model is materially different.

## Required Shared Prerequisites

### 1. Backend-facing advanced PostFX snapshot

Extend the exported snapshot/helper API so GPU backends can consume:

- SSAO params
- DOF params
- motion-blur params

Do not make OpenGL parse the private `rt_postfx3d` struct directly.

### 2. Defined input requirements per effect

The runtime/backends must explicitly define what extra data each effect can rely on.

Minimum viable inputs:

- SSAO:
  - scene depth texture
  - projection parameters sufficient for view-space position reconstruction
- DOF:
  - scene depth texture
  - scene color texture
- motion blur:
  - scene depth texture
  - previous-frame camera transform at minimum

Full per-object motion blur is not implementable from the current API alone. If that level of correctness is required, shared runtime work must preserve previous model transforms or provide a velocity buffer path.

### 3. Scene-depth availability on the GPU postfx path

If onscreen postfx is active and any depth-based effect is enabled, the OpenGL backend must render the scene into a depth texture, not only a depth renderbuffer.

This is a prerequisite, not an optional optimization.

## OpenGL Implementation

### Phase A: SSAO + DOF foundation

Add postfx resources for:

- scene depth texture
- one or more extra fullscreen pass programs
- scratch textures/FBOs for AO and blur or for circle-of-confusion/blur passes

Suggested ordering:

1. render scene color + depth into offscreen textures
2. run SSAO from depth
3. blur AO if needed
4. composite AO into the lit scene
5. run DOF using depth-derived blur strength
6. run the existing OGL-13 color-chain pass or integrate color-chain effects into the final fullscreen pass

### Phase B: Motion blur

Define v1 explicitly:

- recommended v1: camera-motion blur only
- required shared state: previous frame view-projection matrix

If product requirements demand object-motion blur, split that into another prerequisite plan rather than pretending it is free.

### Phase C: Present path ownership

Keep the OGL-13 contract:

- GPU postfx owns onscreen presentation
- RTT readback remains scene color, not an implicit advanced-postfx composite, unless product requirements change

## OpenGL Shader Notes

### SSAO

Acceptable v1 approaches:

- reconstruct view-space position from depth
- reconstruct normals from depth, or
- add an explicit normal buffer if depth-only reconstruction is not good enough

The plan should choose one and document the tradeoff. Do not leave SSAO input quality unspecified.

### DOF

Compute blur strength from focus distance, aperture, and max blur. Keep the implementation stable for both near and far blur; do not only blur background unless that is an explicit scope cut.

### Motion blur

Camera-only blur can be derived by reprojection from current depth plus previous/current camera transforms. That still requires a depth texture and previous frame matrices.

## Files

- [`src/runtime/graphics/rt_postfx3d.h`](/Users/stephen/git/viper/src/runtime/graphics/rt_postfx3d.h)
- [`src/runtime/graphics/rt_postfx3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_postfx3d.c)
- [`src/runtime/graphics/rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c)
- [`src/runtime/graphics/vgfx3d_backend_opengl.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_opengl.c)

## Done When

- SSAO, DOF, and the chosen motion-blur scope all render on the OpenGL GPU presentation path
- depth-based effects sample real GPU depth rather than the software framebuffer
- the advanced effects are represented in the exported backend-facing PostFX snapshot
