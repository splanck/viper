# Runtime contract tracker

This file tracks every runtime contract item from `runtime-changes.md`.

## 1. Frame, post-FX, overlay, and final screenshot

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-FRAME-001 | Correct `rt_canvas3d_end` comments/docs for `End` vs `Flip` semantics | `runtime-changes.md` §1 | done | `rt_canvas3d.c` comment fixed |  | runtime comment | local diff | Docs page still tracked in R-DOC rows |
| R-FRAME-002 | Add final overlay command queue | `runtime-changes.md` §1 | done | `final_overlay_*` queue in `rt_canvas3d` | `test_rt_canvas3d_gpu_paths` | header/runtime comments | focused ctest passed | Not a thin alias for current `Begin2D` |
| R-FRAME-003 | Add `Canvas3D.BeginOverlay()` | `runtime-changes.md` §1 | done | runtime + `runtime.def` | `test_rt_canvas3d_gpu_paths` | header comment | focused ctest passed | Working name |
| R-FRAME-004 | Add `Canvas3D.EndOverlay()` | `runtime-changes.md` §1 | done | runtime + `runtime.def` | `test_rt_canvas3d_gpu_paths` | header comment | focused ctest passed | Working name |
| R-FRAME-005 | Add `Canvas3D.ClearOverlay()` | `runtime-changes.md` §1 | done | runtime + `runtime.def` | `test_rt_canvas3d_gpu_paths` | header comment | focused ctest passed | Working name |
| R-FRAME-006 | Preserve legacy `DrawRect2D` / `DrawText2D` auto-frame behavior | `runtime-changes.md` §1 | done | legacy helpers unchanged | `ctest -L graphics3d` |  | 35/35 graphics3d passed | Document as legacy/convenience |
| R-FRAME-007 | Add idempotent `Canvas3D.FinalizeFrame()` helper | `runtime-changes.md` §1 | done | runtime + `runtime.def` | `test_rt_canvas3d_gpu_paths` | header comment | focused ctest passed | Public/internal decision D-005 |
| R-FRAME-008 | Add `Canvas3D.ScreenshotFinal()` | `runtime-changes.md` §1 | done | runtime + `runtime.def` | `test_rt_canvas3d_gpu_paths` | header comment | focused ctest passed | Must not present |
| R-FRAME-009 | Allow `Flip()` after `ScreenshotFinal()` without re-running post-FX/overlay | `runtime-changes.md` §1 | done | finalization state prevents duplicate finalize | `test_rt_canvas3d_gpu_paths`, `g3d_walk_min_visual_probe` |  | ctests passed | Visual probe executes `ScreenshotFinal()` before `Flip()` |
| R-FRAME-010 | Add `Canvas3D.get_FrameFinalized` | `runtime-changes.md` §1 | done | runtime + `runtime.def` | `test_rt_canvas3d_gpu_paths` | header comment | focused ctest passed |  |
| R-FRAME-011 | Add `BackendSupports("postfx-overlay")` | `runtime-changes.md` §1 | done | capability bit + aliases | `test_rt_canvas3d_production` | header comment | focused ctest passed |  |
| R-FRAME-012 | Add `BackendSupports("final-screenshot")` | `runtime-changes.md` §1 | done | capability bit + aliases | `test_rt_canvas3d_production` | header comment | focused ctest passed |  |
| R-FRAME-013 | Add `BackendSupports("gpu-postfx-overlay")` if GPU backend has distinct path | `runtime-changes.md` §1 | done | capability string recognized and false by default | `test_rt_canvas3d_production` | header comment | focused ctest passed | GPU backend path still not advertised |
| R-FRAME-014 | Implement software/CPU finalization path: scene post-FX then overlay replay | `runtime-changes.md` §1 | done | `FinalizeFrame` applies CPU post-FX then replays final overlay | `test_rt_canvas3d_gpu_paths` | runtime comment | focused ctest passed |  |
| R-FRAME-015 | Implement or capability-gate GPU after-postFX overlay path | `runtime-changes.md` §1 | done | `gpu-postfx-overlay` is capability-gated off | `test_rt_canvas3d_production` |  | focused ctest passed | Future backend work can turn this on |
| R-FRAME-016 | Define render-target finalization behavior or capability limitation | `runtime-changes.md` §1 | todo |  |  |  |  |  |
| R-FRAME-017 | C test: `FinalizeFrame()` idempotent | `runtime-changes.md` §1 Tests | done |  | `test_rt_canvas3d_gpu_paths` |  | focused ctest passed |  |
| R-FRAME-018 | C test: `ScreenshotFinal()` then `Flip()` does not double-apply | `runtime-changes.md` §1 Tests | partial | C idempotence/readback tests cover the finalization side | `test_rt_canvas3d_gpu_paths`; Zia `g3d_walk_min_visual_probe` covers the public `Flip` sequence |  | ctests passed | Direct fake-backend `Flip` counter would enter real `vgfx_update` |
| R-FRAME-019 | Zia probe: bright/blooming scene plus crisp final overlay | `runtime-changes.md` §1 Tests | done | `examples/3d/walk_min_probe.zia` | `g3d_walk_min_visual_probe` | docs updated | ctest passed | Cyan final-overlay swatch sampled after post-FX |
| R-FRAME-020 | Zia probe: `ScreenshotFinal()` includes scene post-FX and overlay | `runtime-changes.md` §1 Tests | done | `Canvas3D.ScreenshotFinal` in `walk_min_probe` | `g3d_walk_min_visual_probe` | docs updated | ctest passed | Compares to software baseline and structural regions |
| R-FRAME-021 | Regression: legacy overlay helpers still work | `runtime-changes.md` §1 Tests | done |  | `ctest -L graphics3d` |  | 35/35 graphics3d passed |  |
| R-FRAME-022 | Update 3D bowling smoke probes to capture final frames | `runtime-changes.md` §1 Tests | todo |  |  |  |  |  |

## 2. Explicit default lighting

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-LIGHT-001 | Add `Canvas3D.SetDefaultLighting()` | `runtime-changes.md` §2 | done | runtime + stubs + `runtime.def` | `test_rt_canvas3d` | `docs/graphics3d-guide.md`, `docs/viperlib/graphics/rendering3d.md` | focused ctest passed | Key/fill/ambient |
| R-LIGHT-002 | Add `Canvas3D.ClearLights()` | `runtime-changes.md` §2 | done | runtime + stubs + `runtime.def` | `test_rt_canvas3d` | `docs/graphics3d-guide.md`, `docs/viperlib/graphics/rendering3d.md` | focused ctest passed |  |
| R-LIGHT-003 | Keep implicit fallback lighting disabled in first pass | `runtime-changes.md` §2 | done | only explicit `SetDefaultLighting` installs direct lights | `test_rt_canvas3d` | docs updated | focused ctest passed | Decision D-007 still tracks whether fallback is ever added |
| R-LIGHT-004 | If implicit fallback is added, gate by flag and suppress after intentional clear | `runtime-changes.md` §2 | todo |  |  |  |  |  |
| R-LIGHT-005 | If implicit fallback is added, count both canvas-slot and scene-node lights | `runtime-changes.md` §2 | todo |  |  |  |  |  |
| R-LIGHT-006 | Unit test: default lighting params and ambient | `runtime-changes.md` §2 Tests | done |  | `test_canvas_default_lighting_and_clear_lights` |  | focused ctest passed |  |
| R-LIGHT-007 | Unit test: `ClearLights` removes every canvas-slot light | `runtime-changes.md` §2 Tests | done |  | `test_canvas_default_lighting_and_clear_lights` |  | focused ctest passed |  |
| R-LIGHT-008 | Visual test: single mesh has clear directional shading | `runtime-changes.md` §2 Tests | done | `walk_min` uses `SetDefaultLighting` | `g3d_walk_min_visual_probe` | docs updated | ctest passed | Luminance-region assertion rejects flat/dark output |
| R-LIGHT-009 | Regression: explicit dark scene remains dark | `runtime-changes.md` §2 Tests | todo |  |  |  |  |  |

## 3. Missing inspection and state controls

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-GET-001 | `Light3D.get_Type` | `runtime-changes.md` §3 | done | runtime + stubs + `runtime.def` | `test_rt_canvas3d` | `docs/graphics3d-guide.md`, `docs/viperlib/graphics/rendering3d.md` | focused ctest passed | Real + stub + `runtime.def` |
| R-GET-002 | `Light3D.get_Color -> Vec3` | `runtime-changes.md` §3 | done | runtime + stubs + `runtime.def` | `test_rt_canvas3d` | docs updated | focused ctest passed |  |
| R-GET-003 | `Light3D.get_Intensity -> f64` | `runtime-changes.md` §3 | done | runtime + stubs + `runtime.def` | `test_rt_canvas3d` | docs updated | focused ctest passed |  |
| R-GET-004 | `Light3D.get_Enabled` / `SetEnabled` | `runtime-changes.md` §3 | done | runtime + stubs + `runtime.def`; disabled lights skipped in params | `test_rt_canvas3d`, `test_rt_canvas3d_gpu_paths` | docs updated | focused ctest passed | Must affect `build_light_params` |
| R-GET-005 | `Light3D.get_Direction` | `runtime-changes.md` §3 | done | runtime + stubs + `runtime.def` | `test_rt_canvas3d` | docs updated | focused ctest passed |  |
| R-GET-006 | `Light3D.get_Position` | `runtime-changes.md` §3 | done | runtime + stubs + `runtime.def` | `test_rt_canvas3d` | docs updated | focused ctest passed |  |
| R-GET-007 | Optional `Light3D.get_CastsShadows` / `SetCastsShadows` | `runtime-changes.md` §3 | todo |  |  |  |  | Skip non-shadow-casting lights |
| R-GET-008 | `SceneNode3D.get_WorldPosition -> Vec3` | `runtime-changes.md` §3 | done | runtime + stubs + `runtime.def` | `test_rt_scene3d` | `docs/graphics3d-guide.md` | focused ctest passed |  |
| R-GET-009 | `SceneNode3D.get_WorldScale -> Vec3` | `runtime-changes.md` §3 | done | runtime + stubs + `runtime.def` | `test_rt_scene3d` | `docs/graphics3d-guide.md` | focused ctest passed |  |
| R-GET-010 | `Material3D.get_Color -> Vec3` | `runtime-changes.md` §3 | done | runtime + stubs + `runtime.def` | `test_rt_canvas3d` | `docs/graphics3d-guide.md` | focused ctest passed |  |
| R-GET-011 | `Material3D.get_AlphaMode` | `runtime-changes.md` §3 | done | already implemented | existing material tests | `docs/graphics3d-guide.md` | focused ctest passed | Existing runtime surface |
| R-GET-012 | `Material3D.get_ShadingModel` | `runtime-changes.md` §3 | done | runtime + stubs + `runtime.def` | `test_rt_canvas3d` | `docs/graphics3d-guide.md` | focused ctest passed |  |
| R-GET-013 | `Material3D.get_Unlit` | `runtime-changes.md` §3 | done | runtime + stubs + `runtime.def` | `test_rt_canvas3d` | `docs/graphics3d-guide.md` | focused ctest passed |  |
| R-GET-014 | Optional material texture-presence getters | `runtime-changes.md` §3 | todo |  |  |  |  | For debug/presets |
| R-GET-015 | `Canvas3D.get_LightCount` | `runtime-changes.md` §3 | done | runtime + stubs + `runtime.def` | `test_rt_canvas3d` | docs updated | focused ctest passed | Counts active enabled canvas-slot lights |
| R-GET-016 | `Canvas3D.get_FrameFinalized` | `runtime-changes.md` §3 | done | runtime + `runtime.def` | `test_rt_canvas3d_gpu_paths` | header comment | focused ctest passed | Same as R-FRAME-010 |
| R-GET-017 | Canvas capability strings from frame section | `runtime-changes.md` §3 | done | runtime + `runtime.def` | `test_rt_canvas3d_production` | header comment | focused ctest passed | Same as R-FRAME-011/013 |
| R-GET-018 | `AnimController3D.get_StateTime -> f64` | `runtime-changes.md` §3 | todo |  |  |  |  | Do not duplicate `get_CurrentState` |
| R-GET-019 | Optional `Audio3D` fallback-listener getters | `runtime-changes.md` §3 | todo |  |  |  |  | Only if debug needs them |
| R-GET-020 | Avoid wrappers for already exposed runtime state without concrete need | `runtime-changes.md` §3 | todo |  |  |  |  | Review gate |

## 4. Metal shader finite guards

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-METAL-001 | Audit Metal backend for raw `normalize`, unchecked divide, non-finite color paths | `runtime-changes.md` §4 | done | `vgfx3d_backend_metal.m` audited; source guard added | `test_vgfx3d_backend_metal_shared` |  | focused ctest passed | `rg " normalize\\("` finds no Metal shader raw normalize calls |
| R-METAL-002 | Route skybox direction through safe helper | `runtime-changes.md` §4 | done | `skybox_safe_normalize3` handles camera-forward, inverse-projection, and inverse-view directions | `test_vgfx3d_backend_metal_shared` |  | focused ctest passed | Zero-length skybox vectors fall back to stable directions |
| R-METAL-003 | Route camera/reflection/normal/tangent/light normalization through safe helper where needed | `runtime-changes.md` §4 | done | material `safe_normalize3` tightened to reject huge/non-finite lengths; existing camera/reflection/normal/tangent/light paths already use it | `test_vgfx3d_backend_metal_shared` |  | focused ctest passed | Visual probe still tracked separately |
| R-METAL-004 | Add final color finite clamp only if repro proves need | `runtime-changes.md` §4 | done | no clamp added | source audit |  | no repro found | Keep deferred until a concrete color-path repro exists |
| R-METAL-005 | Avoid skybox depth-state changes unless repro proves need | `runtime-changes.md` §4 | done | no depth-state change made | source audit |  | no repro found | Skybox fix limited to finite-safe direction generation |
| R-METAL-006 | Metal visual probe for degenerate normals/tangents and zero-length skybox vector | `runtime-changes.md` §4 Tests | todo |  |  |  |  |  |
| R-METAL-007 | Existing software/D3D/OpenGL tests stay green | `runtime-changes.md` §4 Tests | done |  | `ctest --test-dir build -L graphics3d` |  | 35/35 graphics3d passed | Includes shared D3D/OpenGL tests and software-backend Zia probes |

## 5. Naming and documentation cleanups

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-DOC-001 | Mark `Canvas3D.SetOcclusionCulling` as deprecated compatibility alias | `runtime-changes.md` §5 | todo |  |  |  |  | Prefer `SetFrustumCulling` |
| R-DOC-002 | Fix docs that imply `End` runs post-FX or presents | `runtime-changes.md` §5 | done | `docs/graphics3d-guide.md`, `docs/viperlib/graphics/rendering3d.md` |  | docs updated | local diff |  |
| R-DOC-003 | Update 3D guide quick-start for grouped overlay and final screenshot | `runtime-changes.md` §5 | partial | `docs/graphics3d-guide.md` frame lifecycle updated |  | docs updated | local diff | Dedicated snippet ctest still pending |
| R-DOC-004 | Document `Canvas3D.Poll()` event return vs input state APIs | `runtime-changes.md` §5 | done | `docs/graphics3d-guide.md`, `docs/viperlib/graphics/rendering3d.md` |  | docs updated | local diff |  |
| R-DOC-005 | Document first-frame timing and Game3D dt seed/clamp | `runtime-changes.md` §5 | done | Canvas3D timing and Game3D frame/timing helpers documented | `g3d_test_game3d_runframes_probe` | docs updated | focused ctest passed |  |

## 6. Backend capabilities and safe quality selection

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-QUAL-001 | Preserve existing `BackendSupports("postfx")` / `BackendSupports("gpu_postfx")` behavior | `runtime-changes.md` §6 | done | existing capability behavior retained | `test_rt_canvas3d_production` | docs retained | ctest passed |  |
| R-QUAL-002 | Add new capability strings from frame section | `runtime-changes.md` §6 | done | `postfx-overlay`, `final-screenshot`, `gpu-postfx-overlay` | `test_rt_canvas3d_production` | docs updated | ctest passed |  |
| R-QUAL-003 | Define CPU/software-safe effects list | `runtime-changes.md` §6 | done | Performance/Balanced/Cinematic fallback use CPU-safe effects only | `test_rt_canvas3d_gpu_paths`, `g3d_test_canvas3d_quality_profiles` | docs updated | focused ctests passed | Bloom, Tonemap, FXAA, ColorGrade, Vignette |
| R-QUAL-004 | Define GPU-window-only effects list | `runtime-changes.md` §6 | done | Cinematic adds GPU-only effects only with GPU-window postfx | `test_rt_canvas3d_gpu_paths` | docs updated | ctest passed | SSAO, DOF, MotionBlur |
| R-QUAL-005 | `PostFX.Cinematic` enables GPU-only effects only when supported | `runtime-changes.md` §6 | done | `Canvas3D.SetQuality(2)` / `PostFX3D.NewQuality(canvas,2)` are capability-gated runtime APIs | `test_rt_canvas3d_gpu_paths`, `g3d_test_canvas3d_quality_profiles` | docs updated | focused ctests passed | Game3D `PostFX.Cinematic` wrapper remains Phase 3 |
| R-QUAL-006 | Quality fallback is recorded for `Debug3D` | `runtime-changes.md` §6 | partial | `QualityFallback` and `QualityFallbackReason` exposed on Canvas3D | `test_rt_canvas3d_gpu_paths`, `g3d_test_canvas3d_quality_profiles` | docs updated | focused ctests passed | Debug3D overlay still pending |
| R-QUAL-007 | Software backend test: all quality presets apply without traps | `runtime-changes.md` §6 Tests | done | Software Zia probe finalizes/screenshots all profiles | `g3d_test_canvas3d_quality_profiles` |  | ctest passed |  |
| R-QUAL-008 | GPU-capable backend test: GPU-only effects enabled only when supported | `runtime-changes.md` §6 Tests | done | fake GPU-postfx backend includes SSAO/DOF/motion blur; software path excludes them | `test_rt_canvas3d_gpu_paths` |  | ctest passed |  |
| R-QUAL-009 | Debug overlay reports active profile and fallback | `runtime-changes.md` §6 Tests | partial | fallback data is available to overlay code | `g3d_test_canvas3d_quality_profiles` | docs updated | ctest passed | Actual overlay pending Debug3D |

## 7. Synthetic input and synthetic clock

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-SYN-001 | Add `Canvas3D.SetInputSource(mode: i64)` | `runtime-changes.md` §7 | done | `rt_canvas3d_set_input_source`, runtime.def binding | `test_rt_canvas3d_gpu_paths`, `g3d_test_canvas3d_synthetic_input` | docs updated | focused ctests passed | live/synthetic/live+synthetic |
| R-SYN-002 | Add `Canvas3D.PushSyntheticKey(key, down)` | `runtime-changes.md` §7 | done | `rt_canvas3d_push_synthetic_key`, runtime.def binding | `test_rt_canvas3d_gpu_paths`, `g3d_test_canvas3d_synthetic_input` | docs updated | focused ctests passed |  |
| R-SYN-003 | Add `Canvas3D.PushSyntheticMouse(dx, dy, buttons, wheel)` | `runtime-changes.md` §7 | done | `rt_canvas3d_push_synthetic_mouse`, runtime.def binding | `test_rt_canvas3d_gpu_paths`, `g3d_test_canvas3d_synthetic_input` | docs updated | focused ctests passed |  |
| R-SYN-004 | Add `Canvas3D.ClearSyntheticInput()` | `runtime-changes.md` §7 | done | `rt_canvas3d_clear_synthetic_input`, runtime.def binding | `test_rt_canvas3d_gpu_paths`, `g3d_test_canvas3d_synthetic_input` | docs updated | focused ctests passed | Releases synthetic-held state |
| R-SYN-005 | Add `Canvas3D.SetClockSource(mode: i64)` | `runtime-changes.md` §7 | done | `rt_canvas3d_set_clock_source`, runtime.def binding | `test_rt_canvas3d_gpu_paths`, `g3d_test_canvas3d_synthetic_input` | docs updated | focused ctests passed | live/synthetic fixed |
| R-SYN-006 | Add `Canvas3D.SetSyntheticDeltaTimeSec(dt)` | `runtime-changes.md` §7 | done | `rt_canvas3d_set_synthetic_delta_time_sec`, runtime.def binding | `test_rt_canvas3d_gpu_paths`, `g3d_test_canvas3d_synthetic_input` | docs updated | focused ctests passed | dt sanitized/clamped |
| R-SYN-007 | Add `Canvas3D.AdvanceSyntheticFrame()` | `runtime-changes.md` §7 | done | `rt_canvas3d_advance_synthetic_frame`, runtime.def binding | `test_rt_canvas3d_gpu_paths`, `g3d_test_canvas3d_synthetic_input` | docs updated | focused ctests passed | Used by future `World3D.runFrames` |
| R-SYN-008 | Route synthetic events through normal Keyboard/Mouse state-update path | `runtime-changes.md` §7 | done | Synthetic application calls normal keyboard/mouse update functions before action update | `test_rt_canvas3d_gpu_paths`, `g3d_test_canvas3d_synthetic_input` | docs updated | focused ctests passed |  |
| R-SYN-009 | Probe: scripted WASD + mouse-look deterministic across two runs | `runtime-changes.md` §7 Tests | partial | Canvas3D probe compares two identical camera updates | `g3d_test_canvas3d_synthetic_input` |  | ctest passed | Full Game3D/World3D probe pending |
| R-SYN-010 | Final-frame comparisons use tolerance/region checks | `runtime-changes.md` §7 Tests | done | `walk_min` probe uses crisp overlay region and sampled baseline tolerance | `g3d_walk_min_visual_probe` |  | graphics3d ctest passed |  |

## 8. Asset/package resolver for 3D model loading

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-ASSET-001 | Add chosen runtime-level model asset contract | `runtime-changes.md` §8 | done | `Model3D.LoadAsset` and `GLTF.LoadAsset` added to C runtime/runtime.def | `test_rt_model3d`, `g3d_test_model3d_load_asset`, surface link | docs updated | focused ctests passed | Decision D-006: runtime `LoadAsset`, no Zia library |
| R-ASSET-002 | Support `assets/foo.glb` and `asset://foo.glb` through mounted assets first | `runtime-changes.md` §8 | done | `Viper.IO.Assets` strips `asset://`; raw loader uses embedded/mounted/filesystem order | `test_rt_asset_manager`, `test_rt_gltf` | `docs/viperlib/io/assets.md` | focused ctests passed |  |
| R-ASSET-003 | Resolve relative glTF dependencies relative to parent model asset | `runtime-changes.md` §8 | done | external buffers/images resolve against parent model path before asset lookup | `test_rt_gltf`, `test_rt_model3d` | `docs/graphics3d-guide.md` | focused ctests passed |  |
| R-ASSET-004 | Allow filesystem fallback in development builds | `runtime-changes.md` §8 | done | `LoadAsset` tries asset manager first, then direct filesystem where appropriate | `test_rt_gltf`, `g3d_test_model3d_load_asset` | docs updated | focused ctests passed | Absolute dev paths supported for GLB/glTF root load |
| R-ASSET-005 | Add package-aware audio loading if needed | `runtime-changes.md` §8 | done | `Sound.LoadAsset` loads asset bytes and reuses memory-backed sound decoder | `test_rt_audio_integration`, audio surface/unavailable tests | `docs/viperlib/audio.md` | focused ctests passed |  |
| R-ASSET-006 | Test GLB via filesystem and mounted package path | `runtime-changes.md` §8 Tests | done | generated GLB fixture exercises filesystem and mounted VPA | `test_rt_gltf` |  | focused ctest passed |  |
| R-ASSET-007 | Test glTF external `.bin` and texture dependencies via asset resolver | `runtime-changes.md` §8 Tests | done | generated mounted VPA contains `.gltf`, `.bin`, and PNG texture | `test_rt_gltf`, `test_rt_model3d` |  | focused ctests passed |  |
| R-ASSET-008 | Test `Assets3D.LoadModelTemplate` cache without retained asset leaks | `runtime-changes.md` §8 Tests | todo |  |  |  |  |  |

## 9. Camera aspect and resize

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-RESIZE-001 | Public frame path updates camera/render aspect on window resize or documents manual helper | `runtime-changes.md` §9 | partial | `World3D.onResize` and `tick()` sync camera render aspect | `g3d_test_game3d_world_probe` calls resize path | Game3D docs | focused ctest passed | Visual/no-distortion projection probe still pending |
| R-RESIZE-002 | Add `World3D.onResize` or automatic aspect sync in `World3D.tick()` | `runtime-changes.md` §9 | done | `rt_game3d_world_on_resize`; `rt_game3d_world_tick` aspect sync | `g3d_test_game3d_world_probe` | Game3D docs | focused ctest passed | Disabled-graphics stub added |
| R-RESIZE-003 | Resize probe verifies projection does not distort after window resize | `runtime-changes.md` §9 | partial | resize path exists | `g3d_test_game3d_world_probe` |  | focused ctest passed | Needs visual/projection assertion beyond no-trap coverage |

## 10. Game3D ownership, lifetime, and diagnostics

| ID | Item | Source | Status | Impl | Tests | Docs | Proof / link | Notes |
|---|---|---|---|---|---|---|---|---|
| R-LIFE-001 | Define `World3D` ownership of managed entities and registries | `runtime-changes.md` §10 | done | world registry retains spawned entities and releases on despawn/destroy | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| R-LIFE-002 | Define `World3D` ownership of collision/animation event buffers | `runtime-changes.md` §10 | partial | collision buffers are owned by `Physics3DWorld` and exposed/cleared through `World3D` | `test_rt_game3d` | Game3D docs | focused ctest passed | Animation event buffers remain Phase 5 |
| R-LIFE-003 | Define `World3D` ownership of effects, controllers, model templates, audio/effect objects | `runtime-changes.md` §10 | partial | world owns input/audio/effects/camera controller refs | `test_rt_game3d` | Game3D docs | focused ctest passed | Model templates and rich effect objects remain later |
| R-LIFE-004 | Add `World3D.destroy()` / `World3D.isDestroyed()` | `runtime-changes.md` §10 | done | runtime + `runtime.def` | `test_rt_game3d`, `g3d_test_game3d_world_probe` | Game3D docs | focused ctests passed |  |
| R-LIFE-005 | Add entity state queries | `runtime-changes.md` §10 | done | `Entity3D.isSpawned`, `Entity3D.isDestroyed` | `test_rt_game3d`, `g3d_test_game3d_world_probe` | Game3D docs | focused ctests passed |  |
| R-LIFE-006 | Define single-mesh entity despawn semantics | `runtime-changes.md` §10 | done | despawn removes scene node, body, and registry entry | `test_rt_game3d`, `g3d_test_game3d_world_probe` | Game3D docs | focused ctests passed |  |
| R-LIFE-007 | Define imported model-root group despawn semantics | `runtime-changes.md` §10 | partial | `Entity3D.FromNode` exists and group/child despawn rules are documented |  | Game3D docs | local docs update | Imported model tree probe remains Phase 4 |
| R-LIFE-008 | Define child Game3D entity despawn semantics | `runtime-changes.md` §10 | done | recursive Game3D child spawn/despawn | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| R-LIFE-009 | Define raw imported `SceneNode3D` child semantics | `runtime-changes.md` §10 | partial | raw nodes remain under imported root by design |  | Game3D docs | local docs update | Needs model-root probe |
| R-LIFE-010 | Define attached body/audio/effect/animation cleanup semantics | `runtime-changes.md` §10 | partial | attached bodies removed from physics on despawn; world audio/effects released on destroy | `test_rt_game3d` | Game3D docs | focused ctest passed | Per-entity audio/effect/anim cleanup remains later |
| R-LIFE-011 | Document raw escape-hatch lifetime | `runtime-changes.md` §10 | done |  |  | `docs/viperlib/graphics/game3d.md` | local docs update | canvas, scene, node, body, anim controller, templates |
| R-LIFE-012 | Diagnostic for destroyed worlds/entities | `runtime-changes.md` §10 | partial | trap guards exist for destroyed worlds/entities | `test_rt_game3d` covers state; negative probe pending | Game3D docs | focused ctest passed | Needs explicit destroyed-handle negative Zia test |
| R-LIFE-013 | Diagnostic for double despawn | `runtime-changes.md` §10 | partial | despawn is idempotent for already-unspawned entities | `test_rt_game3d` covers normal despawn | Game3D docs | focused ctest passed | Decide whether silent idempotence or trap is final |
| R-LIFE-014 | Diagnostic for attaching one entity to two worlds | `runtime-changes.md` §10 | done | spawn traps if entity already belongs to another world | `test_rt_game3d` covers ownership path indirectly | Game3D docs | focused ctest passed | Add explicit negative probe if this becomes public troubleshooting material |
| R-LIFE-015 | Diagnostic for attaching body before spawn if unsupported | `runtime-changes.md` §10 | done | attaching body before spawn is supported; spawn binds/adds it | `test_rt_game3d` | Game3D docs | focused ctest passed | No diagnostic needed under current policy |
| R-LIFE-016 | Diagnostic for invalid layer/mask values | `runtime-changes.md` §10 | done | single-bit layer validation traps | `test_rt_game3d` | Game3D docs | focused ctest passed |  |
| R-LIFE-017 | Diagnostic for missing package assets | `runtime-changes.md` §10 | todo |  |  |  |  |  |
| R-LIFE-018 | Diagnostic for backend capability fallbacks | `runtime-changes.md` §10 | todo |  |  |  |  |  |
| R-LIFE-019 | C unit: destroy/despawn releases or marks resources once | `runtime-changes.md` §10 Tests | done |  | `test_rt_game3d` |  | focused ctest passed |  |
| R-LIFE-020 | Zia probe: destroyed world/entity traps with documented diagnostic | `runtime-changes.md` §10 Tests | todo |  |  |  |  | Not covered by current world probe |
| R-LIFE-021 | Zia probe: imported model group despawn removes owned root/children only | `runtime-changes.md` §10 Tests | todo |  |  |  |  |  |
| R-LIFE-022 | Leak/retention probe: cache clear and repeated spawn/despawn do not grow managed counts | `runtime-changes.md` §10 Tests | todo |  |  |  |  | Where available |

## Validation for every runtime change

| ID | Item | Source | Status | Proof / link | Notes |
|---|---|---|---|---|---|
| R-VAL-001 | `./scripts/check_runtime_completeness.sh` passes | `runtime-changes.md` Validation | done | `scripts/check_runtime_completeness.sh` |  |
| R-VAL-002 | Builds pass on macOS, Windows, Linux | `runtime-changes.md` Validation | partial | `cmake --build build -j...` passed locally | Windows/Linux pending |
| R-VAL-003 | `ctest --test-dir build -L graphics3d` passes | `runtime-changes.md` Validation | done | 44/44 graphics3d passed | Includes Phase 1 and Phase 2 Game3D C/Zia probes |
| R-VAL-004 | Focused C tests exist for getters/default lighting/finalization/screenshots/synthetic input/assets/lifetime | `runtime-changes.md` Validation | partial | Finalization/screenshot/capability/synthetic-input/assets/Game3D lifetime C tests added | Future API areas pending |
| R-VAL-005 | Zia probes exist for final screenshots/overlay/default lighting/quality/package load/determinism | `runtime-changes.md` Validation | partial | `g3d_walk_min_visual_probe`; `g3d_test_canvas3d_synthetic_input`; `g3d_test_canvas3d_quality_profiles`; `g3d_test_model3d_load_asset`; Game3D probes | Rich gameplay-state replay pending |
| R-VAL-006 | Every new public runtime function has success-path ctest or waiver | `runtime-changes.md` Validation | partial | Phase 0B/0C and Phase 1 core Game3D functions covered by focused tests | Future API areas pending; snippet coverage pending |
| R-VAL-007 | Every applicable public runtime function has negative/capability ctest or waiver | `runtime-changes.md` Validation | partial | Capability false path, invalid masks, missing assets, and Game3D callback rejection covered | Destroyed-handle negative Zia probes still pending |
