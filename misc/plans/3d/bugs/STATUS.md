# 3D Backend Bug Remediation — Implementation Status

_Updated 2026-05-28. Environment: macOS (Apple Clang). The build compiles only the
**Metal + software** backends here; D3D11 (`#if _WIN32`) and OpenGL (`#if __linux__`)
bodies are preprocessor-excluded, and the graphics3d test probes force
`VIPER_3D_BACKEND=software`, so no GPU backend is behaviorally exercised by the suite._

## Implemented and verified (build green, full graphics3d suite passing)

| Plan | Change | Verification |
|------|--------|--------------|
| 01 | Skinning normal-matrix hoisted out of the per-vertex×per-bone loop (`vgfx3d_skinning.c`) | New unit test `test_skinning_multibone_reuses_normal_palette` + existing skin tests |
| 02 | Terrain-splat view setup hoisted out of the per-pixel loop (`vgfx3d_backend_sw.c`) | graphics3d visual probes (walk_min / showcase) unchanged |
| 04 | Per-pixel UV interpolation guarded for unbound specular/emissive/MR/AO slots (`vgfx3d_backend_sw.c`) | graphics3d visual probes unchanged |
| 05 | Env-reflection reuses the main path's perturbed normal instead of rebuilding the TBN (`vgfx3d_backend_sw.c`) | graphics3d visual probes unchanged |
| 06 | Frustum planes validated once at extract via `planes_valid`; per-object tests short-circuit (`vgfx3d_frustum.{c,h}`) | New unit test `test_frustum_valid_classifies_volumes` |
| 08 | Software clear/depth-reset loops widened to `size_t` (`vgfx3d_backend_sw.c`) | graphics3d suite |
| 09 | Singular-determinant epsilon unified (`1e-8`→shared `1e-12`) (`vgfx3d_backend_utils.c`) | New unit test `test_compute_normal_matrix_small_scale` (fails under the old threshold) |

Plus a pre-existing test failure fixed: `test_rt_canvas3d_gpu_paths` — the WIP morph-draw
refactor passed a stack mesh copy to the keyed draw, so the original mesh was no longer
retained (a `temp_obj_count` regression **and** a latent use-after-free on its vertex
buffer). Fixed in `rt_morphtarget3d.c` by retaining the original mesh; 294/294 assertions pass.

## Deferred — require a per-platform build + render verification

Plans **03 (GPU frame-constant gate)**, **07 (backend pipeline-state cache)**, and **10
(OpenGL shadow-VP upload, subsumed by 03)** were **not implemented** in this session. They
target the D3D11 and OpenGL backends, which cannot be compiled or run on macOS, and the
Metal backend, which is not exercised by the automated suite. These are correctness-sensitive
caching changes; shipping them without a compiler or renderer on the target platform risks
silent rendering corruption that this environment cannot detect. The detailed plans
(`03-…`, `07-…`, `10-…`) remain the implementation specs, enriched with the findings below.

### Coherency findings to apply when implementing Plan 03 (discovered during this session)

- **D3D11:** `cb_per_scene` is written by **three** call sites — `d3d11_submit_draw`
  (~4032), `d3d11_submit_draw_instanced` (~4135), **and `d3d11_shadow_draw` (~5709)**.
  `cb_per_lights` is written only by the two main-pass draws. A content-cache must therefore
  (a) be shared by both draw functions, and (b) be invalidated whenever the shadow pass (or
  any non-main pass) writes `cb_per_scene`, in addition to the per-`begin_frame` reset.
- **Metal:** `setVertexBytes`/`setFragmentBytes` bindings do **not** persist across render
  command encoders. Encoders are (re)created at `vgfx3d_backend_metal.m` ~1360 and ~3524, so
  the cache must be invalidated at those sites, not merely per frame. The realistic CPU win
  on Metal is also limited: `setBytes` is cheap (inline argument buffer); the dominant
  per-draw cost is the matrix-transpose pack, which a value-cache only avoids if the gate
  compares *inputs* before packing.
- **OpenGL:** the gate must split `upload_main_uniforms` into a frame-constant group
  (VP/camera/fog/shadow-VPs/lights) uploaded once per frame and a per-object group; the
  program object persists uniforms, so this is safe as long as the program is not relinked
  mid-pass.

### Why Plan 07 is the riskiest

Its own spec notes "a missed invalidation silently corrupts a later frame." It is a stateful
cache across pass boundaries (shadow / skybox / post-FX / RTT / overlay), on backends that
cannot be verified here. Recommend implementing it only after Plan 03 lands and only with a
many-object benchmark on a real Windows/Linux GPU.
