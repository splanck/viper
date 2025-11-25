//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_emit_aarch64_mir_minimal.cpp
// Purpose: Verify minimal AArch64 MIR emission: header, prologue, add, epilogue. 
// Key invariants: To be documented.
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
//
//===----------------------------------------------------------------------===//

#include "tests/unit/GTestStub.hpp"

#include <sstream>
#include <string>

#include "codegen/aarch64/AsmEmitter.hpp"
#include "codegen/aarch64/MachineIR.hpp"

using namespace viper::codegen::aarch64;

TEST(AArch64MIR, PrologueAddEpilogue)
{
    auto &ti = darwinTarget();
    AsmEmitter emit{ti};

    MFunction fn{};
    fn.name = "mir_add";
    fn.blocks.push_back(MBasicBlock{});
    auto &bb = fn.blocks.back();
    // x0 = x0 + x1
    bb.instrs.push_back(MInstr{MOpcode::AddRRR,
                               {MOperand::regOp(PhysReg::X0),
                                MOperand::regOp(PhysReg::X0),
                                MOperand::regOp(PhysReg::X1)}});
    // Return - triggers epilogue emission
    bb.instrs.push_back(MInstr{MOpcode::Ret, {}});

    std::ostringstream os;
    emit.emitFunction(os, fn);
    const std::string asmText = os.str();
    EXPECT_NE(asmText.find(".text"), std::string::npos);
    EXPECT_NE(asmText.find(".globl " + fn.name), std::string::npos);
    EXPECT_NE(asmText.find("stp x29, x30"), std::string::npos);
    EXPECT_NE(asmText.find("add x0, x0, x1"), std::string::npos);
    EXPECT_NE(asmText.find("ldp x29, x30"), std::string::npos);
    EXPECT_NE(asmText.find("ret\n"), std::string::npos);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}
