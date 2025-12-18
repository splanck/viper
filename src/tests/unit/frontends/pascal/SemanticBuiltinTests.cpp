//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/frontends/pascal/SemanticBuiltinTests.cpp
// Purpose: Unit tests for Pascal builtin functions and units.
// Key invariants: Tests core builtins, Viper.Strings, Viper.Math units.
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
// Core I/O Builtins Tests
//===----------------------------------------------------------------------===//

TEST(PascalBuiltinTest, WriteWithValidTypes)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "begin\n"
                                 "  Write('hello');\n"
                                 "  Write(42);\n"
                                 "  Write(3.14);\n"
                                 "  Write(True);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, WriteLnWithValidTypes)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "begin\n"
                                 "  WriteLn('hello');\n"
                                 "  WriteLn(42);\n"
                                 "  WriteLn(3.14);\n"
                                 "  WriteLn(True);\n"
                                 "  WriteLn;\n" // No args = just newline
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, ReadLnReturnsString)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var s: String;\n"
                                 "begin\n"
                                 "  s := ReadLn;\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, ReadIntegerReturnsInteger)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var i: Integer;\n"
                                 "begin\n"
                                 "  i := ReadInteger;\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, ReadRealReturnsReal)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var r: Real;\n"
                                 "begin\n"
                                 "  r := ReadReal;\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Conversion Builtins Tests
//===----------------------------------------------------------------------===//

TEST(PascalBuiltinTest, IntToStr)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var s: String;\n"
                                 "begin\n"
                                 "  s := IntToStr(42);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, RealToStr)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var s: String;\n"
                                 "begin\n"
                                 "  s := RealToStr(3.14);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, FloatToStrExtension)
{
    // FloatToStr is an extension alias for RealToStr
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var s: String;\n"
                                 "begin\n"
                                 "  s := FloatToStr(3.14);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, StrToInt)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var i: Integer;\n"
                                 "begin\n"
                                 "  i := StrToInt('42');\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, StrToReal)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var r: Real;\n"
                                 "begin\n"
                                 "  r := StrToReal('3.14');\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, StrToFloatExtension)
{
    // StrToFloat is an extension alias for StrToReal
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var r: Real;\n"
                                 "begin\n"
                                 "  r := StrToFloat('3.14');\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, IntToStrTypeMismatch)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var s: String;\n"
                                 "begin\n"
                                 "  s := IntToStr('not an int');\n" // Error: expected Integer
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Length and SetLength Tests
//===----------------------------------------------------------------------===//

TEST(PascalBuiltinTest, LengthOnString)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var s: String; len: Integer;\n"
                                 "begin\n"
                                 "  s := 'hello';\n"
                                 "  len := Length(s);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, LengthOnArray)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var arr: array of Integer; len: Integer;\n"
                                 "begin\n"
                                 "  len := Length(arr);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Viper.Strings Unit Tests
//===----------------------------------------------------------------------===//

TEST(PascalBuiltinTest, ViperStringsUpper)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "uses Viper.Strings;\n"
                                 "var s: String;\n"
                                 "begin\n"
                                 "  s := Upper('hello');\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, ViperStringsLower)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "uses Viper.Strings;\n"
                                 "var s: String;\n"
                                 "begin\n"
                                 "  s := Lower('HELLO');\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, ViperStringsLeft)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "uses Viper.Strings;\n"
                                 "var s: String;\n"
                                 "begin\n"
                                 "  s := Left('hello world', 5);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, ViperStringsRight)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "uses Viper.Strings;\n"
                                 "var s: String;\n"
                                 "begin\n"
                                 "  s := Right('hello world', 5);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, ViperStringsMid)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "uses Viper.Strings;\n"
                                 "var s: String;\n"
                                 "begin\n"
                                 "  s := Mid('hello world', 6);\n" // 2 args version
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, ViperStringsChr)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "uses Viper.Strings;\n"
                                 "var s: String;\n"
                                 "begin\n"
                                 "  s := Chr(65);\n" // 'A'
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, ViperStringsAsc)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "uses Viper.Strings;\n"
                                 "var n: Integer;\n"
                                 "begin\n"
                                 "  n := Asc('A');\n" // 65
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, ViperStringsWithoutUsesError)
{
    // Without uses clause, Upper should not be available
    DiagnosticEngine diag;
    bool result =
        analyzeProgram("program Test;\n"
                       "var s: String;\n"
                       "begin\n"
                       "  s := Upper('hello');\n" // Error: Upper requires uses Viper.Strings
                       "end.",
                       diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Viper.Math Unit Tests
//===----------------------------------------------------------------------===//

TEST(PascalBuiltinTest, ViperMathPi)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "uses Viper.Math;\n"
                                 "var r: Real;\n"
                                 "begin\n"
                                 "  r := Pi;\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, ViperMathE)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "uses Viper.Math;\n"
                                 "var r: Real;\n"
                                 "begin\n"
                                 "  r := E;\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, ViperMathPow)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "uses Viper.Math;\n"
                                 "var r: Real;\n"
                                 "begin\n"
                                 "  r := Pow(2.0, 10.0);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, ViperMathPowerExtension)
{
    // Power is an extension alias for Pow
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "uses Viper.Math;\n"
                                 "var r: Real;\n"
                                 "begin\n"
                                 "  r := Power(2.0, 10.0);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, ViperMathSqrt)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "uses Viper.Math;\n"
                                 "var r: Real;\n"
                                 "begin\n"
                                 "  r := Sqrt(16.0);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, ViperMathAtan)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "uses Viper.Math;\n"
                                 "var r: Real;\n"
                                 "begin\n"
                                 "  r := Atan(1.0);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, ViperMathSign)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "uses Viper.Math;\n"
                                 "var i: Integer;\n"
                                 "begin\n"
                                 "  i := Sign(-5);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, ViperMathMinMax)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "uses Viper.Math;\n"
                                 "var i: Integer; r: Real;\n"
                                 "begin\n"
                                 "  i := Min(5, 10);\n"
                                 "  i := Max(5, 10);\n"
                                 "  r := Min(3.14, 2.71);\n"
                                 "  r := Max(3.14, 2.71);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, ViperMathWithoutUsesError)
{
    // Without uses clause, Pow should not be available
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var r: Real;\n"
                                 "begin\n"
                                 "  r := Pow(2.0, 10.0);\n" // Error: Pow requires uses Viper.Math
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Viper.Diagnostics Unit Tests
//===----------------------------------------------------------------------===//

TEST(PascalBuiltinTest, ViperDiagnosticsAssert)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "uses Viper.Diagnostics;\n"
                                 "begin\n"
                                 "  Assert(True, 'ok');\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, ViperDiagnosticsWithoutUsesError)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "begin\n"
                                 "  Assert(True, 'ok');\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Viper.Environment Unit Tests
//===----------------------------------------------------------------------===//

TEST(PascalBuiltinTest, ViperEnvironmentVariables)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "uses Viper.Environment;\n"
                                 "var name, value: String; has: Boolean;\n"
                                 "begin\n"
                                 "  name := 'VIPER_TEST_ENV';\n"
                                 "  value := GetVariable(name);\n"
                                 "  has := HasVariable(name);\n"
                                 "  SetVariable(name, 'abc');\n"
                                 "  value := GetVariable(name);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, ViperEnvironmentEndProgram)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "uses Viper.Environment;\n"
                                 "begin\n"
                                 "  EndProgram(7);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, ViperEnvironmentWithoutUsesError)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var value: String;\n"
                                 "begin\n"
                                 "  value := GetVariable('X');\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Core Math Functions (Available Without Unit Import)
//===----------------------------------------------------------------------===//

TEST(PascalBuiltinTest, CoreSqrt)
{
    // Sqrt is available without importing Viper.Math
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var r: Real;\n"
                                 "begin\n"
                                 "  r := Sqrt(16.0);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, CoreAbs)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var i: Integer; r: Real;\n"
                                 "begin\n"
                                 "  i := Abs(-5);\n"
                                 "  r := Abs(-3.14);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, CoreTrigFunctions)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var r: Real;\n"
                                 "begin\n"
                                 "  r := Sin(1.0);\n"
                                 "  r := Cos(1.0);\n"
                                 "  r := Tan(1.0);\n"
                                 "  r := ArcTan(1.0);\n" // Core uses ArcTan, not Atan
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, CoreExpLn)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var r: Real;\n"
                                 "begin\n"
                                 "  r := Exp(1.0);\n"
                                 "  r := Ln(2.71828);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, CoreFloorCeil)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var i: Integer;\n"
                                 "begin\n"
                                 "  i := Floor(3.7);\n"
                                 "  i := Ceil(3.2);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Multiple Unit Import Tests
//===----------------------------------------------------------------------===//

TEST(PascalBuiltinTest, MultipleUnitsImport)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "uses Viper.Strings, Viper.Math;\n"
                                 "var s: String; r: Real;\n"
                                 "begin\n"
                                 "  s := Upper('hello');\n"
                                 "  r := Pow(2.0, 10.0);\n"
                                 "  r := Pi;\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// v0.1 Spec Compliance Tests - No Char type
//===----------------------------------------------------------------------===//

TEST(PascalBuiltinTest, V01_CharTypeNotRecognized)
{
    // v0.1 spec: Char is NOT a primitive type
    // Using 'Char' as a type should produce an error
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var c: Char;\n" // Error: Char is not a type
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, V01_ChrReturnsString)
{
    // v0.1 spec: Chr returns String (1-byte string), not Char
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "uses Viper.Strings;\n"
                                 "var s: String;\n"
                                 "begin\n"
                                 "  s := Chr(65);\n" // 'A' as a 1-char string
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, V01_AscReturnsInteger)
{
    // v0.1 spec: Asc takes a string and returns Integer (first byte)
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "uses Viper.Strings;\n"
                                 "var i: Integer;\n"
                                 "begin\n"
                                 "  i := Asc('A');\n" // Get ASCII code of 'A'
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, V01_StringIndexingReturnsString)
{
    // v0.1 spec: String indexing returns a 1-character String, not Char
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var s, c: String;\n"
                                 "begin\n"
                                 "  s := 'Hello';\n"
                                 "  c := s[1];\n" // Get first character as a String
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, V01_ChrAscRoundtrip)
{
    // v0.1 spec: Chr and Asc are inverses for single bytes
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "uses Viper.Strings;\n"
                                 "var s: String; i: Integer;\n"
                                 "begin\n"
                                 "  s := Chr(65);\n"       // 'A'
                                 "  i := Asc(s);\n"        // Back to 65
                                 "  s := Chr(Asc('X'));\n" // 'X' -> 88 -> 'X'
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, V01_NewlineViaChr)
{
    // v0.1 spec: Use Chr(10) to produce newline, Chr(9) for tab
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "uses Viper.Strings;\n"
                                 "var nl, tab: String;\n"
                                 "begin\n"
                                 "  nl := Chr(10);\n" // Newline
                                 "  tab := Chr(9);\n" // Tab
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalBuiltinTest, V01_CharCanBeUsedAsIdentifier)
{
    // v0.1 spec: Since Char is not a reserved word or predefined type,
    // it can be used as a variable name
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var Char: Integer;\n" // OK: 'Char' is just an identifier
                                 "begin\n"
                                 "  Char := 42;\n"
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
