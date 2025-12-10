//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/frontends/pascal/SemanticDefaultParamTests.cpp
// Purpose: Unit tests for default parameter values and call-only statements.
// Key invariants: Default params must be trailing, compile-time constants;
//                 bare designators as statements must be calls.
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
// Default Parameter Tests - Basic Usage
//===----------------------------------------------------------------------===//

TEST(PascalDefaultParamTest, ProcedureWithDefaultParam)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "procedure Log(msg: String; level: Integer = 0);\n"
                                 "begin\n"
                                 "end;\n"
                                 "begin\n"
                                 "  Log('Hello');\n"    // Omit level, uses default
                                 "  Log('Hello', 1);\n" // Provide level explicitly
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalDefaultParamTest, FunctionWithDefaultParam)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "function RepeatStr(s: String; times: Integer = 1): String;\n"
                                 "begin\n"
                                 "  Result := s;\n"
                                 "end;\n"
                                 "var x: String;\n"
                                 "begin\n"
                                 "  x := RepeatStr('Hi');\n"    // Omit times
                                 "  x := RepeatStr('Hi', 3);\n" // Provide times
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalDefaultParamTest, MultipleDefaultParams)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "procedure Config(host: String; port: Integer = 80; timeout: Integer = 30);\n"
        "begin\n"
        "end;\n"
        "begin\n"
        "  Config('localhost');\n"           // Only required param
        "  Config('localhost', 8080);\n"     // Required + one optional
        "  Config('localhost', 8080, 60);\n" // All params
        "end.",
        diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalDefaultParamTest, AllParamsHaveDefaults)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "procedure DoWork(x: Integer = 1; y: Integer = 2);\n"
                                 "begin\n"
                                 "end;\n"
                                 "begin\n"
                                 "  DoWork;\n"         // Call with no args
                                 "  DoWork(10);\n"     // Provide first
                                 "  DoWork(10, 20);\n" // Provide both
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Default Parameter Tests - Constant Expressions
//===----------------------------------------------------------------------===//

TEST(PascalDefaultParamTest, LiteralDefaultValues)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "procedure TestInts(a: Integer = 42);\n"
                                 "begin\n"
                                 "end;\n"
                                 "procedure TestReals(a: Real = 3.14);\n"
                                 "begin\n"
                                 "end;\n"
                                 "procedure TestStrings(a: String = 'hello');\n"
                                 "begin\n"
                                 "end;\n"
                                 "procedure TestBools(a: Boolean = True);\n"
                                 "begin\n"
                                 "end;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalDefaultParamTest, ConstantExpressionDefault)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "const DefaultPort = 80;\n"
                                 "procedure Connect(port: Integer = DefaultPort);\n"
                                 "begin\n"
                                 "end;\n"
                                 "begin\n"
                                 "  Connect;\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalDefaultParamTest, NegativeNumberDefault)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "procedure Adjust(offset: Integer = -1);\n"
                                 "begin\n"
                                 "end;\n"
                                 "begin\n"
                                 "  Adjust;\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Default Parameter Tests - Error Cases
//===----------------------------------------------------------------------===//

TEST(PascalDefaultParamTest, NonTrailingDefaultRejected)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "procedure Bad(x: Integer = 0; y: Integer);\n" // y has no default after x does
        "begin\n"
        "end;\n"
        "begin\n"
        "end.",
        diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalDefaultParamTest, TooFewArgumentsRejected)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "procedure NeedsArgs(a: Integer; b: Integer = 0);\n"
                                 "begin\n"
                                 "end;\n"
                                 "begin\n"
                                 "  NeedsArgs;\n" // Missing required 'a'
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalDefaultParamTest, TooManyArgumentsRejected)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "procedure TakesTwo(a: Integer; b: Integer = 0);\n"
                                 "begin\n"
                                 "end;\n"
                                 "begin\n"
                                 "  TakesTwo(1, 2, 3);\n" // Too many args
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalDefaultParamTest, TypeMismatchInDefaultRejected)
{
    DiagnosticEngine diag;
    bool result =
        analyzeProgram("program Test;\n"
                       "procedure Bad(x: Integer = 'hello');\n" // String for Integer param
                       "begin\n"
                       "end;\n"
                       "begin\n"
                       "end.",
                       diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Bare Designator Statement Tests
//===----------------------------------------------------------------------===//

TEST(PascalCallOnlyTest, BareVariableAsStatementRejected)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var x: Integer;\n"
                                 "begin\n"
                                 "  x := 5;\n"
                                 "  x;\n" // Bare variable - should be rejected
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalCallOnlyTest, ProcedureCallAllowed)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "procedure DoWork;\n"
                                 "begin\n"
                                 "end;\n"
                                 "begin\n"
                                 "  DoWork;\n" // This is a valid procedure call
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalCallOnlyTest, FunctionCallAsStatementAllowed)
{
    DiagnosticEngine diag;
    bool result =
        analyzeProgram("program Test;\n"
                       "function GetValue: Integer;\n"
                       "begin\n"
                       "  Result := 42;\n"
                       "end;\n"
                       "begin\n"
                       "  GetValue;\n" // Function call as statement (ignoring return value)
                       "end.",
                       diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalCallOnlyTest, WriteLnCallAllowed)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "begin\n"
                                 "  WriteLn('Hello');\n" // Builtin procedure call
                                 "  WriteLn;\n"          // WriteLn with no args
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalCallOnlyTest, MethodCallAllowed)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type TObj = class\n"
                                 "public\n"
                                 "  procedure DoWork;\n"
                                 "end;\n"
                                 "procedure TObj.DoWork;\n"
                                 "begin\n"
                                 "end;\n"
                                 "var obj: TObj;\n"
                                 "begin\n"
                                 "  obj := TObj.Create;\n"
                                 "  obj.DoWork;\n" // Method call
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Combined Tests
//===----------------------------------------------------------------------===//

TEST(PascalDefaultParamTest, DefaultParamWithMethodCall)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type TLogger = class\n"
                                 "public\n"
                                 "  procedure Log(msg: String; level: Integer = 0);\n"
                                 "end;\n"
                                 "procedure TLogger.Log(msg: String; level: Integer = 0);\n"
                                 "begin\n"
                                 "end;\n"
                                 "var logger: TLogger;\n"
                                 "begin\n"
                                 "  logger := TLogger.Create;\n"
                                 "  logger.Log('test');\n"    // Use default
                                 "  logger.Log('test', 1);\n" // Provide level
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
