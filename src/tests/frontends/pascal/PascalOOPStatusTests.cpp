//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/frontends/pascal/PascalOOPStatusTests.cpp
// Purpose: Track Pascal OOP implementation status and known bugs.
// Key invariants: Tests document working vs blocked features per the roadmap.
// Ownership/Lifetime: Test suite.
// Links: docs/devdocs/pascal-oop-roadmap.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_HAS_GTEST
#include <gtest/gtest.h>
#else
#include "../../../tests/unit/GTestStub.hpp"
#endif

#include "frontends/pascal/AST.hpp"
#include "frontends/pascal/Lexer.hpp"
#include "frontends/pascal/Lowerer.hpp"
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

/// @brief Parse, analyze, and lower a program to IL.
/// @return True if the entire pipeline succeeded.
bool compileProgram(const std::string &source, DiagnosticEngine &diag)
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
    (void)lowerer.lower(*prog, analyzer);
    return diag.errorCount() == 0;
}

//===----------------------------------------------------------------------===//
// WORKING: Parser-Level OOP Features
// These tests verify that Pascal OOP parsing is complete.
//===----------------------------------------------------------------------===//

TEST(PascalOOPStatus, ParserClassDeclaration)
{
    DiagnosticEngine diag;
    std::string src = "program Test;\n"
                      "type\n"
                      "  TPoint = class\n"
                      "  public\n"
                      "    X: Integer;\n"
                      "    Y: Integer;\n"
                      "  end;\n"
                      "begin\n"
                      "end.";

    Lexer lexer(src, 0, diag);
    Parser parser(lexer, diag);
    auto prog = parser.parseProgram();
    ASSERT_NE(prog, nullptr);
    EXPECT_FALSE(parser.hasError());
}

TEST(PascalOOPStatus, ParserInterfaceDeclaration)
{
    DiagnosticEngine diag;
    std::string src = "program Test;\n"
                      "type\n"
                      "  IDrawable = interface\n"
                      "    procedure Draw;\n"
                      "  end;\n"
                      "begin\n"
                      "end.";

    Lexer lexer(src, 0, diag);
    Parser parser(lexer, diag);
    auto prog = parser.parseProgram();
    ASSERT_NE(prog, nullptr);
    EXPECT_FALSE(parser.hasError());
}

TEST(PascalOOPStatus, ParserVirtualOverrideAbstract)
{
    DiagnosticEngine diag;
    std::string src = "program Test;\n"
                      "type\n"
                      "  TBase = class\n"
                      "  public\n"
                      "    procedure DoWork; virtual; abstract;\n"
                      "  end;\n"
                      "  TChild = class(TBase)\n"
                      "  public\n"
                      "    procedure DoWork; override;\n"
                      "  end;\n"
                      "begin\n"
                      "end.";

    Lexer lexer(src, 0, diag);
    Parser parser(lexer, diag);
    auto prog = parser.parseProgram();
    ASSERT_NE(prog, nullptr);
    EXPECT_FALSE(parser.hasError());
}

TEST(PascalOOPStatus, ParserConstructorDestructor)
{
    DiagnosticEngine diag;
    std::string src = "program Test;\n"
                      "type\n"
                      "  TFoo = class\n"
                      "  public\n"
                      "    constructor Create;\n"
                      "    destructor Destroy;\n"
                      "  end;\n"
                      "begin\n"
                      "end.";

    Lexer lexer(src, 0, diag);
    Parser parser(lexer, diag);
    auto prog = parser.parseProgram();
    ASSERT_NE(prog, nullptr);
    EXPECT_FALSE(parser.hasError());
}

TEST(PascalOOPStatus, ParserProperty)
{
    DiagnosticEngine diag;
    std::string src = "program Test;\n"
                      "type\n"
                      "  TCounter = class\n"
                      "  private\n"
                      "    FValue: Integer;\n"
                      "  public\n"
                      "    property Value: Integer read FValue write FValue;\n"
                      "  end;\n"
                      "begin\n"
                      "end.";

    Lexer lexer(src, 0, diag);
    Parser parser(lexer, diag);
    auto prog = parser.parseProgram();
    ASSERT_NE(prog, nullptr);
    EXPECT_FALSE(parser.hasError());
}

TEST(PascalOOPStatus, ParserWeakField)
{
    DiagnosticEngine diag;
    std::string src = "program Test;\n"
                      "type\n"
                      "  TNode = class\n"
                      "  public\n"
                      "    Next: TNode;\n"
                      "    weak Prev: TNode;\n"
                      "  end;\n"
                      "begin\n"
                      "end.";

    Lexer lexer(src, 0, diag);
    Parser parser(lexer, diag);
    auto prog = parser.parseProgram();
    ASSERT_NE(prog, nullptr);
    EXPECT_FALSE(parser.hasError());
}

TEST(PascalOOPStatus, ParserInheritedStatement)
{
    DiagnosticEngine diag;
    std::string src = "program Test;\n"
                      "type\n"
                      "  TBase = class\n"
                      "  public\n"
                      "    procedure DoWork; virtual;\n"
                      "  end;\n"
                      "  TChild = class(TBase)\n"
                      "  public\n"
                      "    procedure DoWork; override;\n"
                      "  end;\n"
                      "procedure TBase.DoWork;\n"
                      "begin\n"
                      "end;\n"
                      "procedure TChild.DoWork;\n"
                      "begin\n"
                      "  inherited\n"
                      "end;\n"
                      "begin\n"
                      "end.";

    Lexer lexer(src, 0, diag);
    Parser parser(lexer, diag);
    auto prog = parser.parseProgram();
    ASSERT_NE(prog, nullptr);
    EXPECT_FALSE(parser.hasError());
}

//===----------------------------------------------------------------------===//
// WORKING: Semantic Analysis OOP Features
// These tests verify that semantic validation works correctly.
//===----------------------------------------------------------------------===//

TEST(PascalOOPStatus, SemanticOverrideValidation)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TBase = class\n"
                                 "  public\n"
                                 "    procedure Foo; virtual;\n"
                                 "  end;\n"
                                 "  TChild = class(TBase)\n"
                                 "  public\n"
                                 "    procedure Foo; override;\n"
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalOOPStatus, SemanticInterfaceConformance)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  IFoo = interface\n"
                                 "    procedure DoFoo;\n"
                                 "  end;\n"
                                 "  TFoo = class(IFoo)\n"
                                 "  public\n"
                                 "    procedure DoFoo;\n"
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalOOPStatus, SemanticAbstractClassDetection)
{
    DiagnosticEngine diag;
    // An abstract class cannot be instantiated
    // This should pass semantic analysis (class definition is valid)
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TAbstract = class\n"
                                 "  public\n"
                                 "    procedure DoWork; virtual; abstract;\n"
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalOOPStatus, SemanticSelfInMethod)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TCounter = class\n"
                                 "  public\n"
                                 "    Value: Integer;\n"
                                 "    procedure Inc;\n"
                                 "  end;\n"
                                 "procedure TCounter.Inc;\n"
                                 "begin\n"
                                 "  Self.Value := Self.Value + 1\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// WORKING: IS Expression (per Pascal spec)
// The Pascal spec includes IS operator for RTTI.
//===----------------------------------------------------------------------===//

TEST(PascalOOPStatus, IsExpressionSemantics)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TBase = class\n"
                                 "  public\n"
                                 "    x: Integer;\n"
                                 "  end;\n"
                                 "  TChild = class(TBase)\n"
                                 "  public\n"
                                 "    y: Integer;\n"
                                 "  end;\n"
                                 "var\n"
                                 "  obj: TBase;\n"
                                 "begin\n"
                                 "  if obj is TChild then\n"
                                 "    WriteLn('Is child')\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// BUG-PAS-OOP-001: Field Access in Methods - FIXED
// Status: Now working - implicit field access resolves correctly
// Verified: 2025-12 - fields can be accessed without Self prefix
//===----------------------------------------------------------------------===//

TEST(PascalOOPStatus, BUG001_ImplicitFieldAccessInMethod_FIXED)
{
    // BUG-PAS-OOP-001 is FIXED. Implicit field access now works.
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TCircle = class\n"
                                 "  public\n"
                                 "    Radius: Real;\n"
                                 "    function Area: Real;\n"
                                 "  end;\n"
                                 "function TCircle.Area: Real;\n"
                                 "begin\n"
                                 "  Result := 3.14159 * Radius * Radius\n" // Implicit field access
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    // BUG-PAS-OOP-001: Implicit field access fails
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// BUG-PAS-OOP-002: Constructor Calls - FIXED
// Status: TClassName.Create syntax now correctly recognized and lowered
// Verified: 2025-12 - constructor calls generate proper allocation + ctor call
//===----------------------------------------------------------------------===//

TEST(PascalOOPStatus, BUG002_ConstructorCallSyntax_FIXED)
{
    // BUG-PAS-OOP-002 is FIXED. Constructor call syntax now works.
    DiagnosticEngine diag;
    bool result = compileProgram("program Test;\n"
                                 "type\n"
                                 "  TPoint = class\n"
                                 "  public\n"
                                 "    X: Integer;\n"
                                 "    Y: Integer;\n"
                                 "    constructor Create(aX: Integer; aY: Integer);\n"
                                 "  end;\n"
                                 "constructor TPoint.Create(aX: Integer; aY: Integer);\n"
                                 "begin\n"
                                 "  Self.X := aX;\n"
                                 "  Self.Y := aY\n"
                                 "end;\n"
                                 "var\n"
                                 "  p: TPoint;\n"
                                 "begin\n"
                                 "  p := TPoint.Create(10, 20)\n" // Constructor call syntax
                                 "end.",
                                 diag);
    // BUG-PAS-OOP-002: Constructor call syntax not recognized
    EXPECT_TRUE(result);
}

//===----------------------------------------------------------------------===//
// BUG-PAS-OOP-003: Record/Class Field Access - FIXED
// Status: Now working - global record/class field access lowered correctly
// Verified: 2025-12 - field read/write generates proper GEP + load/store IL
//===----------------------------------------------------------------------===//

TEST(PascalOOPStatus, BUG003_RecordFieldAccess_FIXED)
{
    // BUG-PAS-OOP-003 is FIXED. Record field access now works.
    DiagnosticEngine diag;
    bool result = compileProgram("program Test;\n"
                                 "type\n"
                                 "  TPoint = record\n"
                                 "    X: Integer;\n"
                                 "    Y: Integer;\n"
                                 "  end;\n"
                                 "var\n"
                                 "  p: TPoint;\n"
                                 "begin\n"
                                 "  p.X := 5;\n"
                                 "  WriteLn(p.X)\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalOOPStatus, BUG003_ClassFieldAccess_FIXED)
{
    // BUG-PAS-OOP-003 is FIXED. Class field access now works.
    // Note: requires explicit constructor (implicit ctors are a separate issue)
    DiagnosticEngine diag;
    bool result = compileProgram("program Test;\n"
                                 "type\n"
                                 "  TCounter = class\n"
                                 "  public\n"
                                 "    Value: Integer;\n"
                                 "    constructor Create(v: Integer);\n"
                                 "  end;\n"
                                 "constructor TCounter.Create(v: Integer);\n"
                                 "begin\n"
                                 "  Self.Value := v\n"
                                 "end;\n"
                                 "var\n"
                                 "  c: TCounter;\n"
                                 "begin\n"
                                 "  c := TCounter.Create(42);\n"
                                 "  WriteLn(c.Value)\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Feature Gap Documentation (Per Spec)
// These tests document features intentionally NOT in Pascal spec.
//===----------------------------------------------------------------------===//

// NOTE: The following features are NOT in the Viper Pascal spec by design:
// - FINAL modifier for methods/classes (use abstract instead)
// - AS operator for casting (use IS with explicit casts)
// - DELETE/Dispose statements (automatic memory management)
// - Static fields/methods (not supported)
//
// These are intentional omissions per the ViperPascal v0.1 Draft6 Specification,
// not bugs to be fixed. See docs/devdocs/pascal-oop-roadmap.md for details.

} // namespace

#ifndef VIPER_HAS_GTEST
int main()
{
    return RUN_ALL_TESTS();
}
#endif
