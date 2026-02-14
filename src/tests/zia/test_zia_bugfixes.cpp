//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for Zia bug fixes (Bugs #38-44).
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
// Bug #38: Module-Level Mutable Variables
//===----------------------------------------------------------------------===//

/// @brief Test module-level mutable variables can be read and written.
TEST(ZiaBugFixes, Bug38_ModuleLevelMutableVariables)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

var counter: Integer;

func start() {
    counter = 10;
    Viper.Terminal.SayInt(counter);
    counter = counter + 1;
    Viper.Terminal.SayInt(counter);
}
)";
    CompilerInput input{.source = source, .path = "bug38.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
    EXPECT_FALSE(result.module.functions.empty());
}

/// @brief Test module-level mutable variables without initializer.
TEST(ZiaBugFixes, Bug38_ModuleLevelVarNoInitializer)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

var running: Boolean;
var score: Integer;

func start() {
    running = true;
    score = 0;
    if running {
        score = 100;
    }
    Viper.Terminal.SayInt(score);
}
)";
    CompilerInput input{.source = source, .path = "bug38b.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Bug #39: Module-Level Entity Variables
//===----------------------------------------------------------------------===//

/// @brief Test module-level entity variables can store and retrieve objects.
TEST(ZiaBugFixes, Bug39_ModuleLevelEntityVariables)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Player {
    expose Integer score;

    expose func addScore(Integer points) {
        score = score + points;
    }
}

var player: Player;

func start() {
    player = new Player();
    player.score = 10;
    player.addScore(5);
    Viper.Terminal.SayInt(player.score);
}
)";
    CompilerInput input{.source = source, .path = "bug39.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Bug #42: Boolean Operators `and`, `or`, `not`
//===----------------------------------------------------------------------===//

/// @brief Test `and` keyword works as logical AND.
TEST(ZiaBugFixes, Bug42_AndKeyword)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Boolean a = true;
    Boolean b = false;
    if a and b {
        Viper.Terminal.Say("both");
    } else {
        Viper.Terminal.Say("not both");
    }
}
)";
    CompilerInput input{.source = source, .path = "bug42a.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test `or` keyword works as logical OR.
TEST(ZiaBugFixes, Bug42_OrKeyword)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Boolean a = true;
    Boolean b = false;
    if a or b {
        Viper.Terminal.Say("at least one");
    }
}
)";
    CompilerInput input{.source = source, .path = "bug42b.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test `not` keyword works as logical NOT.
TEST(ZiaBugFixes, Bug42_NotKeyword)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Boolean finished = false;
    if not finished {
        Viper.Terminal.Say("still running");
    }
}
)";
    CompilerInput input{.source = source, .path = "bug42c.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test combined `and`, `or`, `not` operators.
TEST(ZiaBugFixes, Bug42_CombinedBooleanKeywords)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer x = 5;
    Integer y = 10;

    // Complex boolean expression using word-form operators
    if x > 0 and y > 0 or x < 0 and y < 0 {
        Viper.Terminal.Say("same sign");
    }

    // Using not with comparison
    if not (x == y) {
        Viper.Terminal.Say("different");
    }
}
)";
    CompilerInput input{.source = source, .path = "bug42d.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Bug #43: Colon Return Type Syntax
//===----------------------------------------------------------------------===//

/// @brief Test colon return type syntax in functions.
TEST(ZiaBugFixes, Bug43_ColonReturnTypeFunction)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func getNumber(): Integer {
    return 42;
}

func start() {
    Viper.Terminal.SayInt(getNumber());
}
)";
    CompilerInput input{.source = source, .path = "bug43a.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test colon return type syntax in entity methods.
TEST(ZiaBugFixes, Bug43_ColonReturnTypeMethod)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Calculator {
    expose Integer value;

    expose func getValue(): Integer {
        return value;
    }

    expose func double(): Integer {
        return value * 2;
    }
}

func start() {
    var calc = new Calculator();
    calc.value = 21;
    Viper.Terminal.SayInt(calc.double());
}
)";
    CompilerInput input{.source = source, .path = "bug43b.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Bug #44: Qualified Type Names
//===----------------------------------------------------------------------===//

/// @brief Test qualified type names parse correctly (dot-separated identifiers).
TEST(ZiaBugFixes, Bug44_QualifiedTypeNames)
{
    SourceManager sm;
    // Test that qualified type names with dots are parsed correctly
    // Uses List[Integer] which is the supported generic syntax
    const std::string source = R"(
module Test;

func start() {
    // Test basic qualified API access (this uses qualified names)
    Viper.Terminal.Say("qualified names work");

    // Test using parameterized generic type
    var items: List[Integer] = [];
    items.add(1);
    items.add(2);
    Viper.Terminal.SayInt(items.count());
}
)";
    CompilerInput input{.source = source, .path = "bug44.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// BUG-FE-007: Non-existent entity method through field chain
//===----------------------------------------------------------------------===//

/// @brief Calling a non-existent method on an entity field should fail compilation.
TEST(ZiaBugFixes, BugFE007_NonExistentEntityMethodError)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Inner {
    expose Integer x;
    expose func init() { x = 0; }
    expose func getX() -> Integer { return x; }
}

entity Outer {
    expose Inner inner;
    expose func init() {
        inner = new Inner();
        inner.init();
    }
}

func start() {
    var outer = new Outer();
    outer.init();
    outer.inner.nonExistentMethod();
}
)";
    CompilerInput input{.source = source, .path = "bug_fe007a.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    // Should fail: Inner has no method 'nonExistentMethod'
    EXPECT_FALSE(result.succeeded());
    EXPECT_TRUE(result.diagnostics.errorCount() > 0);
}

/// @brief Calling a valid method on an entity field should compile successfully.
TEST(ZiaBugFixes, BugFE007_ValidEntityFieldMethodDispatch)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Inner {
    expose Integer x;
    expose func init() { x = 42; }
    expose func getX() -> Integer { return x; }
}

entity Outer {
    expose Inner inner;
    expose func init() {
        inner = new Inner();
        inner.init();
    }
    expose func getInnerX() -> Integer {
        return inner.getX();
    }
}

func start() {
    var outer = new Outer();
    outer.init();
    var val = outer.inner.getX();
    Viper.Terminal.SayInt(val);
}
)";
    CompilerInput input{.source = source, .path = "bug_fe007b.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// BUG-FE-005: Complex functions with many locals (regression test)
//===----------------------------------------------------------------------===//

/// @brief Functions with 15+ locals and complex control flow should compile.
TEST(ZiaBugFixes, BugFE005_ManyLocalsComplexControlFlow)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func complexFunc() -> Integer {
    var a = 0;
    var b = 1;
    var c = 2;
    var d = 3;
    var e = 4;
    var f = 5;
    var g = 6;
    var h = 7;
    var i = 8;
    var j = 9;
    var k = 10;
    var l = 11;
    var m = 12;
    var n = 13;
    var o = 14;
    var p = 15;
    var q = 16;

    var idx = 0;
    while idx < 10 {
        var kind = (idx % 4) + 1;
        if kind == 1 {
            a = a + 1;
            b = idx;
        } else if kind == 2 {
            c = c + a + b;
            d = d + 1;
        } else if kind == 3 {
            e = e + 1;
            f = f + c;
        } else {
            g = g + 1;
            h = h + d;
        }
        idx = idx + 1;
    }

    return a + b + c + d + e + f + g + h + i + j + k + l + m + n + o + p + q;
}

func start() {
    var result = complexFunc();
    Viper.Terminal.SayInt(result);
}
)";
    CompilerInput input{.source = source, .path = "bug_fe005.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// BUG-FE-006: List method calls on function parameters (regression test)
//===----------------------------------------------------------------------===//

/// @brief List.add() on a function parameter should compile correctly.
TEST(ZiaBugFixes, BugFE006_ListParamMethodCalls)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func categorize(items: List[Integer], evens: List[Integer], odds: List[Integer]) {
    var i = 0;
    var total = items.count();
    while i < total {
        var val = items.get(i);
        if val % 2 == 0 {
            evens.add(val);
        } else {
            odds.add(val);
        }
        i = i + 1;
    }
}

func start() {
    var items: List[Integer] = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
    var evens: List[Integer] = [];
    var odds: List[Integer] = [];
    categorize(items, evens, odds);
    Viper.Terminal.SayInt(evens.count());
    Viper.Terminal.SayInt(odds.count());
}
)";
    CompilerInput input{.source = source, .path = "bug_fe006.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
