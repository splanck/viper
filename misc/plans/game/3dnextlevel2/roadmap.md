# Roadmap, milestones, and testing

Phases are ordered **most-foundational-first**, where "foundational" means how
many other systems depend on an item and how close it sits to the engine core
(see `README.md` §3). The order is a buildable dependency chain: you cannot build
streaming before the job system, or large-world rendering before a coordinate
foundation.

Two phases precede the new work:

- **Phase C — Carryover.** Close out the unfinished tail of `3Dnextlevel`
  (verified 2026-05-29; see `carryover.md`). Several items are hard prerequisites
  for the new work (cross-platform lane, performance lane, render-target
  finalization, skinned-character proof).
- **Phase 0 — Foundations spike.** Decide the determinism/threading contract,
  spatial-index choice, floating-origin strategy, and stand up the performance
  harness and big-world fixtures before broad implementation.

Software backend is the canonical correctness baseline throughout; GPU backends
get capability-gated parity plus secondary smoke checks. Every new system is
opt-in and must not regress the existing bounded-scene path.

---

## Phase C — Carry over and close out 3D Next Level

Goal: finish the unfinished/​waived items from `3Dnextlevel` that this plan
depends on, before extending the engine. Full enumeration with prior IDs and
verified status is in `carryover.md`; the prerequisites are summarized here.

- **CO-1 Cross-platform lane (prerequisite for everything).** Build and run
  `ctest -L graphics3d` green on macOS, Windows, and Linux via the platform build
  scripts. The previous plan proved macOS only.
- **CO-11 Performance lane (prerequisite for Phase 0 harness).** A Release /
  reference-hardware software-backend FPS lane plus a GPU interactive-framerate
  smoke lane (closes waived AC-004 / W-002 / W-005).
- **CO-2 Managed Zia callback trampoline (VM).** Let interpreted Zia pass
  closures to native callback loops so `run(update)`, `onCollision`, `onUpdate`
  sugar work (closes W-001 and the dependent `API-WORLD-*`/`API-ENT-*` rows). May
  be delegated to a VM plan; tracked here as a dependency.
- **CO-4 Render-target finalization contract.** Define/implement
  `FinalizeFrame`/`ScreenshotFinal` for render-target-backed frames, or
  capability-gate it (R-FRAME-016/018). Needed by streaming/portals/reflections.
- **CO-3 Lifetime/diagnostics hardening.** Destroyed-handle, double-despawn,
  missing-asset, and capability-fallback diagnostics; imported-group despawn and
  cache-clear leak probes (GATE-006 / D-009 / R-LIFE-*).
- **CO-9 Committed fixtures + skinned-character proof.** Commit small
  redistributable GLB / glTF-with-deps / audio / skinned-character fixtures and
  prove skinned play/crossfade in a sample (closes the review's
  "proven-vs-theoretical" gap; W-003).
- **CO-5 Implicit fallback lighting decision (D-007)**, **CO-6 optional getters**
  (incl. `Light3D.CastsShadows`, synergy with Phase 7), **CO-7 Metal robustness
  probe**, **CO-8 docs cleanups** (deprecate `SetOcclusionCulling` alias —
  reconciled in Phase 6), **CO-10 determinism/resize probe completeness**, and
  **CO-12 remaining Phase-1 exit partials**.

Exit:

- `ctest -L graphics3d` green on macOS, Windows, and Linux (CO-1).
- A recorded software-backend FPS baseline on named hardware and a GPU smoke
  lane (CO-11).
- Render-target finalization behavior defined and tested or capability-gated
  (CO-4).
- A committed skinned character renders, plays, and crossfades in a sample
  ctest (CO-9).
- `carryover.md` has no `todo`/`partial` rows except those explicitly re-waived
  with a re-open condition.
- `check_runtime_completeness.sh` green.

---

## Phase 0 — Foundations spike: determinism, threading, spatial index, perf harness

Goal: pin the cross-cutting decisions and tooling that every later phase relies
on, before broad implementation.

- **Determinism-under-threads contract.** Confirm the recommended policy:
  simulation (physics step, animation sampling, gameplay) is deterministically
  scheduled; worker threads handle only load/decode/cull/bake/transcode, merged
  back in a deterministic order. Prove `runFrames` keeps VM/native parity with
  workers enabled. Raise an ADR if any IL/VM change is implied.
- **Job-system shape.** Choose thread-pool + work-stealing vs. per-domain queues;
  must sit on `rt_platform.h` / `PlatformCapabilities.hpp` (no raw
  `_WIN32`/`__APPLE__`/`__linux__`).
- **Spatial-index choice.** Loose octree vs. BVH vs. grid; decide whether one
  structure serves cull + scene query + physics broadphase or physics keeps a
  sibling.
- **Floating-origin strategy.** Periodic active-scene rebase vs. per-cell local
  origins + camera-relative upload; define interaction with existing `double`
  node transforms and the `float` GPU path.
- **Performance harness + big-world fixtures.** Extend the Phase-C perf lane into
  a reusable harness (frame-time, draw-call, body-count, memory counters) and
  commit deterministic large-scene fixtures (e.g. a 10k-node grid, a >4 km²
  terrain stand-in) used by every scale test.
- **Capability/flag scheme.** Define the opt-in flags and capability strings for
  every new system so default bounded-scene behavior is unchanged.

Exit:

- A `3dnext2_surface_probe` (or equivalent) runs a worker-backed job and a
  deterministic two-run simulation under `runFrames` with identical state.
- All Phase-0 decisions in `progress/02-decisions.md` are closed or waived.
- Perf harness reports baseline numbers for the committed fixtures on all three
  platforms.
- No new runtime API traps with flags off; `check_runtime_completeness.sh` green.

---

## Phase 1 — Concurrency runtime (job/worker system) — the keystone

Goal: a deterministic-by-policy job system so later phases can offload work
without breaking simulation reproducibility or cross-platform parity.

- Implement a C runtime worker pool + job/task API on `rt_platform.h` threads,
  with thread-safe queues, futures/handles, and a deterministic ordered-merge
  primitive for results fed back into simulation.
- Make the 3D allocator/GC interaction and handle-validation paths the workers
  touch thread-safe (audit `rt_obj_new_i64`, pool allocators, string interning).
- Provide a "main-thread upload/commit" queue primitive (used by Phase 4).
- Expose minimal control to game code (`World3D` worker-count / enable getters);
  keep the system internal-first.

Exit:

- Parallel map over N tasks returns deterministic aggregated results; race
  detectors / stress runs are clean.
- `runFrames` simulation state is identical with the pool enabled vs. disabled.
- Builds + `-L graphics3d` green on all three platforms; no raw platform macros
  outside adapters.

Cannot precede: nothing depends on a later phase. **Everything async depends on
this.**

---

## Phase 2 — Large-coordinate foundation (floating origin)

Goal: travel kilometres from origin without rendering precision collapse or
physics jitter.

- Implement the chosen floating-origin strategy (Phase 0): origin rebase and/or
  per-cell local origins with camera-relative matrices on upload.
- Promote cull bounds (currently `float` `aabb_min/max` in
  `rt_scene3d_internal.h`) and physics body state to the precision policy decided
  in Phase 0; keep GPU upload `float` but camera-relative.
- Add a rebase hook that updates scene nodes, physics bodies, audio sources,
  particles, and active queries atomically between frames.
- Capability-gate: bounded scenes with floating origin off are byte-for-byte
  unchanged.

Exit:

- A test scene at 50 km from origin renders without visible jitter/z-fighting and
  matches the same scene rendered near origin (within tolerance).
- Physics and raycasts are stable far from origin.
- Determinism preserved across a rebase under `runFrames`.

Cannot precede: Phase 5 (streaming places cells far from origin) depends on this.

---

## Phase 3 — Spatial acceleration substrate (octree/BVH)

Goal: per-frame cost scales with *visible* content, not *total* content, and one
structure serves cull + queries + broadphase.

- Implement the chosen index over the scene graph; maintain it incrementally as
  nodes move (dirty/refit), respecting double-precision transforms.
- Replace the full-tree `draw_node` walk (`rt_scene3d.c:2100`) with an index
  query that yields only candidate-visible nodes; keep the old path behind the
  flag for parity testing.
- Route `Scene3D`/physics spatial queries (raycast, overlap, sweep) and the
  physics broadphase (currently single-axis SAP, `rt_physics3d.c:3662`) through
  the shared index where Phase 0 chose to unify them.
- Optionally parallelize index queries via Phase 1 (deterministic merge).

Exit:

- On the 10k-node fixture, frame cull cost scales with visible count; recorded
  speedup vs. the flat walk.
- Cull results match the flat-walk reference exactly (no popping/missing nodes).
- Query/broadphase results unchanged within tolerance; determinism preserved.

Cannot precede: Phases 5, 6, 8, 9 all build on the index.

---

## Phase 4 — Async asset & GPU-upload streaming

Goal: load content without hitching the main thread.

- Move file read + glTF/FBX/image decode onto Phase-1 workers; keep GPU resource
  creation/upload on the main-thread commit queue.
- Add `Assets3D.LoadModelAsync` / handle-based residency; convert `Preload` to a
  real background warm.
- Add per-resource residency + reference counting and an eviction policy
  (LRU/distance) so streamed assets unload.
- Add streaming hooks for textures/meshes (mip/LOD residency) consumed by
  Phases 5/6/11.

Exit:

- Loading a model mid-frame does not stall the main thread beyond a bounded
  upload budget; recorded hitch delta vs. the blocking path.
- Async and blocking loads produce identical resources.
- Residency counters return to baseline after load/unload churn (no leak).

Cannot precede: Phase 5 streams through this path.

---

## Phase 5 — World partition & terrain streaming

Goal: worlds larger than RAM and larger than one ≤4 km² heightmap.

- Implement streamable terrain tiles (lift the single-heightmap 4096² cap at
  `rt_terrain3d.c:216` into a tile grid) with seam stitching across tile LODs.
- Implement scene cells: load/unload scene-node subtrees and their physics/nav
  around the player using Phases 1/3/4.
- Define the streamed scene container (extend VSCN or a tiled side-format; no new
  top-level format) and an authoring/bake hook ViperIDE can target later.
- Wire terrain collision to the heightfield collider per active tile.

Exit:

- A >4 km² world streams tiles/cells in and out as the player crosses boundaries
  with no hitch beyond budget and no seams.
- Memory stays bounded while traversing a world larger than the resident set.
- Determinism preserved for a scripted traversal under `runFrames`.

Cannot precede: this is the defining open-world milestone; Phase 12 exercises it.

---

## Phase 6 — Visibility scaling (occlusion + auto-LOD/HLOD)

Goal: keep dense scenes within frame budget by drawing less.

- Implement occlusion culling (software rasterized depth / portal-PVS for
  interiors) over the Phase-3 index; capability-gate per backend.
- Implement automatic mesh LOD selection (extend the existing per-node discrete
  LOD at `rt_scene3d.c:2012` with auto-generated/screen-error LOD) and
  HLOD/impostor proxies for distant clusters/vegetation.
- Reconcile the legacy `SetOcclusionCulling` alias (carryover CO-8): make it
  select real occlusion culling, keeping the frustum-only behavior available.

Exit:

- On a dense city/forest fixture, occluded draws are skipped; recorded
  draw-call/fill reduction.
- LOD/impostor swaps are stable (no popping beyond a tolerance) and match a
  full-detail reference within tolerance.

Cannot precede: Phase 12 needs this for a populated world to hit budget.

---

## Phase 7 — Lighting scaling (clustered/forward+ and cascaded shadows)

Goal: many lights and large outdoor shadows beyond the forward 16-light /
2-shadow cap (`rt_canvas3d_internal.h:308`).

- Implement a clustered/forward+ light-culling path (keep existing forward
  shaders; cull lights per cluster) raising the effective light count; software
  backend gets a correct reference path.
- Implement cascaded shadow maps (CSM) for the primary directional light and lift
  the shadow-caster cap where backends allow; use `Light3D.CastsShadows` from
  carryover CO-6.
- Capability-gate: backends/platforms without the path fall back to the current
  16-light forward behavior.

Exit:

- A scene with >16 active lights renders correctly with per-cluster culling;
  recorded cost vs. naive forward.
- CSM gives stable large-range directional shadows without acne/peter-panning
  beyond tolerance.

Cannot precede: Phase 12 lighting fidelity.

---

## Phase 8 — Physics depth (manifolds, solver, GJK/EPA, joints)

Goal: stable stacking, scalable mesh collision, real convex shapes, and the
joints the docs already promise.

- Implement multi-point contact manifolds (replace the single contact point;
  `physics3d.md:145`) and an iterated, warm-started sequential-impulse solver so
  bodies stack/rest stably.
- Drive mesh narrow-phase through the existing per-mesh BVH (currently raycast-
  only) and/or the Phase-3 index; remove the brute-force O(triangles) path.
- Implement GJK/EPA convex-vs-convex (the current "convex hull" is triangle soup;
  `rt_collider3d.c:569`).
- Implement the missing joints the header advertises — hinge and rope — plus a
  6DOF/configurable joint; close the doc overclaim (`rt_physics3d.h:80`).
- Add solver islands/sleeping at scale; integrate broadphase with Phase 3.

Exit:

- A stack of boxes rests stably; a pile settles without sinking/jitter.
- Hundreds-to-thousands of dynamic bodies hit the recorded target on the perf
  fixture (improvement vs. current ~hundreds ceiling).
- Hinge/rope/6DOF joints behave correctly; convex-vs-convex is true convex.
- Determinism preserved under `runFrames`.

Cannot precede: independent of streaming; may be pulled earlier for a
physics-first game (see decision SEQ).

---

## Phase 9 — Navigation & AI depth (autogen, tiled, avoidance, off-mesh)

Goal: many NPCs navigating a large, changing world.

- Implement navmesh auto-generation (voxelize scene geometry → walkable regions →
  contour → mesh; a from-scratch Recast-style baker), replacing bake-from-one-
  mesh only.
- Implement tiled/streamable navmesh aligned to Phase-5 cells, with dynamic
  obstacle carving (doors, placed objects) and runtime tile rebuild.
- Implement off-mesh links (jumps/ladders/drop-downs) and apply `agent_radius`
  corridor erosion (currently stored but ignored, `rt_navmesh3d.c:80`).
- Implement local avoidance (ORCA/RVO-style) and pathfinding acceleration
  (spatialized find-tri, path caching, time-sliced/hierarchical A*), parallelized
  via Phase 1.

Exit:

- A navmesh auto-bakes from arbitrary scene geometry and rebuilds a tile at
  runtime when an obstacle changes.
- Hundreds of agents path and avoid each other within the recorded frame budget;
  wide agents respect corridors; agents traverse off-mesh links.

Cannot precede: independent of rendering; may be pulled earlier for an NPC-first
game (see decision SEQ).

---

## Phase 10 — Animation depth (IK, additive, blend trees, retargeting)

Goal: characters that plant on terrain, aim, and share animations.

- Implement IK: two-bone foot placement, look-at/aim, and a general FABRIK chain
  solver, integrated with the bone palette before skinning.
- Implement true additive layers (reference-pose subtraction) — the current
  "additive" layers are replace-blends (`rt_animcontroller3d.c:823`).
- Implement parametric blend trees / 1D-2D blendspaces over the existing
  `AnimBlend3D` mixer.
- Implement cross-skeleton animation retargeting (humanoid bone mapping).
- Add animation LOD (reduced update rate / bone count for distant characters),
  using Phase 3 distance.

Exit:

- A character's feet plant on a slope/stairs via IK and head/eyes track a target.
- Additive recoil/breath layers compose correctly over a base clip.
- One clip retargets onto a different-proportioned skeleton.
- Distant characters use reduced-cost animation without visible artifacts.

Cannot precede: Phase 12 character fidelity.

---

## Phase 11 — Asset pipeline depth (compression, KTX2/Basis, camera/multi-scene)

Goal: VRAM/RAM and import coverage for a large, content-rich world.

- Implement GPU texture compression upload (BC/DXT on D3D11/desktop GL, ASTC/ETC2
  where supported) with a software-decode reference path; capability-gate per
  backend.
- Implement KTX2 + Basis-universal transcode (from scratch) and streaming mip
  residency via Phase 4.
- Add glTF camera import and multi-scene selection (currently neither;
  `rt_gltf.c`), and Draco/meshopt decode (currently unsupported).
- Keep OBJ `.mtl` handling on the backlog (currently ignored,
  `rt_mesh3d.c:2165`).

Exit:

- A compressed-texture model loads and renders correctly on each capable backend
  with the recorded VRAM reduction; software path matches within tolerance.
- glTF cameras and a selected secondary scene import; a Draco/meshopt asset
  loads.

Cannot precede: Phase 12 content scale.

---

## Phase 12 — Open-world vertical slice, tooling hooks, docs, and perf baselines

Goal: prove the whole stack end-to-end in one playable sample and record the
open-world baselines (mirrors `3Dnextlevel` Phase 7 for this plan's scope).

- Build `examples/3d/openworld_slice/`: a streamed >4 km² world with terrain
  tiles, scene cells, a rigged/animated character with IK, several avoiding NPCs
  on an auto-baked navmesh, occlusion + LOD, clustered lighting + CSM, physics
  interactions, compressed-texture assets, and async streaming — all under
  `runFixed` gameplay and `runFramesOnly` deterministic replay.
- Expose the runtime hooks the ViperIDE level editor will consume (cell/partition
  authoring, residency inspection, navmesh bake, perf counters). Build the editor
  itself under the ViperIDE roadmap, not here.
- Add docs under `docs/viperlib/graphics/` and `docs/` for every new system, each
  with a ctest-compiled snippet; update `graphics3d-architecture.md`.
- Record open-world perf baselines (frame time, streamed memory, draw calls,
  body/agent counts) on named hardware across all three platforms.

Exit:

- The slice streams, renders, simulates, and replays deterministically; software
  visual baseline + GPU smoke pass.
- The skinned-character / asset pipeline is proven *at game scale* (closes the
  review's proven-vs-theoretical gap).
- All new runtime functions, API areas, samples, and docs snippets have ctest
  coverage or a tracked waiver.

---

## Testing strategy

| Area | How |
|---|---|
| Concurrency/determinism | Job stress + race runs; `runFrames` parity with pool on/off; VM/native parity |
| Floating origin | Far-from-origin vs. near-origin render/physics within tolerance; rebase determinism |
| Spatial index | Cull/query results match flat-walk reference exactly; recorded scaling on 10k-node fixture |
| Streaming | Hitch-budget assertions; resource-identity equality (async == blocking); bounded-memory traversal; no-leak residency |
| Visibility | Occluded-draw skip counts; LOD/impostor stability vs. full-detail reference |
| Lighting | >16-light correctness; CSM stability; capability fallback to forward |
| Physics | Stacking/rest stability; manifold correctness; GJK vs. analytic; joint behavior; perf body-count fixture |
| Navigation | Autogen vs. hand-baked equivalence; tile rebuild; avoidance interpenetration bounds; agent-count perf |
| Animation | IK target error bounds; additive vs. replace correctness; retarget visual check; anim-LOD parity |
| Asset pipeline | Compressed vs. raw within tolerance per backend; KTX2/Draco/meshopt load; glTF camera/scene import |
| Cross-platform | Build + `-L graphics3d` green on macOS/Windows/Linux every phase (carryover CO-1) |
| Performance | Before/after numbers on committed fixtures from the Phase-0 harness; recorded per phase |
| Software baseline | New visual features have a correct software path; GPU backends capability-gated + smoke |

Register tests following `src/tests/CMakeLists.txt` patterns with
`requires_display` labels where a real window is needed. Software-backend
correctness should not require a display.

### Required ctest inventory (per phase)

| Phase | Required tests |
|---|---|
| C (carryover) | Cross-platform green; perf baseline; render-target finalization; skinned-character sample; lifetime/diagnostics probes |
| 0 | Worker-job + deterministic-replay probe; perf-harness fixtures |
| 1 | Parallel-map determinism; race/stress; pool on/off parity |
| 2 | Far-origin render/physics tolerance; rebase determinism |
| 3 | Cull/query equality vs. flat walk; scaling benchmark |
| 4 | Hitch-budget; async==blocking resource equality; residency no-leak |
| 5 | Tile/cell stream in/out; seam check; bounded-memory traversal; determinism |
| 6 | Occlusion skip counts; LOD/impostor stability |
| 7 | >16-light correctness; CSM stability; fallback |
| 8 | Stacking/rest; manifold; GJK; hinge/rope/6DOF; body-count perf |
| 9 | Autogen equivalence; tile rebuild; avoidance bounds; agent-count perf |
| 10 | IK error bounds; additive correctness; retarget; anim-LOD |
| 11 | Compressed-texture tolerance per backend; KTX2/Draco/meshopt; glTF camera/scene |
| 12 | Slice smoke; deterministic replay; software visual baseline; perf baselines |

Coverage gaps are allowed only with a named waiver in `progress/06-waivers.md`.

## Acceptance criteria (measurable)

1. **Determinism preserved.** Fixed-step `runFrames` yields identical simulation
   state with the job pool enabled and disabled, and across VM/native.
2. **Far-from-origin stability.** A scene 50 km from origin renders and simulates
   without jitter/z-fighting beyond tolerance.
3. **Visible-scaling cull.** On the 10k-node fixture, frame cull cost scales with
   *visible* node count; recorded speedup over the flat walk.
4. **No streaming hitch.** Streaming content in/out stays within a bounded
   per-frame upload budget; recorded hitch delta vs. blocking load.
5. **Large world.** A traversal of a >4 km² streamed world keeps memory bounded
   and shows no seams.
6. **Overdraw reduction.** Occlusion + LOD cut draw calls/fill on the dense
   fixture by a recorded factor with no missing geometry.
7. **Many lights.** >16 active lights render correctly via clustered culling;
   CSM gives stable directional shadows.
8. **Stable physics at scale.** Boxes stack and piles rest; the body-count target
   is met on the perf fixture; hinge/rope/6DOF and true convex work.
9. **NPCs at scale.** A navmesh auto-bakes and rebuilds tiles at runtime; the
   agent-count target paths and avoids within budget.
10. **Characters on terrain.** IK foot-planting and look-at work; additive layers,
    blend trees, and retargeting work.
11. **Asset scale.** Compressed textures load per capable backend with recorded
    VRAM reduction; glTF camera/multi-scene/Draco/meshopt import.
12. **Vertical slice.** `examples/3d/openworld_slice/` demonstrates streaming,
    visibility, lighting, physics, NPCs, animated characters, compressed assets,
    and deterministic replay in one playable sample.
13. **Cross-platform + completeness.** Every phase is green on macOS/Windows/Linux
    with `check_runtime_completeness.sh` passing and docs+ctest for each new API.
14. **No regression.** With all flags off, an existing bounded-scene game behaves
    byte-for-byte as before.

## Risks and mitigations

- **Determinism vs. threads.** Mitigation: simulation stays deterministically
  scheduled; workers do throughput-only work merged in fixed order; `runFrames`
  parity gate (Phase 0).
- **Scope/size.** Several phases are real subsystems, not glue. Mitigation:
  strict opt-in flags, software-first correctness, before/after perf gates, and a
  phase order that ships a "small streamed world" at Phase 5 before raising the
  ceiling.
- **Cross-platform drift.** Mitigation: CO-1 lane first; everything on
  `rt_platform.h`; no raw platform macros outside adapters.
- **No-dependency constraint on hard algorithms** (navmesh voxelizer, Basis
  transcode, BC encoder). Mitigation: from-scratch, software-reference-first,
  capability-gated; descope to decode-only where encode is out of budget.
- **Backend divergence** (occlusion, clustered lighting, compressed textures).
  Mitigation: software is the baseline; GPU paths capability-gated + smoke-tested.
- **Floating origin vs. existing float GPU path.** Mitigation: camera-relative
  upload; bounded-scene path unchanged with the flag off.
- **VM callback trampoline (CO-2) is cross-cutting.** Mitigation: track as a
  dependency; event-buffer polling remains the authoritative fallback (as in the
  first plan).
- **Sequencing.** Physics-/NPC-first teams may pull Phase 8/9 earlier; only the
  dependency edges noted per phase are hard (decision SEQ in
  `progress/02-decisions.md`).
