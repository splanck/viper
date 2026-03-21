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
//   - Mesh/material/name are borrowed GC pointers (not owned by the node).
//
// Links: rt_scene3d.h, rt_quat.h, rt_mat4.h, plans/3d/12-scene-graph.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_scene3d.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "vgfx3d_frustum.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
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

/* Forward declarations for Canvas3D draw functions */
extern void rt_canvas3d_begin(void *obj, void *camera);
extern void rt_canvas3d_draw_mesh(void *obj, void *mesh, void *transform, void *material);
extern void rt_canvas3d_end(void *obj);

#define NODE_INIT_CHILDREN 4

/*==========================================================================
 * SceneNode3D internal structure
 *=========================================================================*/

typedef struct rt_scene_node3d
{
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

    /* Name (borrowed rt_string — GC-managed) */
    rt_string name;

    /* Bounding volume (Phase 13 frustum culling) */
    float aabb_min[3];
    float aabb_max[3];
    float bsphere_radius;

    /* LOD levels (F1) — sorted by distance ascending */
    struct { double distance; void *mesh; } *lod_levels;
    int32_t lod_count;
    int32_t lod_capacity;
} rt_scene_node3d;

typedef struct
{
    void *vptr;
    rt_scene_node3d *root;
    int32_t node_count;
    int32_t last_culled_count; /* from most recent Draw call */
} rt_scene3d;

/*==========================================================================
 * Helpers
 *=========================================================================*/

/// @brief Build a TRS matrix: Translate * Rotate * Scale (row-major).
/// Quaternion (x,y,z,w) is expanded inline to avoid allocating a Mat4.
static void build_trs_matrix(const double *pos, const double *quat, const double *scl, double *out)
{
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
static void mat4d_mul(const double *a, const double *b, double *out)
{
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            out[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c] +
                             a[r * 4 + 2] * b[2 * 4 + c] + a[r * 4 + 3] * b[3 * 4 + c];
}

/// @brief Recursively mark a node and all descendants as dirty.
static void mark_dirty(rt_scene_node3d *node)
{
    node->world_dirty = 1;
    for (int32_t i = 0; i < node->child_count; i++)
        mark_dirty(node->children[i]);
}

/// @brief Recompute the world matrix if dirty (recursive up to parent).
static void recompute_world_matrix(rt_scene_node3d *node)
{
    if (!node->world_dirty)
        return;

    double local[16];
    build_trs_matrix(node->position, node->rotation, node->scale_xyz, local);

    if (node->parent)
    {
        recompute_world_matrix(node->parent);
        mat4d_mul(node->parent->world_matrix, local, node->world_matrix);
    }
    else
    {
        memcpy(node->world_matrix, local, sizeof(double) * 16);
    }

    node->world_dirty = 0;
}

/// @brief Count nodes in a subtree (including the root).
static int32_t count_subtree(const rt_scene_node3d *node)
{
    int32_t n = 1;
    for (int32_t i = 0; i < node->child_count; i++)
        n += count_subtree(node->children[i]);
    return n;
}

/// @brief Recursive depth-first search by name.
extern const char *rt_string_cstr(rt_string s);

static rt_scene_node3d *find_by_name(rt_scene_node3d *node, const char *target)
{
    if (node->name)
    {
        const char *s = rt_string_cstr(node->name);
        if (s && strcmp(s, target) == 0)
            return node;
    }
    for (int32_t i = 0; i < node->child_count; i++)
    {
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
                      const float *cam_pos)
{
    if (!node->visible)
        return;

    recompute_world_matrix(node);

    int draw_self = 1;

    /* Frustum cull: test world-space AABB if the node has a mesh */
    if (frustum && node->mesh && node->bsphere_radius > 0.0f)
    {
        float world_min[3], world_max[3];
        vgfx3d_transform_aabb(
            node->aabb_min, node->aabb_max, node->world_matrix, world_min, world_max);
        if (vgfx3d_frustum_test_aabb(frustum, world_min, world_max) == 0)
        {
            draw_self = 0;
            if (culled)
                (*culled)++;
        }
    }

    if (draw_self && node->mesh && node->material)
    {
        const double *m = node->world_matrix;
        void *mat4 = rt_mat4_new(m[0],
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
        /* LOD selection: pick mesh by distance from camera */
        void *draw_mesh = node->mesh;
        if (node->lod_count > 0 && cam_pos)
        {
            float dx = (float)m[3] - cam_pos[0];
            float dy = (float)m[7] - cam_pos[1];
            float dz = (float)m[11] - cam_pos[2];
            float dist = sqrtf(dx * dx + dy * dy + dz * dz);
            for (int32_t l = node->lod_count - 1; l >= 0; l--)
            {
                if (dist >= (float)node->lod_levels[l].distance)
                {
                    draw_mesh = node->lod_levels[l].mesh;
                    break;
                }
            }
        }
        rt_canvas3d_draw_mesh(canvas3d, draw_mesh, mat4, node->material);
    }

    for (int32_t i = 0; i < node->child_count; i++)
        draw_node(node->children[i], canvas3d, frustum, culled, cam_pos);
}

/*==========================================================================
 * SceneNode3D — lifecycle
 *=========================================================================*/

static void rt_scene_node3d_finalize(void *obj)
{
    rt_scene_node3d *node = (rt_scene_node3d *)obj;
    free(node->children);
    node->children = NULL;
    node->child_count = 0;
    free(node->lod_levels);
    node->lod_levels = NULL;
    node->lod_count = 0;
}

void *rt_scene_node3d_new(void)
{
    rt_scene_node3d *node = (rt_scene_node3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_scene_node3d));
    if (!node)
    {
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

void rt_scene_node3d_set_position(void *obj, double x, double y, double z)
{
    if (!obj)
        return;
    rt_scene_node3d *n = (rt_scene_node3d *)obj;
    n->position[0] = x;
    n->position[1] = y;
    n->position[2] = z;
    mark_dirty(n);
}

void *rt_scene_node3d_get_position(void *obj)
{
    if (!obj)
        return rt_vec3_new(0, 0, 0);
    rt_scene_node3d *n = (rt_scene_node3d *)obj;
    return rt_vec3_new(n->position[0], n->position[1], n->position[2]);
}

void rt_scene_node3d_set_rotation(void *obj, void *quat)
{
    if (!obj || !quat)
        return;
    rt_scene_node3d *n = (rt_scene_node3d *)obj;
    n->rotation[0] = rt_quat_x(quat);
    n->rotation[1] = rt_quat_y(quat);
    n->rotation[2] = rt_quat_z(quat);
    n->rotation[3] = rt_quat_w(quat);
    mark_dirty(n);
}

void *rt_scene_node3d_get_rotation(void *obj)
{
    if (!obj)
        return rt_quat_new(0, 0, 0, 1);
    rt_scene_node3d *n = (rt_scene_node3d *)obj;
    return rt_quat_new(n->rotation[0], n->rotation[1], n->rotation[2], n->rotation[3]);
}

void rt_scene_node3d_set_scale(void *obj, double x, double y, double z)
{
    if (!obj)
        return;
    rt_scene_node3d *n = (rt_scene_node3d *)obj;
    n->scale_xyz[0] = x;
    n->scale_xyz[1] = y;
    n->scale_xyz[2] = z;
    mark_dirty(n);
}

void *rt_scene_node3d_get_scale(void *obj)
{
    if (!obj)
        return rt_vec3_new(1, 1, 1);
    rt_scene_node3d *n = (rt_scene_node3d *)obj;
    return rt_vec3_new(n->scale_xyz[0], n->scale_xyz[1], n->scale_xyz[2]);
}

void *rt_scene_node3d_get_world_matrix(void *obj)
{
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

void rt_scene_node3d_add_child(void *obj, void *child_obj)
{
    if (!obj || !child_obj || obj == child_obj)
        return;
    rt_scene_node3d *parent = (rt_scene_node3d *)obj;
    rt_scene_node3d *child = (rt_scene_node3d *)child_obj;

    /* Detach from previous parent if any */
    if (child->parent)
        rt_scene_node3d_remove_child(child->parent, child);

    /* Grow children array if needed */
    if (parent->child_count >= parent->child_capacity)
    {
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
    mark_dirty(child);
}

void rt_scene_node3d_remove_child(void *obj, void *child_obj)
{
    if (!obj || !child_obj)
        return;
    rt_scene_node3d *parent = (rt_scene_node3d *)obj;
    rt_scene_node3d *child = (rt_scene_node3d *)child_obj;

    for (int32_t i = 0; i < parent->child_count; i++)
    {
        if (parent->children[i] == child)
        {
            /* Shift remaining children down */
            for (int32_t j = i; j < parent->child_count - 1; j++)
                parent->children[j] = parent->children[j + 1];
            parent->child_count--;
            child->parent = NULL;
            mark_dirty(child);
            return;
        }
    }
}

int64_t rt_scene_node3d_child_count(void *obj)
{
    return obj ? ((rt_scene_node3d *)obj)->child_count : 0;
}

void *rt_scene_node3d_get_child(void *obj, int64_t index)
{
    if (!obj)
        return NULL;
    rt_scene_node3d *n = (rt_scene_node3d *)obj;
    if (index < 0 || index >= n->child_count)
        return NULL;
    return n->children[index];
}

void *rt_scene_node3d_get_parent(void *obj)
{
    return obj ? ((rt_scene_node3d *)obj)->parent : NULL;
}

void *rt_scene_node3d_find(void *obj, rt_string name)
{
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

void rt_scene_node3d_set_mesh(void *obj, void *mesh)
{
    if (!obj)
        return;
    rt_scene_node3d *n = (rt_scene_node3d *)obj;
    n->mesh = mesh;

    /* Compute object-space AABB from mesh vertices */
    if (mesh)
    {
        rt_mesh3d *m = (rt_mesh3d *)mesh;
        vgfx3d_compute_mesh_aabb(
            m->vertices, m->vertex_count, sizeof(vgfx3d_vertex_t), n->aabb_min, n->aabb_max);
        /* Bounding sphere radius = half-diagonal of AABB */
        float dx = n->aabb_max[0] - n->aabb_min[0];
        float dy = n->aabb_max[1] - n->aabb_min[1];
        float dz = n->aabb_max[2] - n->aabb_min[2];
        n->bsphere_radius = 0.5f * sqrtf(dx * dx + dy * dy + dz * dz);
    }
    else
    {
        n->aabb_min[0] = n->aabb_min[1] = n->aabb_min[2] = 0.0f;
        n->aabb_max[0] = n->aabb_max[1] = n->aabb_max[2] = 0.0f;
        n->bsphere_radius = 0.0f;
    }
}

void rt_scene_node3d_set_material(void *obj, void *material)
{
    if (obj)
        ((rt_scene_node3d *)obj)->material = material;
}

void rt_scene_node3d_set_visible(void *obj, int8_t visible)
{
    if (obj)
        ((rt_scene_node3d *)obj)->visible = visible;
}

int8_t rt_scene_node3d_get_visible(void *obj)
{
    return obj ? ((rt_scene_node3d *)obj)->visible : 0;
}

void rt_scene_node3d_set_name(void *obj, rt_string name)
{
    if (obj)
        ((rt_scene_node3d *)obj)->name = name;
}

rt_string rt_scene_node3d_get_name(void *obj)
{
    if (obj && ((rt_scene_node3d *)obj)->name)
        return ((rt_scene_node3d *)obj)->name;
    return rt_const_cstr("");
}

void *rt_scene_node3d_get_aabb_min(void *obj)
{
    if (!obj)
        return rt_vec3_new(0, 0, 0);
    rt_scene_node3d *n = (rt_scene_node3d *)obj;
    return rt_vec3_new(n->aabb_min[0], n->aabb_min[1], n->aabb_min[2]);
}

void *rt_scene_node3d_get_aabb_max(void *obj)
{
    if (!obj)
        return rt_vec3_new(0, 0, 0);
    rt_scene_node3d *n = (rt_scene_node3d *)obj;
    return rt_vec3_new(n->aabb_max[0], n->aabb_max[1], n->aabb_max[2]);
}

/*==========================================================================
 * Scene3D
 *=========================================================================*/

static void rt_scene3d_finalize(void *obj)
{
    (void)obj;
    /* Root node is GC-managed; its finalizer frees children array.
     * Scene3D itself has no heap-allocated state to free. */
}

void *rt_scene3d_new(void)
{
    rt_scene3d *s = (rt_scene3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_scene3d));
    if (!s)
    {
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

void *rt_scene3d_get_root(void *obj)
{
    return obj ? ((rt_scene3d *)obj)->root : NULL;
}

void rt_scene3d_add(void *obj, void *node)
{
    if (!obj)
        return;
    rt_scene3d *s = (rt_scene3d *)obj;
    rt_scene_node3d_add_child(s->root, node);
    s->node_count = count_subtree(s->root);
}

void rt_scene3d_remove(void *obj, void *node)
{
    if (!obj || !node)
        return;
    rt_scene3d *s = (rt_scene3d *)obj;
    rt_scene_node3d *n = (rt_scene_node3d *)node;
    if (n->parent)
        rt_scene_node3d_remove_child(n->parent, n);
    s->node_count = count_subtree(s->root);
}

void *rt_scene3d_find(void *obj, rt_string name)
{
    if (!obj || !name)
        return NULL;
    const char *str = rt_string_cstr(name);
    if (!str)
        return NULL;
    rt_scene3d *s = (rt_scene3d *)obj;
    return find_by_name(s->root, str);
}

/// @brief Helper to convert double[16] to float[16] for frustum extraction.
static void mat4_d2f_local(const double *src, float *dst)
{
    for (int i = 0; i < 16; i++)
        dst[i] = (float)src[i];
}

void rt_scene3d_draw(void *obj, void *canvas3d, void *camera)
{
    if (!obj || !canvas3d || !camera)
        return;
    rt_scene3d *s = (rt_scene3d *)obj;
    rt_camera3d *cam = (rt_camera3d *)camera;

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
    rt_canvas3d_begin(canvas3d, camera);
    float cam_pos[3] = {(float)cam->eye[0], (float)cam->eye[1], (float)cam->eye[2]};
    draw_node(s->root, canvas3d, &frustum, &culled, cam_pos);
    rt_canvas3d_end(canvas3d);
    s->last_culled_count = culled;
}

void rt_scene3d_clear(void *obj)
{
    if (!obj)
        return;
    rt_scene3d *s = (rt_scene3d *)obj;
    /* Detach all children from root */
    for (int32_t i = 0; i < s->root->child_count; i++)
        s->root->children[i]->parent = NULL;
    s->root->child_count = 0;
    s->node_count = 1; /* just root */
}

int64_t rt_scene3d_get_node_count(void *obj)
{
    return obj ? ((rt_scene3d *)obj)->node_count : 0;
}

int64_t rt_scene3d_get_culled_count(void *obj)
{
    return obj ? ((rt_scene3d *)obj)->last_culled_count : 0;
}

/*==========================================================================
 * LOD — Level of Detail per SceneNode3D
 *=========================================================================*/

void rt_scene_node3d_add_lod(void *obj, double distance, void *mesh)
{
    if (!obj || !mesh) return;
    rt_scene_node3d *node = (rt_scene_node3d *)obj;

    if (node->lod_count >= node->lod_capacity)
    {
        int32_t new_cap = node->lod_capacity < 4 ? 4 : node->lod_capacity * 2;
        void *tmp = realloc(node->lod_levels,
                            (size_t)new_cap * sizeof(node->lod_levels[0]));
        if (!tmp)
            return;
        node->lod_levels = tmp;
        node->lod_capacity = new_cap;
    }

    /* Insert sorted by distance ascending */
    int32_t pos = node->lod_count;
    for (int32_t i = 0; i < node->lod_count; i++)
    {
        if (distance < node->lod_levels[i].distance)
        {
            pos = i;
            break;
        }
    }
    /* Shift elements right */
    for (int32_t i = node->lod_count; i > pos; i--)
        node->lod_levels[i] = node->lod_levels[i - 1];

    node->lod_levels[pos].distance = distance;
    node->lod_levels[pos].mesh = mesh;
    node->lod_count++;
}

void rt_scene_node3d_clear_lod(void *obj)
{
    if (!obj) return;
    rt_scene_node3d *node = (rt_scene_node3d *)obj;
    node->lod_count = 0;
}

#endif /* VIPER_ENABLE_GRAPHICS */
