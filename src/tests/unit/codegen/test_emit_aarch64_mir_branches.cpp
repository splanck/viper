//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_emit_aarch64_mir_branches.cpp
// Purpose: Verify MIR emits basic block labels and branches (b, b.<cond>).
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

TEST(AArch64MIR, Branches)
{
    auto &ti = darwinTarget();
    AsmEmitter emit{ti};

    MFunction fn{};
    fn.name = "mir_br";
    fn.blocks.push_back(MBasicBlock{});
    fn.blocks.push_back(MBasicBlock{});
    fn.blocks[0].name = "entry";
    fn.blocks[1].name = "label1";

    // In entry: b.eq label1
    fn.blocks[0].instrs.push_back(
        MInstr{MOpcode::BCond, {MOperand::condOp("eq"), MOperand::labelOp("label1")}});
    // Unconditional branch back (to test 'b label')
    fn.blocks[1].instrs.push_back(MInstr{MOpcode::Br, {MOperand::labelOp("entry")}});

    std::ostringstream os;
    emit.emitFunction(os, fn);
    const std::string text = os.str();
    EXPECT_NE(text.find("entry:"), std::string::npos);
    EXPECT_NE(text.find("label1:"), std::string::npos);
    EXPECT_NE(text.find("b.eq label1"), std::string::npos);
    EXPECT_NE(text.find("b entry"), std::string::npos);
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
