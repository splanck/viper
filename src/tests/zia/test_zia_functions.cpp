//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for Zia function declarations and calls.
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "il/core/Opcode.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
#include <string>

using namespace il::frontends::zia;
using namespace il::support;

namespace {

//===----------------------------------------------------------------------===//
// Basic Functions
//===----------------------------------------------------------------------===//

/// @brief Test function with no parameters and no return.
TEST(ZiaFunctions, VoidNoParams) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func sayHello() {    Viper.Terminal.Say("Hello!");
}

func start() {    sayHello();
}
)";
    CompilerInput input{.source = source, .path = "voidnoparam.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test function with parameters.
TEST(ZiaFunctions, WithParameters) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func greet(name: String) {    Viper.Terminal.Say("Hello, " + name + "!");
}

func addNumbers(a: Integer, b: Integer) {    Viper.Terminal.SayInt(a + b);
}

func start() {    greet("World");
    addNumbers(5, 3);
}
)";
    CompilerInput input{.source = source, .path = "params.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test function with return value (arrow syntax).
TEST(ZiaFunctions, ReturnValueArrow) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func add(a: Integer, b: Integer) -> Integer {    return a + b;
}

func multiply(a: Integer, b: Integer) -> Integer {    return a * b;
}

func isEven(n: Integer) -> Boolean {    return n % 2 == 0;
}

func start() {    var sum: Integer = add(10, 20);
    var product: Integer = multiply(5, 6);
    var even: Boolean = isEven(4);

    Viper.Terminal.SayInt(sum);
    Viper.Terminal.SayInt(product);
    Viper.Terminal.SayBool(even);
}
)";
    CompilerInput input{.source = source, .path = "returnarrow.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test canonical arrow return syntax with typed locals.
TEST(ZiaFunctions, ReturnValueCanonicalArrow) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func add(a: Integer, b: Integer) -> Integer {    return a + b;
}

func multiply(a: Integer, b: Integer) -> Integer {    return a * b;
}

func start() {    var sum: Integer = add(10, 20);
    var product: Integer = multiply(5, 6);

    Viper.Terminal.SayInt(sum);
    Viper.Terminal.SayInt(product);
}
)";
    CompilerInput input{.source = source, .path = "returncolon.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Parameter Styles
//===----------------------------------------------------------------------===//

/// @brief Test Swift-style parameters (name: Type).
TEST(ZiaFunctions, SwiftStyleParams) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func process(value: Integer, factor: Integer) -> Integer {    return value * factor;
}

func start() {    var result: Integer = process(10, 2);
    Viper.Terminal.SayInt(result);
}
)";
    CompilerInput input{.source = source, .path = "swiftparams.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test named arguments, including reordered argument binding.
TEST(ZiaFunctions, NamedArgumentsReorder) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func formatPair(first: Integer, second: Integer) -> Integer {    return first * 10 + second;
}

func start() {    var result: Integer = formatPair(second: 2, first: 4);
    Viper.Terminal.SayInt(result);
}
)";
    CompilerInput input{.source = source, .path = "namedargs.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test single-expression function declarations.
TEST(ZiaFunctions, SingleExpressionFunctions) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func double(value: Integer) -> Integer = value * 2;
func square(value: Integer) -> Integer = value * value;

func start() {
    var a: Integer = double(5);
    var b: Integer = square(6);
    Viper.Terminal.SayInt(a + b);
}
)";
    CompilerInput input{.source = source, .path = "singleexpr.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Recursion
//===----------------------------------------------------------------------===//

/// @brief Test recursive function (factorial).
TEST(ZiaFunctions, Recursion) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func factorial(n: Integer) -> Integer {    if n <= 1 {
        return 1;
    }
    return n * factorial(n - 1);
}

func start() {    Viper.Terminal.SayInt(factorial(5));
}
)";
    CompilerInput input{.source = source, .path = "recursion.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test mutual recursion.
TEST(ZiaFunctions, MutualRecursion) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func isEven(n: Integer) -> Boolean {    if n == 0 {
        return true;
    }
    return isOdd(n - 1);
}

func isOdd(n: Integer) -> Boolean {    if n == 0 {
        return false;
    }
    return isEven(n - 1);
}

func start() {    Viper.Terminal.SayBool(isEven(4));
    Viper.Terminal.SayBool(isOdd(5));
}
)";
    CompilerInput input{.source = source, .path = "mutual.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Multiple Return Paths
//===----------------------------------------------------------------------===//

/// @brief Test early return.
TEST(ZiaFunctions, EarlyReturn) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func findIndex(items: List[Integer], target: Integer) -> Integer {    var i = 0;
    for item in items {
        if item == target {
            return i;
        }
        i = i + 1;
    }
    return -1;
}

func start() {    var list = [10, 20, 30, 40, 50];
    var idx: Integer = findIndex(list, 30);
    Viper.Terminal.SayInt(idx);
}
)";
    CompilerInput input{.source = source, .path = "earlyret.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test conditional return.
TEST(ZiaFunctions, ConditionalReturn) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func sign(n: Integer) -> Integer {    if n > 0 {
        return 1;
    } else {
        if n < 0 {
            return -1;
        } else {
            return 0;
        }
    }
}

func start() {    Viper.Terminal.SayInt(sign(42));
    Viper.Terminal.SayInt(sign(-17));
    Viper.Terminal.SayInt(sign(0));
}
)";
    CompilerInput input{.source = source, .path = "condret.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Functions with Collections
//===----------------------------------------------------------------------===//

/// @brief Test function taking list parameter.
TEST(ZiaFunctions, ListParameter) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func sum(numbers: List[Integer]) -> Integer {    var total = 0;
    for n in numbers {
        total = total + n;
    }
    return total;
}

func start() {    var nums = [1, 2, 3, 4, 5];
    var result: Integer = sum(nums);
    Viper.Terminal.SayInt(result);
}
)";
    CompilerInput input{.source = source, .path = "listparam.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test function returning list.
TEST(ZiaFunctions, ListReturn) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func range(start: Integer, end: Integer) -> List[Integer] {    var result: List[Integer] = [];
    var i = start;
    while i < end {
        result.add(i);
        i = i + 1;
    }
    return result;
}

func start() {    var nums = range(1, 6);
    Viper.Terminal.SayInt(nums.count());
}
)";
    CompilerInput input{.source = source, .path = "listret.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Functions with Optional Types
//===----------------------------------------------------------------------===//

/// @brief Test function returning optional.
TEST(ZiaFunctions, OptionalReturn) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func findFirst(items: List[Integer], target: Integer) -> Integer? {    for item in items {
        if item == target {
            return item;
        }
    }
    return null;
}

func start() {    var list = [1, 2, 3, 4, 5];
    var found: Integer? = findFirst(list, 3);
    var notFound: Integer? = findFirst(list, 10);

    if found != null {
        Viper.Terminal.Say("found");
    }
    if notFound == null {
        Viper.Terminal.Say("not found");
    }
}
)";
    CompilerInput input{.source = source, .path = "optret.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test function with optional parameter.
TEST(ZiaFunctions, OptionalParameter) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func printValue(value: Integer?) {    if value != null {
        Viper.Terminal.SayInt(value ?? 0);
    } else {
        Viper.Terminal.Say("no value");
    }
}

func start() {    printValue(42);
    printValue(null);
}
)";
    CompilerInput input{.source = source, .path = "optparam.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Higher-Order Functions
//===----------------------------------------------------------------------===//

/// @brief Test function that could take callbacks (simplified without lambda syntax).
TEST(ZiaFunctions, CallbackPattern) {
    SourceManager sm;
    const std::string source = R"(
module Test;

func double(value: Integer) -> Integer {    return value * 2;
}

func square(value: Integer) -> Integer {    return value * value;
}

func start() {    var doubled: Integer = double(5);
    var squared: Integer = square(4);

    Viper.Terminal.SayInt(doubled);
    Viper.Terminal.SayInt(squared);
}
)";
    CompilerInput input{.source = source, .path = "callback.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

} // namespace

int main() {
    return viper_test::run_all_tests();
}
