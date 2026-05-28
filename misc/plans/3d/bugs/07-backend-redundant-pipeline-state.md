# Plan 07 — Cache backend pipeline state to skip redundant per-draw binds

- **Severity:** Medium (performance) — largest scope, highest risk; do last
- **Type:** Optimization (behavior-preserving, with careful invalidation)
- **Primary files:** `vgfx3d_backend_d3d11.c`, `vgfx3d_backend_opengl.c` (Metal optional)
- **Status:** Planned (do not code yet)

## Problem

The GPU backends re-bind invariant pipeline state on **every** draw call:

- **D3D11** `d3d11_bind_main_pipeline` (`vgfx3d_backend_d3d11.c:3770-3822`) sets, per draw: rasterizer,
  render targets, depth-stencil state, blend state, primitive topology, input layout, VS, PS, seven
  constant-buffer bindings, common state, and VS SRVs. Of these, topology / input layout / VS / PS /
  CB *bindings* are identical for every non-instanced main-pass draw.
- **OpenGL** `gl_submit_draw` (`vgfx3d_backend_opengl.c:4726`) calls `gl.UseProgram(ctx->program)`
  every draw even though `gl_begin_frame` already bound it (`4687`) and it never changes mid-pass;
  `configure_mesh_attributes` (`3037`) re-specifies all vertex-attribute pointers per draw.

## Investigation notes / nuances (important — this reframes the original finding)

- **Canvas3D already sorts and culls.** It frustum-culls (`occlusion_culling`, `rt_canvas3d.c:3833`)
  and depth-sorts the deferred queue: opaque **front-to-back** (`qsort … cmp_front_to_back`,
  `rt_canvas3d.c:3863`) for early-Z, transparent **back-to-front** (`cmp_back_to_front`, `:3892`) for
  correct blending. So the fix is **NOT** "add sorting" — re-sorting by material/pipeline would break
  the depth ordering these passes depend on (correctness + early-Z benefit). The fix is purely
  **backend-side state-change minimization**.
- Some state genuinely varies per draw and must stay dynamic: blend mode (opaque/alpha/additive),
  depth-write state (opaque vs. transparent vs. depth-disabled overlays), rasterizer (cull/wireframe),
  and the per-object/material/texture bindings. These should be **cached-and-skipped when equal to the
  last set value**, not hoisted.
- Truly invariant-per-pass state (topology, input layout, main VS/PS, CB *bindings* — not contents)
  can be bound once per pass and skipped thereafter.
- **Primary hazard: cache coherence across pass boundaries.** The backend changes pipeline state
  outside the main draw loop — shadow pass, skybox, post-FX, RTT switches, overlay pass,
  `begin_frame`. Any "last bound X" cache MUST be invalidated at every such boundary, or a stale cache
  will skip a needed re-bind and corrupt rendering. This is why this item is highest-risk and last.

## Proposed fix (incremental, measure-driven)

1. Add a `d3d11_pipeline_cache` / `gl_pipeline_cache` struct to each ctx holding the last-bound:
   blend mode, depth-state kind, rasterizer kind, topology, input-layout id, VS, PS, program. Add a
   single `invalidate_pipeline_cache(ctx)` helper.
2. In the per-draw bind paths, compare-then-set: only issue the API call when the value differs from
   the cache; update the cache on set.
3. Call `invalidate_pipeline_cache` at **every** pass boundary: `begin_frame`, shadow begin/end,
   skybox draw, post-FX present, RTT bind/unbind, overlay begin/end. (Audit each backend's pass
   transitions first and enumerate them in the implementation PR.)
4. OpenGL quick wins independent of the cache: drop the redundant per-draw `UseProgram` (rely on the
   `begin_frame` bind + cache), and only re-run `configure_mesh_attributes` when the bound VBO/IBO or
   layout actually changes.

## Files to modify

- `src/runtime/graphics/3d/backend/vgfx3d_backend_d3d11.c` (+ ctx)
- `src/runtime/graphics/3d/backend/vgfx3d_backend_opengl.c` (+ ctx)
- Optionally `vgfx3d_backend_metal.m` (Metal's encoder already de-dupes pipeline-state objects; lower
  value — defer).

## Tests

- **Correctness is the whole risk here.** Every existing `g3d_*` probe must be unchanged, *and* the
  probes that stress pass transitions must pass: shadows, skybox, post-FX, RTT, transparent-over-
  opaque, and screen overlays. Add a probe that interleaves opaque + transparent + overlay in one
  frame to stress cache invalidation across passes.
- Perf: many-object benchmark (shared with Plan 03) measuring draw-submit CPU time before/after.
- Per-platform validation (D3D11/Windows, OpenGL/Linux).

## Risk

High (for a perf change): a missed invalidation silently corrupts a later frame/pass. Land **after**
Plan 03, behind the same many-object benchmark, and review every pass-transition site. If risk/reward
looks marginal after Plan 03's win, this can be deferred — Plan 03 captures most of the per-draw CPU
cost on its own.
