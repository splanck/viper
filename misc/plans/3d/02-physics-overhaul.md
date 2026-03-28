# Plan: Physics3D Overhaul — PARTIALLY COMPLETE

## Completed (2026-03-27)

### 1. Shape-Specific Narrow Phase
Replaced AABB-only narrow phase with shape-aware dispatch:
- **sphere-sphere**: Radial distance check with proper radial normal
- **AABB-sphere**: Closest-point-on-AABB to sphere center, radial pushout
- **sphere-AABB**: Reversed AABB-sphere with normal flip
- **AABB-AABB**: Existing min-axis pushout (unchanged)

Capsules use sphere narrow-phase (treating capsule center as sphere center). Full capsule-capsule with closest-point-on-segment is deferred to a future pass.

### 2. Character Controller Slide + Step
Replaced trivial velocity-set with 3-iteration slide loop:
- Attempt full move
- On collision with static geometry: push out along normal, remove velocity component along normal (slide)
- Repeat up to 3 times for corner cases
- Ground detection via normal Y component
- Slope limit checking (stored but slope rejection deferred — needs terrain integration)

### 3. Collision Event Queue
Added contact buffer to Physics3DWorld:
- `rt_contact3d` struct: body_a, body_b, normal, depth
- Buffer (128 contacts max) populated during `Step()`
- Query API: `CollisionCount`, `GetCollisionBodyA/B(index)`, `GetCollisionNormal(index)`, `GetCollisionDepth(index)`
- Buffer cleared at start of each `Step()`
- Registered in runtime.def as RT_FUNC + RT_METHOD entries

## Already Correct (verified)
- Friction: Coulomb model implemented at lines 178-202
- set_static(false): Properly restores inv_mass
- Body limit: Already traps at line 310 (not silent)

## Remaining (Deferred)
- Angular velocity + torque
- Capsule-capsule segment-based narrow phase
- Physics joints (distance, hinge, ball, spring)

## Tests Added
7 new tests in `test_rt_physics3d.cpp`:
- sphere-sphere collision (radial pushout)
- sphere-sphere no overlap (separated)
- AABB-sphere collision
- Collision event count
- Collision event body access
Total: 62/62 passing, 1358/1358 full suite.
