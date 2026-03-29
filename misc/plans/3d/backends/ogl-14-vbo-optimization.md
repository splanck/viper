# OGL-14: Persistent Dynamic VBO/IBO

## Current State

The OpenGL backend creates and destroys a VBO and IBO for every draw. That is needlessly expensive and makes later features pile onto an already wasteful path.

## Implementation

Add persistent dynamic buffers to `gl_context_t`:

```c
GLuint dynamic_vbo;
GLuint dynamic_ibo;
size_t dynamic_vbo_size;
size_t dynamic_ibo_size;
```

Recommended initial sizes:

- VBO: 4 MiB
- IBO: 1 MiB

## Upload Strategy

For meshes that fit:

1. bind the persistent VBO/IBO
2. orphan with `glBufferData(..., NULL, GL_STREAM_DRAW)`
3. upload with `glBufferSubData`
4. draw from the persistent buffers

For oversized meshes:

- either grow the persistent buffers, or
- fall back to temporary per-draw buffers

The earlier plan did not define the oversize behavior. This needs to be explicit.

Recommended behavior:

- grow the persistent buffers to the largest seen size and keep them

## VAO Setup

Once the persistent buffers exist:

- create/bind the VAO once in `create_ctx()`
- configure all vertex attributes against the persistent VBO there
- later draws only need to bind the VAO and refresh buffer contents

If a temporary oversize fallback is retained, remember that VAO attribute bindings are buffer-specific and must be updated accordingly.

## Loader And Constant Additions

Load:

- `BufferSubData`

Add:

- `GL_STREAM_DRAW`

## Files

- [`src/runtime/graphics/vgfx3d_backend_opengl.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_opengl.c)

## Done When

- Regular draws no longer call `glGenBuffers` / `glDeleteBuffers`
- Output is unchanged
- Oversized meshes still render correctly
