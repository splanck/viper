//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for the ViperLang frontend.
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
TEST(ViperLangCompilerTest, EmptyStartFunction)
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

    // Print diagnostics for debugging
    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for EmptyStartFunction:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    // Should succeed (no errors)
    EXPECT_TRUE(result.succeeded());

    // Module should have @main function
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
TEST(ViperLangCompilerTest, ProducesEntryBlock)
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

    // Check that the main function has at least one basic block
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
TEST(ViperLangCompilerTest, HelloWorld)
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

    // Should have main function with Viper.Terminal.Say call
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
TEST(ViperLangCompilerTest, VariableDeclaration)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var x = 42;
    Viper.Terminal.SayInt(x);
}
)";
    CompilerInput input{.source = source, .path = "var.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    // Print diagnostics for debugging
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

    // Should have main function
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

/// @brief Test that if statements compile correctly.
TEST(ViperLangCompilerTest, IfStatement)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    if (true) {
        Viper.Terminal.Say("yes");
    } else {
        Viper.Terminal.Say("no");
    }
}
)";
    CompilerInput input{.source = source, .path = "if.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    // Check for conditional branch
    bool foundCBr = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &block : fn.blocks)
            {
                for (const auto &instr : block.instructions)
                {
                    if (instr.op == il::core::Opcode::CBr)
                    {
                        foundCBr = true;
                        break;
                    }
                }
            }
        }
    }
    EXPECT_TRUE(foundCBr);
}

/// @brief Test that while loops compile correctly.
TEST(ViperLangCompilerTest, WhileLoop)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var i = 0;
    while (i < 10) {
        i = i + 1;
    }
}
)";
    CompilerInput input{.source = source, .path = "while.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    // Check for comparison instruction (SCmpLT)
    bool foundCmp = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &block : fn.blocks)
            {
                for (const auto &instr : block.instructions)
                {
                    if (instr.op == il::core::Opcode::SCmpLT)
                    {
                        foundCmp = true;
                        break;
                    }
                }
            }
        }
    }
    EXPECT_TRUE(foundCmp);
}

/// @brief Test that function calls work.
TEST(ViperLangCompilerTest, FunctionCall)
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

    // Check for both main and greet functions
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

/// @brief Test arithmetic expressions.
TEST(ViperLangCompilerTest, Arithmetic)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var x = 1 + 2 * 3;
    Viper.Terminal.SayInt(x);
}
)";
    CompilerInput input{.source = source, .path = "arith.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    // Check for Mul and Add instructions
    bool foundMul = false;
    bool foundAdd = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &block : fn.blocks)
            {
                for (const auto &instr : block.instructions)
                {
                    if (instr.op == il::core::Opcode::Mul)
                        foundMul = true;
                    if (instr.op == il::core::Opcode::Add)
                        foundAdd = true;
                }
            }
        }
    }
    EXPECT_TRUE(foundMul);
    EXPECT_TRUE(foundAdd);
}

/// @brief Test that value types parse correctly.
TEST(ViperLangCompilerTest, ValueTypeDeclaration)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

value Point {
    Integer x;
    Integer y;
}

func start() {
}
)";
    CompilerInput input{.source = source, .path = "value.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    // Print diagnostics for debugging
    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for ValueTypeDeclaration:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
