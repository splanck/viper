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
// Inheritance and Override Tests
//===----------------------------------------------------------------------===//

TEST(PascalClassTest, VirtualMethodOverrideValid)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
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
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  TBase = class\n"
        "    public\n"
        "      procedure Foo;\n"  // Not virtual
        "  end;\n"
        "  TChild = class(TBase)\n"
        "    public\n"
        "      procedure Bar; override;\n"  // No base Bar at all
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
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  TBase = class\n"
        "    public\n"
        "      procedure Foo(x: Integer); virtual;\n"
        "  end;\n"
        "  TChild = class(TBase)\n"
        "    public\n"
        "      procedure Foo(x: String); override;\n"  // Wrong signature
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
    bool result = analyzeProgram(
        "program Test;\n"
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
    bool result = analyzeProgram(
        "program Test;\n"
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
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  IFoo = interface\n"
        "    procedure DoFoo;\n"
        "  end;\n"
        "  TBad = class(IFoo)\n"
        "    public\n"
        "      procedure DoBar;\n"  // Wrong method name
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
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  IFoo = interface\n"
        "    procedure DoFoo(x: Integer);\n"
        "  end;\n"
        "  TBad = class(IFoo)\n"
        "    public\n"
        "      procedure DoFoo;\n"  // Wrong signature
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
    bool result = analyzeProgram(
        "program Test;\n"
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
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  TA = class\n"
        "    public\n"
        "      x: Integer;\n"
        "  end;\n"
        "  TB = class\n"
        "    public\n"
        "      y: Integer;\n"
        "  end;\n"
        "  TBad = class(TA, TB)\n"  // TB is a class, not an interface
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
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  IFoo = interface\n"
        "    procedure DoFoo;\n"
        "  end;\n"
        "  TBase = class\n"
        "    public\n"
        "      x: Integer;\n"
        "  end;\n"
        "  TChild = class(TBase, IFoo)\n"  // OK: one class, one interface
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
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  TNode = class\n"
        "    public\n"
        "      weak Prev: TNode;\n"  // OK: weak on class type
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
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  TBad = class\n"
        "    public\n"
        "      weak Count: Integer;\n"  // Error: weak on value type
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
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  TBad = class\n"
        "    public\n"
        "      weak Name: String;\n"  // Error: weak on string
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
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  TNode = class\n"
        "    public\n"
        "      weak Parent: TNode?;\n"  // OK: weak on optional class
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
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  IObserver = interface\n"
        "    procedure Notify;\n"
        "  end;\n"
        "  TSubject = class\n"
        "    public\n"
        "      weak Observer: IObserver;\n"  // OK: weak on interface
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
    bool result = analyzeProgram(
        "program Test;\n"
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
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  TFoo = class\n"
        "    public\n"
        "      destructor Free;\n"  // Error: must be named Destroy
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
    std::string src =
        "program Test;\n"
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

    std::string src =
        "program Test;\n"
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
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  IFoo = interface\n"
        "    procedure DoFoo;\n"
        "  end;\n"
        "  TBase = class\n"
        "    public\n"
        "      procedure DoFoo;\n"
        "  end;\n"
        "  TChild = class(TBase, IFoo)\n"  // DoFoo inherited from TBase
        "    public\n"
        "      x: Integer;\n"
        "  end;\n"
        "begin\n"
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
