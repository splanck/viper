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

namespace
{

/// Compile Zia source and return the result.
CompilerResult compileSource(const std::string &source, SourceManager &sm)
{
    CompilerInput input{.source = source, .path = "soundness.zia"};
    CompilerOptions opts{};
    return compile(input, opts, sm);
}

/// Check if any error diagnostic contains the given substring.
bool hasErrorContaining(const CompilerResult &result, const std::string &needle)
{
    for (const auto &d : result.diagnostics.diagnostics())
    {
        if (d.severity == Severity::Error && d.message.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

/// Check if any warning diagnostic contains the given substring.
bool hasWarningContaining(const CompilerResult &result, const std::string &needle)
{
    for (const auto &d : result.diagnostics.diagnostics())
    {
        if (d.severity == Severity::Warning && d.message.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

//=============================================================================
// Category 1: Incompatible Type Assignment
//=============================================================================

TEST(ZiaTypeSoundness, AssignStringToInteger)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    Integer x = "hello";
}
)",
                                sm);
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, AssignBooleanToString)
{
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

TEST(ZiaTypeSoundness, AssignIntegerToBoolean)
{
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

TEST(ZiaTypeSoundness, AssignNumberToInteger)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    Integer x = 3.14;
}
)",
                                sm);
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, AssignWrongListType)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    List[String] s = [1, 2, 3];
}
)",
                                sm);
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, AssignUnrelatedEntities)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
entity Cat { expose Integer lives; }
entity Dog { expose Integer age; }
func start() {
    Cat c = new Dog();
}
)",
                                sm);
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, AssignEntityToValueType)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
value Point { Integer x; Integer y; }
entity Dog { expose Integer age; }
func start() {
    Point p = new Dog();
}
)",
                                sm);
    EXPECT_FALSE(result.succeeded());
}

//=============================================================================
// Category 2: Wrong Argument Types to Functions
//=============================================================================

TEST(ZiaTypeSoundness, GAP_WrongArgTypeStringForInt)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func add(Integer a, Integer b) -> Integer {
    return a + b;
}
func start() {
    Integer x = add("hello", 5);
}
)",
                                sm);
    // FIXED GAP-7: Function call argument types are now validated.
    // Passing a String where Integer is expected is rejected.
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, GAP_WrongArgTypeIntForEntity)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
entity Dog { expose Integer age; }
func petDog(Dog d) {
    Viper.Terminal.SayInt(d.age);
}
func start() {
    petDog(42);
}
)",
                                sm);
    // FIXED GAP-7: Integer argument for entity parameter is now rejected.
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, GAP_TooManyArguments)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func greet(String name) {
    Viper.Terminal.Say(name);
}
func start() {
    greet("Alice", "Bob");
}
)",
                                sm);
    // FIXED GAP-7: Extra arguments are now rejected.
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, GAP_TooFewArguments)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func add(Integer a, Integer b) -> Integer {
    return a + b;
}
func start() {
    Integer x = add(1);
}
)",
                                sm);
    // FIXED GAP-7: Missing arguments are now rejected.
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, GAP_MethodWrongArgType)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
entity Counter {
    expose Integer count;
    expose func addAmount(Integer amount) {
        count = count + amount;
    }
}
func start() {
    Counter c = new Counter();
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

TEST(ZiaTypeSoundness, NonExistentFieldOnEntity)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
entity Dog { expose Integer age; }
func start() {
    Dog d = new Dog();
    Integer c = d.color;
}
)",
                                sm);
    // FIXED GAP-6: Missing field on entity is now caught at sema level.
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasErrorContaining(result, "color") ||
                hasErrorContaining(result, "member"));
}

TEST(ZiaTypeSoundness, PrivateFieldAccess)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
entity Secret {
    Integer hidden;
    expose func getHidden() -> Integer { return hidden; }
}
func start() {
    Secret s = new Secret();
    Integer x = s.hidden;
}
)",
                                sm);
    // Private (unexposed) field should be rejected
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, FieldOnPrimitive)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    Integer x = 42;
    Integer y = x.value;
}
)",
                                sm);
    // FIXED GAP-6: Primitives have no fields — now rejected at sema level.
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasErrorContaining(result, "member") ||
                hasErrorContaining(result, "value"));
}

TEST(ZiaTypeSoundness, NonExistentFieldOnValue)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
value Point { Integer x; Integer y; }
func start() {
    Point p = Point(1, 2);
    Integer z = p.z;
}
)",
                                sm);
    // FIXED GAP-6: Missing field on value type is now caught at sema level.
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasErrorContaining(result, "z") ||
                hasErrorContaining(result, "member"));
}

//=============================================================================
// Category 4: Uninitialized Variables
//=============================================================================

TEST(ZiaTypeSoundness, GAP_UninitializedVariable)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    Integer x;
    Integer y = x + 1;
    Viper.Terminal.SayInt(y);
}
)",
                                sm);
    // FIXED GAP-1: Definite-assignment analysis now warns on uninitialized use.
    // Code still compiles (warning, not error) but the warning is emitted.
    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasWarningContaining(result, "initialization"));
}

TEST(ZiaTypeSoundness, GAP_ConditionalInit)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    Integer x;
    Boolean cond = true;
    if cond {
        x = 10;
    }
    Integer y = x + 1;
    Viper.Terminal.SayInt(y);
}
)",
                                sm);
    // FIXED GAP-2: Flow-sensitive initialization analysis warns when variable
    // is only initialized in one branch of an if-statement.
    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasWarningContaining(result, "initialization"));
}

TEST(ZiaTypeSoundness, UndefinedVariable)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    Integer y = x + 1;
}
)",
                                sm);
    // Completely undeclared variable — must be rejected
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasErrorContaining(result, "undefined") || hasErrorContaining(result, "Undefined"));
}

TEST(ZiaTypeSoundness, UseVariableWithoutTypeOrInit)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    var x;
}
)",
                                sm);
    // var without type annotation or initializer — cannot infer type
    EXPECT_FALSE(result.succeeded());
}

//=============================================================================
// Category 5: Integer-to-Pointer Coercions
//=============================================================================

TEST(ZiaTypeSoundness, AssignIntegerToEntity)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
entity Foo { expose Integer val; }
func start() {
    Foo f = 42;
}
)",
                                sm);
    // Direct integer-to-entity assignment — must be rejected
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, GAP_UncheckedAsCastIntToEntity)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
entity Foo { expose Integer val; }
func start() {
    Integer n = 12345;
    Foo f = n as Foo;
}
)",
                                sm);
    // FIXED GAP-3: 'as' cast now validates type compatibility.
    // Integer-to-Entity cast is rejected.
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, GAP_AsCastEntityToInteger)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
entity Foo { expose Integer val; }
func start() {
    Foo f = new Foo();
    Integer n = f as Integer;
}
)",
                                sm);
    // FIXED GAP-3: Entity-to-Integer cast is now rejected.
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, AssignNullToNonOptional)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    Integer x = null;
}
)",
                                sm);
    // null is only assignable to Optional types — must be rejected
    EXPECT_FALSE(result.succeeded());
}

//=============================================================================
// Category 6: Implicit Narrowing Conversions
//=============================================================================

TEST(ZiaTypeSoundness, NumberVariableToByte)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    Number n = 300.5;
    Byte b = n;
}
)",
                                sm);
    // Number -> Byte is narrowing — must be rejected
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, LargeIntLiteralToByte)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    Byte b = 300;
}
)",
                                sm);
    // 300 > 255 — outside Byte literal range, must be rejected
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, IntegerVariableToByte)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    Integer big = 1000;
    Byte b = big;
}
)",
                                sm);
    // Integer variable -> Byte is narrowing (not a literal, no range check)
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, IntegerAsCondition)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    Integer x = 42;
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

TEST(ZiaTypeSoundness, NumberReturnFromIntegerFunc)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func half(Integer n) -> Integer {
    Number result = n / 2.0;
    return result;
}
func start() {
    Integer x = half(10);
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

TEST(ZiaTypeSoundness, GAP_NullOptionalFieldAccess)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
entity Dog {
    expose Integer age;
}
func start() {
    Dog? d = null;
    Integer a = d.age;
}
)",
                                sm);
    // FIXED GAP-4: Optional auto-unwrap now emits a null-safety warning.
    // Code still compiles (no flow-sensitive analysis yet) but warns.
    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasWarningContaining(result, "Optional"));
}

TEST(ZiaTypeSoundness, GAP_NullOptionalMethodCall)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
entity Dog {
    expose Integer age;
    expose func bark() -> String { return "Woof"; }
}
func start() {
    Dog? d = null;
    String s = d.bark();
}
)",
                                sm);
    // FIXED GAP-5: Optional auto-unwrap now warns on method calls too.
    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasWarningContaining(result, "Optional"));
}

TEST(ZiaTypeSoundness, ReturnNullFromNonOptionalFunc)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func getValue() -> Integer {
    return null;
}
func start() {
    Integer x = getValue();
}
)",
                                sm);
    // Returning null from a non-Optional return type — must be rejected
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, GAP_PassOptionalToNonOptionalParam)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func double(Integer n) -> Integer {
    return n + n;
}
func start() {
    Integer? x = null;
    Integer y = double(x);
}
)",
                                sm);
    // FIXED GAP-8: Optional[Integer] where Integer is expected is now rejected
    // by the argument type validation added in the GAP-7 fix.
    EXPECT_FALSE(result.succeeded());
}

TEST(ZiaTypeSoundness, OptionalAcceptsInnerType)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    Integer? x = 42;
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

TEST(ZiaTypeSoundness, LogicalAndWithIntegers)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    Integer a = 1;
    Integer b = 2;
    Boolean c = a && b;
}
)",
                                sm);
    // Logical AND requires Boolean operands
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasErrorContaining(result, "Boolean") || hasErrorContaining(result, "Logical"));
}

TEST(ZiaTypeSoundness, BitwiseOrWithFloats)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    Number a = 1.5;
    Number b = 2.5;
    Integer c = a | b;
}
)",
                                sm);
    // Bitwise OR requires integral operands
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(hasErrorContaining(result, "integral") || hasErrorContaining(result, "Bitwise"));
}

TEST(ZiaTypeSoundness, NegateString)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    String s = "hello";
    Integer n = -s;
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

TEST(ZiaTypeSoundness, IntegerToNumberWidening)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    Number x = 42;
}
)",
                                sm);
    // Integer -> Number is a valid widening conversion
    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaTypeSoundness, ByteLiteralInRange)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    Byte b = 200;
}
)",
                                sm);
    // 200 is in [0,255] — valid Byte literal
    EXPECT_TRUE(result.succeeded());
}

TEST(ZiaTypeSoundness, EmptyListInference)
{
    SourceManager sm;
    auto result = compileSource(R"(
module Test;
func start() {
    List[Integer] x = [];
}
)",
                                sm);
    // Empty list with declared element type — valid
    EXPECT_TRUE(result.succeeded());
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
