# OGL-13: GPU Post-Processing Pipeline

## Depends On

- OGL-10
- shared flip/present handoff

## Current State

The runtime already exports a backend-facing PostFX snapshot via [`vgfx3d_postfx_get_snapshot()`](/Users/stephen/git/viper/src/runtime/graphics/rt_postfx3d.h#L65). The missing piece is not only the OpenGL fullscreen-quad shader, but also shared presentation ownership:

- [`rt_canvas3d_flip()`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c#L993) still always applies the CPU PostFX path to the software framebuffer before calling `backend->present()`

As written, that makes a GPU postfx implementation impossible to own end to end.

## Required Shared Prerequisite

Before OGL-13 can be finished, the runtime must define one of these shared mechanisms:

1. add a backend hook that receives the PostFX snapshot and owns GPU postfx presentation, or
2. extend `present()` so Canvas3D can pass the PostFX snapshot into it

Either way, `rt_canvas3d_flip()` must skip the CPU postfx path when the active backend is handling postfx on the GPU.

## OpenGL Backend Scope

v1 should implement:

- bloom
- tone mapping
- FXAA
- color grade
- vignette

Do not expand this plan to SSAO, DOF, or motion blur without adding the required depth/history inputs.

## Rendering Model

For onscreen rendering with GPU postfx enabled:

1. render the scene into an offscreen color target
2. in `present()`, bind the default framebuffer
3. draw a fullscreen quad with the postfx shader
4. swap buffers

For `RenderTarget3D`:

- do not apply GPU postfx unless the product requirement explicitly changes
- preserve the current behavior where RTT readback represents the scene render, not an implicit postfx composite

## OpenGL Implementation

Add:

- a postfx shader program
- a minimal fullscreen VAO for core-profile draws
- an offscreen scene color texture if the existing RTT FBO is not appropriate to reuse
- cached uniform locations for the PostFX parameters

Use `gl_VertexID` for the fullscreen quad and an empty VAO to satisfy GL 3.3 core requirements.

## Files

- [`src/runtime/graphics/vgfx3d_backend_opengl.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_opengl.c)
- shared runtime change in [`src/runtime/graphics/rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c)

## Done When

- GPU postfx owns the onscreen final image
- CPU postfx is skipped for the OpenGL path when GPU postfx is active
- RTT output remains well-defined
