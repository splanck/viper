//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/graphics/3d/physics/rt_body3d_kinematics_internal.h
// Purpose: Private shared prefix view used by the Body3D core and joint solvers.
//
// Key invariants:
//   - This layout is implementation-only and never part of the public C ABI.
//   - rt_physics3d.c statically verifies every field offset against rt_body3d.
//
// Ownership/Lifetime:
//   - Values are borrowed views of live Body3D payloads; this type owns nothing.
//
// Links: rt_physics3d_internal.h, rt_joints3d_internal.h, rt_physics3d.c
//
//===----------------------------------------------------------------------===//
#pragma once

/// @brief Shared read/write view of a Body3D's private kinematic prefix.
typedef struct rt_body3d_kinematics {
    void *vptr;
    double position[3];
    double orientation[4];
    double scale[3];
    double velocity[3];
    double angular_velocity[3];
    double force[3];
    double torque[3];
    double mass;
    double inv_mass;
} rt_body3d_kinematics;
