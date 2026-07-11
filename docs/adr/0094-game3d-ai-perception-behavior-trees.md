# ADR 0094: NPC AI — Perception3D and BehaviorTree3D

Date: 2026-07-11

## Status

Accepted

## Context

Enemy and NPC logic in the demos is hand-rolled per game: ad-hoc distance
checks for "can the guard see me", wall-clock timers, and if/else ladders
for chase/patrol. The third-person plan needs stealth-grade senses (view
cones with line-of-sight and reaction hysteresis, sound stimuli) and a
reusable decision structure, both deterministic across VM and native.

## Decision

- **`Perception3D.New(entity)`** installs on the entity component slot.
  `SetSight(range, fovDegrees, eyeHeight)` + `SetHearing(range)` configure
  senses; `SetTargetMask`/`SetLosMask` filter candidates by entity layer and
  raycast mask. Sight runs in the world-step AI pass (before controllers):
  world-entity-list scan order, cone test from the owner's world-quaternion
  forward, then an LoS raycast that blocks unless the hit body is the target
  itself. Detection is hysteretic — 0.3 s time-to-see, 2.0 s time-to-lose —
  so single-frame glimpses never flip state. Polled surface: `SeenCount`,
  `SeenTarget(i)`, `LastKnownPosition(target)`, `SeenChanged()` one-shot.
- **`World3D.ReportSound(position, loudness, tag)`** delivers a heard event
  to every perceiver with `distance <= hearingRange * loudness`. Heard
  events live until the next world step (`HeardCount`/`HeardPosition`/
  `HeardTag`), so scripts poll them the frame they fire.
- **`BehaviorTree3D`** is a shared immutable node arena (128 nodes, 8
  children each): `Sequence`/`Selector`/`Inverter` composites plus
  `TargetVisible`, `Wait(seconds)`, `MoveToTarget(speed, arrive)`,
  `MoveToLastKnown(speed, arrive)`, and `Custom(id)` leaves; builders return
  node handles wired with `AddChild`/`SetRoot`. One tree serves any number
  of entities.
- **`BehaviorTreeInstance3D.New(entity, tree)`** holds all mutable state
  (composite resume points, wait timers, custom results) per entity.
  `Custom` leaves park the tree (RUNNING) and expose `PendingCustom`; the
  script acts and calls `Resolve(success)` — no VM callbacks from the
  runtime, same polled pattern as Dialogue3D choices. Movement leaves drive
  the entity kinematically via its transform.

## Consequences

- Deterministic by construction: no wall clock, no RNG, world-list scan
  order, fixed-step hysteresis thresholds.
- Deferred (recorded): navmesh pathing (movement leaves go straight-line;
  terrain/obstacle avoidance composes later behind the same leaves),
  blackboard key/value store (SetTarget covers the v1 contract), parallel
  composite, and per-track sound accumulation (heard events are
  step-scoped).
- Test: `g3d_test_game3d_ai_probe` — time-to-see gate, seen one-shot
  read-reset, last-known tracking, MoveToTarget chase closes an 8 m gap,
  out-of-cone sound heard with tag and expires next step, Custom leaf
  parks/resolves/re-parks. VM == native.

## Links

- misc/plans/thirdpersonupgrade/22-ai-perception-behavior.md
- src/runtime/graphics/3d/rt_game3d_ai.c
- ADR 0093 (interaction), ADR 0092 (footsteps/surface events)
