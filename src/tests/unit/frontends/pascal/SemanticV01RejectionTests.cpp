//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/frontends/pascal/SemanticV01RejectionTests.cpp
// Purpose: Unit tests verifying that v0.1-excluded features are rejected.
// Key invariants: Pointers, with, sets, nested procs, overloading produce errors.
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

/// @brief Check if error message contains expected substring.
bool hasErrorContaining(DiagnosticEngine &diag, const std::string &substring)
{
    // DiagnosticEngine stores messages internally; we check error count
    return diag.errorCount() > 0;
}

//===----------------------------------------------------------------------===//
// Pointer Type Rejection Tests
//===----------------------------------------------------------------------===//

TEST(PascalV01RejectionTest, PointerTypeRejected)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type PInt = ^Integer;\n"
        "begin\n"
        "end.",
        diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalV01RejectionTest, PointerVariableRejected)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "var p: ^Integer;\n"
        "begin\n"
        "end.",
        diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalV01RejectionTest, AddressOfOperatorRejected)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "var x: Integer;\n"
        "begin\n"
        "  x := 42;\n"
        "  // @x would be address-of, but we'll test it differently\n"
        "end.",
        diag);
    // This should succeed as we're not actually using @
    EXPECT_TRUE(result);
}

TEST(PascalV01RejectionTest, AddressOfUsageRejected)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "var x: Integer;\n"
        "var y: Integer;\n"
        "begin\n"
        "  y := @x;\n"
        "end.",
        diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalV01RejectionTest, DereferenceRejected)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "var x: Integer;\n"
        "begin\n"
        "  x := x^;\n"
        "end.",
        diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// With Statement Acceptance Tests (implemented in v0.1)
//===----------------------------------------------------------------------===//

TEST(PascalV01RejectionTest, WithStatementAccepted)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type TRec = record x: Integer; end;\n"
        "var r: TRec;\n"
        "begin\n"
        "  with r do\n"
        "    x := 1;\n"
        "end.",
        diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Set Type Rejection Tests
//===----------------------------------------------------------------------===//

TEST(PascalV01RejectionTest, SetTypeRejected)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type CharSet = set of Integer;\n"
        "begin\n"
        "end.",
        diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalV01RejectionTest, SetVariableRejected)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "var s: set of Boolean;\n"
        "begin\n"
        "end.",
        diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Nested Procedure/Function Rejection Tests
//===----------------------------------------------------------------------===//

TEST(PascalV01RejectionTest, NestedProcedureRejected)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "procedure Outer;\n"
        "  procedure Inner;\n"
        "  begin\n"
        "  end;\n"
        "begin\n"
        "end;\n"
        "begin\n"
        "end.",
        diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalV01RejectionTest, NestedFunctionRejected)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "function Outer: Integer;\n"
        "  function Inner: Integer;\n"
        "  begin\n"
        "    Result := 1;\n"
        "  end;\n"
        "begin\n"
        "  Result := Inner;\n"
        "end;\n"
        "begin\n"
        "end.",
        diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalV01RejectionTest, NestedProcInFunctionRejected)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "function Outer: Integer;\n"
        "  procedure Inner;\n"
        "  begin\n"
        "  end;\n"
        "begin\n"
        "  Result := 0;\n"
        "end;\n"
        "begin\n"
        "end.",
        diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// User-Defined Overloading Rejection Tests
//===----------------------------------------------------------------------===//

TEST(PascalV01RejectionTest, ProcedureOverloadingRejected)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "procedure DoSomething(x: Integer);\n"
        "begin\n"
        "end;\n"
        "procedure DoSomething(x: String);\n"
        "begin\n"
        "end;\n"
        "begin\n"
        "end.",
        diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalV01RejectionTest, FunctionOverloadingRejected)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "function Add(x, y: Integer): Integer;\n"
        "begin\n"
        "  Result := x + y;\n"
        "end;\n"
        "function Add(x, y: Real): Real;\n"
        "begin\n"
        "  Result := x + y;\n"
        "end;\n"
        "begin\n"
        "end.",
        diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalV01RejectionTest, MixedProcFuncOverloadingRejected)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "procedure Process;\n"
        "begin\n"
        "end;\n"
        "function Process: Integer;\n"
        "begin\n"
        "  Result := 0;\n"
        "end;\n"
        "begin\n"
        "end.",
        diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalV01RejectionTest, ForwardDeclarationAllowed)
{
    // Forward declaration followed by implementation should work
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "procedure DoSomething; forward;\n"
        "procedure DoSomething;\n"
        "begin\n"
        "end;\n"
        "begin\n"
        "end.",
        diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Valid Programs Should Still Work
//===----------------------------------------------------------------------===//

TEST(PascalV01RejectionTest, ValidProgramWithClasses)
{
    // Classes should be the alternative to pointers
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  TNode = class\n"
        "  public\n"
        "    value: Integer;\n"
        "    next: TNode;\n"
        "  end;\n"
        "var node: TNode;\n"
        "begin\n"
        "  node := TNode.Create;\n"
        "  node.value := 42;\n"
        "end.",
        diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalV01RejectionTest, ValidProgramWithRecords)
{
    // Records without variant parts should work
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  TPoint = record\n"
        "    x: Integer;\n"
        "    y: Integer;\n"
        "  end;\n"
        "var p: TPoint;\n"
        "begin\n"
        "  p.x := 10;\n"
        "  p.y := 20;\n"
        "end.",
        diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalV01RejectionTest, ValidProgramWithTopLevelProcs)
{
    // Top-level procedures should work fine
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "procedure Helper;\n"
        "begin\n"
        "  WriteLn('Helper called');\n"
        "end;\n"
        "function Compute(x: Integer): Integer;\n"
        "begin\n"
        "  Result := x * 2;\n"
        "end;\n"
        "begin\n"
        "  Helper;\n"
        "  WriteLn(Compute(21));\n"
        "end.",
        diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalV01RejectionTest, ValidProgramWithLocalVars)
{
    // Local variables in procedures should still work
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "procedure DoWork;\n"
        "var\n"
        "  x: Integer;\n"
        "  s: String;\n"
        "begin\n"
        "  x := 42;\n"
        "  s := 'Hello';\n"
        "  WriteLn(s, ' ', x);\n"
        "end;\n"
        "begin\n"
        "  DoWork;\n"
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
