//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/frontends/pascal/SemanticArrayTests.cpp
// Purpose: Unit tests for Pascal array semantics.
// Key invariants: Tests fixed arrays (value types), dynamic arrays (ref types),
//                 0-based indexing, Length, SetLength, and dimension validation.
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
// Fixed Array Tests (Value Types)
//===----------------------------------------------------------------------===//

TEST(PascalArrayTest, FixedArrayDeclaration)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var arr: array[10] of Integer;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalArrayTest, FixedMultiDimArray)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var matrix: array[3, 4] of Real;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalArrayTest, FixedArrayElementAccess)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var arr: array[10] of Integer;\n"
                                 "var x: Integer;\n"
                                 "begin\n"
                                 "  x := arr[0];\n"
                                 "  arr[5] := 42;\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalArrayTest, FixedArrayNonConstantDimensionError)
{
    DiagnosticEngine diag;
    bool result =
        analyzeProgram("program Test;\n"
                       "var n: Integer;\n"
                       "var arr: array[n] of Integer;\n" // Error: dimension must be constant
                       "begin\n"
                       "end.",
                       diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalArrayTest, FixedArrayZeroDimensionError)
{
    DiagnosticEngine diag;
    bool result =
        analyzeProgram("program Test;\n"
                       "var arr: array[0] of Integer;\n" // Error: dimension must be positive
                       "begin\n"
                       "end.",
                       diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalArrayTest, FixedArrayNegativeDimensionError)
{
    DiagnosticEngine diag;
    bool result =
        analyzeProgram("program Test;\n"
                       "var arr: array[-5] of Integer;\n" // Error: dimension must be positive
                       "begin\n"
                       "end.",
                       diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Dynamic Array Tests (Reference Types)
//===----------------------------------------------------------------------===//

TEST(PascalArrayTest, DynamicArrayDeclaration)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var arr: array of Integer;\n"
                                 "begin\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalArrayTest, DynamicArrayElementAccess)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var arr: array of Integer;\n"
                                 "var x: Integer;\n"
                                 "begin\n"
                                 "  x := arr[0];\n"
                                 "  arr[5] := 42;\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalArrayTest, DynamicArrayNilAssignment)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var arr: array of Integer;\n"
                                 "begin\n"
                                 "  arr := nil;\n" // Dynamic arrays accept nil
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalArrayTest, FixedArrayNilAssignmentError)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var arr: array[10] of Integer;\n"
                                 "begin\n"
                                 "  arr := nil;\n" // Error: fixed arrays are value types
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Length Builtin Tests
//===----------------------------------------------------------------------===//

TEST(PascalArrayTest, LengthOnDynamicArray)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var arr: array of Integer;\n"
                                 "var len: Integer;\n"
                                 "begin\n"
                                 "  len := Length(arr);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalArrayTest, LengthOnFixedArray)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var arr: array[10] of Integer;\n"
                                 "var len: Integer;\n"
                                 "begin\n"
                                 "  len := Length(arr);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// SetLength Builtin Tests
//===----------------------------------------------------------------------===//

TEST(PascalArrayTest, SetLengthOnDynamicArray)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var arr: array of Integer;\n"
                                 "begin\n"
                                 "  SetLength(arr, 10);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalArrayTest, SetLengthOnFixedArrayError)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var arr: array[10] of Integer;\n"
                                 "begin\n"
                                 "  SetLength(arr, 20);\n" // Error: cannot resize fixed arrays
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalArrayTest, SetLengthOnDynamicRealArray)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var arr: array of Real;\n"
                                 "begin\n"
                                 "  SetLength(arr, 10);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalArrayTest, SetLengthOnDynamicStringArray)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var arr: array of String;\n"
                                 "begin\n"
                                 "  SetLength(arr, 10);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalArrayTest, SetLengthOnDynamicBooleanArray)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var arr: array of Boolean;\n"
                                 "begin\n"
                                 "  SetLength(arr, 10);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalArrayTest, SetLengthOnDynamicObjectArray)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "type TItem = class\n"
                                 "  Value: Integer;\n"
                                 "end;\n"
                                 "var arr: array of TItem;\n"
                                 "begin\n"
                                 "  SetLength(arr, 10);\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Index Type Tests
//===----------------------------------------------------------------------===//

TEST(PascalArrayTest, IntegerIndexValid)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var arr: array of Integer;\n"
                                 "var i: Integer;\n"
                                 "begin\n"
                                 "  arr[i] := 42;\n"
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalArrayTest, NonOrdinalIndexError)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var arr: array of Integer;\n"
                                 "var f: Real;\n"
                                 "begin\n"
                                 "  arr[f] := 42;\n" // Error: index must be ordinal
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalArrayTest, StringIndexError)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var arr: array of Integer;\n"
                                 "var s: String;\n"
                                 "begin\n"
                                 "  arr[s] := 42;\n" // Error: index must be ordinal
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Array Type Compatibility Tests
//===----------------------------------------------------------------------===//

TEST(PascalArrayTest, DynamicArrayAssignment)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var a, b: array of Integer;\n"
                                 "begin\n"
                                 "  a := b;\n" // Dynamic arrays share reference
                                 "end.",
                                 diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalArrayTest, ArrayOfDifferentElementTypesError)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var intArr: array of Integer;\n"
                                 "var realArr: array of Real;\n"
                                 "begin\n"
                                 "  intArr := realArr;\n" // Error: different element types
                                 "end.",
                                 diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalArrayTest, ArrayOfArrayDeclaration)
{
    DiagnosticEngine diag;
    bool result = analyzeProgram("program Test;\n"
                                 "var matrix: array of array of Integer;\n"
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
