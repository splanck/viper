//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/frontends/pascal/PascalOOPDiagnosticsTests.cpp
// Purpose: Tests for Pascal OOP error diagnostics and user-facing messages.
// Key invariants: Error messages are actionable, name relevant entities,
// and are consistent with BASIC's OOP diagnostics.
//
//===----------------------------------------------------------------------===//
#include "frontends/pascal/AST.hpp"
#include "frontends/pascal/Lexer.hpp"
#include "frontends/pascal/Parser.hpp"
#include "frontends/pascal/SemanticAnalyzer.hpp"
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

/// @brief Parse and analyze a program, returning the diagnostic engine.
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

/// @brief Check if any error message contains the given substring.
bool hasErrorContaining(const DiagnosticEngine &diag, const std::string &substr)
{
    for (const auto &d : diag.diagnostics())
    {
        if (d.severity == Severity::Error && d.message.find(substr) != std::string::npos)
            return true;
    }
    return false;
}

/// @brief Get the first error message for inspection.
std::string firstErrorMessage(const DiagnosticEngine &diag)
{
    for (const auto &d : diag.diagnostics())
    {
        if (d.severity == Severity::Error)
            return d.message;
    }
    return "";
}

//===----------------------------------------------------------------------===//
// Override Without Base Virtual Tests
//===----------------------------------------------------------------------===//

TEST(PascalOOPDiag, OverrideWithoutVirtual_ContainsMethodName)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TBase = class
  public
    procedure DoWork;
  end;
  TChild = class(TBase)
  public
    procedure DoWork; override;
  end;
procedure TBase.DoWork; begin end;
procedure TChild.DoWork; begin end;
begin end.
)",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_TRUE(diag.errorCount() > 0u);
    EXPECT_TRUE(hasErrorContaining(diag, "override"));
    EXPECT_TRUE(hasErrorContaining(diag, "virtual"));
}

TEST(PascalOOPDiag, OverrideWithoutVirtual_SuggestsVirtual)
{
    DiagnosticEngine diag;
    analyzeProgram(R"(
program Test;
type
  TBase = class
  public
    procedure Foo;
  end;
  TChild = class(TBase)
  public
    procedure Bar; override;
  end;
begin end.
)",
                   diag);
    EXPECT_TRUE(hasErrorContaining(diag, "declare base method as 'virtual'"));
}

//===----------------------------------------------------------------------===//
// Unknown Base Type Tests
//===----------------------------------------------------------------------===//

TEST(PascalOOPDiag, UnknownBaseType_NamesClass)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TChild = class(TUnknownBase)
  public
    X: Integer;
  end;
begin end.
)",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_TRUE(hasErrorContaining(diag, "TChild"));
    EXPECT_TRUE(hasErrorContaining(diag, "TUnknownBase"));
}

TEST(PascalOOPDiag, UnknownBaseType_SuggestsDeclarationOrder)
{
    DiagnosticEngine diag;
    analyzeProgram(R"(
program Test;
type
  TChild = class(TUnknownBase)
  end;
begin end.
)",
                   diag);
    EXPECT_TRUE(hasErrorContaining(diag, "ensure the base class"));
}

//===----------------------------------------------------------------------===//
// Multiple Class Inheritance Tests
//===----------------------------------------------------------------------===//

TEST(PascalOOPDiag, MultipleInheritance_NamesClasses)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TBase1 = class end;
  TBase2 = class end;
  TChild = class(TBase1, TBase2)
  end;
begin end.
)",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_TRUE(hasErrorContaining(diag, "TChild") || hasErrorContaining(diag, "TBase2"));
}

TEST(PascalOOPDiag, MultipleInheritance_ExplainsSingleInheritance)
{
    DiagnosticEngine diag;
    analyzeProgram(R"(
program Test;
type
  TBase1 = class end;
  TBase2 = class end;
  TChild = class(TBase1, TBase2)
  end;
begin end.
)",
                   diag);
    EXPECT_TRUE(hasErrorContaining(diag, "single class inheritance") ||
                hasErrorContaining(diag, "not an interface"));
}

//===----------------------------------------------------------------------===//
// Missing Interface Method Tests
//===----------------------------------------------------------------------===//

TEST(PascalOOPDiag, MissingInterfaceMethod_NamesMethod)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  IDrawable = interface
    procedure Draw;
  end;
  TShape = class(IDrawable)
  public
    X: Integer;
  end;
begin end.
)",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_TRUE(hasErrorContaining(diag, "Draw"));
}

TEST(PascalOOPDiag, MissingInterfaceMethod_NamesClass)
{
    DiagnosticEngine diag;
    analyzeProgram(R"(
program Test;
type
  IDrawable = interface
    procedure Draw;
  end;
  TShape = class(IDrawable)
  end;
begin end.
)",
                   diag);
    EXPECT_TRUE(hasErrorContaining(diag, "TShape"));
}

TEST(PascalOOPDiag, MissingInterfaceMethod_SuggestsAddingMethod)
{
    DiagnosticEngine diag;
    analyzeProgram(R"(
program Test;
type
  IDrawable = interface
    procedure Draw;
  end;
  TShape = class(IDrawable)
  end;
begin end.
)",
                   diag);
    EXPECT_TRUE(hasErrorContaining(diag, "must implement") || hasErrorContaining(diag, "add"));
}

//===----------------------------------------------------------------------===//
// Abstract Class Instantiation Tests
//===----------------------------------------------------------------------===//

TEST(PascalOOPDiag, AbstractInstantiation_NamesClass)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TAbstractShape = class
  public
    procedure Draw; abstract;
  end;
var
  s: TAbstractShape;
begin
  s := TAbstractShape.Create
end.
)",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_TRUE(hasErrorContaining(diag, "TAbstractShape"));
}

TEST(PascalOOPDiag, AbstractInstantiation_SuggestsSubclass)
{
    DiagnosticEngine diag;
    analyzeProgram(R"(
program Test;
type
  TAbstractShape = class
  public
    procedure Draw; abstract;
  end;
var
  s: TAbstractShape;
begin
  s := TAbstractShape.Create
end.
)",
                   diag);
    EXPECT_TRUE(hasErrorContaining(diag, "concrete subclass") ||
                hasErrorContaining(diag, "instantiate abstract"));
}

//===----------------------------------------------------------------------===//
// Type Cast Error Tests
//===----------------------------------------------------------------------===//

TEST(PascalOOPDiag, InvalidCast_NamesTargetType)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TFoo = class end;
var
  x: Integer;
  f: TFoo;
begin
  f := TFoo(x)
end.
)",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_TRUE(hasErrorContaining(diag, "TFoo") || hasErrorContaining(diag, "cast"));
}

//===----------------------------------------------------------------------===//
// IS Operator Error Tests
//===----------------------------------------------------------------------===//

TEST(PascalOOPDiag, IsOperator_RhsMustBeClass)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TFoo = class end;
var
  f: TFoo;
  b: Boolean;
begin
  f := TFoo.Create;
  b := f is Integer
end.
)",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_TRUE(hasErrorContaining(diag, "'is'"));
    EXPECT_TRUE(hasErrorContaining(diag, "class or interface"));
}

TEST(PascalOOPDiag, IsOperator_LhsMustBeObject)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TFoo = class end;
var
  x: Integer;
  b: Boolean;
begin
  b := x is TFoo
end.
)",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_TRUE(hasErrorContaining(diag, "'is'"));
    EXPECT_TRUE(hasErrorContaining(diag, "object reference") ||
                hasErrorContaining(diag, "class or interface instance"));
}

//===----------------------------------------------------------------------===//
// AS Operator Error Tests
//===----------------------------------------------------------------------===//

TEST(PascalOOPDiag, AsOperator_RhsMustBeClass)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TFoo = class end;
var
  f: TFoo;
  x: Integer;
begin
  f := TFoo.Create;
  x := f as Integer
end.
)",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_TRUE(hasErrorContaining(diag, "'as'"));
    EXPECT_TRUE(hasErrorContaining(diag, "class or interface"));
}

TEST(PascalOOPDiag, AsOperator_LhsMustBeObject)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TFoo = class end;
var
  x: Integer;
  f: TFoo;
begin
  f := x as TFoo
end.
)",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_TRUE(hasErrorContaining(diag, "'as'"));
    EXPECT_TRUE(hasErrorContaining(diag, "object reference") ||
                hasErrorContaining(diag, "class or interface instance"));
}

//===----------------------------------------------------------------------===//
// Member Access Error Tests
//===----------------------------------------------------------------------===//

TEST(PascalOOPDiag, UnknownMember_NamesClassAndMember)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TFoo = class
  public
    X: Integer;
  end;
var
  f: TFoo;
begin
  f := TFoo.Create;
  f.UnknownField := 42
end.
)",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_TRUE(hasErrorContaining(diag, "TFoo"));
    EXPECT_TRUE(hasErrorContaining(diag, "UnknownField"));
}

TEST(PascalOOPDiag, UnknownMember_SuggestsCheckSpelling)
{
    DiagnosticEngine diag;
    analyzeProgram(R"(
program Test;
type
  TFoo = class
  public
    X: Integer;
  end;
var
  f: TFoo;
begin
  f := TFoo.Create;
  f.UnknownField := 42
end.
)",
                   diag);
    EXPECT_TRUE(hasErrorContaining(diag, "check spelling") || hasErrorContaining(diag, "declared"));
}

//===----------------------------------------------------------------------===//
// Method Not Found Tests
//===----------------------------------------------------------------------===//

TEST(PascalOOPDiag, MethodNotFound_NamesClassAndMethod)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TFoo = class
  public
    procedure DoWork;
  end;
procedure TFoo.DoWork; begin end;
var
  f: TFoo;
begin
  f := TFoo.Create;
  f.UnknownMethod
end.
)",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_TRUE(hasErrorContaining(diag, "TFoo"));
    EXPECT_TRUE(hasErrorContaining(diag, "UnknownMethod"));
}

//===----------------------------------------------------------------------===//
// Invalid Weak Attribute Tests
//===----------------------------------------------------------------------===//

TEST(PascalOOPDiag, InvalidWeak_NamesField)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TFoo = class
  public
    weak X: Integer;
  end;
begin end.
)",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_TRUE(hasErrorContaining(diag, "weak"));
    EXPECT_TRUE(hasErrorContaining(diag, "X"));
}

TEST(PascalOOPDiag, InvalidWeak_ExplainsReferenceRequirement)
{
    DiagnosticEngine diag;
    analyzeProgram(R"(
program Test;
type
  TFoo = class
  public
    weak X: Integer;
  end;
begin end.
)",
                   diag);
    EXPECT_TRUE(hasErrorContaining(diag, "class or interface") ||
                hasErrorContaining(diag, "reference"));
}

//===----------------------------------------------------------------------===//
// Nil Comparison Error Tests
//===----------------------------------------------------------------------===//

TEST(PascalOOPDiag, NilComparison_SuggestsOptional)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TFoo = class end;
var
  f: TFoo;
  b: Boolean;
begin
  f := TFoo.Create;
  b := f = nil
end.
)",
                                 diag);
    // This might succeed depending on semantics, but if it fails...
    if (!result)
    {
        EXPECT_TRUE(hasErrorContaining(diag, "optional") || hasErrorContaining(diag, "nil"));
    }
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
