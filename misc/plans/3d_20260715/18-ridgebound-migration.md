# Plan 18 — Ridgebound Incremental Migration

## Outcome

Migrate Ridgebound's existing hybrid World3D application onto FrameDriver,
SceneScope, environment registration, shared asset/quality/query/cue policy,
and GameBase3D where these preserve its traversal, survival, weather, world
simulation, menu/HUD, and visual identity.

## Current architecture

- `game.zia`: World3D construction, lifecycle/state machine, manual fixed step,
  custom render order, menu/quality orchestration;
- `terrain.zia`: terrain generation, collision, vegetation draw/quality;
- `water_sky.zia`: water, sky, reeds, render and quality;
- `weather.zia`: rain/snow/storm cycle and quality scale;
- `worldsim.zia`: time of day, world systems, physics objects;
- `playerctl.zia`: custom character/controller movement/sprint/swim policy;
- `camctl.zia`: custom camera;
- `forest.zia`, `critters.zia`, `survival.zia`: content/simulation ownership;
- `postfx.zia`, `audio.zia`, `hud.zia`, `menu3d.zia`, `minimap.zia`:
  presentation/application systems.

World3D already hosts canvas/scene/physics/effects. The migration focuses on
composition, not host replacement.

## Dependencies

Required: plans 03–12, 14–15. Plans 06–07 are adopted only after movement/
camera spikes meet Ridgebound behavior. Plan 13 is not required.

## Non-negotiable behavior invariants

- terrain topology, height samples, collider alignment, vegetation/forest
  placement and fallback materials;
- traversal/jump/sprint/swim/platform/slope behavior and survival resource rates;
- world clock, weather transition, critter, beacon/objective and victory state;
- Performance/Balanced/Cinematic authored appearance and density/budget policy;
- water/sky/terrain/scene/effects/overlay ordering;
- menu/restart/quit-to-title deterministic reset;
- minimap/HUD/accessibility/input behavior;
- all current smoke, state, topology, traversal, material and performance gates.

## Migration strategy

Keep `game.zia` as the composition root initially. Introduce small adapters for
environment registration, profile distribution, and application scenes.
Replace one manual responsibility per slice. Do not rewrite terrain/world
simulation content merely to use a new runtime type.

## Implementation sequence

### Slice 0 — Freeze baseline

1. Run `zanna check`, `run_probes.sh`, smoke, state, topology, traversal, tree
   material/fallback, and documented performance/visual gates on software and
   Metal.
2. Capture deterministic traces for player pose/state, terrain height samples,
   world/weather clocks, survival values, quality settings, restart counts, and
   objective/victory state.
3. Capture title/gameplay clear/storm/night, water, forest, HUD/menu at all
   three tiers.
4. Record World3D entity/body/node/effect/audio plus raw terrain/water/
   vegetation counts through restart and quit-to-title cycles.

### Slice 1 — FrameDriver3D

1. Match current fixed step, accumulator limits, and pause handling.
2. Poll through FrameDriver once; run Ridgebound authoritative update before
   each commit.
3. Preserve current post-physics updates and camera timing.
4. Reproduce current render sequence with explicit phases before adopting the
   environment stack.
5. Compare normal, 30/60/144 Hz, long-frame, pause/resume, and deterministic
   state traces.

Gates: state, traversal, smoke and performance.

### Slice 2 — Scene/resource scopes

Define:

- root scope: World3D, global menu/settings/audio assets;
- gameplay scope: terrain, water, sky/time-of-day, vegetation/forest, critters,
  survival objects, physics bodies, effects/audio;
- transient weather/effect child scopes only if lifetime differs.

Track current resources without changing generation. Replace restart/quit
manual teardown with idempotent gameplay scope Release. Validate 100 restart
cycles and partial boot failure cleanup.

### Slice 3 — EnvironmentStack3D

1. Register Terrain3D at current offset.
2. Register vegetation and verify draw/cull count.
3. Register Water3D in the correct transparent phase.
4. Register sky/time-of-day state and preserve custom weather influence.
5. Remove manual `Canvas3D.DrawTerrain/DrawVegetation/DrawWater` only after
   captures and pass counters match.
6. Keep weather simulation in Ridgebound; it updates environment objects rather
   than moving into runtime.

Gates: topology, material/fallback, clear/storm/night captures, draw counters,
all backends.

### Slice 4 — Shared quality profile

1. Resolve one profile when menu quality changes.
2. Apply runtime-owned canvas/shadow/render/environment fields.
3. Have terrain, water/sky, weather, post-FX, worldsim, and effects read the same
   immutable profile fields.
4. Preserve authored post-FX and deliberate Ridgebound overrides.
5. Remove duplicate tier constants only after state probe asserts exact values.
6. Persist requested tier, never hardware-resolved active values.

Gates: low/cinematic state/smoke checks, all three captures and capability
fallback diagnostics.

### Slice 5 — Asset catalog

Replace source/package FBX/tree/material lookup and missing fallback policy with
the shared catalog. Preserve procedural trees/fallback material and package
behavior. Run tree asset/material/fallback probes from repo root, project dir,
and packaged executable.

### Slice 6 — Entity-aware queries/events

Use WorldHit/WorldEvent where Ridgebound currently maps ground/traversal/
interaction/collision identities. Preserve raw-body behavior for worldsim
physics objects. Do not move general weather/survival/application events into
the runtime stream.

Compare traversal ground entity, interaction/objective, and collision traces.

### Slice 7 — CharacterMotor3D adoption

Perform a gated spike:

1. Map playerctl move/yaw/jump/sprint/crouch intent into the motor.
2. Keep stamina, survival restrictions, swim detection/state, animation, and
   game-specific speed decisions outside.
3. Adopt generic swim mode only if it reproduces Ridgebound's behavior; else
   retain a thin Ridgebound swim adapter.
4. Compare acceleration, jump apex, slope/step, water entry/exit, stamina and
   traversal traces.

If any core movement feel changes, retain playerctl's custom motor and use only
the shared intent-drive prerequisite. The migration is still valid.

### Slice 8 — CameraRig3D adoption

Map current camctl base pose/collision/smoothing and any shake/FOV behavior.
Keep traversal-specific framing/weather presentation policy outside. Compare
fixed camera traces and captures around slopes, water, forest occlusion,
teleport/restart, and pause. Retain custom camera if the generic rig cannot
match without special cases.

### Slice 9 — Pooled cues

Migrate bounded common feedback:

- sprint dust/splash/weather bursts;
- scorch/impact decal;
- beacon/victory/spatial ambience cues.

Keep continuous weather density control and music/state orchestration in game
code. Compare allocation counts, pool drops, active counts, audio behavior, and
quality scaling.

### Slice 10 — GameBase3D application flow

Use GameBase3D for title/menu, gameplay, pause, result/victory, transition, and
resize. Keep world simulation's internal state machine in gameplay. Associate
the gameplay scope with the gameplay scene. Reuse current HUD/menu drawing or
existing Game.UI widgets; do not rewrite the 3D menu solely for migration.

Validate restart/quit-to-title resets, pause accumulator, quality persistence,
and UI navigation.

### Slice 11 — Save composition

Define a Ridgebound schema for requested settings and survival/progression
fields plus World3D persistent state if the game supports continuing. Preserve
current restart-new-world semantics. Add corruption/migration probes before
advertising save slots.

### Slice 12 — Harness and cleanup

Migrate repeated probe loops to scenario harness, compare side by side, remove
obsolete accumulator/teardown/asset/quality/manual environment code, and update
README/IMPROVEMENT_AUDIT/RUNTIME_API_BUGS. Close only runtime bugs actually
fixed and tested.

## Per-slice validation

```sh
build/src/tools/zanna/zanna check examples/games/ridgebound --diagnostic-format=json
examples/games/ridgebound/run_probes.sh
ctest --test-dir build -L graphics3d --output-on-failure
```

Also run named state/topology/traversal/material probes explicitly, representative
software/Metal captures at three tiers, and full platform/package gates before
completion.

## Regression budgets

- terrain topology/height and state/event integers exact;
- player/world floats within existing probe tolerances, not newly widened;
- visual structural samples pass at all tiers/backends;
- no extra environment draw or input/simulation update;
- lifecycle counts return exactly to baseline;
- quality settings exact and reapply idempotent;
- performance probe regression >10% on same machine/backend requires analysis;
- no warmed cue allocations.

## Acceptance criteria

- All Ridgebound probes and quality/backend captures pass.
- FrameDriver, scopes, environment stack, asset catalog, and quality profile
  replace corresponding manual boilerplate.
- Movement/camera are adopted only to the degree they match existing feel.
- Weather/world/survival policy remains game-owned.
- Restart/quit lifecycle is leak-free and deterministic.
- Source-tree and packaged assets/fallbacks work.
- Documentation reflects actual retained versus runtime-owned systems.

## Stop conditions

Stop a slice if it changes terrain topology, traversal feel, environment draw
order, authored quality look, or restart state. Keep the custom subsystem and
report the generic abstraction gap rather than adding Ridgebound-only runtime
branches.

## Handoff evidence

Provide full probe/backend/tier matrix, state/camera/environment traces,
reference captures, lifecycle/allocation counts, adopted/retained subsystem
table, package results, and runtime gaps.

