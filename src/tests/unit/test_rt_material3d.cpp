//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#ifndef VIPER_ENABLE_GRAPHICS
#define VIPER_ENABLE_GRAPHICS 1
#endif

#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_pixels.h"

#include <cmath>
#include <cstdio>

static int tests_passed = 0;
static int tests_run = 0;

#define EXPECT_TRUE(cond, msg)                                                                     \
    do {                                                                                           \
        tests_run++;                                                                               \
        if (!(cond))                                                                               \
            std::fprintf(stderr, "FAIL: %s\n", msg);                                               \
        else                                                                                       \
            tests_passed++;                                                                        \
    } while (0)

#define EXPECT_NEAR(a, b, eps, msg)                                                                \
    do {                                                                                           \
        tests_run++;                                                                               \
        if (std::fabs((a) - (b)) > (eps))                                                          \
            std::fprintf(stderr, "FAIL: %s\n", msg);                                               \
        else                                                                                       \
            tests_passed++;                                                                        \
    } while (0)

static void test_new_pbr_defaults() {
    rt_material3d *mat = (rt_material3d *)rt_material3d_new_pbr(0.8, 0.6, 0.4);
    EXPECT_TRUE(mat != nullptr, "Material3D.NewPBR creates a material");
    if (!mat)
        return;

    EXPECT_TRUE(mat->workflow == RT_MATERIAL3D_WORKFLOW_PBR,
                "Material3D.NewPBR selects the PBR workflow");
    EXPECT_NEAR(mat->diffuse[0], 0.8, 0.001, "Material3D.NewPBR stores albedo color");
    EXPECT_NEAR(mat->metallic, 0.0, 0.001, "Material3D.NewPBR defaults metallic to dielectric");
    EXPECT_NEAR(mat->roughness, 0.5, 0.001, "Material3D.NewPBR defaults roughness to mid value");
    EXPECT_NEAR(mat->ao, 1.0, 0.001, "Material3D.NewPBR defaults AO to full");
    EXPECT_NEAR(mat->emissive_intensity,
                1.0,
                0.001,
                "Material3D.NewPBR defaults emissive intensity to 1");
    EXPECT_NEAR(mat->normal_scale, 1.0, 0.001, "Material3D.NewPBR defaults normal scale to 1");
    EXPECT_TRUE(mat->alpha_mode == RT_MATERIAL3D_ALPHA_MODE_OPAQUE,
                "Material3D.NewPBR defaults to opaque alpha mode");
    EXPECT_TRUE(mat->double_sided == 0, "Material3D.NewPBR defaults to backface culling");
}

static void test_pbr_setters_promote_legacy_material() {
    rt_material3d *mat = (rt_material3d *)rt_material3d_new_color(0.2, 0.4, 0.6);
    EXPECT_TRUE(mat != nullptr, "Material3D.NewColor creates a legacy material");
    if (!mat)
        return;

    EXPECT_TRUE(mat->workflow == RT_MATERIAL3D_WORKFLOW_LEGACY,
                "Material3D.NewColor stays on the legacy workflow");
    rt_material3d_set_metallic(mat, 0.85);
    rt_material3d_set_roughness(mat, 0.25);
    rt_material3d_set_ao(mat, 0.9);

    EXPECT_TRUE(mat->workflow == RT_MATERIAL3D_WORKFLOW_PBR,
                "PBR setters promote legacy materials into the PBR workflow");
    EXPECT_NEAR(rt_material3d_get_metallic(mat), 0.85, 0.001, "Metallic getter round-trips");
    EXPECT_NEAR(rt_material3d_get_roughness(mat), 0.25, 0.001, "Roughness getter round-trips");
    EXPECT_NEAR(rt_material3d_get_ao(mat), 0.9, 0.001, "AO getter round-trips");
}

static void test_clone_and_instance_share_resources_but_copy_scalars() {
    rt_material3d *base = (rt_material3d *)rt_material3d_new_pbr(0.7, 0.5, 0.3);
    void *px = rt_pixels_new(1, 1);
    void *cubemap = rt_cubemap3d_new(px, px, px, px, px, px);
    EXPECT_TRUE(base != nullptr, "Base PBR material exists");
    EXPECT_TRUE(px != nullptr && cubemap != nullptr, "Pixels and CubeMap fixtures exist");
    if (!base || !px || !cubemap)
        return;

    rt_material3d_set_albedo_map(base, px);
    rt_material3d_set_normal_map(base, px);
    rt_material3d_set_metallic_roughness_map(base, px);
    rt_material3d_set_ao_map(base, px);
    rt_material3d_set_emissive_map(base, px);
    rt_material3d_set_env_map(base, cubemap);
    rt_material3d_set_metallic(base, 0.9);
    rt_material3d_set_roughness(base, 0.15);
    rt_material3d_set_ao(base, 0.8);
    rt_material3d_set_emissive_intensity(base, 2.0);
    rt_material3d_set_normal_scale(base, 0.6);
    rt_material3d_set_alpha_mode(base, RT_MATERIAL3D_ALPHA_MODE_BLEND);
    rt_material3d_set_double_sided(base, 1);

    rt_material3d *clone = (rt_material3d *)rt_material3d_clone(base);
    rt_material3d *inst = (rt_material3d *)rt_material3d_make_instance(base);
    EXPECT_TRUE(clone != nullptr, "Material3D.Clone duplicates materials");
    EXPECT_TRUE(inst != nullptr, "Material3D.MakeInstance duplicates materials");
    if (!clone || !inst)
        return;

    EXPECT_TRUE(clone->texture == base->texture && clone->normal_map == base->normal_map &&
                    clone->metallic_roughness_map == base->metallic_roughness_map &&
                    clone->ao_map == base->ao_map && clone->emissive_map == base->emissive_map,
                "Material3D.Clone shares immutable texture resources by reference");
    EXPECT_TRUE(clone->env_map == base->env_map,
                "Material3D.Clone shares environment resources by reference");
    EXPECT_TRUE(inst->texture == base->texture && inst->env_map == base->env_map,
                "Material3D.MakeInstance shares immutable resources by reference");

    rt_material3d_set_roughness(inst, 0.65);
    rt_material3d_set_metallic(inst, 0.1);
    rt_material3d_set_double_sided(inst, 0);

    EXPECT_NEAR(base->roughness,
                0.15,
                0.001,
                "Material3D.MakeInstance keeps scalar overrides independent");
    EXPECT_NEAR(base->metallic,
                0.9,
                0.001,
                "Instance scalar writes do not mutate the source material");
    EXPECT_TRUE(base->double_sided == 1 && inst->double_sided == 0,
                "Instance boolean overrides do not mutate the source material");
    EXPECT_NEAR(clone->emissive_intensity,
                2.0,
                0.001,
                "Material3D.Clone preserves PBR scalar state");
    EXPECT_TRUE(clone->alpha_mode == RT_MATERIAL3D_ALPHA_MODE_BLEND,
                "Material3D.Clone preserves alpha-mode state");
}

int main() {
    test_new_pbr_defaults();
    test_pbr_setters_promote_legacy_material();
    test_clone_and_instance_share_resources_but_copy_scalars();

    if (tests_passed != tests_run) {
        std::fprintf(
            stderr, "test_rt_material3d: %d/%d checks passed\n", tests_passed, tests_run);
        return 1;
    }

    std::printf("test_rt_material3d: %d/%d checks passed\n", tests_passed, tests_run);
    return 0;
}
