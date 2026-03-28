# Plan: Collision Callbacks + Physics Joints — COMPLETE

## 1. Collision Callbacks — DONE (Plan 02)
Event queue with `CollisionCount`, `GetCollisionBodyA/B`, `GetCollisionNormal`, `GetCollisionDepth`.

## 2. Physics Joints — DONE (2026-03-28)

### DistanceJoint3D
- Positional correction: pushes bodies to maintain target distance
- Velocity correction: removes relative velocity along constraint axis
- Handles coincident centers (zero-distance edge case)
- Properties: `Distance` (read/write)

### SpringJoint3D
- Hooke's law: `F = -stiffness * (dist - rest)`
- Damping: `F_damp = -damping * rel_velocity_along_axis`
- Forces applied as velocity delta (F * inv_mass * dt)
- Properties: `Stiffness`, `Damping` (read/write), `RestLength` (read-only)

### World Integration
- Joint array (128 max) in `rt_world3d` struct
- 6 solver iterations per physics step (after collision resolution)
- `AddJoint(joint, type)`, `RemoveJoint(joint)`, `JointCount` on Physics3DWorld

### Files Created/Modified
- New: `src/runtime/graphics/rt_joints3d.c/h` — DistanceJoint3D + SpringJoint3D
- `src/runtime/graphics/rt_physics3d.c/h` — Joint array, solve loop, add/remove/count
- `src/runtime/CMakeLists.txt` — New source file
- `src/il/runtime/runtime.def` — 12 RT_FUNC entries + 2 class blocks
- `src/il/runtime/classes/RuntimeClasses.hpp` — 2 RTCLS entries
- `src/il/runtime/RuntimeSignatures.cpp` — Include

### Remaining (Plan 14)
- HingeJoint3D and BallJoint3D require angular velocity (not yet implemented)

### Tests Added
5 new tests in `test_rt_physics3d.cpp`:
- Distance joint creation + property access
- Distance joint constraint (bodies converge to target distance over 60 steps)
- Spring joint creation + property access
- Spring joint force (body pulled toward rest length)
- World joint management (add/remove/count)

Total: 73/73 physics, 1358/1358 full suite.
