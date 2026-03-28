# OGL-15: Instanced Rendering

## Context
Same as SW-06, MTL-13, D3D-15. OpenGL supports `glDrawElementsInstanced` with per-instance data in a second VBO.

## Implementation

### Loop approach (v1)
Same vtable extension as SW-06/D3D-15 — loop with per-instance model matrix.

### Hardware instancing (v2)
```c
// Instance buffer with per-instance model matrices
GLuint instBuf;
gl.GenBuffers(1, &instBuf);
gl.BindBuffer(GL_ARRAY_BUFFER, instBuf);
gl.BufferData(GL_ARRAY_BUFFER, instance_count * 64, instance_matrices, GL_STREAM_DRAW);

// Bind instance matrix as 4 vec4 attributes (mat4 = 4 × vec4)
for (int i = 0; i < 4; i++) {
    GLuint loc = 7 + i; // attributes 7-10 for instance matrix rows
    gl.EnableVertexAttribArray(loc);
    gl.VertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, 64, (void *)(i * 16));
    gl.VertexAttribDivisor(loc, 1); // per-instance
}

gl.DrawElementsInstanced(GL_TRIANGLES, cmd->index_count,
                         GL_UNSIGNED_INT, NULL, instance_count);

// Cleanup
gl.DeleteBuffers(1, &instBuf);
for (int i = 0; i < 4; i++)
    gl.VertexAttribDivisor(7 + i, 0); // reset
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
- `src/runtime/graphics/vgfx3d_backend.h` — vtable `submit_draw_instanced`
- `src/runtime/graphics/vgfx3d_backend_opengl.c` — instanced draw, GLSL instance matrix, GL function loading

## Testing
- Same tests as MTL-13 and D3D-15
