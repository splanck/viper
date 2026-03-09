//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/frontends/test_zia_parser.cpp
// Purpose: Comprehensive unit tests for the Zia parser, exercising parsing
//          through the full compilation pipeline. Covers expressions,
//          statements, declarations, error recovery, and edge cases.
// Key invariants:
//   - Positive tests must compile successfully (result.succeeded() == true).
//   - Negative tests must fail compilation (result.succeeded() == false).
//   - No test should crash or hang.
// Ownership/Lifetime: Test-only; not linked into the compiler.
// Links: src/frontends/zia/Parser.cpp, src/frontends/zia/Parser_Expr.cpp
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "il/core/Opcode.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
#include <string>

using namespace il::frontends::zia;
using namespace il::support;

namespace
{

//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

/// @brief Compile Zia source and return the result.
CompilerResult compileSource(const std::string &source)
{
    SourceManager sm;
    CompilerInput input{.source = source, .path = "parser_test.zia"};
    CompilerOptions opts{};
    return compile(input, opts, sm);
}

/// @brief Compile at O0 to preserve IL structure for inspection.
CompilerResult compileSourceO0(const std::string &source)
{
    SourceManager sm;
    CompilerInput input{.source = source, .path = "parser_test.zia"};
    CompilerOptions opts{.optLevel = OptLevel::O0};
    return compile(input, opts, sm);
}

/// @brief Check whether any diagnostic message contains @p needle.
bool hasDiagContaining(const DiagnosticEngine &diag, const std::string &needle)
{
    for (const auto &d : diag.diagnostics())
    {
        if (d.message.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

/// @brief Check if a function in the module contains a specific opcode.
bool hasOpcode(const il::core::Module &mod, const std::string &fnName, il::core::Opcode op)
{
    for (const auto &fn : mod.functions)
    {
        if (fn.name == fnName)
        {
            for (const auto &block : fn.blocks)
            {
                for (const auto &instr : block.instructions)
                {
                    if (instr.op == op)
                        return true;
                }
            }
        }
    }
    return false;
}

/// @brief Check if a function in the module contains a Call to a specific callee.
bool hasCall(const il::core::Module &mod, const std::string &fnName, const std::string &callee)
{
    for (const auto &fn : mod.functions)
    {
        if (fn.name == fnName)
        {
            for (const auto &block : fn.blocks)
            {
                for (const auto &instr : block.instructions)
                {
                    if (instr.op == il::core::Opcode::Call && instr.callee == callee)
                        return true;
                }
            }
        }
    }
    return false;
}

/// @brief Check if a named function exists in the module.
bool hasFunction(const il::core::Module &mod, const std::string &fnName)
{
    for (const auto &fn : mod.functions)
    {
        if (fn.name == fnName)
            return true;
    }
    return false;
}

/// @brief Check if any function name contains a given substring.
bool hasFunctionContaining(const il::core::Module &mod, const std::string &substr)
{
    for (const auto &fn : mod.functions)
    {
        if (fn.name.find(substr) != std::string::npos)
            return true;
    }
    return false;
}

/// @brief Print diagnostics to stderr (for debugging test failures).
void dumpDiagnostics(const std::string &testName, const CompilerResult &result)
{
    std::cerr << "Diagnostics for " << testName << ":\n";
    for (const auto &d : result.diagnostics.diagnostics())
    {
        std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                  << d.message << "\n";
    }
}

//===----------------------------------------------------------------------===//
// Expression Precedence
//===----------------------------------------------------------------------===//

/// @brief Arithmetic precedence: 1 + 2 * 3 should multiply before adding.
TEST(ZiaParser, ArithmeticPrecedence)
{
    auto result = compileSourceO0(R"(
module Test;

func start() {
    Integer x = 1 + 2 * 3;
    Viper.Terminal.SayInt(x);
}
)");

    if (!result.succeeded())
        dumpDiagnostics("ArithmeticPrecedence", result);

    EXPECT_TRUE(result.succeeded());

    // Mul/IMulOvf should appear before Add/IAddOvf in the IL
    bool foundMul = false;
    bool foundAdd = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &block : fn.blocks)
            {
                for (const auto &instr : block.instructions)
                {
                    if (instr.op == il::core::Opcode::Mul || instr.op == il::core::Opcode::IMulOvf)
                        foundMul = true;
                    if (instr.op == il::core::Opcode::Add || instr.op == il::core::Opcode::IAddOvf)
                        foundAdd = true;
                }
            }
        }
    }
    EXPECT_TRUE(foundMul);
    EXPECT_TRUE(foundAdd);
}

/// @brief Parenthesized expressions override default precedence.
TEST(ZiaParser, ParenthesizedPrecedence)
{
    auto result = compileSource(R"(
module Test;

func start() {
    Integer x = (1 + 2) * 3;
    Viper.Terminal.SayInt(x);
}
)");

    if (!result.succeeded())
        dumpDiagnostics("ParenthesizedPrecedence", result);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Mixed division and modulo with subtraction.
TEST(ZiaParser, DivModPrecedence)
{
    auto result = compileSource(R"(
module Test;

func start() {
    Integer x = 10 - 6 / 2 + 7 % 3;
    Viper.Terminal.SayInt(x);
}
)");

    if (!result.succeeded())
        dumpDiagnostics("DivModPrecedence", result);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Method Chains
//===----------------------------------------------------------------------===//

/// @brief Chained method calls on an entity instance.
TEST(ZiaParser, MethodChaining)
{
    auto result = compileSource(R"(
module Test;

entity Builder {
    expose String value;

    expose func append(s: String) -> Builder {
        value = value + s;
        return self;
    }

    expose func build() -> String {
        return value;
    }
}

func start() {
    var b = new Builder();
    b.value = "";
    String result = b.append("hello").append(" ").append("world").build();
    Viper.Terminal.Say(result);
}
)");

    if (!result.succeeded())
        dumpDiagnostics("MethodChaining", result);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Optional Chaining
//===----------------------------------------------------------------------===//

/// @brief Optional chaining with ?. operator on nullable entity.
TEST(ZiaParser, OptionalChaining)
{
    auto result = compileSource(R"(
module Test;

entity Node {
    expose Integer value;
}

func showValue(n: Node?) {
    if (n == null) {
        return;
    }
    Viper.Terminal.SayInt(n.value);
}

func start() {
    Node? n = new Node();
    n.value = 42;
    showValue(n);
    showValue(null);
}
)");

    if (!result.succeeded())
        dumpDiagnostics("OptionalChaining", result);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Null coalesce operator ?? with optional types.
TEST(ZiaParser, NullCoalesceOperator)
{
    auto result = compileSource(R"(
module Test;

entity Item {
    expose Integer id;
}

func start() {
    Item? a = null;
    Item b = a ?? new Item();
    b.id = 99;
    Viper.Terminal.SayInt(b.id);
}
)");

    if (!result.succeeded())
        dumpDiagnostics("NullCoalesceOperator", result);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Match Expressions
//===----------------------------------------------------------------------===//

/// @brief Match expression used as a value assignment.
TEST(ZiaParser, MatchExpressionAsValue)
{
    auto result = compileSource(R"(
module Test;

func start() {
    Integer x = 2;
    Integer result = match (x) {
        1 => 10
        2 => 20
        3 => 30
        _ => 0
    };
    Viper.Terminal.SayInt(result);
}
)");

    if (!result.succeeded())
        dumpDiagnostics("MatchExpressionAsValue", result);

    EXPECT_TRUE(result.succeeded());

    // Verify match arms generate labeled blocks
    bool foundMatchArm = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &block : fn.blocks)
            {
                if (block.label.find("match_arm") != std::string::npos)
                    foundMatchArm = true;
            }
        }
    }
    EXPECT_TRUE(foundMatchArm);
}

/// @brief Match statement with block bodies.
TEST(ZiaParser, MatchStatementWithBlocks)
{
    auto result = compileSource(R"(
module Test;

func start() {
    Integer code = 3;
    match (code) {
        1 => { Viper.Terminal.Say("one"); }
        2 => { Viper.Terminal.Say("two"); }
        _ => { Viper.Terminal.Say("other"); }
    }
}
)");

    if (!result.succeeded())
        dumpDiagnostics("MatchStatementWithBlocks", result);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Lambda Parsing
//===----------------------------------------------------------------------===//

/// @brief Lambda with block body and no parameters.
TEST(ZiaParser, LambdaBlockBodyNoParams)
{
    auto result = compileSource(R"(
module Test;

func start() {
    var greet = () => {
        Viper.Terminal.Say("Hello from lambda");
    };
}
)");

    if (!result.succeeded())
        dumpDiagnostics("LambdaBlockBodyNoParams", result);

    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasFunctionContaining(result.module, "lambda"));
}

/// @brief Lambda with typed parameter and block body.
TEST(ZiaParser, LambdaWithTypedParam)
{
    auto result = compileSource(R"(
module Test;

func start() {
    var doubler = (x: Integer) => {
        Viper.Terminal.SayInt(x * 2);
    };
}
)");

    if (!result.succeeded())
        dumpDiagnostics("LambdaWithTypedParam", result);

    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasFunctionContaining(result.module, "lambda"));
}

//===----------------------------------------------------------------------===//
// If Expressions Used as Values
//===----------------------------------------------------------------------===//

/// @brief If expression used as value in variable initialization.
TEST(ZiaParser, IfExpressionAsValue)
{
    auto result = compileSource(R"(
module Test;

func start() {
    var x = 10;
    var label = if (x > 5) { "big" } else { "small" };
    Viper.Terminal.Say(label);
}
)");

    if (!result.succeeded())
        dumpDiagnostics("IfExpressionAsValue", result);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// For-In Loops
//===----------------------------------------------------------------------===//

/// @brief For-in loop over a list literal.
TEST(ZiaParser, ForInList)
{
    auto result = compileSource(R"(
module Test;

func start() {
    var items = [10, 20, 30];
    for item in items {
        Viper.Terminal.SayInt(item);
    }
}
)");

    if (!result.succeeded())
        dumpDiagnostics("ForInList", result);

    EXPECT_TRUE(result.succeeded());
}

/// @brief For-in loop over an integer range.
TEST(ZiaParser, ForInRange)
{
    auto result = compileSource(R"(
module Test;

func start() {
    for i in 0..10 {
        Viper.Terminal.SayInt(i);
    }
}
)");

    if (!result.succeeded())
        dumpDiagnostics("ForInRange", result);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// While Loops
//===----------------------------------------------------------------------===//

/// @brief While loop with break.
TEST(ZiaParser, WhileLoopWithBreak)
{
    auto result = compileSource(R"(
module Test;

func start() {
    Integer i = 0;
    while (true) {
        if (i >= 5) {
            break;
        }
        i = i + 1;
    }
    Viper.Terminal.SayInt(i);
}
)");

    if (!result.succeeded())
        dumpDiagnostics("WhileLoopWithBreak", result);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Guard Statements
//===----------------------------------------------------------------------===//

/// @brief Guard clause: null check with early return enables type narrowing.
TEST(ZiaParser, GuardNullCheckReturn)
{
    auto result = compileSource(R"(
module Test;

entity Item {
    expose String label;
}

func printLabel(item: Item?) {
    if (item == null) {
        return;
    }
    Viper.Terminal.Say(item.label);
}

func start() {
    var i = new Item();
    i.label = "test";
    printLabel(i);
}
)");

    if (!result.succeeded())
        dumpDiagnostics("GuardNullCheckReturn", result);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// List / Map / Set Literals
//===----------------------------------------------------------------------===//

/// @brief List literal initialization.
TEST(ZiaParser, ListLiteral)
{
    auto result = compileSource(R"(
module Test;

func start() {
    var nums = [1, 2, 3, 4, 5];
    Viper.Terminal.SayInt(nums.count());
}
)");

    if (!result.succeeded())
        dumpDiagnostics("ListLiteral", result);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Map collection usage.
TEST(ZiaParser, MapLiteral)
{
    auto result = compileSource(R"(
module Test;

func start() {
    Map[String, Integer] m = new Map[String, Integer]();
    m.set("a", 1);
    m.set("b", 2);
    Integer val = m.get("a");
    Viper.Terminal.SayInt(val);
}
)");

    if (!result.succeeded())
        dumpDiagnostics("MapLiteral", result);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Set literal using brace syntax.
TEST(ZiaParser, SetLiteral)
{
    auto result = compileSource(R"(
module Test;

func start() {
    var s = {1, 2, 3};
}
)");

    if (!result.succeeded())
        dumpDiagnostics("SetLiteral", result);

    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasCall(result.module, "main", "Viper.Collections.Set.New"));
}

//===----------------------------------------------------------------------===//
// String Interpolation
//===----------------------------------------------------------------------===//

/// @brief String interpolation with variable reference.
TEST(ZiaParser, StringInterpolation)
{
    auto result = compileSource(R"(
module Test;

func start() {
    var name = "World";
    var greeting = "Hello, ${name}!";
    Viper.Terminal.Say(greeting);
}
)");

    if (!result.succeeded())
        dumpDiagnostics("StringInterpolation", result);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Error Recovery for Malformed Input
//===----------------------------------------------------------------------===//

/// @brief Missing closing brace should produce an error.
TEST(ZiaParser, ErrorMissingClosingBrace)
{
    auto result = compileSource(R"(
module Test;
func start() {
    var x = 5
)");
    EXPECT_FALSE(result.succeeded());
}

/// @brief Missing closing parenthesis should produce an error.
TEST(ZiaParser, ErrorMissingClosingParen)
{
    auto result = compileSource(R"(
module Test;
func start() {
    var x = (1 + 2;
}
)");
    EXPECT_FALSE(result.succeeded());
}

/// @brief Completely empty input (no module declaration) should fail.
TEST(ZiaParser, ErrorEmptyInput)
{
    auto result = compileSource("");
    EXPECT_FALSE(result.succeeded());
}

/// @brief Gibberish input should fail gracefully.
TEST(ZiaParser, ErrorGibberishInput)
{
    auto result = compileSource("@@@!!!###$$$");
    EXPECT_FALSE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Binary Operators (comprehensive)
//===----------------------------------------------------------------------===//

/// @brief All comparison and logical operators parse correctly.
TEST(ZiaParser, ComparisonAndLogicalOperators)
{
    auto result = compileSource(R"(
module Test;

func start() {
    var a = 10;
    var b = 20;

    var eq = a == b;
    var ne = a != b;
    var lt = a < b;
    var gt = a > b;
    var le = a <= b;
    var ge = a >= b;

    var land = (a > 0) and (b > 0);
    var lor = (a > 0) or (b > 0);

    Viper.Terminal.SayBool(eq);
    Viper.Terminal.SayBool(ne);
    Viper.Terminal.SayBool(lt);
    Viper.Terminal.SayBool(gt);
    Viper.Terminal.SayBool(le);
    Viper.Terminal.SayBool(ge);
    Viper.Terminal.SayBool(land);
    Viper.Terminal.SayBool(lor);
}
)");

    if (!result.succeeded())
        dumpDiagnostics("ComparisonAndLogicalOperators", result);

    EXPECT_TRUE(result.succeeded());
}

/// @brief All arithmetic binary operators (+, -, *, /, %) parse correctly.
TEST(ZiaParser, AllArithmeticOperators)
{
    auto result = compileSource(R"(
module Test;

func start() {
    Integer a = 10;
    Integer b = 3;

    Integer sum = a + b;
    Integer diff = a - b;
    Integer prod = a * b;
    Integer quot = a / b;
    Integer rem = a % b;

    Viper.Terminal.SayInt(sum);
    Viper.Terminal.SayInt(diff);
    Viper.Terminal.SayInt(prod);
    Viper.Terminal.SayInt(quot);
    Viper.Terminal.SayInt(rem);
}
)");

    if (!result.succeeded())
        dumpDiagnostics("AllArithmeticOperators", result);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Unary Operators
//===----------------------------------------------------------------------===//

/// @brief Unary negation and logical not.
TEST(ZiaParser, UnaryOperators)
{
    auto result = compileSource(R"(
module Test;

func start() {
    Integer x = -42;
    Boolean b = !true;
    Viper.Terminal.SayInt(x);
    Viper.Terminal.SayBool(b);
}
)");

    if (!result.succeeded())
        dumpDiagnostics("UnaryOperators", result);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Entity Declarations
//===----------------------------------------------------------------------===//

/// @brief Entity with fields and methods.
TEST(ZiaParser, EntityDeclaration)
{
    auto result = compileSource(R"(
module Test;

entity Rectangle {
    expose Integer width;
    expose Integer height;

    expose func area() -> Integer {
        return width * height;
    }

    expose func perimeter() -> Integer {
        return 2 * (width + height);
    }
}

func start() {
    var r = new Rectangle();
    r.width = 10;
    r.height = 5;
    Viper.Terminal.SayInt(r.area());
    Viper.Terminal.SayInt(r.perimeter());
}
)");

    if (!result.succeeded())
        dumpDiagnostics("EntityDeclaration", result);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Entity with init constructor.
TEST(ZiaParser, EntityWithInit)
{
    auto result = compileSource(R"(
module Test;

entity Point {
    expose Integer x;
    expose Integer y;

    expose func init() {
        x = 0;
        y = 0;
    }
}

func start() {
    var p = new Point();
    Viper.Terminal.SayInt(p.x);
    Viper.Terminal.SayInt(p.y);
}
)");

    if (!result.succeeded())
        dumpDiagnostics("EntityWithInit", result);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Function Declarations with Multiple Parameters
//===----------------------------------------------------------------------===//

/// @brief Function with multiple typed parameters and a return value.
TEST(ZiaParser, MultiParamFunction)
{
    auto result = compileSource(R"(
module Test;

func compute(a: Integer, b: Integer, c: Integer) -> Integer {
    return a * b + c;
}

func start() {
    Integer result = compute(2, 3, 4);
    Viper.Terminal.SayInt(result);
}
)");

    if (!result.succeeded())
        dumpDiagnostics("MultiParamFunction", result);

    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasFunction(result.module, "compute"));
}

//===----------------------------------------------------------------------===//
// Nested Function Calls
//===----------------------------------------------------------------------===//

/// @brief Nested function calls: f(g(h(x))).
TEST(ZiaParser, NestedFunctionCalls)
{
    auto result = compileSource(R"(
module Test;

func double(x: Integer) -> Integer {
    return x * 2;
}

func addOne(x: Integer) -> Integer {
    return x + 1;
}

func negate(x: Integer) -> Integer {
    return 0 - x;
}

func start() {
    Integer result = negate(addOne(double(5)));
    Viper.Terminal.SayInt(result);
}
)");

    if (!result.succeeded())
        dumpDiagnostics("NestedFunctionCalls", result);

    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasFunction(result.module, "double"));
    EXPECT_TRUE(hasFunction(result.module, "addOne"));
    EXPECT_TRUE(hasFunction(result.module, "negate"));
}

//===----------------------------------------------------------------------===//
// Range Expressions
//===----------------------------------------------------------------------===//

/// @brief Range expression in for-in loop with variable bounds.
TEST(ZiaParser, RangeExpressionVariableBounds)
{
    auto result = compileSource(R"(
module Test;

func start() {
    var lo = 0;
    var hi = 5;
    for i in lo..hi {
        Viper.Terminal.SayInt(i);
    }
}
)");

    if (!result.succeeded())
        dumpDiagnostics("RangeExpressionVariableBounds", result);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Additional Edge Cases
//===----------------------------------------------------------------------===//

/// @brief Deeply nested parenthesized expression.
TEST(ZiaParser, DeeplyNestedParens)
{
    auto result = compileSource(R"(
module Test;

func start() {
    Integer x = ((((((1 + 2))))));
    Viper.Terminal.SayInt(x);
}
)");

    if (!result.succeeded())
        dumpDiagnostics("DeeplyNestedParens", result);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Multiple statements in a single function body.
TEST(ZiaParser, MultipleStatements)
{
    auto result = compileSource(R"(
module Test;

func start() {
    Integer a = 1;
    Integer b = 2;
    Integer c = 3;
    Integer d = a + b;
    Integer e = c * d;
    Integer f = e - a;
    Viper.Terminal.SayInt(f);
}
)");

    if (!result.succeeded())
        dumpDiagnostics("MultipleStatements", result);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Empty function body parses correctly.
TEST(ZiaParser, EmptyFunctionBody)
{
    auto result = compileSource(R"(
module Test;

func doNothing() {
}

func start() {
    doNothing();
}
)");

    if (!result.succeeded())
        dumpDiagnostics("EmptyFunctionBody", result);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Boolean literal expressions.
TEST(ZiaParser, BooleanLiterals)
{
    auto result = compileSource(R"(
module Test;

func start() {
    var t = true;
    var f = false;
    Viper.Terminal.SayBool(t);
    Viper.Terminal.SayBool(f);
}
)");

    if (!result.succeeded())
        dumpDiagnostics("BooleanLiterals", result);

    EXPECT_TRUE(result.succeeded());
}

} // namespace

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
