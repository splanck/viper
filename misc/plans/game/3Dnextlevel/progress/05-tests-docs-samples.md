# Tests, docs, and samples tracker

This file tracks the delivery artifacts that prove Game3D is a good code-first
3D game workflow, not just a runtime API.

## Required ctest inventory

| ID | Area | Required tests | Source | Status | Test names / proof | Waiver | Notes |
|---|---|---|---|---|---|---|---|
| TEST-000 | Phase 0A Zia surface | Alias bind, function-typed no-return callback, block lambda, controller interface, optional interface chaining, input read after `Canvas3D.Poll()` | `roadmap.md` Phase 0A | done | `g3d_game3d_surface_probe` |  | Uses `VIPER_3D_BACKEND=software` |
| TEST-001 | Frame/final overlay | C unit tests plus Zia final-frame screenshot and overlay-crispness probes | `roadmap.md` Required ctest inventory | done | C: `test_rt_canvas3d_gpu_paths`, `test_rt_canvas3d_production`; Zia: `g3d_walk_min_visual_probe`; graphics3d 35/35 passed |  | Direct fake-backend `Flip` counter has a note in R-FRAME-018 |
| TEST-002 | Default lighting/getters | C unit tests plus visual/default-lit mesh probe | `roadmap.md` Required ctest inventory | done | C: `test_rt_canvas3d`, `test_rt_scene3d`, `test_rt_canvas3d_gpu_paths`; Zia: `g3d_walk_min_visual_probe` |  | Visual/default-lit mesh coverage added through `walk_min` |
| TEST-003 | Synthetic input/clock | Deterministic camera/gameplay state probe | `roadmap.md` Required ctest inventory | partial | C: `test_rt_canvas3d_gpu_paths`; Zia: `g3d_test_canvas3d_synthetic_input` |  | Canvas3D deterministic camera coverage added; full Game3D gameplay state probe pending |
| TEST-004 | Backend-safe quality | Software no-trap probe and GPU capability smoke where available | `roadmap.md` Required ctest inventory | done | C: `test_rt_canvas3d_gpu_paths`; Zia: `g3d_test_canvas3d_quality_profiles` |  | Software profile no-trap and fake GPU capability gating covered |
| TEST-005 | Asset resolver | Filesystem and mounted-package GLB/glTF/audio probes | `roadmap.md` Required ctest inventory | done | C: `test_rt_asset_manager`, `test_rt_gltf`, `test_rt_model3d`, `test_rt_audio_integration`; Zia: `g3d_test_model3d_load_asset`; surface: `test_rt_audio_surface_link`, `test_rt_graphics_surface_link` |  | GLB filesystem+mounted, glTF external buffer+texture, audio mounted-pack, missing-dep diagnostics |
| TEST-006 | Core world/entity/input | `world_probe.zia`, spawn/despawn/destroy cleanup, invalid-handle diagnostics, resize/aspect, escape hatches | `roadmap.md` Required ctest inventory | partial | C: `test_rt_game3d`; Zia: `g3d_test_game3d_world_probe`, `g3d_test_game3d_runframes_probe`, `g3d_test_game3d_runframes_callback_reject` |  | Core coverage is in place; explicit destroyed-handle negative Zia diagnostics and projection distortion checks still pending |
| TEST-007 | Cameras/character | Deterministic pose, grounded movement, follow camera late-update | `roadmap.md` Required ctest inventory | todo |  |  |  |
| TEST-008 | Presets/environment/debug | Parameter-range probe, visual smoke, final-overlay debug text | `roadmap.md` Required ctest inventory | todo |  |  |  |
| TEST-009 | Physics/collisions | Layer/mask filtering, enter/stay/exit, trigger, rich event data, optional simple callback | `roadmap.md` Required ctest inventory | todo |  |  |  |
| TEST-010 | Animation | Play/crossfade/event/root-motion under `runFrames` | `roadmap.md` Required ctest inventory | todo |  |  |  |
| TEST-011 | Audio/VFX | Listener/source binding, attenuation, attached audio, effect expiry | `roadmap.md` Required ctest inventory | todo |  |  |  |
| TEST-012 | Docs | Compile/import snippets for every new docs page | `roadmap.md` Required ctest inventory | todo |  |  |  |
| TEST-013 | Starter project | Copy/generate smoke, package-load smoke, deterministic test | `roadmap.md` Required ctest inventory | todo |  |  |  |
| TEST-014 | Showcase game | Smoke run, deterministic replay, final screenshot baseline, package-load path | `roadmap.md` Required ctest inventory | todo |  |  |  |
| TEST-015 | Regression | `ctest --test-dir build -L graphics3d` stays green | `roadmap.md` Testing strategy | done | `ctest --test-dir build -L graphics3d --output-on-failure` 42/42 passed |  | Includes Phase 1 Game3D probes, synthetic input, quality profiles, and model asset loading probe |
| TEST-016 | Cross-platform | Build plus graphics3d tests green on macOS/Windows/Linux | `roadmap.md` Testing strategy | todo |  |  |  |

## Test registration and waiver rules

| ID | Item | Source | Status | Proof / link | Notes |
|---|---|---|---|---|---|
| TEST-RULE-001 | Tests follow `src/tests/CMakeLists.txt` registration patterns | `roadmap.md` Testing strategy | partial | Phase 0A, `walk_min`, synthetic-input, and quality probes registered; Phase 0B/0C C coverage added to existing registered Canvas3D unit tests | Future probes still need registration |
| TEST-RULE-002 | Display-dependent tests carry `requires_display` labels | `roadmap.md` Testing strategy | partial | `g3d_game3d_surface_probe`, `g3d_test_canvas3d_synthetic_input`, `g3d_test_canvas3d_quality_profiles`, `g3d_walk_min_visual_probe` | Existing graphics tests still need audit before marking done |
| TEST-RULE-003 | Software-backend baseline does not require real display unless unavoidable | `roadmap.md` Testing strategy | todo |  |  |
| TEST-RULE-004 | Coverage gaps have named waiver with reason and owner | `roadmap.md` Required ctest inventory | todo |  | Decision D-012 |
| TEST-RULE-005 | Final-frame comparisons use tolerance plus structural region assertions | `roadmap.md` Testing strategy | done | `g3d_walk_min_visual_probe` | Baseline sample comparison plus overlay/shading region checks |

## Docs deliverables

| ID | Page / area | Required content | Source | Status | Snippet ctest | Proof / link | Notes |
|---|---|---|---|---|---|---|---|
| DOC-001 | Getting started | Install/build/run starter scene, no `Mat4`, expected first result | `roadmap.md` Phase 7 | todo |  |  | Code-first workflow |
| DOC-002 | Frame loop and overlay contract | `End`, `FinalizeFrame`, `ScreenshotFinal`, `Flip`, final overlay path | `roadmap.md` Phase 7 | partial | pending | `docs/graphics3d-guide.md`, `docs/viperlib/graphics/rendering3d.md`, `examples/3d/README.md` | Snippet ctest still pending |
| DOC-003 | Deterministic tests | Synthetic input/clock, `runFrames`, replay assertions | `roadmap.md` Phase 7 | partial | `g3d_test_game3d_runframes_probe` | `docs/viperlib/graphics/game3d.md`, rendering docs | Game3D docs explain `runFramesOnly`; full replay tutorial pending |
| DOC-004 | Presets | Lighting, materials, post-FX, quality profiles | `roadmap.md` Phase 7 | todo |  |  |  |
| DOC-005 | Environment | Outdoor/sunset/overcast/night, terrain/water/fog | `roadmap.md` Phase 7 | todo |  |  |  |
| DOC-006 | Assets and packages | GLB, glTF external deps, audio assets, package layout | `roadmap.md` Phase 7 | partial | `g3d_test_model3d_load_asset` covers API import | `docs/graphics3d-guide.md`, `docs/viperlib/io/assets.md`, `docs/viperlib/audio.md` | Full Game3D `Assets3D` docs still pending |
| DOC-007 | Animation | `Animator3D`, crossfade, events, root motion | `roadmap.md` Phase 7 | todo |  |  |  |
| DOC-008 | Audio/VFX | Listener, positional/attached/2D audio, effect registry, presets | `roadmap.md` Phase 7 | todo |  |  |  |
| DOC-009 | Physics/collision events | `BodyDef`, layers/masks, event buffers, optional callbacks | `roadmap.md` Phase 7 | todo |  |  |  |
| DOC-010 | Troubleshooting | Missing assets, backend fallbacks, destroyed handles, visual test failures | `roadmap.md` Phase 7 | todo |  |  |  |
| DOC-011 | API reference | Every new runtime function and Game3D API area | `roadmap.md` Phase 7 | partial | pending | `docs/viperlib/graphics/game3d.md` | Phase 1 Game3D surface documented; future API areas pending |
| DOC-012 | Runtime Canvas3D docs | Distinguish `End`, `FinalizeFrame`, `ScreenshotFinal`, `Flip` | `roadmap.md` Phase 7 | partial | pending | `docs/graphics3d-guide.md`, `docs/viperlib/graphics/rendering3d.md`, `examples/3d/README.md` | Snippet ctest still pending |
| DOC-013 | Raw escape hatches and lifetime | `world.canvas`, `world.scene`, `entity.node/body/anim`, raw `Graphics3D` calls | `README.md` Design principles | done |  | `docs/viperlib/graphics/game3d.md` | Phase 1 ownership and escape hatches documented |
| DOC-014 | Starter project README | Run/package/test commands and asset layout | `roadmap.md` Phase 7 | todo |  |  |  |

## Samples and games

| ID | Sample | Required content | Source | Status | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| SAMPLE-001 | `examples/3d/walk_min.zia` | Ground, three props, explicit lighting, free-fly or first-person camera, CPU-safe post-FX, crisp overlay | `roadmap.md` Phase 0B | done | `g3d_walk_min_visual_probe` | `examples/3d/README.md`, graphics docs | ctest passed | Baseline: `examples/3d/baselines/walk_min_software.png` |
| SAMPLE-002 | API hello world | About 15 lines, lit walkable scene, no common-case `Mat4` | `api-spec.md` Hello world | partial | `g3d_test_game3d_world_probe` | `docs/viperlib/graphics/game3d.md` | ctest passed | Dedicated sample/snippet ctest still pending |
| SAMPLE-003 | Richer API example | Physics ball, collision event, audio/VFX, follow camera, fixed timestep | `api-spec.md` Richer example | todo |  |  |  | Event-buffer version if callback sugar not approved |
| SAMPLE-004 | `examples/3d/game3d_starter/` or template | Source layout, asset layout, package config, run command, package command, deterministic test | `roadmap.md` Phase 7 | todo |  |  |  | AC-010 |
| SAMPLE-005 | `examples/3d/game3d_showcase/` | Full-stack playable mini-game | `roadmap.md` Phase 7 | todo |  |  |  | AC-009 |
| SAMPLE-006 | Bowling migration | Re-skin/setup migration showing line-count reduction | `roadmap.md` Phase 7 | todo |  |  |  | AC-003 |

## Showcase feature checklist

| ID | Feature | Source | Status | Proof / link | Notes |
|---|---|---|---|---|---|
| SHOW-001 | `World3D.runFixed` for gameplay | `roadmap.md` Phase 7 | todo |  |  |
| SHOW-002 | `runFrames` deterministic replay tests | `roadmap.md` Phase 7 | partial | `g3d_test_game3d_runframes_probe` | Showcase-level gameplay replay still pending |
| SHOW-003 | Final-frame post-FX plus crisp HUD/debug overlay | `roadmap.md` Phase 7 | todo |  |  |
| SHOW-004 | Lighting/material/post-FX/quality/environment preset toggles | `roadmap.md` Phase 7 | todo |  |  |
| SHOW-005 | Terrain/ground, water or fog | `roadmap.md` Phase 7 | todo |  |  |
| SHOW-006 | Prefabs and imported packaged GLB/glTF props | `roadmap.md` Phase 7 | todo |  |  |
| SHOW-007 | First-person camera mode | `roadmap.md` Phase 7 | todo |  |  |
| SHOW-008 | Follow or orbit camera mode | `roadmap.md` Phase 7 | todo |  |  |
| SHOW-009 | Character controller or player-controlled physics body | `roadmap.md` Phase 7 | todo |  |  |
| SHOW-010 | `LayerMask`, `BodyDef`, triggers, collision event handling | `roadmap.md` Phase 7 | todo |  |  |
| SHOW-011 | Animated skinned model with animation events | `roadmap.md` Phase 7 | todo |  |  |
| SHOW-012 | Positional audio | `roadmap.md` Phase 7 | todo |  |  |
| SHOW-013 | Attached audio source | `roadmap.md` Phase 7 | todo |  |  |
| SHOW-014 | Non-spatial UI/music audio | `roadmap.md` Phase 7 | todo |  |  |
| SHOW-015 | `Effects3D` particles/decals spawned from collisions | `roadmap.md` Phase 7 | todo |  |  |
| SHOW-016 | Package-aware model/audio loading | `roadmap.md` Phase 7 | todo |  |  |
| SHOW-017 | Raw escape-hatch usage in clearly marked advanced section | `roadmap.md` Phase 7 | todo |  |  |

## Bowling migration

| ID | Item | Source | Status | Proof / link | Notes |
|---|---|---|---|---|---|
| BOWL-001 | Record pre-migration LOC for renderer/camera/HUD/engine glue | `roadmap.md` Acceptance criteria | todo |  | Baseline currently 789 lines |
| BOWL-002 | Port/re-skin bowling setup to Game3D | `roadmap.md` Phase 7 | todo |  |  |
| BOWL-003 | Record post-migration LOC | `roadmap.md` Acceptance criteria | todo |  |  |
| BOWL-004 | Verify >= 50% scaffolding reduction | `roadmap.md` Acceptance criteria | todo |  |  |
| BOWL-005 | Document clarity improvements and remaining pain points | `roadmap.md` Phase 7 | todo |  | Feed back into API |
