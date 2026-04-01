#include "vgfx3d_backend_utils.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

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
            fprintf(stderr, "FAIL: %s (got %.6f expected %.6f)\n", msg, (double)(a), (double)(b)); \
        } else {                                                                                   \
            tests_passed++;                                                                        \
        }                                                                                          \
    } while (0)

typedef struct {
    int64_t w;
    int64_t h;
    uint32_t *data;
    uint64_t generation;
} fake_pixels_t;

typedef struct {
    void *vptr;
    void *faces[6];
    int64_t face_size;
} fake_cubemap_t;

static void test_unpack_pixels_rgba_success(void) {
    uint32_t data[2] = {0x11223344u, 0xAABBCCDDu};
    fake_pixels_t px = {2, 1, data, 7};
    int32_t w = 0;
    int32_t h = 0;
    uint8_t *rgba = NULL;

    EXPECT_TRUE(vgfx3d_unpack_pixels_rgba(&px, &w, &h, &rgba) == 0, "Pixels unpack succeeds");
    EXPECT_TRUE(w == 2 && h == 1, "Pixels unpack preserves dimensions");
    EXPECT_TRUE(rgba != NULL, "Pixels unpack allocates output");
    if (rgba) {
        EXPECT_TRUE(rgba[0] == 0x11 && rgba[1] == 0x22 && rgba[2] == 0x33 && rgba[3] == 0x44,
                    "Pixels unpack converts first texel to RGBA bytes");
        EXPECT_TRUE(rgba[4] == 0xAA && rgba[5] == 0xBB && rgba[6] == 0xCC && rgba[7] == 0xDD,
                    "Pixels unpack converts second texel to RGBA bytes");
    }

    free(rgba);
}

static void test_unpack_pixels_rgba_rejects_invalid(void) {
    fake_pixels_t px = {0, 1, NULL, 0};
    int32_t w = 0;
    int32_t h = 0;
    uint8_t *rgba = NULL;

    EXPECT_TRUE(vgfx3d_unpack_pixels_rgba(&px, &w, &h, &rgba) != 0,
                "Pixels unpack rejects invalid input");
}

static void test_unpack_cubemap_faces_rgba_success(void) {
    uint32_t face_data[6] = {
        0x11223344u, 0x22334455u, 0x33445566u,
        0x44556677u, 0x55667788u, 0x66778899u,
    };
    fake_pixels_t faces[6];
    fake_cubemap_t cubemap;
    int32_t face_size = 0;
    uint8_t *rgba_faces[6];

    memset(&cubemap, 0, sizeof(cubemap));
    for (int i = 0; i < 6; i++) {
        faces[i].w = 1;
        faces[i].h = 1;
        faces[i].data = &face_data[i];
        cubemap.faces[i] = &faces[i];
    }
    cubemap.face_size = 1;

    EXPECT_TRUE(vgfx3d_unpack_cubemap_faces_rgba(&cubemap, &face_size, rgba_faces) == 0,
                "Cubemap unpack succeeds");
    EXPECT_TRUE(face_size == 1, "Cubemap unpack preserves face size");
    EXPECT_TRUE(rgba_faces[0] != NULL && rgba_faces[5] != NULL,
                "Cubemap unpack allocates all face buffers");
    if (rgba_faces[0] && rgba_faces[5]) {
        EXPECT_TRUE(rgba_faces[0][0] == 0x11 && rgba_faces[0][1] == 0x22 &&
                        rgba_faces[0][2] == 0x33 && rgba_faces[0][3] == 0x44,
                    "Cubemap unpack converts the +X face to RGBA bytes");
        EXPECT_TRUE(rgba_faces[5][0] == 0x66 && rgba_faces[5][1] == 0x77 &&
                        rgba_faces[5][2] == 0x88 && rgba_faces[5][3] == 0x99,
                    "Cubemap unpack converts the -Z face to RGBA bytes");
    }

    for (int i = 0; i < 6; i++)
        free(rgba_faces[i]);
}

static void test_unpack_cubemap_faces_rgba_rejects_invalid(void) {
    uint32_t data = 0x11223344u;
    fake_pixels_t good_face = {1, 1, &data, 2};
    fake_pixels_t bad_face = {2, 1, &data, 5};
    fake_cubemap_t cubemap = {0};
    int32_t face_size = 0;
    uint8_t *rgba_faces[6];

    for (int i = 0; i < 6; i++)
        cubemap.faces[i] = &good_face;
    cubemap.faces[3] = &bad_face;
    cubemap.face_size = 1;

    EXPECT_TRUE(vgfx3d_unpack_cubemap_faces_rgba(&cubemap, &face_size, rgba_faces) != 0,
                "Cubemap unpack rejects mismatched face dimensions");
}

static void test_flip_rgba_rows(void) {
    uint8_t rgba[16] = {
        1, 2, 3, 4,
        5, 6, 7, 8,
        9, 10, 11, 12,
        13, 14, 15, 16,
    };

    vgfx3d_flip_rgba_rows(rgba, 2, 2);

    EXPECT_TRUE(rgba[0] == 9 && rgba[1] == 10 && rgba[2] == 11 && rgba[3] == 12,
                "Row flip swaps the first row");
    EXPECT_TRUE(rgba[8] == 1 && rgba[9] == 2 && rgba[10] == 3 && rgba[11] == 4,
                "Row flip swaps the second row");
}

static void test_generation_helpers(void) {
    uint32_t data = 0x11223344u;
    fake_pixels_t faces[6] = {
        {1, 1, &data, 1}, {1, 1, &data, 4}, {1, 1, &data, 2},
        {1, 1, &data, 9}, {1, 1, &data, 3}, {1, 1, &data, 5},
    };
    fake_cubemap_t cubemap = {0};

    for (int i = 0; i < 6; i++)
        cubemap.faces[i] = &faces[i];
    cubemap.face_size = 1;

    EXPECT_TRUE(vgfx3d_get_pixels_generation(&faces[1]) == 4,
                "Pixels generation helper exposes the object generation");
    EXPECT_TRUE(vgfx3d_get_cubemap_generation(&cubemap) == 9,
                "Cubemap generation helper returns the max face generation");
}

static void test_compute_normal_matrix_inverse_transpose(void) {
    const float model[16] = {
        2.0f, 0.0f, 0.0f, 5.0f,
        0.0f, 3.0f, 0.0f, 6.0f,
        0.0f, 0.0f, 4.0f, 7.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    float normal[16];

    vgfx3d_compute_normal_matrix4(model, normal);

    EXPECT_NEAR(normal[0], 0.5f, 1e-5f, "Normal matrix inverts X scale");
    EXPECT_NEAR(normal[5], 1.0f / 3.0f, 1e-5f, "Normal matrix inverts Y scale");
    EXPECT_NEAR(normal[10], 0.25f, 1e-5f, "Normal matrix inverts Z scale");
    EXPECT_NEAR(normal[3], 0.0f, 1e-6f, "Normal matrix removes translation");
    EXPECT_NEAR(normal[15], 1.0f, 1e-6f, "Normal matrix keeps homogeneous identity");
}

static void test_compute_normal_matrix_singular_fallback(void) {
    const float model[16] = {
        1.0f, 2.0f, 3.0f, 4.0f,
        4.0f, 5.0f, 6.0f, 7.0f,
        7.0f, 8.0f, 9.0f, 10.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    float normal[16];

    vgfx3d_compute_normal_matrix4(model, normal);

    EXPECT_NEAR(normal[0], 1.0f, 1e-6f, "Singular fallback copies row 0 col 0");
    EXPECT_NEAR(normal[1], 2.0f, 1e-6f, "Singular fallback copies row 0 col 1");
    EXPECT_NEAR(normal[2], 3.0f, 1e-6f, "Singular fallback copies row 0 col 2");
    EXPECT_NEAR(normal[4], 4.0f, 1e-6f, "Singular fallback copies row 1 col 0");
    EXPECT_NEAR(normal[10], 9.0f, 1e-6f, "Singular fallback copies row 2 col 2");
}

static void test_invert_matrix4_success(void) {
    const float matrix[16] = {
        2.0f, 0.0f, 0.0f, 4.0f,
        0.0f, 4.0f, 0.0f, 6.0f,
        0.0f, 0.0f, 8.0f, 8.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    float inv[16];

    EXPECT_TRUE(vgfx3d_invert_matrix4(matrix, inv) == 0, "Matrix inversion succeeds");
    EXPECT_NEAR(inv[0], 0.5f, 1e-5f, "Matrix inversion inverts X scale");
    EXPECT_NEAR(inv[5], 0.25f, 1e-5f, "Matrix inversion inverts Y scale");
    EXPECT_NEAR(inv[10], 0.125f, 1e-5f, "Matrix inversion inverts Z scale");
    EXPECT_NEAR(inv[3], -2.0f, 1e-5f, "Matrix inversion undoes X translation");
    EXPECT_NEAR(inv[7], -1.5f, 1e-5f, "Matrix inversion undoes Y translation");
    EXPECT_NEAR(inv[11], -1.0f, 1e-5f, "Matrix inversion undoes Z translation");
}

static void test_invert_matrix4_rejects_singular(void) {
    const float matrix[16] = {
        1.0f, 2.0f, 3.0f, 4.0f,
        2.0f, 4.0f, 6.0f, 8.0f,
        0.0f, 1.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    float inv[16];

    EXPECT_TRUE(vgfx3d_invert_matrix4(matrix, inv) != 0,
                "Matrix inversion rejects singular matrices");
}

int main(void) {
    test_unpack_pixels_rgba_success();
    test_unpack_pixels_rgba_rejects_invalid();
    test_unpack_cubemap_faces_rgba_success();
    test_unpack_cubemap_faces_rgba_rejects_invalid();
    test_flip_rgba_rows();
    test_generation_helpers();
    test_compute_normal_matrix_inverse_transpose();
    test_compute_normal_matrix_singular_fallback();
    test_invert_matrix4_success();
    test_invert_matrix4_rejects_singular();

    printf("vgfx3d_backend_utils tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
