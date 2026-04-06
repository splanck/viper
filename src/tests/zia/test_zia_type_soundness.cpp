//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Type system soundness tests for the Zia frontend.
//
// Each test tries to break the type system in a specific way and verifies the
// compiler either rejects the code with a clear diagnostic or (for known gaps)
// documents that unsound code compiles silently.
//
// Gaps are prefixed with GAP_ and use EXPECT_TRUE(result.succeeded()) with
// documentary comments. When a gap is fixed, the test will start failing
// (the compiler rejects the code), signalling the assertion should flip.
//
//===----------------------------------------------------------------------===//

#include "frontends/zia/Compiler.hpp"
#include "support/source_manager.hpp"
#include "tests/TestHarness.hpp"
#include <string>

using namespace il::frontends::zia;
using namespace il::support;

namespace {

/// Compile Zia source and return the result.
CompilerResult compileSource(const std::string &source, SourceManager &sm) {
    CompilerInput input{.source = source, .path = "soundness.zia"};
    CompilerOptions opts{};
    return compile(input, opts, sm);
}

/// Check if any error diagnostic contains the given substring.
bool hasErrorContaining(const CompilerResult &result, const std::string &needle) {
    for (const auto &d : result.diagnostics.diagnostics()) {
        if (d.severity == Severity::Error && d.message.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

/// Check if any warning diagnostic contains the given substring.
bool hasWarningContaining(const CompilerResult &result, const std::string &needle) {
    for (const auto &d : result.diagnostics.diagnostics()) {
        if (d.severity == Severity::Warning && d.message.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

//=============================================================================
// Category 1: Incompatible Type Assignment
//=============================================================================

TEST(ZiaTypeSoundness, AssignStringToInteger) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
/// @brief Start.
func start() {    var x: Integer = "hello";
}
)",
                                sm);
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, AssignBooleanToString) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
/// @brief Start.
func start() {    var s: String = true;
}
)",
                                sm);
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, AssignIntegerToBoolean) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
/// @brief Start.
func start() {    var b: Boolean = 42;
}
)",
                                sm);
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, AssignNumberToInteger) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
/// @brief Start.
func start() {    var x: Integer = 3.14;
}
)",
                                sm);
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, AssignWrongListType) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
/// @brief Start.
func start() {    var s: List[String] = [1, 2, 3];
}
)",
                                sm);
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, AssignUnrelatedEntities) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
class Cat { expose Integer lives; }
class Dog { expose Integer age; }
/// @brief Start.
func start() {    var c: Cat = new Dog();
}
)",
                                sm);
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, AssignEntityToValueType) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
struct Point { Integer x; Integer y; }
class Dog { expose Integer age; }
/// @brief Start.
func start() {    var p: Point = new Dog();
}
)",
                                sm);
    EXPECT_FALSE(result.succeeded());
}

//=============================================================================
// Category 2: Wrong Argument Types to Functions
//=============================================================================

TEST(ZiaTypeSoundness, GAP_WrongArgTypeStringForInt) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
/// @brief Add.
func add(a: Integer, b: Integer) -> Integer {    return a + b;
}
/// @brief Start.
func start() {    var x: Integer = add("hello", 5);
}
)",
                                sm);
    // FIXED GAP-7: Function call argument types are now validated.
    // Passing a String where Integer is expected is rejected.
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, GAP_WrongArgTypeIntForEntity) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
class Dog { expose Integer age; }
/// @brief Pet dog.
func petDog(d: Dog) {    Viper.Terminal.SayInt(d.age);
}
/// @brief Start.
func start() {    petDog(42);
}
)",
                                sm);
    // FIXED GAP-7: Integer argument for class parameter is now rejected.
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, GAP_TooManyArguments) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
/// @brief Greet.
func greet(name: String) {    Viper.Terminal.Say(name);
}
/// @brief Start.
func start() {    greet("Alice", "Bob");
}
)",
                                sm);
    // FIXED GAP-7: Extra arguments are now rejected.
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, GAP_TooFewArguments) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
/// @brief Add.
func add(a: Integer, b: Integer) -> Integer {    return a + b;
}
/// @brief Start.
func start() {    var x: Integer = add(1);
}
)",
                                sm);
    // FIXED GAP-7: Missing arguments are now rejected.
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, GAP_MethodWrongArgType) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
class Counter {
    expose Integer count;
    expose func addAmount(amount: Integer) {        count = count + amount;
    }
}
/// @brief Start.
func start() {    var c: Counter = new Counter();
    c.addAmount("ten");
}
)",
                                sm);
    // FIXED GAP-7: Method call argument types are now validated.
    EXPECT_FALSE(result.succeeded());
}

//=============================================================================
// Category 3: Non-Existent Fields
//=============================================================================

TEST(ZiaTypeSoundness, NonExistentFieldOnEntity) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
class Dog { expose Integer age; }
/// @brief Start.
func start() {    var d: Dog = new Dog();
    var c: Integer = d.color;
}
)",
                                sm);
    // FIXED GAP-6: Missing field on class is now caught at sema level.
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasErrorContaining(result, "color") || hasErrorContaining(result, "member"));
}

TEST(ZiaTypeSoundness, PrivateFieldAccess) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
class Secret {
    Integer hidden;
    expose func getHidden() -> Integer { return hidden; }}
/// @brief Start.
func start() {    var s: Secret = new Secret();
    var x: Integer = s.hidden;
}
)",
                                sm);
    // Private (unexposed) field should be rejected
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, FieldOnPrimitive) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
/// @brief Start.
func start() {    var x: Integer = 42;
    var y: Integer = x.value;
}
)",
                                sm);
    // FIXED GAP-6: Primitives have no fields — now rejected at sema level.
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasErrorContaining(result, "member") || hasErrorContaining(result, "value"));
}

TEST(ZiaTypeSoundness, NonExistentFieldOnValue) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
struct Point { Integer x; Integer y; }
/// @brief Start.
func start() {    var p: Point = new Point(1, 2);
    var z: Integer = p.z;
}
)",
                                sm);
    // FIXED GAP-6: Missing field on value type is now caught at sema level.
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasErrorContaining(result, "z") || hasErrorContaining(result, "member"));
}

//=============================================================================
// Category 4: Uninitialized Variables
//=============================================================================

TEST(ZiaTypeSoundness, GAP_UninitializedVariable) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
/// @brief Start.
func start() {    var x: Integer;
    var y: Integer = x + 1;
    Viper.Terminal.SayInt(y);
}
)",
                                sm);
    // FIXED GAP-1: Definite-assignment analysis now warns on uninitialized use.
    // Code still compiles (warning, not error) but the warning is emitted.
    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasWarningContaining(result, "initialization"));
}

TEST(ZiaTypeSoundness, GAP_ConditionalInit) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
/// @brief Start.
func start() {    var x: Integer;
    var cond: Boolean = true;
    if cond {
        x = 10;
    }
    var y: Integer = x + 1;
    Viper.Terminal.SayInt(y);
}
)",
                                sm);
    // FIXED GAP-2: Flow-sensitive initialization analysis warns when variable
    // is only initialized in one branch of an if-statement.
    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasWarningContaining(result, "initialization"));
}

TEST(ZiaTypeSoundness, UndefinedVariable) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
/// @brief Start.
func start() {    var y: Integer = x + 1;
}
)",
                                sm);
    // Completely undeclared variable — must be rejected
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasErrorContaining(result, "undefined") || hasErrorContaining(result, "Undefined"));
}

TEST(ZiaTypeSoundness, UseVariableWithoutTypeOrInit) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
/// @brief Start.
func start() {    var x;
}
)",
                                sm);
    // var without type annotation or initializer — cannot infer type
    EXPECT_FALSE(result.succeeded());
}

//=============================================================================
// Category 5: Integer-to-Pointer Coercions
//=============================================================================

TEST(ZiaTypeSoundness, AssignIntegerToEntity) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
class Foo { expose Integer val; }
/// @brief Start.
func start() {    var f: Foo = 42;
}
)",
                                sm);
    // Direct integer-to-class assignment — must be rejected
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, GAP_UncheckedAsCastIntToEntity) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
class Foo { expose Integer val; }
/// @brief Start.
func start() {    var n: Integer = 12345;
    var f: Foo = n as Foo;
}
)",
                                sm);
    // FIXED GAP-3: 'as' cast now validates type compatibility.
    // Integer-to-Entity cast is rejected.
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, GAP_AsCastEntityToInteger) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
class Foo { expose Integer val; }
/// @brief Start.
func start() {    var f: Foo = new Foo();
    var n: Integer = f as Integer;
}
)",
                                sm);
    // FIXED GAP-3: Entity-to-Integer cast is now rejected.
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, AssignNullToNonOptional) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
/// @brief Start.
func start() {    var x: Integer = null;
}
)",
                                sm);
    // null is only assignable to Optional types — must be rejected
    EXPECT_FALSE(result.succeeded());
}

//=============================================================================
// Category 6: Implicit Narrowing Conversions
//=============================================================================

TEST(ZiaTypeSoundness, NumberVariableToByte) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
/// @brief Start.
func start() {    var n: Number = 300.5;
    var b: Byte = n;
}
)",
                                sm);
    // Number -> Byte is narrowing — must be rejected
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, LargeIntLiteralToByte) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
/// @brief Start.
func start() {    var b: Byte = 300;
}
)",
                                sm);
    // 300 > 255 — outside Byte literal range, must be rejected
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, IntegerVariableToByte) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
/// @brief Start.
func start() {    var big: Integer = 1000;
    var b: Byte = big;
}
)",
                                sm);
    // Integer variable -> Byte is narrowing (not a literal, no range check)
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, IntegerAsCondition) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
/// @brief Start.
func start() {    var x: Integer = 42;
    if x {
        Viper.Terminal.Say("truthy");
    }
}
)",
                                sm);
    // Condition must be Boolean — Integer is not Boolean
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasErrorContaining(result, "Boolean") || hasErrorContaining(result, "Condition"));
}

TEST(ZiaTypeSoundness, NumberReturnFromIntegerFunc) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
/// @brief Half.
func half(n: Integer) -> Integer {    var result: Number = n / 2.0;
    return result;
}
/// @brief Start.
func start() {    var x: Integer = half(10);
}
)",
                                sm);
    // Intentional special case: Number -> Integer is allowed in return
    // statements to support Floor/Ceil/Round/Trunc results.
    // See Sema_Stmt.cpp:334-335.
    EXPECT_TRUE(result.succeeded());
}

//=============================================================================
// Category 7: Null/Nil Dereference Scenarios
//=============================================================================

TEST(ZiaTypeSoundness, GAP_NullOptionalFieldAccess) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
class Dog {
    expose Integer age;
}
/// @brief Start.
func start() {    var d: Dog? = null;
    var a: Integer = d.age;
}
)",
                                sm);
    // FIXED GAP-4: Optional auto-unwrap now emits a null-safety warning.
    // Code still compiles (no flow-sensitive analysis yet) but warns.
    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasWarningContaining(result, "Optional"));
}

TEST(ZiaTypeSoundness, GAP_NullOptionalMethodCall) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
class Dog {
    expose Integer age;
    expose func bark() -> String { return "Woof"; }}
/// @brief Start.
func start() {    var d: Dog? = null;
    var s: String = d.bark();
}
)",
                                sm);
    // FIXED GAP-5: Optional auto-unwrap now warns on method calls too.
    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasWarningContaining(result, "Optional"));
}

TEST(ZiaTypeSoundness, ReturnNullFromNonOptionalFunc) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
/// @brief Get value.
func getValue() -> Integer {    return null;
}
/// @brief Start.
func start() {    var x: Integer = getValue();
}
)",
                                sm);
    // Returning null from a non-Optional return type — must be rejected
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, GAP_PassOptionalToNonOptionalParam) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
/// @brief Double.
func double(n: Integer) -> Integer {    return n + n;
}
/// @brief Start.
func start() {    var x: Integer? = null;
    var y: Integer = double(x);
}
)",
                                sm);
    // FIXED GAP-8: Optional[Integer] where Integer is expected is now rejected
    // by the argument type validation added in the GAP-7 fix.
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, OptionalAcceptsInnerType) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
/// @brief Start.
func start() {    var x: Integer? = 42;
    Viper.Terminal.SayInt(x);
}
)",
                                sm);
    // Optional accepts its inner type — this is correct behavior
    EXPECT_TRUE(result.succeeded());
}

//=============================================================================
// Operator-Level Tests
//=============================================================================

TEST(ZiaTypeSoundness, LogicalAndWithIntegers) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
/// @brief Start.
func start() {    var a: Integer = 1;
    var b: Integer = 2;
    var c: Boolean = a && b;
}
)",
                                sm);
    // Logical AND requires Boolean operands
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasErrorContaining(result, "Boolean") || hasErrorContaining(result, "Logical"));
}

TEST(ZiaTypeSoundness, BitwiseOrWithFloats) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
/// @brief Start.
func start() {    var a: Number = 1.5;
    var b: Number = 2.5;
    var c: Integer = a | b;
}
)",
                                sm);
    // Bitwise OR requires integral operands
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasErrorContaining(result, "integral") || hasErrorContaining(result, "Bitwise"));
}

TEST(ZiaTypeSoundness, NegateString) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
/// @brief Start.
func start() {    var s: String = "hello";
    var n: Integer = -s;
}
)",
                                sm);
    // Negation requires numeric operand
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasErrorContaining(result, "numeric") || hasErrorContaining(result, "Negation"));
}

//=============================================================================
// Positive Correctness Tests (must compile)
//=============================================================================

TEST(ZiaTypeSoundness, IntegerToNumberWidening) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
/// @brief Start.
func start() {    var x: Number = 42;
}
)",
                                sm);
    // Integer -> Number is a valid widening conversion
    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaTypeSoundness, ByteLiteralInRange) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
/// @brief Start.
func start() {    var b: Byte = 200;
}
)",
                                sm);
    // 200 is in [0,255] — valid Byte literal
    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaTypeSoundness, EmptyListInference) {
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
/// @brief Start.
func start() {    var x: List[Integer] = [];
}
)",
                                sm);
    // Empty list with declared element type — valid
    EXPECT_TRUE(result.succeeded());
}

} // namespace

int main() {
    return viper_test::run_all_tests();
}
