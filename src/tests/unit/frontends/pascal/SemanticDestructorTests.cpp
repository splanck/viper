//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/frontends/pascal/SemanticDestructorTests.cpp
// Purpose: Unit tests for Pascal destructor semantics.
// Key invariants: Tests destructor declaration, virtual/override modifiers,
//                 and inherited calls.
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
// Destructor Declaration Tests
//===----------------------------------------------------------------------===//

TEST(PascalDestructorTest, BasicDestructor)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TMyClass = class\n"
                                 "  public\n"
                                 "    destructor Destroy;\n"
                                 "  end;\n"
                                 "destructor TMyClass.Destroy;\n"
                                 "begin\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalDestructorTest, VirtualDestructor)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TBase = class\n"
                                 "  public\n"
                                 "    destructor Destroy; virtual;\n"
                                 "  end;\n"
                                 "destructor TBase.Destroy;\n"
                                 "begin\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalDestructorTest, OverrideDestructor)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TBase = class\n"
                                 "  public\n"
                                 "    destructor Destroy; virtual;\n"
                                 "  end;\n"
                                 "  TChild = class(TBase)\n"
                                 "  public\n"
                                 "    destructor Destroy; override;\n"
                                 "  end;\n"
                                 "destructor TBase.Destroy;\n"
                                 "begin\n"
                                 "end;\n"
                                 "destructor TChild.Destroy;\n"
                                 "begin\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalDestructorTest, InheritedDestroyCall)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TBase = class\n"
                                 "  public\n"
                                 "    destructor Destroy; virtual;\n"
                                 "  end;\n"
                                 "  TChild = class(TBase)\n"
                                 "  public\n"
                                 "    destructor Destroy; override;\n"
                                 "  end;\n"
                                 "destructor TBase.Destroy;\n"
                                 "begin\n"
                                 "  WriteLn('Base destroyed');\n"
                                 "end;\n"
                                 "destructor TChild.Destroy;\n"
                                 "begin\n"
                                 "  WriteLn('Child destroyed');\n"
                                 "  inherited Destroy;\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalDestructorTest, DestructorWithLocalVars)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TMyClass = class\n"
                                 "  public\n"
                                 "    destructor Destroy;\n"
                                 "  end;\n"
                                 "destructor TMyClass.Destroy;\n"
                                 "var\n"
                                 "  i: Integer;\n"
                                 "begin\n"
                                 "  i := 42;\n"
                                 "  WriteLn(i);\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalDestructorTest, DestructorAccessesField)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TMyClass = class\n"
                                 "  private\n"
                                 "    FValue: Integer;\n"
                                 "  public\n"
                                 "    constructor Create;\n"
                                 "    destructor Destroy;\n"
                                 "  end;\n"
                                 "constructor TMyClass.Create;\n"
                                 "begin\n"
                                 "  FValue := 100;\n"
                                 "end;\n"
                                 "destructor TMyClass.Destroy;\n"
                                 "begin\n"
                                 "  WriteLn(FValue);\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Destructor Error Tests
//===----------------------------------------------------------------------===//

TEST(PascalDestructorTest, DestructorMustBeNamedDestroy)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TMyClass = class\n"
                                 "  public\n"
                                 "    destructor Finalize;\n" // Wrong name
                                 "  end;\n"
                                 "destructor TMyClass.Finalize;\n"
                                 "begin\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Destructor Chaining Tests
//===----------------------------------------------------------------------===//

TEST(PascalDestructorTest, ThreeLevelInheritanceChain)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TGrandParent = class\n"
                                 "  public\n"
                                 "    destructor Destroy; virtual;\n"
                                 "  end;\n"
                                 "  TParent = class(TGrandParent)\n"
                                 "  public\n"
                                 "    destructor Destroy; override;\n"
                                 "  end;\n"
                                 "  TChild = class(TParent)\n"
                                 "  public\n"
                                 "    destructor Destroy; override;\n"
                                 "  end;\n"
                                 "destructor TGrandParent.Destroy;\n"
                                 "begin\n"
                                 "end;\n"
                                 "destructor TParent.Destroy;\n"
                                 "begin\n"
                                 "  inherited Destroy;\n"
                                 "end;\n"
                                 "destructor TChild.Destroy;\n"
                                 "begin\n"
                                 "  inherited Destroy;\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

} // anonymous namespace

#ifndef VIPER_HAS_GTEST
int main()
{
    return RUN_ALL_TESTS();
}
#endif
