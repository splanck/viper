//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for Pascal bug fixes:
// - BUG-001/002: Array size calculation
// - BUG-004: Global variable access in procedures
//
//===----------------------------------------------------------------------===//

#include "frontends/pascal/Compiler.hpp"
#include "il/core/Opcode.hpp"
#include "support/source_manager.hpp"

#ifdef VIPER_HAS_GTEST
#include <gtest/gtest.h>
#else
#include "../../unit/GTestStub.hpp"
#endif

#include <string>

using namespace il::frontends::pascal;
using namespace il::support;
using namespace il::core;

namespace
{

/// @brief Test that local array allocation uses correct size (BUG-001/002 fix).
TEST(PascalBugfixTest, LocalArrayAllocaSizeIsCorrect)
{
    SourceManager sm;
    // Use a local array inside a procedure to test alloca size
    const std::string source = R"(
program ArrayTest;
procedure TestArraySize;
var
    Board: array[10] of Integer;
begin
    Board[0] := 1;
end;
begin
    TestArraySize;
end.
)";
    PascalCompilerInput input{.source = source, .path = "test.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    ASSERT_TRUE(result.succeeded());

    // Find the TestArraySize procedure and check for alloca 80 (10 * 8 bytes)
    bool foundCorrectAlloca = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "TestArraySize")
        {
            for (const auto &bb : fn.blocks)
            {
                for (const auto &instr : bb.instructions)
                {
                    if (instr.op == Opcode::Alloca && !instr.operands.empty())
                    {
                        if (instr.operands[0].kind == Value::Kind::ConstInt &&
                            instr.operands[0].i64 == 80)
                        {
                            foundCorrectAlloca = true;
                        }
                    }
                }
            }
        }
    }
    // Expected alloca 80 for array[10] of Integer (10 * 8 bytes)
    EXPECT_TRUE(foundCorrectAlloca);
}

/// @brief Test that global variables are accessed via runtime storage (BUG-004 fix).
TEST(PascalBugfixTest, GlobalVariablesUseRuntimeStorage)
{
    SourceManager sm;
    const std::string source = R"(
program GlobalTest;
var
    GlobalCounter: Integer;
procedure IncrementCounter;
begin
    GlobalCounter := GlobalCounter + 1;
end;
begin
    GlobalCounter := 0;
    IncrementCounter;
end.
)";
    PascalCompilerInput input{.source = source, .path = "test.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    ASSERT_TRUE(result.succeeded());

    // Check that IncrementCounter procedure calls rt_modvar_addr_i64
    bool procedureUsesModvar = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "IncrementCounter")
        {
            for (const auto &bb : fn.blocks)
            {
                for (const auto &instr : bb.instructions)
                {
                    if (instr.op == Opcode::Call && instr.callee == "rt_modvar_addr_i64")
                    {
                        procedureUsesModvar = true;
                    }
                }
            }
        }
    }
    // Procedure should use rt_modvar_addr_i64 for global variable access
    EXPECT_TRUE(procedureUsesModvar);

    // Check that main also uses rt_modvar_addr_i64 for GlobalCounter (not local alloca)
    bool mainUsesModvar = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &bb : fn.blocks)
            {
                for (const auto &instr : bb.instructions)
                {
                    if (instr.op == Opcode::Call && instr.callee == "rt_modvar_addr_i64")
                    {
                        mainUsesModvar = true;
                    }
                }
            }
        }
    }
    // Main should use rt_modvar_addr_i64 for global variable access
    EXPECT_TRUE(mainUsesModvar);
}

/// @brief Test that local variables in procedures don't conflict with globals.
TEST(PascalBugfixTest, LocalVariablesShadowGlobals)
{
    SourceManager sm;
    const std::string source = R"(
program ShadowTest;
var
    X: Integer;
procedure Test;
var
    X: Integer;
begin
    X := 10;
end;
begin
    X := 5;
    Test;
end.
)";
    PascalCompilerInput input{.source = source, .path = "test.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);
    ASSERT_TRUE(result.succeeded());

    // Test procedure should have alloca for local X (shadowing global)
    bool testHasLocalAlloca = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "Test")
        {
            for (const auto &bb : fn.blocks)
            {
                for (const auto &instr : bb.instructions)
                {
                    if (instr.op == Opcode::Alloca)
                    {
                        testHasLocalAlloca = true;
                    }
                }
            }
        }
    }
    // Test procedure should have alloca for local X
    EXPECT_TRUE(testHasLocalAlloca);
}

} // namespace

#ifndef VIPER_HAS_GTEST
int main()
{
    return RUN_ALL_TESTS();
}
#endif
