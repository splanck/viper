//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Regression checks for known Zia documentation drift.
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace il::frontends::zia;
using namespace il::support;

namespace {

struct DocCheck {
    std::string relativePath;
    std::vector<std::string> forbiddenSubstrings;
};

fs::path repoRoot() {
#ifdef ZANNA_REPO_ROOT
    return fs::path(ZANNA_REPO_ROOT);
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

void expectMissing(const fs::path &path, const std::string &content, const std::string &needle) {
    if (content.find(needle) != std::string::npos) {
        std::cerr << path << " still contains stale doc pattern: " << needle << '\n';
        EXPECT_TRUE(false);
    }
}

CompilerResult compileSnippet(const std::string &source, const std::string &path) {
    SourceManager sm;
    CompilerInput input{.source = source, .path = path};
    return compile(input, {}, sm);
}

void expectCompileSuccess(const std::string &source, const std::string &path) {
    auto result = compileSnippet(source, path);
    if (!result.succeeded()) {
        std::cerr << "doc snippet failed to compile: " << path << '\n';
        for (const auto &diag : result.diagnostics.diagnostics())
            std::cerr << diag.message << '\n';
    }
    EXPECT_TRUE(result.succeeded());
}

} // namespace

TEST(ZiaDocs, NoKnownStaleSyntaxOrApiPatterns) {
    const fs::path root = repoRoot();

    const std::vector<DocCheck> checks = {
        {"docs/languages/zia-reference.md",
         {
             "Either `get` or `set` may be omitted for read-only or write-only properties.",
             "Reading a write-only property is an error.",
             "Tuple destructuring in `var` declarations",
             "The binding is a `String`",
             "runtime faults such as divide-by-zero bind an empty string",
             "tryStmt     ::= \"try\" block [\"catch\"",
             "throwStmt   ::= \"throw\" expr \";\"",
             "member      ::= [\"expose\" | \"hide\"] [\"static\" | \"override\"]",
             "A setter-only property is not currently supported",
             "Zia programs have access to the full Zanna Runtime through",
             "The following keyword is recognized by the lexer but has no current semantics",
             "| `Ptr` | Raw pointer / opaque handle",
             "Function references are stored as `Ptr` type",
             "func handler(arg: Ptr)",
             "function pointer at the correct slot",
             "Zanna.Memory.Release(handle)",
             "```zanna",
         }},
        {"docs/tutorials/zia-tutorial.md",
         {
             "```zanna",
         }},
        {"misc/reviews/feature-parity.md",
         {
             "| Function overloading | None | None | Neither |",
             "Zia parses `weak` but doesn't lower",
             "Zia has the type but no construction/destructure",
             "**No construction or destructuring**",
             "**No lowering or runtime support**",
             "Both produce function pointers",
             "address-of (unary)",
         }},
        {"docs/zannalib/collections/sequential.md",
         {
             "pointer value",
             "pointer equality",
         }},
        {"docs/zannalib/network.md",
         {
             "cache the pointer",
         }},
        {"docs/book/appendices/a-zia-reference.md",
         {
             "../../../zia-reference.md",
         }},
        {"docs/book/part2-building-blocks/12-modules.md",
         {
             "export func",
             "export struct",
             "mockDatabase.Contains",
             "email.Contains",
         }},
        {"docs/book/part2-building-blocks/13-stdlib.md",
         {
             "GetOS(",
             "Zanna.Random",
             "createAll(",
             "listFiles(",
             "listDirs(",
             "createNew(",
             "homeDir(",
             "isNumeric(",
             "= [:];",
         }},
        {"docs/book/appendices/c-runtime-reference.md",
         {
             "function-based, not object-oriented",
             "var map = new Map[String, Integer]();",
             "var set = new Set[String]();",
             "var queue = new Queue[String]();",
             "var stack = new Stack[String]();",
             "var heap = new Heap[String]();",
         }},
        {"docs/book/appendices/d-error-messages.md",
         {
             "bind Zanna.Json",
             "catch e:",
             "catch e {",
             "createNew(",
             "homeDir(",
             "isNumeric(",
             "var data = parse(",
         }},
        {"docs/book/part5-mastery/27-testing.md",
         {
             "catch e {",
         }},
        {"docs/book/part3-objects/17-polymorphism.md",
         {
             "bind Zanna.Random;",
             "var r = Int(0, 3);",
         }},
        {"docs/zannalib/game/physics.md",
         {
             "Zanna.Random.Chance(",
         }},
        {"docs/zannalib/collections/functional.md",
         {
             "Zanna.Random.Seed(",
         }},
        {"docs/zannalib/core.md",
         {
             "advanced Zia/BASIC code",
             "Wrap a native `void (*)(void*)` callback pointer as a managed handler",
         }},
        {"docs/zannalib/threads.md",
         {
             "advanced Zia feature",
             "advanced Zia code",
             "Callback `arg` values are forwarded as raw pointers",
             "Pool requires function pointers (`addr_of`)",
             "Parallel requires function pointers (`addr_of`)",
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

TEST(ZiaDocs, ReferenceAndGettingStartedSnippetsCompile) {
    expectCompileSuccess(R"(
module DocReferenceSnippet;

func cleanup() {
}

func useInt(value: Integer) {
    if value == -1 {
        return;
    }
}

func useString(value: String) {
    if value == "" {
        return;
    }
}

func start() {
    var grid: Integer[4];
    grid[0] = 1;
    grid[1] = 2;
    useInt(grid[0] + grid[1]);

    var ages: Map[String, Integer] = new Map[String, Integer]();
    useInt(ages.count());

    var queue: Queue[String] = new Queue[String]();
    queue.push("ready");
    var item: String = queue.peek();
    useString(item);

    defer cleanup();
}
)",
                         "doc_reference_snippet.zia");

    expectCompileSuccess(R"(
module DocGettingStartedSnippet;

func useInt(value: Integer) {
    if value == -1 {
        return;
    }
}

func useString(value: String) {
    if value == "" {
        return;
    }
}

func start() {
    var scores: Map[String, Integer] = new Map[String, Integer]();
    useInt(scores.count());

    var bytes: Bytes = new Bytes(2);
    bytes.set(0, 65);
    bytes.set(1, 66);
    useString(bytes.toStr());
}
)",
                         "doc_getting_started_snippet.zia");
}

TEST(ZiaDocs, PointerDiagnosticsUseSafeVocabulary) {
    auto result = compileSnippet(R"(
module PointerDiagnosticSnippet;

func takeAny(value: Any) {
}

func start() {
    var p: Ptr = null;
    var x = 1;
    takeAny(&x);
}
)",
                                 "pointer_diagnostic_snippet.zia");

    EXPECT_TRUE(!result.succeeded());
    const std::vector<std::string> banned = {
        "raw pointer",
        "opaque pointer",
        "function pointer",
        "address-of",
    };
    for (const auto &diag : result.diagnostics.diagnostics()) {
        for (const auto &needle : banned) {
            if (diag.message.find(needle) != std::string::npos) {
                std::cerr << "diagnostic contains stale pointer vocabulary: " << diag.message
                          << '\n';
                EXPECT_TRUE(false);
            }
        }
    }
}

int main() {
    return zanna_test::run_all_tests();
}
