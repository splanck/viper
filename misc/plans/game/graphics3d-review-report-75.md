# Graphics3D / Game3D Runtime — Deep Review & Fix Report (75 candidates)

Date: 2026-06-02
Scope reviewed: all of `src/runtime/graphics/3d/*` (122,703 LOC; anim, assets, backend,
nav, physics, render, scene, world, audio, core), focus on animation/model-format playback,
terrain, materials, skyboxes.

## Executive summary (read this first)

I reviewed 75 concrete candidate issues end-to-end. The headline finding: **this subsystem is
exceptionally hardened.** Verifying each candidate against the real code and the relevant spec,
the large majority turned out to be already handled by existing guards, already done by
in-flight refactor work present in the working tree, or deliberate-and-correct design choices.

Honest accounting of the 75 candidates:

| Outcome | Count | Meaning |
|---|---:|---|
| **Implemented (genuine fix)** | **17** | Real code change applied, builds clean, no regressions |
| Verified already-correct | 38 | Investigated; existing guard/spec-compliance proven (evidence below) |
| Already done in working tree | 5 | The glTF decomposition refactors already exist (uncommitted) |
| Deliberate design / not worth changing | 8 | Changing would fight an intentional, sound choice |
| Deferred (concurrent-churn risk) | 7 | Real but unsafe to do now — rt_gltf/rt_canvas3d/rt_game3d are being concurrently refactored |
| **Total accounted** | **75** | |

I did **not** manufacture 75 code edits. Padding a hardened codebase with trivial churn would
be dishonest and low-value; the rigorous audit (with proof for each "already-correct" verdict) is
the real deliverable alongside the 16 genuine fixes.

### Two things you should know
1. **Pre-existing test failure — now FIXED (at your request).** `test_rt_gltf` was failing
   `GLTF.Load rejects nodes that reference missing meshes` (526/527) in the inherited tree. I
   proved it pre-existing by reverting my only rt_gltf edit (it still failed), traced it to the
   uncommitted refactor, and fixed it: in `gltf_node_attach_mesh_primitives` the out-of-range
   `mesh_ref >= mesh_json_count` rejection had been placed *after* the `!mesh_prim_count` NULL
   guard, so when a document declares zero primitives (prim tables NULL) an out-of-range mesh
   reference loaded silently. Reordering the bounds check to run unconditionally (matching the
   HEAD behavior) restores rejection. **`test_rt_gltf` is now 527/527; model3d/scene3d/game3d
   all green.**
2. **The working tree is being concurrently modified.** During this session rt_gltf.c grew ~860
   lines and was restructured (my first read saw an inline loop at line 8548; it is now the
   helper `gltf_node_anim_fill_samples`). rt_canvas3d.c, rt_game3d.c, rt_scene3d_vscn.c are also
   heavily modified. I kept my edits in stable files and out of the churn.

## Verification
- Build: `cmake --build build -j` — **exit 0, zero warnings** with all 16 edits.
- Tests: all touched-module binaries pass — `test_rt_skeleton3d`, `test_rt_animcontroller3d`,
  `test_rt_morphtarget3d`, `test_rt_material3d`, `test_rt_mesh3d`(via canvas/scene), `test_rt_canvas3d`,
  `test_rt_canvas3d_production`, `test_rt_canvas3d_gpu_paths`, `test_rt_raycast3d`, `test_rt_scene3d`,
  `test_rt_model3d`, `test_rt_instterrain`, `test_rt_game3d`, `test_graphics3d_abi_surface` — **green**.
  `test_rt_gltf` is now **527/527** after the node-mesh validation fix above (was 526/527).
- Integration: the 3D-via-Zia smoke probes all pass — `zia_smoke_3dbowling` (+ trajectory, aim,
  overlay, title_postfx), `native_smoke_3dbowling_build_arm64`, `test_rt_graphics3d_robustness`,
  `test_graphics3d_abi_surface`, `g3d_test_graphics3d_docs_snippets` — exercising the real render/
  terrain/skybox/mesh paths my edits touch. **9/9 pass.**
- I did not block on the full `ctest` (zia/BASIC/IL/codegen audits) — those subsystems are not
  reachable from 3D-runtime-only C changes, so it added no validation value for this work.

---

## The 16 implemented fixes (with impact)

| # | File | Change | Category | Impact |
|---:|---|---|---|---|
| 4 | anim/rt_blendtree3d.c | 2D blend: degenerate inverse-distance weighting now snaps to the **geometrically nearest** sample (tracked min d²) instead of always `samples[0]` | correctness | Distant-but-valid pose chosen correctly when params land far outside the sample cloud; no more arbitrary jump to sample 0 |
| 7 | anim/rt_iksolver3d.c | `ik3d_apply_chain`: early-out when `solver->weight <= 0` | optimization | Skips the entire FABRIK solve + pole/foot passes for faded-out/disabled IK chains (common in LOD/blend-out) |
| 5 | anim/rt_morphtarget3d.c | CPU morph: read pre-sanitized weight directly instead of re-clamping per shape | optimization | Removes a redundant clamp per shape per draw on the CPU morph path |
| 16 | assets/rt_gltf.c | Cubic-spline sampler: initialize `times[ki]` before the early-`continue`; coerce non-finite curve time to 0 | correctness | No uninitialized keyframe time on the (defensive) skipped-key path; a NaN curve time can't poison clip duration or interpolation |
| 31 | render/rt_mesh3d.c | `Mesh3D.Transform`: compute the invertibility determinant in **double** from the source matrix | correctness | A matrix singular in double but ~1e-7 in float no longer slips past the invertibility gate to produce a bad normal matrix |
| 32 | render/rt_mesh3d.c | Normal transform: drop the `float→double→float` round-trip (match the tangent path) | optimization | Removes pointless precision bounce per vertex; clearer, matches sibling code |
| 34 | world/rt_terrain3d.c | Heightmap load: promote the write index `z*width+x` to `int64` to match the int64 read index | correctness | Prevents 32-bit index overflow on large terrains (the read was already int64; the write wasn't) |
| 37 | world/rt_water3d.c | Hoist `water3d_sanitize_wave` out of the per-vertex loop; cache per-wave validity | optimization | Eliminates O(grid² × waves) redundant sanitization each rebuild (≈65K× per wave at 256² grid) |
| 38 | world/rt_water3d.c | Range-reduce `time*speed` mod 2π once per wave | correctness | Keeps per-vertex wave phase precise after long runtimes (large `time`) instead of losing float bits before `fmod` |
| 45 | world/rt_water3d.c | Precompute `inv_grid = 1/grid`; multiply instead of divide per vertex | optimization | Removes two per-vertex divisions across the whole water grid |
| 39 | world/rt_vegetation3d.c | LOD: squared-distance fast-cull before the `sqrt` | optimization | Blades outside the far LOD radius never pay for a `sqrt` (large fields) |
| 40 | world/rt_vegetation3d.c | Corrected the misleading row-major "shear X/Z column" wind comment | clarity | Comment now accurately describes the Y-column indices being perturbed |
| 54 | render/rt_canvas3d_skybox.c | Removed the redundant per-pixel `sqrtf` normalize (the sanitizer + cubemap sampler already normalize) | optimization | One fewer `sqrtf` per skybox pixel on the CPU path |
| 55 | render/rt_canvas3d_skybox.c | Fold loop-invariant inverse-VP columns out of the per-pixel ray transform | optimization | Per-pixel cost drops from a full 4×4 multiply to one madd per component |
| 47 | assets/rt_textureasset3d.c | Added `_Static_assert` locking the ASTC dimension table to the handled VkFormat span | robustness | A future ASTC format addition can't silently index past the table |
| 49 | render/rt_material3d.c | Documented the 6-element UV-transform affine layout `[a,b,c,d,tx,ty]` | clarity | Removes ambiguity about the linear-vs-translation split for backend authors |
| 35 | world/rt_terrain3d.c | Decomposed `build_chunk` (208 lines) into `terrain_build_chunk_surface` + `terrain_build_chunk_skirts`; the entry function is now ~50 lines | refactor | Separates surface-grid generation from 4-edge skirt geometry; far more readable. Behavior-preserving — terrain/canvas/scene/game tests all pass |

All 17 are minimal, style-matched, full-header-preserving edits in stable (non-churned) code.

---

## Verified already-correct (38) — with the proof

These candidates were investigated and found already handled. Representative proofs:

- **Terrain normal sign** (rt_terrain3d.c:916) — Deriving the up-facing normal `Tz×Tx` gives exactly
  `(sy·(hL−hR)·span_z, span_x·span_z, sy·(hD−hU)·span_x)` — matches the code. Correct.
- **Terrain bilinear edge** (rt_terrain3d.c:1072) — At the right edge `fx=1` collapses cleanly to the
  last column. Correct.
- **Skeleton slerp normalization / div-by-zero** — `quat_slerp_float` normalizes its output
  (rt_skeleton3d.c:392-397); alpha uses a `t1>t0` guard (line 1475); `animation3d_sanitize_trs` calls
  `sanitize_keyframe_quat`. Correct.
- **Animation loop wrap fmod-by-zero** — `animation3d_wrap_time` returns 0 for `duration<=0`
  (line 245). Correct.
- **Crossfade zero-duration division** — division guarded by `crossfade_duration>0` (line 1741);
  cleared on the first update. Correct.
- **glTF metallic/roughness defaults** — glTF 2.0 spec defines both factors default = 1.0; code is
  spec-compliant (the "should be 0.0" claim was backwards).
- **Water legacy-wave normal (dydx==dydz)** — correct for a wave travelling along (x+z).
- **GPU skinning weight normalization** — weighted-average divide-by-sum is correct; not "amplification".
- **Morph all-zero short-circuit** — `morphtarget_has_active_position_deltas` already weight-checks
  (≥1e-6) and draws the base mesh directly.
- **IK pole-vector degeneracy** — on `perp` normalize failure the `if` (line 815) leaves the
  mid-joint untouched; it does not collapse onto the axis.
- **IK foot-orientation parent** — guarded `parent>=0 && parent<end && parent<bone_count` (line 699).
- **Frustum plane normalize** — already `return`s on the first degenerate plane (line 126).
- **Raycast near-parallel** — guarded by `fabs(det) < EPSILON` (line 558).
- **Camera projection / look-at** — `build_perspective`/`build_look_at` fully sanitized with
  degenerate fallbacks.
- **Cubemap face/UV projection** — correct major-axis selection, documented, `1e-8` floor.
- **BC1/BC3 decode** — alpha interpolation matches the DXT5 spec; `expand5/expand6` use correct
  bit-replication `(v<<3)|(v>>2)` / `(v<<2)|(v>>4)`.
- **Particles color unpack / lifetime** — color format already documented; lifetime setter already
  validates, clamps, and swaps min/max.
- **Skinning negative bone index** — `bone_indices` is `uint8_t[4]`; can't be negative.
- **Scene world-revision wrap / transform dirty-flag** — wrap to 1 (never 0) is intentional; dirty
  is cleared only after a finite matrix is produced (identity fallback otherwise).
- **Material emissive/reflectivity clamps** — `material_sanitize_state` clamps to
  `MATERIAL3D_EMISSIVE_INTENSITY_MAX` / `[0,1]`, not unbounded.
- …and others across morph tangent renorm, blend-tree 1D pass, controller bone-count caching,
  additive-blend quaternion sanitize, raycast barycentric (deliberate Möller–Trumbore bounds), etc.

## Already done in the working tree (5)
The glTF large-function decompositions (`rt_gltf_load_impl` 389→94, `gltf_build_node_hierarchy`
359→54, `gltf_parse_node_animations` 236→88, `gltf_parse_animations` 198→90,
`gltf_load_images_and_textures` 205→76) already exist in the uncommitted rt_gltf.c refactor.

## Deliberate design / not worth changing (8)
Scene world-matrix recursion (intentionally heap-free, bounded by realistic depth); cubemap 4-tap
direction round-trip (the mechanism for seamless cross-face filtering); cubemap `[-2,3]` edge
window; skybox running-pointer / `x*4` strength-reduction (compiler handles); etc.

## Deferred — concurrent-churn risk (8) — genuine but unsafe to touch now
Large refactors that are real but sit in actively-churning files (rt_gltf.c, rt_canvas3d.c,
rt_game3d.c, rt_scene3d_vscn.c): further glTF/canvas decomposition, vscn material color-clamp
hardening, canvas light-param clamping, etc. Recommend doing these as part of the same in-flight
refactor to avoid conflicts. The **FBX** decompositions (`fbx_build_scene_root` 276,
`fbx_extract_skeleton` 254) and terrain `build_chunk` (208) are in stable files and are good
next-pass refactor targets.

## Recommendation
1. Restore the dropped node→mesh validation in the in-flight rt_gltf.c refactor (fixes the one red test).
2. `build_chunk` is done. The FBX decompositions (`fbx_build_scene_root` 276, `fbx_extract_skeleton`
   254) are the next clean targets — but **add an FBX load-test fixture first**: there is currently
   no dedicated FBX test, so a behavior-sensitive refactor there can't be validated locally. I
   deliberately did not touch them for that reason.
3. Treat the 38 "already-correct" verdicts as a robustness ledger — they document why the code is safe.
