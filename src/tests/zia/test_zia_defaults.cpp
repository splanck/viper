//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for Zia default parameter values.
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
// Helper: check if a function calls a specific callee
// ============================================================================

static bool hasCallee(const il::core::Module &mod, const std::string &fnName,
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

/// @brief Count the number of operands in the first call to a specific callee.
static int countCallOperands(const il::core::Module &mod, const std::string &fnName,
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
                        return static_cast<int>(instr.operands.size());
                }
            }
        }
    }
    return -1;
}

// ============================================================================
// Default parameter tests
// ============================================================================

/// @brief Test calling a function with one default parameter omitted.
TEST(ZiaDefaults, SingleDefaultOmitted)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func greet(name: String, greeting: String = "Hello") -> String {
    return greeting;
}

func start() {
    var result = greet("World");
}
)";

    CompilerInput input{.source = source, .path = "test_defaults.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());

    // The call to greet should have 2 operands (explicit "World" + default "Hello")
    int operandCount = countCallOperands(result.module, "main", "greet");
    EXPECT_EQ(operandCount, 2);
}

/// @brief Test calling a function with all arguments provided (no defaults used).
TEST(ZiaDefaults, AllArgsProvided)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func greet(name: String, greeting: String = "Hello") -> String {
    return greeting;
}

func start() {
    var result = greet("World", "Hi");
}
)";

    CompilerInput input{.source = source, .path = "test_defaults_all.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());

    // The call to greet should have 2 operands (both explicit)
    int operandCount = countCallOperands(result.module, "main", "greet");
    EXPECT_EQ(operandCount, 2);
}

/// @brief Test calling a function with multiple default parameters, some omitted.
TEST(ZiaDefaults, MultipleDefaults)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func configure(name: String, width: Integer = 800, height: Integer = 600) -> Integer {
    return width;
}

func start() {
    var a = configure("window");
    var b = configure("window", 1024);
    var c = configure("window", 1024, 768);
}
)";

    CompilerInput input{.source = source, .path = "test_multi_defaults.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    EXPECT_TRUE(hasCallee(result.module, "main", "configure"));
}

/// @brief Test that too few arguments without defaults produces an error.
TEST(ZiaDefaults, TooFewWithoutDefault)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func add(a: Integer, b: Integer) -> Integer {
    return a;
}

func start() {
    var result = add(1);
}
)";

    CompilerInput input{.source = source, .path = "test_too_few.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    // This should produce a sema error (too few arguments)
    EXPECT_FALSE(result.succeeded());
}

/// @brief Test default parameter with integer literal.
TEST(ZiaDefaults, IntegerDefault)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func repeat(count: Integer = 3) -> Integer {
    return count;
}

func start() {
    var a = repeat();
    var b = repeat(5);
}
)";

    CompilerInput input{.source = source, .path = "test_int_default.zia"};
    CompilerOptions opts{};
    auto result = compile(input, opts, sm);

    ASSERT_TRUE(result.succeeded());
    EXPECT_TRUE(hasCallee(result.module, "main", "repeat"));
}

} // anonymous namespace

int main()
{
    return viper_test::run_all_tests();
}
