//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/frontends/pascal/SemanticShadowTests.cpp
// Purpose: Unit tests for builtin name shadowing in Viper Pascal.
// Key invariants: Tests that user-defined identifiers correctly shadow
//                 builtin constants and functions (e, pi, pos, etc.).
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
// Variable Shadowing Tests
//===----------------------------------------------------------------------===//

TEST(PascalShadowTest, LocalVarShadowsE)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var\n"
                                 "  e: Real;\n" // shadows Euler's constant
                                 "begin\n"
                                 "  e := 5.0;\n"
                                 "  WriteLn(e);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalShadowTest, LocalVarShadowsPi)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var\n"
                                 "  pi: Real;\n" // shadows Pi constant
                                 "begin\n"
                                 "  pi := 3.0;\n"
                                 "  WriteLn(pi);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalShadowTest, LocalVarShadowsPos)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var\n"
                                 "  pos: Integer;\n" // shadows Pos function
                                 "begin\n"
                                 "  pos := 10;\n"
                                 "  WriteLn(pos);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Parameter Shadowing Tests
//===----------------------------------------------------------------------===//

TEST(PascalShadowTest, ParameterShadowsE)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "procedure UseE(e: Real);\n"
                                 "begin\n"
                                 "  WriteLn(e);\n"
                                 "end;\n"
                                 "begin\n"
                                 "  UseE(5.0);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalShadowTest, ParameterShadowsPi)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "function DoublePi(pi: Real): Real;\n"
                                 "begin\n"
                                 "  Result := pi * 2;\n"
                                 "end;\n"
                                 "begin\n"
                                 "  WriteLn(DoublePi(3.0));\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Class Field Shadowing Tests
//===----------------------------------------------------------------------===//

TEST(PascalShadowTest, ClassFieldShadowsE)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TMyClass = class\n"
                                 "  public\n"
                                 "    e: Real;\n" // field shadows Euler's constant
                                 "    procedure ShowE;\n"
                                 "  end;\n"
                                 "procedure TMyClass.ShowE;\n"
                                 "begin\n"
                                 "  e := 42.0;\n" // should refer to field
                                 "  WriteLn(e);\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalShadowTest, ClassFieldShadowsPi)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TCircle = class\n"
                                 "  public\n"
                                 "    pi: Real;\n" // field shadows Pi constant
                                 "    procedure SetPi;\n"
                                 "  end;\n"
                                 "procedure TCircle.SetPi;\n"
                                 "begin\n"
                                 "  pi := 3.14;\n" // should refer to field
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Record Field Shadowing Tests
//===----------------------------------------------------------------------===//

TEST(PascalShadowTest, RecordFieldShadowsE)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TRec = record\n"
                                 "    e: Real;\n" // field shadows Euler's constant
                                 "  end;\n"
                                 "var\n"
                                 "  r: TRec;\n"
                                 "begin\n"
                                 "  r.e := 2.5;\n"
                                 "  WriteLn(r.e);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// With Statement Shadowing Tests
//===----------------------------------------------------------------------===//

TEST(PascalShadowTest, WithContextFieldShadowsE)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TRec = record\n"
                                 "    e: Real;\n" // field shadows Euler's constant in with context
                                 "  end;\n"
                                 "var\n"
                                 "  r: TRec;\n"
                                 "begin\n"
                                 "  with r do\n"
                                 "  begin\n"
                                 "    e := 99.0;\n" // should refer to record field r.e
                                 "  end;\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Constants Shadowing Tests
//===----------------------------------------------------------------------===//

TEST(PascalShadowTest, UserConstantShadowsE)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "const\n"
                                 "  e = 100;\n" // user constant shadows Euler's constant
                                 "begin\n"
                                 "  WriteLn(e);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalShadowTest, UserConstantShadowsPi)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "const\n"
                                 "  pi = 3;\n" // user constant shadows Pi constant
                                 "begin\n"
                                 "  WriteLn(pi);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Local Variable Takes Precedence Over All Tests
//===----------------------------------------------------------------------===//

TEST(PascalShadowTest, LocalTakesPrecedenceOverFieldWithSameName)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type\n"
                                 "  TMyClass = class\n"
                                 "  public\n"
                                 "    e: Integer;\n"
                                 "    procedure TestShadow;\n"
                                 "  end;\n"
                                 "procedure TMyClass.TestShadow;\n"
                                 "var\n"
                                 "  e: Real;\n" // local shadows field AND builtin
                                 "begin\n"
                                 "  e := 5.5;\n" // should refer to local var, not field
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

} // anonymous namespace

int main()
{
    return viper_test::run_all_tests();
}
