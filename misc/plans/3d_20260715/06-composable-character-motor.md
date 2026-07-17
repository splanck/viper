# Plan 06 — Intent-Driven Composable Character Motor

## Outcome

Add a `CharacterMotor3D` that converts explicit gameplay intent into movement
through an existing `CharacterController3D`. It must not poll devices or own a
camera. It should cover common grounded/airborne sprint, jump, crouch, and
optional swim/fly policy while preserving the low-level Character3D and current
built-in controllers.

## Problem statement

The current `CharacterController3D.Update(world,input,camera,dt)` is convenient
for a standard controller, but its input and camera policy are coupled to its
movement update. Ridgebound and Ashfall therefore own custom motors for sprint,
swim, stamina/gameplay state, first-person control, and genre-specific response.
AI, replay, network, and deterministic tests also need to drive movement without
fabricating device input.

This plan extracts reusable motion policy at the correct seam: explicit intent
in, existing physics character/controller out.

## Dependencies

- plan 03 fixed-step scheduling;
- plan 10 entity-aware ground/query information. Note that
  `CharacterController3D.GroundEntity` already resolves the ground entity
  today; the plan-10 dependency covers the broader query/event surfaces, not
  basic ground identity, so motor extraction work may begin while plan 10 is
  still landing;
- current Character3D/CharacterController3D contracts and ADR 0105/0075/0076;
- API register and risk R6.

## ADR and API

Required: new public class and movement/tick-order semantics. The ADR must
approve whether the new class wraps `CharacterController3D` or extends it with
an intent update plus a thin motor facade. Do not duplicate collision/step/
slope/platform logic.

The API register is the target behavior. If a smaller extension such as
`CharacterController3D.UpdateIntent` plus a Zia motor wrapper meets all needs,
prefer it and update dependent plans.

## Scope

In scope:

- device-independent move/facing/jump/sprint/crouch intent;
- acceleration/deceleration, air control, gravity, jump, and state telemetry;
- reuse of current grounded/sliding/platform/crouch/traversal behavior;
- deterministic fixed-step update;
- input convenience outside the core;
- first-person/third-person/Ridgebound/Ashfall adoption spikes.

Out of scope:

- animation state machines;
- stamina, damage, inventory, or game-specific movement restrictions;
- camera calculation;
- network prediction/rollback protocol;
- rewriting low-level Character3D collision resolution.

## Primary source owners

- `rt_game3d_controllers.c` current character controller and shared drive code;
- new focused motor file if approved;
- `rt_game3d_internal.h`, `rt_game3d.h`, class IDs and controllers definitions;
- low-level `rt_physics3d_character.c` only for proven prerequisite bugs;
- Game3D controller unit tests and Zia fixtures;
- controller/movement docs and starters.

## Core design

### Intent

Intent is sampled by the game once per fixed step:

- `MoveIntent`: normalized or clamped 2D local movement;
- `FacingYaw`: world yaw that maps local forward/right;
- edge-triggered `JumpRequested`;
- held `Sprint` and `Crouch`;
- optional mode selected explicitly by game state.

The motor clears one-shot intent after `Update`. Held intent remains until
changed or `ClearTransientIntent` per ADR. Document this precisely to prevent
double jumps.

### Physics authority

The wrapped CharacterController3D/Character3D remains the authority for
grounded state, collision, slopes, steps, crouch clearance, pushing, and moving
platforms. The motor computes target planar velocity and vertical policy, then
calls a refactored internal intent-drive helper used by both the new motor and
the existing input-driven `Update`.

Existing `CharacterController3D.Update` becomes:

1. read Input3D and camera orientation;
2. construct intent;
3. call the shared intent-drive core.

Thus current games retain behavior while new games can bypass input coupling.

### Modes

Land v1 in slices:

1. Ground/air: acceleration, deceleration, air control, sprint, jump, crouch,
   gravity.
2. Swim: only after a spike proves a generic water-volume/mode input; the motor
   does not discover game water automatically.
3. Fly: simple unconstrained intent for debug/spectator/AI if it reuses
   Character3D safely; otherwise defer rather than corrupt grounded semantics.

## Implementation sequence

### Phase 0 — Behavior characterization

Add/confirm traces for current CharacterController3D:

- accelerate/stop on flat ground;
- jump apex/landing;
- slope/step behavior;
- crouch with clear/blocked ceiling;
- sliding and ground entity/platform ride;
- gravity changes;
- camera yaw mapping;
- frame-rate/fixed-delta consistency.

These are compatibility tests for the refactor.

### Phase 1 — Intent-drive extraction

1. Identify the smallest internal function that accepts world/controller,
   world-space desired movement, jump/crouch flags, and dt.
2. Extract it without changing existing results; keep validation, finite
   clamps, and tick order identical.
3. Run the behavior traces before adding the class.
4. Add an internal direct-intent unit test to prove device independence.

### Phase 2 — ADR and motor state

Define validated defaults and ranges for speed, sprint multiplier,
acceleration/deceleration, air control, gravity, and jump speed. Define state
edge lifetime (`JustLanded`, `JustJumped`), teleport/reset behavior, world and
controller ownership, and one-motor-per-controller policy.

Append class ID and implement lifecycle with no world/controller retain cycle.

### Phase 3 — Ground/air motor

1. Clamp/sanitize intent and parameters.
2. Convert local intent by `FacingYaw` to world-space XZ direction.
3. Compute target speed and deterministic acceleration/deceleration.
4. Apply air-control policy without overriding vertical collision response.
5. Forward crouch/jump and shared drive input.
6. Implement the public post-simulation `AfterSimulation()` refresh. It samples
   velocity/ground/crouch state after the driver commit, derives
   `JustLanded`/`JustJumped`, and rejects or no-ops a duplicate refresh for the
   same committed step as specified by the ADR. Do not report pre-physics data
   as current.
7. Clear transient intent once per committed fixed step.

### Phase 4 — Optional modes

For swim, require explicit game input such as `Mode=Swim` and a vertical intent
or buoyancy configuration. Do not raycast for water by name. Test entry/exit,
gravity restoration, and surface behavior. For fly, prove collision policy.
If either expands the API materially, split it into a follow-up ADR and ship
ground/air first.

### Phase 5 — Input/controller adapters

Add a convenience that maps Input3D plus an explicit yaw into intent only if it
does not recouple the motor. Prefer a small example-library adapter using the
existing `fps3d` Action preset. AI and probes set intent directly.

### Phase 6 — Registration and tests

Add public defs, qualified signatures, real/disabled symbols, CMake, docs,
surface audits. Tests include:

- all characterization cases unchanged for existing Update;
- direct intent with no input object;
- yaw 0/90/180 mapping;
- acceleration/deceleration curves at multiple fixed dt values;
- sprint and crouch transitions;
- one jump per request and correct edge lifetime;
- `AfterSimulation` state refresh runs once per committed step and is safe when
  queried before the first commit;
- air control bounds;
- moving platform and ground entity resolution;
- teleport/reset clears transient state;
- pause/time scale behavior;
- controller/world destroyed in either order;
- two motors on one controller rejected;
- identical input traces produce identical state traces;
- zero steady-state allocation.

### Phase 7 — Adoption spikes

1. Drive current FirstPersonController behavior through intent and compare.
2. Replace only Ridgebound player movement core, leaving stamina/swim/game
   policy outside; record missing generic seams.
3. Replace only Ashfall walking/jump/crouch core, leaving weapons/health outside.
4. Prove an AI/test fixture can drive the same motor without Input3D.

Full migrations remain plans 18–19.

## Validation

Run character/controller/third-person/traversal/physics tests, new motor
fixtures, fixed determinism protocol, graphics3d label, surface audits,
disabled graphics, platform policy, and full builds.

## Acceptance criteria

- Existing CharacterController3D traces do not change outside explicit
  tolerances.
- Direct intent moves a character without Input3D or Camera3D.
- Motor and camera can be combined independently.
- Ridgebound/Ashfall spikes retain game-specific policy outside the motor.
- Ground/platform/crouch/jump semantics remain authoritative in existing
  physics code.
- Fixed traces are deterministic and steady state allocates nothing.

## Stop conditions

Stop if the design requires a second collision solver, per-frame script
callbacks, automatic game-specific water discovery, or two writers to the same
Character3D. Narrow the motor to a shared intent-drive extension.

## Handoff evidence

Provide before/after characterization traces, shared drive call graph, motor
state-edge table, adoption spike results, allocation data, surface diff, and
ADR.
