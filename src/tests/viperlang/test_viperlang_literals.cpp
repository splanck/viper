//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for ViperLang literal expressions and basic syntax.
//
//===----------------------------------------------------------------------===//

#include "frontends/viperlang/Compiler.hpp"
#include "il/core/Opcode.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
#include <string>

using namespace il::frontends::viperlang;
using namespace il::support;

namespace
{

//===----------------------------------------------------------------------===//
// Integer Literals
//===----------------------------------------------------------------------===//

/// @brief Test decimal integer literals.
TEST(ViperLangLiterals, DecimalIntegers)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer a = 0;
    Integer b = 42;
    Integer c = 123456789;
    Integer d = -100;
    Viper.Terminal.SayInt(a);
    Viper.Terminal.SayInt(b);
    Viper.Terminal.SayInt(c);
    Viper.Terminal.SayInt(d);
}
)";
    CompilerInput input{.source = source, .path = "decimal.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test hexadecimal integer literals.
TEST(ViperLangLiterals, HexIntegers)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer a = 0x0;
    Integer b = 0xFF;
    Integer c = 0xDEADBEEF;
    Integer d = 0x1a2B3c;
    Viper.Terminal.SayInt(a);
    Viper.Terminal.SayInt(b);
    Viper.Terminal.SayInt(c);
    Viper.Terminal.SayInt(d);
}
)";
    CompilerInput input{.source = source, .path = "hex.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test binary integer literals.
TEST(ViperLangLiterals, BinaryIntegers)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer a = 0b0;
    Integer b = 0b1;
    Integer c = 0b1010;
    Integer d = 0b11111111;
    Viper.Terminal.SayInt(a);
    Viper.Terminal.SayInt(b);
    Viper.Terminal.SayInt(c);
    Viper.Terminal.SayInt(d);
}
)";
    CompilerInput input{.source = source, .path = "binary.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Floating-Point Literals
//===----------------------------------------------------------------------===//

/// @brief Test floating-point literals with var inference.
TEST(ViperLangLiterals, FloatingPoint)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var a = 0.0;
    var b = 3.14159;
    var c = 1.0;
    Viper.Terminal.Say("floats work");
}
)";
    CompilerInput input{.source = source, .path = "float.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test scientific notation with var inference.
TEST(ViperLangLiterals, ScientificNotation)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var a = 1e10;
    var b = 2.5e-3;
    Viper.Terminal.Say("scientific notation works");
}
)";
    CompilerInput input{.source = source, .path = "scientific.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// String Literals
//===----------------------------------------------------------------------===//

/// @brief Test basic string literals.
TEST(ViperLangLiterals, BasicStrings)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    String a = "hello";
    String b = "world";
    String c = "";
    String d = "Hello, World!";
    Viper.Terminal.Say(a);
    Viper.Terminal.Say(b);
    Viper.Terminal.Say(c);
    Viper.Terminal.Say(d);
}
)";
    CompilerInput input{.source = source, .path = "strings.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test string escape sequences.
TEST(ViperLangLiterals, StringEscapes)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    String a = "line1\nline2";
    String b = "tab\there";
    String c = "quote\"inside";
    String d = "backslash\\path";
    Viper.Terminal.Say(a);
    Viper.Terminal.Say(b);
    Viper.Terminal.Say(c);
    Viper.Terminal.Say(d);
}
)";
    CompilerInput input{.source = source, .path = "escapes.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Boolean Literals
//===----------------------------------------------------------------------===//

/// @brief Test boolean literals.
TEST(ViperLangLiterals, Booleans)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Boolean a = true;
    Boolean b = false;
    Viper.Terminal.SayBool(a);
    Viper.Terminal.SayBool(b);
}
)";
    CompilerInput input{.source = source, .path = "booleans.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Null Literal
//===----------------------------------------------------------------------===//

/// @brief Test null literal with optional types.
TEST(ViperLangLiterals, NullLiteral)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    String? a = null;
    Integer? b = null;
    if a == null {
        Viper.Terminal.Say("a is null");
    }
}
)";
    CompilerInput input{.source = source, .path = "null.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// List Literals
//===----------------------------------------------------------------------===//

/// @brief Test list literals.
TEST(ViperLangLiterals, ListLiterals)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var empty: List[Integer] = [];
    var numbers = [1, 2, 3, 4, 5];
    var strings = ["a", "b", "c"];
    Viper.Terminal.SayInt(numbers.count());
    Viper.Terminal.SayInt(strings.count());
}
)";
    CompilerInput input{.source = source, .path = "lists.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Tuple Literals
//===----------------------------------------------------------------------===//

/// @brief Test tuple literals.
TEST(ViperLangLiterals, TupleLiterals)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var pair = (1, "hello");
    var triple = (true, 42, 3.14);
    Viper.Terminal.SayInt(pair.0);
    Viper.Terminal.Say(pair.1);
}
)";
    CompilerInput input{.source = source, .path = "tuples.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
