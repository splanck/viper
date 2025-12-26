//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/frontends/pascal/SemanticBasicTests.cpp
// Purpose: Unit tests for the Viper Pascal semantic analyzer.
// Key invariants: Tests type checking, name resolution, and control flow validation.
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

/// @brief Parse and analyze a program, returning the analyzer for inspection.
std::unique_ptr<SemanticAnalyzer> analyzeAndGet(const std::string &source, DiagnosticEngine &diag)
{
    Lexer lexer(source, 0, diag);
    Parser parser(lexer, diag);
    auto prog = parser.parseProgram();
    if (!prog || parser.hasError())
        return nullptr;

    auto analyzer = std::make_unique<SemanticAnalyzer>(diag);
    analyzer->analyze(*prog);
    return analyzer;
}

//===----------------------------------------------------------------------===//
// Happy Path Tests
//===----------------------------------------------------------------------===//

TEST(PascalSemanticTest, SimpleIntegerAssignment)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var a, b: Integer;\n"
                                 "begin\n"
                                 "  a := 1;\n"
                                 "  b := a + 2\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, IntegerToRealPromotion)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var x: Real;\n"
                                 "begin\n"
                                 "  x := 1 + 2.0\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, BooleanCondition)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var x: Integer;\n"
                                 "begin\n"
                                 "  if x > 0 then\n"
                                 "    x := 1\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, WhileLoop)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var x: Integer;\n"
                                 "begin\n"
                                 "  while x < 10 do\n"
                                 "    x := x + 1\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, ForLoop)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var i, sum: Integer;\n"
                                 "begin\n"
                                 "  sum := 0;\n"
                                 "  for i := 1 to 10 do\n"
                                 "    sum := sum + i\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, RepeatUntil)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var x: Integer;\n"
                                 "begin\n"
                                 "  x := 0;\n"
                                 "  repeat\n"
                                 "    x := x + 1\n"
                                 "  until x >= 10\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, BreakInsideLoop)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var x: Integer;\n"
                                 "begin\n"
                                 "  while True do begin\n"
                                 "    x := 1;\n"
                                 "    break\n"
                                 "  end\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, ContinueInsideLoop)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var i: Integer;\n"
                                 "begin\n"
                                 "  for i := 1 to 10 do begin\n"
                                 "    if i = 5 then continue;\n"
                                 "    WriteLn(i)\n"
                                 "  end\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, ProcedureCall)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "begin\n"
                                 "  WriteLn('Hello')\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, FunctionDeclaration)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "function Add(a, b: Integer): Integer;\n"
                                 "begin\n"
                                 "  Result := a + b\n"
                                 "end;\n"
                                 "begin\n"
                                 "  WriteLn(Add(1, 2))\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, ProcedureDeclaration)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "procedure PrintNumber(n: Integer);\n"
                                 "begin\n"
                                 "  WriteLn(n)\n"
                                 "end;\n"
                                 "begin\n"
                                 "  PrintNumber(42)\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, ConstDeclaration)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "const\n"
                                 "  MaxValue = 100;\n"
                                 "  Pi = 3.14159;\n"
                                 "var x: Integer;\n"
                                 "begin\n"
                                 "  x := MaxValue\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, StringOperations)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var s: String;\n"
                                 "var len: Integer;\n"
                                 "begin\n"
                                 "  s := 'Hello';\n"
                                 "  len := Length(s)\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, LogicalOperators)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var a, b, c: Boolean;\n"
                                 "begin\n"
                                 "  a := True;\n"
                                 "  b := False;\n"
                                 "  c := a and b;\n"
                                 "  c := a or b;\n"
                                 "  c := not a\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Error Detection Tests
//===----------------------------------------------------------------------===//

TEST(PascalSemanticTest, UndeclaredVariable)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "begin\n"
                                 "  x := 1\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, TypeMismatchAssignment)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var a: Integer;\n"
                                 "begin\n"
                                 "  a := 'hello'\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, NonBooleanCondition)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "begin\n"
                                 "  if 1 then\n"
                                 "    WriteLn('test')\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, NonBooleanWhileCondition)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var x: Integer;\n"
                                 "begin\n"
                                 "  while x do\n"
                                 "    x := x - 1\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, NonBooleanRepeatCondition)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var x: Integer;\n"
                                 "begin\n"
                                 "  x := 0;\n"
                                 "  repeat\n"
                                 "    x := x + 1\n"
                                 "  until x\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, BreakOutsideLoop)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "begin\n"
                                 "  break\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, ContinueOutsideLoop)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "begin\n"
                                 "  continue\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, UndefinedProcedure)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "begin\n"
                                 "  UnknownProc(1, 2, 3)\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, WrongArgumentCount)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "function Add(a, b: Integer): Integer;\n"
                                 "begin\n"
                                 "  Result := a + b\n"
                                 "end;\n"
                                 "begin\n"
                                 "  WriteLn(Add(1))\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, ArgumentTypeMismatch)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "function Square(x: Integer): Integer;\n"
                                 "begin\n"
                                 "  Result := x * x\n"
                                 "end;\n"
                                 "begin\n"
                                 "  WriteLn(Square('hello'))\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, LogicalOperatorNonBoolean)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var x: Integer;\n"
                                 "var b: Boolean;\n"
                                 "begin\n"
                                 "  b := x and True\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, NotOperatorNonBoolean)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var x: Integer;\n"
                                 "var b: Boolean;\n"
                                 "begin\n"
                                 "  b := not x\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, DivModNonInteger)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var x: Real;\n"
                                 "var r: Integer;\n"
                                 "begin\n"
                                 "  r := x div 2\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, UndefinedType)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var x: UnknownType;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Type Checking Tests
//===----------------------------------------------------------------------===//

TEST(PascalSemanticTest, TypeLookup)
{
    DiagnosticEngine diag;
    auto analyzer = analyzeAndGet("program Test;\n"
                                  "var x: Integer;\n"
                                  "var y: Real;\n"
                                  "var s: String;\n"
                                  "var b: Boolean;\n"
                                  "begin\n"
                                  "end.",
                                  diag);
    ASSERT_NE(analyzer, nullptr);

    auto intType = analyzer->lookupVariable("x");
    ASSERT_TRUE(intType.has_value());
    EXPECT_EQ(intType->kind, PasTypeKind::Integer);

    auto realType = analyzer->lookupVariable("y");
    ASSERT_TRUE(realType.has_value());
    EXPECT_EQ(realType->kind, PasTypeKind::Real);

    auto strType = analyzer->lookupVariable("s");
    ASSERT_TRUE(strType.has_value());
    EXPECT_EQ(strType->kind, PasTypeKind::String);

    auto boolType = analyzer->lookupVariable("b");
    ASSERT_TRUE(boolType.has_value());
    EXPECT_EQ(boolType->kind, PasTypeKind::Boolean);
}

TEST(PascalSemanticTest, FunctionLookup)
{
    DiagnosticEngine diag;
    auto analyzer = analyzeAndGet("program Test;\n"
                                  "function Add(a, b: Integer): Integer;\n"
                                  "begin\n"
                                  "  Result := a + b\n"
                                  "end;\n"
                                  "begin\n"
                                  "end.",
                                  diag);
    ASSERT_NE(analyzer, nullptr);

    auto sig = analyzer->lookupFunction("add");
    ASSERT_NE(sig, nullptr);
    EXPECT_EQ(sig->name, "Add");
    EXPECT_EQ(sig->params.size(), 2u);
    EXPECT_EQ(sig->returnType.kind, PasTypeKind::Integer);
}

TEST(PascalSemanticTest, ConstantLookup)
{
    DiagnosticEngine diag;
    auto analyzer = analyzeAndGet("program Test;\n"
                                  "const\n"
                                  "  Max = 100;\n"
                                  "  Pi = 3.14;\n"
                                  "begin\n"
                                  "end.",
                                  diag);
    ASSERT_NE(analyzer, nullptr);

    auto maxConst = analyzer->lookupConstant("max");
    ASSERT_TRUE(maxConst.has_value());
    EXPECT_EQ(maxConst->kind, PasTypeKind::Integer);

    auto piConst = analyzer->lookupConstant("pi");
    ASSERT_TRUE(piConst.has_value());
    EXPECT_EQ(piConst->kind, PasTypeKind::Real);
}

//===----------------------------------------------------------------------===//
// PasType Helper Tests
//===----------------------------------------------------------------------===//

TEST(PasTypeTest, IsNumeric)
{
    EXPECT_TRUE(PasType::integer().isNumeric());
    EXPECT_TRUE(PasType::real().isNumeric());
    EXPECT_FALSE(PasType::boolean().isNumeric());
    EXPECT_FALSE(PasType::string().isNumeric());
}

TEST(PasTypeTest, IsOrdinal)
{
    EXPECT_TRUE(PasType::integer().isOrdinal());
    EXPECT_TRUE(PasType::boolean().isOrdinal());
    EXPECT_FALSE(PasType::real().isOrdinal());
    EXPECT_FALSE(PasType::string().isOrdinal());
}

TEST(PasTypeTest, IsNilAssignable)
{
    EXPECT_TRUE(PasType::optional(PasType::integer()).isNilAssignable());
    EXPECT_TRUE(PasType::pointer(PasType::integer()).isNilAssignable());
    EXPECT_FALSE(PasType::integer().isNilAssignable());
    EXPECT_FALSE(PasType::string().isNilAssignable());
}

TEST(PasTypeTest, ToString)
{
    EXPECT_EQ(PasType::integer().toString(), "Integer");
    EXPECT_EQ(PasType::real().toString(), "Real");
    EXPECT_EQ(PasType::boolean().toString(), "Boolean");
    EXPECT_EQ(PasType::string().toString(), "String");
    EXPECT_EQ(PasType::nil().toString(), "nil");
    EXPECT_EQ(PasType::voidType().toString(), "void");
    EXPECT_EQ(PasType::unknown().toString(), "<unknown>");
}

TEST(PasTypeTest, OptionalToString)
{
    auto optInt = PasType::optional(PasType::integer());
    EXPECT_EQ(optInt.toString(), "Integer?");
}

TEST(PasTypeTest, ArrayToString)
{
    auto dynArr = PasType::array(PasType::integer(), 0);
    EXPECT_EQ(dynArr.toString(), "array of Integer");

    auto staticArr = PasType::array(PasType::integer(), 1);
    EXPECT_EQ(staticArr.toString(), "array[1] of Integer");
}

TEST(PasTypeTest, PointerToString)
{
    auto ptrInt = PasType::pointer(PasType::integer());
    EXPECT_EQ(ptrInt.toString(), "^Integer");
}

//===----------------------------------------------------------------------===//
// Builtin Function Tests
//===----------------------------------------------------------------------===//

TEST(PascalSemanticBuiltinTest, WriteLnNoArgs)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "begin\n"
                                 "  WriteLn\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticBuiltinTest, WriteLnSingleArg)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "begin\n"
                                 "  WriteLn('Hello')\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticBuiltinTest, WriteLnMultipleArgs)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var x: Integer;\n"
                                 "begin\n"
                                 "  x := 42;\n"
                                 "  WriteLn('Value: ', x, ' is the answer')\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticBuiltinTest, LengthWithString)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var s: String;\n"
                                 "    n: Integer;\n"
                                 "begin\n"
                                 "  s := 'hello';\n"
                                 "  n := Length(s)\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticBuiltinTest, SqrtReturnsReal)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var x: Real;\n"
                                 "begin\n"
                                 "  x := Sqrt(16.0)\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticBuiltinTest, AbsPreservesType)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var x: Integer;\n"
                                 "begin\n"
                                 "  x := Abs(-5)\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticBuiltinTest, OrdReturnsInteger)
{
    DiagnosticEngine diag;
    // Ord accepts ordinal (Integer) and returns Integer
    bool result = analyzeProgram("program Test;\n"
                                 "var n: Integer;\n"
                                 "begin\n"
                                 "  n := Ord(65)\n" // Use integer for now; Boolean->Integer
                                                    // coercion would need special handling
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticBuiltinTest, ChrReturnsString)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var s: String;\n"
                                 "begin\n"
                                 "  s := Chr(65)\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticBuiltinTest, PredSuccWithInteger)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var x: Integer;\n"
                                 "begin\n"
                                 "  x := Pred(10);\n"
                                 "  x := Succ(x)\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticBuiltinTest, TruncRoundReturnInteger)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var n: Integer;\n"
                                 "begin\n"
                                 "  n := Trunc(3.7);\n"
                                 "  n := Round(3.5)\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticBuiltinTest, MathFunctionsReturnReal)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var x: Real;\n"
                                 "begin\n"
                                 "  x := Sin(0.5);\n"
                                 "  x := Cos(0.5);\n"
                                 "  x := Tan(0.5);\n"
                                 "  x := Exp(1.0);\n"
                                 "  x := Ln(2.0)\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticBuiltinTest, IntToStrReturnsString)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var s: String;\n"
                                 "begin\n"
                                 "  s := IntToStr(42)\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticBuiltinTest, RandomNoArg)
{
    DiagnosticEngine diag;
    // Use explicit parentheses for 0-arg function calls
    bool result = analyzeProgram("program Test;\n"
                                 "var x: Real;\n"
                                 "begin\n"
                                 "  Randomize();\n"
                                 "  x := Random()\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Exception Handling Tests
//===----------------------------------------------------------------------===//

TEST(PascalSemanticEHTest, TryExceptBasic)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "begin\n"
                                 "  try\n"
                                 "    WriteLn('In try')\n"
                                 "  except\n"
                                 "    on E: Exception do\n"
                                 "      WriteLn('Caught exception')\n"
                                 "  end\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticEHTest, TryFinallyBasic)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "begin\n"
                                 "  try\n"
                                 "    WriteLn('In try')\n"
                                 "  finally\n"
                                 "    WriteLn('In finally')\n"
                                 "  end\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticEHTest, RaiseWithException)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var e: Exception;\n"
                                 "begin\n"
                                 "  raise e\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticEHTest, ReraiseInsideHandler)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "begin\n"
                                 "  try\n"
                                 "    WriteLn('In try')\n"
                                 "  except\n"
                                 "    on E: Exception do\n"
                                 "      raise\n" // Re-raise inside handler - valid
                                 "  end\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticEHTest, ReraiseOutsideHandler)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "begin\n"
                                 "  raise\n" // Re-raise outside handler - invalid
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalSemanticEHTest, RaiseNonClassType)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var x: Integer;\n"
                                 "begin\n"
                                 "  raise x\n" // Cannot raise an Integer
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalSemanticEHTest, HandlerNonExceptionType)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "begin\n"
                                 "  try\n"
                                 "    WriteLn('In try')\n"
                                 "  except\n"
                                 "    on E: Integer do\n" // Integer is not an exception type
                                 "      WriteLn('Error')\n"
                                 "  end\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalSemanticEHTest, MultipleHandlers)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "begin\n"
                                 "  try\n"
                                 "    WriteLn('In try')\n"
                                 "  except\n"
                                 "    on E: Exception do\n"
                                 "      WriteLn('Exception');\n"
                                 "    on E: Exception do\n" // Multiple handlers allowed
                                 "      WriteLn('Another handler')\n"
                                 "  end\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticEHTest, NestedTryExcept)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "begin\n"
                                 "  try\n"
                                 "    try\n"
                                 "      WriteLn('Inner try')\n"
                                 "    except\n"
                                 "      on E: Exception do\n"
                                 "        WriteLn('Inner handler')\n"
                                 "    end\n"
                                 "  except\n"
                                 "    on E: Exception do\n"
                                 "      WriteLn('Outer handler')\n"
                                 "  end\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticEHTest, TryFinallyNested)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "begin\n"
                                 "  try\n"
                                 "    try\n"
                                 "      WriteLn('Inner try')\n"
                                 "    finally\n"
                                 "      WriteLn('Inner finally')\n"
                                 "    end\n"
                                 "  finally\n"
                                 "    WriteLn('Outer finally')\n"
                                 "  end\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticEHTest, ReraiseInNestedHandler)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "begin\n"
                                 "  try\n"
                                 "    try\n"
                                 "      WriteLn('Inner try')\n"
                                 "    except\n"
                                 "      on E: Exception do begin\n"
                                 "        WriteLn('Inner handler');\n"
                                 "        raise\n" // Re-raise in nested handler - valid
                                 "      end\n"
                                 "    end\n"
                                 "  except\n"
                                 "    on E: Exception do\n"
                                 "      WriteLn('Outer handler')\n"
                                 "  end\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Enum Type Tests
//===----------------------------------------------------------------------===//

TEST(PascalSemanticEnumTest, EnumTypeDeclaration)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type Color = (Red, Green, Blue);\n"
                                 "var c: Color;\n"
                                 "begin\n"
                                 "  c := Red\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticEnumTest, EnumComparison)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type Color = (Red, Green, Blue);\n"
                                 "var c: Color; b: Boolean;\n"
                                 "begin\n"
                                 "  c := Red;\n"
                                 "  b := c = Green;\n"
                                 "  b := c < Blue\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticEnumTest, EnumArithmeticNotAllowed)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type Color = (Red, Green, Blue);\n"
                                 "var c: Color;\n"
                                 "begin\n"
                                 "  c := Red + 1\n" // Arithmetic on enum not allowed
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_TRUE(diag.errorCount() > 0);
}

TEST(PascalSemanticEnumTest, EnumTypeMismatch)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type Color = (Red, Green, Blue);\n"
                                 "type Size = (Small, Medium, Large);\n"
                                 "var c: Color; s: Size; b: Boolean;\n"
                                 "begin\n"
                                 "  b := c = s\n" // Cannot compare different enum types
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_TRUE(diag.errorCount() > 0);
}

//===----------------------------------------------------------------------===//
// Case Statement Tests
//===----------------------------------------------------------------------===//

TEST(PascalSemanticCaseTest, IntegerCase)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var x, y: Integer;\n"
                                 "begin\n"
                                 "  x := 2;\n"
                                 "  case x of\n"
                                 "    1: y := 10;\n"
                                 "    2: y := 20;\n"
                                 "    3: y := 30\n"
                                 "  end\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticCaseTest, EnumCase)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type Color = (Red, Green, Blue);\n"
                                 "var c: Color; x: Integer;\n"
                                 "begin\n"
                                 "  c := Green;\n"
                                 "  case c of\n"
                                 "    Red: x := 1;\n"
                                 "    Green: x := 2;\n"
                                 "    Blue: x := 3\n"
                                 "  end\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticCaseTest, CaseWithElse)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var x, y: Integer;\n"
                                 "begin\n"
                                 "  x := 99;\n"
                                 "  case x of\n"
                                 "    1: y := 1;\n"
                                 "    2: y := 2\n"
                                 "  else\n"
                                 "    y := 0\n"
                                 "  end\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticCaseTest, CaseMultipleLabels)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var x, y: Integer;\n"
                                 "begin\n"
                                 "  x := 2;\n"
                                 "  case x of\n"
                                 "    1, 2, 3: y := 10\n"
                                 "  end\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticCaseTest, CaseStringNotAllowed)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var s: String; x: Integer;\n"
                                 "begin\n"
                                 "  s := 'hello';\n"
                                 "  case s of\n"
                                 "    'a': x := 1;\n"
                                 "    'b': x := 2\n"
                                 "  end\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result); // String case not allowed in v0.1
    EXPECT_TRUE(diag.errorCount() > 0);
}

TEST(PascalSemanticCaseTest, DuplicateCaseLabel)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var x, y: Integer;\n"
                                 "begin\n"
                                 "  x := 2;\n"
                                 "  case x of\n"
                                 "    1: y := 10;\n"
                                 "    1: y := 20\n" // Duplicate label
                                 "  end\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_TRUE(diag.errorCount() > 0);
}

TEST(PascalSemanticCaseTest, CaseLabelTypeMismatch)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type Color = (Red, Green, Blue);\n"
                                 "var x: Integer; c: Color; y: Integer;\n"
                                 "begin\n"
                                 "  x := 1;\n"
                                 "  case x of\n"
                                 "    Red: y := 1\n" // Enum label for integer case
                                 "  end\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_TRUE(diag.errorCount() > 0);
}

//===----------------------------------------------------------------------===//
// For Loop Variable Semantics Tests
//===----------------------------------------------------------------------===//

TEST(PascalSemanticTest, ForLoopVariableReadOnly)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var i: Integer;\n"
                                 "begin\n"
                                 "  for i := 1 to 10 do\n"
                                 "    i := 5\n" // Error: cannot assign to loop variable
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, ForLoopVariableUndefinedAfter)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var i, x: Integer;\n"
                                 "begin\n"
                                 "  for i := 1 to 10 do\n"
                                 "    x := i;\n"
                                 "  x := i\n" // Error: i is undefined after loop
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, ForLoopVariableOrdinalOnly)
{
    DiagnosticEngine diag;
    bool result =
        analyzeProgram("program Test;\n"
                       "var r: Real;\n"
                       "begin\n"
                       "  for r := 1.0 to 10.0 do\n" // Error: loop variable must be ordinal
                       "    WriteLn(r)\n"
                       "end.",
                       diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, ForLoopWithEnumVariable)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type Color = (Red, Green, Blue);\n"
                                 "var c: Color;\n"
                                 "begin\n"
                                 "  for c := Red to Blue do\n"
                                 "    WriteLn('color')\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, BreakInNestedLoop)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var i, j: Integer;\n"
                                 "begin\n"
                                 "  for i := 1 to 10 do begin\n"
                                 "    for j := 1 to 10 do begin\n"
                                 "      if i + j = 15 then break\n"
                                 "    end\n"
                                 "  end\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, ContinueInRepeatLoop)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var x: Integer;\n"
                                 "begin\n"
                                 "  x := 0;\n"
                                 "  repeat\n"
                                 "    x := x + 1;\n"
                                 "    if x = 5 then continue;\n"
                                 "    WriteLn(x)\n"
                                 "  until x = 10\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// For-In Loop Tests
//===----------------------------------------------------------------------===//

TEST(PascalSemanticTest, ForInOverDynamicArray)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var arr: array of Integer;\n"
                                 "var item: Integer;\n"
                                 "begin\n"
                                 "  for item in arr do\n"
                                 "    WriteLn(item)\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, ForInOverString)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var s: String;\n"
                                 "var ch: String;\n"
                                 "begin\n"
                                 "  s := 'Hello';\n"
                                 "  for ch in s do\n"
                                 "    WriteLn(ch)\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, ForInVariableReadOnly)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var arr: array of Integer;\n"
                                 "var item: Integer;\n"
                                 "begin\n"
                                 "  for item in arr do\n"
                                 "    item := 5\n" // Error: cannot assign to for-in loop variable
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, ForInInvalidCollection)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var x, item: Integer;\n"
                                 "begin\n"
                                 "  x := 10;\n"
                                 "  for item in x do\n" // Error: Integer is not iterable
                                 "    WriteLn(item)\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, ForInWithBreak)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var arr: array of Integer;\n"
                                 "var item: Integer;\n"
                                 "begin\n"
                                 "  for item in arr do begin\n"
                                 "    if item > 5 then break;\n"
                                 "    WriteLn(item)\n"
                                 "  end\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalSemanticTest, ForInWithContinue)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var s: String;\n"
                                 "var ch: String;\n"
                                 "begin\n"
                                 "  s := 'abc';\n"
                                 "  for ch in s do begin\n"
                                 "    if ch = 'b' then continue;\n"
                                 "    WriteLn(ch)\n"
                                 "  end\n"
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
