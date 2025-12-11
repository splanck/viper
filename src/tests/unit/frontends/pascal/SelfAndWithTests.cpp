//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/frontends/pascal/SelfAndWithTests.cpp
// Purpose: Tests for Pascal Self binding, member access, and with statement.
// Key invariants: Verifies Self resolves correctly within methods, field/method
//                 access works properly, and with statements bind names correctly.
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

/// @brief Parse and analyze a program, returning success status.
bool analyzeProgram(const std::string &source, DiagnosticEngine &diag)
{
    Lexer lexer(source, 0, diag);
    Parser parser(lexer, diag);
    auto prog = parser.parseProgram();
    if (!prog || parser.hasError())
        return false;

    auto analyzer = std::make_unique<SemanticAnalyzer>(diag);
    return analyzer->analyze(*prog);
}

//===----------------------------------------------------------------------===//
// Self Binding Tests
//===----------------------------------------------------------------------===//

TEST(PascalSelfAndWith, SelfAccessFieldInMethod)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TPoint = class
    X, Y: Integer;
    procedure SetX(val: Integer);
  end;

procedure TPoint.SetX(val: Integer);
begin
  Self.X := val
end;

begin
end.
)",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSelfAndWith, SelfImplicitFieldAccess)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TCounter = class
    Value: Integer;
    procedure Increment;
  end;

procedure TCounter.Increment;
begin
  Value := Value + 1
end;

begin
end.
)",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSelfAndWith, SelfDisambiguatesShadowedField)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TPoint = class
    X, Y: Real;
    constructor Create(X, Y: Real);
  end;

constructor TPoint.Create(X, Y: Real);
begin
  Self.X := X;
  Self.Y := Y
end;

begin
end.
)",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSelfAndWith, SelfCallsOwnMethod)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TCalc = class
    function GetValue: Integer;
    function GetDoubleValue: Integer;
  end;

function TCalc.GetValue: Integer;
begin
  Result := 42
end;

function TCalc.GetDoubleValue: Integer;
begin
  Result := Self.GetValue * 2
end;

begin
end.
)",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSelfAndWith, SelfImplicitMethodCall)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TCalc = class
    function GetValue: Integer;
    function GetTripleValue: Integer;
  end;

function TCalc.GetValue: Integer;
begin
  Result := 10
end;

function TCalc.GetTripleValue: Integer;
begin
  Result := GetValue * 3
end;

begin
end.
)",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSelfAndWith, SelfOutsideMethodError)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
var
  x: Integer;
begin
  x := Self.Value
end.
)",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalSelfAndWith, SelfInConstructor)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TBox = class
    Width, Height: Integer;
    constructor Create(w, h: Integer);
    function Area: Integer;
  end;

constructor TBox.Create(w, h: Integer);
begin
  Self.Width := w;
  Self.Height := h
end;

function TBox.Area: Integer;
begin
  Result := Width * Height
end;

begin
end.
)",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Member Access Tests
//===----------------------------------------------------------------------===//

TEST(PascalSelfAndWith, FieldAccessOnVariable)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TPoint = class
    X, Y: Integer;
    constructor Create;
  end;

constructor TPoint.Create;
begin
  X := 0;
  Y := 0
end;

var
  p: TPoint;
begin
  p := TPoint.Create;
  p.X := 10;
  p.Y := 20
end.
)",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSelfAndWith, MethodCallOnVariable)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TGreeter = class
    Name: String;
    function Greet: String;
  end;

function TGreeter.Greet: String;
begin
  Result := 'Hello, ' + Name
end;

var
  g: TGreeter;
  s: String;
begin
  s := g.Greet
end.
)",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSelfAndWith, FieldAccessChain)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TInner = class
    Value: Integer;
  end;
  TOuter = class
    Inner: TInner;
  end;

var
  o: TOuter;
  x: Integer;
begin
  x := o.Inner.Value
end.
)",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSelfAndWith, NonExistentFieldError)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TPoint = class
    X, Y: Integer;
  end;

var
  p: TPoint;
begin
  p.Z := 10
end.
)",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalSelfAndWith, NonExistentMethodError)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TPoint = class
    X, Y: Integer;
  end;

var
  p: TPoint;
begin
  p.Move(1, 2)
end.
)",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalSelfAndWith, PrivateFieldAccessError)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TSecret = class
  private
    Value: Integer;
  end;

var
  s: TSecret;
begin
  s.Value := 42
end.
)",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalSelfAndWith, PrivateFieldAccessFromSameClassOK)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TSecret = class
  private
    Value: Integer;
  public
    procedure SetValue(v: Integer);
  end;

procedure TSecret.SetValue(v: Integer);
begin
  Value := v
end;

begin
end.
)",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSelfAndWith, PropertyAccess)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TCounter = class
  private
    FValue: Integer;
  public
    property Value: Integer read FValue write FValue;
  end;

var
  c: TCounter;
  x: Integer;
begin
  c.Value := 10;
  x := c.Value
end.
)",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// With Statement Tests (for records and classes)
//===----------------------------------------------------------------------===//

TEST(PascalSelfAndWith, WithRecordFieldAccess)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TPoint = record
    X, Y: Integer;
  end;

var
  p: TPoint;
begin
  with p do
  begin
    X := 10;
    Y := 20
  end
end.
)",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSelfAndWith, WithClassFieldAccess)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TPoint = class
    X, Y: Integer;
  end;

var
  p: TPoint;
begin
  with p do
  begin
    X := 10;
    Y := 20
  end
end.
)",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSelfAndWith, WithClassMethodCall)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TCalc = class
    Value: Integer;
    function GetValue: Integer;
  end;

function TCalc.GetValue: Integer;
begin
  Result := Value
end;

var
  c: TCalc;
  x: Integer;
begin
  with c do
  begin
    Value := 42;
    x := GetValue
  end
end.
)",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSelfAndWith, WithNestedRecords)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TInner = record
    A, B: Integer;
  end;
  TOuter = record
    Inner: TInner;
    C: Integer;
  end;

var
  o: TOuter;
begin
  with o do
  begin
    C := 10;
    with Inner do
    begin
      A := 1;
      B := 2
    end
  end
end.
)",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSelfAndWith, WithMultipleObjects)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TPoint = record
    X, Y: Integer;
  end;
  TRect = record
    Width, Height: Integer;
  end;

var
  p: TPoint;
  r: TRect;
begin
  with p, r do
  begin
    X := 10;
    Y := 20;
    Width := 100;
    Height := 200
  end
end.
)",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSelfAndWith, WithNonRecordClassError)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
var
  x: Integer;
begin
  with x do
    x := 10
end.
)",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalSelfAndWith, WithPropertyAccess)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TCounter = class
  private
    FValue: Integer;
  public
    property Value: Integer read FValue write FValue;
  end;

var
  c: TCounter;
begin
  with c do
    Value := 42
end.
)",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Non-OOP Regression Tests
//===----------------------------------------------------------------------===//

TEST(PascalSelfAndWith, GlobalVariablesUnaffected)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
var
  x, y: Integer;
begin
  x := 10;
  y := x + 5
end.
)",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSelfAndWith, LocalVariablesUnaffected)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;

procedure Foo;
var
  x, y: Integer;
begin
  x := 10;
  y := x + 5
end;

begin
  Foo
end.
)",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSelfAndWith, NonMethodFunctionCallsUnaffected)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;

function Add(a, b: Integer): Integer;
begin
  Result := a + b
end;

var
  x: Integer;
begin
  x := Add(1, 2)
end.
)",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSelfAndWith, RecordFieldAccessWithoutWith)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TPoint = record
    X, Y: Integer;
  end;

var
  p: TPoint;
begin
  p.X := 10;
  p.Y := 20
end.
)",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Inherited and Base Class Access Tests
//===----------------------------------------------------------------------===//

TEST(PascalSelfAndWith, InheritedFieldAccess)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TBase = class
    Value: Integer;
  end;
  TDerived = class(TBase)
    procedure SetValue(v: Integer);
  end;

procedure TDerived.SetValue(v: Integer);
begin
  Value := v
end;

begin
end.
)",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSelfAndWith, SelfAccessInheritedField)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TBase = class
    Name: String;
  end;
  TChild = class(TBase)
    function GetName: String;
  end;

function TChild.GetName: String;
begin
  Result := Self.Name
end;

begin
end.
)",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSelfAndWith, InheritedMethodCall)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(R"(
program Test;
type
  TBase = class
    procedure DoWork; virtual;
  end;
  TDerived = class(TBase)
    procedure DoWork; override;
  end;

procedure TBase.DoWork;
begin
end;

procedure TDerived.DoWork;
begin
  inherited DoWork
end;

begin
end.
)",
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
