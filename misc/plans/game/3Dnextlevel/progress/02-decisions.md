# Decision log

These decisions must be closed or explicitly waived before the affected work
can move from design to broad implementation.

## Open decisions

| ID | Decision | Source | Status | Options / default | Owner | Due before | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| D-007 | Implicit fallback lighting | `README.md` §8, `runtime-changes.md` §2 | todo | Default explicit-only; later automatic fallback only with disable flag |  | Phase 3 |  | Must not surprise intentionally dark scenes |
| D-009 | Runtime object lifetime policy | `runtime-changes.md` §10 | partial | Explicit destroy/despawn invalidation and diagnostics |  | Phase 4/7 | `test_rt_game3d`; `g3d_test_game3d_assets_probe`; `docs/viperlib/graphics/game3d.md` | Core world/entity ownership and imported template groups covered; effects/animation diagnostics still open |
| D-010 | Starter project delivery mechanism | `roadmap.md` Phase 7 | todo | `examples/3d/game3d_starter/` vs project-template generator |  | Phase 7 |  | Must be code-first and package-aware |
| D-011 | Software visual baseline storage/update policy | `roadmap.md` Testing strategy | todo | Baseline artifact path, tolerance file, update command |  | Phase 0B |  | Prevent brittle visual tests |
| D-012 | Test waiver location | `roadmap.md` Required ctest inventory | todo | Roadmap row, issue tracker, or dedicated waiver file |  | First waiver |  | No untracked coverage gaps |

## Closed decisions

| ID | Decision | Outcome | Proof / link | Date | Notes |
|---|---|---|---|---|---|
| D-001 | Public runtime namespace | Use `Viper.Game3D.*` implemented through `runtime.def` like other runtime namespaces; do not create a source-level Zia package as the authoritative API. | `tests/runtime/game3d_surface_probe.zia:4,42` proves alias/call syntax on `Viper.Graphics3D`; `zia-feasibility.md` | 2026-05-27 | First Game3D symbols must be C runtime entries. |
| D-002 | Authoritative event model | Collision and animation events are runtime-owned, pollable buffers. Optional callbacks are convenience only. | `api-spec.md` Conventions; `zia-feasibility.md` Phase 0A scope | 2026-05-27 | Keeps deterministic tests and non-callback consumers first-class. |
| D-003 | Zia void function-type spelling | Use `Unit` for no-return function types, e.g. `(Float) -> Unit`. | `tests/runtime/game3d_surface_probe.zia:37,52-55`; ctest `g3d_game3d_surface_probe` | 2026-05-27 | `(Float) -> Void` mismatched block lambdas during the spike. |
| D-004 | Final overlay API names | Keep `Canvas3D.BeginOverlay`, `EndOverlay`, and `ClearOverlay`. | `runtime.def`; `test_rt_canvas3d_gpu_paths`; `docs/graphics3d-guide.md` | 2026-05-27 | Names match implemented runtime surface. |
| D-005 | Finalization API shape | Expose public `Canvas3D.FinalizeFrame`; `ScreenshotFinal` finalizes without presenting; `Flip` finalizes/presents idempotently. | `runtime.def`; `test_rt_canvas3d_gpu_paths`; `docs/viperlib/graphics/rendering3d.md` | 2026-05-27 | Direct `ScreenshotFinal` then `Flip` integration test still tracked separately. |
| D-006 | Asset resolver API shape | Use runtime-level `LoadAsset` APIs (`Model3D.LoadAsset`, `GLTF.LoadAsset`, `Sound.LoadAsset`) with `asset://` support and filesystem fallback for development. | `test_rt_gltf`; `test_rt_model3d`; `test_rt_audio_integration`; `g3d_test_model3d_load_asset`; `g3d_test_game3d_assets_probe` | 2026-05-27 | Game3D `Assets3D` wrappers use the same runtime asset resolver path. |
| D-008 | Runtime callback bridge support scope | Game3D callback-loop methods are native callback APIs for now. Interpreted Zia uses manual frame APIs or `runFramesOnly`; passing managed Zia function references traps with a clear diagnostic until a VM callback trampoline exists. | `test_rt_game3d`; `g3d_test_game3d_runframes_callback_reject`; `docs/viperlib/graphics/game3d.md` | 2026-05-27 | Event buffers remain authoritative; optional managed callback sugar is deferred. |

## Decision update rules

- Do not delete closed decisions. Move them to "Closed decisions" with proof.
- If an outcome changes, add a new row referencing the old decision.
- Any `blocked` tracker item should name the blocking decision ID.
