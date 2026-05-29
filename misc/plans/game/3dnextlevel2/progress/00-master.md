# Master progress

Tracks global readiness for the 3D scale tier. Detailed work items live in the
sibling files. All rows start `todo`; this is a fresh plan.

## Overall gates

| ID | Gate | Source | Status | Proof / link | Notes |
|---|---|---|---|---|---|
| GATE-001 | Carryover (Phase C) closed before broad new work | `carryover.md`, `roadmap.md` Phase C | todo |  | CO-1 + CO-11 are hard prerequisites |
| GATE-002 | Determinism preserved: `runFrames` parity with job pool on/off, VM/native | `runtime-changes.md` §1, `roadmap.md` Phase 0/1 | todo |  | Core Principle #5 |
| GATE-003 | Every phase green on macOS/Windows/Linux (`-L graphics3d`) | `roadmap.md` Testing | todo |  | Core Principle #7; CO-1 first |
| GATE-004 | Performance harness records before/after for every scale feature | `roadmap.md` Phase 0 | todo |  | CO-11 perf lane is the seed |
| GATE-005 | Software-backend correctness baseline for each new visual feature | `README.md` §6 | todo |  | GPU parity capability-gated + smoke |
| GATE-006 | All systems opt-in; bounded-scene path byte-unchanged with flags off | `README.md` §6 | todo |  | No-regression contract |
| GATE-007 | Zero new dependencies; from-scratch implementations | `README.md` §5 | todo |  | Core Principle #6 |
| GATE-008 | `check_runtime_completeness.sh` green + docs+ctest per new API | `runtime-changes.md` Validation | todo |  |  |
| GATE-009 | Any IL/VM-touching change has an ADR | `README.md` §5 | todo |  | Core Principle #1 |

## Phase status

| ID | Phase | Status | Entry dependency | Exit proof / link | Notes |
|---|---|---|---|---|---|
| PHASE-C | Carryover: close out 3D Next Level | todo | Prior plan ~90% done | — | CO-1..CO-12; see `carryover.md` |
| PHASE-0 | Foundations spike (determinism/threading/index/perf) | todo | PHASE-C prerequisites | — | Closes `02-decisions.md` |
| PHASE-1 | Concurrency runtime (job system) | todo | PHASE-0 decisions | — | Keystone |
| PHASE-2 | Floating origin | todo | PHASE-0 strategy | — | Before streaming |
| PHASE-3 | Spatial acceleration index | todo | PHASE-0 choice | — | Shared substrate |
| PHASE-4 | Async asset + GPU-upload streaming | todo | PHASE-1 | — | Non-blocking loads |
| PHASE-5 | World partition + terrain streaming | todo | PHASE-1/3/4, PHASE-2 | — | Defining open-world milestone |
| PHASE-6 | Visibility scaling (occlusion + auto-LOD/HLOD) | todo | PHASE-3 | — | Reconciles CO-8 |
| PHASE-7 | Lighting scaling (clustered + CSM) | todo | PHASE-3; CO-6 | — | >16 lights |
| PHASE-8 | Physics depth | todo | PHASE-3 | — | May pull earlier (SEQ) |
| PHASE-9 | Navigation + AI depth | todo | PHASE-3/5 | — | May pull earlier (SEQ) |
| PHASE-10 | Animation depth (IK, additive, blend trees, retarget) | todo | CO-9 | — | Characters on terrain |
| PHASE-11 | Asset pipeline depth (compression, KTX2, glTF camera/scene) | todo | PHASE-4 | — | VRAM/RAM + import |
| PHASE-12 | Open-world vertical slice + tooling + docs + baselines | todo | All API areas usable | — | Proves end-to-end |

## Acceptance criteria

| ID | Criterion | Status | Proof / link | Notes |
|---|---|---|---|---|
| AC-001 | Determinism preserved with job pool on/off, VM/native | todo |  |  |
| AC-002 | Scene 50 km from origin renders/simulates without jitter beyond tolerance | todo |  |  |
| AC-003 | Frame cull scales with visible (not total) node count; recorded speedup | todo |  | 10k-node fixture |
| AC-004 | Streaming stays within bounded per-frame budget; recorded hitch delta | todo |  |  |
| AC-005 | >4 km² streamed world: bounded memory, no seams | todo |  |  |
| AC-006 | Occlusion + LOD cut draw calls/fill by recorded factor, no missing geo | todo |  |  |
| AC-007 | >16 lights render correctly (clustered); CSM stable | todo |  |  |
| AC-008 | Boxes stack/pile rests; body-count target met; hinge/rope/6DOF + true convex | todo |  |  |
| AC-009 | Navmesh auto-bakes + tile-rebuilds; agent-count target paths + avoids | todo |  |  |
| AC-010 | IK foot-plant + look-at; additive layers, blend trees, retargeting | todo |  |  |
| AC-011 | Compressed textures per backend (recorded VRAM); glTF camera/scene/Draco/meshopt | todo |  |  |
| AC-012 | `examples/3d/openworld_slice/` demonstrates the full stack + replay | todo |  |  |
| AC-013 | Cross-platform + completeness + docs/ctest per API every phase | todo |  |  |
| AC-014 | No regression: bounded-scene game byte-unchanged with flags off | todo |  |  |

## Current readiness notes

- Nothing implemented yet. Phase C must close CO-1 (cross-platform) and CO-11
  (perf lane) before Phase 0 can record meaningful baselines.
- Sequencing override (pull Phase 8/9 earlier) is decision SEQ in
  `02-decisions.md`.
