//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for Zia operators (arithmetic, comparison, logical, bitwise).
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
// Arithmetic Operators
//===----------------------------------------------------------------------===//

/// @brief Test addition operator.
TEST(ZiaOperators, Addition)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer a = 10 + 20;
    Integer b = -5 + 5;
    var c = 1.5 + 2.5;
    Viper.Terminal.SayInt(a);
    Viper.Terminal.SayInt(b);
}
)";
    CompilerInput input{.source = source, .path = "add.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test subtraction operator.
TEST(ZiaOperators, Subtraction)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer a = 30 - 10;
    Integer b = 5 - 10;
    var c = 5.0 - 2.5;
    Viper.Terminal.SayInt(a);
    Viper.Terminal.SayInt(b);
}
)";
    CompilerInput input{.source = source, .path = "sub.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test multiplication operator.
TEST(ZiaOperators, Multiplication)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer a = 6 * 7;
    Integer b = -3 * 4;
    var c = 2.5 * 4.0;
    Viper.Terminal.SayInt(a);
    Viper.Terminal.SayInt(b);
}
)";
    CompilerInput input{.source = source, .path = "mul.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test division operator.
TEST(ZiaOperators, Division)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer a = 20 / 4;
    Integer b = 17 / 5;
    var c = 10.0 / 4.0;
    Viper.Terminal.SayInt(a);
    Viper.Terminal.SayInt(b);
}
)";
    CompilerInput input{.source = source, .path = "div.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test modulo operator.
TEST(ZiaOperators, Modulo)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer a = 17 % 5;
    Integer b = 10 % 3;
    Integer c = 8 % 4;
    Viper.Terminal.SayInt(a);
    Viper.Terminal.SayInt(b);
    Viper.Terminal.SayInt(c);
}
)";
    CompilerInput input{.source = source, .path = "mod.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test unary negation.
TEST(ZiaOperators, UnaryNegation)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer a = 5;
    Integer b = -a;
    Integer c = -(-10);
    var d = -3.14;
    Viper.Terminal.SayInt(b);
    Viper.Terminal.SayInt(c);
}
)";
    CompilerInput input{.source = source, .path = "neg.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test operator precedence.
TEST(ZiaOperators, Precedence)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    // Multiplication before addition
    Integer a = 2 + 3 * 4;  // 14, not 20

    // Parentheses override precedence
    Integer b = (2 + 3) * 4;  // 20

    // Mixed operations
    Integer c = 10 - 2 * 3 + 4;  // 10 - 6 + 4 = 8

    Viper.Terminal.SayInt(a);
    Viper.Terminal.SayInt(b);
    Viper.Terminal.SayInt(c);
}
)";
    CompilerInput input{.source = source, .path = "precedence.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Comparison Operators
//===----------------------------------------------------------------------===//

/// @brief Test equality operators.
TEST(ZiaOperators, Equality)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Boolean a = 5 == 5;
    Boolean b = 5 == 6;
    Boolean c = 5 != 6;
    Boolean d = 5 != 5;
    Viper.Terminal.SayBool(a);
    Viper.Terminal.SayBool(b);
    Viper.Terminal.SayBool(c);
    Viper.Terminal.SayBool(d);
}
)";
    CompilerInput input{.source = source, .path = "equality.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test relational operators.
TEST(ZiaOperators, Relational)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Boolean a = 5 < 10;
    Boolean b = 10 < 5;
    Boolean c = 5 <= 5;
    Boolean d = 5 > 3;
    Boolean e = 3 > 5;
    Boolean f = 5 >= 5;
    Viper.Terminal.SayBool(a);
    Viper.Terminal.SayBool(b);
    Viper.Terminal.SayBool(c);
    Viper.Terminal.SayBool(d);
    Viper.Terminal.SayBool(e);
    Viper.Terminal.SayBool(f);
}
)";
    CompilerInput input{.source = source, .path = "relational.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test string comparison.
TEST(ZiaOperators, StringComparison)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Boolean a = "hello" == "hello";
    Boolean b = "hello" == "world";
    Boolean c = "hello" != "world";
    Boolean d = "" == "";
    Viper.Terminal.SayBool(a);
    Viper.Terminal.SayBool(b);
    Viper.Terminal.SayBool(c);
    Viper.Terminal.SayBool(d);
}
)";
    CompilerInput input{.source = source, .path = "strcomp.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Logical Operators
//===----------------------------------------------------------------------===//

/// @brief Test logical AND (both forms).
TEST(ZiaOperators, LogicalAnd)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Boolean a = true && true;
    Boolean b = true && false;
    Boolean c = false && true;
    Boolean d = false && false;

    // Word form
    Boolean e = true and true;
    Boolean f = true and false;

    Viper.Terminal.SayBool(a);
    Viper.Terminal.SayBool(b);
    Viper.Terminal.SayBool(c);
    Viper.Terminal.SayBool(d);
    Viper.Terminal.SayBool(e);
    Viper.Terminal.SayBool(f);
}
)";
    CompilerInput input{.source = source, .path = "logand.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test logical OR (both forms).
TEST(ZiaOperators, LogicalOr)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Boolean a = true || true;
    Boolean b = true || false;
    Boolean c = false || true;
    Boolean d = false || false;

    // Word form
    Boolean e = true or false;
    Boolean f = false or false;

    Viper.Terminal.SayBool(a);
    Viper.Terminal.SayBool(b);
    Viper.Terminal.SayBool(c);
    Viper.Terminal.SayBool(d);
    Viper.Terminal.SayBool(e);
    Viper.Terminal.SayBool(f);
}
)";
    CompilerInput input{.source = source, .path = "logor.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test logical NOT (both forms).
TEST(ZiaOperators, LogicalNot)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Boolean a = !true;
    Boolean b = !false;
    Boolean c = !!true;

    // Word form
    Boolean d = not true;
    Boolean e = not false;

    Viper.Terminal.SayBool(a);
    Viper.Terminal.SayBool(b);
    Viper.Terminal.SayBool(c);
    Viper.Terminal.SayBool(d);
    Viper.Terminal.SayBool(e);
}
)";
    CompilerInput input{.source = source, .path = "lognot.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test short-circuit evaluation.
TEST(ZiaOperators, ShortCircuit)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

var counter: Integer = 0;

func increment() -> Boolean {
    counter = counter + 1;
    return true;
}

func start() {
    // With &&, second operand not evaluated if first is false
    Boolean a = false && increment();

    // With ||, second operand not evaluated if first is true
    Boolean b = true || increment();

    Viper.Terminal.SayInt(counter);  // Should be 0
}
)";
    CompilerInput input{.source = source, .path = "shortcircuit.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Ternary Operator
//===----------------------------------------------------------------------===//

/// @brief Test ternary conditional operator.
TEST(ZiaOperators, Ternary)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer a = true ? 1 : 2;
    Integer b = false ? 1 : 2;
    Integer x = 5;
    Integer c = x > 0 ? x : -x;  // abs

    Viper.Terminal.SayInt(a);
    Viper.Terminal.SayInt(b);
    Viper.Terminal.SayInt(c);
}
)";
    CompilerInput input{.source = source, .path = "ternary.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test nested ternary expressions.
TEST(ZiaOperators, NestedTernary)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer x = 5;
    String result = x < 0 ? "negative" : (x == 0 ? "zero" : "positive");
    Viper.Terminal.Say(result);
}
)";
    CompilerInput input{.source = source, .path = "nestedternary.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Assignment Operators
//===----------------------------------------------------------------------===//

/// @brief Test basic assignment.
TEST(ZiaOperators, Assignment)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var x = 10;
    x = 20;
    x = x + 5;
    Viper.Terminal.SayInt(x);
}
)";
    CompilerInput input{.source = source, .path = "assign.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test chained assignment.
TEST(ZiaOperators, ChainedAssignment)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var a = 1;
    var b = 2;
    var c = 3;

    // Chained reassignment
    a = b = c = 10;

    Viper.Terminal.SayInt(a);
    Viper.Terminal.SayInt(b);
    Viper.Terminal.SayInt(c);
}
)";
    CompilerInput input{.source = source, .path = "chainassign.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Null-Related Operators
//===----------------------------------------------------------------------===//

/// @brief Test null coalescing operator.
TEST(ZiaOperators, NullCoalescing)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    String? a = null;
    String? b = "hello";

    String c = a ?? "default";
    String d = b ?? "default";

    Viper.Terminal.Say(c);
    Viper.Terminal.Say(d);
}
)";
    CompilerInput input{.source = source, .path = "coalesce.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test optional chaining operator.
TEST(ZiaOperators, OptionalChaining)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Person {
    expose String name;
}

func start() {
    Person? person = null;
    String? name = person?.name;

    if name == null {
        Viper.Terminal.Say("no name");
    }
}
)";
    CompilerInput input{.source = source, .path = "optchain.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
