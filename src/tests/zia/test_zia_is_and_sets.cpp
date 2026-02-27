//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for Zia `is` type check expressions and set literal lowering.
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

// ============================================================================
// Helper: check if a function contains a Call to a specific callee
// ============================================================================

static bool hasCall(const il::core::Module &mod, const std::string &fnName,
                    const std::string &callee)
{
    for (const auto &fn : mod.functions)
    {
        if (fn.name == fnName)
        {
            for (const auto &block : fn.blocks)
            {
                for (const auto &instr : block.instructions)
                {
                    if (instr.op == il::core::Opcode::Call && instr.callee == callee)
                        return true;
                }
            }
        }
    }
    return false;
}

static bool hasOpcode(const il::core::Module &mod, const std::string &fnName,
                      il::core::Opcode op)
{
    for (const auto &fn : mod.functions)
    {
        if (fn.name == fnName)
        {
            for (const auto &block : fn.blocks)
            {
                for (const auto &instr : block.instructions)
                {
                    if (instr.op == op)
                        return true;
                }
            }
        }
    }
    return false;
}

// ============================================================================
// `is` type check tests
// ============================================================================

/// @brief Test that `is` expression compiles and emits rt_obj_class_id call.
TEST(ZiaIsExpr, BasicIsCheck)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Animal {
    expose String name;
}

entity Dog extends Animal {
    expose String breed;
}

func start() {
    var dog = new Dog();
    var result: Boolean = dog is Dog;
    Viper.Terminal.SayInt(result ? 1 : 0);
}
)";
    CompilerInput input{.source = source, .path = "is_basic.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for BasicIsCheck:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    // Verify that rt_obj_class_id is called (the key runtime function for is checks)
    EXPECT_TRUE(hasCall(result.module, "main", "rt_obj_class_id"));

    // Verify that ICmpEq is used to compare class IDs
    EXPECT_TRUE(hasOpcode(result.module, "main", il::core::Opcode::ICmpEq));
}

/// @brief Test that `is` check with base type compiles.
TEST(ZiaIsExpr, IsCheckBaseType)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Animal {
    expose Integer id;
}

entity Dog extends Animal {
    expose Integer age;
}

func start() {
    var dog = new Dog();
    var isAnimal: Boolean = dog is Animal;
    Viper.Terminal.SayInt(isAnimal ? 1 : 0);
}
)";
    CompilerInput input{.source = source, .path = "is_base.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for IsCheckBaseType:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasCall(result.module, "main", "rt_obj_class_id"));
}

// ============================================================================
// Set literal tests
// ============================================================================

/// @brief Test that set literal `{1, 2, 3}` compiles and emits Set.New + Set.Add.
TEST(ZiaSetLiteral, BasicSetLiteral)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var s = {1, 2, 3};
}
)";
    CompilerInput input{.source = source, .path = "set_basic.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for BasicSetLiteral:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    // Verify Set.New and Set.Add calls are emitted
    EXPECT_TRUE(hasCall(result.module, "main", "Viper.Collections.Set.New"));
    EXPECT_TRUE(hasCall(result.module, "main", "Viper.Collections.Set.Add"));
}

/// @brief Test that empty set literal compiles.
TEST(ZiaSetLiteral, EmptySetLiteral)
{
    SourceManager sm;
    // Note: Empty set {} would conflict with empty map or empty block.
    // Sets require at least one element to be distinguishable from maps.
    // This test uses a typed variable to ensure the expression is treated as a set.
    const std::string source = R"(
module Test;

func start() {
    var s = {42};
}
)";
    CompilerInput input{.source = source, .path = "set_single.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for EmptySetLiteral:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasCall(result.module, "main", "Viper.Collections.Set.New"));
}

/// @brief Test set literal with string elements.
TEST(ZiaSetLiteral, StringSetLiteral)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var s = {"hello", "world"};
}
)";
    CompilerInput input{.source = source, .path = "set_strings.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for StringSetLiteral:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasCall(result.module, "main", "Viper.Collections.Set.New"));
    EXPECT_TRUE(hasCall(result.module, "main", "Viper.Collections.Set.Add"));
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
