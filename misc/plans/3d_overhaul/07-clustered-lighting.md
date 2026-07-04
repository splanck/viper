# Plan 07 — Clustered Forward+ Light Culling (CPU Froxel Binning)

> **Status (2026-07-03): IMPLEMENTED (with one documented scope change: `VGFX3D_MAX_LIGHTS` stays 64).**
>
> - **Binning core** (`render/rt_canvas3d_clusters.c/.h`): 16×9×24 froxel grid,
>   exponential Z slicing, influence radius from `intensity/(1+atten·d²) = 1/255`
>   (zero attenuation ⇒ unbounded sentinel), conservative projected-AABB XY rects
>   (any behind-eye corner ⇒ full-row coverage), u16 prefix-sum offsets + index
>   list capped at `VGFX3D_MAX_CLUSTER_LIGHT_INDICES` (8192) with order-stable
>   per-cluster truncation and an overflow counter (`c->cluster_overflow_total`).
>   Tables live in a revision-keyed ring of 4 on the canvas, invalidated at frame
>   Begin (binning is camera-dependent); draws carry `cmd->cluster_table` next to
>   the plan-04 `lights_revision` stamp. `build_light_params` now emits
>   directional/ambient lights first (the "global prefix" the shader loops flatly)
>   — order-independent sum, so flat-path output is unchanged.
> - **Shaders** (all three GPU backends; MSL verified via the offline harness,
>   GLSL/HLSL code-complete under the Linux/Windows waivers): light-loop bodies are
>   untouched; a prologue maps the loop counter through the per-cluster index list
>   (`i = k < globalCount ? k : indices[clusterStart + k - globalCount]`), gated by
>   a `clusterGlobalCount >= 0` uniform so the flat path costs one comparison.
>   Metal: `ClusterTable` at fragment buffer 3 (4-deep `MTLBuffer` ring keyed by
>   revision, keys dropped each `begin_frame`; zeroed dummy bound when off).
>   GL: two std140 UBOs at bindings 2/3 (u16 pairs packed in `uvec4` lanes — the
>   little-endian u16 array uploads as-is; indices UBO = 16384 B = the GL 3.3
>   min-spec block size). D3D11: cbuffers b6/b7, same uint4 packing; per-frame
>   re-upload is inherited from the existing DISCARD gate.
> - **Control**: clustering defaults ON for GPU backends at canvas creation
>   (identified by the `present_postfx` hook; software keeps the flat 16-light
>   path). New `Canvas3D.ClusteredLighting` property (get/set) *replaces* the old
>   `SetClusteredLighting` method — the qualified-surface audit forbids a method
>   that duplicates a writable property; `VIPER_3D_CLUSTERS=0` env kill switch.
> - **Scope change:** the planned 64 → 256 `VGFX3D_MAX_LIGHTS` raise is dropped.
>   The GL backend passes lights as loose uniform arrays (~13 scalars/vec3s per
>   light); 256 lights exceed the GL 3.3 min-spec 1024 fragment uniform components
>   by an order of magnitude, and moving the light array into a UBO is a bigger
>   refactor than this plan warrants. 64 clustered lights already covers the
>   many-light demos; revisit with a PerLights UBO port if a real scene needs more.
> - **Tests** (`test_rt_canvas3d`, 253/253 green): slice/radius math; binning
>   conservativeness (6-light fixture swept over 12 log-spaced depths × 7×7 screen
>   points — every light contributing ≥1/255 at a sample must appear in that
>   sample's cluster list); overflow truncation (3 unbounded lights vs the 8192
>   cap: exact overflow count, monotone offsets, deterministic rebuild);
>   revision-keyed ring + backend gating; globals-first light ordering.
> - CPU cost: binning is one pass over lights × touched froxels with u16 writes —
>   worst case (64 full-screen lights) is bounded by the 8192-entry cap; typical
>   scenes touch a few hundred entries. No per-frame cost when lights are
>   unchanged and no draw stamps a new revision (tables are revision-cached).

## 1. Objective & scope

Lighting cost is O(pixels × lights): the fragment shader loops over up to 64 lights for every pixel (`uLightCount` brute force). Many-light scenes — the "Unity-level" town at night, particle-lit caves — are either capped or slow. Add clustered forward+ lighting: bin lights into a view-space froxel grid on the CPU each frame, upload per-cluster light index lists, and have the shader loop only its cluster's lights. CPU binning (not GPU compute) keeps determinism and works within the existing backend model.

**Depends on plan 04** (per-frame constant upload — cluster tables ride the same per-frame block/hook).

**In scope:** froxel grid + CPU binning; cluster table upload via the plan-04 `set_frame_lights` hook; shader cluster lookup (GL/D3D11/Metal); raising `VGFX3D_MAX_LIGHTS`; capability wiring (bit already exists). **Out of scope:** GPU-compute binning, shadowed-light count changes (plan 06 owns shadow budgets), software backend (keeps the flat loop — it's CPU-bound elsewhere and typically renders few lights).

**Zero external dependencies:** binning math, cluster data layout, and shaders are all from-scratch in-tree code; no lighting/culling libraries.

## 2. Current state (verified anchors)

- Shader loop: per-pixel `for (i = 0; i < uLightCount; ++i)` over `uLight*[VGFX3D_MAX_LIGHTS]` arrays (`_opengl_shaders.inc:436-466`, light types 0=dir/1=point/2=ambient/3=spot); same structure in HLSL (`PerLights` cbuffer, `_d3d11_shaders.inc:86-97`) and MSL (`PerLights`, `metal.m`).
- Limits: `VGFX3D_FORWARD_LIGHT_LIMIT 16`, `VGFX3D_MAX_LIGHTS 64` (`rt_canvas3d_internal.h:486-487`); canvas flattens scene lights per frame in `rt_canvas3d_lighting.c` (`canvas3d_active_light_limit`), storage `internal.h:826-831`.
- Light params carry position/direction/attenuation/cone (`vgfx3d_light_params_t`, `backend.h:244-259`) — enough to compute bounding spheres (point: `radius ≈ sqrt(intensity/(atten·ε)) `; spot: cone-bounding sphere).
- **Capability scaffolding already exists:** `RT_CANVAS3D_BACKEND_CAP_CLUSTERED_LIGHTING` + the `"clustered-lighting"` capability name (`_overlay.c:465-500,511`), with a consumer at `rt_canvas3d.c:2210`. Audit what that consumer currently gates (per the 3dnextlevel2 review, the existing "clustered" path submits a bounded payload without real culling) — this plan replaces/fulfills that promise; do not build a second parallel mechanism.
- Camera params (`vgfx3d_camera_params_t`, `backend.h:218-232`) carry view/projection — the binning needs near/far + inverse projection, available from the canvas camera state.
- Plan 04 adds `set_frame_lights(ctx, lights, count, ambient, shadow_vps, bias)` — extend rather than add another hook.

## 3. Design

### 3.1 Grid + binning (canvas layer, new `render/rt_canvas3d_clusters.c`)

- Grid: 16×9×24 froxels (X×Y×Z-slices), Z exponential slicing (`z = near·(far/near)^(slice/24)`) — standard, cache the per-slice bounds per frame.
- Binning: for each active light (post-flatten), compute a view-space bounding sphere; conservatively rasterize it into the grid (slice-range from z-extent, per-slice XY rect from projected extent). Output:
  - `uint16 cluster_light_offsets[16*9*24 + 1]` (prefix sums)
  - `uint16 cluster_light_indices[total]` (capped: `VGFX3D_MAX_CLUSTER_LIGHT_INDICES 8192`; overflow ⇒ per-cluster truncation with a diagnostics counter, never UB)
- Directional + ambient lights bypass binning (applied globally; they're cheap and unbounded) — the cluster lists carry only point/spot indices. This matches the shader split below.
- Determinism: lights binned in their flattened array order; ties and truncation are order-stable.
- CPU cost target: 64 lights × ~hundreds of clusters touched ⇒ trivially < 0.1 ms; measure and record.

### 3.2 Upload + shader lookup

- Extend the plan-04 hook payload: `set_frame_lights(..., const vgfx3d_cluster_table_t *clusters)` where the struct carries grid dims, z-params, and the two arrays. Backends without cluster support (SW; or `clusters == NULL` when disabled) keep the flat loop — the capability bit reports which path is live.
- GPU representation: GL = two UBOs (or one UBO + texture buffer if index count exceeds UBO limits — 8192×2 bytes fits comfortably in the 16 KB min UBO size as u16 packed pairs; use `uint` packing, std140-safe); D3D11 = `StructuredBuffer<uint>` SRVs (t-slots after the shadow textures); Metal = two `MTLBuffer`s.
- Fragment shader: compute cluster index from `gl_FragCoord` + linear depth → slice; loop `for (k = offsets[c]; k < offsets[c+1]; ++k) { i = indices[k]; ...existing point/spot math... }`; directional/ambient handled in a short separate loop over a small `uGlobalLightCount` prefix (canvas sorts directional/ambient to the front of the flattened array — one-line change in `rt_canvas3d_lighting.c`).
- Raise `VGFX3D_MAX_LIGHTS` 64 → 256 once clustered is on (PerLights buffer grows; per-frame upload only, thanks to plan 04). Flat-loop backends clamp to the first 64 (documented; sort guarantees directional/ambient survive the clamp).

### 3.3 Control & rollout

- Canvas toggle `rt_canvas3d_set_clustered_lighting(obj, enabled)` default **on** for GPU backends after parity proof; env escape hatch `VIPER_3D_CLUSTERS=0` for bisection. `BackendSupports("clustered-lighting")` now reports the real thing (reconcile the existing consumer at `rt_canvas3d.c:2210`).
- Parity mode for tests: a debug canvas flag renders the same frame flat vs clustered for image compare.

## 4. Implementation steps

1. Audit + document the existing `CLUSTERED_LIGHTING` cap consumer (`rt_canvas3d.c:2210`) — decide reuse/replace; write the findings into this plan file.
2. `rt_canvas3d_clusters.c`: grid math + binning + unit tests (sphere spans expected slices; truncation stable; directional excluded) — pure CPU, no backend work.
3. Hook payload extension (plan 04's struct) + canvas wiring + light-array sort (directional/ambient first) + flat-path regression proof (sort must not change output).
4. Metal shader + buffers + parity test (flat vs clustered image identical on a 32-light probe scene).
5. GLSL + HLSL implementations (code-complete; Linux/Windows waivers).
6. `VGFX3D_MAX_LIGHTS` raise + 200-light probe (parity + frame-time recorded) + docs + capability reconciliation.

## 5. Public API changes

- `Viper.Graphics3D.Canvas3D`: `RT_PROP("ClusteredLighting","i1", get/set)`; existing `"clustered-lighting"` `BackendSupports` string now truthful. `Viper.Game3D.Quality` profiles unchanged (clustering is a correctness-neutral perf feature, always-on where supported).
- `Light3D`/`World3D` light APIs unchanged; the 64→256 cap raise documented in `rendering3d.md`.
- New file `rt_canvas3d_clusters.c` → `source_health_baseline.tsv` bump. No ADR (internal mechanism; public surface is one prop).

## 6. Tests

- Unit: froxel slice math (near/far edge cases), sphere-to-grid conservative coverage (a light fully inside cluster C appears in C and neighbors it overlaps — never missing), prefix-sum integrity, overflow truncation determinism.
- Parity golden: 32-light scene flat vs clustered byte-identical (Metal); 200-light scene clustered matches a flat render with the cap raised (SW flat as CPU reference where light count ≤ SW clamp).
- Perf recorded on openworld_slice + a synthetic 200-point-light scene (frame ms before/after).
- VM/native determinism: binning is canvas-CPU code shared by both — covered by existing e2e equivalence lanes once a light-heavy example exists; add one to `examples/3d/`.

## 7. Verification gates

Full build + ctest + `-L slow`; parity goldens on Metal; SW unchanged; GL/D3D11 waivers; diagnostics counter (`cluster overflow count`) exposed via `Diagnostics3D` and asserted zero in the probe scenes.

## 8. Risks & constraints

- **Conservative-vs-tight binning:** err conservative (over-inclusion costs a few shader iterations; under-inclusion = light pops). The parity golden is the guard.
- **UBO size limits (GL):** 8192 u16 indices = 16 KB packed — exactly at the minimum guaranteed UBO size; pack as 4096 uints and split across two blocks if a driver rejects it (fallback already designed).
- **Interaction with plan 06:** shadowed lights are still few; cluster lists carry the *lighting* set — shadow-map selection logic untouched.
- **Zero external dependencies:** no culling/BVH libraries; the binning is ~200 lines of from-scratch math.
