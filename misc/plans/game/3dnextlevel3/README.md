# 3D Next Level 3 - Remaining Work Checklist

> Supersedes `misc/plans/game/3dnextlevel2/`.
>
> `3dnextlevel2` remains the historical record: roadmap, API/runtime specs,
> backend notes, progress trackers, and waivers. This file is the reset plan:
> only unresolved work, checked against the current code and the old trackers,
> with one checkbox per item that needs to be closed.

## Rules for this plan

- One checkbox is one closeable work item. Do not split completion state inside a
  checkbox; if only part of an item is done, leave the checkbox open.
- Items are ordered by dependency and leverage. Earlier items unblock later
  items, especially compressed texture upload, spatial indexing, and GPU light
  scaling.
- Each checkbox carries source tags from the old plan: phase (`P#`), gate
  (`GATE-#`), or acceptance criterion (`AC-#`).
- Items that are not closeable on this macOS host, or are explicitly optional,
  live in "Deferred / External / Stretch" instead of the work-down checklist.

## Already Closed Locally

- Phase C carryover is locally closed: lifetime and diagnostics hardening,
  render-target finalization contract, explicit fallback-lighting decision,
  optional getters, Metal robustness probe, occlusion docs cleanup,
  determinism/resize probes, starter samples, and committed skinned-character /
  GLB / audio fixtures.
- Phase 0 foundations are locally closed: determinism/threading policy,
  job-system shape, spatial-index direction, floating-origin strategy,
  streaming format/granularity, lighting direction, texture-compression scope,
  perf harness, and big-world fixtures.
- Phase 1 core concurrency is in place: the thread-pool substrate, ordered merge,
  main-thread commit queue, worker controls, and pool-on/off `runFrames` parity.
- Phase 10 animation is closed: TwoBone / LookAt / FABRIK IK, pole vectors,
  terrain-foot orientation, true additive layers, blend trees, animation-rate and
  bone-count LOD, humanoid-role retargeting, and proportional retargeting.

## Work-Down Checklist

### Cross-Cutting First

- [x] **NL3-001 [P4/P5/P11/P12, AC-004/005/011/012] Add native compressed-texture backend upload and capability enablement.** Extend the backend vtable beyond CPU `Pixels` uploads so retained KTX2 BC7 / ASTC / ETC2 mip blocks can upload natively under the existing `Canvas3D.SetTextureUploadBudget`, `TextureUploadPendingBytes`, and `TextureUploadBytes` controls; enable `BackendSupports("bc7"|"astc"|"etc2")` per Metal, D3D11, and OpenGL only when the native path is wired and tested, with Metal choosing BC on Intel/AMD Macs and ASTC/ETC2 on Apple Silicon as described in `../3dnextlevel2/metal.md`.

- [x] **NL3-002 [P3, AC-003] Replace the flat sweep-style Scene3D index with a true tree/BVH and incremental refit.** The current `Scene3D` index is a min-X sorted array rebuilt lazily in `scene/rt_scene3d.c`; replace it with a real octree/BVH-style structure that respects double-precision world bounds, incrementally refits dirty nodes, preserves the flat-walk parity fallback behind `use_spatial_index`, and records cull/query speedup on the 10k-node fixture.

- [x] **NL3-003 [P3/P8] Decide and implement the shared Scene3D/physics spatial structure, or prove the sibling physics broadphase is better.** Physics still uses sweep-and-prune broadphase sorting in `physics/rt_physics3d.c`; either route scene queries and physics broadphase through one shared structure or record a correctness/perf proof that a separate physics broadphase should remain.

- [x] **NL3-004 [P1, GATE-002] Run and record the race-stress / TSan lane for the job and commit-queue paths.** Parallel-map determinism and pool-on/off parity are proven; the remaining concurrency exit evidence is a clean stress/race-detector run covering the worker pool, ordered merge, asset workers, and main-thread commit queue. Evidence: `tsan-concurrency-lane.md`.

### Phase 2 - Floating Origin

- [x] **NL3-005 [P2, AC-002] Finish camera-relative handling for raw/generated vertex paths.** `Mesh3D.AddVertex` now preserves double positions for identity-matrix raw world-space draws, Canvas3D snapshots/subtracts the active camera origin before float upload, and standalone `Particles3D` / `Sprite3D` / decal-style generated vertices upload camera-relative payloads. Evidence: `test_rt_canvas3d` camera-relative raw/generated slice, `test_rt_particles3d_contract`, `test_rt_game3d`, and `test_graphics3d_abi_surface`.

- [x] **NL3-006 [P2] Complete the atomic cross-system rebase hook.** Added `World3D.rebaseOrigin(dx,dy,dz)` as a between-frame boundary that traps during active frames and shares the automatic floating-origin path; Game3D now routes physics through `Physics3DWorld.RebaseOrigin`, while `Particles3D.RebaseOrigin` and `Sprite3D.RebaseOrigin` cover standalone generated paths. Evidence: `test_rt_game3d`, `test_rt_physics3d`, `test_rt_canvas3d`, `test_rt_particles3d_contract`, and `test_graphics3d_abi_surface`.

- [x] **NL3-007 [P2, AC-002/014] Prove 50 km rendered near/far parity and flag-off byte equality.** `test_rt_game3d` now renders the same software scene near origin and after a 50 km floating-origin rebase with Canvas3D frustum culling both on and off, verifies the far render uses camera-relative upload, compares final-frame pixels within tolerance, and proves a bounded scene stays byte-identical after toggling `floatingOrigin` back off.

### Phase 4 - Async Asset Streaming

- [x] **NL3-008 [P4, AC-004] Add distance-aware priority/residency to the Assets3D template cache.** `Assets3D.SetResidencyHint(template, priority, distance)` now annotates cached `ModelTemplate` entries, and byte/entry pressure evicts lower-priority and farther templates before falling back to LRU. Evidence: `test_rt_game3d` three-template budget-pressure proof and `test_graphics3d_abi_surface`.

- [x] **NL3-009 [P4/P6/P11] Add unified mesh LOD residency and reference-counted streaming hooks.** `Mesh3D.Resident` / `ResidentBytes` now expose mesh-payload residency, Canvas3D/Scene3D skip nonresident meshes, SceneNode3D LOD selection falls back through resident meshes via `SetLodResident` / `GetLodResidentBytes`, and WorldStream3D remeasures loaded VSCN cells after LOD demotion/promotion so detail can unload without releasing the whole scene/template. Evidence: `test_rt_scene3d`, `test_rt_game3d`, and `test_graphics3d_abi_surface`.

- [x] **NL3-010 [P4, AC-004] Close the recorded streaming hitch-budget proof with native compressed upload enabled.** `streaming_hitch_probe.zia` now has an opt-in native-compressed GPU upload lane; the CTest `g3d_openworld_slice_streaming_hitch_native_compressed_probe` runs the same hitch probe with the platform GPU backend and records backend, format, zero-budget pending bytes, release time, and latest-frame upload bytes. Local evidence on macOS/Metal/Apple Silicon: `native_compressed_upload=1 native_backend=metal native_format=astc native_zero_pending_bytes=16 native_upload_bytes=16`; the original async template hitch probe still passes unchanged.

### Phase 5 - World Partition And Terrain Streaming

- [x] **NL3-011 [P5, AC-005] Implement terrain tile LOD seam stitching and prove worlds beyond the 4096 heightmap cap.** Streamed terrain tiles now stitch full matching manifest edges when adjacent resident tiles load, averaging border samples in world-height space and invalidating cached LOD meshes before render. Collider/nav sources are rebuilt from the stitched height grid. Evidence: `test_rt_game3d` includes a two-tile 9600-unit / >4 km2 proof with mismatched height sidecars, skirts disabled, and adjacent tiles forced to different terrain LOD thresholds.

- [x] **NL3-012 [P5] Extend the VSCN streaming manifest with richer authored physics/nav metadata.** Streamed `cells[]` and `tiles[]` now parse `material`, `layer` / `collisionLayer`, `collisionMask` or nested `collision`, `navArea`, `traversalCost`, and `sidecar` / `binarySidecar` metadata. WorldStream3D exposes typed inspection getters for those fields, applies parsed cell layer/mask to spawned root entities, and applies terrain collision layer/mask/enabled state to streamed heightfield collider bodies. Evidence: `test_rt_game3d` inspection hooks fixture and `test_graphics3d_abi_surface`.

- [x] **NL3-013 [P5, AC-005] Record the named >4 km2 traversal hitch and memory proof.** `long_traversal.zia` now emits `TRAVERSAL:` telemetry for all-quadrant stream churn: backend, visits, elapsed/max-visit time, max resident stream bytes, draw/body/entity counts, one resident cell/tile, zero pending requests, streamed area, and seam status. Evidence recorded in `examples/3d/openworld_slice/baselines/perf_macos_apple_m4_max.md` on Apple M4 Max: software and Metal both traversed 32 + 32 visits over 18,939,904 m2 with checksum replay match, max stream bytes 327730, pending requests 0, and max visit 3.383 ms software / 1.191 ms Metal.

### Phase 6 - Visibility Scaling

- [x] **NL3-014 [P6] Feed CPU occlusion from the spatial index instead of the sorted opaque draw list.** `Canvas3D.OcclusionCandidateCount` now records CPU grid workload, and Scene3D draw feeds the grid from BVH-selected candidates before Canvas3D opaque sorting. Evidence: `test_rt_scene3d` indexes 130 drawables, narrows them to 2 occlusion candidates, and submits one front draw; `test_rt_canvas3d` still covers the raw Canvas3D sorted-queue fallback with 65 opaque draws, one submission, 64 skips, and 65 candidates.

- [x] **NL3-015 [P6] Add portal/PVS occlusion for interiors.** Scene3D now supports authored visibility-zone AABBs and directed/bidirectional portal links through `AddVisibilityZone` and `AddVisibilityPortal`; `Draw` builds a camera-zone PVS, skips drawables inside unreachable zones while keeping unzoned outdoor nodes visible, and reports `PvsCulledCount`, `VisibilityZoneCount`, and `VisibilityPortalCount`. Evidence: `test_rt_scene3d` culls an unlinked interior room, reports one PVS skip, then reveals the room after a portal is added; `test_graphics3d_abi_surface` guards the public names.

- [x] **NL3-016 [P6, AC-006] Build an authored dense city/forest fixture and record draw-call/fill-rate reduction.** `examples/3d/openworld_slice/visibility_dense_probe.zia` now authors a dense city/forest PVS scene, CTest registers `g3d_openworld_slice_visibility_dense_probe`, and the local macOS Apple M4 Max Release baseline records 169 authored drawables reduced to 49 submitted draws, 120 PVS skips, 71.006% draw reduction, 50.407% fill-proxy reduction, and `no_missing_geometry=1` from a software final-frame comparison against the no-PVS render.

### Phase 7 - Lighting Scaling

- [x] **NL3-017 [P7, AC-007] Implement real GPU clustered/forward+ lighting on Metal, D3D11, and OpenGL.** The real platform GPU backend vtables now advertise `BackendSupports("clustered-lighting")` while fake GPU-named test backends stay unsupported; `SetClusteredLighting(true)` raises `MaxActiveLights` from 16 to the bounded 64-light payload on Metal/D3D11/OpenGL. `gpu_smoke.zia` records a 24-light GPU draw against the 16-light fallback, and the local macOS Apple M4 Max Release baseline records Metal `fallback_max_lights=16`, `clustered_max_lights=64`, `configured_lights=24`, `fallback_us=66`, and `clustered_us=35`.

- [x] **NL3-018 [P7, AC-007] Implement cascaded shadow maps and lift the shadow-caster cap where supported.** `VGFX3D_MAX_SHADOW_LIGHTS` is now four, `BackendSupports("shadow-csm")` advertises only the software backend and real platform GPU vtables with shadow hooks, and `SetShadowCascades(count)` drives primary-directional-light cascades with camera-depth split metadata in the backend light payload. Metal/D3D11/OpenGL/software shadow samplers now resolve up to four shadow slots, `test_rt_canvas3d_gpu_paths` proves three contiguous cascade passes plus monotonic split payloads, and `gpu_smoke.zia` records a 3-cascade Metal CSM draw (`draws=4`, `shadow_map=1024`, Release `csm_us=239` direct / `211` CTest).

### Phase 8 - Physics Depth

- [x] **NL3-019 [P8, AC-008] Add OBB-vs-OBB clipped contact manifolds for rotated boxes.** Rotated box face contacts now choose a reference face from the SAT normal, clip the incident face against the reference OBB side planes, and publish up to four stable manifold points through the existing warm-started solver/event surface. Evidence: `test_rt_physics3d` `test_rotated_box_box_exposes_clipped_manifold` plus the existing AABB stack/manifold coverage.

- [x] **NL3-020 [P8, AC-008] Add island-batched solve throughput and a named body-count/resting-pile target.** Contact solving now builds awake, non-trigger contact islands, solves each island batch independently, and exposes `LastSolverIslandCount`, `LastSolverActiveBodyCount`, and `LastSolverContactCount` telemetry. Evidence: `test_rt_physics3d` `test_world_solver_island_batches_resting_pile_target` records `PHYSICS_ISLAND_BATCH_TARGET: bodies=257 piles=32 height=8 islands=32 active_bodies=256 contacts=256 first_step_us=16629 settle_steps=120 settle_us=2650914 min_top_y=7.256`, and the ABI surface guard covers the new PascalCase runtime properties.

- [x] **NL3-021 [P8] Broaden mesh narrow-phase coverage and integrate it with the Phase-3 broadphase.** Triangle-mesh colliders now handle convex-hull contacts by traversing the per-mesh BVH and running GJK/EPA against candidate triangles, with the full triangle scan retained as a correctness fallback. Evidence: `test_rt_physics3d` `test_mesh_convex_hull_bvh_and_body_broadphase_target` records `PHYSICS_MESH_BVH_TARGET: tiles=16 triangles_per_tile=512 built_mesh_bvhs=1 collisions=1 step_us=979`, proving the chosen body-centric physics broadphase prunes distant mesh tiles before any per-mesh triangle BVH is built.

- [x] **NL3-022 [P8] Broaden GJK/EPA convex coverage and analytics.** Convex hull contacts now have explicit coverage for hull-vs-capsule, hull-vs-box through the reversed simple-vs-hull branch, separated boxes that overlap a tetrahedron AABB but not the convex volume, positive penetration/finite-normal assertions, and a named mixed-shape target. Evidence: `test_rt_physics3d` `test_convex_hull_gjk_handles_box_capsule_and_simple_edge_cases` plus `PHYSICS_CONVEX_GJK_TARGET: pairs=32 contacts=32 spheres=8 capsules=8 boxes=8 hulls=8 step_us=856`.

- [x] **NL3-023 [P8] Deepen 6DOF pose-angle semantics and stability coverage.** SixDof angular limits now store the creation relative orientation as the zero pose, clamp per-axis joint-frame pose angles in radians, and remove angular velocity only for locked axes or motion pushing farther past a pose stop. Evidence: `test_rt_physics3d` `test_sixdof_joint_limits`, `test_sixdof_joint_angular_pose_limits_hold_against_spin`, and `test_sixdof_joint_linear_motor_preserves_angular_pose_limits` cover pose clamping, locked-axis stability, and a linear motor running while angular pose limits hold.

### Phase 9 - Navigation And AI Depth

- [x] **NL3-024 [P9, AC-009] Implement per-tile geometry re-voxelization.** Tiled navmesh bakes now retain per-cell voxel source height/walkability and corner mappings so `RebuildTile(tileX, tileZ)` refreshes only that tile's geometry, heights, and blocked state without a whole-scene voxel pass. Evidence: `test_rt_navmesh_blend` `test_navmesh_rebuild_tile_refreshes_retained_geometry_source` proves a retained tile source edit stays stale before rebuild, remains stale after a far-tile rebuild, updates height after its own tile rebuild, and removes walkability when the retained source tile becomes unwalkable.

- [x] **NL3-025 [P9] Add fine polygon-level carving plus traversal metadata.** Obstacle carving now tests exact triangle footprint overlap instead of triangle AABB overlap, `SetArea`/`GetArea`/`GetTraversalCost` assign and query polygon area/cost metadata, A* weights polygon traversal costs and publishes `LastPathCost`, and `SetOffMeshLinkMetadata` plus link getters store link kind/cost/state metadata that affects link cost. Evidence: `test_rt_navmesh_blend` covers footprint-only carving false positives, area/cost metadata, link metadata, and cost-weighted path queries; `test_graphics3d_abi_surface` and `test_rt_graphics_surface_link` guard the public surface.

- [x] **NL3-026 [P9, AC-009] Implement full ORCA/RVO-quality crowd avoidance and record an agent-count perf baseline.** `NavAgent3D` avoidance now uses a deterministic reciprocal-velocity-obstacle candidate solver over the existing spatial grid, with path/target fallback peer intent, speed-preserving candidate scoring, and a stable passing-side tie-break. Evidence: `test_rt_navagent3d` covers lateral RVO passing, head-on deadlock avoidance, multi-lane crossing, grid/full-scan parity, and `NAVAGENT_CROWD_TARGET: agents=200 frames=180 update_us=564686 min_pair_distance=1.142 crossed=170` from the Release perf build.

### Phase 11 - Asset Pipeline Depth

- [x] **NL3-027 [P11, AC-011] Add ETC2 / ASTC software decode and BC7 partitioned-mode decode.** `TextureAsset3D` now has table-driven BC7 modes 0-7 with fixed BC7 partition/anchor tables, representative ETC2 RGBA8/EAC individual/differential fallback, ASTC LDR void-extent fallback, retained native payloads for unsupported blocks, and compressed block/KTX2 fixtures. Evidence: `test_rt_canvas3d` covers BC7 partitioned constant-white modes 0-3/7, direct ETC2/ASTC block decode, ETC2/ASTC KTX2 `Pixels` fallbacks, and native-only ASTC draw forwarding; ABI/link/runtime-surface audits guard the helper symbols. Unsupported ETC2 T/H/planar and non-void ASTC blocks remain native-only until a broader decoder lands.

- [x] **NL3-028 [P11, AC-011] Wire native compressed block residency through TextureAsset3D and the streaming/upload path.** `TextureAsset3D` resident mip ranges now feed native compressed backend submission through the shared backend helpers/cache keys, and `test_rt_canvas3d` covers relative resident native mip selection, pending native bytes, capability gating, empty-residency upload disablement, and compressed-vs-raw resident bytes. The opt-in `g3d_openworld_slice_streaming_hitch_native_compressed_probe` now generates known BC7/ASTC/ETC2 native fixtures, validates final-frame readback tolerance, and records raw RGBA bytes, compressed resident bytes, RAM/VRAM reduction, and backend upload bytes. Local macOS/Metal evidence: `native_backend=metal native_format=astc native_zero_pending_bytes=16 native_upload_bytes=16 native_raw_rgba_bytes=64 native_compressed_bytes=16 native_ram_reduction_pct=75 native_vram_reduction_pct=75 native_tolerance_checked=1 native_tolerance_max_diff=73`.

### Phase 12 - Slice, Docs, And Formal Gates

- [x] **NL3-029 [P12, AC-012] Keep wiring new systems into `examples/3d/openworld_slice/` as they land.** The slice now renders a `TextureAsset3D` BC7/native-compressed material panel in the main scene, keeps the existing native-compressed GPU hitch lane for capable backends, and turns the terrain-sampled TwoBone foot IK proof into visible marker/leg entities near the streamed tile center. Evidence: `g3d_openworld_slice_probe` validates the visible node wiring, compressed texture residency, terrain-foot IK solve, software final-frame baseline tolerance, deterministic replay, stream traversal, physics/nav/character stepping, async/package fixtures, and runtime counters.

- [ ] **NL3-030 [P12, AC-013, GATE-008] Refresh docs and ctest-compiled snippets for every new or changed API.** Keep `docs/viperlib/graphics/`, `docs/graphics3d-guide.md`, `docs/graphics3d-architecture.md`, examples, disabled-graphics stubs, runtime completeness, ABI/class-id guards, and strict runtime surface audits current; also clean stale public docs that still describe already-landed Phase 9/10 slices as future work.

- [ ] **NL3-031 [AC-001, GATE-002] Formally close determinism.** The pool-on/off `runFrames` and ordered-map evidence exists; record the VM/native determinism arm explicitly for the acceptance row and require the same proof for any future simulation-touching change.

- [ ] **NL3-032 [AC-014, GATE-006] Prove no-regression for bounded scenes.** Run an existing bounded-scene game/sample with all new scale flags off and prove byte-for-byte output/state compatibility.

- [ ] **NL3-033 [GATE-005] Record software-baseline correctness for each remaining visual feature before GPU enablement.** For compressed textures, clustered lighting, CSM, occlusion/PVS, HLOD-related work, and any new visual path, keep the software backend as the correctness baseline with capability-gated GPU parity tests.

- [ ] **NL3-034 [GATE-007/009] Complete dependency, platform-policy, and ADR audits for each new slice.** Keep the zero-new-dependency rule, `lint_platform_policy.sh`, no raw platform macros outside adapters, and ADR coverage for any IL/VM-touching change.

## Deferred / External / Stretch

These are not in the work-down checklist because they are blocked by external
hardware/CI, delegated to another roadmap, or explicitly optional. They are kept
here so they do not disappear.

- **W2-001 - VM managed-closure callback trampoline.** `run(update)`,
  `onCollision`, `onUpdate`, and streaming callback sugar remain delegated to VM
  work; manual `tick`, `stepSimulation`, `runFramesOnly`, pollable event buffers,
  and handle polling remain authoritative.
- **W2-002 - Cross-platform GPU interactive-framerate proof.** Needs Windows and
  Linux reference GPU hardware. Local macOS Metal smoke and perf evidence exist.
- **W2-003 - Windows/Linux Release software FPS baselines and `-L graphics3d`
  green.** Needs named reference hosts; local macOS `graphics3d` and Release
  Apple M4 Max software/Metal baselines exist.
- **W2-004 - Basis supercompression, Draco, and meshopt decoders.** Optional
  Phase 11b/import-depth work, not a Phase 12 gate.
- **Optional P2 - per-cell local-origin policy.** Implement only if active-world
  rebase plus camera-relative upload is not enough after the 50 km proof.
- **Stretch P6 - automatic mesh simplification and backend-baked HLOD.** Authored
  LOD selection and generated impostor proxies are already in place; auto
  simplification/HLOD baking is separate stretch scope.
- **Polish P10 - deeper humanoid bone-role coverage and a dedicated visible
  skinned-foot demo.** Phase 10 acceptance is closed locally; this is polish
  unless a future game requires more retargeting coverage.
