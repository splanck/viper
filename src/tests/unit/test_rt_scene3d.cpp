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
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_internal.h"
#include "rt_scene3d.h"
#include "rt_string.h"
#include "vgfx3d_backend.h"
#include <cassert>
#define _USE_MATH_DEFINES
#include <cmath>
#include <cstdio>
#include <cstring>

extern "C" {
extern void *rt_vec3_new(double x, double y, double z);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);
extern rt_string rt_const_cstr(const char *s);
extern void *rt_quat_from_euler(double pitch, double yaw, double roll);
extern double rt_quat_x(void *q);
extern double rt_quat_y(void *q);
extern double rt_quat_z(void *q);
extern double rt_quat_w(void *q);
extern void rt_camera3d_look_at(void *cam, void *eye, void *target, void *up);
extern void *rt_material3d_new_color(double r, double g, double b);
extern void *rt_camera3d_new(double fov, double aspect, double near, double far);
extern void *rt_mesh3d_new(void);
extern void rt_mesh3d_add_vertex(void *obj,
                                 double x,
                                 double y,
                                 double z,
                                 double nx,
                                 double ny,
                                 double nz,
                                 double u,
                                 double v);
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

static void scene_test_begin_frame(void *, const vgfx3d_camera_params_t *) { g_scene_begin_count++; }
static void scene_test_end_frame(void *) { g_scene_end_count++; }
static void scene_test_submit_draw(void *,
                                   vgfx_window_t,
                                   const vgfx3d_draw_cmd_t *cmd,
                                   const vgfx3d_light_params_t *,
                                   int32_t,
                                   const float *,
                                   int8_t,
                                   int8_t) {
    g_scene_submit_count++;
    g_scene_last_vertices = cmd ? cmd->vertices : nullptr;
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

    EXPECT_TRUE(g_scene_begin_count == 0, "Scene3D.Draw does not nest Begin when a 3D frame is already active");
    EXPECT_TRUE(g_scene_end_count == 0, "Scene3D.Draw does not end an externally-owned frame");
    EXPECT_TRUE(canvas.draw_count == 1,
                "Scene3D.Draw queues scene geometry inside an externally-owned frame");
    EXPECT_TRUE(g_scene_submit_count == 0,
                "Scene3D.Draw defers backend submission until the caller ends the frame");
    EXPECT_TRUE(canvas.in_frame == 1, "Scene3D.Draw leaves the caller-owned frame active");
}

static void test_scene_save_escapes_json_names() {
    void *scene = rt_scene3d_new();
    void *node = rt_scene_node3d_new();
    const char *path = "/tmp/viper_scene_escape_test.vscn";
    const char *name = "quote\"slash\\line\nbreak";

    rt_scene_node3d_set_name(node, rt_const_cstr(name));
    rt_scene3d_add(scene, node);

    EXPECT_TRUE(rt_scene3d_save(scene, rt_const_cstr(path)) == 1, "Scene3D.Save writes the scene file");

    FILE *f = fopen(path, "rb");
    EXPECT_TRUE(f != nullptr, "Scene3D.Save output can be reopened");
    if (!f)
        return;
    char buf[1024];
    size_t bytes = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[bytes] = '\0';

    EXPECT_TRUE(std::strstr(buf, "\"name\": \"quote\\\"slash\\\\line\\nbreak\"") != nullptr,
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

    FILE *f = fopen(path, "rb");
    EXPECT_TRUE(f != nullptr, "Scene3D.Save metadata output can be reopened");
    if (!f)
        return;
    char buf[2048];
    size_t bytes = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[bytes] = '\0';

    EXPECT_TRUE(std::strstr(buf, "\"visible\": false") != nullptr,
                "Scene3D.Save serializes node visibility");
    EXPECT_TRUE(std::strstr(buf, "\"hasMesh\": true") != nullptr,
                "Scene3D.Save serializes mesh presence");
    EXPECT_TRUE(std::strstr(buf, "\"hasMaterial\": true") != nullptr,
                "Scene3D.Save serializes material presence");
    EXPECT_TRUE(std::strstr(buf, "\"lod\": [") != nullptr,
                "Scene3D.Save serializes LOD metadata");
    EXPECT_TRUE(std::strstr(buf, "\"distance\": 10.000000") != nullptr,
                "Scene3D.Save serializes LOD distances");
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
    EXPECT_TRUE(rt_scene3d_get_culled_count(scene) == 1,
                "Scene3D increments culled count when the selected LOD mesh is outside the frustum");
}

int main() {
    test_create_scene_and_node();
    test_add_remove_child();
    test_translation_propagation();
    test_rotation_propagation();
    test_scale_propagation();
    test_deep_hierarchy();
    test_dirty_flag();
    test_find_by_name();
    test_reparenting();
    test_prevent_cycle();
    test_visibility();
    test_clear();
    test_get_child();
    test_default_transform();

    /* Frustum culling */
    test_frustum_aabb_inside();
    test_frustum_sphere_aabb();
    test_frustum_plane_aabb();
    test_frustum_no_mesh_no_aabb();
    test_frustum_culled_count_initial();
    test_lod_culling_uses_selected_mesh_bounds();
    test_scene_draw_reuses_active_frame();
    test_scene_save_escapes_json_names();
    test_scene_save_serializes_visibility_and_lod_metadata();

    printf("Scene3D tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
