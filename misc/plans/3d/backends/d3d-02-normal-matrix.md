# D3D-02: Fix Normal Matrix Bug

## Problem

The backend currently copies the model matrix into the normal-matrix slot. That is only correct for rigid transforms and uniform scale.

## Correction To The Earlier Plan

Do not use the earlier scale-length heuristic. As with the OpenGL review, it is only an approximation and is not the right implementation plan for general affine transforms.

The D3D11 plan should compute the true inverse-transpose of the upper-left 3x3.

## Implementation

Add a helper in [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c):

1. extract the upper-left 3x3 from `cmd->model_matrix`
2. compute its determinant and inverse
3. transpose the inverse
4. repack it into a 4x4 with translation cleared and the bottom row/column set appropriately

Fallback behavior:

- if the 3x3 is singular or nearly singular, fall back to the model matrix upper-left 3x3 rather than writing NaNs

## Integration Points

- replace the current `memcpy(obj.nm, cmd->model_matrix, ...)`
- reuse the same helper in the future instanced path rather than creating a second normal-matrix implementation

## Files

- [`src/runtime/graphics/vgfx3d_backend_d3d11.c`](/Users/stephen/git/viper/src/runtime/graphics/vgfx3d_backend_d3d11.c)

## Done When

- non-uniformly scaled meshes shade correctly
- no NaNs or unstable lighting on singular transforms
