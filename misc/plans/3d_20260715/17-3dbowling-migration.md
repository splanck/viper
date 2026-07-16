# Plan 17 — 3dbowling Incremental Migration

## Outcome

Migrate Neon Lanes (`examples/games/3dbowling`) from independently owned
Canvas3D/SceneGraph/PhysicsWorld3D orchestration to the useful Game3D
application layers while preserving its tuned bowling physics, scoring,
camera/replay, presentation, accessibility, and all 27 release gates.

This is a strangler migration, not a rewrite. The current game remains the
behavior oracle. Runtime abstractions are adopted only where they remove
boilerplate without changing game feel.

## Current architecture

Key owners:

- `engine/renderer.zia`: Canvas3D, SceneGraph, camera, post-FX, lighting,
  quality, render order;
- `engine/game_setup.zia`: raw PhysicsWorld3D and scene construction/reset;
- `engine/game_flow.zia`: outer loop, fixed-step calculation, input, collision
  processing, screen/game state;
- `lane/ball.zia`, `lane/pins.zia`, `lane/lane.zia`: authoritative gameplay and
  physical objects;
- `menu.zia`, `engine/hud.zia`: UI/settings/profile flow;
- `effects/*`, `presentation/*`: feedback, bowler, replay;
- `util/assets.zia`: repository/project/package/direct candidate resolution;
- `game3d/game3d_setup.zia`: bounded World3D setup comparison, not the release
  game.

The separate migration sample measured 789 lines of setup reduced to 129
(83.7%) for its selected scope. Do not extrapolate that number to the full game
or use line-count reduction as the acceptance criterion.

## Dependencies

Required before migration slices:

- plans 01–02 overlay defects;
- plan 03 FrameDriver3D;
- plan 04 SceneScope3D;
- plan 08 asset policy;
- plan 09 quality profile if adopted;
- plan 10 entity-aware collision/query surfaces;
- plan 11 pooled cues for selected feedback;
- plan 12 GameBase3D for state orchestration;
- plan 14 save composition if profile/slot work is migrated;
- plan 15 scenario harness.

Plans 05–07 and 13 are optional for bowling. Do not force an environment,
character motor, or ranged-combat abstraction into this game.

## Non-negotiable behavior invariants

- identical pin rack geometry, body shape/mass/material/damping, constraints,
  lane/gutter/collider layout, gravity, and fixed-step size;
- identical shot input/release model, oil response, trajectory, pinfall settle
  thresholds, scoring, frame/match progression, and AI delivery;
- collision events processed in the same simulation-relative order;
- replay state/camera timing and result presentation preserved;
- quality modes retain authored look and backend fallbacks;
- accessibility, menu/profile save clamping, and input rebinding preserved;
- headless/state probes remain deterministic;
- no additional physics step, input poll, or scene sync per frame.

Any intentional gameplay correction is a separate change with updated design
spec/probes, not hidden inside migration.

## Primary files likely to change

- `engine/game_flow.zia`, `game_setup.zia`, `renderer.zia`, `game_render.zia`;
- `engine/game_state.zia`, `menu.zia`, `engine/hud.zia` for application scopes;
- lane/ball/pins only for ownership/entity association adapters;
- effects/presentation for cue/scope adoption;
- `util/assets.zia` for catalog delegation;
- main/game entry modules and project file;
- probes/scripts/README/audit documents.

Avoid broad mechanical edits to scoring/gameplay modules unless a focused
adapter cannot preserve behavior.

## Migration strategy

Introduce a bowling-specific adapter module, for example
`engine/runtime_host.zia`, that exposes the old renderer/world accessors while
internally using World3D/FrameDriver/SceneScope. This allows one owner to change
at a time. Remove the adapter only if the final code becomes clearer; it may
remain as the game's composition root.

Each slice lands green. Do not maintain two physics simulations or render the
same node/body in both hosts.

## Implementation sequence

### Slice 0 — Freeze release baseline

1. Run `viper check` and all 27 release gates through `run_probes.sh`/Windows
   companion. Record backend, dimensions, runtime, and the two Metal-only
   high-resolution gates.
2. Run all known-issue repros and record expected status after plans 01–02.
3. Use scenario harness to capture authoritative traces for:
   - launch velocity/spin and ball pose at defined fixed steps;
   - first-contact body IDs/order/impulses;
   - pin poses/velocities and standing/deadwood state;
   - settle time, pinfall count, score/frame state;
   - replay state/camera markers;
   - menu/profile/quality values.
4. Capture representative title, aiming, rolling, result, and menu frames on
   software and Metal.
5. Record entity/body/node/effect/audio counts across start match -> reset ->
   quit to title -> new match.

Commit no migration until these artifacts are reviewed.

### Slice 1 — Adopt World3D as the single host

1. Construct World3D with the exact existing title/size/camera projection.
2. Replace renderer-owned canvas/scene/camera access with `world.Canvas`,
   `world.Scene`, and `world.Camera` through the adapter.
3. Replace independently constructed PhysicsWorld3D with `world.Physics` and
   apply exact gravity/solver/body settings.
4. Keep current low-level mesh/material/node/body creation initially. Add them
   to World3D's underlying scene/physics through existing public APIs; do not
   convert every object to Entity3D in this slice.
5. Remove the old host only after counts prove one canvas/scene/physics world.
6. Keep current manual loop/render calls until Slice 2.

Gates: all physics/trajectory/pinfall/scoring probes and basic visual probes.

### Slice 2 — Replace timing accumulator with FrameDriver3D

1. Express current `game_flow` fixed-step policy in driver configuration. Match
   fixed dt, max frame dt, maximum steps, and dropped-step behavior exactly.
2. Poll only through `driver.BeginFrame`.
3. For each reserved step, run bowling's current authoritative update ordering,
   then `CommitFixedStep` once.
4. Keep collision/scoring processing at the same pre/post physics boundary. If
   current code consumes collision events after the step, do so after commit
   before the next reserved step.
5. Delegate standard/custom render phases in the same order as current
   Renderer3D, then overlay/present.
6. Compare traces under 30/60/144 Hz schedules and injected long frames.

Gates: frame-rate, impact-order, trajectory, stability, pinfall, release
upgrade, and deterministic trace comparisons.

### Slice 3 — SceneScope ownership

Create scopes:

- root/application: shared renderer assets, title art, global audio/settings;
- match: lane, rack, ball, physics bodies/nodes, match effects;
- throw/replay transient child if useful.

1. Track resources as they are created; do not alter creation values.
2. Replace manual clear-reference/reset choreography with scope Release in
   reverse order.
3. Handle self-expiring effects and manually removed bodies idempotently.
4. Compare counts across 100 lifecycle cycles and run lifecycle probe.
5. Retain immutable shared assets outside the match scope.

### Slice 4 — Entity association and event queries

Convert only identity-bearing physical objects first:

- ball Entity3D with attached existing-equivalent body/node;
- ten pin Entity3D values with names/stable tags;
- optional lane/gutter static entities only if lookup benefits.

Use World3D entity-aware collision events/body index to replace the current
two-pass body matching where semantics and performance improve. Preserve raw
contacts/impulses and stable pin index. Do not replace collision scanning if
the new event view cannot express the same ordering; file a runtime gap.

Compare exact first-contact and pinfall event traces.

### Slice 5 — Assets and quality

1. Configure AssetCatalog for title art/lane texture and optional assets.
2. Replace candidate search while preserving procedural fallback and exactly
   one missing diagnostic per logical asset.
3. Map two bowling UI quality choices to resolved profile values.
4. Preserve bowling's authored post-FX/shadow tuning; use profile fields for
   common values and retain deliberate overrides explicitly.
5. Re-run source-root, project-root, executable/package asset probes and quality
   captures.

### Slice 6 — Effects/audio cue adoption

Migrate one category at a time:

1. ball/pin impact cue;
2. trail or dust particle pool;
3. celebration/results cue;
4. optional replay feedback.

Keep scoring/feedback timing in bowling. Compare active caps, drop counters,
allocation counts, and captures. Do not replace specialized trajectory trail
data if a generic particle cue loses its shape.

### Slice 7 — GameBase3D scene flow

Represent title/menu, gameplay, pause, result, and replay as game application
states/scenes only after FrameDriver integration is stable.

- keep bowling domain state machine inside gameplay where it models throw/
  settle/scoring phases;
- use GameBase3D for outer lifecycle, not every internal state;
- assign match scope to gameplay and root scope to persistent UI/settings;
- preserve menu input/accessibility and fade/replay ordering;
- use existing HUD/widgets/draw code; no UI rewrite is required.

Compare menu-flow, match-mode, release-menu, replay-scene, and accessibility
probes.

### Slice 8 — Save composition

Adopt SaveGame3D only if it simplifies profile/progression without changing
existing paths unexpectedly. Define schema version and migrate current keys
explicitly. Missing/corrupt data must retain current clamp/default behavior.
Match-in-progress world snapshot is optional product policy; do not add it just
because the runtime supports it.

### Slice 9 — Probe consolidation and cleanup

1. Migrate repeated fixed/render/input loops to scenario harness while keeping
   domain checks.
2. Remove old host/accumulator/ownership/asset code only after no consumer
   remains and all probes pass.
3. Decide whether `game3d/game3d_setup.zia` remains a historical comparison,
   becomes a focused fixture, or is removed with README update. Do not leave two
   advertised architectures without explanation.
4. Update README, UPGRADE_AUDIT, long-term spec, known-issues status, and code
   map comments.

## Per-slice validation

At minimum after every slice:

```sh
build/src/tools/viper/viper check examples/games/3dbowling --diagnostic-format=json
examples/games/3dbowling/run_probes.sh
ctest --test-dir build -R 'g3d_game3d_bowling_setup|test_rt_game3d|test_rt_canvas3d' --output-on-failure
```

Use Windows script on Windows. Run state traces twice and representative
software/Metal captures. Before final completion run graphics3d label, surface
audits if runtime surfaces changed, package dry run, and full platform builds.

## Regression budgets

- authoritative fixed-step positions/velocities use existing probe tolerances;
  any new tolerance needs physics-owner review;
- scoring/pinfall/event IDs/order are exact;
- no extra dropped fixed steps under the same injected schedule;
- body/node/entity/effect counts return exactly to baseline after lifecycle;
- warmed feedback allocations do not increase;
- representative visual samples stay within existing release checks;
- release probe runtime must not materially regress; investigate >10% in the
  same backend/configuration.

## Acceptance criteria

- All 27 release gates pass with documented backend selection.
- Fixed physics/scoring/replay traces match baseline.
- One World3D host and one input/time poll exist.
- FrameDriver replaces game-owned accumulator without constraining rendering.
- Match scope releases all mutable match resources idempotently.
- Asset/quality/event/cue/framework adoption removes duplicate policy where it
  proves equivalent.
- The game remains playable with procedural fallbacks and packages correctly.
- Documentation describes one current architecture.

## Stop conditions

Stop a slice if it changes solver/body parameters, scoring order, event timing,
or visual identity; if an abstraction cannot express the current behavior,
retain the game code and report the gap to the owning runtime plan. Do not bend
the game to validate an abstraction.

## Handoff evidence

Provide the 27-gate matrix, before/after state/event traces and captures,
lifecycle counts, architecture/code ownership diff, removed boilerplate list,
retained custom-policy list, package result, and any runtime gaps.

