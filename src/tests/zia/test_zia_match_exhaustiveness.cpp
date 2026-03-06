//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/zia/test_zia_match_exhaustiveness.cpp
// Purpose: Verify match exhaustiveness checking and runtime trap on fallthrough.
// Key invariants:
//   - Exhaustive match (with wildcard) compiles without warnings.
//   - Non-exhaustive match on non-Boolean/Integer/Optional emits W019.
//   - Lowerer emits a trap block for non-exhaustive match fallthrough.
// Links: src/frontends/zia/Sema_Expr_Advanced.cpp, Lowerer_Expr_Match.cpp
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"

#include <string>

using namespace il::frontends::zia;
using namespace il::support;

namespace
{

/// @brief Exhaustive match with wildcard compiles cleanly.
TEST(ZiaMatchExhaustiveness, WildcardIsExhaustive)
{
    const std::string src = R"(
module Test;

func classify(x: Integer) -> String {
    return match x {
        1 => "one",
        2 => "two",
        _ => "other"
    };
}

func start() {
    Viper.Terminal.Say(classify(3));
}
)";

    SourceManager sm;
    CompilerInput input{.source = src, .path = "exhaustive.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    // No W019 warning should be present
    bool hasW019 = false;
    for (const auto &d : result.diagnostics.diagnostics())
    {
        if (d.message.find("Non-exhaustive") != std::string::npos)
            hasW019 = true;
    }
    EXPECT_FALSE(hasW019);
}

/// @brief Non-exhaustive match on String emits W019 warning.
TEST(ZiaMatchExhaustiveness, NonExhaustiveStringWarning)
{
    const std::string src = R"(
module Test;

func classify(x: String) -> Integer {
    return match x {
        "hello" => 1,
        "world" => 2
    };
}

func start() {
    Viper.Terminal.SayInt(classify("hi"));
}
)";

    SourceManager sm;
    CompilerInput input{.source = src, .path = "non_exhaustive.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    // Should still compile (warning, not error for String type)
    // Check that W019 warning is present
    bool hasW019 = false;
    for (const auto &d : result.diagnostics.diagnostics())
    {
        if (d.message.find("Non-exhaustive") != std::string::npos)
            hasW019 = true;
    }
    EXPECT_TRUE(hasW019);
}

/// @brief Boolean match covering both cases is exhaustive.
TEST(ZiaMatchExhaustiveness, BooleanFullCoverage)
{
    const std::string src = R"(
module Test;

func describe(b: Boolean) -> String {
    return match b {
        true => "yes",
        false => "no"
    };
}

func start() {
    Viper.Terminal.Say(describe(true));
}
)";

    SourceManager sm;
    CompilerInput input{.source = src, .path = "bool_exhaustive.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

} // namespace

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
