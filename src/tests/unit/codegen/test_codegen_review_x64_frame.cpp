//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/codegen/test_codegen_review_x64_frame.cpp
// Purpose: Regression tests for x86_64 frame lowering bugs found during the
//          comprehensive backend codegen review.
//
//===----------------------------------------------------------------------===//
#include "codegen/x86_64/FrameLowering.hpp"
#include "codegen/x86_64/MachineIR.hpp"
#include "tests/TestHarness.hpp"

#include <algorithm>
#include <variant>

using namespace viper::codegen::x64;

// Helper to count instructions with a given opcode
static int countOpcode(const MFunction &func, MOpcode opc)
{
    int count = 0;
    for (const auto &block : func.blocks)
        for (const auto &instr : block.instructions)
            if (instr.opcode == opc)
                ++count;
    return count;
}

// Helper to count stack probe instructions: MOVmr loading from (%rsp) into RAX
// (the probe touch pattern). Excludes loads into RBP which are the standard
// frame setup `mov (%rsp), %rbp`.
static int countStackProbes(const MFunction &func)
{
    int count = 0;
    for (const auto &block : func.blocks)
    {
        for (const auto &instr : block.instructions)
        {
            if (instr.opcode == MOpcode::MOVmr && instr.operands.size() >= 2)
            {
                // Check if destination is RAX (probe target, not RBP frame restore)
                const auto *dst = std::get_if<OpReg>(&instr.operands[0]);
                if (!dst || !dst->isPhys || static_cast<PhysReg>(dst->idOrPhys) != PhysReg::RAX)
                    continue;

                // Check if source is (%rsp) with zero displacement
                if (const auto *mem = std::get_if<OpMem>(&instr.operands[1]))
                {
                    if (mem->base.isPhys &&
                        static_cast<PhysReg>(mem->base.idOrPhys) == PhysReg::RSP && mem->disp == 0)
                    {
                        ++count;
                    }
                }
            }
        }
    }
    return count;
}

// ---------------------------------------------------------------------------
// Fix: Large frame stack probing now emits actual probe code on Unix/macOS
// ---------------------------------------------------------------------------

TEST(X64FrameLowering, LargeFrameEmitsProbeLoop)
{
    MFunction func;
    func.name = "test_large_frame";
    MBasicBlock block;
    block.label = "entry";
    // Add a RET so insertPrologueEpilogue has something to attach epilogue to
    block.instructions.push_back(MInstr::make(MOpcode::RET, {}));
    func.blocks.push_back(std::move(block));

    FrameInfo frame;
    // Frame larger than one page (4096 bytes) to trigger probing
    frame.frameSize = 8192;

    const auto &target = sysvTarget();
    insertPrologueEpilogue(func, target, frame);

#if !defined(_WIN32)
    // On Unix/macOS, the large frame should emit at least one probe (MOVmr from (%rsp))
    int probeCount = countStackProbes(func);
    EXPECT_TRUE(probeCount >= 2); // 8192 bytes = 2 pages = at least 2 probes
#endif
}

TEST(X64FrameLowering, SmallFrameNoProbe)
{
    MFunction func;
    func.name = "test_small_frame";
    MBasicBlock block;
    block.label = "entry";
    block.instructions.push_back(MInstr::make(MOpcode::RET, {}));
    func.blocks.push_back(std::move(block));

    FrameInfo frame;
    // Frame smaller than a page — no probing needed
    frame.frameSize = 256;

    const auto &target = sysvTarget();
    insertPrologueEpilogue(func, target, frame);

    // Small frames should NOT emit stack probes
    EXPECT_EQ(countStackProbes(func), 0);
}

TEST(X64FrameLowering, ZeroFrameNoPrologue)
{
    MFunction func;
    func.name = "test_zero_frame";
    MBasicBlock block;
    block.label = "entry";
    block.instructions.push_back(MInstr::make(MOpcode::RET, {}));
    func.blocks.push_back(std::move(block));

    FrameInfo frame;
    frame.frameSize = 0;

    const auto &target = sysvTarget();
    insertPrologueEpilogue(func, target, frame);

    // Leaf functions with no frame, no calls, and no callee-saved registers
    // should skip the prologue entirely (leaf frame elimination).
    int movCount = countOpcode(func, MOpcode::MOVrr);
    EXPECT_EQ(movCount, 0); // No prologue for leaf functions
}

int main(int argc, char **argv)
{
    viper_test::init(&argc, &argv);
    return viper_test::run_all_tests();
}
