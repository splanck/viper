//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_rt_scene3d.cpp
// Purpose: Unit tests for Viper.Graphics3D.Scene3D and SceneNode3D —
//   hierarchy management, TRS transform propagation, dirty flags, and
//   search-by-name.
//
// Links: src/runtime/graphics/rt_scene3d.h, plans/3d/12-scene-graph.md
//
//===----------------------------------------------------------------------===//

#ifndef VIPER_ENABLE_GRAPHICS
#define VIPER_ENABLE_GRAPHICS 1
#endif

#include "rt.hpp"
#include "rt_animcontroller3d.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_internal.h"
#include "rt_mat4.h"
#include "rt_morphtarget3d.h"
#include "rt_physics3d.h"
#include "rt_skeleton3d.h"
#include "rt_pixels.h"
#include "rt_scene3d.h"
#include "rt_scene3d_internal.h"
#include "rt_string.h"
#include "vgfx3d_backend.h"
#include <cassert>
#define _USE_MATH_DEFINES
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" {
extern void *rt_vec3_new(double x, double y, double z);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);
extern rt_string rt_const_cstr(const char *s);
extern void *rt_quat_new(double x, double y, double z, double w);
extern void *rt_quat_from_euler(double pitch, double yaw, double roll);
extern double rt_quat_x(void *q);
extern double rt_quat_y(void *q);
extern double rt_quat_z(void *q);
extern double rt_quat_w(void *q);
extern void rt_camera3d_look_at(void *cam, void *eye, void *target, void *up);
extern void *rt_material3d_new_color(double r, double g, double b);
extern void *rt_camera3d_new(double fov, double aspect, double near, double far);
extern void *rt_mesh3d_new(void);
extern void *rt_mesh3d_new_box(double w, double h, double d);
extern void rt_mesh3d_add_vertex(
    void *obj, double x, double y, double z, double nx, double ny, double nz, double u, double v);
extern void rt_mesh3d_add_triangle(void *obj, int64_t i0, int64_t i1, int64_t i2);
}

static int tests_passed = 0;
static int tests_run = 0;

#define EXPECT_TRUE(cond, msg)                                                                     \
    do {                                                                                           \
        tests_run++;                                                                               \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL: %s\n", msg);                                                    \
        } else {                                                                                   \
            tests_passed++;                                                                        \
        }                                                                                          \
    } while (0)

#define EXPECT_NEAR(a, b, eps, msg)                                                                \
    do {                                                                                           \
        tests_run++;                                                                               \
        if (fabs((a) - (b)) > (eps)) {                                                             \
            fprintf(stderr, "FAIL: %s (got %f, expected %f)\n", msg, (double)(a), (double)(b));    \
        } else {                                                                                   \
            tests_passed++;                                                                        \
        }                                                                                          \
    } while (0)

static bool read_text_file(const char *path, std::string &out) {
    out.clear();
    FILE *f = std::fopen(path, "rb");
    if (!f)
        return false;
    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        return false;
    }
    long size = std::ftell(f);
    if (size < 0 || std::fseek(f, 0, SEEK_SET) != 0) {
        std::fclose(f);
        return false;
    }
    out.resize((size_t)size);
    if (size > 0 && std::fread(out.data(), 1, (size_t)size, f) != (size_t)size) {
        std::fclose(f);
        out.clear();
        return false;
    }
    std::fclose(f);
    return true;
}

static bool write_text_file(const char *path, const char *text) {
    FILE *f = std::fopen(path, "wb");
    if (!f)
        return false;
    const size_t len = std::strlen(text);
    const bool ok = len == 0 || std::fwrite(text, 1, len, f) == len;
    const bool closed = std::fclose(f) == 0;
    return ok && closed;
}

static void test_create_scene_and_node() {
    void *scene = rt_scene3d_new();
    EXPECT_TRUE(scene != nullptr, "Scene3D.New returns non-null");

    void *root = rt_scene3d_get_root(scene);
    EXPECT_TRUE(root != nullptr, "Scene3D.Root is non-null at creation");

    void *node = rt_scene_node3d_new();
    EXPECT_TRUE(node != nullptr, "SceneNode3D.New returns non-null");

    EXPECT_TRUE(rt_scene3d_get_node_count(scene) == 1, "Initial node count is 1 (root)");
}

static void test_add_remove_child() {
    void *scene = rt_scene3d_new();
    void *node = rt_scene_node3d_new();

    rt_scene3d_add(scene, node);
    EXPECT_TRUE(rt_scene3d_get_node_count(scene) == 2, "After Add: node count is 2");
    EXPECT_TRUE(rt_scene_node3d_child_count(rt_scene3d_get_root(scene)) == 1, "Root has 1 child");

    void *parent = rt_scene_node3d_get_parent(node);
    EXPECT_TRUE(parent == rt_scene3d_get_root(scene), "Child's parent is root");

    rt_scene3d_remove(scene, node);
    EXPECT_TRUE(rt_scene3d_get_node_count(scene) == 1, "After Remove: node count is 1");
    EXPECT_TRUE(rt_scene_node3d_get_parent(node) == nullptr, "Removed node has no parent");
}

static void test_scene_remove_ignores_nodes_from_other_scenes() {
    void *scene_a = rt_scene3d_new();
    void *scene_b = rt_scene3d_new();
    void *node = rt_scene_node3d_new();

    rt_scene3d_add(scene_b, node);
    EXPECT_TRUE(rt_scene_node3d_get_parent(node) == rt_scene3d_get_root(scene_b),
                "Node starts parented in scene B");

    rt_scene3d_remove(scene_a, node);

    EXPECT_TRUE(rt_scene_node3d_get_parent(node) == rt_scene3d_get_root(scene_b),
                "Scene3D.Remove ignores nodes outside the scene root subtree");
    EXPECT_TRUE(rt_scene3d_get_node_count(scene_a) == 1, "Scene A count remains unchanged");
    EXPECT_TRUE(rt_scene3d_get_node_count(scene_b) == 2, "Scene B keeps its node");
}

static void test_translation_propagation() {
    void *parent = rt_scene_node3d_new();
    void *child = rt_scene_node3d_new();
    rt_scene_node3d_set_position(parent, 5.0, 0.0, 0.0);
    rt_scene_node3d_set_position(child, 1.0, 0.0, 0.0);
    rt_scene_node3d_add_child(parent, child);

    /* World matrix of child: parent(5,0,0) + child(1,0,0) = (6,0,0) */
    void *wm = rt_scene_node3d_get_world_matrix(child);
    EXPECT_TRUE(wm != nullptr, "GetWorldMatrix returns non-null");

    /* Extract translation from row-major Mat4: m[3], m[7], m[11] */
    typedef struct {
        double m[16];
    } mat4_view;

    mat4_view *mv = (mat4_view *)wm;
    EXPECT_NEAR(mv->m[3], 6.0, 0.001, "Child world X = parent(5) + local(1) = 6");
    EXPECT_NEAR(mv->m[7], 0.0, 0.001, "Child world Y = 0");
    EXPECT_NEAR(mv->m[11], 0.0, 0.001, "Child world Z = 0");
}

static void test_world_position_and_scale_getters() {
    void *parent = rt_scene_node3d_new();
    void *child = rt_scene_node3d_new();
    rt_scene_node3d_set_position(parent, 5.0, 2.0, -1.0);
    rt_scene_node3d_set_scale(parent, 2.0, 3.0, 4.0);
    rt_scene_node3d_set_position(child, 1.0, 0.0, 0.5);
    rt_scene_node3d_set_scale(child, 0.5, 2.0, 0.25);
    rt_scene_node3d_add_child(parent, child);

    void *world_pos = rt_scene_node3d_get_world_position(child);
    void *world_scale = rt_scene_node3d_get_world_scale(child);
    EXPECT_NEAR(rt_vec3_x(world_pos), 7.0, 0.001, "WorldPosition includes parent scaled X");
    EXPECT_NEAR(rt_vec3_y(world_pos), 2.0, 0.001, "WorldPosition includes parent Y");
    EXPECT_NEAR(rt_vec3_z(world_pos), 1.0, 0.001, "WorldPosition includes parent scaled Z");
    EXPECT_NEAR(rt_vec3_x(world_scale), 1.0, 0.001, "WorldScale X composes parent/child");
    EXPECT_NEAR(rt_vec3_y(world_scale), 6.0, 0.001, "WorldScale Y composes parent/child");
    EXPECT_NEAR(rt_vec3_z(world_scale), 1.0, 0.001, "WorldScale Z composes parent/child");
}

static void test_rotation_propagation() {
    void *parent = rt_scene_node3d_new();
    void *child = rt_scene_node3d_new();

    /* Rotate parent 90° around Y axis.
     * rt_quat_from_euler(pitch, yaw, roll) — pitch maps to Y rotation. */
    double angle = M_PI / 2.0;
    void *rot = rt_quat_from_euler(angle, 0.0, 0.0);
    rt_scene_node3d_set_rotation(parent, rot);

    /* Child at local position (1, 0, 0) */
    rt_scene_node3d_set_position(child, 1.0, 0.0, 0.0);
    rt_scene_node3d_add_child(parent, child);

    typedef struct {
        double m[16];
    } mat4_view;

    mat4_view *mv = (mat4_view *)rt_scene_node3d_get_world_matrix(child);

    /* 90° Y rotation of (1,0,0) → (0,0,-1) */
    EXPECT_NEAR(mv->m[3], 0.0, 0.01, "Rotated child world X ≈ 0");
    EXPECT_NEAR(mv->m[7], 0.0, 0.01, "Rotated child world Y ≈ 0");
    EXPECT_NEAR(mv->m[11], -1.0, 0.01, "Rotated child world Z ≈ -1");
}

static void test_scale_propagation() {
    void *parent = rt_scene_node3d_new();
    void *child = rt_scene_node3d_new();
    rt_scene_node3d_set_scale(parent, 2.0, 2.0, 2.0);
    rt_scene_node3d_set_position(child, 1.0, 1.0, 1.0);
    rt_scene_node3d_add_child(parent, child);

    typedef struct {
        double m[16];
    } mat4_view;

    mat4_view *mv = (mat4_view *)rt_scene_node3d_get_world_matrix(child);
    /* Scale(2) * Translate(1,1,1) → world pos = (2,2,2) */
    EXPECT_NEAR(mv->m[3], 2.0, 0.001, "Scaled child world X = 2");
    EXPECT_NEAR(mv->m[7], 2.0, 0.001, "Scaled child world Y = 2");
    EXPECT_NEAR(mv->m[11], 2.0, 0.001, "Scaled child world Z = 2");
}

static void test_deep_hierarchy() {
    /* 5-level chain: each node translates +1 in X */
    void *nodes[5];
    for (int i = 0; i < 5; i++) {
        nodes[i] = rt_scene_node3d_new();
        rt_scene_node3d_set_position(nodes[i], 1.0, 0.0, 0.0);
        if (i > 0)
            rt_scene_node3d_add_child(nodes[i - 1], nodes[i]);
    }

    typedef struct {
        double m[16];
    } mat4_view;

    mat4_view *mv = (mat4_view *)rt_scene_node3d_get_world_matrix(nodes[4]);
    EXPECT_NEAR(mv->m[3], 5.0, 0.001, "5-level hierarchy: world X = 5");
}

static void test_deep_hierarchy_iterative_traversal() {
    void *scene = rt_scene3d_new();
    void *parent = rt_scene3d_get_root(scene);
    void *leaf = nullptr;
    const int depth = 1024;
    for (int i = 0; i < depth; i++) {
        leaf = rt_scene_node3d_new();
        rt_scene_node3d_set_position(leaf, 1.0, 0.0, 0.0);
        rt_scene_node3d_add_child(parent, leaf);
        parent = leaf;
    }

    rt_scene_node3d_set_position(rt_scene3d_get_root(scene), 0.0, 0.0, 0.0);

    typedef struct {
        double m[16];
    } mat4_view;
    mat4_view *mv = (mat4_view *)rt_scene_node3d_get_world_matrix(leaf);
    EXPECT_NEAR(mv->m[3], (double)depth, 0.001, "Deep hierarchy world matrix traversal is iterative");
    EXPECT_TRUE(rt_scene3d_get_node_count(scene) == depth + 1,
                "Deep hierarchy node count traversal is iterative");
}

static void test_dirty_flag() {
    void *parent = rt_scene_node3d_new();
    void *child = rt_scene_node3d_new();
    rt_scene_node3d_add_child(parent, child);

    /* Access child world matrix to clear dirty */
    rt_scene_node3d_get_world_matrix(child);

    /* Change parent → child should become dirty and recompute */
    rt_scene_node3d_set_position(parent, 10.0, 0.0, 0.0);

    typedef struct {
        double m[16];
    } mat4_view;

    mat4_view *mv = (mat4_view *)rt_scene_node3d_get_world_matrix(child);
    EXPECT_NEAR(mv->m[3], 10.0, 0.001, "After parent move: child world X updated to 10");
}

static void test_find_by_name() {
    void *scene = rt_scene3d_new();
    void *n1 = rt_scene_node3d_new();
    void *n2 = rt_scene_node3d_new();

    rt_string name_bat = rt_const_cstr("bat");
    rt_string name_glove = rt_const_cstr("glove");
    rt_scene_node3d_set_name(n1, name_bat);
    rt_scene_node3d_set_name(n2, name_glove);
    rt_scene3d_add(scene, n1);
    rt_scene_node3d_add_child(n1, n2);

    void *found = rt_scene3d_find(scene, name_bat);
    EXPECT_TRUE(found == n1, "Find 'bat' returns correct node");

    void *found2 = rt_scene3d_find(scene, name_glove);
    EXPECT_TRUE(found2 == n2, "Find 'glove' returns nested node");

    rt_string name_missing = rt_const_cstr("nonexistent");
    void *nf = rt_scene3d_find(scene, name_missing);
    EXPECT_TRUE(nf == nullptr, "Find nonexistent returns null");
}

static void test_reparenting() {
    void *scene = rt_scene3d_new();
    void *a = rt_scene_node3d_new();
    void *b = rt_scene_node3d_new();
    void *child = rt_scene_node3d_new();

    rt_scene3d_add(scene, a);
    rt_scene3d_add(scene, b);
    rt_scene_node3d_add_child(a, child);
    EXPECT_TRUE(rt_scene_node3d_get_parent(child) == a, "Initial parent is A");

    /* Reparent child from A to B */
    rt_scene_node3d_add_child(b, child);
    EXPECT_TRUE(rt_scene_node3d_get_parent(child) == b, "After reparent: parent is B");
    EXPECT_TRUE(rt_scene_node3d_child_count(a) == 0, "A has 0 children after reparent");
    EXPECT_TRUE(rt_scene_node3d_child_count(b) == 1, "B has 1 child after reparent");
}

static void test_node_count_tracks_nested_hierarchy_edits() {
    void *scene = rt_scene3d_new();
    void *parent = rt_scene_node3d_new();
    void *child = rt_scene_node3d_new();

    rt_scene3d_add(scene, parent);
    EXPECT_TRUE(rt_scene3d_get_node_count(scene) == 2, "Scene count includes root and parent");

    rt_scene_node3d_add_child(parent, child);
    EXPECT_TRUE(rt_scene3d_get_node_count(scene) == 3,
                "Scene count reflects nested children added outside Scene3D.Add");

    rt_scene_node3d_remove_child(parent, child);
    EXPECT_TRUE(rt_scene3d_get_node_count(scene) == 2,
                "Scene count reflects nested children removed outside Scene3D.Remove");
}

static void test_prevent_cycle() {
    void *a = rt_scene_node3d_new();
    void *b = rt_scene_node3d_new();
    void *c = rt_scene_node3d_new();

    rt_scene_node3d_add_child(a, b);
    rt_scene_node3d_add_child(b, c);
    rt_scene_node3d_add_child(c, a);

    EXPECT_TRUE(rt_scene_node3d_get_parent(a) == nullptr,
                "Cycle insertion leaves ancestor parent unchanged");
    EXPECT_TRUE(rt_scene_node3d_get_parent(b) == a,
                "Existing parent link is preserved after cycle attempt");
    EXPECT_TRUE(rt_scene_node3d_get_parent(c) == b,
                "Descendant parent link is preserved after cycle attempt");
    EXPECT_TRUE(rt_scene_node3d_child_count(c) == 0, "Cycle insertion does not add a child");
}

static void test_visibility() {
    void *node = rt_scene_node3d_new();
    EXPECT_TRUE(rt_scene_node3d_get_visible(node) == 1, "Default visible = true");

    rt_scene_node3d_set_visible(node, 0);
    EXPECT_TRUE(rt_scene_node3d_get_visible(node) == 0, "After set visible=false");
}

static void test_subtree_aabb_includes_child_meshes() {
    void *root = rt_scene_node3d_new();
    void *child = rt_scene_node3d_new();
    void *mesh = rt_mesh3d_new_box(2.0, 4.0, 6.0);

    rt_scene_node3d_set_mesh(child, mesh);
    rt_scene_node3d_set_scale(child, 1.5, 0.5, 2.0);
    rt_scene_node3d_set_position(child, 5.0, 1.0, -2.0);
    rt_scene_node3d_add_child(root, child);

    void *min_v = rt_scene_node3d_get_aabb_min(root);
    void *max_v = rt_scene_node3d_get_aabb_max(root);

    EXPECT_NEAR(rt_vec3_x(min_v), 3.5, 0.001, "Synthetic roots report subtree AABB min X");
    EXPECT_NEAR(rt_vec3_y(min_v), 0.0, 0.001, "Synthetic roots report subtree AABB min Y");
    EXPECT_NEAR(rt_vec3_z(min_v), -8.0, 0.001, "Synthetic roots report subtree AABB min Z");

    EXPECT_NEAR(rt_vec3_x(max_v), 6.5, 0.001, "Synthetic roots report subtree AABB max X");
    EXPECT_NEAR(rt_vec3_y(max_v), 2.0, 0.001, "Synthetic roots report subtree AABB max Y");
    EXPECT_NEAR(rt_vec3_z(max_v), 4.0, 0.001, "Synthetic roots report subtree AABB max Z");

    rt_scene_node3d *root_impl = (rt_scene_node3d *)root;
    EXPECT_NEAR(root_impl->aabb_min[0], 0.0, 0.001, "AABB query does not mutate node min X");
    EXPECT_NEAR(root_impl->aabb_max[2], 0.0, 0.001, "AABB query does not mutate node max Z");
}

static void test_clear() {
    void *scene = rt_scene3d_new();
    void *n1 = rt_scene_node3d_new();
    void *n2 = rt_scene_node3d_new();
    rt_scene3d_add(scene, n1);
    rt_scene3d_add(scene, n2);
    EXPECT_TRUE(rt_scene3d_get_node_count(scene) == 3, "Before clear: 3 nodes");

    rt_scene3d_clear(scene);
    EXPECT_TRUE(rt_scene3d_get_node_count(scene) == 1, "After clear: 1 node (root)");
    EXPECT_TRUE(rt_scene_node3d_child_count(rt_scene3d_get_root(scene)) == 0,
                "Root has 0 children");
}

static void test_get_child() {
    void *parent = rt_scene_node3d_new();
    void *c1 = rt_scene_node3d_new();
    void *c2 = rt_scene_node3d_new();
    rt_scene_node3d_add_child(parent, c1);
    rt_scene_node3d_add_child(parent, c2);

    EXPECT_TRUE(rt_scene_node3d_get_child(parent, 0) == c1, "GetChild(0) returns first child");
    EXPECT_TRUE(rt_scene_node3d_get_child(parent, 1) == c2, "GetChild(1) returns second child");
    EXPECT_TRUE(rt_scene_node3d_get_child(parent, 2) == nullptr,
                "GetChild(2) out of bounds returns null");
}

static void test_default_transform() {
    void *node = rt_scene_node3d_new();
    void *pos = rt_scene_node3d_get_position(node);
    EXPECT_NEAR(rt_vec3_x(pos), 0.0, 0.001, "Default position X = 0");
    EXPECT_NEAR(rt_vec3_y(pos), 0.0, 0.001, "Default position Y = 0");
    EXPECT_NEAR(rt_vec3_z(pos), 0.0, 0.001, "Default position Z = 0");

    void *scl = rt_scene_node3d_get_scale(node);
    EXPECT_NEAR(rt_vec3_x(scl), 1.0, 0.001, "Default scale X = 1");
    EXPECT_NEAR(rt_vec3_y(scl), 1.0, 0.001, "Default scale Y = 1");
    EXPECT_NEAR(rt_vec3_z(scl), 1.0, 0.001, "Default scale Z = 1");

    void *rot = rt_scene_node3d_get_rotation(node);
    EXPECT_NEAR(rt_quat_w(rot), 1.0, 0.001, "Default rotation W = 1 (identity)");
}

static void test_node_sanitizes_nonfinite_transform_and_lod() {
    void *node = rt_scene_node3d_new();
    void *mesh = rt_mesh3d_new_box(1.0, 1.0, 1.0);

    rt_scene_node3d_set_position(node, NAN, 3.0, INFINITY);
    void *pos = rt_scene_node3d_get_position(node);
    EXPECT_NEAR(rt_vec3_x(pos), 0.0, 0.001, "SceneNode non-finite position X falls back to 0");
    EXPECT_NEAR(rt_vec3_y(pos), 3.0, 0.001, "SceneNode finite position Y is preserved");
    EXPECT_NEAR(rt_vec3_z(pos), 0.0, 0.001, "SceneNode non-finite position Z falls back to 0");

    rt_scene_node3d_set_scale(node, NAN, -2.0, INFINITY);
    void *scale = rt_scene_node3d_get_scale(node);
    EXPECT_NEAR(rt_vec3_x(scale), 1.0, 0.001, "SceneNode non-finite scale X falls back to 1");
    EXPECT_NEAR(rt_vec3_y(scale), -2.0, 0.001, "SceneNode finite negative scale is preserved");
    EXPECT_NEAR(rt_vec3_z(scale), 1.0, 0.001, "SceneNode non-finite scale Z falls back to 1");

    rt_scene_node3d_set_scale(node, 0.0, -0.0, 1e-16);
    scale = rt_scene_node3d_get_scale(node);
    EXPECT_NEAR(rt_vec3_x(scale), 0.0, 0.001, "SceneNode zero scale X is preserved");
    EXPECT_NEAR(rt_vec3_y(scale), 0.0, 0.001, "SceneNode negative-zero scale Y is preserved");
    EXPECT_NEAR(rt_vec3_z(scale), 1e-16, 0.001, "SceneNode near-zero finite scale Z is preserved");

    rt_scene_node3d_set_rotation(node, rt_quat_new(NAN, 0.0, 0.0, 0.0));
    void *rot = rt_scene_node3d_get_rotation(node);
    EXPECT_NEAR(rt_quat_x(rot), 0.0, 0.001, "SceneNode invalid quaternion resets X");
    EXPECT_NEAR(rt_quat_y(rot), 0.0, 0.001, "SceneNode invalid quaternion resets Y");
    EXPECT_NEAR(rt_quat_z(rot), 0.0, 0.001, "SceneNode invalid quaternion resets Z");
    EXPECT_NEAR(rt_quat_w(rot), 1.0, 0.001, "SceneNode invalid quaternion resets W");

    rt_scene_node3d_set_rotation(node, node);
    rot = rt_scene_node3d_get_rotation(node);
    EXPECT_NEAR(rt_quat_w(rot), 1.0, 0.001, "SceneNode rejects non-Quat rotation handles");

    rt_scene_node3d_set_visible(node, 42);
    EXPECT_TRUE(rt_scene_node3d_get_visible(node) == 1, "SceneNode visibility normalizes to bool");

    rt_scene_node3d_add_lod(node, NAN, mesh);
    EXPECT_TRUE(rt_scene_node3d_get_lod_count(node) == 1, "SceneNode accepts sanitized LOD");
    EXPECT_NEAR(rt_scene_node3d_get_lod_distance(node, 0),
                0.0,
                0.001,
                "SceneNode non-finite LOD distance clamps to zero");
}

static void test_node_body_sync_preserves_negative_scale_handedness() {
    void *scene = rt_scene3d_new();
    void *node = rt_scene_node3d_new();
    void *body = rt_body3d_new_sphere(0.5, 1.0);

    rt_scene_node3d_set_scale(node, -5.0, 3.0, 4.0);
    rt_scene_node3d_bind_body(node, body);
    rt_scene3d_add(scene, node);
    rt_body3d_set_position(body, 2.0, 3.0, 4.0);

    rt_scene3d_sync_bindings(scene, 0.016);

    void *scale = rt_scene_node3d_get_scale(node);
    EXPECT_NEAR(rt_vec3_x(scale), -5.0, 0.001, "SceneNode body sync preserves mirrored X scale");
    EXPECT_NEAR(rt_vec3_y(scale), 3.0, 0.001, "SceneNode body sync preserves Y scale");
    EXPECT_NEAR(rt_vec3_z(scale), 4.0, 0.001, "SceneNode body sync preserves Z scale");
}

/*==========================================================================
 * Frustum culling tests
 *=========================================================================*/

extern "C" {
extern void *rt_mesh3d_new_box(double w, double h, double d);
extern void *rt_mesh3d_new_sphere(double r, int64_t seg);
extern void *rt_mesh3d_new_plane(double sx, double sz);
extern int64_t rt_scene3d_get_culled_count(void *scene);
}

static int g_scene_submit_count = 0;
static int g_scene_begin_count = 0;
static int g_scene_end_count = 0;
static const void *g_scene_last_vertices = nullptr;
static int32_t g_scene_last_bone_count = 0;
static vgfx3d_light_params_t g_scene_last_lights[VGFX3D_MAX_LIGHTS];
static int32_t g_scene_last_light_count = 0;

static void scene_test_begin_frame(void *, const vgfx3d_camera_params_t *) {
    g_scene_begin_count++;
}

static void scene_test_end_frame(void *) {
    g_scene_end_count++;
}

static void scene_test_submit_draw(void *,
                                   vgfx_window_t,
                                   const vgfx3d_draw_cmd_t *cmd,
                                   const vgfx3d_light_params_t *lights,
                                   int32_t light_count,
                                   const float *,
                                   int8_t,
                                   int8_t) {
    g_scene_submit_count++;
    g_scene_last_vertices = cmd ? cmd->vertices : nullptr;
    g_scene_last_bone_count = cmd ? cmd->bone_count : 0;
    g_scene_last_light_count = light_count > VGFX3D_MAX_LIGHTS ? VGFX3D_MAX_LIGHTS : light_count;
    if (lights && g_scene_last_light_count > 0)
        std::memcpy(g_scene_last_lights,
                    lights,
                    (size_t)g_scene_last_light_count * sizeof(g_scene_last_lights[0]));
}

static void init_scene_test_canvas(rt_canvas3d *canvas, const vgfx3d_backend_t *backend) {
    std::memset(canvas, 0, sizeof(*canvas));
    canvas->backend = backend;
    canvas->gfx_win = (vgfx_window_t)1;
}

static void reset_scene_capture(void) {
    g_scene_submit_count = 0;
    g_scene_begin_count = 0;
    g_scene_end_count = 0;
    g_scene_last_vertices = nullptr;
    g_scene_last_bone_count = 0;
    std::memset(g_scene_last_lights, 0, sizeof(g_scene_last_lights));
    g_scene_last_light_count = 0;
}

static void test_scene_draw_reuses_active_frame() {
    vgfx3d_backend_t backend = {};
    backend.name = "opengl";
    backend.begin_frame = scene_test_begin_frame;
    backend.end_frame = scene_test_end_frame;
    backend.submit_draw = scene_test_submit_draw;

    rt_canvas3d canvas;
    init_scene_test_canvas(&canvas, &backend);
    canvas.in_frame = 1;
    canvas.frame_is_2d = 0;
    reset_scene_capture();

    void *scene = rt_scene3d_new();
    void *node = rt_scene_node3d_new();
    void *mesh = rt_mesh3d_new_box(1.0, 1.0, 1.0);
    void *material = rt_material3d_new_color(1.0, 1.0, 1.0);
    void *camera = rt_camera3d_new(60.0, 1.0, 0.1, 100.0);
    void *eye = rt_vec3_new(0.0, 0.0, 5.0);
    void *target = rt_vec3_new(0.0, 0.0, 0.0);
    void *up = rt_vec3_new(0.0, 1.0, 0.0);

    rt_scene_node3d_set_mesh(node, mesh);
    rt_scene_node3d_set_material(node, material);
    rt_scene3d_add(scene, node);
    rt_camera3d_look_at(camera, eye, target, up);

    rt_scene3d_draw(scene, &canvas, camera);

    EXPECT_TRUE(g_scene_begin_count == 0,
                "Scene3D.Draw does not nest Begin when a 3D frame is already active");
    EXPECT_TRUE(g_scene_end_count == 0, "Scene3D.Draw does not end an externally-owned frame");
    EXPECT_TRUE(canvas.draw_count == 1,
                "Scene3D.Draw queues scene geometry inside an externally-owned frame");
    EXPECT_TRUE(g_scene_submit_count == 0,
                "Scene3D.Draw defers backend submission until the caller ends the frame");
    EXPECT_TRUE(canvas.in_frame == 1, "Scene3D.Draw leaves the caller-owned frame active");
}

static void test_scene_draw_culling_uses_canvas_output_aspect() {
    vgfx3d_backend_t backend = {};
    backend.name = "opengl";
    backend.begin_frame = scene_test_begin_frame;
    backend.end_frame = scene_test_end_frame;
    backend.submit_draw = scene_test_submit_draw;

    rt_canvas3d canvas;
    init_scene_test_canvas(&canvas, &backend);
    canvas.width = 200;
    canvas.height = 100;
    reset_scene_capture();

    void *scene = rt_scene3d_new();
    void *node = rt_scene_node3d_new();
    void *mesh = rt_mesh3d_new_box(1.0, 1.0, 1.0);
    void *material = rt_material3d_new_color(1.0, 1.0, 1.0);
    void *camera = rt_camera3d_new(60.0, 1.0, 0.1, 100.0);
    void *eye = rt_vec3_new(0.0, 0.0, 5.0);
    void *target = rt_vec3_new(0.0, 0.0, 0.0);
    void *up = rt_vec3_new(0.0, 1.0, 0.0);

    rt_scene_node3d_set_position(node, 4.0, 0.0, 0.0);
    rt_scene_node3d_set_mesh(node, mesh);
    rt_scene_node3d_set_material(node, material);
    rt_scene3d_add(scene, node);
    rt_camera3d_look_at(camera, eye, target, up);

    rt_scene3d_draw(scene, &canvas, camera);

    EXPECT_TRUE(g_scene_submit_count == 1,
                "Scene3D culling uses the active canvas aspect instead of the camera's stored aspect");
    EXPECT_TRUE(rt_scene3d_get_culled_count(scene) == 0,
                "Wide active outputs keep edge-visible nodes from being culled by a stale camera projection");
}

static void test_scene_save_escapes_json_names() {
    void *scene = rt_scene3d_new();
    void *node = rt_scene_node3d_new();
    const char *path = "/tmp/viper_scene_escape_test.vscn";
    const char *name = "quote\"slash\\line\nbreak";

    rt_scene_node3d_set_name(node, rt_const_cstr(name));
    rt_scene3d_add(scene, node);

    EXPECT_TRUE(rt_scene3d_save(scene, rt_const_cstr(path)) == 1,
                "Scene3D.Save writes the scene file");

    std::string text;
    EXPECT_TRUE(read_text_file(path, text), "Scene3D.Save output can be reopened");
    if (text.empty())
        return;

    EXPECT_TRUE(text.find("\"name\": \"quote\\\"slash\\\\line\\nbreak\"") != std::string::npos,
                "Scene3D.Save escapes quotes, backslashes, and newlines in node names");
}

static void test_scene_save_serializes_visibility_and_lod_metadata() {
    void *scene = rt_scene3d_new();
    void *node = rt_scene_node3d_new();
    const char *path = "/tmp/viper_scene_metadata_test.vscn";

    rt_scene_node3d_set_visible(node, 0);
    rt_scene_node3d_set_mesh(node, rt_mesh3d_new_box(1.0, 1.0, 1.0));
    rt_scene_node3d_set_material(node, rt_material3d_new_color(1.0, 0.0, 0.0));
    rt_scene_node3d_add_lod(node, 10.0, rt_mesh3d_new_box(0.5, 0.5, 0.5));
    rt_scene3d_add(scene, node);

    EXPECT_TRUE(rt_scene3d_save(scene, rt_const_cstr(path)) == 1,
                "Scene3D.Save writes metadata-rich scene files");

    std::string text;
    EXPECT_TRUE(read_text_file(path, text), "Scene3D.Save metadata output can be reopened");
    if (text.empty())
        return;

    EXPECT_TRUE(text.find("\"visible\": false") != std::string::npos,
                "Scene3D.Save serializes node visibility");
    EXPECT_TRUE(text.find("\"hasMesh\": true") != std::string::npos,
                "Scene3D.Save serializes mesh presence");
    EXPECT_TRUE(text.find("\"hasMaterial\": true") != std::string::npos,
                "Scene3D.Save serializes material presence");
    EXPECT_TRUE(text.find("\"lod\": [") != std::string::npos,
                "Scene3D.Save serializes LOD metadata");
    EXPECT_TRUE(text.find("\"distance\": 10") != std::string::npos,
                "Scene3D.Save serializes LOD distances");
}

static void test_scene_roundtrip_loads_shared_assets() {
    const char *path = "/tmp/viper_scene_roundtrip_test.vscn";
    void *scene = rt_scene3d_new();
    void *parent = rt_scene_node3d_new();
    void *child = rt_scene_node3d_new();
    void *mesh = rt_mesh3d_new();
    void *lod_mesh = rt_mesh3d_new_box(0.5, 0.5, 0.5);
    rt_material3d *material = (rt_material3d *)rt_material3d_new();

    rt_mesh3d_add_vertex(mesh, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0);
    rt_mesh3d_add_vertex(mesh, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0);
    rt_mesh3d_add_vertex(mesh, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0, 0.0, 1.0);
    rt_mesh3d_add_triangle(mesh, 0, 1, 2);
    ((rt_mesh3d *)mesh)->vertices[0].tangent[0] = 1.0f;
    ((rt_mesh3d *)mesh)->vertices[0].bone_indices[0] = 3;
    ((rt_mesh3d *)mesh)->vertices[0].bone_weights[0] = 1.0f;
    ((rt_mesh3d *)mesh)->bone_count = 4;

    void *diffuse = rt_pixels_new(2, 1);
    void *normal = rt_pixels_new(1, 1);
    void *specular = rt_pixels_new(1, 1);
    void *emissive = rt_pixels_new(1, 1);
    void *metallic_roughness = rt_pixels_new(1, 1);
    void *ao = rt_pixels_new(1, 1);
    void *faces[6];
    const int64_t face_colors[6] = {
        0xFF0000FFll, 0x00FF00FFll, 0x0000FFFFll, 0xFFFF00FFll, 0xFF00FFFFll, 0x00FFFFFFll};

    rt_pixels_set(diffuse, 0, 0, 0x10203040ll);
    rt_pixels_set(diffuse, 1, 0, 0x50607080ll);
    rt_pixels_set(normal, 0, 0, 0x7F7FFFFFll);
    rt_pixels_set(specular, 0, 0, 0x808080FFll);
    rt_pixels_set(emissive, 0, 0, 0xFF8040FFll);
    rt_pixels_set(metallic_roughness, 0, 0, 0x2244CCFFll);
    rt_pixels_set(ao, 0, 0, 0x7F0000FFll);
    for (int i = 0; i < 6; i++) {
        faces[i] = rt_pixels_new(1, 1);
        rt_pixels_set(faces[i], 0, 0, face_colors[i]);
    }

    material->diffuse[0] = 0.25;
    material->diffuse[1] = 0.5;
    material->diffuse[2] = 0.75;
    material->diffuse[3] = 0.9;
    material->specular[0] = 0.2;
    material->specular[1] = 0.4;
    material->specular[2] = 0.6;
    material->shininess = 48.0;
    material->workflow = RT_MATERIAL3D_WORKFLOW_PBR;
    material->emissive[0] = 0.1;
    material->emissive[1] = 0.2;
    material->emissive[2] = 0.3;
    material->metallic = 0.65;
    material->roughness = 0.35;
    material->ao = 0.55;
    material->emissive_intensity = 1.8;
    material->normal_scale = 0.7;
    material->alpha = 0.8;
    material->alpha_mode = RT_MATERIAL3D_ALPHA_MODE_BLEND;
    material->alpha_cutoff = 0.42;
    material->double_sided = 1;
    material->reflectivity = 0.6;
    material->unlit = 1;
    material->shading_model = 4;
    material->custom_params[0] = 3.5;
    material->custom_params[1] = 1.25;
    material->texture_slot_uv_set[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR] = 1;
    material->texture_slot_wrap_s[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR] =
        RT_MATERIAL3D_TEXTURE_WRAP_CLAMP_TO_EDGE;
    material->texture_slot_wrap_t[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR] =
        RT_MATERIAL3D_TEXTURE_WRAP_MIRRORED_REPEAT;
    material->texture_slot_filter[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR] =
        RT_MATERIAL3D_TEXTURE_FILTER_NEAREST;
    material->texture_slot_uv_transform[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR][0] = 2.0;
    material->texture_slot_uv_transform[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR][3] = 3.0;
    material->texture_slot_uv_transform[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR][4] = 0.25;
    material->texture_slot_uv_transform[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR][5] = 0.5;
    material->texture_slot_uv_set[RT_MATERIAL3D_TEXTURE_SLOT_NORMAL] = 1;
    material->texture_slot_uv_transform[RT_MATERIAL3D_TEXTURE_SLOT_NORMAL][0] = 0.5;
    material->texture_slot_uv_transform[RT_MATERIAL3D_TEXTURE_SLOT_NORMAL][3] = 0.75;
    material->texture_slot_uv_transform[RT_MATERIAL3D_TEXTURE_SLOT_NORMAL][4] = -0.125;
    material->texture_slot_uv_transform[RT_MATERIAL3D_TEXTURE_SLOT_NORMAL][5] = 0.875;
    material->texture_wrap_s =
        material->texture_slot_wrap_s[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR];
    material->texture_wrap_t =
        material->texture_slot_wrap_t[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR];
    material->texture_filter =
        material->texture_slot_filter[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR];
    rt_material3d_set_texture(material, diffuse);
    rt_material3d_set_normal_map(material, normal);
    rt_material3d_set_specular_map(material, specular);
    rt_material3d_set_emissive_map(material, emissive);
    rt_material3d_set_metallic_roughness_map(material, metallic_roughness);
    rt_material3d_set_ao_map(material, ao);
    rt_material3d_set_env_map(
        material, rt_cubemap3d_new(faces[0], faces[1], faces[2], faces[3], faces[4], faces[5]));

    rt_scene_node3d_set_name(parent, rt_const_cstr("parent"));
    rt_scene_node3d_set_name(child, rt_const_cstr("child"));
    rt_scene_node3d_set_position(parent, 1.0, 2.0, 3.0);
    rt_scene_node3d_set_scale(parent, 2.0, 2.0, 2.0);
    rt_scene_node3d_set_visible(parent, 0);
    rt_scene_node3d_set_mesh(parent, mesh);
    rt_scene_node3d_set_material(parent, material);
    rt_scene_node3d_add_lod(parent, 10.0, lod_mesh);
    rt_scene_node3d_set_mesh(child, mesh);
    rt_scene_node3d_set_material(child, material);
    rt_scene_node3d_add_child(parent, child);
    rt_scene3d_add(scene, parent);

    EXPECT_TRUE(rt_scene3d_save(scene, rt_const_cstr(path)) == 1,
                "Scene3D.Save writes roundtrip scene files");

    void *loaded_scene = rt_scene3d_load(rt_const_cstr(path));
    EXPECT_TRUE(loaded_scene != nullptr, "Scene3D.Load reconstructs saved scenes");
    if (!loaded_scene)
        return;

    void *loaded_parent = rt_scene3d_find(loaded_scene, rt_const_cstr("parent"));
    void *loaded_child = rt_scene3d_find(loaded_scene, rt_const_cstr("child"));
    EXPECT_TRUE(loaded_parent != nullptr, "Scene3D.Load restores named parent nodes");
    EXPECT_TRUE(loaded_child != nullptr, "Scene3D.Load restores named child nodes");
    if (!loaded_parent || !loaded_child)
        return;

    EXPECT_TRUE(rt_scene_node3d_get_parent(loaded_child) == loaded_parent,
                "Scene3D.Load restores hierarchy links");
    EXPECT_TRUE(rt_scene_node3d_get_visible(loaded_parent) == 0,
                "Scene3D.Load restores node visibility");
    EXPECT_NEAR(rt_vec3_x(rt_scene_node3d_get_position(loaded_parent)),
                1.0,
                0.001,
                "Scene3D.Load restores node position");
    EXPECT_NEAR(rt_vec3_y(rt_scene_node3d_get_scale(loaded_parent)),
                2.0,
                0.001,
                "Scene3D.Load restores node scale");
    EXPECT_TRUE(rt_scene_node3d_get_mesh(loaded_parent) == rt_scene_node3d_get_mesh(loaded_child),
                "Scene3D.Load preserves shared mesh references");
    EXPECT_TRUE(rt_scene_node3d_get_material(loaded_parent) ==
                    rt_scene_node3d_get_material(loaded_child),
                "Scene3D.Load preserves shared material references");
    EXPECT_TRUE(rt_scene_node3d_get_lod_count(loaded_parent) == 1,
                "Scene3D.Load restores LOD entries");
    EXPECT_NEAR(rt_scene_node3d_get_lod_distance(loaded_parent, 0),
                10.0,
                0.001,
                "Scene3D.Load restores LOD distances");
    EXPECT_TRUE(rt_scene_node3d_get_lod_mesh(loaded_parent, 0) !=
                    rt_scene_node3d_get_mesh(loaded_parent),
                "Scene3D.Load restores LOD mesh references");

    rt_mesh3d *loaded_mesh = (rt_mesh3d *)rt_scene_node3d_get_mesh(loaded_parent);
    EXPECT_TRUE(loaded_mesh != nullptr && loaded_mesh->vertex_count == 3 &&
                    loaded_mesh->index_count == 3,
                "Scene3D.Load restores mesh geometry");
    if (loaded_mesh) {
        EXPECT_NEAR(
            loaded_mesh->vertices[1].pos[0], 1.0, 0.001, "Scene3D.Load restores vertex positions");
        EXPECT_NEAR(loaded_mesh->vertices[0].tangent[0],
                    1.0,
                    0.001,
                    "Scene3D.Load restores vertex tangents");
        EXPECT_TRUE(loaded_mesh->vertices[0].bone_indices[0] == 3 &&
                        fabs(loaded_mesh->vertices[0].bone_weights[0] - 1.0f) < 0.001f &&
                        loaded_mesh->bone_count == 4,
                    "Scene3D.Load restores skinning vertex data");
    }

    rt_material3d *loaded_material = (rt_material3d *)rt_scene_node3d_get_material(loaded_parent);
    EXPECT_TRUE(loaded_material != nullptr, "Scene3D.Load restores materials");
    if (loaded_material) {
        EXPECT_NEAR(loaded_material->diffuse[0],
                    0.25,
                    0.001,
                    "Scene3D.Load restores material diffuse color");
        EXPECT_NEAR(loaded_material->alpha, 0.8, 0.001, "Scene3D.Load restores material alpha");
        EXPECT_TRUE(loaded_material->workflow == RT_MATERIAL3D_WORKFLOW_PBR,
                    "Scene3D.Load restores the PBR workflow");
        EXPECT_NEAR(loaded_material->metallic,
                    0.65,
                    0.001,
                    "Scene3D.Load restores material metallic");
        EXPECT_NEAR(loaded_material->roughness,
                    0.35,
                    0.001,
                    "Scene3D.Load restores material roughness");
        EXPECT_NEAR(loaded_material->ao, 0.55, 0.001, "Scene3D.Load restores material AO");
        EXPECT_NEAR(loaded_material->emissive_intensity,
                    1.8,
                    0.001,
                    "Scene3D.Load restores emissive intensity");
        EXPECT_NEAR(loaded_material->normal_scale,
                    0.7,
                    0.001,
                    "Scene3D.Load restores normal scale");
        EXPECT_TRUE(loaded_material->alpha_mode == RT_MATERIAL3D_ALPHA_MODE_BLEND &&
                        std::fabs(loaded_material->alpha_cutoff - 0.42) < 0.001 &&
                        loaded_material->double_sided == 1,
                    "Scene3D.Load restores alpha-mode and culling flags");
        EXPECT_NEAR(loaded_material->reflectivity,
                    0.6,
                    0.001,
                    "Scene3D.Load restores material reflectivity");
        EXPECT_TRUE(loaded_material->unlit == 1 && loaded_material->shading_model == 4,
                    "Scene3D.Load restores material shading flags");
        EXPECT_NEAR(loaded_material->custom_params[0],
                    3.5,
                    0.001,
                    "Scene3D.Load restores custom shader params");
        EXPECT_TRUE(loaded_material->texture_slot_uv_set[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR] == 1 &&
                        loaded_material->texture_slot_wrap_s[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR] ==
                            RT_MATERIAL3D_TEXTURE_WRAP_CLAMP_TO_EDGE &&
                        loaded_material->texture_slot_wrap_t[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR] ==
                            RT_MATERIAL3D_TEXTURE_WRAP_MIRRORED_REPEAT &&
                        loaded_material->texture_slot_filter[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR] ==
                            RT_MATERIAL3D_TEXTURE_FILTER_NEAREST,
                    "Scene3D.Load restores base texture slot sampler and UV set");
        EXPECT_NEAR(loaded_material->texture_slot_uv_transform[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR][0],
                    2.0,
                    0.001,
                    "Scene3D.Load restores base texture slot U scale");
        EXPECT_NEAR(loaded_material->texture_slot_uv_transform[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR][5],
                    0.5,
                    0.001,
                    "Scene3D.Load restores base texture slot V offset");
        EXPECT_TRUE(loaded_material->texture_wrap_s ==
                        RT_MATERIAL3D_TEXTURE_WRAP_CLAMP_TO_EDGE &&
                        loaded_material->texture_wrap_t ==
                            RT_MATERIAL3D_TEXTURE_WRAP_MIRRORED_REPEAT &&
                        loaded_material->texture_filter == RT_MATERIAL3D_TEXTURE_FILTER_NEAREST,
                    "Scene3D.Load keeps legacy primary sampler fields in sync");
        EXPECT_TRUE(loaded_material->texture_slot_uv_set[RT_MATERIAL3D_TEXTURE_SLOT_NORMAL] == 1,
                    "Scene3D.Load restores normal-map texture coordinate set");
        EXPECT_NEAR(loaded_material->texture_slot_uv_transform[RT_MATERIAL3D_TEXTURE_SLOT_NORMAL][4],
                    -0.125,
                    0.001,
                    "Scene3D.Load restores independent normal-map UV transform");
        EXPECT_TRUE(loaded_material->texture != nullptr &&
                        rt_pixels_get(loaded_material->texture, 0, 0) == 0x10203040ll &&
                        rt_pixels_get(loaded_material->texture, 1, 0) == 0x50607080ll,
                    "Scene3D.Load restores diffuse textures");
        EXPECT_TRUE(loaded_material->normal_map != nullptr &&
                        rt_pixels_get(loaded_material->normal_map, 0, 0) == 0x7F7FFFFFll,
                    "Scene3D.Load restores normal maps");
        EXPECT_TRUE(loaded_material->specular_map != nullptr &&
                        rt_pixels_get(loaded_material->specular_map, 0, 0) == 0x808080FFll,
                    "Scene3D.Load restores specular maps");
        EXPECT_TRUE(loaded_material->emissive_map != nullptr &&
                        rt_pixels_get(loaded_material->emissive_map, 0, 0) == 0xFF8040FFll,
                    "Scene3D.Load restores emissive maps");
        EXPECT_TRUE(loaded_material->metallic_roughness_map != nullptr &&
                        rt_pixels_get(loaded_material->metallic_roughness_map, 0, 0) ==
                            0x2244CCFFll,
                    "Scene3D.Load restores metallic-roughness maps");
        EXPECT_TRUE(loaded_material->ao_map != nullptr &&
                        rt_pixels_get(loaded_material->ao_map, 0, 0) == 0x7F0000FFll,
                    "Scene3D.Load restores AO maps");
        rt_cubemap3d *env = (rt_cubemap3d *)loaded_material->env_map;
        EXPECT_TRUE(env != nullptr && env->face_size == 1,
                    "Scene3D.Load restores environment cubemaps");
        if (env) {
            EXPECT_TRUE(rt_pixels_get(env->faces[0], 0, 0) == face_colors[0] &&
                            rt_pixels_get(env->faces[5], 0, 0) == face_colors[5],
                        "Scene3D.Load restores cubemap face textures");
        }
    }
}

static void test_node_animator_handles_large_morph_weight_channels() {
    void *scene = rt_scene3d_new();
    void *node = rt_scene_node3d_new();
    void *mesh = rt_mesh3d_new();
    void *morph = rt_morphtarget3d_new(3);
    double times[2] = {0.0, 1.0};
    float values[40] = {};
    void *anim;
    void *animator;
    void *clips[1];

    rt_mesh3d_add_vertex(mesh, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0);
    rt_mesh3d_add_vertex(mesh, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 0.0);
    rt_mesh3d_add_vertex(mesh, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0);
    rt_mesh3d_add_triangle(mesh, 0, 1, 2);

    for (int i = 0; i < 20; i++)
        rt_morphtarget3d_add_shape(morph, rt_const_cstr("shape"));
    rt_mesh3d_set_morph_targets(mesh, morph);
    rt_scene_node3d_set_name(node, rt_const_cstr("target"));
    rt_scene_node3d_set_mesh(node, mesh);
    rt_scene3d_add(scene, node);

    values[20 + 19] = 0.8f;
    anim = rt_node_animation3d_new(rt_const_cstr("many_weights"), 1.0);
    EXPECT_TRUE(rt_node_animation3d_add_channel(anim,
                                                rt_const_cstr("target"),
                                                RT_NODE_ANIM_PATH_WEIGHTS,
                                                RT_NODE_ANIM_INTERP_LINEAR,
                                                2,
                                                20,
                                                times,
                                                values) >= 0,
                "Node animation accepts morph-weight channels wider than the stack scratch buffer");
    clips[0] = anim;
    animator = rt_node_animator3d_new_from_clips(clips, 1);
    rt_scene_node3d_bind_node_animator(rt_scene3d_get_root(scene), animator);
    rt_scene3d_sync_bindings(scene, 0.5);

    EXPECT_NEAR(rt_morphtarget3d_get_weight(morph, 19),
                0.4,
                0.001,
                "Node animation applies morph weights beyond the old fixed scratch limit");
}

static void test_node_animator_samples_cubic_translation_channels() {
    void *scene = rt_scene3d_new();
    void *node = rt_scene_node3d_new();
    double times[2] = {0.0, 1.0};
    float values[6] = {0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f};
    float in_tangents[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    float out_tangents[6] = {2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    void *anim;
    void *animator;
    void *clips[1];

    rt_scene_node3d_set_name(node, rt_const_cstr("target"));
    rt_scene3d_add(scene, node);
    anim = rt_node_animation3d_new(rt_const_cstr("cubic"), 1.0);
    EXPECT_TRUE(rt_node_animation3d_add_cubic_channel(anim,
                                                      rt_const_cstr("target"),
                                                      RT_NODE_ANIM_PATH_TRANSLATION,
                                                      2,
                                                      3,
                                                      times,
                                                      values,
                                                      in_tangents,
                                                      out_tangents) >= 0,
                "Node animation accepts CUBICSPLINE channels with tangent payloads");
    clips[0] = anim;
    animator = rt_node_animator3d_new_from_clips(clips, 1);
    rt_scene_node3d_bind_node_animator(rt_scene3d_get_root(scene), animator);
    rt_scene3d_sync_bindings(scene, 0.5);

    void *pos = rt_scene_node3d_get_position(node);
    EXPECT_NEAR(rt_vec3_x(pos),
                1.25,
                0.001,
                "Node animation evaluates CUBICSPLINE translation with Hermite tangents");
}

static void test_node_animator_slerps_linear_rotation_channels() {
    void *scene = rt_scene3d_new();
    void *node = rt_scene_node3d_new();
    double times[2] = {0.0, 1.0};
    float values[8] = {0.0f, 0.0f, 0.0f, 1.0f,
                       0.0f, 0.0f, 0.70710678f, 0.70710678f};
    void *anim;
    void *animator;
    void *clips[1];

    rt_scene_node3d_set_name(node, rt_const_cstr("target"));
    rt_scene3d_add(scene, node);
    anim = rt_node_animation3d_new(rt_const_cstr("rotate"), 1.0);
    EXPECT_TRUE(rt_node_animation3d_add_channel(anim,
                                                rt_const_cstr("target"),
                                                RT_NODE_ANIM_PATH_ROTATION,
                                                RT_NODE_ANIM_INTERP_LINEAR,
                                                2,
                                                4,
                                                times,
                                                values) >= 0,
                "Node animation accepts linear rotation channels");
    clips[0] = anim;
    animator = rt_node_animator3d_new_from_clips(clips, 1);
    rt_scene_node3d_bind_node_animator(rt_scene3d_get_root(scene), animator);
    rt_scene3d_sync_bindings(scene, 0.25);

    void *rot = rt_scene_node3d_get_rotation(node);
    EXPECT_NEAR(rt_quat_z(rot), 0.19509, 0.001, "Node animation uses quaternion slerp for rotation Z");
    EXPECT_NEAR(rt_quat_w(rot), 0.98079, 0.001, "Node animation uses quaternion slerp for rotation W");
}

static void test_node_animation_rejects_invalid_channel_data() {
    void *anim = rt_node_animation3d_new(rt_const_cstr("invalid"), 1.0);
    double unsorted_times[2] = {1.0, 0.0};
    double valid_times[2] = {0.0, 1.0};
    float translation_values[6] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    float bad_values[6] = {0.0f, 0.0f, 0.0f, NAN, 0.0f, 0.0f};
    float tangent_values[6] = {};
    float bad_tangents[6] = {0.0f, INFINITY, 0.0f, 0.0f, 0.0f, 0.0f};

    EXPECT_TRUE(rt_node_animation3d_add_channel(anim,
                                                rt_const_cstr("target"),
                                                RT_NODE_ANIM_PATH_TRANSLATION,
                                                RT_NODE_ANIM_INTERP_LINEAR,
                                                2,
                                                3,
                                                unsorted_times,
                                                translation_values) < 0,
                "Node animation rejects unsorted key times");
    EXPECT_TRUE(rt_node_animation3d_add_channel(anim,
                                                rt_const_cstr("target"),
                                                RT_NODE_ANIM_PATH_TRANSLATION,
                                                RT_NODE_ANIM_INTERP_LINEAR,
                                                2,
                                                3,
                                                valid_times,
                                                bad_values) < 0,
                "Node animation rejects non-finite values");
    EXPECT_TRUE(rt_node_animation3d_add_channel(anim,
                                                rt_const_cstr("target"),
                                                RT_NODE_ANIM_PATH_TRANSLATION,
                                                RT_NODE_ANIM_INTERP_LINEAR,
                                                2,
                                                2,
                                                valid_times,
                                                translation_values) < 0,
                "Node animation rejects translation channels narrower than xyz");
    EXPECT_TRUE(rt_node_animation3d_add_cubic_channel(anim,
                                                      rt_const_cstr("target"),
                                                      RT_NODE_ANIM_PATH_TRANSLATION,
                                                      2,
                                                      3,
                                                      valid_times,
                                                      translation_values,
                                                      bad_tangents,
                                                      tangent_values) < 0,
                "Node animation rejects non-finite cubic tangents");
}

static void test_frustum_aabb_inside() {
    /* Object at origin, camera looking at it → visible (not culled) */
    void *scene = rt_scene3d_new();
    void *node = rt_scene_node3d_new();
    rt_scene_node3d_set_mesh(node, rt_mesh3d_new_box(1.0, 1.0, 1.0));
    rt_scene_node3d_set_material(node, rt_material3d_new_color(1, 0, 0));
    rt_scene3d_add(scene, node);

    /* Can't call Draw without a canvas (needs window), but we can
     * verify AABB is computed by checking aabb_min/max via getters */
    void *amin = rt_scene_node3d_get_aabb_min(node);
    void *amax = rt_scene_node3d_get_aabb_max(node);
    EXPECT_NEAR(rt_vec3_x(amin), -0.5, 0.01, "Box AABB min X = -0.5");
    EXPECT_NEAR(rt_vec3_y(amin), -0.5, 0.01, "Box AABB min Y = -0.5");
    EXPECT_NEAR(rt_vec3_z(amin), -0.5, 0.01, "Box AABB min Z = -0.5");
    EXPECT_NEAR(rt_vec3_x(amax), 0.5, 0.01, "Box AABB max X = 0.5");
    EXPECT_NEAR(rt_vec3_y(amax), 0.5, 0.01, "Box AABB max Y = 0.5");
    EXPECT_NEAR(rt_vec3_z(amax), 0.5, 0.01, "Box AABB max Z = 0.5");
}

static void test_frustum_sphere_aabb() {
    /* Sphere AABB should be [-radius, +radius] in all axes */
    void *node = rt_scene_node3d_new();
    extern void *rt_mesh3d_new_sphere(double r, int64_t seg);
    rt_scene_node3d_set_mesh(node, rt_mesh3d_new_sphere(2.0, 8));
    void *amin = rt_scene_node3d_get_aabb_min(node);
    void *amax = rt_scene_node3d_get_aabb_max(node);
    EXPECT_NEAR(rt_vec3_x(amin), -2.0, 0.1, "Sphere AABB min X ≈ -2");
    EXPECT_NEAR(rt_vec3_x(amax), 2.0, 0.1, "Sphere AABB max X ≈ 2");
}

static void test_frustum_plane_aabb() {
    /* Plane AABB should match half-extents */
    void *node = rt_scene_node3d_new();
    extern void *rt_mesh3d_new_plane(double sx, double sz);
    rt_scene_node3d_set_mesh(node, rt_mesh3d_new_plane(6.0, 4.0));
    void *amin = rt_scene_node3d_get_aabb_min(node);
    void *amax = rt_scene_node3d_get_aabb_max(node);
    EXPECT_NEAR(rt_vec3_x(amin), -3.0, 0.01, "Plane AABB min X = -3");
    EXPECT_NEAR(rt_vec3_z(amin), -2.0, 0.01, "Plane AABB min Z = -2");
    EXPECT_NEAR(rt_vec3_x(amax), 3.0, 0.01, "Plane AABB max X = 3");
    EXPECT_NEAR(rt_vec3_z(amax), 2.0, 0.01, "Plane AABB max Z = 2");
    /* Plane is at Y=0, so Y min=max=0 */
    EXPECT_NEAR(rt_vec3_y(amin), 0.0, 0.01, "Plane AABB min Y = 0");
    EXPECT_NEAR(rt_vec3_y(amax), 0.0, 0.01, "Plane AABB max Y = 0");
}

static void test_frustum_no_mesh_no_aabb() {
    /* Node without mesh has zero AABB */
    void *node = rt_scene_node3d_new();
    void *amin = rt_scene_node3d_get_aabb_min(node);
    EXPECT_NEAR(rt_vec3_x(amin), 0.0, 0.01, "No-mesh AABB min X = 0");
}

static void test_node_aabb_refreshes_after_mesh_mutation() {
    void *node = rt_scene_node3d_new();
    void *mesh = rt_mesh3d_new();
    rt_mesh3d_add_vertex(mesh, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0);
    rt_mesh3d_add_vertex(mesh, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0);
    rt_mesh3d_add_vertex(mesh, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0);
    rt_mesh3d_add_triangle(mesh, 0, 1, 2);
    rt_scene_node3d_set_mesh(node, mesh);

    void *amax = rt_scene_node3d_get_aabb_max(node);
    EXPECT_NEAR(rt_vec3_x(amax), 1.0, 0.01, "Initial mesh AABB max X = 1");

    rt_mesh3d_add_vertex(mesh, 2.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0);
    amax = rt_scene_node3d_get_aabb_max(node);
    EXPECT_NEAR(rt_vec3_x(amax), 2.0, 0.01, "Scene node AABB refreshes after mesh mutation");
}

static void test_frustum_culled_count_initial() {
    void *scene = rt_scene3d_new();
    EXPECT_TRUE(rt_scene3d_get_culled_count(scene) == 0, "Initial culled count = 0");
}

static void test_lod_culling_uses_selected_mesh_bounds() {
    vgfx3d_backend_t backend = {};
    backend.name = "opengl";
    backend.begin_frame = scene_test_begin_frame;
    backend.end_frame = scene_test_end_frame;
    backend.submit_draw = scene_test_submit_draw;

    rt_canvas3d canvas;
    init_scene_test_canvas(&canvas, &backend);
    reset_scene_capture();

    void *scene = rt_scene3d_new();
    void *node = rt_scene_node3d_new();
    void *base_mesh = rt_mesh3d_new_box(1.0, 1.0, 1.0);
    void *lod_mesh = rt_mesh3d_new();
    rt_mesh3d_add_vertex(lod_mesh, 5.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0);
    rt_mesh3d_add_vertex(lod_mesh, 6.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 0.0);
    rt_mesh3d_add_vertex(lod_mesh, 5.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0);
    rt_mesh3d_add_triangle(lod_mesh, 0, 1, 2);

    void *material = rt_material3d_new_color(1.0, 1.0, 1.0);
    void *camera = rt_camera3d_new(60.0, 1.0, 0.1, 100.0);
    void *eye = rt_vec3_new(0.0, 0.0, 5.0);
    void *target = rt_vec3_new(0.0, 0.0, 0.0);
    void *up = rt_vec3_new(0.0, 1.0, 0.0);

    rt_scene_node3d_set_mesh(node, base_mesh);
    rt_scene_node3d_set_material(node, material);
    rt_scene_node3d_add_lod(node, 0.0, lod_mesh);
    rt_scene3d_add(scene, node);
    rt_camera3d_look_at(camera, eye, target, up);

    rt_scene3d_draw(scene, &canvas, camera);

    EXPECT_TRUE(g_scene_submit_count == 0,
                "Scene3D culls against the selected LOD mesh bounds, not the base mesh bounds");
    EXPECT_TRUE(
        rt_scene3d_get_culled_count(scene) == 1,
        "Scene3D increments culled count when the selected LOD mesh is outside the frustum");
}

static void test_dynamic_deformation_skips_static_frustum_culling() {
    vgfx3d_backend_t backend = {};
    backend.name = "opengl";
    backend.begin_frame = scene_test_begin_frame;
    backend.end_frame = scene_test_end_frame;
    backend.submit_draw = scene_test_submit_draw;

    rt_canvas3d canvas;
    init_scene_test_canvas(&canvas, &backend);
    reset_scene_capture();

    void *scene = rt_scene3d_new();
    void *node = rt_scene_node3d_new();
    void *mesh = rt_mesh3d_new_box(1.0, 1.0, 1.0);
    void *material = rt_material3d_new_color(1.0, 1.0, 1.0);
    void *camera = rt_camera3d_new(60.0, 1.0, 0.1, 100.0);
    void *eye = rt_vec3_new(0.0, 0.0, 5.0);
    void *target = rt_vec3_new(0.0, 0.0, 0.0);
    void *up = rt_vec3_new(0.0, 1.0, 0.0);

    ((rt_mesh3d *)mesh)->morph_shape_count = 1;
    rt_scene_node3d_set_position(node, 1000.0, 0.0, 0.0);
    rt_scene_node3d_set_mesh(node, mesh);
    rt_scene_node3d_set_material(node, material);
    rt_scene3d_add(scene, node);
    rt_camera3d_look_at(camera, eye, target, up);

    rt_scene3d_draw(scene, &canvas, camera);

    EXPECT_TRUE(g_scene_submit_count == 1,
                "Scene3D draws dynamic meshes instead of culling against stale static bounds");
    EXPECT_TRUE(rt_scene3d_get_culled_count(scene) == 0,
                "Scene3D does not record frustum culls for dynamic-deformation meshes");
}

static void test_parent_animator_drives_child_skinned_meshes() {
    vgfx3d_backend_t backend = {};
    backend.name = "opengl";
    backend.begin_frame = scene_test_begin_frame;
    backend.end_frame = scene_test_end_frame;
    backend.submit_draw = scene_test_submit_draw;

    rt_canvas3d canvas;
    init_scene_test_canvas(&canvas, &backend);
    reset_scene_capture();

    void *scene = rt_scene3d_new();
    void *parent = rt_scene_node3d_new();
    void *child = rt_scene_node3d_new();
    void *mesh = rt_mesh3d_new();
    void *material = rt_material3d_new_color(1.0, 1.0, 1.0);
    void *camera = rt_camera3d_new(60.0, 1.0, 0.1, 100.0);
    void *eye = rt_vec3_new(0.0, 0.0, 5.0);
    void *target = rt_vec3_new(0.0, 0.0, 0.0);
    void *up = rt_vec3_new(0.0, 1.0, 0.0);
    void *bind = rt_mat4_identity();
    void *skel = rt_skeleton3d_new();
    rt_skeleton3d_add_bone(skel, rt_const_cstr("root"), -1, bind);
    rt_skeleton3d_compute_inverse_bind(skel);
    void *controller = rt_anim_controller3d_new(skel);

    rt_mesh3d_add_vertex(mesh, -0.5, -0.5, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0);
    rt_mesh3d_add_vertex(mesh, 0.5, -0.5, 0.0, 0.0, 0.0, 1.0, 1.0, 0.0);
    rt_mesh3d_add_vertex(mesh, 0.0, 0.5, 0.0, 0.0, 0.0, 1.0, 0.5, 1.0);
    rt_mesh3d_add_triangle(mesh, 0, 1, 2);
    rt_mesh3d_set_bone_weights(mesh, 0, 0, 1.0, 0, 0.0, 0, 0.0, 0, 0.0);
    rt_mesh3d_set_bone_weights(mesh, 1, 0, 1.0, 0, 0.0, 0, 0.0, 0, 0.0);
    rt_mesh3d_set_bone_weights(mesh, 2, 0, 1.0, 0, 0.0, 0, 0.0, 0, 0.0);
    ((rt_mesh3d *)mesh)->bone_count = 1;

    rt_scene_node3d_bind_animator(parent, controller);
    rt_scene_node3d_set_mesh(child, mesh);
    rt_scene_node3d_set_material(child, material);
    rt_scene_node3d_add_child(parent, child);
    rt_scene3d_add(scene, parent);
    rt_camera3d_look_at(camera, eye, target, up);

    rt_scene3d_draw(scene, &canvas, camera);

    EXPECT_TRUE(g_scene_submit_count == 1, "Scene3D draws the child skinned mesh");
    EXPECT_TRUE(g_scene_last_bone_count == 1,
                "Scene3D inherits a bound parent animator when drawing child meshes");
}

static void test_scene_draw_includes_node_attached_lights() {
    vgfx3d_backend_t backend = {};
    backend.name = "opengl";
    backend.begin_frame = scene_test_begin_frame;
    backend.end_frame = scene_test_end_frame;
    backend.submit_draw = scene_test_submit_draw;

    rt_canvas3d canvas;
    init_scene_test_canvas(&canvas, &backend);
    reset_scene_capture();

    void *scene = rt_scene3d_new();
    void *mesh_node = rt_scene_node3d_new();
    void *light_node = rt_scene_node3d_new();
    void *mesh = rt_mesh3d_new_box(1.0, 1.0, 1.0);
    void *material = rt_material3d_new_color(1.0, 1.0, 1.0);
    void *camera = rt_camera3d_new(60.0, 1.0, 0.1, 100.0);
    void *eye = rt_vec3_new(0.0, 0.0, 5.0);
    void *target = rt_vec3_new(0.0, 0.0, 0.0);
    void *up = rt_vec3_new(0.0, 1.0, 0.0);
    void *local_pos = rt_vec3_new(0.0, 0.0, 0.0);
    void *light = rt_light3d_new_point(local_pos, 0.25, 0.5, 1.0, 0.2);

    rt_scene_node3d_set_mesh(mesh_node, mesh);
    rt_scene_node3d_set_material(mesh_node, material);
    rt_scene_node3d_set_position(light_node, 2.0, 3.0, 4.0);
    rt_scene_node3d_set_light(light_node, light);
    rt_scene3d_add(scene, mesh_node);
    rt_scene3d_add(scene, light_node);
    rt_camera3d_look_at(camera, eye, target, up);

    rt_scene3d_draw(scene, &canvas, camera);

    EXPECT_TRUE(g_scene_submit_count == 1, "Scene3D draws lit scene geometry");
    EXPECT_TRUE(g_scene_last_light_count == 1, "Scene3D forwards node-attached lights");
    if (g_scene_last_light_count > 0) {
        EXPECT_TRUE(g_scene_last_lights[0].type == 1,
                    "Scene3D preserves node-attached point light type");
        EXPECT_NEAR(g_scene_last_lights[0].position[0],
                    2.0,
                    0.001,
                    "Scene3D transforms node-attached light X position");
        EXPECT_NEAR(g_scene_last_lights[0].position[1],
                    3.0,
                    0.001,
                    "Scene3D transforms node-attached light Y position");
        EXPECT_NEAR(g_scene_last_lights[0].position[2],
                    4.0,
                    0.001,
                    "Scene3D transforms node-attached light Z position");
    }
}

static void test_scene_roundtrip_preserves_node_lights() {
    const char *path = "/tmp/viper_scene_node_light_roundtrip.vscn";
    void *scene = rt_scene3d_new();
    void *node = rt_scene_node3d_new();
    void *pos = rt_vec3_new(0.0, 0.0, 0.0);
    void *dir = rt_vec3_new(0.0, 0.0, -1.0);
    void *light = rt_light3d_new_spot(pos, dir, 0.3, 0.6, 0.9, 0.25, 10.0, 30.0);

    rt_light3d_set_intensity(light, 4.0);
    rt_scene_node3d_set_name(node, rt_const_cstr("lamp"));
    rt_scene_node3d_set_position(node, 1.0, 2.0, 3.0);
    rt_scene_node3d_set_light(node, light);
    rt_scene3d_add(scene, node);

    EXPECT_TRUE(rt_scene3d_save(scene, rt_const_cstr(path)) == 1,
                "Scene3D.Save writes node-light fixtures");
    void *loaded = rt_scene3d_load(rt_const_cstr(path));
    EXPECT_TRUE(loaded != nullptr, "Scene3D.Load reads node-light fixtures");
    if (!loaded)
        return;
    void *loaded_node = rt_scene3d_find(loaded, rt_const_cstr("lamp"));
    rt_light3d *loaded_light = (rt_light3d *)rt_scene_node3d_get_light(loaded_node);
    EXPECT_TRUE(loaded_light != nullptr, "Scene3D.Load restores node-attached lights");
    if (!loaded_light)
        return;
    EXPECT_TRUE(loaded_light->type == 3, "Scene3D.Load restores spot-light type");
    EXPECT_NEAR(loaded_light->color[2], 0.9, 0.001, "Scene3D.Load restores light color");
    EXPECT_NEAR(loaded_light->intensity, 4.0, 0.001, "Scene3D.Load restores light intensity");
    EXPECT_NEAR(loaded_light->attenuation, 0.25, 0.001, "Scene3D.Load restores light attenuation");
    EXPECT_TRUE(loaded_light->inner_cos > loaded_light->outer_cos,
                "Scene3D.Load restores spot cone cosines");
}

static void test_scene_save_rejects_wrong_handle() {
    void *node = rt_scene_node3d_new();
    EXPECT_TRUE(rt_scene3d_save(node, rt_const_cstr("/tmp/viper_scene_wrong_handle.vscn")) == 0,
                "Scene3D.Save rejects non-Scene3D handles");
}

static void test_scene_load_rejects_malformed_json() {
    const char *path = "/tmp/viper_scene_malformed_json.vscn";
    EXPECT_TRUE(write_text_file(path, "{\"format\":\"vscn\", \"nodes\": ["),
                "Malformed VSCN fixture can be written");
    void *loaded = rt_scene3d_load(rt_const_cstr(path));
    EXPECT_TRUE(loaded == nullptr, "Scene3D.Load rejects malformed JSON instead of partial maps");
}

static void test_scene_load_rejects_invalid_node_references() {
    const char *path = "/tmp/viper_scene_invalid_node_ref.vscn";
    const char *json =
        "{\n"
        "  \"format\": \"vscn\",\n"
        "  \"version\": 2,\n"
        "  \"textures\": [],\n"
        "  \"cubemaps\": [],\n"
        "  \"materials\": [],\n"
        "  \"meshes\": [],\n"
        "  \"nodes\": [\n"
        "    {\"name\": \"bad\", \"position\": [0,0,0], \"rotation\": [0,0,0,1], "
        "\"scale\": [1,1,1], \"visible\": true, \"mesh\": 0, \"material\": -1}\n"
        "  ]\n"
        "}\n";
    EXPECT_TRUE(write_text_file(path, json), "Invalid-reference VSCN fixture can be written");
    void *loaded = rt_scene3d_load(rt_const_cstr(path));
    EXPECT_TRUE(loaded == nullptr, "Scene3D.Load rejects node mesh references outside the mesh table");
}

static void test_scene_load_sanitizes_degenerate_rotation() {
    const char *path = "/tmp/viper_scene_degenerate_rotation.vscn";
    const char *json =
        "{\n"
        "  \"format\": \"vscn\",\n"
        "  \"version\": 2,\n"
        "  \"textures\": [],\n"
        "  \"cubemaps\": [],\n"
        "  \"materials\": [],\n"
        "  \"meshes\": [],\n"
        "  \"nodes\": [\n"
        "    {\"name\": \"rot\", \"position\": [0,0,0], \"rotation\": [0,0,0,0], "
        "\"scale\": [1,1,1], \"visible\": true, \"mesh\": -1, \"material\": -1}\n"
        "  ]\n"
        "}\n";
    EXPECT_TRUE(write_text_file(path, json), "Degenerate-rotation VSCN fixture can be written");
    void *loaded = rt_scene3d_load(rt_const_cstr(path));
    EXPECT_TRUE(loaded != nullptr, "Scene3D.Load accepts valid scene with sanitized transform data");
    if (!loaded)
        return;
    void *node = rt_scene3d_find(loaded, rt_const_cstr("rot"));
    void *rotation = rt_scene_node3d_get_rotation(node);
    EXPECT_NEAR(rt_quat_x(rotation), 0.0, 0.001, "Scene3D.Load resets degenerate rotation X");
    EXPECT_NEAR(rt_quat_y(rotation), 0.0, 0.001, "Scene3D.Load resets degenerate rotation Y");
    EXPECT_NEAR(rt_quat_z(rotation), 0.0, 0.001, "Scene3D.Load resets degenerate rotation Z");
    EXPECT_NEAR(rt_quat_w(rotation), 1.0, 0.001, "Scene3D.Load resets degenerate rotation W");
}

static void test_scene_roundtrip_preserves_high_precision_transform() {
    const char *path = "/tmp/viper_scene_precision_roundtrip.vscn";
    void *scene = rt_scene3d_new();
    void *node = rt_scene_node3d_new();
    const double x = 0.000000123456789;
    rt_scene_node3d_set_name(node, rt_const_cstr("precise"));
    rt_scene_node3d_set_position(node, x, -2.5, 3.75);
    rt_scene3d_add(scene, node);

    EXPECT_TRUE(rt_scene3d_save(scene, rt_const_cstr(path)) == 1,
                "Scene3D.Save writes high-precision transform scene");
    void *loaded = rt_scene3d_load(rt_const_cstr(path));
    EXPECT_TRUE(loaded != nullptr, "Scene3D.Load reads high-precision transform scene");
    if (!loaded)
        return;
    void *loaded_node = rt_scene3d_find(loaded, rt_const_cstr("precise"));
    void *pos = rt_scene_node3d_get_position(loaded_node);
    EXPECT_NEAR(rt_vec3_x(pos), x, 1e-12, "Scene3D.Save/Load preserves sub-micro positions");
}

int main() {
    test_create_scene_and_node();
    test_add_remove_child();
    test_scene_remove_ignores_nodes_from_other_scenes();
    test_translation_propagation();
    test_world_position_and_scale_getters();
    test_rotation_propagation();
    test_scale_propagation();
    test_deep_hierarchy();
    test_deep_hierarchy_iterative_traversal();
    test_dirty_flag();
    test_find_by_name();
    test_reparenting();
    test_node_count_tracks_nested_hierarchy_edits();
    test_prevent_cycle();
    test_visibility();
    test_subtree_aabb_includes_child_meshes();
    test_clear();
    test_get_child();
    test_default_transform();
    test_node_sanitizes_nonfinite_transform_and_lod();
    test_node_body_sync_preserves_negative_scale_handedness();

    /* Frustum culling */
    test_frustum_aabb_inside();
    test_frustum_sphere_aabb();
    test_frustum_plane_aabb();
    test_frustum_no_mesh_no_aabb();
    test_node_aabb_refreshes_after_mesh_mutation();
    test_frustum_culled_count_initial();
    test_lod_culling_uses_selected_mesh_bounds();
    test_dynamic_deformation_skips_static_frustum_culling();
    test_parent_animator_drives_child_skinned_meshes();
    test_scene_draw_reuses_active_frame();
    test_scene_draw_culling_uses_canvas_output_aspect();
    test_scene_save_escapes_json_names();
    test_scene_save_serializes_visibility_and_lod_metadata();
    test_scene_roundtrip_loads_shared_assets();
    test_node_animator_handles_large_morph_weight_channels();
    test_node_animator_samples_cubic_translation_channels();
    test_node_animator_slerps_linear_rotation_channels();
    test_node_animation_rejects_invalid_channel_data();
    test_scene_draw_includes_node_attached_lights();
    test_scene_roundtrip_preserves_node_lights();
    test_scene_save_rejects_wrong_handle();
    test_scene_load_rejects_malformed_json();
    test_scene_load_rejects_invalid_node_references();
    test_scene_load_sanitizes_degenerate_rotation();
    test_scene_roundtrip_preserves_high_precision_transform();

    printf("Scene3D tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
