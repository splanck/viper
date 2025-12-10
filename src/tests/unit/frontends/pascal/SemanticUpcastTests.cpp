//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/frontends/pascal/SemanticUpcastTests.cpp
// Purpose: Unit tests for implicit upcasting from derived to base class types.
// Key invariants: Tests that derived classes can be assigned to base class
//                 variables, passed as base class parameters, and returned
//                 as base class results without explicit casts.
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
// Implicit Upcast - Assignment Tests
//===----------------------------------------------------------------------===//

TEST(PascalUpcastTest, DerivedToBaseAssignment)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TBase = class end;\n"
                                 "  TDerived = class(TBase) end;\n"
                                 "var\n"
                                 "  d: TDerived;\n"
                                 "  b: TBase;\n"
                                 "begin\n"
                                 "  d := TDerived.Create;\n"
                                 "  b := d;\n" // implicit upcast
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalUpcastTest, GrandchildToGrandparentAssignment)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TGrandparent = class end;\n"
                                 "  TParent = class(TGrandparent) end;\n"
                                 "  TChild = class(TParent) end;\n"
                                 "var\n"
                                 "  c: TChild;\n"
                                 "  gp: TGrandparent;\n"
                                 "begin\n"
                                 "  c := TChild.Create;\n"
                                 "  gp := c;\n" // implicit upcast through two levels
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Implicit Upcast - Parameter Tests
//===----------------------------------------------------------------------===//

TEST(PascalUpcastTest, DerivedAsBaseParameter)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TAnimal = class\n"
                                 "  public\n"
                                 "    procedure Speak; virtual;\n"
                                 "  end;\n"
                                 "  TDog = class(TAnimal)\n"
                                 "  public\n"
                                 "    procedure Speak; override;\n"
                                 "  end;\n"
                                 "procedure TAnimal.Speak; begin WriteLn('Animal'); end;\n"
                                 "procedure TDog.Speak; begin WriteLn('Dog'); end;\n"
                                 "procedure MakeSpeak(a: TAnimal);\n"
                                 "begin\n"
                                 "  a.Speak;\n"
                                 "end;\n"
                                 "var\n"
                                 "  d: TDog;\n"
                                 "begin\n"
                                 "  d := TDog.Create;\n"
                                 "  MakeSpeak(d);\n" // implicit upcast on parameter
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalUpcastTest, MultipleUpcastParameters)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TBase = class end;\n"
                                 "  TDerived1 = class(TBase) end;\n"
                                 "  TDerived2 = class(TBase) end;\n"
                                 "procedure Process(a, b: TBase);\n"
                                 "begin\n"
                                 "end;\n"
                                 "var\n"
                                 "  d1: TDerived1;\n"
                                 "  d2: TDerived2;\n"
                                 "begin\n"
                                 "  d1 := TDerived1.Create;\n"
                                 "  d2 := TDerived2.Create;\n"
                                 "  Process(d1, d2);\n" // both parameters are implicit upcasts
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Implicit Upcast - Field Assignment Tests
//===----------------------------------------------------------------------===//

TEST(PascalUpcastTest, DerivedToFieldUpcast)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TAnimal = class end;\n"
                                 "  TDog = class(TAnimal) end;\n"
                                 "  TZoo = class\n"
                                 "  public\n"
                                 "    animal: TAnimal;\n"
                                 "  end;\n"
                                 "var\n"
                                 "  z: TZoo;\n"
                                 "  d: TDog;\n"
                                 "begin\n"
                                 "  z := TZoo.Create;\n"
                                 "  d := TDog.Create;\n"
                                 "  z.animal := d;\n" // implicit upcast to field
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Implicit Upcast - Function Return Tests
//===----------------------------------------------------------------------===//

TEST(PascalUpcastTest, DerivedAsBaseReturn)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TAnimal = class end;\n"
                                 "  TDog = class(TAnimal) end;\n"
                                 "function CreateAnimal: TAnimal;\n"
                                 "var\n"
                                 "  d: TDog;\n"
                                 "begin\n"
                                 "  d := TDog.Create;\n"
                                 "  Result := d;\n" // implicit upcast on return
                                 "end;\n"
                                 "var\n"
                                 "  a: TAnimal;\n"
                                 "begin\n"
                                 "  a := CreateAnimal;\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Downcast Rejection Tests (implicit downcasts should fail)
//===----------------------------------------------------------------------===//

TEST(PascalUpcastTest, DowncastAssignmentRejected)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TBase = class end;\n"
                                 "  TDerived = class(TBase) end;\n"
                                 "var\n"
                                 "  b: TBase;\n"
                                 "  d: TDerived;\n"
                                 "begin\n"
                                 "  b := TBase.Create;\n"
                                 "  d := b;\n" // implicit downcast - should fail
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalUpcastTest, DowncastParameterRejected)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TBase = class end;\n"
                                 "  TDerived = class(TBase) end;\n"
                                 "procedure NeedsDerived(d: TDerived);\n"
                                 "begin\n"
                                 "end;\n"
                                 "var\n"
                                 "  b: TBase;\n"
                                 "begin\n"
                                 "  b := TBase.Create;\n"
                                 "  NeedsDerived(b);\n" // implicit downcast - should fail
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalUpcastTest, DowncastFieldRejected)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TBase = class end;\n"
                                 "  TDerived = class(TBase) end;\n"
                                 "  THolder = class\n"
                                 "  public\n"
                                 "    derived: TDerived;\n"
                                 "  end;\n"
                                 "var\n"
                                 "  h: THolder;\n"
                                 "  b: TBase;\n"
                                 "begin\n"
                                 "  h := THolder.Create;\n"
                                 "  b := TBase.Create;\n"
                                 "  h.derived := b;\n" // implicit downcast - should fail
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalUpcastTest, DowncastReturnRejected)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TBase = class end;\n"
                                 "  TDerived = class(TBase) end;\n"
                                 "function CreateDerived: TDerived;\n"
                                 "var\n"
                                 "  b: TBase;\n"
                                 "begin\n"
                                 "  b := TBase.Create;\n"
                                 "  Result := b;\n" // implicit downcast - should fail
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Same Type Assignment (identity, not inheritance)
//===----------------------------------------------------------------------===//

TEST(PascalUpcastTest, SameTypeAssignment)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TAnimal = class end;\n"
                                 "var\n"
                                 "  a1, a2: TAnimal;\n"
                                 "begin\n"
                                 "  a1 := TAnimal.Create;\n"
                                 "  a2 := a1;\n" // same type assignment
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Sibling Class Assignment (should fail)
//===----------------------------------------------------------------------===//

TEST(PascalUpcastTest, SiblingClassAssignmentRejected)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TBase = class end;\n"
                                 "  TDerived1 = class(TBase) end;\n"
                                 "  TDerived2 = class(TBase) end;\n"
                                 "var\n"
                                 "  d1: TDerived1;\n"
                                 "  d2: TDerived2;\n"
                                 "begin\n"
                                 "  d1 := TDerived1.Create;\n"
                                 "  d2 := d1;\n" // sibling assignment - should fail
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
