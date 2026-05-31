# Decision log

Cross-cutting decisions that must be closed or waived before the affected work
moves from design to broad implementation. See `../README.md` §8.

## Open decisions

No open decisions remain for the currently approved 3D next-level scope. New
scope changes should add rows here before implementation starts.

## Closed decisions

| ID | Decision | Outcome | Proof / link | Date | Notes |
|---|---|---|---|---|---|
| D-DET | Determinism-under-threads contract | Simulation remains deterministically scheduled. Worker threads perform throughput-only work; any result that affects visible/runtime state is merged in fixed order. | `g3d_3dnext2_surface_probe`; `test_rt_game3d` worker-count replay parity | 2026-05-29 | No IL/VM semantics changed. VM callback-loop sugar remains waived under D-CB. |
| D-JOB | Job-system shape | Reuse the existing `Viper.Threads.Pool` / `Viper.Threads.Parallel` runtime (`rt_threadpool` + `rt_parallel`) on `rt_platform.h`; do not add a second 3D-only job API. `World3D` keeps small worker controls only. | `g3d_3dnext2_surface_probe`; `test_rt_parallel`; `test_rt_parallel_reduce`; `test_rt_threadpool` | 2026-05-29 | Remaining implementation work is domain queues/upload integration, not architecture. |
| D-IDX | Spatial-index choice + scope | Use the current internal Scene3D indexed candidate store as the scene cull/query contract. Physics broadphase remains a sibling until shared-tree parity and perf are proven. | `test_rt_scene3d`; `progress/03-runtime-contracts.md` R-IDX rows | 2026-05-29 | Avoids coupling physics solver correctness to renderer index changes. |
| D-ORIG | Floating-origin strategy | Default to periodic active-world rebase through `World3D.floatingOrigin` and `Scene3D.RebaseOrigin`; camera-relative/per-cell upload remains a backend refinement under Phase 2, not a Phase-0 blocker. | `test_rt_game3d`; `test_rt_scene3d`; `progress/03-runtime-contracts.md` R-ORIG rows | 2026-05-29 | Off by default; bounded-scene behavior remains unchanged. |
| D-STREAM | Streaming granularity + container | Use VSCN manifests for cells and tiled terrain; optional binary/height sidecars are payload storage only, not a new general scene container. | `test_rt_game3d`; `g3d_openworld_slice_probe`; `g3d_openworld_slice_long_traversal` | 2026-05-29 | Current open-world fixture uses far-apart cells/terrain tiles with staged `update(dt)` load budgeting. |
| D-LIGHT | Lighting path | Keep forward shaders and add capability-gated clustered/forward+ behavior; do not switch to deferred rendering for this plan. | `test_rt_canvas3d`; `test_rt_canvas3d_gpu_paths`; `g3d_openworld_slice_gpu_smoke`; `progress/03-runtime-contracts.md` R-LIT rows | 2026-05-29 | Software many-light baseline remains the correctness reference; real GPU backends now advertise the bounded 64-light path and the local Metal Release smoke records 24 configured lights. |
| D-TEX | Texture-compression scope | KTX2/precompressed block upload comes first, with software RGBA fallback. Basis supercompression/encoders are outside the approved core scope unless Phase 11b is explicitly approved. | `test_rt_canvas3d`; `test_rt_canvas3d_production`; `g3d_openworld_slice_probe` | 2026-05-29 | Keeps Phase 12 unblocked by from-scratch supercodec work. |
| D-SEQ | Sequencing override | Keep dependency order as the default. Targeted Phase 8/9 slices may land early only when they do not bypass Phase 0/3 contracts. | `progress/00-master.md`; `progress/01-phase-progress.md` | 2026-05-29 | Hard edges remain fixed: cross-platform/perf evidence, determinism, API naming, and capability gates. |
| D-MS | v0.3.x milestone scope | Treat the plan as capability-driven rather than tied to a version label in these trackers. Phases 0–5 prove the small streamed-world base; Phases 6–12 raise scale/fidelity. | `roadmap.md`; `progress/00-master.md` | 2026-05-29 | Release/version ownership stays outside this implementation tracker. |
| D-CB | Carryover CO-2 ownership | Re-waive managed Zia callback trampoline for this milestone; manual loops, `runFramesOnly`, event buffers, and handle polling are authoritative. | `progress/06-waivers.md` W2-001; `zia-feasibility.md` | 2026-05-29 | No runtime surface depends on callback sugar. |
| D-ASSET-STRETCH | Asset import stretch scope | Draco/meshopt remain optional Phase 11b/import-depth work, not approved core scope or a Phase 12 gate. | `progress/06-waivers.md` W2-004; `test_rt_model3d`; `test_rt_canvas3d` | 2026-05-29 | Ordinary glTF, KTX2/precompressed metadata, and software fallback cover the current vertical slice. |
| D-API-NAMING | New API naming convention | `Viper.Graphics3D` keeps PascalCase; stateful `Viper.Game3D` keeps existing lower/camel style; static Game3D helper namespaces keep PascalCase factories/actions | `../review.md`, `../api-spec.md` | 2026-05-29 | New public classes still use fully qualified `Viper.*` names and append class ids |
| D-007 | Implicit fallback lighting | No automatic fallback lights. `Canvas3D.SetDefaultLighting()` is explicit opt-in setup; `ClearLights()` plus zero ambient stays dark. | `test_rt_canvas3d_gpu_paths` explicit-dark regression | 2026-05-29 | Avoids hidden draw-state mutation and keeps dark-scene authoring stable |
| D-MATERIAL-TEX | Texture asset material binding | Extend existing `Material3D.SetTexture` / `Set*Map` methods to accept `TextureAsset3D`; do not add duplicate `Set*Asset` methods | `../review.md`, `../api-spec.md` | 2026-05-29 | Keeps public surface small |
| D-MODEL-SCENE | glTF multi-scene API | Keep cached `Model3D` immutable; use scene-indexed camera queries and `InstantiateSceneAt(index)`, not mutable `SelectScene` | `../review.md`, `../api-spec.md` | 2026-05-29 | Avoids shared-cache state surprises |

## Decision update rules

- Do not delete closed decisions; move them to "Closed decisions" with proof.
- If an outcome changes, add a new row referencing the old decision.
- Any `blocked` tracker item must name the blocking decision ID.
