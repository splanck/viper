//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/frontends/pascal/DefaultParamLoweringTests.cpp
// Purpose: Unit tests for default parameter value lowering.
// Key invariants: Tests that default values are correctly filled in during lowering.
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
// Function Default Parameter Tests
//===----------------------------------------------------------------------===//

TEST(DefaultParamLoweringTest, FunctionWithSingleDefault)
{
    DiagnosticEngine diag;
    il::core::Module module;

    const char *source = R"(
program TestDefaults;

function Greet(name: String; greeting: String = 'Hello'): String;
begin
    Result := greeting + ', ' + name + '!';
end;

begin
    WriteLn(Greet('Alice'));           // Should use default: Hello, Alice!
    WriteLn(Greet('Bob', 'Goodbye'));  // Should use provided: Goodbye, Bob!
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(DefaultParamLoweringTest, FunctionWithMultipleDefaults)
{
    DiagnosticEngine diag;
    il::core::Module module;

    const char *source = R"(
program TestDefaults;

function Connect(host: String; port: Integer = 80; timeout: Integer = 30): Integer;
begin
    Result := port + timeout;
end;

var result: Integer;
begin
    result := Connect('localhost');             // port=80, timeout=30
    result := Connect('localhost', 8080);       // port=8080, timeout=30
    result := Connect('localhost', 8080, 60);   // All explicit
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(DefaultParamLoweringTest, ProcedureWithDefault)
{
    DiagnosticEngine diag;
    il::core::Module module;

    const char *source = R"(
program TestDefaults;

procedure Log(msg: String; level: Integer = 0);
begin
    WriteLn(msg);
end;

begin
    Log('Info message');           // level=0
    Log('Error message', 2);       // level=2
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(DefaultParamLoweringTest, DefaultBooleanParam)
{
    DiagnosticEngine diag;
    il::core::Module module;

    const char *source = R"(
program TestDefaults;

procedure ProcessData(data: String; validate: Boolean = True);
begin
    if validate then
        WriteLn('Validating: ' + data)
    else
        WriteLn('Skipping validation');
end;

begin
    ProcessData('test');             // validate=True
    ProcessData('test', False);      // validate=False
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(DefaultParamLoweringTest, DefaultRealParam)
{
    DiagnosticEngine diag;
    il::core::Module module;

    const char *source = R"(
program TestDefaults;

function ScaleValue(value: Real; factor: Real = 1.0): Real;
begin
    Result := value * factor;
end;

var result: Real;
begin
    result := ScaleValue(10.0);          // factor=1.0
    result := ScaleValue(10.0, 2.5);     // factor=2.5
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Method Default Parameter Tests
//===----------------------------------------------------------------------===//

TEST(DefaultParamLoweringTest, MethodWithDefault)
{
    DiagnosticEngine diag;
    il::core::Module module;

    const char *source = R"(
program TestDefaults;

type
    TGreeter = class
        name: String;
        constructor Create(n: String);
        function SayHello(greeting: String = 'Hello'): String;
    end;

constructor TGreeter.Create(n: String);
begin
    name := n;
end;

function TGreeter.SayHello(greeting: String = 'Hello'): String;
begin
    Result := greeting + ', ' + name;
end;

var g: TGreeter;
    msg: String;
begin
    g := TGreeter.Create('World');
    msg := g.SayHello;             // Uses default: Hello
    msg := g.SayHello('Hi');       // Uses provided: Hi
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(DefaultParamLoweringTest, ConstructorWithDefault)
{
    DiagnosticEngine diag;
    il::core::Module module;

    const char *source = R"(
program TestDefaults;

type
    TConfig = class
        host: String;
        port: Integer;
        constructor Create(h: String; p: Integer = 80);
    end;

constructor TConfig.Create(h: String; p: Integer = 80);
begin
    host := h;
    port := p;
end;

var cfg1, cfg2: TConfig;
begin
    cfg1 := TConfig.Create('localhost');        // port=80
    cfg2 := TConfig.Create('localhost', 8080);  // port=8080
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

TEST(DefaultParamLoweringTest, ImplicitSelfMethodCall)
{
    DiagnosticEngine diag;
    il::core::Module module;

    // Test calling a method on Self without explicit receiver
    const char *source = R"(
program TestDefaults;

type
    TProcessor = class
        constructor Create;
        procedure Process(data: String; priority: Integer = 0);
        procedure Run;
    end;

constructor TProcessor.Create;
begin
end;

procedure TProcessor.Process(data: String; priority: Integer = 0);
begin
    WriteLn(data);
end;

procedure TProcessor.Run;
begin
    Process('Item1');        // Implicit Self, default priority
    Process('Item2', 5);     // Implicit Self, explicit priority
end;

var p: TProcessor;
begin
    p := TProcessor.Create;
    p.Run;
end.
)";

    EXPECT_TRUE(compileProgram(source, diag, module));
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Expression Default Parameter Tests
//===----------------------------------------------------------------------===//

TEST(DefaultParamLoweringTest, ConstantExpressionDefault)
{
    DiagnosticEngine diag;
    il::core::Module module;

    const char *source = R"(
program TestDefaults;

const
    DEFAULT_PORT = 80;
    DEFAULT_TIMEOUT = 30;

function Connect(host: String; port: Integer = DEFAULT_PORT): Integer;
begin
    Result := port;
end;

var result: Integer;
begin
    result := Connect('localhost');       // port=DEFAULT_PORT (80)
    result := Connect('localhost', 443);  // port=443
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
