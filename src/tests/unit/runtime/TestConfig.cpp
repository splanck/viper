//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
// Tests for Zanna.Game.Config file loading and defaulted lookups.
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

extern "C" {
#include "rt_config.h"
#include "rt_heap.h"
#include "rt_jsonpath.h"
#include "rt_object.h"
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
    return std::string(tmp) + "/zanna_rt_config_" + name;
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
    release_obj(cfg);

    std::remove(path.c_str());
}

TEST(GameConfig, MissingStringReturnsProvidedDefault) {
    void *cfg = rt_config_from_string(rt_const_cstr("{\"player\":{\"name\":\"Ada\"}}"));
    ASSERT_TRUE(cfg != nullptr);

    auto fallback = rt_const_cstr("fallback");
    auto missing =
        (rt_string)rt_config_get_str(cfg, rt_const_cstr("player.title"), (void *)fallback);
    auto present =
        (rt_string)rt_config_get_str(cfg, rt_const_cstr("player.name"), (void *)fallback);

    EXPECT_EQ(std::strcmp(rt_string_cstr(missing), "fallback"), 0);
    EXPECT_EQ(std::strcmp(rt_string_cstr(present), "Ada"), 0);
    release_obj(cfg);
}

// VDOC-236: Config queries used rt_jsonpath_get (which retains the resolved node)
// as an existence check without releasing it, leaking one reference per successful
// query. The fix routes existence checks through the non-retaining rt_jsonpath_has.
// This test holds one owned reference to a resolved node and confirms repeated
// Config queries leave its refcount unchanged.
TEST(GameConfig, QueriesDoNotRetainResolvedNodes) {
    void *cfg = rt_config_from_string(
        rt_const_cstr("{\"player\":{\"name\":\"hero\"},\"level\":5,\"flag\":true}"));
    ASSERT_TRUE(cfg != nullptr);
    void *root = rt_config_json_root(cfg);
    ASSERT_TRUE(root != nullptr);

    // "player" resolves to a heap-backed JSON object; hold one owned reference and
    // snapshot its refcount. (Scalar leaves like "level" are immediate values that
    // retain_maybe never touches, so the leak only manifests on heap-backed nodes.)
    void *player = rt_jsonpath_get(root, rt_const_cstr("player"));
    ASSERT_TRUE(player != nullptr);
    const size_t base = rt_heap_hdr(player)->refcnt;

    // Existence checks and value queries against these paths must not retain the
    // resolved node. With the leak, each existence probe bumped the refcount.
    for (int i = 0; i < 100; ++i) {
        EXPECT_TRUE(rt_config_has(cfg, rt_const_cstr("player")) != 0);
        EXPECT_EQ(rt_config_get_int(cfg, rt_const_cstr("level"), -1), 5);
        EXPECT_TRUE(rt_config_get_bool(cfg, rt_const_cstr("flag"), 0) != 0);
        void *s = rt_config_get_str(cfg, rt_const_cstr("player.name"), nullptr);
        release_obj(s); // the returned string is caller-owned
    }
    EXPECT_EQ(rt_heap_hdr(player)->refcnt, base);

    // Guard against a no-op assertion: rt_jsonpath_get itself DOES retain a
    // heap-backed node, so the refcount must actually move when a real retain
    // happens and settle back on release. This proves the loop above would have
    // caught a retaining regression.
    void *again = rt_jsonpath_get(root, rt_const_cstr("player"));
    EXPECT_EQ(rt_heap_hdr(player)->refcnt, base + 1);
    release_obj(again);
    EXPECT_EQ(rt_heap_hdr(player)->refcnt, base);

    release_obj(player);
    release_obj(cfg);
}

// VDOC-245: the typed getters must return the caller's default when a present
// value cannot be converted to the requested type, not a coerced 0/false/"".
TEST(GameConfig, WrongTypeValueReturnsSuppliedDefault) {
    void *cfg = rt_config_from_string(rt_const_cstr(
        "{\"obj\":{\"k\":1},\"arr\":[1,2],\"text\":\"hello\",\"num\":7,\"numStr\":\"42\"}"));
    ASSERT_TRUE(cfg != nullptr);

    // GetInt: object/array/non-numeric-string values are not int-convertible, so
    // the supplied default is returned instead of 0.
    EXPECT_EQ(rt_config_get_int(cfg, rt_const_cstr("obj"), -1), -1);
    EXPECT_EQ(rt_config_get_int(cfg, rt_const_cstr("arr"), -1), -1);
    EXPECT_EQ(rt_config_get_int(cfg, rt_const_cstr("text"), -1), -1);
    // A genuine number and a numeric string still convert.
    EXPECT_EQ(rt_config_get_int(cfg, rt_const_cstr("num"), -1), 7);
    EXPECT_EQ(rt_config_get_int(cfg, rt_const_cstr("numStr"), -1), 42);
    // An absent path still yields the default.
    EXPECT_EQ(rt_config_get_int(cfg, rt_const_cstr("missing"), -1), -1);

    // GetBool: a non-int-convertible value returns the default (here true), not false.
    EXPECT_EQ(rt_config_get_bool(cfg, rt_const_cstr("obj"), 1), 1);
    EXPECT_EQ(rt_config_get_bool(cfg, rt_const_cstr("num"), 0), 1); // 7 -> truthy

    // GetStr: an object/array value is not string-convertible, so the default is
    // returned rather than an empty string; scalars still coerce to text.
    auto fallback = rt_const_cstr("dflt");
    auto objStr = (rt_string)rt_config_get_str(cfg, rt_const_cstr("obj"), (void *)fallback);
    EXPECT_EQ(std::strcmp(rt_string_cstr(objStr), "dflt"), 0);
    auto textStr = (rt_string)rt_config_get_str(cfg, rt_const_cstr("text"), (void *)fallback);
    EXPECT_EQ(std::strcmp(rt_string_cstr(textStr), "hello"), 0);
    auto numAsStr = (rt_string)rt_config_get_str(cfg, rt_const_cstr("num"), (void *)fallback);
    EXPECT_EQ(std::strcmp(rt_string_cstr(numAsStr), "7"), 0);

    release_obj(cfg);
}

int main() {
    return zanna_test::run_all_tests();
}
