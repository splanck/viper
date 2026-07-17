# Plan 19 — Ashfall Incremental Migration

## Outcome

Migrate Ashfall's reusable infrastructure onto the new Game3D application
layers while preserving its FPS movement/game feel, viewmodel/custom render
pipeline, campaign/levels, AI, weapon/economy/difficulty policy, procedural
fallbacks, and all 14 probes. Ashfall remains the strongest extensibility test;
the runtime must adapt to its legitimate custom needs rather than absorb the
whole game.

## Current architecture

- `game.zia`: World3D boot/settings, fixed accumulator, tick order, render
  phases, capture/present, orchestration;
- `player/playerctl.zia`: custom Character3D motor;
- `player/camera_rig.zia`, `viewmodel.zia`: camera, bob/recoil/FOV/viewmodel;
- `core/events.zia`: allocation-free game event bus;
- `weapons/targets.zia`: body -> game target/region registry;
- `weapons/ballistics.zia`, `projectiles.zia`, weapon modules: rays,
  penetration, blasts, projectiles, stats/ammo/cadence;
- `world/level_base.zia`, `level_manager.zia`: owned nodes/bodies and coordinated
  subsystem unload;
- environment/effects/lighting/post-FX/audio/menu/HUD/save systems;
- 14 focused probes under `probes/`.

The game explicitly documents why `RunFixed` is insufficient: it needs custom
camera, viewmodel, world, HUD, and presentation control. FrameDriver/GameBase3D
must preserve those phases.

## Dependencies

Required: plans 03–15. Plans 06–07/13 must pass Ashfall spikes before this
migration begins. Plan 16 docs can proceed alongside cleanup but final docs wait
for actual migration decisions.

## Non-negotiable behavior invariants

- exact fixed tick ordering for player, weapons/projectiles, AI, hazards,
  pickups/objectives, physics, event consumption, camera, viewmodel, effects;
- movement acceleration/jump/crouch/ground behavior and input sensitivity/
  invert/FOV settings;
- weapon stats, spread seeded behavior, penetration, blast dedup, damage/team/
  region rules, ammo/reload/cadence/economy/difficulty/scoring;
- AI and campaign/level/objective progression;
- procedural fallback remains fully playable without downloaded assets;
- custom terrain/water/level render, viewmodel, post-FX, HUD/menu order;
- quality settings and performance/stress budgets;
- save schema/migration behavior;
- all 14 probes remain green and deterministic where currently deterministic.

## Migration strategy

Use adapter modules and dual-comparison fixtures, never two active simulations.
Migrate infrastructure from the outside inward:

1. frame/lifecycle/environment/assets/quality;
2. queries/events/cues;
3. motor/camera;
4. ballistics;
5. app/save/test cleanup.

Keep Ashfall's ECS-like integer IDs and game event bus for game-specific
events. Runtime entity/event identity replaces only the physics bridge.

## Implementation sequence

### Slice 0 — Freeze complete baseline

1. Run `zanna check` and all 14 probes: assets, campaign, combat, core, enemy,
   level, manifest, menu, meta, movement, perf, render, smoke, stress combat.
2. Capture fixed traces for:
   - player pose/velocity/ground/crouch and input settings;
   - camera/viewmodel pose/FOV/recoil;
   - representative weapon shot rays, impact order/regions/damage/ammo;
   - projectile and radial damage/dedup;
   - enemy health/state/projectiles and objective/campaign transitions;
   - level resource counts and unload/reload;
   - settings/save schema values;
   - event kinds/IDs/order.
3. Capture representative level/menu/HUD/combat frames at three quality tiers
   on software and Metal where feasible.
4. Record allocation/counter/timing data from perf and stress probes.

### Slice 1 — FrameDriver3D without render simplification

1. Match Ashfall fixed dt, frame clamp, max steps, pause/menu behavior and
   dropped-step policy.
2. Replace only accumulator/input/window timing first.
3. Run current `tickSim` body before each driver commit at the identical
   boundary. If `tickSim` currently performs its own raw physics step, separate
   pre-simulation game work from World3D `CommitFixedStep` so physics happens
   once.
4. Reproduce the existing render method exactly using explicit driver phases:
   camera/viewmodel preparation, level terrain, scene, water/custom content,
   effects, overlay, capture, present.
5. Compare 30/60/144 Hz, long frame, pause/menu, no-present capture traces.

No other subsystem migrates until all 14 probes pass this slice.

### Slice 2 — Level SceneScope3D ownership

1. Create root/application scope for global assets/audio/menu/services.
2. Give each loaded level a child scope.
3. Track LevelBase owned nodes/bodies/entities, terrain/water registrations,
   effects/sounds/lights and subsystem-owned transient objects.
4. Replace `ownedNodes`/`ownedBodies` release first, then coordinated
   LevelManager unload calls as supported.
5. Keep game-specific subsystem reset callbacks when they clear logical state,
   not just resources.
6. Test partial level boot, death/restart, level change, campaign completion,
   quit, and 100 load/unload cycles with exact live counts.

### Slice 3 — EnvironmentStack3D

Register per-level Terrain3D offset, Water3D, sky/time-of-day/lighting bindings,
and atmospheric renderables. Preserve level environment data and authored draw
order. Keep hazards/weather/game rules in Ashfall. Remove manual draws only
after reference captures and draw counters match.

### Slice 4 — Asset catalog

1. Register logical enemy/weapon assets and package/source candidates.
2. Preserve disabled-by-default headless asset policy and explicit probe opt-in.
3. Preserve per-archetype scale, template/SceneAsset caches, tried/loaded/failed
   counters, procedural fallback, and animation clip-intent mapping unless an
   existing runtime cache directly replaces them.
4. Replace only existence/path/source/package resolution first.
5. Run asset, manifest, enemy, render and packaged/no-assets probes.

### Slice 5 — Shared quality profile

Resolve requested quality once. Apply renderer/environment fields and let
Ashfall lighting/post-FX/effects read light budget, render scale, shadow,
particle/decal fields. Preserve authored post-FX and gameplay-neutral quality
policy. Reapply must be idempotent. Run menu/render/perf/smoke at all tiers.

### Slice 6 — Entity-aware queries and runtime events

1. Associate Ashfall physics target entities/bodies with World3D Entity3D where
   practical; retain game target integer ID in StateTag or an explicit game
   adapter, not pointer identity.
2. Replace body lookup in simple ray/ground/interaction paths with WorldHit3D.
3. Use ray-queryable hurt regions for region tag/multiplier.
4. Mirror runtime collision/hit/damage into plan-10 world events and consume
   them through an Ashfall adapter.
5. Keep `core/events.zia` for weapon fired, pickup, objective, campaign, UI,
   economy and other game-specific events.
6. Compare event order/IDs and allocation-free behavior.

Do not delete `targets.zia` until every game-target mapping consumer is either
runtime-backed or intentionally retained.

### Slice 7 — Pooled effects/audio cues

Migrate common named impacts, explosions, decals, muzzle/ambient/positional
sounds in small groups. Keep lighting flashes, camera shake, viewmodel feedback,
music mix, and weapon-specific orchestration in Ashfall. Map quality budgets to
cue pools. Compare stress allocations, drops/evictions, and render samples.

### Slice 8 — CharacterMotor3D

1. Map move/yaw/jump/sprint/crouch intent.
2. Preserve Ashfall input Action/settings, acceleration/game-feel parameters,
   weapon movement restrictions, health/death, and animation/viewmodel signals.
3. Adopt shared grounded/air/crouch drive only if movement probe traces match.
4. Compare step-by-step pose/velocity/jump apex/landing/ceiling crouch and
   long-run determinism.
5. Retain a thin custom motor policy if necessary. Do not add Ashfall weapon
   concepts to CharacterMotor.

### Slice 9 — CameraRig3D

1. Configure first-person base from player/entity pose and settings.
2. Map look sensitivity/invert outside the rig into look intent.
3. Map recoil, bob, damage shake, FOV aim/menu blends to modifier channels.
4. Keep viewmodel transform/render logic in Ashfall, consuming final camera/
   modifier state as appropriate.
5. Compare deterministic camera/viewmodel traces, seeded shake/recoil, pause,
   settings changes, and captures.

If viewmodel requires a modifier output not safely exposed, propose a generic
read-only camera offset/result through the owning ADR; do not reach into rig
internals.

### Slice 10 — Ballistics3D

Migrate representative weapon groups in increasing complexity:

1. single hitscan;
2. multi-pellet/spread (Ashfall generates deterministic rays; Ballistics handles
   each ray);
3. penetrating ray;
4. radial blast;
5. projectile impact may call Ballistics radial/single damage while projectile
   lifecycle remains game-owned.

Keep weapon definitions, cadence, ammo, reload, spread seeds, economy,
difficulty, scoring, and feedback in Ashfall. Compare impact order, region tags,
health, dedup, impulses, ammo/events, and stress timing. Retain custom paths for
weapon mechanics outside generic contract rather than bloating runtime.

### Slice 11 — GameBase3D application flow

Use GameBase3D for boot/menu/gameplay/pause/cinematic/results/transition only
after custom render spike is proven. Gameplay scene owns level scope and fixed
tick; HUD/menu reuse current code/available Game.UI. Maintain headless probe
boot paths and deterministic frame limits.

Do not move AI/weapon/objective systems into scene callbacks beyond their
current orchestrated calls; the framework is the shell.

### Slice 12 — SaveGame3D composition

Define an explicit Ashfall schema mapping settings/campaign/meta/custom fields
and existing World3D persistent level state. Provide migration from current
files/keys where product policy requires. Validate corrupt/missing/newer/older
versions and transactional failure. Preserve headless probe isolation and
procedural asset settings.

### Slice 13 — Harness consolidation and cleanup

1. Migrate representative movement/combat/render/stress probes to scenario
   harness, compare side-by-side, then reduce duplicate loop/assertion code.
2. Remove obsolete accumulator, resource arrays, body lookup, resolver,
   quality/pool code only when no behavior consumer remains.
3. Keep game event bus, weapon/AI/level logic, clip maps, viewmodel, and custom
   policies that remain legitimately game-owned.
4. Update README, OVERHAUL_REPORT, credits/package manifest, and code headers.

## Per-slice validation

Run `zanna check`, all 14 probes, focused runtime tests for the adopted layer,
deterministic trace comparison, and relevant captures after every slice. Run
graphics3d/perf labels, surface audits, package/no-assets modes, and full
platform builds before completion.

## Regression budgets

- game state/event/ammo/health/objective integers exact;
- movement/camera/impact floats within existing probe tolerances only;
- fixed tick and event order exact;
- no additional physics/input/camera update;
- level unload returns live counts exactly;
- no-assets/headless probe runtime remains flat;
- warmed cue/query/event paths retain allocation-free goal;
- stress/perf >10% regression on same machine/backend requires root cause;
- visual structural samples pass at all supported tiers/backends.

## Acceptance criteria

- All 14 Ashfall probes pass after every final slice and in full composition.
- FrameDriver supports the complete custom render/capture path.
- Level scopes and environment registration remove manual resource/draw
  boilerplate without losing logical reset work.
- Asset/quality/query/event/cue layers replace shared policy while procedural
  fallback and game event bus remain intact.
- Motor/camera/ballistics match behavior or remain partially custom with a clear
  retained-policy explanation.
- Save slots migrate safely and package/no-assets execution works.
- Performance, allocations, visuals, docs, and platform builds pass.

## Stop conditions

Stop a slice if it changes game feel, weapon semantics, AI/campaign behavior,
viewmodel order, headless asset policy, or stress budget; if the runtime layer
cannot express Ashfall without genre-specific branches, retain the game code
and feed the limitation back to its plan/ADR.

## Handoff evidence

Provide 14-probe matrix per major slice, state/event/camera/ballistics traces,
captures, lifecycle/allocation/perf results, packaged/no-assets results,
adopted-versus-retained architecture table, and runtime gaps.

