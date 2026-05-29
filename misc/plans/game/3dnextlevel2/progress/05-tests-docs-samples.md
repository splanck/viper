# Tests, docs, samples tracker

Required ctests, docs pages, fixtures, and the vertical slice from
`../roadmap.md`. All rows start `todo`.

## Required ctests (per phase)

| ID | Area | Coverage | Source | Status | Proof / link | Notes |
|---|---|---|---|---|---|---|
| TEST-C1 | Carryover | Cross-platform `-L graphics3d` green mac/win/linux | CO-1 | todo |  | prerequisite |
| TEST-C2 | Carryover | Perf lane (software FPS + GPU smoke) | CO-11 | todo |  | prerequisite |
| TEST-C3 | Carryover | Render-target finalization + skinned-character sample | CO-4/CO-9 | todo |  |  |
| TEST-C4 | Carryover | Lifetime/diagnostics + leak probes | CO-3 | todo |  |  |
| TEST-0 | Foundations | Worker-job + deterministic two-run replay; perf fixtures | Phase 0 | todo |  |  |
| TEST-1 | Concurrency | Parallel-map determinism; race/stress; pool on/off parity | Phase 1 | todo |  | GATE-002 |
| TEST-2 | Floating origin | Far/near tolerance; rebase determinism; flag-off equality | Phase 2 | todo |  |  |
| TEST-3 | Spatial index | Cull/query equality vs flat walk; scaling benchmark | Phase 3 | todo |  |  |
| TEST-4 | Streaming I/O | Hitch budget; async==blocking equality; no-leak residency | Phase 4 | todo |  |  |
| TEST-5 | World partition | Tile/cell stream in/out; seam; bounded-memory; determinism | Phase 5 | todo |  |  |
| TEST-6 | Visibility | Occlusion skip counts; LOD/impostor stability | Phase 6 | todo |  |  |
| TEST-7 | Lighting | >16-light correctness; CSM stability; fallback | Phase 7 | todo |  |  |
| TEST-8 | Physics | Stacking/rest; manifold; GJK; hinge/rope/6DOF; body-count perf | Phase 8 | todo |  |  |
| TEST-9 | Navigation | Autogen equivalence; tile rebuild; avoidance bounds; agent perf | Phase 9 | todo |  |  |
| TEST-10 | Animation | IK error bounds; additive; retarget; anim-LOD | Phase 10 | todo |  |  |
| TEST-11 | Asset pipeline | Compressed tolerance per backend; KTX2/Draco/meshopt; glTF camera/scene | Phase 11 | todo |  |  |
| TEST-12 | Vertical slice | Smoke; deterministic replay; software baseline; perf baselines | Phase 12 | todo |  |  |

## Fixtures

| ID | Fixture | Source | Status | Proof / link | Notes |
|---|---|---|---|---|---|
| FIX-001 | 10k-node scene (cull scaling) | Phase 0 | todo |  |  |
| FIX-002 | >4 km² terrain/world stand-in (streaming) | Phase 0/5 | todo |  |  |
| FIX-003 | Dense city/forest (occlusion/LOD) | Phase 6 | todo |  |  |
| FIX-004 | Many-light scene (clustered/CSM) | Phase 7 | todo |  |  |
| FIX-005 | Body-count stress (physics) | Phase 8 | todo |  |  |
| FIX-006 | Agent-count nav scene | Phase 9 | todo |  |  |
| FIX-007 | Committed skinned character + GLB/glTF-deps + audio | CO-9 | todo |  | redistributable |
| FIX-008 | Compressed-texture / KTX2 / Draco / meshopt assets | Phase 11 | todo |  |  |

## Docs

| ID | Page | Source | Status | Proof / link | Notes |
|---|---|---|---|---|---|
| DOC-001 | Concurrency + determinism model | Phase 1 | todo |  | `docs/` |
| DOC-002 | Large worlds: floating origin + streaming + partition | Phase 2/5 | todo |  |  |
| DOC-003 | Visibility + lighting scaling | Phase 6/7 | todo |  |  |
| DOC-004 | Physics depth (joints, solver, convex) | Phase 8 | todo |  |  |
| DOC-005 | Navigation + AI depth | Phase 9 | todo |  |  |
| DOC-006 | Animation depth (IK, additive, blend trees, retarget) | Phase 10 | todo |  |  |
| DOC-007 | Asset pipeline (compression, KTX2, glTF camera/scene) | Phase 11 | todo |  |  |
| DOC-008 | Update `graphics3d-architecture.md` + viperlib pages | Phase 12 | todo |  |  |
| DOC-009 | Every page has a ctest-compiled snippet | All | todo |  |  |

## Sample

| ID | Item | Source | Status | Proof / link | Notes |
|---|---|---|---|---|---|
| SLICE-001 | `examples/3d/openworld_slice/` full-stack sample | Phase 12 | todo |  |  |
| SLICE-002 | Editor runtime hooks consumed by a probe | Phase 12 | todo |  | editor in ViperIDE roadmap |
| SLICE-003 | Deterministic replay + software visual baseline | Phase 12 | todo |  |  |
