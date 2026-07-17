# Plan 12 — Game Application, Scene, and UI Framework

## Outcome

Upgrade `examples/games/lib/gamebase3d.zia` into a production-capable,
fixed-step application framework using FrameDriver3D and SceneScope3D. It must
support scene lifecycle, custom render phases, pause/resize/transitions,
deterministic frame limits, and existing `Zanna.Game.UI` widgets without adding
another widget toolkit.

## Problem statement

The current example-library GameBase3D is valuable but intentionally small. It
uses variable `world.DeltaTime`, calls one `scene.update` before one
`StepSimulation`, owns a fixed `DrawScene/DrawEffects` sequence, and exposes
only a global overlay hook. `IScene3D` provides enter/exit/update/overlay.

That does not cover the reviewed games:

- bowling needs fixed gameplay/scoring/replay phases;
- Ridgebound needs environment draw phases, quality/menu state, and pause;
- Ashfall needs fixed simulation, pre/post scene world draws, viewmodel/HUD,
  and capture/present control.

The framework should orchestrate existing layers, not promote a monolithic game
engine or duplicate UI.

## Dependencies

- plans 01–02 overlay correctness;
- plan 03 FrameDriver3D;
- plan 04 SceneScope3D;
- plan 05 environment integration is optional for first framework landing but
  required for final starter;
- existing `Zanna.Game.UI`, `Zanna.GUI`, StateMachine, ScreenFX, Action input;
- API register and risk R13.

## API/ADR decision

First delivery is **example-library incubation**, not runtime C ABI. No ADR is
required solely for `.zia` library changes. If the implementation promotes
GameBase3D/scene interfaces into the runtime, adds cross-layer runtime
dependencies, or changes public C surfaces, stop and create an ADR.

The framework may evolve during all three demo adoption spikes without ABI
cost. Promotion is a later decision based on plan 20 evidence.

## Scope

In scope:

- fixed and variable update hooks;
- deferred, scope-backed scene transition;
- before-scene, after-scene, overlay, resize hooks;
- pause/time policy and unscaled transition clock;
- root/global and per-scene resource scopes;
- existing Game.UI/Hud widgets and optional UI layer organization;
- deterministic frame/capture controls;
- starter and framework tests.

Out of scope:

- new Button/Menu/Panel/TextInput runtime classes;
- forcing one state machine/genre architecture;
- serializing scenes;
- loading level assets automatically;
- script callbacks from C workers.

## Primary source owners

- `examples/games/lib/gamebase3d.zia`
- `examples/games/lib/iscene3d.zia`
- new narrowly named support modules under `examples/games/lib/` if needed;
- `examples/3d/game3d_scenes/` and `game3d_starter/`;
- docs GameBase3D section, game loop/UI links;
- Zia fixtures/package dry runs.

## Framework contract

### Scene interface

Avoid a circular dependency on the concrete GameBase3D type. Preferred
incubation signature:

```text
onEnter(world, scope)
onExit(world)
fixedUpdate(world, dt)
afterFixedStep(world, dt)
update(world, frameDt, interpolationAlpha)
drawBeforeScene(world)
drawAfterScene(world)
drawOverlay(world)
onResize(world, width, height)
```

If Zia interfaces cannot provide no-op defaults, ship a `SceneBase3D` adapter
class or retain a NullScene implementation so simple scenes do not implement
unused behavior. Game-specific services are passed to scene constructors or
stored in a game-owned context; do not make the interface reference a concrete
game subtype.

### Frame sequence

```text
while driver.BeginFrame():
  apply pending scene transition at safe boundary
  while driver.BeginFixedStep():
    current.fixedUpdate(world, driver.FixedDt)
    driver.CommitFixedStep()
    current.afterFixedStep(world, driver.FixedDt)
  current.update(world, driver.FrameDt, driver.InterpolationAlpha)
  driver.BeginRender()
  current.drawBeforeScene(world)
  environment/standard scene draws
  current.drawAfterScene(world)
  standard effects
  driver.EndRender()
  driver.BeginOverlay()
  current.drawOverlay(world)
  global UI and transition fade
  driver.EndOverlay()
  capture if requested
  driver.Present()
```

Exact environment placement follows plan 05. `afterFixedStep` consumes
post-physics collision/damage/events and refreshes controller/motor state before
another reserved step. Variable `update` is presentation/UI work;
authoritative pre-simulation logic belongs in `fixedUpdate`.

### Scene transition

- request is deferred; never call exit/enter in the middle of fixed update or
  draw;
- construct a new child SceneScope3D before `onEnter`;
- if enter fails/traps under supported recovery, release the new scope and keep
  or move to a safe NullScene per documented behavior;
- fade out uses unscaled time; switch at full opacity; release old scope after
  `onExit`; enter new scene; fade in;
- repeated requests have a defined replace/queue policy;
- quit/close releases current scope, root scope, and world once.

### Pause

Separate:

- application running/window polling;
- simulation paused (no fixed updates/commits or World3D paused per one
  authority);
- presentation/UI updating in unscaled time;
- audio pause policy chosen by game.

Do not accumulate paused wall time into a burst of fixed steps on resume.

## Implementation sequence

### Phase 0 — Framework characterization

Add tests for current GameBase3D scene enter/exit, transition, max frames, and
overlay. Capture current simple example behavior. List all direct consumers so
the upgrade can provide a compatibility adapter or update them together; the
known consumers at review time were `examples/3d/game3d_scenes/main.zia` and
docs snippets, but re-enumerate before editing.

### Phase 1 — New scene interface/base adapter

1. Add fixed/pre-simulation, after-fixed-step, variable, draw, and resize
   methods.
2. Provide no-op base/null implementation.
3. Keep old `update` and single-argument `onEnter(world)` compatibility
   temporarily through an adapter if existing examples use them (the current
   interface passes no scope); mark and remove only within this coordinated
   example update.
4. Document which methods may mutate simulation.

### Phase 2 — Driver and scope integration

1. Construct World3D then FrameDriver3D and root SceneScope3D.
2. Replace variable StepSimulation with reserve/fixedUpdate/commit loop.
3. Create/release per-scene child scopes.
4. Expose safe getters for world, driver, root/current scope, frame dt, fixed dt,
   interpolation alpha, and current scene state.
5. Preserve max-frame deterministic termination and add finish-without-present
   capture mode only through FrameDriver's approved API.

### Phase 3 — Custom render phases

Add scene hooks around standard/environment draws. Ensure:

- hooks run only inside a valid 3D frame;
- before/after names correspond to documented depth/transparency semantics;
- standard draws can be disabled/replaced explicitly for menus or unusual
  scenes without corrupting driver phase;
- overlay hook always runs inside final overlay;
- resize is dispatched after World3D/Canvas resize handling.

### Phase 4 — Transition/pause robustness

1. Reimplement fades using fixed plan-01 alpha behavior.
2. Use unscaled frame time for UI transitions.
3. Define scene request collision policy.
4. Add pause/resume without accumulator burst.
5. Add close during fade, scene enter, and paused menu tests.
6. Ensure old scope releases after its final valid callback and before new
   scene starts depending on documented switch order.

### Phase 5 — UI integration without duplication

Create an optional example-library `UiLayer3D` organizer only if needed. It may
hold and draw existing `Zanna.Game.UI.Hud*` values, route Input3D/Action state,
and manage focus/modal order. It must not implement duplicate widget visuals,
text editing, layout, or hit testing already in Game.UI/GUI.

Add recipes for:

- title/menu scene;
- pause modal over a still-rendered gameplay scene;
- HUD layer plus debug overlay;
- resize-safe normalized/anchored layout using current widget APIs;
- keyboard/mouse/gamepad Action navigation.

### Phase 6 — Tests

Framework tests:

- enter/exit exactly once and safe deferred switch;
- multiple fixed steps/zero-step render frame;
- fixed versus variable update counters;
- post-fixed hook runs once after each commit and before the next reserved step;
- custom draw hook order;
- environment/scene/effects/overlay order;
- pause/resume no step burst;
- fade timing unaffected by time scale;
- replacement transition request policy;
- resize dispatch;
- close at every lifecycle phase;
- scene scope resources fully released;
- root resources survive scene switch and release on quit;
- deterministic max frames/capture without present;
- UI modal/focus/action navigation smoke;
- simple old scene compatibility or coordinated migration;
- no per-frame framework allocation after warm-up beyond UI behavior already
  documented.

### Phase 7 — Three adoption spikes

Build non-release branches/fixtures:

- bowling title -> gameplay -> result with fixed scoring loop;
- Ridgebound title/pause/gameplay with environment hooks;
- Ashfall boot/menu/gameplay with viewmodel and capture hook.

The API is not ready for promotion until all fit without subclass hacks or
private driver access. Record game-specific services that remain outside.

### Phase 8 — Starter/docs

Update `game3d_scenes` and `game3d_starter` to show fixed update, resource scope,
quality, UI, and custom render hook. Keep the smallest hello example on
`World3D.Run` so users see the simple path first.

## Validation

Run `zanna check` on all changed libraries/examples, scene/starter/docs fixtures,
package dry run, FrameDriver/scope tests, UI widget/Canvas adapter tests,
graphics3d label, and full build scripts.

## Acceptance criteria

- All three adoption spikes fit the framework without bypassing timing or
  phase validation.
- Authoritative logic uses fixedUpdate; UI/presentation uses variable/unscaled
  time as documented.
- Scene scope lifecycle is exact and leak-free.
- Custom world/viewmodel/environment draws and capture are supported.
- Existing Game.UI is reused; no duplicate widgets are added.
- Simple Run path remains documented and unchanged.
- Framework/starter/package/tests pass.

## Stop conditions

Stop if the framework needs a concrete game type in the scene interface,
requires C callbacks, duplicates UI widgets, or cannot express an Ashfall
custom render phase. Keep it incubating and revise rather than promoting an
inflexible API.

## Handoff evidence

Provide lifecycle and frame-order traces, pause/transition state table, scope
counts, three adoption spike diffs, UI reuse inventory, allocation measurement,
and promotion recommendation.
