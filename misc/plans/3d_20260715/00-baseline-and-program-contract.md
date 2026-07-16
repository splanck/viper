# Plan 00 — Baseline and Program Contract

## Objective

Freeze the evidence and architectural rules that every other plan relies on.
This plan does not change product code. Its output is a reproducible baseline,
an ownership model, and a set of constraints that prevents later plans from
solving the same problem in incompatible ways.

## Evidence reviewed

The 2026-07-15 review covered:

- Every `.zia` source, probe, project file, and relevant README under
  `examples/games/3dbowling`, `examples/games/ridgebound`, and
  `examples/games/ashfall`.
- The public definitions in `src/il/runtime/defs/game3d/*.def` and
  `src/il/runtime/defs/graphics3d/*.def`.
- Game3D runtime implementation, internals, VM callback bridge, graphics
  backends, scene, physics, render, terrain/water/vegetation, audio, asset,
  persistence, effect, controller, combat, and test ownership.
- `docs/internals/graphics3d-architecture.md`,
  `docs/viperlib/graphics/game3d.md`, and related Game, IO, Graphics3D, and UI
  documentation.
- Existing ADRs through ADR 0102 and historical 3D plans from repository
  history. Historical plans were used only to understand intent; current source
  is authoritative because many previously proposed features have landed.

At review time the live public inventory contained 61 Game3D classes and 64
Graphics3D classes. The Game3D definitions exposed 254 properties and 461
methods; Graphics3D exposed 391 properties and 638 methods. The 3D runtime tree
contained approximately 208,005 lines of C/header/include implementation and
the modular 3D definition files contained approximately 5,210 lines. Recompute
these numbers before using them as release metrics; they are descriptive, not
acceptance thresholds.

## Verified baseline behavior

The following checks passed during the review:

- `viper check` for all three game projects.
- Ashfall's 14 game probes.
- Ridgebound's four primary release gates on software and Metal.
- All 27 3dbowling release gates; 25 ran on software and two high-resolution
  visual gates used Metal because the software renderer was prohibitively slow.
- A focused set of 22 Graphics3D/Game3D runtime unit and contract tests.

Do not assume this green state still exists on a future branch. Before starting
any implementation plan, capture a fresh baseline using the commands in the
plan and [appendices/shared-validation-matrix.md](appendices/shared-validation-matrix.md).
If an unrelated pre-existing failure exists, record its exact command and
output; do not hide it by weakening a gate.

## Confirmed defects

### Final-overlay alpha

`examples/games/3dbowling/known_viper_issues/overlay_alpha_repro.zia`
draws a 50% black rectangle over a known `192,128,64` background. Both Metal
and software produced `0,0,0`; the expected blended result is approximately
`96,64,32` subject to normal rounding. This is a confirmed correctness defect,
not a proposed enhancement.

### Metal same-size AA-text alias

`examples/games/3dbowling/known_viper_issues/overlay_aa_text_repro.zia`
queues same-size `DrawText2DAA` textures with distinct colors. On Metal the
first texture can display the later texture's content; software renders the
two correctly. This is consistent with resource identity or upload lifetime
aliasing but the root cause must be demonstrated, not guessed.

### Historical material/overlay interference

`material_overlay_repro.zia` did not reproduce in the reviewed run. Keep it as
a sentinel. Do not claim it fixed and do not expand plans 01–02 to speculative
material work without a new deterministic failure.

## Current game patterns and pain points

### 3dbowling

The main game is deliberately low-level. `engine/renderer.zia` owns Canvas3D,
SceneGraph, camera, post-FX, lighting, and quality. `engine/game_setup.zia`
creates raw PhysicsWorld3D state. `engine/game_flow.zia` owns the outer loop,
fixed-step calculation, gameplay ordering, and collision processing. Its
separate `game3d/game3d_setup.zia` demonstrates that World3D can reduce setup
substantially—789 setup lines to 129 in that bounded comparison—but is not a
full migration of the release game.

Pain points relevant to more than bowling:

- manual frame/physics scheduling;
- manual ownership and release of mixed render/physics resources;
- custom asset candidate resolution;
- custom quality propagation;
- custom UI/scene orchestration;
- collision scans and game-side identity mapping;
- many one-off probe loops.

### Ridgebound

Ridgebound is a hybrid World3D application. `game.zia` uses World3D for core
scene/physics/effects ownership, but manually steps and renders because terrain,
water, vegetation, weather, custom HUD, and menus need explicit phases.
`playerctl.zia` and `camctl.zia` implement genre-specific movement/camera
behavior. Quality is propagated separately into Canvas3D, post-FX, terrain,
water, weather, simulation, and occlusion.

Pain points shared with Ashfall:

- environment objects are not all registered World3D renderables;
- level resources are released by subsystem-specific choreography;
- quality decisions are copied across subsystems;
- movement and camera composition outgrow the built-in all-in-one controllers;
- application states and HUD/menu handling remain game-owned.

### Ashfall

Ashfall already uses World3D as a host but explicitly documents why it keeps a
manual loop: fixed simulation plus custom camera, viewmodel, world-pass, HUD,
and presentation control. `player/playerctl.zia` is a custom Character3D motor;
`player/camera_rig.zia` is a custom camera; `core/events.zia` is an
allocation-free event bus; `weapons/targets.zia` maps physics bodies to targets
and regions; `weapons/ballistics.zia` implements ray, penetration, blast, and
dedup behavior; `world/level_base.zia` and `level_manager.zia` manually own and
unload nodes, bodies, effects, and subsystem state.

Ashfall is the proof that low-level capability is not the main gap. The missing
layer is reusable application composition that does not constrain custom render
and gameplay policy.

## Existing runtime features that must be reused

Before adding a type, the implementation owner must re-evaluate these current
surfaces:

| Need | Existing surface |
|---|---|
| World host and frame phases | `World3D.Update`, `StepSimulation`, `BeginFrame`, `DrawScene`, `DrawEffects`, `EndScene`, `DrawOverlay`, `Present` |
| Concise loops | `World3D.Run`, `RunFixed`, `Run*WithOverlay`, `RunFrames*` |
| Fixed-loop telemetry | `DroppedFixedSteps`, `FixedInterpolationAlpha`, `RenderInterpolation` |
| Movement | low-level `Character3D`, `CharacterController3D`, first/third-person controllers, traversal probes |
| Camera | free-fly, orbit, follow, first-person, third-person, target lock, rail camera, timeline camera |
| Entities and lookup | `Entity3D`, World3D spawn/despawn, body/name indexes, entity-aware collision events |
| Combat | `Hitbox3D`, `Health3D`, hit/damage events, hit stop, ragdoll |
| Effects/audio | `EffectRegistry3D`, `Effects3D` presets, `Sound3D`, ambient/reverb/footstep surfaces |
| Environment | `Environment3D`, `EnvHandle`, terrain/water/vegetation/sky/time-of-day low-level objects, streamed terrain |
| Assets | `Viper.IO.Assets`, `Viper.Assets.Resolver`, `Assets3D`, `AssetHandle3D`, `SceneTemplate`, model cache |
| Persistence | `Viper.IO.SaveData`, `Entity3D.SetPersistent`, `World3D.SaveState/LoadState`, streamed-world persistence |
| UI | `Viper.Game.UI.Hud*` widgets, Canvas3D GameUI adapter, general `Viper.GUI` |
| Common game utilities | StateMachine, Timer, Tween, SmoothValue, ObjectPool, ScreenFX |
| Tests | synthetic Canvas3D input, `RunFramesOnly`, final-frame capture, Game3D fixtures, software/GPU probes |

If one of these can meet the need with a compatible extension, extend it. New
parallel subsystems require an ADR justification explaining why composition was
not viable.

## Architecture contract

### Namespace ownership

- `Viper.Graphics3D` remains the low-level rendering, scene, physics,
  navigation, animation, asset-decoding, and backend layer.
- `Viper.Game3D` owns ergonomic world/entity/application composition.
- Generic filesystem and packaged-asset policy belongs to `Viper.IO` or
  `Viper.Assets`; Game3D may provide typed convenience without implementing a
  second resolver.
- Generic UI widgets stay in `Viper.Game.UI` or `Viper.GUI`; Game3D supplies the
  canvas/scene integration only.
- Example-library classes under `examples/games/lib` are valid incubation
  points. They are not stable runtime ABI until promoted through an ADR.

### Frame order

All plans preserve this semantic order unless an ADR explicitly supersedes it:

1. poll window/input and measure unscaled frame delta;
2. clamp/accumulate frame time;
3. for each fixed step, run game intent/update before World3D simulation;
4. update built-in controller/behavior/animation, physics, scene bindings,
   gameplay services, effects, and late camera in the documented World3D order;
5. interpolate render state when enabled;
6. begin the 3D frame;
7. draw registered pre-scene environment content if its contract requires it;
8. draw the scene graph and streamed terrain;
9. draw registered post-scene environment/custom content and effects;
10. end the 3D scene and apply post-processing;
11. render final overlay/UI;
12. capture if requested, then present;
13. release frame-lifetime event/query views.

The exact placement of terrain, sky, water, vegetation, transparencies, and
effects must be verified against renderer pass semantics in plan 05. The list
above defines orchestration intent, not permission to reorder blend/depth
requirements casually.

### Ownership and lifetime

- World3D owns its built-in canvas, camera, scene, physics, input, audio,
  effects, stream, and registered services.
- An Entity3D spawned into a world follows current spawn/despawn/stale-handle
  rules. A scope must call the public lifecycle operations rather than freeing
  internals.
- Scene scopes own only resources explicitly registered to them. Registration
  retains the resource; release is idempotent and occurs in reverse dependency
  order.
- Frame events and query-list views are valid only for the documented frame or
  until the next mutation. Persistent storage must copy stable IDs/data, not
  retain transient event objects.
- Callback execution stays on the VM thread. No worker may invoke script.

### Errors and diagnostics

- Programmer contract violations use the established trap conventions.
- Optional asset absence, unsupported capability fallback, corrupt save files,
  and pool exhaustion are recoverable and observable; they must not silently
  become traps.
- Hot-loop fallbacks expose bounded counters through existing diagnostics or a
  plan-approved telemetry surface.
- Error strings name the public operation and the violated condition.

### Determinism and performance

- Simulation-affecting systems use fixed delta and deterministic iteration
  order.
- Render interpolation and camera smoothing must not mutate authoritative
  simulation state.
- New per-frame paths must have a no-allocation steady state after warm-up, or
  the plan must document and benchmark the exception.
- Lists and registries have overflow behavior, repair paths, and diagnostics.
- Background asset work commits on the established commit queue and never
  touches GPU objects from an arbitrary worker.

## Source and registration contract

For a new public runtime class, the implementation checklist includes:

1. ADR approval.
2. Append a permanent class ID in the existing 3D ID registry; never reuse or
   renumber.
3. Add the C struct and lifecycle rules to the correct internal owner.
4. Add public declarations with complete Viper source headers.
5. Add real runtime implementation.
6. Add disabled-graphics behavior with matching symbols and documented
   fallback/trap semantics.
7. Register functions/classes in the appropriate modular `.def` file.
8. Add VM bridge work only when signatures require it; verify native and VM
   call paths.
9. Add sources to every relevant runtime CMake source list/variant.
10. Add unit, contract, fixture, surface-audit, documentation, and example
    coverage.
11. Regenerate or update generated documentation only through the repository's
    supported workflow.
12. Run the complete validation matrix for the change type.

## Per-plan start checklist

Before editing:

- Read root `AGENTS.md` and `CLAUDE.md`.
- Read this plan, the selected plan, dependencies, and appendices.
- Run `git status --short`; identify user-owned changes and overlapping files.
- Re-run the selected plan's fail-before probe.
- Inspect the live runtime API with `build/src/tools/viper/viper --dump-runtime-api`.
- Search current definitions and ADRs for naming/behavior conflicts.
- Record baseline test commands, backend, platform, dimensions, and hashes or
  sampled values where relevant.
- Decide whether an ADR is required. When uncertain, treat public runtime/C ABI
  or cross-layer changes as ADR-required.

## Per-plan handoff checklist

The implementing owner must provide:

- ADR link or explicit explanation why no ADR was required.
- Exact files changed and public surface delta.
- Fail-before and pass-after evidence.
- Tests added and commands run.
- Backend/platform matrix, including explicit untested cells and owners.
- Allocation/performance evidence where the plan has a hot path.
- Compatibility and migration notes.
- Documentation/example updates.
- Remaining risks or follow-up issues; no silent deferrals.

## Stop and escalation rules

Stop the selected plan and request architectural direction when:

- the fix requires changing normative IL or verifier behavior not covered by
  the plan;
- a proposed public signature cannot be represented consistently in Zia,
  BASIC, VM, and native paths;
- the implementation would require a new dependency;
- a plan would break an existing public API rather than extend it compatibly;
- a backend cannot implement the semantic contract without a documented
  capability fallback;
- ownership cannot be made idempotent under current object lifetime rules;
- a demo migration changes gameplay outcomes outside accepted tolerances;
- unrelated user changes overlap the required source and cannot be preserved.

