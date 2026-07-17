//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/test_vgfx3d_backend_utils.c
// Purpose: Unit tests for Graphics3D backend utility helpers and CPU skinning.
// Key invariants:
//   - Tests run headless and do not create a real graphics backend.
//   - CPU skinning output remains stable across scratch allocation paths.
// Ownership/Lifetime:
//   - Test-local buffers are stack-owned unless explicitly freed in the same case.
//   - Runtime scratch structs are initialized to zero and released before return.
// Links: src/runtime/graphics/3d/backend/vgfx3d_backend_utils.c,
//        src/runtime/graphics/3d/backend/vgfx3d_skinning.c
//
//===----------------------------------------------------------------------===//

#ifndef ZANNA_ENABLE_GRAPHICS
#define ZANNA_ENABLE_GRAPHICS 1
#endif

#include "vgfx3d_backend.h"
#include "vgfx3d_backend_utils.h"
#include "vgfx3d_frustum.h"
#include "vgfx3d_skinning.h"

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
    uint64_t cache_identity;
} fake_pixels_t;

typedef struct {
    void *vptr;
    void *faces[6];
    int64_t face_size;
    uint64_t cache_identity;
} fake_cubemap_t;

static void test_unpack_pixels_rgba_success(void) {
    uint32_t data[2] = {0x11223344u, 0xAABBCCDDu};
    fake_pixels_t px = {2, 1, data, 7, 41};
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
    fake_pixels_t px = {0, 1, NULL, 0, 0};
    int32_t w = 0;
    int32_t h = 0;
    uint8_t *rgba = NULL;

    EXPECT_TRUE(vgfx3d_unpack_pixels_rgba(&px, &w, &h, &rgba) != 0,
                "Pixels unpack rejects invalid input");
}

static void test_estimate_pixels_rgba_upload_bytes(void) {
    uint32_t data[6] = {0};
    fake_pixels_t px = {3, 2, data, 9, 43};
    fake_pixels_t invalid = {3, 0, data, 0, 0};
    uint64_t bytes = 99;

    EXPECT_TRUE(vgfx3d_estimate_pixels_rgba_upload_bytes(&px, &bytes) == 1,
                "Pixels upload byte estimate accepts valid Pixels");
    EXPECT_TRUE(bytes == 24, "Pixels upload byte estimate is width*height*RGBA8");
    EXPECT_TRUE(vgfx3d_estimate_pixels_rgba_upload_bytes(&invalid, &bytes) == 0,
                "Pixels upload byte estimate rejects invalid dimensions");
    EXPECT_TRUE(bytes == 0, "Pixels upload byte estimate clears output on failure");
    EXPECT_TRUE(vgfx3d_estimate_pixels_rgba_upload_bytes(&px, NULL) == 0,
                "Pixels upload byte estimate rejects null output");
}

static void test_unpack_pixels_rgba_rows_and_extent(void) {
    uint32_t data[6] = {
        0x01020304u,
        0x11121314u,
        0x21222324u,
        0x31323334u,
        0x41424344u,
        0x51525354u,
    };
    fake_pixels_t px = {2, 3, data, 9, 43};
    int32_t w = 0;
    int32_t h = 0;
    int32_t rows = 0;
    uint8_t *rgba = NULL;

    EXPECT_TRUE(vgfx3d_get_pixels_extent(&px, &w, &h) == 1, "Pixels extent accepts valid input");
    EXPECT_TRUE(w == 2 && h == 3, "Pixels extent preserves dimensions");

    EXPECT_TRUE(vgfx3d_unpack_pixels_rgba_rows(&px, 1, 1, 0, &w, &rows, &rgba) == 0,
                "Pixels row-slice unpack succeeds");
    EXPECT_TRUE(w == 2 && rows == 1, "Pixels row-slice reports width and row count");
    if (rgba) {
        EXPECT_TRUE(rgba[0] == 0x21 && rgba[1] == 0x22 && rgba[2] == 0x23 && rgba[3] == 0x24,
                    "Pixels row-slice starts at the requested source row");
    }
    free(rgba);
    rgba = NULL;

    EXPECT_TRUE(vgfx3d_unpack_pixels_rgba_rows(&px, 0, 1, 1, &w, &rows, &rgba) == 0,
                "Pixels flipped row-slice unpack succeeds");
    if (rgba) {
        EXPECT_TRUE(rgba[0] == 0x41 && rgba[1] == 0x42 && rgba[2] == 0x43 && rgba[3] == 0x44,
                    "Pixels flipped row-slice reads from the bottom source row");
    }
    free(rgba);
}

static void test_upload_rows_for_budget(void) {
    EXPECT_TRUE(vgfx3d_upload_rows_for_budget(8, 10, 0, UINT64_MAX, 0) == 10,
                "Unlimited texture upload budget permits all remaining rows");
    EXPECT_TRUE(vgfx3d_upload_rows_for_budget(8, 10, 2, 96, 0) == 3,
                "Texture upload budget converts bytes to row count");
    EXPECT_TRUE(vgfx3d_upload_rows_for_budget(8, 10, 2, 96, 64) == 1,
                "Texture upload budget uses remaining frame bytes");
    EXPECT_TRUE(vgfx3d_upload_rows_for_budget(8, 10, 2, 16, 0) == 1,
                "Positive sub-row texture upload budget still permits one row for liveness");
    EXPECT_TRUE(vgfx3d_upload_rows_for_budget(8, 10, 2, 0, 0) == 0,
                "Zero texture upload budget pauses uploads");
    EXPECT_TRUE(vgfx3d_upload_rows_for_budget(8, 10, 10, UINT64_MAX, 0) == 0,
                "Texture upload budget rejects completed row ranges");
}

static void test_pending_upload_bytes_return_to_baseline(void) {
    EXPECT_TRUE(vgfx3d_pending_rgba_upload_bytes(8, 4, 0, 1) == 128,
                "2D texture pending bytes include every queued row");
    EXPECT_TRUE(vgfx3d_pending_rgba_upload_bytes(8, 4, 3, 1) == 32,
                "2D texture pending bytes shrink as row slices upload");
    EXPECT_TRUE(vgfx3d_pending_rgba_upload_bytes(8, 4, 4, 1) == 0,
                "2D texture pending bytes return to baseline after the final row");
    EXPECT_TRUE(vgfx3d_pending_rgba_upload_bytes(8, 4, 0, 0) == 0,
                "2D texture pending bytes stay at baseline when no upload is in progress");

    EXPECT_TRUE(vgfx3d_pending_cubemap_rgba_upload_bytes(4, 0, 0, 1) == 384,
                "Cubemap pending bytes include all six queued faces");
    EXPECT_TRUE(vgfx3d_pending_cubemap_rgba_upload_bytes(4, 3, 2, 1) == 160,
                "Cubemap pending bytes count the current face slice plus later faces");
    EXPECT_TRUE(vgfx3d_pending_cubemap_rgba_upload_bytes(4, 5, 4, 1) == 0,
                "Cubemap pending bytes return to baseline after the final face row");
    EXPECT_TRUE(vgfx3d_pending_cubemap_rgba_upload_bytes(4, 0, 0, 0) == 0,
                "Cubemap pending bytes stay at baseline when no upload is in progress");
}

static void test_compressed_block_upload_budget_and_pending_bytes(void) {
    EXPECT_TRUE(vgfx3d_upload_block_rows_for_budget(10, 9, 4, 4, 16, 0, UINT64_MAX, 0) == 3,
                "Unlimited compressed upload budget permits all block rows");
    EXPECT_TRUE(vgfx3d_upload_block_rows_for_budget(10, 9, 4, 4, 16, 0, 48, 0) == 1,
                "Compressed upload budget converts bytes to block-row count");
    EXPECT_TRUE(vgfx3d_upload_block_rows_for_budget(10, 9, 4, 4, 16, 1, 96, 48) == 1,
                "Compressed upload budget accounts for current-frame bytes");
    EXPECT_TRUE(vgfx3d_upload_block_rows_for_budget(10, 9, 4, 4, 16, 1, 16, 0) == 1,
                "Positive sub-row compressed upload budget still permits one block row");
    EXPECT_TRUE(vgfx3d_upload_block_rows_for_budget(10, 9, 4, 4, 16, 0, 0, 0) == 0,
                "Zero compressed upload budget pauses block-row uploads");
    EXPECT_TRUE(vgfx3d_upload_block_rows_for_budget(10, 9, 4, 4, 16, 3, UINT64_MAX, 0) == 0,
                "Compressed upload budget rejects completed block-row ranges");

    EXPECT_TRUE(vgfx3d_pending_block_upload_bytes(10, 9, 4, 4, 16, 0, 1) == 144,
                "Compressed pending bytes include all queued block rows");
    EXPECT_TRUE(vgfx3d_pending_block_upload_bytes(10, 9, 4, 4, 16, 2, 1) == 48,
                "Compressed pending bytes shrink as block rows upload");
    EXPECT_TRUE(vgfx3d_pending_block_upload_bytes(10, 9, 4, 4, 16, 3, 1) == 0,
                "Compressed pending bytes return to baseline after final block row");
    EXPECT_TRUE(vgfx3d_pending_block_upload_bytes(10, 9, 4, 4, 16, 0, 0) == 0,
                "Compressed pending bytes stay at baseline when no upload is in progress");

    /* Finer-grained slicing: a per-frame budget smaller than a full mip drains the mip in
     * sub-mip block-row bands, with pending bytes strictly decreasing each step until zero. */
    {
        const int32_t w = 64, h = 64, bw = 4, bh = 4, bb = 16; /* 16 block-rows, 256 bytes/row */
        const int32_t total_block_rows = 16;
        const uint64_t tight_budget = 768; /* fits 3 of 16 block-rows: a sub-mip slice */
        int32_t next = 0;
        int32_t steps = 0;
        uint64_t prev_pending = vgfx3d_pending_block_upload_bytes(w, h, bw, bh, bb, 0, 1) + 1u;
        while (next < total_block_rows) {
            int32_t rows =
                vgfx3d_upload_block_rows_for_budget(w, h, bw, bh, bb, next, tight_budget, 0);
            uint64_t pending = vgfx3d_pending_block_upload_bytes(w, h, bw, bh, bb, next, 1);
            EXPECT_TRUE(rows >= 1, "tight budget still uploads at least one block-row (progress)");
            EXPECT_TRUE(rows < total_block_rows,
                        "tight budget uploads a sub-mip slice, not the whole mip at once");
            EXPECT_TRUE(pending < prev_pending,
                        "pending bytes strictly decrease as sub-mip block-rows drain");
            prev_pending = pending;
            next += rows;
            steps++;
        }
        EXPECT_TRUE(steps > 1, "tight budget drains the mip over multiple sub-mip slices");
        EXPECT_TRUE(vgfx3d_pending_block_upload_bytes(w, h, bw, bh, bb, next, 1) == 0,
                    "pending bytes reach zero after the final sub-mip slice");
    }
}

static void test_unpack_cubemap_faces_rgba_success(void) {
    uint32_t face_data[6] = {
        0x11223344u,
        0x22334455u,
        0x33445566u,
        0x44556677u,
        0x55667788u,
        0x66778899u,
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
    cubemap.cache_identity = 1;

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
    fake_pixels_t good_face = {1, 1, &data, 2, 61};
    fake_pixels_t bad_face = {2, 1, &data, 5, 62};
    fake_pixels_t huge_face = {INT32_MAX, INT32_MAX, &data, 6, 63};
    fake_cubemap_t cubemap = {0};
    int32_t face_size = 0;
    uint8_t *rgba_faces[6];

    for (int i = 0; i < 6; i++)
        rgba_faces[i] = (uint8_t *)1;
    face_size = 7;
    EXPECT_TRUE(vgfx3d_unpack_cubemap_faces_rgba(NULL, &face_size, rgba_faces) != 0,
                "Cubemap unpack rejects null cubemaps");
    EXPECT_TRUE(face_size == 0, "Cubemap unpack clears face size on early failure");
    for (int i = 0; i < 6; i++)
        EXPECT_TRUE(rgba_faces[i] == NULL, "Cubemap unpack clears face outputs on early failure");

    for (int i = 0; i < 6; i++)
        cubemap.faces[i] = &good_face;
    cubemap.faces[3] = &bad_face;
    cubemap.face_size = 1;
    cubemap.cache_identity = 1;

    EXPECT_TRUE(vgfx3d_unpack_cubemap_faces_rgba(&cubemap, &face_size, rgba_faces) != 0,
                "Cubemap unpack rejects mismatched face dimensions");

    for (int i = 0; i < 6; i++) {
        rgba_faces[i] = (uint8_t *)1;
        cubemap.faces[i] = &huge_face;
    }
    cubemap.face_size = 1;
    cubemap.cache_identity = 1;
    face_size = 7;
    EXPECT_TRUE(vgfx3d_unpack_cubemap_faces_rgba(&cubemap, &face_size, rgba_faces) != 0,
                "Cubemap unpack rejects mismatched huge faces before decoding");
    EXPECT_TRUE(face_size == 0, "Cubemap unpack clears face size for huge mismatched faces");
    for (int i = 0; i < 6; i++)
        EXPECT_TRUE(rgba_faces[i] == NULL, "Cubemap unpack clears outputs for huge mismatches");
}

static void test_estimate_cubemap_rgba_upload_bytes(void) {
    uint32_t face_data[6][4] = {{0}};
    fake_pixels_t faces[6];
    fake_cubemap_t cubemap = {0};
    uint64_t bytes = 99;

    for (int i = 0; i < 6; i++) {
        faces[i].w = 2;
        faces[i].h = 2;
        faces[i].data = face_data[i];
        cubemap.faces[i] = &faces[i];
    }
    cubemap.face_size = 2;
    cubemap.cache_identity = 1;

    EXPECT_TRUE(vgfx3d_estimate_cubemap_rgba_upload_bytes(&cubemap, &bytes) == 1,
                "Cubemap upload byte estimate accepts valid faces");
    EXPECT_TRUE(bytes == 96, "Cubemap upload byte estimate is six RGBA8 faces");
    faces[4].h = 1;
    EXPECT_TRUE(vgfx3d_estimate_cubemap_rgba_upload_bytes(&cubemap, &bytes) == 0,
                "Cubemap upload byte estimate rejects mismatched faces");
    EXPECT_TRUE(bytes == 0, "Cubemap upload byte estimate clears output on failure");
    faces[4].h = 2;
    EXPECT_TRUE(vgfx3d_estimate_cubemap_rgba_upload_bytes(&cubemap, NULL) == 0,
                "Cubemap upload byte estimate rejects null output");
    cubemap.cache_identity = 0;
    EXPECT_TRUE(vgfx3d_estimate_cubemap_rgba_upload_bytes(&cubemap, &bytes) == 0,
                "Cubemap upload byte estimate rejects zero cache identities");
    cubemap.cache_identity = 1;
    cubemap.face_size = 32769;
    for (int i = 0; i < 6; i++) {
        faces[i].w = 32769;
        faces[i].h = 32769;
    }
    EXPECT_TRUE(vgfx3d_estimate_cubemap_rgba_upload_bytes(&cubemap, &bytes) == 0,
                "Cubemap upload byte estimate rejects oversized declared faces");
}

static void test_unpack_cubemap_rows_and_extent(void) {
    uint32_t face_data[6][4] = {
        {0x01020304u, 0x11121314u, 0x21222324u, 0x31323334u},
        {0x41424344u, 0x51525354u, 0x61626364u, 0x71727374u},
        {0x81828384u, 0x91929394u, 0xA1A2A3A4u, 0xB1B2B3B4u},
        {0xC1C2C3C4u, 0xD1D2D3D4u, 0xE1E2E3E4u, 0xF1F2F3F4u},
        {0x10203040u, 0x20304050u, 0x30405060u, 0x40506070u},
        {0x50607080u, 0x60708090u, 0x708090A0u, 0x8090A0B0u},
    };
    fake_pixels_t faces[6];
    fake_cubemap_t cubemap = {0};
    int32_t face_size = 0;
    int32_t rows = 0;
    uint8_t *rgba = NULL;

    for (int i = 0; i < 6; i++) {
        faces[i].w = 2;
        faces[i].h = 2;
        faces[i].data = face_data[i];
        cubemap.faces[i] = &faces[i];
    }
    cubemap.face_size = 2;
    cubemap.cache_identity = 1;

    EXPECT_TRUE(vgfx3d_get_cubemap_face_size(&cubemap, &face_size) == 1,
                "Cubemap face-size helper accepts matching square faces");
    EXPECT_TRUE(face_size == 2, "Cubemap face-size helper reports the square extent");

    EXPECT_TRUE(vgfx3d_unpack_cubemap_rgba_rows(&cubemap, 2, 1, 1, 0, &face_size, &rows, &rgba) ==
                    0,
                "Cubemap row-slice unpack succeeds");
    EXPECT_TRUE(face_size == 2 && rows == 1, "Cubemap row-slice reports face size and row count");
    if (rgba) {
        EXPECT_TRUE(rgba[0] == 0xA1 && rgba[1] == 0xA2 && rgba[2] == 0xA3 && rgba[3] == 0xA4,
                    "Cubemap row-slice starts at the requested face row");
    }
    free(rgba);
    rgba = NULL;

    EXPECT_TRUE(vgfx3d_unpack_cubemap_rgba_rows(&cubemap, 5, 0, 1, 1, &face_size, &rows, &rgba) ==
                    0,
                "Cubemap flipped row-slice unpack succeeds");
    if (rgba) {
        EXPECT_TRUE(rgba[0] == 0x70 && rgba[1] == 0x80 && rgba[2] == 0x90 && rgba[3] == 0xA0,
                    "Cubemap flipped row-slice reads from the bottom source row");
    }
    free(rgba);

    faces[4].h = 1;
    EXPECT_TRUE(vgfx3d_get_cubemap_face_size(&cubemap, &face_size) == 0,
                "Cubemap face-size helper rejects mismatched faces");
}

static void test_flip_rgba_rows(void) {
    uint8_t rgba[16] = {
        1,
        2,
        3,
        4,
        5,
        6,
        7,
        8,
        9,
        10,
        11,
        12,
        13,
        14,
        15,
        16,
    };

    vgfx3d_flip_rgba_rows(rgba, 2, 2);

    EXPECT_TRUE(rgba[0] == 9 && rgba[1] == 10 && rgba[2] == 11 && rgba[3] == 12,
                "Row flip swaps the first row");
    EXPECT_TRUE(rgba[8] == 1 && rgba[9] == 2 && rgba[10] == 3 && rgba[11] == 4,
                "Row flip swaps the second row");
}

static void test_hdr_readback_helpers(void) {
    uint16_t rgba16f[8] = {
        0x3C00u, /* 1.0 */
        0x3800u, /* 0.5 */
        0x0000u, /* 0.0 */
        0x3C00u, /* 1.0 alpha */
        0x4400u, /* 4.0 */
        0x0000u, /* 0.0 */
        0x3800u, /* 0.5 */
        0x3C00u, /* 1.0 alpha */
    };
    float rgba32f[8] = {
        1.0f,
        0.5f,
        0.0f,
        1.0f,
        4.0f,
        0.0f,
        0.5f,
        1.0f,
    };
    uint8_t rgba8_from_16f[8] = {0};
    uint8_t rgba8_from_32f[8] = {0};
    float rgba32f_from_16f[8] = {0};

    EXPECT_NEAR(vgfx3d_half_to_float(0x3C00u), 1.0f, 1e-6f, "Half-float helper decodes 1.0");
    EXPECT_NEAR(vgfx3d_half_to_float(0xC000u), -2.0f, 1e-6f, "Half-float helper decodes -2.0");
    EXPECT_TRUE(vgfx3d_hdr_to_unorm8(4.0f) == 204,
                "HDR tonemap helper compresses highlights instead of hard-clamping them");

    vgfx3d_copy_linear_rgba16f_to_rgba8(rgba8_from_16f, 8, 2, 1, rgba16f, 16);
    vgfx3d_copy_linear_rgba32f_to_rgba8(rgba8_from_32f, 8, 2, 1, rgba32f, 32);
    vgfx3d_copy_linear_rgba16f_to_rgba32f(rgba32f_from_16f, 8, 2, 1, rgba16f, 16);

    EXPECT_TRUE(rgba8_from_16f[0] == 128 && rgba8_from_16f[1] == 85 && rgba8_from_16f[2] == 0 &&
                    rgba8_from_16f[3] == 255,
                "RGBA16F conversion tonemaps linear HDR colors to RGBA8");
    EXPECT_TRUE(rgba8_from_16f[4] == 204 && rgba8_from_16f[5] == 0 && rgba8_from_16f[6] == 85 &&
                    rgba8_from_16f[7] == 255,
                "RGBA16F conversion preserves alpha while tonemapping bright highlights");
    EXPECT_TRUE(memcmp(rgba8_from_16f, rgba8_from_32f, sizeof(rgba8_from_16f)) == 0,
                "RGBA16F and RGBA32F conversion helpers produce matching display-space bytes");
    EXPECT_NEAR(
        rgba32f_from_16f[0], 1.0f, 1e-6f, "RGBA16F to RGBA32F conversion preserves linear red");
    EXPECT_NEAR(rgba32f_from_16f[4],
                4.0f,
                1e-6f,
                "RGBA16F to RGBA32F conversion preserves HDR values before tonemapping");
}

static void test_generation_helpers(void) {
    uint32_t data = 0x11223344u;
    fake_pixels_t faces[6] = {
        {1, 1, &data, 1, 101},
        {1, 1, &data, 4, 102},
        {1, 1, &data, 2, 103},
        {1, 1, &data, 9, 104},
        {1, 1, &data, 3, 105},
        {1, 1, &data, 5, 106},
    };
    fake_cubemap_t cubemap = {0};
    uint64_t base_generation;
    uint64_t mutated_generation;

    for (int i = 0; i < 6; i++)
        cubemap.faces[i] = &faces[i];
    cubemap.face_size = 1;
    cubemap.cache_identity = 7;

    EXPECT_TRUE(vgfx3d_get_pixels_generation(&faces[1]) == 4,
                "Pixels generation helper exposes the object generation");
    EXPECT_TRUE(vgfx3d_get_pixels_cache_key(&faces[1]) != vgfx3d_get_pixels_cache_key(&faces[2]),
                "Pixels cache key distinguishes different image identities");
    faces[1].generation = 0;
    faces[2].generation = 0;
    EXPECT_TRUE(vgfx3d_get_pixels_cache_key(&faces[1]) != vgfx3d_get_pixels_cache_key(&faces[2]),
                "Pixels cache key remains unique when multiple images share generation zero");
    faces[1].generation = 4;
    faces[2].generation = 2;
    base_generation = vgfx3d_get_cubemap_generation(&cubemap);
    EXPECT_TRUE(base_generation != 0,
                "Cubemap generation helper returns a non-zero signature for populated cubemaps");

    faces[0].generation = 11;
    mutated_generation = vgfx3d_get_cubemap_generation(&cubemap);
    EXPECT_TRUE(mutated_generation != base_generation,
                "Cubemap generation signature changes when a non-max face mutates");

    faces[0].generation = 1;
    faces[4].generation = 12;
    EXPECT_TRUE(vgfx3d_get_cubemap_generation(&cubemap) != base_generation,
                "Cubemap generation signature depends on per-face generations, not just their max");

    faces[4].generation = 3;
    fake_pixels_t replacement = {1, 1, &data, faces[0].generation, 207};
    cubemap.faces[0] = &replacement;
    EXPECT_TRUE(vgfx3d_get_cubemap_generation(&cubemap) != base_generation,
                "Cubemap generation signature changes when a same-generation face is replaced");
    cubemap.faces[0] = &faces[0];

    cubemap.cache_identity = 8;
    EXPECT_TRUE(vgfx3d_get_cubemap_generation(&cubemap) != base_generation,
                "Cubemap generation signature changes when the cubemap identity changes");

    faces[2].h = 2;
    EXPECT_TRUE(vgfx3d_get_cubemap_generation(&cubemap) == 0,
                "Cubemap generation helper rejects malformed face layouts");
    faces[2].h = 1;
}

static void test_compute_normal_matrix_inverse_transpose(void) {
    const float model[16] = {
        2.0f,
        0.0f,
        0.0f,
        5.0f,
        0.0f,
        3.0f,
        0.0f,
        6.0f,
        0.0f,
        0.0f,
        4.0f,
        7.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
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
    float model[16] = {
        1.0f,
        2.0f,
        3.0f,
        4.0f,
        4.0f,
        5.0f,
        6.0f,
        7.0f,
        7.0f,
        8.0f,
        9.0f,
        10.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
    };
    float normal[16];

    vgfx3d_compute_normal_matrix4(model, normal);

    EXPECT_NEAR(normal[0], 1.0f, 1e-6f, "Singular fallback uses identity row 0 col 0");
    EXPECT_NEAR(normal[1], 0.0f, 1e-6f, "Singular fallback clears off-diagonal entries");
    EXPECT_NEAR(normal[4], 0.0f, 1e-6f, "Singular fallback clears row 1 col 0");
    EXPECT_NEAR(normal[5], 1.0f, 1e-6f, "Singular fallback uses identity row 1 col 1");
    EXPECT_NEAR(normal[10], 1.0f, 1e-6f, "Singular fallback uses identity row 2 col 2");

    model[0] = NAN;
    vgfx3d_compute_normal_matrix4(model, normal);
    EXPECT_NEAR(normal[0], 1.0f, 1e-6f, "Non-finite normal matrix input falls back to identity");
    EXPECT_NEAR(
        normal[10], 1.0f, 1e-6f, "Non-finite normal matrix input avoids propagating NaN values");
}

/* Regression guard for the inverse-transpose property on a matrix with non-zero
 * off-diagonals. The diagonal-scale test above cannot distinguish (M^-1) from
 * (M^-1)^T because a diagonal matrix equals its own transpose. Here the model has
 * rotation/shear, so the cofactors are asymmetric and the normal matrix must equal
 * the transpose of the true inverse: normal[i][j] == inv[j][i]. */
static void test_compute_normal_matrix_inverse_transpose_offdiagonal(void) {
    const float model[16] = {
        1.0f,
        2.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        3.0f,
        0.0f,
        4.0f,
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
    };
    float normal[16];
    float inv[16];

    vgfx3d_compute_normal_matrix4(model, normal);
    EXPECT_TRUE(vgfx3d_invert_matrix4(model, inv) == 0, "model with shear is invertible");

    EXPECT_NEAR(normal[0], inv[0], 1e-5f, "normal[0][0] == inv[0][0]");
    EXPECT_NEAR(normal[1], inv[4], 1e-5f, "normal[0][1] == inv[1][0] (inverse-transpose)");
    EXPECT_NEAR(normal[2], inv[8], 1e-5f, "normal[0][2] == inv[2][0] (inverse-transpose)");
    EXPECT_NEAR(normal[4], inv[1], 1e-5f, "normal[1][0] == inv[0][1] (inverse-transpose)");
    EXPECT_NEAR(normal[5], inv[5], 1e-5f, "normal[1][1] == inv[1][1]");
    EXPECT_NEAR(normal[6], inv[9], 1e-5f, "normal[1][2] == inv[2][1] (inverse-transpose)");
    EXPECT_NEAR(normal[8], inv[2], 1e-5f, "normal[2][0] == inv[0][2] (inverse-transpose)");
    EXPECT_NEAR(normal[9], inv[6], 1e-5f, "normal[2][1] == inv[1][2] (inverse-transpose)");
    EXPECT_NEAR(normal[10], inv[10], 1e-5f, "normal[2][2] == inv[2][2]");
}

static void test_invert_matrix4_success(void) {
    const float matrix[16] = {
        2.0f,
        0.0f,
        0.0f,
        4.0f,
        0.0f,
        4.0f,
        0.0f,
        6.0f,
        0.0f,
        0.0f,
        8.0f,
        8.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
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
        1.0f,
        2.0f,
        3.0f,
        4.0f,
        2.0f,
        4.0f,
        6.0f,
        8.0f,
        0.0f,
        1.0f,
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
    };
    float inv[16];

    EXPECT_TRUE(vgfx3d_invert_matrix4(matrix, inv) != 0,
                "Matrix inversion rejects singular matrices");
}

static void test_draw_cmd_alpha_blend_policy(void) {
    vgfx3d_draw_cmd_t cmd;

    memset(&cmd, 0, sizeof(cmd));
    cmd.alpha = 0.5f;
    cmd.workflow = RT_MATERIAL3D_WORKFLOW_LEGACY;
    EXPECT_TRUE(vgfx3d_draw_cmd_uses_alpha_blend(&cmd),
                "Legacy translucent materials use alpha blending");

    memset(&cmd, 0, sizeof(cmd));
    cmd.alpha = 1.0f;
    cmd.workflow = RT_MATERIAL3D_WORKFLOW_PBR;
    cmd.alpha_mode = RT_MATERIAL3D_ALPHA_MODE_OPAQUE;
    cmd.diffuse_color[3] = 0.0f;
    EXPECT_TRUE(vgfx3d_draw_cmd_uses_alpha_blend(&cmd),
                "PBR scalar/base-color alpha uses alpha blending for backend depth policy");

    memset(&cmd, 0, sizeof(cmd));
    cmd.alpha = 1.0f;
    cmd.workflow = RT_MATERIAL3D_WORKFLOW_PBR;
    cmd.alpha_mode = RT_MATERIAL3D_ALPHA_MODE_MASK;
    EXPECT_TRUE(!vgfx3d_draw_cmd_uses_alpha_blend(&cmd),
                "PBR masked materials do not fall onto the blend path");

    memset(&cmd, 0, sizeof(cmd));
    cmd.alpha = 1.0f;
    cmd.workflow = RT_MATERIAL3D_WORKFLOW_PBR;
    cmd.alpha_mode = RT_MATERIAL3D_ALPHA_MODE_BLEND;
    EXPECT_TRUE(vgfx3d_draw_cmd_uses_alpha_blend(&cmd),
                "PBR blend materials disable depth writes across GPU backends");

    memset(&cmd, 0, sizeof(cmd));
    cmd.alpha = 1.0f;
    cmd.workflow = RT_MATERIAL3D_WORKFLOW_LEGACY;
    cmd.alpha_mode = RT_MATERIAL3D_ALPHA_MODE_BLEND;
    EXPECT_TRUE(vgfx3d_draw_cmd_uses_alpha_blend(&cmd),
                "Legacy materials honor explicit blend alpha mode");

    memset(&cmd, 0, sizeof(cmd));
    cmd.alpha = 0.25f;
    cmd.workflow = RT_MATERIAL3D_WORKFLOW_LEGACY;
    cmd.alpha_mode = RT_MATERIAL3D_ALPHA_MODE_MASK;
    EXPECT_TRUE(!vgfx3d_draw_cmd_uses_alpha_blend(&cmd),
                "Legacy masked materials stay on the opaque path");

    memset(&cmd, 0, sizeof(cmd));
    cmd.workflow = RT_MATERIAL3D_WORKFLOW_LEGACY;
    cmd.additive_blend = 1;
    EXPECT_TRUE(!vgfx3d_draw_cmd_uses_alpha_blend(&cmd),
                "Additive materials do not use the alpha-blend depth policy");
    EXPECT_TRUE(vgfx3d_draw_cmd_uses_transparent_blend(&cmd),
                "Additive materials still route through the transparent pass");
}

static void set_identity4x4(float *m) {
    memset(m, 0, sizeof(float) * 16u);
    m[0] = 1.0f;
    m[5] = 1.0f;
    m[10] = 1.0f;
    m[15] = 1.0f;
}

static void test_skinning_normalizes_weights_and_copies_without_palette(void) {
    vgfx3d_vertex_t src[1];
    vgfx3d_vertex_t dst[1];
    float palette[32];
    vgfx3d_skinning_scratch_t scratch = {0};

    memset(src, 0, sizeof(src));
    set_identity4x4(&palette[0]);
    set_identity4x4(&palette[16]);
    palette[16 + 3] = 4.0f;
    src[0].pos[0] = 1.0f;
    src[0].normal[0] = 1.0f;
    src[0].bone_indices[0] = 0;
    src[0].bone_indices[1] = 1;
    src[0].bone_weights[0] = 0.25f;
    src[0].bone_weights[1] = 0.25f;

    memset(dst, 0, sizeof(dst));
    vgfx3d_skin_vertices(src, dst, 1, palette, 2, &scratch);
    EXPECT_NEAR(dst[0].pos[0],
                3.0f,
                1e-6f,
                "CPU skinning normalizes non-unit bone weights before writing positions");
    EXPECT_NEAR(dst[0].normal[0],
                1.0f,
                1e-6f,
                "CPU skinning preserves normalized normals after weight normalization");

    memset(dst, 0, sizeof(dst));
    vgfx3d_skin_vertices(src, dst, 1, NULL, 0, &scratch);
    EXPECT_NEAR(dst[0].pos[0],
                src[0].pos[0],
                1e-6f,
                "CPU skinning copies vertices through when no palette is available");
    vgfx3d_skinning_scratch_free(&scratch);
}

static void test_skinning_uses_inverse_transpose_normals(void) {
    vgfx3d_vertex_t src[1];
    vgfx3d_vertex_t dst[1];
    float palette[16];
    vgfx3d_skinning_scratch_t scratch = {0};

    memset(src, 0, sizeof(src));
    set_identity4x4(palette);
    palette[0] = 2.0f;
    palette[10] = 0.5f;
    src[0].normal[0] = 0.70710677f;
    src[0].normal[2] = 0.70710677f;
    src[0].bone_indices[0] = 0;
    src[0].bone_weights[0] = 1.0f;

    memset(dst, 0, sizeof(dst));
    vgfx3d_skin_vertices(src, dst, 1, palette, 1, &scratch);

    EXPECT_NEAR(dst[0].normal[0],
                0.24253564f,
                1e-5f,
                "CPU skinning applies inverse-transpose X scale to normals");
    EXPECT_NEAR(dst[0].normal[2],
                0.97014254f,
                1e-5f,
                "CPU skinning applies inverse-transpose Z scale to normals");
    vgfx3d_skinning_scratch_free(&scratch);
}

static void test_frustum_and_mesh_aabb_reject_invalid_inputs_conservatively(void) {
    vgfx3d_frustum_t f;
    float minv[3] = {-1.0f, -1.0f, -1.0f};
    float maxv[3] = {1.0f, 1.0f, 1.0f};
    float out_min[3] = {9.0f, 9.0f, 9.0f};
    float out_max[3] = {9.0f, 9.0f, 9.0f};
    vgfx3d_vertex_t vertices[2];
    float invalid_vp[16];

    memset(invalid_vp, 0, sizeof(invalid_vp));
    invalid_vp[0] = NAN;
    vgfx3d_frustum_extract(&f, invalid_vp);
    EXPECT_TRUE(vgfx3d_frustum_test_aabb(&f, minv, maxv) == 1,
                "Invalid frustum extraction keeps AABB culling conservative");
    EXPECT_TRUE(vgfx3d_frustum_test_aabb(NULL, minv, maxv) == 1,
                "AABB culling treats null frustums as conservative intersections");
    EXPECT_TRUE(vgfx3d_frustum_test_sphere(&f, minv, -1.0f) == 1,
                "Sphere culling treats invalid radii as conservative intersections");

    memset(vertices, 0, sizeof(vertices));
    vertices[0].pos[0] = NAN;
    vertices[1].pos[0] = -2.0f;
    vertices[1].pos[1] = 3.0f;
    vertices[1].pos[2] = 4.0f;
    vgfx3d_compute_mesh_aabb(vertices, 2, sizeof(vertices[0]), out_min, out_max);
    EXPECT_NEAR(out_min[0],
                -2.0f,
                1e-6f,
                "Mesh AABB skips non-finite positions while keeping valid vertices");
    EXPECT_NEAR(
        out_max[2], 4.0f, 1e-6f, "Mesh AABB keeps finite position bounds after invalid vertices");

    out_min[0] = out_max[0] = 9.0f;
    vgfx3d_compute_mesh_aabb(vertices, 2, 1, out_min, out_max);
    EXPECT_NEAR(
        out_min[0], 0.0f, 1e-6f, "Mesh AABB rejects strides too small to contain positions");
    EXPECT_NEAR(out_max[0], 0.0f, 1e-6f, "Mesh AABB zeroes invalid stride outputs");
}

static void test_transform_aabb_orders_inverted_extents(void) {
    const float obj_min[3] = {3.0f, -2.0f, 5.0f};
    const float obj_max[3] = {-1.0f, 4.0f, -6.0f};
    const double world[16] = {
        1.0,
        0.0,
        0.0,
        10.0,
        0.0,
        1.0,
        0.0,
        20.0,
        0.0,
        0.0,
        1.0,
        30.0,
        0.0,
        0.0,
        0.0,
        1.0,
    };
    float out_min[3];
    float out_max[3];

    vgfx3d_transform_aabb(obj_min, obj_max, world, out_min, out_max);

    EXPECT_TRUE(out_min[0] <= 9.0f && out_min[0] >= 8.99999f,
                "AABB transform accepts inverted X extents conservatively");
    EXPECT_TRUE(out_min[2] <= 24.0f && out_min[2] >= 23.99998f,
                "AABB transform accepts inverted Z extents conservatively");
    EXPECT_TRUE(out_max[0] >= 13.0f && out_max[0] <= 13.00001f,
                "AABB transform refits max X after ordering conservatively");
    EXPECT_TRUE(out_max[2] >= 35.0f && out_max[2] <= 35.00002f,
                "AABB transform refits max Z after ordering conservatively");
}

static void test_skinning_multibone_reuses_normal_palette(void) {
    vgfx3d_vertex_t src[3];
    vgfx3d_vertex_t dst[3];
    float palette[32]; /* two bones */
    vgfx3d_skinning_scratch_t scratch = {0};

    memset(src, 0, sizeof(src));
    set_identity4x4(&palette[0]);
    palette[0] = 2.0f;             /* bone 0: scale x by 2 */
    palette[10] = 0.5f;            /* bone 0: scale z by 0.5 */
    set_identity4x4(&palette[16]); /* bone 1: identity (normals pass through) */

    for (int v = 0; v < 3; v++) {
        src[v].normal[0] = 0.70710677f;
        src[v].normal[2] = 0.70710677f;
        src[v].bone_weights[0] = 1.0f;
    }
    /* Vertices 0 and 2 share bone 0 (exercises reuse of the precomputed normal
     * palette); vertex 1 uses the distinct identity bone 1. */
    src[0].bone_indices[0] = 0;
    src[1].bone_indices[0] = 1;
    src[2].bone_indices[0] = 0;

    memset(dst, 0, sizeof(dst));
    vgfx3d_skin_vertices(src, dst, 3, palette, 2, &scratch);

    /* Bone 0's inverse-transpose of diag(2,1,0.5) is diag(0.5,1,2); applied to
     * (0.707,0,0.707) and renormalized gives (0.2425, 0, 0.9701). */
    EXPECT_NEAR(dst[0].normal[0],
                0.24253564f,
                1e-5f,
                "Multi-bone skinning matches the single-bone normal reference (vertex 0)");
    EXPECT_NEAR(dst[2].normal[0],
                0.24253564f,
                1e-5f,
                "Precomputed bone normal palette is reused across vertices (vertex 2)");
    EXPECT_NEAR(dst[2].normal[2],
                0.97014254f,
                1e-5f,
                "Reused bone normal palette preserves the Z component (vertex 2)");
    EXPECT_NEAR(dst[1].normal[0],
                0.70710677f,
                1e-5f,
                "A distinct identity bone leaves vertex 1's normal unchanged");
    vgfx3d_skinning_scratch_free(&scratch);
}

static void test_skinning_clamps_corrupt_oversized_bone_count(void) {
    vgfx3d_vertex_t src[1];
    vgfx3d_vertex_t dst[1];
    float palette[256 * 16];
    vgfx3d_skinning_scratch_t scratch = {0};

    memset(src, 0, sizeof(src));
    for (int i = 0; i < 256; i++)
        set_identity4x4(&palette[i * 16]);
    palette[255 * 16 + 3] = 7.0f;

    src[0].pos[0] = 2.0f;
    src[0].normal[0] = 1.0f;
    src[0].tangent[1] = 1.0f;
    src[0].tangent[3] = 1.0f;
    src[0].bone_indices[0] = 255;
    src[0].bone_weights[0] = 1.0f;

    memset(dst, 0, sizeof(dst));
    vgfx3d_skin_vertices(src, dst, 1, palette, INT32_MAX, &scratch);

    EXPECT_NEAR(dst[0].pos[0],
                9.0f,
                1e-6f,
                "CPU skinning clamps corrupt oversized bone counts to the 8-bit palette range");
    EXPECT_NEAR(dst[0].normal[0],
                1.0f,
                1e-6f,
                "CPU skinning keeps normals valid when clamping oversized bone counts");
    EXPECT_NEAR(dst[0].tangent[1],
                1.0f,
                1e-6f,
                "CPU skinning keeps tangents valid when clamping oversized bone counts");
    vgfx3d_skinning_scratch_free(&scratch);
}

static void test_skinning_scratch_reuses_normal_palette_without_output_drift(void) {
    enum { VERTEX_COUNT = 8, BONE_COUNT = 4 };

    vgfx3d_vertex_t src[VERTEX_COUNT];
    vgfx3d_vertex_t reference[VERTEX_COUNT];
    vgfx3d_vertex_t warmed[VERTEX_COUNT];
    float palette[BONE_COUNT * 16];
    vgfx3d_skinning_scratch_t scratch = {0};

    memset(src, 0, sizeof(src));
    for (int b = 0; b < BONE_COUNT; b++)
        set_identity4x4(&palette[b * 16]);
    palette[0] = 1.25f;
    palette[5] = 0.75f;
    palette[10] = 1.5f;
    palette[16 + 3] = 2.0f;
    palette[16 + 7] = -1.0f;
    palette[32 + 0] = 0.5f;
    palette[32 + 10] = 2.0f;
    palette[48 + 3] = -3.0f;
    palette[48 + 7] = 0.5f;
    palette[48 + 11] = 1.0f;

    for (int i = 0; i < VERTEX_COUNT; i++) {
        src[i].pos[0] = (float)i * 0.25f + 1.0f;
        src[i].pos[1] = (float)(i % 3) - 1.0f;
        src[i].pos[2] = (float)(i % 5) * 0.5f;
        src[i].normal[0] = 0.25f;
        src[i].normal[1] = 0.5f;
        src[i].normal[2] = 0.75f;
        src[i].tangent[0] = 1.0f;
        src[i].tangent[1] = 0.25f;
        src[i].tangent[2] = 0.0f;
        src[i].tangent[3] = 1.0f;
        src[i].uv[0] = (float)i * 0.1f;
        src[i].uv[1] = (float)i * 0.2f;
        src[i].color[0] = 1.0f;
        src[i].color[1] = 0.5f;
        src[i].color[2] = 0.25f;
        src[i].color[3] = 1.0f;
        src[i].bone_indices[0] = (uint8_t)(i % BONE_COUNT);
        src[i].bone_indices[1] = (uint8_t)((i + 1) % BONE_COUNT);
        src[i].bone_indices[2] = (uint8_t)((i + 2) % BONE_COUNT);
        src[i].bone_weights[0] = 0.2f;
        src[i].bone_weights[1] = 0.3f;
        src[i].bone_weights[2] = 0.5f;
    }

    memset(reference, 0, sizeof(reference));
    vgfx3d_skin_vertices(src, reference, VERTEX_COUNT, palette, BONE_COUNT, NULL);
    memset(warmed, 0, sizeof(warmed));
    vgfx3d_skin_vertices(src, warmed, VERTEX_COUNT, palette, BONE_COUNT, &scratch);
    EXPECT_TRUE(memcmp(reference, warmed, sizeof(reference)) == 0,
                "Scratch-backed CPU skinning is byte-identical to the no-scratch fallback");
    EXPECT_TRUE(scratch.normal_palette_grow_count == 1,
                "CPU skinning scratch grows once during warmup");

    uint64_t grow_count_after_warmup = scratch.normal_palette_grow_count;
    for (int draw = 0; draw < 100; draw++) {
        memset(warmed, 0, sizeof(warmed));
        vgfx3d_skin_vertices(src, warmed, VERTEX_COUNT, palette, BONE_COUNT, &scratch);
        EXPECT_TRUE(memcmp(reference, warmed, sizeof(reference)) == 0,
                    "Warm CPU skinning scratch preserves byte-identical output");
    }
    EXPECT_TRUE(scratch.normal_palette_grow_count == grow_count_after_warmup,
                "CPU skinning performs zero scratch allocations in a 100-draw warm loop");
    vgfx3d_skinning_scratch_free(&scratch);
}

static void test_frustum_valid_classifies_volumes(void) {
    vgfx3d_frustum_t f;
    float vp[16];
    float in_min[3] = {-0.5f, -0.5f, -0.5f};
    float in_max[3] = {0.5f, 0.5f, 0.5f};
    float out_min[3] = {5.0f, 5.0f, 5.0f};
    float out_max[3] = {6.0f, 6.0f, 6.0f};
    float cross_min[3] = {0.5f, 0.5f, 0.5f};
    float cross_max[3] = {1.5f, 1.5f, 1.5f};
    float center_in[3] = {0.0f, 0.0f, 0.0f};
    float center_out[3] = {5.0f, 0.0f, 0.0f};

    /* An identity view-projection yields the canonical NDC cube [-1,1]^3. */
    set_identity4x4(vp);
    vgfx3d_frustum_extract(&f, vp);
    EXPECT_TRUE(f.planes_valid == 1, "A valid VP extraction marks the frustum planes valid");
    EXPECT_TRUE(vgfx3d_frustum_test_aabb(&f, in_min, in_max) == 2,
                "Frustum classifies an enclosed AABB as fully inside");
    EXPECT_TRUE(vgfx3d_frustum_test_aabb(&f, out_min, out_max) == 0,
                "Frustum classifies a distant AABB as outside");
    EXPECT_TRUE(vgfx3d_frustum_test_aabb(&f, cross_min, cross_max) == 1,
                "Frustum classifies a straddling AABB as intersecting");
    EXPECT_TRUE(vgfx3d_frustum_test_sphere(&f, center_in, 0.25f) == 2,
                "Frustum classifies an enclosed sphere as fully inside");
    EXPECT_TRUE(vgfx3d_frustum_test_sphere(&f, center_out, 0.25f) == 0,
                "Frustum classifies a distant sphere as outside");
}

static void test_compute_normal_matrix_small_scale(void) {
    float model[16];
    float nm[16];

    /* A uniform 0.002 scale has a 3x3 determinant of 8e-9 — below the former
     * 1e-8 singular threshold, which wrongly fell back to identity. The shared
     * 1e-12 threshold must instead produce the real normal matrix (diag 500). */
    set_identity4x4(model);
    model[0] = 0.002f;
    model[5] = 0.002f;
    model[10] = 0.002f;
    vgfx3d_compute_normal_matrix4(model, nm);

    EXPECT_TRUE(nm[0] > 100.0f,
                "Small uniform scale yields a real normal matrix, not the identity fallback");
    EXPECT_NEAR(nm[0], 500.0f, 1.0f, "Normal matrix inverts a small uniform scale (X)");
    EXPECT_NEAR(nm[5], 500.0f, 1.0f, "Normal matrix inverts a small uniform scale (Y)");
}

/// @brief Decode IEEE binary16 bits back to float (test-local reference decoder).
static float test_half_bits_to_float(uint16_t h) {
    uint32_t sign = (uint32_t)(h & 0x8000u) << 16;
    uint32_t exponent = (h >> 10) & 0x1Fu;
    uint32_t mantissa = h & 0x3FFu;
    uint32_t bits;
    float out;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {
            /* subnormal: value = mantissa * 2^-24 */
            float sub = (float)mantissa / 16777216.0f;
            memcpy(&bits, &sub, sizeof(bits));
            bits |= sign;
        }
    } else {
        bits = sign | ((exponent - 15u + 127u) << 23) | (mantissa << 13);
    }
    memcpy(&out, &bits, sizeof(out));
    return out;
}

static void test_compact_vertex_stream_round_trip(void) {
    vgfx3d_vertex_t v;
    uint8_t out[VGFX3D_COMPACT_VERTEX_STRIDE];
    int16_t s16[4];
    uint16_t h16[2];
    float pos_back[3];

    memset(&v, 0, sizeof(v));
    v.pos[0] = 1.25f;
    v.pos[1] = -37.5f;
    v.pos[2] = 1024.0625f;
    v.normal[0] = 0.267261f;
    v.normal[1] = 0.534522f;
    v.normal[2] = -0.801784f;
    v.uv[0] = 0.375f;
    v.uv[1] = 12.5f; /* tiled UV must survive half precision */
    v.uv1[0] = -0.25f;
    v.uv1[1] = 0.9990234375f; /* exactly representable in half */
    v.color[0] = 1.0f;
    v.color[1] = 0.5f;
    v.color[2] = 0.0f;
    v.color[3] = 0.2f;
    v.tangent[0] = -1.0f;
    v.tangent[1] = 0.0f;
    v.tangent[2] = 0.0f;
    v.tangent[3] = -1.0f; /* handedness must round-trip exactly */
    v.bone_indices[0] = 3;
    v.bone_indices[1] = 200;
    v.bone_indices[2] = 0;
    v.bone_indices[3] = 255;
    v.bone_weights[0] = 0.7f;
    v.bone_weights[1] = 0.3f;

    vgfx3d_encode_compact_vertices(&v, 1u, out);

    memcpy(pos_back, out + 0, sizeof(pos_back));
    EXPECT_NEAR(pos_back[0], 1.25f, 0.0f, "Compact stream keeps full-precision positions (X)");
    EXPECT_NEAR(pos_back[2], 1024.0625f, 0.0f,
                "Compact stream keeps full-precision positions (Z)");

    memcpy(s16, out + 12, sizeof(s16));
    EXPECT_NEAR((float)s16[0] / 32767.0f, 0.267261f, 0.0001f, "Normal X survives snorm16");
    EXPECT_NEAR((float)s16[2] / 32767.0f, -0.801784f, 0.0001f, "Normal Z survives snorm16");
    EXPECT_TRUE(s16[3] == 0, "Normal W lane stays zero");

    memcpy(h16, out + 20, sizeof(h16));
    EXPECT_NEAR(test_half_bits_to_float(h16[0]), 0.375f, 0.0f, "UV0.x is exact in half");
    EXPECT_NEAR(test_half_bits_to_float(h16[1]), 12.5f, 0.0f, "Tiled UV0.y is exact in half");
    memcpy(h16, out + 24, sizeof(h16));
    EXPECT_NEAR(test_half_bits_to_float(h16[0]), -0.25f, 0.0f, "UV1.x sign survives half");
    EXPECT_NEAR(test_half_bits_to_float(h16[1]), 0.9990234375f, 0.0f, "UV1.y is exact in half");

    EXPECT_TRUE(out[28] == 255 && out[30] == 0, "Color endpoints encode exactly in unorm8");
    EXPECT_TRUE(out[29] == 128, "Color midpoint rounds to 128");

    memcpy(s16, out + 32, sizeof(s16));
    EXPECT_TRUE(s16[0] == -32767, "Tangent X = -1 encodes to snorm16 minimum");
    EXPECT_TRUE(s16[3] == -32767, "Tangent handedness -1 round-trips exactly");

    EXPECT_TRUE(out[40] == 3 && out[41] == 200 && out[43] == 255,
                "Bone indices copy through untouched");
    EXPECT_NEAR((float)out[44] / 255.0f, 0.7f, 0.003f, "Bone weight survives unorm8");
    EXPECT_NEAR((float)out[44] / 255.0f + (float)out[45] / 255.0f, 1.0f, 0.006f,
                "Weight pair stays renormalizable");
}

static void test_compact_vertex_stream_half_edge_cases(void) {
    vgfx3d_vertex_t v;
    uint8_t out[VGFX3D_COMPACT_VERTEX_STRIDE];
    uint16_t h16[2];

    memset(&v, 0, sizeof(v));
    v.uv[0] = 1.0e9f;  /* beyond half range: clamps to 65504 */
    v.uv[1] = -1.0e9f; /* clamps to -65504 */
    vgfx3d_encode_compact_vertices(&v, 1u, out);
    memcpy(h16, out + 20, sizeof(h16));
    EXPECT_NEAR(test_half_bits_to_float(h16[0]), 65504.0f, 0.0f, "Half overflow clamps to max");
    EXPECT_NEAR(test_half_bits_to_float(h16[1]), -65504.0f, 0.0f,
                "Half overflow clamps to min");

    v.uv[0] = 1.0e-9f; /* below subnormal range: flushes to zero */
    vgfx3d_encode_compact_vertices(&v, 1u, out);
    memcpy(h16, out + 20, sizeof(h16));
    EXPECT_NEAR(test_half_bits_to_float(h16[0]), 0.0f, 0.0f, "Half underflow flushes to zero");

    v.normal[0] = 5.0f; /* out-of-range normals clamp instead of wrapping */
    vgfx3d_encode_compact_vertices(&v, 1u, out);
    {
        int16_t s16[4];
        memcpy(s16, out + 12, sizeof(s16));
        EXPECT_TRUE(s16[0] == 32767, "Out-of-range normal clamps to snorm16 max");
    }
}

int main(void) {
    test_unpack_pixels_rgba_success();
    test_unpack_pixels_rgba_rejects_invalid();
    test_estimate_pixels_rgba_upload_bytes();
    test_unpack_pixels_rgba_rows_and_extent();
    test_upload_rows_for_budget();
    test_pending_upload_bytes_return_to_baseline();
    test_compressed_block_upload_budget_and_pending_bytes();
    test_unpack_cubemap_faces_rgba_success();
    test_unpack_cubemap_faces_rgba_rejects_invalid();
    test_estimate_cubemap_rgba_upload_bytes();
    test_unpack_cubemap_rows_and_extent();
    test_flip_rgba_rows();
    test_hdr_readback_helpers();
    test_generation_helpers();
    test_compute_normal_matrix_inverse_transpose();
    test_compute_normal_matrix_inverse_transpose_offdiagonal();
    test_compute_normal_matrix_singular_fallback();
    test_invert_matrix4_success();
    test_invert_matrix4_rejects_singular();
    test_draw_cmd_alpha_blend_policy();
    test_skinning_normalizes_weights_and_copies_without_palette();
    test_skinning_uses_inverse_transpose_normals();
    test_frustum_and_mesh_aabb_reject_invalid_inputs_conservatively();
    test_transform_aabb_orders_inverted_extents();
    test_skinning_multibone_reuses_normal_palette();
    test_skinning_clamps_corrupt_oversized_bone_count();
    test_skinning_scratch_reuses_normal_palette_without_output_drift();
    test_frustum_valid_classifies_volumes();
    test_compute_normal_matrix_small_scale();
    test_compact_vertex_stream_round_trip();
    test_compact_vertex_stream_half_edge_cases();

    printf("vgfx3d_backend_utils tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
