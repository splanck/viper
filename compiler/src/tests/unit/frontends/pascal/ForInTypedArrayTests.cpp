//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/frontends/pascal/ForInTypedArrayTests.cpp
// Purpose: Unit tests for for-in loop element type dispatch.
// Key invariants: Tests that for-in correctly handles different array element types.
// Ownership/Lifetime: Test suite.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//

#include "frontends/pascal/AST.hpp"
#include "frontends/pascal/Lexer.hpp"
#include "frontends/pascal/Lowerer.hpp"
#include "frontends/pascal/Parser.hpp"
#include "frontends/pascal/SemanticAnalyzer.hpp"
#include "frontends/pascal/sem/Types.hpp"
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

/// @brief Parse, analyze, and lower a program.
/// @return True if all phases succeeded without errors.
bool compileProgram(const std::string &source, DiagnosticEngine &diag, il::core::Module &outModule)
{
    Lexer lexer(source, 0, diag);
    Parser parser(lexer, diag);
    auto prog = parser.parseProgram();
    if (!prog || parser.hasError())
        return false;

    SemanticAnalyzer analyzer(diag);
    if (!analyzer.analyze(*prog))
        return false;

    Lowerer lowerer;
    outModule = lowerer.lower(*prog, analyzer);
    return true;
}

//===----------------------------------------------------------------------===//
// For-In Integer Array Tests
//===----------------------------------------------------------------------===//

TEST(ForInTypedArrayTest, IntegerArray)
{
    DiagnosticEngine diag;
    il::core::Module module;

    const char *source = R"(
program TestForIn;
var
    ints: array of Integer;
    n: Integer;
    sum: Integer;
begin
    SetLength(ints, 3);
    ints[0] := 10;
    ints[1] := 20;
    ints[2] := 30;

    sum := 0;
    for n in ints do
        sum := sum + n;
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// For-In Real Array Tests
//===----------------------------------------------------------------------===//

TEST(ForInTypedArrayTest, RealArray)
{
    DiagnosticEngine diag;
    il::core::Module module;

    const char *source = R"(
program TestForIn;
var
    reals: array of Real;
    r: Real;
begin
    SetLength(reals, 3);
    reals[0] := 1.1;
    reals[1] := 2.2;
    reals[2] := 3.3;

    for r in reals do
        WriteLn(r);
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(ForInTypedArrayTest, RealArraySum)
{
    DiagnosticEngine diag;
    il::core::Module module;

    const char *source = R"(
program TestForIn;
var
    values: array of Real;
    v: Real;
    total: Real;
begin
    SetLength(values, 4);
    values[0] := 0.5;
    values[1] := 1.5;
    values[2] := 2.5;
    values[3] := 3.5;

    total := 0.0;
    for v in values do
        total := total + v;
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// For-In String Array Tests
//===----------------------------------------------------------------------===//

TEST(ForInTypedArrayTest, StringArray)
{
    DiagnosticEngine diag;
    il::core::Module module;

    const char *source = R"(
program TestForIn;
var
    strs: array of String;
    s: String;
begin
    SetLength(strs, 2);
    strs[0] := 'Hello';
    strs[1] := 'World';

    for s in strs do
        WriteLn(s);
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(ForInTypedArrayTest, StringArrayConcat)
{
    DiagnosticEngine diag;
    il::core::Module module;

    const char *source = R"(
program TestForIn;
var
    words: array of String;
    word: String;
    result: String;
begin
    SetLength(words, 3);
    words[0] := 'One';
    words[1] := 'Two';
    words[2] := 'Three';

    result := '';
    for word in words do
        result := result + word + ' ';
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// For-In String Iteration Tests
//===----------------------------------------------------------------------===//

TEST(ForInTypedArrayTest, StringIteration)
{
    DiagnosticEngine diag;
    il::core::Module module;

    const char *source = R"(
program TestForIn;
var
    text: String;
    ch: String;
begin
    text := 'Hello';

    for ch in text do
        WriteLn(ch);
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// For-In Boolean Array Tests
//===----------------------------------------------------------------------===//

TEST(ForInTypedArrayTest, BooleanArray)
{
    DiagnosticEngine diag;
    il::core::Module module;

    const char *source = R"(
program TestForIn;
var
    flags: array of Boolean;
    f: Boolean;
    trueCount: Integer;
begin
    SetLength(flags, 4);
    flags[0] := True;
    flags[1] := False;
    flags[2] := True;
    flags[3] := True;

    trueCount := 0;
    for f in flags do
        if f then
            trueCount := trueCount + 1;
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// For-In Object Array Tests
//===----------------------------------------------------------------------===//

TEST(ForInTypedArrayTest, ObjectArray)
{
    DiagnosticEngine diag;
    il::core::Module module;

    const char *source = R"(
program TestForIn;

type
    TItem = class
        name: String;
        constructor Create(n: String);
    end;

constructor TItem.Create(n: String);
begin
    name := n;
end;

var
    items: array of TItem;
    item: TItem;
begin
    SetLength(items, 2);
    items[0] := TItem.Create('First');
    items[1] := TItem.Create('Second');

    for item in items do
        WriteLn(item.name);
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// For-In Empty Array Tests
//===----------------------------------------------------------------------===//

TEST(ForInTypedArrayTest, EmptyIntegerArray)
{
    DiagnosticEngine diag;
    il::core::Module module;

    const char *source = R"(
program TestForIn;
var
    ints: array of Integer;
    n: Integer;
    count: Integer;
begin
    SetLength(ints, 0);

    count := 0;
    for n in ints do
        count := count + 1;
    // count should be 0 after loop (no iterations)
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(ForInTypedArrayTest, EmptyRealArray)
{
    DiagnosticEngine diag;
    il::core::Module module;

    const char *source = R"(
program TestForIn;
var
    reals: array of Real;
    r: Real;
begin
    SetLength(reals, 0);

    for r in reals do
        WriteLn(r);
    // No iterations
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
