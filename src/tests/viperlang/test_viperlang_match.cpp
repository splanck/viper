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

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
