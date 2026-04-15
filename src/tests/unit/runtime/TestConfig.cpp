//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// Tests for Viper.Game.Config file loading and defaulted lookups.
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

extern "C" {
#include "rt_config.h"
#include "rt_string.h"
}

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static std::string temp_path(const char *name) {
    const char *tmp = std::getenv("TMPDIR");
    if (!tmp || !*tmp)
        tmp = "/tmp";
    return std::string(tmp) + "/viper_rt_config_" + name;
}

static void write_file(const std::string &path, const char *contents) {
    FILE *f = std::fopen(path.c_str(), "wb");
    ASSERT_TRUE(f != nullptr);
    if (contents && *contents)
        std::fwrite(contents, 1, std::strlen(contents), f);
    std::fclose(f);
}

TEST(GameConfig, MissingFileReturnsNull) {
    auto path = temp_path("missing.json");
    std::remove(path.c_str());

    void *cfg = rt_config_load(rt_const_cstr(path.c_str()));
    EXPECT_EQ(cfg, nullptr);
}

TEST(GameConfig, EmptyFileReturnsNull) {
    auto path = temp_path("empty.json");
    write_file(path, "");

    void *cfg = rt_config_load(rt_const_cstr(path.c_str()));
    EXPECT_EQ(cfg, nullptr);

    std::remove(path.c_str());
}

TEST(GameConfig, ValidFileLoadsAndDefaultsMissingKeys) {
    auto path = temp_path("valid.json");
    write_file(path, "{\"debug\":{\"enabled\":true},\"game\":{\"startLevel\":3}}");

    void *cfg = rt_config_load(rt_const_cstr(path.c_str()));
    ASSERT_TRUE(cfg != nullptr);

    EXPECT_EQ(rt_config_get_bool(cfg, rt_const_cstr("debug.enabled"), 0), 1);
    EXPECT_EQ(rt_config_get_int(cfg, rt_const_cstr("game.startLevel"), 1), 3);
    EXPECT_EQ(rt_config_get_int(cfg, rt_const_cstr("missing.value"), 42), 42);

    std::remove(path.c_str());
}

int main() {
    return viper_test::run_all_tests();
}
