//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for ViperLang statement types and control flow.
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
// Variable Declarations
//===----------------------------------------------------------------------===//

/// @brief Test var with type inference.
TEST(ViperLangStatements, VarInference)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var x = 42;
    var y = 3;
    var z = "hello";
    var b = true;

    Viper.Terminal.SayInt(x);
    Viper.Terminal.SayInt(y);
    Viper.Terminal.Say(z);
    Viper.Terminal.SayBool(b);
}
)";
    CompilerInput input{.source = source, .path = "varinfer.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test var with explicit type.
TEST(ViperLangStatements, VarExplicitType)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var x: Integer = 42;
    var y: Integer = 3;
    var z: String = "hello";
    var b: Boolean = true;

    Viper.Terminal.SayInt(x);
    Viper.Terminal.SayInt(y);
    Viper.Terminal.Say(z);
    Viper.Terminal.SayBool(b);
}
)";
    CompilerInput input{.source = source, .path = "varexplicit.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test final (immutable) variable.
TEST(ViperLangStatements, FinalVariable)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    final PI = 314159;
    final NAME = "Viper";
    final COUNT = 100;

    Viper.Terminal.SayInt(PI);
    Viper.Terminal.Say(NAME);
    Viper.Terminal.SayInt(COUNT);
}
)";
    CompilerInput input{.source = source, .path = "final.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// If Statements
//===----------------------------------------------------------------------===//

/// @brief Test basic if statement.
TEST(ViperLangStatements, IfBasic)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer x = 5;

    if x > 0 {
        Viper.Terminal.Say("positive");
    }
}
)";
    CompilerInput input{.source = source, .path = "ifbasic.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test if-else statement.
TEST(ViperLangStatements, IfElse)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer x = -3;

    if x >= 0 {
        Viper.Terminal.Say("non-negative");
    } else {
        Viper.Terminal.Say("negative");
    }
}
)";
    CompilerInput input{.source = source, .path = "ifelse.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test nested if statements.
TEST(ViperLangStatements, IfNested)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer x = 15;

    if x > 0 {
        if x < 10 {
            Viper.Terminal.Say("small positive");
        } else {
            if x < 100 {
                Viper.Terminal.Say("medium positive");
            } else {
                Viper.Terminal.Say("large positive");
            }
        }
    } else {
        Viper.Terminal.Say("non-positive");
    }
}
)";
    CompilerInput input{.source = source, .path = "ifnested.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// While Loops
//===----------------------------------------------------------------------===//

/// @brief Test basic while loop.
TEST(ViperLangStatements, WhileBasic)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var i = 0;
    while i < 5 {
        Viper.Terminal.SayInt(i);
        i = i + 1;
    }
}
)";
    CompilerInput input{.source = source, .path = "whilebasic.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test while with break.
TEST(ViperLangStatements, WhileBreak)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var i = 0;
    while true {
        if i >= 5 {
            break;
        }
        Viper.Terminal.SayInt(i);
        i = i + 1;
    }
    Viper.Terminal.Say("done");
}
)";
    CompilerInput input{.source = source, .path = "whilebreak.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test while with continue.
TEST(ViperLangStatements, WhileContinue)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var i = 0;
    while i < 10 {
        i = i + 1;
        if i % 2 == 0 {
            continue;
        }
        Viper.Terminal.SayInt(i);
    }
}
)";
    CompilerInput input{.source = source, .path = "whilecont.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// For Loops
//===----------------------------------------------------------------------===//

/// @brief Test for-in loop with list.
TEST(ViperLangStatements, ForInList)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var numbers = [1, 2, 3, 4, 5];
    for n in numbers {
        Viper.Terminal.SayInt(n);
    }
}
)";
    CompilerInput input{.source = source, .path = "forinlist.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test for-in loop with range.
TEST(ViperLangStatements, ForInRange)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    for i in 0..5 {
        Viper.Terminal.SayInt(i);
    }
}
)";
    CompilerInput input{.source = source, .path = "forinrange.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test C-style for loop.
TEST(ViperLangStatements, ForCStyle)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    for (var i = 0; i < 5; i = i + 1) {
        Viper.Terminal.SayInt(i);
    }
}
)";
    CompilerInput input{.source = source, .path = "forcstyle.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test for with break.
TEST(ViperLangStatements, ForBreak)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var numbers = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
    for n in numbers {
        if n > 5 {
            break;
        }
        Viper.Terminal.SayInt(n);
    }
}
)";
    CompilerInput input{.source = source, .path = "forbreak.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Guard Statements
//===----------------------------------------------------------------------===//

/// @brief Test guard statement.
TEST(ViperLangStatements, Guard)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func process(Integer? value) {
    guard value != null else {
        Viper.Terminal.Say("value is null");
        return;
    }
    Viper.Terminal.SayInt(value ?? 0);
}

func start() {
    process(42);
    process(null);
}
)";
    CompilerInput input{.source = source, .path = "guard.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test guard in loop.
TEST(ViperLangStatements, GuardInLoop)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var values = [1, 2, 0, 4, 0, 6];
    for v in values {
        guard v != 0 else {
            continue;
        }
        Viper.Terminal.SayInt(100 / v);
    }
}
)";
    CompilerInput input{.source = source, .path = "guardloop.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Block Statements
//===----------------------------------------------------------------------===//

/// @brief Test block scoping.
TEST(ViperLangStatements, BlockScoping)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var x = 10;
    {
        var x = 20;  // Shadows outer x
        Viper.Terminal.SayInt(x);  // 20
    }
    Viper.Terminal.SayInt(x);  // 10
}
)";
    CompilerInput input{.source = source, .path = "blockscope.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Return Statements
//===----------------------------------------------------------------------===//

/// @brief Test return with value.
TEST(ViperLangStatements, ReturnValue)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func double(Integer x) -> Integer {
    return x * 2;
}

func start() {
    Viper.Terminal.SayInt(double(21));
}
)";
    CompilerInput input{.source = source, .path = "returnval.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test return without value (void).
TEST(ViperLangStatements, ReturnVoid)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func earlyExit(Integer x) {
    if x < 0 {
        Viper.Terminal.Say("negative");
        return;
    }
    Viper.Terminal.Say("non-negative");
}

func start() {
    earlyExit(-5);
    earlyExit(5);
}
)";
    CompilerInput input{.source = source, .path = "returnvoid.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

//===----------------------------------------------------------------------===//
// Complex Control Flow
//===----------------------------------------------------------------------===//

/// @brief Test nested loops with break/continue.
TEST(ViperLangStatements, NestedLoops)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    for i in 0..3 {
        for j in 0..3 {
            if i == j {
                continue;
            }
            Viper.Terminal.SayInt(i * 10 + j);
        }
    }
}
)";
    CompilerInput input{.source = source, .path = "nestedloops.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test complex condition chains.
TEST(ViperLangStatements, ComplexConditions)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func classify(Integer n) -> String {
    if n < 0 {
        return "negative";
    }
    if n == 0 {
        return "zero";
    }
    if n < 10 {
        return "single digit";
    }
    if n < 100 {
        return "double digit";
    }
    return "large";
}

func start() {
    Viper.Terminal.Say(classify(-5));
    Viper.Terminal.Say(classify(0));
    Viper.Terminal.Say(classify(7));
    Viper.Terminal.Say(classify(42));
    Viper.Terminal.Say(classify(1000));
}
)";
    CompilerInput input{.source = source, .path = "complexcond.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
