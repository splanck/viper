# OGL-15: Hardware Instanced Rendering

## Depends On

- shared `submit_draw_instanced()` hook
- OGL-14 recommended

## Correction To The Earlier Plan

The shared instanced hook is already present in [`vgfx3d_backend.h`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend.h) and already used by [`rt_instbatch3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_instbatch3d.c). This plan is not about adding that hook anymore. It is about making the OpenGL backend implement it with real hardware instancing rather than a backend-side loop.

## Backend Work

Add to `gl_context_t`:

- persistent instance buffer
- instance buffer capacity

Implement `submit_draw_instanced()` using:

- one vertex buffer
- one index buffer
- one instance buffer containing `instance_count * 16` floats
- `glDrawElementsInstanced`

## GLSL Changes

Add:

```glsl
layout(location=7) in mat4 aInstanceMatrix;
uniform int uUseInstancing;
```

Vertex shader model selection:

```glsl
mat4 model = (uUseInstancing != 0) ? aInstanceMatrix : uModelMatrix;
```

## Normal Handling

This plan must not reintroduce the normal-matrix bug for instanced draws.

Recommended v1 approach:

- compute `transpose(inverse(mat3(model)))` in the vertex shader for the instanced path

That avoids extending the instance payload with a second normal-matrix stream. If profiling later shows this is too expensive, a follow-up optimization can precompute per-instance normal matrices on the CPU.

## GL API Additions

Load:

- `VertexAttribDivisor`
- `DrawElementsInstanced`

Configure the instance matrix as four `vec4` attributes with divisor `1`.

After an instanced draw:

- reset divisors to `0` or maintain a dedicated VAO/state path so normal draws are unaffected

## Files

- [`src/runtime/graphics/vgfx3d_backend_opengl.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_opengl.c)

## Done When

- One draw call renders N instances
- Per-instance transforms work
- Instanced lighting stays correct under non-uniform scale
