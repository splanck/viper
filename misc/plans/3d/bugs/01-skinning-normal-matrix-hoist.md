# Plan 01 — Hoist the skinning normal-matrix computation out of the vertex loop

- **Severity:** High (performance)
- **Type:** Optimization (behavior-preserving)
- **Primary file:** `src/runtime/graphics/3d/backend/vgfx3d_skinning.c`
- **Status:** Planned (do not code yet)

## Problem

`vgfx3d_skin_vertices` (the CPU 4-influence linear-blend skinning path) recomputes each bone's
inverse-transpose **normal matrix inside the per-vertex, per-influence loop**:

```c
for (uint32_t v = 0; v < vertex_count; v++) {        // per vertex
    for (int b = 0; b < 4; b++) {                    // per influence
        ...
        const float *m = &palette[idx * 16];
        float nm[16];
        vgfx3d_compute_normal_matrix4(m, nm);        // <-- full 3x3 cofactor inverse, every time
        ...
    }
}
```
(`vgfx3d_skinning.c:67-69`)

`vgfx3d_compute_normal_matrix4` is a full 3×3 cofactor inverse with nine `isfinite` checks
(`vgfx3d_backend_utils.c:368-414`). It depends **only** on `palette[idx]`, never on the vertex.

**Impact:** for a 10k-vertex mesh weighted to a 50-bone skeleton, this performs up to
`10000 × 4 = 40,000` matrix inversions where **`bone_count` (≤ a few hundred)** would suffice — a
~3 order-of-magnitude reduction in the per-skinned-mesh CPU cost of the normal path.

## Investigation notes / nuances

- **Standalone function, internal fix.** Only caller of the CPU path is
  `rt_canvas3d_draw_mesh_matrix_skinned_keyed` (`rt_skeleton3d.c:1533`), which already `malloc`s a
  per-call `skinned` vertex buffer (line 1528) and tracks it as a temp buffer. The GPU-skinning
  branch is taken first when the backend prefers it (`vgfx3d_backend_prefers_gpu_skinning`), so this
  path only runs for the software backend / small palettes. Adding one more small allocation here is
  consistent with the existing pattern and negligible against the vertex loop.
- **`bone_count` is caller-supplied and unbounded by this function.** A fixed stack array sized to a
  hard cap (e.g. 256 → 16 KB) is risky if a caller passes more. Prefer a heap palette sized exactly
  `bone_count`.
- **Bit-identical output.** Precomputing `palette[idx]`'s normal matrix once yields exactly the same
  `nm` the inner loop computes today, so the skinned positions/normals are unchanged. This is a pure
  hoist, not an algorithm change — important for golden/visual-probe stability.
- Unused bones cost one extra inverse each (we precompute all `bone_count`, even unreferenced ones),
  but `bone_count ≪ vertex_count*4`, so this is still a massive net win and keeps the code simple.

## Proposed fix

Inside `vgfx3d_skin_vertices`, after the `palette`/`bone_count` guard and before the vertex loop:

1. Allocate `float *normal_palette = malloc((size_t)bone_count * 16 * sizeof(float))` (guard the
   `bone_count * 16` multiply against overflow with the same idiom used elsewhere; `bone_count > 0`
   already checked).
2. If allocation fails, **fall back to the current in-loop behavior** (compute `nm` per influence) so
   skinning still works under memory pressure — no new failure mode.
3. Fill `normal_palette[i*16]` with `vgfx3d_compute_normal_matrix4(&palette[i*16], …)` for
   `i in [0, bone_count)`.
4. In the inner loop, replace the `vgfx3d_compute_normal_matrix4(m, nm)` call with
   `const float *nm = &normal_palette[idx * 16];`.
5. `free(normal_palette)` before every return path after allocation.

Optional (nice-to-have, defer): lazily compute only referenced bones with a `computed[]` bitmap —
more complex, not worth it given `bone_count` is small.

## Files to modify

- `src/runtime/graphics/3d/backend/vgfx3d_skinning.c` — only.

## Tests

- **Regression (must stay identical):** extend `src/tests/unit/test_vgfx3d_backend_utils.c`
  (already exercises `vgfx3d_skin_vertices` at lines 469/480/502) with a multi-bone, multi-vertex
  case and assert position + normal are unchanged vs. a reference computed the old way (or hard-coded
  expected values). The hoist must be bit-identical.
- Add a degenerate-palette case (singular bone matrix) to confirm the identity fallback inside
  `vgfx3d_compute_normal_matrix4` still applies per bone.
- Build + `ctest --test-dir build -L graphics3d`; a skinned-mesh visual probe frame should match
  baseline.

## Risk

Very low. Pure hoist with a malloc-failure fallback to current behavior. No API/signature change.
