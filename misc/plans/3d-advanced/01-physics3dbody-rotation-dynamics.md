# Plan 01: Physics3DBody Rotation and Rigid-Body Dynamics

## Goal

Turn `Viper.Graphics3D.Physics3DBody` into a real rigid-body API instead of a linear-only body with shape-specific constructors.

This is the most urgent gameplay-facing physics gap. Without it, doors, vehicles, ragdolls, tumbling debris, physically-correct pins, and root-motion-aware character bodies all require hacks.

## Verified Current State

- `rt_physics3d.h` exposes position, linear velocity, force, impulse, friction, restitution, collision masks, static/trigger flags, and grounded state.
- The public API does not expose orientation, angular velocity, torque, angular impulse, inertia, sleep state, kinematic mode, or CCD.
- `3dbowling` still fakes pin rotation in game code because `Physics3DBody` has no rotational state.

## Status of Older Plans

`misc/plans/3d/14-angular-velocity-joints.md` was only partially realized:

- landed: world joint plumbing, `DistanceJoint3D`, `SpringJoint3D`
- missing: angular velocity, orientation, torque, damping, hinge/ball constraints

This plan replaces the rotational half of that older plan and intentionally keeps the public type as `Physics3DBody` for compatibility.

## API Shape

Extend the existing class rather than introducing a parallel `RigidBody3D` type.

### New properties

- `Orientation -> Quat`
- `AngularVelocity -> Vec3`
- `AngularDamping -> f64`
- `LinearDamping -> f64`
- `IsKinematic -> i1`
- `IsSleeping -> i1`
- `CanSleep -> i1`
- `UseCCD -> i1`

### New methods

- `SetOrientation(quat)`
- `SetAngularVelocity(wx, wy, wz)`
- `ApplyTorque(tx, ty, tz)`
- `ApplyAngularImpulse(ix, iy, iz)`
- `Wake()`
- `Sleep()`
- `SetKinematic(i1)`
- `SetUseCCD(i1)`

### Compatibility requirements

- existing `NewAABB`, `NewSphere`, and `NewCapsule` constructors stay valid
- existing position/velocity APIs keep their meaning
- old callers that ignore rotation remain source-compatible

## Internal Design

Add rotational state to the body core:

- quaternion orientation
- angular velocity
- accumulated torque
- inverse inertia in local space
- damping and sleep counters
- body mode enum: dynamic, static, kinematic
- CCD flag and cached swept bounds

Important design constraint:

- do not bake box-orientation assumptions directly into the current AABB narrow phase

The correct layering is:

1. body owns mass, transform, velocity, sleep, and mode
2. collider owns shape data and local offset/orientation
3. world step integrates the body core and asks colliders for world-space bounds / contacts

That means this plan should land in a way that is ready for Plan 02, even if `NewAABB` remains an axis-aligned compatibility wrapper for the first phase.

## Implementation Phases

### Phase 1: body-core rotation support

Files:

- `src/runtime/graphics/rt_physics3d.h`
- `src/runtime/graphics/rt_physics3d.c`
- `src/il/runtime/runtime.def`
- `src/runtime/graphics/rt_graphics_stubs.c`

Work:

- add quaternion orientation and angular velocity to the body struct
- integrate torque to angular velocity and angular velocity to orientation
- expose public getters/setters and impulse helpers
- add damping, sleep, and wake semantics
- preserve trigger/static behavior

### Phase 2: rigid-body modes

Work:

- separate static, dynamic, and kinematic behavior explicitly
- define how kinematic bodies interact with dynamic ones
- make `SetPosition`/`SetOrientation` on kinematic bodies authoritative without breaking dynamic bodies

### Phase 3: CCD hook points

Work:

- add `UseCCD`
- start with sphere/capsule swept tests and conservative advancement
- expose a fail-safe behavior for unsupported collider types instead of silently pretending CCD exists

### Phase 4: inertia quality and collider integration

Work:

- move inertia computation behind collider-derived mass properties
- let compound colliders override body inertia once Plan 02 lands

## Testing

### Runtime tests

- torque changes angular velocity for dynamic bodies
- quaternion orientation changes over repeated `Step`
- sleeping bodies stop integrating until `Wake`
- static bodies ignore force and torque
- kinematic bodies do not accumulate external impulses

### Regression tests

- `3dbowling`-style pin body can rotate without external tilt bookkeeping
- body orientation round-trips through public API
- graphics-disabled stub exports exist for all new entry points

### Test files to add or expand

- `src/tests/runtime/RTPhysics3DTests.cpp`
- `src/tests/runtime/RTGraphicsSurfaceLinkTests.cpp`
- `examples/apiaudit/graphics3d/`

## Documentation

- update the `Physics3DBody` section in `docs/graphics3d-guide.md`
- document body modes and sleep/CCD semantics clearly
- add one example showing dynamic body rotation and one showing a kinematic platform

## Risks and Non-Goals

Risks:

- rotational state without collider separation can accidentally produce visually-rotating but collision-static boxes
- CCD can become a footgun if exposed before it actually changes collision behavior

Non-goals for this plan:

- hinge and ball joints
- collider authoring
- structured contact events

Those belong to Plans `02` and `04`.
