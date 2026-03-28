# OGL-02: Fix Normal Matrix Bug

## Context
Line 653 copies model matrix as normal matrix. Same bug as D3D-02.

## Current Code
```c
gl.UniformMatrix4fv(ctx->uNormalMatrix, 1, GL_TRUE, cmd->model_matrix);
```

## Fix
Same approach as D3D-02 — compute inverse-transpose for non-uniform scaling:
```c
float nm[16];
memcpy(nm, cmd->model_matrix, 16 * sizeof(float));

// Extract scale per axis
float sx = sqrtf(nm[0]*nm[0] + nm[1]*nm[1] + nm[2]*nm[2]);
float sy = sqrtf(nm[4]*nm[4] + nm[5]*nm[5] + nm[6]*nm[6]);
float sz = sqrtf(nm[8]*nm[8] + nm[9]*nm[9] + nm[10]*nm[10]);

// If non-uniform scale, adjust by inverse scale²
if (fabsf(sx - sy) > 0.001f || fabsf(sy - sz) > 0.001f) {
    float isx2 = 1.0f / (sx * sx), isy2 = 1.0f / (sy * sy), isz2 = 1.0f / (sz * sz);
    nm[0] *= isx2; nm[1] *= isx2; nm[2] *= isx2;
    nm[4] *= isy2; nm[5] *= isy2; nm[6] *= isy2;
    nm[8] *= isz2; nm[9] *= isz2; nm[10] *= isz2;
}

gl.UniformMatrix4fv(ctx->uNormalMatrix, 1, GL_TRUE, nm);
```

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_opengl.c` — normal matrix computation at line 653

## Testing
- Non-uniformly scaled box → lighting correct on all faces
