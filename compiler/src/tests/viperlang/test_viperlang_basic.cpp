//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for basic ViperLang compilation.
//
//===----------------------------------------------------------------------===//

#include "frontends/viperlang/Compiler.hpp"
#include "il/core/Opcode.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
#include <string>

using namespace il::frontends::viperlang;
using namespace il::support;

namespace
{

/// @brief Test that an empty start function compiles.
TEST(ViperLangBasic, EmptyStartFunction)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
}
)";
    CompilerInput input{.source = source, .path = "test.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for EmptyStartFunction:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    bool hasMain = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            hasMain = true;
            break;
        }
    }
    EXPECT_TRUE(hasMain);
}

/// @brief Test that the compiler produces an entry block.
TEST(ViperLangBasic, ProducesEntryBlock)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
}
)";
    CompilerInput input{.source = source, .path = "test.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    bool foundMainWithBlocks = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main" && !fn.blocks.empty())
        {
            foundMainWithBlocks = true;
            break;
        }
    }
    EXPECT_TRUE(foundMainWithBlocks);
}

/// @brief Test that Hello World compiles and calls Viper.Terminal.Say.
TEST(ViperLangBasic, HelloWorld)
{
    SourceManager sm;
    const std::string source = R"(
module Hello;

func start() {
    Viper.Terminal.Say("Hello, World!");
}
)";
    CompilerInput input{.source = source, .path = "hello.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    bool hasMain = false;
    bool foundCall = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            hasMain = true;
            for (const auto &block : fn.blocks)
            {
                for (const auto &instr : block.instructions)
                {
                    if (instr.op == il::core::Opcode::Call && instr.callee == "Viper.Terminal.Say")
                    {
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
TEST(ViperLangBasic, VariableDeclaration)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer x = 42;
    Viper.Terminal.SayInt(x);
}
)";
    CompilerInput input{.source = source, .path = "var.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for VariableDeclaration:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    bool hasMain = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            hasMain = true;
            break;
        }
    }
    EXPECT_TRUE(hasMain);
}

/// @brief Test that function calls work.
TEST(ViperLangBasic, FunctionCall)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func greet() {
    Viper.Terminal.Say("Hello");
}

func start() {
    greet();
}
)";
    CompilerInput input{.source = source, .path = "call.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    bool hasMain = false;
    bool hasGreet = false;
    for (const auto &fn : result.module.functions)
    {
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
TEST(ViperLangBasic, TerminalFunctionsRecognized)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Viper.Terminal.Clear();
    Viper.Terminal.SetPosition(1, 1);
    Viper.Terminal.SetColor(1, 0);
    Viper.Terminal.Print("Hello");
    Viper.Terminal.SetCursorVisible(0);
    Viper.Terminal.SetCursorVisible(1);
    String key = Viper.Terminal.GetKeyTimeout(1);
    if (key != "") {
        key = Viper.Terminal.GetKey();
    }
    Viper.Time.SleepMs(100);
    Viper.Terminal.Say("Done");
}
)";
    CompilerInput input{.source = source, .path = "terminal.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for TerminalFunctionsRecognized:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded()); // Bug #22: Terminal functions should be recognized
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
