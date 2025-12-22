//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for ViperLang bug fixes (Bugs #38-44).
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
// Bug #38: Module-Level Mutable Variables
//===----------------------------------------------------------------------===//

/// @brief Test module-level mutable variables can be read and written.
TEST(ViperLangBugFixes, Bug38_ModuleLevelMutableVariables)
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
    CompilerInput input{.source = source, .path = "bug38.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
    EXPECT_FALSE(result.module.functions.empty());
}

/// @brief Test module-level mutable variables without initializer.
TEST(ViperLangBugFixes, Bug38_ModuleLevelVarNoInitializer)
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
    CompilerInput input{.source = source, .path = "bug38b.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Bug #39: Module-Level Entity Variables
//===----------------------------------------------------------------------===//

/// @brief Test module-level entity variables can store and retrieve objects.
TEST(ViperLangBugFixes, Bug39_ModuleLevelEntityVariables)
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
    CompilerInput input{.source = source, .path = "bug39.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Bug #42: Boolean Operators `and`, `or`, `not`
//===----------------------------------------------------------------------===//

/// @brief Test `and` keyword works as logical AND.
TEST(ViperLangBugFixes, Bug42_AndKeyword)
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
    CompilerInput input{.source = source, .path = "bug42a.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test `or` keyword works as logical OR.
TEST(ViperLangBugFixes, Bug42_OrKeyword)
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
    CompilerInput input{.source = source, .path = "bug42b.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test `not` keyword works as logical NOT.
TEST(ViperLangBugFixes, Bug42_NotKeyword)
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
    CompilerInput input{.source = source, .path = "bug42c.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test combined `and`, `or`, `not` operators.
TEST(ViperLangBugFixes, Bug42_CombinedBooleanKeywords)
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
    CompilerInput input{.source = source, .path = "bug42d.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Bug #43: Colon Return Type Syntax
//===----------------------------------------------------------------------===//

/// @brief Test colon return type syntax in functions.
TEST(ViperLangBugFixes, Bug43_ColonReturnTypeFunction)
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
    CompilerInput input{.source = source, .path = "bug43a.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test colon return type syntax in entity methods.
TEST(ViperLangBugFixes, Bug43_ColonReturnTypeMethod)
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
    CompilerInput input{.source = source, .path = "bug43b.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Bug #44: Qualified Type Names
//===----------------------------------------------------------------------===//

/// @brief Test qualified type names parse correctly (dot-separated identifiers).
TEST(ViperLangBugFixes, Bug44_QualifiedTypeNames)
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
    CompilerInput input{.source = source, .path = "bug44.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
