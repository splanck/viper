# Master progress

This file tracks global readiness. Detailed work items live in the other
tracker files.

## Overall gates

| ID | Gate | Source | Status | Proof / link | Notes |
|---|---|---|---|---|---|
| GATE-001 | Game3D is runtime-first C implementation, not a Zia source package | `README.md` §2/§5 | done | D-001 closed; `zia-feasibility.md` runtime-first scope | Must hold for every API area |
| GATE-002 | Public namespace decision closed before broad `runtime.def` additions | `README.md` §8, `roadmap.md` Phase 0A | done | D-001 closed; `g3d_game3d_surface_probe` passed | Use `Viper.Game3D.*` |
| GATE-003 | Frame finalization and final screenshot contract proven before ergonomic API | `runtime-changes.md` §1, `roadmap.md` Phase 0B | partial | C contract tests; `walk_min` exact visual baseline; showcase structural baseline; `ctest --test-dir build -L graphics3d` 57/57 passed | Cross-platform build/test proof still pending |
| GATE-004 | Synthetic input/clock proven before deterministic gameplay tests | `runtime-changes.md` §7, `roadmap.md` Phase 0C | done | `test_rt_canvas3d_gpu_paths`; `g3d_test_canvas3d_synthetic_input`; `g3d_test_game3d_runframes_probe`; `g3d_game3d_starter_probe`; `g3d_game3d_showcase`; graphics3d 57/57 | Canvas3D contract, Game3D `runFramesOnly` timing, starter movement replay, and showcase gameplay-state replay proven |
| GATE-005 | Package-aware model/audio asset loading proven before showcase/starter | `runtime-changes.md` §8, `roadmap.md` Phase 0D | done | `test_rt_gltf`, `test_rt_model3d`, `test_rt_audio_integration`, `g3d_test_model3d_load_asset`, `g3d_test_game3d_assets_probe`, `g3d_game3d_starter_package_dry_run`, `g3d_game3d_showcase` | Runtime model/audio loading, Game3D `Assets3D`, starter package layout, and showcase asset API use covered |
| GATE-006 | Ownership/lifetime semantics documented and tested before broad samples | `runtime-changes.md` §10 | partial | `test_rt_game3d`; `g3d_test_game3d_world_probe`; `g3d_test_game3d_effects_probe`; `docs/viperlib/graphics/game3d.md` | Core world/entity ownership, imported templates, animation, audio source tracking, and effects registry cleanup covered; richer destroyed-handle diagnostics remain later |
| GATE-007 | Every public runtime function has ctest coverage or explicit waiver | `runtime-changes.md` Validation | done | Phase 0B through Phase 7 Game3D C/Zia tests plus `progress/06-waivers.md`; graphics3d 57/57 | Interpreted-Zia callback loop, GPU timing, imported skinned-art fixture, and exact showcase PNG baseline have named waivers |
| GATE-008 | Every docs snippet that claims to compile is compiled/imported by ctest | `roadmap.md` Testing strategy | done | `g3d_test_game3d_docs_snippets`; graphics3d 57/57 | Includes quick starts and API topic snippets |
| GATE-009 | Starter project can run, package assets, and execute deterministic test | `roadmap.md` Phase 7 | done | `g3d_game3d_starter_probe`, `g3d_game3d_starter_package_dry_run` | Code-first onboarding gate |
| GATE-010 | Showcase demonstrates the full Game3D stack in one playable sample | `roadmap.md` Phase 7 | done | `g3d_game3d_showcase`, showcase README | W-003/W-004 record content/baseline tradeoffs |

## Phase status

| ID | Phase | Status | Entry dependency | Exit proof / link | Notes |
|---|---|---|---|---|---|
| PHASE-0A | Runtime surface confirmation spike | done | Plan approved | `g3d_game3d_surface_probe` passed; `zia-feasibility.md` Confirm-0A rows resolved; D-001/D-002/D-003 closed | Namespace, event model, sample syntax |
| PHASE-0B | Runtime final-frame contract and visual baseline | in_progress | PHASE-0A decisions where needed | C runtime contract tests, `walk_min` visual baseline, showcase structural baseline, graphics3d 57/57 | Windows/Linux proof still pending |
| PHASE-0C | Deterministic test input, clock, and backend-safe quality | done | PHASE-0B for final-frame probes | Synthetic input/clock and quality ctests passed; graphics3d 51/51 | Game3D `runFrames` and Debug3D wrappers now implemented in later phases |
| PHASE-0D | Package-aware model/audio asset loading | done | Asset resolver decision | GLB filesystem+mounted, glTF external deps, audio mounted-pack ctests, and Game3D `Assets3D` wrapper probes passed | Starter/showcase mounted-package integration proof remains under Phase 7 |
| PHASE-1 | Core Game3D world/entity/input/masks | partial | Phase 0 contracts | `test_rt_game3d`; `g3d_test_game3d_world_probe`; `g3d_test_game3d_runframes_probe`; callback rejection probe; graphics3d 51/51 | Core C runtime layer is implemented; docs-snippet and richer negative diagnostics remain |
| PHASE-2 | Cameras and walkable movement | done | Core world/input | `test_rt_game3d`; `g3d_test_game3d_camera_controllers_probe`; `g3d_test_game3d_character_controller_probe`; `g3d_walk_min_visual_probe` | Built-in controllers, character movement, and grounded `walk_min` migration covered |
| PHASE-3 | Presets, prefabs, quality, environment, debug | done | Core world + quality caps | `test_rt_game3d`; `g3d_test_game3d_presets_probe`; docs updated | Good-looking defaults and final-overlay debug path covered |
| PHASE-4 | Assets, physics, and collision events | done | Core entity + asset resolver | `test_rt_game3d`; `g3d_test_game3d_assets_probe`; `g3d_test_game3d_physics_probe`; `g3d_test_game3d_collision_probe`; Game3D docs | Runtime event buffers, BodyDef attach path, Assets3D templates, and callback-sugar waiver covered |
| PHASE-5 | Animation | done | Asset/model + event strategy | `test_rt_game3d`; `g3d_test_game3d_anim_probe`; Game3D docs | Runtime `Animator3D`, state time, event buffers, entity attachment, and root-motion sync covered |
| PHASE-6 | 3D audio and VFX | done | World/entity + asset resolver | `test_rt_game3d`; `g3d_test_game3d_audio_probe`; `g3d_test_game3d_effects_probe`; Game3D docs; graphics3d 51/51 | Audio/VFX runtime helpers covered |
| PHASE-7 | Showcase, docs, starter, and bowling migration | done | All API areas usable | `g3d_game3d_hello`; `g3d_game3d_starter_probe`; `g3d_game3d_starter_package_dry_run`; `g3d_game3d_showcase`; `g3d_game3d_bowling_setup`; `g3d_test_game3d_docs_snippets`; graphics3d 57/57 | Product-readiness phase complete with waivers W-001 through W-005 |

## Acceptance criteria

| ID | Criterion | Status | Proof / link | Notes |
|---|---|---|---|---|
| AC-001 | Hello-world <= 20 source lines renders a lit, walkable scene | done | `examples/3d/game3d_hello.zia`; `g3d_game3d_hello` | 16 nonblank source lines |
| AC-002 | Zero `Mat4.` calls in hello-world, open-world, and common placement code | done | `examples/3d/game3d_hello.zia`, starter, bowling setup; `rg Mat4` only finds showcase procedural animation skeleton | Raw `Mat4` remains escape hatch for advanced animation setup |
| AC-003 | Bowling non-gameplay scaffolding reduced >= 50%; before/after LOC recorded | done | `examples/games/3dbowling/game3d/README.md`; `g3d_game3d_bowling_setup` | 789 -> 129 LOC, 83.7% reduction |
| AC-004 | Hello-world reaches >= 30 FPS at 1280x720 on software backend reference hardware | waived | Debug local measurement recorded in W-005 | Needs Release/reference-hardware performance lane; Debug run was 30 frames / 2812 ms on software backend |
| AC-005 | Fixed-step synthetic input/clock yields exact matching simulation state | done | `g3d_test_canvas3d_synthetic_input`; `g3d_test_game3d_runframes_probe`; controller probes; `g3d_game3d_starter_probe`; `g3d_game3d_showcase` | Showcase and starter replay metrics match |
| AC-006 | Software-backend final frame matches visual baseline within tolerance | done | `g3d_walk_min_visual_probe`; `g3d_game3d_showcase` | `walk_min` exact-tolerance PNG baseline plus showcase structural baseline under W-004 |
| AC-007 | Overlay pixels are not bloomed or tonemapped | done | `g3d_walk_min_visual_probe` | Crisp HUD gate covered by final-overlay swatch |
| AC-008 | Same model loads from filesystem and mounted package path with glTF deps | done | `test_rt_gltf`, `test_rt_model3d`, `g3d_test_model3d_load_asset`, `g3d_test_game3d_assets_probe`, `g3d_game3d_starter_package_dry_run`, `g3d_game3d_showcase` | Runtime mounted assets plus Game3D asset wrappers and starter packaged asset layout covered |
| AC-009 | `examples/3d/game3d_showcase/` demonstrates full stack | done | `g3d_game3d_showcase`; showcase README | See W-003/W-004 |
| AC-010 | Starter workflow can run, package, and ctest a new 3D game | done | `g3d_game3d_starter_probe`, `g3d_game3d_starter_package_dry_run`; starter README |  |
| AC-011 | Every runtime function/API area has docs and compiling snippets covered by ctest | done | `docs/viperlib/graphics/game3d.md`; `g3d_test_game3d_docs_snippets`; subsystem probes | Waivers recorded in `06-waivers.md` |
| AC-012 | Each subsystem ships a `*_probe.zia` ctest and verifies escape hatches | done | Game3D subsystem probes, docs snippet probe, starter/showcase/bowling ctests | Raw escape hatches covered via `world.canvas`, scene/node/body/anim controller, `Canvas3D`, `SceneNode3D`, and lower `Graphics3D` probes |

## Current readiness notes

- Phase 1 through Phase 7 are now represented by runtime tests, docs, samples,
  starter/package checks, the showcase, and the bowling migration.
- Remaining non-local proof is tracked as waivers: managed callback trampolines,
  GPU timing, imported skinned showcase art, exact showcase PNG baseline, and a
  Release/reference-hardware software performance lane.
