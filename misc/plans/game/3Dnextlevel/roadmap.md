# Roadmap, milestones, and testing

Phases are ordered so runtime contracts and a visual baseline land before the
ergonomic API. A pleasant wrapper over ambiguous frame behavior would preserve
the current failure mode. The software backend is the canonical correctness
baseline; GPU backends get secondary smoke checks and capability-specific tests.

## Phase 0A - Runtime surface confirmation spike

Goal: pin down the public runtime namespace and the small language-surface
details needed by samples before adding broad `runtime.def` coverage.

- Confirm the public namespace, with `Viper.Game3D.*` as the recommended
  default unless the runtime audit finds a stronger local convention.
- Confirm the authoritative event model: runtime-owned collision/animation
  event buffers first, with optional Zia callback sugar only if it fits existing
  callback conventions.
- Confirm the **void function-type spelling** for callbacks; Phase 0A resolved
  this to `(Float) -> Unit` for no-return callback helpers.
- Compile a tiny probe exercising: alias bind, a function-typed callback
  parameter, a block lambda, a class implementing `CameraController`, optional
  interface chaining, and input read after `Canvas3D.Poll()`.

Exit:

- `game3d_surface_probe.zia` compiles and runs under ctest.
- `zia-feasibility.md` Confirm-0A rows are resolved.
- API docs are updated if the callback spelling uses interfaces or if callbacks
  are demoted to optional convenience wrappers over runtime event buffers.

## Phase 0B - Runtime final-frame contract and visual baseline

Goal: one mesh, one post-FX chain, one final overlay, and one screenshot all
refer to the same final frame.

- Implement `runtime-changes.md` §1:
  - final overlay command queue,
  - idempotent frame finalization,
  - `ScreenshotFinal`,
  - frame-finalized state,
  - backend capability strings.
- Implement §2 default lighting and `ClearLights`.
- Implement the minimum §3 getters needed by Game3D probes.
- Implement §4 remaining Metal finite guards.
- Build `examples/3d/walk_min.zia`: ground, three primitive props, explicit
  default lighting, free-fly or first-person camera, CPU-safe post-FX, and one
  crisp final overlay element.
- Capture a final-frame screenshot on the software backend and store it as the
  canonical visual-regression baseline.

Exit:

- Final-frame screenshot includes post-FX and crisp overlay.
- Overlay pixels are not bloomed/tonemapped.
- Single mesh shows clear directional shading.
- `ScreenshotFinal()` before `Flip()` and `Flip()` after `ScreenshotFinal()` do
  not double-apply post-FX.
- Builds pass on macOS, Windows, Linux.
- `check_runtime_completeness.sh` passes.
- `ctest --test-dir build -L graphics3d` passes.

## Phase 0C - Deterministic test input, clock, and backend-safe quality

Goal: tests can drive cameras/gameplay without live input or wall-clock time, and
quality presets cannot trap on unsupported backends.

- Implement `runtime-changes.md` §6 backend-safe quality policy.
- Implement §7 synthetic input and synthetic clock.
- Add `World3D.runFrames` probe scaffolding, even before full Game3D exists, if
  needed as a temporary Zia test utility. This is not a separate runtime; any
  new behavior is implemented in the normal C runtime APIs described in
  `runtime-changes.md`.
- Add software-backend probes for every quality profile.

Exit:

- Scripted WASD + mouse-look sequence produces identical camera pose and
  simulation state across two runs.
- Final-frame screenshot comparison uses tolerance/region checks, not
  byte-identical cross-machine assertions.
- Software backend can apply `Performance`, `Balanced`, and `Cinematic` profiles
  without traps.
- Debug/capability output reports any quality fallback.

## Phase 0D - Package-aware model/audio asset loading

Goal: the asset path used in a source-tree run also works after packaging.

- Implement the chosen `Model3D.LoadAsset` or `Model3D.LoadResolved` contract from
  `runtime-changes.md` §8.
- Support glTF external `.bin` and texture dependencies through the mounted asset
  resolver.
- Add package-aware audio loading if sound loading is path-only.
- Add small committed fixtures: one GLB, one glTF with external dependencies,
  one short audio clip.

Exit:

- GLB loads through filesystem path and mounted package path.
- glTF external dependencies resolve correctly through the asset system.
- Missing dependency diagnostics name the model and dependency path.
- Model template caching does not leak retained assets.

## Phase 1 - Core Game3D: `World3D`, `Entity3D`, `Input3D`, masks

- Implement runtime-backed `Viper.Game3D` classes and functions in the existing
  C runtime tree, with `runtime.def` entries, headers, disabled-graphics stubs,
  docs, and completeness checks like the rest of the runtime.
- Add only thin Zia sample/import convenience where useful; do not make a Zia
  source package the authoritative implementation.
- `World3D.New` creates canvas/camera/scene/physics/input/audio/effects and
  applies the explicit default environment.
- Implement `run`, `runFixed`, `runFrames`, manual loop methods, final-frame
  capture, and the documented frame order.
- Implement `LayerMask`, entity layer/mask plumbing, body ownership registry,
  spawn/despawn cleanup, and resize/aspect handling.
- Implement `World3D.destroy`, entity state queries, invalid-handle
  diagnostics, and raw escape-hatch lifetime docs before broad samples depend on
  managed entities.
- `Input3D` reads named keyboard/mouse state and never decodes `Canvas3D.Poll()`
  as a bitmask.

Exit:

- The API-spec hello-world compiles and renders.
- `world_probe.zia` validates construction, spawn/despawn, child cleanup,
  findNode/findEntity, resize/aspect sync, final-frame render, one manual
  `tick()`, one `stepSimulation()`, one `runFrames()` sequence, destroy
  semantics, and invalid-handle diagnostics.
- No common-case sample uses `Mat4.`.

## Phase 2 - Cameras and walkable movement

- Implement `FirstPersonController`, `FreeFlyController`, `OrbitController`,
  `FollowController`, and `CharacterController3D`.
- Use the two-phase controller contract:
  - `update` for input/movement before physics,
  - `lateUpdate` for final camera placement after physics/sync.
- Wire mouse capture/release for first-person controllers.

Exit:

- Synthetic-input probes deterministically change camera pose.
- First-person character movement is not one frame late relative to physics.
- Follow camera tracks the post-physics entity pose.
- `walk_min` supports walking with collision/grounding.
- No hand-authored camera `LookAt` loop is needed in game code.

## Phase 3 - Presets, prefabs, quality, environment, debug

- Implement `Lighting.*`, `Materials.*`, `PostFX.*`, `Quality.Apply`,
  `Prefab.*`, `Environment.*`, and `Debug3D`.
- Quality profiles control shadows, post-FX, culling, and overlay diagnostics
  consistently and capability-safely.
- `Debug3D` shows backend, capabilities, active/fallback quality profile, FPS,
  draw/cull counts, axes, camera position, and optional physics.

Exit:

- Swapping lighting/material/post-FX/environment presets visibly changes the
  sample.
- Each prefab returns a spawnable entity.
- `Environment.*` yields an instantly respectable scene.
- Debug text uses the final overlay path.
- `presets_probe.zia` validates light/material/post-FX parameter ranges and
  backend fallback behavior.

## Phase 4 - Assets, physics, and collision events

- Implement `Assets3D.LoadModel`, `Assets3D.LoadModelAsset`,
  `Assets3D.LoadModelTemplate`, `Assets3D.LoadModelTemplateAsset`, `BodyDef`,
  `Entity3D.attachBody`, `Collision3DEvent`, runtime collision event buffer
  accessors, and optional `World3D.onCollision` / `World3D.onCollisionSimple`
  convenience hooks if Phase 0A approves the callback bridge.
- Model loads return group entities whose root is the instantiated
  `SceneNode3D` subtree and whose `entity.anim` is populated when skinned.
- Collision dispatch uses a clear body-owner registry and runtime
  enter/stay/exit buffers.
- Layer masks are applied to `Physics3DBody.CollisionLayer` /
  `CollisionMask`.

Exit:

- A loaded glTF/GLB renders with hierarchy/materials intact from filesystem and
  mounted asset paths.
- A dropped physics ball rests on a ground body.
- Runtime collision events expose the requested phase and include point/normal,
  relative speed, impulse, trigger status, and owning entities.
- Optional simple collision callbacks, if shipped, fire once on enter by
  default.
- `assets_probe.zia`, `physics_probe.zia`, and `collision_probe.zia` pass.

## Phase 5 - Animation

- Implement runtime-backed `Animator3D` over `AnimController3D`/`Skeleton3D`,
  exposed as `entity.anim`.
- `World3D` advances animation in the documented pre-physics phase;
  `SyncMode.NodeFromAnimRootMotion` drives node transforms from root motion.
- Add `stateTime()` backed by the runtime getter added in Phase 0B.

Exit:

- A skinned glTF plays an animation and crossfades between two states.
- Runtime animation event accessors report named events at the right times;
  optional `onAnimEvent` sugar may dispatch them if Phase 0A approved callbacks.
- Root motion moves the entity's node deterministically.
- `anim_probe.zia` passes under `runFrames`.

## Phase 6 - 3D audio and VFX

- Implement runtime-backed `Audio3D` on `world` with listener-follows-camera,
  `load`/`loadAsset`/`playAt`/`playAttached`/`play2D`.
- Implement runtime-backed `EffectRegistry3D` and `Effects3D` presets:
  explosion/sparks/dust/smoke/impact decal.
- Ensure effects update, draw, and expire through `World3D`.

Exit:

- A positional sound attenuates correctly with listener distance.
- Attached sound follows an entity after scene/body sync.
- Each `Effects3D` preset spawns and auto-expires without leaking
  emitters/decals.
- Collision events can trigger both positional sound and an effect, with
  callback sugar available only if Phase 0A approved it.
- `audio_probe.zia` and `effects_probe.zia` pass.

## Phase 7 - Showcase sample game, docs, and bowling migration

- Build `examples/3d/game3d_showcase/`: one cohesive mini-game that exercises
  every new Game3D feature in normal gameplay, not isolated demos:
  - `World3D.runFixed` for gameplay and `runFrames` for deterministic replay
    tests,
  - final-frame post-FX plus crisp HUD/debug overlay,
  - lighting/material/post-FX/quality/environment presets with runtime toggles,
  - terrain/ground, water or fog, prefabs, and imported packaged GLB/glTF props,
  - first-person and follow/orbit camera modes,
  - `CharacterController3D` or player-controlled physics body,
  - `LayerMask`, `BodyDef`, `Collision3DEvent`, triggers, and collision event
    handling,
  - animated skinned model with animation events,
  - positional audio, attached audio source, and non-spatial UI/music audio,
  - `Effects3D` particles/decals spawned from collisions,
  - package-aware model/audio loading,
  - raw escape-hatch usage in one clearly marked advanced section.
- Port/re-skin the 3D bowling setup to Game3D to demonstrate line-count
  reduction and improved clarity.
- Add `examples/3d/game3d_starter/`, or the nearest existing project-template
  mechanism, as the recommended code-first starting point: source layout, asset
  layout, package manifest/config, run command, package command, and a first
  deterministic test.
- Add docs under `docs/viperlib/graphics/`: getting started, frame loop &
  overlay contract, deterministic tests, presets, environment, assets/packages,
  animation, audio/VFX, physics/collision events, troubleshooting.
- Add API reference updates for every new runtime function and every Game3D API
  area. Runtime docs must explicitly distinguish `End`, `FinalizeFrame`,
  `ScreenshotFinal`, and `Flip`.
- Every docs page must include at least one copy-pasteable snippet that is
  compiled by ctest or imported by a ctest probe.

Exit:

- Showcase sample runs at interactive framerate on at least one GPU backend and
  has a documented software-backend baseline.
- Showcase final-frame visual regression passes on the software backend.
- Showcase deterministic replay test reaches the same gameplay state under
  `runFrames` and synthetic input/clock.
- Bowling migration meets the setup-code reduction target.
- Starter project can be copied or generated, run, package assets, and execute
  its deterministic ctest without borrowing private files from the showcase.
- Docs contain copy-pasteable examples that compile.
- All new runtime functions, Game3D API areas, sample entry points, and docs
  snippets have ctest coverage or an explicit tracked waiver.

## Testing strategy

| Area | How |
|---|---|
| Runtime contracts | C unit tests + Zia final-frame probes |
| Zia sample syntax | Phase 0A compile/run probe |
| Game3D runtime API | C unit tests plus `*_probe.zia` consumers registered as ctest entries |
| Visual quality | final-frame screenshot vs. software-backend baseline with tolerance and region asserts |
| Determinism | `runFrames` + synthetic input/clock -> exact simulation state across runs |
| Screenshots | final-frame image tolerance; no cross-machine byte-identical requirement |
| Cross-platform | build + `-L graphics3d` green on macOS/Windows/Linux; GPU backends get secondary smoke checks |
| Overlay correctness | post-FX scene with crisp overlay pixel assertions |
| Input/cameras | synthetic keyboard/mouse, deterministic pose assertions |
| Assets/packages | filesystem and mounted package loads for GLB, glTF dependencies, and audio |
| Docs snippets | extract or import every fenced Zia snippet that claims to compile |
| Showcase game | smoke, deterministic replay, final-frame visual baseline, package-load smoke |
| Regression | `ctest --test-dir build -L graphics3d` stays green |

Register tests following `src/tests/CMakeLists.txt` patterns, with
`requires_display` labels where a real window is needed. The software-backend
baseline should not require a real display unless the current backend cannot
capture offscreen final frames yet.

### Required ctest inventory

Every phase that adds a public runtime function, Game3D API area, sample, or docs
page must add or update ctests in the same change. The final inventory must
include:

| Area | Required tests |
|---|---|
| Frame/final overlay | C unit tests plus Zia final-frame screenshot and overlay-crispness probes |
| Default lighting/getters | C unit tests plus visual/default-lit mesh probe |
| Synthetic input/clock | deterministic camera/gameplay state probe |
| Backend-safe quality | software no-trap probe and GPU capability smoke where available |
| Asset resolver | filesystem and mounted-package GLB/glTF/audio probes |
| Core world/entity/input | `world_probe.zia`, spawn/despawn/destroy cleanup, invalid-handle diagnostics, resize/aspect, escape hatches |
| Cameras/character | deterministic pose, grounded movement, follow camera late-update |
| Presets/environment/debug | parameter-range probe, visual smoke, final-overlay debug text |
| Physics/collisions | layer/mask filtering, enter/stay/exit, trigger, rich event data, optional simple callback |
| Animation | play/crossfade/event/root-motion under `runFrames` |
| Audio/VFX | listener/source binding, attenuation, attached audio, effect expiry |
| Docs | compile/import snippets for every new docs page |
| Starter project | copy/generate smoke, package-load smoke, deterministic test |
| Showcase game | smoke run, deterministic replay, final screenshot baseline, package-load path |

Coverage gaps are allowed only with a named waiver in the roadmap or issue
tracker explaining why the behavior cannot be automated yet.

## Acceptance criteria (measurable)

1. **Hello-world <= 20 source lines** excluding blanks/comments renders a lit,
   walkable scene.
2. **Zero `Mat4.` calls** in hello-world, open-world, and common prefab/model
   placement code.
3. **Bowling migration:** non-gameplay scaffolding - per-game renderer/camera/
   HUD/engine glue, approximately 789 lines today (`engine/renderer.zia` 291 +
   `engine/hud.zia` 279 + `engine/camera.zia` 219) - reduced **>= 50%** when
   re-skinned onto Game3D; record before/after LOC.
4. **Simple-scene frame budget:** hello-world reaches >= 30 FPS (<= 33 ms/frame)
   at 1280x720 on the software backend on named reference hardware; record
   CPU, build mode, and backend. Flagship performance is recorded separately
   and is not blocked on this CPU budget.
5. **Determinism:** fixed-step run with scripted synthetic input/clock yields
   exact matching simulation state across two runs and VM/native where the VM
   uses the same build and backend.
6. **Visual regression:** software-backend final frame matches baseline within
   a defined tolerance plus structural region assertions.
7. **Overlay crispness:** overlay pixels are not bloomed/tonemapped.
8. **Package assets:** the same sample model loads from filesystem and mounted
   package path, including external glTF dependencies.
9. **Showcase game:** `examples/3d/game3d_showcase/` demonstrates final overlay,
   quality/environment presets, packaged assets, cameras, character/physics,
   collision events, animation, audio, VFX, debug overlay, deterministic replay,
   and escape hatches in one playable sample.
10. **Starter workflow:** a user can start from `game3d_starter` or the project
   template, run the game, package assets, and run its deterministic ctest using
   documented commands.
11. **Docs:** every new runtime function and Game3D API area has docs, and each
   compiling docs snippet is covered by ctest.
12. **Coverage & escape hatches:** each subsystem (cameras, presets, assets,
   physics, animation, audio, VFX, environment) ships a `*_probe.zia` ctest;
   escape hatches are verified for `world.canvas`, `world.scene`, `entity.node`,
   `entity.mesh`, `entity.material`, `entity.body`, `entity.anim?.controller`,
   and raw `Viper.Graphics3D.*` calls.

## Risks and mitigations

- **Runtime namespace mismatch.** Mitigation: Phase 0A spike before adding broad
  `runtime.def` entries.
- **Void function-type spelling.** Mitigation: one-method-interface fallback.
- **Frame API churn.** Mitigation: finalization contract sits behind Game3D
  methods; raw runtime docs are updated at the same time.
- **Overlay implementation differs by backend.** Mitigation: capability strings,
  backend-specific tests, and safe quality fallback.
- **Implicit lighting surprises.** Mitigation: explicit-only first; automatic
  fallback off unless proven necessary.
- **GPU vs. software divergence.** Mitigation: software backend is the
  correctness baseline; GPU backends are smoke-checked and capability-gated.
- **Visual tests too brittle.** Mitigation: tolerances + structural region
  checks, not only exact full-image diffs.
- **Asset resolver complexity.** Mitigation: GLB first, then glTF external
  dependencies; diagnostics must name the missing dependency path.
- **Animation/audio test assets.** Mitigation: small committed fixtures or
  procedurally built skeletons/clips where practical.
- **Callback bridge complexity.** Mitigation: runtime-owned event buffers are
  authoritative; callback sugar is optional.
