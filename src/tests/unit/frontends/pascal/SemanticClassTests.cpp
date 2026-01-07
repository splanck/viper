//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/frontends/pascal/SemanticClassTests.cpp
// Purpose: Unit tests for Pascal class, inheritance, and interface semantics.
// Key invariants: Tests override checking, interface implementation, weak refs.
// Ownership/Lifetime: Test suite.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
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
// Inheritance and Override Tests
//===----------------------------------------------------------------------===//

TEST(PascalClassTest, VirtualMethodOverrideValid)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TBase = class\n"
                                 "    public\n"
                                 "      procedure Foo; virtual;\n"
                                 "  end;\n"
                                 "  TChild = class(TBase)\n"
                                 "    public\n"
                                 "      procedure Foo; override;\n"
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalClassTest, OverrideWithoutBaseVirtual)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TBase = class\n"
                                 "    public\n"
                                 "      procedure Foo;\n" // Not virtual
                                 "  end;\n"
                                 "  TChild = class(TBase)\n"
                                 "    public\n"
                                 "      procedure Bar; override;\n" // No base Bar at all
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalClassTest, OverrideSignatureMismatch)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TBase = class\n"
                                 "    public\n"
                                 "      procedure Foo(x: Integer); virtual;\n"
                                 "  end;\n"
                                 "  TChild = class(TBase)\n"
                                 "    public\n"
                                 "      procedure Foo(x: String); override;\n" // Wrong signature
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalClassTest, InheritFromUnknownClass)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TChild = class(TUnknown)\n"
                                 "    public\n"
                                 "      x: Integer;\n"
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Interface Implementation Tests
//===----------------------------------------------------------------------===//

TEST(PascalClassTest, InterfaceImplementationValid)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  IFoo = interface\n"
                                 "    procedure DoFoo;\n"
                                 "  end;\n"
                                 "  TGood = class(IFoo)\n"
                                 "    public\n"
                                 "      procedure DoFoo;\n"
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalClassTest, InterfaceNotImplemented)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  IFoo = interface\n"
                                 "    procedure DoFoo;\n"
                                 "  end;\n"
                                 "  TBad = class(IFoo)\n"
                                 "    public\n"
                                 "      procedure DoBar;\n" // Wrong method name
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalClassTest, InterfaceSignatureMismatch)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  IFoo = interface\n"
                                 "    procedure DoFoo(x: Integer);\n"
                                 "  end;\n"
                                 "  TBad = class(IFoo)\n"
                                 "    public\n"
                                 "      procedure DoFoo;\n" // Wrong signature
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalClassTest, UnknownInterface)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TBad = class(IUnknown)\n"
                                 "    public\n"
                                 "      x: Integer;\n"
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Multiple Inheritance Error Tests
//===----------------------------------------------------------------------===//

TEST(PascalClassTest, MultipleBaseClassesError)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TA = class\n"
                                 "    public\n"
                                 "      x: Integer;\n"
                                 "  end;\n"
                                 "  TB = class\n"
                                 "    public\n"
                                 "      y: Integer;\n"
                                 "  end;\n"
                                 "  TBad = class(TA, TB)\n" // TB is a class, not an interface
                                 "    public\n"
                                 "      z: Integer;\n"
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalClassTest, ClassWithBaseAndInterface)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  IFoo = interface\n"
                                 "    procedure DoFoo;\n"
                                 "  end;\n"
                                 "  TBase = class\n"
                                 "    public\n"
                                 "      x: Integer;\n"
                                 "  end;\n"
                                 "  TChild = class(TBase, IFoo)\n" // OK: one class, one interface
                                 "    public\n"
                                 "      procedure DoFoo;\n"
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Weak Field Tests
//===----------------------------------------------------------------------===//

TEST(PascalClassTest, WeakFieldClassValid)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TNode = class\n"
                                 "    public\n"
                                 "      weak Prev: TNode;\n" // OK: weak on class type
                                 "      Next: TNode;\n"
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalClassTest, WeakFieldIntegerError)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TBad = class\n"
                                 "    public\n"
                                 "      weak Count: Integer;\n" // Error: weak on value type
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalClassTest, WeakFieldStringError)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TBad = class\n"
                                 "    public\n"
                                 "      weak Name: String;\n" // Error: weak on string
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalClassTest, WeakFieldOptionalClassValid)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TNode = class\n"
                                 "    public\n"
                                 "      weak Parent: TNode?;\n" // OK: weak on optional class
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalClassTest, WeakFieldInterfaceValid)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  IObserver = interface\n"
                                 "    procedure Notify;\n"
                                 "  end;\n"
                                 "  TSubject = class\n"
                                 "    public\n"
                                 "      weak Observer: IObserver;\n" // OK: weak on interface
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Destructor Tests
//===----------------------------------------------------------------------===//

TEST(PascalClassTest, DestructorNamedDestroyValid)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TFoo = class\n"
                                 "    public\n"
                                 "      destructor Destroy;\n"
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalClassTest, DestructorWrongNameError)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TFoo = class\n"
                                 "    public\n"
                                 "      destructor Free;\n" // Error: must be named Destroy
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Class Registry and Lookup Tests
//===----------------------------------------------------------------------===//

TEST(PascalClassTest, ClassLookup)
{
    DiagnosticEngine diag;

    // Note: Using separate field declarations since parser doesn't support
    // multi-field declarations like "x, y: Integer" in class members yet.
    std::string src = "program Test;\n"
                      "type\n"
                      "  TPoint = class\n"
                      "    public\n"
                      "      x: Integer;\n"
                      "      y: Integer;\n"
                      "  end;\n"
                      "begin\n"
                      "end.";

    Lexer lexer(src, 0, diag);
    Parser parser(lexer, diag);
    auto prog = parser.parseProgram();
    ASSERT_NE(prog, nullptr);

    SemanticAnalyzer analyzer(diag);
    bool result = analyzer.analyze(*prog);
    EXPECT_TRUE(result);

    const ClassInfo *classInfo = analyzer.lookupClass("TPoint");
    ASSERT_NE(classInfo, nullptr);
    EXPECT_EQ(classInfo->name, "TPoint");
    EXPECT_EQ(classInfo->fields.size(), 2u);
}

TEST(PascalClassTest, InterfaceLookup)
{
    DiagnosticEngine diag;

    std::string src = "program Test;\n"
                      "type\n"
                      "  IRunnable = interface\n"
                      "    procedure Run;\n"
                      "  end;\n"
                      "begin\n"
                      "end.";

    Lexer lexer(src, 0, diag);
    Parser parser(lexer, diag);
    auto prog = parser.parseProgram();
    ASSERT_NE(prog, nullptr);

    SemanticAnalyzer analyzer(diag);
    bool result = analyzer.analyze(*prog);
    EXPECT_TRUE(result);

    const InterfaceInfo *ifaceInfo = analyzer.lookupInterface("IRunnable");
    ASSERT_NE(ifaceInfo, nullptr);
    EXPECT_EQ(ifaceInfo->name, "IRunnable");
    EXPECT_EQ(ifaceInfo->methods.size(), 1u);
}

//===----------------------------------------------------------------------===//
// Inherited Method Implementation Tests
//===----------------------------------------------------------------------===//

TEST(PascalClassTest, InterfaceImplementedByBase)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  IFoo = interface\n"
                                 "    procedure DoFoo;\n"
                                 "  end;\n"
                                 "  TBase = class\n"
                                 "    public\n"
                                 "      procedure DoFoo;\n"
                                 "  end;\n"
                                 "  TChild = class(TBase, IFoo)\n" // DoFoo inherited from TBase
                                 "    public\n"
                                 "      x: Integer;\n"
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Self Identifier Tests
//===----------------------------------------------------------------------===//

TEST(PascalClassTest, SelfInMethod)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TCounter = class\n"
                                 "    public\n"
                                 "      Value: Integer;\n"
                                 "      procedure Increment;\n"
                                 "  end;\n"
                                 "procedure TCounter.Increment;\n"
                                 "begin\n"
                                 "  Self.Value := Self.Value + 1\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalClassTest, SelfOutsideMethodError)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var x: Integer;\n"
                                 "begin\n"
                                 "  x := Self.Value\n" // Error: Self outside method
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalClassTest, SelfInConstructor)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TPoint = class\n"
                                 "    public\n"
                                 "      X: Integer;\n"
                                 "      Y: Integer;\n"
                                 "      constructor Create(aX: Integer; aY: Integer);\n"
                                 "  end;\n"
                                 "constructor TPoint.Create(aX: Integer; aY: Integer);\n"
                                 "begin\n"
                                 "  Self.X := aX;\n"
                                 "  Self.Y := aY\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Method Implementation Syntax Tests
//===----------------------------------------------------------------------===//

TEST(PascalClassTest, MethodImplementationSyntax)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TFoo = class\n"
                                 "    public\n"
                                 "      procedure DoSomething;\n"
                                 "      function GetValue: Integer;\n"
                                 "  end;\n"
                                 "procedure TFoo.DoSomething;\n"
                                 "begin\n"
                                 "end;\n"
                                 "function TFoo.GetValue: Integer;\n"
                                 "begin\n"
                                 "  Result := 42\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Inherited Statement Tests
//===----------------------------------------------------------------------===//

TEST(PascalClassTest, InheritedInOverride)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TBase = class\n"
                                 "    public\n"
                                 "      procedure DoWork; virtual;\n"
                                 "  end;\n"
                                 "  TChild = class(TBase)\n"
                                 "    public\n"
                                 "      procedure DoWork; override;\n"
                                 "  end;\n"
                                 "procedure TBase.DoWork;\n"
                                 "begin\n"
                                 "end;\n"
                                 "procedure TChild.DoWork;\n"
                                 "begin\n"
                                 "  inherited\n" // Call base DoWork
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalClassTest, InheritedOutsideMethodError)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "begin\n"
                                 "  inherited\n" // Error: inherited outside method
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalClassTest, InheritedNoBaseClassError)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TRoot = class\n"
                                 "    public\n"
                                 "      procedure DoWork;\n"
                                 "  end;\n"
                                 "procedure TRoot.DoWork;\n"
                                 "begin\n"
                                 "  inherited\n" // Error: TRoot has no base class
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Result Variable Tests
//===----------------------------------------------------------------------===//

TEST(PascalClassTest, ResultInFunction)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "function Double(x: Integer): Integer;\n"
                                 "begin\n"
                                 "  Result := x * 2\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalClassTest, FunctionNameAssignmentError)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "function Double(x: Integer): Integer;\n"
                                 "begin\n"
                                 "  Double := x * 2\n" // Error: cannot assign to function name
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalClassTest, ResultInMethodFunction)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TCalc = class\n"
                                 "    public\n"
                                 "      function Add(a: Integer; b: Integer): Integer;\n"
                                 "  end;\n"
                                 "function TCalc.Add(a: Integer; b: Integer): Integer;\n"
                                 "begin\n"
                                 "  Result := a + b\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Multiple Interface Implementation Tests
//===----------------------------------------------------------------------===//

TEST(PascalClassTest, MultipleInterfacesValid)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  IDrawable = interface\n"
                                 "    procedure Draw;\n"
                                 "  end;\n"
                                 "  ISerializable = interface\n"
                                 "    function ToJson: String;\n"
                                 "  end;\n"
                                 "  TButton = class(IDrawable, ISerializable)\n"
                                 "    public\n"
                                 "      Label: String;\n"
                                 "      procedure Draw;\n"
                                 "      function ToJson: String;\n"
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalClassTest, BaseClassAndMultipleInterfaces)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  IClickable = interface\n"
                                 "    procedure OnClick;\n"
                                 "  end;\n"
                                 "  IAnimatable = interface\n"
                                 "    procedure Animate;\n"
                                 "  end;\n"
                                 "  TButton = class\n"
                                 "    public\n"
                                 "      procedure OnClick;\n"
                                 "      procedure Animate;\n"
                                 "  end;\n"
                                 "  TFancyButton = class(TButton, IClickable, IAnimatable)\n"
                                 "    public\n"
                                 "      FancyEffect: String;\n"
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalClassTest, InterfaceInheritance)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  IBase = interface\n"
                                 "    procedure BaseMethod;\n"
                                 "  end;\n"
                                 "  IDerived = interface(IBase)\n"
                                 "    procedure DerivedMethod;\n"
                                 "  end;\n"
                                 "  TImpl = class(IDerived)\n"
                                 "    public\n"
                                 "      procedure BaseMethod;\n"
                                 "      procedure DerivedMethod;\n"
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalClassTest, InterfaceNotFullyImplemented)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  IDrawable = interface\n"
                                 "    procedure Draw;\n"
                                 "  end;\n"
                                 "  ISerializable = interface\n"
                                 "    function ToJson: String;\n"
                                 "  end;\n"
                                 "  TBad = class(IDrawable, ISerializable)\n"
                                 "    public\n"
                                 "      procedure Draw;\n" // Missing ToJson
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Weak Reference Extended Tests
//===----------------------------------------------------------------------===//

TEST(PascalClassTest, WeakFieldArrayError)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TBad = class\n"
                                 "    public\n"
                                 "      weak Items: array of Integer;\n" // Error: weak on array
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalClassTest, DoublyLinkedListWithWeak)
{
    // Test that a doubly-linked list structure with weak references compiles
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TNode = class\n"
                                 "    public\n"
                                 "      Value: Integer;\n"
                                 "      Next: TNode;\n"      // strong reference
                                 "      weak Prev: TNode;\n" // weak reference (prevents cycle)
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalClassTest, WeakInterfaceReference)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  IEventHandler = interface\n"
                                 "    procedure HandleEvent;\n"
                                 "  end;\n"
                                 "  TPublisher = class\n"
                                 "    public\n"
                                 "      weak Handler: IEventHandler;\n" // weak on interface is OK
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalClassTest, WeakOptionalInterfaceReference)
{
    DiagnosticEngine diag;
    bool result =
        analyzeProgram("program Test;\n"
                       "type\n"
                       "  IObserver = interface\n"
                       "    procedure Update;\n"
                       "  end;\n"
                       "  TSubject = class\n"
                       "    public\n"
                       "      weak OptionalObserver: IObserver?;\n" // weak on optional interface
                       "  end;\n"
                       "begin\n"
                       "end.",
                       diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Interface Function Return Type Tests
//===----------------------------------------------------------------------===//

TEST(PascalClassTest, InterfaceWithFunctionReturningClass)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TData = class\n"
                                 "    public\n"
                                 "      Value: Integer;\n"
                                 "  end;\n"
                                 "  IFactory = interface\n"
                                 "    function Create: TData;\n"
                                 "  end;\n"
                                 "  TDataFactory = class(IFactory)\n"
                                 "    public\n"
                                 "      function Create: TData;\n"
                                 "  end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Interface Assignment and Polymorphism Tests
//===----------------------------------------------------------------------===//

TEST(PascalClassTest, ClassToInterfaceAssignment)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  IDrawable = interface\n"
                                 "    procedure Draw;\n"
                                 "  end;\n"
                                 "  TButton = class(IDrawable)\n"
                                 "    public\n"
                                 "      procedure Draw;\n"
                                 "  end;\n"
                                 "var\n"
                                 "  drawable: IDrawable;\n"
                                 "  button: TButton;\n"
                                 "begin\n"
                                 "  drawable := button\n" // Class to interface assignment
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalClassTest, ClassToNonImplementedInterfaceError)
{
    DiagnosticEngine diag;
    bool result =
        analyzeProgram("program Test;\n"
                       "type\n"
                       "  IDrawable = interface\n"
                       "    procedure Draw;\n"
                       "  end;\n"
                       "  ISerializable = interface\n"
                       "    function ToJson: String;\n"
                       "  end;\n"
                       "  TButton = class(IDrawable)\n"
                       "    public\n"
                       "      procedure Draw;\n"
                       "  end;\n"
                       "var\n"
                       "  serial: ISerializable;\n"
                       "  button: TButton;\n"
                       "begin\n"
                       "  serial := button\n" // Error: TButton doesn't implement ISerializable
                       "end.",
                       diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalClassTest, ClassToBaseClassAssignment)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TBase = class\n"
                                 "    public\n"
                                 "      x: Integer;\n"
                                 "  end;\n"
                                 "  TDerived = class(TBase)\n"
                                 "    public\n"
                                 "      y: Integer;\n"
                                 "  end;\n"
                                 "var\n"
                                 "  base: TBase;\n"
                                 "  derived: TDerived;\n"
                                 "begin\n"
                                 "  base := derived\n" // Derived to base assignment
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalClassTest, InheritedInterfaceAssignment)
{
    // Test that a class implementing a derived interface can be assigned to base interface var
    DiagnosticEngine diag;
    bool result =
        analyzeProgram("program Test;\n"
                       "type\n"
                       "  IBase = interface\n"
                       "    procedure BaseMethod;\n"
                       "  end;\n"
                       "  IDerived = interface(IBase)\n"
                       "    procedure DerivedMethod;\n"
                       "  end;\n"
                       "  TImpl = class(IDerived)\n"
                       "    public\n"
                       "      procedure BaseMethod;\n"
                       "      procedure DerivedMethod;\n"
                       "  end;\n"
                       "var\n"
                       "  baseRef: IBase;\n"
                       "  impl: TImpl;\n"
                       "begin\n"
                       "  baseRef := impl\n" // TImpl implements IDerived which extends IBase
                       "end.",
                       diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalClassTest, InterfaceToInterfaceAssignment)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  IBase = interface\n"
                                 "    procedure BaseMethod;\n"
                                 "  end;\n"
                                 "  IDerived = interface(IBase)\n"
                                 "    procedure DerivedMethod;\n"
                                 "  end;\n"
                                 "  TImpl = class(IDerived)\n"
                                 "    public\n"
                                 "      procedure BaseMethod;\n"
                                 "      procedure DerivedMethod;\n"
                                 "  end;\n"
                                 "var\n"
                                 "  baseRef: IBase;\n"
                                 "  derivedRef: IDerived;\n"
                                 "begin\n"
                                 "  baseRef := derivedRef\n" // Derived interface to base interface
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalClassTest, InterfaceToUnrelatedInterfaceError)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  IFoo = interface\n"
                                 "    procedure Foo;\n"
                                 "  end;\n"
                                 "  IBar = interface\n"
                                 "    procedure Bar;\n"
                                 "  end;\n"
                                 "var\n"
                                 "  foo: IFoo;\n"
                                 "  bar: IBar;\n"
                                 "begin\n"
                                 "  foo := bar\n" // Error: unrelated interfaces
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
