//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/frontends/pascal/SemanticWithTests.cpp
// Purpose: Unit tests for the Viper Pascal 'with' statement.
// Key invariants: Tests name resolution, type checking, and scope handling.
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
// With Statement - Record Tests
//===----------------------------------------------------------------------===//

TEST(PascalWithTest, BasicRecordWith)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type TPoint = record X, Y: Integer; end;\n"
        "var p: TPoint;\n"
        "begin\n"
        "  with p do\n"
        "  begin\n"
        "    X := 1;\n"
        "    Y := 2;\n"
        "  end;\n"
        "end.",
        diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalWithTest, RecordWithExpression)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type TPoint = record X, Y: Integer; end;\n"
        "var p: TPoint;\n"
        "begin\n"
        "  with p do\n"
        "    X := X + Y;\n"
        "end.",
        diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// With Statement - Class Tests
//===----------------------------------------------------------------------===//

TEST(PascalWithTest, BasicClassWith)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type TPoint = class\n"
        "public\n"
        "  X, Y: Integer;\n"
        "end;\n"
        "var p: TPoint;\n"
        "begin\n"
        "  with p do\n"
        "  begin\n"
        "    X := 10;\n"
        "    Y := 20;\n"
        "  end;\n"
        "end.",
        diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalWithTest, ClassWithMethodCall)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type TPoint = class\n"
        "public\n"
        "  X, Y: Integer;\n"
        "  procedure SetXY(AX, AY: Integer);\n"
        "end;\n"
        "procedure TPoint.SetXY(AX, AY: Integer);\n"
        "begin\n"
        "  X := AX;\n"
        "  Y := AY;\n"
        "end;\n"
        "var p: TPoint;\n"
        "begin\n"
        "  with p do\n"
        "    SetXY(1, 2);\n"
        "end.",
        diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// With Statement - Multiple Objects
//===----------------------------------------------------------------------===//

TEST(PascalWithTest, MultipleObjects)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  TPoint = record X, Y: Integer; end;\n"
        "  TRect = record Left, Top, Right, Bottom: Integer; end;\n"
        "var p: TPoint; r: TRect;\n"
        "begin\n"
        "  with p, r do\n"
        "  begin\n"
        "    X := 1;\n"
        "    Left := 10;\n"
        "  end;\n"
        "end.",
        diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// With Statement - Nested With
//===----------------------------------------------------------------------===//

TEST(PascalWithTest, NestedWith)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  TPoint = record X, Y: Integer; end;\n"
        "  TRect = record Left, Top: Integer; end;\n"
        "var p: TPoint; r: TRect;\n"
        "begin\n"
        "  with p do\n"
        "  begin\n"
        "    X := 1;\n"
        "    with r do\n"
        "    begin\n"
        "      Left := X;\n"
        "    end;\n"
        "  end;\n"
        "end.",
        diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// With Statement - Local Variable Shadowing
//===----------------------------------------------------------------------===//

TEST(PascalWithTest, LocalShadowsWithField)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type TPoint = record X, Y: Integer; end;\n"
        "var p: TPoint; X: Integer;\n"
        "begin\n"
        "  X := 100;\n"
        "  with p do\n"
        "  begin\n"
        "    X := 1;\n"   // This should refer to local X, not p.X
        "  end;\n"
        "end.",
        diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// With Statement - Error Cases
//===----------------------------------------------------------------------===//

TEST(PascalWithTest, WithNonClassOrRecord)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "var x: Integer;\n"
        "begin\n"
        "  with x do\n"
        "    x := 1;\n"
        "end.",
        diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalWithTest, WithUndefinedField)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type TPoint = record X, Y: Integer; end;\n"
        "var p: TPoint;\n"
        "begin\n"
        "  with p do\n"
        "    Z := 1;\n"  // Z is not a field of TPoint
        "end.",
        diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

} // anonymous namespace

#ifndef VIPER_HAS_GTEST
int main()
{
    return RUN_ALL_TESTS();
}
#endif
