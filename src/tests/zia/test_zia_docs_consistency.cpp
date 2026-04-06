//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Regression checks for known Zia documentation drift.
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct DocCheck {
    std::string relativePath;
    std::vector<std::string> forbiddenSubstrings;
};

fs::path repoRoot() {
#ifdef VIPER_REPO_ROOT
    return fs::path(VIPER_REPO_ROOT);
#else
    return fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
#endif
}

std::string readFile(const fs::path &path) {
    std::ifstream in(path);
    if (!in) {
        std::cerr << "failed to open " << path << '\n';
        EXPECT_TRUE(false);
        return {};
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void expectMissing(const fs::path &path,
                   const std::string &content,
                   const std::string &needle) {
    if (content.find(needle) != std::string::npos) {
        std::cerr << path << " still contains stale doc pattern: " << needle << '\n';
        EXPECT_TRUE(false);
    }
}

} // namespace

TEST(ZiaDocs, NoKnownStaleSyntaxOrApiPatterns) {
    const fs::path root = repoRoot();

    const std::vector<DocCheck> checks = {
        {"docs/zia-reference.md",
         {
             "Either `get` or `set` may be omitted for read-only or write-only properties.",
             "Reading a write-only property is an error.",
         }},
        {"docs/bible/part2-building-blocks/12-modules.md",
         {
             "export func",
             "export struct",
             "mockDatabase.Contains",
             "email.Contains",
         }},
        {"docs/bible/part2-building-blocks/13-stdlib.md",
         {
             "GetOS(",
             "Viper.Random",
             "createAll(",
             "listFiles(",
             "listDirs(",
             "createNew(",
             "homeDir(",
             "isNumeric(",
             "= [:];",
         }},
        {"docs/bible/appendices/d-runtime-reference.md",
         {
             "function-based, not object-oriented",
             "var map = new Map[String, Integer]();",
             "var set = new Set[String]();",
             "var queue = new Queue[String]();",
             "var stack = new Stack[String]();",
             "var heap = new Heap[String]();",
         }},
        {"docs/bible/appendices/e-error-messages.md",
         {
             "bind Viper.Json",
             "catch e:",
             "catch e {",
             "createNew(",
             "homeDir(",
             "isNumeric(",
             "var data = parse(",
         }},
        {"docs/bible/part5-mastery/27-testing.md",
         {
             "catch e {",
         }},
        {"docs/bible/part3-objects/17-polymorphism.md",
         {
             "bind Viper.Random;",
             "var r = Int(0, 3);",
         }},
        {"docs/viperlib/game/physics.md",
         {
             "Viper.Random.Chance(",
         }},
        {"docs/viperlib/collections/functional.md",
         {
             "Viper.Random.Seed(",
         }},
    };

    for (const auto &check : checks) {
        const fs::path path = root / check.relativePath;
        const std::string content = readFile(path);
        ASSERT_FALSE(content.empty());
        for (const auto &needle : check.forbiddenSubstrings)
            expectMissing(path, content, needle);
    }
}

int main() {
    return viper_test::run_all_tests();
}
