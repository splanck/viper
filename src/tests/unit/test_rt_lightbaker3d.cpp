//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_rt_lightbaker3d.cpp
// Purpose: Unit tests for baked GI — LightBaker3D determinism, direct/bounce
//   behavior, chart UV1 writes, Apply wiring, and LightProbeGrid3D SH sampling
//   with .vlpg round-trips.
// Key invariants:
//   - Same scene + options bake byte-identical atlases (fixed seeds).
//   - Colored walls bleed their tint into neighboring texels via bounces.
// Ownership/Lifetime:
//   - Test-created runtime handles rely on production GC conventions.
// Links: misc/plans/thirdpersonupgrade/14-baked-gi.md
//
//===----------------------------------------------------------------------===//

#ifndef ZANNA_ENABLE_GRAPHICS
#define ZANNA_ENABLE_GRAPHICS 1
#endif

#include "rt_canvas3d.h"
#include "rt_lightbaker3d.h"
#include "rt_object.h"
#include "rt_pixels.h"
#include "rt_scene3d.h"
#include "rt_string.h"
#include "rt_vec3.h"

#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace {
static std::jmp_buf g_trap_jmp;
static const char *g_last_trap = nullptr;
static bool g_expect_trap = false;
static int g_tests_passed = 0;
static int g_tests_total = 0;
} // namespace

extern "C" void vm_trap(const char *msg) {
    g_last_trap = msg;
    if (g_expect_trap)
        std::longjmp(g_trap_jmp, 1);
    std::fprintf(stderr, "unexpected runtime trap: %s\n", msg ? msg : "(null)");
    std::abort();
}

#define TEST(name)                                                                                 \
    do {                                                                                           \
        ++g_tests_total;                                                                           \
        std::printf("  [%d] %s... ", g_tests_total, name);                                         \
    } while (0)

#define PASS()                                                                                     \
    do {                                                                                           \
        ++g_tests_passed;                                                                          \
        std::printf("ok\n");                                                                       \
        return true;                                                                               \
    } while (0)

#define FAIL(msg)                                                                                  \
    do {                                                                                           \
        std::printf("FAIL: %s\n", msg);                                                            \
        return false;                                                                              \
    } while (0)

#define EXPECT_TRUE(cond, msg)                                                                     \
    do {                                                                                           \
        if (!(cond))                                                                               \
            FAIL(msg);                                                                             \
    } while (0)

namespace {

void *make_quad(double size, double y, double nx, double ny, double nz, int vertical_x) {
    void *mesh = rt_mesh3d_new();
    double h = size * 0.5;
    if (!vertical_x) {
        rt_mesh3d_add_vertex(mesh, -h, y, -h, nx, ny, nz, 0.0, 0.0);
        rt_mesh3d_add_vertex(mesh, h, y, -h, nx, ny, nz, 1.0, 0.0);
        rt_mesh3d_add_vertex(mesh, h, y, h, nx, ny, nz, 1.0, 1.0);
        rt_mesh3d_add_vertex(mesh, -h, y, h, nx, ny, nz, 0.0, 1.0);
    } else {
        /* Wall at x = y-param (reuse y as offset), facing -X. */
        rt_mesh3d_add_vertex(mesh, y, 0.0, -h, nx, ny, nz, 0.0, 0.0);
        rt_mesh3d_add_vertex(mesh, y, 0.0, h, nx, ny, nz, 1.0, 0.0);
        rt_mesh3d_add_vertex(mesh, y, size, h, nx, ny, nz, 1.0, 1.0);
        rt_mesh3d_add_vertex(mesh, y, size, -h, nx, ny, nz, 0.0, 1.0);
    }
    rt_mesh3d_add_triangle(mesh, 0, 1, 2);
    rt_mesh3d_add_triangle(mesh, 0, 2, 3);
    return mesh;
}

void *add_static_node(void *scene, void *mesh, void *material, const char *name) {
    void *node = rt_scene_node3d_new();
    rt_scene_node3d_set_name(node, rt_const_cstr(name));
    rt_scene_node3d_set_mesh(node, mesh);
    if (material)
        rt_scene_node3d_set_material(node, material);
    rt_scene_node3d_set_static(node, 1);
    rt_scene3d_add(scene, node);
    return node;
}

void *make_sun() {
    void *dir = rt_vec3_new(0.0, -1.0, 0.0);
    void *sun = rt_light3d_new_directional(dir, 1.0, 1.0, 1.0);
    if (rt_obj_release_check0(dir))
        rt_obj_free(dir);
    return sun;
}

long long atlas_channel_sum(void *atlas, int shift) {
    long long total = 0;
    int64_t w = rt_pixels_width(atlas);
    int64_t h = rt_pixels_height(atlas);
    for (int64_t y = 0; y < h; y += 4)
        for (int64_t x = 0; x < w; x += 4)
            total += (rt_pixels_get(atlas, x, y) >> shift) & 0xFF;
    return total;
}

bool run_bake(void *baker) {
    for (int i = 0; i < 100000; ++i) {
        if (rt_lightbaker3d_bake_step(baker))
            return true;
    }
    return false;
}

//=========================================================================

bool test_direct_bake_and_apply() {
    TEST("direct-only bake lights the floor atlas, writes UV1, and applies");

    void *scene = rt_scene3d_new();
    void *floor_mesh = make_quad(8.0, 0.0, 0.0, 1.0, 0.0, 0);
    void *floor_mat = rt_material3d_new_color(1.0, 1.0, 1.0);
    void *floor_node = add_static_node(scene, floor_mesh, floor_mat, "bake_floor");

    void *baker = rt_lightbaker3d_new(scene);
    rt_lightbaker3d_set_samples(baker, 8);
    rt_lightbaker3d_set_bounces(baker, 0);
    void *sun = make_sun();
    rt_lightbaker3d_add_light(baker, sun);
    EXPECT_TRUE(run_bake(baker), "bake completes");
    EXPECT_TRUE(rt_lightbaker3d_get_progress(baker) == 1.0, "progress reaches 1");

    void *atlas = rt_lightbaker3d_get_atlas(baker);
    EXPECT_TRUE(atlas != nullptr, "atlas produced");
    long long lit = atlas_channel_sum(atlas, 24);
    EXPECT_TRUE(lit > 0, "sunlit floor bakes non-black texels");

    rt_lightbaker3d_apply(baker);
    void *applied = rt_scene_node3d_get_material(floor_node);
    EXPECT_TRUE(applied != nullptr && rt_material3d_get_has_lightmap(applied),
                "Apply installs a lightmap material instance on the baked node");

    if (rt_obj_release_check0(atlas))
        rt_obj_free(atlas);
    for (void *o : {sun, baker, floor_mat, floor_mesh, scene}) {
        if (rt_obj_release_check0(o))
            rt_obj_free(o);
    }
    PASS();
}

bool test_bounce_color_bleed_and_determinism() {
    TEST("red wall bleeds into bounced floor light; bakes are deterministic");

    long long red_sum[2] = {0, 0};
    long long green_sum[2] = {0, 0};
    long long first_hash = 0;
    for (int variant = 0; variant < 2; ++variant) {
        void *scene = rt_scene3d_new();
        void *floor_mesh = make_quad(6.0, 0.0, 0.0, 1.0, 0.0, 0);
        void *floor_mat = rt_material3d_new_color(1.0, 1.0, 1.0);
        add_static_node(scene, floor_mesh, floor_mat, "floor");
        void *wall_mesh = make_quad(6.0, 2.9, -1.0, 0.0, 0.0, 1);
        void *wall_mat = variant == 0 ? rt_material3d_new_color(1.0, 0.05, 0.05)
                                      : rt_material3d_new_color(1.0, 1.0, 1.0);
        add_static_node(scene, wall_mesh, wall_mat, "wall");

        void *baker = rt_lightbaker3d_new(scene);
        rt_lightbaker3d_set_samples(baker, 16);
        rt_lightbaker3d_set_bounces(baker, 2);
        void *sun = make_sun();
        rt_lightbaker3d_add_light(baker, sun);
        EXPECT_TRUE(run_bake(baker), "bake completes");
        void *atlas = rt_lightbaker3d_get_atlas(baker);
        EXPECT_TRUE(atlas != nullptr, "atlas produced");
        red_sum[variant] = atlas_channel_sum(atlas, 24);
        green_sum[variant] = atlas_channel_sum(atlas, 16);

        if (variant == 0) {
            /* Determinism: re-bake the identical scene and compare a channel hash. */
            void *baker2 = rt_lightbaker3d_new(scene);
            rt_lightbaker3d_set_samples(baker2, 16);
            rt_lightbaker3d_set_bounces(baker2, 2);
            void *sun2 = make_sun();
            rt_lightbaker3d_add_light(baker2, sun2);
            EXPECT_TRUE(run_bake(baker2), "second bake completes");
            void *atlas2 = rt_lightbaker3d_get_atlas(baker2);
            EXPECT_TRUE(atlas2 != nullptr, "second atlas produced");
            long long h1 = atlas_channel_sum(atlas, 24) * 131 + atlas_channel_sum(atlas, 16);
            long long h2 = atlas_channel_sum(atlas2, 24) * 131 + atlas_channel_sum(atlas2, 16);
            first_hash = h1;
            EXPECT_TRUE(h1 == h2, "identical scene + options bake identically");
            if (rt_obj_release_check0(atlas2))
                rt_obj_free(atlas2);
            for (void *o : {sun2, baker2}) {
                if (rt_obj_release_check0(o))
                    rt_obj_free(o);
            }
        }
        if (rt_obj_release_check0(atlas))
            rt_obj_free(atlas);
        for (void *o : {sun, baker, wall_mat, wall_mesh, floor_mat, floor_mesh, scene}) {
            if (rt_obj_release_check0(o))
                rt_obj_free(o);
        }
    }
    (void)first_hash;
    /* Red wall variant must skew red vs the white wall variant. */
    double ratio_red = (double)red_sum[0] / (double)(green_sum[0] + 1);
    double ratio_white = (double)red_sum[1] / (double)(green_sum[1] + 1);
    EXPECT_TRUE(ratio_red > ratio_white * 1.02, "red wall bounce skews the floor bake toward red");
    PASS();
}

bool test_probe_grid_sampling_and_roundtrip() {
    TEST("probe grid samples brighter near light, round-trips via .vlpg");

    /* Open scene: a bright sky on one half via an emissive wall at x=+4. */
    void *scene = rt_scene3d_new();
    void *floor_mesh = make_quad(16.0, 0.0, 0.0, 1.0, 0.0, 0);
    void *floor_mat = rt_material3d_new_color(0.8, 0.8, 0.8);
    add_static_node(scene, floor_mesh, floor_mat, "floor");

    void *baker = rt_lightbaker3d_new(scene);
    rt_lightbaker3d_set_samples(baker, 32);
    rt_lightbaker3d_set_bounces(baker, 1);
    rt_lightbaker3d_set_sky_color(baker, 0.6, 0.6, 0.6);
    /* Point light near x = +5 lights the positive-X side. */
    void *lp = rt_vec3_new(5.0, 2.0, 0.0);
    void *point = rt_light3d_new_point(lp, 1.0, 0.9, 0.7, 0.05);
    if (rt_obj_release_check0(lp))
        rt_obj_free(lp);
    rt_lightbaker3d_add_light(baker, point);

    void *gmin = rt_vec3_new(-6.0, 0.5, -1.0);
    void *gmax = rt_vec3_new(6.0, 2.5, 1.0);
    void *grid = rt_lightprobegrid3d_new(gmin, gmax, 2.0);
    EXPECT_TRUE(grid != nullptr, "grid allocates");
    EXPECT_TRUE(rt_lightprobegrid3d_get_probe_count(grid) > 0, "grid has probes");
    rt_lightprobegrid3d_bake(grid, baker);

    void *near_pos = rt_vec3_new(5.0, 1.5, 0.0);
    void *far_pos = rt_vec3_new(-5.0, 1.5, 0.0);
    /* Down-facing normal: the probe signal here is floor-bounced light, which
     * carries the point light's attenuation gradient (an up normal would read
     * the uniform sky instead). */
    void *up = rt_vec3_new(0.0, -1.0, 0.0);
    void *near_s = rt_lightprobegrid3d_sample(grid, near_pos, up);
    void *far_s = rt_lightprobegrid3d_sample(grid, far_pos, up);
    double nr = rt_vec3_x(near_s), fr = rt_vec3_x(far_s);
    EXPECT_TRUE(nr > fr, "probe irradiance is brighter near the light");

    EXPECT_TRUE(rt_lightprobegrid3d_save(grid, rt_const_cstr("/tmp/zanna_probe_grid.vlpg")) == 1,
                "grid saves");
    void *gmin2 = rt_vec3_new(0.0, 0.0, 0.0);
    void *gmax2 = rt_vec3_new(1.0, 1.0, 1.0);
    void *grid2 = rt_lightprobegrid3d_new(gmin2, gmax2, 1.0);
    EXPECT_TRUE(rt_lightprobegrid3d_load(grid2, rt_const_cstr("/tmp/zanna_probe_grid.vlpg")) == 1,
                "grid loads");
    void *near_s2 = rt_lightprobegrid3d_sample(grid2, near_pos, up);
    EXPECT_TRUE(rt_vec3_x(near_s2) == nr, "loaded grid samples identically");

    const char *truncated_path = "/tmp/zanna_probe_grid_truncated.vlpg";
    FILE *truncated = std::fopen(truncated_path, "wb");
    EXPECT_TRUE(truncated != nullptr, "truncated-grid fixture opens");
    EXPECT_TRUE(std::fwrite("VLPG0001", 1, 8, truncated) == 8, "truncated-grid header writes");
    std::fclose(truncated);
    EXPECT_TRUE(rt_lightprobegrid3d_load(grid2, rt_const_cstr(truncated_path)) == 0,
                "truncated grid is rejected");
    void *near_s3 = rt_lightprobegrid3d_sample(grid2, near_pos, up);
    EXPECT_TRUE(rt_vec3_x(near_s3) == nr, "failed load preserves the previous grid");
    std::remove(truncated_path);

    for (void *o : {near_s,
                    far_s,
                    near_s2,
                    near_s3,
                    near_pos,
                    far_pos,
                    up,
                    gmin,
                    gmax,
                    gmin2,
                    gmax2,
                    grid,
                    grid2,
                    point,
                    baker,
                    floor_mat,
                    floor_mesh,
                    scene}) {
        if (rt_obj_release_check0(o))
            rt_obj_free(o);
    }
    PASS();
}

} // namespace

int main() {
    std::printf("LightBaker3D + LightProbeGrid3D tests\n");
    bool ok = true;
    ok &= test_direct_bake_and_apply();
    ok &= test_bounce_color_bleed_and_determinism();
    ok &= test_probe_grid_sampling_and_roundtrip();
    std::printf("\nBaked GI tests: %d/%d passed\n", g_tests_passed, g_tests_total);
    return ok && g_tests_passed == g_tests_total ? 0 : 1;
}
