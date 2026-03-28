# Plan: Collision Callbacks + Physics Joints

## Overview
Enable event-driven game logic (not just polling) and add constraints for doors, vehicles, ragdoll.

## 1. Collision Callbacks

### API
```
Physics3DWorld.OnCollision(callback)        // Register callback function
Physics3DWorld.OnTriggerEnter(callback)     // Body entered trigger zone
Physics3DWorld.OnTriggerExit(callback)      // Body left trigger zone
```

Since Zia doesn't have function pointers, use an event queue pattern instead:
```
Physics3DWorld.GetCollisionCount() -> Integer
Physics3DWorld.GetCollisionBodyA(index) -> Body3D
Physics3DWorld.GetCollisionBodyB(index) -> Body3D
Physics3DWorld.GetCollisionNormal(index) -> Vec3
Physics3DWorld.GetCollisionDepth(index) -> Float

Physics3DWorld.GetTriggerEnterCount() -> Integer
Physics3DWorld.GetTriggerEnterBody(index) -> Body3D
Physics3DWorld.GetTriggerEnterZone(index) -> Trigger3D

Physics3DWorld.GetTriggerExitCount() -> Integer
Physics3DWorld.GetTriggerExitBody(index) -> Body3D
Physics3DWorld.GetTriggerExitZone(index) -> Trigger3D
```

### Implementation
**File:** `src/runtime/graphics/rt_physics3d.c`
- Add contact pair buffer to world: `struct { int32_t body_a, body_b; double normal[3], depth; } contacts[PH3D_MAX_CONTACTS]`
- During `step()`, populate contact buffer when collisions occur
- Track previous-frame contacts to compute enter/exit:
  - New pair = enter
  - Missing pair = exit
  - Still present = stay
- For triggers: same approach using Trigger3D zones

## 2. Physics Joints

### API
```
DistanceJoint3D.New(bodyA, bodyB, distance)
HingeJoint3D.New(bodyA, bodyB, anchor_vec3, axis_vec3)
BallJoint3D.New(bodyA, bodyB, anchor_vec3)
SpringJoint3D.New(bodyA, bodyB, restLength, stiffness, damping)
```

### Implementation
**File:** New file `src/runtime/graphics/rt_joints3d.c`
- Joint constraint solver runs after collision resolution in `step()`
- Each joint type implements a `solve(dt)` function:
  - **Distance:** Pull/push bodies to maintain target distance
  - **Hinge:** Constrain to single rotation axis via cross-product projection
  - **Ball:** Constrain to point contact (3 DOF rotation allowed)
  - **Spring:** Hooke's law force along joint axis
- Iterate constraints 4-8 times per step for stability (sequential impulse solver)

### Files Modified
- `src/runtime/graphics/rt_physics3d.c/h` — Contact buffer, trigger tracking
- New: `src/runtime/graphics/rt_joints3d.c/h` — Joint implementations
- `src/runtime/CMakeLists.txt` — Add new files
- `src/il/runtime/runtime.def` — New RT_FUNC entries for all APIs
- `src/il/runtime/classes/RuntimeClasses.hpp` — New RTCLS entries

## Verification
- Collision events: Two bodies collide → contact count > 0 → body A/B accessible
- Trigger enter/exit: Body enters zone → enter count increments → body exits → exit count increments
- Distance joint: Two bodies connected — pull one, other follows at fixed distance
- Hinge joint: Door on hinge — push opens, gravity closes
- Spring joint: Bouncy connection between bodies
