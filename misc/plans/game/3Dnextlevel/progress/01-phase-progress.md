# Phase progress

Use this file for execution tracking against `roadmap.md`. Lower-level runtime,
API, test, and docs items are tracked in the sibling files.

## Phase 0A - runtime surface confirmation spike

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| P0A-001 | Confirm public namespace; recommended default `Viper.Game3D.*` | `roadmap.md` Phase 0A | done | runtime namespace decision logged | `g3d_game3d_surface_probe` proves comparable `Viper.*` runtime alias/call syntax | `zia-feasibility.md`, `02-decisions.md` | ctest passed | Use `Viper.Game3D.*` through `runtime.def`; no Zia helper package |
| P0A-002 | Confirm runtime-owned collision/animation event buffers as authoritative model | `roadmap.md` Phase 0A | done | design decision closed |  | `api-spec.md`, `zia-feasibility.md`, `02-decisions.md` | D-002 closed | Callback sugar optional and never authoritative |
| P0A-003 | Confirm void function-type spelling or select one-method interface fallback | `roadmap.md` Phase 0A | done | `(Float) -> Unit` selected | `g3d_game3d_surface_probe` | `api-spec.md`, `zia-feasibility.md` | ctest passed | `(Float) -> Void` rejected by spike |
| P0A-004 | Build `game3d_surface_probe.zia` | `roadmap.md` Phase 0A | done | `tests/runtime/game3d_surface_probe.zia` | manual run and ctest |  | ctest passed | Alias bind, function callback, lambda, controller, optional chaining, input read |
| P0A-005 | Register `game3d_surface_probe.zia` under ctest | `roadmap.md` Phase 0A Exit | done | `src/tests/CMakeLists.txt` | `g3d_game3d_surface_probe` |  | ctest passed | Uses `VIPER_3D_BACKEND=software` and `requires_display` label |
| P0A-006 | Resolve all Confirm-0A rows in `zia-feasibility.md` | `roadmap.md` Phase 0A Exit | done |  | `g3d_game3d_surface_probe` | `zia-feasibility.md` | ctest passed | Default interface methods avoided by design |
| P0A-007 | Update API docs if callbacks use interfaces or are demoted to event buffers only | `roadmap.md` Phase 0A Exit | done |  |  | `api-spec.md` | local diff | Callback examples now use `Unit`; event buffers remain mandatory |

## Phase 0B - runtime final-frame contract and visual baseline

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| P0B-001 | Implement final overlay command queue | `roadmap.md` Phase 0B | done | `rt_canvas3d` final overlay queue | `test_rt_canvas3d_gpu_paths` | header/runtime comments | focused Canvas3D ctest passed | See R-FRAME rows |
| P0B-002 | Implement idempotent frame finalization | `roadmap.md` Phase 0B | done | `Canvas3D.FinalizeFrame` | `test_rt_canvas3d_gpu_paths` | header/runtime comments | focused ctest passed | Direct `Flip` harness still tracked separately |
| P0B-003 | Implement `ScreenshotFinal` | `roadmap.md` Phase 0B | done | `Canvas3D.ScreenshotFinal` | `test_rt_canvas3d_gpu_paths` | header/runtime comments | focused ctest passed |  |
| P0B-004 | Implement frame-finalized state | `roadmap.md` Phase 0B | done | `Canvas3D.get_FrameFinalized` | `test_rt_canvas3d_gpu_paths` | header/runtime comments | focused ctest passed |  |
| P0B-005 | Implement backend capability strings | `roadmap.md` Phase 0B | done | `postfx-overlay`, `final-screenshot`, `gpu-postfx-overlay` | `test_rt_canvas3d_production` | header/runtime comments | focused ctest passed | GPU post-FX overlay capability remains false until backend path exists |
| P0B-006 | Implement default lighting and `ClearLights` | `roadmap.md` Phase 0B | done | `Canvas3D.SetDefaultLighting`, `ClearLights`, `LightCount` | `test_rt_canvas3d` | `docs/graphics3d-guide.md`, `docs/viperlib/graphics/rendering3d.md` | focused ctest passed | Explicit helper only; no implicit fallback lighting |
| P0B-007 | Implement minimum getters needed by Game3D probes | `roadmap.md` Phase 0B | done | Light3D, Material3D, SceneNode3D, Canvas3D getters | `test_rt_canvas3d`, `test_rt_scene3d`, `test_rt_canvas3d_gpu_paths` | docs updated | focused ctest passed | Optional texture getters and anim state time still tracked separately |
| P0B-008 | Implement remaining Metal finite guards | `roadmap.md` Phase 0B | done | Metal material helper rejects huge/non-finite lengths; skybox shader uses safe helper | `test_vgfx3d_backend_metal_shared` |  | focused ctest passed | Metal visual probe still tracked as R-METAL-006 |
| P0B-009 | Build `examples/3d/walk_min.zia` | `roadmap.md` Phase 0B | done | `examples/3d/walk_min.zia` | `g3d_walk_min_visual_probe` imports sample | `examples/3d/README.md`, graphics docs | ctest passed | Ground, props, explicit default lighting, camera, CPU-safe post-FX, final overlay |
| P0B-010 | Capture software-backend canonical final-frame baseline | `roadmap.md` Phase 0B | done | `examples/3d/baselines/walk_min_software.png` | `g3d_walk_min_visual_probe` | docs updated | ctest passed | Probe saves current frame to `/tmp/walk_min_software_current.png` |
| P0B-011 | Prove screenshot includes post-FX and crisp overlay | `roadmap.md` Phase 0B Exit | done | `Canvas3D.ScreenshotFinal` used by `walk_min_probe` | `g3d_walk_min_visual_probe` | docs updated | ctest passed | Baseline plus structural overlay checks |
| P0B-012 | Prove overlay pixels are not bloomed/tonemapped | `roadmap.md` Phase 0B Exit | done | final overlay cyan swatch sampled after `ScreenshotFinal` | `g3d_walk_min_visual_probe` | docs updated | ctest passed | Adjacent sample rejects bloom bleed |
| P0B-013 | Prove default-lit mesh has clear directional shading | `roadmap.md` Phase 0B Exit | done | `SetDefaultLighting` in sample; luminance-region check | `g3d_walk_min_visual_probe` | docs updated | ctest passed | Structural visibility/shading assertion |
| P0B-014 | Prove `ScreenshotFinal()` then `Flip()` does not double-apply finalization | `roadmap.md` Phase 0B Exit | done | finalization idempotence implemented; probe calls `ScreenshotFinal()` then `Flip()` | `test_rt_canvas3d_gpu_paths`, `g3d_walk_min_visual_probe` |  | ctests passed | Direct fake-backend `Flip` counter remains impractical because `Flip` enters `vgfx_update` |
| P0B-015 | Build passes on macOS, Windows, Linux | `roadmap.md` Phase 0B Exit | partial | local macOS build passed |  |  | `cmake --build build -j...` | Windows/Linux pending |
| P0B-016 | `check_runtime_completeness.sh` passes | `roadmap.md` Phase 0B Exit | done | runtime.def complete | script |  | `scripts/check_runtime_completeness.sh` |  |
| P0B-017 | `ctest --test-dir build -L graphics3d` passes | `roadmap.md` Phase 0B Exit | done | graphics3d regression green | 48/48 ctests |  | `ctest --test-dir build -L graphics3d --output-on-failure` | Includes Phase 0A surface probe, synthetic input, quality profiles, model asset loading, Game3D Phase 1-4 probes, and `walk_min` visual probe |

## Phase 0C - deterministic test input, clock, and backend-safe quality

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| P0C-001 | Implement backend-safe quality policy | `roadmap.md` Phase 0C | done | `Canvas3D.SetQuality`, `PostFX3D.NewQuality`, fallback inspection | `test_rt_canvas3d_gpu_paths`, `g3d_test_canvas3d_quality_profiles` | `docs/graphics3d-guide.md`, `docs/viperlib/graphics/rendering3d.md` | focused ctests passed | Game3D wrappers completed in Phase 3 |
| P0C-002 | Implement synthetic input and synthetic clock | `roadmap.md` Phase 0C | done | Canvas3D input/clock source APIs implemented in C runtime and runtime.def | `test_rt_canvas3d_gpu_paths`, `g3d_test_canvas3d_synthetic_input` | `docs/graphics3d-guide.md`, `docs/viperlib/graphics/rendering3d.md` | focused ctests passed | Runtime-first; no Zia helper library |
| P0C-003 | Add `World3D.runFrames` probe scaffolding if needed | `roadmap.md` Phase 0C | done | Not needed before Game3D runtime exists; Canvas3D deterministic frame APIs cover the Phase 0C contract | `g3d_test_canvas3d_synthetic_input` |  | ctest passed | Not a separate runtime |
| P0C-004 | Add software-backend probes for every quality profile | `roadmap.md` Phase 0C | done | Performance/Balanced/Cinematic applied on software without GPU-only traps | `g3d_test_canvas3d_quality_profiles` |  | ctest passed |  |
| P0C-005 | Prove scripted WASD + mouse-look is deterministic across two runs | `roadmap.md` Phase 0C Exit | done | Canvas3D deterministic camera probe compares two identical scripted frames | `g3d_test_canvas3d_synthetic_input` |  | ctest passed | Full Game3D simulation-state proof remains in later `World3D.runFrames` rows |
| P0C-006 | Final-frame comparison uses tolerance/region checks | `roadmap.md` Phase 0C Exit | done | `walk_min` visual probe uses region/tolerance assertions and baseline diff | `g3d_walk_min_visual_probe` |  | graphics3d ctest passed |  |
| P0C-007 | Software backend applies all quality profiles without traps | `roadmap.md` Phase 0C Exit | done | software ctest runs Performance/Balanced/Cinematic through finalization/screenshot | `g3d_test_canvas3d_quality_profiles` |  | ctest passed |  |
| P0C-008 | Debug/capability output reports quality fallback | `roadmap.md` Phase 0C Exit | done | Canvas3D fallback flag/reason exposed and Debug3D reports requested/active/fallback state | `test_rt_canvas3d_gpu_paths`, `g3d_test_canvas3d_quality_profiles`, `g3d_test_game3d_presets_probe` | docs updated | focused ctests passed |  |

## Phase 0D - package-aware model/audio asset loading

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| P0D-001 | Implement chosen `Model3D.LoadAsset` or `Model3D.LoadResolved` contract | `roadmap.md` Phase 0D | done | `Model3D.LoadAsset` + `GLTF.LoadAsset` in C runtime; `asset://` support in `Viper.IO.Assets` | `test_rt_model3d`, `g3d_test_model3d_load_asset`, surface link tests | `docs/graphics3d-guide.md`, `docs/viperlib/io/assets.md` | focused ctests passed | D-006 resolved as runtime-level `LoadAsset`; no Zia helper library |
| P0D-002 | Support glTF external `.bin` dependencies through mounted asset resolver | `roadmap.md` Phase 0D | done | `rt_gltf_load_asset` resolves buffer URIs relative to parent asset through `rt_asset_load_raw` | `test_rt_gltf`, `test_rt_model3d` | docs updated | focused ctests passed |  |
| P0D-003 | Support glTF texture dependencies through mounted asset resolver | `roadmap.md` Phase 0D | done | external image URIs decode through mounted asset bytes before filesystem fallback | `test_rt_gltf` | docs updated | focused ctest passed |  |
| P0D-004 | Add package-aware audio loading if sound loading is path-only | `roadmap.md` Phase 0D | done | `Sound.LoadAsset` in canonical and compatibility audio namespaces | `test_rt_audio_integration`, `test_rt_audio_surface_link` | `docs/viperlib/audio.md` | focused ctests passed |  |
| P0D-005 | Add committed GLB fixture | `roadmap.md` Phase 0D | partial | GLB fixture is generated in `test_rt_gltf` to avoid binary fixture churn | `test_rt_gltf` |  | focused ctest passed | Commit a binary fixture later only if needed for cross-tool parity |
| P0D-006 | Add committed glTF fixture with external dependencies | `roadmap.md` Phase 0D | partial | mounted-pack external `.bin`/PNG fixtures generated in C tests; data-URI Zia fixture committed | `test_rt_gltf`, `g3d_test_model3d_load_asset` |  | focused ctests passed |  |
| P0D-007 | Add short committed audio fixture | `roadmap.md` Phase 0D | partial | WAV fixture is generated in audio ctest and packed into VPA | `test_rt_audio_integration` |  | focused ctest passed | Commit a reusable audio fixture if later sample/docs need one |
| P0D-008 | Prove GLB loads via filesystem and mounted package path | `roadmap.md` Phase 0D Exit | done | `GLTF.LoadAsset` loads generated GLB from `/tmp` and mounted VPA | `test_rt_gltf` | docs updated | focused ctest passed |  |
| P0D-009 | Prove glTF external dependencies resolve through asset system | `roadmap.md` Phase 0D Exit | done | external buffer and texture assets loaded from mounted VPA | `test_rt_gltf`, `test_rt_model3d` | docs updated | focused ctests passed |  |
| P0D-010 | Missing dependency diagnostics name model and dependency path | `roadmap.md` Phase 0D Exit | done | `GLTF.LoadAsset` trap includes parent model and resolved dependency | `test_rt_model3d` | docs updated | focused ctest passed |  |
| P0D-011 | Model template caching does not leak retained assets | `roadmap.md` Phase 0D Exit | todo |  |  |  |  |  |

## Phase 1 - core Game3D

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| P1-001 | Implement runtime-backed `Viper.Game3D` classes/functions in C runtime tree | `roadmap.md` Phase 1 | done | `src/runtime/graphics/3d/rt_game3d.c`, `rt_game3d.h` | `test_rt_game3d`; Zia probes | `docs/viperlib/graphics/game3d.md` | focused ctests passed | Normal C runtime implementation; not a Zia library |
| P1-002 | Add `runtime.def` entries, headers, disabled-graphics stubs, docs, completeness checks | `roadmap.md` Phase 1 | done | `runtime.def`, runtime signatures/classes, CMake, graphics stubs | focused build passed; graphics3d 48/48 | Game3D docs added | `scripts/check_runtime_completeness.sh` passed |  |
| P1-003 | Add only thin Zia sample/import convenience where useful | `roadmap.md` Phase 1 | done | Zia probes consume runtime namespace directly | `g3d_test_game3d_world_probe`, `g3d_test_game3d_runframes_probe` | docs explain runtime-first policy | focused ctests passed | No authoritative Zia helper package added |
| P1-004 | `World3D.New` creates canvas/camera/scene/physics/input/audio/effects | `roadmap.md` Phase 1 | done | `rt_game3d_world_new_with_camera` | `test_rt_game3d`, `g3d_test_game3d_world_probe` | Game3D docs | focused ctests passed |  |
| P1-005 | `World3D.New` applies explicit default environment | `roadmap.md` Phase 1 | done | default camera, lighting, ambient, quality, frustum culling | `test_rt_game3d`, `g3d_test_game3d_world_probe` | Game3D docs | focused ctests passed | Additional environment presets completed in Phase 3 |
| P1-006 | Implement `run`, `runFixed`, `runFrames` | `roadmap.md` Phase 1 | partial | Native callback loops plus `runFramesOnly` | C native callback path; Zia `runFramesOnly`; callback rejection probe | Game3D callback-boundary docs | focused ctests passed | Interpreted Zia callback trampoline is deferred; manual APIs are required in interpreted Zia |
| P1-007 | Implement manual loop methods and final-frame capture | `roadmap.md` Phase 1 | done | `tick`, `stepSimulation`, `beginFrame`, `drawScene`, `drawEffects`, `endScene`, `captureFinalFrame`, `present` | `test_rt_game3d`, `g3d_test_game3d_world_probe` | Game3D docs | focused ctests passed |  |
| P1-008 | Implement documented frame order | `roadmap.md` Phase 1 | done | managed loop helpers share `game3d_world_render_once` and manual frame stages | `test_rt_game3d` | Game3D docs | focused ctests passed |  |
| P1-009 | Implement `LayerMask`, entity layer/mask plumbing, body ownership registry | `roadmap.md` Phase 1 | done | masks, entity layer/mask, raw body attachment, world registry | `test_rt_game3d`, `g3d_test_game3d_world_probe`, `g3d_test_game3d_physics_probe`, `g3d_test_game3d_collision_probe` | Game3D docs | focused ctests passed | Higher-level `BodyDef` completed in Phase 4 |
| P1-010 | Implement spawn/despawn cleanup and resize/aspect handling | `roadmap.md` Phase 1 | done | recursive spawn/despawn, registry cleanup, `onResize`, tick aspect sync | `test_rt_game3d`, `g3d_test_game3d_world_probe` | Game3D docs | focused ctests passed | Projection visual/no-distortion probe remains runtime-contract follow-up |
| P1-011 | Implement `World3D.destroy`, entity state queries, invalid-handle diagnostics | `roadmap.md` Phase 1 | partial | destroy/isDestroyed/isSpawned/isDestroyed and trap guards | `test_rt_game3d` | Game3D docs | focused ctests passed | More negative destroyed-handle Zia diagnostics still tracked in runtime contracts |
| P1-012 | Document raw escape-hatch lifetimes | `roadmap.md` Phase 1 | done |  |  | `docs/viperlib/graphics/game3d.md` | local docs update | Escape hatches still need richer examples |
| P1-013 | `Input3D` reads named keyboard/mouse state, not `Canvas3D.Poll()` bitmasks | `roadmap.md` Phase 1 | done | `rt_game3d_input_*` over runtime keyboard/mouse APIs | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| P1-014 | API-spec hello-world compiles and renders | `roadmap.md` Phase 1 Exit | partial | docs quick start mirrors passing world probe structure | `g3d_test_game3d_world_probe` renders/captures | Game3D docs | focused ctest passed | Dedicated snippet ctest still pending |
| P1-015 | `world_probe.zia` covers construction, cleanup, find, resize, final-frame render, tick, step, runFrames, destroy, diagnostics | `roadmap.md` Phase 1 Exit | partial | world/runframes/callback-reject probes added | `g3d_test_game3d_world_probe`, `g3d_test_game3d_runframes_probe`, `g3d_test_game3d_runframes_callback_reject`; graphics3d 48/48 | Game3D docs | ctests passed | Tick/step and destroyed-handle diagnostics need explicit Zia probe coverage |
| P1-016 | No common-case sample uses `Mat4.` | `roadmap.md` Phase 1 Exit | partial | Game3D docs/probes and `walk_min` use entity transform helpers | `g3d_test_game3d_world_probe`; `g3d_walk_min_visual_probe` | Game3D docs; `examples/3d/README.md` | focused ctests passed | Final starter/showcase samples still pending |

## Phase 2 - cameras and walkable movement

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| P2-001 | Implement `FirstPersonController` | `roadmap.md` Phase 2 | done | `rt_game3d_first_person_controller_*` in C runtime | `test_rt_game3d`; `g3d_test_game3d_character_controller_probe` | Game3D docs | focused tests passed | Drives camera directly or a `CharacterController3D` |
| P2-002 | Implement `FreeFlyController` | `roadmap.md` Phase 2 | done | `rt_game3d_free_fly_controller_*` | `test_rt_game3d`; `g3d_test_game3d_camera_controllers_probe` | Game3D docs | focused tests passed | WASD/vertical movement plus mouse look |
| P2-003 | Implement `OrbitController` | `roadmap.md` Phase 2 | done | `rt_game3d_orbit_controller_*` | `test_rt_game3d`; `g3d_test_game3d_camera_controllers_probe` | Game3D docs | focused tests passed | Drag orbit, wheel zoom, pitch clamp |
| P2-004 | Implement `FollowController` | `roadmap.md` Phase 2 | done | `rt_game3d_follow_controller_*` | `test_rt_game3d`; `g3d_test_game3d_camera_controllers_probe` | Game3D docs | focused tests passed | Late-update entity tracking after physics/sync |
| P2-005 | Implement `CharacterController3D` | `roadmap.md` Phase 2 | done | `rt_game3d_character_controller_*` wrapping `Viper.Graphics3D.Character3D` | `test_rt_game3d`; `g3d_test_game3d_character_controller_probe` | Game3D docs | focused tests passed | Camera-relative movement, gravity, jump, teleport, grounded |
| P2-006 | Use two-phase controller contract (`update` before physics, `lateUpdate` after sync) | `roadmap.md` Phase 2 | done | `World3D.stepSimulation` dispatches update before `rt_world3d_step` and lateUpdate after scene/audio sync | C and Zia controller probes | Game3D docs | focused tests passed | Manual loops can adjust controller state between `tick` and `stepSimulation` |
| P2-007 | Wire mouse capture/release for first-person controllers | `roadmap.md` Phase 2 | done | First-person and free-fly capture/release methods and per-update capture policy | build + controller tests | Game3D docs | focused tests passed | Uses runtime mouse capture APIs |
| P2-008 | Synthetic-input probes deterministically change camera pose | `roadmap.md` Phase 2 Exit | done | Controller dispatch consumes Canvas3D synthetic input | `g3d_test_game3d_camera_controllers_probe` | Game3D docs | direct probe passed | Free-fly camera pose changes from scripted W/mouse |
| P2-009 | First-person character movement is not one frame late | `roadmap.md` Phase 2 Exit | done | Character update happens before physics in `stepSimulation` | `test_rt_game3d`; `g3d_test_game3d_character_controller_probe` | Game3D docs | focused tests passed | One `runFramesOnly` frame moves the player entity |
| P2-010 | Follow camera tracks post-physics entity pose | `roadmap.md` Phase 2 Exit | done | Follow controller lateUpdate after physics/sync | `test_rt_game3d`; `g3d_test_game3d_camera_controllers_probe` | Game3D docs | focused tests passed | Target move before `stepSimulation` is observed in same frame |
| P2-011 | `walk_min` supports walking with collision/grounding | `roadmap.md` Phase 2 Exit | done | `walk_min` now uses `World3D`, `FirstPersonController`, `CharacterController3D`, static ground/prop colliders, and `Entity3D` scene props | `g3d_walk_min_visual_probe` checks final frame plus grounded synthetic movement | `examples/3d/README.md`; Game3D docs | focused ctest passed | Probe uses a fresh movement instance with synthetic input source enabled |
| P2-012 | No hand-authored camera `LookAt` loop needed in game code | `roadmap.md` Phase 2 Exit | done | Built-in controllers call Camera3D helpers internally | Controller probes and docs examples | Game3D docs | focused tests passed | Raw `Camera3D.LookAt` remains an escape hatch |

## Phase 3 - presets, prefabs, quality, environment, debug

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| P3-001 | Implement `Lighting.*` | `roadmap.md` Phase 3 | done | `Lighting.Studio/Outdoor/Night/Interior/Clear` in `rt_game3d.c` | `test_rt_game3d`, `g3d_test_game3d_presets_probe` | Game3D docs | focused and graphics3d ctests passed | Runtime C surface, not a Zia helper package |
| P3-002 | Implement `Materials.*` | `roadmap.md` Phase 3 | done | `Materials.Plastic/Metal/Rubber/Glass/Emissive/Unlit/FromAlbedoMap` | `test_rt_game3d`, `g3d_test_game3d_presets_probe` | Game3D docs | focused and graphics3d ctests passed | PBR/default material ranges covered |
| P3-003 | Implement `PostFX.*` | `roadmap.md` Phase 3 | done | `PostFX.Cinematic/Crisp/None` update world effect registry and canvas | `test_rt_game3d`, `g3d_test_game3d_presets_probe` | Game3D docs | focused and graphics3d ctests passed | Uses backend-safe software/GPU-compatible chains |
| P3-004 | Implement `Quality.Apply` | `roadmap.md` Phase 3 | done | Applies Canvas3D quality, frustum culling, and shadow policy by capability | `test_rt_game3d`, `g3d_test_game3d_presets_probe` | Game3D docs | focused and graphics3d ctests passed | Valid active profile after fallback |
| P3-005 | Implement `Prefab.*` | `roadmap.md` Phase 3 | done | `Prefab.Box/BoxXYZ/Sphere/Cylinder/Plane/Ground` | `test_rt_game3d`, `g3d_test_game3d_presets_probe` | Game3D docs | focused and graphics3d ctests passed | Returns normal spawnable `Entity3D` |
| P3-006 | Implement `Environment.*` | `roadmap.md` Phase 3 | done | `Environment.Outdoor/Sunset/Overcast/Night` plus `EnvHandle.withTerrain/withWater/withFog` | `test_rt_game3d`, `g3d_test_game3d_presets_probe` | Game3D docs | focused and graphics3d ctests passed | Terrain is a ground entity with static body in this phase |
| P3-007 | Implement `Debug3D` | `roadmap.md` Phase 3 | done | `Debug3D.ShowOverlay/DrawAxes/DrawPhysics/DrawCameraInfo/DrawCapabilities` | `test_rt_game3d`, `g3d_test_game3d_presets_probe` | Game3D docs | focused and graphics3d ctests passed | Text uses final overlay; axes/physics render in `drawEffects` |
| P3-008 | Quality controls shadows/post-FX/culling/diagnostics capability-safely | `roadmap.md` Phase 3 | done | `Quality.Apply` gates shadows via backend capability and keeps Canvas3D fallback state | `test_rt_game3d`, `g3d_test_game3d_presets_probe` | Game3D docs | focused and graphics3d ctests passed | Software backend safely disables shadow maps |
| P3-009 | Debug shows backend, caps, quality/fallback, FPS, draw/cull counts, axes, camera, physics | `roadmap.md` Phase 3 | done | Debug overlay text plus 3D axes/AABB wire debug | `test_rt_game3d`, `g3d_test_game3d_presets_probe` | Game3D docs | focused and graphics3d ctests passed | Physics debug uses body AABBs |
| P3-010 | Preset swaps visibly change sample | `roadmap.md` Phase 3 Exit | done | Lighting changes clear/ambient/lights; environment changes sky/fog/ground/water; post-FX swaps chains | `test_rt_game3d`, `g3d_test_game3d_presets_probe` | Game3D docs | focused and graphics3d ctests passed | Dedicated visual baseline for presets can be added later if needed |
| P3-011 | Each prefab returns a spawnable entity | `roadmap.md` Phase 3 Exit | done | Prefabs create `Entity3D` with mesh/material and ground layer | `test_rt_game3d`, `g3d_test_game3d_presets_probe` | Game3D docs | focused and graphics3d ctests passed | Zia probe spawns box/sphere/ground |
| P3-012 | `Environment.*` yields instantly respectable scene | `roadmap.md` Phase 3 Exit | done | Environment constructors apply lighting, fog, clear color, terrain, and optional water | `test_rt_game3d`, `g3d_test_game3d_presets_probe` | Game3D docs | focused and graphics3d ctests passed | Outdoor/sunset/overcast/night constructors covered in C |
| P3-013 | Debug text uses final overlay path | `roadmap.md` Phase 3 Exit | done | `World3D.endScene` records Debug3D overlay after `Canvas3D.End` | `test_rt_game3d`, `g3d_test_game3d_presets_probe` | Game3D docs | focused and graphics3d ctests passed | Captured via `captureFinalFrame` |
| P3-014 | `presets_probe.zia` validates ranges and backend fallback | `roadmap.md` Phase 3 Exit | done | `tests/runtime/test_game3d_presets_probe.zia` registered under ctest | `g3d_test_game3d_presets_probe` | Game3D docs | ctest passed | Included in focused and full graphics3d validation |

## Phase 4 - assets, physics, and collision events

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| P4-001 | Implement `Assets3D.LoadModel` | `roadmap.md` Phase 4 | done | runtime C wrapper instantiates group entity from `Model3D.Load` | `test_rt_game3d`, `g3d_test_game3d_assets_probe` | Game3D docs | focused ctests passed | Filesystem/dev path |
| P4-002 | Implement `Assets3D.LoadModelAsset` | `roadmap.md` Phase 4 | done | runtime C wrapper instantiates group entity from `Model3D.LoadAsset` | `test_rt_game3d`, `g3d_test_game3d_assets_probe` | Game3D docs | focused ctests passed | Asset resolver path, filesystem fallback covered |
| P4-003 | Implement `Assets3D.LoadModelTemplate` | `roadmap.md` Phase 4 | done | cached `ModelTemplate` over filesystem `Model3D` | `test_rt_game3d`, `g3d_test_game3d_assets_probe` | Game3D docs | focused ctests passed | C test verifies cache identity |
| P4-004 | Implement `Assets3D.LoadModelTemplateAsset` | `roadmap.md` Phase 4 | done | cached asset-resolver `ModelTemplate` | `test_rt_game3d`, `g3d_test_game3d_assets_probe` | Game3D docs | focused ctests passed | Records `isAsset` |
| P4-005 | Implement `BodyDef` | `roadmap.md` Phase 4 | done | runtime C BodyDef class with shape, material, filter, trigger, CCD, and sync fields | `test_rt_game3d`, `g3d_test_game3d_physics_probe` | Game3D docs | focused ctests passed | StaticPlane is shallow world-layer box |
| P4-006 | Implement `Entity3D.attachBody` | `roadmap.md` Phase 4 | done | accepts BodyDef and raw Physics3DBody, binds node/body, applies filters, registers spawned bodies | `test_rt_game3d`, `g3d_test_game3d_physics_probe` | Game3D docs | focused ctests passed | Raw body remains escape hatch |
| P4-007 | Implement `Collision3DEvent` | `roadmap.md` Phase 4 | done | runtime wrapper exposes phase, owning entities, raw event, trigger, speed/impulse, point/normal/other | `test_rt_game3d`, `g3d_test_game3d_collision_probe` | Game3D docs | focused ctests passed | Entity lookup uses world body-owner registry |
| P4-008 | Implement runtime collision event buffer accessors | `roadmap.md` Phase 4 | done | `collisionEventCount` and `collisionEvent` expose enter/stay/exit/any buffers | `test_rt_game3d`, `g3d_test_game3d_collision_probe` | Game3D docs | focused ctests passed | Any iterates enter, stay, then exit |
| P4-009 | Implement optional `World3D.onCollision` and `onCollisionSimple` if approved | `roadmap.md` Phase 4 | deferred | not shipped because interpreted-Zia callbacks are not C-callable yet | callback rejection probe covers current boundary | Game3D docs | explicit waiver | Event-buffer polling is supported path |
| P4-010 | Model loads return group entities with root subtree and populated `entity.anim` when skinned | `roadmap.md` Phase 4 | done | instantiated model roots become group Entity3D values; root skeletal animator copied into `entity.anim` if present | `test_rt_game3d`, `g3d_test_game3d_assets_probe` | Game3D docs | focused ctests passed | Skinned animation behavior expands in Phase 5 |
| P4-011 | Collision dispatch uses body-owner registry and enter/stay/exit buffers | `roadmap.md` Phase 4 | done | World3D resolves collision bodies to owning Game3D entities | `test_rt_game3d`, `g3d_test_game3d_collision_probe` | Game3D docs | focused ctests passed |  |
| P4-012 | Layer masks apply to `Physics3DBody.CollisionLayer` / `CollisionMask` | `roadmap.md` Phase 4 | done | BodyDef and entity masks propagate to attached bodies | `test_rt_game3d`, `g3d_test_game3d_physics_probe` | Game3D docs | focused ctests passed |  |
| P4-013 | GLB/glTF renders with hierarchy/materials intact from filesystem and package | `roadmap.md` Phase 4 Exit | partial | Game3D model loading preserves hierarchy; package-aware loading proven at lower Model3D/GLTF layer | `test_rt_game3d`, `g3d_test_game3d_assets_probe`, `test_rt_gltf`, `test_rt_model3d` | Game3D docs | focused ctests passed | Visual render proof belongs in starter/showcase |
| P4-014 | Dropped physics ball rests on ground body | `roadmap.md` Phase 4 Exit | done | BodyDef sphere falls onto StaticPlane and settles within expected height/velocity bounds | `g3d_test_game3d_physics_probe` | Game3D docs | focused ctest passed |  |
| P4-015 | Runtime collision events expose phase, point/normal, speed, impulse, trigger, entities | `roadmap.md` Phase 4 Exit | done | Collision3DEvent wrapper over raw event buffers | `test_rt_game3d`, `g3d_test_game3d_collision_probe` | Game3D docs | focused ctests passed |  |
| P4-016 | Optional simple callbacks fire once on enter by default if shipped | `roadmap.md` Phase 4 Exit | deferred | callback sugar not shipped under current VM callback policy | callback rejection probe | Game3D docs | explicit waiver | Use `collisionEventCount` / `collisionEvent` in Zia |
| P4-017 | `assets_probe.zia`, `physics_probe.zia`, and `collision_probe.zia` pass | `roadmap.md` Phase 4 Exit | done | probes added and registered in CTest | `g3d_test_game3d_assets_probe`, `g3d_test_game3d_physics_probe`, `g3d_test_game3d_collision_probe` | Game3D docs | focused ctests passed |  |

## Phase 5 - animation

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| P5-001 | Implement runtime-backed `Animator3D` over `AnimController3D`/`Skeleton3D` | `roadmap.md` Phase 5 | todo |  |  |  |  |  |
| P5-002 | Expose `entity.anim` | `roadmap.md` Phase 5 | todo |  |  |  |  |  |
| P5-003 | Advance animation in documented pre-physics phase | `roadmap.md` Phase 5 | todo |  |  |  |  |  |
| P5-004 | `SyncMode.NodeFromAnimRootMotion` drives node transforms deterministically | `roadmap.md` Phase 5 | todo |  |  |  |  |  |
| P5-005 | Add `stateTime()` backed by runtime getter | `roadmap.md` Phase 5 | todo |  |  |  |  |  |
| P5-006 | Skinned glTF plays and crossfades animations | `roadmap.md` Phase 5 Exit | todo |  |  |  |  |  |
| P5-007 | Runtime animation event accessors report named events at right times | `roadmap.md` Phase 5 Exit | todo |  |  |  |  |  |
| P5-008 | Optional `onAnimEvent` sugar dispatches if approved | `roadmap.md` Phase 5 Exit | todo |  |  |  |  |  |
| P5-009 | Root motion moves entity node deterministically | `roadmap.md` Phase 5 Exit | todo |  |  |  |  |  |
| P5-010 | `anim_probe.zia` passes under `runFrames` | `roadmap.md` Phase 5 Exit | todo |  |  |  |  |  |

## Phase 6 - 3D audio and VFX

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| P6-001 | Implement runtime-backed `Audio3D` on `world` | `roadmap.md` Phase 6 | todo |  |  |  |  |  |
| P6-002 | Implement listener-follows-camera | `roadmap.md` Phase 6 | todo |  |  |  |  |  |
| P6-003 | Implement audio `load`/`loadAsset`/`playAt`/`playAttached`/`play2D` | `roadmap.md` Phase 6 | todo |  |  |  |  |  |
| P6-004 | Implement runtime-backed `EffectRegistry3D` | `roadmap.md` Phase 6 | todo |  |  |  |  |  |
| P6-005 | Implement `Effects3D` presets: explosion/sparks/dust/smoke/impact decal | `roadmap.md` Phase 6 | todo |  |  |  |  |  |
| P6-006 | Effects update, draw, and expire through `World3D` | `roadmap.md` Phase 6 | todo |  |  |  |  |  |
| P6-007 | Positional sound attenuates with listener distance | `roadmap.md` Phase 6 Exit | todo |  |  |  |  |  |
| P6-008 | Attached sound follows entity after scene/body sync | `roadmap.md` Phase 6 Exit | todo |  |  |  |  |  |
| P6-009 | Each VFX preset spawns and auto-expires without leaking | `roadmap.md` Phase 6 Exit | todo |  |  |  |  |  |
| P6-010 | Collision events can trigger positional sound and effect | `roadmap.md` Phase 6 Exit | todo |  |  |  |  |  |
| P6-011 | `audio_probe.zia` and `effects_probe.zia` pass | `roadmap.md` Phase 6 Exit | todo |  |  |  |  |  |

## Phase 7 - showcase sample, docs, starter, and bowling migration

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| P7-001 | Build `examples/3d/game3d_showcase/` cohesive mini-game | `roadmap.md` Phase 7 | todo |  |  |  |  | Full feature stack |
| P7-002 | Showcase uses `runFixed` and `runFrames` deterministic replay | `roadmap.md` Phase 7 | todo |  |  |  |  |  |
| P7-003 | Showcase includes final-frame post-FX plus crisp HUD/debug overlay | `roadmap.md` Phase 7 | todo |  |  |  |  |  |
| P7-004 | Showcase includes lighting/material/post-FX/quality/environment toggles | `roadmap.md` Phase 7 | todo |  |  |  |  |  |
| P7-005 | Showcase includes terrain/ground, water or fog, prefabs, packaged GLB/glTF props | `roadmap.md` Phase 7 | todo |  |  |  |  |  |
| P7-006 | Showcase includes first-person and follow/orbit cameras | `roadmap.md` Phase 7 | todo |  |  |  |  |  |
| P7-007 | Showcase includes character controller or player-controlled physics body | `roadmap.md` Phase 7 | todo |  |  |  |  |  |
| P7-008 | Showcase includes layers, bodies, triggers, collision event handling | `roadmap.md` Phase 7 | todo |  |  |  |  |  |
| P7-009 | Showcase includes animated skinned model with animation events | `roadmap.md` Phase 7 | todo |  |  |  |  |  |
| P7-010 | Showcase includes positional, attached, and non-spatial audio | `roadmap.md` Phase 7 | todo |  |  |  |  |  |
| P7-011 | Showcase includes VFX particles/decals from collisions | `roadmap.md` Phase 7 | todo |  |  |  |  |  |
| P7-012 | Showcase includes package-aware model/audio loading | `roadmap.md` Phase 7 | todo |  |  |  |  |  |
| P7-013 | Showcase includes raw escape-hatch usage in marked advanced section | `roadmap.md` Phase 7 | todo |  |  |  |  |  |
| P7-014 | Port/re-skin 3D bowling setup to Game3D | `roadmap.md` Phase 7 | todo |  |  |  |  |  |
| P7-015 | Record bowling setup-code reduction and clarity notes | `roadmap.md` Phase 7 | todo |  |  |  |  | Target >= 50% scaffolding reduction |
| P7-016 | Add `examples/3d/game3d_starter/` or project-template equivalent | `roadmap.md` Phase 7 | todo |  |  |  |  | Source/assets/package/test |
| P7-017 | Add docs under `docs/viperlib/graphics/` for all planned topics | `roadmap.md` Phase 7 | todo |  |  |  |  | See docs tracker |
| P7-018 | Update API reference for every new runtime function and Game3D API area | `roadmap.md` Phase 7 | todo |  |  |  |  |  |
| P7-019 | Runtime docs distinguish `End`, `FinalizeFrame`, `ScreenshotFinal`, `Flip` | `roadmap.md` Phase 7 | todo |  |  |  |  |  |
| P7-020 | Every docs page includes a copy-pasteable snippet covered by ctest/import | `roadmap.md` Phase 7 | todo |  |  |  |  |  |
| P7-021 | Showcase runs at interactive framerate on at least one GPU backend | `roadmap.md` Phase 7 Exit | todo |  |  |  |  |  |
| P7-022 | Showcase has documented software-backend baseline | `roadmap.md` Phase 7 Exit | todo |  |  |  |  |  |
| P7-023 | Showcase final-frame visual regression passes on software backend | `roadmap.md` Phase 7 Exit | todo |  |  |  |  |  |
| P7-024 | Showcase deterministic replay reaches same gameplay state | `roadmap.md` Phase 7 Exit | todo |  |  |  |  |  |
| P7-025 | Starter can be copied/generated, run, package assets, and execute deterministic ctest | `roadmap.md` Phase 7 Exit | todo |  |  |  |  |  |
| P7-026 | Docs contain copy-pasteable examples that compile | `roadmap.md` Phase 7 Exit | todo |  |  |  |  |  |
| P7-027 | All new runtime functions/API areas/samples/docs snippets have ctest coverage or waiver | `roadmap.md` Phase 7 Exit | todo |  |  |  |  |  |
