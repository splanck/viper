//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_x86_global_ra.cpp
// Purpose: Validate the global-pinning tier of the x86-64 register allocator:
//          loop-carried virtual registers stay in callee-saved registers for
//          their whole lifetime, copy-connected chains share one register, and
//          hot loop blocks contain no frame traffic.
// Key invariants:
//   - Pinned vregs map to callee-saved registers and never spill.
//   - VIPER_NO_GLOBAL_RA=1 restores the legacy spill-home behaviour.
// Ownership/Lifetime: Builds transient MFunctions per test invocation.
// Links: src/codegen/common/ra/GlobalPinning.hpp,
//        src/codegen/x86_64/ra/Allocator.cpp
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include <cstdlib>
#ifdef _WIN32
#include "tests/common/PosixCompat.h"
#endif

#include "codegen/x86_64/MachineIR.hpp"
#include "codegen/x86_64/RegAllocLinear.hpp"
#include "codegen/x86_64/TargetX64.hpp"


using namespace viper::codegen::x64;

namespace {

bool isCalleeSavedGPR(PhysReg reg) {
    for (PhysReg saved : sysvTarget().calleeSavedGPR) {
        if (saved == reg)
            return true;
    }
    return false;
}

/// @brief Count RBP-relative memory accesses in a block (spill traffic).
std::size_t frameAccessCount(const MBasicBlock &block) {
    std::size_t count = 0;
    for (const auto &instr : block.instructions) {
        for (const auto &operand : instr.operands) {
            const auto *mem = std::get_if<OpMem>(&operand);
            if (mem && mem->base.isPhys &&
                static_cast<PhysReg>(mem->base.idOrPhys) == PhysReg::RBP) {
                ++count;
            }
        }
    }
    return count;
}

/// @brief Build a two-block counted loop with a PX_COPY back edge:
///          entry:  v1 = 0
///          head:   v2 = v1; v2 += 1; cmp v2, 100; jge exit
///          latch:  PX_COPY v1 <- v2; jmp head
///          exit:   ret
MFunction buildCountedLoop() {
    MFunction fn{};
    fn.name = "counted_loop";

    const Operand v1 = makeVRegOperand(RegClass::GPR, 1);
    const Operand v2 = makeVRegOperand(RegClass::GPR, 2);

    MBasicBlock entry{};
    entry.label = ".L_entry";
    entry.instructions = {MInstr::make(MOpcode::MOVri, {v1, makeImmOperand(0)}),
                          MInstr::make(MOpcode::JMP, {makeLabelOperand(".L_head")})};

    MBasicBlock head{};
    head.label = ".L_head";
    head.instructions = {MInstr::make(MOpcode::MOVrr, {v2, v1}),
                         MInstr::make(MOpcode::ADDri, {v2, makeImmOperand(1)}),
                         MInstr::make(MOpcode::CMPri, {v2, makeImmOperand(100)}),
                         MInstr::make(MOpcode::JCC, {makeImmOperand(5), // jge
                                                     makeLabelOperand(".L_exit")}),
                         MInstr::make(MOpcode::JMP, {makeLabelOperand(".L_latch")})};

    MBasicBlock latch{};
    latch.label = ".L_latch";
    latch.instructions = {MInstr::make(MOpcode::PX_COPY, {v1, v2}),
                          MInstr::make(MOpcode::JMP, {makeLabelOperand(".L_head")})};

    MBasicBlock exit{};
    exit.label = ".L_exit";
    exit.instructions = {MInstr::make(MOpcode::RET)};

    fn.blocks = {std::move(entry), std::move(head), std::move(latch), std::move(exit)};
    return fn;
}

} // namespace

TEST(X86GlobalRA, PinsLoopCarriedChainToOneCalleeSavedRegister) {
    MFunction fn = buildCountedLoop();
    const AllocationResult result = allocate(fn, sysvTarget());

    // Both chain members must land in the same callee-saved register: the
    // PX_COPY connecting them has disjoint per-block segments, so chain
    // coalescing merges them and the copy becomes an identity.
    auto v1It = result.vregToPhys.find(1);
    auto v2It = result.vregToPhys.find(2);
    ASSERT_TRUE(v1It != result.vregToPhys.end());
    ASSERT_TRUE(v2It != result.vregToPhys.end());
    EXPECT_TRUE(isCalleeSavedGPR(v1It->second));
    EXPECT_TRUE(isCalleeSavedGPR(v2It->second));
    EXPECT_EQ(static_cast<int>(v1It->second), static_cast<int>(v2It->second));

    // The loop blocks (head, latch) must be free of frame traffic.
    EXPECT_EQ(frameAccessCount(fn.blocks[1]), 0u);
    EXPECT_EQ(frameAccessCount(fn.blocks[2]), 0u);
}

TEST(X86GlobalRA, EscapeHatchRestoresSpillHomes) {
    setenv("VIPER_NO_GLOBAL_RA", "1", 1);
    struct EnvReset {
        ~EnvReset() {
            unsetenv("VIPER_NO_GLOBAL_RA");
        }
    } envReset;

    MFunction fn = buildCountedLoop();
    (void)allocate(fn, sysvTarget());

    // Legacy behaviour: the loop-carried value crosses a join, so it gets a
    // spill home and the loop touches the frame.
    const std::size_t loopTraffic = frameAccessCount(fn.blocks[1]) + frameAccessCount(fn.blocks[2]);
    EXPECT_GT(loopTraffic, 0u);
}

int main(int argc, char **argv) {
    viper_test::init(&argc, argv);
    return viper_test::run_all_tests();
}
