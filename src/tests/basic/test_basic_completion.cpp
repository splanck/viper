//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/basic/test_basic_completion.cpp
// Purpose: Unit tests for the BasicCompletionEngine.
// Key invariants:
//   - Engine returns filtered/ranked CompletionItem results
//   - Keywords, builtins, snippets, and scope symbols are all providers
//   - Dot-trigger invokes member completion from OopIndex or runtime
// Ownership/Lifetime:
//   - Test-only file
// Links: frontends/basic/BasicCompletion.hpp
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/BasicCompletion.hpp"
#include "tests/TestHarness.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

using namespace il::frontends::basic;

// ===== Keyword completions =====

TEST(BasicCompletion, KeywordPrefixMatch) {
    BasicCompletionEngine engine;
    // "PRI" should match PRINT and PRIVATE
    auto items = engine.complete("PRI\n", 1, 4, "test.bas");
    bool foundPrint = false;
    for (const auto &item : items) {
        if (item.label == "PRINT")
            foundPrint = true;
    }
    EXPECT_TRUE(foundPrint);
}

TEST(BasicCompletion, KeywordFullList) {
    BasicCompletionEngine engine;
    // Empty prefix at start of line should return many completions
    auto items = engine.complete("\n", 1, 1, "test.bas");
    // Should include common keywords like DIM, IF, FOR, PRINT, etc.
    EXPECT_TRUE(items.size() > 10u);
}

// ===== Builtin function completions =====

TEST(BasicCompletion, BuiltinFunctions) {
    BasicCompletionEngine engine;
    // "LE" prefix should match LEFT$, LEN, etc.
    auto items = engine.complete("LE\n", 1, 3, "test.bas");
    bool foundLen = false;
    for (const auto &item : items) {
        if (item.label == "LEN")
            foundLen = true;
    }
    EXPECT_TRUE(foundLen);
}

// ===== Scope symbol completions =====

TEST(BasicCompletion, ScopeVariables) {
    BasicCompletionEngine engine;
    // BASIC lexer uppercases: "myVariable" → "MYVARIABLE"
    std::string source = "DIM myVariable AS INTEGER\nm\n";
    auto items = engine.complete(source, 2, 2, "test.bas");
    bool foundMyVar = false;
    for (const auto &item : items) {
        if (item.label == "MYVARIABLE")
            foundMyVar = true;
    }
    EXPECT_TRUE(foundMyVar);
}

TEST(BasicCompletion, ScopeProcedures) {
    BasicCompletionEngine engine;
    // BASIC lexer uppercases: "MyProc" → "MYPROC"
    std::string source = "SUB MyProc()\nEND SUB\nM\n";
    auto items = engine.complete(source, 3, 2, "test.bas");
    bool foundProc = false;
    for (const auto &item : items) {
        if (item.label == "MYPROC" || item.label == "MyProc")
            foundProc = true;
    }
    EXPECT_TRUE(foundProc);
}

// ===== No-crash edge cases =====

TEST(BasicCompletion, EmptySource) {
    BasicCompletionEngine engine;
    auto items = engine.complete("", 1, 1, "test.bas");
    // Should not crash; may return keywords
    (void)items;
}

TEST(BasicCompletion, CursorBeyondSource) {
    BasicCompletionEngine engine;
    auto items = engine.complete("PRINT 42\n", 100, 100, "test.bas");
    // Should not crash
    (void)items;
}

TEST(BasicCompletion, ClearCache) {
    BasicCompletionEngine engine;
    // Should not crash
    engine.clearCache();
    auto items = engine.complete("DIM x AS INTEGER\n\n", 2, 1, "test.bas");
    (void)items;
}

TEST(BasicCompletion, CacheKeyIncludesFilePath) {
    namespace fs = std::filesystem;

    const fs::path tempRoot = fs::temp_directory_path() / "basic_completion_cache_paths";
    fs::remove_all(tempRoot);

    const fs::path dirA = tempRoot / "a";
    const fs::path dirB = tempRoot / "b";
    fs::create_directories(dirA);
    fs::create_directories(dirB);

    {
        std::ofstream(dirA / "inc.bas") << "DIM Apple AS INTEGER\n";
        std::ofstream(dirB / "inc.bas") << "DIM Apricot AS INTEGER\n";
    }

    const std::string source = "ADDFILE \"inc.bas\"\nA\n";
    BasicCompletionEngine engine;
    auto itemsA = engine.complete(source, 2, 2, (dirA / "main.bas").string(), 0);
    auto itemsB = engine.complete(source, 2, 2, (dirB / "main.bas").string(), 0);

    bool foundAppleA = false;
    bool foundApricotA = false;
    for (const auto &item : itemsA) {
        if (item.label == "APPLE")
            foundAppleA = true;
        if (item.label == "APRICOT")
            foundApricotA = true;
    }

    bool foundApricotB = false;
    for (const auto &item : itemsB) {
        if (item.label == "APRICOT")
            foundApricotB = true;
    }

    EXPECT_TRUE(foundAppleA);
    EXPECT_FALSE(foundApricotA);
    EXPECT_TRUE(foundApricotB);

    fs::remove_all(tempRoot);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
