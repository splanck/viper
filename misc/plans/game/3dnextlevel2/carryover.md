# Carryover from 3D Next Level (Phase C)

This file records the unfinished/waived tail of `misc/plans/game/3Dnextlevel/`,
verified against the codebase and that plan's own `progress/` trackers on
**2026-05-29**. Phase C of `roadmap.md` closes these out first, because several
are hard prerequisites for the new scale work.

## Verification method

- Enumerated every non-`done` row across the prior plan's `progress/*.md`
  (statuses `todo` / `partial` / `in-progress` / `waived`).
- Spot-checked that claimed-`done` items are real: `runtime.def` contains
  `ScreenshotFinal`/`FinalizeFrame`/`SetInputSource`/`SetDefaultLighting` (8
  matches); `g3d_test_game3d_world_probe`, `g3d_game3d_showcase`,
  `g3d_test_game3d_anim_probe`, `g3d_test_game3d_assets_probe`,
  `g3d_test_game3d_collision_probe` are registered ctests.
- Spot-checked that gaps are genuinely open: no "deprecated" note on
  `SetOcclusionCulling` in the 3D docs (R-DOC-001 open); the only model under
  `examples/3d` is an 18-line `triangle.gltf` (no committed skinned character —
  P5-006/P7-009 partial; real FBX characters live only under
  `tests/runtime/assets/characters/`).

**Conclusion:** the prior plan is ~90% implemented; its tracker is trustworthy.
The tail is grouped below as CO-1..CO-12.

## Status legend

Same as `progress/README.md`: `todo` / `in-progress` / `blocked` / `done` /
`waived`. "Prior status" is the value found in the old tracker on 2026-05-29.

## Carryover items

| ID | Theme | Prior IDs | Prior status | New-plan role | Notes |
|---|---|---|---|---|---|
| CO-1 | **Cross-platform build + `-L graphics3d` green on macOS/Windows/Linux** | P0B-015, R-VAL-002, TEST-016, GATE-003 | partial / todo | **Prerequisite for all phases** | Prior plan proved macOS only; Core Principle #7. Use platform build scripts. |
| CO-2 | **Managed Zia callback trampoline (VM)** | W-001, API-WORLD-022/023/025/026/027/028/029/036, API-ENT-020/021, P1-006, SHOW-001 | waived / partial / todo | Dependency (may delegate to VM plan) | Interpreted Zia can't pass closures to native callback loops; blocks `run(update)`/`onCollision`/`onUpdate` sugar. Event-buffer polling remains the fallback. |
| CO-3 | **Lifetime/ownership diagnostics + leak probes** | GATE-006, D-009, R-LIFE-012/013/017/018/020/021/022, P1-011, API-ENT-027/030, P0D-011 | partial / todo | Hardening before broad new samples | Destroyed-handle, double-despawn, missing-asset, capability-fallback diagnostics; imported-group despawn + cache-clear leak probes. |
| CO-4 | **Render-target finalization contract** | R-FRAME-016, R-FRAME-018 | todo / partial | **Prerequisite for streaming/portals/reflections (Phase 5/6/7)** | Define/implement `FinalizeFrame`/`ScreenshotFinal` for render-target frames or capability-gate it. |
| CO-5 | **Implicit fallback lighting decision** | D-007, R-LIGHT-004/005/009 | todo | Close before Phase 7 lighting | If added: gate by flag, count canvas-slot + scene-node lights, add dark-scene regression. |
| CO-6 | **Optional getters / debug helpers** | R-GET-007, R-GET-014, R-GET-019, R-GET-020 | todo | `Light3D.CastsShadows` synergizes with Phase 7 CSM | Add per-light shadow toggle; material texture-presence + audio listener getters as needed; avoid redundant wrappers. |
| CO-7 | **Metal robustness visual probe** | R-METAL-006 | todo | Backend hardening | Degenerate normals/tangents + zero-length skybox vector. |
| CO-8 | **Docs cleanups** | R-DOC-001, R-DOC-003 | todo / partial | Reconciled in Phase 6 | Deprecate `SetOcclusionCulling` alias; when real occlusion lands (Phase 6) make the alias select it. Finish quick-start overlay/screenshot docs. |
| CO-9 | **Committed fixtures + skinned-character proof** | P0D-005/006/007, P5-006, P7-009, SHOW-011, W-003 | partial | **Prerequisite for Phase 12 vertical slice** | Commit small redistributable GLB / glTF-with-deps / audio / skinned character; prove skinned play/crossfade in a sample. Closes the review's proven-vs-theoretical gap. |
| CO-10 | **Determinism + resize probe completeness** | R-SYN-009, P7-002, R-RESIZE-001/003 | partial | Determinism baseline for Phase 0 | Game3D-level two-run replay equality; resize-distortion probe. |
| CO-11 | **Performance lane** | AC-004, W-005, P7-021, W-002 | waived | **Prerequisite for Phase 0 perf harness + all scale metrics** | Release/reference-hardware software FPS lane + GPU interactive-framerate smoke lane. |
| CO-12 | **Remaining Phase-1 / exit-criteria partials** | P1-014/015/016, P4-013, API-EX-001/002, API-WORLD-046, API-ENT-004, TEST-006 | partial | Small finish items | Hello-world/world_probe completeness, Mat4-free check, GLB/glTF hierarchy from package, fixed-timestep accumulator, `Entity3D.FromNode`. |

## Dependency notes

- **CO-1 and CO-11 must land first** — every new phase ships cross-platform and
  records before/after performance numbers.
- **CO-4** is needed before Phase 5/6/7 lean on render targets for streamed
  reflections, portals, and post-FX-over-streamed-scenes.
- **CO-9** is needed before Phase 12 can prove the asset→character pipeline at
  game scale.
- **CO-6 (`CastsShadows`)** feeds Phase 7 CSM; **CO-8 (occlusion alias)** is
  reconciled in Phase 6.
- **CO-2** is cross-cutting VM work; if it slips, the authoritative event-buffer
  polling API (already shipped) carries the new systems, exactly as in the first
  plan.

## Re-waivers

Items legitimately deferred again must be re-recorded in
`progress/06-waivers.md` with a fresh re-open condition, never silently dropped.
Candidates: CO-2 (if a VM trampoline is out of this milestone's scope) and the
GPU half of CO-11 (if no GPU CI lane exists yet).
