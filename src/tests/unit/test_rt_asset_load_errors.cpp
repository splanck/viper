//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/test_rt_asset_load_errors.cpp
// Purpose: Unit tests for recoverable runtime content-loader diagnostics.
// Key invariants:
//   - Bad or missing content returns NULL and records a queryable error.
//   - Optional material texture loss records warnings without failing the parent load.
// Ownership/Lifetime:
//   - Tests release any GC-managed runtime objects they receive.
//   - Temporary files are created under /tmp and removed by each test.
// Links: rt_asset_error.h, docs/viperlib/graphics/rendering3d.md
//
//===----------------------------------------------------------------------===//

#ifndef VIPER_ENABLE_GRAPHICS
#define VIPER_ENABLE_GRAPHICS 1
#endif

#include "rt_asset_error.h"
#include "rt_string.h"
#include "tests/TestHarness.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

extern "C" {
void *rt_model3d_load(rt_string path);
void *rt_fbx_load(rt_string path);
void *rt_gltf_load(rt_string path);
void *rt_mesh3d_from_obj(rt_string path);
void *rt_mesh3d_from_stl(rt_string path);
void *rt_scene3d_load(rt_string path);
void *rt_pixels_load(void *path);
rt_string rt_const_cstr(const char *str);
int64_t rt_obj_release_check0(void *obj);
void rt_obj_free(void *obj);
}

extern "C" void vm_trap(const char *msg) {
    std::fprintf(stderr, "unexpected runtime trap: %s\n", msg ? msg : "(null)");
    std::abort();
}

using LoaderFn = void *(*)(rt_string path);

static std::string tmp_path(const char *name) {
    return std::string("/tmp/viper_asset_error_") + name;
}

static void write_bytes(const char *path, const void *data, size_t size) {
    FILE *f = std::fopen(path, "wb");
    ASSERT_TRUE(f != nullptr);
    ASSERT_EQ(std::fwrite(data, 1, size, f), size);
    std::fclose(f);
}

static void write_text(const char *path, const char *text) {
    write_bytes(path, text, std::strlen(text));
}

static void release_obj(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static void expect_error_recorded(const char *context) {
    const char *message = rt_asset_error_get_message();
    EXPECT_NE(rt_asset_error_get_code(), RT_ASSET_ERROR_NONE);
    EXPECT_TRUE(message != nullptr);
    EXPECT_GT(std::strlen(message), (size_t)0);
    (void)context;
}

static void expect_null_with_error(LoaderFn loader, const char *path, const char *context) {
    void *obj = loader(rt_const_cstr(path));
    EXPECT_EQ(obj, nullptr);
    expect_error_recorded(context);
}

static void expect_null_pixels_with_error(const char *path, const char *context) {
    void *obj = rt_pixels_load(rt_const_cstr(path));
    EXPECT_EQ(obj, nullptr);
    expect_error_recorded(context);
}

TEST(AssetLoadErrors, MissingFilesReturnNullAndSetError) {
    std::string model = tmp_path("missing_model.fbx");
    std::remove(model.c_str());
    expect_null_with_error(rt_model3d_load, model.c_str(), "Model3D.Load missing");

    std::string fbx = tmp_path("missing_direct.fbx");
    std::remove(fbx.c_str());
    expect_null_with_error(rt_fbx_load, fbx.c_str(), "FBX.Load missing");

    std::string gltf = tmp_path("missing_scene.gltf");
    std::remove(gltf.c_str());
    expect_null_with_error(rt_gltf_load, gltf.c_str(), "GLTF.Load missing");

    std::string obj = tmp_path("missing_mesh.obj");
    std::remove(obj.c_str());
    expect_null_with_error(rt_mesh3d_from_obj, obj.c_str(), "Mesh3D.FromOBJ missing");

    std::string stl = tmp_path("missing_mesh.stl");
    std::remove(stl.c_str());
    expect_null_with_error(rt_mesh3d_from_stl, stl.c_str(), "Mesh3D.FromSTL missing");

    std::string vscn = tmp_path("missing_scene.vscn");
    std::remove(vscn.c_str());
    expect_null_with_error(rt_scene3d_load, vscn.c_str(), "Scene3D.Load missing");

    std::string png = tmp_path("missing_pixels.png");
    std::remove(png.c_str());
    expect_null_pixels_with_error(png.c_str(), "Pixels.Load missing");
}

TEST(AssetLoadErrors, TruncatedFilesReturnNullAndSetError) {
    const uint8_t png_header[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

    std::string model = tmp_path("truncated_model.fbx");
    write_text(model.c_str(), "K");
    expect_null_with_error(rt_model3d_load, model.c_str(), "Model3D.Load truncated");
    std::remove(model.c_str());

    std::string fbx = tmp_path("truncated_direct.fbx");
    write_text(fbx.c_str(), "K");
    expect_null_with_error(rt_fbx_load, fbx.c_str(), "FBX.Load truncated");
    std::remove(fbx.c_str());

    std::string gltf = tmp_path("truncated_scene.gltf");
    write_text(gltf.c_str(), "{");
    expect_null_with_error(rt_gltf_load, gltf.c_str(), "GLTF.Load truncated");
    std::remove(gltf.c_str());

    std::string obj = tmp_path("truncated_mesh.obj");
    write_text(obj.c_str(), "v 0 0");
    expect_null_with_error(rt_mesh3d_from_obj, obj.c_str(), "Mesh3D.FromOBJ truncated");
    std::remove(obj.c_str());

    std::string stl = tmp_path("truncated_mesh.stl");
    write_text(stl.c_str(), "so");
    expect_null_with_error(rt_mesh3d_from_stl, stl.c_str(), "Mesh3D.FromSTL truncated");
    std::remove(stl.c_str());

    std::string vscn = tmp_path("truncated_scene.vscn");
    write_text(vscn.c_str(), "{");
    expect_null_with_error(rt_scene3d_load, vscn.c_str(), "Scene3D.Load truncated");
    std::remove(vscn.c_str());

    std::string png = tmp_path("truncated_pixels.png");
    write_bytes(png.c_str(), png_header, sizeof(png_header));
    expect_null_pixels_with_error(png.c_str(), "Pixels.Load truncated");
    std::remove(png.c_str());
}

TEST(AssetLoadErrors, WrongMagicFilesReturnNullAndSetError) {
    std::string model = tmp_path("wrong_model.fbx");
    write_text(model.c_str(), "not an fbx");
    expect_null_with_error(rt_model3d_load, model.c_str(), "Model3D.Load wrong magic");
    std::remove(model.c_str());

    std::string fbx = tmp_path("wrong_direct.fbx");
    write_text(fbx.c_str(), "not an fbx");
    expect_null_with_error(rt_fbx_load, fbx.c_str(), "FBX.Load wrong magic");
    std::remove(fbx.c_str());

    std::string gltf = tmp_path("wrong_scene.gltf");
    write_text(gltf.c_str(), "not json");
    expect_null_with_error(rt_gltf_load, gltf.c_str(), "GLTF.Load wrong magic");
    std::remove(gltf.c_str());

    std::string obj = tmp_path("wrong_mesh.obj");
    write_text(obj.c_str(), "not obj");
    expect_null_with_error(rt_mesh3d_from_obj, obj.c_str(), "Mesh3D.FromOBJ wrong magic");
    std::remove(obj.c_str());

    std::string stl = tmp_path("wrong_mesh.stl");
    write_text(stl.c_str(), "not stl");
    expect_null_with_error(rt_mesh3d_from_stl, stl.c_str(), "Mesh3D.FromSTL wrong magic");
    std::remove(stl.c_str());

    std::string vscn = tmp_path("wrong_scene.vscn");
    write_text(vscn.c_str(), "not json");
    expect_null_with_error(rt_scene3d_load, vscn.c_str(), "Scene3D.Load wrong magic");
    std::remove(vscn.c_str());

    std::string img = tmp_path("wrong_pixels.img");
    write_text(img.c_str(), "not image");
    expect_null_pixels_with_error(img.c_str(), "Pixels.Load wrong magic");
    std::remove(img.c_str());
}

TEST(AssetLoadErrors, MissingObjMaterialTextureRecordsOneWarning) {
    std::string obj_path = tmp_path("textured_missing.obj");
    std::string mtl_path = tmp_path("textured_missing.mtl");
    write_text(mtl_path.c_str(), "newmtl bark\nmap_Kd missing_albedo.png\n");
    write_text(obj_path.c_str(),
               "mtllib viper_asset_error_textured_missing.mtl\n"
               "v 0 0 0\n"
               "v 1 0 0\n"
               "v 0 1 0\n"
               "vt 0 0\n"
               "vt 1 0\n"
               "vt 0 1\n"
               "usemtl bark\n"
               "f 1/1 2/2 3/3\n");

    void *model = rt_model3d_load(rt_const_cstr(obj_path.c_str()));
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(rt_asset_error_get_code(), RT_ASSET_ERROR_NONE);
    EXPECT_EQ(rt_asset_error_get_warning_count(), INT64_C(1));
    EXPECT_CONTAINS(rt_asset_error_get_warning(0), "missing_albedo.png");

    release_obj(model);
    std::remove(obj_path.c_str());
    std::remove(mtl_path.c_str());
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
