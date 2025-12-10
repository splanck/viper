//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/frontends/pascal/SemanticOptionalTests.cpp
// Purpose: Unit tests for Pascal optional type semantics and flow narrowing.
// Key invariants: Tests optional assignment, coalescing, and narrowing.
// Ownership/Lifetime: Test suite.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_HAS_GTEST
#include <gtest/gtest.h>
#else
#include "../../GTestStub.hpp"
#endif

#include "frontends/pascal/AST.hpp"
#include "frontends/pascal/Lexer.hpp"
#include "frontends/pascal/Parser.hpp"
#include "frontends/pascal/SemanticAnalyzer.hpp"
#include "support/diagnostics.hpp"
#include <memory>
#include <string>

using namespace il::frontends::pascal;
using namespace il::support;

namespace
{

//===----------------------------------------------------------------------===//
// Test Helpers
//===----------------------------------------------------------------------===//

/// @brief Parse and analyze a program.
/// @return True if analysis succeeded without errors.
bool analyzeProgram(const std::string &source, DiagnosticEngine &diag)
{
    Lexer lexer(source, 0, diag);
    Parser parser(lexer, diag);
    auto prog = parser.parseProgram();
    if (!prog || parser.hasError())
        return false;

    SemanticAnalyzer analyzer(diag);
    return analyzer.analyze(*prog);
}

//===----------------------------------------------------------------------===//
// PasType Optional Helpers Tests
//===----------------------------------------------------------------------===//

TEST(PasTypeOptionalTest, IsOptional)
{
    auto intType = PasType::integer();
    EXPECT_FALSE(intType.isOptional());

    auto optInt = PasType::optional(PasType::integer());
    EXPECT_TRUE(optInt.isOptional());
}

TEST(PasTypeOptionalTest, Unwrap)
{
    auto intType = PasType::integer();
    auto unwrapped = intType.unwrap();
    EXPECT_EQ(unwrapped.kind, PasTypeKind::Integer);

    auto optInt = PasType::optional(PasType::integer());
    unwrapped = optInt.unwrap();
    EXPECT_EQ(unwrapped.kind, PasTypeKind::Integer);
}

TEST(PasTypeOptionalTest, MakeOptional)
{
    auto intType = PasType::integer();
    auto optInt = PasType::makeOptional(intType);
    EXPECT_TRUE(optInt.isOptional());
    EXPECT_EQ(optInt.innerType->kind, PasTypeKind::Integer);

    // Making already-optional type should not double-wrap
    auto doubleOpt = PasType::makeOptional(optInt);
    EXPECT_TRUE(doubleOpt.isOptional());
    EXPECT_EQ(doubleOpt.innerType->kind, PasTypeKind::Integer);
}

TEST(PasTypeOptionalTest, IsNilAssignable)
{
    // Optional accepts nil
    EXPECT_TRUE(PasType::optional(PasType::integer()).isNilAssignable());

    // Pointer accepts nil
    EXPECT_TRUE(PasType::pointer(PasType::integer()).isNilAssignable());

    // Non-optional class does NOT accept nil
    EXPECT_FALSE(PasType::classType("TMyClass").isNilAssignable());

    // Non-optional integer does NOT accept nil
    EXPECT_FALSE(PasType::integer().isNilAssignable());
}

TEST(PasTypeOptionalTest, ToString)
{
    auto optInt = PasType::optional(PasType::integer());
    EXPECT_EQ(optInt.toString(), "Integer?");

    auto optStr = PasType::optional(PasType::string());
    EXPECT_EQ(optStr.toString(), "String?");
}

//===----------------------------------------------------------------------===//
// Basic Optional Type Tests
//===----------------------------------------------------------------------===//

TEST(PascalOptionalTest, OptionalVariableAssignNil)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var username: String?;\n"
                                 "begin\n"
                                 "  username := nil\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalOptionalTest, OptionalVariableAssignValue)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var username: String?;\n"
                                 "begin\n"
                                 "  username := 'Alice'\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalOptionalTest, OptionalVariableAssignBoth)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var username: String?;\n"
                                 "begin\n"
                                 "  username := nil;\n"
                                 "  username := 'Alice'\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalOptionalTest, OptionalIntegerAssignment)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var count: Integer?;\n"
                                 "begin\n"
                                 "  count := nil;\n"
                                 "  count := 42\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Nil Coalescing Operator Tests
//===----------------------------------------------------------------------===//

TEST(PascalOptionalTest, CoalesceWithDefault)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var username: String?;\n"
                                 "var display: String;\n"
                                 "begin\n"
                                 "  username := nil;\n"
                                 "  display := username ?? 'Guest'\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalOptionalTest, CoalesceChaining)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var first, second: String?;\n"
                                 "var result: String;\n"
                                 "begin\n"
                                 "  first := nil;\n"
                                 "  second := nil;\n"
                                 "  result := first ?? second ?? 'default'\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalOptionalTest, CoalesceWithNonOptional)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var a, b: String;\n"
                                 "var c: String;\n"
                                 "begin\n"
                                 "  a := 'hello';\n"
                                 "  b := 'world';\n"
                                 "  c := a ?? b\n"
                                 "end.",
                                 diag);
    // This should work - coalesce on non-optionals is valid (no-op)
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalOptionalTest, CoalesceIntegerTypes)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var count: Integer?;\n"
                                 "var result: Integer;\n"
                                 "begin\n"
                                 "  count := nil;\n"
                                 "  result := count ?? 0\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Flow Narrowing Tests
//===----------------------------------------------------------------------===//

TEST(PascalOptionalTest, NarrowInIfNotNil)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var name: String?;\n"
                                 "begin\n"
                                 "  name := 'test';\n"
                                 "  if name <> nil then\n"
                                 "    WriteLn(name)\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalOptionalTest, NarrowInElseBranch)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var name: String?;\n"
                                 "begin\n"
                                 "  name := nil;\n"
                                 "  if name = nil then\n"
                                 "    name := 'default'\n"
                                 "  else\n"
                                 "    WriteLn(name)\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalOptionalTest, NarrowInWhileLoop)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var name: String?;\n"
                                 "begin\n"
                                 "  name := 'test';\n"
                                 "  while name <> nil do begin\n"
                                 "    WriteLn(name);\n"
                                 "    name := nil\n"
                                 "  end\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalOptionalTest, NarrowInvalidatedByAssignment)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var name: String?;\n"
                                 "var other: String?;\n"
                                 "begin\n"
                                 "  name := 'test';\n"
                                 "  other := nil;\n"
                                 "  if name <> nil then begin\n"
                                 "    WriteLn(name);\n"
                                 "    name := other\n"
                                 "  end\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// T? Does Not Convert to T Tests
//===----------------------------------------------------------------------===//

TEST(PascalOptionalTest, OptionalDoesNotConvertToNonOptional)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var opt: String?;\n"
                                 "var nonOpt: String;\n"
                                 "begin\n"
                                 "  opt := 'test';\n"
                                 "  nonOpt := opt\n" // Error: cannot assign String? to String
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalOptionalTest, NonOptionalConvertsToOptional)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var opt: String?;\n"
                                 "var nonOpt: String;\n"
                                 "begin\n"
                                 "  nonOpt := 'test';\n"
                                 "  opt := nonOpt\n" // OK: String converts to String?
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Double Optional Error Tests
//===----------------------------------------------------------------------===//

TEST(PascalOptionalTest, DoubleOptionalTypeError)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type Bad = Integer??;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalOptionalTest, NestedOptionalVarError)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var x: String??;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Non-Nullable Reference Type Tests
//===----------------------------------------------------------------------===//

TEST(PascalOptionalTest, ClassTypeNilAssignmentError)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type TCircle = class\n"
                                 "  public\n"
                                 "    radius: Real;\n"
                                 "end;\n"
                                 "var c: TCircle;\n"
                                 "begin\n"
                                 "  c := nil\n" // Error: cannot assign nil to non-optional class
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalOptionalTest, ClassTypeNilComparisonError)
{
    DiagnosticEngine diag;
    bool result =
        analyzeProgram("program Test;\n"
                       "type TCircle = class\n"
                       "  public\n"
                       "    radius: Real;\n"
                       "end;\n"
                       "var c: TCircle;\n"
                       "begin\n"
                       "  if c = nil then\n" // Error: non-optional class cannot be compared to nil
                       "    WriteLn('nil')\n"
                       "end.",
                       diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalOptionalTest, OptionalClassNilAssignment)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type TCircle = class\n"
                                 "  public\n"
                                 "    radius: Real;\n"
                                 "end;\n"
                                 "var c: TCircle?;\n"
                                 "begin\n"
                                 "  c := nil\n" // OK: TCircle? accepts nil
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalOptionalTest, OptionalClassNilComparison)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type TCircle = class\n"
                                 "  public\n"
                                 "    radius: Real;\n"
                                 "end;\n"
                                 "var c: TCircle?;\n"
                                 "begin\n"
                                 "  c := nil;\n"
                                 "  if c = nil then\n" // OK: TCircle? can be compared to nil
                                 "    WriteLn('nil')\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Integer Promotion to Real? Tests
//===----------------------------------------------------------------------===//

TEST(PascalOptionalTest, IntegerPromotesToOptionalReal)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var x: Real?;\n"
                                 "begin\n"
                                 "  x := 42\n" // OK: Integer promotes to Real?
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

} // namespace

#ifndef VIPER_HAS_GTEST
int main()
{
    return RUN_ALL_TESTS();
}
#endif
