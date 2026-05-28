# 3D Next Level - a game-focused, developer-friendly 3D runtime API for Viper/Zia

> Status: **DRAFT plan**, revised after auditing the current
> `Viper.Graphics3D` runtime, Zia syntax, and the 3D bowling demo. Phase 0 now
> explicitly covers final-frame rendering, deterministic test clocks, backend
> capability gates, and package-aware assets before the ergonomic runtime API.

## 1. Why this exists

Building substantial 3D in Viper today is painful even though a lot of runtime
capability already exists:

- Every drawable commonly needs a hand-built `Mat4`; the frame loop is manual
  (`Clear` / `Begin` / `DrawMesh` / `End` / `Flip`); collision handling is manual
  indexed event polling.
- `Canvas3D.New` sets a small ambient value but installs no default light rig and
  no attractive scene preset. A simple scene is visible, but not good-looking by
  default.
- The frame contract is confusing: `Canvas3D.End` flushes scene work while
  post-FX and present happen in `Canvas3D.Flip`; 2D overlay calls can auto-open
  temporary overlay frames, making HUD rendering easy to misuse and hard to test.
- There is no high-level entity concept, no managed camera/input/controller
  workflow, no prefab/asset convenience, no animation/audio/VFX ergonomics, no
  quality profiles, and no small polished reference sample.
- Packaged-game asset loading is not a first-class 3D workflow yet: model,
  texture, buffer, and audio dependencies need to resolve the same way from the
  source tree and mounted assets.

Result: the 3D bowling demo is **6,251 lines** across 19 files (`game.zia` alone
is 1,015), and a "walk around" scene can technically run while still requiring
too much setup and looking worse than it should.

## 2. The reframe: capability exists; contracts and composition are missing

A direct audit of `src/il/runtime/runtime.def` and the C runtime
(`src/runtime/graphics/3d/`, Metal/D3D11/OpenGL/software backends) shows the
rendering capability largely exists. The "next level" is **not** a renderer
rewrite. It is:

1. A small set of runtime contract fixes that remove sharp edges.
2. A first-class `Viper.Game3D` runtime API that composes existing primitives
   with good defaults and is exposed to Zia/BASIC the same way other Viper
   runtime APIs are exposed.
3. Better samples and visual tests that measure final-frame quality, not just
   "not magenta".
4. A packaging and asset-resolution workflow suitable for real games.

| Area | Existing capability | Gap this plan addresses |
|---|---|---|
| Immediate rendering | `Canvas3D` (3D draw, fog, shadows, skybox, post-FX, instancing, terrain, water, vegetation, 2D overlay) | Final-frame contract, post-FX-safe overlay, final screenshots, safer defaults |
| Retained scene graph | `Scene3D` / `SceneNode3D`, hierarchy, culling, `Find`, `Save`/`Load`, `SyncBindings` | Entity wrapper, node/entity registry, group/model entities, sync ordering |
| Materials | PBR-ish `Material3D`, alpha modes, unlit, maps, env maps | Presets and inspection helpers |
| Lights | Directional, point, ambient, spot | Default rigs, getters, enable/disable, clearer shadow control |
| Model import | `Model3D.Load`/`Instantiate`/`InstantiateScene`, glTF/GLB/FBX/OBJ | Asset templates, model-root entities, packaged glTF/GLB dependency resolution |
| Animation | `Skeleton3D`, `AnimController3D` (states, crossfade, events, root motion) | `Animator3D` wrapper on entities, deterministic update/root-motion ordering |
| Audio | positional audio, listener/source binding, attenuation, doppler | `Audio3D` on `world`, listener-follows-camera, package-aware loading, `playAt`/attached sources |
| VFX | `Particles3D`, `Decal3D` | `Effects3D` presets plus world-owned update/draw/expiry lifecycle |
| Environment | Skybox, `Terrain3D`, `Water3D`, fog | `Environment` scenery presets (sky+ground+fog, optional terrain/water) |
| Camera/input | `Camera3D` verbs + `Viper.Input.Keyboard`/`Mouse` | Two-phase controllers, named input intent, synthetic input/clock for tests |
| Physics/navigation | `Physics3DWorld`, `Physics3DBody`, `CollisionEvent3D`, `Character3D`, queries, navmesh/agents | Event buffers, optional callback sugar, masks, body/entity ownership, walkable sample |
| Transforms | `f64` / `Vec3` / `Quat` | No common-case `Mat4` authoring |

## 3. Goals

1. A lit, walkable, visually respectable scene in about 15 lines.
2. No hand-written `Mat4` for common placement, prefab, and model-spawn cases.
3. Sensible defaults: camera, lighting, quality profile, post-FX, shadows,
   culling, input, audio listener, and final-frame overlay behavior.
4. A safe managed loop with variable, fixed, and deterministic test-runner modes.
5. Entity wrappers that work for single meshes and imported model subtrees.
6. Ergonomic animation (`entity.anim?.play(...)`), 3D audio (`world.audio`),
   VFX presets (`Effects3D`), and environment presets (`Environment.*`) over the
   existing runtime.
7. Collision event buffers with event phase, contact point/normal,
   impulse/speed, trigger status, and optional simple callbacks for common
   cases.
8. Layer/mask helpers so game code does not need bitwise arithmetic.
9. Package-aware loading for models, model dependencies, textures, and audio.
10. Escape hatches to every underlying `Viper.Graphics3D.*` primitive.
11. Deterministic simulation tests and final-frame visual regression with
    backend-aware tolerances.
12. Ctest coverage for every new runtime contract, Game3D API area, docs snippet,
    and sample entry point, with explicit waivers for anything that cannot be
    automated yet.
13. A final playable showcase sample that demonstrates the whole Game3D stack in
    one place, not only isolated probes.
14. A code-first starter workflow: a small project/template, package layout,
    asset conventions, and troubleshooting path that let a new user create,
    run, package, and test a 3D game without copying engine glue from bowling.

## 4. Non-goals

- Rewriting the renderer or replacing the existing backends.
- Inventing new asset formats.
- Building an editor in this initiative.
- Solving every backend rendering artifact up front. Phase 0 creates minimal
  repros and fixes small high-confidence blockers needed for "good by default".
- Guaranteeing byte-identical screenshots across different machines. Exactness
  applies to fixed-step simulation state; images use tolerances and structural
  assertions.

## 5. Design principles

- **Cross-platform parity.** Every Game3D feature works on macOS, Windows, and
  Linux. No macOS-only code paths in the runtime API.
- **Determinism where it matters.** `runFixed`, `runFrames`, synthetic input, and
  a synthetic clock make simulation reproducible. Visual tests compare final
  frames with tolerances, not unrealistic cross-machine byte equality.
- **Software backend is the canonical correctness baseline.** Visual regression
  runs on the CPU rasterizer; GPU backends get secondary smoke checks and
  capability-gated feature assertions.
- **Backend-safe presets.** Quality and post-FX presets must query backend
  capabilities and degrade without traps.
- **Zero new dependencies.** Every feature maps to existing `Viper.Graphics3D.*`
  / `rt_*` capability or a small, named runtime contract addition.
- **Heavy work stays in C.** `Scene3D.Draw`, physics, post-FX, model import,
  audio, entity ownership, collision dispatch, package-aware asset loading, and
  managed Game3D loops are implemented in the normal Viper C runtime. Zia and
  BASIC consume the public API rather than owning the core implementation.
- **Zia remains a consumer surface.** Samples, docs snippets, and optional thin
  convenience wrappers can be written in Zia, but the authoritative Game3D
  surface is runtime-backed and registered through `runtime.def`.
- **Runtime ownership is explicit.** `World3D` owns managed entities, effects,
  collision/event buffers, and package-loaded assets. Despawn/destroy behavior,
  raw escape-hatch lifetimes, and invalid-handle diagnostics must be documented
  and covered before broad samples depend on them.
- **Code-first workflow is a product requirement.** A working starter project,
  package manifest/layout, copy-paste docs, and actionable diagnostics are part
  of the deliverable, not follow-up polish.

## 6. Architecture

```text
Game code (.zia/.bas)
      |
      v
Public runtime API: Viper.Game3D
   World3D, Entity3D, Input3D, CameraControllers, CharacterController3D,
   Animator3D, Audio3D, EffectRegistry3D, Effects3D, Environment,
   Lighting/Material/PostFX/Quality presets, Prefab, Assets3D, Debug3D,
   Layers/LayerMask/Collision3DEvent
      |
      v
Existing lower-level runtime: Viper.Graphics3D / Viper.Audio / Viper.Input
   Canvas3D, Scene3D, SceneNode3D, Material3D, Light3D, Model3D,
   Physics3D, Character3D, AnimController3D, Audio3D, Particles3D,
   Decal3D, Terrain3D, Water3D, NavMesh3D, PostFX3D, ...
   + small contract additions:
     final-frame finalization, post-FX-safe overlay queue, ScreenshotFinal,
     default lighting, missing getters, synthetic input/clock, backend caps,
     package-aware model/audio asset resolution, remaining Metal finite guards
```

## 7. Package index

| File | Contents |
|---|---|
| `README.md` | Overview, design principles, architecture, open decisions |
| `api-spec.md` | Runtime-backed `Viper.Game3D` API, frame order, collisions, assets, examples |
| `runtime-changes.md` | Runtime contracts and validation points |
| `roadmap.md` | Phases, required ctest inventory, docs deliverables, showcase sample, acceptance criteria, risks |
| `zia-feasibility.md` | Evidence matrix for Zia samples and optional callback/convenience syntax |
| `progress/` | Checklists for every phase, runtime contract, API item, test, doc, sample, decision, and acceptance criterion |

## 8. Open decisions before implementation

Most prior open risks are resolved with evidence (see `zia-feasibility.md`):
lambdas, non-void function-typed params, interfaces, optional/interface `?.`,
enums and enum-to-Integer coercion, and fluent chaining are all proven by
checked-in tests.

Genuinely open items, deliberately deferred to Phase 0:

1. **Runtime namespace.** Recommended default is `Viper.Game3D.*`, parallel to
   existing `Viper.Game.*` helpers while clearly building on
   `Viper.Graphics3D.*`. Phase 0A should confirm this before adding
   `runtime.def` entries.
2. **Collision/event ergonomics.** The authoritative runtime API should expose
   pollable event objects/buffers. Zia callback sugar is optional and only ships
   if the callback bridge fits existing runtime conventions.
3. **Void function-type spelling.** Needed only for Zia samples or optional
   callback wrappers; proven fallback is a one-method interface.
4. **Final overlay API names.** `BeginOverlay` / `EndOverlay` are placeholders
   for the post-FX-safe overlay pass.
5. **Finalization API shape.** Decide whether `FinalizeFrame` is public,
   internal-only, or exposed through `ScreenshotFinal`/`Flip` only.
6. **Asset resolver shape.** Choose `Model3D.LoadAsset` vs. `LoadResolved` after
   a small loader spike.
7. **Implicit C fallback lighting.** Start explicit-only. Add automatic fallback
   only if a Phase 0 repro proves it is needed and it can be disabled without
   surprising deliberately dark scenes.
