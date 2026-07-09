# 03 — Engine: Point-Light Shadows, Shadow Budget Decoupling, Light Telemetry

> **STATUS: IMPLEMENTED (2026-07-07)** · Baseline `3166d1dc2` · Track E.
> Shipped: **E10** — `VGFX3D_MAX_SHADOW_LIGHTS` 4→12 with `VGFX3D_CSM_SLOTS 4` for
> cascade-semantic sizes (splits stay a float4; audited so the shader payload cannot widen).
> Slots 0-3 stay per-texture; slots 4-11 are tiles of each GPU backend's INTERNAL 4×2 depth
> atlas (Metal texture 17 / D3D11 t17; static per-tile UV rects in-shader — no constant-buffer
> layout change), driven through the unchanged `shadow_begin/draw/end` vtable. Software keeps
> per-slot CPU buffers for all 12. New `vgfx3d_backend_t.shadow_atlas_slots` capability clamps
> the frame to 4 slots on backends without atlas sampling — **OpenGL stays there by explicit
> waiver: its 16 fragment samplers are fully assigned (units 0-15 + morph 16/17); the recorded
> follow-up is unifying ALL GL slots into one atlas, which frees three units.** The flip also
> surfaced and fixed two latent bind bugs the old constant masked (D3D11 srvs[4..15] clobbering
> splat/env slots; the same loop shape on GL). CSM decoupling proof:
> `tests/runtime/test_canvas3d_shadow_budget.zia` — 3 cascades + 5 shadowed spots →
> `ShadowSlotsUsed == 8`, zero drops (impossible on the old shared budget), budget starvation
> observable, plus a toggle-based visual assert that an atlas-slot shadow really darkens.
> **E9** — omnidirectional point shadows: a granted point light claims 6 consecutive atlas
> slots (92° perspective faces, +X,-X,+Y,-Y,+Z,-Z, far plane derived from caster bounds like
> spots — SetShadowNearFar deemed unnecessary; resolution is the uniform EnableShadows tile
> size; hemisphere demotion deferred until a consumer needs it). Shaders (MSL/HLSL/SW) select
> the face by dominant axis then reuse the perspective path; `VGFX3D_SHADOW_PROJECTION_CUBE`;
> selection scores point lights like spots; `BackendSupports("shadow-point")`. Proof:
> `tests/runtime/test_canvas3d_point_shadows.zia` — sun + cube = exactly 7 slots, zero drops,
> and toggling only the point light's `CastsShadows` measurably darkens the scene (PASS on
> Metal AND software; self-skips on non-atlas backends). **E11/E12** — `SetClusterLightBudget`
> (8..64 per-cluster cap in the cluster prefix-sum), `get_ClusterOverflowCount`,
> `get_DroppedLightCount` (forward-path truncation), `SetShadowBudget`/`get_ShadowSlotsUsed`/
> `get_ShadowRequestsDropped`; stale `rt_light3d.c` "up to 16" comment fixed.
> **Bugs found & fixed along the way:** BUG-E8 — software legacy materials NEVER received
> shadows (per-vertex path lacks shadow sampling; now forced per-pixel when a shadowed light
> is present), plus the shadow-light dedupe/match and shading-loop gates predating point
> lights. Full `-L graphics3d` 98/98; runtime completeness green. D3D11 changes are
> pattern-matched for the Windows lane. Docs: rendering3d.md lighting/shadow sections
> rewritten (12-slot model, point shadows, budgets/telemetry, Light3D.CastsShadows).
> Eliminates constraints #1 (point lights never shadow) and #2 (4-slot budget shared with
> CSM cascades, silent overflow). Showcase consumer: L5 Hydroponics Caverns (bioluminescent
> point-light shadows), L2/L4 interiors, muzzle-flash shadows in set-pieces.

## 0. TL;DR

Point lights currently accept `set_CastsShadows` but never allocate a shadow slot
(docs/viperlib/graphics/rendering3d.md:325,760 — the one true stub found in validation).
The 4-entry shadow array is shared between CSM cascades and every other shadow light
(`VGFX3D_MAX_SHADOW_LIGHTS = 4`, `rt_canvas3d_internal.h:490`; cascade loop
`rt_canvas3d_render_pass.inc:99-183`; selection `rt_canvas3d_deferred.inc:1206`), so
"3-cascade sun + 1 spot" exhausts it and further lights silently render unshadowed.
This doc: (E10) give the directional light **dedicated cascade storage** and grow the general
budget to **8 slots in a shadow atlas**; (E9) implement **omnidirectional (cube) point-light
shadows** on Metal/D3D11/GL/software; (E11/E12) make every budget observable
(`get_DroppedLightCount`, `get_ClusterOverflowCount`, configurable cluster capacity).

## 1. Current state (verified anchors)

- Budget: `VGFX3D_MAX_SHADOW_LIGHTS = 4`; fixed arrays `shadow_rts[4]`, `shadow_light_vps[4]`
  (`rt_canvas3d_internal.h:490,933-935`).
- CSM: `SetShadowCascades` clamps to 4, traps without `"shadow-csm"` capability
  (`rt_canvas3d.c:2483-2489`); cascades consume general slots (`render_pass.inc:99-183`);
  stable texel-snapped splits exist.
- Spot shadows: real, single perspective map (`rt_canvas3d_shadow.inc:306-339`, type==3 at :411).
- Selection: `canvas3d_select_shadow_lights_from_draws` picks ≤4 by priority
  (`rt_canvas3d_deferred.inc:1206`); overflow lights keep `shadow_index = -1` (backend.h:323) —
  silent.
- Filtering: 16-tap rotated Poisson PCF in all three GPU shader sources (metal.m:868-937,
  d3d11_shaders.inc:491-550, opengl_shaders.inc:386-448); software uses basic depth compare
  (`vgfx3d_backend_sw_shadow.inc`).
- Clustered: 16×9×24 froxels, `VGFX3D_MAX_LIGHTS = 64`, per-cluster index list truncates
  deterministically (`rt_canvas3d_clusters.c:268-283`), overflow counted internally
  (`internal.h:966` `cluster_overflow_total`) but not exposed. Forward path drops lights >16
  silently (`rt_canvas3d_lighting.c:91-121`).
- Stale doc comment says "up to 16" at `rt_light3d.c:15` — fix in passing.

## 2. Design

### E10 — Decouple cascades; 8-slot general atlas (session A)
- New dedicated arrays: `csm_rts[VGFX3D_MAX_CSM_CASCADES=4]` + `csm_vps[4]` — the primary
  directional light's cascades live here and **no longer consume general slots**.
- General budget: `VGFX3D_MAX_SHADOW_LIGHTS` 4 → **8**. Storage becomes a **shadow atlas**
  (single depth target, 4×2 tiles at the configured resolution per tile) to keep texture-unit
  pressure flat on GL 3.3 (binding 8 separate depth textures would exceed comfortable limits;
  one atlas + per-tile viewport scissor keeps every backend at 1 shadow sampler + 1 CSM sampler).
- Per-light shadow resolution: `Light3D.SetShadowResolution(i64)` (256–2048, tile-clamped).
- Selection priority (documented, deterministic): primary directional CSM always; then
  shadow-requesting lights scored by `intensity × screen-coverage estimate / distance²`,
  stable-sorted; point lights count as **6 tile consumers** (see E9) unless demoted to
  single-face mode by the selector under pressure.
- API: `Canvas3D.SetShadowBudget(i64)` (1–8, default 8; lets low tiers keep 4),
  `Canvas3D.get_ShadowSlotsUsed`, `Canvas3D.get_ShadowRequestsDropped` (per-frame).

### E9 — Omnidirectional point-light shadows (session B)
- Representation: **cube shadow** = 6 perspective faces (90° FOV) rendered into 6 atlas tiles;
  shader samples by dominant axis of the light-to-fragment vector → face select + 2D PCF
  (same 16-tap Poisson helper, shared code). This avoids `samplerCube` depth-compare
  divergence across GL 3.3/D3D11/Metal and reuses the existing 2D shadow sampler path —
  four-shader-source cost stays bounded.
- Budget shaping: full cube = 6 tiles. The selector may demote a point light to
  **hemisphere mode** (1 paraboloid-approx face, cheap, for small radius/low priority) —
  `Light3D.SetShadowMode(i64)` 0=auto,1=cube,2=hemisphere. Auto: cube if ≤1 point light
  requests shadows, else hemisphere.
- Culling per face: reuse existing per-shadow-pass frustum cull; skip faces whose frustum
  intersects no shadow-casting draws (typical savings: 2–3 faces).
- Software backend: same 6-face raster through `sw_shadow.inc` depth path (correctness
  reference; perf acceptable at 256–512 tiles on Performance tier).
- Bias: per-face slope bias reuses `SetShadowBias/SetShadowSlopeBias`; add
  `Light3D.SetShadowNearFar(f64,f64)` for point ranges (default 0.05–range).

### E11/E12 — Telemetry + config (session A)
- `Canvas3D.SetClusterLightBudget(i64)` — resizes per-cluster index capacity (8–64, default
  current constant); `Canvas3D.get_ClusterOverflowCount` exposes `cluster_overflow_total`.
- `Canvas3D.get_DroppedLightCount` — forward-path lights dropped this frame
  (`build_light_params` counts what it truncates, `rt_canvas3d_lighting.c:91-121`).
- Fix stale `rt_light3d.c:15` comment (16 → forward cap vs 64 clustered).

## 3. New API surface (runtime.def)

```text
Viper.Graphics3D.Canvas3D.SetShadowBudget(i64)          void(obj,i64)
Viper.Graphics3D.Canvas3D.get_ShadowSlotsUsed           i64(obj)
Viper.Graphics3D.Canvas3D.get_ShadowRequestsDropped     i64(obj)
Viper.Graphics3D.Canvas3D.SetClusterLightBudget(i64)    void(obj,i64)
Viper.Graphics3D.Canvas3D.get_ClusterOverflowCount      i64(obj)
Viper.Graphics3D.Canvas3D.get_DroppedLightCount         i64(obj)
Viper.Graphics3D.Light3D.SetShadowResolution(i64)       void(obj,i64)
Viper.Graphics3D.Light3D.SetShadowMode(i64)             void(obj,i64)
Viper.Graphics3D.Light3D.SetShadowNearFar(f64,f64)      void(obj,f64,f64)
```
`BackendSupports("shadow-point")` added; true on all four backends when E9 lands (capability
bit derived from the same vtable hooks — shadow_begin/draw/end already exist everywhere, so
the bit is unconditional once the face-render path ships).

## 4. Files

| File | Change |
|---|---|
| `src/runtime/graphics/3d/render/rt_canvas3d_internal.h` | CSM arrays, atlas layout, budget fields, counters |
| `src/runtime/graphics/3d/render/rt_canvas3d_shadow.inc` | atlas allocation, cube-face VP builders, hemisphere mode |
| `src/runtime/graphics/3d/render/rt_canvas3d_render_pass.inc` | cascade loop → csm arrays; face loops; per-face culling |
| `src/runtime/graphics/3d/render/rt_canvas3d_deferred.inc` | selector: scoring, cube 6-tile accounting, drop counter |
| `src/runtime/graphics/3d/render/rt_canvas3d_lighting.c` | forward drop counter; shader param plumbing (atlas UV rects) |
| `src/runtime/graphics/3d/render/rt_canvas3d_clusters.c` | configurable capacity, overflow getter |
| `src/runtime/graphics/3d/backend/vgfx3d_backend_metal.m` + `metal` shaders | atlas sampling, face-select shadow function |
| `src/runtime/graphics/3d/backend/vgfx3d_backend_d3d11.c` + `d3d11_shaders.inc` | same (HLSL) |
| `src/runtime/graphics/3d/backend/vgfx3d_backend_opengl.c` + `opengl_shaders.inc` | same (GLSL 330) |
| `src/runtime/graphics/3d/backend/vgfx3d_backend_sw*.inc` | SW face raster + face-select sampling |
| `src/runtime/graphics/3d/render/rt_light3d.c` | new setters; stale comment fix |
| `src/il/runtime/runtime.def`, `docs/viperlib/graphics/rendering3d.md` | surface + docs (shadow section rewrite) |

## 5. Tests

1. Unit (`test_rt_canvas3d.cpp`): budget accounting — CSM 3-cascade sun + 8 spot requests →
   `ShadowSlotsUsed == 8`, `ShadowRequestsDropped == 0` (cascades no longer consume);
   9th request → dropped counter increments; priority order stable across frames.
2. Point-shadow golden (software backend, deterministic): single point light in a box room
   with an occluder pillar → 1-frame render probe; shadow appears on all 6 walls; golden image
   hash per face-select region. Hemisphere mode golden for the demoted path.
3. Cube face culling: light in room corner → ≥2 faces skipped (assert face-render counter).
4. Forward drop telemetry: 20 lights, clustered off → `DroppedLightCount == 4`.
5. Cluster overflow: pack 40 lights into one froxel column with budget 8 →
   `ClusterOverflowCount > 0`, stable across two identical frames (determinism).
6. GPU parity spot-check (Metal, manual): same scene as (2) — face seams < 1 texel wobble,
   PCF softness comparable to spot shadows.
7. Existing shadow tests (`test_rt_canvas3d.cpp:4828-4840` CSM clamp) stay green.

## 6. Verification gate

`ctest -R canvas3d` + `-L graphics3d` green → golden probes committed → runtime completeness +
surface audits → full no-skip build → manual Metal visual pass on the L5-style test scene
(three colored point lights + moving occluder). ADR for surface additions. Perf note recorded
in doc: cube shadow cost @512² ≈ 6 shadow draws-lists — budgeted 1 cube OR 4 hemisphere lights
per zone at Balanced tier (22-world-systems lighting-zone manager enforces).
