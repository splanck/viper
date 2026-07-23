//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
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
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

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

void *mapValue(void *map, std::string_view keyText) {
    rt_string key = str(keyText);
    void *result = rt_map_get(map, key);
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

TEST(ZiaProjectIndex, RenameEnforcesLexerIdentifierRules) {
    // VDOC-114: the rename validator mirrors the Zia lexer — ASCII-only
    // classification and the 1024-byte identifier cap.
    IndexFixture fx("rename_lexer_rules");
    const fs::path mainPath = fx.path("main.zia");
    const std::string source = "module Main;\n"
                               "func start() {\n"
                               "    var target = 1;\n"
                               "    var useTarget = target;\n"
                               "}\n";
    fx.update(mainPath, source);

    // Non-ASCII bytes are rejected regardless of the host locale.
    void *r1 = fx.rename(mainPath, source, 4, columnOf(source, 4, "target"), "caf\xC3\xA9");
    ASSERT_TRUE(r1 != nullptr);
    EXPECT_FALSE(mapBool(r1, "success"));
    EXPECT_EQ(mapStr(r1, "reason"), "invalid_name");
    releaseObj(r1);

    // A 1025-byte identifier exceeds the lexer's cap.
    std::string longName(1025, 'a');
    void *r2 = fx.rename(mainPath, source, 4, columnOf(source, 4, "target"), longName);
    ASSERT_TRUE(r2 != nullptr);
    EXPECT_FALSE(mapBool(r2, "success"));
    EXPECT_EQ(mapStr(r2, "reason"), "invalid_name");
    releaseObj(r2);

    // A maximal valid identifier is accepted.
    std::string okName(1024, 'a');
    void *r3 = fx.rename(mainPath, source, 4, columnOf(source, 4, "target"), okName);
    ASSERT_TRUE(r3 != nullptr);
    EXPECT_TRUE(mapBool(r3, "success"));
    releaseObj(r3);
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

TEST(ZiaProjectIndex, RenameEditsCarryExpectedTextAndRejectExcessiveReferences) {
    IndexFixture fx("bounded_rename");
    const fs::path mainPath = fx.path("main.zia");
    const std::string smallSource = "module Main;\n"
                                    "func start() {\n"
                                    "    var target = 1;\n"
                                    "    var useTarget = target;\n"
                                    "}\n";
    fx.update(mainPath, smallSource);

    void *smallRename =
        fx.rename(mainPath, smallSource, 4, columnOf(smallSource, 4, "target"), "renamed");
    ASSERT_TRUE(smallRename != nullptr);
    ASSERT_TRUE(mapBool(smallRename, "success"));
    void *smallEdits = mapValue(smallRename, "edits");
    ASSERT_TRUE(smallEdits != nullptr);
    ASSERT_EQ(rt_seq_len(smallEdits), 2);
    for (int64_t i = 0; i < rt_seq_len(smallEdits); ++i)
        EXPECT_EQ(mapStr(rt_seq_get(smallEdits, i), "expectedText"), "target");
    releaseObj(smallRename);

    std::string largeSource = "module Main;\nfunc start() {\n    var target = 1;\n";
    for (int i = 0; i < 2000; ++i) {
        largeSource += "    var use" + std::to_string(i) + " = target;\n";
    }
    largeSource += "}\n";
    fx.update(mainPath, largeSource);

    void *refs = fx.references(mainPath, largeSource, 3, columnOf(largeSource, 3, "target"));
    ASSERT_TRUE(refs != nullptr);
    EXPECT_EQ(rt_seq_len(refs), 2001);
    releaseObj(refs);

    void *largeRename =
        fx.rename(mainPath, largeSource, 3, columnOf(largeSource, 3, "target"), "renamed");
    ASSERT_TRUE(largeRename != nullptr);
    EXPECT_FALSE(mapBool(largeRename, "success"));
    EXPECT_EQ(mapStr(largeRename, "reason"), "reference_limit");
    releaseObj(largeRename);
}

TEST(ZiaProjectIndex, QueriesUseThreadSafeSnapshotsDuringMutationAndDestroy) {
    IndexFixture fx("concurrent_snapshot");
    const fs::path libPath = fx.path("lib.zia");
    const fs::path mainPath = fx.path("main.zia");
    const std::string firstLib = "module Lib;\n"
                                 "expose func value() -> Integer { return 1; }\n";
    const std::string secondLib = "module Lib;\n"
                                  "expose func value() -> Integer { return 2; }\n";
    const std::string mainSource = "module Main;\n"
                                   "bind \"lib.zia\";\n"
                                   "func start() -> Integer { return value(); }\n";
    fx.update(libPath, firstLib);
    fx.update(mainPath, mainSource);

    std::atomic<bool> stop{false};
    std::atomic<bool> failed{false};
    std::atomic<int> completedQueries{0};
    std::mutex progressMutex;
    std::condition_variable progress;
    std::thread reader([&] {
        while (!stop.load(std::memory_order_acquire)) {
            void *definition =
                fx.definition(mainPath, mainSource, 3, columnOf(mainSource, 3, "value"));
            if (!definition) {
                failed.store(true, std::memory_order_release);
                progress.notify_one();
                break;
            }
            releaseObj(definition);
            completedQueries.fetch_add(1, std::memory_order_relaxed);
            progress.notify_one();
        }
    });

    {
        std::unique_lock lock(progressMutex);
        progress.wait(lock, [&] {
            return completedQueries.load(std::memory_order_relaxed) > 0 ||
                   failed.load(std::memory_order_acquire);
        });
    }
    for (int i = 0; i < 100; ++i) {
        fx.update(libPath, (i % 2 == 0) ? firstLib : secondLib);
        if (i % 7 == 0) {
            rt_string libPathStr = str(libPath.string());
            EXPECT_TRUE(rt_zia_project_index_remove_file(fx.handle, libPathStr) != 0);
            rt_string_unref(libPathStr);
            fx.update(libPath, firstLib);
        }
    }
    rt_zia_project_index_destroy(fx.handle);
    stop.store(true, std::memory_order_release);
    reader.join();

    EXPECT_FALSE(failed.load(std::memory_order_acquire));
    EXPECT_TRUE(completedQueries.load(std::memory_order_relaxed) > 0);
    EXPECT_FALSE(rt_zia_project_index_is_valid(fx.handle) != 0);
}

TEST(ZiaRuntimeBridge, HoverOnQualifiedRuntimeClassIncludesAuthoredDocumentation) {
    const std::string source = "module Main;\n"
                               "func use(app: Zanna.GUI.App) {}\n";
    rt_string sourceStr = str(source);
    rt_string pathStr = str("runtime_class_hover.zia");
    void *hover = rt_zia_hover_info_for_file(sourceStr, pathStr, 2, columnOf(source, 2, "App"));
    rt_string_unref(pathStr);
    rt_string_unref(sourceStr);

    ASSERT_TRUE(hover != nullptr);
    EXPECT_TRUE(mapBool(hover, "available"));
    EXPECT_EQ(mapStr(hover, "type"), "Zanna.GUI.App");
    EXPECT_TRUE(mapStr(hover, "documentation").find("Owns a GUI application window") !=
                std::string::npos);
    releaseObj(hover);
}

TEST(ZiaRuntimeBridge, HoverOnAliasedRuntimeClassIncludesAuthoredDocumentation) {
    const std::string source = "module Main;\n"
                               "bind Zanna.GUI as GUI;\n"
                               "func use(app: GUI.App) {}\n";
    rt_string sourceStr = str(source);
    rt_string pathStr = str("runtime_class_alias_hover.zia");
    void *hover = rt_zia_hover_info_for_file(sourceStr, pathStr, 3, columnOf(source, 3, "App"));
    rt_string_unref(pathStr);
    rt_string_unref(sourceStr);

    ASSERT_TRUE(hover != nullptr);
    EXPECT_TRUE(mapBool(hover, "available"));
    EXPECT_EQ(mapStr(hover, "type"), "Zanna.GUI.App");
    EXPECT_TRUE(mapStr(hover, "documentation").find("Owns a GUI application window") !=
                std::string::npos);
    releaseObj(hover);
}

} // namespace

int main() {
    return zanna_test::run_all_tests();
}
