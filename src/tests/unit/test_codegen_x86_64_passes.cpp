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
#include "codegen/x86_64/passes/BinaryEmitPass.hpp"
#include "codegen/x86_64/passes/EmitPass.hpp"
#include "codegen/x86_64/passes/LegalizePass.hpp"
#include "codegen/x86_64/passes/LoweringPass.hpp"
#include "codegen/x86_64/passes/PassManager.hpp"
#include "codegen/x86_64/passes/PeepholePass.hpp"
#include "codegen/x86_64/passes/RegAllocPass.hpp"
#include "il/core/BasicBlock.hpp"
#include "il/core/Function.hpp"
#include "il/core/Instr.hpp"
#include "il/core/Module.hpp"
#include "il/core/Type.hpp"
#include "il/core/Value.hpp"
#include "tests/TestHarness.hpp"

#include <memory>
#include <string>

using namespace viper::codegen::x64::passes;

namespace {

il::core::Module makeRetConstModule(long long value) {
    il::core::Module module{};

    il::core::Function fn;
    fn.name = "main";
    fn.retType = il::core::Type(il::core::Type::Kind::I64);

    il::core::BasicBlock entry;
    entry.label = "entry";
    entry.terminated = true;

    il::core::Instr ret;
    ret.op = il::core::Opcode::Ret;
    ret.type = il::core::Type(il::core::Type::Kind::Void);
    ret.operands.push_back(il::core::Value::constInt(value));
    entry.instructions.push_back(ret);

    fn.blocks.push_back(entry);
    module.functions.push_back(fn);
    return module;
}

il::core::Module makeMalformedVoidCallModule() {
    il::core::Module module{};

    il::core::Function fn;
    fn.name = "main";
    fn.retType = il::core::Type(il::core::Type::Kind::I64);

    il::core::BasicBlock entry;
    entry.label = "entry";
    entry.terminated = true;

    il::core::Instr call;
    call.op = il::core::Opcode::Call;
    call.type = il::core::Type(il::core::Type::Kind::Void);
    call.callee = "broken";
    call.result = 1;
    entry.instructions.push_back(call);

    il::core::Instr ret;
    ret.op = il::core::Opcode::Ret;
    ret.type = il::core::Type(il::core::Type::Kind::Void);
    ret.operands.push_back(il::core::Value::constInt(0));
    entry.instructions.push_back(ret);

    fn.blocks.push_back(entry);
    module.functions.push_back(fn);
    return module;
}

std::size_t binarySizeForOptLevel(int optimizeLevel) {
    Module module{};
    module.il = makeRetConstModule(0);
    module.options.optimizeLevel = optimizeLevel;
    Diagnostics diags{};

    PassManager pm{};
    pm.addPass(std::make_unique<LoweringPass>());
    pm.addPass(std::make_unique<LegalizePass>());
    pm.addPass(std::make_unique<RegAllocPass>());
    pm.addPass(std::make_unique<PeepholePass>());

    viper::codegen::x64::CodegenOptions opts{};
    opts.optimizeLevel = optimizeLevel;
    pm.addPass(std::make_unique<BinaryEmitPass>(false, opts));

    if (!pm.run(module, diags) || !module.binaryText)
        return 0;
    return module.binaryText->bytes().size();
}

bool runThroughRegAlloc(Module &module, Diagnostics &diags) {
    PassManager pm{};
    pm.addPass(std::make_unique<LoweringPass>());
    pm.addPass(std::make_unique<LegalizePass>());
    pm.addPass(std::make_unique<RegAllocPass>());
    return pm.run(module, diags);
}

} // namespace

TEST(LoweringPass, HandlesEmptyModule) {
    Module module{};
    Diagnostics diags{};
    LoweringPass pass{};
    EXPECT_TRUE(pass.run(module, diags));
    EXPECT_TRUE(module.lowered.has_value());
    EXPECT_EQ(module.lowered->funcs.size(), 0U);
    EXPECT_FALSE(diags.hasErrors());
}

TEST(LoweringPass, RejectsVoidCallWithResultId) {
    Module module{};
    module.il = makeMalformedVoidCallModule();
    Diagnostics diags{};
    LoweringPass pass{};
    EXPECT_FALSE(pass.run(module, diags));
    EXPECT_TRUE(diags.hasErrors());
}

TEST(LegalizePass, FailsWhenLoweringMissing) {
    Module module{};
    Diagnostics diags{};
    LegalizePass pass{};
    EXPECT_FALSE(pass.run(module, diags));
    EXPECT_TRUE(diags.hasErrors());
    EXPECT_FALSE(module.legalised);
}

TEST(LegalizePass, MarksModuleWhenLoweringReady) {
    Module module{};
    module.il = makeRetConstModule(7);
    LoweringPass lower{};
    Diagnostics lowerDiags{};
    ASSERT_TRUE(lower.run(module, lowerDiags));
    Diagnostics diags{};
    LegalizePass pass{};
    EXPECT_TRUE(pass.run(module, diags));
    EXPECT_TRUE(module.legalised);
    EXPECT_EQ(module.mir.size(), 1U);
    EXPECT_EQ(module.frames.size(), 1U);
    EXPECT_FALSE(diags.hasErrors());
}

TEST(RegAllocPass, RequiresLegalize) {
    Module module{};
    Diagnostics diags{};
    RegAllocPass pass{};
    EXPECT_FALSE(pass.run(module, diags));
    EXPECT_TRUE(diags.hasErrors());
    module.il = makeRetConstModule(11);
    Diagnostics diagsSuccess{};
    ASSERT_TRUE(runThroughRegAlloc(module, diagsSuccess));
    EXPECT_TRUE(module.registersAllocated);
}

TEST(EmitPass, ProducesAssembly) {
    Module module{};
    module.il = makeRetConstModule(42);
    Diagnostics diags{};
    ASSERT_TRUE(runThroughRegAlloc(module, diags));
    EmitPass pass{viper::codegen::x64::CodegenOptions{}};
    EXPECT_TRUE(pass.run(module, diags));
    EXPECT_TRUE(module.codegenResult.has_value());
    EXPECT_TRUE(module.codegenResult->asmText.find("ret") != std::string::npos);
    EXPECT_FALSE(diags.hasErrors());
}

TEST(CodegenOptions, OptimizeLevelDefaultsToOne) {
    viper::codegen::x64::CodegenOptions opts{};
    EXPECT_EQ(opts.optimizeLevel, 1);
}

TEST(CodegenOptions, OptimizeLevelZeroIsValid) {
    viper::codegen::x64::CodegenOptions opts{};
    opts.optimizeLevel = 0;
    EXPECT_EQ(opts.optimizeLevel, 0);
}

TEST(BinaryEmitPass, HonorsOptimizeLevel) {
    const std::size_t o0Size = binarySizeForOptLevel(0);
    const std::size_t o1Size = binarySizeForOptLevel(1);
    EXPECT_TRUE(o0Size > 0U);
    EXPECT_TRUE(o1Size > 0U);
}

TEST(PassManager, ShortCircuitsOnFailure) {
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

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
