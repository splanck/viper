//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_emit_aarch64_mir_bitwise.cpp
// Purpose: Verify MIR bitwise rr emission for and/or/xor.
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

static std::string emit(const MInstr &mi)
{
    auto &ti = darwinTarget();
    AsmEmitter emit{ti};
    MFunction fn{};
    fn.name = "mir_bits";
    fn.blocks.push_back(MBasicBlock{});
    fn.blocks.back().instrs.push_back(mi);
    std::ostringstream os;
    emit.emitFunction(os, fn);
    return os.str();
}

TEST(AArch64MIR, BitwiseRR)
{
    {
        auto text = emit(MInstr{MOpcode::AndRRR,
                                {MOperand::regOp(PhysReg::X0),
                                 MOperand::regOp(PhysReg::X0),
                                 MOperand::regOp(PhysReg::X1)}});
        EXPECT_NE(text.find("and x0, x0, x1"), std::string::npos);
    }
    {
        auto text = emit(MInstr{MOpcode::OrrRRR,
                                {MOperand::regOp(PhysReg::X0),
                                 MOperand::regOp(PhysReg::X0),
                                 MOperand::regOp(PhysReg::X1)}});
        EXPECT_NE(text.find("orr x0, x0, x1"), std::string::npos);
    }
    {
        auto text = emit(MInstr{MOpcode::EorRRR,
                                {MOperand::regOp(PhysReg::X0),
                                 MOperand::regOp(PhysReg::X0),
                                 MOperand::regOp(PhysReg::X1)}});
        EXPECT_NE(text.find("eor x0, x0, x1"), std::string::npos);
    }
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, &argv);
    return RUN_ALL_TESTS();
}
