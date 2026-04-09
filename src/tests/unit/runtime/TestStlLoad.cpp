//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/runtime/TestStlLoad.cpp
// Purpose: Unit tests for the STL (Binary + ASCII) mesh loader.
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>

extern "C" {
void *rt_mesh3d_from_stl(void *path);
int64_t rt_mesh3d_get_vertex_count(void *obj);
int64_t rt_mesh3d_get_triangle_count(void *obj);
void *rt_const_cstr(const char *str);
int64_t rt_obj_release_check0(void *obj);
void rt_obj_free(void *obj);
}

static void free_mesh(void *m) {
    if (m && rt_obj_release_check0(m))
        rt_obj_free(m);
}

static const char *write_temp(const char *name, const void *data, size_t len) {
    static char path[256];
    snprintf(path, sizeof(path), "/tmp/viper_test_%s", name);
    FILE *f = fopen(path, "wb");
    if (!f)
        return nullptr;
    fwrite(data, 1, len, f);
    fclose(f);
    return path;
}

TEST(StlLoadTest, RejectNull) {
    void *mesh = rt_mesh3d_from_stl(nullptr);
    EXPECT_EQ(mesh, nullptr);
}

TEST(StlLoadTest, RejectNonExistent) {
    void *rts = rt_const_cstr("/tmp/nonexistent_stl.stl");
    void *mesh = rt_mesh3d_from_stl(rts);
    EXPECT_EQ(mesh, nullptr);
}

TEST(StlLoadTest, RejectGarbage) {
    const uint8_t garbage[] = {0x00, 0x01, 0x02, 0x03};
    const char *path = write_temp("garbage.stl", garbage, sizeof(garbage));
    void *rts = rt_const_cstr(path);
    void *mesh = rt_mesh3d_from_stl(rts);
    EXPECT_EQ(mesh, nullptr);
    remove(path);
}

TEST(StlLoadTest, BinaryOneTriangle) {
    // Construct a minimal binary STL: 80-byte header + 1 triangle (134 bytes total)
    uint8_t stl[134];
    memset(stl, 0, sizeof(stl));

    // Header: 80 bytes (already zero)
    // Triangle count: 1
    stl[80] = 1;
    stl[81] = stl[82] = stl[83] = 0;

    // Triangle: normal (0, 1, 0) + 3 vertices forming an XZ triangle
    float normal[] = {0.0f, 1.0f, 0.0f};
    float v1[] = {0.0f, 0.0f, 0.0f};
    float v2[] = {1.0f, 0.0f, 0.0f};
    float v3[] = {0.0f, 0.0f, 1.0f};

    memcpy(stl + 84, normal, 12);
    memcpy(stl + 96, v1, 12);
    memcpy(stl + 108, v2, 12);
    memcpy(stl + 120, v3, 12);
    // Attribute bytes: stl[132..133] = 0 (already zero)

    const char *path = write_temp("one_tri.stl", stl, sizeof(stl));
    void *rts = rt_const_cstr(path);
    void *mesh = rt_mesh3d_from_stl(rts);
    ASSERT_TRUE(mesh != nullptr);

    EXPECT_EQ(rt_mesh3d_get_vertex_count(mesh), 3);
    EXPECT_EQ(rt_mesh3d_get_triangle_count(mesh), 1);

    free_mesh(mesh);
    remove(path);
}

TEST(StlLoadTest, AsciiOneTriangle) {
    const char *ascii_stl = "solid test\n"
                            "  facet normal 0 1 0\n"
                            "    outer loop\n"
                            "      vertex 0 0 0\n"
                            "      vertex 1 0 0\n"
                            "      vertex 0 0 1\n"
                            "    endloop\n"
                            "  endfacet\n"
                            "endsolid test\n";

    const char *path = write_temp("one_tri_ascii.stl", ascii_stl, strlen(ascii_stl));
    void *rts = rt_const_cstr(path);
    void *mesh = rt_mesh3d_from_stl(rts);
    ASSERT_TRUE(mesh != nullptr);

    EXPECT_EQ(rt_mesh3d_get_vertex_count(mesh), 3);
    EXPECT_EQ(rt_mesh3d_get_triangle_count(mesh), 1);

    free_mesh(mesh);
    remove(path);
}

int main() {
    return viper_test::run_all_tests();
}
