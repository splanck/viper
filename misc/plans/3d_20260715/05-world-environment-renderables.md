# Plan 05 — World Environment Renderables

## Outcome

Let a World3D own, tick, and draw registered terrain, water, vegetation, sky,
and time-of-day objects in defined render phases. Preserve low-level manual draw
APIs and streamed-terrain ownership. Make `Environment3D` presets participate
in the same registry instead of remaining a separate convenience path.

## Problem statement

World3D automatically draws its scene graph, streamed terrain, registered
effects, and debug overlays. It does not automatically draw arbitrary
Graphics3D `Terrain3D`, `Water3D`, or `Vegetation3D` objects. Ridgebound
therefore calls `DrawTerrain`, `DrawVegetation`, and `DrawWater` in custom
phases; Ashfall calls `DrawTerrainAt` and `DrawWater`; both manually update sky,
lighting, and time-of-day policy.

Current `Environment3D` presets are useful but limited: `EnvHandle.WithTerrain`
creates a flat ground entity/collider and `WithWater` creates a translucent
plane entity. They do not register the low-level terrain/water systems used by
the demos. This plan unifies ownership/orchestration without removing simple
presets.

## Dependencies

- plan 03 for explicit render phases;
- plan 04 for scope ownership;
- plans 01–02 for reliable overlay/capture validation;
- API and risk registers, especially R5.

## ADR and API

Required: new public `EnvironmentStack3D` class or an approved equivalent,
World3D ownership, cross-layer references to low-level Graphics3D types, and a
render-order contract.

The existing `Game3D.RenderPass` values are profiling attribution IDs, not
automatically orchestration phases. The ADR must decide whether to add a
separate `EnvironmentPhase3D` constants surface or prove that extending
`RenderPass` is semantically correct. Do not overload current meanings merely
to avoid a small constants class.

## Scope

In scope:

- registration/removal/enable for terrain, water, vegetation, sky, and
  time-of-day;
- update and render phase scheduling;
- positions for terrain and optional bounds/culling metadata;
- scope-linked lifetime;
- simple Environment3D preset delegation;
- quality hooks consumed by plan 09;
- telemetry and cross-backend captures.

Out of scope:

- a new terrain/water/sky renderer;
- merging streamed terrain into the non-stream registry;
- game-specific weather simulation;
- automatic discovery of raw objects held by script;
- changing Graphics3D draw APIs.

## Primary source owners

- new focused Game3D environment-stack implementation/header;
- World3D struct, construction, simulation tick, render, and destruction;
- `rt_game3d_world_sim.inc` current EnvHandle implementation;
- `rt_game3d_presets.c` and Game3D assets/environment definitions;
- low-level terrain/water/vegetation/sky/time-of-day headers for validated calls;
- class ID/defs/CMake real and disabled variants;
- Game3D unit tests, visual fixture, docs, and environment starter.

## Render/update contract

The ADR must freeze an explicit sequence based on actual low-level requirements.
The expected design is:

1. fixed update: advance TimeOfDay3D only when configured for automatic advance;
2. before opaque scene: sky state/setup only if required by the backend;
3. opaque environment: terrain and opaque vegetation;
4. standard World3D scene graph and streamed terrain;
5. transparent environment: water and transparent vegetation/elements;
6. standard effects/debug;
7. end scene/post-FX;
8. overlay.

If source shows sky must render inside a different pass, encode that exact
requirement. Registration order is stable within a phase. A type gets a safe
default phase; `SetPhase` permits only valid type/phase combinations.

World3D manual callers may either call stack draw methods at explicit points or
use FrameDriver helpers. Existing `World3D.DrawScene` behavior must remain
compatible until an explicit environment entry is registered.

## Data model and ownership

Each entry stores:

- monotonic nonzero handle;
- tagged type and retained low-level object;
- phase, enabled flag, insertion sequence;
- terrain world offset and optional bounds;
- optional owning SceneScope3D weak link/registration token;
- type-specific update/draw flags;
- last-draw and fallback diagnostics.

The stack is world-owned. Removal drops registration and retained reference but
does not directly free an object still owned elsewhere. Scope release removes
its registrations before releasing the underlying resources. Registering the
same mutable object twice is rejected or idempotent per ADR; never double-draw
silently.

## Implementation sequence

### Phase 0 — Render-order proof

1. Trace current low-level DrawTerrain/At, DrawWater, DrawVegetation, sky, and
   TimeOfDay methods for depth, blending, camera, post-FX, and frame-state
   prerequisites.
2. Document current Ridgebound and Ashfall call order and produce reference
   captures at fixed camera/quality.
3. Test what happens when each draw is moved before/after scene/effects. Use
   this only to choose phases; do not commit speculative reordering.
4. Record streamed-terrain placement in `World3D.DrawScene` and exclude it from
   new registration to prevent double draws.

### Phase 1 — ADR and core registry

1. Approve phases, public names, ownership, duplicate rules, limits, and
   disabled behavior.
2. Append class ID and add stack pointer to World3D.
3. Implement allocation, registration, stable handles, enable/remove/clear,
   finalizer, and typed validation.
4. Use capacity growth with checked arithmetic and a no-allocation steady state.
5. Add a world invalidation path and scope registration hook.

### Phase 2 — Typed update/draw adapters

Implement one adapter at a time with a unit test:

1. Terrain3D at an explicit offset; preserve `DrawTerrain` zero-offset behavior.
2. Vegetation3D in its supported pass and camera/culling setup.
3. Water3D using the active camera and transparent ordering.
4. Sky3D if an explicit draw/register operation is needed; otherwise store it
   as world environment state and document why it is not a draw entry.
   `World3D.SetSkybox`, `SetAmbient`, and `SetFog` already exist as world
   environment state setters, which favors the state option unless the render
   trace proves a per-frame draw entry is required.
5. TimeOfDay3D advance using fixed or selected time domain; bind its sun/sky/
   reflection probe through existing methods.

Every adapter checks object validity and continues safely after a removed or
invalid entry according to established diagnostics.

### Phase 3 — World and frame integration

1. Add `World3D.Environment` read-only property.
2. Call environment update at the documented simulation location exactly once.
3. Add explicit draw-by-phase methods for fully manual callers.
4. Integrate FrameDriver convenience calls only after explicit phase methods
   work. Do not make `DrawScene` silently draw water after it has already ended
   the compatible pass.
5. Ensure capture and profiling attribute environment draws to correct existing
   renderer pass counters.

### Phase 4 — Preset compatibility

Refactor `EnvHandle` so:

- its current flat terrain/water entity behavior remains source-compatible;
- created entities are scope-trackable;
- new overloads or methods may register actual Terrain3D/Water3D values if the
  ADR approves them;
- handle finalization removes only its own registrations/entities;
- applying a second preset does not leak/double-register the first;
- simple examples still require one or two calls.

Do not rename or remove `Environment3D.Outdoor/Sunset/Overcast/Night`.

### Phase 5 — Quality and diagnostics hooks

Expose internal setters that plan 09 can use for vegetation density, terrain
LOD bias, and water quality. If the low-level types lack these controls, the
resolved profile remains informational rather than inventing fake behavior.

Add bounded diagnostics for skipped invalid entries and draw counts. Reuse
World3D/Canvas pass counters where possible.

### Phase 6 — Tests

Required tests:

- each type registers, draws/updates once, disables, re-enables, removes;
- stable insertion order within phase;
- invalid type/phase rejection;
- duplicate registration behavior;
- terrain offset and multiple terrain entries;
- streamed terrain plus registered terrain without double draw;
- transparent water after opaque scene on software and GPUs;
- TimeOfDay fixed-step determinism and pause/time-scale behavior;
- scope release removes entries before object release;
- world destroy with entries and entry object released first;
- Environment3D preset compatibility;
- quality change does not recreate resources unnecessarily;
- zero steady-state allocation;
- reference captures match manual Ridgebound/Ashfall equivalents within
  documented tolerances.

### Phase 7 — Docs and adoption spike

Add a small starter scene with terrain, vegetation, water, and time of day. In
temporary migration branches, replace only Ridgebound/Ashfall manual draw calls
with stack registration and compare captures/probes. Full migrations remain
plans 18–19.

## Performance budget

- update/draw is linear in registered entries with a small constant;
- no heap allocation after registration;
- no extra scene traversal;
- disabled entries are cheap and do not call low-level renderers;
- quality changes occur off the per-frame hot path;
- 100 registered entries should not show quadratic handle lookup/removal.

## Validation

Run Game3D, terrain, water, vegetation, Canvas3D production/GPU paths, visual
fixtures, graphics3d label, surface audits, disabled graphics, platform policy,
and complete build scripts. Validate software, Metal, D3D11, and OpenGL.

## Acceptance criteria

- Ridgebound and Ashfall environment draw spikes can replace manual calls with
  equivalent captures and no private access.
- World3D owns/ticks/draws registered entries exactly once in documented order.
- Streamed terrain and Environment3D presets remain compatible.
- Scope/world destruction is idempotent and leak-free.
- Transparent and opaque ordering passes all backends.
- No steady-state allocation and no public low-level API regression.

## Stop conditions

Stop if one generic phase model cannot represent actual backend/render
requirements, if registration would double-own streamed terrain, or if scope
integration cannot avoid cycles. Refine the phase/ownership design through ADR.

## Handoff evidence

Provide low-level phase proof, reference/manual versus registered captures,
entry lifecycle trace, allocation measurement, surface diff, backend matrix,
and ADR.

