//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for ViperLang expressions (arithmetic, operators).
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

/// @brief Test arithmetic expressions.
TEST(ViperLangExpressions, Arithmetic)
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

/// @brief Test module-level constants are resolved correctly (Bug #23, #25).
TEST(ViperLangExpressions, ModuleLevelConstants)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

// Use 'final' for compile-time constants that should be inlined
final GAME_WIDTH = 70;
final PLAYER_START = 35;

func start() {
    // Constants should resolve to their actual values, not 0
    Viper.Terminal.SayInt(GAME_WIDTH);
    Viper.Terminal.SayInt(PLAYER_START);
}
)";
    CompilerInput input{.source = source, .path = "constants.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    // Verify that the constants 70 and 35 appear in the generated IL
    bool found70 = false;
    bool found35 = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &block : fn.blocks)
            {
                for (const auto &instr : block.instructions)
                {
                    for (const auto &op : instr.operands)
                    {
                        if (op.kind == il::core::Value::Kind::ConstInt)
                        {
                            if (op.i64 == 70)
                                found70 = true;
                            if (op.i64 == 35)
                                found35 = true;
                        }
                    }
                }
            }
        }
    }
    EXPECT_TRUE(found70);
    EXPECT_TRUE(found35);
}

/// @brief Test boolean AND/OR with comparison operands (Bug #24).
/// Boolean operators should zero-extend I1 to I64, perform op, truncate back.
TEST(ViperLangExpressions, BooleanAndOrWithComparisons)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Integer x = 5;
    // These expressions use AND/OR with comparison results (I1 type)
    Boolean a = x > 0 && x < 10;
    Boolean b = x < 0 || x > 3;
    if (a && b) {
        Viper.Terminal.SayInt(1);
    }
}
)";
    CompilerInput input{.source = source, .path = "boolops.viper"};
    // Use O0 to test IL generation without optimization (SCCP would constant-fold these)
    CompilerOptions opts{.optLevel = OptLevel::O0};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    // Verify that zext1 and trunc1 opcodes are generated for boolean ops
    bool foundZext1 = false;
    bool foundTrunc1 = false;
    bool foundAnd = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &block : fn.blocks)
            {
                for (const auto &instr : block.instructions)
                {
                    if (instr.op == il::core::Opcode::Zext1)
                        foundZext1 = true;
                    if (instr.op == il::core::Opcode::Trunc1)
                        foundTrunc1 = true;
                    if (instr.op == il::core::Opcode::And)
                        foundAnd = true;
                }
            }
        }
    }
    EXPECT_TRUE(foundZext1);
    EXPECT_TRUE(foundTrunc1);
    EXPECT_TRUE(foundAnd);
}

/// @brief Test ternary conditional expressions lower into branch blocks.
TEST(ViperLangExpressions, TernaryExpression)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func start() {
    Boolean flag = true;
    Integer value = flag ? 10 : 20;
    Viper.Terminal.SayInt(value);
}
)";
    CompilerInput input{.source = source, .path = "ternary.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    bool foundThen = false;
    bool foundElse = false;
    bool foundMerge = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &block : fn.blocks)
            {
                if (block.label.find("ternary_then") != std::string::npos)
                    foundThen = true;
                if (block.label.find("ternary_else") != std::string::npos)
                    foundElse = true;
                if (block.label.find("ternary_merge") != std::string::npos)
                    foundMerge = true;
            }
        }
    }
    EXPECT_TRUE(foundThen);
    EXPECT_TRUE(foundElse);
    EXPECT_TRUE(foundMerge);
}

/// @brief Bug #29: String comparison with empty string.
/// Empty string literals should be compared using Viper.Strings.Equals.
TEST(ViperLangExpressions, StringComparisonWithEmptyString)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

func checkEmpty(String s) -> Boolean {
    return s == "";
}

func checkNotEmpty(String s) -> Boolean {
    return s != "";
}

func start() {
    Boolean empty = checkEmpty("");
    Boolean notEmpty = checkNotEmpty("hello");
}
)";
    CompilerInput input{.source = source, .path = "emptystr.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for StringComparisonWithEmptyString:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    // Verify that we're calling Viper.Strings.Equals for both functions
    bool foundEqualsCall = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "checkEmpty" || fn.name == "checkNotEmpty")
        {
            for (const auto &block : fn.blocks)
            {
                for (const auto &instr : block.instructions)
                {
                    if (instr.op == il::core::Opcode::Call &&
                        instr.callee == "Viper.Strings.Equals")
                    {
                        foundEqualsCall = true;
                    }
                }
            }
        }
    }
    EXPECT_TRUE(foundEqualsCall);
}

/// @brief Bug #32: String constants should be dereferenced when used.
/// Global string constants should emit const_str instructions when accessed.
TEST(ViperLangExpressions, StringConstantsDereferenced)
{
    SourceManager sm;
    const std::string source = R"(
module Test;

final KEY_QUIT = "q";

func checkKey(String key) -> Boolean {
    return key == KEY_QUIT;
}

func start() {
    Boolean result = checkKey("q");
}
)";
    CompilerInput input{.source = source, .path = "strconst.viper"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);

    if (!result.succeeded())
    {
        std::cerr << "Diagnostics for StringConstantsDereferenced:\n";
        for (const auto &d : result.diagnostics.diagnostics())
        {
            std::cerr << "  [" << (d.severity == Severity::Error ? "ERROR" : "WARN") << "] "
                      << d.message << "\n";
        }
    }

    EXPECT_TRUE(result.succeeded());

    // Verify that const_str is used to load the constant
    bool foundConstStr = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "checkKey")
        {
            for (const auto &block : fn.blocks)
            {
                for (const auto &instr : block.instructions)
                {
                    if (instr.op == il::core::Opcode::ConstStr)
                    {
                        foundConstStr = true;
                    }
                }
            }
        }
    }
    EXPECT_TRUE(foundConstStr);
}

} // namespace

int main()
{
    return viper_test::run_all_tests();
}
