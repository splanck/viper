# Runtime contract tracker

Tracks every runtime contract from `../runtime-changes.md`. Most new rows start
`todo`; carryover rows may reflect verified current status.

## 0. Carryover runtime items (Phase C)

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-CARRY-001 | Render-target finalization or `BackendSupports("rt-finalize")` | §0 / CO-4 | done | existing `FinalizeFrame`/`ScreenshotFinal` contract | `test_rt_canvas3d_gpu_paths`; `test_runtime_surface_cli`; graphics3d non-display lane | `api-spec.md` | local 2026-05-29 hardening pass | Backend capability probes remain per platform |
| R-CARRY-002 | Lifetime/diagnostics (`Game3D.<Type>.<method>`) + leak probes | §0 / CO-3 | done | destroyed world/entity and double-despawn diagnostics include the failing API; missing package assets return terminal error handles; quality fallback reasons remain inspectable; imported groups despawn repeatedly without registry retention; cache clear/reload remains usable | `test_rt_game3d`; `g3d_test_game3d_destroyed_handle_reject`; `g3d_test_game3d_destroyed_entity_reject`; `g3d_test_game3d_double_despawn_reject`; `g3d_test_game3d_assets_probe`; `g3d_test_canvas3d_quality_profiles` | `docs/viperlib/graphics/game3d.md`; `runtime-changes.md` | focused ctest slice green | R-LIFE-* |
| R-CARRY-003 | `Light3D.CastsShadows` + material texture-presence getters | §0 / CO-6 | done | `Light3D.CastsShadows` public get/set, persisted VSCN flag, glTF imported-light default; `Material3D.HasTexture/HasNormalMap/HasSpecularMap/HasEmissiveMap/HasMetallicRoughnessMap/HasAOMap/HasEnvMap` | Canvas3D accessor coverage + GPU shadow-selection + Scene3D roundtrip + ABI tests | `docs/viperlib/graphics/rendering3d.md`; `api-spec.md` | focused ctest slices green; `./scripts/check_runtime_completeness.sh` |  |
| R-CARRY-004 | Implicit-lighting decision + conditional gating/count | §0 / CO-5 | done | no implicit fallback lights; explicit `SetDefaultLighting()` path only | `test_rt_canvas3d_gpu_paths` explicit-dark regression | `docs/viperlib/graphics/rendering3d.md`; `api-spec.md`; `02-decisions.md` | `ctest --test-dir build -R '^test_rt_canvas3d_gpu_paths$' --output-on-failure` | D-007 closed |
| R-CARRY-005 | Metal robustness guards + probe | §0 / CO-7 | done | Metal skybox shader safe-normalizes zero camera vectors to Canvas3D's `-Z` convention; Canvas3D queues normal-mapped degenerate-basis meshes through a finite tangent snapshot while backend shaders retain safe normal fallbacks | `test_rt_canvas3d_gpu_paths`; `test_vgfx3d_backend_metal_shared`; `g3d_openworld_slice_gpu_smoke` | `docs/graphics3d-architecture.md`; `docs/graphics3d-guide.md`; `metal.md` | focused ctest slice green; local `VIPER_3D_BACKEND=metal ... gpu_smoke.zia` passed | R-METAL-006 closed |

## ABI / class registration

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-ABI-001 | Append class ids + `RT_CLASS_BEGIN` for every new public 3D class | `runtime-changes.md` intro, `api-spec.md` conventions | partial | ABI guard covers appended `AssetHandle3D`, `WorldStream3D`, `TextureAsset3D`, Hinge/Rope/SixDof, and prior Game3D additions | `test_graphics3d_abi_surface` | `progress/04-api-surface.md` | focused ctest slice green | keep updating for every future public class; never renumber existing `RT_G3D_*_CLASS_ID` values |

## 1. Concurrency runtime

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-JOB-001 | Worker pool + `rt_job_submit`/`wait`/parallel-for on `rt_platform.h` | §1 | partial | Existing `Viper.Threads.Pool` / `Viper.Threads.Parallel` runtime is the approved worker substrate; `World3D` owns a lazy internal `ThreadPool` and dispatches animator batches through it | `g3d_3dnext2_surface_probe`; `test_rt_game3d`; `test_rt_parallel`; `test_rt_parallel_reduce`; `test_rt_threadpool` | `docs/viperlib/graphics/game3d.md`; `docs/viperlib/threads.md`; `api-spec.md` | focused ctest slice green | domain-specific upload/commit integration remains |
| R-JOB-002 | Deterministic ordered-merge primitive | §1 | done | `rt_parallel_map` preserves input order for worker-produced results; Game3D worker batches merge before visible state advances | `g3d_3dnext2_surface_probe`; `test_rt_parallel`; `test_rt_game3d` | `docs/viperlib/threads.md`; `docs/graphics3d-architecture.md` | `ctest --test-dir build -R '^(g3d_3dnext2_surface_probe\|test_rt_parallel\|test_rt_game3d)$' --output-on-failure` | GATE-002 still needs VM/native parity for any future VM-touching simulation change |
| R-JOB-003 | Main-thread commit/upload queue | §1 | done | Internal Graphics3D commit queue accepts worker-produced callbacks, drains FIFO on the main thread with an optional per-frame budget, and exposes pending/submitted/drained telemetry for internal tests | `test_rt_g3d_commit_queue` | `docs/graphics3d-architecture.md` | `ctest --test-dir build -R '^test_rt_g3d_commit_queue$' --output-on-failure` | §4 still needs asset decode/upload integration |
| R-JOB-004 | Thread-safe allocator/GC/handle paths | §1 | done | Existing thread/parallel tests cover pool ownership, nested calls, retained map results, task traps, and shutdown; Phase 1 3D worker paths are copied-data jobs plus commit-queue enqueue, while public handle mutation and commit draining remain main-thread-only | `test_rt_threadpool`; `test_rt_parallel`; `test_rt_parallel_reduce`; `g3d_3dnext2_surface_probe`; `test_rt_g3d_commit_queue` worker-drain rejection | `docs/viperlib/threads.md`; `docs/graphics3d-architecture.md`; `runtime-changes.md` | `ctest --test-dir build -R '^test_rt_g3d_commit_queue$' --output-on-failure` | future worker-touched 3D loaders/upload queues reopen this audit under §4 |
| R-JOB-005 | `World3D.workerCount` / `jobsEnabled` / `setWorkerCount` controls | §1 | done | `rt_game3d_world_get_worker_count`; `rt_game3d_world_get_jobs_enabled`; `rt_game3d_world_set_worker_count` | `test_rt_game3d`; `test_graphics3d_abi_surface` | `docs/viperlib/graphics/game3d.md` | focused ctest slice green | internal-first; Game3D lower/camel style |

## 2. Floating origin

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-ORIG-001 | `rt_scene3d_rebase_origin` (nodes/bodies/audio/particles/queries) | §2 | partial | `Scene3D.RebaseOrigin` shifts root-level scene subtrees; Game3D routes floating-origin scene nodes through it | `test_rt_scene3d`, `test_rt_game3d`, `test_graphics3d_abi_surface` | `docs/viperlib/graphics/rendering3d.md`, `docs/viperlib/graphics/game3d.md` | focused ctest slice green | raw physics/audio/particles/query rebase hooks still todo outside Game3D path |
| R-ORIG-002 | Promote cull bounds + physics state precision | §2 | todo |  |  |  |  |  |
| R-ORIG-003 | Camera-relative upload path | §2 | todo |  |  |  |  | GPU stays float |
| R-ORIG-004 | `World3D.floatingOrigin` capability gate | §2 | done | `rt_game3d_world_get/set_floating_origin`; `worldOrigin`; `setOriginRebaseThreshold` | `test_rt_game3d` | `docs/viperlib/graphics/game3d.md` | focused ctest slice green | off = unchanged |

## 3. Spatial index

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-IDX-001 | Index over scene graph, incremental refit | §3 | partial | internal sweep-style Scene3D spatial index over visible drawable nodes; lazy rebuild on hierarchy/transform/mesh/visibility/LOD/impostor dirties; generated 10k grid proves query/draw candidate reduction | `test_rt_scene3d` | `docs/viperlib/graphics/rendering3d.md` | focused ctest slice green | true tree/BVH refit and named-hardware timing baseline still todo |
| R-IDX-002 | `Scene3D.QueryAABB` / `QuerySphere` / `RaycastNodes` | §3 | partial | public query methods use indexed candidates with deterministic flat-walk fallback; `VisibleNodeCount` draw telemetry | `test_rt_scene3d`; `test_graphics3d_abi_surface` | `docs/viperlib/graphics/rendering3d.md`; `api-spec.md` | focused ctest slice green | PascalCase public methods; physics sharing still todo |
| R-IDX-003 | Replace draw walk with index query (flag-gated) | §3 | partial | Scene3D.Draw uses indexed candidates before exact selected-LOD/impostor frustum culling; internal `use_spatial_index` flag keeps the flat path for parity; generated 10k fixture keeps candidates below 10% of drawables | `test_rt_scene3d` | `docs/viperlib/graphics/rendering3d.md` | focused ctest slice green | release perf-lane timing still todo |
| R-IDX-004 | Shared query contract; physics broadphase may be sibling | §3 | partial | Scene query semantics are shared and tested; physics broadphase remains separate | `test_rt_scene3d` | `api-spec.md` | focused ctest slice green | decide sharing under D-IDX |

## 4. Async asset + upload

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-ASYNC-001 | Worker decode + budgeted main-thread upload | §4 | todo |  |  |  |  |  |
| R-ASYNC-002 | Model + template async handles, residency, eviction | §4 | partial | `AssetHandle3D` deferred-handle baseline wraps entity and template load requests; first observation completes synchronously; `cancel()` is terminal before observation and stable after completion; missing filesystem/asset paths, unsupported model extensions, and malformed glTF JSON complete with non-cancel load errors; `SetResidencyBudget` and `Evict` evict cached templates without invalidating held handles | `test_rt_game3d`; `test_graphics3d_abi_surface`; `test_rt_model3d`; `test_rt_graphics_surface_link` | `docs/viperlib/graphics/game3d.md`; `api-spec.md` | focused ctest slice green | worker scheduling, broader corrupt-payload recovery, and distance-aware residency remain |
| R-ASYNC-003 | Texture/mesh mip/LOD residency hooks | §4 | todo |  |  |  |  |  |

## 5. World partition + terrain streaming

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-STREAM-001 | Terrain tile grid + seam stitching (lift 4096² cap) | §5 | partial | Resident `tiles[]` manifest entries instantiate `Terrain3D` payloads using manifest dimensions/scale, apply optional `viper-heightmap-v1` sidecars, render through `World3D.drawScene`, unload with stream residency, spawn matching heightfield collider bodies, and keep matching shared-edge samples | `test_rt_game3d`; `g3d_openworld_slice_probe`; `test_graphics3d_abi_surface`; `test_rt_graphics_surface_link` | `docs/viperlib/graphics/game3d.md`; `api-spec.md` | focused ctest slice green | LOD coordination and >4096 proof remain |
| R-STREAM-002 | Scene cell load/unload (subtree+physics+nav) | §5 | partial | `World3D.stream` owns a lazy stream handle; `WorldStream3D.mountCells` parses manifests and loads/unloads resident `.vscn` scene subtrees by center/radii/budget; `update(dt)` stages desired loads behind a deterministic per-frame budget and exposes deferred work through `pendingRequestCount`; terrain stream residency now includes static heightfield physics bodies plus hidden mesh-only nav-bake source nodes; long traversal probe repeats deterministic all-quadrant churn | `test_rt_game3d`; `g3d_openworld_slice_probe`; `g3d_openworld_slice_long_traversal`; `test_graphics3d_abi_surface` | `docs/viperlib/graphics/game3d.md`; `api-spec.md` | focused ctest slice green | worker-backed async decode/upload remains |
| R-STREAM-003 | VSCN streaming manifest/extension + bake hook | §5 | partial | cell manifest schema supports `cells[]`; tiled terrain manifests support `tiles[]` with `name`, `path`, optional `heightmap`, `center`, `radius`, `bytes`, `width`, `depth`, and `scale`; resident tiles own rendered heightmapped `Terrain3D` payloads, heightfield collider entities, and hidden terrain nav-bake source nodes consumed by `World3D.bakeNavMesh` | `test_rt_game3d`; `g3d_openworld_slice_probe`; `test_graphics3d_abi_surface`; `test_rt_graphics_surface_link` | `docs/viperlib/graphics/game3d.md`; `api-spec.md` | focused ctest slice green | richer authored physics/nav metadata and binary sidecars remain |
| R-STREAM-004 | Per-tile heightfield colliders | §5 | partial | Resident terrain tiles build static heightfield bodies from the same heightmap pixels used for the terrain payload, spawn invisible `<tile>_heightfield_collider` entities, spawn hidden `<tile>_navmesh_source` bake nodes, and remove those bodies/sources on unload/budget eviction | `test_rt_game3d`; `g3d_openworld_slice_probe` | `docs/viperlib/graphics/game3d.md`; `examples/3d/openworld_slice/README.md` | focused ctest slice green | authored material/layer/nav-area metadata remains |

## 6. Visibility

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-VIS-001 | Occlusion culling (software depth / portal-PVS) | §6 | partial | conservative CPU coverage/depth grid for sorted opaque draws; dense covered-draw fixture reduces 65 queued opaque submissions to one backend draw with 64 reported skips; `BackendSupports("occlusion")` reports baseline support | `test_rt_canvas3d`; `test_rt_canvas3d_gpu_paths` | `docs/viperlib/graphics/rendering3d.md`; `api-spec.md` | focused ctest slice green | portal/PVS, index-fed occluder set, and named-hardware timing remain |
| R-VIS-002 | Authored-LOD selection + HLOD/impostors | §6 | partial | authored screen-error LOD selection; generated textured impostor proxy retained on scene nodes | `test_rt_scene3d` | `docs/viperlib/graphics/rendering3d.md`; `api-spec.md` | focused ctest slice green | auto mesh simplification/backend-baked HLOD remains stretch |
| R-VIS-003 | Reconcile `SetOcclusionCulling` alias | §6 | done | `SetFrustumCulling` is frustum-only; `SetOcclusionCulling` enables frustum + CPU occlusion | `test_rt_canvas3d`; `test_rt_canvas3d_production` | `docs/viperlib/graphics/rendering3d.md`; `api-spec.md` | focused ctest slice green | CO-8 closed for runtime/docs |

## 7. Lighting

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-LIT-001 | Clustered/forward+ light culling | §7 | partial | `Canvas3D.SetClusteredLighting` and `MaxActiveLights` capability-gated; software backend provides a 64-light correctness baseline while unsupported backends stay on the 16-light forward cap | `test_rt_canvas3d`; `test_graphics3d_abi_surface` | `docs/viperlib/graphics/rendering3d.md`; `api-spec.md` | focused ctest slice green | keep forward shaders; GPU cluster build/upload still todo |
| R-LIT-002 | Cascaded shadow maps + shadow-caster cap lift | §7 | partial | current shadow selector honors `Light3D.CastsShadows`; `Canvas3D.SetShadowCascades` capability gate keeps unsupported CSM explicit | `test_rt_canvas3d`; `test_rt_canvas3d_gpu_paths`; `test_graphics3d_abi_surface` | `docs/viperlib/graphics/rendering3d.md`; `api-spec.md` | focused ctest slice green | CSM/cap lift remain |
| R-LIT-003 | Capability fallback to 16-light forward | §7 | partial | unsupported clustered lighting reports no capability, traps on enable, keeps `MaxActiveLights == 16`, and the deferred draw payload caps at the forward limit | `test_rt_canvas3d`; `test_rt_canvas3d_gpu_paths` | `docs/viperlib/graphics/rendering3d.md` | focused ctest slice green | GPU backend capability bits remain off until implementation lands |

## 8. Physics depth

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-PHYS-001 | Contact manifolds + iterated warm-started solver + Game3D contact accessors | §8 | partial | Game3D contact accessors added; `Physics3DWorld.SolverIterations` now iterates contacts and joints; AABB raw events expose bounded multi-point manifolds | `test_rt_game3d`, `test_rt_physics3d`, `test_graphics3d_abi_surface` | `docs/viperlib/graphics/game3d.md`, `docs/viperlib/graphics/physics3d.md` | `Viper.Game3D.Collision3DEvent.contact*`; `Physics3DWorld.SetSolverIterations` | non-AABB manifold grouping + warm-started contact solver still todo |
| R-PHYS-002 | BVH/index mesh narrow-phase | §8 | partial | per-mesh BVH prunes candidate triangles for mesh/convex-vs-sphere/capsule/box contacts, with full-scan fallback | `test_rt_physics3d` | `docs/viperlib/graphics/physics3d.md` | focused ctest slice green | perf fixture and remaining mesh paths still open |
| R-PHYS-003 | GJK/EPA convex narrow phase | §8 | partial | convex-hull pairs and hull-vs-simple primitive pairs use support-point GJK plus EPA penetration normal/depth; separated overlapping-AABB hulls reject correctly and contained sphere contacts are detected | `test_rt_physics3d` | `docs/viperlib/graphics/physics3d.md`; `api-spec.md` | focused ctest slice green | broader analytic/perf coverage remains |
| R-PHYS-004 | Hinge/rope/6DOF joints | §8 | partial | hinge anchor constraint, rope max-length constraint, and SixDof frame-anchor/limit setters registered as real `Viper.Graphics3D.*` classes | `test_rt_physics3d`, `test_graphics3d_abi_surface` | `docs/viperlib/graphics/physics3d.md`; `api-spec.md` | focused ctest slice green | broader 6DOF pose-angle semantics/stability coverage remain |
| R-PHYS-005 | Solver islands + scaled sleeping | §8 | partial | Dynamic bodies expose `CanSleep`/`Sleeping`, manual `Sleep`/`Wake`, idle auto-sleep, and skip integration while sleeping; sparse 321-body step stress exercises broadphase/body-count scaling | `test_rt_physics3d` | `docs/viperlib/graphics/physics3d.md` | focused ctest slice green | solver islands and scale-aware sleep thresholds remain |

## 9. Navigation depth

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-NAV-001 | Navmesh auto-generation (voxelize→mesh) | §9 | partial | `NavMesh3D.Bake` validates a `Scene3D`, gathers all `Mesh3D`-bearing nodes through world transforms, and builds a navmesh through the existing triangle baker; `World3D.bakeNavMesh`/`bakeTiledNavMesh` expose the same path for Game3D/editor workflows and include resident streamed-terrain nav source nodes | `test_rt_navmesh_blend`; `test_rt_game3d`; `test_graphics3d_abi_surface`; `test_rt_graphics_surface_link`; `g3d_openworld_slice_probe` | docs updated | focused ctest slice green | no voxel/region generation yet |
| R-NAV-002 | Tiled/streamable navmesh + tile rebuild + carving | §9 | partial | `NavMesh3D.BakeTiled` accepts tiled bake parameters as a full-scene baseline; `RebuildTile` refilters the preserved full mesh; `AddObstacle`, `RemoveObstacle`, and `UpdateObstacle` validate finite AABB bounds, edit coarse obstacles, remove overlapping triangles from the filtered walkable set, rebuild adjacency, and refresh off-mesh endpoint resolution | `test_rt_navmesh_blend`; `test_graphics3d_abi_surface`; `test_rt_graphics_surface_link` | docs updated | focused ctest slice green | no tile-local rebuild path or fine polygon carving yet |
| R-NAV-003 | Off-mesh links + `agent_radius` corridors | §9 | partial | `NavMesh3D` retains authored off-mesh links, refreshes endpoint triangle resolution after slope refilters, lets A* traverse directed/bidirectional link edges, and gates shared portals by `agent_radius * 2` | `test_rt_navmesh_blend`; `test_graphics3d_abi_surface`; `test_rt_graphics_surface_link` | docs updated | focused ctest slice green | no full polygon/corridor erosion or link animation/state metadata yet |
| R-NAV-004 | Local avoidance + pathfinding acceleration | §9 | partial | `rt_navagent3d_update` biases desired velocity away from overlapping/head-on enabled peers on the same `NavMesh3D`; radius clamps to `>=0` | `test_rt_navagent3d`; `test_graphics3d_abi_surface`; `test_rt_graphics_surface_link` | docs updated | focused ctest slice green | no tiled query accelerator or full crowd solver yet |

## 10. Animation depth

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-ANIM-001 | IK: two-bone + look-at + FABRIK | §10 | partial | `IKSolver3D` owns same-skeleton IK state, validates parented two-bone/FABRIK chains, clamps finite weights, and can apply controller-bound results after overlay composition and before palette generation; `LookAt` aims local +Z; openworld covers imported skinned play/crossfade plus LookAt IK and terrain-sampled TwoBone foot IK as sample proof | `test_rt_animcontroller3d`; `test_rt_game3d`; `g3d_openworld_slice_probe`; ABI/link guards | docs updated | focused ctest slices green | pole-vector and terrain-foot orientation constraints remain |
| R-ANIM-002 | True additive layers | §10 | partial | `AnimController3D.PlayLayerAdditive` and `CrossfadeLayerAdditive` compose `(overlay - bind_pose) * weight` onto the current base pose for masked overlay layers; Game3D additive wrappers forward and capture events | `test_rt_animcontroller3d`; `test_rt_game3d`; `g3d_openworld_slice_probe`; ABI/link guards | docs updated | focused ctest slices green | authored additive visual samples remain |
| R-ANIM-003 | Blend trees / blendspaces | §10 | partial | `BlendTree3D` owns an internal `AnimBlend3D`, computes normalized 1D/2D sample weights from finite parameters, can be passed directly to `Canvas3D.DrawMeshBlended`, and can drive `AnimController3D`/`Game3D.Animator3D` base poses | `test_rt_navmesh_blend`; `test_rt_animcontroller3d`; `test_rt_game3d`; ABI/link guards | docs updated | focused ctest slices green | higher-order blend-tree nodes remain |
| R-ANIM-004 | Retargeting + animation LOD | §10 | partial | `AnimController3D.SetAnimationLOD` batches positive-dt updates at a requested lower rate while preserving accumulated playback time; `Animation3D.Retarget` preserves clip metadata and remaps channels by bone name/index | `test_rt_animcontroller3d`; `test_rt_skeleton3d`; ABI/link guards | docs updated | focused ctest slices green | bone-count reduction and humanoid/proportional mapping remain |

## 11. Asset pipeline depth

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-TEX-001 | GPU texture compression upload + software decode | §11 | partial | `BackendSupports("bc7"/"astc"/"etc2")` names and capability bits are registered but remain false until backend upload exists; RGBA8 KTX2 per-mip fallbacks provide CPU material binding | `test_rt_canvas3d`; `test_rt_canvas3d_production`; `test_graphics3d_abi_surface` | `docs/graphics3d-guide.md`; `docs/viperlib/graphics/rendering3d.md` | focused ctest slice green | native BC/ASTC/ETC2 upload and software decode reference remain |
| R-TEX-002 | KTX2/precompressed block loading + streaming mips | §11 | partial | `TextureAsset3D.LoadKTX2` and `LoadKTX2Asset` parse KTX2 headers, expose dimensions/mips/format/compressed flags, track declared mip payload bytes via `ResidentMipStart`/`ResidentMipCount`/`ResidentBytes`, decode uncompressed RGBA8 mips into `Pixels` fallbacks, and select the active fallback from the resident mip range | `test_rt_canvas3d`; `test_graphics3d_abi_surface`; `test_rt_graphics_surface_link` | `docs/graphics3d-guide.md`; `docs/viperlib/graphics/rendering3d.md`; `api-spec.md` | focused ctest slice green | backend-native compressed block upload/residency remain |
| R-TEX-003 | glTF camera + immutable multi-scene queries/instantiation | §11 | partial | `Model3D` exposes immutable scene-indexed query/instantiate APIs over active/default and secondary glTF scenes, retaining scene-local `Camera3D` handles: `SceneCount`, `GetCameraCount`, `GetCamera`, `GetSceneName`, `InstantiateSceneAt` | `test_rt_model3d`, `test_graphics3d_abi_surface`, `test_rt_graphics_surface_link` | `docs/graphics3d-guide.md`; `docs/viperlib/graphics/rendering3d.md`; `api-spec.md` | focused ctest slice green | no mutable `SelectScene`; native compressed upload/streaming mips remain separate P11 work |
| R-TEX-004 | Optional Basis/Draco/meshopt import-depth decoders | §11 | todo |  |  |  |  | Phase 11b/stretch; not Phase 12 blocker |

## Validation (every change)

| ID | Item | Source | Status | Proof / link | Notes |
|---|---|---|---|---|---|
| R-VAL-001 | `check_runtime_completeness.sh` green | Validation | todo |  |  |
| R-VAL-002 | Build + `-L graphics3d` green mac/win/linux | Validation | todo |  | CO-1 |
| R-VAL-003 | `lint_platform_policy.sh` + `run_cross_platform_smoke.sh` | Validation | todo |  |  |
| R-VAL-004 | Before/after perf number per scale feature | Validation | todo |  | GATE-004 |
| R-VAL-005 | `runFrames` VM/native determinism per sim change | Validation | todo |  | GATE-002 |
| R-VAL-006 | Software path + capability-gated GPU per visual feature | Validation | todo |  | GATE-005 |
| R-VAL-007 | Success + negative/capability ctest per new fn, or waiver | Validation | todo |  |  |
