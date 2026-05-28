# Plan 05 — Reuse the perturbed normal in the software env-reflection path

- **Severity:** Medium (performance) — narrow: only reflective **and** normal-mapped materials
- **Type:** Optimization (behavior-preserving)
- **Primary file:** `src/runtime/graphics/3d/backend/vgfx3d_backend_sw.c`
- **Status:** Planned (do not code yet)

## Problem

When a fragment uses both per-pixel normal-mapped lighting and environment reflection, the
tangent-space → world-space normal perturbation (sample normal map, build TBN, transform, normalize)
runs **twice per pixel**:

1. In the main per-pixel lighting block (`raster_triangle`, `vgfx3d_backend_sw.c:1643-1690`) — computes
   the perturbed world normal `pnx/pny/pnz` used for lighting.
2. Again inside `sw_apply_environment_reflection` (`1144-1280`, called at `2024-2038`), which
   re-interpolates the normal, re-samples the normal map, and rebuilds the TBN from scratch
   (`1190-1240`) to compute the reflection vector.

The normal-map sample + TBN construction is the expensive part (a texture fetch + several
normalizations). Doing it twice for the same fragment is pure redundancy.

## Investigation notes / nuances

- The main block's perturbed `pnx/pny/pnz` are declared **inside a nested scope** that only runs when
  `!cmd->unlit && (workflow == PBR || normal_map)` (`1564-1567`). To reuse them in the reflection
  call (made later, at `2024`), they must be lifted to a scope visible at the reflection call site,
  with a "valid" flag.
- **The reflection path can run when the main block did not.** Reflection is gated only on
  `cmd->env_map && cmd->reflectivity > 0.0001f` (`2024`). An **unlit** material with an env map (and
  possibly a normal map) reflects but skips the main lighting block — so `sw_apply_environment_
  reflection` must keep its own normal/TBN computation as a fallback. The optimization is *conditional
  reuse*, not removal.
- Output must stay identical: the perturbed normal computed in the main block is exactly what the
  reflection function would recompute (same interpolation, same TBN, same normal-map sample), so
  passing it through is behavior-neutral for the lit+normal-mapped case.

## Proposed fix

1. In `raster_triangle`, hoist the final perturbed world normal (`pnx/pny/pnz`) and a
   `int perturbed_normal_valid` flag to a scope that reaches the reflection call (initialize the flag
   to 0; set it to 1 at the end of the main block once `pnx/pny/pnz` hold the final perturbed normal).
2. Extend `sw_apply_environment_reflection` with optional inputs: a precomputed world normal + a
   `has_precomputed_normal` flag. When set, skip the internal interpolation + normal-map TBN block and
   use the supplied normal; otherwise compute as today (preserves the unlit-with-env path).
3. At the call site, pass the hoisted normal + flag.

Keep the reflection function's view-vector and roughness computation as-is (cheap, and roughness may
come from the mr map which the main block doesn't necessarily resolve into a scalar the same way).

## Files to modify

- `src/runtime/graphics/3d/backend/vgfx3d_backend_sw.c` — only (one static function signature changes;
  it has a single caller).

## Tests

- Reflective + normal-mapped visual probe (software backend) frame must match baseline exactly.
- Confirm the **unlit + env_map + normal_map** case still reflects correctly (the fallback path):
  add/extend a probe so the precomputed-normal branch and the fallback branch are both exercised.
- Build + `ctest --test-dir build -L graphics3d`.

## Risk

Low–medium. The scope hoist plus the dual-path (reuse vs. fallback) is the only subtlety; the
fallback guarantees the rarely-hit unlit-reflective case is unchanged. Lower priority than 01/02
because it only benefits reflective + normal-mapped surfaces.
