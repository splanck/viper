//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// Tests for Viper.Game2D.LevelDocument JSON loading.
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

extern "C" {
#include "rt_leveldata.h"
#include "rt_object.h"
#include "rt_string.h"
#include "rt_tilemap.h"
}

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static std::string temp_path(const char *name) {
    const char *tmp = std::getenv("TMPDIR");
    if (!tmp || !*tmp)
        tmp = std::getenv("TEMP");
    if (!tmp || !*tmp)
        tmp = ".";

    std::string base(tmp);
    if (!base.empty() && base.back() != '/' && base.back() != '\\')
        base += '/';
    return base + "viper_rt_leveldata_" + name;
}

static void write_file(const std::string &path, const char *contents) {
    FILE *f = std::fopen(path.c_str(), "wb");
    ASSERT_TRUE(f != nullptr);
    if (contents && *contents)
        std::fwrite(contents, 1, std::strlen(contents), f);
    std::fclose(f);
}

static void release_obj(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

TEST(LevelData, EmptyFileReturnsNull) {
    auto path = temp_path("empty.json");
    write_file(path, "");

    void *level = rt_leveldata_load(rt_const_cstr(path.c_str()));
    EXPECT_EQ(level, nullptr);

    std::remove(path.c_str());
}

// VDOC-238: Load is a nullable factory, so a missing file must soft-fail to NULL
// (mirroring Config.Load) rather than trapping inside the hardened read path.
TEST(LevelData, MissingFileReturnsNull) {
    auto path = temp_path("does_not_exist.json");
    std::remove(path.c_str()); // ensure absent

    void *level = rt_leveldata_load(rt_const_cstr(path.c_str()));
    EXPECT_EQ(level, nullptr);
}

TEST(LevelData, ValidFileLoadsTilemapAndObjects) {
    auto path = temp_path("valid.json");
    write_file(path,
               "{\"width\":2,\"height\":2,\"tileWidth\":8,\"tileHeight\":8,"
               "\"properties\":{\"theme\":\"forest\",\"playerStartX\":12,\"playerStartY\":34},"
               "\"layers\":[{\"type\":\"tiles\",\"data\":[1,2,3,4]}],"
               "\"objects\":[{\"type\":\"enemy\",\"id\":\"slime\",\"x\":56,\"y\":78}]}");

    void *level = rt_leveldata_load(rt_const_cstr(path.c_str()));
    ASSERT_TRUE(level != nullptr);

    void *tilemap = rt_leveldata_get_tilemap(level);
    ASSERT_TRUE(tilemap != nullptr);
    EXPECT_EQ(rt_tilemap_get_width(tilemap), 2);
    EXPECT_EQ(rt_tilemap_get_height(tilemap), 2);
    EXPECT_EQ(rt_tilemap_get_tile_width(tilemap), 8);
    EXPECT_EQ(rt_tilemap_get_tile_height(tilemap), 8);
    EXPECT_EQ(rt_tilemap_get_tile(tilemap, 1, 1), 4);

    EXPECT_EQ(rt_leveldata_player_start_x(level), 12);
    EXPECT_EQ(rt_leveldata_player_start_y(level), 34);
    EXPECT_EQ(std::strcmp(rt_string_cstr(rt_leveldata_get_theme(level)), "forest"), 0);
    EXPECT_EQ(rt_leveldata_object_count(level), 1);
    EXPECT_EQ(std::strcmp(rt_string_cstr(rt_leveldata_object_type(level, 0)), "enemy"), 0);
    EXPECT_EQ(std::strcmp(rt_string_cstr(rt_leveldata_object_id(level, 0)), "slime"), 0);
    EXPECT_EQ(rt_leveldata_object_x(level, 0), 56);
    EXPECT_EQ(rt_leveldata_object_y(level, 0), 78);

    release_obj(level);
    std::remove(path.c_str());
}

// GAME #7: layer/object nodes with the wrong JSON type (here a number and a
// string) must be skipped, not crash the loader. Before the type guard,
// rt_seq_len() on a non-seq trapped the process.
TEST(LevelData, MalformedLayersAndObjectsAreSkipped) {
    auto path = temp_path("malformed.json");
    write_file(path,
               "{\"width\":2,\"height\":2,\"tileWidth\":8,\"tileHeight\":8,"
               "\"layers\":5,\"objects\":\"nope\"}");

    void *level = rt_leveldata_load(rt_const_cstr(path.c_str()));
    // Dimensions are valid so the level still loads; the malformed layers/objects
    // nodes are ignored instead of trapping.
    ASSERT_TRUE(level != nullptr);
    EXPECT_EQ(rt_leveldata_object_count(level), 0);

    release_obj(level);
    std::remove(path.c_str());
}

// Return true if s is well-formed UTF-8 (no truncated multi-byte sequence).
static bool is_valid_utf8(const char *s) {
    const unsigned char *p = reinterpret_cast<const unsigned char *>(s);
    while (*p) {
        int extra;
        if (*p < 0x80)
            extra = 0;
        else if ((*p & 0xE0) == 0xC0)
            extra = 1;
        else if ((*p & 0xF0) == 0xE0)
            extra = 2;
        else if ((*p & 0xF8) == 0xF0)
            extra = 3;
        else
            return false; // stray continuation or invalid lead byte
        ++p;
        for (int i = 0; i < extra; ++i) {
            if ((*p & 0xC0) != 0x80)
                return false; // truncated or malformed sequence
            ++p;
        }
    }
    return true;
}

// VDOC-239: the fixed-size theme/type/id fields (32 bytes) must truncate on a
// UTF-8 character boundary, never mid-sequence, so the stored value is always
// valid UTF-8. Here the 3-byte euro sign straddles the 31-byte cutoff.
TEST(LevelData, MetadataTruncatesOnUtf8Boundary) {
    // 30 ASCII bytes, then "€" (E2 82 AC). The euro's lead byte lands at index 30,
    // so a naive 31-byte copy would keep a lone E2 and drop the continuation bytes.
    std::string theme(30, 'a');
    theme += "\xE2\x82\xAC"; // U+20AC EURO SIGN
    std::string json = "{\"width\":1,\"height\":1,\"tileWidth\":8,\"tileHeight\":8,"
                       "\"properties\":{\"theme\":\"" +
                       theme + "\"}}";

    auto path = temp_path("utf8.json");
    write_file(path, json.c_str());

    void *level = rt_leveldata_load(rt_const_cstr(path.c_str()));
    ASSERT_TRUE(level != nullptr);

    const char *stored = rt_string_cstr(rt_leveldata_get_theme(level));
    EXPECT_TRUE(is_valid_utf8(stored));
    // The incomplete euro character was dropped whole, leaving the 30 ASCII bytes.
    EXPECT_EQ(std::string(stored), std::string(30, 'a'));

    release_obj(level);
    std::remove(path.c_str());
}

int main() {
    return viper_test::run_all_tests();
}
