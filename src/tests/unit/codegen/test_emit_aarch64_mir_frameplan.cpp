// File: tests/unit/codegen/test_emit_aarch64_mir_frameplan.cpp
// Purpose: Verify AsmEmitter.emitFunction respects MFunction::savedGPRs for prologue/epilogue.

#include "tests/unit/GTestStub.hpp"

#include <sstream>
#include <string>

#include "codegen/aarch64/AsmEmitter.hpp"
#include "codegen/aarch64/MachineIR.hpp"

using namespace viper::codegen::aarch64;

TEST(AArch64MIR, FramePlanEmitFunction)
{
    auto &ti = darwinTarget();
    AsmEmitter emitter{ti};
    MFunction mf{};
    mf.name = "fpfn";
    // Save three callee-saved regs (odd count ensures both stp and str code paths appear)
    mf.savedGPRs = {PhysReg::X19, PhysReg::X20, PhysReg::X21};
    mf.blocks.emplace_back();
    mf.blocks.back().name = "entry";

    std::ostringstream os;
    emitter.emitFunction(os, mf);
    const std::string s = os.str();
    // Prologue header
    EXPECT_NE(s.find(".globl fpfn"), std::string::npos);
    EXPECT_NE(s.find("fpfn:"), std::string::npos);
    // Frame saves after FP/LR
    EXPECT_NE(s.find("stp x29, x30"), std::string::npos);
    EXPECT_NE(s.find("stp x19, x20"), std::string::npos);
    EXPECT_NE(s.find("str x21, [sp, #-16]!"), std::string::npos);
    // Epilogue restores before FP/LR
    EXPECT_NE(s.find("ldr x21, [sp], #16"), std::string::npos);
    EXPECT_NE(s.find("ldp x19, x20, [sp], #16"), std::string::npos);
    EXPECT_NE(s.find("ldp x29, x30, [sp], #16"), std::string::npos);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}
