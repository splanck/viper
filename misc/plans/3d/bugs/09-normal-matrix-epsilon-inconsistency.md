# Plan 09 — Unify the singular-matrix determinant epsilon

- **Severity:** Low–medium (correctness edge for very small-scaled objects)
- **Type:** Correctness / consistency
- **Primary file:** `src/runtime/graphics/3d/backend/vgfx3d_backend_utils.c`
- **Status:** Planned (do not code yet)

## Problem

Two sibling matrix routines use **different** thresholds to decide a matrix is singular:

- `vgfx3d_compute_normal_matrix4` treats the upper-3×3 as singular when `fabsf(det) <= 1e-8f`
  (`vgfx3d_backend_utils.c:394`) and falls back to the **identity** normal matrix.
- `vgfx3d_invert_matrix4` treats the 4×4 as singular when `fabsf(det) < 1e-12f`
  (`vgfx3d_backend_utils.c:476`).

The normal-matrix threshold (`1e-8`) is far more aggressive. Because a model matrix's 3×3 determinant
scales as the **cube** of object scale, a uniformly small object (scale below ~`(1e-8)^(1/3) ≈ 0.0046`)
has `|det| < 1e-8` and gets the **identity** normal matrix — i.e. its rotation is dropped and normals
are computed as if unrotated, mis-shading small props. `vgfx3d_invert_matrix4` would accept the same
matrix fine.

## Investigation notes / nuances

- The normal matrix is **renormalized per use** (in `vgfx3d_skin_vertices` and the SW vertex transform),
  so the *magnitude* of `inv_det` is irrelevant — only the cofactor **directions** matter. That means a
  small-but-nonzero determinant still yields a usable (direction-correct) normal matrix; rejecting it
  at `1e-8` loses correct normals for no numerical-stability benefit at that scale.
- So the fix is to **lower the normal-matrix threshold to match `invert_matrix4` (`1e-12`)** (or adopt
  a single shared constant), not to raise `invert`'s.
- Nuance / guard: don't go *too* permissive — a genuinely near-zero determinant (e.g. a degenerate
  flattened transform) can still produce wild cofactors. `1e-12` is the existing, already-trusted
  bound used by `invert_matrix4`, and the downstream renormalize + the existing `isfinite(inv_det)`
  check already protect against NaN/Inf. A *relative* epsilon (scaled by the 3×3 Frobenius norm) would
  be the most principled fix but is more code; the minimal, low-risk change is to align on `1e-12f`.
- Define a single named constant (e.g. `VGFX3D_MATRIX_SINGULAR_EPS`) used by both functions so they
  can't drift again.

## Proposed fix

1. Add a shared constant (in `vgfx3d_backend_utils.c`, file scope) e.g.
   `static const float kVgfx3dSingularDetEps = 1e-12f;`
2. `vgfx3d_compute_normal_matrix4`: change the `fabsf(det) > 1e-8f` test to use the shared constant
   (`> kVgfx3dSingularDetEps`). Keep the `isfinite(det)` guard and the identity fallback for the truly
   singular/non-finite case.
3. `vgfx3d_invert_matrix4`: change `1e-12f` to the shared constant (same value, just centralized).

## Files to modify

- `src/runtime/graphics/3d/backend/vgfx3d_backend_utils.c` — only.

## Tests

- Add unit tests in `test_vgfx3d_backend_utils.c`: a small-uniform-scale model matrix (e.g. scale
  0.002) should now yield a **rotation-correct** (non-identity) normal matrix; a genuinely singular
  matrix (zero column) should still fall back to identity. Confirm normals match the un-scaled
  rotation after renormalization.
- A visual probe with a small rotated prop + directional light would show the shading fix, but the
  unit test is the precise gate.
- Build + `ctest --test-dir build -L graphics3d`.

## Risk

Low. Aligns one threshold to an already-trusted value; the renormalize + finite guards bound the
behavior. Mainly improves shading of very small rotated objects.
