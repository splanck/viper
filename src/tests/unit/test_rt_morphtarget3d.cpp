//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_rt_morphtarget3d.cpp
// Purpose: Unit tests for MorphTarget3D — shape creation, delta application,
//   weight blending, and morph computation.
//
// Links: rt_morphtarget3d.h, plans/3d/16-morph-targets.md
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_canvas3d.h"
#include "rt_internal.h"
#include "rt_morphtarget3d.h"
#include "rt_string.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
extern rt_string rt_const_cstr(const char *s);
extern void *rt_mesh3d_new_box(double w, double h, double d);
extern void *rt_mesh3d_clone(void *obj);
extern void rt_mesh3d_clear(void *obj);
extern int rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
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
        if (fabs((double)(a) - (double)(b)) > (eps)) {                                             \
            fprintf(stderr, "FAIL: %s (got %f, expected %f)\n", msg, (double)(a), (double)(b));    \
        } else {                                                                                   \
            tests_passed++;                                                                        \
        }                                                                                          \
    } while (0)

static void test_create() {
    void *mt = rt_morphtarget3d_new(10);
    EXPECT_TRUE(mt != nullptr, "MorphTarget3D.New returns non-null");
    EXPECT_TRUE(rt_morphtarget3d_get_shape_count(mt) == 0, "Initial shape count = 0");
}

static void test_add_shape() {
    void *mt = rt_morphtarget3d_new(4);
    int64_t idx = rt_morphtarget3d_add_shape(mt, rt_const_cstr("smile"));
    EXPECT_TRUE(idx == 0, "First shape index = 0");
    EXPECT_TRUE(rt_morphtarget3d_get_shape_count(mt) == 1, "Shape count = 1");

    int64_t idx2 = rt_morphtarget3d_add_shape(mt, rt_const_cstr("frown"));
    EXPECT_TRUE(idx2 == 1, "Second shape index = 1");
}

static void test_weight_zero() {
    void *mt = rt_morphtarget3d_new(4);
    rt_morphtarget3d_add_shape(mt, rt_const_cstr("test"));
    rt_morphtarget3d_set_delta(mt, 0, 0, 1.0, 2.0, 3.0);
    rt_morphtarget3d_set_weight(mt, 0, 0.0);

    EXPECT_NEAR(rt_morphtarget3d_get_weight(mt, 0), 0.0, 0.001, "Weight = 0.0");
}

static void test_weight_set_get() {
    void *mt = rt_morphtarget3d_new(4);
    rt_morphtarget3d_add_shape(mt, rt_const_cstr("test"));
    rt_morphtarget3d_set_weight(mt, 0, 0.75);
    EXPECT_NEAR(rt_morphtarget3d_get_weight(mt, 0), 0.75, 0.001, "Weight = 0.75");
}

static void test_weight_by_name() {
    void *mt = rt_morphtarget3d_new(4);
    rt_morphtarget3d_add_shape(mt, rt_const_cstr("blink"));
    rt_morphtarget3d_set_weight_by_name(mt, rt_const_cstr("blink"), 0.5);
    EXPECT_NEAR(rt_morphtarget3d_get_weight(mt, 0), 0.5, 0.001, "SetWeightByName works");
}

static void test_weight_by_name_clamps_like_indexed_set_weight() {
    void *mt = rt_morphtarget3d_new(4);
    rt_morphtarget3d_add_shape(mt, rt_const_cstr("blink"));
    rt_morphtarget3d_set_weight_by_name(mt, rt_const_cstr("blink"), 4.0);
    EXPECT_NEAR(rt_morphtarget3d_get_weight(mt, 0),
                1.0,
                0.001,
                "SetWeightByName clamps weights to the same unit range");
}

static void test_negative_weight() {
    void *mt = rt_morphtarget3d_new(4);
    rt_morphtarget3d_add_shape(mt, rt_const_cstr("test"));
    rt_morphtarget3d_set_weight(mt, 0, -0.5);
    EXPECT_NEAR(rt_morphtarget3d_get_weight(mt, 0), -0.5, 0.001, "Negative weight = -0.5");
}

/* Audit fix #9 — set_weight clamps to [-1, 1] so over-range values don't
 * silently over-extrude geometry past the target mesh. */
static void test_weight_clamped_to_unit_range() {
    void *mt = rt_morphtarget3d_new(4);
    rt_morphtarget3d_add_shape(mt, rt_const_cstr("test"));

    rt_morphtarget3d_set_weight(mt, 0, 2.5);
    EXPECT_NEAR(rt_morphtarget3d_get_weight(mt, 0), 1.0, 0.001, "Weight > 1.0 clamps to 1.0");

    rt_morphtarget3d_set_weight(mt, 0, -3.0);
    EXPECT_NEAR(rt_morphtarget3d_get_weight(mt, 0), -1.0, 0.001, "Weight < -1.0 clamps to -1.0");

    rt_morphtarget3d_set_weight(mt, 0, 0.5);
    EXPECT_NEAR(rt_morphtarget3d_get_weight(mt, 0), 0.5, 0.001, "In-range weight unchanged");
}

static void test_bounds_checks() {
    void *mt = rt_morphtarget3d_new(4);
    rt_morphtarget3d_add_shape(mt, rt_const_cstr("test"));

    /* Out-of-bounds shape index — should be no-op */
    rt_morphtarget3d_set_weight(mt, 5, 1.0);
    EXPECT_NEAR(rt_morphtarget3d_get_weight(mt, 5), 0.0, 0.001, "Out of bounds returns 0");

    /* Out-of-bounds vertex — should be no-op */
    rt_morphtarget3d_set_delta(mt, 0, 100, 1.0, 2.0, 3.0); /* vertex 100 > 4 */
    EXPECT_TRUE(1, "Out-of-bounds vertex delta is no-op (no crash)");
}

static void test_null_safety() {
    rt_morphtarget3d_set_weight(NULL, 0, 1.0);
    EXPECT_NEAR(rt_morphtarget3d_get_weight(NULL, 0), 0.0, 0.001, "Null safety");
    EXPECT_TRUE(rt_morphtarget3d_get_shape_count(NULL) == 0, "Null shape count = 0");
}

static void test_packed_payload_generation_tracks_delta_edits_only() {
    void *mt = rt_morphtarget3d_new(2);
    uint64_t initial_generation = rt_morphtarget3d_get_payload_generation(mt);

    rt_morphtarget3d_add_shape(mt, rt_const_cstr("smile"));
    uint64_t after_shape_generation = rt_morphtarget3d_get_payload_generation(mt);
    rt_morphtarget3d_set_delta(mt, 0, 0, 1.0, 2.0, 3.0);
    uint64_t after_delta_generation = rt_morphtarget3d_get_payload_generation(mt);
    rt_morphtarget3d_set_weight(mt, 0, 0.5);
    EXPECT_TRUE(after_shape_generation > initial_generation,
                "Adding a shape bumps the packed-payload generation");
    EXPECT_TRUE(after_delta_generation > after_shape_generation,
                "Editing morph deltas bumps the packed-payload generation");
    EXPECT_TRUE(rt_morphtarget3d_get_payload_generation(mt) == after_delta_generation,
                "Changing only morph weights does not bump the packed-payload generation");
}

static void test_packed_payload_exports_positions_and_normals() {
    void *mt = rt_morphtarget3d_new(2);
    const float *packed_pos;
    const float *packed_nrm;

    rt_morphtarget3d_add_shape(mt, rt_const_cstr("raise"));
    rt_morphtarget3d_set_delta(mt, 0, 0, 1.0, 2.0, 3.0);
    rt_morphtarget3d_set_delta(mt, 0, 1, 4.0, 5.0, 6.0);
    packed_pos = rt_morphtarget3d_get_packed_deltas(mt);
    EXPECT_TRUE(packed_pos != nullptr, "Packed morph positions export successfully");
    if (packed_pos) {
        EXPECT_NEAR(packed_pos[0], 1.0f, 1e-6f, "Packed morph positions keep vertex 0 X");
        EXPECT_NEAR(packed_pos[1], 2.0f, 1e-6f, "Packed morph positions keep vertex 0 Y");
        EXPECT_NEAR(packed_pos[2], 3.0f, 1e-6f, "Packed morph positions keep vertex 0 Z");
        EXPECT_NEAR(packed_pos[3], 4.0f, 1e-6f, "Packed morph positions keep vertex 1 X");
        EXPECT_NEAR(packed_pos[4], 5.0f, 1e-6f, "Packed morph positions keep vertex 1 Y");
        EXPECT_NEAR(packed_pos[5], 6.0f, 1e-6f, "Packed morph positions keep vertex 1 Z");
    }

    EXPECT_TRUE(rt_morphtarget3d_get_packed_normal_deltas(mt) == nullptr,
                "Packed morph normals stay null until normal deltas exist");

    rt_morphtarget3d_set_normal_delta(mt, 0, 0, 0.25, 0.5, 0.75);
    packed_nrm = rt_morphtarget3d_get_packed_normal_deltas(mt);
    EXPECT_TRUE(packed_nrm != nullptr, "Packed morph normals export successfully");
    if (packed_nrm) {
        EXPECT_NEAR(packed_nrm[0], 0.25f, 1e-6f, "Packed morph normals keep vertex 0 X");
        EXPECT_NEAR(packed_nrm[1], 0.5f, 1e-6f, "Packed morph normals keep vertex 0 Y");
        EXPECT_NEAR(packed_nrm[2], 0.75f, 1e-6f, "Packed morph normals keep vertex 0 Z");
        EXPECT_NEAR(packed_nrm[3], 0.0f, 1e-6f, "Packed morph normals zero-pad untouched vertices");
    }
}

static void test_add_shape_grows_beyond_32_entries() {
    void *mt = rt_morphtarget3d_new(1);
    int64_t last_index = -1;
    for (int i = 0; i < 40; i++)
        last_index = rt_morphtarget3d_add_shape(mt, rt_const_cstr("shape"));
    EXPECT_TRUE(last_index == 39, "MorphTarget3D.AddShape no longer traps at 32 shapes");
    EXPECT_TRUE(rt_morphtarget3d_get_shape_count(mt) == 40,
                "MorphTarget3D tracks shape counts beyond the old 32-shape ceiling");
}

static void test_packed_payload_keeps_shapes_beyond_32() {
    void *mt = rt_morphtarget3d_new(1);
    const float *packed_pos;
    const float *packed_nrm;
    for (int i = 0; i < 33; i++)
        rt_morphtarget3d_add_shape(mt, rt_const_cstr("shape"));
    rt_morphtarget3d_set_delta(mt, 32, 0, 7.0, 8.0, 9.0);
    rt_morphtarget3d_set_normal_delta(mt, 32, 0, 0.25, 0.5, 0.75);
    packed_pos = rt_morphtarget3d_get_packed_deltas(mt);
    packed_nrm = rt_morphtarget3d_get_packed_normal_deltas(mt);
    EXPECT_TRUE(packed_pos != nullptr, "Packed morph positions rebuild for shapes beyond slot 31");
    EXPECT_TRUE(packed_nrm != nullptr, "Packed morph normals rebuild for shapes beyond slot 31");
    if (packed_pos) {
        size_t offset = (size_t)32 * 3u;
        EXPECT_NEAR(packed_pos[offset + 0], 7.0f, 1e-6f, "Packed payload keeps shape 32 X");
        EXPECT_NEAR(packed_pos[offset + 1], 8.0f, 1e-6f, "Packed payload keeps shape 32 Y");
        EXPECT_NEAR(packed_pos[offset + 2], 9.0f, 1e-6f, "Packed payload keeps shape 32 Z");
    }
    if (packed_nrm) {
        size_t offset = (size_t)32 * 3u;
        EXPECT_NEAR(packed_nrm[offset + 0], 0.25f, 1e-6f, "Packed normal payload keeps shape 32 X");
        EXPECT_NEAR(packed_nrm[offset + 1], 0.5f, 1e-6f, "Packed normal payload keeps shape 32 Y");
        EXPECT_NEAR(packed_nrm[offset + 2], 0.75f, 1e-6f, "Packed normal payload keeps shape 32 Z");
    }
}

static void test_clone_copies_delta_payloads_and_weights() {
    void *mt = rt_morphtarget3d_new(2);
    rt_morphtarget3d_add_shape(mt, rt_const_cstr("raise"));
    rt_morphtarget3d_set_delta(mt, 0, 0, 1.0, 2.0, 3.0);
    rt_morphtarget3d_set_normal_delta(mt, 0, 0, 0.25, 0.5, 0.75);
    rt_morphtarget3d_set_weight(mt, 0, 0.6);

    void *clone = rt_morphtarget3d_clone(mt);
    EXPECT_TRUE(clone != nullptr, "MorphTarget3D.Clone returns a complete clone");
    EXPECT_TRUE(rt_morphtarget3d_get_shape_count(clone) == 1,
                "MorphTarget3D.Clone copies shape count");
    EXPECT_NEAR(rt_morphtarget3d_get_weight(clone, 0), 0.6, 0.001,
                "MorphTarget3D.Clone copies weights");

    const float *clone_pos = rt_morphtarget3d_get_packed_deltas(clone);
    const float *clone_nrm = rt_morphtarget3d_get_packed_normal_deltas(clone);
    EXPECT_TRUE(clone_pos != nullptr, "MorphTarget3D.Clone copies position deltas");
    EXPECT_TRUE(clone_nrm != nullptr, "MorphTarget3D.Clone copies normal deltas");
    if (clone_pos)
        EXPECT_NEAR(clone_pos[0], 1.0f, 1e-6f, "Clone keeps position delta X");
    if (clone_nrm)
        EXPECT_NEAR(clone_nrm[2], 0.75f, 1e-6f, "Clone keeps normal delta Z");
}

namespace {
static int g_morph_release_count = 0;
} // namespace

extern "C" void tracked_morph_finalizer(void *obj) {
    (void)obj;
    g_morph_release_count++;
}

static void test_mesh_clone_and_clear_manage_morph_target_lifetime() {
    void *mesh = rt_mesh3d_new_box(1.0, 1.0, 1.0);
    void *mt = rt_morphtarget3d_new(24);
    void *clone;
    assert(mesh != nullptr && mt != nullptr);

    rt_morphtarget3d_add_shape(mt, rt_const_cstr("raise"));
    g_morph_release_count = 0;
    rt_obj_set_finalizer(mt, tracked_morph_finalizer);

    rt_mesh3d_set_morph_targets(mesh, mt);
    clone = rt_mesh3d_clone(mesh);
    EXPECT_TRUE(clone != nullptr, "Mesh3D.Clone succeeds with attached morph targets");

    if (rt_obj_release_check0(mt))
        rt_obj_free(mt);
    EXPECT_TRUE(g_morph_release_count == 0,
                "Mesh3D retains attached morph targets across user-side releases");

    rt_mesh3d_clear(mesh);
    EXPECT_TRUE(g_morph_release_count == 0,
                "Clearing one mesh does not release morph targets still owned by a clone");

    if (rt_obj_release_check0(clone))
        rt_obj_free(clone);
    EXPECT_TRUE(g_morph_release_count == 1,
                "Destroying the last attached mesh releases the shared morph targets");
}

int main() {
    test_create();
    test_add_shape();
    test_weight_zero();
    test_weight_set_get();
    test_weight_by_name();
    test_weight_by_name_clamps_like_indexed_set_weight();
    test_negative_weight();
    test_weight_clamped_to_unit_range();
    test_bounds_checks();
    test_null_safety();
    test_packed_payload_generation_tracks_delta_edits_only();
    test_packed_payload_exports_positions_and_normals();
    test_add_shape_grows_beyond_32_entries();
    test_packed_payload_keeps_shapes_beyond_32();
    test_clone_copies_delta_payloads_and_weights();
    test_mesh_clone_and_clear_manage_morph_target_lifetime();

    printf("MorphTarget3D tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
