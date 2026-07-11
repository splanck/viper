# ADR 0076: Physics3D Traversal Probes

Date: 2026-07-10

## Status

Accepted

## Context

Climb/mantle/vault gameplay had no first-class query: games hand-rolled
raycast forests to find ledges and validate landings. The runtime already has
all the building blocks — capsule/sphere sweeps with fraction/penetration
flags and a temporarily-placed narrow-phase test pattern in the character
controller — but no composed API returning structured traversal results.

## Decision

`Physics3DWorld` gains three composed probes (`rt_physics3d_probes.c`), plus
the result class `Viper.Graphics3D.LedgeHit3D` (class id `-0x60304D`, a plain
snapshot handle following the `PhysicsHit3D` pattern):

- `ProbeClearance(position, radius, height, mask)` — capsule overlap test at
  an explicit pose using a per-call scratch capsule body run through the
  standard narrow-phase against masked world bodies (never registered in the
  world; thread-clean by construction).
- `ProbeLedge(origin, forward, radius, maxHeight, maxDepth, mask)` — wall
  capsule sweep, then a sphere down-sweep onto the candidate top (normals
  with `y < 0.6` are rejected as overhangs), then standing-room clearance for
  a capsule of the probe radius and `maxHeight`. Returns grab point (wall
  contact XZ at top height), surface/wall normals, rise, and the
  standing-room flag — a hang-only ledge is still a valid result.
- `ProbeVault(origin, forward, radius, maxHeight, maxThickness, mask)` — the
  ledge steps plus a far-side down-sweep at `maxThickness + radius` beyond the
  wall contact, requiring walkable ground between `+0.3` above and `-2.0`
  below origin level (rejects thick walls whose top is the only "landing").

Probe origins are **foot-level** points; the wall-sweep capsule bottom is
lifted by `radius + skin` so standing on the ground never reads as an initial
penetration. `CharacterController3D.ProbeLedge/ProbeVault` sugar defaults the
origin (capsule center minus half height), facing (entity node world rotation
applied to -Z), and radius from the bound character.

## Consequences

Traversal gameplay pairs the probes with existing root-motion animation:
probe on input, validate `HasStandingRoom`/`LandingPoint`, then play a mantle
clip or teleport. Results are world-space snapshots — callers re-probe after
movement. Probes cost 2–3 sweeps per call and are documented as event-driven,
not per-frame. Tuning constants (walkable normal 0.6, skin 0.02, vault landing
window) live in `rt_physics3d_probes.c` and are asserted by ordering-style
tests so goldens stay stable.
