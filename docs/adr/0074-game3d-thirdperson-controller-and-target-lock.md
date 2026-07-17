---
status: active
audience: contributors
last-verified: 2026-07-11
---

# ADR 0074: Game3D Third-Person Controller And Target Lock

Date: 2026-07-10

## Status

Accepted

## Context

None of the four built-in Game3D camera controllers performs any collision
query: `FollowController` tracks an entity with damping but clips through
walls, and only `FirstPersonController` can drive a `CharacterController3D`.
Every third-person game on the platform had to hand-roll a spring-arm camera,
occluder handling, and lock-on targeting from raw sweeps — the exact genre
glue a commercially viable third-person adventure game needs from the runtime.

## Decision

Two new public classes join `Zanna.Game3D`:

- `ThirdPersonController` (`rt_game3d_thirdperson.c`, class id `-0x60304B`) —
  an over-the-shoulder camera on a collision-aware spring arm. `Update`
  consumes `Input3D.lookAxis()` into absolute yaw/pitch (yaw 0 faces -Z,
  yaw 90 faces -X; positive pitch places the camera above the pivot), advances
  a linear aim blend, and drives an optional `CharacterController3D` along the
  yaw basis through the shared `game3d_character_controller_drive` core
  (extracted from `CharacterController3D.update` so vertical-velocity state
  lives in exactly one place). `LateUpdate` sphere-sweeps the boom from the
  pivot: pull-in is instant (the camera never clips), release is damped, and
  a sweep that starts penetrating snaps to `MinDistance`. Aim mode blends
  distance and FOV (the pre-aim FOV is captured and restored). Opt-in
  occluder fading raycasts pivot→eye across **all** layers — fadeable props
  are excluded from the boom `CollisionMask` instead — and swaps occluders to
  `Material3D.MakeInstance` clones whose originals are restored on clear,
  disable, detach, and finalization.
- `TargetLock3D` (`rt_game3d_targetlock.c`, class id `-0x60304C`) — lock-on
  acquisition scored 2:1 angle-over-distance inside a camera-forward cone,
  sticky toward the current target, LoS-gated origin-to-origin via
  `raycast_all` (hits on the owner's or candidate's own bodies are
  transparent). `Cycle(±1)` orders candidates by signed camera-basis yaw.
  Maintenance auto-releases on death, `BreakDistance`, or LoS broken past
  `LosGraceSeconds`; `JustAcquired`/`JustLost` follow the `just_landed`
  one-shot polling pattern (no VM callbacks). `LockedMoveBias` bends a planar
  move vector up to 12 degrees toward the target inside a 30-degree window.

`ThirdPersonController.LockTarget` installs a lock as the framing source: the
controller ticks `TargetLock3D.Update` once per world step, ignores look input
while a target is engaged, eases yaw/pitch onto the player→target bearing, and
pulls the look point 40% toward the target.

## Consequences

Third-person games get camera, character drive, and lock-on from three calls
instead of bespoke sweep code. Candidate resolution requires
`Entity3D.attachBody`-managed actors, and layer policy remains entity-owned
(`attachBody` copies the entity layer onto the body), so targetable layers are
configured through `Entity3D.Layer` + `CandidateMask`. The boom's
instant-pull-in / damped-release asymmetry and the lock framing constants
(40% look bias, 0.5 s default LoS grace) are semantic commitments pinned by
`test_rt_game3d_thirdperson` and the `g3d_test_game3d_thirdperson_probe` Zia
probe.
