# Plan 03 — Fixed-Step Frame Driver and Explicit Render Phases

## Outcome

Add a poll-based `FrameDriver3D` that owns window/input polling, frame-delta
clamping, fixed-step accumulation, spiral protection, interpolation telemetry,
and phase validation while leaving gameplay and custom world rendering under
script control. It must cover all three reviewed loop shapes without forcing
callbacks or a fixed render order.

## Problem statement

World3D provides both concise `Run*` loops and complete manual phase methods.
The concise loops accept only update and overlay callbacks and own the standard
render sequence. The demos need more:

- bowling runs a gameplay fixed-step loop and collision/scoring order;
- Ridgebound inserts terrain, water, weather, and custom HUD phases;
- Ashfall needs fixed simulation, a custom camera and viewmodel, level terrain
  and water, HUD/menu, capture, and explicit present control.

Consequently each game owns accumulator/clamp/spiral logic or accepts variable
steps. The driver should remove timing/state boilerplate without hiding the
existing manual operations.

## Dependencies

- plan 00 contracts;
- plans 01–02 complete, so framework overlays and textured HUD validation rest
  on correct rendering;
- API register and risk R2/R3 review.

## ADR and public surface

Required: new public runtime class/C ABI and frame-order contract. The ADR must
approve the `FrameDriver3D` name, phase state machine, time ownership, error
behavior, disabled-graphics behavior, and relationship to `World3D.Run*`.

Do not implement before three disposable script spikes demonstrate the draft
API against bowling, Ridgebound, and Ashfall loop shapes.

## Primary source owners

- new focused `src/runtime/graphics/3d/rt_game3d_frame_driver.c/.h` or a naming
  variant approved by the ADR;
- `rt_game3d_internal.h`, `rt_game3d.h`, class ID registry;
- `src/il/runtime/defs/game3d/world.def` or the modular owner selected by review;
- `src/runtime/CMakeLists.txt` real and disabled variants;
- World3D tick/fixed-loop helpers in `rt_game3d.c`;
- VM bridge only if the ADR rejects poll-based design and approves callbacks;
- `test_rt_game3d.cpp`, a focused new unit test if size warrants it;
- Game3D fixtures and `src/tests/CMakeLists.txt`;
- Game3D docs/frame-order architecture and starters.

## Required semantics

State machine:

```text
Idle -> FrameOpen -> StepReserved -> FrameOpen -> Render3D -> Overlay -> Presented -> Idle
```

Permitted branches:

- zero or more fixed steps per frame;
- cancel/close after `BeginFrame` without rendering;
- render without present for deterministic capture;
- custom Canvas3D draws between standard delegated phases;
- no rendering in test-only stepping if the caller chooses.

Core rules:

- only `BeginFrame` polls input/window and advances frame clocks;
- `BeginFixedStep` reserves one accumulated scaled-time step but does not call
  script or simulate;
- the game runs its fixed update, then `CommitFixedStep` performs exactly one
  World3D simulation step and consumes the reservation;
- a second `BeginFrame` while a frame is open traps;
- a reserved step must be committed before render/present; cancellation has a
  documented rule and cannot partially advance authoritative time;
- `MaxFrameDt` clamps accumulator input; `MaxFixedSteps` drops excess full steps
  deterministically and increments telemetry;
- interpolation alpha is `remainingAccumulator/fixedDt`, clamped to `[0,1)`;
- World3D remains the pause/hit-stop/time-scale authority. `BeginFrame` uses the
  scaled `world.DeltaTime` produced by the single World3D poll for its
  accumulator and exposes `UnscaledFrameDt` separately. Commit calls the same
  internal fixed simulation core used by current `RunFixed`, so it does not
  apply time scale or advance poll/frame/unscaled clocks a second time;
- render interpolation is visual only.

## Implementation sequence

### Phase 0 — Reconfirm current loop internals

1. Read current `World3D.Update`, fixed-loop implementation, delta/elapsed
   updates, input snapshot, dropped-step counters, and render interpolation.
2. Document which private helpers can be reused without changing Run behavior.
3. Add current loop-order tests if any timing edge is unprotected.
4. Capture traces for a normal frame, zero-delta frame, long frame, paused
   frame, and close request.

### Phase 1 — Three API feasibility spikes

Create temporary or non-shipping small fixtures that express:

- bowling: `while BeginFixedStep`, game update, commit, standard scene, overlay;
- Ridgebound: game update, terrain draw, scene draw, water draw, effects,
  overlay;
- Ashfall: custom camera/viewmodel before/after scene, optional capture without
  present, menu pause behavior.

The spikes may initially use a Zia mock driver. Review them before ABI freeze.
Reject designs that require duplicate `World3D.Update`, access to private
accumulator fields, or callbacks with captured game state.

### Phase 2 — ADR and data model

Define:

- retained world reference and one-driver-per-world policy;
- fixed dt, max frame dt, max steps, accumulator, phase, counters;
- how world destruction invalidates the driver;
- whether existing World3D fixed counters delegate to or remain separate from
  driver counters;
- phase enum values and whether they are public diagnostics only;
- `CancelFrame` behavior on close/error;
- finalizer behavior if a step is reserved.

Append a class ID after checking the live registry. Add full source headers.

### Phase 3 — Scheduling core

1. Extract or reuse a single internal poll/delta helper so `World3D.Update` and
   FrameDriver cannot drift.
2. Implement `BeginFrame` by sharing the existing World3D poll/time-control
   path, then add its already-scaled delta to the accumulator with the driver's
   additional max-frame clamp, step-count calculation, and close result.
3. Implement reservation/commit. Commit calls
   `game3d_world_step_simulation_impl(world, fixed, 0)` or a renamed shared
   equivalent exactly once. It must not call public `StepSimulation`, because
   that entry applies time control and advances unscaled/frame counters for a
   standalone manual step. Update driver counters only after validation.
4. Implement dropped-step behavior with an explicit test for the retained
   remainder.
5. Implement interpolation telemetry and sync it with World3D render
   interpolation without double application.
6. Make all error paths phase-safe and diagnosable.
7. Ensure steady-state scheduling allocates nothing.

### Phase 4 — Render delegation

Implement thin validated delegates:

- `BeginRender` -> World3D begin frame;
- `DrawStandardScene` -> World3D scene plus existing streamed terrain;
- `DrawStandardEffects` -> existing effects/debug draw;
- `EndRender` -> World3D end scene/post-FX boundary;
- `BeginOverlay`/`EndOverlay` -> Canvas3D final overlay;
- `Present` -> existing flip and state reset.

Do not automatically call standard draws. This is the extensibility feature.
Callers can draw registered environment phases from plan 05 or custom content
around them.

Support capture between `EndOverlay` and `Present`. A no-present frame must be
explicitly completed/reset without polling another frame in an invalid state;
add `FinishWithoutPresent` only if existing capture cannot safely serve this
role and the ADR approves it.

### Phase 5 — Registration and disabled graphics

1. Add declarations, definitions, qualified types, class metadata, and docs.
2. Add real and disabled-graphics symbols. Disabled mode should allow
   deterministic scheduling tests if World3D construction supports it, or fail
   through the established graphics-disabled diagnostic consistently.
3. Add CMake sources to all runtime variants.
4. Dump and review runtime surface diff.
5. Avoid VM changes because calls are ordinary methods. If callbacks appear,
   stop and revisit the design.

### Phase 6 — Tests

Unit tests must cover:

- normal 60 Hz schedule;
- render at 144 Hz with some zero-step frames;
- 30 Hz with two fixed steps;
- long-frame clamp and max-step drop;
- remainder/interpolation alpha;
- pause/time scale/hit-stop interaction;
- close request before simulation;
- every illegal phase transition and diagnostic;
- world destroyed before driver and driver released before world;
- fixed update state visible to the following simulation step;
- capture without present and next-frame recovery;
- driver plus `RenderInterpolation` leaves authoritative transforms unchanged;
- no allocation after warm-up;
- two identical runs produce identical traces.

Add Zia fixtures for all three loop shapes and at least one native/AOT use of
the class surface.

### Phase 7 — Compatibility and docs

- Keep `World3D.Update` and all `Run*` methods behavior-compatible.
- Where safe, refactor existing fixed loops to share private scheduling helpers,
  but compare their existing probes before/after byte-for-byte.
- Document when to use Run, FrameDriver, or fully manual phases.
- Update frame-order diagrams and the starter, but leave full GameBase3D work
  to plan 12.

## Validation

Run focused Game3D unit tests, run-fixed/run-frames fixtures, surface audits,
docs snippets, graphics3d label, allocation/perf probe, disabled-graphics
build, and full platform script. Follow the shared matrix.

## Acceptance criteria

- All three feasibility spikes fit without private access, duplicate polling,
  or callbacks.
- Fixed-step traces match current World3D semantics for equivalent inputs.
- Custom render phases can be inserted before scene, after scene, before
  overlay, and before present.
- Illegal ordering traps clearly; normal close/cancel does not.
- Steady-state driver work allocates zero heap memory.
- Existing Run/RunFixed/RunFrames tests remain unchanged and green.
- Surface audits, disabled graphics, VM, native, docs, and all backends pass.

## Stop conditions

Stop if a poll-based class cannot represent one of the three game loops, if
time would be advanced twice, or if public callbacks are needed solely to
recover object-oriented dispatch. Rework the design/ADR rather than shipping a
second inflexible loop.

## Handoff evidence

Provide the three spike snippets, state-machine table, timing traces, allocation
measurement, API dump diff, compatibility test results, and ADR.
