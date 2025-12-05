//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_codegen_x86_64_passes.cpp
// Purpose: Unit tests for the x86-64 codegen pass manager and individual passes.
// Key invariants: Passes respect prerequisite state and report diagnostics accordingly.
// Ownership/Lifetime: Tests construct Module and Diagnostics instances on the stack.
// Links: src/codegen/x86_64/passes
//
//===----------------------------------------------------------------------===//

#include "GTestStub.hpp"

#include "codegen/x86_64/passes/EmitPass.hpp"
#include "codegen/x86_64/passes/LegalizePass.hpp"
#include "codegen/x86_64/passes/LoweringPass.hpp"
#include "codegen/x86_64/passes/PassManager.hpp"
#include "codegen/x86_64/passes/RegAllocPass.hpp"

#include <memory>
#include <string>

using namespace viper::codegen::x64::passes;

TEST(LoweringPass, HandlesEmptyModule)
{
    Module module{};
    Diagnostics diags{};
    LoweringPass pass{};
    EXPECT_TRUE(pass.run(module, diags));
    EXPECT_TRUE(module.lowered.has_value());
    EXPECT_EQ(module.lowered->funcs.size(), 0U);
    EXPECT_FALSE(diags.hasErrors());
}

TEST(LegalizePass, FailsWhenLoweringMissing)
{
    Module module{};
    Diagnostics diags{};
    LegalizePass pass{};
    EXPECT_FALSE(pass.run(module, diags));
    EXPECT_TRUE(diags.hasErrors());
    EXPECT_FALSE(module.legalised);
}

TEST(LegalizePass, MarksModuleWhenLoweringReady)
{
    Module module{};
    module.lowered.emplace();
    Diagnostics diags{};
    LegalizePass pass{};
    EXPECT_TRUE(pass.run(module, diags));
    EXPECT_TRUE(module.legalised);
    EXPECT_FALSE(diags.hasErrors());
}

TEST(RegAllocPass, RequiresLegalize)
{
    Module module{};
    Diagnostics diags{};
    RegAllocPass pass{};
    EXPECT_FALSE(pass.run(module, diags));
    EXPECT_TRUE(diags.hasErrors());
    module.lowered.emplace();
    module.legalised = true;
    Diagnostics diagsSuccess{};
    EXPECT_TRUE(pass.run(module, diagsSuccess));
    EXPECT_TRUE(module.registersAllocated);
}

TEST(EmitPass, ProducesAssembly)
{
    Module module{};
    module.lowered.emplace();
    module.legalised = true;
    module.registersAllocated = true;
    Diagnostics diags{};
    EmitPass pass{viper::codegen::x64::CodegenOptions{}};
    EXPECT_TRUE(pass.run(module, diags));
    EXPECT_TRUE(module.codegenResult.has_value());
    EXPECT_FALSE(diags.hasErrors());
}

TEST(CodegenOptions, OptimizeLevelDefaultsToOne)
{
    viper::codegen::x64::CodegenOptions opts{};
    EXPECT_EQ(opts.optimizeLevel, 1);
}

TEST(CodegenOptions, OptimizeLevelZeroIsValid)
{
    viper::codegen::x64::CodegenOptions opts{};
    opts.optimizeLevel = 0;
    EXPECT_EQ(opts.optimizeLevel, 0);
}

TEST(PassManager, ShortCircuitsOnFailure)
{
    Module module{};
    Diagnostics diags{};
    PassManager pm{};
    pm.addPass(std::make_unique<LegalizePass>());
    pm.addPass(std::make_unique<RegAllocPass>());
    pm.addPass(std::make_unique<EmitPass>(viper::codegen::x64::CodegenOptions{}));
    EXPECT_FALSE(pm.run(module, diags));
    EXPECT_TRUE(diags.hasErrors());
    EXPECT_FALSE(module.registersAllocated);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
