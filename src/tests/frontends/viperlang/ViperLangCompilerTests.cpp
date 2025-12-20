//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for the ViperLang frontend.
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

/// @brief Test that an empty start function compiles.
TEST(ViperLangCompilerTest, EmptyStartFunction)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
}
)";
    CompilerInput input{.source = source, .path = "test.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    // Print diagnostics for debugging
    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for EmptyStartFunction:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    // Should succeed (no errors)
    EXPECT_TRUE(result.succeeded());

    // Module should have @main function
    bool hasMain = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            hasMain = true;
            break;
        }
    }
    EXPECT_TRUE(hasMain);
}

/// @brief Test that the compiler produces an entry block.
TEST(ViperLangCompilerTest, ProducesEntryBlock)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
}
)";
    CompilerInput input{.source = source, .path = "test.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    // Check that the main function has at least one basic block
    bool foundMainWithBlocks = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main" && !fn.blocks.empty())
        {
            foundMainWithBlocks = true;
            break;
        }
    }
    EXPECT_TRUE(foundMainWithBlocks);
}

/// @brief Test that Hello World compiles and calls Viper.Terminal.Say.
TEST(ViperLangCompilerTest, HelloWorld)
{
    SourceManager sm;
    const std::string source = R"(
module Hello;

func start() {
    Viper.Terminal.Say("Hello, World!");
}
)";
    CompilerInput input{.source = source, .path = "hello.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    // Should have main function with Viper.Terminal.Say call
    bool hasMain = false;
    bool foundCall = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            hasMain = true;
            for (const auto &block : fn.blocks)
            {
                for (const auto &instr : block.instructions)
                {
                    if (instr.op == il::core::Opcode::Call && instr.callee == "Viper.Terminal.Say")
                    {
                        foundCall = true;
                    }
                }
            }
        }
    }
    EXPECT_TRUE(hasMain);
    EXPECT_TRUE(foundCall);
}

/// @brief Test that variables are handled correctly.
TEST(ViperLangCompilerTest, VariableDeclaration)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer x = 42;
    Viper.Terminal.SayInt(x);
}
)";
    CompilerInput input{.source = source, .path = "var.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    // Print diagnostics for debugging
    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for VariableDeclaration:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    // Should have main function
    bool hasMain = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            hasMain = true;
            break;
        }
    }
    EXPECT_TRUE(hasMain);
}

/// @brief Test that if statements compile correctly.
TEST(ViperLangCompilerTest, IfStatement)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    if (true) {
        Viper.Terminal.Say("yes");
    } else {
        Viper.Terminal.Say("no");
    }
}
)";
    CompilerInput input{.source = source, .path = "if.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    // Check for conditional branch
    bool foundCBr = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &block : fn.blocks)
            {
                for (const auto &instr : block.instructions)
                {
                    if (instr.op == il::core::Opcode::CBr)
                    {
                        foundCBr = true;
                        break;
                    }
                }
            }
        }
    }
    EXPECT_TRUE(foundCBr);
}

/// @brief Test that while loops compile correctly.
TEST(ViperLangCompilerTest, WhileLoop)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer i = 0;
    while (i < 10) {
        i = i + 1;
    }
}
)";
    CompilerInput input{.source = source, .path = "while.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    // Check for comparison instruction (SCmpLT)
    bool foundCmp = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &block : fn.blocks)
            {
                for (const auto &instr : block.instructions)
                {
                    if (instr.op == il::core::Opcode::SCmpLT)
                    {
                        foundCmp = true;
                        break;
                    }
                }
            }
        }
    }
    EXPECT_TRUE(foundCmp);
}

/// @brief Test that function calls work.
TEST(ViperLangCompilerTest, FunctionCall)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func greet() {
    Viper.Terminal.Say("Hello");
}

func start() {
    greet();
}
)";
    CompilerInput input{.source = source, .path = "call.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    // Check for both main and greet functions
    bool hasMain = false;
    bool hasGreet = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
            hasMain = true;
        if (fn.name == "greet")
            hasGreet = true;
    }
    EXPECT_TRUE(hasMain);
    EXPECT_TRUE(hasGreet);
}

/// @brief Test arithmetic expressions.
TEST(ViperLangCompilerTest, Arithmetic)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer x = 1 + 2 * 3;
    Viper.Terminal.SayInt(x);
}
)";
    CompilerInput input{.source = source, .path = "arith.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    // Check for Mul (or IMulOvf for signed) and Add instructions
    bool foundMul = false;
    bool foundAdd = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &block : fn.blocks)
            {
                for (const auto &instr : block.instructions)
                {
                    if (instr.op == il::core::Opcode::Mul || instr.op == il::core::Opcode::IMulOvf)
                        foundMul = true;
                    if (instr.op == il::core::Opcode::Add || instr.op == il::core::Opcode::IAddOvf)
                        foundAdd = true;
                }
            }
        }
    }
    EXPECT_TRUE(foundMul);
    EXPECT_TRUE(foundAdd);
}

/// @brief Test that value types parse correctly.
TEST(ViperLangCompilerTest, ValueTypeDeclaration)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

value Point {
    Integer x;
    Integer y;
}

func start() {
}
)";
    CompilerInput input{.source = source, .path = "value.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    // Print diagnostics for debugging
    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for ValueTypeDeclaration:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test that entity types with new keyword work correctly.
TEST(ViperLangCompilerTest, EntityTypeWithNew)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Person {
    expose Integer age;
    expose Integer score;

    expose func getAge() -> Integer {
        return age;
    }
}

func start() {
    Person p = new Person(30, 100);
    Integer age = p.age;
    Integer method_age = p.getAge();
    Viper.Terminal.SayInt(age);
    Viper.Terminal.SayInt(method_age);
}
)";
    CompilerInput input{.source = source, .path = "entity.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    // Print diagnostics for debugging
    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for EntityTypeWithNew:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    // Check that rt_alloc is used for entity allocation
    bool foundRtAlloc = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &block : fn.blocks)
            {
                for (const auto &instr : block.instructions)
                {
                    if (instr.op == il::core::Opcode::Call && instr.callee == "rt_alloc")
                        foundRtAlloc = true;
                }
            }
        }
    }
    EXPECT_TRUE(foundRtAlloc);
}

/// @brief Test that optional types and coalesce operator work correctly.
TEST(ViperLangCompilerTest, OptionalAndCoalesce)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Person {
    expose Integer age;
}

func start() {
    Person? p1 = new Person(30);
    Person? p2 = null;

    Person result1 = p1 ?? new Person(99);
    Person result2 = p2 ?? new Person(88);

    Integer age1 = result1.age;
    Integer age2 = result2.age;

    Viper.Terminal.SayInt(age1);
    Viper.Terminal.SayInt(age2);
}
)";
    CompilerInput input{.source = source, .path = "optional.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    // Print diagnostics for debugging
    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for OptionalAndCoalesce:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    // Check for coalesce-related blocks (should have coalesce_has, coalesce_null, coalesce_merge)
    bool foundCoalesceBlock = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &block : fn.blocks)
            {
                if (block.label.find("coalesce") != std::string::npos)
                {
                    foundCoalesceBlock = true;
                    break;
                }
            }
        }
    }
    EXPECT_TRUE(foundCoalesceBlock);
}

/// @brief Test that for-in loops with ranges work correctly.
TEST(ViperLangCompilerTest, ForInLoop)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer sum = 0;
    for (i in 0..5) {
        sum = sum + i;
    }
    Viper.Terminal.SayInt(sum);
}
)";
    CompilerInput input{.source = source, .path = "forin.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    // Print diagnostics for debugging
    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for ForInLoop:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    // Check for forin blocks and alloca/store/load pattern
    bool foundForInCond = false;
    bool foundAlloca = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &block : fn.blocks)
            {
                if (block.label.find("forin_cond") != std::string::npos)
                    foundForInCond = true;
                for (const auto &instr : block.instructions)
                {
                    if (instr.op == il::core::Opcode::Alloca)
                        foundAlloca = true;
                }
            }
        }
    }
    EXPECT_TRUE(foundForInCond);
    EXPECT_TRUE(foundAlloca);
}

/// @brief Test that Map collections compile correctly.
TEST(ViperLangCompilerTest, MapCollection)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Map[String, Integer] ages = new Map[String, Integer]();
    ages.set("Alice", 30);
    ages.set("Bob", 25);
    Integer aliceAge = ages.get("Alice");
    Integer count = ages.count();
    Viper.Terminal.SayInt(aliceAge);
    Viper.Terminal.SayInt(count);
}
)";
    CompilerInput input{.source = source, .path = "map.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    // Print diagnostics for debugging
    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for MapCollection:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    // Check for Map.New and Map.set_Item calls
    bool foundMapNew = false;
    bool foundMapSet = false;
    bool foundMapGet = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &block : fn.blocks)
            {
                for (const auto &instr : block.instructions)
                {
                    if (instr.op == il::core::Opcode::Call)
                    {
                        if (instr.callee == "Viper.Collections.Map.New")
                            foundMapNew = true;
                        if (instr.callee == "Viper.Collections.Map.set_Item")
                            foundMapSet = true;
                        if (instr.callee == "Viper.Collections.Map.get_Item")
                            foundMapGet = true;
                    }
                }
            }
        }
    }
    EXPECT_TRUE(foundMapNew);
    EXPECT_TRUE(foundMapSet);
    EXPECT_TRUE(foundMapGet);
}

/// @brief Test that Map index access and assignment work correctly.
TEST(ViperLangCompilerTest, MapIndexAccess)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Map[Integer, String] names = new Map[Integer, String]();
    names[1] = "One";
    names[2] = "Two";
    String name = names[1];
    Viper.Terminal.Say(name);
}
)";
    CompilerInput input{.source = source, .path = "mapindex.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    // Print diagnostics for debugging
    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for MapIndexAccess:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    // Check for Map.set_Item and Map.get_Item calls
    bool foundMapSet = false;
    bool foundMapGet = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &block : fn.blocks)
            {
                for (const auto &instr : block.instructions)
                {
                    if (instr.op == il::core::Opcode::Call)
                    {
                        if (instr.callee == "Viper.Collections.Map.set_Item")
                            foundMapSet = true;
                        if (instr.callee == "Viper.Collections.Map.get_Item")
                            foundMapGet = true;
                    }
                }
            }
        }
    }
    EXPECT_TRUE(foundMapSet);
    EXPECT_TRUE(foundMapGet);
}

// NOTE: Closure capture test disabled due to lambda lowering issue that needs debugging.
// The lambda lowering has a bug causing an infinite loop.
// TODO: Fix lambda lowering and re-enable this test.

/// @brief Test that visibility enforcement works (private members are rejected).
TEST(ViperLangCompilerTest, VisibilityEnforcement)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Person {
    Integer secretAge;
    expose Integer publicAge;
}

func start() {
    Person p = new Person(30, 25);
    Integer age = p.secretAge;
}
)";
    CompilerInput input{.source = source, .path = "visibility.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    // This should FAIL because secretAge is private
    EXPECT_FALSE(result.succeeded());

    // Check for visibility error
    bool foundVisibilityError = false;
    for (const auto &d : result.diagnostics.diagnostics())
    {
        if (d.message.find("private") != std::string::npos)
            foundVisibilityError = true;
    }
    EXPECT_TRUE(foundVisibilityError);
}

/// @brief Test that visibility works correctly with exposed members.
TEST(ViperLangCompilerTest, VisibilityExposed)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Person {
    expose Integer age;
}

func start() {
    Person p = new Person(30);
    Integer age = p.age;
    Viper.Terminal.SayInt(age);
}
)";
    CompilerInput input{.source = source, .path = "visibility_exposed.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    // Print diagnostics for debugging
    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for VisibilityExposed:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

// NOTE: Match expression tests disabled due to lowering issue that needs debugging.
// Match expressions (as opposed to match statements) hang during compilation.
// Match STATEMENTS work fine - see tests below.
// TODO: Add lowerMatchExpr to handle match expressions.

/// @brief Test that match statement works correctly.
TEST(ViperLangCompilerTest, MatchStatement)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer x = 5;
    match (x) {
        1 => { Viper.Terminal.Say("one"); }
        _ => { Viper.Terminal.Say("other"); }
    }
}
)";
    CompilerInput input{.source = source, .path = "match_stmt.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    // Print diagnostics for debugging
    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for MatchStatement:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    // Check for match-related blocks
    bool foundMatchBlock = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &block : fn.blocks)
            {
                if (block.label.find("match_arm") != std::string::npos)
                    foundMatchBlock = true;
            }
        }
    }
    EXPECT_TRUE(foundMatchBlock);
}

/// @brief Test that empty list type inference works.
TEST(ViperLangCompilerTest, EmptyListTypeInference)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    List[Integer] numbers = [];
    numbers.add(42);
    Integer first = numbers.get(0);
    Viper.Terminal.SayInt(first);
}
)";
    CompilerInput input{.source = source, .path = "emptylist.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    // Print diagnostics for debugging
    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for EmptyListTypeInference:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test that lambda with block body compiles.
TEST(ViperLangCompilerTest, LambdaWithBlockBody)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var greet = () => {
        Viper.Terminal.Say("Hello");
    };
}
)";
    CompilerInput input{.source = source, .path = "lambda_block.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    // Print diagnostics for debugging
    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for LambdaWithBlockBody:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    // Check that a lambda function was generated
    bool foundLambdaFunc = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name.find("lambda") != std::string::npos)
        {
            foundLambdaFunc = true;
            break;
        }
    }
    EXPECT_TRUE(foundLambdaFunc);
}

/// @brief Test that match expression (used as value) compiles.
TEST(ViperLangCompilerTest, MatchExpression)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer x = 2;
    Integer result = match (x) {
        1 => 10,
        2 => 20,
        _ => 0
    };
    Viper.Terminal.SayInt(result);
}
)";
    CompilerInput input{.source = source, .path = "match_expr.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    // Print diagnostics for debugging
    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for MatchExpression:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    // Check for match-related blocks
    bool foundMatchBlock = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &block : fn.blocks)
            {
                if (block.label.find("match_arm") != std::string::npos)
                    foundMatchBlock = true;
            }
        }
    }
    EXPECT_TRUE(foundMatchBlock);
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
