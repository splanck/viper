//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
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

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

// Build-time ZPAK writer
#include "ZpakWriter.hpp"

extern "C" {
#include "rt_asset.h"
#include "rt_string.h"
void *rt_bytes_from_raw(const uint8_t *data, size_t len);
uint8_t *rt_bytes_extract_raw(void *bytes, size_t *out_len);
int64_t rt_bytes_len(void *bytes);
rt_string rt_const_cstr(const char *str);
}

static const char *write_zpak_temp(const char *name,
                                  const char *entry_name,
                                  const uint8_t *data,
                                  size_t len) {
    static char path[256];
    snprintf(path, sizeof(path), "/tmp/zanna_test_%s", name);

    zanna::asset::ZpakWriter writer;
    writer.addEntry(entry_name, data, len, false);

    std::string err;
    if (!writer.writeToFile(path, err))
        return nullptr;
    return path;
}

static bool write_zpak_at(const char *path,
                         const char *entry_name,
                         const uint8_t *data,
                         size_t len) {
    zanna::asset::ZpakWriter writer;
    writer.addEntry(entry_name, data, len, false);
    std::string err;
    return writer.writeToFile(path, err);
}

// ─── Tests ──────────────────────────────────────────────────────────────────

TEST(AssetManager, MountAndFind) {
    const uint8_t data[] = "hello from pack";
    const char *zpakPath = write_zpak_temp("mount_test.zpak", "greet.txt", data, sizeof(data) - 1);
    ASSERT_TRUE(zpakPath != nullptr);

    rt_string path_str = rt_const_cstr(zpakPath);
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
    remove(zpakPath);
}

TEST(AssetManager, LoadBytesFromPack) {
    const uint8_t data[] = "raw bytes payload";
    const char *zpakPath = write_zpak_temp("load_bytes.zpak", "payload.bin", data, sizeof(data) - 1);
    ASSERT_TRUE(zpakPath != nullptr);

    rt_string path_str = rt_const_cstr(zpakPath);
    rt_asset_mount(path_str);

    rt_string name_str = rt_const_cstr("payload.bin");
    void *result = rt_asset_load_bytes(name_str);
    ASSERT_TRUE(result != nullptr);
    EXPECT_EQ(rt_bytes_len(result), (int64_t)(sizeof(data) - 1));

    rt_asset_unmount(path_str);
    remove(zpakPath);
}

TEST(AssetManager, AssetSchemeUsesMountedPack) {
    const uint8_t data[] = "asset uri payload";
    const char *zpakPath =
        write_zpak_temp("asset_scheme.zpak", "models/payload.bin", data, sizeof(data) - 1);
    ASSERT_TRUE(zpakPath != nullptr);

    rt_string path_str = rt_const_cstr(zpakPath);
    EXPECT_EQ(rt_asset_mount(path_str), 1);

    rt_string uri = rt_const_cstr("asset://models/payload.bin");
    void *bytes = rt_asset_load_bytes(uri);
    ASSERT_TRUE(bytes != nullptr);
    EXPECT_EQ(rt_bytes_len(bytes), (int64_t)(sizeof(data) - 1));

    size_t raw_len = 0;
    uint8_t *raw = rt_asset_load_raw(uri, &raw_len);
    ASSERT_TRUE(raw != nullptr);
    EXPECT_EQ(raw_len, sizeof(data) - 1);
    EXPECT_EQ(std::memcmp(raw, data, raw_len), 0);
    std::free(raw);

    rt_asset_unmount(path_str);
    remove(zpakPath);
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
    rt_string path = rt_const_cstr("/tmp/no_such_file_12345.zpak");
    int64_t ok = rt_asset_mount(path);
    EXPECT_EQ(ok, 0);
}

TEST(AssetManager, UnmountNonExistent) {
    rt_string path = rt_const_cstr("never_mounted.zpak");
    int64_t ok = rt_asset_unmount(path);
    EXPECT_EQ(ok, 0);
}

TEST(AssetManager, AmbiguousBasenameUnmountDoesNotRemoveWrongPack) {
    namespace fs = std::filesystem;
    fs::path root = fs::temp_directory_path() / "zanna_asset_basename_unmount";
    fs::remove_all(root);
    fs::create_directories(root / "a");
    fs::create_directories(root / "b");

    fs::path packA = root / "a" / "shared.zpak";
    fs::path packB = root / "b" / "shared.zpak";
    std::string packAStr = packA.string();
    std::string packBStr = packB.string();
    const uint8_t dataA[] = "a";
    const uint8_t dataB[] = "b";
    ASSERT_TRUE(write_zpak_at(packAStr.c_str(), "only-a.txt", dataA, sizeof(dataA) - 1));
    ASSERT_TRUE(write_zpak_at(packBStr.c_str(), "only-b.txt", dataB, sizeof(dataB) - 1));

    EXPECT_EQ(rt_asset_mount(rt_const_cstr(packAStr.c_str())), 1);
    EXPECT_EQ(rt_asset_mount(rt_const_cstr(packBStr.c_str())), 1);

    EXPECT_EQ(rt_asset_unmount(rt_const_cstr("shared.zpak")), 0);
    EXPECT_EQ(rt_asset_exists(rt_const_cstr("only-a.txt")), 1);
    EXPECT_EQ(rt_asset_exists(rt_const_cstr("only-b.txt")), 1);

    EXPECT_EQ(rt_asset_unmount(rt_const_cstr(packAStr.c_str())), 1);
    EXPECT_EQ(rt_asset_exists(rt_const_cstr("only-a.txt")), 0);
    EXPECT_EQ(rt_asset_exists(rt_const_cstr("only-b.txt")), 1);

    EXPECT_EQ(rt_asset_unmount(rt_const_cstr("shared.zpak")), 1);
    EXPECT_EQ(rt_asset_exists(rt_const_cstr("only-b.txt")), 0);

    fs::remove_all(root);
}

TEST(AssetManager, ListFromPack) {
    const uint8_t d1[] = "a";
    const uint8_t d2[] = "b";
    zanna::asset::ZpakWriter writer;
    writer.addEntry("alpha.txt", d1, 1, false);
    writer.addEntry("beta.txt", d2, 1, false);

    const char *zpakPath = "/tmp/zanna_test_list.zpak";
    std::string err;
    ASSERT_TRUE(writer.writeToFile(zpakPath, err));

    rt_string path_str = rt_const_cstr(zpakPath);
    rt_asset_mount(path_str);

    void *list = rt_asset_list();
    ASSERT_TRUE(list != nullptr);
    // List should contain at least our 2 entries (may have more from other tests)

    rt_asset_unmount(path_str);
    remove(zpakPath);
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
    return zanna_test::run_all_tests();
}
