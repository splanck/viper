//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_scene3d.c
// Purpose: Viper.Graphics3D.Scene3D / SceneNode3D — 3D scene graph with
//   parent-child transform propagation. Each node holds local TRS, and the
//   world matrix is lazily recomputed on access or draw.
//
// Key invariants:
//   - TRS order: world = parent_world * Translate * Rotate * Scale
//   - Dirty flag propagates DOWN: changing a parent marks all descendants dirty.
//   - Children array is heap-allocated (not GC-managed); freed in finalizer.
//   - Mesh/material/name and LOD meshes are retained by the node.
//
// Links: rt_scene3d.h, rt_quat.h, rt_mat4.h, plans/3d/12-scene-graph.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_scene3d.h"
#include "rt_scene3d_internal.h"
#include "rt_audio3d.h"
#include "rt_animcontroller3d.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_pixels_internal.h"
#include "rt_physics3d.h"
#include "vgfx3d_frustum.h"

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_obj_retain_maybe(void *obj);
extern int rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);
#include "rt_trap.h"
extern void *rt_vec3_new(double x, double y, double z);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);
extern void *rt_quat_new(double x, double y, double z, double w);
extern double rt_quat_x(void *q);
extern double rt_quat_y(void *q);
extern double rt_quat_z(void *q);
extern double rt_quat_w(void *q);
extern void *rt_mat4_new(double m0,
                         double m1,
                         double m2,
                         double m3,
                         double m4,
                         double m5,
                         double m6,
                         double m7,
                         double m8,
                         double m9,
                         double m10,
                         double m11,
                         double m12,
                         double m13,
                         double m14,
                         double m15);
extern rt_string rt_const_cstr(const char *s);
extern rt_string rt_string_from_bytes(const char *data, size_t len);
extern void *rt_json_parse_object(rt_string text);
extern void *rt_map_get(void *map, rt_string key);
extern int64_t rt_seq_len(void *seq);
extern void *rt_seq_get(void *seq, int64_t index);
extern int64_t rt_box_type(void *box);
extern int64_t rt_unbox_i64(void *boxed);
extern double rt_unbox_f64(void *boxed);
extern int64_t rt_unbox_i1(void *boxed);
extern rt_string rt_unbox_str(void *boxed);
extern void *rt_material3d_new(void);
extern void rt_material3d_set_color(void *obj, double r, double g, double b);
extern void rt_material3d_set_texture(void *obj, void *pixels);
extern void rt_material3d_set_shininess(void *obj, double s);
extern void rt_material3d_set_unlit(void *obj, int8_t unlit);
extern void rt_material3d_set_shading_model(void *obj, int64_t model);
extern void rt_material3d_set_custom_param(void *obj, int64_t index, double value);
extern void rt_material3d_set_alpha(void *obj, double alpha);
extern void rt_material3d_set_normal_map(void *obj, void *pixels);
extern void rt_material3d_set_specular_map(void *obj, void *pixels);
extern void rt_material3d_set_emissive_map(void *obj, void *pixels);
extern void rt_material3d_set_emissive_color(void *obj, double r, double g, double b);
extern void rt_material3d_set_env_map(void *obj, void *cubemap);
extern void rt_material3d_set_reflectivity(void *obj, double r);
extern void *rt_cubemap3d_new(void *px, void *nx, void *py, void *ny, void *pz, void *nz);

/* Forward declarations for Canvas3D draw functions */
extern void rt_canvas3d_begin(void *obj, void *camera);
extern void rt_canvas3d_draw_mesh(void *obj, void *mesh, void *transform, void *material);
extern void rt_canvas3d_draw_mesh_matrix_keyed(void *obj,
                                               void *mesh,
                                               const double *model_matrix,
                                               void *material,
                                               const void *motion_key,
                                               const float *prev_bone_palette,
                                               const float *prev_morph_weights);
extern void rt_canvas3d_end(void *obj);

#define NODE_INIT_CHILDREN 4

static void scene3d_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

/*==========================================================================
 * Helpers
 *=========================================================================*/

/// @brief Build a TRS matrix: Translate * Rotate * Scale (row-major).
/// Quaternion (x,y,z,w) is expanded inline to avoid allocating a Mat4.
static void build_trs_matrix(const double *pos,
                             const double *quat,
                             const double *scl,
                             double *out) {
    double x = quat[0], y = quat[1], z = quat[2], w = quat[3];
    double x2 = x + x, y2 = y + y, z2 = z + z;
    double xx = x * x2, xy = x * y2, xz = x * z2;
    double yy = y * y2, yz = y * z2, zz = z * z2;
    double wx = w * x2, wy = w * y2, wz = w * z2;

    double sx = scl[0], sy = scl[1], sz = scl[2];

    /* R * S (rotation columns scaled) */
    out[0] = (1.0 - (yy + zz)) * sx;
    out[1] = (xy - wz) * sy;
    out[2] = (xz + wy) * sz;
    out[3] = pos[0];

    out[4] = (xy + wz) * sx;
    out[5] = (1.0 - (xx + zz)) * sy;
    out[6] = (yz - wx) * sz;
    out[7] = pos[1];

    out[8] = (xz - wy) * sx;
    out[9] = (yz + wx) * sy;
    out[10] = (1.0 - (xx + yy)) * sz;
    out[11] = pos[2];

    out[12] = 0.0;
    out[13] = 0.0;
    out[14] = 0.0;
    out[15] = 1.0;
}

/// @brief Multiply two 4x4 row-major matrices: out = a * b.
static void mat4d_mul(const double *a, const double *b, double *out) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            out[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c] +
                             a[r * 4 + 2] * b[2 * 4 + c] + a[r * 4 + 3] * b[3 * 4 + c];
}

static int mat4d_invert(const double *m, double *out) {
    double inv[16];
    double det;
    double inv_det;
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
             m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
             m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
             m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
              m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
             m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
             m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
             m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
              m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
             m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
             m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
              m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
              m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
             m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
             m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] -
              m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
              m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

    det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (fabs(det) < 1e-12)
        return -1;

    inv_det = 1.0 / det;
    for (int i = 0; i < 16; i++)
        out[i] = inv[i] * inv_det;
    return 0;
}

static void mat4d_transform_point(const double *m,
                                  double x,
                                  double y,
                                  double z,
                                  double *out_x,
                                  double *out_y,
                                  double *out_z) {
    if (!m || !out_x || !out_y || !out_z)
        return;
    *out_x = m[0] * x + m[1] * y + m[2] * z + m[3];
    *out_y = m[4] * x + m[5] * y + m[6] * z + m[7];
    *out_z = m[8] * x + m[9] * y + m[10] * z + m[11];
}

static void quat_identity(double *out) {
    if (!out)
        return;
    out[0] = 0.0;
    out[1] = 0.0;
    out[2] = 0.0;
    out[3] = 1.0;
}

static void quat_normalize_local(double *q) {
    double len_sq;
    double inv_len;
    if (!q)
        return;
    len_sq = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    if (len_sq < 1e-20) {
        quat_identity(q);
        return;
    }
    inv_len = 1.0 / sqrt(len_sq);
    q[0] *= inv_len;
    q[1] *= inv_len;
    q[2] *= inv_len;
    q[3] *= inv_len;
}

static void quat_conjugate_local(const double *q, double *out) {
    if (!q || !out)
        return;
    out[0] = -q[0];
    out[1] = -q[1];
    out[2] = -q[2];
    out[3] = q[3];
}

static void quat_mul_local(const double *a, const double *b, double *out) {
    double x;
    double y;
    double z;
    double w;
    if (!a || !b || !out)
        return;
    x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    out[0] = x;
    out[1] = y;
    out[2] = z;
    out[3] = w;
    quat_normalize_local(out);
}

static void quat_from_matrix_rows(double m00,
                                  double m01,
                                  double m02,
                                  double m10,
                                  double m11,
                                  double m12,
                                  double m20,
                                  double m21,
                                  double m22,
                                  double *out) {
    double trace;
    if (!out)
        return;
    trace = m00 + m11 + m22;
    if (trace > 0.0) {
        double s = sqrt(trace + 1.0) * 2.0;
        out[3] = 0.25 * s;
        out[0] = (m21 - m12) / s;
        out[1] = (m02 - m20) / s;
        out[2] = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        double s = sqrt(1.0 + m00 - m11 - m22) * 2.0;
        out[3] = (m21 - m12) / s;
        out[0] = 0.25 * s;
        out[1] = (m01 + m10) / s;
        out[2] = (m02 + m20) / s;
    } else if (m11 > m22) {
        double s = sqrt(1.0 + m11 - m00 - m22) * 2.0;
        out[3] = (m02 - m20) / s;
        out[0] = (m01 + m10) / s;
        out[1] = 0.25 * s;
        out[2] = (m12 + m21) / s;
    } else {
        double s = sqrt(1.0 + m22 - m00 - m11) * 2.0;
        out[3] = (m10 - m01) / s;
        out[0] = (m02 + m20) / s;
        out[1] = (m12 + m21) / s;
        out[2] = 0.25 * s;
    }
    quat_normalize_local(out);
}

static void quat_from_world_matrix(const double *m, double *out) {
    double rx;
    double ry;
    double rz;
    double ux;
    double uy;
    double uz;
    double fx;
    double fy;
    double fz;
    double rlen;
    double ulen;
    double flen;
    if (!m || !out) {
        return;
    }
    rx = m[0];
    ry = m[4];
    rz = m[8];
    ux = m[1];
    uy = m[5];
    uz = m[9];
    fx = m[2];
    fy = m[6];
    fz = m[10];
    rlen = sqrt(rx * rx + ry * ry + rz * rz);
    ulen = sqrt(ux * ux + uy * uy + uz * uz);
    flen = sqrt(fx * fx + fy * fy + fz * fz);
    if (rlen < 1e-12 || ulen < 1e-12 || flen < 1e-12) {
        quat_identity(out);
        return;
    }
    rx /= rlen;
    ry /= rlen;
    rz /= rlen;
    ux /= ulen;
    uy /= ulen;
    uz /= ulen;
    fx /= flen;
    fy /= flen;
    fz /= flen;
    quat_from_matrix_rows(rx, ux, fx, ry, uy, fy, rz, uz, fz, out);
}

/// @brief Recursively mark a node and all descendants as dirty.
static void mark_dirty(rt_scene_node3d *node) {
    node->world_dirty = 1;
    for (int32_t i = 0; i < node->child_count; i++)
        mark_dirty(node->children[i]);
}

/// @brief Recompute the world matrix if dirty (recursive up to parent).
static void recompute_world_matrix(rt_scene_node3d *node) {
    if (!node->world_dirty)
        return;

    double local[16];
    build_trs_matrix(node->position, node->rotation, node->scale_xyz, local);

    if (node->parent) {
        recompute_world_matrix(node->parent);
        mat4d_mul(node->parent->world_matrix, local, node->world_matrix);
    } else {
        memcpy(node->world_matrix, local, sizeof(double) * 16);
    }

    node->world_dirty = 0;
}

/// @brief Count nodes in a subtree (including the root).
static int32_t count_subtree(const rt_scene_node3d *node) {
    int32_t n = 1;
    for (int32_t i = 0; i < node->child_count; i++)
        n += count_subtree(node->children[i]);
    return n;
}

static void scene_node_get_world_position(rt_scene_node3d *node, double *x, double *y, double *z) {
    if (!x || !y || !z) {
        return;
    }
    *x = 0.0;
    *y = 0.0;
    *z = 0.0;
    if (!node)
        return;
    recompute_world_matrix(node);
    *x = node->world_matrix[3];
    *y = node->world_matrix[7];
    *z = node->world_matrix[11];
}

static void scene_node_get_world_rotation(rt_scene_node3d *node, double *out_quat) {
    if (!out_quat) {
        return;
    }
    quat_identity(out_quat);
    if (!node)
        return;
    recompute_world_matrix(node);
    quat_from_world_matrix(node->world_matrix, out_quat);
}

static void scene_node_set_world_transform(rt_scene_node3d *node,
                                           const double *world_pos,
                                           const double *world_quat) {
    double local_pos[3];
    double local_quat[4];
    double inv_parent[16];
    if (!node || !world_pos || !world_quat)
        return;

    if (!node->parent) {
        node->position[0] = world_pos[0];
        node->position[1] = world_pos[1];
        node->position[2] = world_pos[2];
        node->rotation[0] = world_quat[0];
        node->rotation[1] = world_quat[1];
        node->rotation[2] = world_quat[2];
        node->rotation[3] = world_quat[3];
        mark_dirty(node);
        return;
    }

    recompute_world_matrix(node->parent);
    if (mat4d_invert(node->parent->world_matrix, inv_parent) == 0) {
        mat4d_transform_point(inv_parent,
                              world_pos[0],
                              world_pos[1],
                              world_pos[2],
                              &local_pos[0],
                              &local_pos[1],
                              &local_pos[2]);
    } else {
        local_pos[0] = world_pos[0];
        local_pos[1] = world_pos[1];
        local_pos[2] = world_pos[2];
    }

    node->position[0] = local_pos[0];
    node->position[1] = local_pos[1];
    node->position[2] = local_pos[2];

    {
        double parent_world_quat[4];
        double parent_inv_quat[4];
        scene_node_get_world_rotation(node->parent, parent_world_quat);
        quat_conjugate_local(parent_world_quat, parent_inv_quat);
        quat_mul_local(parent_inv_quat, world_quat, local_quat);
        node->rotation[0] = local_quat[0];
        node->rotation[1] = local_quat[1];
        node->rotation[2] = local_quat[2];
        node->rotation[3] = local_quat[3];
    }

    mark_dirty(node);
}

static void scene_node_apply_root_motion(rt_scene_node3d *node) {
    void *delta;
    if (!node || !node->bound_animator)
        return;
    delta = rt_anim_controller3d_consume_root_motion(node->bound_animator);
    if (!delta)
        return;
    node->position[0] += rt_vec3_x(delta);
    node->position[1] += rt_vec3_y(delta);
    node->position[2] += rt_vec3_z(delta);
    mark_dirty(node);
}

static void scene_node_sync_recursive(rt_scene_node3d *node) {
    int64_t mode;
    int pull_from_body;
    int push_to_body;
    int body_is_kinematic;
    if (!node)
        return;

    mode = node->sync_mode;
    body_is_kinematic = node->bound_body ? rt_body3d_is_kinematic(node->bound_body) : 0;
    pull_from_body = node->bound_body &&
                     (mode == RT_SCENE_NODE3D_SYNC_NODE_FROM_BODY ||
                      (mode == RT_SCENE_NODE3D_SYNC_TWO_WAY_KINEMATIC && !body_is_kinematic));
    push_to_body = node->bound_body &&
                   (mode == RT_SCENE_NODE3D_SYNC_BODY_FROM_NODE ||
                    (mode == RT_SCENE_NODE3D_SYNC_TWO_WAY_KINEMATIC && body_is_kinematic));

    if (pull_from_body) {
        double world_pos[3];
        double world_quat[4];
        void *pos = rt_body3d_get_position(node->bound_body);
        void *quat = rt_body3d_get_orientation(node->bound_body);
        world_pos[0] = pos ? rt_vec3_x(pos) : 0.0;
        world_pos[1] = pos ? rt_vec3_y(pos) : 0.0;
        world_pos[2] = pos ? rt_vec3_z(pos) : 0.0;
        world_quat[0] = quat ? rt_quat_x(quat) : 0.0;
        world_quat[1] = quat ? rt_quat_y(quat) : 0.0;
        world_quat[2] = quat ? rt_quat_z(quat) : 0.0;
        world_quat[3] = quat ? rt_quat_w(quat) : 1.0;
        scene_node_set_world_transform(node, world_pos, world_quat);
    } else if (node->bound_animator &&
               (mode == RT_SCENE_NODE3D_SYNC_NODE_FROM_ANIMATOR_ROOT_MOTION || push_to_body)) {
        scene_node_apply_root_motion(node);
    }

    if (push_to_body) {
        double world_pos[3];
        double world_quat[4];
        scene_node_get_world_position(node, &world_pos[0], &world_pos[1], &world_pos[2]);
        scene_node_get_world_rotation(node, world_quat);
        rt_body3d_set_position(node->bound_body, world_pos[0], world_pos[1], world_pos[2]);
        rt_body3d_set_orientation(
            node->bound_body,
            rt_quat_new(world_quat[0], world_quat[1], world_quat[2], world_quat[3]));
    }

    for (int32_t i = 0; i < node->child_count; i++)
        scene_node_sync_recursive(node->children[i]);
}

static int node_contains(const rt_scene_node3d *root, const rt_scene_node3d *target) {
    if (!root)
        return 0;
    if (root == target)
        return 1;
    for (int32_t i = 0; i < root->child_count; i++) {
        if (node_contains(root->children[i], target))
            return 1;
    }
    return 0;
}

static void scene_mesh_bounds(rt_mesh3d *mesh,
                              float out_min[3],
                              float out_max[3],
                              float *out_radius) {
    if (!mesh) {
        if (out_min)
            out_min[0] = out_min[1] = out_min[2] = 0.0f;
        if (out_max)
            out_max[0] = out_max[1] = out_max[2] = 0.0f;
        if (out_radius)
            *out_radius = 0.0f;
        return;
    }
    rt_mesh3d_refresh_bounds(mesh);
    if (out_min) {
        out_min[0] = mesh->aabb_min[0];
        out_min[1] = mesh->aabb_min[1];
        out_min[2] = mesh->aabb_min[2];
    }
    if (out_max) {
        out_max[0] = mesh->aabb_max[0];
        out_max[1] = mesh->aabb_max[1];
        out_max[2] = mesh->aabb_max[2];
    }
    if (out_radius)
        *out_radius = mesh->bsphere_radius;
}

static void scene_world_point(const double *world_matrix, const float local[3], float out[3]) {
    if (!world_matrix || !local || !out)
        return;
    out[0] = (float)(world_matrix[0] * (double)local[0] + world_matrix[1] * (double)local[1] +
                     world_matrix[2] * (double)local[2] + world_matrix[3]);
    out[1] = (float)(world_matrix[4] * (double)local[0] + world_matrix[5] * (double)local[1] +
                     world_matrix[6] * (double)local[2] + world_matrix[7]);
    out[2] = (float)(world_matrix[8] * (double)local[0] + world_matrix[9] * (double)local[1] +
                     world_matrix[10] * (double)local[2] + world_matrix[11]);
}

/// @brief Recursive depth-first search by name.
extern const char *rt_string_cstr(rt_string s);

static rt_scene_node3d *find_by_name(rt_scene_node3d *node, const char *target) {
    if (node->name) {
        const char *s = rt_string_cstr(node->name);
        if (s && strcmp(s, target) == 0)
            return node;
    }
    for (int32_t i = 0; i < node->child_count; i++) {
        rt_scene_node3d *found = find_by_name(node->children[i], target);
        if (found)
            return found;
    }
    return NULL;
}

/// @brief Draw traversal: depth-first, skip invisible nodes, frustum-cull meshes.
/// Children are ALWAYS traversed even if the parent mesh is culled, because
/// child transforms may place them inside the frustum independently.
static void draw_node(rt_scene_node3d *node,
                      void *canvas3d,
                      const vgfx3d_frustum_t *frustum,
                      int32_t *culled,
                      const float *cam_pos) {
    if (!node->visible)
        return;

    recompute_world_matrix(node);

    int draw_self = 1;
    void *draw_mesh = node->mesh;
    float draw_min[3] = {0.0f, 0.0f, 0.0f};
    float draw_max[3] = {0.0f, 0.0f, 0.0f};
    float draw_radius = 0.0f;

    if (draw_mesh) {
        if (node->lod_count > 0 && cam_pos) {
            float local_center[3];
            float world_center[3];
            scene_mesh_bounds((rt_mesh3d *)node->mesh, draw_min, draw_max, &draw_radius);
            local_center[0] = 0.5f * (draw_min[0] + draw_max[0]);
            local_center[1] = 0.5f * (draw_min[1] + draw_max[1]);
            local_center[2] = 0.5f * (draw_min[2] + draw_max[2]);
            scene_world_point(node->world_matrix, local_center, world_center);
            {
                float dx = world_center[0] - cam_pos[0];
                float dy = world_center[1] - cam_pos[1];
                float dz = world_center[2] - cam_pos[2];
                float dist = sqrtf(dx * dx + dy * dy + dz * dz);
                for (int32_t l = node->lod_count - 1; l >= 0; l--) {
                    if (dist >= (float)node->lod_levels[l].distance) {
                        draw_mesh = node->lod_levels[l].mesh;
                        break;
                    }
                }
            }
        }
        scene_mesh_bounds((rt_mesh3d *)draw_mesh, draw_min, draw_max, &draw_radius);
    }

    /* Frustum cull: test world-space AABB if the node has a mesh */
    if (frustum && draw_mesh && draw_radius > 0.0f) {
        float world_min[3], world_max[3];
        vgfx3d_transform_aabb(draw_min, draw_max, node->world_matrix, world_min, world_max);
        if (vgfx3d_frustum_test_aabb(frustum, world_min, world_max) == 0) {
            draw_self = 0;
            if (culled)
                (*culled)++;
        }
    }

    if (draw_self && draw_mesh && node->material) {
        const float *saved_palette = ((rt_mesh3d *)draw_mesh)->bone_palette;
        const float *saved_prev_palette = ((rt_mesh3d *)draw_mesh)->prev_bone_palette;
        int32_t saved_bone_count = ((rt_mesh3d *)draw_mesh)->bone_count;
        const float *anim_palette = NULL;
        const float *anim_prev_palette = NULL;
        int32_t anim_bone_count = 0;

        if (node->bound_animator) {
            anim_palette =
                rt_anim_controller3d_get_final_palette_data(node->bound_animator, &anim_bone_count);
            anim_prev_palette = rt_anim_controller3d_get_previous_palette_data(node->bound_animator,
                                                                                &anim_bone_count);
        }
        if (anim_palette && anim_bone_count > 0 && saved_bone_count > 0) {
            ((rt_mesh3d *)draw_mesh)->bone_palette = anim_palette;
            ((rt_mesh3d *)draw_mesh)->bone_count =
                anim_bone_count < saved_bone_count ? anim_bone_count : saved_bone_count;
        }
        rt_canvas3d_draw_mesh_matrix_keyed(
            canvas3d, draw_mesh, node->world_matrix, node->material, node, anim_prev_palette, NULL);
        ((rt_mesh3d *)draw_mesh)->bone_palette = saved_palette;
        ((rt_mesh3d *)draw_mesh)->prev_bone_palette = saved_prev_palette;
        ((rt_mesh3d *)draw_mesh)->bone_count = saved_bone_count;
    }

    for (int32_t i = 0; i < node->child_count; i++)
        draw_node(node->children[i], canvas3d, frustum, culled, cam_pos);
}

/*==========================================================================
 * SceneNode3D — lifecycle
 *=========================================================================*/

static void rt_scene_node3d_finalize(void *obj) {
    rt_scene_node3d *node = (rt_scene_node3d *)obj;
    if (!node)
        return;

    for (int32_t i = 0; i < node->child_count; i++) {
        if (node->children[i])
            node->children[i]->parent = NULL;
        scene3d_release_ref((void **)&node->children[i]);
    }
    free(node->children);
    node->children = NULL;
    node->child_count = 0;
    node->child_capacity = 0;
    for (int32_t i = 0; i < node->lod_count; i++)
        scene3d_release_ref(&node->lod_levels[i].mesh);
    free(node->lod_levels);
    node->lod_levels = NULL;
    node->lod_count = 0;
    node->lod_capacity = 0;
    scene3d_release_ref(&node->mesh);
    scene3d_release_ref(&node->material);
    scene3d_release_ref(&node->bound_body);
    scene3d_release_ref(&node->bound_animator);
    scene3d_release_ref((void **)&node->name);
}

void *rt_scene_node3d_new(void) {
    rt_scene_node3d *node = (rt_scene_node3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_scene_node3d));
    if (!node) {
        rt_trap("SceneNode3D.New: memory allocation failed");
        return NULL;
    }
    node->vptr = NULL;
    node->position[0] = node->position[1] = node->position[2] = 0.0;
    node->rotation[0] = node->rotation[1] = node->rotation[2] = 0.0;
    node->rotation[3] = 1.0; /* identity quaternion (0,0,0,1) */
    node->scale_xyz[0] = node->scale_xyz[1] = node->scale_xyz[2] = 1.0;

    /* Identity world matrix */
    memset(node->world_matrix, 0, sizeof(double) * 16);
    node->world_matrix[0] = node->world_matrix[5] = 1.0;
    node->world_matrix[10] = node->world_matrix[15] = 1.0;
    node->world_dirty = 1;

    node->parent = NULL;
    node->children = NULL;
    node->child_count = 0;
    node->child_capacity = 0;

    node->mesh = NULL;
    node->material = NULL;
    node->bound_body = NULL;
    node->bound_animator = NULL;
    node->sync_mode = RT_SCENE_NODE3D_SYNC_NODE_FROM_BODY;
    node->visible = 1;
    node->name = NULL;

    memset(node->aabb_min, 0, sizeof(float) * 3);
    memset(node->aabb_max, 0, sizeof(float) * 3);
    node->bsphere_radius = 0.0f;

    node->lod_levels = NULL;
    node->lod_count = 0;
    node->lod_capacity = 0;

    rt_obj_set_finalizer(node, rt_scene_node3d_finalize);
    return node;
}

/*==========================================================================
 * SceneNode3D — transform
 *=========================================================================*/

void rt_scene_node3d_set_position(void *obj, double x, double y, double z) {
    if (!obj)
        return;
    rt_scene_node3d *n = (rt_scene_node3d *)obj;
    n->position[0] = x;
    n->position[1] = y;
    n->position[2] = z;
    mark_dirty(n);
}

void *rt_scene_node3d_get_position(void *obj) {
    if (!obj)
        return rt_vec3_new(0, 0, 0);
    rt_scene_node3d *n = (rt_scene_node3d *)obj;
    return rt_vec3_new(n->position[0], n->position[1], n->position[2]);
}

void rt_scene_node3d_set_rotation(void *obj, void *quat) {
    if (!obj || !quat)
        return;
    rt_scene_node3d *n = (rt_scene_node3d *)obj;
    n->rotation[0] = rt_quat_x(quat);
    n->rotation[1] = rt_quat_y(quat);
    n->rotation[2] = rt_quat_z(quat);
    n->rotation[3] = rt_quat_w(quat);
    mark_dirty(n);
}

void *rt_scene_node3d_get_rotation(void *obj) {
    if (!obj)
        return rt_quat_new(0, 0, 0, 1);
    rt_scene_node3d *n = (rt_scene_node3d *)obj;
    return rt_quat_new(n->rotation[0], n->rotation[1], n->rotation[2], n->rotation[3]);
}

void rt_scene_node3d_set_scale(void *obj, double x, double y, double z) {
    if (!obj)
        return;
    rt_scene_node3d *n = (rt_scene_node3d *)obj;
    n->scale_xyz[0] = x;
    n->scale_xyz[1] = y;
    n->scale_xyz[2] = z;
    mark_dirty(n);
}

void *rt_scene_node3d_get_scale(void *obj) {
    if (!obj)
        return rt_vec3_new(1, 1, 1);
    rt_scene_node3d *n = (rt_scene_node3d *)obj;
    return rt_vec3_new(n->scale_xyz[0], n->scale_xyz[1], n->scale_xyz[2]);
}

void *rt_scene_node3d_get_world_matrix(void *obj) {
    if (!obj)
        return NULL;
    rt_scene_node3d *n = (rt_scene_node3d *)obj;
    recompute_world_matrix(n);
    const double *m = n->world_matrix;
    return rt_mat4_new(m[0],
                       m[1],
                       m[2],
                       m[3],
                       m[4],
                       m[5],
                       m[6],
                       m[7],
                       m[8],
                       m[9],
                       m[10],
                       m[11],
                       m[12],
                       m[13],
                       m[14],
                       m[15]);
}

/*==========================================================================
 * SceneNode3D — hierarchy
 *=========================================================================*/

void rt_scene_node3d_add_child(void *obj, void *child_obj) {
    if (!obj || !child_obj || obj == child_obj)
        return;
    rt_scene_node3d *parent = (rt_scene_node3d *)obj;
    rt_scene_node3d *child = (rt_scene_node3d *)child_obj;

    /* Reject cycle formation: parent may not already be inside child's subtree. */
    if (node_contains(child, parent))
        return;

    /* Detach from previous parent if any */
    if (child->parent)
        rt_scene_node3d_remove_child(child->parent, child);

    /* Grow children array if needed */
    if (parent->child_count >= parent->child_capacity) {
        int32_t new_cap =
            parent->child_capacity == 0 ? NODE_INIT_CHILDREN : parent->child_capacity * 2;
        rt_scene_node3d **nc = (rt_scene_node3d **)realloc(
            parent->children, (size_t)new_cap * sizeof(rt_scene_node3d *));
        if (!nc)
            return;
        parent->children = nc;
        parent->child_capacity = new_cap;
    }

    parent->children[parent->child_count++] = child;
    child->parent = parent;
    rt_obj_retain_maybe(child);
    mark_dirty(child);
}

void rt_scene_node3d_remove_child(void *obj, void *child_obj) {
    if (!obj || !child_obj)
        return;
    rt_scene_node3d *parent = (rt_scene_node3d *)obj;
    rt_scene_node3d *child = (rt_scene_node3d *)child_obj;

    for (int32_t i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == child) {
            /* Shift remaining children down */
            for (int32_t j = i; j < parent->child_count - 1; j++)
                parent->children[j] = parent->children[j + 1];
            parent->child_count--;
            parent->children[parent->child_count] = NULL;
            child->parent = NULL;
            mark_dirty(child);
            if (rt_obj_release_check0(child))
                rt_obj_free(child);
            return;
        }
    }
}

int64_t rt_scene_node3d_child_count(void *obj) {
    return obj ? ((rt_scene_node3d *)obj)->child_count : 0;
}

void *rt_scene_node3d_get_child(void *obj, int64_t index) {
    if (!obj)
        return NULL;
    rt_scene_node3d *n = (rt_scene_node3d *)obj;
    if (index < 0 || index >= n->child_count)
        return NULL;
    return n->children[index];
}

void *rt_scene_node3d_get_parent(void *obj) {
    return obj ? ((rt_scene_node3d *)obj)->parent : NULL;
}

void *rt_scene_node3d_find(void *obj, rt_string name) {
    if (!obj || !name)
        return NULL;
    const char *s = rt_string_cstr(name);
    if (!s)
        return NULL;
    return find_by_name((rt_scene_node3d *)obj, s);
}

/*==========================================================================
 * SceneNode3D — renderable / visibility / name
 *=========================================================================*/

void rt_scene_node3d_set_mesh(void *obj, void *mesh) {
    if (!obj)
        return;
    rt_scene_node3d *n = (rt_scene_node3d *)obj;
    if (n->mesh == mesh) {
        if (mesh)
            scene_mesh_bounds((rt_mesh3d *)mesh, n->aabb_min, n->aabb_max, &n->bsphere_radius);
        return;
    }
    rt_obj_retain_maybe(mesh);
    scene3d_release_ref(&n->mesh);
    n->mesh = mesh;

    /* Compute object-space AABB from mesh vertices */
    if (mesh) {
        scene_mesh_bounds((rt_mesh3d *)mesh, n->aabb_min, n->aabb_max, &n->bsphere_radius);
    } else {
        n->aabb_min[0] = n->aabb_min[1] = n->aabb_min[2] = 0.0f;
        n->aabb_max[0] = n->aabb_max[1] = n->aabb_max[2] = 0.0f;
        n->bsphere_radius = 0.0f;
    }
}

void *rt_scene_node3d_get_mesh(void *obj) {
    return obj ? ((rt_scene_node3d *)obj)->mesh : NULL;
}

void rt_scene_node3d_set_material(void *obj, void *material) {
    if (!obj)
        return;
    rt_scene_node3d *node = (rt_scene_node3d *)obj;
    if (node->material == material)
        return;
    rt_obj_retain_maybe(material);
    scene3d_release_ref(&node->material);
    node->material = material;
}

void *rt_scene_node3d_get_material(void *obj) {
    return obj ? ((rt_scene_node3d *)obj)->material : NULL;
}

void rt_scene_node3d_set_visible(void *obj, int8_t visible) {
    if (obj)
        ((rt_scene_node3d *)obj)->visible = visible;
}

int8_t rt_scene_node3d_get_visible(void *obj) {
    return obj ? ((rt_scene_node3d *)obj)->visible : 0;
}

void rt_scene_node3d_set_name(void *obj, rt_string name) {
    if (!obj)
        return;
    rt_scene_node3d *node = (rt_scene_node3d *)obj;
    if (!name)
        name = rt_const_cstr("");
    if (node->name == name)
        return;
    rt_obj_retain_maybe(name);
    scene3d_release_ref((void **)&node->name);
    node->name = name;
}

rt_string rt_scene_node3d_get_name(void *obj) {
    if (obj && ((rt_scene_node3d *)obj)->name)
        return ((rt_scene_node3d *)obj)->name;
    return rt_const_cstr("");
}

void *rt_scene_node3d_get_aabb_min(void *obj) {
    if (!obj)
        return rt_vec3_new(0, 0, 0);
    rt_scene_node3d *n = (rt_scene_node3d *)obj;
    return rt_vec3_new(n->aabb_min[0], n->aabb_min[1], n->aabb_min[2]);
}

void *rt_scene_node3d_get_aabb_max(void *obj) {
    if (!obj)
        return rt_vec3_new(0, 0, 0);
    rt_scene_node3d *n = (rt_scene_node3d *)obj;
    return rt_vec3_new(n->aabb_max[0], n->aabb_max[1], n->aabb_max[2]);
}

void rt_scene_node3d_bind_body(void *obj, void *body) {
    rt_scene_node3d *node;
    if (!obj)
        return;
    node = (rt_scene_node3d *)obj;
    if (node->bound_body == body)
        return;
    rt_obj_retain_maybe(body);
    scene3d_release_ref(&node->bound_body);
    node->bound_body = body;
}

void rt_scene_node3d_clear_body_binding(void *obj) {
    if (!obj)
        return;
    scene3d_release_ref(&((rt_scene_node3d *)obj)->bound_body);
}

void *rt_scene_node3d_get_body(void *obj) {
    return obj ? ((rt_scene_node3d *)obj)->bound_body : NULL;
}

void rt_scene_node3d_set_sync_mode(void *obj, int64_t sync_mode) {
    rt_scene_node3d *node;
    if (!obj)
        return;
    node = (rt_scene_node3d *)obj;
    switch (sync_mode) {
    case RT_SCENE_NODE3D_SYNC_NODE_FROM_BODY:
    case RT_SCENE_NODE3D_SYNC_BODY_FROM_NODE:
    case RT_SCENE_NODE3D_SYNC_NODE_FROM_ANIMATOR_ROOT_MOTION:
    case RT_SCENE_NODE3D_SYNC_TWO_WAY_KINEMATIC:
        node->sync_mode = (int32_t)sync_mode;
        break;
    default:
        node->sync_mode = RT_SCENE_NODE3D_SYNC_NODE_FROM_BODY;
        break;
    }
}

int64_t rt_scene_node3d_get_sync_mode(void *obj) {
    return obj ? ((rt_scene_node3d *)obj)->sync_mode : RT_SCENE_NODE3D_SYNC_NODE_FROM_BODY;
}

void rt_scene_node3d_bind_animator(void *obj, void *controller) {
    rt_scene_node3d *node;
    if (!obj)
        return;
    node = (rt_scene_node3d *)obj;
    if (node->bound_animator == controller)
        return;
    rt_obj_retain_maybe(controller);
    scene3d_release_ref(&node->bound_animator);
    node->bound_animator = controller;
}

void rt_scene_node3d_clear_animator_binding(void *obj) {
    if (!obj)
        return;
    scene3d_release_ref(&((rt_scene_node3d *)obj)->bound_animator);
}

void *rt_scene_node3d_get_animator(void *obj) {
    return obj ? ((rt_scene_node3d *)obj)->bound_animator : NULL;
}

/*==========================================================================
 * Scene3D
 *=========================================================================*/

static void rt_scene3d_finalize(void *obj) {
    rt_scene3d *scene = (rt_scene3d *)obj;
    if (!scene)
        return;
    if (scene->root)
        scene->root->parent = NULL;
    scene3d_release_ref((void **)&scene->root);
}

void *rt_scene3d_new(void) {
    rt_scene3d *s = (rt_scene3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_scene3d));
    if (!s) {
        rt_trap("Scene3D.New: memory allocation failed");
        return NULL;
    }
    s->vptr = NULL;
    s->root = (rt_scene_node3d *)rt_scene_node3d_new();
    s->node_count = 1; /* root */
    s->last_culled_count = 0;
    rt_obj_set_finalizer(s, rt_scene3d_finalize);
    return s;
}

void *rt_scene3d_get_root(void *obj) {
    return obj ? ((rt_scene3d *)obj)->root : NULL;
}

void rt_scene3d_add(void *obj, void *node) {
    if (!obj)
        return;
    rt_scene3d *s = (rt_scene3d *)obj;
    rt_scene_node3d_add_child(s->root, node);
    s->node_count = count_subtree(s->root);
}

void rt_scene3d_remove(void *obj, void *node) {
    if (!obj || !node)
        return;
    rt_scene3d *s = (rt_scene3d *)obj;
    rt_scene_node3d *n = (rt_scene_node3d *)node;
    if (n->parent)
        rt_scene_node3d_remove_child(n->parent, n);
    s->node_count = count_subtree(s->root);
}

void *rt_scene3d_find(void *obj, rt_string name) {
    if (!obj || !name)
        return NULL;
    const char *str = rt_string_cstr(name);
    if (!str)
        return NULL;
    rt_scene3d *s = (rt_scene3d *)obj;
    return find_by_name(s->root, str);
}

/// @brief Helper to convert double[16] to float[16] for frustum extraction.
static void mat4_d2f_local(const double *src, float *dst) {
    for (int i = 0; i < 16; i++)
        dst[i] = (float)src[i];
}

void rt_scene3d_draw(void *obj, void *canvas3d, void *camera) {
    if (!obj || !canvas3d || !camera)
        return;
    rt_scene3d *s = (rt_scene3d *)obj;
    rt_canvas3d *canvas = (rt_canvas3d *)canvas3d;
    rt_camera3d *cam = (rt_camera3d *)camera;
    int8_t started_frame = 0;

    /* Build VP matrix and extract frustum planes */
    float vf[16], pf[16], vp[16];
    mat4_d2f_local(cam->view, vf);
    mat4_d2f_local(cam->projection, pf);
    /* VP = P * V (row-major) */
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            vp[r * 4 + c] = pf[r * 4 + 0] * vf[0 * 4 + c] + pf[r * 4 + 1] * vf[1 * 4 + c] +
                            pf[r * 4 + 2] * vf[2 * 4 + c] + pf[r * 4 + 3] * vf[3 * 4 + c];

    vgfx3d_frustum_t frustum;
    vgfx3d_frustum_extract(&frustum, vp);

    int32_t culled = 0;
    if (canvas->in_frame) {
        if (canvas->frame_is_2d) {
            rt_trap("Scene3D.Draw: cannot draw a 3D scene during Begin2D/End");
            return;
        }
    } else {
        rt_canvas3d_begin(canvas3d, camera);
        started_frame = 1;
    }
    float cam_pos[3] = {(float)cam->eye[0], (float)cam->eye[1], (float)cam->eye[2]};
    draw_node(s->root, canvas3d, &frustum, &culled, cam_pos);
    if (started_frame)
        rt_canvas3d_end(canvas3d);
    s->last_culled_count = culled;
}

void rt_scene3d_clear(void *obj) {
    if (!obj)
        return;
    rt_scene3d *s = (rt_scene3d *)obj;
    /* Detach all children from root */
    for (int32_t i = 0; i < s->root->child_count; i++) {
        s->root->children[i]->parent = NULL;
        scene3d_release_ref((void **)&s->root->children[i]);
    }
    s->root->child_count = 0;
    s->node_count = 1; /* just root */
}

void rt_scene3d_sync_bindings(void *obj, double dt) {
    rt_scene3d *scene = (rt_scene3d *)obj;
    if (!scene || !scene->root)
        return;
    scene_node_sync_recursive(scene->root);
    rt_audio3d_sync_bindings(dt);
}

int64_t rt_scene3d_get_node_count(void *obj) {
    return obj ? ((rt_scene3d *)obj)->node_count : 0;
}

int64_t rt_scene3d_get_culled_count(void *obj) {
    return obj ? ((rt_scene3d *)obj)->last_culled_count : 0;
}

/*==========================================================================
 * LOD — Level of Detail per SceneNode3D
 *=========================================================================*/

void rt_scene_node3d_add_lod(void *obj, double distance, void *mesh) {
    if (!obj || !mesh)
        return;
    rt_scene_node3d *node = (rt_scene_node3d *)obj;

    if (node->lod_count >= node->lod_capacity) {
        int32_t new_cap = node->lod_capacity < 4 ? 4 : node->lod_capacity * 2;
        void *tmp = realloc(node->lod_levels, (size_t)new_cap * sizeof(node->lod_levels[0]));
        if (!tmp)
            return;
        node->lod_levels = tmp;
        node->lod_capacity = new_cap;
    }

    /* Insert sorted by distance ascending */
    int32_t pos = node->lod_count;
    for (int32_t i = 0; i < node->lod_count; i++) {
        if (distance < node->lod_levels[i].distance) {
            pos = i;
            break;
        }
    }
    /* Shift elements right */
    for (int32_t i = node->lod_count; i > pos; i--)
        node->lod_levels[i] = node->lod_levels[i - 1];

    node->lod_levels[pos].distance = distance;
    node->lod_levels[pos].mesh = mesh;
    rt_obj_retain_maybe(mesh);
    node->lod_count++;
}

void rt_scene_node3d_clear_lod(void *obj) {
    if (!obj)
        return;
    rt_scene_node3d *node = (rt_scene_node3d *)obj;
    for (int32_t i = 0; i < node->lod_count; i++)
        scene3d_release_ref(&node->lod_levels[i].mesh);
    node->lod_count = 0;
}

int64_t rt_scene_node3d_get_lod_count(void *obj) {
    return obj ? ((rt_scene_node3d *)obj)->lod_count : 0;
}

double rt_scene_node3d_get_lod_distance(void *obj, int64_t index) {
    if (!obj)
        return 0.0;
    rt_scene_node3d *node = (rt_scene_node3d *)obj;
    if (index < 0 || index >= node->lod_count)
        return 0.0;
    return node->lod_levels[index].distance;
}

void *rt_scene_node3d_get_lod_mesh(void *obj, int64_t index) {
    if (!obj)
        return NULL;
    rt_scene_node3d *node = (rt_scene_node3d *)obj;
    if (index < 0 || index >= node->lod_count)
        return NULL;
    return node->lod_levels[index].mesh;
}

//=============================================================================

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
