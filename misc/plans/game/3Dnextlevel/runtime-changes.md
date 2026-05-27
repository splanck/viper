# Runtime additions

Game3D is a runtime-first API built in the normal Viper C runtime on top of the
existing `Viper.Graphics3D`, `Viper.Input`, and audio primitives. These changes
are not a renderer rewrite. They define the behavior that the public
`Viper.Game3D` API can safely rely on.

There is no new sidecar or separate "helper runtime". Every change in this file
is a normal Viper runtime API implemented in the existing C runtime tree,
registered through `runtime.def`, declared in the relevant runtime header, and
backed by disabled-graphics stubs where the surrounding subsystem already uses
that pattern. If a thin Zia helper is added later, it is only convenience code
over this runtime surface, not the implementation boundary.

Adding a runtime function follows the established pattern:

1. `RT_FUNC(Tag, c_handler, "Viper.Game3D.Type.Method", "sig(args)")` or the
   relevant lower-level `Viper.Graphics3D.*` path in
   `src/il/runtime/runtime.def`.
2. `RT_METHOD("Method", "sig()", Tag)` or `RT_PROP` inside the class block.
3. C declaration in the relevant header.
4. Real implementation and a `#ifdef VIPER_ENABLE_GRAPHICS` disabled-graphics
   stub.
5. `./scripts/check_runtime_completeness.sh` must pass.

## 1. Frame, post-FX, overlay, and final screenshot contract

**Current facts.**

- `Canvas3D.End` flushes queued scene work, but post-FX and present happen in
  `Canvas3D.Flip`.
- `DrawRect2D` / `DrawText2D` auto-open and auto-close temporary overlay frames
  when called outside an explicit frame.
- Existing examples can draw HUD/menu output after `End` and before `Flip`; on
  software/CPU post-FX this output can be processed as part of the scene image.
- Existing `Screenshot()` reads current pixels. A screenshot before `Flip` does
  not prove that post-FX, final overlay, and present-ready pixels are correct.

**Required contract.** Split finalization from presentation. The final frame has
one authoritative state:

```text
scene Begin/Draw/End
optional final overlay queue
FinalizeFrame:
    apply scene post-FX
    composite final overlay after post-FX
    mark frame finalized
ScreenshotFinal:
    FinalizeFrame if needed
    read finalized pixels
Flip:
    FinalizeFrame if needed
    present finalized pixels
    update timing/bookkeeping for the next frame
```

**Change.**

- Correct `rt_canvas3d_end` comments/docs so `End` means "flush queued scene
  frame" and `Flip` means "finalize if needed, present, then update timing".
- Add explicit post-FX-safe overlay calls. Working names:
  - `Canvas3D.BeginOverlay()`
  - `Canvas3D.EndOverlay()`
  - `Canvas3D.ClearOverlay()` if a queued overlay must be discarded before
    finalization.
- `BeginOverlay` / `EndOverlay` must record into a **final overlay command
  queue**. They must not be a thin alias for the current `Begin2D` path, because
  the current path can render before CPU post-FX.
- Preserve current `DrawRect2D` / `DrawText2D` auto-frame behavior for
  compatibility, but document it as legacy/convenience behavior. Game3D always
  groups HUD work through the explicit final overlay path.
- Add `Canvas3D.FinalizeFrame()` as an idempotent internal/public helper. It
  applies post-FX and composites the final overlay exactly once.
- Add `Canvas3D.ScreenshotFinal()`:
  - calls `FinalizeFrame()` if needed,
  - reads the finalized surface,
  - does **not** present,
  - allows a later `Flip()` to present the same finalized frame without
    re-running post-FX or overlay composition.
- Add `Canvas3D.get_FrameFinalized`.
- Add backend capability strings:
  - `BackendSupports("postfx-overlay")`
  - `BackendSupports("final-screenshot")`
  - `BackendSupports("gpu-postfx-overlay")` if a GPU backend has a distinct path.

**Backend notes.**

- Software/CPU path: render scene, run `rt_postfx3d_apply_to_canvas`, replay the
  final overlay queue into the framebuffer, then present/readback.
- GPU window path: `present_postfx` needs an after-postfx overlay step before
  the drawable is presented. If a backend cannot composite the overlay after
  GPU post-FX yet, it must report `postfx-overlay = false` and Game3D must fall
  back to a CPU-safe or no-postFX profile for crisp HUD tests.
- Render targets: finalization must work for render-target-backed frames too, or
  the capability string must make that limitation explicit.

**Tests.**

- C unit: `FinalizeFrame()` is idempotent; calling `ScreenshotFinal()` then
  `Flip()` does not double-apply post-FX or overlay.
- Zia probe: bright/blooming scene + crisp final overlay; assert overlay pixels
  are not bloomed/tonemapped.
- Zia probe: `ScreenshotFinal()` includes both scene post-FX and overlay.
- Regression: legacy `DrawRect2D`/`DrawText2D` still work, but new Game3D
  samples use `BeginOverlay` / `EndOverlay`.
- Update 3D bowling smoke probes to capture final frames, not pre-`Flip` frames.

## 2. Explicit default lighting

**Current facts.** A fresh `Canvas3D` currently sets ambient to `0.1, 0.1, 0.1`
but installs no direct lights. Default materials are lit, so a scene is usually
visible but not attractive or well-shaped.

**Change.**

- Add `Canvas3D.SetDefaultLighting()` as an explicit helper that installs a
  conservative key/fill/ambient setup:
  - one soft directional key,
  - optional weak fill,
  - ambient in a reasonable range,
  - sane shadow bias if shadows are enabled.
- Add `Canvas3D.ClearLights()` so presets can fully reset slots without ad hoc
  loops in every caller.
- Do **not** enable implicit fallback lighting by default in the first pass.
  If later added, it must be gated by a flag and suppressed whenever the caller
  intentionally clears lights.
- If implicit fallback is later added, the resolver must count both canvas-slot
  lights and `Scene3D` node lights before deciding that the scene is unlit.

**Tests.**

- Unit: `SetDefaultLighting` populates expected light params and ambient.
- Unit: `ClearLights` removes every canvas-slot light.
- Visual: a single mesh with default lighting has clear directional shading.
- Regression: explicit dark scene remains dark after `ClearLights` and low
  ambient.

## 3. Missing inspection and state controls

The public Game3D runtime API needs to inspect state for presets, debug
overlays, and serialization without duplicating parallel state in Zia or BASIC.

Add real + stub + `runtime.def` entries for:

| Type | New members |
|---|---|
| `Light3D` | `get_Type`, `get_Color -> Vec3`, `get_Intensity -> f64`, `get_Enabled` / `SetEnabled`, `get_Direction`, `get_Position` |
| `Light3D` | optional `get_CastsShadows` / `SetCastsShadows`, implemented by skipping disabled shadow selection |
| `SceneNode3D` | `get_WorldPosition -> Vec3`, `get_WorldScale -> Vec3` |
| `Material3D` | `get_Color -> Vec3`, `get_AlphaMode`, `get_ShadingModel`, `get_Unlit`, optional texture-presence getters for debug/presets |
| `Canvas3D` | `get_LightCount`, `get_FrameFinalized`, capability strings from §1 |
| `AnimController3D` | `get_StateTime -> f64`; `get_CurrentState` already exists and should not be duplicated |
| `Audio3D` | optional effective fallback-listener getters only if debug needs static listener readback; object listeners already expose pose getters |

Notes:

- `SceneNode3D` already exposes `WorldMatrix`; world-position/scale getters are
  convenience and avoid repeated Zia-side decomposition.
- Enabling/disabling a light must affect `build_light_params`.
- If `CastsShadows` is added, backend light params or shadow selection need a
  clear way to skip non-shadow-casting directional lights.
- Do not add wrappers for already exposed runtime state unless a Game3D API area
  has a concrete need.

## 4. Metal shader finite guards

**Current facts.** Metal already has a `safe_normalize3` helper in the main
shader path, while some remaining shader code still uses raw `normalize`
directly, notably skybox direction reconstruction.

**Change.**

- Audit `src/runtime/graphics/3d/backend/vgfx3d_backend_metal.m` for remaining
  raw `normalize`, unchecked divide, and non-finite color paths.
- Route skybox direction, camera vectors, reflection vectors, and any remaining
  normal/tangent/light normalization through the existing safe helper or an
  equivalent helper in that shader source block.
- Add a final color finite clamp only if a current repro shows non-finite output
  can escape shader math.
- Do not change skybox depth state unless a current repro proves it is wrong.

**Tests.**

- Metal visual probe with degenerate normals/tangents and a zero-length skybox
  camera vector.
- Existing software/D3D/OpenGL tests stay green.

## 5. Naming and documentation cleanups

- `Canvas3D.SetOcclusionCulling` is currently a compatibility alias for
  frustum culling. Keep the alias, but mark it deprecated in docs and prefer
  `SetFrustumCulling` in all new samples.
- Fix docs that imply `End` runs post-FX or presents.
- Update 3D guide quick-start examples to show grouped overlay and final-frame
  screenshot usage.
- Document that `Canvas3D.Poll()` returns an event type, while input state comes
  from `Viper.Input.Keyboard` / `Viper.Input.Mouse`.
- Document first-frame timing: `DeltaTimeSec` can be zero before the first
  completed `Flip`; Game3D clamps/seeds dt for managed loops.

## 6. Backend capabilities and safe quality selection

Quality presets must never enable a runtime path that traps on the selected
backend.

- Keep existing `BackendSupports("postfx")` / `BackendSupports("gpu_postfx")`
  behavior and add the new strings from §1.
- Add a helper or documented policy for Game3D:
  - CPU/software-safe effects: Bloom, Tonemap, FXAA, ColorGrade, Vignette.
  - GPU-window-only effects: SSAO, DOF, MotionBlur.
- `PostFX.Cinematic` may enable GPU-only effects only when the backend reports
  support. Otherwise it degrades to a CPU-safe cinematic profile and records that
  degradation for `Debug3D`.

**Tests.**

- Software backend: all quality presets apply without traps.
- GPU-capable backend: GPU-only effects are enabled only when supported.
- Debug overlay reports active profile and any fallback.

## 7. Synthetic input and synthetic clock

**Problem.** `Input3D` reads live `Viper.Input.Keyboard` / `Viper.Input.Mouse`.
That cannot be driven headlessly or reproducibly. A fixed timestep plus
synthetic input is still not enough if frame dt comes from the wall clock.

**Change.** Add a test-oriented input and clock source the runtime consults
during `Poll()` / timing update, on the shared cross-platform path:

- `Canvas3D.SetInputSource(mode: i64)`:
  - `0 = live` (default),
  - `1 = synthetic`,
  - `2 = live + synthetic`.
- `Canvas3D.PushSyntheticKey(key: i64, down: bool)` queues a key transition.
- `Canvas3D.PushSyntheticMouse(dx: f64, dy: f64, buttons: i64, wheel: f64)`
  queues a mouse delta/button/wheel sample.
- `Canvas3D.ClearSyntheticInput()` clears queued synthetic events and state.
- `Canvas3D.SetClockSource(mode: i64)`:
  - `0 = live` (default),
  - `1 = synthetic fixed`.
- `Canvas3D.SetSyntheticDeltaTimeSec(dt: f64)` sets the dt reported after each
  synthetic frame.
- `Canvas3D.AdvanceSyntheticFrame()` advances one synthetic frame without
  relying on wall time; managed tests may call this through `World3D.runFrames`.

Synthetic events flow through the same state-update path that normally
populates `Keyboard`/`Mouse`, so `Input3D.isDown/pressed/mouseDelta/...` work
identically.

**Tests.**

- A probe selects synthetic input and clock, pushes a scripted WASD + mouse-look
  sequence, runs `World3D.runFrames`, and asserts deterministic camera pose and
  simulation state across two runs.
- Final-frame screenshot comparison uses tolerances/region checks, not
  byte-identical cross-machine expectations.

## 8. Asset/package resolver for 3D model loading

**Problem.** `Model3D.Load` is path-based and glTF support reads files from disk,
while packaged games use `Viper.IO.Assets` / VPA-style embedded assets. A good
game workflow needs model, buffer, texture, and audio assets to load the same
way in source-tree runs and packaged runs.

**Change.**

- Add one of these runtime-level contracts after a spike:
  - `Model3D.LoadAsset(assetPath: str)`, using `Viper.IO.Assets` to load the
    model and its external dependencies, or
  - `Model3D.LoadResolved(path: str, resolverMode: i64)`, where the loader asks
    the runtime asset resolver for external buffers/textures before falling back
    to filesystem paths.
- Define URI/path rules:
  - `assets/foo.glb` and `asset://foo.glb` resolve through the mounted asset
    system first.
  - Relative glTF dependencies resolve relative to the parent model asset.
  - Filesystem fallback is allowed in development builds.
- Add package-aware audio loading if `Sound` currently has the same path-only
  limitation.

**Tests.**

- Load a committed GLB through filesystem path and through mounted package path.
- Load a glTF with external `.bin` and texture dependencies through the asset
  resolver.
- `Assets3D.LoadModelTemplate` caches the loaded model without leaking retained
  meshes/materials/textures.

## 9. Camera aspect and resize

- Ensure the public frame path updates camera/render aspect on window resize, or
  document that the game must call the aspect sync helper manually.
- Add `World3D.onResize` or automatic camera aspect sync in `World3D.tick()`.
- Add a resize probe that verifies world-space aspect-sensitive projection does
  not distort after a window resize.

## 10. Game3D ownership, lifetime, and diagnostics

**Problem.** A runtime-backed Game3D API will hold many lower-level runtime
objects: canvas, scene, camera, physics world, bodies, imported model nodes,
audio sources, animation controllers, particles, decals, and package-loaded
assets. Without explicit ownership rules, Game3D could remove boilerplate while
introducing use-after-despawn bugs or unclear leaks.

**Change.**

- Define `World3D` as the owner of managed entities, body/entity registries,
  collision/animation event buffers, effect registries, camera controllers,
  package-loaded model templates, and world-created audio/effect objects.
- Add `World3D.destroy()` / `World3D.isDestroyed()` and entity state queries so
  generated bindings and docs can describe when handles are valid.
- Define `despawn` semantics for:
  - single-mesh entities,
  - imported model-root group entities,
  - child Game3D entities,
  - raw imported `SceneNode3D` children,
  - attached bodies/audio/effects/animation controllers.
- Document raw escape-hatch lifetime for `world.canvas`, `world.scene`,
  `entity.node`, `entity.body`, `entity.anim?.controller`, and model templates.
- Add clear diagnostics for destroyed worlds/entities, double despawn,
  attaching one entity to two worlds, attaching a body before spawn if unsupported,
  invalid layer/mask values, missing package assets, and backend capability
  fallbacks.

**Tests.**

- C unit: `destroy` and `despawn` release/mark owned resources once and tolerate
  documented repeated cleanup paths.
- Zia probe: using a destroyed world/entity traps with the documented diagnostic.
- Zia probe: imported model group despawn removes the root and owned children
  without invalidating unrelated raw scene nodes.
- Leak/retention probe where available: model template cache clear releases
  retained model assets and repeated spawn/despawn does not grow managed counts.

## Validation for every runtime change

- `./scripts/check_runtime_completeness.sh`
- Build on all three platforms (`build_viper.sh` / `build_viper.cmd`); no
  macOS-only paths.
- `ctest --test-dir build -L graphics3d`
- New focused C tests for getters, default lighting, frame finalization, final
  screenshot, synthetic input/clock, asset resolver behavior, and Game3D
  ownership/lifetime behavior.
- New Zia probes for final-frame screenshot, overlay order, default-lit mesh,
  backend-safe quality presets, package model load, and deterministic
  fixed-step simulation state.
- Every new public runtime function must have at least one C-level or Zia-level
  ctest that exercises the success path and one negative/capability path where
  applicable. Any function without direct automation needs a named waiver in
  `roadmap.md` or the implementation tracking issue.
