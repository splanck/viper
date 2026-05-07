//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/runtime/TestAssetManager.cpp
// Purpose: Unit tests for the runtime asset manager — existence checks,
//          mounting/unmounting, and resolution order.
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

// Build-time VPA writer
#include "VpaWriter.hpp"

extern "C" {
#include "rt_asset.h"
#include "rt_string.h"
void *rt_bytes_from_raw(const uint8_t *data, size_t len);
uint8_t *rt_bytes_extract_raw(void *bytes, size_t *out_len);
int64_t rt_bytes_len(void *bytes);
rt_string rt_const_cstr(const char *str);
}

static const char *write_vpa_temp(const char *name,
                                  const char *entry_name,
                                  const uint8_t *data,
                                  size_t len) {
    static char path[256];
    snprintf(path, sizeof(path), "/tmp/viper_test_%s", name);

    viper::asset::VpaWriter writer;
    writer.addEntry(entry_name, data, len, false);

    std::string err;
    if (!writer.writeToFile(path, err))
        return nullptr;
    return path;
}

static bool write_vpa_at(const char *path, const char *entry_name, const uint8_t *data, size_t len) {
    viper::asset::VpaWriter writer;
    writer.addEntry(entry_name, data, len, false);
    std::string err;
    return writer.writeToFile(path, err);
}

// ─── Tests ──────────────────────────────────────────────────────────────────

TEST(AssetManager, MountAndFind) {
    const uint8_t data[] = "hello from pack";
    const char *vpaPath = write_vpa_temp("mount_test.vpa", "greet.txt", data, sizeof(data) - 1);
    ASSERT_TRUE(vpaPath != nullptr);

    rt_string path_str = rt_const_cstr(vpaPath);
    int64_t ok = rt_asset_mount(path_str);
    EXPECT_EQ(ok, 1);

    rt_string name_str = rt_const_cstr("greet.txt");
    EXPECT_EQ(rt_asset_exists(name_str), 1);

    int64_t sz = rt_asset_size(name_str);
    EXPECT_EQ(sz, (int64_t)(sizeof(data) - 1));

    // Unmount
    ok = rt_asset_unmount(path_str);
    EXPECT_EQ(ok, 1);

    // After unmount, should not find via pack (may still find via filesystem fallback)
    remove(vpaPath);
}

TEST(AssetManager, LoadBytesFromPack) {
    const uint8_t data[] = "raw bytes payload";
    const char *vpaPath = write_vpa_temp("load_bytes.vpa", "payload.bin", data, sizeof(data) - 1);
    ASSERT_TRUE(vpaPath != nullptr);

    rt_string path_str = rt_const_cstr(vpaPath);
    rt_asset_mount(path_str);

    rt_string name_str = rt_const_cstr("payload.bin");
    void *result = rt_asset_load_bytes(name_str);
    ASSERT_TRUE(result != nullptr);
    EXPECT_EQ(rt_bytes_len(result), (int64_t)(sizeof(data) - 1));

    rt_asset_unmount(path_str);
    remove(vpaPath);
}

TEST(AssetManager, ExistsReturnsFalse) {
    rt_string name = rt_const_cstr("definitely_nonexistent_12345.xyz");
    EXPECT_EQ(rt_asset_exists(name), 0);
}

TEST(AssetManager, SizeMissingReturnsNegativeOne) {
    rt_string name = rt_const_cstr("definitely_nonexistent_12345.xyz");
    EXPECT_EQ(rt_asset_size(name), -1);
}

TEST(AssetManager, LoadReturnsNull) {
    rt_string name = rt_const_cstr("definitely_nonexistent_12345.xyz");
    void *result = rt_asset_load(name);
    EXPECT_EQ(result, nullptr);
}

TEST(AssetManager, MountNonExistent) {
    rt_string path = rt_const_cstr("/tmp/no_such_file_12345.vpa");
    int64_t ok = rt_asset_mount(path);
    EXPECT_EQ(ok, 0);
}

TEST(AssetManager, UnmountNonExistent) {
    rt_string path = rt_const_cstr("never_mounted.vpa");
    int64_t ok = rt_asset_unmount(path);
    EXPECT_EQ(ok, 0);
}

TEST(AssetManager, AmbiguousBasenameUnmountDoesNotRemoveWrongPack) {
    namespace fs = std::filesystem;
    fs::path root = fs::temp_directory_path() / "viper_asset_basename_unmount";
    fs::remove_all(root);
    fs::create_directories(root / "a");
    fs::create_directories(root / "b");

    fs::path packA = root / "a" / "shared.vpa";
    fs::path packB = root / "b" / "shared.vpa";
    std::string packAStr = packA.string();
    std::string packBStr = packB.string();
    const uint8_t dataA[] = "a";
    const uint8_t dataB[] = "b";
    ASSERT_TRUE(write_vpa_at(packAStr.c_str(), "only-a.txt", dataA, sizeof(dataA) - 1));
    ASSERT_TRUE(write_vpa_at(packBStr.c_str(), "only-b.txt", dataB, sizeof(dataB) - 1));

    EXPECT_EQ(rt_asset_mount(rt_const_cstr(packAStr.c_str())), 1);
    EXPECT_EQ(rt_asset_mount(rt_const_cstr(packBStr.c_str())), 1);

    EXPECT_EQ(rt_asset_unmount(rt_const_cstr("shared.vpa")), 0);
    EXPECT_EQ(rt_asset_exists(rt_const_cstr("only-a.txt")), 1);
    EXPECT_EQ(rt_asset_exists(rt_const_cstr("only-b.txt")), 1);

    EXPECT_EQ(rt_asset_unmount(rt_const_cstr(packAStr.c_str())), 1);
    EXPECT_EQ(rt_asset_exists(rt_const_cstr("only-a.txt")), 0);
    EXPECT_EQ(rt_asset_exists(rt_const_cstr("only-b.txt")), 1);

    EXPECT_EQ(rt_asset_unmount(rt_const_cstr("shared.vpa")), 1);
    EXPECT_EQ(rt_asset_exists(rt_const_cstr("only-b.txt")), 0);

    fs::remove_all(root);
}

TEST(AssetManager, ListFromPack) {
    const uint8_t d1[] = "a";
    const uint8_t d2[] = "b";
    viper::asset::VpaWriter writer;
    writer.addEntry("alpha.txt", d1, 1, false);
    writer.addEntry("beta.txt", d2, 1, false);

    const char *vpaPath = "/tmp/viper_test_list.vpa";
    std::string err;
    ASSERT_TRUE(writer.writeToFile(vpaPath, err));

    rt_string path_str = rt_const_cstr(vpaPath);
    rt_asset_mount(path_str);

    void *list = rt_asset_list();
    ASSERT_TRUE(list != nullptr);
    // List should contain at least our 2 entries (may have more from other tests)

    rt_asset_unmount(path_str);
    remove(vpaPath);
}

TEST(AssetManager, NullInputs) {
    EXPECT_EQ(rt_asset_load(nullptr), nullptr);
    EXPECT_EQ(rt_asset_load_bytes(nullptr), nullptr);
    EXPECT_EQ(rt_asset_exists(nullptr), 0);
    EXPECT_EQ(rt_asset_size(nullptr), -1);
    EXPECT_EQ(rt_asset_mount(nullptr), 0);
    EXPECT_EQ(rt_asset_unmount(nullptr), 0);
}

int main() {
    return viper_test::run_all_tests();
}
