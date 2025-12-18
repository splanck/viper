//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/frontends/pascal/ValueTypeOptionalTests.cpp
// Purpose: Unit tests for Pascal value-type optional representation.
// Key invariants: Tests value-type optional (hasValue, value) pair layout.
// Ownership/Lifetime: Test suite.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//

#include "frontends/pascal/AST.hpp"
#include "frontends/pascal/Lexer.hpp"
#include "frontends/pascal/Lowerer.hpp"
#include "frontends/pascal/Parser.hpp"
#include "frontends/pascal/SemanticAnalyzer.hpp"
#include "frontends/pascal/sem/Types.hpp"
#include "support/diagnostics.hpp"
#include "tests/TestHarness.hpp"
#include <memory>
#include <string>

using namespace il::frontends::pascal;
using namespace il::support;

namespace
{

//===----------------------------------------------------------------------===//
// Test Helpers
//===----------------------------------------------------------------------===//

/// @brief Parse, analyze, and lower a program.
/// @return True if all phases succeeded without errors.
bool compileProgram(const std::string &source, DiagnosticEngine &diag, il::core::Module &outModule)
{
    Lexer lexer(source, 0, diag);
    Parser parser(lexer, diag);
    auto prog = parser.parseProgram();
    if (!prog || parser.hasError())
        return false;

    SemanticAnalyzer analyzer(diag);
    if (!analyzer.analyze(*prog))
        return false;

    Lowerer lowerer;
    outModule = lowerer.lower(*prog, analyzer);
    return true;
}

//===----------------------------------------------------------------------===//
// PasType Value-Type Optional Tests
//===----------------------------------------------------------------------===//

TEST(PasTypeValueTypeOptionalTest, IsValueType)
{
    // Integer is a value type
    EXPECT_TRUE(PasType::integer().isValueType());
    // Real is a value type
    EXPECT_TRUE(PasType::real().isValueType());
    // Boolean is a value type
    EXPECT_TRUE(PasType::boolean().isValueType());
    // String is NOT a value type (it's a reference type)
    EXPECT_FALSE(PasType::string().isValueType());
    // Class is NOT a value type
    EXPECT_FALSE(PasType::classType("TMyClass").isValueType());
}

TEST(PasTypeValueTypeOptionalTest, IsValueTypeOptional)
{
    // Integer? is a value-type optional
    auto optInt = PasType::optional(PasType::integer());
    EXPECT_TRUE(optInt.isValueTypeOptional());

    // Real? is a value-type optional
    auto optReal = PasType::optional(PasType::real());
    EXPECT_TRUE(optReal.isValueTypeOptional());

    // Boolean? is a value-type optional
    auto optBool = PasType::optional(PasType::boolean());
    EXPECT_TRUE(optBool.isValueTypeOptional());

    // String? is NOT a value-type optional (reference type)
    auto optStr = PasType::optional(PasType::string());
    EXPECT_FALSE(optStr.isValueTypeOptional());

    // TMyClass? is NOT a value-type optional (reference type)
    auto optClass = PasType::optional(PasType::classType("TMyClass"));
    EXPECT_FALSE(optClass.isValueTypeOptional());

    // Non-optional integer is NOT a value-type optional
    EXPECT_FALSE(PasType::integer().isValueTypeOptional());
}

//===----------------------------------------------------------------------===//
// Value-Type Optional Lowering Tests
//===----------------------------------------------------------------------===//

TEST(ValueTypeOptionalLoweringTest, IntegerOptionalNilAssignment)
{
    // Test: var x: Integer?; x := nil;
    DiagnosticEngine diag;
    il::core::Module module;

    const char *source = R"(
program TestOptional;
var x: Integer?;
begin
    x := nil;
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(ValueTypeOptionalLoweringTest, IntegerOptionalValueAssignment)
{
    // Test: var x: Integer?; x := 42;
    DiagnosticEngine diag;
    il::core::Module module;

    const char *source = R"(
program TestOptional;
var x: Integer?;
begin
    x := 42;
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(ValueTypeOptionalLoweringTest, IntegerOptionalNilComparison)
{
    // Test: if x = nil then ... / if x <> nil then ...
    DiagnosticEngine diag;
    il::core::Module module;

    const char *source = R"(
program TestOptional;
var x: Integer?;
begin
    x := nil;
    if x = nil then
        WriteLn('is nil');
    x := 42;
    if x <> nil then
        WriteLn('not nil');
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(ValueTypeOptionalLoweringTest, IntegerOptionalCoalesce)
{
    // Test: WriteLn(x ?? 0);
    DiagnosticEngine diag;
    il::core::Module module;

    const char *source = R"(
program TestOptional;
var x: Integer?;
begin
    x := nil;
    WriteLn(x ?? 0);
    x := 42;
    WriteLn(x ?? 0);
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(ValueTypeOptionalLoweringTest, RealOptional)
{
    // Test: Real? optional
    DiagnosticEngine diag;
    il::core::Module module;

    const char *source = R"(
program TestOptional;
var r: Real?;
begin
    r := nil;
    if r = nil then
        WriteLn('real is nil');
    r := 3.14;
    WriteLn(r ?? 0.0);
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(ValueTypeOptionalLoweringTest, BooleanOptional)
{
    // Test: Boolean? optional
    DiagnosticEngine diag;
    il::core::Module module;

    const char *source = R"(
program TestOptional;
var b: Boolean?;
begin
    b := nil;
    if b = nil then
        WriteLn('bool is nil');
    b := True;
    if b <> nil then
        WriteLn('bool has value');
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(ValueTypeOptionalLoweringTest, CoalesceChain)
{
    // Test: coalesce chain a ?? b ?? 0
    DiagnosticEngine diag;
    il::core::Module module;

    const char *source = R"(
program TestOptional;
var a, b: Integer?;
begin
    a := nil;
    b := 10;
    WriteLn(a ?? b ?? 0);
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(ValueTypeOptionalLoweringTest, ReferenceTypeOptionalStillWorks)
{
    // Test: String? (reference type) still works as before
    DiagnosticEngine diag;
    il::core::Module module;

    const char *source = R"(
program TestOptional;
var s: String?;
begin
    s := nil;
    if s = nil then
        WriteLn('string is nil');
    s := 'hello';
    WriteLn(s ?? 'default');
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
