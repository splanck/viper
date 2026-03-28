# Plan: Angular Velocity + Physics Joints

## Overview
Add rotational dynamics and constraint-based joints to Physics3D. These enable doors, ragdoll, vehicles, swinging platforms, and any physics object that spins or pivots.

## Part 1: Angular Velocity + Torque

### Body3D Struct Changes
**File:** `src/runtime/graphics/rt_physics3d.c`

Add to `rt_body3d`:
```c
double angular_vel[3];    // Angular velocity (radians/sec per axis)
double torque[3];         // Accumulated torque (zeroed per step)
double orientation[4];    // Quaternion (w, x, y, z)
double inv_inertia[3];    // Diagonal inverse moment of inertia
```

### Moment of Inertia (per shape)
Computed once at body creation in `make_body()`:
- **AABB** (box): `I_xx = m/12 * (h² + d²)`, `I_yy = m/12 * (w² + d²)`, `I_zz = m/12 * (w² + h²)` where w/h/d = `2 * half_extents[0/1/2]`
- **Sphere**: `I = 2/5 * m * r²` (uniform on all axes)
- **Capsule**: Approximate as cylinder: `I_xx = I_zz = m/12 * (3r² + h²)`, `I_yy = m/2 * r²`
- Store as `inv_inertia[i] = 1.0 / I[i]` (0 for static bodies)

### Integration in `rt_world3d_step()`
After existing linear integration, add:
```c
// Angular: torque → angular velocity → orientation
b->angular_vel[0] += b->torque[0] * b->inv_inertia[0] * dt;
b->angular_vel[1] += b->torque[1] * b->inv_inertia[1] * dt;
b->angular_vel[2] += b->torque[2] * b->inv_inertia[2] * dt;

// Angular velocity → quaternion delta
// dq = 0.5 * omega * q * dt
double wx = b->angular_vel[0] * dt * 0.5;
double wy = b->angular_vel[1] * dt * 0.5;
double wz = b->angular_vel[2] * dt * 0.5;
double qw = b->orientation[0], qx = b->orientation[1];
double qy = b->orientation[2], qz = b->orientation[3];
b->orientation[0] += (-wx*qx - wy*qy - wz*qz);
b->orientation[1] += ( wx*qw + wz*qy - wy*qz);
b->orientation[2] += ( wy*qw - wz*qx + wx*qz);
b->orientation[3] += ( wz*qw + wy*qx - wx*qy);
// Renormalize quaternion
double len = sqrt(b->orientation[0]*b->orientation[0] + ...);
for (int i = 0; i < 4; i++) b->orientation[i] /= len;

b->torque[0] = b->torque[1] = b->torque[2] = 0;
```

### Collision Response Update
In `resolve_collision()`, angular impulse from contact:
```c
// Contact point relative to body center
double r_a[3] = { contact_point[i] - a->position[i] };
double r_b[3] = { contact_point[i] - b->position[i] };

// Angular impulse: torque = r × (j * n)
a->angular_vel[0] -= inv_inertia_a[0] * (r_a[1]*jn[2] - r_a[2]*jn[1]);
// ... (cross product for all 3 axes, both bodies)
```

Note: Contact point is approximated as midpoint of penetration along normal.

### New API
```
Body3D.ApplyTorque(tx, ty, tz)       // Accumulate torque
Body3D.SetAngularVelocity(wx, wy, wz)
Body3D.AngularVelocity -> Vec3       // Property
Body3D.Orientation -> Quat           // Property
Body3D.SetOrientation(quat)
Body3D.SetAngularDamping(factor)     // 0-1, reduces spin over time
```

### Files Modified
- `src/runtime/graphics/rt_physics3d.c` — Struct fields, integration, collision angular impulse
- `src/runtime/graphics/rt_physics3d.h` — New function declarations
- `src/il/runtime/runtime.def` — RT_FUNC entries + RT_CLASS updates

---

## Part 2: Physics Joints

### New File: `src/runtime/graphics/rt_joints3d.c/h`

### Joint Types

**DistanceJoint** — Maintains fixed distance between two body anchor points.
```c
struct rt_distance_joint3d {
    rt_body3d *body_a, *body_b;
    double anchor_a[3], anchor_b[3]; // Local-space anchor offsets
    double target_distance;
    double stiffness; // 0=rigid, 1=soft
};
```
Solver: compute current distance between world-space anchors, apply correction impulse along the connecting axis.

**HingeJoint** — Constrains rotation to a single axis (doors, wheels).
```c
struct rt_hinge_joint3d {
    rt_body3d *body_a, *body_b;
    double anchor[3];     // World-space pivot point
    double axis[3];       // Rotation axis (unit vector)
    double angle_min, angle_max; // Optional angle limits
};
```
Solver: project relative position onto axis perpendicular plane, apply positional correction. For angular limits, clamp relative rotation angle.

**BallJoint** — Constrains to a shared point (shoulder, hip for ragdoll).
```c
struct rt_ball_joint3d {
    rt_body3d *body_a, *body_b;
    double anchor_a[3], anchor_b[3]; // Local-space anchor offsets
};
```
Solver: compute world-space anchor positions, apply impulse to make them coincide.

**SpringJoint** — Hooke's law force between two points.
```c
struct rt_spring_joint3d {
    rt_body3d *body_a, *body_b;
    double anchor_a[3], anchor_b[3];
    double rest_length;
    double stiffness;
    double damping;
};
```
Solver: `force = -stiffness * (dist - rest_length) - damping * relative_velocity_along_axis`

### World Integration
Add joint array to `rt_world3d`:
```c
#define PH3D_MAX_JOINTS 128
void *joints[PH3D_MAX_JOINTS];
int32_t joint_types[PH3D_MAX_JOINTS]; // 0=distance, 1=hinge, 2=ball, 3=spring
int32_t joint_count;
```

In `rt_world3d_step()`, after collision resolution:
```c
// Phase 3: Joint constraint solving (4-8 iterations for stability)
for (int iter = 0; iter < 6; iter++) {
    for (int j = 0; j < w->joint_count; j++) {
        solve_joint(w->joints[j], w->joint_types[j], dt);
    }
}
```

### API
```
DistanceJoint3D.New(bodyA, bodyB, distance)
HingeJoint3D.New(bodyA, bodyB, anchor_vec3, axis_vec3)
BallJoint3D.New(bodyA, bodyB, anchor_vec3)
SpringJoint3D.New(bodyA, bodyB, restLength, stiffness, damping)

Physics3DWorld.AddJoint(joint)
Physics3DWorld.RemoveJoint(joint)
Physics3DWorld.JointCount -> Integer
```

### Files Modified
- New: `src/runtime/graphics/rt_joints3d.c/h`
- `src/runtime/graphics/rt_physics3d.c/h` — Joint array in world, solve loop
- `src/runtime/CMakeLists.txt` — New source files
- `src/il/runtime/runtime.def` — RT_FUNC + RT_CLASS blocks for 4 joint types
- `src/il/runtime/classes/RuntimeClasses.hpp` — RTCLS entries for joint types

---

## Verification

### Angular Velocity Tests
- Apply torque to box → verify angular velocity increases
- Apply torque then step multiple frames → verify orientation changes (quaternion != identity)
- Apply angular damping → verify spin decays over time
- Static body → torque has no effect

### Joint Tests
- Distance joint: pull body A away → body B follows at fixed distance
- Hinge joint: bodies connected at pivot, only rotation around axis allowed
- Ball joint: bodies share anchor point, free rotation
- Spring joint: displaced body oscillates around rest length

### Integration Tests
- Stack of boxes: should settle under gravity without exploding
- Ragdoll: chain of ball joints, collapses under gravity
- Door: hinge joint with angle limits, push opens, gravity closes
