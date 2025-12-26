//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for ViperLang function declarations and calls.
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
// Basic Functions
//===----------------------------------------------------------------------===//

/// @brief Test function with no parameters and no return.
TEST(ViperLangFunctions, VoidNoParams)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func sayHello() {
    Viper.Terminal.Say("Hello!");
}

func start() {
    sayHello();
}
)";
    CompilerInput input{.source = source, .path = "voidnoparam.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test function with parameters.
TEST(ViperLangFunctions, WithParameters)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func greet(String name) {
    Viper.Terminal.Say("Hello, " + name + "!");
}

func addNumbers(Integer a, Integer b) {
    Viper.Terminal.SayInt(a + b);
}

func start() {
    greet("World");
    addNumbers(5, 3);
}
)";
    CompilerInput input{.source = source, .path = "params.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test function with return value (arrow syntax).
TEST(ViperLangFunctions, ReturnValueArrow)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func add(Integer a, Integer b) -> Integer {
    return a + b;
}

func multiply(Integer a, Integer b) -> Integer {
    return a * b;
}

func isEven(Integer n) -> Boolean {
    return n % 2 == 0;
}

func start() {
    Integer sum = add(10, 20);
    Integer product = multiply(5, 6);
    Boolean even = isEven(4);

    Viper.Terminal.SayInt(sum);
    Viper.Terminal.SayInt(product);
    Viper.Terminal.SayBool(even);
}
)";
    CompilerInput input{.source = source, .path = "returnarrow.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test function with return value (colon syntax).
TEST(ViperLangFunctions, ReturnValueColon)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func add(Integer a, Integer b): Integer {
    return a + b;
}

func multiply(Integer a, Integer b): Integer {
    return a * b;
}

func start() {
    Integer sum = add(10, 20);
    Integer product = multiply(5, 6);

    Viper.Terminal.SayInt(sum);
    Viper.Terminal.SayInt(product);
}
)";
    CompilerInput input{.source = source, .path = "returncolon.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Parameter Styles
//===----------------------------------------------------------------------===//

/// @brief Test Swift-style parameters (name: Type).
TEST(ViperLangFunctions, SwiftStyleParams)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func process(value: Integer, factor: Integer) -> Integer {
    return value * factor;
}

func start() {
    Integer result = process(10, 2);
    Viper.Terminal.SayInt(result);
}
)";
    CompilerInput input{.source = source, .path = "swiftparams.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test Java-style parameters (Type name).
TEST(ViperLangFunctions, JavaStyleParams)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func process(Integer value, Integer factor) -> Integer {
    return value * factor;
}

func start() {
    Integer result = process(10, 2);
    Viper.Terminal.SayInt(result);
}
)";
    CompilerInput input{.source = source, .path = "javaparams.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Recursion
//===----------------------------------------------------------------------===//

/// @brief Test recursive function (factorial).
TEST(ViperLangFunctions, Recursion)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func factorial(Integer n) -> Integer {
    if n <= 1 {
        return 1;
    }
    return n * factorial(n - 1);
}

func start() {
    Viper.Terminal.SayInt(factorial(5));
}
)";
    CompilerInput input{.source = source, .path = "recursion.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test mutual recursion.
TEST(ViperLangFunctions, MutualRecursion)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func isEven(Integer n) -> Boolean {
    if n == 0 {
        return true;
    }
    return isOdd(n - 1);
}

func isOdd(Integer n) -> Boolean {
    if n == 0 {
        return false;
    }
    return isEven(n - 1);
}

func start() {
    Viper.Terminal.SayBool(isEven(4));
    Viper.Terminal.SayBool(isOdd(5));
}
)";
    CompilerInput input{.source = source, .path = "mutual.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Multiple Return Paths
//===----------------------------------------------------------------------===//

/// @brief Test early return.
TEST(ViperLangFunctions, EarlyReturn)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func findIndex(List[Integer] items, Integer target) -> Integer {
    var i = 0;
    for item in items {
        if item == target {
            return i;
        }
        i = i + 1;
    }
    return -1;
}

func start() {
    var list = [10, 20, 30, 40, 50];
    Integer idx = findIndex(list, 30);
    Viper.Terminal.SayInt(idx);
}
)";
    CompilerInput input{.source = source, .path = "earlyret.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test conditional return.
TEST(ViperLangFunctions, ConditionalReturn)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func sign(Integer n) -> Integer {
    if n > 0 {
        return 1;
    } else {
        if n < 0 {
            return -1;
        } else {
            return 0;
        }
    }
}

func start() {
    Viper.Terminal.SayInt(sign(42));
    Viper.Terminal.SayInt(sign(-17));
    Viper.Terminal.SayInt(sign(0));
}
)";
    CompilerInput input{.source = source, .path = "condret.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Functions with Collections
//===----------------------------------------------------------------------===//

/// @brief Test function taking list parameter.
TEST(ViperLangFunctions, ListParameter)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func sum(List[Integer] numbers) -> Integer {
    var total = 0;
    for n in numbers {
        total = total + n;
    }
    return total;
}

func start() {
    var nums = [1, 2, 3, 4, 5];
    Integer result = sum(nums);
    Viper.Terminal.SayInt(result);
}
)";
    CompilerInput input{.source = source, .path = "listparam.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test function returning list.
TEST(ViperLangFunctions, ListReturn)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func range(Integer start, Integer end) -> List[Integer] {
    var result: List[Integer] = [];
    var i = start;
    while i < end {
        result.add(i);
        i = i + 1;
    }
    return result;
}

func start() {
    var nums = range(1, 6);
    Viper.Terminal.SayInt(nums.count());
}
)";
    CompilerInput input{.source = source, .path = "listret.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Functions with Optional Types
//===----------------------------------------------------------------------===//

/// @brief Test function returning optional.
TEST(ViperLangFunctions, OptionalReturn)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func findFirst(List[Integer] items, Integer target) -> Integer? {
    for item in items {
        if item == target {
            return item;
        }
    }
    return null;
}

func start() {
    var list = [1, 2, 3, 4, 5];
    Integer? found = findFirst(list, 3);
    Integer? notFound = findFirst(list, 10);

    if found != null {
        Viper.Terminal.Say("found");
    }
    if notFound == null {
        Viper.Terminal.Say("not found");
    }
}
)";
    CompilerInput input{.source = source, .path = "optret.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test function with optional parameter.
TEST(ViperLangFunctions, OptionalParameter)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func printValue(Integer? value) {
    if value != null {
        Viper.Terminal.SayInt(value ?? 0);
    } else {
        Viper.Terminal.Say("no value");
    }
}

func start() {
    printValue(42);
    printValue(null);
}
)";
    CompilerInput input{.source = source, .path = "optparam.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Higher-Order Functions
//===----------------------------------------------------------------------===//

/// @brief Test function that could take callbacks (simplified without lambda syntax).
TEST(ViperLangFunctions, CallbackPattern)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func double(Integer value) -> Integer {
    return value * 2;
}

func square(Integer value) -> Integer {
    return value * value;
}

func start() {
    Integer doubled = double(5);
    Integer squared = square(4);

    Viper.Terminal.SayInt(doubled);
    Viper.Terminal.SayInt(squared);
}
)";
    CompilerInput input{.source = source, .path = "callback.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
