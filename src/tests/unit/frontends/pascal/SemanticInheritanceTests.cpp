//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/frontends/pascal/SemanticInheritanceTests.cpp
// Purpose: Tests for Pascal OOP inheritance semantic checks.
// Key invariants: Single inheritance, interface implementation, override validation.
// Ownership/Lifetime: Test suite.
// Links: docs/devdocs/oop-semantics.md
//
//===----------------------------------------------------------------------===//
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
// Single Inheritance Tests
//===----------------------------------------------------------------------===//

TEST(PascalInheritance, SimpleInheritance)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TBase = class\n"
                                 "  public\n"
                                 "    X: Integer;\n"
                                 "  end;\n"
                                 "  TChild = class(TBase)\n"
                                 "  public\n"
                                 "    Y: Integer;\n"
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalInheritance, ChainedInheritance)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TGrandparent = class\n"
                                 "  public\n"
                                 "    A: Integer;\n"
                                 "  end;\n"
                                 "  TParent = class(TGrandparent)\n"
                                 "  public\n"
                                 "    B: Integer;\n"
                                 "  end;\n"
                                 "  TChild = class(TParent)\n"
                                 "  public\n"
                                 "    C: Integer;\n"
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalInheritance, InheritedFieldAccess)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
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
                                 "  Self.Value := v\n" // Access inherited field
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalInheritance, UnknownBaseClassFails)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TChild = class(TUnknown)\n"
                                 "  public\n"
                                 "    X: Integer;\n"
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Virtual/Override Tests
//===----------------------------------------------------------------------===//

TEST(PascalInheritance, VirtualMethodDeclaration)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TBase = class\n"
                                 "  public\n"
                                 "    procedure DoWork; virtual;\n"
                                 "  end;\n"
                                 "procedure TBase.DoWork;\n"
                                 "begin\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalInheritance, OverrideVirtualMethod)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
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
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalInheritance, OverrideWithoutVirtualFails)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TBase = class\n"
                                 "  public\n"
                                 "    procedure DoWork;\n" // Not virtual
                                 "  end;\n"
                                 "  TChild = class(TBase)\n"
                                 "  public\n"
                                 "    procedure DoWork; override;\n" // Cannot override non-virtual
                                 "  end;\n"
                                 "procedure TBase.DoWork;\n"
                                 "begin\n"
                                 "end;\n"
                                 "procedure TChild.DoWork;\n"
                                 "begin\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalInheritance, OverrideNoBaseFails)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TChild = class\n" // No base class
                                 "  public\n"
                                 "    procedure DoWork; override;\n" // Cannot override without base
                                 "  end;\n"
                                 "procedure TChild.DoWork;\n"
                                 "begin\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Abstract Method Tests
//===----------------------------------------------------------------------===//

TEST(PascalInheritance, AbstractMethodDeclaration)
{
    DiagnosticEngine diag;
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

TEST(PascalInheritance, AbstractClassInstantiationFails)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TAbstract = class\n"
                                 "  public\n"
                                 "    constructor Create;\n"
                                 "    procedure DoWork; virtual; abstract;\n"
                                 "  end;\n"
                                 "constructor TAbstract.Create;\n"
                                 "begin\n"
                                 "end;\n"
                                 "var\n"
                                 "  a: TAbstract;\n"
                                 "begin\n"
                                 "  a := TAbstract.Create\n" // Cannot instantiate abstract class
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalInheritance, ConcreteSubclassOfAbstract)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TAbstract = class\n"
                                 "  public\n"
                                 "    procedure DoWork; virtual; abstract;\n"
                                 "  end;\n"
                                 "  TConcrete = class(TAbstract)\n"
                                 "  public\n"
                                 "    constructor Create;\n"
                                 "    procedure DoWork; override;\n"
                                 "  end;\n"
                                 "constructor TConcrete.Create;\n"
                                 "begin\n"
                                 "end;\n"
                                 "procedure TConcrete.DoWork;\n"
                                 "begin\n"
                                 "end;\n"
                                 "var\n"
                                 "  c: TConcrete;\n"
                                 "begin\n"
                                 "  c := TConcrete.Create\n" // Can instantiate concrete subclass
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalInheritance, AbstractFunctionReturningReal)
{
    // Test abstract method returning a value type (Real)
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TShape = class\n"
                                 "  public\n"
                                 "    function Area: Real; virtual; abstract;\n"
                                 "  end;\n"
                                 "  TCircle = class(TShape)\n"
                                 "  public\n"
                                 "    Radius: Real;\n"
                                 "    constructor Create(r: Real);\n"
                                 "    function Area: Real; override;\n"
                                 "  end;\n"
                                 "constructor TCircle.Create(r: Real);\n"
                                 "begin\n"
                                 "  Radius := r\n"
                                 "end;\n"
                                 "function TCircle.Area: Real;\n"
                                 "begin\n"
                                 "  Result := 3.14159 * Radius * Radius\n"
                                 "end;\n"
                                 "var\n"
                                 "  c: TCircle;\n"
                                 "begin\n"
                                 "  c := TCircle.Create(5.0)\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalInheritance, AbstractShapeCannotBeInstantiated)
{
    // TShape with abstract Area cannot be instantiated
    DiagnosticEngine diag;
    bool result =
        analyzeProgram("program Test;\n"
                       "type\n"
                       "  TShape = class\n"
                       "  public\n"
                       "    constructor Create;\n"
                       "    function Area: Real; virtual; abstract;\n"
                       "  end;\n"
                       "constructor TShape.Create;\n"
                       "begin\n"
                       "end;\n"
                       "var\n"
                       "  s: TShape;\n"
                       "begin\n"
                       "  s := TShape.Create\n" // Error: cannot instantiate abstract class
                       "end.",
                       diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalInheritance, InheritedAbstractNotImplementedIsAbstract)
{
    // If a derived class doesn't implement the abstract method, it's also abstract
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TBase = class\n"
                                 "  public\n"
                                 "    procedure DoWork; virtual; abstract;\n"
                                 "  end;\n"
                                 "  TDerived = class(TBase)\n"
                                 "  public\n"
                                 "    constructor Create;\n"
                                 "    // Does not override DoWork - still abstract\n"
                                 "  end;\n"
                                 "constructor TDerived.Create;\n"
                                 "begin\n"
                                 "end;\n"
                                 "var\n"
                                 "  d: TDerived;\n"
                                 "begin\n"
                                 "  d := TDerived.Create\n" // Error: TDerived is still abstract
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Interface Implementation Tests
//===----------------------------------------------------------------------===//

TEST(PascalInheritance, InterfaceDeclaration)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  IDrawable = interface\n"
                                 "    procedure Draw;\n"
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalInheritance, ClassImplementsInterface)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  IDrawable = interface\n"
                                 "    procedure Draw;\n"
                                 "  end;\n"
                                 "  TShape = class(IDrawable)\n"
                                 "  public\n"
                                 "    procedure Draw;\n"
                                 "  end;\n"
                                 "procedure TShape.Draw;\n"
                                 "begin\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalInheritance, MissingInterfaceMethodFails)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  IDrawable = interface\n"
                                 "    procedure Draw;\n"
                                 "    procedure Render;\n"
                                 "  end;\n"
                                 "  TShape = class(IDrawable)\n"
                                 "  public\n"
                                 "    procedure Draw;\n" // Missing Render
                                 "  end;\n"
                                 "procedure TShape.Draw;\n"
                                 "begin\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalInheritance, InterfaceExtension)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  IBase = interface\n"
                                 "    procedure DoBase;\n"
                                 "  end;\n"
                                 "  IExtended = interface(IBase)\n"
                                 "    procedure DoExtended;\n"
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Multiple Interface Implementation
//===----------------------------------------------------------------------===//

TEST(PascalInheritance, ClassImplementsMultipleInterfaces)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  IDrawable = interface\n"
                                 "    procedure Draw;\n"
                                 "  end;\n"
                                 "  IResizable = interface\n"
                                 "    procedure Resize(w: Integer; h: Integer);\n"
                                 "  end;\n"
                                 "  TWidget = class(IDrawable, IResizable)\n"
                                 "  public\n"
                                 "    procedure Draw;\n"
                                 "    procedure Resize(w: Integer; h: Integer);\n"
                                 "  end;\n"
                                 "procedure TWidget.Draw;\n"
                                 "begin\n"
                                 "end;\n"
                                 "procedure TWidget.Resize(w: Integer; h: Integer);\n"
                                 "begin\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Class with Base Class and Interface
//===----------------------------------------------------------------------===//

TEST(PascalInheritance, ClassWithBaseAndInterface)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  IDrawable = interface\n"
                                 "    procedure Draw;\n"
                                 "  end;\n"
                                 "  TBase = class\n"
                                 "  public\n"
                                 "    procedure DoBase;\n"
                                 "  end;\n"
                                 "  TChild = class(TBase, IDrawable)\n"
                                 "  public\n"
                                 "    procedure Draw;\n"
                                 "  end;\n"
                                 "procedure TBase.DoBase;\n"
                                 "begin\n"
                                 "end;\n"
                                 "procedure TChild.Draw;\n"
                                 "begin\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// IS Expression Type Checking
//===----------------------------------------------------------------------===//

TEST(PascalInheritance, IsExpressionWithInheritance)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TBase = class\n"
                                 "  public\n"
                                 "    X: Integer;\n"
                                 "  end;\n"
                                 "  TChild = class(TBase)\n"
                                 "  public\n"
                                 "    Y: Integer;\n"
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

TEST(PascalInheritance, IsExpressionWithInterface)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  IDrawable = interface\n"
                                 "    procedure Draw;\n"
                                 "  end;\n"
                                 "  TShape = class(IDrawable)\n"
                                 "  public\n"
                                 "    procedure Draw;\n"
                                 "  end;\n"
                                 "procedure TShape.Draw;\n"
                                 "begin\n"
                                 "end;\n"
                                 "var\n"
                                 "  obj: TShape;\n"
                                 "begin\n"
                                 "  if obj is IDrawable then\n"
                                 "    WriteLn('Implements IDrawable')\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Inherited Method Call (inherited statement)
//===----------------------------------------------------------------------===//

TEST(PascalInheritance, InheritedStatement)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
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
                                 "  inherited\n" // Call base class DoWork
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalInheritance, InheritedWithoutBaseFails)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TRoot = class\n" // No base class
                                 "  public\n"
                                 "    procedure DoWork;\n"
                                 "  end;\n"
                                 "procedure TRoot.DoWork;\n"
                                 "begin\n"
                                 "  inherited\n" // No base class to inherit from
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Interface Implementation Completeness Tests
//===----------------------------------------------------------------------===//

TEST(PascalInheritance, InterfaceMethodSignatureMismatchFails)
{
    DiagnosticEngine diag;
    // Interface requires procedure Draw, but class has function Draw
    bool result =
        analyzeProgram("program Test;\n"
                       "type\n"
                       "  IDrawable = interface\n"
                       "    procedure Draw;\n"
                       "  end;\n"
                       "  TBadShape = class(IDrawable)\n"
                       "  public\n"
                       "    function Draw: Integer;\n" // Wrong: returns Integer instead of void
                       "  end;\n"
                       "function TBadShape.Draw: Integer;\n"
                       "begin\n"
                       "  Result := 0\n"
                       "end;\n"
                       "begin\n"
                       "end.",
                       diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalInheritance, InterfaceMethodParamCountMismatchFails)
{
    DiagnosticEngine diag;
    // Interface requires procedure SetColor(c: Integer), but class has procedure SetColor
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  IColorable = interface\n"
                                 "    procedure SetColor(c: Integer);\n"
                                 "  end;\n"
                                 "  TBadWidget = class(IColorable)\n"
                                 "  public\n"
                                 "    procedure SetColor;\n" // Wrong: missing parameter
                                 "  end;\n"
                                 "procedure TBadWidget.SetColor;\n"
                                 "begin\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalInheritance, InterfaceMethodParamTypeMismatchFails)
{
    DiagnosticEngine diag;
    // Interface requires procedure SetValue(x: Integer), but class has SetValue(x: String)
    bool result =
        analyzeProgram("program Test;\n"
                       "type\n"
                       "  IValued = interface\n"
                       "    procedure SetValue(x: Integer);\n"
                       "  end;\n"
                       "  TBadItem = class(IValued)\n"
                       "  public\n"
                       "    procedure SetValue(x: String);\n" // Wrong: String instead of Integer
                       "  end;\n"
                       "procedure TBadItem.SetValue(x: String);\n"
                       "begin\n"
                       "end;\n"
                       "begin\n"
                       "end.",
                       diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalInheritance, InterfaceMethodVarParamMismatchFails)
{
    DiagnosticEngine diag;
    // Interface requires procedure Update(var x: Integer), but class has Update(x: Integer)
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  IUpdatable = interface\n"
                                 "    procedure Update(var x: Integer);\n"
                                 "  end;\n"
                                 "  TBadUpdater = class(IUpdatable)\n"
                                 "  public\n"
                                 "    procedure Update(x: Integer);\n" // Wrong: missing var
                                 "  end;\n"
                                 "procedure TBadUpdater.Update(x: Integer);\n"
                                 "begin\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalInheritance, InterfaceWithFunctionMissingGetNameFails)
{
    // Test case from the prompt: TBadButton missing GetName
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  IDrawable = interface\n"
                                 "    procedure Draw;\n"
                                 "    function GetName: String;\n"
                                 "  end;\n"
                                 "  TBadButton = class(IDrawable)\n"
                                 "  public\n"
                                 "    procedure Draw;\n"
                                 "    // Missing: function GetName: String;\n"
                                 "  end;\n"
                                 "procedure TBadButton.Draw;\n"
                                 "begin\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalInheritance, InterfaceCompleteImplementationSucceeds)
{
    // All interface methods are implemented correctly
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  IDrawable = interface\n"
                                 "    procedure Draw;\n"
                                 "    function GetName: String;\n"
                                 "  end;\n"
                                 "  TGoodButton = class(IDrawable)\n"
                                 "  public\n"
                                 "    procedure Draw;\n"
                                 "    function GetName: String;\n"
                                 "  end;\n"
                                 "procedure TGoodButton.Draw;\n"
                                 "begin\n"
                                 "end;\n"
                                 "function TGoodButton.GetName: String;\n"
                                 "begin\n"
                                 "  Result := 'Button'\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
