# D3D-02: Fix Normal Matrix Bug

## Context
Line 537 copies the model matrix as the normal matrix. Should use the inverse-transpose of the upper-left 3x3 for correct normals under non-uniform scaling.

## Current Code (line 537)
```c
memcpy(obj.nm, cmd->model_matrix, 16 * sizeof(float));
```

## Fix
Compute inverse-transpose in C before uploading:
```c
// Compute inverse of model matrix
float inv[16];
if (!mat4f_inverse(cmd->model_matrix, inv)) {
    // Fallback: use model matrix (works for uniform scale + rotation)
    memcpy(obj.nm, cmd->model_matrix, 16 * sizeof(float));
} else {
    // Transpose the inverse
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            obj.nm[r * 4 + c] = inv[c * 4 + r];
}
```

Need `mat4f_inverse()` — a 4x4 float matrix inverse. Check if one already exists in the codebase:
- `rt_mat4.c` has `rt_mat4_inverse()` but it works on double, not float
- Add a static `mat4f_inverse()` helper in d3d11 backend (or convert model matrix to double, invert, convert back)

Simpler approach — for the common case (uniform scale + rotation), the model matrix IS its own normal matrix. Only non-uniform scaling breaks it. A pragmatic fix:
```c
// Extract scale from model matrix diagonal
float sx = sqrtf(cmd->model_matrix[0]*cmd->model_matrix[0] +
                 cmd->model_matrix[1]*cmd->model_matrix[1] +
                 cmd->model_matrix[2]*cmd->model_matrix[2]);
float sy = sqrtf(cmd->model_matrix[4]*cmd->model_matrix[4] +
                 cmd->model_matrix[5]*cmd->model_matrix[5] +
                 cmd->model_matrix[6]*cmd->model_matrix[6]);
float sz = sqrtf(cmd->model_matrix[8]*cmd->model_matrix[8] +
                 cmd->model_matrix[9]*cmd->model_matrix[9] +
                 cmd->model_matrix[10]*cmd->model_matrix[10]);

// If scale is uniform (sx ≈ sy ≈ sz), model matrix works as normal matrix
// If non-uniform, divide each row by its scale² to get inverse-transpose
memcpy(obj.nm, cmd->model_matrix, 16 * sizeof(float));
if (fabsf(sx - sy) > 0.001f || fabsf(sy - sz) > 0.001f) {
    float isx2 = 1.0f / (sx * sx), isy2 = 1.0f / (sy * sy), isz2 = 1.0f / (sz * sz);
    obj.nm[0] *= isx2; obj.nm[1] *= isx2; obj.nm[2] *= isx2;
    obj.nm[4] *= isy2; obj.nm[5] *= isy2; obj.nm[6] *= isy2;
    obj.nm[8] *= isz2; obj.nm[9] *= isz2; obj.nm[10] *= isz2;
}
```

This is faster than a full 4x4 inverse and handles the common rotation+non-uniform-scale case correctly.

## Files Modified
- `src/runtime/graphics/vgfx3d_backend_d3d11.c` — normal matrix computation in submit_draw (line 537)

## Testing
- Uniformly scaled mesh → identical to before
- Non-uniformly scaled mesh (e.g., stretched box) → normals correct, lighting not distorted
