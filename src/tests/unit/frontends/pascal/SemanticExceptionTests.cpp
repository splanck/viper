//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/frontends/pascal/SemanticExceptionTests.cpp
// Purpose: Unit tests for Pascal exception handling semantics.
// Key invariants: Tests Exception class, typed handlers, raise, except...else.
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
// Exception Class Tests
//===----------------------------------------------------------------------===//

TEST(PascalExceptionTest, ExceptionIsBuiltIn)
{
    // Exception should be available as a built-in type
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "var e: Exception;\n"
        "begin\n"
        "end.",
        diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalExceptionTest, CannotRedefineException)
{
    // User code cannot redefine Exception
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  Exception = class\n"
        "    public\n"
        "      Code: Integer;\n"
        "  end;\n"
        "begin\n"
        "end.",
        diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalExceptionTest, CustomExceptionDerivesFromException)
{
    // Custom exceptions can derive from Exception
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  EInvalidInput = class(Exception)\n"
        "    public\n"
        "      InputValue: String;\n"
        "  end;\n"
        "begin\n"
        "end.",
        diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Typed Handler Tests
//===----------------------------------------------------------------------===//

TEST(PascalExceptionTest, TypedHandlerValid)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  EInvalidInput = class(Exception)\n"
        "    public\n"
        "      InputValue: String;\n"
        "  end;\n"
        "begin\n"
        "  try\n"
        "    WriteLn('test');\n"
        "  except\n"
        "    on E: EInvalidInput do\n"
        "      WriteLn('Bad input: ', E.InputValue);\n"
        "    on E: Exception do\n"
        "      WriteLn('Unknown error: ', E.Message);\n"
        "  end;\n"
        "end.",
        diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalExceptionTest, TypedHandlerWithoutVariable)
{
    // Handler can omit the variable name
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "begin\n"
        "  try\n"
        "    WriteLn('test');\n"
        "  except\n"
        "    on Exception do\n"
        "      WriteLn('Error occurred');\n"
        "  end;\n"
        "end.",
        diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalExceptionTest, HandlerNonExceptionTypeError)
{
    // Handler type must derive from Exception
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  TNotAnException = class\n"
        "    public\n"
        "      x: Integer;\n"
        "  end;\n"
        "begin\n"
        "  try\n"
        "    WriteLn('test');\n"
        "  except\n"
        "    on E: TNotAnException do\n"  // Error: not an Exception subclass
        "      WriteLn('Error');\n"
        "  end;\n"
        "end.",
        diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalExceptionTest, HandlerNonClassTypeError)
{
    // Handler type must be a class
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "begin\n"
        "  try\n"
        "    WriteLn('test');\n"
        "  except\n"
        "    on E: Integer do\n"  // Error: not a class
        "      WriteLn('Error');\n"
        "  end;\n"
        "end.",
        diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalExceptionTest, HandlerUnknownTypeError)
{
    // Handler type must exist
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "begin\n"
        "  try\n"
        "    WriteLn('test');\n"
        "  except\n"
        "    on E: EUnknownType do\n"  // Error: undefined type
        "      WriteLn('Error');\n"
        "  end;\n"
        "end.",
        diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalExceptionTest, ExceptionVariableInScope)
{
    // Exception variable should be accessible in handler body
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "var msg: String;\n"
        "begin\n"
        "  try\n"
        "    WriteLn('test');\n"
        "  except\n"
        "    on E: Exception do\n"
        "      msg := E.Message;\n"  // E.Message should be valid
        "  end;\n"
        "end.",
        diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Except...Else Tests
//===----------------------------------------------------------------------===//

TEST(PascalExceptionTest, ExceptElseRejected)
{
    // except...else is not supported in v0.1
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "begin\n"
        "  try\n"
        "    WriteLn('test');\n"
        "  except\n"
        "    on E: Exception do\n"
        "      WriteLn('Caught');\n"
        "  else\n"
        "    WriteLn('Else branch');\n"
        "  end;\n"
        "end.",
        diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Raise Statement Tests
//===----------------------------------------------------------------------===//

TEST(PascalExceptionTest, RaiseExceptionValid)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  EMyError = class(Exception)\n"
        "  end;\n"
        "var e: EMyError;\n"
        "begin\n"
        "  raise e;\n"
        "end.",
        diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalExceptionTest, RaiseNonExceptionError)
{
    // raise must have an Exception subclass
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  TNotException = class\n"
        "    public\n"
        "      x: Integer;\n"
        "  end;\n"
        "var obj: TNotException;\n"
        "begin\n"
        "  raise obj;\n"  // Error: not an Exception
        "end.",
        diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalExceptionTest, RaiseNonClassError)
{
    // raise must have a class type
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "var x: Integer;\n"
        "begin\n"
        "  raise x;\n"  // Error: not a class
        "end.",
        diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalExceptionTest, ReraiseInExceptHandler)
{
    // raise; (re-raise) is valid inside except handler
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "begin\n"
        "  try\n"
        "    WriteLn('test');\n"
        "  except\n"
        "    on E: Exception do\n"
        "    begin\n"
        "      WriteLn('Caught: ', E.Message);\n"
        "      raise;\n"  // Re-raise the exception
        "    end;\n"
        "  end;\n"
        "end.",
        diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalExceptionTest, ReraiseOutsideExceptError)
{
    // raise; (re-raise) is not valid outside except handler
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "begin\n"
        "  raise;\n"  // Error: not in except handler
        "end.",
        diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalExceptionTest, ReraiseInFinallyError)
{
    // raise; is not valid in finally block (not an except handler)
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "begin\n"
        "  try\n"
        "    WriteLn('test');\n"
        "  finally\n"
        "    raise;\n"  // Error: not in except handler
        "  end;\n"
        "end.",
        diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalExceptionTest, ReraiseInNestedTryExcept)
{
    // raise; works in nested except handlers
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "begin\n"
        "  try\n"
        "    try\n"
        "      WriteLn('inner');\n"
        "    except\n"
        "      on E: Exception do\n"
        "        raise;\n"  // OK: re-raise in inner handler
        "    end;\n"
        "  except\n"
        "    on E: Exception do\n"
        "      WriteLn('outer caught');\n"
        "  end;\n"
        "end.",
        diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Try-Finally Tests
//===----------------------------------------------------------------------===//

TEST(PascalExceptionTest, TryFinallyValid)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "begin\n"
        "  try\n"
        "    WriteLn('try');\n"
        "  finally\n"
        "    WriteLn('finally');\n"
        "  end;\n"
        "end.",
        diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Multiple Handler Order Tests
//===----------------------------------------------------------------------===//

TEST(PascalExceptionTest, MultipleHandlersInOrder)
{
    // Multiple handlers should be checked in order
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  EInvalidInput = class(Exception)\n"
        "  end;\n"
        "  EOverflow = class(Exception)\n"
        "  end;\n"
        "begin\n"
        "  try\n"
        "    WriteLn('test');\n"
        "  except\n"
        "    on E: EInvalidInput do\n"
        "      WriteLn('Invalid input');\n"
        "    on E: EOverflow do\n"
        "      WriteLn('Overflow');\n"
        "    on E: Exception do\n"  // Catch-all last
        "      WriteLn('Other error');\n"
        "  end;\n"
        "end.",
        diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalExceptionTest, DeepExceptionHierarchy)
{
    // Test deep exception class hierarchy
    DiagnosticEngine diag;
    bool result = analyzeProgram(
        "program Test;\n"
        "type\n"
        "  EBase = class(Exception)\n"
        "  end;\n"
        "  EDerived = class(EBase)\n"
        "  end;\n"
        "  EMoreDerived = class(EDerived)\n"
        "  end;\n"
        "var e: EMoreDerived;\n"
        "begin\n"
        "  try\n"
        "    raise e;\n"
        "  except\n"
        "    on E: EMoreDerived do\n"
        "      WriteLn('Most specific');\n"
        "    on E: EDerived do\n"
        "      WriteLn('Less specific');\n"
        "    on E: Exception do\n"
        "      WriteLn('Catch-all');\n"
        "  end;\n"
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
