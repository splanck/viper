//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for the Zia compiler warning infrastructure (W001-W016).
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "frontends/zia/Warnings.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
#include <algorithm>
#include <string>

using namespace il::frontends::zia;
using namespace il::support;

namespace
{

/// @brief Helper: compile with a specific warning policy.
CompilerResult compileWithPolicy(const std::string &source, WarningPolicy policy = {})
{
    SourceManager sm;
    CompilerInput input{.source = source, .path = "test.zia"};
    CompilerOptions opts{};
    opts.warningPolicy = policy;
    return compile(input, opts, sm);
}

/// @brief Check if any diagnostic has the given code.
bool hasWarningCode(const CompilerResult &r, const char *code)
{
    for (const auto &d : r.diagnostics.diagnostics())
    {
        if (d.code == code)
            return true;
    }
    return false;
}

/// @brief Count diagnostics with the given code.
size_t countWarningCode(const CompilerResult &r, const char *code)
{
    size_t n = 0;
    for (const auto &d : r.diagnostics.diagnostics())
    {
        if (d.code == code)
            n++;
    }
    return n;
}

//=============================================================================
// W001: Unused Variable
//=============================================================================

TEST(ZiaWarnings, W001_UnusedVariable)
{
    auto r = compileWithPolicy(R"(
module T;
func start() {
    var x = 5;
}
)");
    EXPECT_TRUE(r.succeeded()); // Warning doesn't fail compilation
    EXPECT_TRUE(hasWarningCode(r, "W001"));
}

TEST(ZiaWarnings, W001_UsedVariable_NoWarning)
{
    auto r = compileWithPolicy(R"(
module T;
bind IO = Viper.Terminal;
func start() {
    var x = 5;
    IO.Say(x);
}
)");
    EXPECT_TRUE(r.succeeded());
    EXPECT_FALSE(hasWarningCode(r, "W001"));
}

TEST(ZiaWarnings, W001_DiscardVariable_NoWarning)
{
    auto r = compileWithPolicy(R"(
module T;
func start() {
    var _ = 5;
}
)");
    EXPECT_TRUE(r.succeeded());
    EXPECT_FALSE(hasWarningCode(r, "W001"));
}

//=============================================================================
// W002: Unreachable Code
//=============================================================================

TEST(ZiaWarnings, W002_UnreachableAfterReturn)
{
    WarningPolicy policy;
    policy.enableAll = true; // W002 is -Wall only
    auto r = compileWithPolicy(R"(
module T;
func foo(): Integer {
    return 1;
    var x = 2;
}
func start() { }
)",
                               policy);
    EXPECT_TRUE(hasWarningCode(r, "W002"));
}

//=============================================================================
// W003: Implicit Narrowing
//=============================================================================

TEST(ZiaWarnings, W003_ImplicitNarrowing)
{
    WarningPolicy policy;
    policy.enableAll = true; // W003 is -Wall only
    auto r = compileWithPolicy(R"(
module T;
func start() {
    var x: Integer = 3.14;
}
)",
                               policy);
    EXPECT_TRUE(hasWarningCode(r, "W003"));
}

//=============================================================================
// W004: Variable Shadowing
//=============================================================================

TEST(ZiaWarnings, W004_VariableShadowing)
{
    WarningPolicy policy;
    policy.enableAll = true; // W004 is -Wall only
    auto r = compileWithPolicy(R"(
module T;
func start() {
    var x = 1;
    if (true) {
        var x = 2;
    }
}
)",
                               policy);
    EXPECT_TRUE(hasWarningCode(r, "W004"));
}

//=============================================================================
// W005: Float Equality
//=============================================================================

TEST(ZiaWarnings, W005_FloatEquality)
{
    auto r = compileWithPolicy(R"(
module T;
func start() {
    var a = 0.1;
    var b = 0.2;
    var c = (a + b) == 0.3;
}
)");
    EXPECT_TRUE(r.succeeded());
    EXPECT_TRUE(hasWarningCode(r, "W005"));
}

//=============================================================================
// W006: Empty Loop Body
//=============================================================================

TEST(ZiaWarnings, W006_EmptyWhileBody)
{
    WarningPolicy policy;
    policy.enableAll = true; // W006 is -Wall only
    auto r = compileWithPolicy(R"(
module T;
func start() {
    while (false) { }
}
)",
                               policy);
    EXPECT_TRUE(hasWarningCode(r, "W006"));
}

//=============================================================================
// W007: Assignment in Condition
//=============================================================================

TEST(ZiaWarnings, W007_AssignmentInCondition)
{
    WarningPolicy policy;
    policy.enableAll = true; // W007 is -Wall only
    // Note: this will also trigger a type error since assignment returns
    // the assigned type (Integer), not Boolean. We just check the warning exists.
    auto r = compileWithPolicy(R"(
module T;
func start() {
    var x = 0;
    if (x = 1) { }
}
)",
                               policy);
    EXPECT_TRUE(hasWarningCode(r, "W007"));
}

//=============================================================================
// W008: Missing Return
//=============================================================================

TEST(ZiaWarnings, W008_MissingReturn)
{
    auto r = compileWithPolicy(R"(
module T;
func foo(): Integer {
    var x = 5;
}
func start() { }
)");
    EXPECT_TRUE(hasWarningCode(r, "W008"));
}

TEST(ZiaWarnings, W008_HasReturn_NoWarning)
{
    auto r = compileWithPolicy(R"(
module T;
func foo(): Integer {
    return 5;
}
func start() { }
)");
    EXPECT_TRUE(r.succeeded());
    EXPECT_FALSE(hasWarningCode(r, "W008"));
}

//=============================================================================
// W009: Self-Assignment
//=============================================================================

TEST(ZiaWarnings, W009_SelfAssignment)
{
    auto r = compileWithPolicy(R"(
module T;
func start() {
    var x = 5;
    x = x;
}
)");
    EXPECT_TRUE(r.succeeded());
    EXPECT_TRUE(hasWarningCode(r, "W009"));
}

//=============================================================================
// W010: Division By Zero
//=============================================================================

TEST(ZiaWarnings, W010_DivisionByZero)
{
    auto r = compileWithPolicy(R"(
module T;
func start() {
    var x = 10 / 0;
}
)");
    EXPECT_TRUE(r.succeeded());
    EXPECT_TRUE(hasWarningCode(r, "W010"));
}

//=============================================================================
// W011: Redundant Bool Comparison
//=============================================================================

TEST(ZiaWarnings, W011_RedundantBoolComparison)
{
    WarningPolicy policy;
    policy.enableAll = true; // W011 is -Wall only
    auto r = compileWithPolicy(R"(
module T;
func start() {
    var flag = true;
    var b = (flag == true);
}
)",
                               policy);
    EXPECT_TRUE(hasWarningCode(r, "W011"));
}

//=============================================================================
// W013: Empty Body
//=============================================================================

TEST(ZiaWarnings, W013_EmptyIfBody)
{
    WarningPolicy policy;
    policy.enableAll = true; // W013 is -Wall only
    auto r = compileWithPolicy(R"(
module T;
func start() {
    if (true) { }
}
)",
                               policy);
    EXPECT_TRUE(hasWarningCode(r, "W013"));
}

//=============================================================================
// W014: Unused Result
//=============================================================================

TEST(ZiaWarnings, W014_UnusedResult)
{
    WarningPolicy policy;
    policy.enableAll = true; // W014 is -Wall only
    auto r = compileWithPolicy(R"(
module T;
func compute(): Integer {
    return 42;
}
func start() {
    compute();
}
)",
                               policy);
    EXPECT_TRUE(hasWarningCode(r, "W014"));
}

//=============================================================================
// W015: Uninitialized Variable (migrated from V3001)
//=============================================================================

TEST(ZiaWarnings, W015_UninitializedVariable)
{
    auto r = compileWithPolicy(R"(
module T;
bind IO = Viper.Terminal;
func start() {
    var x: Integer;
    IO.Say(x);
}
)");
    EXPECT_TRUE(r.succeeded());
    EXPECT_TRUE(hasWarningCode(r, "W015"));
}

//=============================================================================
// Infrastructure Tests
//=============================================================================

TEST(ZiaWarnings, WerrorMakesWarningAnError)
{
    WarningPolicy policy;
    policy.warningsAsErrors = true;
    auto r = compileWithPolicy(R"(
module T;
func start() {
    var x = 10 / 0;
}
)",
                               policy);
    // Division by zero (W010) is default-enabled and should become an error
    EXPECT_TRUE(hasWarningCode(r, "W010"));
    // With -Werror, the compilation should fail
    EXPECT_FALSE(r.succeeded());
}

TEST(ZiaWarnings, WnoDisablesSpecificWarning)
{
    WarningPolicy policy;
    policy.disabled.insert(WarningCode::W010_DivisionByZero);
    auto r = compileWithPolicy(R"(
module T;
func start() {
    var x = 10 / 0;
}
)",
                               policy);
    EXPECT_TRUE(r.succeeded());
    EXPECT_FALSE(hasWarningCode(r, "W010"));
}

TEST(ZiaWarnings, SuppressPragmaDisablesWarning)
{
    auto r = compileWithPolicy(R"(
module T;
func start() {
    // @suppress(W010)
    var x = 10 / 0;
}
)");
    EXPECT_TRUE(r.succeeded());
    EXPECT_FALSE(hasWarningCode(r, "W010"));
}

TEST(ZiaWarnings, SuppressPragmaByName)
{
    auto r = compileWithPolicy(R"(
module T;
func start() {
    // @suppress(division-by-zero)
    var x = 10 / 0;
}
)");
    EXPECT_TRUE(r.succeeded());
    EXPECT_FALSE(hasWarningCode(r, "W010"));
}

TEST(ZiaWarnings, WallEnablesAllWarnings)
{
    // Without -Wall, W002 (unreachable) is not enabled
    auto r1 = compileWithPolicy(R"(
module T;
func foo(): Integer {
    return 1;
    var x = 2;
}
func start() { }
)");
    EXPECT_FALSE(hasWarningCode(r1, "W002"));

    // With -Wall, W002 is enabled
    WarningPolicy policy;
    policy.enableAll = true;
    auto r2 = compileWithPolicy(R"(
module T;
func foo(): Integer {
    return 1;
    var x = 2;
}
func start() { }
)",
                                policy);
    EXPECT_TRUE(hasWarningCode(r2, "W002"));
}

TEST(ZiaWarnings, DefaultPolicyEnablesConservativeSet)
{
    // W010 (division by zero) should be enabled by default
    auto r = compileWithPolicy(R"(
module T;
func start() {
    var x = 10 / 0;
}
)");
    EXPECT_TRUE(hasWarningCode(r, "W010"));
}

TEST(ZiaWarnings, ParseWarningCode_NumericAndName)
{
    auto c1 = parseWarningCode("W001");
    EXPECT_TRUE(c1.has_value());
    EXPECT_EQ(*c1, WarningCode::W001_UnusedVariable);

    auto c2 = parseWarningCode("unused-variable");
    EXPECT_TRUE(c2.has_value());
    EXPECT_EQ(*c2, WarningCode::W001_UnusedVariable);

    auto c3 = parseWarningCode("bogus");
    EXPECT_FALSE(c3.has_value());
}

// =============================================================================
// W017: XOR Confusion (^ is bitwise XOR, not exponentiation)
// =============================================================================

TEST(ZiaWarnings, W017_XorConfusion)
{
    WarningPolicy policy;
    policy.enableAll = true;
    auto r = compileWithPolicy(R"(
module Test;
func start() {
    var x = 2 ^ 3;
}
)",
                               policy);

    EXPECT_TRUE(r.succeeded());
    EXPECT_TRUE(hasWarningCode(r, "W017"));
}

// =============================================================================
// W018: Bitwise AND Confusion (& is bitwise AND, not concatenation)
// =============================================================================

TEST(ZiaWarnings, W018_BitwiseAndConfusion)
{
    WarningPolicy policy;
    policy.enableAll = true;
    auto r = compileWithPolicy(R"(
module Test;
func start() {
    var x = 5 & 3;
}
)",
                               policy);

    EXPECT_TRUE(r.succeeded());
    EXPECT_TRUE(hasWarningCode(r, "W018"));
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
