//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/frontends/pascal/SemanticUnitTests.cpp
// Purpose: Unit tests for Pascal units and multi-file compilation.
// Key invariants: Tests unit parsing, uses resolution, interface var check.
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
#include "frontends/pascal/Compiler.hpp"
#include "frontends/pascal/Lexer.hpp"
#include "frontends/pascal/Lowerer.hpp"
#include "frontends/pascal/Parser.hpp"
#include "frontends/pascal/SemanticAnalyzer.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"
#include <memory>
#include <string>

using namespace il::frontends::pascal;
using namespace il::support;

namespace
{

//===----------------------------------------------------------------------===//
// Test Helpers
//===----------------------------------------------------------------------===//

/// @brief Parse and analyze a unit.
/// @return True if analysis succeeded without errors.
bool analyzeUnit(const std::string &source, SemanticAnalyzer &analyzer, DiagnosticEngine &diag)
{
    Lexer lexer(source, 0, diag);
    Parser parser(lexer, diag);
    auto [prog, unit] = parser.parse();
    if (!unit || parser.hasError())
        return false;

    return analyzer.analyze(*unit);
}

/// @brief Parse and analyze a program with a shared analyzer.
bool analyzeProgram(const std::string &source, SemanticAnalyzer &analyzer, DiagnosticEngine &diag)
{
    Lexer lexer(source, 0, diag);
    Parser parser(lexer, diag);
    auto prog = parser.parseProgram();
    if (!prog || parser.hasError())
        return false;

    return analyzer.analyze(*prog);
}

//===----------------------------------------------------------------------===//
// Unit Parsing Tests
//===----------------------------------------------------------------------===//

TEST(PascalUnitTest, BasicUnitParsing)
{
    DiagnosticEngine diag;
    SemanticAnalyzer analyzer(diag);

    const std::string unitSource = R"(
unit MyMath;
interface
  const Tau = 6.28;
  function Square(x: Integer): Integer;
implementation
  function Square(x: Integer): Integer;
  begin
    Result := x * x
  end;
end.
)";

    bool result = analyzeUnit(unitSource, analyzer, diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalUnitTest, UnitWithTypes)
{
    DiagnosticEngine diag;
    SemanticAnalyzer analyzer(diag);

    const std::string unitSource = R"(
unit Types;
interface
  type TNumber = Integer;
implementation
end.
)";

    bool result = analyzeUnit(unitSource, analyzer, diag);
    EXPECT_TRUE(result);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Interface Var Check Tests
//===----------------------------------------------------------------------===//

TEST(PascalUnitTest, InterfaceVarError)
{
    DiagnosticEngine diag;
    SemanticAnalyzer analyzer(diag);

    const std::string unitSource = R"(
unit BadUnit;
interface
  var x: Integer;
implementation
end.
)";

    bool result = analyzeUnit(unitSource, analyzer, diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Uses Clause Tests
//===----------------------------------------------------------------------===//

TEST(PascalUnitTest, UsesClauseResolution)
{
    DiagnosticEngine diag;
    SemanticAnalyzer analyzer(diag);

    // First analyze the unit
    const std::string mathUnit = R"(
unit MyMath;
interface
  const Tau = 6.28;
  function Square(x: Integer): Integer;
implementation
  function Square(x: Integer): Integer;
  begin
    Result := x * x
  end;
end.
)";

    bool unitOk = analyzeUnit(mathUnit, analyzer, diag);
    ASSERT_TRUE(unitOk);

    // Then analyze the program that uses it
    const std::string program = R"(
program Demo;
uses MyMath;
var n: Integer;
begin
  n := Square(5)
end.
)";

    bool progOk = analyzeProgram(program, analyzer, diag);
    EXPECT_TRUE(progOk);
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(PascalUnitTest, MissingUnitError)
{
    DiagnosticEngine diag;
    SemanticAnalyzer analyzer(diag);

    const std::string program = R"(
program Demo;
uses NonExistentUnit;
begin
end.
)";

    bool result = analyzeProgram(program, analyzer, diag);
    EXPECT_FALSE(result);
    EXPECT_NE(diag.errorCount(), 0u);
}

TEST(PascalUnitTest, UsesConstantFromUnit)
{
    DiagnosticEngine diag;
    SemanticAnalyzer analyzer(diag);

    // First analyze the unit
    const std::string mathUnit = R"(
unit MyMath;
interface
  const Pi = 3.14159;
  const Tau = 6.28318;
implementation
end.
)";

    bool unitOk = analyzeUnit(mathUnit, analyzer, diag);
    ASSERT_TRUE(unitOk);

    // Then analyze the program that uses the constants
    const std::string program = R"(
program Demo;
uses MyMath;
var x: Real;
begin
  x := Pi + Tau
end.
)";

    bool progOk = analyzeProgram(program, analyzer, diag);
    EXPECT_TRUE(progOk);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Multi-File Compilation Tests
//===----------------------------------------------------------------------===//

TEST(PascalUnitTest, MultiFileCompilation)
{
    SourceManager sm;
    PascalCompilerOptions opts{};

    const std::string mathUnit = R"(
unit MyMath;
interface
  function Square(x: Integer): Integer;
implementation
  function Square(x: Integer): Integer;
  begin
    Result := x * x
  end;
end.
)";

    const std::string program = R"(
program Demo;
uses MyMath;
var n: Integer;
begin
  n := Square(5);
  WriteLn(IntToStr(n))
end.
)";

    PascalMultiFileInput input;
    input.units.push_back({mathUnit, "MyMath.pas"});
    input.program = {program, "Demo.pas"};

    auto result = compilePascalMultiFile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
    EXPECT_EQ(result.diagnostics.errorCount(), 0u);

    // Check that both @main and @Square functions exist
    bool hasMain = false;
    bool hasSquare = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
            hasMain = true;
        if (fn.name == "Square")
            hasSquare = true;
    }
    EXPECT_TRUE(hasMain);
    EXPECT_TRUE(hasSquare);
}

TEST(PascalUnitTest, MultipleUnits)
{
    SourceManager sm;
    PascalCompilerOptions opts{};

    const std::string mathUnit = R"(
unit MyMath;
interface
  function Square(x: Integer): Integer;
implementation
  function Square(x: Integer): Integer;
  begin
    Result := x * x
  end;
end.
)";

    const std::string stringsUnit = R"(
unit MyStrings;
interface
  procedure PrintNum(n: Integer);
implementation
  procedure PrintNum(n: Integer);
  begin
    WriteLn(IntToStr(n))
  end;
end.
)";

    const std::string program = R"(
program Demo;
uses MyMath, MyStrings;
var n: Integer;
begin
  n := Square(7);
  PrintNum(n)
end.
)";

    PascalMultiFileInput input;
    input.units.push_back({mathUnit, "MyMath.pas"});
    input.units.push_back({stringsUnit, "MyStrings.pas"});
    input.program = {program, "Demo.pas"};

    auto result = compilePascalMultiFile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
    EXPECT_EQ(result.diagnostics.errorCount(), 0u);

    // Check that all functions exist
    bool hasMain = false;
    bool hasSquare = false;
    bool hasPrintNum = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
            hasMain = true;
        if (fn.name == "Square")
            hasSquare = true;
        if (fn.name == "PrintNum")
            hasPrintNum = true;
    }
    EXPECT_TRUE(hasMain);
    EXPECT_TRUE(hasSquare);
    EXPECT_TRUE(hasPrintNum);
}

} // namespace

#ifndef VIPER_HAS_GTEST
int main()
{
    return RUN_ALL_TESTS();
}
#endif
