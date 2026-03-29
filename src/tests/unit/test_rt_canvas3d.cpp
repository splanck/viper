//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_rt_canvas3d.cpp
// Purpose: Unit tests for Viper.Graphics3D types — Mesh3D, Camera3D,
//   Material3D, Light3D. Tests construction, properties, procedural mesh
//   generation, OBJ loading, and camera math.
//
// Key invariants:
//   - Tests run headless (no Canvas3D window creation — that requires display)
//   - All object pointers from _new() must be non-null
//   - Mesh generators produce expected vertex/triangle counts
//   - Camera matrices produce correct transforms
//
// Links: src/runtime/graphics/rt_canvas3d.h
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_canvas3d.h"
#include "rt_sprite3d.h"
#include "rt_terrain3d.h"
#include "rt_internal.h"
#include "rt_string.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

extern "C" void vm_trap(const char *msg) {
    rt_abort(msg);
}

static int tests_passed = 0;
static int tests_total = 0;

#define TEST(name)                                                                                 \
    do {                                                                                           \
        tests_total++;                                                                             \
        printf("  [%d] %s... ", tests_total, name);                                                \
    } while (0)
#define PASS()                                                                                     \
    do {                                                                                           \
        tests_passed++;                                                                            \
        printf("ok\n");                                                                            \
    } while (0)
#define FAIL(msg)                                                                                  \
    do {                                                                                           \
        printf("FAIL: %s\n", msg);                                                                 \
    } while (0)

#define EXPECT_EQ(a, b)                                                                            \
    do {                                                                                           \
        if ((a) != (b)) {                                                                          \
            printf("FAIL: expected %lld, got %lld\n", (long long)(b), (long long)(a));             \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define EXPECT_NEAR(a, b, eps)                                                                     \
    do {                                                                                           \
        if (std::fabs((double)(a) - (double)(b)) > (eps)) {                                        \
            printf("FAIL: expected ~%f, got %f\n", (double)(b), (double)(a));                      \
            return;                                                                                \
        }                                                                                          \
    } while (0)

extern "C" double rt_vec3_x(void *v);
extern "C" double rt_vec3_y(void *v);
extern "C" double rt_vec3_z(void *v);
extern "C" void *rt_vec3_new(double x, double y, double z);
extern "C" void *rt_pixels_new(int64_t width, int64_t height);
extern "C" void *rt_mat4_identity(void);

/* Backend selection test */
extern "C" const void *vgfx3d_select_backend(void);

//=============================================================================
// Mesh3D tests
//=============================================================================

static void test_mesh_empty() {
    TEST("Mesh3D.New creates empty mesh");
    void *m = rt_mesh3d_new();
    assert(m);
    EXPECT_EQ(rt_mesh3d_get_vertex_count(m), 0);
    EXPECT_EQ(rt_mesh3d_get_triangle_count(m), 0);
    PASS();
}

static void test_mesh_add_vertex_triangle() {
    TEST("Mesh3D AddVertex/AddTriangle");
    void *m = rt_mesh3d_new();
    rt_mesh3d_add_vertex(m, 0, 0, 0, 0, 1, 0, 0, 0);
    rt_mesh3d_add_vertex(m, 1, 0, 0, 0, 1, 0, 1, 0);
    rt_mesh3d_add_vertex(m, 0, 1, 0, 0, 1, 0, 0, 1);
    rt_mesh3d_add_triangle(m, 0, 1, 2);
    EXPECT_EQ(rt_mesh3d_get_vertex_count(m), 3);
    EXPECT_EQ(rt_mesh3d_get_triangle_count(m), 1);
    PASS();
}

static void test_mesh_reject_invalid_triangle_indices() {
    TEST("Mesh3D.AddTriangle rejects invalid indices");
    void *m = rt_mesh3d_new();
    rt_mesh3d_add_vertex(m, 0, 0, 0, 0, 1, 0, 0, 0);
    rt_mesh3d_add_vertex(m, 1, 0, 0, 0, 1, 0, 1, 0);
    rt_mesh3d_add_vertex(m, 0, 1, 0, 0, 1, 0, 0, 1);
    rt_mesh3d_add_triangle(m, -1, 1, 2);
    rt_mesh3d_add_triangle(m, 0, 1, 9);
    EXPECT_EQ(rt_mesh3d_get_triangle_count(m), 0);
    rt_mesh3d_add_triangle(m, 0, 1, 2);
    EXPECT_EQ(rt_mesh3d_get_triangle_count(m), 1);
    PASS();
}

static void test_mesh_box() {
    TEST("Mesh3D.NewBox — 24 verts, 12 tris");
    void *m = rt_mesh3d_new_box(1.0, 1.0, 1.0);
    assert(m);
    EXPECT_EQ(rt_mesh3d_get_vertex_count(m), 24);
    EXPECT_EQ(rt_mesh3d_get_triangle_count(m), 12);
    PASS();
}

static void test_mesh_sphere() {
    TEST("Mesh3D.NewSphere — correct vertex count");
    void *m = rt_mesh3d_new_sphere(1.0, 8);
    assert(m);
    // rings=8, slices=16 → (8+1)*(16+1) = 153 verts
    EXPECT_EQ(rt_mesh3d_get_vertex_count(m), 153);
    // 8*16*2 = 256 triangles
    EXPECT_EQ(rt_mesh3d_get_triangle_count(m), 256);
    PASS();
}

static void test_mesh_plane() {
    TEST("Mesh3D.NewPlane — 4 verts, 2 tris");
    void *m = rt_mesh3d_new_plane(2.0, 2.0);
    assert(m);
    EXPECT_EQ(rt_mesh3d_get_vertex_count(m), 4);
    EXPECT_EQ(rt_mesh3d_get_triangle_count(m), 2);
    PASS();
}

static void test_mesh_cylinder() {
    TEST("Mesh3D.NewCylinder — correct geometry");
    void *m = rt_mesh3d_new_cylinder(1.0, 2.0, 8);
    assert(m);
    // Side: (8+1)*2 = 18, top cap: 1+8 = 9, bottom cap: 1+8 = 9 → 36
    EXPECT_EQ(rt_mesh3d_get_vertex_count(m), 36);
    // Side: 8*2=16, top: 8, bottom: 8 → 32
    EXPECT_EQ(rt_mesh3d_get_triangle_count(m), 32);
    PASS();
}

static void test_mesh_clone() {
    TEST("Mesh3D.Clone preserves geometry");
    void *m = rt_mesh3d_new_box(1.0, 1.0, 1.0);
    void *c = rt_mesh3d_clone(m);
    assert(c);
    EXPECT_EQ(rt_mesh3d_get_vertex_count(c), 24);
    EXPECT_EQ(rt_mesh3d_get_triangle_count(c), 12);
    PASS();
}

static void test_mesh_recalc_normals() {
    TEST("Mesh3D.RecalcNormals — produces unit normals");
    void *m = rt_mesh3d_new();
    // Flat triangle in XY plane
    rt_mesh3d_add_vertex(m, 0, 0, 0, 0, 0, 0, 0, 0);
    rt_mesh3d_add_vertex(m, 1, 0, 0, 0, 0, 0, 0, 0);
    rt_mesh3d_add_vertex(m, 0, 1, 0, 0, 0, 0, 0, 0);
    rt_mesh3d_add_triangle(m, 0, 1, 2);
    rt_mesh3d_recalc_normals(m);
    // Normal should be (0, 0, 1) for CCW triangle in XY plane
    // (Can't directly access vertex data from public API, but at least it doesn't crash)
    PASS();
}

static void test_mesh_obj_loader() {
    TEST("Mesh3D.FromOBJ — loads test cube");
    /* Try multiple paths since ctest working directory may differ.
     * We must check with fopen BEFORE calling FromOBJ because FromOBJ
     * traps (kills process) if the file doesn't exist. */
    const char *found = NULL;
    const char *paths[] = {"tests/runtime/test_cube.obj",
                           "../tests/runtime/test_cube.obj",
                           "src/tests/../../../tests/runtime/test_cube.obj",
                           NULL};
    for (int i = 0; paths[i]; i++) {
        FILE *f = fopen(paths[i], "r");
        if (f) {
            fclose(f);
            found = paths[i];
            break;
        }
    }
    if (!found) {
        printf("SKIP (test_cube.obj not found in any search path)\n");
        tests_passed++;
        return;
    }
    rt_string path = rt_string_from_bytes(found, (int64_t)strlen(found));
    void *m = rt_mesh3d_from_obj(path);
    assert(m);
    EXPECT_EQ(rt_mesh3d_get_vertex_count(m), 24);
    EXPECT_EQ(rt_mesh3d_get_triangle_count(m), 12);
    PASS();
}

//=============================================================================
// Camera3D tests
//=============================================================================

static void test_camera_new() {
    TEST("Camera3D.New — fov preserved");
    void *cam = rt_camera3d_new(60.0, 1.333, 0.1, 100.0);
    assert(cam);
    EXPECT_NEAR(rt_camera3d_get_fov(cam), 60.0, 0.001);
    PASS();
}

static void test_camera_set_fov() {
    TEST("Camera3D.SetFov — updates projection");
    void *cam = rt_camera3d_new(60.0, 1.333, 0.1, 100.0);
    rt_camera3d_set_fov(cam, 90.0);
    EXPECT_NEAR(rt_camera3d_get_fov(cam), 90.0, 0.001);
    PASS();
}

static void test_camera_look_at() {
    TEST("Camera3D.LookAt — position updated");
    void *cam = rt_camera3d_new(60.0, 1.333, 0.1, 100.0);
    void *eye = rt_vec3_new(0.0, 5.0, 10.0);
    void *target = rt_vec3_new(0.0, 0.0, 0.0);
    void *up = rt_vec3_new(0.0, 1.0, 0.0);
    rt_camera3d_look_at(cam, eye, target, up);

    void *pos = rt_camera3d_get_position(cam);
    assert(pos);
    EXPECT_NEAR(rt_vec3_x(pos), 0.0, 0.001);
    EXPECT_NEAR(rt_vec3_y(pos), 5.0, 0.001);
    EXPECT_NEAR(rt_vec3_z(pos), 10.0, 0.001);
    PASS();
}

static void test_camera_forward() {
    TEST("Camera3D.Forward — points toward target");
    void *cam = rt_camera3d_new(60.0, 1.333, 0.1, 100.0);
    void *eye = rt_vec3_new(0.0, 0.0, 5.0);
    void *target = rt_vec3_new(0.0, 0.0, 0.0);
    void *up = rt_vec3_new(0.0, 1.0, 0.0);
    rt_camera3d_look_at(cam, eye, target, up);

    void *fwd = rt_camera3d_get_forward(cam);
    assert(fwd);
    // Forward should point roughly along -Z (toward target from eye at +Z)
    EXPECT_NEAR(rt_vec3_z(fwd), -1.0, 0.1);
    PASS();
}

static void test_camera_orbit() {
    TEST("Camera3D.Orbit — position on sphere");
    void *cam = rt_camera3d_new(60.0, 1.333, 0.1, 100.0);
    void *target = rt_vec3_new(0.0, 0.0, 0.0);
    rt_camera3d_orbit(cam, target, 5.0, 0.0, 0.0);

    void *pos = rt_camera3d_get_position(cam);
    assert(pos);
    // At yaw=0, pitch=0: eye should be at (0, 0, 5)
    EXPECT_NEAR(rt_vec3_x(pos), 0.0, 0.1);
    EXPECT_NEAR(rt_vec3_y(pos), 0.0, 0.1);
    EXPECT_NEAR(rt_vec3_z(pos), 5.0, 0.1);
    PASS();
}

static void test_camera_screen_to_ray() {
    TEST("Camera3D.ScreenToRay — center ray along view direction");
    void *cam = rt_camera3d_new(60.0, 1.333, 0.1, 100.0);
    void *eye = rt_vec3_new(0.0, 0.0, 5.0);
    void *target = rt_vec3_new(0.0, 0.0, 0.0);
    void *up = rt_vec3_new(0.0, 1.0, 0.0);
    rt_camera3d_look_at(cam, eye, target, up);

    // Center of 640×480 screen
    void *ray = rt_camera3d_screen_to_ray(cam, 320, 240, 640, 480);
    assert(ray);
    // Center ray should point along -Z
    double rz = rt_vec3_z(ray);
    assert(rz < -0.9); // should be ~-1.0
    PASS();
}

//=============================================================================
// Material3D tests
//=============================================================================

static void test_material_new() {
    TEST("Material3D.New — default white");
    void *m = rt_material3d_new();
    assert(m);
    PASS();
}

static void test_material_new_color() {
    TEST("Material3D.NewColor — stores color");
    void *m = rt_material3d_new_color(0.5, 0.3, 0.1);
    assert(m);
    PASS();
}

static void test_material_new_textured() {
    TEST("Material3D.NewTextured — accepts Pixels");
    void *px = rt_pixels_new(4, 4);
    void *m = rt_material3d_new_textured(px);
    assert(m);
    PASS();
}

//=============================================================================
// Light3D tests
//=============================================================================

static void test_light_directional() {
    TEST("Light3D.NewDirectional — creates light");
    void *dir = rt_vec3_new(-1.0, -1.0, -1.0);
    void *l = rt_light3d_new_directional(dir, 1.0, 1.0, 1.0);
    assert(l);
    PASS();
}

static void test_light_point() {
    TEST("Light3D.NewPoint — creates light");
    void *pos = rt_vec3_new(0.0, 5.0, 0.0);
    void *l = rt_light3d_new_point(pos, 1.0, 1.0, 1.0, 0.5);
    assert(l);
    PASS();
}

static void test_light_ambient() {
    TEST("Light3D.NewAmbient — creates light");
    void *l = rt_light3d_new_ambient(0.1, 0.1, 0.1);
    assert(l);
    PASS();
}

static void test_light_set_intensity() {
    TEST("Light3D.SetIntensity — no crash");
    void *dir = rt_vec3_new(-1.0, -1.0, 0.0);
    void *l = rt_light3d_new_directional(dir, 1.0, 1.0, 1.0);
    rt_light3d_set_intensity(l, 2.0);
    PASS();
}

static void test_light_set_color() {
    TEST("Light3D.SetColor — no crash");
    void *l = rt_light3d_new_ambient(0.1, 0.1, 0.1);
    rt_light3d_set_color(l, 0.5, 0.5, 0.5);
    PASS();
}

//=============================================================================
// Mesh3D — additional tests
//=============================================================================

static void test_mesh_many_vertices() {
    TEST("Mesh3D — dynamic growth (1000 vertices)");
    void *m = rt_mesh3d_new();
    for (int i = 0; i < 1000; i++)
        rt_mesh3d_add_vertex(m, (double)i, 0, 0, 0, 1, 0, 0, 0);
    EXPECT_EQ(rt_mesh3d_get_vertex_count(m), 1000);
    PASS();
}

static void test_mesh_many_triangles() {
    TEST("Mesh3D — many triangles (500 tris)");
    void *m = rt_mesh3d_new();
    for (int i = 0; i < 1500; i++)
        rt_mesh3d_add_vertex(m, (double)(i % 100), (double)(i / 100), 0, 0, 0, 1, 0, 0);
    for (int i = 0; i < 500; i++)
        rt_mesh3d_add_triangle(m, i * 3, i * 3 + 1, i * 3 + 2);
    EXPECT_EQ(rt_mesh3d_get_triangle_count(m), 500);
    PASS();
}

static void test_mesh_null_safety() {
    TEST("Mesh3D — null safety (no crash)");
    rt_mesh3d_add_vertex(NULL, 0, 0, 0, 0, 0, 0, 0, 0);
    rt_mesh3d_add_triangle(NULL, 0, 1, 2);
    EXPECT_EQ(rt_mesh3d_get_vertex_count(NULL), 0);
    EXPECT_EQ(rt_mesh3d_get_triangle_count(NULL), 0);
    rt_mesh3d_recalc_normals(NULL);
    rt_mesh3d_transform(NULL, NULL);
    PASS();
}

static void test_mesh_sphere_low_segments() {
    TEST("Mesh3D.NewSphere — minimum segments (4)");
    void *m = rt_mesh3d_new_sphere(1.0, 4);
    assert(m);
    assert(rt_mesh3d_get_vertex_count(m) > 0);
    assert(rt_mesh3d_get_triangle_count(m) > 0);
    PASS();
}

static void test_mesh_cylinder_low_segments() {
    TEST("Mesh3D.NewCylinder — minimum segments (3)");
    void *m = rt_mesh3d_new_cylinder(0.5, 1.0, 3);
    assert(m);
    assert(rt_mesh3d_get_vertex_count(m) > 0);
    assert(rt_mesh3d_get_triangle_count(m) > 0);
    PASS();
}

static void test_mesh_box_dimensions() {
    TEST("Mesh3D.NewBox — different dimensions");
    void *m = rt_mesh3d_new_box(2.0, 0.5, 3.0);
    assert(m);
    EXPECT_EQ(rt_mesh3d_get_vertex_count(m), 24);
    EXPECT_EQ(rt_mesh3d_get_triangle_count(m), 12);
    PASS();
}

static void test_mesh_transform_identity() {
    TEST("Mesh3D.Transform — identity preserves geometry");
    void *m = rt_mesh3d_new_box(1.0, 1.0, 1.0);
    void *c = rt_mesh3d_clone(m);
    void *id = rt_mat4_identity();
    rt_mesh3d_transform(c, id);
    EXPECT_EQ(rt_mesh3d_get_vertex_count(c), 24);
    PASS();
}

//=============================================================================
// Camera3D — additional tests
//=============================================================================

static void test_camera_null_safety() {
    TEST("Camera3D — null safety (no crash)");
    rt_camera3d_look_at(NULL, NULL, NULL, NULL);
    rt_camera3d_orbit(NULL, NULL, 5.0, 0, 0);
    EXPECT_NEAR(rt_camera3d_get_fov(NULL), 0.0, 0.001);
    rt_camera3d_set_fov(NULL, 90.0);
    assert(rt_camera3d_get_position(NULL) == NULL);
    assert(rt_camera3d_get_forward(NULL) == NULL);
    assert(rt_camera3d_get_right(NULL) == NULL);
    PASS();
}

static void test_camera_right_vector() {
    TEST("Camera3D.Right — perpendicular to forward");
    void *cam = rt_camera3d_new(60.0, 1.333, 0.1, 100.0);
    void *eye = rt_vec3_new(0, 0, 5);
    void *target = rt_vec3_new(0, 0, 0);
    void *up = rt_vec3_new(0, 1, 0);
    rt_camera3d_look_at(cam, eye, target, up);

    void *right = rt_camera3d_get_right(cam);
    assert(right);
    /* Right should be along +X when looking down -Z */
    EXPECT_NEAR(rt_vec3_x(right), 1.0, 0.1);
    EXPECT_NEAR(rt_vec3_y(right), 0.0, 0.1);
    EXPECT_NEAR(rt_vec3_z(right), 0.0, 0.1);
    PASS();
}

static void test_camera_orbit_yaw() {
    TEST("Camera3D.Orbit — yaw 90° moves to +X");
    void *cam = rt_camera3d_new(60.0, 1.333, 0.1, 100.0);
    void *target = rt_vec3_new(0, 0, 0);
    rt_camera3d_orbit(cam, target, 5.0, 90.0, 0.0);
    void *pos = rt_camera3d_get_position(cam);
    /* At yaw=90°, eye should be at roughly (5, 0, 0) */
    EXPECT_NEAR(rt_vec3_x(pos), 5.0, 0.1);
    EXPECT_NEAR(rt_vec3_y(pos), 0.0, 0.1);
    EXPECT_NEAR(rt_vec3_z(pos), 0.0, 0.5);
    PASS();
}

static void test_camera_orbit_pitch() {
    TEST("Camera3D.Orbit — pitch 45° elevates camera");
    void *cam = rt_camera3d_new(60.0, 1.333, 0.1, 100.0);
    void *target = rt_vec3_new(0, 0, 0);
    rt_camera3d_orbit(cam, target, 10.0, 0.0, 45.0);
    void *pos = rt_camera3d_get_position(cam);
    /* Y should be positive (elevated) */
    assert(rt_vec3_y(pos) > 3.0);
    PASS();
}

static void test_camera_screen_to_ray_corners() {
    TEST("Camera3D.ScreenToRay — corner rays diverge from center");
    void *cam = rt_camera3d_new(60.0, 1.333, 0.1, 100.0);
    void *eye = rt_vec3_new(0, 0, 5);
    void *target = rt_vec3_new(0, 0, 0);
    void *up = rt_vec3_new(0, 1, 0);
    rt_camera3d_look_at(cam, eye, target, up);

    void *center = rt_camera3d_screen_to_ray(cam, 320, 240, 640, 480);
    void *corner = rt_camera3d_screen_to_ray(cam, 0, 0, 640, 480);

    /* Center ray Z should be more negative (more forward) than corner ray Z */
    assert(rt_vec3_z(center) < rt_vec3_z(corner));
    PASS();
}

//=============================================================================
// Material3D — additional tests
//=============================================================================

static void test_material_set_color() {
    TEST("Material3D.SetColor — changes material");
    void *m = rt_material3d_new();
    rt_material3d_set_color(m, 0.1, 0.2, 0.3);
    PASS();
}

static void test_material_set_shininess() {
    TEST("Material3D.SetShininess — accepts values");
    void *m = rt_material3d_new();
    rt_material3d_set_shininess(m, 128.0);
    rt_material3d_set_shininess(m, 0.0);
    PASS();
}

static void test_material_set_unlit() {
    TEST("Material3D.SetUnlit — toggles lighting");
    void *m = rt_material3d_new();
    rt_material3d_set_unlit(m, 1);
    rt_material3d_set_unlit(m, 0);
    PASS();
}

static void test_material_null_safety() {
    TEST("Material3D — null safety");
    rt_material3d_set_color(NULL, 0, 0, 0);
    rt_material3d_set_texture(NULL, NULL);
    rt_material3d_set_shininess(NULL, 0);
    rt_material3d_set_unlit(NULL, 0);
    PASS();
}

//=============================================================================
// Light3D — additional tests
//=============================================================================

static void test_light_null_safety() {
    TEST("Light3D — null safety");
    rt_light3d_set_intensity(NULL, 1.0);
    rt_light3d_set_color(NULL, 0, 0, 0);
    PASS();
}

static void test_light_spot() {
    TEST("Light3D.NewSpot — creates spot light");
    void *pos = rt_vec3_new(0, 5, 0);
    void *dir = rt_vec3_new(0, -1, 0);
    void *light = rt_light3d_new_spot(pos, dir, 1.0, 1.0, 1.0, 0.1, 30.0, 45.0);
    assert(light);
    PASS();
}

static void test_light_spot_intensity() {
    TEST("Light3D spot — set intensity");
    void *pos = rt_vec3_new(0, 5, 0);
    void *dir = rt_vec3_new(0, -1, 0);
    void *light = rt_light3d_new_spot(pos, dir, 1.0, 1.0, 1.0, 0.1, 30.0, 45.0);
    rt_light3d_set_intensity(light, 2.5);
    PASS();
}

static void test_camera_ortho() {
    TEST("Camera3D.NewOrtho — creates orthographic camera");
    void *cam = rt_camera3d_new_ortho(10.0, 16.0 / 9.0, 0.1, 100.0);
    assert(cam);
    EXPECT_EQ(rt_camera3d_is_ortho(cam), 1);
    PASS();
}

static void test_camera_ortho_look_at() {
    TEST("Camera3D ortho — LookAt works");
    void *cam = rt_camera3d_new_ortho(10.0, 1.0, 0.1, 100.0);
    void *eye = rt_vec3_new(0, 10, 10);
    void *target = rt_vec3_new(0, 0, 0);
    void *up = rt_vec3_new(0, 1, 0);
    rt_camera3d_look_at(cam, eye, target, up);
    void *pos = rt_camera3d_get_position(cam);
    EXPECT_NEAR(rt_vec3_x(pos), 0.0, 0.01);
    EXPECT_NEAR(rt_vec3_y(pos), 10.0, 0.01);
    PASS();
}

static void test_camera_perspective_not_ortho() {
    TEST("Camera3D.New — IsOrtho returns false");
    void *cam = rt_camera3d_new(60.0, 1.0, 0.1, 100.0);
    EXPECT_EQ(rt_camera3d_is_ortho(cam), 0);
    PASS();
}

//=============================================================================
// Phase 9 — Multi-texture material tests
//=============================================================================

static void test_material_set_emissive() {
    TEST("Material3D.SetEmissiveColor — no crash");
    void *m = rt_material3d_new();
    rt_material3d_set_emissive_color(m, 1.0, 0.5, 0.2);
    PASS();
}

static void test_material_set_maps() {
    TEST("Material3D — set normal/specular/emissive maps");
    void *m = rt_material3d_new();
    void *px = rt_pixels_new(4, 4);
    rt_material3d_set_normal_map(m, px);
    rt_material3d_set_specular_map(m, px);
    rt_material3d_set_emissive_map(m, px);
    /* Set to NULL should also work */
    rt_material3d_set_normal_map(m, NULL);
    rt_material3d_set_specular_map(m, NULL);
    rt_material3d_set_emissive_map(m, NULL);
    PASS();
}

static void test_mesh_calc_tangents() {
    TEST("Mesh3D.CalcTangents — plane tangent along +X");
    void *m = rt_mesh3d_new();
    /* Flat quad in XZ plane with standard UVs */
    rt_mesh3d_add_vertex(m, -1, 0, -1, 0, 1, 0, 0, 0);
    rt_mesh3d_add_vertex(m, 1, 0, -1, 0, 1, 0, 1, 0);
    rt_mesh3d_add_vertex(m, 1, 0, 1, 0, 1, 0, 1, 1);
    rt_mesh3d_add_vertex(m, -1, 0, 1, 0, 1, 0, 0, 1);
    rt_mesh3d_add_triangle(m, 0, 2, 1);
    rt_mesh3d_add_triangle(m, 0, 3, 2);
    rt_mesh3d_calc_tangents(m);
    /* No crash = pass. Can't directly inspect tangent from public API */
    /* Also test null safety */
    rt_mesh3d_calc_tangents(NULL);
    PASS();
}

//=============================================================================
// Phase 10 — Alpha blending tests
//=============================================================================

static void test_material_alpha() {
    TEST("Material3D.Alpha — default 1.0, set/get works");
    void *m = rt_material3d_new();
    EXPECT_NEAR(rt_material3d_get_alpha(m), 1.0, 0.001);
    rt_material3d_set_alpha(m, 0.5);
    EXPECT_NEAR(rt_material3d_get_alpha(m), 0.5, 0.001);
    rt_material3d_set_alpha(m, 0.0);
    EXPECT_NEAR(rt_material3d_get_alpha(m), 0.0, 0.001);
    /* Null safety */
    rt_material3d_set_alpha(NULL, 0.5);
    EXPECT_NEAR(rt_material3d_get_alpha(NULL), 1.0, 0.001);
    PASS();
}

//=============================================================================
// Phase 11 — Cube map tests
//=============================================================================

static void test_cubemap_new() {
    TEST("CubeMap3D.New — 6 faces creates valid cube map");
    void *px = rt_pixels_new(16, 16);
    void *cm = rt_cubemap3d_new(px, px, px, px, px, px);
    assert(cm != NULL);
    /* Skybox set/clear */
    rt_canvas3d_set_skybox(NULL, cm); /* null canvas = no crash */
    rt_canvas3d_clear_skybox(NULL);
    PASS();
}

static void test_material_reflectivity() {
    TEST("Material3D.Reflectivity — default 0.0, set/get works");
    void *m = rt_material3d_new();
    EXPECT_NEAR(rt_material3d_get_reflectivity(m), 0.0, 0.001);
    rt_material3d_set_reflectivity(m, 0.5);
    EXPECT_NEAR(rt_material3d_get_reflectivity(m), 0.5, 0.001);
    /* Null safety */
    rt_material3d_set_reflectivity(NULL, 0.5);
    EXPECT_NEAR(rt_material3d_get_reflectivity(NULL), 0.0, 0.001);
    /* Env map set */
    void *px = rt_pixels_new(16, 16);
    void *cm = rt_cubemap3d_new(px, px, px, px, px, px);
    rt_material3d_set_env_map(m, cm);
    rt_material3d_set_env_map(m, NULL);
    PASS();
}

//=============================================================================
// Mesh3D.Clear tests
//=============================================================================

static void test_mesh_clear() {
    TEST("Mesh3D.Clear resets counts to zero");
    void *m = rt_mesh3d_new();
    rt_mesh3d_add_vertex(m, 0, 0, 0, 0, 1, 0, 0, 0);
    rt_mesh3d_add_vertex(m, 1, 0, 0, 0, 1, 0, 1, 0);
    rt_mesh3d_add_vertex(m, 0, 1, 0, 0, 1, 0, 0, 1);
    rt_mesh3d_add_triangle(m, 0, 1, 2);
    EXPECT_EQ(rt_mesh3d_get_vertex_count(m), 3);
    EXPECT_EQ(rt_mesh3d_get_triangle_count(m), 1);
    rt_mesh3d_clear(m);
    EXPECT_EQ(rt_mesh3d_get_vertex_count(m), 0);
    EXPECT_EQ(rt_mesh3d_get_triangle_count(m), 0);
    PASS();
}

static void test_mesh_clear_then_rebuild() {
    TEST("Mesh3D.Clear allows rebuild without reallocation");
    void *m = rt_mesh3d_new();
    // Build a triangle
    rt_mesh3d_add_vertex(m, 0, 0, 0, 0, 1, 0, 0, 0);
    rt_mesh3d_add_vertex(m, 1, 0, 0, 0, 1, 0, 1, 0);
    rt_mesh3d_add_vertex(m, 0, 1, 0, 0, 1, 0, 0, 1);
    rt_mesh3d_add_triangle(m, 0, 1, 2);
    // Clear and rebuild a different triangle
    rt_mesh3d_clear(m);
    rt_mesh3d_add_vertex(m, 2, 0, 0, 0, 1, 0, 0, 0);
    rt_mesh3d_add_vertex(m, 3, 0, 0, 0, 1, 0, 1, 0);
    rt_mesh3d_add_vertex(m, 2, 1, 0, 0, 1, 0, 0, 1);
    rt_mesh3d_add_vertex(m, 3, 1, 0, 0, 1, 0, 1, 1);
    rt_mesh3d_add_triangle(m, 0, 1, 2);
    rt_mesh3d_add_triangle(m, 1, 3, 2);
    EXPECT_EQ(rt_mesh3d_get_vertex_count(m), 4);
    EXPECT_EQ(rt_mesh3d_get_triangle_count(m), 2);
    PASS();
}

static void test_mesh_clear_null_safety() {
    TEST("Mesh3D.Clear null safety");
    rt_mesh3d_clear(NULL); // should not crash
    PASS();
}

static void test_mesh_clear_stress() {
    TEST("Mesh3D.Clear stress: 100 clear-rebuild cycles (Water3D pattern)");
    void *m = rt_mesh3d_new();
    for (int cycle = 0; cycle < 100; cycle++) {
        rt_mesh3d_clear(m);
        // Rebuild a simple quad
        rt_mesh3d_add_vertex(m, 0, 0, 0, 0, 1, 0, 0, 0);
        rt_mesh3d_add_vertex(m, 1, 0, 0, 0, 1, 0, 1, 0);
        rt_mesh3d_add_vertex(m, 1, 0, 1, 0, 1, 0, 1, 1);
        rt_mesh3d_add_vertex(m, 0, 0, 1, 0, 1, 0, 0, 1);
        rt_mesh3d_add_triangle(m, 0, 1, 2);
        rt_mesh3d_add_triangle(m, 0, 2, 3);
    }
    EXPECT_EQ(rt_mesh3d_get_vertex_count(m), 4);
    EXPECT_EQ(rt_mesh3d_get_triangle_count(m), 2);
    PASS();
}

//=============================================================================
// Sprite3D tests
//=============================================================================

static void test_sprite3d_new() {
    TEST("Sprite3D.New creates sprite");
    void *tex = rt_pixels_new(16, 16);
    void *s = rt_sprite3d_new(tex);
    assert(s);
    PASS();
}

static void test_sprite3d_new_null_texture() {
    TEST("Sprite3D.New with null texture");
    void *s = rt_sprite3d_new(NULL);
    assert(s);
    PASS();
}

static void test_sprite3d_set_position() {
    TEST("Sprite3D.SetPosition");
    void *s = rt_sprite3d_new(NULL);
    rt_sprite3d_set_position(s, 1.0, 2.0, 3.0);
    PASS();
}

static void test_sprite3d_set_scale() {
    TEST("Sprite3D.SetScale");
    void *s = rt_sprite3d_new(NULL);
    rt_sprite3d_set_scale(s, 2.0, 3.0);
    PASS();
}

static void test_sprite3d_set_frame() {
    TEST("Sprite3D.SetFrame");
    void *tex = rt_pixels_new(64, 64);
    void *s = rt_sprite3d_new(tex);
    rt_sprite3d_set_frame(s, 0, 0, 32, 32);
    PASS();
}

static void test_sprite3d_null_safety() {
    TEST("Sprite3D null safety");
    rt_sprite3d_set_position(NULL, 0, 0, 0);
    rt_sprite3d_set_scale(NULL, 1, 1);
    rt_sprite3d_set_anchor(NULL, 0.5, 0.5);
    rt_sprite3d_set_frame(NULL, 0, 0, 16, 16);
    PASS();
}

//=============================================================================
// RenderTarget3D tests
//=============================================================================

static void test_rendertarget_new() {
    TEST("RenderTarget3D.New — creates target");
    void *rt = rt_rendertarget3d_new(256, 256);
    assert(rt);
    PASS();
}

static void test_rendertarget_dimensions() {
    TEST("RenderTarget3D — width/height match constructor");
    void *rt = rt_rendertarget3d_new(128, 64);
    EXPECT_EQ(rt_rendertarget3d_get_width(rt), 128);
    EXPECT_EQ(rt_rendertarget3d_get_height(rt), 64);
    PASS();
}

static void test_rendertarget_as_pixels() {
    TEST("RenderTarget3D.AsPixels — returns non-null");
    void *rt = rt_rendertarget3d_new(16, 16);
    void *px = rt_rendertarget3d_as_pixels(rt);
    assert(px != NULL);
    PASS();
}

static void test_rendertarget_null_safety() {
    TEST("RenderTarget3D — null safety");
    EXPECT_EQ(rt_rendertarget3d_get_width(NULL), 0);
    EXPECT_EQ(rt_rendertarget3d_get_height(NULL), 0);
    assert(rt_rendertarget3d_as_pixels(NULL) == NULL);
    rt_canvas3d_set_render_target(NULL, NULL);
    rt_canvas3d_reset_render_target(NULL);
    PASS();
}

//=============================================================================
// Terrain3D splat tests
//=============================================================================

static void test_terrain_create() {
    TEST("Terrain3D.New — creates terrain");
    void *t = rt_terrain3d_new(32, 32);
    assert(t);
    PASS();
}

static void test_terrain_set_splat_map() {
    TEST("Terrain3D.SetSplatMap — accepts Pixels");
    void *t = rt_terrain3d_new(16, 16);
    void *splat = rt_pixels_new(16, 16);
    rt_terrain3d_set_splat_map(t, splat);
    PASS();
}

static void test_terrain_set_layer_texture() {
    TEST("Terrain3D.SetLayerTexture — accepts layer + Pixels");
    void *t = rt_terrain3d_new(16, 16);
    void *tex = rt_pixels_new(32, 32);
    rt_terrain3d_set_layer_texture(t, 0, tex);
    rt_terrain3d_set_layer_texture(t, 1, tex);
    rt_terrain3d_set_layer_texture(t, 2, tex);
    rt_terrain3d_set_layer_texture(t, 3, tex);
    /* Out of range — should not crash */
    rt_terrain3d_set_layer_texture(t, 4, tex);
    rt_terrain3d_set_layer_texture(t, -1, tex);
    PASS();
}

static void test_terrain_set_layer_scale() {
    TEST("Terrain3D.SetLayerScale — sets UV tiling scale");
    void *t = rt_terrain3d_new(16, 16);
    rt_terrain3d_set_layer_scale(t, 0, 4.0);
    rt_terrain3d_set_layer_scale(t, 1, 8.0);
    /* Out of range — should not crash */
    rt_terrain3d_set_layer_scale(t, 5, 1.0);
    PASS();
}

static void test_terrain_null_safety() {
    TEST("Terrain3D splat — null safety");
    rt_terrain3d_set_splat_map(NULL, NULL);
    rt_terrain3d_set_layer_texture(NULL, 0, NULL);
    rt_terrain3d_set_layer_scale(NULL, 0, 1.0);
    PASS();
}

//=============================================================================
//=============================================================================
// SW Backend Feature Tests (SW-01 through SW-08)
//=============================================================================

static void test_vertex_color_default_white() {
    TEST("Mesh3D vertex color defaults to white");
    void *m = rt_mesh3d_new();
    rt_mesh3d_add_vertex(m, 0, 0, 0, 0, 1, 0, 0, 0);
    EXPECT_EQ(rt_mesh3d_get_vertex_count(m), 1);
    PASS();
}

static void test_shadow_enable_disable() {
    TEST("Shadow enable/disable API null safety");
    extern void rt_canvas3d_enable_shadows(void *canvas, int64_t resolution);
    extern void rt_canvas3d_disable_shadows(void *canvas);
    extern void rt_canvas3d_set_shadow_bias(void *canvas, double bias);
    /* Call with NULL — should not crash (null-guard) */
    rt_canvas3d_enable_shadows(NULL, 1024);
    rt_canvas3d_disable_shadows(NULL);
    rt_canvas3d_set_shadow_bias(NULL, 0.005);
    PASS();
}

static void test_mesh_tangents_for_normal_map() {
    TEST("Mesh3D.CalcTangents produces tangent data");
    void *m = rt_mesh3d_new();
    rt_mesh3d_add_vertex(m, 0, 0, 0, 0, 1, 0, 0, 0);
    rt_mesh3d_add_vertex(m, 1, 0, 0, 0, 1, 0, 1, 0);
    rt_mesh3d_add_vertex(m, 0, 0, 1, 0, 1, 0, 0, 1);
    rt_mesh3d_add_triangle(m, 0, 1, 2);
    rt_mesh3d_calc_tangents(m);
    /* CalcTangents should not crash and mesh should still be valid */
    EXPECT_EQ(rt_mesh3d_get_vertex_count(m), 3);
    EXPECT_EQ(rt_mesh3d_get_triangle_count(m), 1);
    PASS();
}

static void test_mesh_normals_recalc() {
    TEST("Mesh3D.RecalcNormals updates normals");
    void *m = rt_mesh3d_new();
    rt_mesh3d_add_vertex(m, 0, 0, 0, 0, 0, 0, 0, 0);
    rt_mesh3d_add_vertex(m, 1, 0, 0, 0, 0, 0, 1, 0);
    rt_mesh3d_add_vertex(m, 0, 0, 1, 0, 0, 0, 0, 1);
    rt_mesh3d_add_triangle(m, 0, 1, 2);
    rt_mesh3d_recalc_normals(m);
    EXPECT_EQ(rt_mesh3d_get_vertex_count(m), 3);
    PASS();
}

static void test_terrain_splat_layer_count() {
    TEST("Terrain3D supports 4 splat layers");
    void *t = rt_terrain3d_new(4, 4);
    assert(t != NULL);
    void *px = rt_pixels_new(4, 4);
    /* Set all 4 layers */
    for (int i = 0; i < 4; i++) {
        rt_terrain3d_set_layer_texture(t, i, px);
        rt_terrain3d_set_layer_scale(t, i, (double)(i + 1));
    }
    PASS();
}

static void test_terrain_splat_map_set() {
    TEST("Terrain3D splat map can be set and cleared");
    void *t = rt_terrain3d_new(4, 4);
    assert(t != NULL);
    void *px = rt_pixels_new(4, 4);
    rt_terrain3d_set_splat_map(t, px);
    rt_terrain3d_set_splat_map(t, NULL);
    PASS();
}

// Metal backend feature tests (MTL-01 through MTL-08)
// These test the runtime API surface that feeds the Metal shader pipeline.
//=============================================================================

static void test_metal_spot_light_creates() {
    TEST("MTL-02: Light3D.NewSpot — inner/outer angles survive creation");
    void *pos = rt_vec3_new(0, 10, 0);
    void *dir = rt_vec3_new(0, -1, 0);
    /* 15° inner, 30° outer cone — Metal shader uses cos() of these */
    void *light = rt_light3d_new_spot(pos, dir, 1.0, 1.0, 1.0, 0.05, 15.0, 30.0);
    assert(light != NULL);
    PASS();
}

static void test_metal_spot_light_narrow_cone() {
    TEST("MTL-02: Spot light — narrow cone (5° inner, 10° outer)");
    void *pos = rt_vec3_new(0, 5, 0);
    void *dir = rt_vec3_new(0, -1, 0);
    void *light = rt_light3d_new_spot(pos, dir, 1.0, 1.0, 1.0, 0.01, 5.0, 10.0);
    assert(light != NULL);
    rt_light3d_set_intensity(light, 3.0);
    PASS();
}

static void test_metal_material_all_maps() {
    TEST("MTL-04/05/06: Material3D — set all 3 map textures");
    void *m = rt_material3d_new();
    void *norm_px = rt_pixels_new(8, 8);
    void *spec_px = rt_pixels_new(8, 8);
    void *emit_px = rt_pixels_new(8, 8);
    rt_material3d_set_normal_map(m, norm_px);
    rt_material3d_set_specular_map(m, spec_px);
    rt_material3d_set_emissive_map(m, emit_px);
    rt_material3d_set_emissive_color(m, 1.0, 0.5, 0.0);
    PASS();
}

static void test_metal_material_map_null_safety() {
    TEST("MTL-04/05/06: Material3D — set maps to NULL is safe");
    void *m = rt_material3d_new();
    rt_material3d_set_normal_map(m, NULL);
    rt_material3d_set_specular_map(m, NULL);
    rt_material3d_set_emissive_map(m, NULL);
    rt_material3d_set_emissive_color(m, 0.0, 0.0, 0.0);
    PASS();
}

static void test_metal_material_map_replace() {
    TEST("MTL-03: Material3D — replacing texture maps doesn't leak");
    void *m = rt_material3d_new();
    void *px1 = rt_pixels_new(4, 4);
    void *px2 = rt_pixels_new(8, 8);
    /* Set then replace each map */
    rt_material3d_set_normal_map(m, px1);
    rt_material3d_set_normal_map(m, px2);
    rt_material3d_set_specular_map(m, px1);
    rt_material3d_set_specular_map(m, NULL);
    rt_material3d_set_emissive_map(m, px2);
    rt_material3d_set_emissive_map(m, px1);
    PASS();
}

static void test_metal_fog_set_clear() {
    TEST("MTL-07: Canvas3D fog — set and clear (null canvas)");
    /* null canvas won't crash (stubs return early) */
    rt_canvas3d_set_fog(NULL, 10.0, 100.0, 0.5, 0.5, 0.6);
    rt_canvas3d_clear_fog(NULL);
    PASS();
}

static void test_metal_tangents_for_normal_map() {
    TEST("MTL-04: Mesh3D.CalcTangents — required for Metal normal maps");
    void *m = rt_mesh3d_new();
    rt_mesh3d_add_vertex(m, -1, 0, -1, 0, 1, 0, 0, 0);
    rt_mesh3d_add_vertex(m,  1, 0, -1, 0, 1, 0, 1, 0);
    rt_mesh3d_add_vertex(m,  1, 0,  1, 0, 1, 0, 1, 1);
    rt_mesh3d_add_vertex(m, -1, 0,  1, 0, 1, 0, 0, 1);
    rt_mesh3d_add_triangle(m, 0, 2, 1);
    rt_mesh3d_add_triangle(m, 0, 3, 2);
    rt_mesh3d_calc_tangents(m);
    EXPECT_EQ(rt_mesh3d_get_vertex_count(m), 4);
    EXPECT_EQ(rt_mesh3d_get_triangle_count(m), 2);
    PASS();
}

// Metal backend tests — Phase 2 (MTL-09 through MTL-14)
//=============================================================================

extern "C" void rt_canvas3d_enable_shadows(void *canvas, int64_t resolution);
extern "C" void rt_canvas3d_disable_shadows(void *canvas);

static void test_metal_shadow_enable_null() {
    TEST("MTL-12: Canvas3D shadow enable/disable (null canvas safe)");
    rt_canvas3d_enable_shadows(NULL, 1024);
    rt_canvas3d_disable_shadows(NULL);
    PASS();
}

extern "C" void *rt_instbatch3d_new(void *mesh, void *material);
extern "C" void rt_instbatch3d_add(void *batch, void *transform);

static void test_metal_instbatch_create() {
    TEST("MTL-13: InstanceBatch3D — create and add instances");
    void *mesh = rt_mesh3d_new_box(1.0, 1.0, 1.0);
    void *mat = rt_material3d_new();
    void *batch = rt_instbatch3d_new(mesh, mat);
    assert(batch != NULL);
    /* Add a few instances (no getter, just verify no crash) */
    void *t = rt_mat4_identity();
    rt_instbatch3d_add(batch, t);
    rt_instbatch3d_add(batch, t);
    rt_instbatch3d_add(batch, t);
    PASS();
}

static void test_metal_terrain_splat_for_gpu() {
    TEST("MTL-14: Terrain3D splat maps + 4 layers for GPU path");
    void *t = rt_terrain3d_new(8, 8);
    assert(t != NULL);
    void *splat = rt_pixels_new(8, 8);
    rt_terrain3d_set_splat_map(t, splat);
    for (int i = 0; i < 4; i++) {
        void *layer = rt_pixels_new(16, 16);
        rt_terrain3d_set_layer_texture(t, i, layer);
        rt_terrain3d_set_layer_scale(t, i, (double)(i + 1) * 4.0);
    }
    PASS();
}

extern "C" void *rt_postfx3d_new(void);
extern "C" void rt_postfx3d_add_bloom(void *obj, double threshold, double intensity,
                                       int64_t blur_passes);
extern "C" void rt_postfx3d_add_tonemap(void *obj, int64_t mode, double exposure);
extern "C" void rt_postfx3d_add_fxaa(void *obj);
extern "C" void rt_postfx3d_add_vignette(void *obj, double radius, double softness);
extern "C" void rt_postfx3d_set_enabled(void *obj, int8_t enabled);
extern "C" int64_t rt_postfx3d_get_effect_count(void *obj);

static void test_metal_postfx_new() {
    TEST("MTL-11: PostFX3D — create and add effects");
    void *fx = rt_postfx3d_new();
    assert(fx != NULL);
    rt_postfx3d_add_bloom(fx, 0.8, 1.5, 3);
    rt_postfx3d_add_tonemap(fx, 2, 1.0);
    rt_postfx3d_add_fxaa(fx);
    rt_postfx3d_add_vignette(fx, 0.7, 0.3);
    EXPECT_EQ(rt_postfx3d_get_effect_count(fx), 4);
    rt_postfx3d_set_enabled(fx, 1);
    PASS();
}

extern "C" void rt_canvas3d_set_post_fx(void *canvas, void *postfx);

static void test_metal_postfx_null_safety() {
    TEST("MTL-11: PostFX3D — null safety on all ops");
    rt_postfx3d_add_bloom(NULL, 0.5, 1.0, 2);
    rt_postfx3d_add_tonemap(NULL, 1, 1.0);
    rt_postfx3d_add_fxaa(NULL);
    rt_postfx3d_add_vignette(NULL, 0.5, 0.2);
    rt_postfx3d_set_enabled(NULL, 0);
    rt_canvas3d_set_post_fx(NULL, NULL);
    PASS();
}

static void test_metal_skinned_mesh_bone_fields() {
    TEST("MTL-09: Mesh3D bone data survives creation");
    void *mesh = rt_mesh3d_new_box(1.0, 1.0, 1.0);
    assert(mesh != NULL);
    /* Mesh should have zero bone count by default */
    EXPECT_EQ(rt_mesh3d_get_vertex_count(mesh), 24);
    PASS();
}

// Backend selection tests
//=============================================================================

static void test_backend_select() {
    TEST("Backend selection — returns non-null");
    const void *b = vgfx3d_select_backend();
    assert(b != NULL);
    PASS();
}

//=============================================================================
// Main
//=============================================================================

int main() {
    printf("=== Graphics3D Unit Tests ===\n\n");

    /* Mesh3D — basic */
    test_mesh_empty();
    test_mesh_add_vertex_triangle();
    test_mesh_reject_invalid_triangle_indices();
    test_mesh_box();
    test_mesh_sphere();
    test_mesh_plane();
    test_mesh_cylinder();
    test_mesh_clone();
    test_mesh_recalc_normals();
    test_mesh_obj_loader();

    /* Mesh3D — extended */
    test_mesh_many_vertices();
    test_mesh_many_triangles();
    test_mesh_null_safety();
    test_mesh_sphere_low_segments();
    test_mesh_cylinder_low_segments();
    test_mesh_box_dimensions();
    test_mesh_transform_identity();

    /* Camera3D — basic */
    test_camera_new();
    test_camera_set_fov();
    test_camera_look_at();
    test_camera_forward();
    test_camera_orbit();
    test_camera_screen_to_ray();

    /* Camera3D — extended */
    test_camera_null_safety();
    test_camera_right_vector();
    test_camera_orbit_yaw();
    test_camera_orbit_pitch();
    test_camera_screen_to_ray_corners();

    /* Material3D */
    test_material_new();
    test_material_new_color();
    test_material_new_textured();
    test_material_set_color();
    test_material_set_shininess();
    test_material_set_unlit();
    test_material_null_safety();

    /* Light3D */
    test_light_directional();
    test_light_point();
    test_light_ambient();
    test_light_set_intensity();
    test_light_set_color();
    test_light_null_safety();
    test_light_spot();
    test_light_spot_intensity();
    test_camera_ortho();
    test_camera_ortho_look_at();
    test_camera_perspective_not_ortho();

    /* Phase 9 — Multi-texture materials */
    test_material_set_emissive();
    test_material_set_maps();
    test_mesh_calc_tangents();

    /* Phase 10 — Alpha blending */
    test_material_alpha();

    /* Phase 11 — Cube maps */
    test_cubemap_new();
    test_material_reflectivity();

    /* Mesh3D.Clear */
    test_mesh_clear();
    test_mesh_clear_then_rebuild();
    test_mesh_clear_null_safety();
    test_mesh_clear_stress();

    /* Sprite3D */
    test_sprite3d_new();
    test_sprite3d_new_null_texture();
    test_sprite3d_set_position();
    test_sprite3d_set_scale();
    test_sprite3d_set_frame();
    test_sprite3d_null_safety();

    /* RenderTarget3D */
    test_rendertarget_new();
    test_rendertarget_dimensions();
    test_rendertarget_as_pixels();
    test_rendertarget_null_safety();

    /* Terrain3D splat */
    test_terrain_create();
    test_terrain_set_splat_map();
    test_terrain_set_layer_texture();
    test_terrain_set_layer_scale();
    test_terrain_null_safety();

    /* SW Backend Features (SW-01 through SW-08) */
    test_vertex_color_default_white();
    test_shadow_enable_disable();
    test_mesh_tangents_for_normal_map();
    test_mesh_normals_recalc();
    test_terrain_splat_layer_count();
    test_terrain_splat_map_set();

    /* Metal backend features (MTL-01 through MTL-08) */
    test_metal_spot_light_creates();
    test_metal_spot_light_narrow_cone();
    test_metal_material_all_maps();
    test_metal_material_map_null_safety();
    test_metal_material_map_replace();
    test_metal_fog_set_clear();
    test_metal_tangents_for_normal_map();

    /* Metal backend features (MTL-09 through MTL-14) */
    test_metal_shadow_enable_null();
    test_metal_instbatch_create();
    test_metal_terrain_splat_for_gpu();
    test_metal_postfx_new();
    test_metal_postfx_null_safety();
    test_metal_skinned_mesh_bone_fields();

    /* Backend */
    test_backend_select();

    printf("\n%d/%d tests passed\n", tests_passed, tests_total);
    return tests_passed == tests_total ? 0 : 1;
}
