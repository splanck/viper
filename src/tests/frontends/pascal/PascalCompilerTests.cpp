//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Unit tests for the Pascal frontend skeleton.
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

namespace
{

/// @brief Test that the Pascal compiler skeleton produces a valid module.
TEST(PascalCompilerTest, SkeletonProducesModule)
{
    SourceManager sm;
    const std::string source = "program Hello; begin end.";
    PascalCompilerInput input{.source = source, .path = "test.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);

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

/// @brief Test that the lowerer produces a main function with an entry block.
TEST(PascalCompilerTest, LowererProducesEntryBlock)
{
    SourceManager sm;
    const std::string source = "program Hello; begin end.";
    PascalCompilerInput input{.source = source, .path = "test.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    // Check that the main function has at least one basic block
    bool foundMainWithBlocks = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main" && !fn.blocks.empty())
        {
            foundMainWithBlocks = true;
            // Check that the first block is the entry block
            EXPECT_EQ(fn.blocks.front().label, "entry_0");
            break;
        }
    }
    EXPECT_TRUE(foundMainWithBlocks);
}

/// @brief Test that diagnostics engine reports no errors for valid (ignored) input.
TEST(PascalCompilerTest, NoDiagnosticsForValidInput)
{
    SourceManager sm;
    const std::string source = "program Test; begin end.";
    PascalCompilerInput input{.source = source, .path = "test.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);

    EXPECT_EQ(result.diagnostics.errorCount(), 0u);
}

/// @brief Test that WriteLn emits runtime calls.
TEST(PascalCompilerTest, WriteLnEmitsRuntimeCalls)
{
    SourceManager sm;
    const std::string source = R"(
program Test;
begin
  WriteLn('Hello')
end.
)";
    PascalCompilerInput input{.source = source, .path = "test.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    // Check that the module has rt_print_str extern declaration
    bool hasExtern = false;
    for (const auto &ext : result.module.externs)
    {
        if (ext.name == "rt_print_str")
        {
            hasExtern = true;
            break;
        }
    }
    EXPECT_TRUE(hasExtern);
}

/// @brief Test that math builtins compile successfully.
TEST(PascalCompilerTest, MathBuiltinsCompile)
{
    SourceManager sm;
    const std::string source = R"(
program Test;
var x: Real;
begin
  x := Sqrt(16.0);
  x := Sin(0.5);
  x := Cos(0.5)
end.
)";
    PascalCompilerInput input{.source = source, .path = "test.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);

    EXPECT_TRUE(result.succeeded());
    EXPECT_EQ(result.diagnostics.errorCount(), 0u);
}

/// @brief Test that ordinal builtins (Pred, Succ) emit inline arithmetic.
TEST(PascalCompilerTest, OrdinalBuiltinsEmitArithmetic)
{
    SourceManager sm;
    const std::string source = R"(
program Test;
var n: Integer;
begin
  n := Pred(10);
  n := Succ(n)
end.
)";
    PascalCompilerInput input{.source = source, .path = "test.pas"};
    PascalCompilerOptions opts{};

    auto result = compilePascal(input, opts, sm);

    EXPECT_TRUE(result.succeeded());

    // Check that the main function has arithmetic instructions
    bool hasSubOrAdd = false;
    for (const auto &fn : result.module.functions)
    {
        if (fn.name == "main")
        {
            for (const auto &blk : fn.blocks)
            {
                for (const auto &instr : blk.instructions)
                {
                    if (instr.op == il::core::Opcode::Sub || instr.op == il::core::Opcode::Add)
                    {
                        hasSubOrAdd = true;
                        break;
                    }
                }
            }
        }
    }
    EXPECT_TRUE(hasSubOrAdd);
}

} // namespace

#ifndef VIPER_HAS_GTEST
int main()
{
    return RUN_ALL_TESTS();
}
#endif
