# Phase progress

Execution tracking against `../roadmap.md`. Lower-level runtime/API/test items are
tracked in sibling files. All rows start `todo`.

## Phase C — carryover from 3D Next Level

See `../carryover.md` for prior IDs and 2026-05-29 verification.

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| PC-001 | CO-1 Cross-platform build + `-L graphics3d` green (mac/win/linux) | `carryover.md` CO-1 | todo |  |  |  |  | Prerequisite for all phases |
| PC-002 | CO-11 Release/reference software FPS lane + GPU framerate smoke | `carryover.md` CO-11 | todo |  |  |  |  | Prerequisite for Phase 0 harness |
| PC-003 | CO-4 Render-target finalization contract or capability-gate | `carryover.md` CO-4 | todo |  |  |  |  | Needed by Phase 5/6/7 |
| PC-004 | CO-2 Managed Zia callback trampoline (VM) | `carryover.md` CO-2 | todo |  |  |  |  | May delegate to VM plan; else re-waive |
| PC-005 | CO-3 Lifetime/diagnostics hardening + leak probes | `carryover.md` CO-3 | todo |  |  |  |  | GATE-006/D-009 from prior plan |
| PC-006 | CO-9 Committed fixtures + skinned-character proof | `carryover.md` CO-9 | todo |  |  |  |  | Closes proven-vs-theoretical gap |
| PC-007 | CO-5 Implicit fallback lighting decision (D-007) | `carryover.md` CO-5 | todo |  |  |  |  | Close before Phase 7 |
| PC-008 | CO-6 Optional getters incl. `Light3D.CastsShadows` | `carryover.md` CO-6 | todo |  |  |  |  | Feeds Phase 7 CSM |
| PC-009 | CO-7 Metal robustness visual probe | `carryover.md` CO-7 | todo |  |  |  |  |  |
| PC-010 | CO-8 Docs cleanups (deprecate `SetOcclusionCulling`, quick-start) | `carryover.md` CO-8 | todo |  |  |  |  | Reconciled in Phase 6 |
| PC-011 | CO-10 Determinism + resize probe completeness | `carryover.md` CO-10 | todo |  |  |  |  |  |
| PC-012 | CO-12 Remaining Phase-1/exit-criteria partials | `carryover.md` CO-12 | todo |  |  |  |  | Small finish items |

## Phase 0 — foundations spike

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| P0-001 | Close determinism-under-threads contract | `roadmap.md` Phase 0 | todo |  |  |  |  | D-DET |
| P0-002 | Close job-system shape decision | `roadmap.md` Phase 0 | todo |  |  |  |  | D-JOB |
| P0-003 | Close spatial-index choice | `roadmap.md` Phase 0 | todo |  |  |  |  | D-IDX |
| P0-004 | Close floating-origin strategy | `roadmap.md` Phase 0 | todo |  |  |  |  | D-ORIG |
| P0-005 | Stand up perf harness + commit big-world fixtures | `roadmap.md` Phase 0 | todo |  |  |  |  | builds on CO-11 |
| P0-006 | Define capability/flag scheme for all new systems | `roadmap.md` Phase 0 | todo |  |  |  |  | opt-in default |
| P0-007 | `3dnext2_surface_probe`: worker job + deterministic two-run replay | `roadmap.md` Phase 0 Exit | todo |  |  |  |  |  |

## Phase 1 — concurrency runtime

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| P1-001 | Worker pool + job API + parallel-for on `rt_platform.h` | `runtime-changes.md` §1 | todo |  |  |  |  |  |
| P1-002 | Deterministic ordered-merge primitive | `runtime-changes.md` §1 | todo |  |  |  |  |  |
| P1-003 | Main-thread commit/upload queue | `runtime-changes.md` §1 | todo |  |  |  |  | used by Phase 4 |
| P1-004 | Thread-safe allocator/GC/handle paths audit | `runtime-changes.md` §1 | todo |  |  |  |  |  |
| P1-005 | Exit: parallel-map determinism + pool on/off `runFrames` parity | `roadmap.md` Phase 1 Exit | todo |  |  |  |  | GATE-002 |

## Phase 2 — floating origin

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| P2-001 | Origin rebase / per-cell local origin + camera-relative upload | `runtime-changes.md` §2 | todo |  |  |  |  |  |
| P2-002 | Promote cull bounds + physics state to precision policy | `runtime-changes.md` §2 | todo |  |  |  |  |  |
| P2-003 | Atomic rebase hook (nodes/bodies/audio/particles/queries) | `runtime-changes.md` §2 | todo |  |  |  |  |  |
| P2-004 | Exit: 50 km parity + rebase determinism + flag-off byte-equality | `roadmap.md` Phase 2 Exit | todo |  |  |  |  | AC-002 |

## Phase 3 — spatial acceleration index

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| P3-001 | Index over scene graph, incremental refit | `runtime-changes.md` §3 | todo |  |  |  |  |  |
| P3-002 | Replace draw walk with index query (old path behind flag) | `runtime-changes.md` §3 | todo |  |  |  |  |  |
| P3-003 | Route queries + physics broadphase through index (per Phase 0) | `runtime-changes.md` §3 | todo |  |  |  |  |  |
| P3-004 | Exit: cull/query equality vs flat walk + recorded scaling | `roadmap.md` Phase 3 Exit | todo |  |  |  |  | AC-003 |

## Phase 4 — async asset + GPU upload

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| P4-001 | Worker decode + main-thread upload budget | `runtime-changes.md` §4 | todo |  |  |  |  |  |
| P4-002 | `LoadModelAsync` + residency + eviction | `runtime-changes.md` §4 | todo |  |  |  |  |  |
| P4-003 | Texture/mesh mip/LOD residency hooks | `runtime-changes.md` §4 | todo |  |  |  |  |  |
| P4-004 | Exit: hitch budget + async==blocking equality + no-leak residency | `roadmap.md` Phase 4 Exit | todo |  |  |  |  | AC-004 |

## Phase 5 — world partition + terrain streaming

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| P5-001 | Terrain tile grid + LOD-seam stitching (lift 4096² cap) | `runtime-changes.md` §5 | todo |  |  |  |  |  |
| P5-002 | Scene cells: subtree + physics + nav load/unload | `runtime-changes.md` §5 | todo |  |  |  |  |  |
| P5-003 | Streamed container (VSCN extension/side-format) + bake hook | `runtime-changes.md` §5 | todo |  |  |  |  |  |
| P5-004 | Per-tile heightfield colliders | `runtime-changes.md` §5 | todo |  |  |  |  |  |
| P5-005 | Exit: >4 km² stream in/out, bounded memory, no seams, determinism | `roadmap.md` Phase 5 Exit | todo |  |  |  |  | AC-005 |

## Phase 6 — visibility scaling

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| P6-001 | Occlusion culling (software depth / portal-PVS) over index | `runtime-changes.md` §6 | todo |  |  |  |  |  |
| P6-002 | Automatic LOD + HLOD/impostors | `runtime-changes.md` §6 | todo |  |  |  |  |  |
| P6-003 | Reconcile `SetOcclusionCulling` alias (CO-8) | `runtime-changes.md` §6 | todo |  |  |  |  |  |
| P6-004 | Exit: occluded-skip counts + LOD stability | `roadmap.md` Phase 6 Exit | todo |  |  |  |  | AC-006 |

## Phase 7 — lighting scaling

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| P7-001 | Clustered/forward+ light culling | `runtime-changes.md` §7 | todo |  |  |  |  |  |
| P7-002 | Cascaded shadow maps + `CastsShadows` | `runtime-changes.md` §7 | todo |  |  |  |  | uses CO-6 |
| P7-003 | Capability fallback to 16-light forward | `runtime-changes.md` §7 | todo |  |  |  |  |  |
| P7-004 | Exit: >16-light correctness + CSM stability | `roadmap.md` Phase 7 Exit | todo |  |  |  |  | AC-007 |

## Phase 8 — physics depth

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| P8-001 | Contact manifolds + iterated warm-started solver | `runtime-changes.md` §8 | todo |  |  |  |  |  |
| P8-002 | BVH/index-driven mesh narrow-phase | `runtime-changes.md` §8 | todo |  |  |  |  |  |
| P8-003 | GJK/EPA convex-vs-convex | `runtime-changes.md` §8 | todo |  |  |  |  |  |
| P8-004 | Hinge/rope/6DOF joints (close doc overclaim) | `runtime-changes.md` §8 | todo |  |  |  |  |  |
| P8-005 | Exit: stacking/rest + body-count target + joints + determinism | `roadmap.md` Phase 8 Exit | todo |  |  |  |  | AC-008 |

## Phase 9 — navigation + AI depth

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| P9-001 | Navmesh auto-generation (voxelize → mesh) | `runtime-changes.md` §9 | todo |  |  |  |  |  |
| P9-002 | Tiled/streamable navmesh + runtime tile rebuild + carving | `runtime-changes.md` §9 | todo |  |  |  |  |  |
| P9-003 | Off-mesh links + `agent_radius` corridors | `runtime-changes.md` §9 | todo |  |  |  |  |  |
| P9-004 | Local avoidance + pathfinding acceleration | `runtime-changes.md` §9 | todo |  |  |  |  |  |
| P9-005 | Exit: autogen equivalence + tile rebuild + agent-count target | `roadmap.md` Phase 9 Exit | todo |  |  |  |  | AC-009 |

## Phase 10 — animation depth

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| P10-001 | IK: two-bone + look-at + FABRIK | `runtime-changes.md` §10 | todo |  |  |  |  |  |
| P10-002 | True additive layers | `runtime-changes.md` §10 | todo |  |  |  |  |  |
| P10-003 | Blend trees / blendspaces | `runtime-changes.md` §10 | todo |  |  |  |  |  |
| P10-004 | Cross-skeleton retargeting + animation LOD | `runtime-changes.md` §10 | todo |  |  |  |  |  |
| P10-005 | Exit: IK error bounds + additive + retarget + anim-LOD | `roadmap.md` Phase 10 Exit | todo |  |  |  |  | AC-010 |

## Phase 11 — asset pipeline depth

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| P11-001 | GPU texture compression upload + software decode reference | `runtime-changes.md` §11 | todo |  |  |  |  |  |
| P11-002 | KTX2 + Basis transcode (from scratch) + streaming mips | `runtime-changes.md` §11 | todo |  |  |  |  |  |
| P11-003 | glTF camera import + multi-scene + Draco/meshopt decode | `runtime-changes.md` §11 | todo |  |  |  |  |  |
| P11-004 | Exit: compressed tolerance per backend + import coverage | `roadmap.md` Phase 11 Exit | todo |  |  |  |  | AC-011 |

## Phase 12 — open-world vertical slice + tooling + docs

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| P12-001 | `examples/3d/openworld_slice/` full-stack sample | `roadmap.md` Phase 12 | todo |  |  |  |  |  |
| P12-002 | Editor runtime hooks (cells/residency/navmesh bake/perf counters) | `roadmap.md` Phase 12 | todo |  |  |  |  | editor in ViperIDE roadmap |
| P12-003 | Docs for every new system + ctest-compiled snippets | `roadmap.md` Phase 12 | todo |  |  |  |  |  |
| P12-004 | Recorded open-world perf baselines cross-platform | `roadmap.md` Phase 12 | todo |  |  |  |  |  |
| P12-005 | Exit: slice streams/renders/simulates/replays; pipeline proven at scale | `roadmap.md` Phase 12 Exit | todo |  |  |  |  | AC-012 |
