# ADR 0075: Character3D Dynamic-Body Interaction, Crouch, And Moving Platforms

Date: 2026-07-10

## Status

Accepted

## Context

`Character3D` filtered dynamic bodies out of collision entirely — the
controller walked *through* crates and props (no blocking and no pushing), a
correctness bug for any gameplay that mixes a character with dynamic objects.
The capsule was also fixed at construction (no crouch), the controller did not
ride moving platforms, and the ground body found during probing was discarded
rather than exposed.

## Decision

- **Dynamic bodies block and push (default flip).** `character3d_candidate_body`
  now accepts dynamic bodies. When a swept contact resolves against a dynamic
  body, one impulse per body per move is applied at the approximate contact
  point along the contact normal: `PushStrength × min(1, ctrlMass/otherMass) ×
  approachSpeed`. Light props yield within a few steps; heavy props wall the
  player. `PushStrength = 0` blocks without pushing, and
  `CollideDynamic = false` restores the legacy ghost-through for compatibility.
  Impulses are never applied inside sweep bisection, so bodies cannot gain
  energy from the search iterations.
- **Crouch/stand resize.** `rt_collider3d_reset_capsule_raw` (twin of the
  box/sphere resets) updates capsule dims and cached bounds.
  `Character3D.TrySetHeight(h)`: shrinking always succeeds with the feet
  planted (the center drops by half the delta); growing probes the stand pose
  (lifted 1 cm so resting ground contact is not a false blocker) and returns
  false when blocked — `TryStand` semantics come free. The bounds revision is
  bumped so the broadphase re-inserts. The Game3D wrapper adds `CrouchHeight`
  plus `SetCrouching(bool)` sugar.
- **Moving platforms.** The ground body found by probing is retained and
  exposed via `GetGroundBody`. With `RidePlatforms` (default on), a controller
  grounded on a kinematic/static body with velocity pre-displaces by the
  platform's step displacement (linear plus yaw about the platform origin)
  *before* the swept move, so a wall on the platform still blocks. Platform
  ride is yaw-only in v1.
- **Steep-slope slide.** Resting against a surface steeper than the slope
  limit reports `IsSliding()` instead of grounding; gravity keeps projecting
  onto the slope plane through the existing contact-plane slide.

## Consequences

The dynamic-body default flip changes behavior for scenes that mixed
characters and dynamics — previously the character ghosted through them.
Existing demos were audited via the full graphics3d suite (115/115 green);
scenes that relied on ghosting can set `CollideDynamic = false`. The push
formula and platform pre-displacement are pinned by deterministic replay
tests in `test_rt_game3d_thirdperson`.
