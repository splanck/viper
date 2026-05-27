# Master progress

This file tracks global readiness. Detailed work items live in the other
tracker files.

## Overall gates

| ID | Gate | Source | Status | Proof / link | Notes |
|---|---|---|---|---|---|
| GATE-001 | Game3D is runtime-first C implementation, not a Zia source package | `README.md` §2/§5 | done | D-001 closed; `zia-feasibility.md` runtime-first scope | Must hold for every API area |
| GATE-002 | Public namespace decision closed before broad `runtime.def` additions | `README.md` §8, `roadmap.md` Phase 0A | done | D-001 closed; `g3d_game3d_surface_probe` passed | Use `Viper.Game3D.*` |
| GATE-003 | Frame finalization and final screenshot contract proven before ergonomic API | `runtime-changes.md` §1, `roadmap.md` Phase 0B | partial | C contract tests added; `walk_min` visual baseline added; `ctest --test-dir build -L graphics3d` 42/42 passed | Cross-platform build/test proof still pending |
| GATE-004 | Synthetic input/clock proven before deterministic gameplay tests | `runtime-changes.md` §7, `roadmap.md` Phase 0C | partial | `test_rt_canvas3d_gpu_paths`; `g3d_test_canvas3d_synthetic_input`; `g3d_test_game3d_runframes_probe`; graphics3d 42/42 | Canvas3D contract and Game3D `runFramesOnly` timing proven; full gameplay-state replay remains later |
| GATE-005 | Package-aware model/audio asset loading proven before showcase/starter | `runtime-changes.md` §8, `roadmap.md` Phase 0D | partial | `test_rt_gltf`, `test_rt_model3d`, `test_rt_audio_integration`, `g3d_test_model3d_load_asset` | Runtime model/audio loading proven; Game3D `Assets3D` cache wrapper still pending |
| GATE-006 | Ownership/lifetime semantics documented and tested before broad samples | `runtime-changes.md` §10 | partial | `test_rt_game3d`; `g3d_test_game3d_world_probe`; `docs/viperlib/graphics/game3d.md` | Core world/entity ownership covered; imported templates, effects, animation, and richer diagnostics remain later |
| GATE-007 | Every public runtime function has ctest coverage or explicit waiver | `runtime-changes.md` Validation | partial | Phase 0B/0C runtime functions and Phase 1 core Game3D functions covered by focused C/Zia tests | Future API areas pending; interpreted-Zia callback loop support explicitly deferred |
| GATE-008 | Every docs snippet that claims to compile is compiled/imported by ctest | `roadmap.md` Testing strategy | todo |  | Includes quick starts |
| GATE-009 | Starter project can run, package assets, and execute deterministic test | `roadmap.md` Phase 7 | todo |  | Code-first onboarding gate |
| GATE-010 | Showcase demonstrates the full Game3D stack in one playable sample | `roadmap.md` Phase 7 | todo |  | Not a substitute for starter |

## Phase status

| ID | Phase | Status | Entry dependency | Exit proof / link | Notes |
|---|---|---|---|---|---|
| PHASE-0A | Runtime surface confirmation spike | done | Plan approved | `g3d_game3d_surface_probe` passed; `zia-feasibility.md` Confirm-0A rows resolved; D-001/D-002/D-003 closed | Namespace, event model, sample syntax |
| PHASE-0B | Runtime final-frame contract and visual baseline | in_progress | PHASE-0A decisions where needed | C runtime contract tests, `walk_min` visual baseline, graphics3d regression pass | Windows/Linux proof still pending |
| PHASE-0C | Deterministic test input, clock, and backend-safe quality | done | PHASE-0B for final-frame probes | Synthetic input/clock and quality ctests passed; graphics3d 38/38 | Game3D `runFrames` and Debug3D overlay wrappers remain later-phase work |
| PHASE-0D | Package-aware model/audio asset loading | in_progress | Asset resolver decision | GLB filesystem+mounted, glTF external deps, audio mounted-pack ctests passed | `Assets3D.LoadModelTemplate` cache/leak row remains for Phase 4 runtime asset wrapper |
| PHASE-1 | Core Game3D world/entity/input/masks | partial | Phase 0 contracts | `test_rt_game3d`; `g3d_test_game3d_world_probe`; `g3d_test_game3d_runframes_probe`; callback rejection probe; graphics3d 42/42 | Core C runtime layer is implemented; docs-snippet and richer negative diagnostics remain |
| PHASE-2 | Cameras and walkable movement | todo | Core world/input |  | Two-phase controller |
| PHASE-3 | Presets, prefabs, quality, environment, debug | todo | Core world + quality caps |  | Good-looking defaults |
| PHASE-4 | Assets, physics, and collision events | todo | Core entity + asset resolver |  | Runtime event buffers |
| PHASE-5 | Animation | todo | Asset/model + event strategy |  | Root motion determinism |
| PHASE-6 | 3D audio and VFX | todo | World/entity + asset resolver |  | Effect lifetime |
| PHASE-7 | Showcase, docs, starter, and bowling migration | todo | All API areas usable |  | Product-readiness phase |

## Acceptance criteria

| ID | Criterion | Status | Proof / link | Notes |
|---|---|---|---|---|
| AC-001 | Hello-world <= 20 source lines renders a lit, walkable scene | todo |  | Excludes blanks/comments |
| AC-002 | Zero `Mat4.` calls in hello-world, open-world, and common placement code | todo |  | Raw `Mat4` remains escape hatch |
| AC-003 | Bowling non-gameplay scaffolding reduced >= 50%; before/after LOC recorded | todo |  | Baseline: 789 lines listed in roadmap |
| AC-004 | Hello-world reaches >= 30 FPS at 1280x720 on software backend reference hardware | todo |  | Record CPU/build/backend |
| AC-005 | Fixed-step synthetic input/clock yields exact matching simulation state | partial | `g3d_test_canvas3d_synthetic_input`; `g3d_test_game3d_runframes_probe` | Canvas3D deterministic camera state and Game3D fixed timing proven; full gameplay-state replay pending |
| AC-006 | Software-backend final frame matches visual baseline within tolerance | partial | `g3d_walk_min_visual_probe` | Phase 0B baseline has structural region assertions; final showcase baseline still pending |
| AC-007 | Overlay pixels are not bloomed or tonemapped | done | `g3d_walk_min_visual_probe` | Crisp HUD gate covered by final-overlay swatch |
| AC-008 | Same model loads from filesystem and mounted package path with glTF deps | partial | `test_rt_gltf`, `test_rt_model3d`, `g3d_test_model3d_load_asset` | GLB filesystem+mounted and glTF external deps/missing diagnostics proven; full Game3D asset wrapper still pending |
| AC-009 | `examples/3d/game3d_showcase/` demonstrates full stack | todo |  | See `05-tests-docs-samples.md` |
| AC-010 | Starter workflow can run, package, and ctest a new 3D game | todo |  | `game3d_starter` or template |
| AC-011 | Every runtime function/API area has docs and compiling snippets covered by ctest | todo |  | Track waivers explicitly |
| AC-012 | Each subsystem ships a `*_probe.zia` ctest and verifies escape hatches | todo |  | Cameras, presets, assets, physics, animation, audio, VFX, environment |

## Current readiness notes

- Phase 1 core runtime work is now represented in the tracker, but the overall
  objective remains incomplete until cameras, presets, assets, physics events,
  animation, audio/VFX, starter, docs snippets, and the showcase are done.
- Do not move an acceptance criterion to `done` unless its detailed tracker rows
  are also complete.
