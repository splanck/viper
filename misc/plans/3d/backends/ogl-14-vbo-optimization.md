# OGL-14: VBO/IBO Per-Draw Optimization

## Context
Same issue as D3D-12. Lines 694-732 create + destroy VBO/IBO every draw call. OpenGL equivalent of Map/Discard is `glBufferData` with `GL_STREAM_DRAW` (orphan + re-upload).

## Implementation

### Persistent dynamic buffers
```c
// In create_ctx:
gl.GenBuffers(1, &ctx->dynamic_vbo);
gl.GenBuffers(1, &ctx->dynamic_ibo);

// Large enough for typical meshes
#define OGL_MAX_VBO_SIZE (4 * 1024 * 1024)  // 4MB
#define OGL_MAX_IBO_SIZE (1 * 1024 * 1024)  // 1MB

gl.BindBuffer(GL_ARRAY_BUFFER, ctx->dynamic_vbo);
gl.BufferData(GL_ARRAY_BUFFER, OGL_MAX_VBO_SIZE, NULL, GL_STREAM_DRAW);
gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ctx->dynamic_ibo);
gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, OGL_MAX_IBO_SIZE, NULL, GL_STREAM_DRAW);
```

### Per-draw: orphan + sub-upload
```c
GLsizei vbo_size = cmd->vertex_count * sizeof(vgfx3d_vertex_t);
GLsizei ibo_size = cmd->index_count * sizeof(uint32_t);

if (vbo_size <= OGL_MAX_VBO_SIZE && ibo_size <= OGL_MAX_IBO_SIZE) {
    gl.BindBuffer(GL_ARRAY_BUFFER, ctx->dynamic_vbo);
    // Orphan: passing NULL + GL_STREAM_DRAW tells driver to allocate new storage
    gl.BufferData(GL_ARRAY_BUFFER, vbo_size, NULL, GL_STREAM_DRAW);
    gl.BufferSubData(GL_ARRAY_BUFFER, 0, vbo_size, cmd->vertices);

    gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ctx->dynamic_ibo);
    gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, ibo_size, NULL, GL_STREAM_DRAW);
    gl.BufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, ibo_size, cmd->indices);
} else {
    // Fallback: per-draw temp buffers for oversized meshes
}
```

### Remove per-draw GenBuffers/DeleteBuffers
Delete the `gl.GenBuffers` / `gl.DeleteBuffers` calls in submit_draw (lines 694-696, 731-732). Use the persistent buffers instead.

### Clean up
Delete dynamic buffers in `gl_destroy_ctx()`.

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_opengl.c` — persistent buffers in context, orphan+sub-upload per draw, remove per-draw gen/delete

## Testing
- Same visual output
- Fewer GL API calls per frame
