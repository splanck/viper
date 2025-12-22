//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for ViperLang control flow (if, while, for).
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

/// @brief Test that if statements compile correctly.
TEST(ViperLangControlFlow, IfStatement)
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
TEST(ViperLangControlFlow, WhileLoop)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer i = 0;
    while (i < 10) {
        i = i + 1;
    }
}
)";
    CompilerInput input{.source = source, .path = "while.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

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

/// @brief Test that for-in loops with ranges work correctly.
TEST(ViperLangControlFlow, ForInLoop)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer sum = 0;
    for (i in 0..5) {
        sum = sum + i;
    }
    Viper.Terminal.SayInt(sum);
}
)";
    CompilerInput input{.source = source, .path = "forin.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for ForInLoop:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    bool foundForInCond = false;
    bool foundAlloca = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &block : fn.blocks)
            {
                if (block.label.find("forin_cond") != std::string::npos)
                    foundForInCond = true;
                for (const auto &instr : block.instructions)
                {
                    if (instr.op == il::core::Opcode::Alloca)
                        foundAlloca = true;
                }
            }
        }
    }
    EXPECT_TRUE(foundForInCond);
    EXPECT_TRUE(foundAlloca);
}

/// @brief Test that for-in loops over lists and maps compile.
TEST(ViperLangControlFlow, ForInCollections)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    List[Integer] numbers = [1, 2, 3];
    Integer sum = 0;
    for (n in numbers) {
        sum = sum + n;
    }

    Map[String, Integer] ages = new Map[String, Integer]();
    ages.set("Alice", 30);
    ages.set("Bob", 25);
    for ((name, age) in ages) {
        sum = sum + age;
    }

    Viper.Terminal.SayInt(sum);
}
)";
    CompilerInput input{.source = source, .path = "forin_collections.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for ForInCollections:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    bool foundListLoop = false;
    bool foundMapLoop = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &block : fn.blocks)
            {
                if (block.label.find("forin_list") != std::string::npos)
                    foundListLoop = true;
                if (block.label.find("forin_map") != std::string::npos)
                    foundMapLoop = true;
            }
        }
    }
    EXPECT_TRUE(foundListLoop);
    EXPECT_TRUE(foundMapLoop);
}

/// @brief Bug #28: Guard statement should work without parentheses.
/// Swift-style guard syntax should be supported in entity methods.
TEST(ViperLangControlFlow, GuardStatementWithoutParens)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Player {
    expose Integer state;

    expose func moveUp() {
        guard state != 0 else { return; }
        state = state + 1;
    }

    expose func moveDown() {
        guard (state != 0) else { return; }
        state = state - 1;
    }
}

func start() {
    Player p = new Player();
    p.state = 1;
    p.moveUp();
    p.moveDown();
}
)";
    CompilerInput input{.source = source, .path = "guard.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for GuardStatementWithoutParens:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded()); // Bug #28: Guard without parens should parse correctly
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
