//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for basic Zia compilation.
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "il/core/Opcode.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
#include <string>

using namespace il::frontends::zia;
using namespace il::support;

namespace {

/// @brief Test that an empty start function compiles.
TEST(ZiaBasic, EmptyStartFunction) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {}
)";
    CompilerInput input{.source = source, .path = "test.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        std::cerr << "Diagnostics for EmptyStartFunction:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    bool hasMain = false;
    for (const auto &fn : result.module.functions) {
        if (fn.name == "main") {
            hasMain = true;
            break;
        }
    }
    EXPECT_TRUE(hasMain);
}

/// @brief Test that the compiler produces an entry block.
TEST(ZiaBasic, ProducesEntryBlock) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {}
)";
    CompilerInput input{.source = source, .path = "test.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    bool foundMainWithBlocks = false;
    for (const auto &fn : result.module.functions) {
        if (fn.name == "main" && !fn.blocks.empty()) {
            foundMainWithBlocks = true;
            break;
        }
    }
    EXPECT_TRUE(foundMainWithBlocks);
}

/// @brief Test that Hello World compiles and calls Zanna.Terminal.Say.
TEST(ZiaBasic, HelloWorld) {
    SourceManager sm;
    const std::string source = R"(
module Hello;

func start() {    Zanna.Terminal.Say("Hello, World!");
}
)";
    CompilerInput input{.source = source, .path = "hello.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    bool hasMain = false;
    bool foundCall = false;
    for (const auto &fn : result.module.functions) {
        if (fn.name == "main") {
            hasMain = true;
            for (const auto &block : fn.blocks) {
                for (const auto &instr : block.instructions) {
                    if (instr.op == il::core::Opcode::Call &&
                        instr.callee == "Zanna.Terminal.Say") {
                        foundCall = true;
                    }
                }
            }
        }
    }
    EXPECT_TRUE(hasMain);
    EXPECT_TRUE(foundCall);
}

/// @brief Test that variables are handled correctly.
TEST(ZiaBasic, VariableDeclaration) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {    var x: Integer = 42;
    Zanna.Terminal.SayInt(x);
}
)";
    CompilerInput input{.source = source, .path = "var.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        std::cerr << "Diagnostics for VariableDeclaration:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    bool hasMain = false;
    for (const auto &fn : result.module.functions) {
        if (fn.name == "main") {
            hasMain = true;
            break;
        }
    }
    EXPECT_TRUE(hasMain);
}

/// @brief Test that function calls work.
TEST(ZiaBasic, FunctionCall) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func greet() {    Zanna.Terminal.Say("Hello");
}

func start() {    greet();
}
)";
    CompilerInput input{.source = source, .path = "call.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    bool hasMain = false;
    bool hasGreet = false;
    for (const auto &fn : result.module.functions) {
        if (fn.name == "main")
            hasMain = true;
        if (fn.name == "greet")
            hasGreet = true;
    }
    EXPECT_TRUE(hasMain);
    EXPECT_TRUE(hasGreet);
}

/// @brief Bug #22: Terminal functions should be recognized.
/// Updated after Bug #31 fix to use correct runtime function names.
TEST(ZiaBasic, TerminalFunctionsRecognized) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {    Zanna.Terminal.Clear();
    Zanna.Terminal.SetPosition(1, 1);
    Zanna.Terminal.SetColor(1, 0);
    Zanna.Terminal.Print("Hello");
    Zanna.Terminal.SetCursorVisible(false);
    Zanna.Terminal.SetCursorVisible(true);
    var key: String = Zanna.Terminal.ReadKeyFor(1);
    if (key != "") {
        key = Zanna.Terminal.ReadKey();
    }
    Zanna.Time.Clock.Sleep(100);
    Zanna.Terminal.Say("Done");
}
)";
    CompilerInput input{.source = source, .path = "terminal.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded()) {
        std::cerr << "Diagnostics for TerminalFunctionsRecognized:\n";
        for (const auto &d : result.diagnostics.diagnostics()) {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded()); // Bug #22: Terminal functions should be recognized
}

} // namespace

int main() {
    return zanna_test::run_all_tests();
}
