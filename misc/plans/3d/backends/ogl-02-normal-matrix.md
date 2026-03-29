# OGL-02: Fix Normal Matrix Bug

## Problem

The backend currently uploads `cmd->model_matrix` as `uNormalMatrix`. That is only correct for rigid transforms and uniform scale. Non-uniform scale produces visibly wrong lighting.

## Correction To The Earlier Plan

Do not use the earlier scale-length heuristic. It only approximates inverse-transpose for a narrow class of matrices and does not correctly handle general affine transforms.

The OpenGL plan should compute the true inverse-transpose of the upper-left 3x3 of the model matrix.

## Implementation

Add a helper in [`src/runtime/graphics/vgfx3d_backend_opengl.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_opengl.c):

1. Extract the upper-left 3x3 from the row-major model matrix.
2. Compute its determinant and inverse.
3. Transpose the inverse to build the normal matrix.
4. Repack it into a 4x4 matrix with:
   - last column = `0, 0, 0, 1`
   - translation row/column zeroed
5. Upload that 4x4 with `GL_TRUE` transpose, matching the backend's row-major convention.

Fallback behavior:

- If the 3x3 is singular or nearly singular, fall back to the model matrix upper-left 3x3 rather than emitting NaNs.

## Integration Points

- Replace the current `uNormalMatrix` upload in the main draw path.
- Reuse the same helper in the future instanced path rather than duplicating matrix logic in OGL-15.

## Files

- [`src/runtime/graphics/vgfx3d_backend_opengl.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_opengl.c)

## Done When

- Non-uniformly scaled meshes shade correctly
- No NaNs or wild lighting on singular transforms
