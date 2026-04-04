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
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_pixels_internal.h"
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
extern void rt_trap(const char *msg);
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

/*==========================================================================
 * SceneNode3D internal structure
 *=========================================================================*/

typedef struct rt_scene_node3d {
    void *vptr;

    /* Local transform */
    double position[3];
    double rotation[4]; /* quaternion (x, y, z, w) */
    double scale_xyz[3];

    /* Cached world transform */
    double world_matrix[16]; /* row-major */
    int8_t world_dirty;

    /* Hierarchy */
    struct rt_scene_node3d *parent;
    struct rt_scene_node3d **children;
    int32_t child_count;
    int32_t child_capacity;

    /* Renderable attachment */
    void *mesh;
    void *material;

    /* Visibility */
    int8_t visible;

    /* Name (retained rt_string) */
    rt_string name;

    /* Bounding volume (Phase 13 frustum culling) */
    float aabb_min[3];
    float aabb_max[3];
    float bsphere_radius;

    /* LOD levels (F1) — sorted by distance ascending */
    struct {
        double distance;
        void *mesh;
    } *lod_levels;

    int32_t lod_count;
    int32_t lod_capacity;
} rt_scene_node3d;

typedef struct {
    void *vptr;
    rt_scene_node3d *root;
    int32_t node_count;
    int32_t last_culled_count; /* from most recent Draw call */
} rt_scene3d;

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
        rt_canvas3d_draw_mesh_matrix_keyed(
            canvas3d, draw_mesh, node->world_matrix, node->material, node, NULL, NULL);
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
// Scene save/load (.vscn JSON format)
//=============================================================================

typedef struct {
    void **items;
    int32_t count;
    int32_t capacity;
} vscn_ptr_table_t;

typedef struct {
    vscn_ptr_table_t meshes;
    vscn_ptr_table_t materials;
    vscn_ptr_table_t textures;
    vscn_ptr_table_t cubemaps;
} vscn_save_context_t;

static const char vscn_base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int vscn_base64_digit_value(char c) {
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    if (c == '=')
        return -2;
    return -1;
}

static char *vscn_base64_encode(const uint8_t *data, size_t len, size_t *out_len) {
    size_t olen = ((len + 2) / 3) * 4;
    char *output = (char *)malloc(olen + 1);
    if (!output)
        return NULL;

    size_t i = 0, j = 0;
    while (i < len) {
        uint32_t octet_a = data[i++];
        uint32_t octet_b = (i < len) ? data[i++] : 0;
        uint32_t octet_c = (i < len) ? data[i++] : 0;
        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        output[j++] = vscn_base64_chars[(triple >> 18) & 0x3F];
        output[j++] = vscn_base64_chars[(triple >> 12) & 0x3F];
        output[j++] = vscn_base64_chars[(triple >> 6) & 0x3F];
        output[j++] = vscn_base64_chars[triple & 0x3F];
    }

    {
        size_t padding = (3 - (len % 3)) % 3;
        for (size_t p = 0; p < padding; p++)
            output[j - 1 - p] = '=';
    }

    output[j] = '\0';
    if (out_len)
        *out_len = j;
    return output;
}

static uint8_t *vscn_base64_decode(const char *data, size_t len, size_t *out_len) {
    if (!data)
        return NULL;
    if (len == 0) {
        uint8_t *empty = (uint8_t *)malloc(1);
        if (out_len)
            *out_len = 0;
        return empty;
    }
    if (len % 4 != 0)
        return NULL;

    size_t olen = (len / 4) * 3;
    if (len > 0 && data[len - 1] == '=')
        olen--;
    if (len > 1 && data[len - 2] == '=')
        olen--;

    uint8_t *output = (uint8_t *)malloc(olen > 0 ? olen : 1);
    if (!output)
        return NULL;

    size_t i = 0, j = 0;
    while (i < len) {
        int a = vscn_base64_digit_value(data[i++]);
        int b = vscn_base64_digit_value(data[i++]);
        int c = vscn_base64_digit_value(data[i++]);
        int d = vscn_base64_digit_value(data[i++]);

        if (a < 0 || b < 0 || c == -1 || d == -1) {
            free(output);
            return NULL;
        }

        if (c == -2)
            c = 0;
        if (d == -2)
            d = 0;

        {
            uint32_t triple =
                ((uint32_t)a << 18) | ((uint32_t)b << 12) | ((uint32_t)c << 6) | (uint32_t)d;
            if (j < olen)
                output[j++] = (uint8_t)((triple >> 16) & 0xFF);
            if (j < olen)
                output[j++] = (uint8_t)((triple >> 8) & 0xFF);
            if (j < olen)
                output[j++] = (uint8_t)(triple & 0xFF);
        }
    }

    if (out_len)
        *out_len = olen;
    return output;
}

static void vscn_free_ptr_table(vscn_ptr_table_t *table) {
    if (!table)
        return;
    free(table->items);
    table->items = NULL;
    table->count = 0;
    table->capacity = 0;
}

static void vscn_release_loaded_refs(void **items, int count) {
    if (!items)
        return;
    for (int i = 0; i < count; i++) {
        void *tmp = items[i];
        scene3d_release_ref(&tmp);
    }
    free(items);
}

static int vscn_ptr_table_index_or_add(vscn_ptr_table_t *table, void *item) {
    if (!table || !item)
        return -1;
    for (int32_t i = 0; i < table->count; i++) {
        if (table->items[i] == item)
            return i;
    }
    if (table->count >= table->capacity) {
        int32_t new_cap = table->capacity == 0 ? 8 : table->capacity * 2;
        void **new_items = (void **)realloc(table->items, (size_t)new_cap * sizeof(void *));
        if (!new_items)
            return -1;
        table->items = new_items;
        table->capacity = new_cap;
    }
    table->items[table->count] = item;
    table->count++;
    return table->count - 1;
}

static void *vjson_get(void *obj, const char *key) {
    if (!obj || !key)
        return NULL;
    return rt_map_get(obj, rt_const_cstr(key));
}

static int64_t vjson_len(void *seq) {
    return seq ? rt_seq_len(seq) : 0;
}

static int64_t vjson_value_i64(void *value, int64_t def) {
    if (!value)
        return def;
    switch (rt_box_type(value)) {
        case 0:
            return rt_unbox_i64(value);
        case 1:
            return (int64_t)rt_unbox_f64(value);
        case 2:
            return rt_unbox_i1(value);
        default:
            return def;
    }
}

static double vjson_value_f64(void *value, double def) {
    if (!value)
        return def;
    switch (rt_box_type(value)) {
        case 0:
            return (double)rt_unbox_i64(value);
        case 1:
            return rt_unbox_f64(value);
        case 2:
            return (double)rt_unbox_i1(value);
        default:
            return def;
    }
}

static int64_t vjson_i64(void *obj, const char *key, int64_t def) {
    void *value = vjson_get(obj, key);
    return vjson_value_i64(value, def);
}

static double vjson_f64(void *obj, const char *key, double def) {
    void *value = vjson_get(obj, key);
    return vjson_value_f64(value, def);
}

static int8_t vjson_bool(void *obj, const char *key, int8_t def) {
    void *value = vjson_get(obj, key);
    return value ? (vjson_value_i64(value, def) ? 1 : 0) : def;
}

static rt_string vjson_string_value(void *obj, const char *key) {
    void *value = vjson_get(obj, key);
    return (rt_string)value;
}

static const char *vjson_cstr(void *obj, const char *key) {
    rt_string value = vjson_string_value(obj, key);
    return value ? rt_string_cstr(value) : NULL;
}

static double vjson_arr_f64(void *arr, int64_t index, double def) {
    if (!arr || index < 0 || index >= vjson_len(arr))
        return def;
    return vjson_value_f64(rt_seq_get(arr, index), def);
}

static int64_t vjson_arr_i64(void *arr, int64_t index, int64_t def) {
    if (!arr || index < 0 || index >= vjson_len(arr))
        return def;
    return vjson_value_i64(rt_seq_get(arr, index), def);
}

static void vscn_make_indent(char *indent, size_t indent_cap, int depth) {
    size_t count;
    if (!indent || indent_cap == 0)
        return;
    count = (size_t)(depth * 2);
    if (count >= indent_cap)
        count = indent_cap - 1;
    memset(indent, ' ', count);
    indent[count] = '\0';
}

static int vscn_append_raw(char **buf, size_t *len, size_t *cap, const char *src, size_t src_len) {
    char *nb;

    if (!buf || !len || !cap || (!src && src_len > 0))
        return 0;
    while (*len + src_len + 1 > *cap) {
        *cap = (*cap == 0) ? 4096 : *cap * 2;
        nb = (char *)realloc(*buf, *cap);
        if (!nb)
            return 0;
        *buf = nb;
    }
    if (src_len > 0)
        memcpy(*buf + *len, src, src_len);
    *len += src_len;
    (*buf)[*len] = '\0';
    return 1;
}

/// @brief Append a formatted string to a dynamic buffer.
__attribute__((format(printf, 4, 5)))
static int vscn_append(char **buf, size_t *len, size_t *cap, const char *fmt, ...) {
    va_list ap;
    char *nb;
    va_start(ap, fmt);
    int needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (needed < 0)
        return 0;
    while (*len + (size_t)needed + 1 > *cap) {
        *cap = (*cap == 0) ? 4096 : *cap * 2;
        nb = (char *)realloc(*buf, *cap);
        if (!nb)
            return 0;
        *buf = nb;
    }
    va_start(ap, fmt);
    vsnprintf(*buf + *len, *cap - *len, fmt, ap);
    va_end(ap);
    *len += (size_t)needed;
    return 1;
}

static int vscn_append_json_string(char **buf, size_t *len, size_t *cap, const char *text) {
    if (!vscn_append_raw(buf, len, cap, "\"", 1))
        return 0;
    if (text) {
        for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
            char unicode_escape[7];
            switch (*p) {
                case '\"':
                    if (!vscn_append_raw(buf, len, cap, "\\\"", 2))
                        return 0;
                    break;
                case '\\':
                    if (!vscn_append_raw(buf, len, cap, "\\\\", 2))
                        return 0;
                    break;
                case '\b':
                    if (!vscn_append_raw(buf, len, cap, "\\b", 2))
                        return 0;
                    break;
                case '\f':
                    if (!vscn_append_raw(buf, len, cap, "\\f", 2))
                        return 0;
                    break;
                case '\n':
                    if (!vscn_append_raw(buf, len, cap, "\\n", 2))
                        return 0;
                    break;
                case '\r':
                    if (!vscn_append_raw(buf, len, cap, "\\r", 2))
                        return 0;
                    break;
                case '\t':
                    if (!vscn_append_raw(buf, len, cap, "\\t", 2))
                        return 0;
                    break;
                default:
                    if (*p < 0x20u) {
                        snprintf(unicode_escape, sizeof(unicode_escape), "\\u%04x", (unsigned)*p);
                        if (!vscn_append_raw(buf, len, cap, unicode_escape, 6))
                            return 0;
                    } else if (!vscn_append_raw(buf, len, cap, (const char *)p, 1)) {
                        return 0;
                    }
                    break;
            }
        }
    }
    return vscn_append_raw(buf, len, cap, "\"", 1);
}

static void vscn_collect_material_assets(rt_material3d *material, vscn_save_context_t *ctx) {
    if (!material || !ctx)
        return;
    if (material->texture)
        vscn_ptr_table_index_or_add(&ctx->textures, material->texture);
    if (material->normal_map)
        vscn_ptr_table_index_or_add(&ctx->textures, material->normal_map);
    if (material->specular_map)
        vscn_ptr_table_index_or_add(&ctx->textures, material->specular_map);
    if (material->emissive_map)
        vscn_ptr_table_index_or_add(&ctx->textures, material->emissive_map);
    if (material->env_map) {
        rt_cubemap3d *cubemap = (rt_cubemap3d *)material->env_map;
        vscn_ptr_table_index_or_add(&ctx->cubemaps, cubemap);
        for (int i = 0; i < 6; i++) {
            if (cubemap->faces[i])
                vscn_ptr_table_index_or_add(&ctx->textures, cubemap->faces[i]);
        }
    }
}

static void vscn_collect_node_assets(rt_scene_node3d *node, vscn_save_context_t *ctx) {
    if (!node || !ctx)
        return;
    if (node->mesh)
        vscn_ptr_table_index_or_add(&ctx->meshes, node->mesh);
    if (node->material) {
        vscn_ptr_table_index_or_add(&ctx->materials, node->material);
        vscn_collect_material_assets((rt_material3d *)node->material, ctx);
    }
    for (int32_t i = 0; i < node->lod_count; i++) {
        if (node->lod_levels[i].mesh)
            vscn_ptr_table_index_or_add(&ctx->meshes, node->lod_levels[i].mesh);
    }
    for (int32_t i = 0; i < node->child_count; i++)
        vscn_collect_node_assets(node->children[i], ctx);
}

static int vscn_serialize_texture(rt_pixels_impl *pixels,
                                  char **buf,
                                  size_t *len,
                                  size_t *cap,
                                  int depth) {
    char indent[64];
    size_t pixel_count;
    size_t rgba_len;
    uint8_t *rgba = NULL;
    char *rgba_base64 = NULL;

    if (!pixels)
        return 0;

    vscn_make_indent(indent, sizeof(indent), depth);
    pixel_count = (size_t)(pixels->width > 0 ? pixels->width : 0) *
                  (size_t)(pixels->height > 0 ? pixels->height : 0);
    rgba_len = pixel_count * 4;
    rgba = (uint8_t *)malloc(rgba_len > 0 ? rgba_len : 1);
    if (!rgba)
        return 0;

    for (size_t i = 0; i < pixel_count; i++) {
        uint32_t px = pixels->data[i];
        rgba[i * 4 + 0] = (uint8_t)((px >> 24) & 0xFF);
        rgba[i * 4 + 1] = (uint8_t)((px >> 16) & 0xFF);
        rgba[i * 4 + 2] = (uint8_t)((px >> 8) & 0xFF);
        rgba[i * 4 + 3] = (uint8_t)(px & 0xFF);
    }

    rgba_base64 = vscn_base64_encode(rgba, rgba_len, NULL);
    free(rgba);
    if (!rgba_base64)
        return 0;

    {
        int ok = vscn_append(buf,
                             len,
                             cap,
                             "%s{\"width\": %lld, \"height\": %lld, \"rgbaBase64\": ",
                             indent,
                             (long long)pixels->width,
                             (long long)pixels->height) &&
                 vscn_append_json_string(buf, len, cap, rgba_base64) &&
                 vscn_append(buf, len, cap, "}");
        free(rgba_base64);
        return ok;
    }
}

static int vscn_serialize_cubemap(rt_cubemap3d *cubemap,
                                  vscn_save_context_t *ctx,
                                  char **buf,
                                  size_t *len,
                                  size_t *cap,
                                  int depth) {
    char indent[64];
    if (!cubemap || !ctx)
        return 0;
    vscn_make_indent(indent, sizeof(indent), depth);
    if (!vscn_append(buf, len, cap, "%s{\"faces\": [", indent))
        return 0;
    for (int i = 0; i < 6; i++) {
        int index = vscn_ptr_table_index_or_add(&ctx->textures, cubemap->faces[i]);
        if (!vscn_append(buf, len, cap, "%s%d", i == 0 ? "" : ", ", index))
            return 0;
    }
    return vscn_append(buf, len, cap, "]}");
}

static int vscn_serialize_material(rt_material3d *material,
                                   vscn_save_context_t *ctx,
                                   char **buf,
                                   size_t *len,
                                   size_t *cap,
                                   int depth) {
    char indent[64];
    if (!material || !ctx)
        return 0;
    vscn_make_indent(indent, sizeof(indent), depth);

    if (!vscn_append(buf,
                     len,
                     cap,
                     "%s{\"diffuse\": [%.17g, %.17g, %.17g, %.17g], "
                     "\"specular\": [%.17g, %.17g, %.17g], "
                     "\"shininess\": %.17g, "
                     "\"emissive\": [%.17g, %.17g, %.17g], "
                     "\"alpha\": %.17g, "
                     "\"reflectivity\": %.17g, "
                     "\"unlit\": %s, "
                     "\"shadingModel\": %d, "
                     "\"texture\": %d, "
                     "\"normalMap\": %d, "
                     "\"specularMap\": %d, "
                     "\"emissiveMap\": %d, "
                     "\"envMap\": %d, "
                     "\"customParams\": [",
                     indent,
                     material->diffuse[0],
                     material->diffuse[1],
                     material->diffuse[2],
                     material->diffuse[3],
                     material->specular[0],
                     material->specular[1],
                     material->specular[2],
                     material->shininess,
                     material->emissive[0],
                     material->emissive[1],
                     material->emissive[2],
                     material->alpha,
                     material->reflectivity,
                     material->unlit ? "true" : "false",
                     material->shading_model,
                     vscn_ptr_table_index_or_add(&ctx->textures, material->texture),
                     vscn_ptr_table_index_or_add(&ctx->textures, material->normal_map),
                     vscn_ptr_table_index_or_add(&ctx->textures, material->specular_map),
                     vscn_ptr_table_index_or_add(&ctx->textures, material->emissive_map),
                     vscn_ptr_table_index_or_add(&ctx->cubemaps, material->env_map))) {
        return 0;
    }

    for (int i = 0; i < 8; i++) {
        if (!vscn_append(buf,
                         len,
                         cap,
                         "%s%.17g",
                         i == 0 ? "" : ", ",
                         material->custom_params[i])) {
            return 0;
        }
    }
    return vscn_append(buf, len, cap, "]}");
}

static int vscn_serialize_mesh(rt_mesh3d *mesh,
                               char **buf,
                               size_t *len,
                               size_t *cap,
                               int depth) {
    char indent[64];
    size_t vertex_bytes_len = (size_t)mesh->vertex_count * sizeof(vgfx3d_vertex_t);
    size_t index_bytes_len = (size_t)mesh->index_count * sizeof(uint32_t);
    char *vertex_base64 = NULL;
    char *index_base64 = NULL;

    if (!mesh)
        return 0;
    vscn_make_indent(indent, sizeof(indent), depth);
    vertex_base64 = vscn_base64_encode((const uint8_t *)mesh->vertices, vertex_bytes_len, NULL);
    index_base64 = vscn_base64_encode((const uint8_t *)mesh->indices, index_bytes_len, NULL);
    if (!vertex_base64 || !index_base64) {
        free(vertex_base64);
        free(index_base64);
        return 0;
    }

    {
        int ok = vscn_append(buf,
                             len,
                             cap,
                             "%s{\"vertexFormat\": \"vgfx3d_vertex_le_v1\", "
                             "\"vertexCount\": %u, "
                             "\"indexCount\": %u, "
                             "\"boneCount\": %d, "
                             "\"verticesBase64\": ",
                             indent,
                             mesh->vertex_count,
                             mesh->index_count,
                             mesh->bone_count) &&
                 vscn_append_json_string(buf, len, cap, vertex_base64) &&
                 vscn_append(buf, len, cap, ", \"indicesBase64\": ") &&
                 vscn_append_json_string(buf, len, cap, index_base64) &&
                 vscn_append(buf, len, cap, "}");
        free(vertex_base64);
        free(index_base64);
        return ok;
    }
}

static int vscn_serialize_node(rt_scene_node3d *node,
                               vscn_save_context_t *ctx,
                               char **buf,
                               size_t *len,
                               size_t *cap,
                               int depth) {
    if (!node)
        return 1;
    char indent[64];
    vscn_make_indent(indent, sizeof(indent), depth);

    const char *name = node->name ? rt_string_cstr(node->name) : "node";
    if (!name)
        name = "node";

    if (!vscn_append(buf, len, cap, "%s{\n", indent) ||
        !vscn_append(buf, len, cap, "%s  \"name\": ", indent) ||
        !vscn_append_json_string(buf, len, cap, name) ||
        !vscn_append(buf, len, cap, ",\n%s  \"position\": [%.6f, %.6f, %.6f],\n", indent,
                     node->position[0], node->position[1], node->position[2]) ||
        !vscn_append(buf, len, cap, "%s  \"rotation\": [%.6f, %.6f, %.6f, %.6f],\n", indent,
                     node->rotation[0], node->rotation[1], node->rotation[2], node->rotation[3]) ||
        !vscn_append(buf, len, cap, "%s  \"scale\": [%.6f, %.6f, %.6f],\n", indent,
                     node->scale_xyz[0], node->scale_xyz[1], node->scale_xyz[2]) ||
        !vscn_append(buf, len, cap, "%s  \"visible\": %s,\n", indent,
                     node->visible ? "true" : "false") ||
        !vscn_append(buf, len, cap, "%s  \"hasMesh\": %s,\n", indent,
                     node->mesh ? "true" : "false") ||
        !vscn_append(buf, len, cap, "%s  \"hasMaterial\": %s,\n", indent,
                     node->material ? "true" : "false") ||
        !vscn_append(buf,
                     len,
                     cap,
                     "%s  \"mesh\": %d,\n%s  \"material\": %d",
                     indent,
                     vscn_ptr_table_index_or_add(&ctx->meshes, node->mesh),
                     indent,
                     vscn_ptr_table_index_or_add(&ctx->materials, node->material))) {
        return 0;
    }

    if (node->lod_count > 0) {
        if (!vscn_append(buf, len, cap, ",\n%s  \"lod\": [\n", indent))
            return 0;
        for (int32_t i = 0; i < node->lod_count; i++) {
            if (!vscn_append(buf,
                             len,
                             cap,
                             "%s    {\"distance\": %.6f, \"hasMesh\": %s, \"mesh\": %d}%s\n",
                             indent,
                             node->lod_levels[i].distance,
                             node->lod_levels[i].mesh ? "true" : "false",
                             vscn_ptr_table_index_or_add(&ctx->meshes, node->lod_levels[i].mesh),
                             i + 1 < node->lod_count ? "," : "")) {
                return 0;
            }
        }
        if (!vscn_append(buf, len, cap, "%s  ]", indent))
            return 0;
    }

    if (node->child_count > 0) {
        if (!vscn_append(buf, len, cap, ",\n%s  \"children\": [\n", indent))
            return 0;
        for (int32_t i = 0; i < node->child_count; i++) {
            if (!vscn_serialize_node(node->children[i], ctx, buf, len, cap, depth + 2))
                return 0;
            if (i < node->child_count - 1 && !vscn_append(buf, len, cap, ","))
                return 0;
            if (!vscn_append(buf, len, cap, "\n"))
                return 0;
        }
        if (!vscn_append(buf, len, cap, "%s  ]", indent))
            return 0;
    }

    return vscn_append(buf, len, cap, "\n%s}", indent);
}

static rt_pixels_impl *vscn_parse_texture(void *texture_obj) {
    int64_t width;
    int64_t height;
    const char *rgba_b64;
    size_t rgba_len = 0;
    uint8_t *rgba = NULL;
    rt_pixels_impl *pixels = NULL;

    if (!texture_obj)
        return NULL;
    width = vjson_i64(texture_obj, "width", 0);
    height = vjson_i64(texture_obj, "height", 0);
    rgba_b64 = vjson_cstr(texture_obj, "rgbaBase64");
    if (width < 0 || height < 0)
        return NULL;
    if (!rgba_b64)
        rgba_b64 = "";

    rgba = vscn_base64_decode(rgba_b64, strlen(rgba_b64), &rgba_len);
    if (!rgba)
        return NULL;
    if (rgba_len != (size_t)width * (size_t)height * 4) {
        free(rgba);
        return NULL;
    }

    pixels = (rt_pixels_impl *)rt_pixels_new(width, height);
    if (!pixels) {
        free(rgba);
        return NULL;
    }

    for (size_t i = 0; i < (size_t)width * (size_t)height; i++) {
        pixels->data[i] = ((uint32_t)rgba[i * 4 + 0] << 24) |
                          ((uint32_t)rgba[i * 4 + 1] << 16) |
                          ((uint32_t)rgba[i * 4 + 2] << 8) |
                          (uint32_t)rgba[i * 4 + 3];
    }
    free(rgba);
    pixels_touch(pixels);
    return pixels;
}

static rt_cubemap3d *vscn_parse_cubemap(void *cubemap_obj, rt_pixels_impl **textures, int tex_count) {
    void *faces_arr;
    void *faces[6];

    if (!cubemap_obj)
        return NULL;
    faces_arr = vjson_get(cubemap_obj, "faces");
    if (!faces_arr || vjson_len(faces_arr) < 6)
        return NULL;

    for (int i = 0; i < 6; i++) {
        int64_t index = vjson_arr_i64(faces_arr, i, -1);
        if (index < 0 || index >= tex_count || !textures[index])
            return NULL;
        faces[i] = textures[index];
    }

    return (rt_cubemap3d *)rt_cubemap3d_new(faces[0], faces[1], faces[2], faces[3], faces[4], faces[5]);
}

static rt_material3d *vscn_parse_material(void *material_obj,
                                          rt_pixels_impl **textures,
                                          int tex_count,
                                          rt_cubemap3d **cubemaps,
                                          int cubemap_count) {
    rt_material3d *material;
    void *arr;

    if (!material_obj)
        return NULL;
    material = (rt_material3d *)rt_material3d_new();
    if (!material)
        return NULL;

    arr = vjson_get(material_obj, "diffuse");
    if (arr && vjson_len(arr) >= 4) {
        material->diffuse[0] = vjson_arr_f64(arr, 0, material->diffuse[0]);
        material->diffuse[1] = vjson_arr_f64(arr, 1, material->diffuse[1]);
        material->diffuse[2] = vjson_arr_f64(arr, 2, material->diffuse[2]);
        material->diffuse[3] = vjson_arr_f64(arr, 3, material->diffuse[3]);
    }

    arr = vjson_get(material_obj, "specular");
    if (arr && vjson_len(arr) >= 3) {
        material->specular[0] = vjson_arr_f64(arr, 0, material->specular[0]);
        material->specular[1] = vjson_arr_f64(arr, 1, material->specular[1]);
        material->specular[2] = vjson_arr_f64(arr, 2, material->specular[2]);
    }

    arr = vjson_get(material_obj, "emissive");
    if (arr && vjson_len(arr) >= 3) {
        material->emissive[0] = vjson_arr_f64(arr, 0, material->emissive[0]);
        material->emissive[1] = vjson_arr_f64(arr, 1, material->emissive[1]);
        material->emissive[2] = vjson_arr_f64(arr, 2, material->emissive[2]);
    }

    material->shininess = vjson_f64(material_obj, "shininess", material->shininess);
    material->alpha = vjson_f64(material_obj, "alpha", material->alpha);
    material->reflectivity = vjson_f64(material_obj, "reflectivity", material->reflectivity);
    material->unlit = vjson_bool(material_obj, "unlit", material->unlit);
    material->shading_model = (int32_t)vjson_i64(material_obj, "shadingModel", material->shading_model);

    arr = vjson_get(material_obj, "customParams");
    for (int i = 0; i < 8; i++) {
        if (arr && i < vjson_len(arr))
            material->custom_params[i] = vjson_arr_f64(arr, i, material->custom_params[i]);
    }

    {
        int64_t index = vjson_i64(material_obj, "texture", -1);
        if (index >= 0 && index < tex_count && textures[index])
            rt_material3d_set_texture(material, textures[index]);
    }
    {
        int64_t index = vjson_i64(material_obj, "normalMap", -1);
        if (index >= 0 && index < tex_count && textures[index])
            rt_material3d_set_normal_map(material, textures[index]);
    }
    {
        int64_t index = vjson_i64(material_obj, "specularMap", -1);
        if (index >= 0 && index < tex_count && textures[index])
            rt_material3d_set_specular_map(material, textures[index]);
    }
    {
        int64_t index = vjson_i64(material_obj, "emissiveMap", -1);
        if (index >= 0 && index < tex_count && textures[index])
            rt_material3d_set_emissive_map(material, textures[index]);
    }
    {
        int64_t index = vjson_i64(material_obj, "envMap", -1);
        if (index >= 0 && index < cubemap_count && cubemaps[index])
            rt_material3d_set_env_map(material, cubemaps[index]);
    }

    return material;
}

static rt_mesh3d *vscn_parse_mesh(void *mesh_obj) {
    rt_mesh3d *mesh;
    const char *vertex_format;
    const char *vertices_b64;
    const char *indices_b64;
    uint32_t vertex_count;
    uint32_t index_count;
    size_t vertices_len = 0;
    size_t indices_len = 0;
    uint8_t *vertices_raw = NULL;
    uint8_t *indices_raw = NULL;

    if (!mesh_obj)
        return NULL;

    vertex_format = vjson_cstr(mesh_obj, "vertexFormat");
    if (vertex_format && strcmp(vertex_format, "vgfx3d_vertex_le_v1") != 0)
        return NULL;

    vertex_count = (uint32_t)vjson_i64(mesh_obj, "vertexCount", 0);
    index_count = (uint32_t)vjson_i64(mesh_obj, "indexCount", 0);
    vertices_b64 = vjson_cstr(mesh_obj, "verticesBase64");
    indices_b64 = vjson_cstr(mesh_obj, "indicesBase64");
    if (!vertices_b64)
        vertices_b64 = "";
    if (!indices_b64)
        indices_b64 = "";

    vertices_raw = vscn_base64_decode(vertices_b64, strlen(vertices_b64), &vertices_len);
    indices_raw = vscn_base64_decode(indices_b64, strlen(indices_b64), &indices_len);
    if (!vertices_raw || !indices_raw) {
        free(vertices_raw);
        free(indices_raw);
        return NULL;
    }
    if (vertices_len != (size_t)vertex_count * sizeof(vgfx3d_vertex_t) ||
        indices_len != (size_t)index_count * sizeof(uint32_t)) {
        free(vertices_raw);
        free(indices_raw);
        return NULL;
    }

    mesh = (rt_mesh3d *)rt_mesh3d_new();
    if (!mesh) {
        free(vertices_raw);
        free(indices_raw);
        return NULL;
    }

    if (vertex_count > 0) {
        vgfx3d_vertex_t *vertices = (vgfx3d_vertex_t *)malloc((size_t)vertex_count * sizeof(vgfx3d_vertex_t));
        if (!vertices) {
            free(vertices_raw);
            free(indices_raw);
            return mesh;
        }
        memcpy(vertices, vertices_raw, (size_t)vertex_count * sizeof(vgfx3d_vertex_t));
        free(mesh->vertices);
        mesh->vertices = vertices;
        mesh->vertex_count = vertex_count;
        mesh->vertex_capacity = vertex_count;
    } else {
        mesh->vertex_count = 0;
    }

    if (index_count > 0) {
        uint32_t *indices = (uint32_t *)malloc((size_t)index_count * sizeof(uint32_t));
        if (!indices) {
            free(vertices_raw);
            free(indices_raw);
            return mesh;
        }
        memcpy(indices, indices_raw, (size_t)index_count * sizeof(uint32_t));
        free(mesh->indices);
        mesh->indices = indices;
        mesh->index_count = index_count;
        mesh->index_capacity = index_count;
    } else {
        mesh->index_count = 0;
    }

    mesh->bone_count = (int32_t)vjson_i64(mesh_obj, "boneCount", 0);
    mesh->bone_palette = NULL;
    mesh->prev_bone_palette = NULL;
    mesh->morph_deltas = NULL;
    mesh->morph_normal_deltas = NULL;
    mesh->morph_weights = NULL;
    mesh->prev_morph_weights = NULL;
    mesh->morph_shape_count = 0;
    mesh->morph_targets_ref = NULL;
    mesh->geometry_revision = 1;
    mesh->bounds_dirty = 1;
    rt_mesh3d_refresh_bounds(mesh);

    free(vertices_raw);
    free(indices_raw);
    return mesh;
}

static rt_scene_node3d *vscn_parse_node(void *node_obj,
                                        rt_mesh3d **meshes,
                                        int mesh_count,
                                        rt_material3d **materials,
                                        int material_count) {
    rt_scene_node3d *node;
    void *arr;
    rt_string name;

    if (!node_obj)
        return NULL;
    node = (rt_scene_node3d *)rt_scene_node3d_new();
    if (!node)
        return NULL;

    name = vjson_string_value(node_obj, "name");
    if (name)
        rt_scene_node3d_set_name(node, name);

    arr = vjson_get(node_obj, "position");
    if (arr && vjson_len(arr) >= 3) {
        node->position[0] = vjson_arr_f64(arr, 0, node->position[0]);
        node->position[1] = vjson_arr_f64(arr, 1, node->position[1]);
        node->position[2] = vjson_arr_f64(arr, 2, node->position[2]);
    }

    arr = vjson_get(node_obj, "rotation");
    if (arr && vjson_len(arr) >= 4) {
        node->rotation[0] = vjson_arr_f64(arr, 0, node->rotation[0]);
        node->rotation[1] = vjson_arr_f64(arr, 1, node->rotation[1]);
        node->rotation[2] = vjson_arr_f64(arr, 2, node->rotation[2]);
        node->rotation[3] = vjson_arr_f64(arr, 3, node->rotation[3]);
    }

    arr = vjson_get(node_obj, "scale");
    if (arr && vjson_len(arr) >= 3) {
        node->scale_xyz[0] = vjson_arr_f64(arr, 0, node->scale_xyz[0]);
        node->scale_xyz[1] = vjson_arr_f64(arr, 1, node->scale_xyz[1]);
        node->scale_xyz[2] = vjson_arr_f64(arr, 2, node->scale_xyz[2]);
    }

    node->visible = vjson_bool(node_obj, "visible", 1);
    node->world_dirty = 1;

    {
        int64_t mesh_index = vjson_i64(node_obj, "mesh", -1);
        if (mesh_index >= 0 && mesh_index < mesh_count && meshes[mesh_index])
            rt_scene_node3d_set_mesh(node, meshes[mesh_index]);
    }
    {
        int64_t material_index = vjson_i64(node_obj, "material", -1);
        if (material_index >= 0 && material_index < material_count && materials[material_index])
            rt_scene_node3d_set_material(node, materials[material_index]);
    }

    arr = vjson_get(node_obj, "lod");
    if (arr) {
        for (int64_t i = 0; i < vjson_len(arr); i++) {
            void *lod_obj = rt_seq_get(arr, i);
            int64_t mesh_index = vjson_i64(lod_obj, "mesh", -1);
            if (mesh_index >= 0 && mesh_index < mesh_count && meshes[mesh_index]) {
                rt_scene_node3d_add_lod(node,
                                        vjson_f64(lod_obj, "distance", 0.0),
                                        meshes[mesh_index]);
            }
        }
    }

    arr = vjson_get(node_obj, "children");
    if (arr) {
        for (int64_t i = 0; i < vjson_len(arr); i++) {
            rt_scene_node3d *child =
                vscn_parse_node(rt_seq_get(arr, i), meshes, mesh_count, materials, material_count);
            if (child) {
                rt_scene_node3d_add_child(node, child);
                {
                    void *tmp = child;
                    scene3d_release_ref(&tmp);
                }
            }
        }
    }

    return node;
}

int64_t rt_scene3d_save(void *scene_obj, rt_string path) {
    if (!scene_obj || !path)
        return 0;
    rt_scene3d *scene = (rt_scene3d *)scene_obj;
    if (!scene->root)
        return 0;

    const char *filepath = rt_string_cstr(path);
    if (!filepath)
        return 0;

    vscn_save_context_t ctx = {0};
    char *buf = NULL;
    size_t len = 0, cap = 0;

    for (int32_t i = 0; i < scene->root->child_count; i++)
        vscn_collect_node_assets(scene->root->children[i], &ctx);

    if (!vscn_append(&buf, &len, &cap, "{\n") ||
        !vscn_append(&buf, &len, &cap, "  \"format\": \"vscn\",\n") ||
        !vscn_append(&buf, &len, &cap, "  \"version\": 2,\n") ||
        !vscn_append(&buf, &len, &cap, "  \"textures\": [\n")) {
        vscn_free_ptr_table(&ctx.meshes);
        vscn_free_ptr_table(&ctx.materials);
        vscn_free_ptr_table(&ctx.textures);
        vscn_free_ptr_table(&ctx.cubemaps);
        free(buf);
        return 0;
    }

    for (int32_t i = 0; i < ctx.textures.count; i++) {
        if (!vscn_serialize_texture((rt_pixels_impl *)ctx.textures.items[i], &buf, &len, &cap, 2) ||
            (i < ctx.textures.count - 1 && !vscn_append(&buf, &len, &cap, ",")) ||
            !vscn_append(&buf, &len, &cap, "\n")) {
            vscn_free_ptr_table(&ctx.meshes);
            vscn_free_ptr_table(&ctx.materials);
            vscn_free_ptr_table(&ctx.textures);
            vscn_free_ptr_table(&ctx.cubemaps);
            free(buf);
            return 0;
        }
    }

    if (!vscn_append(&buf, &len, &cap, "  ],\n") ||
        !vscn_append(&buf, &len, &cap, "  \"cubemaps\": [\n")) {
        vscn_free_ptr_table(&ctx.meshes);
        vscn_free_ptr_table(&ctx.materials);
        vscn_free_ptr_table(&ctx.textures);
        vscn_free_ptr_table(&ctx.cubemaps);
        free(buf);
        return 0;
    }

    for (int32_t i = 0; i < ctx.cubemaps.count; i++) {
        if (!vscn_serialize_cubemap((rt_cubemap3d *)ctx.cubemaps.items[i],
                                    &ctx,
                                    &buf,
                                    &len,
                                    &cap,
                                    2) ||
            (i < ctx.cubemaps.count - 1 && !vscn_append(&buf, &len, &cap, ",")) ||
            !vscn_append(&buf, &len, &cap, "\n")) {
            vscn_free_ptr_table(&ctx.meshes);
            vscn_free_ptr_table(&ctx.materials);
            vscn_free_ptr_table(&ctx.textures);
            vscn_free_ptr_table(&ctx.cubemaps);
            free(buf);
            return 0;
        }
    }

    if (!vscn_append(&buf, &len, &cap, "  ],\n") ||
        !vscn_append(&buf, &len, &cap, "  \"materials\": [\n")) {
        vscn_free_ptr_table(&ctx.meshes);
        vscn_free_ptr_table(&ctx.materials);
        vscn_free_ptr_table(&ctx.textures);
        vscn_free_ptr_table(&ctx.cubemaps);
        free(buf);
        return 0;
    }

    for (int32_t i = 0; i < ctx.materials.count; i++) {
        if (!vscn_serialize_material((rt_material3d *)ctx.materials.items[i],
                                     &ctx,
                                     &buf,
                                     &len,
                                     &cap,
                                     2) ||
            (i < ctx.materials.count - 1 && !vscn_append(&buf, &len, &cap, ",")) ||
            !vscn_append(&buf, &len, &cap, "\n")) {
            vscn_free_ptr_table(&ctx.meshes);
            vscn_free_ptr_table(&ctx.materials);
            vscn_free_ptr_table(&ctx.textures);
            vscn_free_ptr_table(&ctx.cubemaps);
            free(buf);
            return 0;
        }
    }

    if (!vscn_append(&buf, &len, &cap, "  ],\n") ||
        !vscn_append(&buf, &len, &cap, "  \"meshes\": [\n")) {
        vscn_free_ptr_table(&ctx.meshes);
        vscn_free_ptr_table(&ctx.materials);
        vscn_free_ptr_table(&ctx.textures);
        vscn_free_ptr_table(&ctx.cubemaps);
        free(buf);
        return 0;
    }

    for (int32_t i = 0; i < ctx.meshes.count; i++) {
        if (!vscn_serialize_mesh((rt_mesh3d *)ctx.meshes.items[i], &buf, &len, &cap, 2) ||
            (i < ctx.meshes.count - 1 && !vscn_append(&buf, &len, &cap, ",")) ||
            !vscn_append(&buf, &len, &cap, "\n")) {
            vscn_free_ptr_table(&ctx.meshes);
            vscn_free_ptr_table(&ctx.materials);
            vscn_free_ptr_table(&ctx.textures);
            vscn_free_ptr_table(&ctx.cubemaps);
            free(buf);
            return 0;
        }
    }

    if (!vscn_append(&buf, &len, &cap, "  ],\n") ||
        !vscn_append(&buf, &len, &cap, "  \"nodes\": [\n")) {
        vscn_free_ptr_table(&ctx.meshes);
        vscn_free_ptr_table(&ctx.materials);
        vscn_free_ptr_table(&ctx.textures);
        vscn_free_ptr_table(&ctx.cubemaps);
        free(buf);
        return 0;
    }

    // Serialize root's children (root itself is implicit)
    for (int32_t i = 0; i < scene->root->child_count; i++) {
        if (!vscn_serialize_node(scene->root->children[i], &ctx, &buf, &len, &cap, 2) ||
            (i < scene->root->child_count - 1 && !vscn_append(&buf, &len, &cap, ",")) ||
            !vscn_append(&buf, &len, &cap, "\n")) {
            vscn_free_ptr_table(&ctx.meshes);
            vscn_free_ptr_table(&ctx.materials);
            vscn_free_ptr_table(&ctx.textures);
            vscn_free_ptr_table(&ctx.cubemaps);
            free(buf);
            return 0;
        }
    }

    if (!vscn_append(&buf, &len, &cap, "  ]\n") || !vscn_append(&buf, &len, &cap, "}\n")) {
        vscn_free_ptr_table(&ctx.meshes);
        vscn_free_ptr_table(&ctx.materials);
        vscn_free_ptr_table(&ctx.textures);
        vscn_free_ptr_table(&ctx.cubemaps);
        free(buf);
        return 0;
    }

    FILE *f = fopen(filepath, "wb");
    if (!f) {
        vscn_free_ptr_table(&ctx.meshes);
        vscn_free_ptr_table(&ctx.materials);
        vscn_free_ptr_table(&ctx.textures);
        vscn_free_ptr_table(&ctx.cubemaps);
        free(buf);
        return 0;
    }
    size_t written = 0;
    while (written < len) {
        size_t chunk = fwrite(buf + written, 1, len - written, f);
        if (chunk == 0) {
            fclose(f);
            vscn_free_ptr_table(&ctx.meshes);
            vscn_free_ptr_table(&ctx.materials);
            vscn_free_ptr_table(&ctx.textures);
            vscn_free_ptr_table(&ctx.cubemaps);
            free(buf);
            return 0;
        }
        written += chunk;
    }
    if (fflush(f) != 0 || fclose(f) != 0) {
        vscn_free_ptr_table(&ctx.meshes);
        vscn_free_ptr_table(&ctx.materials);
        vscn_free_ptr_table(&ctx.textures);
        vscn_free_ptr_table(&ctx.cubemaps);
        free(buf);
        return 0;
    }
    vscn_free_ptr_table(&ctx.meshes);
    vscn_free_ptr_table(&ctx.materials);
    vscn_free_ptr_table(&ctx.textures);
    vscn_free_ptr_table(&ctx.cubemaps);
    free(buf);
    return 1;
}

void *rt_scene3d_load(rt_string path) {
    const char *filepath;
    FILE *f;
    char *json = NULL;
    long file_size;
    void *root;
    void *textures_arr;
    void *cubemaps_arr;
    void *materials_arr;
    void *meshes_arr;
    void *nodes_arr;
    int tex_count;
    int cubemap_count;
    int material_count;
    int mesh_count;
    rt_pixels_impl **textures = NULL;
    rt_cubemap3d **cubemaps = NULL;
    rt_material3d **materials = NULL;
    rt_mesh3d **meshes = NULL;
    rt_scene3d *scene = NULL;

    if (!path)
        return NULL;
    filepath = rt_string_cstr(path);
    if (!filepath)
        return NULL;

    f = fopen(filepath, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    file_size = ftell(f);
    if (file_size < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    json = (char *)malloc((size_t)file_size + 1);
    if (!json) {
        fclose(f);
        return NULL;
    }
    if (file_size > 0 && fread(json, 1, (size_t)file_size, f) != (size_t)file_size) {
        fclose(f);
        free(json);
        return NULL;
    }
    fclose(f);
    json[file_size] = '\0';

    root = rt_json_parse_object(rt_string_from_bytes(json, (size_t)file_size));
    free(json);
    if (!root)
        return NULL;

    textures_arr = vjson_get(root, "textures");
    cubemaps_arr = vjson_get(root, "cubemaps");
    materials_arr = vjson_get(root, "materials");
    meshes_arr = vjson_get(root, "meshes");
    nodes_arr = vjson_get(root, "nodes");

    tex_count = (int)vjson_len(textures_arr);
    cubemap_count = (int)vjson_len(cubemaps_arr);
    material_count = (int)vjson_len(materials_arr);
    mesh_count = (int)vjson_len(meshes_arr);

    if (tex_count > 0)
        textures = (rt_pixels_impl **)calloc((size_t)tex_count, sizeof(rt_pixels_impl *));
    if (cubemap_count > 0)
        cubemaps = (rt_cubemap3d **)calloc((size_t)cubemap_count, sizeof(rt_cubemap3d *));
    if (material_count > 0)
        materials = (rt_material3d **)calloc((size_t)material_count, sizeof(rt_material3d *));
    if (mesh_count > 0)
        meshes = (rt_mesh3d **)calloc((size_t)mesh_count, sizeof(rt_mesh3d *));
    if ((tex_count > 0 && !textures) || (cubemap_count > 0 && !cubemaps) ||
        (material_count > 0 && !materials) || (mesh_count > 0 && !meshes)) {
        free(textures);
        free(cubemaps);
        free(materials);
        free(meshes);
        return NULL;
    }

    for (int i = 0; i < tex_count; i++)
        textures[i] = vscn_parse_texture(rt_seq_get(textures_arr, (int64_t)i));
    for (int i = 0; i < cubemap_count; i++)
        cubemaps[i] = vscn_parse_cubemap(rt_seq_get(cubemaps_arr, (int64_t)i), textures, tex_count);
    for (int i = 0; i < material_count; i++)
        materials[i] = vscn_parse_material(rt_seq_get(materials_arr, (int64_t)i),
                                           textures,
                                           tex_count,
                                           cubemaps,
                                           cubemap_count);
    for (int i = 0; i < mesh_count; i++)
        meshes[i] = vscn_parse_mesh(rt_seq_get(meshes_arr, (int64_t)i));

    scene = (rt_scene3d *)rt_scene3d_new();
    if (!scene) {
        vscn_release_loaded_refs((void **)meshes, mesh_count);
        vscn_release_loaded_refs((void **)materials, material_count);
        vscn_release_loaded_refs((void **)cubemaps, cubemap_count);
        vscn_release_loaded_refs((void **)textures, tex_count);
        return NULL;
    }

    if (nodes_arr) {
        for (int64_t i = 0; i < vjson_len(nodes_arr); i++) {
            rt_scene_node3d *node =
                vscn_parse_node(rt_seq_get(nodes_arr, i), meshes, mesh_count, materials, material_count);
            if (node) {
                rt_scene_node3d_add_child(scene->root, node);
                {
                    void *tmp = node;
                    scene3d_release_ref(&tmp);
                }
            }
        }
    }
    scene->node_count = count_subtree(scene->root);
    scene->last_culled_count = 0;

    vscn_release_loaded_refs((void **)meshes, mesh_count);
    vscn_release_loaded_refs((void **)materials, material_count);
    vscn_release_loaded_refs((void **)cubemaps, cubemap_count);
    vscn_release_loaded_refs((void **)textures, tex_count);
    return scene;
}

#endif /* VIPER_ENABLE_GRAPHICS */
