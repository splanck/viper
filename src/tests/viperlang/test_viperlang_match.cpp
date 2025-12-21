//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for ViperLang match expressions and statements.
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

/// @brief Test that match statement works correctly.
TEST(ViperLangMatch, MatchStatement)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer x = 5;
    match (x) {
        1 => { Viper.Terminal.Say("one"); }
        _ => { Viper.Terminal.Say("other"); }
    }
}
)";
    CompilerInput input{.source = source, .path = "match_stmt.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for MatchStatement:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    bool foundMatchBlock = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &block : fn.blocks)
            {
                if (block.label.find("match_arm") != std::string::npos)
                    foundMatchBlock = true;
            }
        }
    }
    EXPECT_TRUE(foundMatchBlock);
}

/// @brief Test that match expression (used as value) compiles.
TEST(ViperLangMatch, MatchExpression)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer x = 2;
    Integer result = match (x) {
        1 => 10,
        2 => 20,
        _ => 0
    };
    Viper.Terminal.SayInt(result);
}
)";
    CompilerInput input{.source = source, .path = "match_expr.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for MatchExpression:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    bool foundMatchBlock = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &block : fn.blocks)
            {
                if (block.label.find("match_arm") != std::string::npos)
                    foundMatchBlock = true;
            }
        }
    }
    EXPECT_TRUE(foundMatchBlock);
}

/// @brief Test that match expression with boolean subject and expression patterns works.
/// This tests the guard-style matching: match (true) { cond => value, ... }
TEST(ViperLangMatch, MatchExpressionWithBooleanSubject)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func clamp(Integer value, Integer minVal, Integer maxVal) -> Integer {
    return match (true) {
        value < minVal => minVal,
        value > maxVal => maxVal,
        _ => value
    };
}

func start() {
    Integer a = clamp(5, 0, 10);
    Integer negative = 0 - 5;
    Integer b = clamp(negative, 0, 10);
    Integer c = clamp(15, 0, 10);
    Viper.Terminal.SayInt(a);
    Viper.Terminal.SayInt(b);
    Viper.Terminal.SayInt(c);
}
)";
    CompilerInput input{.source = source, .path = "match_bool.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for MatchExpressionWithBooleanSubject:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    // Verify clamp function has match blocks
    bool foundClampMatchArm = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "clamp")
        {
            for (const auto &block : fn.blocks)
            {
                if (block.label.find("match_arm") != std::string::npos)
                    foundClampMatchArm = true;
            }
        }
    }
    EXPECT_TRUE(foundClampMatchArm);

    // Verify comparison instruction is generated for expression patterns
    bool foundScmpLt = false;
    bool foundScmpGt = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "clamp")
        {
            for (const auto &block : fn.blocks)
            {
                for (const auto &instr : block.instructions)
                {
                    if (instr.op == il::core::Opcode::SCmpLT)
                        foundScmpLt = true;
                    if (instr.op == il::core::Opcode::SCmpGT)
                        foundScmpGt = true;
                }
            }
        }
    }
    EXPECT_TRUE(foundScmpLt);
    EXPECT_TRUE(foundScmpGt);
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
