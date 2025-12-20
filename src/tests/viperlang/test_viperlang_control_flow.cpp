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

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
