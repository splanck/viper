//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/anim/rt_iksolver3d.c
// Purpose: IKSolver3D — two-bone, look-at, and FABRIK inverse-kinematics pose
//   constraints applied over a Skeleton3D's local/global bone matrices.
//
// Key invariants:
//   - Chains have at most RT_IK_SOLVER3D_MAX_CHAIN (32) bones and must form a
//     strict parent -> child path (validated by ik3d_chain_is_parented).
//   - FABRIK runs at most RT_IK_SOLVER3D_FABRIK_ITERS (12) iterations or until
//     the end effector is within 1e-4 of the target.
//   - Bone transforms are row-major 4x4 float matrices stored 16-per-bone;
//     globals are rebuilt from locals after every positional edit.
//   - `weight` in [0,1] blends the solved pose against the input pose; the
//     solver retains its Skeleton3D and freezes it (skeleton->frozen = 1).
//
// Ownership/Lifetime:
//   - IKSolver3D is GC-managed; it owns two heap pose buffers (solved_locals /
//     solved_globals) and a retained Skeleton3D, all released by the finalizer.
//
// Links: rt_iksolver3d.h, rt_skeleton3d_internal.h (bone/bind-pose layout)
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_iksolver3d.h"

#include "rt_box.h"
#include "rt_graphics3d_ids.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_skeleton3d_internal.h"
#include "rt_trap.h"
#include "rt_vec3.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RT_IK_SOLVER3D_MAX_CHAIN 32
#define RT_IK_SOLVER3D_FABRIK_ITERS 12

/// @brief Which IK algorithm a solver runs (two-bone analytic, look-at, FABRIK).
typedef enum {
    RT_IK_SOLVER3D_TWO_BONE = 1,
    RT_IK_SOLVER3D_LOOK_AT = 2,
    RT_IK_SOLVER3D_FABRIK = 3,
} rt_ik_solver3d_kind;

/// @brief IKSolver3D state: the retained skeleton, the bone chain, the solve
///        target/pole/ground-normal goals, the blend weight, and the owned
///        local/global pose buffers the solve writes into.
typedef struct {
    void *vptr;
    rt_skeleton3d *skeleton;
    rt_ik_solver3d_kind kind;
    int32_t chain_count;
    int32_t chain[RT_IK_SOLVER3D_MAX_CHAIN];
    float target[3];
    float pole[3];
    int8_t has_pole;
    float ground_normal[3];
    int8_t has_ground_normal;
    float weight;
    float *solved_locals;
    float *solved_globals;
} rt_ik_solver3d;

/// @brief Validate @p obj as an IKSolver3D handle and return its typed pointer (NULL on mismatch).
static rt_ik_solver3d *ik_solver3d_checked(void *obj) {
    return (rt_ik_solver3d *)rt_g3d_checked_or_null(obj, RT_G3D_IKSOLVER3D_CLASS_ID);
}

/// @brief Validate @p obj as a Skeleton3D handle and return its typed pointer (NULL on mismatch).
static rt_skeleton3d *ik_solver3d_skeleton_checked(void *obj) {
    return (rt_skeleton3d *)rt_g3d_checked_or_null(obj, RT_G3D_SKELETON3D_CLASS_ID);
}

/// @brief Release a GC reference held in @p *slot if this is its last drop, then NULL it.
static void ik_solver3d_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

/// @brief Narrow a double to float, returning @p fallback for NaN/inf and saturating at ±FLT_MAX.
static float ik3d_finite_float(double value, float fallback) {
    if (!isfinite(value))
        return fallback;
    if (value > (double)FLT_MAX)
        return FLT_MAX;
    if (value < -(double)FLT_MAX)
        return -FLT_MAX;
    return (float)value;
}

/// @brief Clamp a value to the [0, 1] float range, mapping NaN/inf to 0.
static float ik3d_clamp01(double value) {
    if (!isfinite(value))
        return 0.0f;
    if (value <= 0.0)
        return 0.0f;
    if (value >= 1.0)
        return 1.0f;
    return (float)value;
}

/// @brief Dot product of two 3-vectors.
static float ik3d_dot3(const float *a, const float *b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/// @brief Cross product out = a x b for 3-vectors.
static void ik3d_cross3(const float *a, const float *b, float *out) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

/// @brief Euclidean length of a 3-vector (0 if the result is non-finite).
static float ik3d_len3(const float *v) {
    float len = sqrtf(ik3d_dot3(v, v));
    return isfinite(len) ? len : 0.0f;
}

/// @brief Normalize a 3-vector in place; returns 0 (leaving it unchanged) if degenerate (len <= 1e-6).
static int ik3d_normalize3(float *v) {
    float len = ik3d_len3(v);
    if (len <= 1e-6f)
        return 0;
    v[0] /= len;
    v[1] /= len;
    v[2] /= len;
    return 1;
}

/// @brief Distance between two 3-space points.
static float ik3d_distance3(const float *a, const float *b) {
    float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    return ik3d_len3(d);
}

/// @brief Row-major 4x4 matrix product out = a * b (out must not alias a or b).
static void ik3d_mat4f_mul(const float *a, const float *b, float *out) {
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            out[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c] +
                             a[r * 4 + 2] * b[2 * 4 + c] + a[r * 4 + 3] * b[3 * 4 + c];
        }
    }
}

/// @brief Accumulate each bone's global matrix as parent_global * local.
/// @details Bones are assumed topologically ordered (parent index < child index),
///          so a single forward pass suffices; roots (parent < 0) copy their local
///          straight through. @p bone_count is clamped to the skeleton's bone count.
static void ik3d_build_globals(const rt_skeleton3d *skeleton,
                               const float *locals,
                               float *globals,
                               int32_t bone_count) {
    if (!skeleton || !locals || !globals)
        return;
    if (bone_count > skeleton->bone_count)
        bone_count = skeleton->bone_count;
    for (int32_t bone = 0; bone < bone_count; bone++) {
        int32_t parent = skeleton->bones[bone].parent_index;
        if (parent >= 0 && parent < bone_count)
            ik3d_mat4f_mul(&globals[parent * 16], &locals[bone * 16], &globals[bone * 16]);
        else
            memcpy(&globals[bone * 16], &locals[bone * 16], 16 * sizeof(float));
    }
}

/// @brief Read a bone's world-space position (the translation column of its global matrix).
static void ik3d_global_position(const float *globals, int32_t bone, float *out) {
    const float *m = &globals[bone * 16];
    out[0] = m[3];
    out[1] = m[7];
    out[2] = m[11];
}

/// @brief Express a world point in @p bone's parent-local frame.
/// @details Projects the world delta onto each parent axis and divides by that axis'
///          squared length, so the result is correct even when the parent matrix carries
///          non-unit scale. Degenerate (near-zero) axes yield a 0 component. Roots with no
///          parent pass the world point through unchanged.
static void ik3d_parent_local_point(const rt_skeleton3d *skeleton,
                                    const float *globals,
                                    int32_t bone,
                                    const float *world,
                                    float *out_local) {
    int32_t parent = skeleton->bones[bone].parent_index;
    if (parent < 0) {
        memcpy(out_local, world, 3 * sizeof(float));
        return;
    }
    const float *p = &globals[parent * 16];
    float d[3] = {world[0] - p[3], world[1] - p[7], world[2] - p[11]};
    float axis_x[3] = {p[0], p[4], p[8]};
    float axis_y[3] = {p[1], p[5], p[9]};
    float axis_z[3] = {p[2], p[6], p[10]};
    float xx = ik3d_dot3(axis_x, axis_x);
    float yy = ik3d_dot3(axis_y, axis_y);
    float zz = ik3d_dot3(axis_z, axis_z);
    out_local[0] = xx > 1e-8f ? ik3d_dot3(d, axis_x) / xx : 0.0f;
    out_local[1] = yy > 1e-8f ? ik3d_dot3(d, axis_y) / yy : 0.0f;
    out_local[2] = zz > 1e-8f ? ik3d_dot3(d, axis_z) / zz : 0.0f;
}

/// @brief Move @p bone to the given world position by rewriting its local translation.
/// @details Converts @p world into the bone's parent-local frame, writes it into the
///          bone's local matrix, then rebuilds globals so downstream bones see the move.
static void ik3d_set_global_position(rt_ik_solver3d *solver,
                                     float *locals,
                                     float *globals,
                                     int32_t bone_count,
                                     int32_t bone,
                                     const float *world) {
    float local[3];
    if (!solver || !locals || !globals || bone < 0 || bone >= bone_count)
        return;
    ik3d_parent_local_point(solver->skeleton, globals, bone, world, local);
    locals[bone * 16 + 3] = local[0];
    locals[bone * 16 + 7] = local[1];
    locals[bone * 16 + 11] = local[2];
    ik3d_build_globals(solver->skeleton, locals, globals, bone_count);
}

/// @brief Write the identity quaternion (0, 0, 0, 1) into @p out.
static void ik3d_quat_identity(float *out) {
    out[0] = 0.0f;
    out[1] = 0.0f;
    out[2] = 0.0f;
    out[3] = 1.0f;
}

/// @brief Normalize a quaternion in place, falling back to identity if degenerate.
static void ik3d_quat_normalize(float *q) {
    float len = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (!isfinite(len) || len <= 1e-8f) {
        ik3d_quat_identity(q);
        return;
    }
    for (int i = 0; i < 4; i++)
        q[i] /= len;
}

/// @brief Convert a 3x3 rotation (given row by row) into a quaternion via Shepperd's method.
/// @details Selects the branch with the largest pivot (trace, or the dominant diagonal
///          term) to keep the divisor well away from zero, avoiding the catastrophic
///          cancellation a naive trace-only formula suffers when the trace is small or
///          negative. The result is normalized before returning.
static void ik3d_quat_from_matrix_rows(float r00,
                                       float r01,
                                       float r02,
                                       float r10,
                                       float r11,
                                       float r12,
                                       float r20,
                                       float r21,
                                       float r22,
                                       float *out) {
    float tr = r00 + r11 + r22;
    if (!out) {
        return;
    }
    if (tr > 0.0f) {
        float s = sqrtf(tr + 1.0f) * 2.0f;
        out[3] = 0.25f * s;
        out[0] = (r21 - r12) / s;
        out[1] = (r02 - r20) / s;
        out[2] = (r10 - r01) / s;
    } else if (r00 > r11 && r00 > r22) {
        float s = sqrtf(1.0f + r00 - r11 - r22) * 2.0f;
        out[3] = (r21 - r12) / s;
        out[0] = 0.25f * s;
        out[1] = (r01 + r10) / s;
        out[2] = (r02 + r20) / s;
    } else if (r11 > r22) {
        float s = sqrtf(1.0f + r11 - r00 - r22) * 2.0f;
        out[3] = (r02 - r20) / s;
        out[0] = (r01 + r10) / s;
        out[1] = 0.25f * s;
        out[2] = (r12 + r21) / s;
    } else {
        float s = sqrtf(1.0f + r22 - r00 - r11) * 2.0f;
        out[3] = (r10 - r01) / s;
        out[0] = (r02 + r20) / s;
        out[1] = (r12 + r21) / s;
        out[2] = 0.25f * s;
    }
    ik3d_quat_normalize(out);
}

/// @brief Spherical linear interpolation between quaternions @p a and @p b by @p t.
/// @details Negates @p b when the dot is negative so interpolation takes the short arc.
///          For nearly-parallel inputs (dot > 0.9995) it falls back to normalized lerp to
///          avoid the division-by-near-zero in the sin(theta) denominator.
static void ik3d_quat_slerp(const float *a, const float *b, float t, float *out) {
    float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    float nb[4] = {b[0], b[1], b[2], b[3]};
    if (dot < 0.0f) {
        dot = -dot;
        nb[0] = -nb[0];
        nb[1] = -nb[1];
        nb[2] = -nb[2];
        nb[3] = -nb[3];
    }
    if (dot > 0.9995f) {
        for (int i = 0; i < 4; i++)
            out[i] = a[i] + t * (nb[i] - a[i]);
    } else {
        if (dot > 1.0f)
            dot = 1.0f;
        float theta = acosf(dot);
        float sin_theta = sinf(theta);
        float wa = sinf((1.0f - t) * theta) / sin_theta;
        float wb = sinf(t * theta) / sin_theta;
        for (int i = 0; i < 4; i++)
            out[i] = wa * a[i] + wb * nb[i];
    }
    ik3d_quat_normalize(out);
}

/// @brief Quaternion conjugate (negated vector part) — the inverse for a unit quaternion.
static void ik3d_quat_conjugate(const float *q, float *out) {
    out[0] = -q[0];
    out[1] = -q[1];
    out[2] = -q[2];
    out[3] = q[3];
}

/// @brief Hamilton product out = a * b (apply b then a), for (x,y,z,w) quaternions.
static void ik3d_quat_mul(const float *a, const float *b, float *out) {
    float x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    float y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    float z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    float w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    out[0] = x;
    out[1] = y;
    out[2] = z;
    out[3] = w;
    ik3d_quat_normalize(out);
}

/// @brief Decompose a row-major 4x4 matrix into translation, rotation quaternion, and scale.
/// @details Scale is the length of each basis column; columns are divided out before
///          extracting the rotation so shear-free TRS matrices round-trip. Non-finite or
///          near-zero scales default to 1 to keep the rotation extraction well-defined.
static void ik3d_decompose_trs(const float *m, float *out_pos, float *out_rot, float *out_scl) {
    float sx = sqrtf(m[0] * m[0] + m[4] * m[4] + m[8] * m[8]);
    float sy = sqrtf(m[1] * m[1] + m[5] * m[5] + m[9] * m[9]);
    float sz = sqrtf(m[2] * m[2] + m[6] * m[6] + m[10] * m[10]);
    out_pos[0] = isfinite(m[3]) ? m[3] : 0.0f;
    out_pos[1] = isfinite(m[7]) ? m[7] : 0.0f;
    out_pos[2] = isfinite(m[11]) ? m[11] : 0.0f;
    out_scl[0] = isfinite(sx) && sx > 1e-6f ? sx : 1.0f;
    out_scl[1] = isfinite(sy) && sy > 1e-6f ? sy : 1.0f;
    out_scl[2] = isfinite(sz) && sz > 1e-6f ? sz : 1.0f;
    ik3d_quat_from_matrix_rows(m[0] / out_scl[0],
                               m[1] / out_scl[1],
                               m[2] / out_scl[2],
                               m[4] / out_scl[0],
                               m[5] / out_scl[1],
                               m[6] / out_scl[2],
                               m[8] / out_scl[0],
                               m[9] / out_scl[1],
                               m[10] / out_scl[2],
                               out_rot);
}

/// @brief Compose translation, a (unit) rotation quaternion, and scale into a row-major 4x4 matrix.
static void ik3d_build_trs(const float *pos, const float *quat, const float *scl, float *out) {
    float x = quat[0], y = quat[1], z = quat[2], w = quat[3];
    float x2 = x + x, y2 = y + y, z2 = z + z;
    float xx = x * x2, xy = x * y2, xz = x * z2;
    float yy = y * y2, yz = y * z2, zz = z * z2;
    float wx = w * x2, wy = w * y2, wz = w * z2;
    out[0] = (1.0f - (yy + zz)) * scl[0];
    out[1] = (xy - wz) * scl[1];
    out[2] = (xz + wy) * scl[2];
    out[3] = pos[0];
    out[4] = (xy + wz) * scl[0];
    out[5] = (1.0f - (xx + zz)) * scl[1];
    out[6] = (yz - wx) * scl[2];
    out[7] = pos[1];
    out[8] = (xz - wy) * scl[0];
    out[9] = (yz + wx) * scl[1];
    out[10] = (1.0f - (xx + yy)) * scl[2];
    out[11] = pos[2];
    out[12] = out[13] = out[14] = 0.0f;
    out[15] = 1.0f;
}

/// @brief Validate that @p chain is a strict parent -> child path of in-range bone indices.
/// @details Each entry must be a valid bone, and every entry after the first must have the
///          previous entry as its direct parent — the precondition every solver relies on.
static int ik3d_chain_is_parented(const rt_skeleton3d *skeleton, const int32_t *chain, int32_t count) {
    if (!skeleton || !chain || count <= 0)
        return 0;
    for (int32_t i = 0; i < count; i++) {
        if (chain[i] < 0 || chain[i] >= skeleton->bone_count)
            return 0;
        if (i > 0 && skeleton->bones[chain[i]].parent_index != chain[i - 1])
            return 0;
    }
    return 1;
}

/// @brief GC finalizer: release the retained skeleton and free both pose buffers.
static void ik_solver3d_finalize(void *obj) {
    rt_ik_solver3d *solver = (rt_ik_solver3d *)obj;
    if (!solver)
        return;
    ik_solver3d_release_ref((void **)&solver->skeleton);
    free(solver->solved_locals);
    free(solver->solved_globals);
    solver->solved_locals = NULL;
    solver->solved_globals = NULL;
}

/// @brief Shared constructor for all solver kinds.
/// @details Validates the chain, retains and freezes the skeleton, allocates the
///          local/global pose buffers seeded from the bind pose, captures the end
///          effector's bind position as the initial target, then runs one solve.
/// @return Opaque IKSolver3D handle, or NULL when validation or allocation fails.
static void *ik_solver3d_new(rt_skeleton3d *skeleton,
                             rt_ik_solver3d_kind kind,
                             const int32_t *chain,
                             int32_t chain_count) {
    if (!skeleton || !chain || chain_count <= 0 || chain_count > RT_IK_SOLVER3D_MAX_CHAIN)
        return NULL;
    if (!ik3d_chain_is_parented(skeleton, chain, chain_count))
        return NULL;
    rt_ik_solver3d *solver = (rt_ik_solver3d *)rt_obj_new_i64(RT_G3D_IKSOLVER3D_CLASS_ID,
                                                              (int64_t)sizeof(*solver));
    if (!solver) {
        rt_trap("IKSolver3D.New: allocation failed");
        return NULL;
    }
    memset(solver, 0, sizeof(*solver));
    solver->kind = kind;
    solver->chain_count = chain_count;
    memcpy(solver->chain, chain, (size_t)chain_count * sizeof(int32_t));
    solver->target[0] = 0.0f;
    solver->target[1] = 0.0f;
    solver->target[2] = 0.0f;
    solver->weight = 1.0f;
    rt_obj_retain_maybe(skeleton);
    solver->skeleton = skeleton;
    skeleton->frozen = 1;
    if (skeleton->bone_count > 0) {
        size_t bytes = (size_t)skeleton->bone_count * 16u * sizeof(float);
        solver->solved_locals = (float *)calloc(1, bytes);
        solver->solved_globals = (float *)calloc(1, bytes);
        if (!solver->solved_locals || !solver->solved_globals) {
            rt_trap("IKSolver3D.New: pose-buffer allocation failed");
            ik_solver3d_finalize(solver);
            rt_obj_free(solver);
            return NULL;
        }
        for (int32_t bone = 0; bone < skeleton->bone_count; bone++) {
            memcpy(&solver->solved_locals[bone * 16],
                   skeleton->bones[bone].bind_pose_local,
                   16 * sizeof(float));
        }
        ik3d_build_globals(skeleton, solver->solved_locals, solver->solved_globals, skeleton->bone_count);
        ik3d_global_position(solver->solved_globals, solver->chain[solver->chain_count - 1], solver->target);
    }
    rt_obj_set_finalizer(solver, ik_solver3d_finalize);
    rt_ik_solver3d_solve(solver);
    return solver;
}

/// @brief Create a two-bone IK solver over a root -> mid -> end bone chain (NULL on bad input).
void *rt_ik_solver3d_two_bone(void *skeleton_obj, int64_t root, int64_t mid, int64_t end) {
    rt_skeleton3d *skeleton = ik_solver3d_skeleton_checked(skeleton_obj);
    int32_t chain[3];
    if (!skeleton)
        return NULL;
    if (root < INT32_MIN || root > INT32_MAX || mid < INT32_MIN || mid > INT32_MAX ||
        end < INT32_MIN || end > INT32_MAX)
        return NULL;
    chain[0] = (int32_t)root;
    chain[1] = (int32_t)mid;
    chain[2] = (int32_t)end;
    return ik_solver3d_new(skeleton, RT_IK_SOLVER3D_TWO_BONE, chain, 3);
}

/// @brief Create a single-bone look-at/aim solver (NULL on bad input).
void *rt_ik_solver3d_look_at(void *skeleton_obj, int64_t bone) {
    rt_skeleton3d *skeleton = ik_solver3d_skeleton_checked(skeleton_obj);
    int32_t chain[1];
    if (!skeleton || bone < INT32_MIN || bone > INT32_MAX)
        return NULL;
    chain[0] = (int32_t)bone;
    return ik_solver3d_new(skeleton, RT_IK_SOLVER3D_LOOK_AT, chain, 1);
}

/// @brief Create a FABRIK solver from a Seq[Integer] bone chain (2..32 bones; NULL otherwise).
void *rt_ik_solver3d_fabrik(void *skeleton_obj, void *chain_obj) {
    rt_skeleton3d *skeleton = ik_solver3d_skeleton_checked(skeleton_obj);
    int64_t len;
    int32_t chain[RT_IK_SOLVER3D_MAX_CHAIN];
    if (!skeleton || !chain_obj)
        return NULL;
    len = rt_seq_len(chain_obj);
    if (len < 2 || len > RT_IK_SOLVER3D_MAX_CHAIN)
        return NULL;
    for (int64_t i = 0; i < len; i++) {
        int64_t value = rt_unbox_i64(rt_seq_get(chain_obj, i));
        if (value < INT32_MIN || value > INT32_MAX)
            return NULL;
        chain[i] = (int32_t)value;
    }
    return ik_solver3d_new(skeleton, RT_IK_SOLVER3D_FABRIK, chain, (int32_t)len);
}

/// @brief Set the world-space target the chain reaches toward (non-Vec3 ignored).
void rt_ik_solver3d_set_target(void *obj, void *target) {
    rt_ik_solver3d *solver = ik_solver3d_checked(obj);
    if (!solver || !rt_g3d_is_vec3(target))
        return;
    solver->target[0] = ik3d_finite_float(rt_vec3_x(target), 0.0f);
    solver->target[1] = ik3d_finite_float(rt_vec3_y(target), 0.0f);
    solver->target[2] = ik3d_finite_float(rt_vec3_z(target), 0.0f);
}

/// @brief Set the solve blend weight, clamped to [0, 1] (0 = pass-through, 1 = full IK).
void rt_ik_solver3d_set_weight(void *obj, double weight) {
    rt_ik_solver3d *solver = ik_solver3d_checked(obj);
    if (solver)
        solver->weight = ik3d_clamp01(weight);
}

/// @brief Set a world-space pole target that swings a two-bone chain's mid joint (non-Vec3 ignored).
void rt_ik_solver3d_set_pole(void *obj, void *pole) {
    rt_ik_solver3d *solver = ik_solver3d_checked(obj);
    if (!solver || !rt_g3d_is_vec3(pole))
        return;
    solver->pole[0] = ik3d_finite_float(rt_vec3_x(pole), 0.0f);
    solver->pole[1] = ik3d_finite_float(rt_vec3_y(pole), 0.0f);
    solver->pole[2] = ik3d_finite_float(rt_vec3_z(pole), 0.0f);
    solver->has_pole = 1;
}

/// @brief Set a ground normal that aligns the end (foot) bone's sole after the position solve.
/// @details Defaults a missing Y component to 1 (up). Non-Vec3 normals are ignored.
void rt_ik_solver3d_set_ground_normal(void *obj, void *normal) {
    rt_ik_solver3d *solver = ik_solver3d_checked(obj);
    if (!solver || !rt_g3d_is_vec3(normal))
        return;
    solver->ground_normal[0] = ik3d_finite_float(rt_vec3_x(normal), 0.0f);
    solver->ground_normal[1] = ik3d_finite_float(rt_vec3_y(normal), 1.0f);
    solver->ground_normal[2] = ik3d_finite_float(rt_vec3_z(normal), 0.0f);
    solver->has_ground_normal = 1;
}

/// @brief Orient the chain's end (foot) bone so its local +Y (sole-up) aligns with the supplied
///   ground normal, preserving the foot's facing as much as possible. Run after the position solve.
///   The desired world rotation is converted into the foot's parent-local space, so it is correct
///   for a foot parented under an arbitrarily-rotated leg.
static void ik3d_apply_foot_orientation(rt_ik_solver3d *solver,
                                        float *locals,
                                        float *globals,
                                        int32_t bone_count) {
    int32_t end;
    int32_t parent;
    float up[3], fwd_ref[3], right[3], fwd[3];
    float desired[4], parent_rot[4], parent_conj[4], local_target[4];
    float cur_pos[3], cur_rot[4], cur_scl[3], blended[4];
    if (!solver || !locals || !globals || solver->chain_count < 2)
        return;
    end = solver->chain[solver->chain_count - 1];
    if (end < 0 || end >= bone_count)
        return;
    up[0] = solver->ground_normal[0];
    up[1] = solver->ground_normal[1];
    up[2] = solver->ground_normal[2];
    if (!ik3d_normalize3(up))
        return;
    /* Reference forward = the foot's current world +Z (column 2 of its global matrix). */
    fwd_ref[0] = globals[end * 16 + 2];
    fwd_ref[1] = globals[end * 16 + 6];
    fwd_ref[2] = globals[end * 16 + 10];
    ik3d_cross3(up, fwd_ref, right);
    if (!ik3d_normalize3(right)) {
        float alt[3] = {1.0f, 0.0f, 0.0f};
        if (fabsf(up[0]) > 0.9f) {
            alt[0] = 0.0f;
            alt[2] = 1.0f;
        }
        ik3d_cross3(up, alt, right);
        if (!ik3d_normalize3(right))
            return;
    }
    ik3d_cross3(right, up, fwd);
    if (!ik3d_normalize3(fwd))
        return;
    /* Columns are right=X, up=Y, fwd=Z. */
    ik3d_quat_from_matrix_rows(
        right[0], up[0], fwd[0], right[1], up[1], fwd[1], right[2], up[2], fwd[2], desired);
    parent = solver->skeleton->bones[end].parent_index;
    if (parent >= 0 && parent < bone_count) {
        float ppos[3], pscl[3];
        ik3d_decompose_trs(&globals[parent * 16], ppos, parent_rot, pscl);
    } else {
        ik3d_quat_identity(parent_rot);
    }
    ik3d_quat_conjugate(parent_rot, parent_conj);
    ik3d_quat_mul(parent_conj, desired, local_target);
    ik3d_decompose_trs(&locals[end * 16], cur_pos, cur_rot, cur_scl);
    ik3d_quat_slerp(cur_rot, local_target, solver->weight, blended);
    ik3d_build_trs(cur_pos, blended, cur_scl, &locals[end * 16]);
    ik3d_build_globals(solver->skeleton, locals, globals, bone_count);
}

/// @brief Solve a multi-bone chain toward the target using FABRIK, then blend by weight.
/// @details If the target is out of reach (distance >= total chain length) the chain is
///          straightened toward it; otherwise FABRIK alternates backward (from the target)
///          and forward (from the fixed root) reaching passes until convergence. For a
///          three-bone chain with a pole target the mid joint is swung onto the pole plane,
///          the solved positions are blended against the originals by @p solver->weight, and
///          a ground normal (if set) finally orients the foot bone.
static int ik3d_apply_chain(rt_ik_solver3d *solver,
                            float *locals,
                            float *globals,
                            int32_t bone_count) {
    float positions[RT_IK_SOLVER3D_MAX_CHAIN][3];
    float original[RT_IK_SOLVER3D_MAX_CHAIN][3];
    float lengths[RT_IK_SOLVER3D_MAX_CHAIN - 1];
    float total = 0.0f;
    int32_t count;
    if (!solver || !locals || !globals || solver->chain_count < 2)
        return 0;
    count = solver->chain_count;
    for (int32_t i = 0; i < count; i++) {
        if (solver->chain[i] < 0 || solver->chain[i] >= bone_count)
            return 0;
        ik3d_global_position(globals, solver->chain[i], original[i]);
        memcpy(positions[i], original[i], 3 * sizeof(float));
    }
    for (int32_t i = 0; i < count - 1; i++) {
        lengths[i] = ik3d_distance3(positions[i], positions[i + 1]);
        total += lengths[i];
    }
    if (total <= 1e-6f)
        return 1;

    float root[3] = {positions[0][0], positions[0][1], positions[0][2]};
    float to_target[3] = {solver->target[0] - root[0],
                          solver->target[1] - root[1],
                          solver->target[2] - root[2]};
    float root_dist = ik3d_len3(to_target);
    if (root_dist >= total) {
        if (!ik3d_normalize3(to_target))
            return 1;
        for (int32_t i = 1; i < count; i++) {
            positions[i][0] = positions[i - 1][0] + to_target[0] * lengths[i - 1];
            positions[i][1] = positions[i - 1][1] + to_target[1] * lengths[i - 1];
            positions[i][2] = positions[i - 1][2] + to_target[2] * lengths[i - 1];
        }
    } else {
        for (int iter = 0; iter < RT_IK_SOLVER3D_FABRIK_ITERS; iter++) {
            memcpy(positions[count - 1], solver->target, 3 * sizeof(float));
            for (int32_t i = count - 2; i >= 0; i--) {
                float dir[3] = {positions[i][0] - positions[i + 1][0],
                                positions[i][1] - positions[i + 1][1],
                                positions[i][2] - positions[i + 1][2]};
                if (!ik3d_normalize3(dir))
                    continue;
                positions[i][0] = positions[i + 1][0] + dir[0] * lengths[i];
                positions[i][1] = positions[i + 1][1] + dir[1] * lengths[i];
                positions[i][2] = positions[i + 1][2] + dir[2] * lengths[i];
            }
            memcpy(positions[0], root, 3 * sizeof(float));
            for (int32_t i = 1; i < count; i++) {
                float dir[3] = {positions[i][0] - positions[i - 1][0],
                                positions[i][1] - positions[i - 1][1],
                                positions[i][2] - positions[i - 1][2]};
                if (!ik3d_normalize3(dir))
                    continue;
                positions[i][0] = positions[i - 1][0] + dir[0] * lengths[i - 1];
                positions[i][1] = positions[i - 1][1] + dir[1] * lengths[i - 1];
                positions[i][2] = positions[i - 1][2] + dir[2] * lengths[i - 1];
            }
            if (ik3d_distance3(positions[count - 1], solver->target) <= 1e-4f)
                break;
        }
    }

    /* Pole-vector control (two-bone chains only): swing the middle joint around
     * the root->end axis so it points toward the pole target, preserving bone
     * lengths. Without this the FABRIK pass leaves the knee/elbow wherever it
     * happened to converge. */
    if (solver->has_pole && count == 3 && root_dist < total) {
        float axis[3] = {positions[2][0] - positions[0][0],
                         positions[2][1] - positions[0][1],
                         positions[2][2] - positions[0][2]};
        if (ik3d_normalize3(axis)) {
            float to_mid[3] = {positions[1][0] - positions[0][0],
                               positions[1][1] - positions[0][1],
                               positions[1][2] - positions[0][2]};
            float proj = to_mid[0] * axis[0] + to_mid[1] * axis[1] + to_mid[2] * axis[2];
            float on_axis[3] = {positions[0][0] + axis[0] * proj,
                                positions[0][1] + axis[1] * proj,
                                positions[0][2] + axis[2] * proj};
            float bend[3] = {positions[1][0] - on_axis[0],
                             positions[1][1] - on_axis[1],
                             positions[1][2] - on_axis[2]};
            float bend_len = ik3d_len3(bend);
            float to_pole[3] = {solver->pole[0] - positions[0][0],
                                solver->pole[1] - positions[0][1],
                                solver->pole[2] - positions[0][2]};
            float pdot = to_pole[0] * axis[0] + to_pole[1] * axis[1] + to_pole[2] * axis[2];
            float perp[3] = {to_pole[0] - axis[0] * pdot,
                             to_pole[1] - axis[1] * pdot,
                             to_pole[2] - axis[2] * pdot};
            if (bend_len > 1e-6f && ik3d_normalize3(perp)) {
                positions[1][0] = on_axis[0] + perp[0] * bend_len;
                positions[1][1] = on_axis[1] + perp[1] * bend_len;
                positions[1][2] = on_axis[2] + perp[2] * bend_len;
            }
        }
    }

    for (int32_t i = 1; i < count; i++) {
        float blended[3];
        int32_t bone = solver->chain[i];
        for (int lane = 0; lane < 3; lane++)
            blended[lane] = original[i][lane] + (positions[i][lane] - original[i][lane]) * solver->weight;
        ik3d_set_global_position(solver, locals, globals, bone_count, bone, blended);
    }
    if (solver->has_ground_normal)
        ik3d_apply_foot_orientation(solver, locals, globals, bone_count);
    return 1;
}

/// @brief Solve a single-bone look-at: rotate the bone so its forward axis faces the target.
/// @details Builds an orthonormal basis (right/up/forward) from the bone-to-target direction,
///          switching the reference up to world +X when forward is near-vertical to avoid a
///          degenerate cross product, then slerps from the current local rotation by weight.
static int ik3d_apply_look_at(rt_ik_solver3d *solver,
                              float *locals,
                              float *globals,
                              int32_t bone_count) {
    int32_t bone;
    float pos[3];
    float forward[3];
    float right[3];
    float up[3] = {0.0f, 1.0f, 0.0f};
    float up2[3];
    float local_pos[3], local_rot[4], local_scl[3];
    float target_rot[4], blended_rot[4];
    if (!solver || !locals || !globals || solver->chain_count != 1)
        return 0;
    bone = solver->chain[0];
    if (bone < 0 || bone >= bone_count)
        return 0;
    ik3d_global_position(globals, bone, pos);
    forward[0] = solver->target[0] - pos[0];
    forward[1] = solver->target[1] - pos[1];
    forward[2] = solver->target[2] - pos[2];
    if (!ik3d_normalize3(forward))
        return 1;
    if (fabsf(ik3d_dot3(forward, up)) > 0.98f) {
        up[0] = 1.0f;
        up[1] = 0.0f;
        up[2] = 0.0f;
    }
    ik3d_cross3(up, forward, right);
    if (!ik3d_normalize3(right))
        return 1;
    ik3d_cross3(forward, right, up2);
    if (!ik3d_normalize3(up2))
        return 1;
    ik3d_quat_from_matrix_rows(right[0],
                               up2[0],
                               forward[0],
                               right[1],
                               up2[1],
                               forward[1],
                               right[2],
                               up2[2],
                               forward[2],
                               target_rot);
    ik3d_decompose_trs(&locals[bone * 16], local_pos, local_rot, local_scl);
    ik3d_quat_slerp(local_rot, target_rot, solver->weight, blended_rot);
    ik3d_build_trs(local_pos, blended_rot, local_scl, &locals[bone * 16]);
    ik3d_build_globals(solver->skeleton, locals, globals, bone_count);
    return 1;
}

/// @brief Apply the solver in place to a controller's local-pose buffer and refresh globals.
/// @details A weight at/below 1e-6 is a no-op success; @p bone_count is clamped to the
///          skeleton. Dispatches to the look-at or chain solver by kind. Returns 1 on
///          success (including the no-op), 0 on invalid arguments.
int8_t rt_ik_solver3d_apply_to_pose(void *obj, float *locals, float *globals, int32_t bone_count) {
    rt_ik_solver3d *solver = ik_solver3d_checked(obj);
    if (!solver || !solver->skeleton || !locals || !globals || bone_count <= 0)
        return 0;
    if (solver->weight <= 1e-6f)
        return 1;
    if (bone_count > solver->skeleton->bone_count)
        bone_count = solver->skeleton->bone_count;
    ik3d_build_globals(solver->skeleton, locals, globals, bone_count);
    if (solver->kind == RT_IK_SOLVER3D_LOOK_AT)
        return (int8_t)ik3d_apply_look_at(solver, locals, globals, bone_count);
    return (int8_t)ik3d_apply_chain(solver, locals, globals, bone_count);
}

/// @brief Re-solve against the skeleton bind pose, refreshing the solver's owned pose buffers.
/// @details Reseeds solved_locals from each bone's bind pose, then applies the solver so the
///          cached solved_locals/solved_globals reflect the current target/weight settings.
void rt_ik_solver3d_solve(void *obj) {
    rt_ik_solver3d *solver = ik_solver3d_checked(obj);
    if (!solver || !solver->skeleton || !solver->solved_locals || !solver->solved_globals)
        return;
    int32_t bone_count = solver->skeleton->bone_count;
    for (int32_t bone = 0; bone < bone_count; bone++) {
        memcpy(&solver->solved_locals[bone * 16],
               solver->skeleton->bones[bone].bind_pose_local,
               16 * sizeof(float));
    }
    rt_ik_solver3d_apply_to_pose(solver, solver->solved_locals, solver->solved_globals, bone_count);
}

/// @brief Borrow the Skeleton3D handle retained by this solver (not retained; NULL if invalid).
void *rt_ik_solver3d_get_skeleton(void *obj) {
    rt_ik_solver3d *solver = ik_solver3d_checked(obj);
    return solver ? solver->skeleton : NULL;
}

#else
typedef int rt_iksolver3d_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
