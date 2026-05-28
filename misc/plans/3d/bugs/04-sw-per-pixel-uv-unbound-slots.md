# Plan 04 — Skip per-pixel UV interpolation for unbound texture slots (software backend)

- **Severity:** Medium (performance)
- **Type:** Optimization (behavior-preserving)
- **Primary file:** `src/runtime/graphics/3d/backend/vgfx3d_backend_sw.c`
- **Status:** Planned (do not code yet)

## Problem

In `raster_triangle`'s per-pixel PBR / normal-mapped lighting block, UV coordinates are interpolated
for **every** material slot unconditionally, even when the corresponding map is not bound:

```c
sw_interpolate_uv_for_slot(cmd, …NORMAL…,   &normal_u,   &normal_v);
sw_interpolate_uv_for_slot(cmd, …SPECULAR…, &specular_u, &specular_v);  // used only if specular_map
sw_interpolate_uv_for_slot(cmd, …EMISSIVE…, &emissive_u, &emissive_v);  // used only if emissive_tex
sw_interpolate_uv_for_slot(cmd, …METALLIC_ROUGHNESS…, &mr_u, &mr_v);    // used only if PBR + mr_map
sw_interpolate_uv_for_slot(cmd, …AO…,       &ao_u,       &ao_v);        // used only if ao_map
```
(`vgfx3d_backend_sw.c:1578-1628`)

Each `sw_interpolate_uv_for_slot` (`897-937`) does a perspective-correct barycentric interpolation
(3 mul-adds + a divide) plus an optional 2×3 affine UV transform. The `specular`, `emissive`,
`metallic_roughness`, and `ao` results are only consumed inside `if (specular_map)` /
`if (emissive_tex)` / `if (metallic_roughness_map)` / `if (ao_map)` guards later in the same block, so
when those maps are absent the interpolation is dead work — paid for every shaded pixel.

**Impact:** for the common case of a PBR material with only a base-color (+ maybe normal) map, this is
3–4 wasted perspective-correct UV interpolations per pixel across the whole covered area.

## Investigation notes / nuances

- `normal_u/v` is genuinely needed whenever the per-pixel block runs (it gates on `PBR || normal_map`
  and the normal-map TBN path consumes it), so leave it unconditional — or guard it on `normal_map`.
- The other four are each tied to a single map pointer that's already resolved once per draw
  (`specular_ptr`, `emissive_ptr`, `metallic_roughness_ptr`, `ao_ptr` in `sw_submit_draw`, passed into
  `raster_triangle`). Those pointers are constant across the triangle, so the guard is a cheap branch.
- Behavior is identical: the interpolated UV is only *read* under the same map-present condition we'd
  guard on, so guarding the *computation* changes nothing observable.
- Minor caution: ensure the variables stay initialized (they're already `= 0.0f`) so no
  uninitialized-read warning if a later reference isn't perfectly co-guarded.

## Proposed fix

Wrap each non-essential `sw_interpolate_uv_for_slot` call in the matching map-present test, e.g.:

```c
if (specular_map)
    sw_interpolate_uv_for_slot(cmd, …SPECULAR…, &specular_u, &specular_v);
```

…and likewise for emissive (`emissive_tex`), metallic-roughness (`metallic_roughness_map`), and AO
(`ao_map`). Keep `normal_u/v` as-is (or guard on `normal_map`). Leave the `= 0.0f` initializers.

## Files to modify

- `src/runtime/graphics/3d/backend/vgfx3d_backend_sw.c` — only.

## Tests

- PBR + normal-mapped visual probe frames (software backend) must match baseline exactly — guarding a
  computation whose result is only read under the same guard is behavior-neutral.
- Add a probe variant that *does* bind specular/emissive/AO maps to confirm those slots still sample
  correctly (i.e. the guards didn't disable a live path).
- Build + `ctest --test-dir build -L graphics3d`.

## Risk

Low. The only hazard is mis-pairing a guard with its consumer; verified the four target UVs are each
read solely under their respective map-present branch.
