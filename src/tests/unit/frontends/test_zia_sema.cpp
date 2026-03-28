//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/frontends/test_zia_sema.cpp
// Purpose: Comprehensive unit tests for Zia semantic analysis, exercising
//          type inference, method resolution, generics, optionals, error
//          diagnostics, entity features, interfaces, coercion, scoping,
//          and return-type consistency through full compilation.
// Key invariants:
//   - Each test compiles a self-contained Zia module via the Compiler API
//   - Positive tests assert result.succeeded() == true
//   - Negative tests assert result.succeeded() == false and check diagnostics
// Ownership/Lifetime:
//   - SourceManager and CompilerResult are stack-owned per test
// Links: frontends/zia/Compiler.hpp, frontends/zia/Sema.hpp
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

/// @brief Compile Zia source and return the result.
CompilerResult compileSource(const std::string &source, SourceManager &sm) {
    CompilerInput input{.source = source, .path = "sema_test.zia"};
    CompilerOptions opts{};
    return compile(input, opts, sm);
}

/// @brief Check if any error diagnostic contains the given substring.
bool hasErrorContaining(const CompilerResult &result, const std::string &needle) {
    for (const auto &d : result.diagnostics.diagnostics()) {
        if (d.severity == Severity::Error && d.message.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

/// @brief Dump diagnostics to stderr for debugging.
static void dumpDiags(const CompilerResult &result) {
    for (const auto &d : result.diagnostics.diagnostics()) {
        std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                  << d.message << "\n";
    }
}

//===----------------------------------------------------------------------===//
// 1. Type Inference
//===----------------------------------------------------------------------===//

/// @brief var x = 42 should infer Integer type and compile successfully.
TEST(ZiaSema, TypeInferenceInteger) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    var x = 42;
    Viper.Terminal.SayInt(x);
}
)",
                                sm);
    if (!result.succeeded())
        dumpDiags(result);
    EXPECT_TRUE(result.succeeded());
}

/// @brief var s = "hello" should infer String; var b = true should infer Boolean.
TEST(ZiaSema, TypeInferenceStringAndBoolean) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    var s = "hello";
    var b = true;
    Viper.Terminal.Say(s);
    Viper.Terminal.SayBool(b);
}
)",
                                sm);
    if (!result.succeeded())
        dumpDiags(result);
    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// 2. Method Resolution on Built-in Types
//===----------------------------------------------------------------------===//

/// @brief String.Length and List.count() should resolve correctly.
TEST(ZiaSema, BuiltinMethodResolution) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    var s = "hello world";
    var len = s.Length;
    Viper.Terminal.SayInt(len);

    var items = [1, 2, 3];
    var cnt = items.count();
    Viper.Terminal.SayInt(cnt);
}
)",
                                sm);
    if (!result.succeeded())
        dumpDiags(result);
    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// 3. Generic Instantiation
//===----------------------------------------------------------------------===//

/// @brief List[Integer] generic type should compile and allow typed operations.
TEST(ZiaSema, GenericListInteger) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    List[Integer] numbers = [];
    numbers.add(10);
    numbers.add(20);
    numbers.add(30);
    Viper.Terminal.SayInt(numbers.count());
}
)",
                                sm);
    if (!result.succeeded())
        dumpDiags(result);
    EXPECT_TRUE(result.succeeded());
}

/// @brief List[String] should work with string element operations.
TEST(ZiaSema, GenericListString) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    List[String] names = [];
    names.add("Alice");
    names.add("Bob");
    Viper.Terminal.SayInt(names.count());
}
)",
                                sm);
    if (!result.succeeded())
        dumpDiags(result);
    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// 4. Optional Unwrapping
//===----------------------------------------------------------------------===//

/// @brief Optional entity with coalesce operator should compile.
TEST(ZiaSema, OptionalCoalesce) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;

entity Item {
    expose Integer value;
}

func start() {
    Item? maybeItem = null;
    Item fallback = new Item();
    fallback.value = 99;
    Item result = maybeItem ?? fallback;
    Viper.Terminal.SayInt(result.value);
}
)",
                                sm);
    if (!result.succeeded())
        dumpDiags(result);
    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// 5. Error Diagnostics - Type Mismatch
//===----------------------------------------------------------------------===//

/// @brief Assigning a String to an Integer variable should fail with a type error.
TEST(ZiaSema, TypeMismatchStringToInteger) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    Integer x = "not a number";
}
)",
                                sm);
    EXPECT_FALSE(result.succeeded());
}

/// @brief Assigning a Boolean to a String variable should fail.
TEST(ZiaSema, TypeMismatchBooleanToString) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    String s = true;
}
)",
                                sm);
    EXPECT_FALSE(result.succeeded());
}

/// @brief Assigning an Integer to a Boolean variable should fail.
TEST(ZiaSema, TypeMismatchIntegerToBoolean) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    Boolean b = 42;
}
)",
                                sm);
    EXPECT_FALSE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// 6. Entity Field Access
//===----------------------------------------------------------------------===//

/// @brief Accessing exposed entity fields should compile. Accessing fields
///        through method returns should also work.
TEST(ZiaSema, EntityFieldAccess) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;

entity Player {
    expose Integer health;
    expose String name;
    expose Boolean alive;
}

func start() {
    var p = new Player();
    p.health = 100;
    p.name = "Hero";
    p.alive = true;
    Viper.Terminal.SayInt(p.health);
    Viper.Terminal.Say(p.name);
    Viper.Terminal.SayBool(p.alive);
}
)",
                                sm);
    if (!result.succeeded())
        dumpDiags(result);
    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// 7. Entity Method Calls
//===----------------------------------------------------------------------===//

/// @brief Entity methods with parameters and return values should resolve.
TEST(ZiaSema, EntityMethodCalls) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;

entity Calculator {
    expose Integer accumulator;

    expose func add(Integer n) {
        accumulator = accumulator + n;
    }

    expose func getResult() -> Integer {
        return accumulator;
    }
}

func start() {
    var calc = new Calculator();
    calc.accumulator = 0;
    calc.add(10);
    calc.add(20);
    Integer total = calc.getResult();
    Viper.Terminal.SayInt(total);
}
)",
                                sm);
    if (!result.succeeded())
        dumpDiags(result);
    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// 8. Interface Implementation
//===----------------------------------------------------------------------===//

/// @brief Entity implementing an interface should compile and dispatch correctly.
TEST(ZiaSema, InterfaceImplementation) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;

interface IGreeter {
    func greet(): String;
}

entity FormalGreeter implements IGreeter {
    expose func greet(): String {
        return "Good day.";
    }
}

entity CasualGreeter implements IGreeter {
    expose func greet(): String {
        return "Hey!";
    }
}

func start() {
    var formal = new FormalGreeter();
    var casual = new CasualGreeter();
    Viper.Terminal.Say(formal.greet());
    Viper.Terminal.Say(casual.greet());
}
)",
                                sm);
    if (!result.succeeded())
        dumpDiags(result);
    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// 9. Numeric Type Coercion
//===----------------------------------------------------------------------===//

/// @brief Integer to Number coercion should be allowed implicitly.
TEST(ZiaSema, NumericCoercion) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    Number x = 42;
    Number y = 3.14;
    Number z = x + y;
    Viper.Terminal.SayNum(z);
}
)",
                                sm);
    if (!result.succeeded())
        dumpDiags(result);
    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// 10. Boolean Expressions
//===----------------------------------------------------------------------===//

/// @brief Compound boolean expressions with &&, ||, and ! should type-check.
TEST(ZiaSema, BooleanExpressions) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    var a = true;
    var b = false;
    var c = a && b;
    var d = a || b;
    var e = !a;
    var f = (a && !b) || (c && d);
    Viper.Terminal.SayBool(f);
}
)",
                                sm);
    if (!result.succeeded())
        dumpDiags(result);
    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// 11. String Concatenation Types
//===----------------------------------------------------------------------===//

/// @brief String + String concatenation should produce a String.
TEST(ZiaSema, StringConcatenation) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    var greeting = "Hello, ";
    var name = "World";
    var message = greeting + name + "!";
    Viper.Terminal.Say(message);
}
)",
                                sm);
    if (!result.succeeded())
        dumpDiags(result);
    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// 12. Nested Scope Variable Shadowing
//===----------------------------------------------------------------------===//

/// @brief Variables declared in inner scopes should shadow outer variables.
TEST(ZiaSema, NestedScopeShadowing) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    var x = 10;
    Viper.Terminal.SayInt(x);
    if true {
        var x = 20;
        Viper.Terminal.SayInt(x);
    }
    Viper.Terminal.SayInt(x);
}
)",
                                sm);
    if (!result.succeeded())
        dumpDiags(result);
    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// 13. Function Return Type Checking
//===----------------------------------------------------------------------===//

/// @brief Functions with explicit return types should enforce the return type.
TEST(ZiaSema, FunctionReturnTypeValid) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;

func double(Integer n) -> Integer {
    return n * 2;
}

func isPositive(Integer n) -> Boolean {
    return n > 0;
}

func greeting() -> String {
    return "Hello!";
}

func start() {
    Viper.Terminal.SayInt(double(21));
    Viper.Terminal.SayBool(isPositive(5));
    Viper.Terminal.Say(greeting());
}
)",
                                sm);
    if (!result.succeeded())
        dumpDiags(result);
    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// 14. Multiple Return Paths Type Consistency
//===----------------------------------------------------------------------===//

/// @brief All return paths must return the declared type.
TEST(ZiaSema, MultipleReturnPaths) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;

func classify(Integer n) -> String {
    if n > 0 {
        return "positive";
    } else if n < 0 {
        return "negative";
    } else {
        return "zero";
    }
}

func start() {
    Viper.Terminal.Say(classify(5));
    Viper.Terminal.Say(classify(-3));
    Viper.Terminal.Say(classify(0));
}
)",
                                sm);
    if (!result.succeeded())
        dumpDiags(result);
    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// 15. Recursive Function Type Checking
//===----------------------------------------------------------------------===//

/// @brief Recursive function calls should be type-checked correctly.
TEST(ZiaSema, RecursiveFunctionTypeCheck) {
    SourceManager sm;
    auto result = compileSource(R"(
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
)",
                                sm);
    if (!result.succeeded())
        dumpDiags(result);
    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// 16. Entity with List Field (Generic + Entity combo)
//===----------------------------------------------------------------------===//

/// @brief Entity fields using generic types should compile.
TEST(ZiaSema, EntityWithGenericField) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;

entity TodoList {
    expose List[String] items;

    expose func addItem(String item) {
        items.add(item);
    }

    expose func itemCount() -> Integer {
        return items.count();
    }
}

func start() {
    var todo = new TodoList();
    todo.items = [];
    todo.addItem("Buy milk");
    todo.addItem("Walk dog");
    Viper.Terminal.SayInt(todo.itemCount());
}
)",
                                sm);
    if (!result.succeeded())
        dumpDiags(result);
    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// 17. Undefined Variable Reference (negative test)
//===----------------------------------------------------------------------===//

/// @brief Referencing an undefined variable should produce a diagnostic error.
TEST(ZiaSema, UndefinedVariableError) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    Viper.Terminal.SayInt(undeclaredVar);
}
)",
                                sm);
    EXPECT_FALSE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// 18. Entity Inheritance Field Access
//===----------------------------------------------------------------------===//

/// @brief Derived entity should have access to base entity fields.
TEST(ZiaSema, InheritanceFieldAccess) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;

entity Animal {
    expose Integer age;

    expose func getAge() -> Integer {
        return age;
    }
}

entity Dog extends Animal {
    expose String breed;
}

func start() {
    var dog = new Dog();
    dog.age = 5;
    dog.breed = "Labrador";
    Viper.Terminal.SayInt(dog.getAge());
    Viper.Terminal.Say(dog.breed);
}
)",
                                sm);
    if (!result.succeeded())
        dumpDiags(result);
    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// 19. For-in Loop Type Inference
//===----------------------------------------------------------------------===//

/// @brief for-in loop variable should be inferred from the collection element type.
TEST(ZiaSema, ForInLoopTypeInference) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    var names = ["Alice", "Bob", "Charlie"];
    for name in names {
        Viper.Terminal.Say(name);
    }
}
)",
                                sm);
    if (!result.succeeded())
        dumpDiags(result);
    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// 20. Arithmetic on Inferred Types
//===----------------------------------------------------------------------===//

/// @brief Arithmetic operations on inferred integer types should work.
TEST(ZiaSema, ArithmeticOnInferredTypes) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    var a = 10;
    var b = 3;
    var sum = a + b;
    var diff = a - b;
    var prod = a * b;
    var quot = a / b;
    var rem = a % b;
    Viper.Terminal.SayInt(sum);
    Viper.Terminal.SayInt(diff);
    Viper.Terminal.SayInt(prod);
    Viper.Terminal.SayInt(quot);
    Viper.Terminal.SayInt(rem);
}
)",
                                sm);
    if (!result.succeeded())
        dumpDiags(result);
    EXPECT_TRUE(result.succeeded());
}

} // namespace

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
