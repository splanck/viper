//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// ProjectIndex runtime bridge tests for semantic IDE navigation.
//
//===----------------------------------------------------------------------===//

#include "runtime/collections/rt_map.h"
#include "runtime/collections/rt_seq.h"
#include "runtime/core/rt_string.h"
#include "runtime/graphics/common/rt_zia_completion.h"
#include "runtime/oop/rt_object.h"
#include "tests/TestHarness.hpp"

#include "tests/common/PosixCompat.h"
#include <filesystem>
#include <string>
#include <string_view>

namespace {

namespace fs = std::filesystem;

rt_string str(std::string_view text) {
    return rt_string_from_bytes(text.data(), text.size());
}

std::string toString(rt_string value) {
    const char *data = value ? rt_string_cstr(value) : "";
    const size_t len = value ? static_cast<size_t>(rt_str_len(value)) : 0;
    return std::string(data ? data : "", len);
}

void releaseObj(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

std::string mapStr(void *map, std::string_view keyText) {
    rt_string key = str(keyText);
    rt_string value = rt_map_get_str(map, key);
    std::string result = toString(value);
    rt_string_unref(value);
    rt_string_unref(key);
    return result;
}

int64_t mapInt(void *map, std::string_view keyText) {
    rt_string key = str(keyText);
    int64_t result = rt_map_get_int(map, key);
    rt_string_unref(key);
    return result;
}

bool mapBool(void *map, std::string_view keyText) {
    rt_string key = str(keyText);
    bool result = rt_map_get_bool(map, key) != 0;
    rt_string_unref(key);
    return result;
}

int64_t columnOf(const std::string &source,
                 int64_t line,
                 std::string_view needle,
                 int64_t occurrence = 0) {
    size_t lineStart = 0;
    for (int64_t current = 1; current < line; ++current) {
        lineStart = source.find('\n', lineStart);
        if (lineStart == std::string::npos)
            return -1;
        ++lineStart;
    }
    size_t lineEnd = source.find('\n', lineStart);
    if (lineEnd == std::string::npos)
        lineEnd = source.size();
    std::string_view text(source.data() + lineStart, lineEnd - lineStart);

    size_t searchFrom = 0;
    for (int64_t i = 0; i <= occurrence; ++i) {
        size_t found = text.find(needle, searchFrom);
        if (found == std::string_view::npos)
            return -1;
        if (i == occurrence)
            return static_cast<int64_t>(found);
        searchFrom = found + needle.size();
    }
    return -1;
}

struct IndexFixture {
    fs::path root;
    void *handle{nullptr};

    explicit IndexFixture(std::string name) {
        root = fs::temp_directory_path() / "zia_project_index_tests" /
               std::to_string(static_cast<unsigned long long>(::getpid())) / std::move(name);
        fs::create_directories(root);
        rt_string rootStr = str(root.string());
        handle = rt_zia_project_index_new(rootStr);
        rt_string_unref(rootStr);
        ASSERT_TRUE(handle != nullptr);
        ASSERT_TRUE(rt_zia_project_index_is_valid(handle) != 0);
    }

    ~IndexFixture() {
        if (handle) {
            rt_zia_project_index_destroy(handle);
            releaseObj(handle);
        }
    }

    fs::path path(std::string_view name) const {
        return root / std::string(name);
    }

    void update(const fs::path &file, const std::string &source) {
        rt_string fileStr = str(file.string());
        rt_string sourceStr = str(source);
        EXPECT_TRUE(rt_zia_project_index_update_file(handle, fileStr, sourceStr) != 0);
        rt_string_unref(sourceStr);
        rt_string_unref(fileStr);
    }

    void *definition(const fs::path &file, const std::string &source, int64_t line, int64_t col) {
        rt_string fileStr = str(file.string());
        rt_string sourceStr = str(source);
        void *result = rt_zia_project_index_definition(handle, fileStr, sourceStr, line, col);
        rt_string_unref(sourceStr);
        rt_string_unref(fileStr);
        return result;
    }

    void *references(const fs::path &file, const std::string &source, int64_t line, int64_t col) {
        rt_string fileStr = str(file.string());
        rt_string sourceStr = str(source);
        void *result = rt_zia_project_index_references(handle, fileStr, sourceStr, line, col);
        rt_string_unref(sourceStr);
        rt_string_unref(fileStr);
        return result;
    }

    void *rename(const fs::path &file,
                 const std::string &source,
                 int64_t line,
                 int64_t col,
                 std::string_view newName) {
        rt_string fileStr = str(file.string());
        rt_string sourceStr = str(source);
        rt_string nameStr = str(newName);
        void *result =
            rt_zia_project_index_rename_edits(handle, fileStr, sourceStr, line, col, nameStr);
        rt_string_unref(nameStr);
        rt_string_unref(sourceStr);
        rt_string_unref(fileStr);
        return result;
    }
};

TEST(ZiaProjectIndex, TwoFileDefinitionUsesDirtyImportSource) {
    IndexFixture fx("two_file_definition");
    const fs::path libPath = fx.path("lib.zia");
    const fs::path mainPath = fx.path("main.zia");

    const std::string libSource = "module Lib;\n"
                                  "expose func greet() -> Integer {\n"
                                  "    return 7;\n"
                                  "}\n";
    const std::string mainSource = "module Main;\n"
                                   "bind \"lib.zia\";\n"
                                   "func start() -> Integer {\n"
                                   "    return greet();\n"
                                   "}\n";

    fx.update(libPath, libSource);
    fx.update(mainPath, mainSource);

    void *def = fx.definition(mainPath, mainSource, 4, columnOf(mainSource, 4, "greet"));
    ASSERT_TRUE(def != nullptr);
    EXPECT_TRUE(mapBool(def, "found"));
    EXPECT_EQ(mapStr(def, "file"), libPath.lexically_normal().string());
    EXPECT_EQ(mapStr(def, "name"), "greet");
    EXPECT_EQ(mapInt(def, "line"), 2);
    releaseObj(def);
}

TEST(ZiaProjectIndex, ReferencesRespectShadowedLocalsAndIgnoreTrivia) {
    IndexFixture fx("shadowed_references");
    const fs::path mainPath = fx.path("main.zia");
    const std::string source = "module Main;\n"
                               "func start() {\n"
                               "    var value = 1;\n"
                               "    var outerUse = value;\n"
                               "    var text = \"value\"; // value in trivia\n"
                               "}\n"
                               "func other() {\n"
                               "    var value = 2;\n"
                               "    var innerUse = value;\n"
                               "}\n";

    fx.update(mainPath, source);

    void *refs = fx.references(mainPath, source, 4, columnOf(source, 4, "value"));
    ASSERT_TRUE(refs != nullptr);
    ASSERT_EQ(rt_seq_len(refs), 2);
    bool sawDeclaration = false;
    bool sawOuterUse = false;
    for (int64_t i = 0; i < rt_seq_len(refs); ++i) {
        void *entry = rt_seq_get(refs, i);
        EXPECT_EQ(mapStr(entry, "file"), mainPath.lexically_normal().string());
        const int64_t line = mapInt(entry, "line");
        if (line == 3)
            sawDeclaration = true;
        if (line == 4)
            sawOuterUse = true;
        EXPECT_NE(line, 8);
        EXPECT_NE(line, 9);
    }
    EXPECT_TRUE(sawDeclaration);
    EXPECT_TRUE(sawOuterUse);
    releaseObj(refs);
}

TEST(ZiaProjectIndex, RenameReportsVisibleCollision) {
    IndexFixture fx("rename_collision");
    const fs::path mainPath = fx.path("main.zia");
    const std::string source = "module Main;\n"
                               "func start() {\n"
                               "    var target = 1;\n"
                               "    var taken = 2;\n"
                               "    var useTarget = target;\n"
                               "}\n";

    fx.update(mainPath, source);

    void *rename = fx.rename(mainPath, source, 5, columnOf(source, 5, "target"), "taken");
    ASSERT_TRUE(rename != nullptr);
    EXPECT_FALSE(mapBool(rename, "success"));
    EXPECT_EQ(mapStr(rename, "reason"), "collision");
    releaseObj(rename);
}

TEST(ZiaProjectIndex, DirtyBufferUpdateReplacesIndexedSource) {
    IndexFixture fx("dirty_update");
    const fs::path mainPath = fx.path("main.zia");
    const std::string firstSource = "module Main;\n"
                                    "func start() {\n"
                                    "    var oldName = 1;\n"
                                    "    var useName = oldName;\n"
                                    "}\n";
    const std::string dirtySource = "module Main;\n"
                                    "func start() {\n"
                                    "    var newName = 1;\n"
                                    "    var useName = newName;\n"
                                    "}\n";

    fx.update(mainPath, firstSource);
    fx.update(mainPath, dirtySource);

    void *def = fx.definition(mainPath, dirtySource, 4, columnOf(dirtySource, 4, "newName"));
    ASSERT_TRUE(def != nullptr);
    EXPECT_TRUE(mapBool(def, "found"));
    EXPECT_EQ(mapStr(def, "name"), "newName");
    EXPECT_EQ(mapInt(def, "line"), 3);
    releaseObj(def);
}

TEST(ZiaRuntimeBridge, HoverOnQualifiedRuntimeClassIncludesAuthoredDocumentation) {
    const std::string source = "module Main;\n"
                               "func use(app: Viper.GUI.App) {}\n";
    rt_string sourceStr = str(source);
    rt_string pathStr = str("runtime_class_hover.zia");
    void *hover = rt_zia_hover_info_for_file(sourceStr, pathStr, 2, columnOf(source, 2, "App"));
    rt_string_unref(pathStr);
    rt_string_unref(sourceStr);

    ASSERT_TRUE(hover != nullptr);
    EXPECT_TRUE(mapBool(hover, "available"));
    EXPECT_EQ(mapStr(hover, "type"), "Viper.GUI.App");
    EXPECT_TRUE(mapStr(hover, "documentation").find("Owns a GUI application window") !=
                std::string::npos);
    releaseObj(hover);
}

TEST(ZiaRuntimeBridge, HoverOnAliasedRuntimeClassIncludesAuthoredDocumentation) {
    const std::string source = "module Main;\n"
                               "bind Viper.GUI as GUI;\n"
                               "func use(app: GUI.App) {}\n";
    rt_string sourceStr = str(source);
    rt_string pathStr = str("runtime_class_alias_hover.zia");
    void *hover = rt_zia_hover_info_for_file(sourceStr, pathStr, 3, columnOf(source, 3, "App"));
    rt_string_unref(pathStr);
    rt_string_unref(sourceStr);

    ASSERT_TRUE(hover != nullptr);
    EXPECT_TRUE(mapBool(hover, "available"));
    EXPECT_EQ(mapStr(hover, "type"), "Viper.GUI.App");
    EXPECT_TRUE(mapStr(hover, "documentation").find("Owns a GUI application window") !=
                std::string::npos);
    releaseObj(hover);
}

} // namespace

int main() {
    return viper_test::run_all_tests();
}
