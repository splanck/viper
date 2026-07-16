---
status: active
audience: contributors
last-verified: 2026-07-11
---

# ADR 0096: Cloth3D — Verlet Chains and Patches

Date: 2026-07-11

## Status

Accepted

## Context

The back of a third-person character — cape, hair tail, tabard — is the
thing the player stares at all game, and Viper had no secondary motion at
all: no cloth, no verlet, only rigid bodies and skinned animation. Banners
and flags for world dressing had the same gap.

## Decision

- **From-scratch verlet core** (`rt_cloth3d.c`, zero dependencies): classic
  position verlet with per-substep damping and air-drag wind coupling
  (`force = windResponse * (wind - velocity)`), Jakobsen distance-constraint
  relaxation (default 4 iterations), analytic sphere/capsule pushout, then
  pin re-fix. Fixed 1/120 substep with an accumulator (max 8 substeps/step,
  spiral guard) — replay is **bit-identical**, and slicing the same
  simulated time into different frame dts produces identical states (unit-
  tested with exact equality).
- **Topologies:** `Cloth3D.NewChain(segments, totalLength)` (structural
  constraints) and `NewPatch(w, h, width, height)` (structural + shear, dims
  capped at 64). Fluent `Pin`, `AddSphere`, `AddCapsule`, `SetWind`;
  `GetPoint` for inspection; `Damping`/`Iterations`/`GravityScale`/
  `WindResponse` knobs (tuning order documented: iterations → substep →
  damping).
- **Bone-chain binding** (`BindBoneChain(controller, rootBone)`): walks the
  linear child chain (branching traps), reseeds rest lengths from bind-pose
  locals, pins point 0 to the root bone's animated model-space pose each
  step, and writes simulated directions back as from-to aim rotations
  (positions preserved — length-safe) through the same masked
  `apply_pose_override` slot ragdolls use, so cloth composes last in the
  IK → ragdoll → facial → cloth override order. Chains simulate in **model
  space**, which makes a static torso capsule exactly right for capes.
  Anchor jumps beyond half the rest length rigid-translate the whole cloth
  (pos + prev), so teleports and the first bind never manufacture phantom
  verlet velocities — without this the constraint snap flings the chain
  into a stable inverted equilibrium (found by the probe).
- **Mesh binding** (`BindMesh(mesh)`, patches): builds the grid mesh once,
  then rewrites vertex positions + central-difference normals in place per
  step and bumps `rt_mesh3d_touch_geometry` (Water3D pattern).
- **World tick:** `World3D.AddCloth/RemoveCloth`; registered cloths step
  after the facial tick so every override producer upstream has settled.

## Consequences

- Deferred (recorded): body-tracked colliders (model-space chains make
  static shapes sufficient for the cape case), plan-16 world wind registry
  (local `SetWind` only), per-point normal-scaled flag billow, self-
  collision/tearing, `Entity3D.AttachCloth` sugar, and floating-origin
  rebase shifts (both binding spaces are local, so rebase is a no-op in v1).
- Tests: `test_rt_cloth3d` (settle within 1% of rest length, 500-step
  bit-identical replay, 1/60-vs-1/240 slicing invariance, sphere pushout,
  immovable pins + wind billow) and `g3d_test_game3d_cloth_probe` (hanging
  chain, pinned banner billowing along the wind, cape anchored to an
  animated bone and hanging below it). VM == native.

## Links

- misc/plans/thirdpersonupgrade/27-cloth.md
- src/runtime/graphics/3d/physics/rt_cloth3d.c
- ADR 0077 (ragdoll override slot), ADR 0095 (audio immersion)
