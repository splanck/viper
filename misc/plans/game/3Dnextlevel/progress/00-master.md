# Master progress

This file tracks global readiness. Detailed work items live in the other
tracker files.

## Overall gates

| ID | Gate | Source | Status | Proof / link | Notes |
|---|---|---|---|---|---|
| GATE-001 | Game3D is runtime-first C implementation, not a Zia source package | `README.md` §2/§5 | done | D-001 closed; `zia-feasibility.md` runtime-first scope | Must hold for every API area |
| GATE-002 | Public namespace decision closed before broad `runtime.def` additions | `README.md` §8, `roadmap.md` Phase 0A | done | D-001 closed; `g3d_game3d_surface_probe` passed | Use `Viper.Game3D.*` |
| GATE-003 | Frame finalization and final screenshot contract proven before ergonomic API | `runtime-changes.md` §1, `roadmap.md` Phase 0B | partial | C contract tests added; `walk_min` visual baseline added; `ctest --test-dir build -L graphics3d` 49/49 passed | Cross-platform build/test proof still pending |
| GATE-004 | Synthetic input/clock proven before deterministic gameplay tests | `runtime-changes.md` §7, `roadmap.md` Phase 0C | partial | `test_rt_canvas3d_gpu_paths`; `g3d_test_canvas3d_synthetic_input`; `g3d_test_game3d_runframes_probe`; `g3d_walk_min_visual_probe`; graphics3d 49/49 | Canvas3D contract, Game3D `runFramesOnly` timing, and sample grounded movement proven; full showcase gameplay-state replay remains later |
| GATE-005 | Package-aware model/audio asset loading proven before showcase/starter | `runtime-changes.md` §8, `roadmap.md` Phase 0D | partial | `test_rt_gltf`, `test_rt_model3d`, `test_rt_audio_integration`, `g3d_test_model3d_load_asset`, `g3d_test_game3d_assets_probe` | Runtime model/audio loading proven; Game3D `Assets3D` filesystem/asset wrapper and template cache covered. Starter/showcase package path still needs final integration proof |
| GATE-006 | Ownership/lifetime semantics documented and tested before broad samples | `runtime-changes.md` §10 | partial | `test_rt_game3d`; `g3d_test_game3d_world_probe`; `docs/viperlib/graphics/game3d.md` | Core world/entity ownership covered; imported templates, effects, animation, and richer diagnostics remain later |
| GATE-007 | Every public runtime function has ctest coverage or explicit waiver | `runtime-changes.md` Validation | partial | Phase 0B/0C runtime functions plus Phase 1 core, Phase 2 controller, Phase 3 preset/environment/debug, Phase 4 assets/physics/collision, and Phase 5 animation Game3D functions covered by focused C/Zia tests | Future API areas pending; interpreted-Zia callback loop plus collision/animation callback sugar explicitly deferred |
| GATE-008 | Every docs snippet that claims to compile is compiled/imported by ctest | `roadmap.md` Testing strategy | todo |  | Includes quick starts |
| GATE-009 | Starter project can run, package assets, and execute deterministic test | `roadmap.md` Phase 7 | todo |  | Code-first onboarding gate |
| GATE-010 | Showcase demonstrates the full Game3D stack in one playable sample | `roadmap.md` Phase 7 | todo |  | Not a substitute for starter |

## Phase status

| ID | Phase | Status | Entry dependency | Exit proof / link | Notes |
|---|---|---|---|---|---|
| PHASE-0A | Runtime surface confirmation spike | done | Plan approved | `g3d_game3d_surface_probe` passed; `zia-feasibility.md` Confirm-0A rows resolved; D-001/D-002/D-003 closed | Namespace, event model, sample syntax |
| PHASE-0B | Runtime final-frame contract and visual baseline | in_progress | PHASE-0A decisions where needed | C runtime contract tests, `walk_min` visual baseline, graphics3d regression pass | Windows/Linux proof still pending |
| PHASE-0C | Deterministic test input, clock, and backend-safe quality | done | PHASE-0B for final-frame probes | Synthetic input/clock and quality ctests passed; graphics3d 49/49 | Game3D `runFrames` and Debug3D wrappers now implemented in later phases |
| PHASE-0D | Package-aware model/audio asset loading | done | Asset resolver decision | GLB filesystem+mounted, glTF external deps, audio mounted-pack ctests, and Game3D `Assets3D` wrapper probes passed | Starter/showcase mounted-package integration proof remains under Phase 7 |
| PHASE-1 | Core Game3D world/entity/input/masks | partial | Phase 0 contracts | `test_rt_game3d`; `g3d_test_game3d_world_probe`; `g3d_test_game3d_runframes_probe`; callback rejection probe; graphics3d 49/49 | Core C runtime layer is implemented; docs-snippet and richer negative diagnostics remain |
| PHASE-2 | Cameras and walkable movement | done | Core world/input | `test_rt_game3d`; `g3d_test_game3d_camera_controllers_probe`; `g3d_test_game3d_character_controller_probe`; `g3d_walk_min_visual_probe` | Built-in controllers, character movement, and grounded `walk_min` migration covered |
| PHASE-3 | Presets, prefabs, quality, environment, debug | done | Core world + quality caps | `test_rt_game3d`; `g3d_test_game3d_presets_probe`; docs updated | Good-looking defaults and final-overlay debug path covered |
| PHASE-4 | Assets, physics, and collision events | done | Core entity + asset resolver | `test_rt_game3d`; `g3d_test_game3d_assets_probe`; `g3d_test_game3d_physics_probe`; `g3d_test_game3d_collision_probe`; Game3D docs | Runtime event buffers, BodyDef attach path, Assets3D templates, and callback-sugar waiver covered |
| PHASE-5 | Animation | done | Asset/model + event strategy | `test_rt_game3d`; `g3d_test_game3d_anim_probe`; Game3D docs | Runtime `Animator3D`, state time, event buffers, entity attachment, and root-motion sync covered |
| PHASE-6 | 3D audio and VFX | todo | World/entity + asset resolver |  | Effect lifetime |
| PHASE-7 | Showcase, docs, starter, and bowling migration | todo | All API areas usable |  | Product-readiness phase |

## Acceptance criteria

| ID | Criterion | Status | Proof / link | Notes |
|---|---|---|---|---|
| AC-001 | Hello-world <= 20 source lines renders a lit, walkable scene | todo |  | Excludes blanks/comments |
| AC-002 | Zero `Mat4.` calls in hello-world, open-world, and common placement code | todo |  | Raw `Mat4` remains escape hatch |
| AC-003 | Bowling non-gameplay scaffolding reduced >= 50%; before/after LOC recorded | todo |  | Baseline: 789 lines listed in roadmap |
| AC-004 | Hello-world reaches >= 30 FPS at 1280x720 on software backend reference hardware | todo |  | Record CPU/build/backend |
| AC-005 | Fixed-step synthetic input/clock yields exact matching simulation state | partial | `g3d_test_canvas3d_synthetic_input`; `g3d_test_game3d_runframes_probe`; `g3d_test_game3d_camera_controllers_probe`; `g3d_test_game3d_character_controller_probe`; `g3d_walk_min_visual_probe` | Canvas3D deterministic camera state, Game3D fixed timing, controller movement, and sample grounded movement proven; full showcase gameplay replay pending |
| AC-006 | Software-backend final frame matches visual baseline within tolerance | partial | `g3d_walk_min_visual_probe` | Game3D `walk_min` baseline has structural region assertions; final showcase baseline still pending |
| AC-007 | Overlay pixels are not bloomed or tonemapped | done | `g3d_walk_min_visual_probe` | Crisp HUD gate covered by final-overlay swatch |
| AC-008 | Same model loads from filesystem and mounted package path with glTF deps | partial | `test_rt_gltf`, `test_rt_model3d`, `g3d_test_model3d_load_asset`, `g3d_test_game3d_assets_probe` | GLB filesystem+mounted and glTF external deps/missing diagnostics proven at lower layer; Game3D wrapper covered for filesystem/asset API and needs starter/showcase mounted-package integration proof |
| AC-009 | `examples/3d/game3d_showcase/` demonstrates full stack | todo |  | See `05-tests-docs-samples.md` |
| AC-010 | Starter workflow can run, package, and ctest a new 3D game | todo |  | `game3d_starter` or template |
| AC-011 | Every runtime function/API area has docs and compiling snippets covered by ctest | partial | `docs/viperlib/graphics/game3d.md`; `g3d_test_game3d_presets_probe`; `g3d_test_game3d_assets_probe`; `g3d_test_game3d_physics_probe`; `g3d_test_game3d_collision_probe`; `g3d_test_game3d_anim_probe` | Phase 1-5 Game3D docs/probes exist; docs snippet ctest and later API areas pending |
| AC-012 | Each subsystem ships a `*_probe.zia` ctest and verifies escape hatches | partial | `g3d_test_game3d_camera_controllers_probe`; `g3d_test_game3d_character_controller_probe`; `g3d_test_game3d_presets_probe`; `g3d_test_game3d_assets_probe`; `g3d_test_game3d_physics_probe`; `g3d_test_game3d_collision_probe`; `g3d_test_game3d_anim_probe` | Cameras/character/presets/environment/debug/assets/physics/collision/animation covered; audio and VFX remain |

## Current readiness notes

- Phase 1 through Phase 5 core runtime work is now represented in the tracker,
  but the overall objective remains incomplete until audio/VFX,
  starter, docs snippets, and the showcase are done.
- Do not move an acceptance criterion to `done` unless its detailed tracker rows
  are also complete.
