# Master progress

Tracks global readiness for the 3D scale tier. Detailed work items live in the
sibling files. All rows start `todo`; this is a fresh plan.

## Overall gates

| ID | Gate | Source | Status | Proof / link | Notes |
|---|---|---|---|---|---|
| GATE-001 | Carryover (Phase C) closed before broad new work | `carryover.md`, `roadmap.md` Phase C | partial | Local macOS CO-1/CO-11 evidence is green; CO-2 is re-waived; CO-3..CO-12 local-closeable rows are done | Windows/Linux CO-1/CO-11 host evidence remains |
| GATE-002 | Determinism preserved: `runFrames` parity with job pool on/off, VM/native | `runtime-changes.md` §1, `roadmap.md` Phase 0/1 | partial | `g3d_3dnext2_surface_probe`; `test_rt_game3d` worker-count replay parity | Core Principle #5; VM callback-loop sugar remains W2-001 |
| GATE-003 | Every phase green on macOS/Windows/Linux (`-L graphics3d`) | `roadmap.md` Testing | todo |  | Core Principle #7; CO-1 first |
| GATE-004 | Performance harness records before/after for every scale feature | `roadmap.md` Phase 0 | partial | Release macOS open-world perf baseline; `g3d_openworld_slice_perf_harness`; `g3d_3dnext2_surface_probe`; `test_rt_scene3d` 10k fixture | Windows/Linux lanes remain |
| GATE-005 | Software-backend correctness baseline for each new visual feature | `README.md` §6 | todo |  | GPU parity capability-gated + smoke |
| GATE-006 | All systems opt-in; bounded-scene path byte-unchanged with flags off | `README.md` §6 | todo |  | No-regression contract |
| GATE-007 | Zero new dependencies; from-scratch implementations | `README.md` §5 | todo |  | Core Principle #6 |
| GATE-008 | `check_runtime_completeness.sh` green + docs+ctest per new API | `runtime-changes.md` Validation | partial | `check_runtime_completeness.sh` green; `test_runtime_surface_cli` strict audit now green after classifying 20 internal helper symbols Codex left unclassified (`RuntimeSurfacePolicy.inc`: commit-queue header + glTF scene/camera, IK, blend-tree, terrain helpers) | Per-new-API docs+ctest coverage still incomplete across phases |
| GATE-009 | Any IL/VM-touching change has an ADR | `README.md` §5 | todo |  | Core Principle #1 |
| GATE-010 | Public API names match current `Viper.*` runtime conventions and class-id ABI | `review.md`, `api-spec.md` | done | `test_graphics3d_abi_surface`; `ctest --test-dir build -R '^(test_graphics3d_abi_surface|test_rt_game3d)$' --output-on-failure` | fully qualified classes, `3D` suffixes, Graphics3D PascalCase, Game3D lower/camel where existing style requires it; appended class ids |

## Phase status

| ID | Phase | Status | Entry dependency | Exit proof / link | Notes |
|---|---|---|---|---|---|
| PHASE-C | Carryover: close out 3D Next Level | partial | Prior plan ~90% done | Local macOS graphics3d and Release perf/GPU lanes green; local-closeable CO rows closed or re-waived | Windows/Linux CO-1/CO-11 host evidence remains |
| PHASE-0 | Foundations spike (determinism/threading/index/perf) | partial | PHASE-C prerequisites | `02-decisions.md` closed; `g3d_3dnext2_surface_probe`; `g3d_openworld_slice_perf_harness`; Release macOS perf baseline | Cross-platform perf host runs remain |
| PHASE-1 | Concurrency runtime (job system) | partial | PHASE-0 decisions | `g3d_3dnext2_surface_probe`; `test_rt_g3d_commit_queue`; `test_rt_game3d`; `test_rt_parallel` | Worker substrate, ordered merge proof, Game3D worker controls, and internal main-thread commit queue landed; race-stress and asset/upload integration remain |
| PHASE-2 | Floating origin | todo | PHASE-0 strategy | — | Before streaming |
| PHASE-3 | Spatial acceleration index | partial | PHASE-0 choice | `test_rt_scene3d` indexed query/draw parity and 10k generated cull/query fixture | Internal Scene3D index landed; measured perf lane and physics broadphase sharing remain |
| PHASE-4 | Async asset + GPU-upload streaming | partial | PHASE-1 | `test_rt_game3d` AssetHandle3D deferred-handle/cancel/error + cache residency baseline; `test_rt_model3d` glTF parse-failure recovery | `AssetHandle3D`, `SetResidencyBudget`, `Evict`, and malformed/unsupported async load-error states landed; worker decode/upload remains |
| PHASE-5 | World partition + terrain streaming | partial | PHASE-1/3/4, PHASE-2 | `test_rt_game3d` WorldStream3D telemetry plus manifest-driven VSCN cell load/unload, rendered heightmapped Terrain3D tile payload residency, static heightfield collider residency, terrain nav-bake source residency, staged `update(dt)` load-budget telemetry, shared-edge seam sampling, and `g3d_openworld_slice_long_traversal` repeated residency churn | Worker-backed async decode/upload remains |
| PHASE-6 | Visibility scaling (occlusion + auto-LOD/HLOD) | partial | PHASE-3 | `test_rt_canvas3d` dense occlusion reduction; `test_rt_scene3d` LOD/impostor coverage | Reconciles CO-8; portal/PVS, index-fed occluders, and named-hardware timing remain |
| PHASE-7 | Lighting scaling (clustered + CSM) | partial | PHASE-3; CO-6 | `test_rt_canvas3d` software many-light baseline; `test_rt_canvas3d_gpu_paths` forward fallback and shadow-caster selection | GPU clustered upload/shaders and CSM remain |
| PHASE-8 | Physics depth | partial | PHASE-3 | Warm-started sequential-impulse solver + sleep islands land a stable deterministic resting box stack (`test_rt_physics3d` `test_box_stack_rests_stably`); also `test_rt_game3d` contact accessors and `test_rt_physics3d` solver iterations, AABB manifolds, mesh BVH narrow phase, GJK/EPA hull/simple slices, sleeping controls, sparse body-count stress, hinge/rope/SixDof slices | Remaining work: OBB-vs-OBB clipped manifolds (rotated boxes), island-batched solve + named body-count perf, richer 6DOF pose-angle semantics |
| PHASE-9 | Navigation + AI depth | partial | PHASE-3/5 | `test_rt_navagent3d` avoidance controls + head-on steering reduction; `test_rt_navmesh_blend` scene `Bake`/`BakeTiled` baseline, whole-mesh `RebuildTile`, off-mesh link bridge/direction, agent-radius portal coverage, and coarse AABB obstacle add/update/remove carving; `g3d_openworld_slice_probe` verifies streamed terrain participates in world nav bake | Voxel/region autogen, real tiled ownership, full corridor erosion, full avoidance, and pathfinding acceleration remain |
| PHASE-10 | Animation depth (IK, additive, blend trees, retarget) | partial | CO-9 | `test_rt_animcontroller3d` IK two-bone/look-at/FABRIK, true additive play/crossfade, animation LOD, and `SetBlendTree` slices; `test_rt_game3d` additive play/crossfade, `setBlendTree`, and `setIKSolver` wrappers; `test_rt_navmesh_blend` BlendTree3D blendspace slice; `test_rt_skeleton3d` Retarget slice; `g3d_openworld_slice_probe` imported skinned play/crossfade plus LookAt IK and terrain-sampled TwoBone foot IK proof | deeper IK constraints/terrain-foot orientation polish, bone-count LOD, and humanoid/proportional retarget mapping remain |
| PHASE-11 | Asset pipeline depth (compression, KTX2, glTF camera/scene) | partial | PHASE-4 | `test_rt_canvas3d` TextureAsset3D KTX2/material bridge, RGBA8 per-mip fallback switching, and mip residency telemetry; `test_rt_model3d` Model3D scene-indexed API, scene-local glTF camera import, and secondary glTF scene roots | Native compressed upload/backend residency and VRAM/RAM measurements remain |
| PHASE-12 | Open-world vertical slice + tooling + docs + baselines | partial | All API areas usable | `g3d_openworld_slice_probe`; `g3d_openworld_slice_perf_probe`; `g3d_openworld_slice_gpu_smoke`; `g3d_openworld_slice_package_dry_run`; `test_rt_game3d` stream inspection, World3D counter, and navmesh bake hooks | Open-world smoke streams/renders/simulates/replays with visible streamed terrain; stream inspection, streamed-terrain world-scoped navmesh bake, aggregate runtime counters, skinned glTF play/crossfade, LookAt IK binding, terrain-sampled TwoBone foot IK, software visual baseline, capability-gated GPU smoke with degenerate-basis robustness pass, deterministic perf probe, and named local macOS Release software/Metal perf baseline landed; native compressed upload proof and cross-platform perf baselines remain |

## Acceptance criteria

| ID | Criterion | Status | Proof / link | Notes |
|---|---|---|---|---|
| AC-001 | Determinism preserved with job pool on/off, VM/native | todo |  |  |
| AC-002 | Scene 50 km from origin renders/simulates without jitter beyond tolerance | todo |  |  |
| AC-003 | Frame cull scales with visible (not total) node count; recorded speedup | partial | `test_rt_scene3d` 10k generated cull/query fixture | candidate-reduction proof landed; named-hardware timing baseline still todo |
| AC-004 | Streaming stays within bounded per-frame budget; recorded hitch delta | partial | `test_rt_game3d` | Deterministic `WorldStream3D.update(dt)` load budget and `pendingRequestCount` telemetry landed; recorded hitch delta against worker-backed blocking decode/upload remains |
| AC-005 | >4 km² streamed world: bounded memory, no seams | partial | `g3d_openworld_slice_probe`; `g3d_openworld_slice_long_traversal`; `test_rt_game3d` | Open-world probes traverse all four far-apart quadrants repeatedly with one resident cell/tile/collider/nav source and bounded bytes over staged stream ticks; unit test samples matching terrain sidecar edge heights, verifies heightfield collider/nav-bake source residency, asserts pending staged loads, and confirms streamed terrain renders through `World3D.drawScene`; worker-backed async decode/upload remains |
| AC-006 | Occlusion + LOD cut draw calls/fill by recorded factor, no missing geo | partial | `test_rt_canvas3d`; `test_rt_scene3d` | Dense CPU occlusion fixture queues 65 opaque draws, submits one backend draw, and reports 64 skips; LOD/impostor correctness is unit-covered. Fill-rate/GPU timing, portal/PVS, and named-hardware reduction baselines remain |
| AC-007 | >16 lights render correctly (clustered); CSM stable | partial | `test_rt_canvas3d`; `test_rt_canvas3d_gpu_paths` | Software clustered baseline submits >16 active lights behind capability gate; unsupported/GPU paths retain 16-light fallback. GPU clustered correctness and CSM stability remain |
| AC-008 | Boxes stack/pile rests; body-count target met; hinge/rope/6DOF + true convex | partial | A warm-started sequential-impulse solver with sleep islands now lands a stable, deterministic resting box stack (`test_rt_physics3d` `test_box_stack_rests_stably`); also covered: solver-iteration support effect, AABB manifolds, mesh BVH narrow phase, GJK/EPA convex-hull + hull-vs-simple contacts, sparse 321-body step stress, hinge/rope/SixDof limits | Resting stacks + warm-start/islands done; named body-count perf target, OBB-vs-OBB clipped manifolds, and richer 6DOF semantics remain |
| AC-009 | Navmesh auto-bakes + tile-rebuilds; agent-count target paths + avoids | partial | `test_rt_navagent3d` local avoidance property and steering tests; `test_rt_navmesh_blend` scene-bake, tiled-entry/refilter, off-mesh link, and coarse AABB obstacle add/update/remove carving tests; `g3d_openworld_slice_probe` streamed-terrain nav bake check | Voxel/region autogen, real tile-local rebuild, path acceleration, full avoidance bounds, and agent-count perf remain |
| AC-010 | IK foot-plant + look-at; additive layers, blend trees, retargeting | partial | `test_rt_animcontroller3d` IK target/weight/look-at/FABRIK coverage and true additive layer play/crossfade deltas; `test_rt_game3d` additive and `setIKSolver` wrappers; `test_rt_navmesh_blend` blend tree weights; `test_rt_skeleton3d` retarget mapping; `g3d_openworld_slice_probe` imported skinned play/crossfade plus LookAt IK and terrain-sampled TwoBone foot IK sample | terrain-foot orientation, full humanoid retargeting, and bone-count LOD remain |
| AC-011 | Compressed textures per backend (recorded VRAM); KTX2/precompressed blocks; glTF camera/scene | partial | `test_rt_canvas3d` KTX2 metadata/RGBA8 per-mip material fallback plus mip residency byte telemetry; `test_rt_canvas3d_production` compression capability defaults; `test_rt_model3d` scene-indexed Model3D API, scene-local glTF cameras, and secondary-scene import | Basis/Draco/meshopt are Phase 11b/stretch; native compression/backend residency remain |
| AC-012 | `examples/3d/openworld_slice/` demonstrates the full stack + replay | partial | `g3d_openworld_slice_probe`; `g3d_openworld_slice_perf_probe`; `g3d_openworld_slice_long_traversal`; `g3d_openworld_slice_gpu_smoke` | Software smoke covers all-four-quadrant bounded stream traversal, repeated deterministic residency churn, rendered heightmapped terrain payloads, stream inspection hooks, World3D counter reads, streamed-terrain world-scoped navmesh bake, KTX2 texture asset usage, skinned glTF play/crossfade plus LookAt IK, terrain-sampled TwoBone foot IK, async asset handle completion, character/physics/nav stepping, final-frame baseline comparison, deterministic replay, software perf metrics, and a named local macOS Release software/Metal perf baseline; GPU smoke is capability-gated and now includes the degenerate-basis robustness pass; native compressed upload proof and cross-platform perf baselines remain |
| AC-013 | Cross-platform + completeness + docs/ctest per API every phase | todo |  |  |
| AC-014 | No regression: bounded-scene game byte-unchanged with flags off | todo |  |  |

## Current readiness notes

- First implementation slice landed: `World3D.workerCount`,
  `World3D.jobsEnabled`, `World3D.setWorkerCount`, plus
  `test_graphics3d_abi_surface` guarding naming/class-id drift. `World3D` now
  also owns a lazy internal worker pool for animator batches, with
  single-worker vs. multi-worker parity covered in `test_rt_game3d`.
  `g3d_3dnext2_surface_probe` now covers the Phase-0 ordered parallel-map
  surface and a two-run `World3D.runFramesOnly` worker-count replay. Phase C
  still must close CO-1 (cross-platform) and the Windows/Linux side of CO-11
  before the plan can record full cross-platform baselines.
- Floating-origin public controls also landed (`floatingOrigin`, `worldOrigin`,
  `setOriginRebaseThreshold`) with flag-off and threshold-rebase unit coverage;
  full 50 km visual/physics parity remains a Phase 2 exit item.
- `Scene3D.RebaseOrigin` now provides the raw scene-subtree rebase primitive,
  and Game3D floating origin calls through it before shifting physics bodies,
  camera, and listener state.
- Game3D collision wrappers now expose `contactCount`, `contactPoint(i)`,
  `contactNormal(i)`, and `contactSeparation(i)` with ctest + ABI-name
  coverage. `Physics3DWorld.SolverIterations` / `SetSolverIterations` also
  landed with a spring-solver effect test. Raw multi-point manifolds and the
  warm-started contact solver remain Phase 8.
- Mesh/convex-vs-sphere/capsule/box narrow phase now traverses the per-mesh
  physics BVH before testing candidate triangles, keeping the full triangle scan
  as a fallback; `test_rt_physics3d` asserts the BVH is built by sphere and box
  mesh contacts.
- Sequencing override (pull Phase 8/9 earlier) is decision SEQ in
  `02-decisions.md`.
- A 2026-05-29 source-comparison pass re-verified every `file:line` claim and
  corrected the plan to the current subsystem layout (`scene/`, `physics/`, …);
  see `../review.md` §"Source-comparison pass". Per-backend checklists now cover
  all three GPU paths: `../metal.md` (macOS), `../directx.md` (Windows),
  `../opengl.md` (Linux). GATE-003's mac/win/linux lane maps to these three.
