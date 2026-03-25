# Feature 5: 3D Physics — Rotation Dynamics + Constraints

## Current Reality

`Physics3DWorld`, `Physics3DBody`, `Character3D`, and `Trigger3D` already exist.
`Physics3DBody` currently exposes:
- position
- velocity
- force / impulse
- mass, friction, restitution
- collision layers/masks

It does not expose orientation, angular velocity, torque, or joints.

## Problem

That limits 3D gameplay to mostly translational motion:
- no spinning rigid bodies
- no hinged doors
- no pendulums or chains
- no physically rotated props

## Corrected Scope

### Extend Physics3DBody

```text
body.AngularVelocity -> Vec3
body.Rotation -> Quat

body.SetAngularVelocity(x, y, z)
body.ApplyTorque(x, y, z)
body.ApplyTorqueImpulse(x, y, z)
body.SetRotation(quat)
body.SetAngularDamping(value)
body.SetInertia(ix, iy, iz)
```

### New Joint Class

`Viper.Graphics3D.Physics3DJoint`

```text
Physics3DJoint.NewBallSocket(bodyA, bodyB, anchor) -> Physics3DJoint
Physics3DJoint.NewHinge(bodyA, bodyB, anchor, axis) -> Physics3DJoint
Physics3DJoint.NewDistance(bodyA, bodyB, length) -> Physics3DJoint

joint.SetLimits(minAngle, maxAngle)
joint.SetMotor(targetVel, maxTorque)
joint.Break()
joint.IsActive -> Boolean
```

v1 should stop there. Springs and ragdoll-grade constraints can be phase 2.

## Implementation

### Phase 1: Angular state + public orientation API (3-4 days)

- Extend `rt_body3d` in `rt_physics3d.c/.h`
- Add:
  - angular velocity
  - torque accumulator
  - angular damping
  - inertia tensor
  - quaternion orientation
- Expose `Rotation` as a `Quat`-compatible runtime object
- Update integration to apply angular dynamics

This public API is required. Without it, adding hidden angular state would not be usable
from game or rendering code.

### Phase 2: world-owned joints (3-5 days)

- Add `src/runtime/graphics/rt_physics3d_joints.c` + `rt_physics3d_joints.h`
- Store joints on the world, not as detached standalone objects
- Step them during `Physics3DWorld.Step(dt)`
- Start with:
  - ball-socket
  - hinge
  - distance

### Phase 3: validation and demo integration (2-3 days)

- Add tests for angular integration and constraint stability
- Add at least one example scene:
  - hinged door
  - pendulum
  - spinning crate

## Viper-Specific Notes

- Use existing `Quat` / quaternion conventions from the 3D runtime
- Match existing `Vec3`-returning APIs used by `Physics3DBody.Position` and `Velocity`
- Build integration belongs in `src/runtime/CMakeLists.txt`
- The world needs a joint container and teardown path; a standalone file without world integration is incomplete

## Runtime Registration

- Extend `Viper.Graphics3D.Physics3DBody`
- Add `Viper.Graphics3D.Physics3DJoint`
- Add `Physics3DWorld.AddJoint/RemoveJoint` if needed for ownership clarity

## Files

| File | Action |
|------|--------|
| `src/runtime/graphics/rt_physics3d.c` | Modify |
| `src/runtime/graphics/rt_physics3d.h` | Modify |
| `src/runtime/graphics/rt_physics3d_joints.c` | New |
| `src/runtime/graphics/rt_physics3d_joints.h` | New |
| `src/il/runtime/runtime.def` | Extend body API, add joints |
| `src/tests/runtime/RTPhysics3DTests.cpp` | Extend / add |

## Documentation Updates

- Update `docs/viperlib/game/physics.md`
- Update `docs/graphics3d-guide.md`
- Update `docs/viperlib/README.md`

## Cross-Platform Requirements

- Pure computation
- No backend-specific code
- Use the existing 3D math/runtime conventions
