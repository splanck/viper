//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/conformance/ZiaArithConformanceTests.cpp
// Purpose: Verify Zia frontend emits the correct IL opcodes for arithmetic
//          operations. Tests both checked (default, overflowChecks=true) and
//          unchecked modes, as well as mixed-type promotion.
//
// Reference: docs/arithmetic-semantics.md (Frontend Promotion Rules → Zia)
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

/// Check if any function in the module contains the given opcode.
bool hasOpcode(const il::core::Module &module, il::core::Opcode op)
{
    for (const auto &fn : module.functions)
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
    return false;
}

/// Check if any function named `funcName` contains the given opcode.
bool hasOpcodeInFunc(const il::core::Module &module, const std::string &funcName,
                     il::core::Opcode op)
{
    for (const auto &fn : module.functions)
    {
        if (fn.name != funcName)
            continue;
        for (const auto &block : fn.blocks)
        {
            for (const auto &instr : block.instructions)
            {
                if (instr.op == op)
                    return true;
            }
        }
    }
    return false;
}

} // namespace

//=============================================================================
// Checked mode (default, overflowChecks=true)
//=============================================================================

/// @brief Integer addition uses IAddOvf in checked mode.
TEST(ZiaArithConformance, IntAddChecked)
{
    SourceManager sm;
    const std::string source = R"(
module Test;
func start() {
    Integer a = 10 + 20;
    Viper.Terminal.SayInt(a);
}
)";
    CompilerInput input{.source = source, .path = "add_checked.zia"};
    CompilerOptions opts{};
    opts.overflowChecks = true;

    auto result = compile(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasOpcode(result.module, il::core::Opcode::IAddOvf));
}

/// @brief Integer division uses SDivChk0 in checked mode.
TEST(ZiaArithConformance, IntDivChecked)
{
    SourceManager sm;
    const std::string source = R"(
module Test;
func start() {
    Integer a = 20 / 4;
    Viper.Terminal.SayInt(a);
}
)";
    CompilerInput input{.source = source, .path = "div_checked.zia"};
    CompilerOptions opts{};
    opts.overflowChecks = true;

    auto result = compile(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasOpcode(result.module, il::core::Opcode::SDivChk0));
}

/// @brief Integer modulo uses SRemChk0 in checked mode.
TEST(ZiaArithConformance, IntModChecked)
{
    SourceManager sm;
    const std::string source = R"(
module Test;
func start() {
    Integer a = 17 % 5;
    Viper.Terminal.SayInt(a);
}
)";
    CompilerInput input{.source = source, .path = "mod_checked.zia"};
    CompilerOptions opts{};
    opts.overflowChecks = true;

    auto result = compile(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasOpcode(result.module, il::core::Opcode::SRemChk0));
}

//=============================================================================
// Unchecked mode (overflowChecks=false)
//=============================================================================

/// @brief Integer addition uses plain Add in unchecked mode.
TEST(ZiaArithConformance, IntAddUnchecked)
{
    SourceManager sm;
    const std::string source = R"(
module Test;
func start() {
    Integer a = 10 + 20;
    Viper.Terminal.SayInt(a);
}
)";
    CompilerInput input{.source = source, .path = "add_unchecked.zia"};
    CompilerOptions opts{};
    opts.overflowChecks = false;

    auto result = compile(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasOpcode(result.module, il::core::Opcode::Add));
    // Should NOT have IAddOvf (except potentially in for-loop increments)
}

/// @brief Integer division uses SDiv in unchecked mode.
TEST(ZiaArithConformance, IntDivUnchecked)
{
    SourceManager sm;
    const std::string source = R"(
module Test;
func start() {
    Integer a = 20 / 4;
    Viper.Terminal.SayInt(a);
}
)";
    CompilerInput input{.source = source, .path = "div_unchecked.zia"};
    CompilerOptions opts{};
    opts.overflowChecks = false;

    auto result = compile(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasOpcode(result.module, il::core::Opcode::SDiv));
}

/// @brief Integer modulo uses SRem in unchecked mode.
TEST(ZiaArithConformance, IntModUnchecked)
{
    SourceManager sm;
    const std::string source = R"(
module Test;
func start() {
    Integer a = 17 % 5;
    Viper.Terminal.SayInt(a);
}
)";
    CompilerInput input{.source = source, .path = "mod_unchecked.zia"};
    CompilerOptions opts{};
    opts.overflowChecks = false;

    auto result = compile(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasOpcode(result.module, il::core::Opcode::SRem));
}

//=============================================================================
// Mixed-type promotion
//=============================================================================

/// @brief Integer + Number promotes integer via Sitofp, then uses FAdd.
TEST(ZiaArithConformance, MixedIntPlusNumber)
{
    SourceManager sm;
    const std::string source = R"(
module Test;
func start() {
    Number x = 42 + 3.14;
    Viper.Terminal.SayNum(x);
}
)";
    CompilerInput input{.source = source, .path = "mixed.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasOpcode(result.module, il::core::Opcode::Sitofp));
    EXPECT_TRUE(hasOpcode(result.module, il::core::Opcode::FAdd));
}

/// @brief Float division uses FDiv.
TEST(ZiaArithConformance, FloatDiv)
{
    SourceManager sm;
    const std::string source = R"(
module Test;
func start() {
    Number x = 10.0 / 4.0;
    Viper.Terminal.SayNum(x);
}
)";
    CompilerInput input{.source = source, .path = "fdiv.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasOpcode(result.module, il::core::Opcode::FDiv));
}

/// @brief Float multiplication uses FMul.
TEST(ZiaArithConformance, FloatMul)
{
    SourceManager sm;
    const std::string source = R"(
module Test;
func start() {
    Number x = 2.5 * 4.0;
    Viper.Terminal.SayNum(x);
}
)";
    CompilerInput input{.source = source, .path = "fmul.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasOpcode(result.module, il::core::Opcode::FMul));
}

/// @brief Number return from Integer function is allowed (special narrowing).
TEST(ZiaArithConformance, NumberReturnFromIntFunc)
{
    SourceManager sm;
    const std::string source = R"(
module Test;
bind Math = Viper.Math;
func f() -> Integer {
    return Math.Floor(3.14);
}
func start() {
    Integer x = f();
    Viper.Terminal.SayInt(x);
}
)";
    CompilerInput input{.source = source, .path = "narrow_return.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
}

//=============================================================================
// Comparison operators
//=============================================================================

/// @brief Integer comparison uses SCmpLT.
TEST(ZiaArithConformance, IntCompare)
{
    SourceManager sm;
    const std::string source = R"(
module Test;
func start() {
    Boolean b = 3 < 5;
    if b { Viper.Terminal.Say("yes"); }
}
)";
    CompilerInput input{.source = source, .path = "cmp.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasOpcode(result.module, il::core::Opcode::SCmpLT));
}

/// @brief Float comparison uses FCmpLT.
TEST(ZiaArithConformance, FloatCompare)
{
    SourceManager sm;
    const std::string source = R"(
module Test;
func start() {
    Boolean b = 3.0 < 5.0;
    if b { Viper.Terminal.Say("yes"); }
}
)";
    CompilerInput input{.source = source, .path = "fcmp.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasOpcode(result.module, il::core::Opcode::FCmpLT));
}

/// @brief Bitwise AND on integers emits IL And opcode.
TEST(ZiaArithConformance, BitwiseAnd)
{
    SourceManager sm;
    const std::string source = R"(
module Test;
func start() {
    Integer x = 0xFF & 0x0F;
    Viper.Terminal.SayInt(x);
}
)";
    CompilerInput input{.source = source, .path = "bitand.zia"};
    CompilerOptions opts{};

    auto result = compile(input, opts, sm);
    EXPECT_TRUE(result.succeeded());
    EXPECT_TRUE(hasOpcode(result.module, il::core::Opcode::And));
}

//=============================================================================
// Entry point
//=============================================================================

int main(int argc, char **argv)
{
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
