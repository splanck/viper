# OGL-17: Backend-Owned CubeMap Skybox Rendering

## Depends On

- OGL-04 recommended for cubemap texture reuse
- shared CubeMap3D runtime already present in [`src/runtime/graphics/rt_cubemap3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_cubemap3d.c)

## Current State

The shared runtime already exposes:

- `CubeMap3D.New(...)`
- `Canvas3D.SetSkybox(...)`
- CPU cubemap sampling in [`rt_cubemap_sample()`](/Users/stephen/git/viper/src/runtime/graphics/rt_cubemap3d.c)

But onscreen GPU rendering still does not own the skybox:

- [`rt_canvas3d_end()`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c#L762) rasterizes the skybox directly into the software framebuffer before geometry submission.
- That path is visible for the software backend and was made safe for GPU RTT readback, but it is not a real OpenGL scene pass.
- [`vgfx3d_backend_opengl.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_opengl.c) has no cubemap upload path, no skybox shader, and no backend hook that receives the active skybox.

This is the main remaining OpenGL presentation gap after OGL-01 through OGL-16.

## Required Shared Prerequisites

The backend abstraction must expose the active skybox to GPU backends. Do not hide this behind implicit global state.

Recommended shape:

1. add an optional backend hook for skybox submission, or
2. extend frame parameters so `begin_frame()` receives the active cubemap

The first option is clearer because skybox rendering is a distinct pass in [`rt_canvas3d_end()`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c#L762).

Recommended vtable addition:

```c
void (*draw_skybox)(void *ctx, const void *cubemap);
```

Shared runtime rules:

- keep the current CPU skybox path for the software backend
- call `draw_skybox()` from [`rt_canvas3d_end()`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c) when the active backend provides it
- preserve the current CPU fallback when the backend hook is `NULL`
- allow skybox-only scenes: `draw_count == 0` must still produce a visible frame
- keep the GPU RTT overwrite fix from OGL-10; this plan is about onscreen GPU ownership, not regressing RTT behavior

## OpenGL Backend Scope

### 1. Cubemap texture upload + cache

Add a cubemap cache parallel to the existing 2D texture cache:

- cache key: the `rt_cubemap3d *` pointer
- cache value: `GLuint` cubemap texture
- lifetime: per backend context, destroyed in `destroy_ctx()`

v1 should match the current 2D texture-cache policy:

- no mutation tracking of face `Pixels` contents beyond object identity
- no mipmaps
- linear filtering
- `GL_CLAMP_TO_EDGE` on `S`, `T`, and `R`

Upload path:

- convert each face into tightly packed RGBA8 bytes using the same CPU-side unpack helper style already used for 2D textures
- upload faces in runtime order:
  - `+X`, `-X`, `+Y`, `-Y`, `+Z`, `-Z`
- use:
  - `GL_TEXTURE_CUBE_MAP`
  - `GL_TEXTURE_CUBE_MAP_POSITIVE_X + face_index`

### 2. Skybox shader program

Add a dedicated skybox shader pair. Reusing the lit-material shader is the wrong model.

Vertex shader requirements:

- render an inside-out cube or a regular cube with front-face culling adjusted
- strip translation from the camera view matrix
- use only view rotation + projection so the skybox stays at infinity

Fragment shader requirements:

- sample `samplerCube`
- output cubemap color directly
- no lighting, fog, or material alpha

Do not depend on inverse-VP reconstruction unless that infrastructure is already justified elsewhere. A static cube mesh is simpler and sufficient for GL 3.3 core.

### 3. Render-state rules

Draw the skybox before scene geometry with:

- depth writes disabled
- depth test disabled or `GL_LEQUAL`
- face culling adjusted for viewing the cube from inside

After the skybox pass, restore the normal scene state:

- `GL_LESS`
- standard culling mode
- depth writes enabled

The plan must not leave these transitions implicit.

### 4. Skybox resources

Add minimal backend-local resources:

- skybox program
- skybox VAO
- static cube VBO/IBO, or a `gl_VertexID`-driven cube if that is cleaner

These resources should be created once in `create_ctx()` and destroyed in `destroy_ctx()`.

## Loader And Constant Additions

Load any missing cubemap-related GL entry points/constants required by the implementation, including:

- `GL_TEXTURE_CUBE_MAP`
- `GL_TEXTURE_CUBE_MAP_POSITIVE_X`
- `GL_TEXTURE_WRAP_R`

If no new function pointers are required beyond already-loaded texture APIs, document that explicitly in the implementation notes.

## Files

- [`src/runtime/graphics/vgfx3d_backend.h`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend.h)
- [`src/runtime/graphics/rt_canvas3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_canvas3d.c)
- [`src/runtime/graphics/vgfx3d_backend_opengl.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_opengl.c)

## Done When

- `Canvas3D.SetSkybox()` produces a visible skybox on the OpenGL backend even with no scene geometry
- camera translation does not move the skybox
- the CPU skybox path remains intact for the software backend
- GPU RTT behavior from OGL-10 remains correct
