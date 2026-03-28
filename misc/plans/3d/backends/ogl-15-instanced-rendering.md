# OGL-15: Instanced Rendering

## Context
Same as MTL-13 and D3D-15. OpenGL supports `glDrawElementsInstanced` with per-instance data in a second VBO. The current `InstanceBatch3D` path already bypasses Canvas3D's deferred queue, so the win here is fewer backend calls and a single instanced draw, not queue overhead.

## Implementation

### Phase 1: shared optional instanced hook
Add a shared optional `submit_draw_instanced()` entry to [`src/runtime/graphics/vgfx3d_backend.h`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend.h), and update [`src/runtime/graphics/rt_instbatch3d.c`](/Users/stephen/git/viper/src/runtime/graphics/rt_instbatch3d.c) to use it when present. Unsupported backends keep the current loop.

### Hardware instancing (v2)
```c
// Reuse a cached instance buffer — grow only when needed (avoid per-draw gen/delete)
if (!ctx->inst_buf) gl.GenBuffers(1, &ctx->inst_buf);
gl.BindBuffer(GL_ARRAY_BUFFER, ctx->inst_buf);
size_t needed = instance_count * 64;
if (needed > ctx->inst_buf_size) {
    gl.BufferData(GL_ARRAY_BUFFER, needed, instance_matrices, GL_STREAM_DRAW);
    ctx->inst_buf_size = needed;
} else {
    gl.BufferData(GL_ARRAY_BUFFER, needed, NULL, GL_STREAM_DRAW); // orphan
    gl.BufferSubData(GL_ARRAY_BUFFER, 0, needed, instance_matrices);
}

// Bind instance matrix as 4 vec4 attributes (mat4 = 4 × vec4)
for (int i = 0; i < 4; i++) {
    GLuint loc = 7 + i; // attributes 7-10 for instance matrix rows
    gl.EnableVertexAttribArray(loc);
    gl.VertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, 64, (void *)(i * 16));
    gl.VertexAttribDivisor(loc, 1); // per-instance
}

gl.DrawElementsInstanced(GL_TRIANGLES, cmd->index_count,
                         GL_UNSIGNED_INT, NULL, instance_count);

// Reset divisors for non-instanced draws (buffer stays cached)
for (int i = 0; i < 4; i++)
    gl.VertexAttribDivisor(7 + i, 0);
```

### GLSL vertex shader modification
```glsl
layout(location=7) in mat4 aInstanceMatrix;
uniform int uUseInstancing;

void main() {
    mat4 model = (uUseInstancing != 0) ? aInstanceMatrix : uModelMatrix;
    vec4 wp = model * vec4(aPosition, 1.0);
    // ...
}
```

### Load additional GL functions
```c
LOAD(VertexAttribDivisor);
LOAD(DrawElementsInstanced);
```

Core GL 3.3.

## Files Modified
- `src/runtime/graphics/vgfx3d_backend.h` — optional shared instanced hook
- `src/runtime/graphics/rt_instbatch3d.c` — hook dispatch
- `src/runtime/graphics/vgfx3d_backend_opengl.c` — instanced draw, GLSL instance matrix, GL function loading

## Testing
- Same tests as MTL-13 and D3D-15
