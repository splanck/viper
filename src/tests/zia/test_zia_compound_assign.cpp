//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for Zia compound assignment operators (+=, -=, *=, /=, %=).
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
// Helper: check if a function contains a specific opcode
// ============================================================================

static bool hasOpcode(const il::core::Module &mod, const std::string &fnName, il::core::Opcode op)
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
// Compound assignment tests
// ============================================================================

/// @brief Test that += desugars to add + assign.
TEST(ZiaCompoundAssign, PlusEqual)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var x: Integer = 10;
    x += 5;
    Viper.Terminal.SayInt(x);
}
)";
    CompilerInput input{.source = source, .path = "plus_eq.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for PlusEqual:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
    // The desugaring produces Add or IAddOvf (depending on overflow check setting)
    bool hasAdd = hasOpcode(result.module, "main", il::core::Opcode::Add) ||
                  hasOpcode(result.module, "main", il::core::Opcode::IAddOvf);
    EXPECT_TRUE(hasAdd);
}

/// @brief Test that -= desugars to sub + assign.
TEST(ZiaCompoundAssign, MinusEqual)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var x: Integer = 10;
    x -= 3;
    Viper.Terminal.SayInt(x);
}
)";
    CompilerInput input{.source = source, .path = "minus_eq.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for MinusEqual:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
    bool hasSub = hasOpcode(result.module, "main", il::core::Opcode::Sub) ||
                  hasOpcode(result.module, "main", il::core::Opcode::ISubOvf);
    EXPECT_TRUE(hasSub);
}

/// @brief Test that *= desugars to mul + assign.
TEST(ZiaCompoundAssign, StarEqual)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var x: Integer = 10;
    x *= 2;
    Viper.Terminal.SayInt(x);
}
)";
    CompilerInput input{.source = source, .path = "star_eq.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for StarEqual:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
    bool hasMul = hasOpcode(result.module, "main", il::core::Opcode::Mul) ||
                  hasOpcode(result.module, "main", il::core::Opcode::IMulOvf);
    EXPECT_TRUE(hasMul);
}

/// @brief Test that /= desugars to div + assign.
TEST(ZiaCompoundAssign, SlashEqual)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var x: Integer = 10;
    x /= 2;
    Viper.Terminal.SayInt(x);
}
)";
    CompilerInput input{.source = source, .path = "slash_eq.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for SlashEqual:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test that %= desugars to mod + assign.
TEST(ZiaCompoundAssign, PercentEqual)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var x: Integer = 10;
    x %= 3;
    Viper.Terminal.SayInt(x);
}
)";
    CompilerInput input{.source = source, .path = "percent_eq.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for PercentEqual:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test compound assignment on entity fields.
TEST(ZiaCompoundAssign, FieldCompoundAssign)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

entity Counter {
    expose Integer count;

    expose func increment() {
        self.count += 1;
    }
}

func start() {
    var c = new Counter();
    c.count = 0;
    c.increment();
    Viper.Terminal.SayInt(c.count);
}
)";
    CompilerInput input{.source = source, .path = "field_compound.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for FieldCompoundAssign:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test compound assignment with multiple operations chained.
TEST(ZiaCompoundAssign, MultipleCompoundOps)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var x: Integer = 10;
    x += 5;
    x -= 2;
    x *= 3;
    Viper.Terminal.SayInt(x);
}
)";
    CompilerInput input{.source = source, .path = "multi_compound.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for MultipleCompoundOps:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
}

/// @brief Test compound assignment with float operands.
TEST(ZiaCompoundAssign, FloatCompoundAssign)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    var x: Number = 1.5;
    x += 2.5;
    x *= 3.0;
    Viper.Terminal.SayNum(x);
}
)";
    CompilerInput input{.source = source, .path = "float_compound.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for FloatCompoundAssign:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());
    // Float addition uses FAdd
    EXPECT_TRUE(hasOpcode(result.module, "main", il::core::Opcode::FAdd));
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
