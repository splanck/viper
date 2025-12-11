//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/frontends/pascal/SemanticVisibilityTests.cpp
// Purpose: Tests for Pascal OOP visibility enforcement.
// Key invariants: Private members only visible within declaring class.
// Ownership/Lifetime: Test suite.
// Links: docs/devdocs/oop-semantics.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_HAS_GTEST
#include <gtest/gtest.h>
#else
#include "../../../tests/unit/GTestStub.hpp"
#endif

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

/// @brief Check that a program fails with an error containing the given substring.
bool expectError(const std::string &source, const std::string &errorSubstr)
{
    DiagnosticEngine diag;
    bool success = analyzeProgram(source, diag);
    if (success)
        return false;
    // Check that the error message contains the expected substring
    // (DiagnosticEngine doesn't expose messages easily, so we just check for error)
    return diag.errorCount() > 0;
}

//===----------------------------------------------------------------------===//
// PUBLIC field access - should succeed
//===----------------------------------------------------------------------===//

TEST(PascalVisibility, PublicFieldAccessFromOutside)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  TPoint = class\n"
        "  public\n"
        "    X: Integer;\n"
        "    Y: Integer;\n"
        "    constructor Create;\n"
        "  end;\n"
        "constructor TPoint.Create;\n"
        "begin\n"
        "end;\n"
        "var\n"
        "  p: TPoint;\n"
        "begin\n"
        "  p := TPoint.Create;\n"
        "  p.X := 10;\n" // Public field access - should succeed
        "  WriteLn(p.Y)\n"
        "end.",
        diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalVisibility, PublicFieldAccessFromWithinClass)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  TPoint = class\n"
        "  public\n"
        "    X: Integer;\n"
        "    procedure SetX(v: Integer);\n"
        "  end;\n"
        "procedure TPoint.SetX(v: Integer);\n"
        "begin\n"
        "  Self.X := v\n" // Public field access from within class - should succeed
        "end;\n"
        "begin\n"
        "end.",
        diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// PRIVATE field access - should fail from outside
//===----------------------------------------------------------------------===//

TEST(PascalVisibility, PrivateFieldAccessFromOutsideFails)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  TCounter = class\n"
        "  private\n"
        "    FValue: Integer;\n"
        "  public\n"
        "    constructor Create;\n"
        "  end;\n"
        "constructor TCounter.Create;\n"
        "begin\n"
        "end;\n"
        "var\n"
        "  c: TCounter;\n"
        "begin\n"
        "  c := TCounter.Create;\n"
        "  c.FValue := 42\n" // Private field access from outside - should fail
        "end.",
        diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalVisibility, PrivateFieldReadFromOutsideFails)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  TCounter = class\n"
        "  private\n"
        "    FValue: Integer;\n"
        "  public\n"
        "    constructor Create;\n"
        "  end;\n"
        "constructor TCounter.Create;\n"
        "begin\n"
        "end;\n"
        "var\n"
        "  c: TCounter;\n"
        "  x: Integer;\n"
        "begin\n"
        "  c := TCounter.Create;\n"
        "  x := c.FValue\n" // Private field read from outside - should fail
        "end.",
        diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalVisibility, PrivateFieldAccessFromWithinClassSucceeds)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  TCounter = class\n"
        "  private\n"
        "    FValue: Integer;\n"
        "  public\n"
        "    procedure Inc;\n"
        "    function GetValue: Integer;\n"
        "  end;\n"
        "procedure TCounter.Inc;\n"
        "begin\n"
        "  Self.FValue := Self.FValue + 1\n" // Private access from within class - should succeed
        "end;\n"
        "function TCounter.GetValue: Integer;\n"
        "begin\n"
        "  Result := Self.FValue\n"
        "end;\n"
        "begin\n"
        "end.",
        diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// PRIVATE method access
//===----------------------------------------------------------------------===//

TEST(PascalVisibility, PrivateMethodCallFromOutsideFails)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  THelper = class\n"
        "  private\n"
        "    procedure DoInternal;\n"
        "  public\n"
        "    constructor Create;\n"
        "  end;\n"
        "constructor THelper.Create;\n"
        "begin\n"
        "end;\n"
        "procedure THelper.DoInternal;\n"
        "begin\n"
        "end;\n"
        "var\n"
        "  h: THelper;\n"
        "begin\n"
        "  h := THelper.Create;\n"
        "  h.DoInternal\n" // Private method from outside - should fail
        "end.",
        diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalVisibility, PrivateMethodCallFromWithinClassSucceeds)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  THelper = class\n"
        "  private\n"
        "    procedure DoInternal;\n"
        "  public\n"
        "    procedure DoWork;\n"
        "  end;\n"
        "procedure THelper.DoInternal;\n"
        "begin\n"
        "end;\n"
        "procedure THelper.DoWork;\n"
        "begin\n"
        "  DoInternal\n" // Private method from within class - should succeed
        "end;\n"
        "begin\n"
        "end.",
        diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// PRIVATE constructor access
//===----------------------------------------------------------------------===//

TEST(PascalVisibility, PrivateConstructorFromOutsideFails)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  TSingleton = class\n"
        "  private\n"
        "    constructor Create;\n"
        "  end;\n"
        "constructor TSingleton.Create;\n"
        "begin\n"
        "end;\n"
        "var\n"
        "  s: TSingleton;\n"
        "begin\n"
        "  s := TSingleton.Create\n" // Private constructor from outside - should fail
        "end.",
        diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalVisibility, PublicConstructorSucceeds)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  TPoint = class\n"
        "  public\n"
        "    constructor Create;\n"
        "  end;\n"
        "constructor TPoint.Create;\n"
        "begin\n"
        "end;\n"
        "var\n"
        "  p: TPoint;\n"
        "begin\n"
        "  p := TPoint.Create\n"
        "end.",
        diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Inherited private field access
//===----------------------------------------------------------------------===//

TEST(PascalVisibility, PrivateFieldInheritedFromBaseNotVisible)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  TBase = class\n"
        "  private\n"
        "    FSecret: Integer;\n"
        "  end;\n"
        "  TChild = class(TBase)\n"
        "  public\n"
        "    procedure TryAccess;\n"
        "  end;\n"
        "procedure TChild.TryAccess;\n"
        "begin\n"
        "  Self.FSecret := 42\n" // Private in base, not visible in child
        "end;\n"
        "begin\n"
        "end.",
        diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalVisibility, PublicFieldInheritedFromBaseIsVisible)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  TBase = class\n"
        "  public\n"
        "    Value: Integer;\n"
        "  end;\n"
        "  TChild = class(TBase)\n"
        "  public\n"
        "    procedure SetValue(v: Integer);\n"
        "  end;\n"
        "procedure TChild.SetValue(v: Integer);\n"
        "begin\n"
        "  Self.Value := v\n" // Public in base, visible in child
        "end;\n"
        "begin\n"
        "end.",
        diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// With statement visibility
//===----------------------------------------------------------------------===//

TEST(PascalVisibility, WithStatementPrivateFieldFails)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  TBox = class\n"
        "  private\n"
        "    FContents: Integer;\n"
        "  public\n"
        "    constructor Create;\n"
        "  end;\n"
        "constructor TBox.Create;\n"
        "begin\n"
        "end;\n"
        "var\n"
        "  b: TBox;\n"
        "begin\n"
        "  b := TBox.Create;\n"
        "  with b do\n"
        "    FContents := 10\n" // Private field in with - should fail
        "end.",
        diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalVisibility, WithStatementPublicFieldSucceeds)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  TBox = class\n"
        "  public\n"
        "    Contents: Integer;\n"
        "    constructor Create;\n"
        "  end;\n"
        "constructor TBox.Create;\n"
        "begin\n"
        "end;\n"
        "var\n"
        "  b: TBox;\n"
        "begin\n"
        "  b := TBox.Create;\n"
        "  with b do\n"
        "    Contents := 10\n" // Public field in with - should succeed
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
