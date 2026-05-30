# Carryover from 3D Next Level (Phase C)

This file records the unfinished/waived tail of `misc/plans/game/3Dnextlevel/`,
verified against the codebase and that plan's own `progress/` trackers on
**2026-05-29**, then updated after the same-day Graphics3D/Game3D hardening pass.
Phase C of `roadmap.md` closes these out first, because several are hard
prerequisites for the new scale work.

## Verification method

- Enumerated every non-`done` row across the prior plan's `progress/*.md`
  (statuses `todo` / `partial` / `in-progress` / `waived`).
- Spot-checked that claimed-`done` items are real: `runtime.def` contains
  `ScreenshotFinal`/`FinalizeFrame`/`SetInputSource`/`SetDefaultLighting` (8
  matches); `g3d_test_game3d_world_probe`, `g3d_game3d_showcase`,
  `g3d_test_game3d_anim_probe`, `g3d_test_game3d_assets_probe`,
  `g3d_test_game3d_collision_probe` are registered ctests.
- Spot-checked the original open gaps: docs now distinguish frustum culling from
  CPU occlusion; `examples/3d/openworld_slice/` now commits redistributable
  `.gltf`+`.bin`, `.glb`, KTX2, WAV, terrain, and baseline fixtures with CTest
  coverage for the package-aware paths.
- Rechecked the 2026-05-29 hardening pass: `test_rt_canvas3d_gpu_paths`,
  `test_runtime_surface_cli`, and the non-display `graphics3d` ctest lane passed
  locally after RT finalization and public-header cleanup.

**Conclusion:** the prior plan is ~90% implemented; its tracker is trustworthy.
The tail is grouped below as CO-1..CO-12.

## Status legend

Same as `progress/README.md`: `todo` / `in-progress` / `blocked` / `done` /
`waived`. "Prior status" is the value found in the old tracker on 2026-05-29.

## Carryover items

| ID | Theme | Prior IDs | Prior status | New-plan role | Notes |
|---|---|---|---|---|---|
| CO-1 | **Cross-platform build + `-L graphics3d` green on macOS/Windows/Linux** | P0B-015, R-VAL-002, TEST-016, GATE-003 | partial / todo | **Prerequisite for all phases** | Local macOS `ctest -L graphics3d` is green after updating a stale CollisionEvent3D manifold-count probe; Windows/Linux must still be run through the platform build scripts. |
| CO-2 | **Managed Zia callback trampoline (VM)** | W-001, API-WORLD-022/023/025/026/027/028/029/036, API-ENT-020/021, P1-006, SHOW-001 | waived / partial / todo | Ergonomic only; may delegate to VM plan | Interpreted Zia can't pass closures to native callback loops; blocks `run(update)`/`onCollision`/`onUpdate` sugar only. Event-buffer polling and manual loops remain authoritative. |
| CO-3 | **Lifetime/ownership diagnostics + leak probes** | GATE-006, D-009, R-LIFE-012/013/017/018/020/021/022, P1-011, API-ENT-027/030, P0D-011 | done | Hardening before broad new samples | Destroyed world/entity diagnostics now include the failing API in `Game3D.<Type>.<method>: <reason>` form; double-despawn traps with ownership context; missing package assets produce terminal `AssetHandle3D.error == "asset not found"` handles; quality fallback reasons remain inspectable; imported model groups spawn/despawn repeatedly without registry retention; cache clear/reload remains usable. |
| CO-4 | **Render-target finalization contract** | R-FRAME-016, R-FRAME-018 | done / verify | Closed for public contract; verify backend capability | Existing `FinalizeFrame`/`ScreenshotFinal` are the contract; `BackendSupports("rt-finalize")` and backend probes keep unsupported paths honest. |
| CO-5 | **Implicit fallback lighting decision** | D-007, R-LIGHT-004/005/009 | done | Closed before Phase 7 lighting | Decision: no automatic fallback lights. `SetDefaultLighting()` is explicit opt-in, and `ClearLights()` plus zero ambient remains dark; covered by `test_rt_canvas3d_gpu_paths`. |
| CO-6 | **Optional getters / debug helpers** | R-GET-007, R-GET-014, R-GET-019, R-GET-020 | done | Feeds Phase 7 CSM | `Light3D.CastsShadows`, material texture-presence getters, existing `SoundListener3D` pose/activity getters, VSCN/glTF defaults, and shadow-selector coverage landed without duplicate wrappers. |
| CO-7 | **Metal robustness visual probe** | R-METAL-006 | done | Backend hardening | Metal skybox zero-vector fallback now follows Canvas3D's `-Z` camera convention. `test_rt_canvas3d_gpu_paths` probes a fake Metal frame with zero camera-forward plus a normal-mapped mesh carrying degenerate normals/tangents; `test_vgfx3d_backend_metal_shared` guards shader safe-normalization; `g3d_openworld_slice_gpu_smoke` renders the degenerate-basis pass on the real platform GPU backend when available. |
| CO-8 | **Docs cleanups** | R-DOC-001, R-DOC-003 | done / partial | Reconciled in Phase 6 | `SetFrustumCulling` is documented as frustum-only; `SetOcclusionCulling` now selects the CPU occlusion baseline. Quick-start overlay/screenshot docs remain separate polish. |
| CO-9 | **Committed fixtures + skinned-character proof** | P0D-005/006/007, P5-006, P7-009, SHOW-011, W-003 | done | Closed Phase-12 prerequisite | `examples/3d/openworld_slice/` commits redistributable skinned glTF+BIN, a minimal GLB fixture, and a WAV fixture under the package asset root. `g3d_openworld_slice_probe` loads the skinned template, crossfades `Idle` to `Wave`, binds LookAt IK, loads the GLB through `Assets3D.LoadModelAsset`, loads the WAV through `Sound3D.loadAsset`, and package dry-run covers the asset layout. |
| CO-10 | **Determinism + resize probe completeness** | R-SYN-009, P7-002, R-RESIZE-001/003 | done | Determinism baseline for Phase 0 | `World3D.runFramesOnly` now has a Game3D worker-count replay parity baseline in `test_rt_game3d`; `Canvas3D.Resize` updates backend framebuffer size and the next `Begin(camera)` projection aspect without mutating the camera, covered by `test_rt_canvas3d`. The larger job-system/VM parity gate remains Phase 1. |
| CO-11 | **Performance lane** | AC-004, W-005, P7-021, W-002 | waived | **Prerequisite for Phase 0 perf harness + all scale metrics** | Local macOS Release software and Metal baselines are recorded on Apple M4 Max, and the Release GPU smoke lane passes. Windows/Linux named-hardware runs remain external CO-11 evidence. |
| CO-12 | **Remaining Phase-1 / exit-criteria partials** | P1-014/015/016, P4-013, API-EX-001/002, API-WORLD-046, API-ENT-004, TEST-006 | done | Small finish items | Dedicated hello and starter CTests cover the common sample path; `g3d_game3d_common_no_mat4` guards common Game3D samples/probes against direct `Mat4.` calls; `test_rt_game3d` covers fixed-loop accumulator/spiral-guard behavior and packaged glTF hierarchy loading through `Assets3D.LoadModelAsset`; `g3d_test_game3d_world_probe` covers direct `Entity3D.FromNode`, synthetic `tick`, clamped `stepSimulation`, final capture, and teardown; `g3d_test_game3d_destroyed_handle_reject` covers the destroyed-world diagnostic. |

## Dependency notes

- **CO-1 and the remaining cross-platform CO-11 evidence must land first** —
  every new phase ships cross-platform and records before/after performance
  numbers.
- **CO-4** is no longer a public-API blocker after the 2026-05-29 hardening
  pass. Phase 5/6/7 still need backend capability probes before relying on
  render-target finalization for reflections, portals, and post-FX-over-
  streamed-scenes.
- **CO-9** is closed; Phase 12 now proves the package-aware
  asset→character/audio/model path at game scale.
- **CO-6 (`CastsShadows`)** feeds Phase 7 CSM; **CO-8 (occlusion alias)** is
  reconciled in Phase 6.
- **CO-2** is cross-cutting VM ergonomics, not an open-world dependency. If it
  slips, the authoritative event-buffer polling API and manual loop carry the
  new systems, exactly as in the first plan.

## Re-waivers

Items legitimately deferred again must be re-recorded in
`progress/06-waivers.md` with a fresh re-open condition, never silently dropped.
Candidates: CO-2 (if a VM trampoline is out of this milestone's scope) and the
Windows/Linux GPU/perf half of CO-11 (until CI/reference hosts exist).
