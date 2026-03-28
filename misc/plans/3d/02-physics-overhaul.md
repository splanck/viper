# Plan: Physics3D Overhaul

## Overview
After code verification: friction IS implemented, set_static IS correct. The real gaps are narrow-phase collision, character controller, angular dynamics, joints, and callbacks.

## Confirmed Issues

### 1. Sphere-Sphere Narrow Phase (CONFIRMED)
**File:** `src/runtime/graphics/rt_physics3d.c:130-168`
**Verified:** Comment at line 140 says "Narrow phase: use AABB penetration for all shapes (simplified)". No sphere-specific distance check exists.
**Fix:** Add shape-aware dispatch in `test_collision()`:
- Sphere-sphere: `dist(center_a, center_b) < radius_a + radius_b`, normal = normalized(center_b - center_a)
- Capsule-capsule: closest-point-on-segment to closest-point-on-segment, then radial test
- AABB-sphere: closest point on AABB surface to sphere center
- Keep AABB-AABB for box shapes
**Technical detail:** The broad-phase AABB check (lines 130-138) stays as-is for early rejection. Only narrow-phase changes.

### 2. Character Controller (CONFIRMED)
**File:** `src/runtime/graphics/rt_physics3d.c:596-613`
**Verified:** Creates capsule body with `step_height = 0.3` and `slope_limit` fields but `rt_character3d_move()` is a basic velocity-set.
**Fix:** Implement proper slide-and-step in `rt_character3d_move()`:
```
1. Try full movement (position + velocity * dt)
2. If collision: project remaining velocity onto collision plane (v - dot(v,n)*n)
3. Repeat up to 3 iterations (slide along walls)
4. Step-up: if horizontal collision and step_height > 0:
   a. Move up by step_height
   b. Try horizontal move
   c. Move down to find ground
   d. Accept if ground found within step_height
5. Slope limit: reject movement up surfaces where dot(normal, up) < slope_limit_cos
```

### 3. Silent Body Limit (CONFIRMED)
**File:** `src/runtime/graphics/rt_physics3d.c:308` (PH3D_MAX_BODIES = 256)
**Fix:** Return 0 from `rt_physics3d_world_add()` when full. Document in API docs.

## ~~Friction~~ — ALREADY IMPLEMENTED
**Verified:** Lines 192-218 contain full Coulomb friction with tangential impulse capping. No fix needed.

## ~~set_static(false)~~ — ALREADY CORRECT
**Verified:** Lines 540-551 properly restore `inv_mass = 1.0/mass` when unsetting static. No fix needed.

## New Features Needed

### 4. Angular Velocity + Torque
Add to body struct:
```c
double angular_vel[3];
double torque[3];
double inertia_inv[3]; // Diagonal moment of inertia inverse (per-axis)
```
Integration:
```c
// In step():
angular_vel[i] += torque[i] * inertia_inv[i] * dt;
// Convert angular velocity to quaternion delta and apply to orientation
// Zero torque after integration (like forces)
```
Moment of inertia per shape:
- Box: `I_xx = m/12 * (h² + d²)`, `I_yy = m/12 * (w² + d²)`, `I_zz = m/12 * (w² + h²)`
- Sphere: `I = 2/5 * m * r²` (all axes)
- Capsule: approximate as cylinder + hemisphere caps

### 5. Collision Callbacks (Event Queue)
Since Zia lacks function pointers, use a query-based event queue:
```
Physics3DWorld.GetCollisionCount() -> Integer
Physics3DWorld.GetCollisionBodyA(index) -> Body3D
Physics3DWorld.GetCollisionBodyB(index) -> Body3D
Physics3DWorld.GetCollisionNormal(index) -> Vec3
```
Implementation: buffer contact pairs during `step()`, expose via getter. Track previous frame for enter/exit detection.

### 6. Physics Joints
New file `src/runtime/graphics/rt_joints3d.c/h`:
- Distance, hinge, ball-socket, spring constraints
- Sequential impulse solver (4-8 iterations per step)
- Integrated into `step()` after collision resolution

## Files Modified
- `src/runtime/graphics/rt_physics3d.c/h` — Narrow-phase, character controller, angular velocity, event queue
- New: `src/runtime/graphics/rt_joints3d.c/h` — Joint constraints
- `src/runtime/CMakeLists.txt` — New files
- `src/il/runtime/runtime.def` — New RT_FUNC entries
- `src/tests/unit/test_rt_physics3d.cpp` — Tests for all new features

## Verification
- Sphere-sphere: two spheres approaching should produce radial bounce (not axis-aligned)
- Character controller: walk into wall → slide along it, walk up ramp → climb if < slope limit
- Angular velocity: torque on box → visible rotation
- Joints: two bodies connected by distance joint → maintain distance when pulled apart
