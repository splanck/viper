# Tests, docs, and samples tracker

This file tracks the delivery artifacts that prove Game3D is a good code-first
3D game workflow, not just a runtime API.

## Required ctest inventory

| ID | Area | Required tests | Source | Status | Test names / proof | Waiver | Notes |
|---|---|---|---|---|---|---|---|
| TEST-000 | Phase 0A Zia surface | Alias bind, function-typed no-return callback, block lambda, controller interface, optional interface chaining, input read after `Canvas3D.Poll()` | `roadmap.md` Phase 0A | done | `g3d_game3d_surface_probe` |  | Uses `VIPER_3D_BACKEND=software` |
| TEST-001 | Frame/final overlay | C unit tests plus Zia final-frame screenshot and overlay-crispness probes | `roadmap.md` Required ctest inventory | done | C: `test_rt_canvas3d_gpu_paths`, `test_rt_canvas3d_production`; Zia: `g3d_walk_min_visual_probe`; graphics3d 51/51 passed |  | Direct fake-backend `Flip` counter has a note in R-FRAME-018 |
| TEST-002 | Default lighting/getters | C unit tests plus visual/default-lit mesh probe | `roadmap.md` Required ctest inventory | done | C: `test_rt_canvas3d`, `test_rt_scene3d`, `test_rt_canvas3d_gpu_paths`; Zia: `g3d_walk_min_visual_probe` |  | Visual/default-lit mesh coverage added through `walk_min` |
| TEST-003 | Synthetic input/clock | Deterministic camera/gameplay state probe | `roadmap.md` Required ctest inventory | done | C: `test_rt_canvas3d_gpu_paths`; Zia: `g3d_test_canvas3d_synthetic_input`, `g3d_test_game3d_camera_controllers_probe`, `g3d_test_game3d_character_controller_probe`, `g3d_walk_min_visual_probe`, `g3d_game3d_starter_probe`, `g3d_game3d_showcase` |  | Canvas3D, controllers, starter movement, and showcase gameplay replay covered |
| TEST-004 | Backend-safe quality | Software no-trap probe and GPU capability smoke where available | `roadmap.md` Required ctest inventory | done | C: `test_rt_canvas3d_gpu_paths`; Zia: `g3d_test_canvas3d_quality_profiles` |  | Software profile no-trap and fake GPU capability gating covered |
| TEST-005 | Asset resolver | Filesystem and mounted-package GLB/glTF/audio probes | `roadmap.md` Required ctest inventory | done | C: `test_rt_asset_manager`, `test_rt_gltf`, `test_rt_model3d`, `test_rt_audio_integration`, `test_rt_game3d`; Zia: `g3d_test_model3d_load_asset`, `g3d_test_game3d_assets_probe`; surface: `test_rt_audio_surface_link`, `test_rt_graphics_surface_link` |  | GLB filesystem+mounted, glTF external buffer+texture, audio mounted-pack, Game3D Assets3D wrapper/cache, missing-dep diagnostics |
| TEST-006 | Core world/entity/input | `world_probe.zia`, spawn/despawn/destroy cleanup, invalid-handle diagnostics, resize/aspect, escape hatches | `roadmap.md` Required ctest inventory | partial | C: `test_rt_game3d`; Zia: `g3d_test_game3d_world_probe`, `g3d_test_game3d_runframes_probe`, `g3d_test_game3d_runframes_callback_reject` |  | Core coverage is in place; explicit destroyed-handle negative Zia diagnostics and projection distortion checks still pending |
| TEST-007 | Cameras/character | Deterministic pose, grounded movement, follow camera late-update | `roadmap.md` Required ctest inventory | done | C: `test_rt_game3d`; Zia: `g3d_test_game3d_camera_controllers_probe`, `g3d_test_game3d_character_controller_probe` |  | Free-fly, orbit, follow, first-person, and character movement covered |
| TEST-008 | Presets/environment/debug | Parameter-range probe, visual smoke, final-overlay debug text | `roadmap.md` Required ctest inventory | done | C: `test_rt_game3d`; Zia: `g3d_test_game3d_presets_probe` |  | Covers material ranges, prefab spawnability, lighting, quality fallback, post-FX, environment terrain/water/fog, static terrain body, and debug final overlay |
| TEST-009 | Physics/collisions | Layer/mask filtering, enter/stay/exit, trigger, rich event data, optional simple callback | `roadmap.md` Required ctest inventory | done | C: `test_rt_game3d`; Zia: `g3d_test_game3d_physics_probe`, `g3d_test_game3d_collision_probe` | optional simple callback sugar deferred under callback-bridge policy | Covers BodyDef, filters, trigger/static/dynamic bodies, resting ball, enter/stay/exit, entity wrappers, raw event escape hatch |
| TEST-010 | Animation | Play/crossfade/event/root-motion under `runFrames` | `roadmap.md` Required ctest inventory | done | C: `test_rt_game3d`; Zia: `g3d_test_game3d_anim_probe` |  | Uses deterministic `runFramesOnly` because interpreted callback loops are deferred |
| TEST-011 | Audio/VFX | Listener/source binding, attenuation, attached audio, effect expiry | `roadmap.md` Required ctest inventory | done | C: `test_rt_game3d`; Zia: `g3d_test_game3d_audio_probe`, `g3d_test_game3d_effects_probe`; lower: `RTAudio3DTests`, `test_rt_audio3d_objects` |  | Focused ctests passed; playback voice checks remain backend-conditional |
| TEST-012 | Docs | Compile/import snippets for every new docs page | `roadmap.md` Required ctest inventory | done | `g3d_test_game3d_docs_snippets` |  | Covers setup, presets, assets, physics, audio/VFX, deterministic frame helpers, and manual final-frame capture snippets |
| TEST-013 | Starter project | Copy/generate smoke, package-load smoke, deterministic test | `roadmap.md` Required ctest inventory | done | `g3d_game3d_starter_probe`, `g3d_game3d_starter_package_dry_run` |  | Starter has `viper.project`, packaged `assets/`, model asset load, deterministic movement/final-frame test |
| TEST-014 | Showcase game | Smoke run, deterministic replay, final screenshot baseline, package-load path | `roadmap.md` Required ctest inventory | done | `g3d_game3d_showcase` | W-003, W-004 | Structural software visual baseline, replay state check, package-aware model/audio APIs, procedural animation fixture |
| TEST-015 | Regression | `ctest --test-dir build -L graphics3d` stays green | `roadmap.md` Testing strategy | done | `ctest --test-dir build -L graphics3d --output-on-failure` 57/57 passed |  | Includes Phase 1-7 Game3D probes, docs snippets, hello, starter, showcase, bowling setup, synthetic input, quality profiles, model asset loading, and `walk_min` |
| TEST-016 | Cross-platform | Build plus graphics3d tests green on macOS/Windows/Linux | `roadmap.md` Testing strategy | todo |  |  |  |

## Test registration and waiver rules

| ID | Item | Source | Status | Proof / link | Notes |
|---|---|---|---|---|---|
| TEST-RULE-001 | Tests follow `src/tests/CMakeLists.txt` registration patterns | `roadmap.md` Testing strategy | done | Phase 0A through Phase 7 C/Zia probes, samples, starter package dry-run, showcase, docs snippets, and bowling setup are registered |  |
| TEST-RULE-002 | Display-dependent tests carry `requires_display` labels | `roadmap.md` Testing strategy | done | Game3D display/final-frame probes are labeled `requires_display`; starter package dry-run is not | Existing lower graphics tests predate this plan but Game3D registrations comply |
| TEST-RULE-003 | Software-backend baseline does not require real display unless unavoidable | `roadmap.md` Testing strategy | done | `VIPER_3D_BACKEND=software` on all Game3D visual/sample probes |  |
| TEST-RULE-004 | Coverage gaps have named waiver with reason and owner | `roadmap.md` Required ctest inventory | done | `progress/06-waivers.md` | D-012 closed |
| TEST-RULE-005 | Final-frame comparisons use tolerance plus structural region assertions | `roadmap.md` Testing strategy | done | `g3d_walk_min_visual_probe` | Baseline sample comparison plus overlay/shading region checks |

## Docs deliverables

| ID | Page / area | Required content | Source | Status | Snippet ctest | Proof / link | Notes |
|---|---|---|---|---|---|---|---|
| DOC-001 | Getting started | Install/build/run starter scene, no `Mat4`, expected first result | `roadmap.md` Phase 7 | done | `g3d_game3d_starter_probe` | `examples/3d/game3d_starter/README.md`, `examples/3d/README.md`, `docs/viperlib/graphics/game3d.md` | Code-first workflow with package dry-run |
| DOC-002 | Frame loop and overlay contract | `End`, `FinalizeFrame`, `ScreenshotFinal`, `Flip`, final overlay path | `roadmap.md` Phase 7 | done | `g3d_test_game3d_docs_snippets`, `g3d_walk_min_visual_probe`, `g3d_game3d_showcase` | `docs/viperlib/graphics/game3d.md`, `examples/3d/README.md` | Game3D docs include raw Canvas3D finalization table |
| DOC-003 | Deterministic tests | Synthetic input/clock, `runFrames`, replay assertions | `roadmap.md` Phase 7 | done | `g3d_test_game3d_runframes_probe`, controller probes, `g3d_game3d_starter_probe`, `g3d_game3d_showcase` | `docs/viperlib/graphics/game3d.md`, starter/showcase READMEs | Interpreted Zia uses manual frame APIs or `runFramesOnly` under W-001 |
| DOC-004 | Presets | Lighting, materials, post-FX, quality profiles | `roadmap.md` Phase 7 | done | `g3d_test_game3d_presets_probe` covers API import/use | `docs/viperlib/graphics/game3d.md` | Snippet-specific ctest remains part of DOC-020/TEST-012 |
| DOC-005 | Environment | Outdoor/sunset/overcast/night, terrain/water/fog | `roadmap.md` Phase 7 | done | `g3d_test_game3d_presets_probe` covers API import/use | `docs/viperlib/graphics/game3d.md` | Terrain is ground entity + static body in this phase |
| DOC-006 | Assets and packages | GLB, glTF external deps, audio assets, package layout | `roadmap.md` Phase 7 | done | `g3d_test_model3d_load_asset`, `g3d_test_game3d_assets_probe`, `g3d_test_game3d_docs_snippets`, `g3d_game3d_starter_package_dry_run`, `g3d_game3d_showcase` | `docs/graphics3d-guide.md`, `docs/viperlib/io/assets.md`, `docs/viperlib/audio.md`, `docs/viperlib/graphics/game3d.md`, starter README | Game3D `Assets3D` docs and starter package layout covered |
| DOC-007 | Animation | `Animator3D`, crossfade, events, root motion | `roadmap.md` Phase 7 | done | `g3d_test_game3d_anim_probe` covers API import/use | `docs/viperlib/graphics/game3d.md` | Snippet-specific ctest remains part of DOC-020/TEST-012 |
| DOC-008 | Audio/VFX | Listener, positional/attached/2D audio, effect registry, presets | `roadmap.md` Phase 7 | done | `g3d_test_game3d_audio_probe`, `g3d_test_game3d_effects_probe` cover API import/use | `docs/viperlib/graphics/game3d.md` | Snippet-specific ctest remains part of DOC-020/TEST-012 |
| DOC-009 | Physics/collision events | `BodyDef`, layers/masks, event buffers, optional callbacks | `roadmap.md` Phase 7 | done | `g3d_test_game3d_physics_probe`, `g3d_test_game3d_collision_probe` cover API import/use | `docs/viperlib/graphics/game3d.md` | Callback sugar documented as deferred; event-buffer polling is supported interpreted-Zia path |
| DOC-010 | Troubleshooting | Missing assets, backend fallbacks, destroyed handles, visual test failures | `roadmap.md` Phase 7 | done | `g3d_test_game3d_docs_snippets` | `docs/viperlib/graphics/game3d.md` | Troubleshooting table added |
| DOC-011 | API reference | Every new runtime function and Game3D API area | `roadmap.md` Phase 7 | done | `g3d_test_game3d_docs_snippets` plus subsystem probes | `docs/viperlib/graphics/game3d.md` | Phase 1 through Phase 7 surfaces documented, with waivers for callback/GPU/skinned-content gaps |
| DOC-012 | Runtime Canvas3D docs | Distinguish `End`, `FinalizeFrame`, `ScreenshotFinal`, `Flip` | `roadmap.md` Phase 7 | done | `g3d_test_game3d_docs_snippets`, `g3d_walk_min_visual_probe`, `g3d_game3d_showcase` | `docs/viperlib/graphics/game3d.md`, `examples/3d/README.md` | Raw Canvas3D finalization table added to Game3D docs |
| DOC-013 | Raw escape hatches and lifetime | `world.canvas`, `world.scene`, `entity.node/body/anim`, raw `Graphics3D` calls | `README.md` Design principles | done |  | `docs/viperlib/graphics/game3d.md` | Phase 1 ownership and escape hatches documented |
| DOC-014 | Starter project README | Run/package/test commands and asset layout | `roadmap.md` Phase 7 | done | `g3d_game3d_starter_probe`, `g3d_game3d_starter_package_dry_run` | `examples/3d/game3d_starter/README.md` |  |

## Samples and games

| ID | Sample | Required content | Source | Status | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| SAMPLE-001 | `examples/3d/walk_min.zia` | Game3D world, ground, props, static colliders, first-person camera, grounded character movement, CPU-safe post-FX, crisp overlay | `roadmap.md` Phase 0B/Phase 2 | done | `g3d_walk_min_visual_probe` final-frame and grounded movement checks | `examples/3d/README.md`, Game3D docs | ctest passed | Baseline: `examples/3d/baselines/walk_min_software.png` |
| SAMPLE-002 | API hello world | About 15 lines, lit walkable scene, no common-case `Mat4` | `api-spec.md` Hello world | done | `g3d_game3d_hello` | `examples/3d/README.md`, Game3D docs | ctest passed | `examples/3d/game3d_hello.zia` has 16 nonblank source lines |
| SAMPLE-003 | Richer API example | Physics ball, collision event, audio/VFX, follow camera, fixed timestep | `api-spec.md` Richer example | done | `g3d_game3d_showcase`, `g3d_test_game3d_docs_snippets` | Game3D docs, showcase README | Event-buffer version implemented |
| SAMPLE-004 | `examples/3d/game3d_starter/` or template | Source layout, asset layout, package config, run command, package command, deterministic test | `roadmap.md` Phase 7 | done | `g3d_game3d_starter_probe`, `g3d_game3d_starter_package_dry_run` | Starter README | AC-010 |
| SAMPLE-005 | `examples/3d/game3d_showcase/` | Full-stack playable mini-game | `roadmap.md` Phase 7 | done | `g3d_game3d_showcase` | Showcase README, examples README | AC-009, with W-003/W-004 |
| SAMPLE-006 | Bowling migration | Re-skin/setup migration showing line-count reduction | `roadmap.md` Phase 7 | done | `g3d_game3d_bowling_setup` | Bowling Game3D README | AC-003 |

## Showcase feature checklist

| ID | Feature | Source | Status | Proof / link | Notes |
|---|---|---|---|---|---|
| SHOW-001 | `World3D.runFixed` for gameplay | `roadmap.md` Phase 7 | waived | `g3d_game3d_showcase` uses manual fixed-step; `g3d_test_game3d_runframes_callback_reject` covers callback boundary | W-001: interpreted Zia cannot call native `runFixed` callbacks yet |
| SHOW-002 | `runFrames` deterministic replay tests | `roadmap.md` Phase 7 | done | `g3d_game3d_showcase`, `g3d_test_game3d_runframes_probe` | Showcase includes `runFramesOnly` helper proof and full replay state comparison |
| SHOW-003 | Final-frame post-FX plus crisp HUD/debug overlay | `roadmap.md` Phase 7 | done | `g3d_game3d_showcase` | Software structural final-frame assertion |
| SHOW-004 | Lighting/material/post-FX/quality/environment preset toggles | `roadmap.md` Phase 7 | done | `g3d_game3d_showcase` | Runtime swaps quality/post-FX/material/fog |
| SHOW-005 | Terrain/ground, water or fog | `roadmap.md` Phase 7 | done | `g3d_game3d_showcase` | Outdoor environment, water, fog, static ground |
| SHOW-006 | Prefabs and imported packaged GLB/glTF props | `roadmap.md` Phase 7 | done | `g3d_game3d_showcase`, `g3d_game3d_starter_probe` | Uses prefabs and `Assets3D.LoadModelAsset` |
| SHOW-007 | First-person camera mode | `roadmap.md` Phase 7 | done | `g3d_game3d_showcase` | Starts with FPS character controller |
| SHOW-008 | Follow or orbit camera mode | `roadmap.md` Phase 7 | done | `g3d_game3d_showcase` | Switches to follow then orbit |
| SHOW-009 | Character controller or player-controlled physics body | `roadmap.md` Phase 7 | done | `g3d_game3d_showcase` | Player uses `CharacterController3D`; ball is physics driven |
| SHOW-010 | `LayerMask`, `BodyDef`, triggers, collision event handling | `roadmap.md` Phase 7 | done | `g3d_game3d_showcase` | Dynamic ball, trigger, layer mask, event polling |
| SHOW-011 | Animated skinned model with animation events | `roadmap.md` Phase 7 | partial | `g3d_game3d_showcase`, `g3d_test_game3d_anim_probe` | Showcase uses procedural skeleton/events/root motion; imported skinned art waived under W-003 |
| SHOW-012 | Positional audio | `roadmap.md` Phase 7 | done | `g3d_game3d_showcase` | Collision `playAt` |
| SHOW-013 | Attached audio source | `roadmap.md` Phase 7 | done | `g3d_game3d_showcase` | Ball-attached source |
| SHOW-014 | Non-spatial UI/music audio | `roadmap.md` Phase 7 | done | `g3d_game3d_showcase` | `play2D` |
| SHOW-015 | `Effects3D` particles/decals spawned from collisions | `roadmap.md` Phase 7 | done | `g3d_game3d_showcase` | Dust and impact decal from collision point/normal |
| SHOW-016 | Package-aware model/audio loading | `roadmap.md` Phase 7 | done | `g3d_game3d_showcase`, `g3d_game3d_starter_package_dry_run` | Showcase uses asset APIs; starter validates packaged `assets/` layout |
| SHOW-017 | Raw escape-hatch usage in clearly marked advanced section | `roadmap.md` Phase 7 | done | `g3d_game3d_showcase`, showcase README | Direct `Canvas3D`, `SceneNode3D`, and `Sound` usage |

## Bowling migration

| ID | Item | Source | Status | Proof / link | Notes |
|---|---|---|---|---|---|
| BOWL-001 | Record pre-migration LOC for renderer/camera/HUD/engine glue | `roadmap.md` Acceptance criteria | done | `wc -l engine/renderer.zia engine/hud.zia engine/camera.zia` = 789 | Baseline files: 291 + 279 + 219 |
| BOWL-002 | Port/re-skin bowling setup to Game3D | `roadmap.md` Phase 7 | done | `examples/games/3dbowling/game3d/game3d_setup.zia`; `g3d_game3d_bowling_setup` |  |
| BOWL-003 | Record post-migration LOC | `roadmap.md` Acceptance criteria | done | `wc -l game3d/game3d_setup.zia` = 129 |  |
| BOWL-004 | Verify >= 50% scaffolding reduction | `roadmap.md` Acceptance criteria | done | 83.7% reduction | 789 -> 129 |
| BOWL-005 | Document clarity improvements and remaining pain points | `roadmap.md` Phase 7 | done | `examples/games/3dbowling/game3d/README.md` | Scoring/menus remain original game scope |
