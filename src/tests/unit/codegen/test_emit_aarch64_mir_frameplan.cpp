//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_emit_aarch64_mir_frameplan.cpp
// Purpose: Verify AsmEmitter.emitFunction respects MFunction::savedGPRs for prologue/epilogue.
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//
#include "tests/TestHarness.hpp"
#include <sstream>
#include <string>

#include "codegen/aarch64/AsmEmitter.hpp"
#include "codegen/aarch64/MachineIR.hpp"

using namespace viper::codegen::aarch64;

/// @brief Returns the expected mangled symbol name on Darwin.
static std::string mangledSym(const std::string &name)
{
#if defined(__APPLE__)
    return "_" + name;
#else
    return name;
#endif
}

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
    // Return - triggers epilogue emission
    mf.blocks.back().instrs.push_back(MInstr{MOpcode::Ret, {}});

    std::ostringstream os;
    emitter.emitFunction(os, mf);
    const std::string s = os.str();
    // Prologue header
    EXPECT_NE(s.find(".globl " + mangledSym("fpfn")), std::string::npos);
    EXPECT_NE(s.find(mangledSym("fpfn") + ":"), std::string::npos);
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
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
